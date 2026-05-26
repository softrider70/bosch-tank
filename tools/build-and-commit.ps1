param(
    [string]$ProjectPath = "$PSScriptRoot\.."
)

$ProjectPath = Resolve-Path -Path $ProjectPath
Set-Location $ProjectPath

# ESP-IDF Umgebung aktivieren (export.bat)
$espIdfPath = "C:\Users\win4g\Downloads\GitHub\VS-Projekte\CascadeProjects\esp-idf"
$exportBat = Join-Path $espIdfPath "export.bat"

if (-not (Test-Path $exportBat)) {
    Write-Host "ESP-IDF export.bat nicht gefunden: $exportBat" -ForegroundColor Red
    exit 1
}

# Build-Nummer inkrementieren
$incrementScript = Join-Path $ProjectPath "tools\increment_build.py"
if (Test-Path $incrementScript) {
    Write-Host "Incrementing build number..." -ForegroundColor Cyan
    & python "$incrementScript"
}

# Build-Nummer auslesen
$buildNumberPath = Join-Path $ProjectPath ".build_number"
$buildNumber = if (Test-Path $buildNumberPath) { Get-Content $buildNumberPath -Raw } else { "?" }

# Build ausführen mit ESP-IDF Umgebung (activate-esp-idf.ps1)
Write-Host "Building project..." -ForegroundColor Cyan
$activateScript = Join-Path $ProjectPath "activate-esp-idf.ps1"
$buildCmd = ". '$activateScript'; idf.py build"
powershell -ExecutionPolicy Bypass -NoProfile -Command $buildCmd
$buildExitCode = $LASTEXITCODE

if ($buildExitCode -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit $buildExitCode
}

# Commit mit Buildnummer
$gitAddFiles = @()

if (Test-Path $buildNumberPath) { $gitAddFiles += $buildNumberPath }

if ($gitAddFiles.Count -gt 0) {
    Write-Host "Staging build metadata files..." -ForegroundColor Cyan
    & git add -f @gitAddFiles

    $commitMessage = "chore: build #$buildNumber"
    & git commit -m $commitMessage 2>&1

    if ($LASTEXITCODE -eq 0) {
        Write-Host "Build metadata committed: $commitMessage" -ForegroundColor Green

        # Push zum Remote Repository
        Write-Host "Pushing to remote repository..." -ForegroundColor Cyan
        $pushResult = & git push 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Push successful" -ForegroundColor Green
        } else {
            Write-Host "Push failed (may need manual push): $pushResult" -ForegroundColor Yellow
        }
    }
}

# Buildnummer am Schluss ausgeben
Write-Host "Build #$buildNumber completed successfully" -ForegroundColor Green
