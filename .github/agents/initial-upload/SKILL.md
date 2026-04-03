---
title: Upload Firmware (First-Time Setup)
description: Full bootloader + partition table + app upload for new devices
author: CascadeProjects
tags: [upload, flash, esp-idf, initial, setup]
contributions:
  - title: Full Bootloader+Partition+App Upload
    description: One-time setup that flashes entire bootloader, partition, and app
    shells: [pwsh, bash]
---

# /initial-upload Skill

Complete, one-time-only setup for new devices. Flashes bootloader, partition table, and application in one operation.

## Invocation
```
/initial-upload
```

## Use Case
- **First device setup**: Brand-new ESP32 with blank flash
- **Factory reset**: Restore to clean state
- **Partition table changes**: When partition layout is modified

## What It Does
1. Verifies all three binary files exist
2. Detects COM port automatically
3. Erases entire flash (optional, default: no erase)
4. Writes bootloader to address `0x0`
5. Writes partition table to address `0x8000`
6. Writes app to address `0x10000`
7. Device reboots automatically

## Duration
- **Erase** (if needed): ~5-10 seconds
- **Write all**: ~8-12 seconds
- **Device reset**: ~2 seconds
- **Total**: ~15-25 seconds

## Output
```
🚀 First-time full bootloader+partition+app upload
  Detected device on COM3
  
  Writing bootloader (0x0)...
  [████████████████████] 100%
  
  Writing partition table (0x8000)...
  [████████████████████] 100%
  
  Writing app (0x10000)...
  [████████████████████] 100%
  
  ✅ Done! (18.4 seconds)
  📡 Device rebooting...
  
  Waiting for device ready... [✓]
```

## Prerequisites
- Project already built via `/build-project`
- Binary files present in `build/` directory:
  - `build/bootloader/bootloader.bin`
  - `build/partition_table/partition-table.bin`
  - `build/${PROJECT_NAME}.bin`
- Device connected via USB
- For some boards: Hold **BOOT** button during upload

## After First Upload
- Use `/upload-firmware` for faster iterative updates (~3 sec instead of ~20 sec)
- Bootloader and partition table never change unless explicitly rebuilt

## Error Handling
- **Port detection fails**: Manually specify with `--com-port COM3`
- **BOOT mode timeout**: Manually hold BOOT button during flash
- **Checksum error**: Try different USB cable or power supply

## Manual Command (if skill fails)
```bash
esptool.py -p COM3 --baud 921600 \
    write_flash 0x0 build/bootloader/bootloader.bin \
                0x8000 build/partition_table/partition-table.bin \
                0x10000 build/${PROJECT_NAME}.bin
```

## Next Steps
- `/monitor` — Watch device boot and application startup logs
- `/commit` — Save your changes to git
- `/upload-firmware` — Use for subsequent firmware updates (much faster)
