#include "gateway.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_err.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stdlib.h>

/* UART configuration */
#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_16   /* IO16 -> Blackpill PA10 (USART1 RX) */
#define UART_RX_PIN     GPIO_NUM_17   /* IO17 <- Blackpill PA9  (USART1 TX) */
#define UART_BAUD_RATE  115200

/* TWAI / CAN configuration */
#define CAN_TX_PIN      GPIO_NUM_21
#define CAN_RX_PIN      GPIO_NUM_22
#define CAN_BITRATE     500000

#define MAX_QUEUE_MS    100
#define CAN_TX_QUEUE_LENGTH 10
#define UART_READ_BUFFER_SIZE 128

static Gateway gateway;
static UartRx uart_rx;
static QueueHandle_t can_tx_queue;
static twai_node_handle_t can_node;
static const char *TAG = "sensing_node";

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
        .tx_queue_depth = CAN_TX_QUEUE_LENGTH,
    };

    esp_err_t err = twai_new_node_onchip(&cfg, &can_node);
    if (err != ESP_OK) return err;

    return twai_node_enable(can_node);
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

    if (xQueueSend(can_tx_queue, frame, 0) != pdTRUE) {
        return GP_PENDING;
    }

    return GP_OK;
}

static void CanTxTask(void *argument)
{
    GpCanFrame queued_frame;
    (void)argument;

    for (;;) {
        if (xQueueReceive(can_tx_queue, &queued_frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        twai_frame_t tx_frame = {
            .header = {
                .id = queued_frame.id,
                .ide = false,
                .rtr = false,
                .fdf = false,
                .dlc = queued_frame.dlc,
            },
            .buffer = queued_frame.data,
            .buffer_len = queued_frame.dlc,
        };

        esp_err_t err = twai_node_transmit(can_node, &tx_frame, -1);
        if (err == ESP_OK) {
            /* Keep queued_frame alive until the asynchronous TX is complete. */
            err = twai_node_transmit_wait_all_done(can_node, pdMS_TO_TICKS(10));
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI transmit failed: %s", esp_err_to_name(err));
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

    can_tx_queue = xQueueCreate(CAN_TX_QUEUE_LENGTH, sizeof(GpCanFrame));
    if (can_tx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CAN transmit queue");
        abort();
    }

    ESP_ERROR_CHECK(UART_Init());
    ESP_ERROR_CHECK(CAN_Init());
    UartRx_Init(&uart_rx);

    if (Gateway_Init(&gateway, CanQueueWrite, NULL, MAX_QUEUE_MS) != GP_OK) {
        ESP_LOGE(TAG, "Failed to initialize gateway");
        abort();
    }

    if (xTaskCreate(CanTxTask, "can_tx", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CAN transmit task");
        abort();
    }

    ESP_LOGI(TAG, "CUERT ESP32 sensing node started");

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
            read_offset = 0u;
            chunk_received_at = xTaskGetTickCount();
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

        if (result == GP_OK) {
            result = Gateway_Forward(&gateway, &command, QueueAgeMs(chunk_received_at));
            if (result == GP_PENDING) {
                pending_command = command;
                pending_since = chunk_received_at;
                have_pending_command = true;
            } else if (result != GP_OK) {
                ESP_LOGW(TAG, "UART command rejected: %d", (int)result);
            }
        } else if (result != GP_PENDING && result != GP_INVALID) {
            ESP_LOGW(TAG, "UART processing failed: %d", (int)result);
        }
    }
}
