#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"
#include "driver/adc.h"
#include <math.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <WebServer.h>

WebServer server(80);

const char* ssid         = "MyOptimum df858f";
const char* password     = "24-rose-3111";
// add your own discord webhook 
const char* discordWebhook = "https://discord.com/api/webhooks/";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset = -18000;
const int   dstOffset = 3600;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define JOY_X    ADC1_CHANNEL_4
#define JOY_Y    ADC1_CHANNEL_5
#define JOY_BTN  15
#define JOY_HIGH 3000
#define JOY_LOW  1000

#define I2S_BCLK 12
#define I2S_LRC  14
#define I2S_DOUT 13

#define SAMPLE_RATE  44100
#define BEEP_FREQ    1000
#define BEEP_ON_MS   400
#define BEEP_OFF_MS  200
#define VOLUME       0.5

#define STOP_BTN 27

#define SCREEN_CLOCK        0
#define SCREEN_MENU         1
#define SCREEN_ALARM_LIST   2
#define SCREEN_SET_ALARM    3
#define SCREEN_ALARM_ON     4

int currentScreen = SCREEN_CLOCK;
int menuIndex = 0;
const int MENU_ITEMS = 2;
String menuOptions[] = {"Clock", "Alarms"};

int clockH = 0, clockM = 0, clockS = 0;

#define MAX_ALARMS 5

struct Alarm {
  int h;
  int m;
  bool active;
};

Alarm alarms[MAX_ALARMS] = {
  {7, 0, false},
  {8, 0, false},
  {9, 0, false},
  {10, 0, false},
  {11, 0, false}
};

int alarmListIndex = 0;
int editingAlarm = 0;
int editField = 0;
bool alarmRinging = false;
int ringingAlarmIdx = -1;

unsigned long lastJoyMove = 0;
#define JOY_DEBOUNCE 200
unsigned long lastBtnPress = 0;
#define BTN_DEBOUNCE 300

static const unsigned char PROGMEM cat_bits[] = {
  0x02,0x02,0x00,0x05,0x05,0x00,0x08,0xf8,0x80,0x08,0xa8,0x80,
  0x10,0x88,0x40,0x10,0x00,0x40,0x20,0x00,0x20,0x23,0x06,0x20,
  0x22,0x04,0x20,0xf3,0x06,0x78,0x20,0x00,0x20,0xf0,0x00,0x78,
  0x21,0x24,0x20,0x10,0xd8,0x40,0x0c,0x01,0x80,0x03,0xfe,0x00
};

// ────────────────────────────────
// ADC
// ────────────────────────────────
void setupADC() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11);
}

int readJoyX() { return adc1_get_raw(JOY_X); }
int readJoyY() { return adc1_get_raw(JOY_Y); }

// ────────────────────────────────
// I2S AUDIO
// ────────────────────────────────
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void playBeep(int freq, int durationMs) {
  int samples = (SAMPLE_RATE * durationMs) / 1000;
  int16_t buffer[64];
  int bufIdx = 0;
  size_t bytesWritten;
  for (int i = 0; i < samples; i++) {
    float sample = sin(2.0 * M_PI * freq * i / SAMPLE_RATE);
    float fade = 1.0;
    int fadeLen = SAMPLE_RATE / 100;
    if (i < fadeLen) fade = (float)i / fadeLen;
    if (i > samples - fadeLen) fade = (float)(samples - i) / fadeLen;
    int16_t val = (int16_t)(sample * VOLUME * fade * 32767);
    buffer[bufIdx++] = val;
    buffer[bufIdx++] = val;
    if (bufIdx >= 64) {
      i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
      bufIdx = 0;
    }
  }
  if (bufIdx > 0) {
    i2s_write(I2S_NUM_0, buffer, bufIdx * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  }
}

void playSilence(int durationMs) {
  int samples = (SAMPLE_RATE * durationMs) / 1000;
  int16_t buffer[64] = {0};
  size_t bytesWritten;
  int sent = 0;
  while (sent < samples * 2) {
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
    sent += 64;
  }
}

void alarmTask(void* param) {
  while (true) {
    if (alarmRinging) {
      playBeep(BEEP_FREQ, BEEP_ON_MS);
      playSilence(BEEP_OFF_MS);
      playBeep(BEEP_FREQ, BEEP_ON_MS);
      playSilence(BEEP_OFF_MS);
      playBeep(BEEP_FREQ, BEEP_ON_MS);
      playSilence(600);
    } else {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

// ────────────────────────────────
// DISCORD
// ────────────────────────────────
void sendDiscord(String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(discordWebhook);
  http.addHeader("Content-Type", "application/json");
  message.replace("\"", "\\\"");
  message.replace("\n", "\\n");
  String body = "{\"content\":\"" + message + "\"}";
  int code = http.POST(body);
  Serial.print("Discord response: ");
  Serial.println(code);
  http.end();
}

// ────────────────────────────────
// WIFI + NTP
// ────────────────────────────────
void drawWifiScreen(int dots) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 0);
  display.println("Connecting WiFi");
  display.drawLine(0, 10, 128, 10, WHITE);
  display.setCursor(0, 20);
  display.print(ssid);
  display.setCursor(0, 36);
  display.print("Please wait");
  for (int i = 0; i < dots; i++) display.print(".");
  display.display();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int dots = 0;
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    drawWifiScreen(dots % 4);
    dots++;
    attempts++;
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed!");
  }
}

void syncTime() {
  configTime(gmtOffset, dstOffset, ntpServer);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(25, 20);
  display.println("Syncing time...");
  display.display();
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(500);
    attempts++;
  }
  if (getLocalTime(&timeinfo)) {
    clockH = timeinfo.tm_hour;
    clockM = timeinfo.tm_min;
    clockS = timeinfo.tm_sec;
  }
}

void checkResync() {
  static unsigned long lastSync = 0;
  if (millis() - lastSync > 3600000) {
    lastSync = millis();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      clockH = timeinfo.tm_hour;
      clockM = timeinfo.tm_min;
      clockS = timeinfo.tm_sec;
    }
  }
}

// ────────────────────────────────
// HELPERS
// ────────────────────────────────
void printTwo(int val) {
  if (val < 10) display.print("0");
  display.print(val);
}

String getJoyDir() {
  if (millis() - lastJoyMove < JOY_DEBOUNCE) return "NONE";
  int x = readJoyX();
  int y = readJoyY();
  if (y < JOY_LOW)  { lastJoyMove = millis(); return "UP"; }
  if (y > JOY_HIGH) { lastJoyMove = millis(); return "DOWN"; }
  if (x > JOY_HIGH) { lastJoyMove = millis(); return "RIGHT"; }
  if (x < JOY_LOW)  { lastJoyMove = millis(); return "LEFT"; }
  return "NONE";
}

bool getBtnPress() {
  if (millis() - lastBtnPress < BTN_DEBOUNCE) return false;
  if (!digitalRead(JOY_BTN)) {
    lastBtnPress = millis();
    return true;
  }
  return false;
}

// ────────────────────────────────
// CLOCK TICK
// ────────────────────────────────
void tickClock() {
  static unsigned long lastTick = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    clockS++;
    if (clockS >= 60) { clockS = 0; clockM++; }
    if (clockM >= 60) { clockM = 0; clockH++; }
    if (clockH >= 24) { clockH = 0; }

    for (int i = 0; i < MAX_ALARMS; i++) {
      if (alarms[i].active &&
          clockH == alarms[i].h &&
          clockM == alarms[i].m &&
          clockS == 0) {
        alarmRinging = true;
        ringingAlarmIdx = i;
        currentScreen = SCREEN_ALARM_ON;

        // send discord notification
        String msg = "⏰ **SMAC Alarm " + String(i + 1) + " going off!**\n";
        msg +="**Habibi wake up, you got stuff to do**\n";
        msg += "🕐 Time: **" + String(alarms[i].h) + ":";
        msg += (alarms[i].m < 10 ? "0" : "") + String(alarms[i].m) + "**\n";

        // list upcoming alarms
        bool hasUpcoming = false;
        String upcoming = "📋 Upcoming alarms:\n";
        for (int j = 0; j < MAX_ALARMS; j++) {
          if (alarms[j].active && j != i) {
            upcoming += "• Alarm " + String(j + 1) + ": ";
            upcoming += String(alarms[j].h) + ":";
            upcoming += (alarms[j].m < 10 ? "0" : "") + String(alarms[j].m) + "\n";
            hasUpcoming = true;
          }
        }
        msg += hasUpcoming ? upcoming : "📋 No more alarms scheduled";
        sendDiscord(msg);
      }
    }
  }
}

// ────────────────────────────────
// SCREENS
// ────────────────────────────────
void drawClock() {
  display.clearDisplay();
  display.drawBitmap(0, 0, cat_bits, 22, 16, WHITE);
  display.drawBitmap(106, 0, cat_bits, 22, 16, WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(33, 4);
  display.println("SMAC Clock");
  display.drawLine(0, 17, 128, 17, WHITE);
  display.setTextSize(2);
  display.setCursor(14, 22);
  printTwo(clockH);
  display.print(":");
  printTwo(clockM);
  display.print(":");
  printTwo(clockS);
  int activeCount = 0;
  for (int i = 0; i < MAX_ALARMS; i++) {
    if (alarms[i].active) activeCount++;
  }
  display.setTextSize(1);
  display.setCursor(0, 50);
  if (activeCount > 0) {
    display.print(activeCount);
    display.print(" alarm");
    if (activeCount > 1) display.print("s");
    display.print(" set");
  } else {
    display.print("No alarms set");
  }
  display.setCursor(90, 56);
  display.print("[MENU]");
  display.display();
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(40, 0);
  display.println("MENU");
  display.drawLine(0, 10, 128, 10, WHITE);
  for (int i = 0; i < MENU_ITEMS; i++) {
    display.setCursor(10, 16 + (i * 16));
    display.print(i == menuIndex ? "> " : "  ");
    display.println(menuOptions[i]);
  }
  display.display();
}

void drawAlarmList() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(22, 0);
  display.println("Alarms");
  display.drawLine(0, 10, 128, 10, WHITE);
  int startIdx = 0;
  if (alarmListIndex >= 3) startIdx = alarmListIndex - 2;
  for (int i = startIdx; i < MAX_ALARMS && i < startIdx + 3; i++) {
    int y = 14 + ((i - startIdx) * 14);
    display.setCursor(0, y);
    display.print(i == alarmListIndex ? ">" : " ");
    display.print(i + 1);
    display.print(": ");
    printTwo(alarms[i].h);
    display.print(":");
    printTwo(alarms[i].m);
    display.print(" ");
    display.print(alarms[i].active ? "[ON] " : "[OFF]");
  }
  display.setCursor(120, 14);
  display.print(alarmListIndex + 1);
  display.setCursor(120, 22);
  display.print("/");
  display.setCursor(120, 30);
  display.print(MAX_ALARMS);
  display.drawLine(0, 52, 128, 52, WHITE);
  display.setCursor(0, 56);
  display.print("BTN:edit L:back R:I/O");
  display.display();
}

void drawSetAlarm() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 0);
  display.print("Edit Alarm ");
  display.println(editingAlarm + 1);
  display.drawLine(0, 10, 128, 10, WHITE);
  display.setTextSize(2);
  display.setCursor(14, 22);
  if (editField == 0) {
    display.setTextColor(BLACK, WHITE);
    printTwo(alarms[editingAlarm].h);
    display.setTextColor(WHITE);
  } else {
    printTwo(alarms[editingAlarm].h);
  }
  display.print(":");
  if (editField == 1) {
    display.setTextColor(BLACK, WHITE);
    printTwo(alarms[editingAlarm].m);
    display.setTextColor(WHITE);
  } else {
    printTwo(alarms[editingAlarm].m);
  }
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.print("Status: ");
  display.print(alarms[editingAlarm].active ? "ON" : "OFF");
  display.setCursor(0, 56);
  display.print("BTN:save HOLD:cancel");
  display.display();
}

void drawAlarmOn() {
  display.clearDisplay();
  display.drawBitmap(0, 0, cat_bits, 22, 16, WHITE);
  display.drawBitmap(106, 0, cat_bits, 22, 16, WHITE);
  display.setTextSize(2);
  display.setCursor(15, 18);
  display.println("WAKE UP!");
  display.setTextSize(1);
  display.setCursor(30, 38);
  if (ringingAlarmIdx >= 0) {
    display.print("Alarm ");
    display.print(ringingAlarmIdx + 1);
    display.print(": ");
    printTwo(alarms[ringingAlarmIdx].h);
    display.print(":");
    printTwo(alarms[ringingAlarmIdx].m);
  }
  display.setCursor(5, 52);
  display.println("SMACK ME to stop!");
  display.display();
}

// ────────────────────────────────
// HANDLERS
// ────────────────────────────────
void handleMenu() {
  String dir = getJoyDir();
  if (dir == "UP")   menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
  if (dir == "DOWN") menuIndex = (menuIndex + 1) % MENU_ITEMS;
  if (getBtnPress()) {
    if (menuIndex == 0) currentScreen = SCREEN_CLOCK;
    if (menuIndex == 1) { currentScreen = SCREEN_ALARM_LIST; alarmListIndex = 0; }
  }
}

void handleAlarmList() {
  String dir = getJoyDir();
  if (dir == "UP")   alarmListIndex = (alarmListIndex - 1 + MAX_ALARMS) % MAX_ALARMS;
  if (dir == "DOWN") alarmListIndex = (alarmListIndex + 1) % MAX_ALARMS;
  if (dir == "RIGHT") alarms[alarmListIndex].active = !alarms[alarmListIndex].active;
  if (dir == "LEFT")  currentScreen = SCREEN_CLOCK;
  if (getBtnPress()) {
    editingAlarm = alarmListIndex;
    editField = 0;
    currentScreen = SCREEN_SET_ALARM;
  }
}

void handleSetAlarm() {
  String dir = getJoyDir();
  if (dir == "LEFT")  editField = 0;
  if (dir == "RIGHT") editField = 1;
  if (editField == 0) {
    if (dir == "UP")   alarms[editingAlarm].h = (alarms[editingAlarm].h + 1) % 24;
    if (dir == "DOWN") alarms[editingAlarm].h = (alarms[editingAlarm].h - 1 + 24) % 24;
  }
  if (editField == 1) {
    if (dir == "UP")   alarms[editingAlarm].m = (alarms[editingAlarm].m + 1) % 60;
    if (dir == "DOWN") alarms[editingAlarm].m = (alarms[editingAlarm].m - 1 + 60) % 60;
  }
  // short press = save
  if (getBtnPress()) {
    alarms[editingAlarm].active = true;
    currentScreen = SCREEN_ALARM_LIST;
  }
  // long press = cancel
  static unsigned long btnHoldStart = 0;
  if (!digitalRead(JOY_BTN)) {
    if (btnHoldStart == 0) btnHoldStart = millis();
    if (millis() - btnHoldStart > 1000) {
      btnHoldStart = 0;
      currentScreen = SCREEN_ALARM_LIST;
    }
  } else {
    btnHoldStart = 0;
  }
}

// ────────────────────────────────
// WEB SERVER
// ────────────────────────────────
void setupWebServer() {
  server.on("/", []() {
    String msg = "";
    if (server.hasArg("h") && server.hasArg("m") && server.hasArg("slot")) {
      int slot = server.arg("slot").toInt();
      if (slot >= 0 && slot < MAX_ALARMS) {
        alarms[slot].h = server.arg("h").toInt();
        alarms[slot].m = server.arg("m").toInt();
        alarms[slot].active = true;
        msg = "Alarm " + String(slot + 1) + " saved";
      }
    }

    String page = R"(
<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>SMAC</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: #f5f0eb;
    color: #2c2420;
    min-height: 100vh;
    padding: 32px 16px;
  }
  .wrap { max-width: 360px; margin: 0 auto; }
  .header { text-align: center; margin-bottom: 28px; }
  .cat-svg { display: block; margin: 0 auto 12px; }
  .app-name { font-size: 11px; letter-spacing: 0.18em; text-transform: uppercase; color: #b08878; margin-bottom: 2px; }
  .time-label { font-size: 11px; letter-spacing: 0.12em; text-transform: uppercase; color: #b08878; margin-bottom: 6px; }
  .time-display { font-size: 56px; font-weight: 300; letter-spacing: -1px; color: #1a1008; line-height: 1; margin-bottom: 4px; }
  .divider { height: 1px; background: #e0d4c8; margin: 22px 0; }
  .section-title { font-size: 11px; letter-spacing: 0.12em; text-transform: uppercase; color: #b08878; margin-bottom: 14px; }
  .alarm-pills { display: flex; flex-direction: column; gap: 8px; margin-bottom: 8px; }
  .alarm-pill { display: flex; align-items: center; justify-content: space-between; background: #ede5dc; border-radius: 10px; padding: 12px 16px; }
  .alarm-pill.active { background: #e8ddd0; }
  .alarm-pill .atime { font-size: 17px; font-weight: 500; color: #2c2420; }
  .alarm-num { font-size: 11px; color: #b08878; margin-bottom: 2px; }
  .badge { font-size: 10px; letter-spacing: 0.08em; text-transform: uppercase; padding: 3px 9px; border-radius: 20px; background: #d8cdc4; color: #8a7060; }
  .badge.on { background: #c8a882; color: #fff; }
  .field label { display: block; font-size: 11px; letter-spacing: 0.1em; text-transform: uppercase; color: #b08878; margin-bottom: 6px; }
  .form-row { display: flex; gap: 10px; margin-bottom: 14px; align-items: flex-end; }
  .field { flex: 1; }
  .field select, .field input { width: 100%; padding: 11px 13px; border: 1.5px solid #d8cdc4; border-radius: 9px; background: #faf6f2; color: #2c2420; font-size: 15px; font-family: inherit; outline: none; -webkit-appearance: none; appearance: none; }
  .field select:focus, .field input:focus { border-color: #c8a882; background: #fff; }
  .btn { width: 100%; padding: 13px; background: #9a6a4a; color: #faf6f2; border: none; border-radius: 9px; font-size: 14px; font-family: inherit; letter-spacing: 0.05em; cursor: pointer; }
  .btn:active { background: #7a4a2a; }
  .msg { margin-top: 14px; padding: 11px 14px; background: #e8d8c8; color: #6a4030; border-radius: 9px; font-size: 13px; text-align: center; }
</style>
</head>
<body>
<div class='wrap'>
  <div class='header'>
    <svg class='cat-svg' width='64' height='52' viewBox='0 0 64 52' fill='none'>
      <polygon points='8,22 2,4 20,14' fill='#c8a882'/>
      <polygon points='56,22 62,4 44,14' fill='#c8a882'/>
      <polygon points='9,20 5,8 17,15' fill='#e8c8b0'/>
      <polygon points='55,20 59,8 47,15' fill='#e8c8b0'/>
      <ellipse cx='32' cy='32' rx='26' ry='22' fill='#c8a882'/>
      <ellipse cx='22' cy='28' rx='4' ry='5' fill='#2c2420'/>
      <ellipse cx='42' cy='28' rx='4' ry='5' fill='#2c2420'/>
      <circle cx='24' cy='26' r='1.5' fill='white'/>
      <circle cx='44' cy='26' r='1.5' fill='white'/>
      <polygon points='32,34 29,38 35,38' fill='#e89878'/>
      <path d='M29,38 Q32,42 35,38' stroke='#2c2420' stroke-width='1' fill='none'/>
      <line x1='6' y1='33' x2='24' y2='35' stroke='#8a7060' stroke-width='1'/>
      <line x1='6' y1='37' x2='24' y2='37' stroke='#8a7060' stroke-width='1'/>
      <line x1='58' y1='33' x2='40' y2='35' stroke='#8a7060' stroke-width='1'/>
      <line x1='58' y1='37' x2='40' y2='37' stroke='#8a7060' stroke-width='1'/>
    </svg>
    <div class='app-name'>S · M · A · C</div>
  </div>
  <div class='time-label'>Current time</div>
  <div class='time-display'>)";

    page += String(clockH) + ":" + (clockM < 10 ? "0" : "") + String(clockM);

    page += R"(</div>
  <div class='divider'></div>
  <div class='section-title'>Alarms</div>
  <div class='alarm-pills'>)";

    for (int i = 0; i < MAX_ALARMS; i++) {
      page += "<div class='alarm-pill" + String(alarms[i].active ? " active" : "") + "'>";
      page += "<div><div class='alarm-num'>Alarm " + String(i + 1) + "</div>";
      page += "<div class='atime'>" + String(alarms[i].h) + ":" + (alarms[i].m < 10 ? "0" : "") + String(alarms[i].m) + "</div></div>";
      page += "<span class='badge" + String(alarms[i].active ? " on" : "") + "'>" + String(alarms[i].active ? "on" : "off") + "</span>";
      page += "</div>";
    }

    page += R"(</div>
  <div class='divider'></div>
  <div class='section-title'>Set alarm</div>
  <form method='GET'>
    <div class='form-row'>
      <div class='field'>
        <label>Slot</label>
        <select name='slot'>)";

    for (int i = 0; i < MAX_ALARMS; i++) {
      page += "<option value='" + String(i) + "'>Alarm " + String(i + 1) + "</option>";
    }

    page += R"(</select>
      </div>
      <div class='field'>
        <label>Hour</label>
        <input type='number' name='h' min='0' max='23' placeholder='7'>
      </div>
      <div class='field'>
        <label>Min</label>
        <input type='number' name='m' min='0' max='59' placeholder='00'>
      </div>
    </div>
    <button class='btn' type='submit'>Set alarm</button>
  </form>)";

    if (msg != "") page += "<div class='msg'>" + msg + "</div>";
    page += "</div></body></html>";
    server.send(200, "text/html", page);
  });

  server.begin();
  Serial.print("Web server at: http://");
  Serial.println(WiFi.localIP());
}

// ────────────────────────────────
// SETUP
// ────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);

  setupADC();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("No display found, continuing...");
  }
  display.clearDisplay();

  display.drawBitmap(30, 10, cat_bits, 22, 16, WHITE);
  display.drawBitmap(76, 10, cat_bits, 22, 16, WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 32);
  display.println("S.M.A.C Clock");
  display.setCursor(10, 45);
  display.println("Smart Moving Alarm");
  display.display();
  delay(2000);

  connectWifi();
  syncTime();
  setupWebServer();

  setupI2S();
  xTaskCreatePinnedToCore(alarmTask, "AlarmTask", 4096, NULL, 1, NULL, 0);

  currentScreen = SCREEN_CLOCK;
  Serial.println("SMAC ready!");
}

// ────────────────────────────────
// LOOP
// ────────────────────────────────
void loop() {
  server.handleClient();
  tickClock();
  checkResync();

  if (currentScreen == SCREEN_CLOCK) {
    drawClock();
    if (getBtnPress()) {
      currentScreen = SCREEN_MENU;
      menuIndex = 0;
    }
  }
  else if (currentScreen == SCREEN_MENU) {
    drawMenu();
    handleMenu();
  }
  else if (currentScreen == SCREEN_ALARM_LIST) {
    drawAlarmList();
    handleAlarmList();
  }
  else if (currentScreen == SCREEN_SET_ALARM) {
    drawSetAlarm();
    handleSetAlarm();
  }
  else if (currentScreen == SCREEN_ALARM_ON) {
    drawAlarmOn();
    if (!digitalRead(STOP_BTN)) {
      delay(50);
      alarmRinging = false;
      currentScreen = SCREEN_CLOCK;
    }
  }
}
