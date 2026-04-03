#ifndef CONFIG_H
#define CONFIG_H

#include "sdkconfig.h"

// ============================================================================
// GPIO Pin Configuration
// ============================================================================
// Adjust these pins based on your hardware
#define GPIO_LED        2   // LED pin (change as needed)
#define GPIO_BUTTON     0   // Button pin (change as needed)

// ============================================================================
// FreeRTOS Configuration
// ============================================================================
// Get these values from sdkconfig.defaults
// CONFIG_APP_STACK_SIZE = Task stack size (bytes)
// CONFIG_APP_PRIORITY   = Task priority (0-24, higher = more important)
// CONFIG_APP_CORE       = Core affinity (0, 1, or tskNO_AFFINITY)

// ============================================================================
// NVS Configuration
// ============================================================================
#define NVS_NAMESPACE "delongi-tank"
#define NVS_STORE_NAME "config"

// ============================================================================
// Application Defaults
// ============================================================================
#define APP_VERSION "0.1.0"
#define APP_LOGLEVEL CONFIG_APP_LOGLEVEL  // From sdkconfig

// ============================================================================
// Security Configuration (optional)
// ============================================================================
// TLS: Configure your certificates here
// #define USE_TLS_CERTIFICATE 1
// #define TLS_CERT_FILE "certificates/ca-cert.pem"

// ============================================================================
// Logging Configuration
// ============================================================================
// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#endif // CONFIG_H
