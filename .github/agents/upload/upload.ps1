# PowerShell script for /upload meta-router skill
# Intelligently routes to either first-time or update upload

param(
    [string]$ComPort = "COM3",
    [int]$Baud = 921600
)

function Get-DeviceHasPartitionTable {
    param([string]$Port)
    
    # Try to read partition table from device
    try {
        $result = & esptool.py -p $Port read_flash_status 2>&1
        return $LASTEXITCODE -eq 0
    } catch {
        return $false
    }
}

# Check device status
Write-Host "🔎 Detecting device status..." -ForegroundColor Cyan

if (Get-DeviceHasPartitionTable -Port $ComPort) {
    $firstTime = $false
    Write-Host "📱 Existing firmware detected" -ForegroundColor Green
} else {
    $firstTime = $true
    Write-Host "⚠️  No firmware detected (new device)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "❓ How do you want to proceed?" -ForegroundColor Cyan
Write-Host ""
Write-Host "  [1] First time setup (flash bootloader + partition + app) ~20 sec" -ForegroundColor Yellow
Write-Host "  [2] Update app only (update firmware) ~3 sec" -ForegroundColor Yellow
Write-Host "  [?] Show help" -ForegroundColor Gray
Write-Host ""

# For now, auto-select based on detection (in real UI, user would choose)
if ($firstTime) {
    Write-Host "→ Auto-selected: First time setup" -ForegroundColor Yellow
    & "$PSScriptRoot/../initial-upload/initial-upload.ps1" -ComPort $ComPort -Baud $Baud
} else {
    Write-Host "→ Auto-selected: Update app only" -ForegroundColor Yellow
    & "$PSScriptRoot/../upload-firmware/upload-firmware.ps1" -ComPort $ComPort -Baud $Baud
}
