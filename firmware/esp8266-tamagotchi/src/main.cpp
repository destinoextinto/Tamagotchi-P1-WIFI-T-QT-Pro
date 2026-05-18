/*
 * ArduinoGotchi - A real Tamagotchi emulator for Arduino UNO
 *
 * Copyright (C) 2022 Gary Kwok
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifdef T_QT_PRO
#include <SPI.h>
#include <TFT_eSPI.h>
#ifdef T_QT_WIFI_MONITOR
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <time.h>
#endif
#else
#include <U8g2lib.h>
#include <Wire.h>
#endif

#include "cpu.h"
#include "tamalib.h"
#include "hw.h"
#include "bitmaps.h"
#include "savestate.h"

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 74880
#endif

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif

#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif

/***** Set display orientation, U8G2_MIRROR_VERTICAL is not supported *****/
//#define U8G2_LAYOUT_NORMAL
#define U8G2_LAYOUT_ROTATE_180
//#define U8G2_LAYOUT_MIRROR
/**************************************************************************/

#ifdef T_QT_PRO
TFT_eSPI display = TFT_eSPI();

#ifndef T_QT_DISPLAY_ROTATION
#define T_QT_DISPLAY_ROTATION 2
#endif

static const uint8_t TQT_TAMA_SCALE = 4;
static const uint8_t TQT_TAMA_X = (SCREEN_WIDTH - LCD_WIDTH * TQT_TAMA_SCALE) / 2;
static const uint8_t TQT_TAMA_Y = 28;
static const uint8_t TQT_ICON_Y = 108;
static const uint16_t TQT_TAMA_FG = TFT_WHITE;
static const uint16_t TQT_TAMA_BG = TFT_BLACK;

void clearTqtPanelMemory()
{
  for (uint8_t rotation = 0; rotation < 4; rotation++)
  {
    display.setRotation(rotation);
    display.fillScreen(TQT_TAMA_BG);
  }
  display.setRotation(T_QT_DISPLAY_ROTATION);
}
#else
#if defined(PIN_I2C_SDA) && defined(PIN_I2C_SCL) 
#ifdef U8G2_LAYOUT_NORMAL
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#endif
#ifdef U8G2_LAYOUT_ROTATE_180
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R2, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#endif
#ifdef U8G2_LAYOUT_MIRROR
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_MIRROR, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#endif
#else
#ifdef U8G2_LAYOUT_NORMAL
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R0);
#endif
#ifdef U8G2_LAYOUT_ROTATE_180
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R2);
#endif
#ifdef U8G2_LAYOUT_MIRROR
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_MIRROR);
#endif
#endif
#endif

void displayTama();
#ifdef T_QT_PRO
bool tamaLcdLooksFlooded();
#endif

/**** TamaLib Specific Variables ****/
static uint16_t current_freq = 0;
#if defined(T_QT_PRO) && defined(T_QT_WIFI_MONITOR)
static volatile bool tqtMonitorSoundPlaying = false;
static volatile uint32_t tqtMonitorSoundFrequency = 0;
static volatile uint32_t tqtMonitorLastSoundMs = 0;
#endif
static bool_t matrix_buffer[LCD_HEIGHT][LCD_WIDTH / 8] = {{0}};
//static byte runOnceBool = 0;
static bool_t icon_buffer[ICON_NUM] = {0};
static cpu_state_t cpuState;
static unsigned long lastSaveTimestamp = 0;
/************************************/

static void hal_halt(void)
{
  // Serial.println("Halt!");
}

static void hal_log(log_level_t level, char *buff, ...)
{
  Serial.println(buff);
}

static void hal_sleep_until(timestamp_t ts)
{
#ifdef T_QT_PRO
  int32_t remaining = (int32_t)(ts - (timestamp_t)(millis() * 1000));
  if (remaining > 0)
  {
    if (remaining >= 20000)
    {
      delay(remaining / 1000);
    }
    else
    {
      delayMicroseconds((uint32_t)remaining);
    }
  }
#else
  // int32_t remaining = (int32_t) (ts - hal_get_timestamp());
  // if (remaining > 0) {
  // delayMicroseconds(1);
  // delay(1);
  //}
#endif
}

static timestamp_t hal_get_timestamp(void)
{
  return millis() * 1000;
}

static void hal_update_screen(void)
{
  displayTama();
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
  uint8_t mask;
  if (val)
  {
    mask = 0b10000000 >> (x % 8);
    matrix_buffer[y][x / 8] = matrix_buffer[y][x / 8] | mask;
  }
  else
  {
    mask = 0b01111111;
    for (byte i = 0; i < (x % 8); i++)
    {
      mask = (mask >> 1) | 0b10000000;
    }
    matrix_buffer[y][x / 8] = matrix_buffer[y][x / 8] & mask;
  }
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
  icon_buffer[icon] = val;
}

static void hal_set_frequency(u32_t freq)
{
  current_freq = freq;
#if defined(T_QT_PRO) && defined(T_QT_WIFI_MONITOR)
  tqtMonitorSoundFrequency = freq;
#endif
}

static void hal_play_frequency(bool_t en)
{
#if defined(T_QT_PRO) && defined(T_QT_WIFI_MONITOR)
  tqtMonitorSoundPlaying = en;
  tqtMonitorLastSoundMs = millis();
#endif
#ifdef ENABLE_TAMA_SOUND
  if (en)
  {
    tone(PIN_BUZZER, current_freq);
  }
  else
  {
    noTone(PIN_BUZZER);
    #ifdef ENABLE_TAMA_SOUND_ACTIVE_LOW
    digitalWrite(PIN_BUZZER, HIGH);
    #endif
  }
#endif
}

static int hal_handler(void)
{
#ifdef ENABLE_SERIAL_DEBUG_INPUT
  if (Serial.available() > 0)
  {
    int incomingByte = Serial.read();
    Serial.println(incomingByte, DEC);
    if (incomingByte == 49)
    {
      hw_set_button(BTN_LEFT, BTN_STATE_PRESSED);
    }
    else if (incomingByte == 50)
    {
      hw_set_button(BTN_LEFT, BTN_STATE_RELEASED);
    }
    else if (incomingByte == 51)
    {
      hw_set_button(BTN_MIDDLE, BTN_STATE_PRESSED);
    }
    else if (incomingByte == 52)
    {
      hw_set_button(BTN_MIDDLE, BTN_STATE_RELEASED);
    }
    else if (incomingByte == 53)
    {
      hw_set_button(BTN_RIGHT, BTN_STATE_PRESSED);
    }
    else if (incomingByte == 54)
    {
      hw_set_button(BTN_RIGHT, BTN_STATE_RELEASED);
    }
  }
#else
#ifdef T_QT_PRO
  static uint8_t lastRawButtons = 0;
  static uint8_t stableButtons = 0;
  static uint32_t lastRawChange = 0;
  static uint32_t stableSince = 0;

  const uint8_t rawButtons =
      (digitalRead(PIN_BTN_L) == LOW ? 0x01 : 0x00) |
      (digitalRead(PIN_BTN_R) == LOW ? 0x02 : 0x00);
  const uint32_t now = millis();

  if (rawButtons != lastRawButtons)
  {
    lastRawButtons = rawButtons;
    lastRawChange = now;
  }

  if (rawButtons != stableButtons && now - lastRawChange >= 35)
  {
    stableButtons = rawButtons;
    stableSince = now;
  }

  uint8_t reportedButtons = stableButtons;
  if ((reportedButtons == 0x01 || reportedButtons == 0x02) && now - stableSince < 120)
  {
    reportedButtons = 0;
  }

  hw_set_button(BTN_LEFT, reportedButtons == 0x01 ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
  hw_set_button(BTN_MIDDLE, reportedButtons == 0x02 ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
  hw_set_button(BTN_RIGHT, reportedButtons == 0x03 ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
#else
  if (digitalRead(PIN_BTN_L) == HIGH)
  {
    hw_set_button(BTN_LEFT, BTN_STATE_PRESSED);
  }
  else
  {
    hw_set_button(BTN_LEFT, BTN_STATE_RELEASED);
  }
  if (digitalRead(PIN_BTN_M) == HIGH)
  {
    hw_set_button(BTN_MIDDLE, BTN_STATE_PRESSED);
  }
  else
  {
    hw_set_button(BTN_MIDDLE, BTN_STATE_RELEASED);
  }
  if (digitalRead(PIN_BTN_R) == HIGH)
  {
    hw_set_button(BTN_RIGHT, BTN_STATE_PRESSED);
  }
  else
  {
    hw_set_button(BTN_RIGHT, BTN_STATE_RELEASED);
  }
#endif
// #ifdef ENABLE_AUTO_SAVE_STATUS
//   static bool_t button4state = 0;
//   if (digitalRead(PIN_BTN_SAVE) == HIGH)
//   {
//     if (button4state == 0)
//     {
//       saveStateToEEPROM(&cpuState);
//     }
//     button4state = 1;
//   }
//   else
//   {
//     button4state = 0;
//   }
// #endif
#endif
  return 0;
}

static hal_t hal = {
    .halt = &hal_halt,
    .log = &hal_log,
    .sleep_until = &hal_sleep_until,
    .get_timestamp = &hal_get_timestamp,
    .update_screen = &hal_update_screen,
    .set_lcd_matrix = &hal_set_lcd_matrix,
    .set_lcd_icon = &hal_set_lcd_icon,
    .set_frequency = &hal_set_frequency,
    .play_frequency = &hal_play_frequency,
    .handler = &hal_handler,
};

#if defined(T_QT_PRO) && defined(T_QT_WIFI_MONITOR)
#ifndef TQT_WIFI_HOSTNAME
#define TQT_WIFI_HOSTNAME "tqt-tamagotchi"
#endif

#ifndef TQT_WIFI_AP_PASSWORD
#define TQT_WIFI_AP_PASSWORD "tamaqt123"
#endif

#ifndef TQT_NTP_TZ
#define TQT_NTP_TZ "CST6"
#endif

#ifndef TQT_WEB_SERVICE_INTERVAL_MS
#define TQT_WEB_SERVICE_INTERVAL_MS 50
#endif

#ifndef TQT_WIFI_STATUS_INTERVAL_MS
#define TQT_WIFI_STATUS_INTERVAL_MS 2000
#endif

#ifndef TQT_NTP_CHECK_INTERVAL_MS
#define TQT_NTP_CHECK_INTERVAL_MS 60000
#endif

static WebServer tqtWebServer(80);
static Preferences tqtWifiPrefs;
static String tqtWifiSsid;
static String tqtTimeZonePosix = TQT_NTP_TZ;
static String tqtTimeZoneName = "Mexico City";
static bool tqtWifiConnected = false;
static bool tqtClockSynced = false;
static bool tqtClockSavedAfterSync = false;
static bool tqtClockSavePending = false;
static uint32_t tqtClockSaveAtMs = 0;
static uint32_t tqtLastClockSyncMs = 0;
static uint32_t tqtLastClockAttemptMs = 0;
static bool tqtRestartPending = false;
static uint32_t tqtRestartAtMs = 0;

static const char TQT_MONITOR_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>T-QT Tamagotchi</title>
<style>
:root{color-scheme:dark;--bg:#0b0c0f;--panel:#171a20;--line:#303640;--text:#f2f4f8;--muted:#a7b0bd;--ok:#55d68b;--warn:#f2c94c}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,sans-serif}
main{width:min(760px,100%);margin:0 auto;padding:18px;display:grid;gap:14px}
.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
h1{font-size:22px;margin:0;font-weight:700}button,input{font:inherit}
canvas{width:100%;max-width:420px;aspect-ratio:1;background:#000;border:1px solid var(--line);image-rendering:pixelated}
.screen{display:grid;place-items:center;background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px}
.label{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.06em}.value{font-size:18px;margin-top:3px;word-break:break-word}
.ok{color:var(--ok)}.warn{color:var(--warn)}form{display:grid;gap:8px}
input{width:100%;border:1px solid var(--line);border-radius:6px;padding:9px;background:#0f1116;color:var(--text)}
button{border:1px solid var(--line);border-radius:6px;padding:9px 11px;background:#232833;color:var(--text);cursor:pointer}
button:hover{border-color:#687182}.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1}
.small{color:var(--muted);font-size:13px}.pill{border:1px solid var(--line);border-radius:999px;padding:5px 9px;color:var(--muted)}
</style>
</head>
<body>
<main>
  <div class="top">
    <h1>T-QT Tamagotchi</h1>
    <button id="audio">Activar sonido</button>
  </div>
  <section class="screen"><canvas id="lcd" width="256" height="256"></canvas></section>
  <section class="grid">
    <div class="card"><div class="label">Red</div><div id="net" class="value">...</div></div>
    <div class="card"><div class="label">IP</div><div id="ip" class="value">...</div></div>
    <div class="card"><div class="label">Hora Tama</div><div id="tamaTime" class="value">--:--:--</div></div>
    <div class="card"><div class="label">NTP</div><div id="ntp" class="value">...</div></div>
    <div class="card"><div class="label">Zona</div><div id="tz" class="value">...</div></div>
  </section>
  <section class="card">
    <div class="label">Configurar WiFi</div>
    <form method="post" action="/wifi">
      <input name="ssid" placeholder="SSID" autocomplete="off">
      <input name="pass" placeholder="Password" type="password">
      <div class="row">
        <button type="submit">Guardar red</button>
      </div>
    </form>
    <form method="post" action="/wifi/clear" onsubmit="return confirm('Borrar red guardada?')">
      <button type="submit">Borrar red guardada</button>
    </form>
    <p class="small">El monitor no envia botones al Tamagotchi; solo muestra pantalla, estado y alerta sonora en este navegador.</p>
  </section>
</main>
<script>
const lcd=document.getElementById('lcd');
const c=lcd.getContext('2d');
const iconNames=['FOOD','LIGHT','GAME','MED','WC','STAT','DISC','ATTN'];
let audioEnabled=false, audioCtx=null, osc=null, gain=null;
let sentTimezone=false;
document.getElementById('audio').addEventListener('click',async()=>{
  audioCtx=audioCtx||new (window.AudioContext||window.webkitAudioContext)();
  await audioCtx.resume();
  audioEnabled=true;
  document.getElementById('audio').textContent='Sonido activo';
});
function setAlarm(on,freq){
  if(!audioEnabled||!audioCtx)return;
  if(on&& !osc){
    osc=audioCtx.createOscillator();
    gain=audioCtx.createGain();
    osc.type='square';
    osc.frequency.value=freq||880;
    gain.gain.value=0.04;
    osc.connect(gain).connect(audioCtx.destination);
    osc.start();
  }else if(on&&osc){
    osc.frequency.value=freq||880;
  }else if(!on&&osc){
    osc.stop();
    osc.disconnect();
    gain.disconnect();
    osc=null;gain=null;
  }
}
function rowBit(hex,x){
  const b=parseInt(hex.slice((x>>3)*2,(x>>3)*2+2),16);
  return (b&(0x80>>(x&7)))!==0;
}
function draw(s){
  c.fillStyle='#000';c.fillRect(0,0,256,256);
  const scale=8, ox=0, oy=56;
  c.fillStyle='#fff';
  for(let y=0;y<s.height;y++){
    for(let x=0;x<s.width;x++){
      if(rowBit(s.rows[y],x)) c.fillRect(ox+x*scale,oy+y*scale,scale,scale);
    }
  }
  c.font='10px system-ui,sans-serif';
  c.textAlign='center';
  c.textBaseline='top';
  for(let i=0;i<8;i++){
    const active=(s.icons&(1<<i))!==0;
    const x=i*32+16;
    c.fillStyle=active?'#fff':'#3a3f46';
    if(active){c.beginPath();c.moveTo(x,190);c.lineTo(x-5,198);c.lineTo(x+5,198);c.fill();}
    c.fillText(iconNames[i],x,210);
  }
}
async function sendBrowserTimezone(){
  if(sentTimezone)return;
  sentTimezone=true;
  const body=new URLSearchParams();
  body.set('offset',String(new Date().getTimezoneOffset()));
  body.set('name',Intl.DateTimeFormat().resolvedOptions().timeZone||'Local');
  try{await fetch('/timezone',{method:'POST',body});}catch(e){}
}
async function tick(){
  try{
    sendBrowserTimezone();
    const s=await fetch('/api/state',{cache:'no-store'}).then(r=>r.json());
    draw(s.screen);
    document.getElementById('net').innerHTML=s.wifi.connected?'<span class="ok">'+s.wifi.ssid+'</span>':'<span class="warn">'+s.wifi.mode+'</span>';
    document.getElementById('ip').textContent=s.wifi.ip||s.wifi.ap_ip||'sin IP';
    document.getElementById('tamaTime').textContent=s.tama_time;
    document.getElementById('ntp').innerHTML=s.ntp.synced?'<span class="ok">'+s.ntp.local+'</span>':'<span class="warn">sin sincronizar</span>';
    document.getElementById('tz').textContent=s.ntp.zone||'local';
    setAlarm(s.alert,s.sound.frequency);
  }catch(e){
    setAlarm(false,0);
  }
  setTimeout(tick,1000);
}
tick();
</script>
</body>
</html>
)rawliteral";

static void appendJsonEscaped(String &json, const String &value)
{
  json += '"';
  for (uint16_t i = 0; i < value.length(); i++)
  {
    char c = value[i];
    if (c == '"' || c == '\\')
    {
      json += '\\';
      json += c;
    }
    else if ((uint8_t)c < 0x20)
    {
      json += ' ';
    }
    else
    {
      json += c;
    }
  }
  json += '"';
}

static uint8_t readTamaDecimal2(uint16_t addr)
{
  return cpu_read_memory_nibble(addr) + cpu_read_memory_nibble(addr + 1) * 10;
}

static uint8_t readTamaHex2(uint16_t addr)
{
  return cpu_read_memory_nibble(addr) | (cpu_read_memory_nibble(addr + 1) << 4);
}

static void writeTamaDecimal2(uint16_t addr, uint8_t value)
{
  if (value > 99)
    value = 99;
  cpu_write_memory_nibble(addr, value % 10);
  cpu_write_memory_nibble(addr + 1, value / 10);
}

static void writeTamaHex2(uint16_t addr, uint8_t value)
{
  cpu_write_memory_nibble(addr, value & 0x0F);
  cpu_write_memory_nibble(addr + 1, (value >> 4) & 0x0F);
}

static String buildFixedPosixTimezoneFromBrowserOffset(int16_t browserOffsetMinutes)
{
  // JavaScript returns UTC - local time. POSIX TZ uses the same sign convention.
  int16_t minutes = browserOffsetMinutes;
  char sign = '\0';
  if (minutes < 0)
  {
    sign = '-';
    minutes = -minutes;
  }

  char buffer[20];
  if ((minutes % 60) == 0)
  {
    if (sign)
      snprintf(buffer, sizeof(buffer), "TQT%c%d", sign, minutes / 60);
    else
      snprintf(buffer, sizeof(buffer), "TQT%d", minutes / 60);
  }
  else
  {
    if (sign)
      snprintf(buffer, sizeof(buffer), "TQT%c%d:%02d", sign, minutes / 60, minutes % 60);
    else
      snprintf(buffer, sizeof(buffer), "TQT%d:%02d", minutes / 60, minutes % 60);
  }
  return String(buffer);
}

static void configureTqtNtp()
{
  tqtClockSynced = false;
  tqtClockSavedAfterSync = false;
  tqtClockSavePending = false;
  tqtLastClockAttemptMs = 0;
  tqtLastClockSyncMs = 0;
  configTzTime(tqtTimeZonePosix.c_str(), "pool.ntp.org", "time.nist.gov");
}

static String getTamaTimeString()
{
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u",
           readTamaHex2(0x014), readTamaDecimal2(0x012), readTamaDecimal2(0x010));
  return String(buffer);
}

static bool getLocalTimeString(String &value)
{
  time_t now = time(nullptr);
  if (now < 1700000000)
    return false;

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  value = buffer;
  return true;
}

static void syncTamaClockFromNtp(bool force)
{
  uint32_t nowMs = millis();
  if (WiFi.status() != WL_CONNECTED)
    return;

  const uint32_t retryInterval = tqtClockSynced ? 10UL * 60UL * 1000UL : 3000UL;
  if (!force && tqtLastClockAttemptMs != 0 && nowMs - tqtLastClockAttemptMs < retryInterval)
    return;

  tqtLastClockAttemptMs = nowMs;
  time_t now = time(nullptr);
  if (now < 1700000000)
    return;

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  bool clockWasInvalid = cpu_read_memory_nibble(0x03C) != 0x0F;
  bool finishWasPending = cpu_read_memory_nibble(0x03F) != 0x0F;
  writeTamaDecimal2(0x010, timeinfo.tm_sec);
  writeTamaDecimal2(0x012, timeinfo.tm_min);
  writeTamaHex2(0x014, timeinfo.tm_hour);
  cpu_write_memory_nibble(0x02E, 0x0F);
  cpu_write_memory_nibble(0x03C, 0x0F);
  cpu_write_memory_nibble(0x03F, 0x0F);

  bool shouldSave = !tqtClockSavedAfterSync && (clockWasInvalid || finishWasPending);
  tqtClockSynced = true;
  tqtLastClockSyncMs = nowMs;

  if (shouldSave)
  {
    tqtClockSavePending = true;
    tqtClockSaveAtMs = nowMs + 5000;
    tqtClockSavedAfterSync = true;
  }
}

static void appendMatrixRows(String &json)
{
  bool flooded = tamaLcdLooksFlooded();
  char hex[3];
  json += F("\"rows\":[");
  for (uint8_t y = 0; y < LCD_HEIGHT; y++)
  {
    if (y)
      json += ',';
    json += '"';
    for (uint8_t xByte = 0; xByte < LCD_WIDTH / 8; xByte++)
    {
      uint8_t value = flooded ? 0x00 : matrix_buffer[y][xByte];
      snprintf(hex, sizeof(hex), "%02X", value);
      json += hex;
    }
    json += '"';
  }
  json += ']';
}

static uint16_t getIconMask()
{
  uint16_t mask = 0;
  for (uint8_t i = 0; i < ICON_NUM; i++)
  {
    if (icon_buffer[i])
      mask |= (1U << i);
  }
  return mask;
}

static void handleTqtRoot()
{
  tqtWebServer.send_P(200, "text/html", TQT_MONITOR_HTML);
}

static void handleTqtState()
{
  String localTime;
  bool ntpReady = getLocalTimeString(localTime);
  bool connected = WiFi.status() == WL_CONNECTED;
  bool attention = icon_buffer[ICON_NUM - 1] != 0;
  uint32_t nowMs = millis();
  bool recentSound = tqtMonitorSoundPlaying || (tqtMonitorLastSoundMs != 0 && nowMs - tqtMonitorLastSoundMs < 1500);

  String json;
  json.reserve(1600);
  json += '{';
  json += F("\"wifi\":{\"connected\":");
  json += connected ? F("true") : F("false");
  json += F(",\"mode\":");
  appendJsonEscaped(json, connected ? String("STA") : String("AP"));
  json += F(",\"ssid\":");
  appendJsonEscaped(json, connected ? WiFi.SSID() : tqtWifiSsid);
  json += F(",\"ip\":");
  appendJsonEscaped(json, connected ? WiFi.localIP().toString() : String(""));
  json += F(",\"ap_ip\":");
  appendJsonEscaped(json, WiFi.softAPIP().toString());
  json += F("},\"ntp\":{\"synced\":");
  json += (tqtClockSynced && ntpReady) ? F("true") : F("false");
  json += F(",\"local\":");
  appendJsonEscaped(json, ntpReady ? localTime : String(""));
  json += F(",\"zone\":");
  appendJsonEscaped(json, tqtTimeZoneName);
  json += F(",\"posix\":");
  appendJsonEscaped(json, tqtTimeZonePosix);
  json += F("},\"tama_time\":");
  appendJsonEscaped(json, getTamaTimeString());
  json += F(",\"sound\":{\"playing\":");
  json += tqtMonitorSoundPlaying ? F("true") : F("false");
  json += F(",\"frequency\":");
  json += (uint32_t)(tqtMonitorSoundFrequency ? tqtMonitorSoundFrequency : current_freq);
  json += F("},\"alert\":");
  json += (attention || recentSound) ? F("true") : F("false");
  json += F(",\"screen\":{\"width\":");
  json += LCD_WIDTH;
  json += F(",\"height\":");
  json += LCD_HEIGHT;
  json += ',';
  appendMatrixRows(json);
  json += F(",\"icons\":");
  json += getIconMask();
  json += F("},\"uptime_ms\":");
  json += nowMs;
  json += '}';

  tqtWebServer.send(200, "application/json", json);
}

static void scheduleTqtRestart()
{
  tqtRestartPending = true;
  tqtRestartAtMs = millis() + 1200;
}

static void handleTqtWifiSave()
{
  String ssid = tqtWebServer.arg("ssid");
  String pass = tqtWebServer.arg("pass");
  ssid.trim();

  if (ssid.length() == 0)
  {
    tqtWebServer.send(400, "text/plain", "SSID requerido");
    return;
  }

  tqtWifiPrefs.begin("tqt_net", false);
  tqtWifiPrefs.putString("ssid", ssid);
  tqtWifiPrefs.putString("pass", pass);
  tqtWifiPrefs.end();

  saveStateToEEPROM(&cpuState);
  tqtWebServer.send(200, "text/html", "<meta name='viewport' content='width=device-width,initial-scale=1'><p>WiFi guardado. Reiniciando...</p>");
  scheduleTqtRestart();
}

static void handleTqtWifiClear()
{
  tqtWifiPrefs.begin("tqt_net", false);
  tqtWifiPrefs.remove("ssid");
  tqtWifiPrefs.remove("pass");
  tqtWifiPrefs.end();

  saveStateToEEPROM(&cpuState);
  tqtWebServer.send(200, "text/html", "<meta name='viewport' content='width=device-width,initial-scale=1'><p>WiFi borrado. Reiniciando en modo AP...</p>");
  scheduleTqtRestart();
}

static void handleTqtTimezone()
{
  if (!tqtWebServer.hasArg("offset"))
  {
    tqtWebServer.send(400, "text/plain", "offset requerido");
    return;
  }

  int16_t offset = (int16_t)tqtWebServer.arg("offset").toInt();
  if (offset < -14 * 60 || offset > 14 * 60)
  {
    tqtWebServer.send(400, "text/plain", "offset invalido");
    return;
  }

  String name = tqtWebServer.arg("name");
  name.trim();
  if (name.length() == 0)
    name = "Local";
  if (name.length() > 48)
    name = name.substring(0, 48);

  String posix = buildFixedPosixTimezoneFromBrowserOffset(offset);
  bool changed = posix != tqtTimeZonePosix || name != tqtTimeZoneName;
  tqtTimeZonePosix = posix;
  tqtTimeZoneName = name;

  if (changed)
  {
    tqtWifiPrefs.begin("tqt_net", false);
    tqtWifiPrefs.putString("tz", tqtTimeZonePosix);
    tqtWifiPrefs.putString("tzname", tqtTimeZoneName);
    tqtWifiPrefs.end();
    if (WiFi.status() == WL_CONNECTED)
      configureTqtNtp();
  }

  syncTamaClockFromNtp(true);
  tqtWebServer.send(200, "text/plain", "OK");
}

static void startTqtAccessPoint()
{
  uint64_t chipId = ESP.getEfuseMac();
  char apName[24];
  snprintf(apName, sizeof(apName), "TamaQT-%04X", (uint16_t)(chipId & 0xFFFF));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName, TQT_WIFI_AP_PASSWORD);
  tqtWifiSsid = apName;
  tqtWifiConnected = false;
  Serial.print(F("WiFi AP: "));
  Serial.println(apName);
  Serial.print(F("AP URL: http://"));
  Serial.println(WiFi.softAPIP());
}

static void startTqtWifiMonitor()
{
  tqtWifiPrefs.begin("tqt_net", true);
  tqtWifiSsid = tqtWifiPrefs.getString("ssid", "");
  String pass = tqtWifiPrefs.getString("pass", "");
  tqtTimeZonePosix = tqtWifiPrefs.getString("tz", TQT_NTP_TZ);
  tqtTimeZoneName = tqtWifiPrefs.getString("tzname", "Mexico City");
  tqtWifiPrefs.end();

  WiFi.setHostname(TQT_WIFI_HOSTNAME);
  if (tqtWifiSsid.length() > 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(tqtWifiSsid.c_str(), pass.c_str());
    Serial.print(F("Connecting WiFi: "));
    Serial.println(tqtWifiSsid);

    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 8000)
    {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    tqtWifiConnected = true;
    configureTqtNtp();
    if (MDNS.begin(TQT_WIFI_HOSTNAME))
    {
      MDNS.addService("http", "tcp", 80);
    }
    Serial.print(F("WiFi URL: http://"));
    Serial.println(WiFi.localIP());
    Serial.print(F("mDNS URL: http://"));
    Serial.print(TQT_WIFI_HOSTNAME);
    Serial.println(F(".local"));
  }
  else
  {
    startTqtAccessPoint();
  }

  tqtWebServer.on("/", HTTP_GET, handleTqtRoot);
  tqtWebServer.on("/api/state", HTTP_GET, handleTqtState);
  tqtWebServer.on("/wifi", HTTP_POST, handleTqtWifiSave);
  tqtWebServer.on("/wifi/clear", HTTP_POST, handleTqtWifiClear);
  tqtWebServer.on("/timezone", HTTP_POST, handleTqtTimezone);
  tqtWebServer.onNotFound(handleTqtRoot);
  tqtWebServer.begin();
}

static void handleTqtWifiMonitor()
{
  uint32_t nowMs = millis();
  static uint32_t lastWebServiceMs = 0;
  static uint32_t lastWifiStatusMs = 0;

  if (tqtRestartPending && (int32_t)(nowMs - tqtRestartAtMs) >= 0)
  {
    ESP.restart();
  }

  if (nowMs - lastWebServiceMs >= TQT_WEB_SERVICE_INTERVAL_MS)
  {
    lastWebServiceMs = nowMs;
    tqtWebServer.handleClient();
  }

  if (nowMs - lastWifiStatusMs < TQT_WIFI_STATUS_INTERVAL_MS)
    return;
  lastWifiStatusMs = nowMs;

  if (tqtClockSavePending && (int32_t)(nowMs - tqtClockSaveAtMs) >= 0)
  {
    saveStateToEEPROM(&cpuState);
    tqtClockSavePending = false;
  }

  wl_status_t status = WiFi.status();
  if (tqtWifiConnected && status != WL_CONNECTED)
  {
    tqtWifiConnected = false;
    Serial.println(F("WiFi disconnected."));
  }
  if (!tqtWifiConnected && status == WL_CONNECTED)
  {
    tqtWifiConnected = true;
    WiFi.setSleep(false);
    configureTqtNtp();
    Serial.print(F("WiFi reconnected: http://"));
    Serial.println(WiFi.localIP());
  }

  syncTamaClockFromNtp(false);
}
#endif

#ifdef T_QT_PRO
void drawTriangle(uint8_t x, uint8_t y)
{
  display.drawLine(x + 1, y + 1, x + 5, y + 1, TQT_TAMA_FG);
  display.drawLine(x + 2, y + 2, x + 4, y + 2, TQT_TAMA_FG);
  display.drawPixel(x + 3, y + 3, TQT_TAMA_FG);
}

void drawTamaRow(uint8_t tamaLCD_y, uint8_t ActualLCD_y, uint8_t thick)
{
  (void)ActualLCD_y;
  (void)thick;

  for (uint8_t i = 0; i < LCD_WIDTH; i++)
  {
    uint8_t mask = 0b10000000 >> (i % 8);
    if ((matrix_buffer[tamaLCD_y][i / 8] & mask) != 0)
    {
      display.fillRect(
          TQT_TAMA_X + i * TQT_TAMA_SCALE,
          TQT_TAMA_Y + tamaLCD_y * TQT_TAMA_SCALE,
          TQT_TAMA_SCALE,
          TQT_TAMA_SCALE,
          TQT_TAMA_FG);
    }
  }
}

void drawTamaSelection(uint8_t y)
{
  for (uint8_t i = 0; i < ICON_NUM; i++)
  {
    if (icon_buffer[i])
      drawTriangle(i * 16 + 5, y);
    display.drawXBitmap(i * 16 + 4, y + 6, bitmaps + i * 18, 16, 9, TQT_TAMA_FG);
  }
}

bool tamaLcdLooksFlooded()
{
  uint16_t activePixels = 0;
  for (uint8_t y = 0; y < LCD_HEIGHT; y++)
  {
    for (uint8_t x = 0; x < LCD_WIDTH / 8; x++)
    {
      uint8_t rowByte = matrix_buffer[y][x];
      for (uint8_t bit = 0; bit < 8; bit++)
      {
        activePixels += (rowByte >> bit) & 0x01;
      }
    }
  }

  return activePixels > ((LCD_WIDTH * LCD_HEIGHT) * 3 / 4);
}

void displayTama()
{
  static uint8_t renderedMatrix[LCD_HEIGHT][LCD_WIDTH / 8];
  static bool_t renderedIcons[ICON_NUM];
  static bool rendererStarted = false;
  static uint8_t scrubStep = 0;

  if (!rendererStarted)
  {
    display.fillScreen(TQT_TAMA_BG);
    for (uint8_t y = 0; y < LCD_HEIGHT; y++)
    {
      for (uint8_t x = 0; x < LCD_WIDTH / 8; x++)
      {
        renderedMatrix[y][x] = 0xFF;
      }
    }
    for (uint8_t i = 0; i < ICON_NUM; i++)
    {
      renderedIcons[i] = !icon_buffer[i];
    }
  }

  bool flooded = tamaLcdLooksFlooded();
  display.startWrite();
  for (uint8_t y = 0; y < LCD_HEIGHT; y++)
  {
    for (uint8_t xByte = 0; xByte < LCD_WIDTH / 8; xByte++)
    {
      uint8_t currentByte = flooded ? 0x00 : matrix_buffer[y][xByte];
      uint8_t previousByte = renderedMatrix[y][xByte];

      if (currentByte == previousByte)
        continue;

      for (uint8_t bit = 0; bit < 8; bit++)
      {
        uint8_t mask = 0b10000000 >> bit;
        bool currentOn = (currentByte & mask) != 0;
        bool previousOn = (previousByte & mask) != 0;

        if (currentOn != previousOn)
        {
          display.fillRect(
              TQT_TAMA_X + (xByte * 8 + bit) * TQT_TAMA_SCALE,
              TQT_TAMA_Y + y * TQT_TAMA_SCALE,
              TQT_TAMA_SCALE,
              TQT_TAMA_SCALE,
              currentOn ? TQT_TAMA_FG : TQT_TAMA_BG);
        }
      }

      renderedMatrix[y][xByte] = currentByte;
    }
  }

  if (rendererStarted)
  {
    if (scrubStep < LCD_HEIGHT)
    {
      uint8_t y = scrubStep;
      for (uint8_t xByte = 0; xByte < LCD_WIDTH / 8; xByte++)
      {
        uint8_t currentByte = flooded ? 0x00 : matrix_buffer[y][xByte];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
          uint8_t mask = 0b10000000 >> bit;
          bool currentOn = (currentByte & mask) != 0;
          display.fillRect(
              TQT_TAMA_X + (xByte * 8 + bit) * TQT_TAMA_SCALE,
              TQT_TAMA_Y + y * TQT_TAMA_SCALE,
              TQT_TAMA_SCALE,
              TQT_TAMA_SCALE,
              currentOn ? TQT_TAMA_FG : TQT_TAMA_BG);
        }
        renderedMatrix[y][xByte] = currentByte;
      }
    }
    else if (scrubStep == LCD_HEIGHT)
    {
      display.fillRect(0, 0, SCREEN_WIDTH, TQT_TAMA_Y, TQT_TAMA_BG);
    }
    else if (scrubStep == LCD_HEIGHT + 1)
    {
      display.fillRect(0, TQT_TAMA_Y + LCD_HEIGHT * TQT_TAMA_SCALE,
                       SCREEN_WIDTH,
                       TQT_ICON_Y - (TQT_TAMA_Y + LCD_HEIGHT * TQT_TAMA_SCALE),
                       TQT_TAMA_BG);
    }
  }
  display.endWrite();

  scrubStep++;
  if (scrubStep > LCD_HEIGHT + 2)
  {
    scrubStep = 0;
  }

  bool iconsChanged = !rendererStarted || scrubStep == LCD_HEIGHT + 2;
  for (uint8_t i = 0; i < ICON_NUM; i++)
  {
    if (renderedIcons[i] != icon_buffer[i])
    {
      iconsChanged = true;
      renderedIcons[i] = icon_buffer[i];
    }
  }

  if (iconsChanged)
  {
    display.fillRect(0, TQT_ICON_Y, SCREEN_WIDTH, SCREEN_HEIGHT - TQT_ICON_Y, TQT_TAMA_BG);
    drawTamaSelection(TQT_ICON_Y);
  }

  rendererStarted = true;
}
#else
void drawTriangle(uint8_t x, uint8_t y)
{
  // display.drawLine(x,y,x+6,y);
  display.drawLine(x + 1, y + 1, x + 5, y + 1);
  display.drawLine(x + 2, y + 2, x + 4, y + 2);
  display.drawLine(x + 3, y + 3, x + 3, y + 3);
}

void drawTamaRow(uint8_t tamaLCD_y, uint8_t ActualLCD_y, uint8_t thick)
{
  uint8_t i;
  for (i = 0; i < LCD_WIDTH; i++)
  {
    uint8_t mask = 0b10000000;
    mask = mask >> (i % 8);
    if ((matrix_buffer[tamaLCD_y][i / 8] & mask) != 0)
    {
      display.drawBox(i + i + i + 16, ActualLCD_y, 2, thick);
    }
  }
}

void drawTamaSelection(uint8_t y)
{
  uint8_t i;
  for (i = 0; i < 7; i++)
  {
    if (icon_buffer[i])
      drawTriangle(i * 16 + 5, y);
    display.drawXBMP(i * 16 + 4, y + 6, 16, 9, bitmaps + i * 18);
  }
  if (icon_buffer[7])
  {
    drawTriangle(7 * 16 + 5, y);
    display.drawXBMP(7 * 16 + 4, y + 6, 16, 9, bitmaps + 7 * 18);
  }
}

void displayTama()
{
  uint8_t j;
  display.firstPage();
#ifdef U8G2_LAYOUT_ROTATE_180
  drawTamaSelection(49);
  display.nextPage();

  for (j = 11; j < LCD_HEIGHT; j++)
  {
    drawTamaRow(j, j + j + j, 2);
  }
  display.nextPage();

  for (j = 5; j <= 10; j++)
  {
    if (j == 5)
    {
      drawTamaRow(j, j + j + j + 1, 1);
    }
    else
    {
      drawTamaRow(j, j + j + j, 2);
    }
  }
  display.nextPage();

  for (j = 0; j <= 5; j++)
  {
    if (j == 5)
    {
      drawTamaRow(j, j + j + j, 1);
    }
    else
    {
      drawTamaRow(j, j + j + j, 2);
    }
  }
  display.nextPage();
#else
  for (j = 0; j < LCD_HEIGHT; j++)
  {
    if (j != 5)
      drawTamaRow(j, j + j + j, 2);
    if (j == 5)
    {
      drawTamaRow(j, j + j + j, 1);
      display.nextPage();
      drawTamaRow(j, j + j + j + 1, 1);
    }
    if (j == 10)
      display.nextPage();
  }
  display.nextPage();
  drawTamaSelection(49);
  display.nextPage();
#endif
}
#endif

#ifdef ENABLE_DUMP_STATE_TO_SERIAL_WHEN_START
void dumpStateToSerial()
{
  uint16_t i, count = 0;
  char tmp[10];
  cpu_get_state(&cpuState);
  u4_t *memTemp = cpuState.memory;
  uint8_t *cpuS = (uint8_t *)&cpuState;

  Serial.println("");
  Serial.println("static const uint8_t hardcodedState[] PROGMEM = {");
  for (i = 0; i < sizeof(cpu_state_t); i++, count++)
  {
    sprintf(tmp, "0x%02X,", cpuS[i]);
    Serial.print(tmp);
    if ((count % 16) == 15)
      Serial.println("");
  }
  for (i = 0; i < MEMORY_SIZE; i++, count++)
  {
    sprintf(tmp, "0x%02X,", memTemp[i]);
    Serial.print(tmp);
    if ((count % 16) == 15)
      Serial.println("");
  }
  Serial.println("};");
  /*
    Serial.println("");
    Serial.println("static const uint8_t bitmaps[] PROGMEM = {");
    for(i=0;i<144;i++) {
      sprintf(tmp, "0x%02X,", bitmaps_raw[i]);
      Serial.print(tmp);
      if ((i % 18)==17) Serial.println("");
    }
    Serial.println("};");  */
}
#endif

uint8_t reverseBits(uint8_t num)
{
  uint8_t reverse_num = 0;
  uint8_t i;
  for (i = 0; i < 8; i++)
  {
    if ((num & (1 << i)))
      reverse_num |= 1 << ((8 - 1) - i);
  }
  return reverse_num;
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  Serial.println(F("\nStarting Tamagotchi..."));

#ifdef T_QT_PRO
  pinMode(PIN_BTN_L, INPUT_PULLUP);
  pinMode(PIN_BTN_R, INPUT_PULLUP);
#else
  pinMode(PIN_BTN_L, INPUT);
  pinMode(PIN_BTN_M, INPUT);
  pinMode(PIN_BTN_R, INPUT);
#endif
#ifdef PIN_BUZZER
  pinMode(PIN_BUZZER, OUTPUT);
#endif

  Serial.println(F("Initializing display..."));
#ifdef T_QT_PRO
  display.begin();
  clearTqtPanelMemory();
#else
  display.begin();
#endif

  tamalib_register_hal(&hal);
  tamalib_set_framerate(TAMA_DISPLAY_FRAMERATE);
  tamalib_init(1000000);

  initEEPROM();

#ifdef ENABLE_LOAD_STATE_FROM_EEPROM
  if (validEEPROM())
  {
    loadStateFromEEPROM(&cpuState);
  } else {
    Serial.println(F("No magic number in state, skipping state restore"));
  }
#elif ENABLE_LOAD_HARCODED_STATE_WHEN_START
  loadHardcodedState();
#endif

#ifdef ENABLE_DUMP_STATE_TO_SERIAL_WHEN_START
  dumpStateToSerial();
#endif
#if defined(T_QT_PRO) && defined(T_QT_WIFI_MONITOR)
  startTqtWifiMonitor();
  syncTamaClockFromNtp(true);
#endif
  Serial.println(F("Tamagotchi initialized."));
}

uint32_t right_long_press_started = 0;

void loop()
{
  tamalib_mainloop_step_by_step();
#if defined(T_QT_PRO) && defined(T_QT_WIFI_MONITOR)
  handleTqtWifiMonitor();
#endif
#ifdef ENABLE_AUTO_SAVE_STATUS
  if ((millis() - lastSaveTimestamp) > (AUTO_SAVE_MINUTES * 60 * 1000))
  {
    lastSaveTimestamp = millis();
    saveStateToEEPROM(&cpuState);
  }

  bool eraseButtonsPressed = false;
#ifdef T_QT_PRO
  eraseButtonsPressed = digitalRead(PIN_BTN_L) == LOW && digitalRead(PIN_BTN_R) == LOW;
#elif defined(PIN_BTN_M)
  eraseButtonsPressed = digitalRead(PIN_BTN_M) == HIGH;
#endif

  if (eraseButtonsPressed) {
    if (millis() - right_long_press_started > AUTO_SAVE_MINUTES * 1000) 
    {
      eraseStateFromEEPROM();
      #if defined(ESP8266) || defined(ESP32)
      ESP.restart();
      #endif
    }
  } else {
    right_long_press_started = millis();
  }
#endif
}
