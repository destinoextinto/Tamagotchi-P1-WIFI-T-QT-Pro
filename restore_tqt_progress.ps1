param(
  [Parameter(Mandatory=$true)]
  [string]$Backup,
  [string]$Port = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Backup)) {
  Write-Host "No existe el backup: $Backup"
  exit 1
}

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

Write-Host "Restaurando progreso/NVS en $Port desde $Backup"
& $python $esptool --chip esp32s3 --port $Port --baud 460800 write_flash 0x9000 $Backup
Write-Host "Restauracion terminada."
