---
project_name: delongi-tank
author: Your Name
version: 0.1.0
description: |
  Brief description of your ESP32 project.
  Include what hardware, features, and capabilities it has.

target_board: esp32  # Options: esp32, esp32s2, esp32s3, esp32c3, esp32c6
target_version: ${ESP_IDF_VERSION}

components:
  # List any external components you depend on
  # Example:
  # - name: json_parser
  #   version: "^1.0.0"

security:
  nvs_encryption: false      # Enable NVS encryption for sensitive data
  secure_boot: false         # Enable Secure Boot (requires keys)
  flash_encryption: false    # Enable flash encryption

hardware:
  description: "Describe your hardware here"
  external_peripherals:
    - "Example: 16x2 LCD display on I2C"
    - "Example: DHT22 temperature sensor on GPIO 4"

notes: |
  Add any additional notes, setup instructions, or troubleshooting tips here.
