idf_component_register(
    SRCS
        "main.c"
        "lib/gateway/src/gateway.c"
        "lib/gp-protocol/src/gp-protocol.c"

    INCLUDE_DIRS
        "."
        "lib/gateway/include"
        "lib/gp-protocol/include"

    REQUIRES
        freertos
        log
        esp_driver_gpio
        esp_driver_uart
        esp_driver_twai
)