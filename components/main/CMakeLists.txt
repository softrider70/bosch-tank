idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "../../include"
    REQUIRES 
        nvs_flash 
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_touch_sens
        esp_wifi
        esp_http_server
        esp_https_ota
        app_update
        mbedtls
        esp_timer
        lwip
    WHOLE_ARCHIVE
)

set(VERSION_HEADER ${CMAKE_SOURCE_DIR}/include/version.h)
set(VERSION_TEMPLATE ${CMAKE_SOURCE_DIR}/include/version.h.in)

# Stempeldatei, die bei JEDEM Build angefasst wird. Sie ist die Abhaengigkeit,
# die version.h jedes Mal neu erzeugen laesst. Ohne sie zaehlt die Build-Nummer
# nur hoch, wenn config.h neuer ist als version.h (erlebt am 2026-09-27:
# dreimal hintereinander Build #87).
add_custom_target(build_stamp ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_SOURCE_DIR}/.build_stamp
    BYPRODUCTS ${CMAKE_SOURCE_DIR}/.build_stamp
    COMMENT "Build-Stempel setzen (erzwingt neue Build-Nummer)"
    VERBATIM
)

add_custom_command(
    OUTPUT ${VERSION_HEADER}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/increment_build.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS ${VERSION_TEMPLATE} ${CMAKE_SOURCE_DIR}/include/config.h ${CMAKE_SOURCE_DIR}/tools/increment_build.py ${CMAKE_SOURCE_DIR}/.build_stamp
    COMMENT "Generating version.h and incrementing build number"
    VERBATIM
)

add_custom_target(generate_version_header DEPENDS ${VERSION_HEADER})
add_dependencies(${COMPONENT_LIB} generate_version_header)
