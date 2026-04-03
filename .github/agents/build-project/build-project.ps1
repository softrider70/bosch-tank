# PowerShell script for /build-project skill
# Compiles the ESP32 project using ESP-IDF

param(
    [string]$ProjectPath = $PWD
)

# Set ESP-IDF environment
if (-not $env:IDF_PATH) {
    Write-Host "❌ ESP-IDF not configured. Please set IDF_PATH environment variable" -ForegroundColor Red
    exit 1
}

# Assume target is configured in sdkconfig or use default
$target = "esp32"  # Could be detected from existing sdkconfig

Write-Host "🔨 Building ${PROJECT_NAME}..." -ForegroundColor Cyan
Write-Host "Target: $target" -ForegroundColor Gray

# Run build
$buildOutput = & idf.py build 2>&1
$buildExitCode = $LASTEXITCODE

if ($buildExitCode -eq 0) {
    Write-Host "✅ Build successful!" -ForegroundColor Green
    
    # Show binary size
    $binaryPath = Join-Path $ProjectPath "build" "${PROJECT_NAME}.bin"
    if (Test-Path $binaryPath) {
        $size = (Get-Item $binaryPath).Length / 1024
        Write-Host "📦 Binary size: $([Math]::Round($size, 2)) KB" -ForegroundColor Green
        Write-Host "📍 Location: $binaryPath" -ForegroundColor Gray
    }
    
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  /upload           — Smart upload (asks: first time or update?)" -ForegroundColor Yellow
    Write-Host "  /upload-firmware  — Fast app-only upload (~3 seconds)" -ForegroundColor Yellow
    Write-Host "  /initial-upload   — Full bootloader+partition+app (~20 seconds, 1st time only)" -ForegroundColor Yellow
} else {
    Write-Host "❌ Build failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Build output:" -ForegroundColor Red
    Write-Host $buildOutput
    exit 1
}
