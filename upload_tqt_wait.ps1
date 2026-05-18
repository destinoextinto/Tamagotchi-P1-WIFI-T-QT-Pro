Write-Host "Conecta el T-QT manteniendo presionado el boton izquierdo/BOOT..."
$port = $null
for ($i = 0; $i -lt 120; $i++) {
  $serial = Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match 'VID_303A|VID_10C4|VID_1A86|VID_0403' } | Select-Object -First 1
  if ($serial) {
    $port = $serial.DeviceID
    Write-Host "Puerto detectado: $port ($($serial.Name))"
    break
  }
  Start-Sleep -Milliseconds 500
}
if (-not $port) {
  Write-Host "No detecte puerto ESP. Mant?n BOOT presionado, desconecta/conecta USB e intenta otra vez."
  exit 1
}
& 'C:\Users\manuc\AppData\Local\Python\bin\python.exe' -m platformio run -d firmware\esp8266-tamagotchi -e t-qt-pro-n4r2 -t upload --upload-port $port
