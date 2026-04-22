$body = @{
    url = "http://192.168.1.191:80/bosch-tank.bin"
} | ConvertTo-Json

$response = Invoke-RestMethod -Uri 'http://192.168.1.236/api/ota/start' -Method Post -ContentType 'application/json' -Body $body
Write-Host "OTA Response:"
$response
