# Copilot Configuration for ESP32 Template

## Skills (Automation)

### Build & Upload
- **/build-project** — Compile the project, generate `${PROJECT_NAME}.bin`
- **/upload** — Smart router (first-time setup vs. update)
- **/upload-firmware** — Fast app-only update (~3 seconds)
- **/initial-upload** — Full bootloader + partition + app (~20 seconds, one-time setup)

### Version Control
- **/commit** — Stage changes, generate smart commit message, push to git

## Workflow
```
1. Code change
2. /build-project       (compile and commit build metadata)
3. /upload-firmware     (fast upload)
4. (watch output)
5. /commit              (save to git)
```

## Documentation
- See `PROJECT.md` for hardware, API, features, and project specification
- See `README.md` for build, flash, OTA workflow, and troubleshooting
- See `include/config.h` for all configuration constants
- See `flash.md` for detailed USB flash instructions
- See `README_OTA_SAFETY.md` for OTA safety concepts

---
Template Version: 0.1.0
