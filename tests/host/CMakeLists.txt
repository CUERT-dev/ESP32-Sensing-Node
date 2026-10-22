cmake_minimum_required(VERSION 3.16)
project(gp_host_tests C)

enable_testing()

add_executable(gateway_integration_test
    gateway_integration_test.c
    ../../lib/gateway/src/gateway.c
    ../../lib/gp_protocol/src/gp_protocol.c
)

target_include_directories(gateway_integration_test PRIVATE
    ../../Contracts
    ../../lib/gateway/include
    ../../lib/gp_protocol/include
)

add_test(NAME gateway_integration_test COMMAND gateway_integration_test)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES gateway gp_protocol
             esp_driver_uart esp_driver_twai esp_driver_gpio
             esp_timer)