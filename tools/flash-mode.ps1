param(
    [ValidateSet("usb", "ota", "last", "auto")]
    [string]$Mode = "last",

    [ValidateSet("initial", "update", "auto")]
    [string]$FlashType = "auto",

    [string]$UsbPort = "",
    [string]$DeviceIp = "",
    [string]$HostIp = "",
    [int]$HttpPort = 8070,
    [int]$StatusTimeoutSec = 240,
    [int]$Baud = 921600
)

$ErrorActionPreference = "Stop"

function Test-PrivateIp {
    param([string]$Ip)
    if ($Ip -match '^10\.') { return $true }
    if ($Ip -match '^192\.168\.') { return $true }
    if ($Ip -match '^172\.(1[6-9]|2[0-9]|3[0-1])\.') { return $true }
    return $false
}

function Get-AutoHostIp {
    $candidates = Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object {
            $_.IPAddress -ne '127.0.0.1' -and
            $_.IPAddress -notlike '169.254*' -and
            (Test-PrivateIp $_.IPAddress)
        }

    $selected = $candidates | Select-Object -First 1
    if ($null -eq $selected) {
        throw "Keine private IPv4-Adresse gefunden. Bitte -HostIp explizit setzen."
    }

    return $selected.IPAddress
}

function Get-AutoUsbPort {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($ports.Count -eq 0) {
        throw "Keine seriellen Ports gefunden. Bitte -UsbPort explizit setzen."
    }

    if ($ports.Count -eq 1) {
        Write-Host "[PORT] Nur ein Port gefunden: $($ports[0])" -ForegroundColor Green
        return $ports[0]
    }

    # Versuche ESP32 zu erkennen via esptool
    Write-Host "[PORT] Suche ESP32 auf verfügbaren Ports..." -ForegroundColor Cyan
    foreach ($port in $ports) {
        try {
            $result = & python -m esptool --port $port --baud 115200 --connect-attempts 1 chip_id 2>&1
            if ($LASTEXITCODE -eq 0 -and $result -match "Chip is") {
                Write-Host "[PORT] ESP32 gefunden auf $port" -ForegroundColor Green
                return $port
            }
        } catch {
            # Nichts tun, nächster Port
        }
    }

    # Fallback: COM3 wenn verfügbar
    if ($ports -contains 'COM3') {
        Write-Host "[PORT] Verwende COM3 (Standard)" -ForegroundColor Yellow
        return 'COM3'
    }

    # Letzter Fallback: Erster Port
    Write-Host "[PORT] Verfügbare Ports: $($ports -join ', ')" -ForegroundColor Yellow
    Write-Host "[PORT] Verwende ersten Port: $($ports[0])" -ForegroundColor Yellow
    return $ports[0]
}

function Test-DeviceHasPartitionTable {
    param([string]$Port)

    Write-Host "[DETECT] Prüfe ob Gerät Partition Table hat..." -ForegroundColor Cyan
    try {
        # Versuche Partition Table zu lesen
        $result = & python -m esptool --port $Port --baud 115200 --connect-attempts 2 read_flash 0x8000 32 - 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "[DETECT] ✓ Partition Table gefunden (Update-Modus)" -ForegroundColor Green
            return $true
        }
    } catch {
        # Fehler ignorieren
    }

    Write-Host "[DETECT] ✗ Keine Partition Table (Initial-Flash nötig)" -ForegroundColor Yellow
    return $false
}

function Get-FlashType {
    param([string]$Port)

    if (Test-DeviceHasPartitionTable -Port $Port) {
        return "update"
    } else {
        return "initial"
    }
}

function Write-ProgressBar {
    param([int]$Percent, [string]$Label)
    $filled = [math]::Floor($Percent / 5)
    $empty = 20 - $filled
    $bar = "[" + ("█" * $filled) + ("░" * $empty) + "]"
    Write-Host "`r$bar $Percent% $Label" -NoNewline -ForegroundColor Cyan
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

$env:ESP_IDF_VERSION = "6.0"
. .\activate-esp-idf.ps1

$stateFile = Join-Path $repoRoot ".last_flash_mode.json"
$previousState = $null
if (Test-Path $stateFile) {
    try {
        $previousState = Get-Content -Raw $stateFile | ConvertFrom-Json
    } catch {
        Write-Host "⚠️ Konnte letzte Flash-Konfiguration nicht laden. Ignoriere sie." -ForegroundColor Yellow
    }
}

function Get-GitCommitHash {
    try {
        return (& git rev-parse HEAD 2>$null).Trim()
    } catch {
        return $null
    }
}

function Is-GitTreeDirty {
    try {
        & git diff --quiet --ignore-submodules --
        $unstaged = $LASTEXITCODE
    } catch {
        $unstaged = 1
    }

    try {
        & git diff --cached --quiet --ignore-submodules --
        $staged = $LASTEXITCODE
    } catch {
        $staged = 1
    }

    try {
        $untracked = (& git ls-files --others --exclude-standard 2>$null)
        $hasUntracked = -not [string]::IsNullOrWhiteSpace($untracked)
    } catch {
        $hasUntracked = $false
    }

    return ($unstaged -ne 0 -or $staged -ne 0 -or $hasUntracked)
}

function Needs-Build {
    param(
        [string]$BinaryPath,
        [string]$CommitStatePath
    )

    if (-not (Test-Path $BinaryPath)) {
        Write-Host "[BUILD CHECK] Kein Build vorhanden. Build wird ausgeführt." -ForegroundColor Yellow
        return $true
    }

    if (-not (Test-Path $CommitStatePath)) {
        Write-Host "[BUILD CHECK] Kein letztes Build-Commit gefunden. Build wird ausgeführt." -ForegroundColor Yellow
        return $true
    }

    $currentCommit = Get-GitCommitHash
    if (-not $currentCommit) {
        Write-Host "[BUILD CHECK] Git Commit kann nicht ermittelt werden. Build wird sicherheitshalber ausgeführt." -ForegroundColor Yellow
        return $true
    }

    $lastBuiltCommit = Get-Content -Raw $CommitStatePath
    if ($currentCommit -ne $lastBuiltCommit) {
        Write-Host "[BUILD CHECK] Neuer Commit erkannt (aktuell: $currentCommit, letzter Build: $lastBuiltCommit). Build wird ausgeführt." -ForegroundColor Yellow
        return $true
    }

    if (Is-GitTreeDirty) {
        Write-Host "[BUILD CHECK] Uncommitted Änderungen vorhanden. Build wird ausgeführt." -ForegroundColor Yellow
        return $true
    }

    Write-Host "[BUILD CHECK] Build ist aktuell für Commit $currentCommit." -ForegroundColor Green
    return $false
}

if ($Mode -eq 'last') {
    if ($previousState -ne $null -and $previousState.mode) {
        $Mode = $previousState.mode
        Write-Host "[FLASH] Verwende letzten Modus: $Mode" -ForegroundColor Cyan
    } else {
        Write-Host "[FLASH] Kein letzter Flash-Modus gefunden, verwende auto-detect." -ForegroundColor Yellow
        $Mode = 'usb'
        $FlashType = 'auto'
    }
}

if ($Mode -eq 'auto') {
    $Mode = 'usb'
    $FlashType = 'auto'
    Write-Host "[FLASH] Auto-Modus: Erkenne USB-Port und Flash-Typ..." -ForegroundColor Cyan
}

if ($Mode -eq 'usb') {
    if ([string]::IsNullOrWhiteSpace($UsbPort) -and $previousState -ne $null -and $previousState.usbPort) {
        $UsbPort = $previousState.usbPort
    }

    if ([string]::IsNullOrWhiteSpace($UsbPort)) {
        $UsbPort = Get-AutoUsbPort
        Write-Host "[FLASH] Auto-detected USB port: $UsbPort" -ForegroundColor Green
    }

    Write-Host "[FLASH] Mode=usb, Port=$UsbPort"

    # Auto-Detect Flash Type
    if ($FlashType -eq 'auto') {
        $FlashType = Get-FlashType -Port $UsbPort
    }

    $binPath = Join-Path $repoRoot "build\bosch-tank.bin"
    $bootloaderPath = Join-Path $repoRoot "build\bootloader\bootloader.bin"
    $partitionPath = Join-Path $repoRoot "build\partition_table\partition-table.bin"
    $lastBuiltCommitPath = Join-Path $repoRoot ".last_built_commit"

    if (Needs-Build -BinaryPath $binPath -CommitStatePath $lastBuiltCommitPath) {
        Write-Host "[FLASH] Baue Projekt vor USB-Flash..." -ForegroundColor Cyan
        & powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot\tools\build-and-commit.ps1"
        if ($LASTEXITCODE -ne 0) {
            throw "Build vor USB-Flash fehlgeschlagen."
        }
    }

    # Prüfe ob alle Dateien für Initial-Flash existieren
    if ($FlashType -eq 'initial') {
        $missing = @()
        if (-not (Test-Path $bootloaderPath)) { $missing += "bootloader.bin" }
        if (-not (Test-Path $partitionPath)) { $missing += "partition-table.bin" }
        if (-not (Test-Path $binPath)) { $missing += "bosch-tank.bin" }

        if ($missing.Count -gt 0) {
            throw "Fehlende Dateien für Initial-Flash: $($missing -join ', ') - Bitte erst build-and-commit ausführen"
        }
    }

    # Flash durchführen
    $startTime = Get-Date
    if ($FlashType -eq 'initial') {
        Write-Host "[FLASH] 🚀 Initial-Flash (Bootloader + Partition + App)..." -ForegroundColor Cyan
        Write-Host "  Bootloader: $bootloaderPath" -ForegroundColor Gray
        Write-Host "  Partition:  $partitionPath" -ForegroundColor Gray
        Write-Host "  App:        $binPath" -ForegroundColor Gray

        & python -m esptool --port $UsbPort --baud $Baud `
            write_flash 0x0 $bootloaderPath `
                        0x8000 $partitionPath `
                        0x10000 $binPath 2>&1 | ForEach-Object {
                Write-Host "  $_" -ForegroundColor Gray
            }
    } else {
        Write-Host "[FLASH] ⚡ Fast Update (nur App)..." -ForegroundColor Cyan
        Write-Host "  App: $binPath -> 0x10000" -ForegroundColor Gray

        & python -m esptool --port $UsbPort --baud $Baud write_flash 0x10000 $binPath 2>&1 | ForEach-Object {
            Write-Host "  $_" -ForegroundColor Gray
        }
    }

    $exitCode = $LASTEXITCODE
    $duration = ((Get-Date) - $startTime).TotalSeconds

    if ($exitCode -eq 0) {
        Write-Host "`n[FLASH] ✅ Flash erfolgreich! (${duration:F1}s)" -ForegroundColor Green
        $state = @{ mode = 'usb'; usbPort = $UsbPort; deviceIp = ''; hostIp = ''; httpPort = $HttpPort; flashType = $FlashType }
        $state | ConvertTo-Json | Set-Content -NoNewline -Encoding UTF8 $stateFile
        Write-Host "[FLASH] Konfiguration gespeichert (Modus: $FlashType)" -ForegroundColor Green

        Write-Host "`n[FLASH] Nächste Schritte:" -ForegroundColor Cyan
        if ($FlashType -eq 'initial') {
            Write-Host "  - Zukünftige Updates: flash-mode.ps1 -Mode usb -FlashType update (schneller)" -ForegroundColor Yellow
        }
        Write-Host "  - Monitor starten: idf.py -p $UsbPort monitor" -ForegroundColor Yellow
    } else {
        Write-Host "`n[FLASH] ❌ Flash fehlgeschlagen!" -ForegroundColor Red
    }
    exit $exitCode
}

if ($Mode -eq 'ota') {
    if ([string]::IsNullOrWhiteSpace($DeviceIp) -and $previousState -ne $null -and $previousState.deviceIp) {
        $DeviceIp = $previousState.deviceIp
    }
    if ([string]::IsNullOrWhiteSpace($HostIp) -and $previousState -ne $null -and $previousState.hostIp) {
        $HostIp = $previousState.hostIp
    }
    if ($previousState -ne $null -and $previousState.httpPort) {
        $HttpPort = $previousState.httpPort
    }

    if ([string]::IsNullOrWhiteSpace($DeviceIp)) {
        throw "Bei Mode=ota muss -DeviceIp gesetzt sein (z.B. 192.168.1.50)."
    }

    if ([string]::IsNullOrWhiteSpace($HostIp)) {
        $HostIp = Get-AutoHostIp
    }

    Write-Host "[FLASH] Mode=ota"
    Write-Host "[FLASH] DeviceIp=$DeviceIp"
    Write-Host "[FLASH] HostIp=$HostIp"
    Write-Host "[FLASH] HttpPort=$HttpPort"

    $binPath = Join-Path $repoRoot "build\bosch-tank.bin"
    $lastBuiltCommitPath = Join-Path $repoRoot ".last_built_commit"
    if (Needs-Build -BinaryPath $binPath -CommitStatePath $lastBuiltCommitPath) {
        Write-Host "[FLASH] Baue Projekt vor OTA-Flash..." -ForegroundColor Cyan
        & powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot\tools\build-and-commit.ps1"
        if ($LASTEXITCODE -ne 0) {
            throw "Build vor OTA fehlgeschlagen."
        }
    }

    $serverProc = $null
    try {
        $serverProc = Start-Process -FilePath "python" -ArgumentList "-m", "http.server", "$HttpPort", "--bind", "$HostIp" -WorkingDirectory (Join-Path $repoRoot "build") -PassThru -WindowStyle Hidden
        [System.Threading.Thread]::Sleep(1200)

        $otaUrl = "http://$HostIp`:$HttpPort/bosch-tank.bin"
        $startUri = "http://$DeviceIp/api/ota/start"
        $statusUri = "http://$DeviceIp/api/ota/status"
        $payload = @{ url = $otaUrl } | ConvertTo-Json -Compress

        Write-Host "[OTA] Start: $startUri"
        Write-Host "[OTA] URL:   $otaUrl"

        $startResp = Invoke-RestMethod -Method Post -Uri $startUri -ContentType "application/json" -Body $payload -TimeoutSec 15
        Write-Host ("[OTA] Antwort: {0}" -f ($startResp | ConvertTo-Json -Compress))

        $deadline = (Get-Date).AddSeconds($StatusTimeoutSec)
        do {
            try {
                $statusResp = Invoke-RestMethod -Method Get -Uri $statusUri -TimeoutSec 8
                $inProgress = [bool]$statusResp.ota.in_progress
                $phase = [string]$statusResp.ota.phase
                $msg = [string]$statusResp.ota.message
                Write-Host "[OTA] Status: $phase - $msg"

                if (-not $inProgress) {
                    if ([bool]$statusResp.ota.last_result_ok) {
                        Write-Host "[OTA] Erfolgreich. Geraet startet neu."
                        $state = @{ mode = 'ota'; usbPort = ''; deviceIp = $DeviceIp; hostIp = $HostIp; httpPort = $HttpPort }
                        $state | ConvertTo-Json | Set-Content -NoNewline -Encoding UTF8 $stateFile
                        Write-Host "[FLASH] Letzte Flash-Konfiguration gespeichert." -ForegroundColor Green
                        exit 0
                    }

                    $err = [string]$statusResp.ota.last_error
                    throw "OTA fehlgeschlagen: $err"
                }
            } catch {
                Write-Host "[OTA] Warte auf Status/Neustart..."
            }

            [System.Threading.Thread]::Sleep(2000)
        } while ((Get-Date) -lt $deadline)

        throw "OTA-Status Timeout nach $StatusTimeoutSec Sekunden."
    }
    finally {
        if ($null -ne $serverProc -and -not $serverProc.HasExited) {
            Stop-Process -Id $serverProc.Id -Force
        }
    }
}

