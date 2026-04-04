---
project_name: delongi-tank
author: user
version: 0.1.0
description: |
  Automatisiertes Wassertank-Managementsystem für Kaffeemaschinen.
  ESP32-basierte Füllstandsüberwachung mit ToF-Sensor, automatischer Ventilsteuerung,
  WiFi-Webserver-UI und NVS-basierter Konfigurationsspeicherung.
  Sichere Fallback-Konfiguration bei WiFi-Verbindungsfehlern.

target_board: esp32  # ESP32-DEVKITC-V4 (38-pin)
target_version: ESP-IDF 6.0.0

components:
  # ESP-IDF built-in components
  - name: freertos
  - name: esp_psram
  - name: nvs_flash
  - name: esp_wifi
  - name: esp_http_server
  - name: esp_tls
  - name: driver (uart, i2c, gpio)
  
external_dependencies:
  - VL53L0X (ToF sensor driver - IIC)
  - MOSFET control circuit (GPIO-based PWM/digital)

security:
  nvs_encryption: true       # WiFi credentials verschlüsselt speichern
  secure_boot: false         # Später optional
  flash_encryption: false    # Später optional
  watchdog: true             # Hardware-Watchdog für Task-Überwachung

hardware:
  board: ESP32-DEVKITC-V4 (38-pin, dual-core, 520KB SRAM)
  power: 5V USB + 12V externe Versorgung
  
  pin_mapping:
    i2c_sda: 21              # VL53L0X Sensor
    i2c_scl: 22              # VL53L0X Sensor
    valve_control: 16        # MOSFET-Modul (12V Magnetventil)
    status_led: 2            # Onboard LED
  
  external_peripherals:
    - VL53L0X Time-of-Flight Sensor (I2C address 0x29)
    - 12V Solenoid Valve (via MOSFET module)
    - 5V Power Supply Module

features:
  - Füllstandsüberwachung via ToF-Sensor (Abstandsmessung von oben)
  - Automatische Ventilsteuerung (Befüllung/Entleerung)
  - WiFi-Konnektivität mit Fallback AP-Modus
  - Web-UI (iPhone15-optimiert, pure HTML/CSS/JS)
  - NVS-Speicherung (Credentials, Grenzwerte, Konfiguration)
  - Hardware-Watchdog für Task-Sicherheit
  - Timeout-Schutz beim Befüllen (Notaus)
  - Brownout-Schutz (sofort Ventil deaktivieren)

notes: |
  - Alle Grenzwerte sind im runtime in der UI einstellbar und persistent
  - Keep it simple: Keine komplexen HTML-Techniken, mobile-first für iPhone
  - Task-Architektur: Sensor-Task + Ventil-Task (oder kombiniert mit Priority)
  - Webserver läuft im AP-Modus, wenn WiFi-Verbindung fehlschlägt
  - Sensor-Messwertlogik: je kleiner Abstand = Tank voller
  - OBEN-Grenzwert: Tank VOLL (kleiner Abstand)
  - UNTEN-Grenzwert: Tank LEER (großer Abstand)
