# delongi-tank — ESP32 Project

A custom ESP32 project built with native ESP-IDF.

## Overview

TODO: Add project description here

- **Board:** See `sdkconfig` for target configuration
- **Version:** See `PROJECT.md` for detailed specs
- **Status:** In development

## Quick Start

### Build
```bash
idf.py build
```

### Flash (First Time)
```bash
/initial-upload
```

### Flash (Iteration)
```bash
/upload-firmware
```

### Monitor
```bash
idf.py monitor
```

## Project Structure

```
delongi-tank/
├── src/
│   ├── main.c              Main application code
│   └── CMakeLists.txt      Component build config
├── include/
│   └── config.h            Hardware configuration (pins, settings)
├── CMakeLists.txt          Project build config
├── sdkconfig               Build configuration (auto-generated)
├── PROJECT.md              Detailed project specs
└── README.md               This file
```

## Configuration

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
