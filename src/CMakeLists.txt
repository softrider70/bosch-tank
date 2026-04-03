idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "../include"
    REQUIRES esp_system nvs_flash freertos esp_log
)
