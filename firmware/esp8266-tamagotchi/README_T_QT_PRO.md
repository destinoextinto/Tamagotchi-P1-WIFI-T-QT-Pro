# Tamagotchi en LilyGo T-QT Pro

Este firmware ahora incluye entornos PlatformIO para el T-QT Pro ESP32-S3.

Este port esta basado en [anabolyc/Tamagotchi](https://github.com/anabolyc/Tamagotchi), fork de [GaryZ88/ArduinoGotchi](https://github.com/GaryZ88/ArduinoGotchi), y mantiene la licencia GPLv2 del proyecto original.

## Requisitos

- PlatformIO.
- El repositorio `T-QT-main` en:
  `C:/Users/manuc/Desktop/DESKTOP/T-QT-main/T-QT-main`

Se usa la copia de `TFT_eSPI` incluida en ese repo porque LilyGo modifica la inicializacion del panel GC9A01 de 128 x 128.

## Compilar

Para la version con 4 MB Flash y 2 MB PSRAM:

```powershell
pio run -d firmware/esp8266-tamagotchi -e t-qt-pro-n4r2
```

Para la version con 8 MB Flash y sin PSRAM:

```powershell
pio run -d firmware/esp8266-tamagotchi -e t-qt-pro-n8
```

## Subir a la placa

```powershell
pio run -d firmware/esp8266-tamagotchi -e t-qt-pro-n4r2 -t upload
```

Si no entra en modo upload, manten presionado BOOT al conectar USB, como indica la guia del T-QT Pro.

## Controles

El Tamagotchi original usa tres botones, pero el T-QT Pro trae dos:

- Boton izquierdo: cambiar opcion.
- Boton derecho: seleccionar.
- Ambos botones a la vez: cancelar/volver.
- Mantener ambos botones por unos segundos borra el estado guardado y reinicia.

## Monitor web y WiFi

El build del T-QT Pro incluye un monitor web de solo lectura:

- Muestra la pantalla LCD emulada y los iconos activos.
- Aplica color base a cada personaje segun su sprite del growth chart.
- Reproduce alertas en el navegador cuando el Tamagotchi activa sonido o atencion.
- Sincroniza la hora interna del Tamagotchi por NTP cuando esta conectado a WiFi.
- Toma la zona horaria del navegador o telefono que abre el portal y la guarda en la placa.
- No expone botones remotos ni controles del Tamagotchi.

Si no hay una red guardada, el T-QT crea un punto de acceso:

- SSID: `TamaQT-XXXX`
- Password: `tamaqt123`
- URL: `http://192.168.4.1`

Desde esa pagina puedes guardar el SSID y password de tu WiFi. Al reiniciar, si logra conectarse a tu red, el monitor queda disponible en:

- `http://tqt-tamagotchi.local`
- o la IP que imprime por serial al arrancar.

La zona horaria por defecto es Mexico City sin horario de verano (`CST6`). Al abrir la pagina del monitor, el navegador envia automaticamente su zona/offset actual al T-QT para ajustar la hora local. Si viajas, abre el monitor desde un dispositivo configurado en la zona correcta y el T-QT actualiza ese dato.
