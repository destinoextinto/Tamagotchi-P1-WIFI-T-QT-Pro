# ArduinoGotchi for LilyGo T-QT Pro

## Synopsis

This is a LilyGo T-QT Pro ESP32-S3 port of **ArduinoGotchi**, a real [Tamagotchi P1](https://tamagotchi.fandom.com/wiki/Tamagotchi_(1996_Pet)) emulator. The emulation core is based on [TamaLib](https://github.com/jcrona/tamalib).

This repository is based on [anabolyc/Tamagotchi](https://github.com/anabolyc/Tamagotchi), which is a fork of [GaryZ88/ArduinoGotchi](https://github.com/GaryZ88/ArduinoGotchi). The T-QT Pro work in this fork keeps the original GPLv2 license and preserves attribution to the upstream projects.

## T-QT Pro port

The T-QT Pro firmware lives in `firmware/esp8266-tamagotchi` and adds:

- LilyGo T-QT Pro ESP32-S3 board support.
- GC9107/GC9A01-style 128 x 128 TFT rendering through LilyGo's `TFT_eSPI` setup.
- Two-button controls mapped to the original three Tamagotchi buttons.
- Incremental TFT rendering to reduce flicker.
- Display memory clearing/scrubbing for the random colored pixel artifacts seen after some power transitions.
- WiFi monitor portal.
- NTP time sync, including timezone capture from the browser that opens the portal.
- Read-only web monitor with LCD view and browser-side alert sound.

See [firmware/esp8266-tamagotchi/README_T_QT_PRO.md](firmware/esp8266-tamagotchi/README_T_QT_PRO.md) for build, upload, WiFi, and usage notes.

![Tamagotchi P1 Actual Devices](/images/TamaP1_devices.jpg)

## Fork notice

I did the following changes after forking [original repo](https://github.com/GaryZ88/ArduinoGotchi)
- Created a platformio project, so it is easy to target multiple platforms
- Created ports for ESP8266 and ESP32, mainly because the speed on 8-bit AVR is just too slow
- Added long click on "back" button - if you press it for 5 seconds, it will reset memory back to egg state
- Added inverted Speaker connection setting. Mainly because the Piezo modules that I have are active on Low. Another reason is mentioned below.

I personally assembled the ESP8266 version with Wemos D1 Mini on a perf board, using the built-in LED together with a speaker, so when it sounds, the LED is blinking as well.

### Demo
![Demo #1](/images/VID_20220923_205516.mp4.gif)
![Demo #2](/images/VID_20220923_205528.mp4.gif)
![Demo #3](/images/VID_20220923_205823.mp4.gif)

## How to build and run

Use Platformio. Run the `build` task to build for all platforms. Next, run the `Upload` task for a specific platform

### Additional notes
- To activate your pet, you have to configure the clock by pressing the middle button. Otherwise, your pet will not be alive.
- The emulator will save the game status every 5 minutes. You can change that by changing the AUTO_SAVE_MINUTES setting
- The speed of the emulator is a bit slower than the actual Tamagotchi device on AVR; still, it is fun. On ESPs, it runs smoothly.
- There are a few costs in the `platformio.ini` that you can adjust to fit your needs:
```
  -D DISPLAY_I2C_ADDRESS=0x3C
  -D SCREEN_WIDTH=128
  -D SCREEN_HEIGHT=64
  -D ENABLE_TAMA_SOUND
  -D ENABLE_TAMA_SOUND_ACTIVE_LOW
  -D ENABLE_AUTO_SAVE_STATUS
  -D ENABLE_LOAD_STATE_FROM_EEPROM
```

### Board revisions

#### Revision A

The first prototype used an external boost converter module soldered on the PCB for simplicity. However boost converter I used would give up too early, not pulling all the juice from the battery.
This version contained one schematic issue: GPIO2 used for the buzzer caused boot issues (bootstrap pin)

<img width="1234" height="555" alt="image" src="https://github.com/user-attachments/assets/9671521b-6a0d-4dc4-b5c5-223fa6ecf9e7" />

#### Revision B

Switched over to on-board boost converter (TPS61040DBVR). Buzzer pin changed to IO0.

<img width="1958" height="2189" alt="image" src="https://github.com/user-attachments/assets/6acdf62b-8212-46a3-83bb-c74f32ccd564" />

#### Revision C

Identical electrically to rev B, however, I switched to onboard SMD buttons (soldered from the factory), as they had much better feel compared to the through-hole buttons I used before. 

<img width="1199" height="581" alt="image" src="https://github.com/user-attachments/assets/4582ea76-ba4b-477e-9e5a-ad1c61f82d2d" />

#### Revision D

Switched to XC9140A331MR boost converter, as this one can run down to 0.9V before it dies out. Added onboard power switch, so it can be switched off without removing batteries. Switched to onboard buzzer I used in another project to make the bottom line flat (that way it can be placed on the desk, for example). 

Also, I decided to switch to ESP32-S2 from ESP8266 as they become more and more available. Also I had a bunch of them, and I didn't really know where to use them ;)

<img width="2959" height="2588" alt="image" src="https://github.com/user-attachments/assets/56465523-89e7-4949-a00a-08f461ae367f" />

### License
ArduinoGotchi is distributed under the GPLv2 license. See the LICENSE file for more information.

### Where to buy

You may support my work by ordering the kit on [Tindie](https://www.tindie.com/products/sonocotta/esp8266-tamagotchi-diy-kit/) or [Elecrow](https://www.elecrow.com/esp8266-tamagotchi-diy-kit.html)
