# PowerShell script for /upload-firmware skill
# Fast app-only firmware upload for iterative development

param(
    [string]$ComPort = "COM3",
    [int]$Baud = 921600,
    [string]$ProjectPath = $PWD
)

$binaryPath = Join-Path $ProjectPath "build" "${PROJECT_NAME}.bin"

# Verify binary exists
if (-not (Test-Path $binaryPath)) {
    Write-Host "❌ Binary not found: $binaryPath" -ForegroundColor Red
    Write-Host "   Run /build-project first" -ForegroundColor Yellow
    exit 1
}

Write-Host "⚡ Fast app-only firmware upload" -ForegroundColor Cyan
Write-Host "   Binary: $binaryPath" -ForegroundColor Gray
Write-Host "   Address: 0x10000" -ForegroundColor Gray
Write-Host ""

# Upload
Write-Host "📝 Writing app to device..." -ForegroundColor Cyan
$uploadResult = & esptool.py --port $ComPort --baud $Baud write_flash 0x10000 $binaryPath 2>&1
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "✅ Upload successful!" -ForegroundColor Green
    Write-Host ""
    Write-Host "📡 Device rebooting..." -ForegroundColor Cyan
    Start-Sleep -Seconds 2
    
    Write-Host "✓ Ready" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next: /monitor — Watch serial output" -ForegroundColor Yellow
} else {
    Write-Host "❌ Upload failed!" -ForegroundColor Red
    Write-Host $uploadResult
    exit 1
}
