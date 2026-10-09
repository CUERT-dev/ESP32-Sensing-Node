#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
