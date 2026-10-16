#include "gateway.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_err.h"

#include <stdio.h>

/* UART configuration */
#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_16
#define UART_BAUD_RATE  115200

/* TWAI / CAN configuration */
#define CAN_TX_PIN      GPIO_NUM_21
#define CAN_RX_PIN      GPIO_NUM_22
#define CAN_BITRATE     500000

#define MAX_QUEUE_MS    100

/// Global instances of Gateway and UartRx
static Gateway gateway;
static UartRx uart_rx;

// Initialize UART with the specified configuration
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

// Initialize TWAI (CAN) with the specified configuration
static twai_node_handle_t can_node = NULL;

static esp_err_t CAN_Init(void)
{
    twai_onchip_node_config_t cfg = {
        .io_cfg = {
            .tx = CAN_TX_PIN,
            .rx = CAN_RX_PIN,
            .quanta_clk_out = -1,
            .bus_off_indicator = -1,
        },
        .bit_timing.bitrate = CAN_BITRATE,   // your 500000 define
        .tx_queue_depth = 10,
    };

    esp_err_t err = twai_new_node_onchip(&cfg, &can_node);
    if (err != ESP_OK) return err;

    return twai_node_enable(can_node);
}

static const char *TAG = "sensing_node";

void app_main(void)
{
    ESP_LOGI(TAG, "CUERT ESP32 sensing node started");

    uint32_t heartbeat = 0;
    while (1) {
        ESP_LOGI(TAG, "Heartbeat: %" PRIu32, heartbeat++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
