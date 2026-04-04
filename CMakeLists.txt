cmake_minimum_required(VERSION 3.16)

# ============================================================================
# Pre-build: Increment build number and generate version.h
# ============================================================================
execute_process(
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/increment_build.py
    RESULT_VARIABLE BUILD_INCREMENT_RESULT
    OUTPUT_VARIABLE BUILD_INCREMENT_OUTPUT
)

if(NOT BUILD_INCREMENT_RESULT EQUAL 0)
    message(WARNING "Failed to increment build number: ${BUILD_INCREMENT_OUTPUT}")
endif()

message(STATUS "${BUILD_INCREMENT_OUTPUT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(delongi-tank)
