---
title: Build Project
description: Compile the ESP32 project and generate a binary firmware file
author: CascadeProjects
tags: [build, compile, esp-idf]
contributions:
  - title: Build ESP32 Project
    description: Builds the project using ESP-IDF CMake build system
    shells: [pwsh, bash]
---

# /build-project Skill

Compiles the ESP32 project and generates the binary firmware file (`${PROJECT_NAME}.bin`) in the `build/` directory.

## Invocation
```
/build-project
```

## What It Does
1. Sets the ESP-IDF target to the configured board
2. Runs `idf.py build` to compile all sources
3. Validates successful compilation
4. Reports binary size and location

## Output
```
Build complete!
Binary: build/${PROJECT_NAME}.bin (452.5 KB)
Ready for upload
```

## Prerequisites
- ESP-IDF installed and environment configured
- VS Code Espressif IDF Extension installed

## Error Handling
- **Syntax errors**: Displayed with file and line numbers
- **Missing dependencies**: Clear error message with fix suggestion
- **Build timeout**: Terminates after 5 minutes

## Next Steps
After successful build:
- `/upload` — Upload to device (smart router)
- `/upload-firmware` — Fast app-only upload (~3 sec)
- `/initial-upload` — Full flash on first device (~20 sec)
