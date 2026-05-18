param(
  [string]$Port = "",
  [switch]$Full
)

$ErrorActionPreference = "Stop"

if (-not $Port) {
  $serial = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.PNPDeviceID -match "VID_303A|VID_10C4|VID_1A86|VID_0403" } |
    Select-Object -First 1

  if (-not $serial) {
    Write-Host "No encontre el T-QT. Conectalo por USB o entra a BOOT y vuelve a correr este script."
    exit 1
  }

  $Port = $serial.DeviceID
}

$python = "C:\Users\manuc\AppData\Local\Python\bin\python.exe"
$esptool = "C:\Users\manuc\.platformio\packages\tool-esptoolpy\esptool.py"
$backupDir = Join-Path $PSScriptRoot "backups"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

$nvsOut = Join-Path $backupDir "tqt-tamagotchi-nvs-$stamp.bin"
Write-Host "Leyendo progreso/NVS desde $Port -> $nvsOut"
& $python $esptool --chip esp32s3 --port $Port --baud 460800 read_flash 0x9000 0x5000 $nvsOut

if ($Full) {
  $fullOut = Join-Path $backupDir "tqt-full-flash-4mb-$stamp.bin"
  Write-Host "Leyendo flash completa 4 MB desde $Port -> $fullOut"
  & $python $esptool --chip esp32s3 --port $Port --baud 460800 read_flash 0x0 0x400000 $fullOut
}

Write-Host "Backup terminado."
