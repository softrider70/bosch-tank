cmake_minimum_required(VERSION 3.5)

project(delongi-tank C)

set(ESP_PLATFORM esp32)
set(EXTRA_CMAKE_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Include FreeRTOS configuration
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SRCS
    src/main.c
)

# Project setup
project_start(delongi-tank)
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS include)
project_end()
