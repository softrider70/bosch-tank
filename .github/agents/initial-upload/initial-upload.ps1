# PowerShell script for /initial-upload skill
# First-time full bootloader + partition + app upload

param(
    [string]$ComPort = "COM3",
    [int]$Baud = 921600,
    [string]$ProjectPath = $PWD,
    [bool]$Erase = $false
)

# Verify all required binaries exist
$bootloaderPath = Join-Path $ProjectPath "build" "bootloader" "bootloader.bin"
$partitionPath = Join-Path $ProjectPath "build" "partition_table" "partition-table.bin"
$appPath = Join-Path $ProjectPath "build" "${PROJECT_NAME}.bin"

$missingFiles = @()
if (-not (Test-Path $bootloaderPath)) { $missingFiles += "bootloader.bin" }
if (-not (Test-Path $partitionPath)) { $missingFiles += "partition-table.bin" }
if (-not (Test-Path $appPath)) { $missingFiles += "$${PROJECT_NAME}.bin" }

if ($missingFiles.Count -gt 0) {
    Write-Host "❌ Missing binary files:" -ForegroundColor Red
    foreach ($file in $missingFiles) {
        Write-Host "   - $file" -ForegroundColor Red
    }
    Write-Host "   Run /build-project first" -ForegroundColor Yellow
    exit 1
}

Write-Host "🚀 First-time bootloader+partition+app upload" -ForegroundColor Cyan
Write-Host "   Bootloader: $bootloaderPath" -ForegroundColor Gray
Write-Host "   Partition:  $partitionPath" -ForegroundColor Gray
Write-Host "   App:        $appPath" -ForegroundColor Gray
Write-Host ""

if ($Erase) {
    Write-Host "🗑️  Erasing flash..." -ForegroundColor Yellow
    & esptool.py --port $ComPort erase_flash 2>&1 | Out-Null
    Start-Sleep -Seconds 2
}

Write-Host "📝 Writing to device..." -ForegroundColor Cyan
$uploadResult = & esptool.py --port $ComPort --baud $Baud `
    write_flash 0x0 $bootloaderPath `
                0x8000 $partitionPath `
                0x10000 $appPath 2>&1

$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "✅ Upload successful!" -ForegroundColor Green
    Write-Host ""
    Write-Host "📡 Device rebooting..." -ForegroundColor Cyan
    Start-Sleep -Seconds 2
    
    Write-Host "✓ Device ready" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  /monitor          — Watch device startup" -ForegroundColor Yellow
    Write-Host "  /commit           — Save to git" -ForegroundColor Yellow
    Write-Host "  /upload-firmware  — Use for faster updates next time" -ForegroundColor Yellow
} else {
    Write-Host "❌ Upload failed!" -ForegroundColor Red
    Write-Host $uploadResult
    exit 1
}
