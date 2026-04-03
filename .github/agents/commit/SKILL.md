---
title: Commit Changes
description: Stage changes, generate smart commit message with AI, and push to git
author: CascadeProjects
tags: [git, commit, automation]
contributions:
  - title: AI-Powered Git Commit
    description: Analyzes changes and generates meaningful commit messages
    shells: [pwsh, bash]
---

# /commit Skill

Automatically stages changes, generates a smart commit message using AI, and optionally pushes to remote.

## Invocation
```
/commit
```

## What It Does
1. Scans for modified files in the project
2. Stages all changes (`git add .`)
3. Analyzes diffs to understand what changed
4. Generates a meaningful commit message using Copilot
5. Creates commit with generated message
6. Asks: "Push to origin?"

## Example Flow
```
📝 Analyzing changes...
  Modified: src/main.c (added WiFi connection logic)
  Modified: include/config.h (added WiFi configuration)
  New:      src/wifi.c (WiFi driver implementation)

✨ Generated commit message:
  feat: Add WiFi connectivity with station mode
  
  - Implement WiFi station connection logic
  - Add WiFi SSID/password configuration
  - Create dedicated WiFi driver module
  - Update main task to initialize WiFi on startup
  
Continue? [Y/n] 
```

## Dialog Options
```
Commit? [Y/n]
  Y — Create commit with generated message
  n — Show message, edit before committing
  ? — Manual edit mode
  
Push? [Y/n]
  Y — Push to origin/main (or current branch)
  n — Keep changes local
```

## Output
```
✅ Commit successful!
   Commit: a7f3c2e
   Message: "feat: Add WiFi connectivity..."
   
Push to origin? [Y/n] 
→ Pushing to origin/main...
✓ Pushed 1 commit (a7f3c2e)
```

## Prerequisites
- Git repository initialized (`git init` or cloned)
- All changes staged or ready for staging
- Copilot Chat available for AI message generation

## Manual Fallback
If auto-message doesn't work:
```bash
git add .
git commit -m "Your manual message here"
git push origin main
```

## Commit Message Convention
Uses semantic commit format:
- `feat: ` — New feature
- `fix: ` — Bug fix
- `refactor: ` — Code restructuring
- `docs: ` — Documentation only
- `test: ` — Tests
- `chore: ` — Build/tooling

## Next Steps
- Continue development or switch branches
- Use `git log --oneline` to view history
- Merge PR once reviewed

## Tips
- Commit frequently (small, logical chunks)
- Don't stage unrelated changes together
- Push to backup remote regularly
