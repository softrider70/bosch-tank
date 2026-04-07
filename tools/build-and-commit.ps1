param(
    [string]$ProjectPath = "$PSScriptRoot\.."
)

$ProjectPath = Resolve-Path -Path $ProjectPath
Set-Location $ProjectPath

if (-not $env:IDF_PATH) {
    Write-Host "❌ ESP-IDF environment is not configured. Run .\activate-esp-idf.ps1 first." -ForegroundColor Red
    exit 1
}

Write-Host "🔨 Running build in $ProjectPath..." -ForegroundColor Cyan

$buildOutput = & idf.py build 2>&1
$buildExitCode = $LASTEXITCODE

if ($buildExitCode -ne 0) {
    Write-Host "❌ Build failed!" -ForegroundColor Red
    Write-Host $buildOutput
    exit $buildExitCode
}

Write-Host "✅ Build successful!" -ForegroundColor Green

$buildNumberPath = Join-Path $ProjectPath ".build_number"
$lastVersionPath = Join-Path $ProjectPath ".last_version"
$versionHeaderPath = Join-Path $ProjectPath "include\version.h"

$gitAddFiles = @()
if (Test-Path $buildNumberPath) {
    $gitAddFiles += $buildNumberPath
}
if (Test-Path $lastVersionPath) {
    $gitAddFiles += $lastVersionPath
}

if ($gitAddFiles.Count -eq 0) {
    Write-Host "⚠️ No build metadata files found to commit." -ForegroundColor Yellow
    exit 0
}

Write-Host "📦 Staging build metadata files..." -ForegroundColor Cyan
& git add -f @gitAddFiles

$buildNumber = if (Test-Path $buildNumberPath) { Get-Content $buildNumberPath -Raw } else { "?" }
$versionString = "build #$buildNumber"
if (Test-Path $versionHeaderPath) {
    $match = Select-String -Path $versionHeaderPath -Pattern 'APP_FULL_VERSION\s+"(.+)"'
    if ($match) {
        $versionString = $match.Matches[0].Groups[1].Value
    }
}

$commitMessage = "chore: auto-commit build metadata after successful build ($versionString)"
& git commit -m $commitMessage

if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Build metadata committed." -ForegroundColor Green
} else {
    Write-Host "⚠️ No changes to commit or commit failed." -ForegroundColor Yellow
}
