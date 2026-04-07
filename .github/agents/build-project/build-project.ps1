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

# Run build and commit metadata if successful
$scriptPath = Join-Path $ProjectPath "tools\build-and-commit.ps1"
if (-not (Test-Path $scriptPath)) {
    Write-Host "❌ Build wrapper not found: $scriptPath" -ForegroundColor Red
    exit 1
}

$buildOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath 2>&1
$buildExitCode = $LASTEXITCODE

if ($buildExitCode -eq 0) {
    Write-Host "✅ Build and commit process completed successfully!" -ForegroundColor Green
} else {
    Write-Host "❌ Build and commit failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Output:" -ForegroundColor Red
    Write-Host $buildOutput
    exit $buildExitCode
}
