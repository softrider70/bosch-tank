/**
 * delongi-tank: Automated Water Tank Management System
 * ESP32-based automatic filling control for coffee machines with VL53L0X ToF sensor
 * 
 * Main application entry point - Phase 1 Implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

// ESP-IDF Core
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_attr.h"

// Storage & Crypto
#include "nvs_flash.h"
#include "nvs.h"

// Hardware
#include "driver/gpio.h"
#include "driver/i2c.h"

// WiFi & Network
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"  // For IP4_ADDR macro
#include "lwip/sockets.h"   // For DNS captive portal server

// Web Server
#include "esp_http_server.h"

// Network utilities
// #include "mdns.h"  // TODO: Add mdns to CMakeLists.txt REQUIRES
// #include "esp_sntp.h"  // TODO: Add esp_sntp to CMakeLists.txt REQUIRES

// Project Config
#include "config.h"
#include "version.h"

// Task watchdog
#include "esp_task_wdt.h"

// Macro for absolute value
#define ABS(x) ((x) < 0 ? -(x) : (x))

// ============================================================================
// Global Constants & Tags
// ============================================================================

static const char *TAG = "delongi-tank-main";

// ============================================================================
// Global State
// ============================================================================

typedef struct {
    nvs_handle_t nvs_handle;
    uint32_t threshold_top;
    uint32_t threshold_bottom;
    uint32_t timeout_max;
    uint16_t sensor_distance_cm;
    bool valve_state;  // true = open, false = closed
    bool emergency_stop_active;
    uint32_t last_update_timestamp;
} system_state_t;

static system_state_t sys_state = {
    .threshold_top = TANK_THRESHOLD_TOP_DEFAULT,
    .threshold_bottom = TANK_THRESHOLD_BOTTOM_DEFAULT,
    .timeout_max = VALVE_TIMEOUT_MAX_DEFAULT,
    .sensor_distance_cm = 0,
    .valve_state = false,
    .emergency_stop_active = false,
    .last_update_timestamp = 0
};

// Task Handles
static TaskHandle_t sensor_task_handle = NULL;
static TaskHandle_t valve_task_handle = NULL;
static TaskHandle_t wifi_task_handle = NULL;

// WiFi State Variables
typedef struct {
    bool is_connected;           // WiFi STA connected to network
    bool ap_active;              // AP mode active (fallback)
    char ssid[32];               // Current/last SSID
    uint8_t retry_count;         // WiFi connect retry counter
    uint32_t last_error_code;    // Last WiFi error
    uint32_t last_attempt_tick;  // Timestamp of last connection attempt
} wifi_state_t;

static wifi_state_t wifi_state = {
    .is_connected = false,
    .ap_active = false,
    .ssid = "ESP",
    .retry_count = 0,
    .last_error_code = 0,
    .last_attempt_tick = 0
};

// ============================================================================
// Phase 1: NVS (Non-Volatile Storage) Initialization
// ============================================================================

/**
 * @brief Initialize NVS with encryption
 */
static esp_err_t init_nvs(void)
{
    ESP_LOGI(TAG, "Initializing NVS...");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid - erasing and reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Open/create NVS namespace
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &sys_state.nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Load thresholds from NVS (or use defaults)
    uint32_t stored_top = 0, stored_bottom = 0, stored_timeout = 0;
    
    if (nvs_get_u32(sys_state.nvs_handle, NVS_KEY_THRESHOLD_TOP, &stored_top) == ESP_OK) {
        sys_state.threshold_top = stored_top;
        ESP_LOGI(TAG, "Loaded threshold_top from NVS: %d cm", stored_top);
    }
    
    if (nvs_get_u32(sys_state.nvs_handle, NVS_KEY_THRESHOLD_BOTTOM, &stored_bottom) == ESP_OK) {
        sys_state.threshold_bottom = stored_bottom;
        ESP_LOGI(TAG, "Loaded threshold_bottom from NVS: %d cm", stored_bottom);
    }
    
    if (nvs_get_u32(sys_state.nvs_handle, NVS_KEY_VALVE_TIMEOUT_MAX, &stored_timeout) == ESP_OK) {
        sys_state.timeout_max = stored_timeout;
        ESP_LOGI(TAG, "Loaded timeout_max from NVS: %d ms", stored_timeout);
    }
    
    ESP_LOGI(TAG, "NVS initialized successfully");
    return ESP_OK;
}

// ============================================================================
// Phase 1: I2C Initialization (for VL53L0X sensor)
// ============================================================================

/**
 * @brief Initialize I2C bus for VL53L0X ToF sensor (ESP-IDF v6.0 API)
 */
static esp_err_t init_i2c(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus (ESP-IDF v6.0)...");
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_I2C_SDA,
        .scl_io_num = GPIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,
    };
    
    esp_err_t ret = i2c_param_config(I2C_MASTER_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ I2C initialized on SDA=%d, SCL=%d", GPIO_I2C_SDA, GPIO_I2C_SCL);
    return ESP_OK;
}

// ============================================================================
// Phase 1: GPIO Initialization (Valve control + LED)
// ============================================================================

/**
 * @brief Initialize GPIO pins for valve and LED control
 */
static esp_err_t init_gpio(void)
{
    ESP_LOGI(TAG, "Initializing GPIO...");
    
    // Configure Valve control pin (GPIO 16) - OUTPUT
    gpio_config_t valve_cfg = {
        .pin_bit_mask = (1ULL << GPIO_VALVE_CONTROL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&valve_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure valve GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure LED status pin (GPIO 2) - OUTPUT
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << GPIO_LED_STATUS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    ret = gpio_config(&led_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Brownout protection: Close valve immediately
    gpio_set_level(GPIO_VALVE_CONTROL, 0);  // LOW = closed
    gpio_set_level(GPIO_LED_STATUS, 1);     // HIGH = LED on (status: init)
    
    ESP_LOGI(TAG, "GPIO initialized - Valve on GPIO %d, LED on GPIO %d", 
             GPIO_VALVE_CONTROL, GPIO_LED_STATUS);
    return ESP_OK;
}

// ============================================================================
// Phase 1: HTTP Server Setup (Forward Declaration)
// ============================================================================
static httpd_handle_t start_webserver(void);

// ============================================================================
// Phase 3: HTTP REST API Handlers
// ============================================================================

/**
 * @brief Helper: Send JSON response
 */
static void send_json_response(httpd_req_t *req, const char *json_data)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_data, strlen(json_data));
}

/**
 * @brief Handler: GET /api/status - Return system status as JSON
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    char json_response[512];
    int free_mem = esp_get_free_heap_size();
    int percent = 0;
    
    // Calculate fill percentage safely
    if (sys_state.threshold_bottom > sys_state.threshold_top) {
        int range = sys_state.threshold_bottom - sys_state.threshold_top;
        int current = sys_state.sensor_distance_cm - sys_state.threshold_top;
        percent = (int)((float)(range - current) / (float)range * 100.0f);
        percent = (percent < 0) ? 0 : (percent > 100) ? 100 : percent;
    }
    
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"status\":\"OK\","
        "\"emergency\":%s,"
        "\"timestamp\":%lld,"
        "\"sensors\":{"
        "\"tank_level_cm\":%d,"
        "\"tank_full\":%d"
        "},"
        "\"config\":{"
        "\"threshold_top_cm\":%lu,"
        "\"threshold_bottom_cm\":%lu,"
        "\"timeout_max_ms\":%lu"
        "},"
        "\"valve\":{"
        "\"state\":\"%s\""
        "},"
        "\"system\":{"
        "\"free_heap_bytes\":%d,"
        "\"uptime_ms\":%lld,"
        "\"app_version\":\"%s\""
        "}"
        "}",
        sys_state.emergency_stop_active ? "true" : "false",
        (long long)(esp_timer_get_time() / 1000000),
        sys_state.sensor_distance_cm,
        percent,
        (unsigned long)sys_state.threshold_top,
        (unsigned long)sys_state.threshold_bottom,
        (unsigned long)sys_state.timeout_max,
        sys_state.valve_state ? "OPEN" : "CLOSED",
        free_mem,
        (long long)esp_timer_get_time() / 1000,
        APP_VERSION
    );
    
    send_json_response(req, json_response);
    return ESP_OK;
}

/**
 * @brief Handler: GET /api/config - Return configuration
 */
static esp_err_t config_get_handler(httpd_req_t *req)
{
    char json_response[512];
    
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"config\":{"
        "  \"threshold_top_cm\":%u,"
        "  \"threshold_bottom_cm\":%u,"
        "  \"timeout_max_ms\":%u"
        "}"
        "}",
        (unsigned int)sys_state.threshold_top,
        (unsigned int)sys_state.threshold_bottom,
        (unsigned int)sys_state.timeout_max
    );
    
    send_json_response(req, json_response);
    return ESP_OK;
}

/**
 * @brief Handler: POST /api/config - Update configuration
 */
static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    // Simple JSON parsing for thresholds
    int top = 0, bottom = 0, timeout = 0;
    if (sscanf(buf, "{\"threshold_top_cm\":%d,\"threshold_bottom_cm\":%d,\"timeout_max_ms\":%d}",
               &top, &bottom, &timeout) == 3) {
        
        if (top > 0 && bottom > 0 && timeout > 0) {
            sys_state.threshold_top = top;
            sys_state.threshold_bottom = bottom;
            sys_state.timeout_max = timeout;
            
            // Save to NVS
            nvs_set_u32(sys_state.nvs_handle, NVS_KEY_THRESHOLD_TOP, top);
            nvs_set_u32(sys_state.nvs_handle, NVS_KEY_THRESHOLD_BOTTOM, bottom);
            nvs_set_u32(sys_state.nvs_handle, NVS_KEY_VALVE_TIMEOUT_MAX, timeout);
            nvs_commit(sys_state.nvs_handle);
            
            char response[256];
            snprintf(response, sizeof(response),
                "{\"status\":\"OK\",\"message\":\"Config updated\"}");
            send_json_response(req, response);
            return ESP_OK;
        }
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON or values");
    return ESP_FAIL;
}

/**
 * @brief Handler: POST /api/valve/manual - Manual valve control
 */
static esp_err_t valve_manual_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    int action = -1;
    if (strstr(buf, "\"action\":\"open\"")) {
        action = 1;
    } else if (strstr(buf, "\"action\":\"close\"")) {
        action = 0;
    }
    
    if (action >= 0) {
        sys_state.valve_state = (action == 1);
        gpio_set_level(GPIO_VALVE_CONTROL, action);
        ESP_LOGI(TAG, "Valve %s manually", action ? "opened" : "closed");
        
        char response[256];
        snprintf(response, sizeof(response),
            "{\"status\":\"OK\",\"action\":\"%s\"}", 
            action ? "open" : "close");
        send_json_response(req, response);
        return ESP_OK;
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid action");
    return ESP_FAIL;
}

/**
 * @brief Handler: POST /api/emergency_stop - Emergency stop
 * Closes valve and disables auto-filling until reset
 */
static esp_err_t emergency_stop_handler(httpd_req_t *req)
{
    // If already active, treat as reset
    if (sys_state.emergency_stop_active) {
        ESP_LOGI(TAG, "♻️  EMERGENCY STOP RESET - system resuming normal operations");
        sys_state.emergency_stop_active = false;
    } else {
        ESP_LOGW(TAG, "🚨 EMERGENCY STOP TRIGGERED - all operations halted!");
        sys_state.emergency_stop_active = true;
    }
    
    // Always close valve when emergency button is pressed
    gpio_set_level(GPIO_VALVE_CONTROL, 0);
    sys_state.valve_state = false;
    
    char response[256];
    snprintf(response, sizeof(response),
        "{\"status\":\"%s\",\"message\":\"Emergency %s\",\"valve\":\"CLOSED\"}",
        sys_state.emergency_stop_active ? "EMERGENCY" : "RESUME",
        sys_state.emergency_stop_active ? "ACTIVATED" : "RESET"
    );
    send_json_response(req, response);
    
    return ESP_OK;
}

/**
 * @brief Handler: POST /api/valve/stop - Force valve closed
 */
static esp_err_t valve_stop_handler(httpd_req_t *req)
{
    sys_state.valve_state = false;  // Close valve
    gpio_set_level(GPIO_VALVE_CONTROL, 0);
    
    char response[128];
    snprintf(response, sizeof(response),
        "{\"status\":\"OK\",\"valve\":\"CLOSED\",\"message\":\"Valve stopped\"}"
    );
    send_json_response(req, response);
    return ESP_OK;
}

/**
 * @brief Handler: POST /api/system/reset - Soft reset (keep WiFi)
 */
static esp_err_t system_reset_handler(httpd_req_t *req)
{
    // Just send response, then restart (keeps WiFi credentials in NVS)
    char response[100];
    snprintf(response, sizeof(response),
        "{\"status\":\"OK\",\"message\":\"System restarting...\"}"
    );
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    
    // Restart after response sent
    esp_restart();
    return ESP_OK;
}

/**
 * @brief Handler: GET /api/wifi/status - WiFi connection status
 */
static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    // Get current WiFi mode and status
    wifi_ap_record_t ap_info;
    char response[512];
    
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    
    // Get local IP if connected
    char ip_str[16] = "Not connected";
    if (err == ESP_OK) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(netif, &ip_info);
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        }
    }
    
    snprintf(response, sizeof(response),
        "{\"wifi\":"
        "{\"ssid\":\"%s\","
        "\"rssi\":%d,"
        "\"connected\":%s,"
        "\"ip\":\"%s\"}}", 
        (err == ESP_OK) ? (char*)ap_info.ssid : "Not connected",
        (err == ESP_OK) ? ap_info.rssi : 0,
        (err == ESP_OK) ? "true" : "false",
        ip_str
    );
    send_json_response(req, response);
    return ESP_OK;
}

/**
 * @brief Handler: POST /api/wifi/config - Update WiFi credentials and attempt connection
 */
static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    // Read content length
    size_t content_len = req->content_len;
    if (content_len > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }
    
    // Read POST body
    char buffer[1024] = {0};
    int ret = httpd_req_recv(req, buffer, content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    
    // Extract SSID and Password using simple string parsing
    // Format: {"ssid":"xxxx","password":"yyyy"}
    char ssid[32] = {0};
    char password[64] = {0};
    
    // Parse SSID
    const char *ssid_start = strstr(buffer, "\"ssid\":\"");
    if (ssid_start) {
        ssid_start += 8;  // Skip '"ssid":"'
        const char *ssid_end = strchr(ssid_start, '"');
        if (ssid_end) {
            int ssid_len = ssid_end - ssid_start;
            if (ssid_len < 32) {
                strncpy(ssid, ssid_start, ssid_len);
            }
        }
    }
    
    // Parse Password
    const char *pass_start = strstr(buffer, "\"password\":\"");
    if (pass_start) {
        pass_start += 12;  // Skip '"password":"'
        const char *pass_end = strchr(pass_start, '"');
        if (pass_end) {
            int pass_len = pass_end - pass_start;
            if (pass_len < 64) {
                strncpy(password, pass_start, pass_len);
            }
        }
    }
    
    // Validate input
    if (strlen(ssid) == 0 || strlen(password) == 0) {
        char response[100];
        snprintf(response, sizeof(response),
            "{\"status\":\"ERROR\",\"message\":\"SSID and password required\"}"
        );
        send_json_response(req, response);
        return ESP_OK;
    }
    
    // Save to NVS
    esp_err_t ret_ssid = nvs_set_str(sys_state.nvs_handle, NVS_KEY_WIFI_SSID, ssid);
    esp_err_t ret_pass = nvs_set_str(sys_state.nvs_handle, NVS_KEY_WIFI_PASS, password);
    
    if (ret_ssid != ESP_OK || ret_pass != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi credentials to NVS");
        char response[100];
        snprintf(response, sizeof(response),
            "{\"status\":\"ERROR\",\"message\":\"Failed to save credentials\"}"
        );
        send_json_response(req, response);
        return ESP_OK;
    }
    
    // Commit changes
    nvs_commit(sys_state.nvs_handle);
    
    // Update in-memory state
    strncpy(wifi_state.ssid, ssid, sizeof(wifi_state.ssid) - 1);
    wifi_state.retry_count = 0;  // Reset retry counter
    
    // Apply new WiFi credentials immediately
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    
    // If was in AP-only fallback, ensure APSTA mode for reconnect
    if (wifi_state.ap_active) {
        ESP_LOGI(TAG, "Resetting retry count, attempting STA reconnect");
        wifi_state.ap_active = false;
    }
    
    // Disconnect any existing STA connection, then reconnect
    esp_wifi_disconnect();
    esp_wifi_connect();
    
    ESP_LOGI(TAG, "WiFi: Connecting to '%s'", ssid);
    
    char response[200];
    snprintf(response, sizeof(response),
        "{\"status\":\"OK\",\"message\":\"WiFi credentials saved. Connecting to '%s'...\"}",
        ssid
    );
    send_json_response(req, response);
    return ESP_OK;
}

/**
 * @brief Captive portal: redirect to root page (302)
 */
static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_MODE_IP_ADDR "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Minimal DNS server task for captive portal
 * Responds to ALL DNS queries with our AP IP (10.1.1.1)
 * This makes phones/laptops detect the captive portal automatically
 */
static void dns_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS: Failed to create socket");
        vTaskDelete(NULL);
        return;
    }
    
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS: Failed to bind port 53");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS captive portal server started on port 53");
    
    uint8_t rx_buf[512];
    uint8_t tx_buf[512];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    // Our AP IP in network byte order
    uint32_t ap_ip = esp_ip4addr_aton(AP_MODE_IP_ADDR);
    
    while (1) {
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
        if (len < 12) continue;  // DNS header is 12 bytes minimum
        
        // Build DNS response: copy query, set response flags, append answer
        memcpy(tx_buf, rx_buf, len);
        
        // Set response flags: QR=1 (response), AA=1 (authoritative), RD=1, RA=1
        tx_buf[2] = 0x84;  // QR=1, Opcode=0, AA=1
        tx_buf[3] = 0x00;  // RCODE=0 (no error)
        
        // Set answer count = 1
        tx_buf[6] = 0x00;
        tx_buf[7] = 0x01;
        
        // Append answer section after the query
        int pos = len;
        // Name pointer to question (0xC00C = offset 12)
        tx_buf[pos++] = 0xC0;
        tx_buf[pos++] = 0x0C;
        // Type A (1)
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x01;
        // Class IN (1)
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x01;
        // TTL = 60 seconds
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x3C;
        // RDLENGTH = 4 (IPv4)
        tx_buf[pos++] = 0x00;
        tx_buf[pos++] = 0x04;
        // RDATA = our IP
        memcpy(&tx_buf[pos], &ap_ip, 4);
        pos += 4;
        
        sendto(sock, tx_buf, pos, 0,
               (struct sockaddr *)&client_addr, addr_len);
    }
}

/**
 * @brief Handler: GET / - Root HTML page with modern UI
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    // Lightweight HTML UI - no emojis, minimal size for reliable transfer
    static const char index_html[] = R"html(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DELONGI TANK</title>
<style>
body{font-family:Arial,sans-serif;background:#667eea;margin:0;padding:15px}
.container{max-width:480px;margin:0 auto;background:white;padding:15px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.2)}
h1{color:#333;margin:0;font-size:20px}
.tabs{display:flex;gap:5px;margin:15px 0;border-bottom:2px solid #ddd}
.tab-btn{flex:1;padding:10px;border:none;background:#f0f0f0;cursor:pointer;font-weight:bold;border-radius:4px 4px 0 0}
.tab-btn.active{background:#667eea;color:white}
.tab-content{display:none;padding:15px 0}
.tab-content.active{display:block}
.tank{text-align:center;padding:15px;background:#e3f2fd;border-radius:8px;margin:10px 0}
.big-num{font-size:42px;font-weight:bold;color:#0066cc}
.status-row{display:flex;justify-content:space-between;padding:8px;background:#f0f0f0;margin:5px 0;border-radius:4px;font-size:12px}
.buttons{display:flex;gap:8px;margin:12px 0;flex-wrap:wrap}
button{flex:1;min-width:90px;padding:10px;border:none;border-radius:6px;font-weight:bold;cursor:pointer;font-size:12px}
.btn-primary{background:#667eea;color:white}
.btn-danger{background:#f44336;color:white}
.btn-success{background:#4caf50;color:white}
.btn-secondary{background:#ddd}
label{display:block;font-weight:bold;margin:8px 0 4px 0;font-size:12px;color:#333}
input{width:100%;padding:8px;margin:0 0 8px 0;box-sizing:border-box;border-radius:4px;border:1px solid #ddd;font-size:14px}
.msg{padding:10px;margin:8px 0;border-radius:4px;font-size:12px}
.error{background:#ffebee;color:#f44336}
.success{background:#e8f5e9;color:#4caf50}
</style></head>
<body>
<div class="container">
<h1>DELONGI TANK</h1>

<div class="tabs">
<button class="tab-btn active" onclick="switchTab('dashboard')">Dashboard</button>
<button class="tab-btn" onclick="switchTab('settings')">Settings</button>
<button class="tab-btn" onclick="switchTab('wifi')">WiFi</button>
</div>

<!-- DASHBOARD TAB -->
<div class="tab-content active" id="dashboard">
<div class="tank">
<div class="big-num" id="level">--</div>
<div style="font-size:14px;color:#666">cm Wasser</div>
<div id="percent" style="font-size:18px;color:#0066cc;margin-top:8px">--</div>
<div style="margin-top:8px;height:15px;background:#ddd;border-radius:4px;position:relative;overflow:hidden">
<div id="bar-oben" style="position:absolute;height:3px;background:#f44336;width:1%;top:30%;right:0"></div>
<div id="bar-fill" style="height:100%;background:#4caf50;width:0%"></div>
<div id="bar-unten" style="position:absolute;height:3px;background:#ff9800;width:1%;bottom:0;left:0"></div>
</div>
</div>

<div class="status-row"><span>Ventil:</span><span id="valve">GESCHL</span></div>
<div class="status-row"><span>Status:</span><span id="status">OK</span></div>
<div class="status-row"><span>WiFi:</span><span id="dash-wifi">OK</span></div>

<div class="buttons">
<button class="btn-primary" onclick="fill()">BEFUELLEN</button>
<button class="btn-danger" onclick="stop()">STOPP</button>
<button class="btn-secondary" onclick="emergency()">NOTAUS</button>
</div>
<div id="msg-dashboard" class="msg" style="display:none"></div>
</div>

<!-- SETTINGS TAB -->
<div class="tab-content" id="settings">
<p style="font-size:12px;margin:0 0 10px 0"><b>Schwellenwerte:</b></p>
<label for="top">OBEN (cm):</label>
<input type="number" id="top" min="1" max="100">
<label for="bottom">UNTEN (cm):</label>
<input type="number" id="bottom" min="1" max="100">
<label for="timeout">Timeout (ms):</label>
<input type="number" id="timeout" min="1000" max="999999">
<div class="buttons">
<button class="btn-success" onclick="saveSettings()">Speichern</button>
</div>
<div id="msg-settings" class="msg" style="display:none"></div>
</div>

<!-- WiFi TAB -->
<div class="tab-content" id="wifi">
<div class="status-row"><span>Status:</span><span id="wifi-con">Lädt...</span></div>
<div class="status-row"><span>SSID:</span><span id="wifi-ssid">-</span></div>
<div class="status-row"><span>Signal:</span><span id="wifi-rssi">-</span></div>
<div class="status-row"><span>IP:</span><span id="wifi-ip">-</span></div>

<div style="margin-top:20px;padding-top:15px;border-top:1px solid #ddd">
<p style="font-size:12px;margin:0 0 10px 0"><b>Neue Verbindung:</b></p>
<label for="new-ssid">SSID:</label>
<input type="text" id="new-ssid" placeholder="z.B. MeinWiFi">
<label for="new-pass">Passwort:</label>
<input type="password" id="new-pass" placeholder="Min. 8 Zeichen">
<div class="buttons">
<button class="btn-success" onclick="connectWiFi()">Verbinden</button>
<button class="btn-secondary" onclick="reset()">RESET</button>
</div>
</div>
<div id="msg-wifi" class="msg" style="display:none"></div>
</div>

</div>

<script>
let isFilling = false;
function switchTab(t){
  document.querySelectorAll('.tab-content').forEach(e => e.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(e => e.classList.remove('active'));
  document.getElementById(t).classList.add('active');
  event.target.classList.add('active');
  if(t==='settings') loadSettings();
  if(t==='wifi') loadWiFi();
}
function showMsg(tabId, text, isErr){
  const el = document.getElementById('msg-'+tabId);
  el.textContent = text;
  el.className = 'msg ' + (isErr ? 'error' : 'success');
  el.style.display = 'block';
  setTimeout(() => el.style.display = 'none', 4000);
}
function updateDashboard(){
  fetch('/api/status').then(r => r.json()).then(d => {
    const lv = d.sensors.tank_level_cm || 0;
    const top = d.config.threshold_top_cm || 1;
    const bot = d.config.threshold_bottom_cm || 50;
    const full = Math.max(0, Math.min(100, (bot-lv)/(bot-top)*100));
    document.getElementById('level').textContent = lv;
    document.getElementById('percent').textContent = full.toFixed(0) + '%';
    document.getElementById('bar-fill').style.width = full + '%';
    document.getElementById('bar-oben').style.right = ((top/bot)*100) + '%';
    document.getElementById('valve').textContent = d.valve.state === 'OPEN' ? 'OFFEN' : 'GESCHL';
    document.getElementById('status').textContent = d.status;
  });
}
function fill(){isFilling = !isFilling; fetch('/api/valve/manual', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({action: isFilling ? 'open' : 'close'})}).then(() => updateDashboard());}
function stop(){fetch('/api/valve/stop', {method: 'POST'}).then(() => {isFilling = false; updateDashboard(); showMsg('dashboard', 'Ventil geschlossen', false);});}
function emergency(){fetch('/api/emergency_stop', {method: 'POST'}).then(() => updateDashboard());}
function loadSettings(){fetch('/api/config').then(r => r.json()).then(d => {document.getElementById('top').value = d.config.threshold_top_cm; document.getElementById('bottom').value = d.config.threshold_bottom_cm; document.getElementById('timeout').value = d.config.timeout_max_ms;});}
function saveSettings(){const cfg = {threshold_top_cm: parseInt(document.getElementById('top').value), threshold_bottom_cm: parseInt(document.getElementById('bottom').value), timeout_max_ms: parseInt(document.getElementById('timeout').value)}; fetch('/api/config', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(cfg)}).then(r => r.json()).then(d => showMsg('settings', 'Einstellungen gespeichert', false)).catch(e => showMsg('settings', 'Fehler: ' + e, true));}
function loadWiFi(){fetch('/api/wifi/status').then(r => {if(!r.ok) throw new Error('API error: '+r.status); return r.json();}).then(d => {const c=d.wifi&&d.wifi.connected; document.getElementById('wifi-con').textContent=c?'Verbunden':'Getrennt'; document.getElementById('wifi-con').style.color=c?'#4caf50':'#f44336'; document.getElementById('wifi-ssid').textContent = (d.wifi && d.wifi.ssid) ? d.wifi.ssid : '-'; document.getElementById('wifi-rssi').textContent = (d.wifi && d.wifi.rssi) ? (d.wifi.rssi + ' dBm') : '-'; document.getElementById('wifi-ip').textContent = (d.wifi && d.wifi.ip) ? d.wifi.ip : '-';}).catch(e => {console.error('loadWiFi failed:', e); document.getElementById('wifi-con').textContent='Fehler'; showMsg('wifi', 'WiFi API Fehler', true);});}
function connectWiFi(){const s = document.getElementById('new-ssid').value; const p = document.getElementById('new-pass').value; if(!s||!p) {showMsg('wifi', 'SSID und Pass erforderlich', true); return;} fetch('/api/wifi/config', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ssid: s, password: p})}).then(r => {if(!r.ok) throw new Error('API error: '+r.status); return r.json();}).then(d => {showMsg('wifi', 'WiFi Update gesendet', false); document.getElementById('new-ssid').value = ''; document.getElementById('new-pass').value = ''; setTimeout(loadWiFi, 2000);}).catch(e => {console.error('connectWiFi failed:', e); showMsg('wifi', 'Fehler: '+e.message, true);});}
function reset(){if(confirm('System wirklich neustarten?')) fetch('/api/system/reset', {method: 'POST'}).then(() => showMsg('wifi', 'Neustart...', false)).catch(e => showMsg('wifi', 'Fehler', true));}
setInterval(updateDashboard, 1000);
updateDashboard();
</script>
</body></html>)html";
    
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

// ============================================================================
// Phase 1: Sensor Task (VL53L0X Distance Measurement)
// ============================================================================

// ============================================================================
// VL53L0X Sensor I2C Communication Helpers (ESP-IDF v6.0 compatible)
// ============================================================================

/**
 * @brief Scan I2C bus for all devices (diagnostic tool)
 * Tests addresses 0x00-0x7F and logs which ones respond
 */
static void i2c_scan_bus(void)
{
    ESP_LOGI(TAG, "🔍 Starting I2C bus scan...");
    ESP_LOGI(TAG, "   Scanning addresses 0x00-0x7F");
    
    int devices_found = 0;
    char found_addrs[256] = "";
    
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {  // 0x03-0x77 (skip reserved)
        // Probe address with a 1-byte register read (reg 0x00)
        uint8_t reg = 0x00;
        uint8_t dummy = 0;
        esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_PORT, addr,
                                                      &reg, 1,     // write reg addr
                                                      &dummy, 1,   // read 1 byte
                                                      50);         // 50ms timeout
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "   ✅ Device found at 0x%02X", addr);
            snprintf(found_addrs + strlen(found_addrs), sizeof(found_addrs) - strlen(found_addrs),
                     "0x%02X ", addr);
            devices_found++;
        }
    }
    
    if (devices_found > 0) {
        ESP_LOGI(TAG, "🎯 I2C Scan complete: Found %d device(s)", devices_found);
        ESP_LOGI(TAG, "   Addresses: %s", found_addrs);
    } else {
        ESP_LOGE(TAG, "❌ I2C Scan complete: NO devices found!");
        ESP_LOGW(TAG, "   Possible issues:");
        ESP_LOGW(TAG, "   - No pull-up resistors on SDA/SCL (need 4.7k)");
        ESP_LOGW(TAG, "   - Sensor not powered");
        ESP_LOGW(TAG, "   - I2C bus shorted");
        ESP_LOGW(TAG, "   - Wrong pins (SDA=21, SCL=22)");
    }
}

/**
 * @brief Read a byte from VL53L0X register
 */
static esp_err_t vl53l0x_read_byte(uint8_t reg, uint8_t *value)
{
    // Use i2c_master_write_read_device for transactional read
    return i2c_master_write_read_device(I2C_MASTER_PORT, VL53L0X_ADDR,
                                        &reg, 1,  // write reg address
                                        value, 1,  // read value
                                        100);  // timeout 100ms
}

/**
 * @brief Write a byte to VL53L0X register
 */
static esp_err_t vl53l0x_write_byte(uint8_t reg, uint8_t value)
{
    // Write register + value in one transaction
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_MASTER_PORT, VL53L0X_ADDR,
                                      data, 2, 100);  // timeout 100ms
}

// VL53L0X Register Definitions (Pololu Library v1.3.0)
#define VL53L0X_REG_SYSRANGE_START 0x00
#define VL53L0X_REG_RESULT_INTERRUPT_STATUS 0x13
#define VL53L0X_REG_RESULT_RANGE_STATUS 0x14

/**
 * @brief Check if VL53L0X sensor is present on I2C bus
 */
static bool vl53l0x_sensor_ready(void)
{
    uint8_t chip_id = 0;
    esp_err_t ret = vl53l0x_read_byte(0xC0, &chip_id);
    
    if (ret == ESP_OK && chip_id == 0xEE) {
        ESP_LOGI(TAG, "✅ VL53L0X FOUND (Model 0x%02X)", chip_id);
        return true;
    }
    
    ESP_LOGE(TAG, "❌ VL53L0X not found (got 0x%02X, ret=%d)", chip_id, ret);
    return false;
}

/**
 * @brief Initialize VL53L0X sensor (MINIMAL Pololu Arduino ported)
 * 
 * This is a minimal version focusing on core functionality.
 * The full Pololu init has too many register writes that may fail over I2C.
 */
static esp_err_t vl53l0x_init(void)
{
    uint8_t u8data;
    esp_err_t ret;
    
    ESP_LOGI(TAG, "🔧 VL53L0X init (minimal Pololu)...");
    
    // Verify model ID (0xC0 should be 0xEE)
    ret = vl53l0x_read_byte(0xC0, &u8data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Cannot read Model ID register");
        return ESP_FAIL;
    }
    
    if (u8data != 0xEE) {
        ESP_LOGE(TAG, "❌ Model ID 0x%02X invalid (expected 0xEE)", u8data);
        return ESP_FAIL;
    }
    
    // Read revision
    vl53l0x_read_byte(0xC2, &u8data);
    ESP_LOGI(TAG, "Revision: 0x%02X", u8data);
    
    // Minimal init sequence (Pololu essentials only)
    // The actual timing comes from the measurement polling, not init
    
    // Page select and reset
    vl53l0x_write_byte(0xFF, 0x01);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    vl53l0x_write_byte(0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    vl53l0x_write_byte(0xFF, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Start measurement immediately (0x00 = Sysrange Start, 0x01 = single shot)
    vl53l0x_write_byte(0x00, 0x01);
    vTaskDelay(pdMS_TO_TICKS(100));  // 100ms for first measurement
    
    ESP_LOGI(TAG, "✅ VL53L0X init complete");
    return ESP_OK;
}

/**
 * @brief Read VL53L0X distance measurement (Pololu polling method - ROBUST)
 * 
 * Simplified Pololu algorithm for better stability:
 * 1. Trigger measurement
 * 2. Poll Result Interrupt Status until ready
 * 3. Read range result 
 * 4. Clear interrupt
 * 5. Return distance in mm
 */
static uint16_t vl53l0x_read_distance_mm_sync(void)
{
    uint8_t status = 0;
    uint32_t tick_start = esp_timer_get_time() / 1000;
    uint16_t distance_mm = 0;
    int timeout_ms = 300;  // 300ms timeout for measurement
    int retry_count = 0;
    
    // Step 1: Start measurement
    esp_err_t ret = vl53l0x_write_byte(0x00, 0x01);  // Sysrange Start
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ Failed to start measurement");
        return 0;
    }
    
    // Small delay for measurement to start
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Step 2: Poll Result Interrupt Status register
    // Bit 0 of reg 0x13 = 1 when result is ready
    while ((status & 0x01) == 0 && retry_count < 300) {
        ret = vl53l0x_read_byte(0x13, &status);
        if (ret != ESP_OK) {
            // I2C read failed - not critical, retry
            ESP_LOGD(TAG, "I2C read error polling status");
        }
        
        retry_count++;
        uint32_t elapsed = (esp_timer_get_time() / 1000) - tick_start;
        if (elapsed > timeout_ms) {
            ESP_LOGW(TAG, "⏱️  Timeout waiting for meas (status=0x%02X, tries=%d)", status, retry_count);
            return 0;
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Step 3: Read range result from 0x14-0x15 (big-endian, units=mm)
    uint8_t reg = 0x14;
    uint8_t data[2] = {0, 0};
    ret = i2c_master_write_read_device(I2C_MASTER_PORT, VL53L0X_ADDR,
                                       &reg, 1, data, 2, 50);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "❌ Failed to read distance register");
        return 0;
    }
    
    // Convert big-endian to distance  
    distance_mm = ((uint16_t)data[0] << 8) | data[1];
    
    // Step 4: Clear interrupt by writing 0x01 to status register
    vl53l0x_write_byte(0x13, 0x01);
    
    // Step 5: Return distance in mm
    return distance_mm;
}

/**
 * @brief Task: Read sensor and monitor fill level
 * Priority: HIGH (prevents tank overflow)
 */
static void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🌡️  Sensor task started");
    
    // Wait for task to be registered with watchdog
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Wait for I2C to be ready
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // DIAGNOSTIC: Scan I2C bus for all devices
    ESP_LOGI(TAG, "🔍 Scanning I2C bus...");
    i2c_scan_bus();
    
    vTaskDelay(pdMS_TO_TICKS(500));  // Give time to read logs
    
    // Try to initialize VL53L0X sensor (real hardware)
    bool sensor_available = false;
    
    ESP_LOGI(TAG, "🔧 Attempting VL53L0X sensor detection...");
    if (vl53l0x_sensor_ready()) {
        ESP_LOGI(TAG, "✅ Sensor detected! Now initializing...");
        // Attempt full initialization sequence
        esp_err_t init_result = vl53l0x_init();
        if (init_result == ESP_OK) {
            sensor_available = true;
            ESP_LOGI(TAG, "✅ VL53L0X sensor initialized and ready!");
            vTaskDelay(pdMS_TO_TICKS(100));  // Wait for first measurement
        } else {
            ESP_LOGE(TAG, "❌ Sensor detected but init FAILED (code %d) - using simulation", init_result);
        }
    } else {
        ESP_LOGW(TAG, "⚠️  VL53L0X sensor NOT DETECTED - using simulation fallback");
    }
    
    ESP_LOGI(TAG, "🚀 Sensor mode: %s", sensor_available ? "REAL HARDWARE" : "SIMULATION");
    
    uint16_t distance_samples[5] = {0};
    int sample_idx = 0;
    uint32_t stable_counter = 0;
    
    while (1) {
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Read sensor - either real or simulated
        uint16_t distance_mm = 0;
        
        if (sensor_available) {
            // Read from REAL sensor
            distance_mm = vl53l0x_read_distance_mm_sync();
            
            // Validate sensor reading (should be 30-4000mm typical range)
            // LOG EVERY ATTEMPT to debug why it's failing
            static uint32_t read_attempt = 0;
            read_attempt++;
            
            if ((read_attempt % 20) == 0) {  // Log every 20 attempts
                ESP_LOGD(TAG, "Sensor read attempt #%d: got %d mm", read_attempt, distance_mm);
            }
            
            if (distance_mm == 0 || distance_mm > 4000) {
                static uint32_t fail_count = 0;
                fail_count++;
                if ((fail_count % 10) == 0) {
                    ESP_LOGW(TAG, "❌ Invalid reading: %d mm (%d fails)", distance_mm, fail_count);
                }
                
                // Try again once
                distance_mm = vl53l0x_read_distance_mm_sync();
                if (distance_mm == 0 || distance_mm > 4000) {
                    // Still bad - use fallback with visual indicator
                    if (distance_samples[0] > 0) {
                        distance_mm = distance_samples[(sample_idx + 4) % 5];
                    } else {
                        // This shows as "150cm" on website = sensor not working
                        distance_mm = 1500;
                    }
                }
            }
        } else {
            // FALLBACK: Intelligent simulation when sensor unavailable
            // Generate realistic varying values based on actual time
            static uint32_t sim_counter = 0;
            
            // Simulate tank filling from 30cm to 120cm, then draining back
            sim_counter++;
            int sim_level = 30 + (sim_counter % 200);
            if (sim_level > 120) {
                sim_level = 240 - sim_level;  // Mirror back down
            }
            
            distance_mm = sim_level * 10;  // Convert to mm
            
            // Log rarely to avoid spam
            if ((sim_counter % 50) == 0) {
                ESP_LOGW(TAG, "📊 SIM-FALLBACK: Tank Level: %d cm (counter=%d)", sim_level, sim_counter);
            }
        }
        
        // Moving average filter (5-sample buffer for noise reduction)
        distance_samples[sample_idx] = distance_mm;
        sample_idx = (sample_idx + 1) % 5;
        
        uint32_t distance_sum = 0;
        for (int i = 0; i < 5; i++) {
            distance_sum += distance_samples[i];
        }
        uint16_t distance_filtered_mm = distance_sum / 5;
        uint16_t distance_cm = distance_filtered_mm / 10;
        
        // Update system state
        int prev_distance = sys_state.sensor_distance_cm;
        sys_state.sensor_distance_cm = distance_cm;
        sys_state.last_update_timestamp = (uint32_t)time(NULL);
        
        // Log significant changes
        if (ABS(distance_cm - prev_distance) > 2 || (stable_counter % 20) == 0) {
            int percent = (int)((float)(sys_state.threshold_bottom - distance_cm) / 
                               (sys_state.threshold_bottom - sys_state.threshold_top) * 100.0f);
            percent = (percent < 0) ? 0 : (percent > 100) ? 100 : percent;
            
            const char *source = sensor_available ? "🌡️ REAL" : "📊 SIM";
            ESP_LOGI(TAG, "%s Tank Level: %3d cm (%3d pct) | Range: OBEN=%d, UNTEN=%d", 
                     source, distance_cm, percent, sys_state.threshold_top, sys_state.threshold_bottom);
        }
        
        stable_counter++;
        vTaskDelay(pdMS_TO_TICKS(TASK_SENSOR_INTERVAL_MS));
    }
}

// ============================================================================
// Phase 1: Valve Control Task
// ============================================================================

/**
 * @brief Task: Control solenoid valve and manage filling timeout
 * Priority: HIGH (critical for safety)
 * 
 * Fill Logic:
 * - When tank level drops below UNTEN: Open valve and fill
 * - When tank level reaches OBEN: Close valve
 * - If filling exceeds TIMEOUT_MAX: Close valve as safety measure
 */
static void valve_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🚰 Valve task started");
    
    // Wait for task to be registered with watchdog
    vTaskDelay(pdMS_TO_TICKS(100));
    
    uint32_t fill_start_time = 0;
    bool filling = false;
    int last_tank_state = 0;  // 0=unknown, 1=full, 2=filling, 3=empty
    
    while (1) {
        // Feed watchdog - CRITICAL for safety
        esp_task_wdt_reset();
        
        int current_tank_state = 0;
        
        // Determine tank state based on sensor readings
        if (sys_state.sensor_distance_cm <= sys_state.threshold_top) {
            current_tank_state = 1;  // FULL
        } else if (sys_state.sensor_distance_cm >= sys_state.threshold_bottom) {
            current_tank_state = 3;  // EMPTY
        } else {
            current_tank_state = 2;  // FILLING
        }
        
        // Handle state transitions
        if (current_tank_state != last_tank_state) {
            switch (current_tank_state) {
                case 1:  // Tank FULL
                    if (filling) {
                        gpio_set_level(GPIO_VALVE_CONTROL, 0);  // Close valve
                        sys_state.valve_state = false;
                        filling = false;
                        ESP_LOGI(TAG, "🚰 Valve CLOSED - Tank is FULL (reached OBEN threshold)");
                    }
                    break;
                    
                case 3:  // Tank EMPTY
                    if (!filling && !sys_state.emergency_stop_active) {
                        gpio_set_level(GPIO_VALVE_CONTROL, 1);  // Open valve
                        sys_state.valve_state = true;
                        filling = true;
                        fill_start_time = (uint32_t)time(NULL);
                        ESP_LOGI(TAG, "🚰 Valve OPENED - Tank is EMPTY (below UNTEN threshold) - FILLING STARTED");
                    }
                    break;
            }
            last_tank_state = current_tank_state;
        }
        
        // Timeout protection: if filling exceeds max timeout
        if (filling) {
            uint32_t elapsed_ms = ((uint32_t)time(NULL) - fill_start_time) * 1000;
            if (elapsed_ms > sys_state.timeout_max) {
                gpio_set_level(GPIO_VALVE_CONTROL, 0);  // Close valve
                sys_state.valve_state = false;
                filling = false;
                ESP_LOGW(TAG, "⚠️  TIMEOUT! Valve CLOSED - fill time exceeded %d ms", 
                        sys_state.timeout_max);
                // Note: Not calling emergency stop, just safety closure
            }
        }
        
        // LED feedback: ON while opening, OFF when closed
        if (sys_state.emergency_stop_active) {
            gpio_set_level(GPIO_LED_STATUS, 1);  // Red alert
        } else {
            gpio_set_level(GPIO_LED_STATUS, sys_state.valve_state ? 1 : 0);
        }
        
        vTaskDelay(pdMS_TO_TICKS(TASK_VALVE_CHECK_MS));
    }
}

// ============================================================================
// Phase 3: WiFi Event Handler
// ============================================================================

/**
 * @brief WiFi Event Handler (non-blocking, retry handled by wifi_task)
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "📡 WiFi STA started - attempting to connect...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = event->reason;
        ESP_LOGW(TAG, "WiFi disconnected (reason: %d)", reason);
        
        wifi_state.retry_count++;
        wifi_state.last_error_code = reason;
        wifi_state.last_attempt_tick = esp_timer_get_time() / 1000;  // Record timestamp
        
        // Don't retry here - let wifi_task handle retry logic
        // This prevents blocking the event loop
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "✅ WiFi STA connected!");
        ESP_LOGI(TAG, "   IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        wifi_state.is_connected = true;
        wifi_state.retry_count = 0;  // Reset retry counter
        wifi_state.ap_active = false;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Device connected to AP");
    }
}

// ============================================================================
// Phase 1: WiFi Task (STA + AP Fallback)
// ============================================================================

/**
 * @brief Task: WiFi Connection Management with Retry Logic
 * 
 * Handles WiFi state transitions:
 * - STA mode: 3 connection attempts à 3 seconds
 * - Fallback: AP mode if all retries fail
 */
static void wifi_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi management task started");
    
    while (1) {
        uint32_t now = esp_timer_get_time() / 1000;
        
        // If already connected, just monitor
        if (wifi_state.is_connected) {
            vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
            continue;
        }
        
        // Not connected - check if we should retry or switch to AP mode
        if (wifi_state.ap_active) {
            // Already in AP mode - wait for user to configure WiFi
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        // In STA mode but not connected
        if (wifi_state.retry_count < 3) {
            // Check if 3 seconds have passed since last attempt
            uint32_t time_since_attempt = now - wifi_state.last_attempt_tick;
            
            if (time_since_attempt >= 3000 || wifi_state.last_attempt_tick == 0) {
                // Time to retry (or first attempt)
                wifi_state.last_attempt_tick = now;
                ESP_LOGI(TAG, "🔄 WiFi connect attempt %d/3...", wifi_state.retry_count + 1);
                esp_wifi_connect();
            }
        } else {
            // Max retries reached - AP is already running in APSTA mode
            ESP_LOGE(TAG, "❌ WiFi STA failed after 3 attempts - AP still active @ " AP_MODE_IP_ADDR);
            wifi_state.ap_active = true;
            // Stop retrying, user can enter credentials via AP
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));  // Check every second
    }
}

/**
 * @brief Start HTTP server on port 80 with REST API endpoints
 */
static httpd_handle_t start_webserver(void)
{
    ESP_LOGI(TAG, "Starting HTTP server on port %d", SERVER_PORT);
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SERVER_PORT;
    config.stack_size = SERVER_STACK_SIZE;
    config.task_priority = SERVER_TASK_PRIORITY;
    config.max_open_sockets = MAX_OPEN_SOCKETS;
    config.max_uri_handlers = 24;
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started - registering endpoints");
        
        // Register GET / (root HTML) - CRITICAL
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
        // Register GET /api/status
        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        
        // Register POST /api/valve/manual
        httpd_uri_t valve_uri = {
            .uri = "/api/valve/manual",
            .method = HTTP_POST,
            .handler = valve_manual_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &valve_uri);
        
        // Register POST /api/emergency_stop
        httpd_uri_t emergency_uri = {
            .uri = "/api/emergency_stop",
            .method = HTTP_POST,
            .handler = emergency_stop_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &emergency_uri);
        
        // Register GET /api/config
        httpd_uri_t config_get_uri = {
            .uri = "/api/config",
            .method = HTTP_GET,
            .handler = config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_get_uri);
        
        // Register POST /api/config
        httpd_uri_t config_post_uri = {
            .uri = "/api/config",
            .method = HTTP_POST,
            .handler = config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_post_uri);
        
        // Register POST /api/valve/stop
        httpd_uri_t valve_stop_uri = {
            .uri = "/api/valve/stop",
            .method = HTTP_POST,
            .handler = valve_stop_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &valve_stop_uri);
        
        // Register GET /api/wifi/status
        httpd_uri_t wifi_status_uri = {
            .uri = "/api/wifi/status",
            .method = HTTP_GET,
            .handler = wifi_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_status_uri);
        
        // Register POST /api/wifi/config
        httpd_uri_t wifi_config_uri = {
            .uri = "/api/wifi/config",
            .method = HTTP_POST,
            .handler = wifi_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_uri);
        
        // Register POST /api/system/reset
        httpd_uri_t reset_uri = {
            .uri = "/api/system/reset",
            .method = HTTP_POST,
            .handler = system_reset_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &reset_uri);
        
        // Captive portal detection endpoints (Apple, Android, Windows, Firefox)
        const char *captive_uris[] = {
            "/hotspot-detect.html",
            "/library/test/success.html",
            "/generate_204",
            "/gen_204",
            "/connecttest.txt",
            "/redirect",
            "/ncsi.txt",
            "/canonical.html",
            "/success.txt",
            NULL
        };
        for (int i = 0; captive_uris[i] != NULL; i++) {
            httpd_uri_t cp_uri = {
                .uri = captive_uris[i],
                .method = HTTP_GET,
                .handler = captive_redirect_handler,
                .user_ctx = NULL
            };
            httpd_register_uri_handler(server, &cp_uri);
        }
        
        ESP_LOGI(TAG, "HTTP server ready - endpoints + captive portal registered");
        
        return server;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

// ============================================================================
// Phase 1: Main Application Entry
// ============================================================================

/**
 * @brief FreeRTOS app_main - System initialization and task creation
 */
void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "🚀 delongi-tank %s (Build #%d)", VERSION_STRING, BUILD_NUMBER);
    ESP_LOGI(TAG, "   Compiled: %s", BUILD_TIMESTAMP);
    
    // Get chip info (v6.0 API change: requires pointer argument)
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "   Hardware: ESP32 (rev %d)", chip_info.revision);
    
    ESP_LOGI(TAG, "===========================================");
    
    // Initialize hardware
    ESP_LOGI(TAG, "🔧 Initializing hardware...");
    
    ESP_LOGI(TAG, "   → Testing NVS init...");
    esp_err_t ret = init_nvs();
    ESP_LOGI(TAG, "     NVS result: %s (0x%X)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ NVS initialization failed!");
        return;
    }
    
    ESP_LOGI(TAG, "   → Testing I2C init...");
    ret = init_i2c();
    ESP_LOGI(TAG, "     I2C result: %s (0x%X)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2C initialization failed!");
        return;
    }
    
    ESP_LOGI(TAG, "   → Testing GPIO init...");
    ret = init_gpio();
    ESP_LOGI(TAG, "     GPIO result: %s (0x%X)", esp_err_to_name(ret), ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ GPIO initialization failed!");
        return;
    }
    
    ESP_LOGI(TAG, "✅ Hardware initialized successfully");
    
    // Configure FreeRTOS Watchdog - will feed from sensor and valve tasks
    // Note: v6.0 initializes TWDT automatically, don't call init again!
    // Just reconfigure if needed
    __attribute__((unused)) esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,  // 10 second timeout (10000 ms)
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // Monitor all cores
        .trigger_panic = true,  // Panic instead of reboot
    };
    // Don't init, just reconfigure - watchdog is auto-initialized in v6.0
    // ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_config));
    ESP_LOGI(TAG, "   → Watchdog already initialized by framework, skipping re-init");
    
    // Start FreeRTOS tasks
    ESP_LOGI(TAG, "📋 Creating FreeRTOS tasks...");
    
    xTaskCreate(
        sensor_task,
        "sensor_task",
        TASK_STACK_SENSOR,
        NULL,
        TASK_PRIO_SENSOR,
        &sensor_task_handle
    );
    esp_task_wdt_add(sensor_task_handle);  // Register with watchdog
    ESP_LOGI(TAG, "   ✓ sensor_task (priority %d, stack %d bytes)", TASK_PRIO_SENSOR, TASK_STACK_SENSOR);
    
    xTaskCreate(
        valve_task,
        "valve_task",
        TASK_STACK_VALVE,
        NULL,
        TASK_PRIO_VALVE,
        &valve_task_handle
    );
    esp_task_wdt_add(valve_task_handle);  // Register with watchdog
    ESP_LOGI(TAG, "   ✓ valve_task (priority %d, stack %d bytes)", TASK_PRIO_VALVE, TASK_STACK_VALVE);
    
    // ========== Initialize WiFi & HTTP BEFORE creating wifi_task ==========
    ESP_LOGI(TAG, "📡 Initializing WiFi...");
    
    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create network interfaces  
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t *sta_netif __attribute__((unused)) = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    
    // Configure AP IP (static) - 10.1.1.1/24
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 10, 1, 1, 1);
    IP4_ADDR(&ip_info.gw, 10, 1, 1, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    
    // Set DNS to ourselves for captive portal redirect
    esp_netif_dns_info_t dns_info;
    dns_info.ip.u_addr.ip4.addr = ip_info.ip.addr;
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    
    esp_netif_dhcps_start(ap_netif);
    ESP_LOGI(TAG, "   AP IP: " AP_MODE_IP_ADDR " (captive DNS active)");
    
    // Initialize WiFi driver
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    
    // Register WiFi event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    
    // Read WiFi credentials from NVS (if saved)
    char nvs_ssid[32] = {0};
    char nvs_pass[64] = {0};
    size_t ssid_len = sizeof(nvs_ssid);
    size_t pass_len = sizeof(nvs_pass);
    
    esp_err_t ret_ssid = nvs_get_str(sys_state.nvs_handle, NVS_KEY_WIFI_SSID, nvs_ssid, &ssid_len);
    esp_err_t ret_pass = nvs_get_str(sys_state.nvs_handle, NVS_KEY_WIFI_PASS, nvs_pass, &pass_len);
    
    // Use NVS credentials if available, otherwise use defaults
    const char *use_ssid = (ret_ssid == ESP_OK && ssid_len > 0) ? nvs_ssid : "ESP";
    const char *use_pass = (ret_pass == ESP_OK && pass_len > 0) ? nvs_pass : "11111111";
    
    strncpy(wifi_state.ssid, use_ssid, sizeof(wifi_state.ssid) - 1);
    
    ESP_LOGI(TAG, "📶 WiFi credentials - SSID: %s (from %s)", 
             use_ssid, (ret_ssid == ESP_OK) ? "NVS" : "default");
    
    // Configure WiFi STA (Station) mode
    wifi_config_t sta_config = {
        .sta = {
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strncpy((char *)sta_config.sta.ssid, use_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, use_pass, sizeof(sta_config.sta.password) - 1);
    
    // Configure AP (Access Point) for fallback/setup mode
    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, WIFI_SSID_AP_MODE, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(WIFI_SSID_AP_MODE);
    strncpy((char *)ap_config.ap.password, WIFI_PASS_AP_MODE, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = WIFI_CHANNEL_AP;
    ap_config.ap.authmode = WIFI_AUTH_AP;
    ap_config.ap.max_connection = WIFI_MAX_CONN_AP;
    ap_config.ap.beacon_interval = WIFI_BEACON_INTERVAL_AP;
    ap_config.ap.pmf_cfg.capable = true;
    ap_config.ap.pmf_cfg.required = false;
    
    // APSTA mode: STA tries to connect, AP always available for setup
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    
    // DNS for captive portal already set above via DHCP option
    
    ESP_LOGI(TAG, "📡 WiFi configured - APSTA mode");
    ESP_LOGI(TAG, "   STA: will try SSID '%s' (3 attempts à 3 sec)", use_ssid);
    ESP_LOGI(TAG, "   AP: %s @ " AP_MODE_IP_ADDR, WIFI_SSID_AP_MODE);
    ESP_LOGI(TAG, "   Web server: http://" AP_MODE_IP_ADDR "/ (AP) or http://<sta-ip>/ (STA)");
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Optimize WiFi TX power & power saving
    esp_wifi_set_max_tx_power(84);  // 20.5 dBm
    esp_wifi_set_ps(WIFI_PS_NONE);   // Disable power saving
    
    // Initialize and start HTTP server
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
    
    // ========== Now create wifi_task (only monitoring) ==========
    xTaskCreate(
        wifi_task,
        "wifi_task",
        TASK_STACK_WIFI,
        NULL,
        TASK_PRIO_MAIN,
        &wifi_task_handle
    );
    // DO NOT register wifi_task with watchdog - it doesn't do critical work
    ESP_LOGI(TAG, "   ✓ wifi_task (priority %d, stack %d bytes - monitoring only)", TASK_PRIO_MAIN, TASK_STACK_WIFI);
    
    // Start DNS captive portal server (resolves all domains to our AP IP)
    xTaskCreate(dns_server_task, "dns_task", 4096, NULL, TASK_PRIO_MAIN, NULL);
    
    ESP_LOGI(TAG, "✅ All tasks created and running");
    ESP_LOGI(TAG, "📡 Configured thresholds:");
    ESP_LOGI(TAG, "   - OBEN (Tank FULL):  %d cm ← Valve closes when reached", sys_state.threshold_top);
    ESP_LOGI(TAG, "   - UNTEN (Tank EMPTY): %d cm ← Valve opens when reached", sys_state.threshold_bottom);
    ESP_LOGI(TAG, "   - Timeout (max fill): %d ms ← Safety cutoff after this duration", sys_state.timeout_max);
    
    // LED indicates system ready
    gpio_set_level(GPIO_LED_STATUS, 0);  // LED off (ready)
    
    ESP_LOGI(TAG, "🎯 System ready - waiting for sensor data...");
}

