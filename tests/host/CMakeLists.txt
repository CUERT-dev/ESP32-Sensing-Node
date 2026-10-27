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

target_compile_options(gateway_integration_test PRIVATE
    -Wall -Wextra -Werror -Wpedantic -UNDEBUG)

set(BLUEPILL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../Bluepill-Actuator-Node"
    CACHE PATH "Optional sibling Bluepill repository for actual codec interoperability tests")
if(EXISTS "${BLUEPILL_SOURCE_DIR}/lib/can/src/gp_can_codec.c")
    add_library(bluepill_codec OBJECT "${BLUEPILL_SOURCE_DIR}/lib/can/src/gp_can_codec.c")
    target_include_directories(bluepill_codec PRIVATE "${BLUEPILL_SOURCE_DIR}/Core/Inc")
    target_compile_definitions(bluepill_codec PRIVATE
        GP_DecodeCan=Bluepill_DecodeCan GP_EncodeCan=Bluepill_EncodeCan)
    add_executable(can_interop_test can_interop_test.c
        ../../lib/gateway/src/gateway.c ../../lib/gp_protocol/src/gp_protocol.c
        $<TARGET_OBJECTS:bluepill_codec>)
    target_include_directories(can_interop_test PRIVATE ../../Contracts
        ../../lib/gateway/include ../../lib/gp_protocol/include)
    target_compile_options(can_interop_test PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
    add_test(NAME esp_to_actual_bluepill_codec COMMAND can_interop_test)
endif()
