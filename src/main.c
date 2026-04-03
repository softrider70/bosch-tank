#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config.h"

static const char *TAG = "delongi-tank";

// NVS handle for persistent storage
nvs_handle_t nvs_handle;

/**
 * @brief Initialize NVS (Non-Volatile Storage)
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ret = nvs_open("delongi-tank", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/**
 * @brief FreeRTOS task - Main application logic
 */
static void app_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Application task started");
    
    // Initialize configuration
    // config_init();  // Uncomment when config.h is implemented
    
    // Main application loop
    for (int i = 0; ; i++) {
        ESP_LOGI(TAG, "Task running [%d]", i);
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5 second delay
        
        // TODO: Add your application logic here
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Template Application Started");
    ESP_LOGI(TAG, "Project: delongi-tank");
    
    // Initialize NVS
    if (init_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed, halting");
        return;
    }
    
    // Print system info
    ESP_LOGI(TAG, "Chip revision: %d", esp_chip_revision());
    ESP_LOGI(TAG, "Free heap: %u bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimum free heap (ever): %u bytes", esp_get_minimum_free_heap_size());
    
    // Create main application task
    BaseType_t ret = xTaskCreate(
        app_task,              // Task function
        "delongi-tank_app", // Task name
        CONFIG_APP_STACK_SIZE, // Stack size (from sdkconfig)
        NULL,                  // Task parameter
        CONFIG_APP_PRIORITY,   // Task priority
        NULL                   // Task handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create application task");
    }
}
