cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "ESP-IDF is not active. Open an ESP-IDF terminal before building.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_sensing_node)
