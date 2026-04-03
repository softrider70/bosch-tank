---
title: Upload Firmware (Smart Router)
description: Intelligently route to app-only or full bootloader+partition+app upload
author: CascadeProjects
tags: [upload, flash, esp-idf, router]
contributions:
  - title: Smart Upload Router
    description: Meta-skill that asks user whether first-time setup or update
    shells: [pwsh, bash]
---

# /upload Skill (Meta-Router)

Smart upload router that intelligently delegates to either:
- **First time?** → `/initial-upload` (full bootloader + partition + app, ~20 sec)
- **Updating firmware?** → `/upload-firmware` (app-only, ~3 sec)

## Invocation
```
/upload
```

## What It Does
1. Detects if device is new (checks for existing partition table)
2. Asks user: "Is this your first time uploading to this device?"
3. Routes to appropriate upload skill
4. Monitors upload progress

## Dialog Example
```
✋ First time uploading to this device?
   [Y] Yes - flash bootloader + partition + app (~20 sec)
   [N] No - update app only (~3 sec)
   [?] Show help
```

## Output (First Time)
```
🚀 Full bootloader + partition + app upload
  Erasing flash...  
  Writing bootloader (0x0)...
  Writing partition table (0x8000)...
  Writing app (0x10000)...
  ✅ Done! (19.2 seconds)
```

## Output (Update)
```
⚡ Fast app-only update
  Writing app (0x10000)...
  ✅ Done! (2.8 seconds)
```

## Prerequisites
- Binary built via `/build-project`
- USB device detected and COM port available
- First-time uploads require manual BOOT button hold or auto-reset support

## Next Steps
- `/monitor` — Watch serial output
- `/commit` — Commit changes to git
