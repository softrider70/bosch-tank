# Passwort aus dem seriellen Startprotokoll
# (Zeile "Weboberflaeche: Benutzer beliebig, Passwort: ...")
$pass = $env:BOSCH_TANK_PASS
if (-not $pass) { Write-Host "Bitte zuerst setzen: `$env:BOSCH_TANK_PASS = '<Passwort aus dem Startprotokoll>'"; exit 1 }
$auth = @{ Authorization = "Basic " + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("admin:$pass")) }

$body = @{
    url = "http://192.168.1.191:8070/bosch-tank.bin"
} | ConvertTo-Json

$response = Invoke-RestMethod -Uri 'http://192.168.1.236/api/ota/start' -Method Post -ContentType 'application/json' -Body $body -Headers $auth
$response
