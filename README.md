# delongi-tank — Automatisiertes Wassertank-Managementsystem

Intelligentes ESP32-Projekt zur Überwachung und Steuerung eines Kaffeemaschinen-Wassertanks mit ToF-Sensor, automatischer Ventilsteuerung und Web-Interface.

## Features (Anforderungen)

✅ **Hardware-Verwaltung**
- VL53L0X ToF-Sensor für Abstandsmessung (inverse Logik: kleiner Abstand = Tank voll)
- 12V Solenoid Valve via MOSFET-Steuerung (GPIO 16)
- Brownout-Schutz: Magnetventil wird sofort deaktiviert nach Neustart

✅ **Füllstandslogik**
- Task mit Priorität überwacht kontinuierlich Sensor
- OBEN-Wert erreicht → Tank voll → Ventil schließen
- UNTEN-Wert erreicht → Tank leer → Ventil öffnen + Timeout starten
- Timeout-Überschreitung → Notaus (Ventil zu, manueller Eingriff nötig)

✅ **Aktor-Steuerung**
- Separater Task mit Priorität für Ventilsteuerung
- Entscheidung: Sensor + Aktor ggf. kombiniert in einem Task bei unsicherer Trennung

✅ **WiFi & Fallback**
- Verbindung zu Basis-AP beim Start
- Bei Fehler: Lokaler AP-Modus mit Webserver starten
- Landing-Page zur WiFi-Credential-Eingabe

✅ **Sicherer Datenspeicher**
- NVS verschlüsselt (nvs_encryption=true)
- WiFi-Credentials persistent speichern
- Alle Grenzwerte (OBEN, UNTEN, Timeout) persistent
- Editierbar in der Web-UI

✅ **Web-UI (Mobile-First, iPhone15)**
- **Einfach und clean**: Kein Webpack, kein React, vanilla HTML/CSS/JS
- **Responsive Design**: Optimiert für iPhone15-Bildschirm
- Graphische Darstellung des Tanks (gefäss mit Füllstand + OBEN/UNTEN Markierungen)
- Messwert in cm anzeigen
- **Buttons:**
  - Notaus (Emergency Stop) beheben
  - Manuelles Befüllen
  - WiFi-Credentials ändern (zur Landing-Page)
  - Grenzwerte anpassen (OBEN, UNTEN, Timeout)
  - Status abfragen (Füllstand, Ventilzustand, Netzwerk)

✅ **Hardware-Watchdog**
- Überwacht wichtige Tasks auf regelmäßige Ausführung
- Triggert Notfall-Reset bei Hang

✅ **Keep It Simple**
- Fokus auf Funktionalität, nicht Komplexität
- Kurze Dokumentation, klares Code-Design

---

## Architektur-Plan (Vor-Implementierung)

### **Task-Struktur**

```
┌─────────────────────────────────────────┐
│         ESP32 FreeRTOS Kernel           │
├─────────────────────────────────────────┤
│                                         │
│  [1] SENSOR_TASK (Prio: HIGH)          │
│      → Liest VL53L0X alle 500ms        │
      → Prüft Grenzwerte (OBEN/UNTEN)   │
│      → Signalisiert an VALVE_TASK      │
│                                         │
│  [2] VALVE_TASK (Prio: HIGH)           │
│      → Steuert GPIO 16 (MOSFET)        │
│      → Verwaltet Timeout-Counter       │
│      → Schreibt Status ins NVS         │
│                                         │
│  [3] WEBSERVER_TASK (Prio: MEDIUM)    │
│      → esp_http_server                 │
│      → REST-Endpoints für UI           │
│      → NVS-Zugriff für Einstellungen   │
│                                         │
│  [4] WATCHDOG_TASK (Prio: MEDIUM)     │
│      → Überwacht Herzschlag von [1],[2]│
│      → HW-Watchdog bei Hang            │
│                                         │
│  [5] MAIN_TASK (Prio: LOW)            │
│      → Initialisierung                 │
│      → Fehlerbehandlung                │
│                                         │
└─────────────────────────────────────────┘
```

**Entscheidungspunkt:** Falls Sensor + Valve unsicher in getrennten Tasks:
→ Kombiniere [1] + [2] in einen Task mit Priority HIGH

### **I2C-Sensor-Interface**

```c
VL53L0X (Addr: 0x29)
    ├─ SDA (GPIO 21)
    ├─ SCL (GPIO 22)
    └─ Returns: distance_mm
       • Logik: distance_min → Tank VOLL
       • Logik: distance_max → Tank LEER
```

### **NVS-Speicherstruktur**

```
Namespace: "delongi-tank"
├─ wifi_ssid (string, max 32)
├─ wifi_pass_encrypted (binary, AES-encrypted)
├─ thresh_top (uint32, cm)            ← Oben: Tank VOLL Grenzwert
├─ thresh_bottom (uint32, cm)         ← Unten: Tank LEER Grenzwert
├─ timeout_max (uint32, ms)
├─ last_full (uint32, timestamp)
└─ error_log (string, neuste Fehler)
```

### **Web-Server-Endpoints**

```
GET  /api/status          → JSON: {tank_level, valve_state, wifi, error}
GET  /api/config          → JSON: {threshold_min, threshold_max, timeout_max}
POST /api/config          → Update Grenzwerte + NVS speichern
POST /api/emergency_stop  → Setzt Emergency-Flag zurück
GET  /settings            → Landing-Page (WiFi-Creds)
POST /settings            → Speichert WiFi + NVS encrypt
GET  /                    → Main Dashboard (HTML + embedded CSS/JS)
```

### **WiFi & AP-Mode**

```
[BOOT]
  ↓
[Versuche Basis-AP-Verbindung (5s Timeout)]
  ├─ Erfolg → HTTP-Server im STA-Mode
  └─ Fehler → Starte eigenen AP-Mode
            └─ AP: "delongi-tank-setup"
            └─ IP: 192.168.4.1
            └─ Landing-Page mit Credentials-Form
```

### **Sicherheit**

- NVS-Verschlüsselung: `CONFIG_NVS_ENCRYPTION = y` in menuconfig
- WiFi-Passwort: Nie in Logs, AES-verschlüsselt im NVS
- Tokens: Für API-Authentifizierung optional (Keep Simple → Skip intial)

---

## Grenzwert-Definition

| Grenzwert | Beschreibung | Default | Unit | Editierbar |
|-----------|-------------|---------|------|-----------|
| `threshold_top` | Abstand wenn Tank VOLL (OBEN) | 1 | cm | Ja |
| `threshold_bottom` | Abstand wenn Tank LEER (UNTEN) | 50 | cm | Ja |
| `timeout_max` | Max. Befüll-Zeit | 60000 | ms | Ja |

---

## Implementierungs-Schritte (Geplant, nicht gestartet)

### **Phase 1: Grundlage vorbereiten**
1. [ ] `config.h` mit Pin-Definitionen + NVS-Struktur
2. [ ] `sdkconfig` anpassen (I2C, WiFi, NVS, HTTP-Server enablen)
3. [ ] HAL-Layer für I2C + GPIO (abstraktive Driver)

### **Phase 2: Hardware-Interface**
4. [ ] VL53L0X-I2C-Code (Sensor initialisieren, Messwerte lesen)
5. [ ] GPIO 16 Ventil-Steuerung (PWM/Digital)
6. [ ] Brownout-Handler (ISR für sofort Ventil-Shutdown)

### **Phase 3: Task-Architektur**
7. [ ] SENSOR_TASK implementieren (Messwerte zyklisch, Grenzwert-Prüfung)
8. [ ] VALVE_TASK implementieren (Steuerlogik, Timeout-Verwaltung)
9. [ ] Queue/Mutex zwischen Tasks (sichere Kommunikation)

### **Phase 4: NVS & Persistierung**
10. [ ] NVS Init mit Verschlüsselung
11. [ ] Grenzwerte aus NVS laden + Defaults setzen
12. [ ] WiFi-Credentials encrypted speichern

### **Phase 5: WiFi & AP-Modus**
13. [ ] WiFi-Event-Handler (STA_CONNECTED, STA_DISCONNECTED, AP_START)
14. [ ] AP-Modus-Fallback implementieren
15. [ ] Landing-Page (HTML-String, embedded in Code)

### **Phase 6: Web-Server**
16. [ ] HTTP-Server mit `/api/status`, `/api/config` Endpoints
17. [ ] POST-Handler für Grenzwert-Updates
18. [ ] Main Dashboard (HTML/CSS/JS, iPhone-optimiert)

### **Phase 7: Überwachung & Sicherheit**
19. [ ] Hardware-Watchdog setup + WATCHDOG_TASK
20. [ ] Error-Logging in NVS
21. [ ] Emergency-Stop-Logik

### **Phase 8: Testing & Doku**
22. [ ] Unit-Tests für Sensor-Logik
23. [ ] Integration-Tests für Task-Kommunikation
24. [ ] Benutzer-Doku (Konfiguration, Bedienung, Troubleshooting)

---

## Build & Flash

### **Build**
```bash
idf.py build
```

### **Flash (Initial)**
```bash
idf.py -p COM3 erase_flash flash monitor
```

### **Flash (Update)**
```bash
idf.py -p COM3 flash monitor
```

### **Monitor**
```bash
idf.py monitor
```

---

## Abhängigkeiten (im `CMakeLists.txt` zu handhaben)

- **I2C-Driver**: ESP-IDF built-in
- **WiFi**: ESP-IDF built-in (esp_wifi)
- **HTTP-Server**: ESP-IDF built-in (esp_http_server)
- **NVS**: ESP-IDF built-in (nvs_flash)
- **FreeRTOS**: ESP-IDF built-in
- **TLS/mbedTLS**: Für verschlüsselte NVS (built-in)

Externe Libraries: Momentan keine → Keep it simple mit ESP-IDF only.

---

## Network & mDNS Konfiguration

### **WiFi Betriebsmodi**

#### **1. STA-Modus (Normal-Betrieb)**
```
WiFi-Router (z.B. FritzBox)
    ↓
ESP32 verbindet sich mit SSID
    ↓
mDNS broadcast: delongi-tank.local
    ↓
Erreichbar via: http://delongi-tank.local
```

- **Hostname:** `delongi-tank` (wird im WiFi-Router angezeigt)
- **mDNS Service:** `_http._tcp` (Geräteerkennung)
- **Credentials:** NVS verschlüsselt gespeichert

#### **2. AP-Modus (Fallback bei WiFi-Fehler)**
```
Wenn Verbindung fehlschlägt:
    ↓
ESP32 startet eigenen WiFi-Access-Point
    ↓
SSID: "delongi-tank-setup"
Password: "delongi2024"
    ↓
IP: 10.1.1.1 (statisch)
    ↓
Landing-Page unter http://10.1.1.1
```

- **Neuer Standard:** `10.1.1.1` (Netzwerk 10.1.1.0/24)
- **Credentials können eingegeben werden** im Browser
- **Danach Neustart** → STA-Mode mit neuen Daten

### **sdkconfig Einstellungen (notwendig)**

Folgende müssen via Menübutton enablet werden:

```ini
# mDNS Service Discovery
CONFIG_MDNS_SERVICE_DISCOVERY=y
CONFIG_MDNS_ENABLE_DEBUG=n

# WiFi Soft-AP mit statischer IP
CONFIG_WIFI_AP_ASSIGN_IP=y
CONFIG_WIFI_SOFTAP_DHCP_IP="10.1.1.1"
CONFIG_WIFI_SOFTAP_DHCP_NETMASK="255.255.255.0"

# HTTP Server
CONFIG_ESP_HTTP_SERVER=y
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024

# NVS Encryption (WiFi-Credentials sichern)
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_ENCRYPTION_KEY_SIZE=256
```

### **Zugriff auf das System**

| Modus | URL | Szenario |
|-------|-----|----------|
| **STA-Normal** | `http://delongi-tank.local` | Router connected |
| **STA-IP** | `http://192.168.x.x` | Wenn mDNS nicht funktioniert |
| **AP-Fallback** | `http://10.1.1.1` | WiFi-Fehler → Konfiguration |

---

## Status

- **Aktuell:** Planungsphase abgeschlossen
- **Nächster Schritt:** Phase 1 (config.h + sdkconfig)
- **Version:** 0.1.0 (pre-alpha)

### Hardware Setup

Edit `include/config.h` to configure:
- GPIO pin assignments
- UART baudrates
- WiFi/BLE settings
- Display configurations
- Sensor parameters

### Board Selection

The target board is configured in `sdkconfig` and `sdkconfig.defaults.*`

To change boards:
```bash
idf.py set-target esp32s3
idf.py menuconfig
```

## Development Workflow

1. **Edit code** in `src/main.c` or `include/config.h`
2. **Build:** `/build-project`
3. **Flash:** `/upload-firmware` (fast iteration)
4. **Test & Debug**
5. **Commit:** `/commit` (auto-generates commit message)

## Useful Skills

- **`/build-project`** — Compile firmware
- **`/upload-firmware`** — Fast app update (~3 seconds)
- **`/upload`** — Smart session router
- **`/commit`** — Git commit with auto-message

## Adding Features

Use Copilot skills to extend functionality:

```
/add-ota          Enable OTA firmware updates
/add-webui        Add responsive web dashboard
/add-library      Manage external components
/add-security     Enable Secure Boot, encryption
/setup-ci         GitHub Actions CI/CD
/add-profiling    Performance monitoring
```

## Documentation

- **`PROJECT.md`** — Detailed project specifications
- **`sdkconfig`** — Build configuration (auto-generated)
- **`include/config.h`** — Hardware pin mappings

## Troubleshooting

**Build fails:**
```bash
idf.py fullclean
idf.py build
```

**Flash doesn't work:**
- Check USB connection: `idf.py monitor --no-reset`
- Select port manually: `idf.py -p /dev/ttyUSB0 flash`

**Memory issues:**
- Check heap with `/add-profiling`
- Review `sdkconfig` memory settings
- Use PSRAM if available

## Next Steps

1. Update `PROJECT.md` with hardware details
2. Configure `include/config.h` for your board setup
3. Implement application in `src/main.c`
4. Test with `/upload-firmware`
5. When production-ready, use `/add-security`

---

Generated from ESP32 Template  
For template docs: https://github.com/softrider70/esp32-template
