---
title: Upload Firmware (App-Only)
description: Fast firmware update - uploads only application binary to device
author: CascadeProjects
tags: [upload, flash, esp-idf, app]
contributions:
  - title: Fast App-Only Upload
    description: Uploads just the application firmware without touching bootloader
    shells: [pwsh, bash]
---

# /upload-firmware Skill

Fast, iterative firmware upload for development. Updates only the application binary at address `0x10000`.

## Invocation
```
/upload-firmware
```

## Use Case
- **Iterative development**: Change code → build → upload (repeat)
- **No bootloader changes**: Leaves bootloader and partition table intact
- **Quick turnaround**: ~3 seconds per upload

## What It Does
1. Verifies binary exists: `build/${PROJECT_NAME}.bin`
2. Detects COM port automatically
3. Uploads app to address `0x10000`
4. Waits for device to reboot
5. Optionally monitors first log output

## Duration
- **Upload**: ~2-3 seconds
- **Device reset**: ~1-2 seconds
- **Total**: ~3 seconds

## Output
```
⚡ Fast app-only firmware upload
  Detected device on COM3
  Writing app (0x10000)...
  [████████████████████] 100%
  ✅ Done! (2.8 seconds)
  📡 Device rebooting...
  
  Waiting for device ready... [✓]
```

## Prerequisites
- Binary already built via `/build-project`
- Device already has bootloader + partition table (use `/initial-upload` first time)
- Device connected via USB

## Error Handling
- **Port not found**: Scans all COM ports automatically
- **Binary not found**: Prompts to run `/build-project` first
- **Device not responding**: Suggests power cycle or manual RESET button

## Next Steps
- `/monitor` — Watch serial output
- `/build-project` — Rebuild after code changes
- `/initial-upload` — Use if bootloader/partition table corrupted

## Limitations
- ⚠️ Cannot change bootloader version
- ⚠️ Cannot modify partition layout
- ℹ️ Use `/initial-upload` for first-time device setup
