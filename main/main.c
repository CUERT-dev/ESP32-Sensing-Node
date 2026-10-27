#include "gateway.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_err.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>

/*internal LED*/
#define LED_PIN GPIO_NUM_2


/* UART configuration */
#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17   /* ESP TX -> Blackpill PA10 (USART1 RX) */
#define UART_RX_PIN     GPIO_NUM_16   /* ESP RX <- Blackpill PA9  (USART1 TX) */
#define UART_BAUD_RATE  115200

/* TWAI / CAN configuration */
#define CAN_TX_PIN      GPIO_NUM_19
#define CAN_RX_PIN      GPIO_NUM_18
#define CAN_BITRATE     500000

#define MAX_QUEUE_MS    100
#define CAN_TX_QUEUE_LENGTH 10
#define UART_READ_BUFFER_SIZE 128

/* ---- DEBUG LOGGING: set DEBUG_LOG to 0 and every debug line is compiled out ---- */
#define DEBUG_LOG       1
#define DEBUG_EVERY_N   10u  /* keep serial printing below the monitor bandwidth */

static Gateway gateway;
static UartRx uart_rx;
static QueueHandle_t can_tx_queue;
static twai_node_handle_t can_node;
static const char *TAG = "sensing_node";
typedef struct {
    GpCanFrame frame;
    TickType_t queued_at;
} QueuedCanFrame;
static TaskHandle_t can_tx_task;
static _Atomic uint32_t uart_bytes_total, uart_frames_total, gateway_rejected;
static _Atomic uint32_t can_queue_busy;
static _Atomic uint32_t can_bus_errors_total, can_last_error;

#if DEBUG_LOG
static uint32_t dbg_rx_count;
static uint32_t dbg_cmd_count;
static uint32_t dbg_bad_count;

/* True for the 1st, (N+1)th, (2N+1)th ... call on this counter. */
#define DBG_DUE(counter) (((counter)++ % DEBUG_EVERY_N) == 0u)

/* Bytes -> "A5 5A 02 ..." text. Stops before the buffer is full. */
static void DbgHex(char *out, size_t cap, const uint8_t *bytes, size_t length)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t o = 0u;
    size_t i;

    for (i = 0u; i < length && (o + 4u) < cap; i++) {
        out[o++] = digits[bytes[i] >> 4];
        out[o++] = digits[bytes[i] & 0x0Fu];
        out[o++] = ' ';
    }
    out[o] = '\0';
}

static const char *DbgResult(GpResult result)
{
    if (result == GP_OK) {
        return "OK";
    }
    if (result == GP_PENDING) {
        return "PENDING";
    }
    return "REJECTED";
}
#endif

static esp_err_t UART_Init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    esp_err_t err = uart_driver_install(
        UART_PORT,
        1024,
        0,
        0,
        NULL,
        0
    );

    if (err != ESP_OK) {
        return err;
    }

    err = uart_param_config(UART_PORT, &uart_config);

    if (err != ESP_OK) {
        return err;
    }

    return uart_set_pin(
        UART_PORT,
        UART_TX_PIN,
        UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );
}

/* The queue becoming empty is not a transmission-success indication. Only
 * this callback reports ACKed success with normal (non-self-test) CAN mode. */
static bool CanTxDone(twai_node_handle_t node, const twai_tx_done_event_data_t *event,
                      void *user)
{
    (void)node; (void)user;
    BaseType_t woken = pdFALSE;
    if (can_tx_task != NULL) {
        xTaskNotifyFromISR(can_tx_task, event->is_tx_success ? 1U : 2U,
                          eSetValueWithOverwrite, &woken);
    }
    return woken == pdTRUE;
}

static bool CanError(twai_node_handle_t node, const twai_error_event_data_t *event,
                     void *user)
{
    (void)node; (void)user;
    atomic_store(&can_last_error, event->err_flags.val);
    atomic_fetch_add(&can_bus_errors_total, 1U);
    return false; /* no logging or blocking from the interrupt */
}

static esp_err_t CAN_Init(void)
{
    twai_onchip_node_config_t cfg = {
        .io_cfg = {
            .tx = CAN_TX_PIN,
            .rx = CAN_RX_PIN,
            .quanta_clk_out = -1,
            .bus_off_indicator = -1,
        },
        .bit_timing.bitrate = CAN_BITRATE,
        /* Single-shot failures return through on_tx_done. No old commands
         * retried forever when the Bluepill is absent. */
        .fail_retry_cnt = 0,
        .tx_queue_depth = 1,
    };

    esp_err_t err = twai_new_node_onchip(&cfg, &can_node);
    if (err != ESP_OK) return err;

    const twai_event_callbacks_t callbacks = { .on_tx_done = CanTxDone, .on_error = CanError };
    err = twai_node_register_event_callbacks(can_node, &callbacks, NULL);
    if (err == ESP_OK) { err = twai_node_enable(can_node); }
    if (err != ESP_OK) {
        /* A failed enable has not handed any frame to this node. */
        ESP_ERROR_CHECK(twai_node_delete(can_node));
        can_node = NULL;
    }
    return err;
}

/* The driver retains frame/data pointers. Delete it before reusing buffers
 * after a completion timeout; simply leaving a wait does not cancel a TX. */
static void CAN_Reset(void)
{
    if (can_node == NULL) { return; }
    twai_node_status_t state;
    ESP_ERROR_CHECK(twai_node_get_info(can_node, &state, NULL));
    if (state.state != TWAI_ERROR_BUS_OFF) {
        ESP_ERROR_CHECK(twai_node_disable(can_node));
    }
    ESP_ERROR_CHECK(twai_node_delete(can_node));
    can_node = NULL;
}

static GpResult CanQueueWrite(void *user, const GpCanFrame *frame)
{
    (void)user;

    if (frame == NULL || can_tx_queue == NULL) {
        return GP_BAD_ARG;
    }

    if (frame->id > 0x7FFu || frame->dlc != GP_CAN_DLC) {
        return GP_INVALID;
    }

    const QueuedCanFrame queued = { .frame = *frame, .queued_at = xTaskGetTickCount() };
    if (xQueueSend(can_tx_queue, &queued, 0) != pdTRUE) {
        atomic_fetch_add(&can_queue_busy, 1U);
        return GP_PENDING;
    }

    return GP_OK;
}

static void CanTxTask(void *argument)
{
    QueuedCanFrame queued;
    uint32_t tx_ok = 0, tx_fail = 0, timeouts = 0, stale_drop = 0, resets = 0;
    TickType_t last_report = xTaskGetTickCount();
    uint16_t last_sequence = 0;
    (void)argument;
    can_tx_task = xTaskGetCurrentTaskHandle();

    for (;;) {
        if (can_node == NULL) {
            esp_err_t init = CAN_Init();
            if (init != ESP_OK) {
                ESP_LOGE(TAG, "CAN INIT FAILED: %s (UART monitoring remains active)", esp_err_to_name(init));
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            ESP_LOGI(TAG, "CAN ONLINE: TX=GPIO%d RX=GPIO%d bitrate=%u ID=0x105 normal ACK-required mode",
                     CAN_TX_PIN, CAN_RX_PIN, CAN_BITRATE);
        }
        if (xQueueReceive(can_tx_queue, &queued, pdMS_TO_TICKS(100)) == pdTRUE) {
            if ((TickType_t)(xTaskGetTickCount() - queued.queued_at) > pdMS_TO_TICKS(MAX_QUEUE_MS)) {
                ++stale_drop;
            } else {
                twai_frame_t tx_frame = {
                    .header = { .id = queued.frame.id, .dlc = queued.frame.dlc },
                    .buffer = queued.frame.data, .buffer_len = queued.frame.dlc
                };
                uint32_t completion = 0;
                (void)xTaskNotifyWait(0, UINT32_MAX, &completion, 0);
                esp_err_t err = twai_node_transmit(can_node, &tx_frame, 0);
                if (err == ESP_OK) {
                    if (xTaskNotifyWait(0, UINT32_MAX, &completion, pdMS_TO_TICKS(100)) == pdTRUE) {
                        if (completion == 1U) {
                            ++tx_ok;
                            last_sequence = (uint16_t)(queued.frame.data[0] | ((uint16_t)queued.frame.data[1] << 8));
                            gpio_set_level(LED_PIN, 1);
                        } else {
                            ++tx_fail;
                            gpio_set_level(LED_PIN, 0);
                        }
                    } else {
                        ++timeouts;
                        ESP_LOGE(TAG, "CAN completion timeout: canceling pending frame before buffer reuse");
                        gpio_set_level(LED_PIN, 0);
                        /* Cancel all retained references before this block
                         * ends and tx_frame goes out of scope. */
                        CAN_Reset();
                        ++resets;
                    }
                } else {
                    ++tx_fail;
                    gpio_set_level(LED_PIN, 0);
                }
            }
        }
        if (can_node != NULL) {
            twai_node_status_t state;
            twai_node_record_t record;
            ESP_ERROR_CHECK(twai_node_get_info(can_node, &state, &record));
            TickType_t now = xTaskGetTickCount();
            if ((TickType_t)(now - last_report) >= pdMS_TO_TICKS(1000)) {
                ESP_LOGI(TAG, "CAN LINK tx_ok=%lu tx_fail=%lu timeout=%lu stale_drop=%lu resets=%lu last_seq=%u state=%d TEC=%u REC=%u bus_err=%lu errors_total=%lu last_error=0x%02lX UART_bytes=%lu frames=%lu gateway_reject=%lu queue_busy=%lu",
                    (unsigned long)tx_ok, (unsigned long)tx_fail, (unsigned long)timeouts,
                    (unsigned long)stale_drop, (unsigned long)resets, (unsigned)last_sequence,
                    (int)state.state, (unsigned)state.tx_error_count, (unsigned)state.rx_error_count,
                    (unsigned long)record.bus_err_num, (unsigned long)atomic_load(&can_bus_errors_total),
                    (unsigned long)atomic_load(&can_last_error), (unsigned long)atomic_load(&uart_bytes_total),
                    (unsigned long)atomic_load(&uart_frames_total), (unsigned long)atomic_load(&gateway_rejected),
                    (unsigned long)atomic_load(&can_queue_busy));
                last_report = now;
            }
            if (state.state == TWAI_ERROR_BUS_OFF) {
                ESP_LOGE(TAG, "CAN BUS OFF: check Bluepill CAN-enabled firmware, transceivers, wiring and termination");
                CAN_Reset();
                ++resets;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}

static uint32_t QueueAgeMs(TickType_t queued_at)
{
    TickType_t elapsed_ticks = xTaskGetTickCount() - queued_at;

    if (elapsed_ticks > pdMS_TO_TICKS(MAX_QUEUE_MS)) {
        return MAX_QUEUE_MS + 1u;
    }

    return (uint32_t)elapsed_ticks * portTICK_PERIOD_MS;
}

void app_main(void)
{
    uint8_t read_buffer[UART_READ_BUFFER_SIZE];
    size_t read_length = 0u;
    size_t read_offset = 0u;
    TargetSample pending_command;
    bool have_pending_command = false;
    TickType_t chunk_received_at = 0;
    TickType_t pending_since = 0;

    can_tx_queue = xQueueCreate(CAN_TX_QUEUE_LENGTH, sizeof(QueuedCanFrame));
    if (can_tx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CAN transmit queue");
        abort();
    }

    ESP_ERROR_CHECK(UART_Init());
    UartRx_Init(&uart_rx);

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    if (Gateway_Init(&gateway, CanQueueWrite, NULL, MAX_QUEUE_MS) != GP_OK) {
        ESP_LOGE(TAG, "Failed to initialize gateway");
        abort();
    }

    if (xTaskCreate(CanTxTask, "can_tx", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CAN transmit task");
        abort();
    }

    ESP_LOGI(TAG, "CUERT ESP32 sensing node started");
    ESP_LOGI(TAG, "FW can-link-v1: start with valid neutral joystick and ARM released; monitor CAN LINK + Bluepill RX");
#if DEBUG_LOG
    ESP_LOGI(TAG, "DEBUG logging ON: printing 1 of every %u messages", (unsigned)DEBUG_EVERY_N);
#endif

    while (1) {
        if (have_pending_command) {
            GpResult result = Gateway_Forward(
                &gateway,
                &pending_command,
                QueueAgeMs(pending_since)
            );

            if (result == GP_PENDING) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            if (result != GP_OK) {
                atomic_fetch_add(&gateway_rejected, 1U);
                ESP_LOGW(TAG, "Queued command rejected: %d", (int)result);
            }
            have_pending_command = false;
        }

        if (read_offset >= read_length) {
            int bytes_read = uart_read_bytes(
                UART_PORT,
                read_buffer,
                sizeof(read_buffer),
                pdMS_TO_TICKS(20)
            );

            if (bytes_read <= 0) {
                continue;
            }

            read_length = (size_t)bytes_read;
            atomic_fetch_add(&uart_bytes_total, (uint32_t)bytes_read);
            read_offset = 0u;
            chunk_received_at = xTaskGetTickCount();

#if DEBUG_LOG
            /* DEBUG 1: raw bytes received from the Blackpill. Must start with A5 5A */
            if (DBG_DUE(dbg_rx_count)) {
                char hex[96];
                size_t shown = (read_length < 28u) ? read_length : 28u;
                DbgHex(hex, sizeof(hex), read_buffer, shown);
                ESP_LOGI(TAG, "UART RX %u bytes: %s%s",
                         (unsigned)read_length, hex, (read_length > shown) ? "..." : "");
            }
#endif
        }

        size_t consumed = 0u;
        TargetSample command;
        GpResult result = UartRx_Feed(
            &uart_rx,
            &read_buffer[read_offset],
            read_length - read_offset,
            &consumed,
            &command
        );

        if (consumed == 0u) {
            ESP_LOGE(TAG, "UART parser made no progress");
            read_offset = read_length;
            continue;
        }
        read_offset += consumed;

#if DEBUG_LOG
        /* DEBUG 2a: frame was rejected by the parser (bad header, CRC or range) */
        if (result == GP_INVALID && DBG_DUE(dbg_bad_count)) {
            ESP_LOGW(TAG, "UART frame INVALID (bad header/CRC/range), consumed=%u",
                     (unsigned)consumed);
        }
#endif

        if (result == GP_OK) {
            atomic_fetch_add(&uart_frames_total, 1U);
            result = Gateway_Forward(&gateway, &command, QueueAgeMs(chunk_received_at));
            if (result == GP_PENDING) {
                pending_command = command;
                pending_since = chunk_received_at;
                have_pending_command = true;
            } else if (result != GP_OK) {
                uint32_t rejected = atomic_fetch_add(&gateway_rejected, 1U);
                if ((rejected % 50U) == 0U) {
                    ESP_LOGW(TAG, "UART command rejected: %d; initial command must be valid neutral with ARM released", (int)result);
                }
            }

#if DEBUG_LOG
            /* DEBUG 2b: decoded message values and what the gateway did with it */
            if (DBG_DUE(dbg_cmd_count)) {
                ESP_LOGI(TAG, "CMD seq=%u thr=%u str=%d flags=0x%02X (valid=%u arm=%u deadman=%u) gateway=%s(%d)",
                         (unsigned)command.sequence, (unsigned)command.throttle_pm,
                         (int)command.steering_pm, (unsigned)command.flags,
                         (unsigned)(command.flags & 1u),
                         (unsigned)((command.flags >> 1) & 1u),
                         (unsigned)((command.flags >> 2) & 1u),
                         DbgResult(result), (int)result);
            }
#endif
        } else if (result != GP_PENDING && result != GP_INVALID) {
            ESP_LOGW(TAG, "UART processing failed: %d", (int)result);
        }
    }
}
