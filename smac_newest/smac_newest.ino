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
#include <Preferences.h>
WebServer server(80);
Preferences preferences;

const char* ssid     = "Ritu_Pixel";
const char* password = "Ritzphoenix07";

// Configured from the web app and persisted in ESP32 flash.
String discordWebhookUrl = "";
String userName = "";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset = -18000;
const int   dstOffset = 3600;

float lat = -999;
float lon = -999;
float t_high;
float t_low;
float temp;
float humidity;
String w_desc;

const char* wthrServer = "https://api.openweathermap.org/data/2.5/weather?lat={lat}&lon={lon}&appid={API key}";

const int LEFT_MOTOR_A = 25; // A-1A
const int RIGHT_MOTOR_A = 16; // B-1A
const int LEFT_MOTOR_B = 26; // A-1B
const int RIGHT_MOTOR_B = 17; // B-2A
const int fullSpeed = 255; // full speed at which motors can operate

const float midSpeed = (0.85)*fullSpeed;   // 50% speed

float operatingSpeed = midSpeed;  //present speed of motors
float aMotorTrim  = 0.80; 
float bMotorTrim = 1.00;

const int IR_SENSOR_PIN = 19;
bool irObstacle;   // true if obstacle present else false
unsigned long lastActionTime = 0; // tracks the last time any movement command what executed

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
#define VOLUME       1

#define STOP_BTN JOY_BTN

// ── Screens ──
#define SCREEN_CLOCK        0
#define SCREEN_MENU         1
#define SCREEN_ALARM_LIST   2  // shows all alarms
#define SCREEN_SET_ALARM    3  // edit one alarm
#define SCREEN_ALARM_ON     4
#define SCREEN_WEATHER

int currentScreen = SCREEN_CLOCK;

// ── Menu ──
int menuIndex = 0;
const int MENU_ITEMS = 3;
String menuOptions[] = {"Clock", "Alarms", "Weather"};

// ── Time ──
int clockH = 0, clockM = 0, clockS = 0;

// ── Multiple Alarms ──
#define MAX_ALARMS 5  // max number of alarms

struct Alarm {
  int h;
  int m;
  bool active;  // is this alarm enabled
  String name;
};

Alarm alarms[MAX_ALARMS] = {
  {7, 0, false, "Alarm 1"},
  {8, 0, false, "Alarm 2"},
  {9, 0, false, "Alarm 3"},
  {10, 0, false, "Alarm 4"},
  {11, 0, false, "Alarm 5"}
};

int alarmListIndex = 0;  // which alarm is selected in list
int editingAlarm = 0;    // which alarm is being edited
int editField = 0;       // 0 = hours, 1 = minutes
bool alarmRinging = false;
int ringingAlarmIdx = -1; // which alarm triggered
unsigned long alarmStartMillis = 0; // when the current alarm started ringing

const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
float frontDist;   // distance of object from front

// ── Debounce ──
unsigned long lastJoyMove = 0;
#define JOY_DEBOUNCE 200 //in miliseconds
unsigned long lastBtnPress = 0;
#define BTN_DEBOUNCE 300

// ── Cat image ──
static const unsigned char PROGMEM cat_bits[] = {
  0x02,0x02,0x00,0x05,0x05,0x00,0x08,0xf8,0x80,0x08,0xa8,0x80,
  0x10,0x88,0x40,0x10,0x00,0x40,0x20,0x00,0x20,0x23,0x06,0x20,
  0x22,0x04,0x20,0xf3,0x06,0x78,0x20,0x00,0x20,0xf0,0x00,0x78,
  0x21,0x24,0x20,0x10,0xd8,0x40,0x0c,0x01,0x80,0x03,0xfe,0x00
};


// ADC

void setupADC() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11);
}

int readJoyX() { return adc1_get_raw(JOY_X); }
int readJoyY() { return adc1_get_raw(JOY_Y); }

//I2S AUDIO

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
// PERSISTENT WEB SETTINGS
// ────────────────────────────────
void loadWebSettings() {
  preferences.begin("smac", false);
  userName = preferences.getString("user", "");
  discordWebhookUrl = preferences.getString("webhook", "");
}

void saveWebSettings() {
  preferences.putString("user", userName);
  preferences.putString("webhook", discordWebhookUrl);
}

bool validDiscordWebhook(const String& url) {
  return url.startsWith("https://discord.com/api/webhooks/") ||
         url.startsWith("https://discordapp.com/api/webhooks/");
}

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "\\r");
  return value;
}

// ────────────────────────────────
// DISCORD
// ────────────────────────────────
void sendDiscord(String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (discordWebhookUrl.length() == 0) {
    Serial.println("Discord webhook not configured.");
    return;
  }

  HTTPClient http;
  http.begin(discordWebhookUrl);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"content\":\"" + jsonEscape(message) + "\"}";
  int code = http.POST(body);
  Serial.print("Discord response: ");
  Serial.println(code);
  http.end();
}


// WIFI + NTP

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
  while (WiFi.status() != WL_CONNECTED) {
    drawWifiScreen(dots % 4);
    dots++;
    delay(500);
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

void syncWeather(){
  if (WiFi.status() == WL_CONNECTED) {
    http.begin(wthrServer);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      
      temp = doc["main"]["temp"];
      t_high = doc["main"]["temp_max"];
      t_low = doc["main"]["temp_min"];
      humidity = doc["main"]["humidity"];
      w_desc = doc["weather"]["description"];
    }
    http.end();
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


// HELPERS

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
        alarmStartMillis = millis();
        currentScreen = SCREEN_ALARM_ON;

        // Send Discord notification.
        String msg = "⏰ **SMAC Alarm " + String(i + 1) + " going off!**\n";
        if (userName.length() > 0) {
          msg += "🚨 **" + userName + ", wake up!**\n";
          msg += "@everyone " + userName + " hasn't woken up yet!\n";
        } else {
          msg += "🚨 **Wake up!**\n";
        }
        msg += "Alarm: **" + alarms[i].name + "**\n";
        msg += "**HABIBI wake up, you got stuff to do!**\n";
        msg += "Your friends are sick of you sleeping!\n";
        msg += "https://cdn.discordapp.com/attachments/841879601667637248/1391084208780476587/zt.gif?ex=6a6b7320&is=6a6a21a0&hm=dc0f37d35aed7dde9aeb771397226da4ef2915ee0b0db4c688689b9f66eca09d&";
        msg += "🕐 Time: **" + String(alarms[i].h) + ":";
        msg += (alarms[i].m < 10 ? "0" : "") + String(alarms[i].m) + "**\n";

        // list upcoming alarms
        bool hasUpcoming = false;
        String upcoming = "📋 Upcoming alarms:\n";
        for (int j = 0; j < MAX_ALARMS; j++) {
          if (alarms[j].active && j != i) {
            upcoming += "• " + alarms[j].name + " (Alarm " + String(j + 1) + "): ";
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


// SCREENS

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

void drawWeather() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(40, 0);
  display.println("WEATHER");
  display.drawLine(0, 10, 128, 10, WHITE);
  display.setCursor(10,9);
  display.println("Displaying Weather for (insert date)");
  display.setCursor(10,18);
  display.print("Desc: ");
  display.println(w_desc);
  display.setCursor(10,27);
  display.print("Temp Now (Celsius): ");
  display.println(temp);
  display.setCursor(10,36);
  display.print("High/Low Temp: ");
  display.print(t_high);
  display.print("; ");
  display.println(t_low);
  display.setCursor(10,45);
  display.print("Weather Status: ");
  display.println(w_desc);
  display.setCursor(0, 56);
  display.print("BTN:return to clock");
  display.display();
}

// shows list of all 5 alarms with on/off status
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

// edit a specific alarm time
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

String formatElapsed(unsigned long elapsedMs) {
  unsigned long totalSeconds = elapsedMs / 1000;
  unsigned long hours = totalSeconds / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  String result = "";
  if (hours < 10) result += "0";
  result += String(hours) + ":";
  if (minutes < 10) result += "0";
  result += String(minutes) + ":";
  if (seconds < 10) result += "0";
  result += String(seconds);
  return result;
}

void drawAlarmOn() {
  display.clearDisplay();
  display.drawBitmap(0, 0, cat_bits, 22, 16, WHITE);
  display.drawBitmap(106, 0, cat_bits, 22, 16, WHITE);

  display.setTextSize(2);
  display.setCursor(15, 15);
  display.println("WAKE UP!");

  if (ringingAlarmIdx >= 0) {
    String name = alarms[ringingAlarmIdx].name;
    if (name.length() > 20) name = name.substring(0, 20);

    display.setTextSize(1);
    display.setCursor(64 - (name.length() * 3), 33);
    display.print(name);

    display.setCursor(30, 44);
    display.print("TIME ");
    display.print(formatElapsed(millis() - alarmStartMillis));
  }

  display.setCursor(5, 57);
  display.println("SMACK ME to stop!");
  display.display();
}



// HANDLERS

void handleMenu() {
  String dir = getJoyDir();
  if (dir == "UP")   menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
  if (dir == "DOWN") menuIndex = (menuIndex + 1) % MENU_ITEMS;
  if (getBtnPress()) {
    if (menuIndex == 0) currentScreen = SCREEN_CLOCK;
    if (menuIndex == 1) { currentScreen = SCREEN_ALARM_LIST; alarmListIndex = 0; }
    if (menuIndex == 2) currentScreen = SCREEN_WEATHER;
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

void handleWeather(){
   if (getBtnPress()) {
    currentScreen = SCREEN_CLOCK;
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
// ────────────────────────────────
// WEB SERVER
// ────────────────────────────────
String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

void handleWebPost() {
  String msg = "";

  if (server.hasArg("action") && server.arg("action") == "settings") {
    String newName = server.hasArg("userName") ? server.arg("userName") : "";
    String newWebhook = server.hasArg("webhook") ? server.arg("webhook") : "";
    bool clearWebhook = server.hasArg("clearWebhook") && server.arg("clearWebhook") == "1";

    newName.trim();
    if (newName.length() > 32) newName = newName.substring(0, 32);
    userName = newName;

    if (clearWebhook) {
      discordWebhookUrl = "";
    } else if (newWebhook.length() > 0) {
      newWebhook.trim();
      if (validDiscordWebhook(newWebhook)) {
        discordWebhookUrl = newWebhook;
      } else {
        msg = "Webhook was not saved: enter a Discord webhook URL.";
      }
    }

    if (msg == "") {
      saveWebSettings();
      msg = "Discord settings saved";
    }
  }

  String page = "<html><head><meta http-equiv='refresh' content='1; url=/'></head><body>";
  page += htmlEscape(msg);
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleWebPage() {
  String msg = "";

  if (server.hasArg("action") && server.arg("action") == "toggle" && server.hasArg("slot")) {
    int slot = server.arg("slot").toInt();
    if (slot >= 0 && slot < MAX_ALARMS) {
      alarms[slot].active = !alarms[slot].active;
      msg = alarms[slot].name + String(alarms[slot].active ? " turned ON" : " turned OFF");
    }
  }

  if (server.hasArg("action") && server.arg("action") == "save" &&
      server.hasArg("h") && server.hasArg("m") &&
      server.hasArg("slot") && server.hasArg("name")) {

    int slot = server.arg("slot").toInt();
    int h = server.arg("h").toInt();
    int m = server.arg("m").toInt();
    String name = server.arg("name");

    if (slot >= 0 && slot < MAX_ALARMS && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
      name.trim();
      if (name.length() == 0) name = "Alarm " + String(slot + 1);
      if (name.length() > 32) name = name.substring(0, 32);

      alarms[slot].h = h;
      alarms[slot].m = m;
      alarms[slot].name = name;
      alarms[slot].active = true;
      msg = alarms[slot].name + " saved and turned ON";
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
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background:#f5f0eb; color:#2c2420; min-height:100vh; padding:24px 16px; }
  .wrap { max-width:390px; margin:0 auto; }
  .header { text-align:center; margin-bottom:24px; }
  .cat-svg { display:block; margin:0 auto 12px; }
  .app-name { font-size:11px; letter-spacing:.18em; text-transform:uppercase; color:#b08878; }
  .time-label { font-size:11px; letter-spacing:.12em; text-transform:uppercase; color:#b08878; margin-bottom:6px; }
  .time-display { font-size:52px; font-weight:300; color:#1a1008; line-height:1; }
  .divider { height:1px; background:#e0d4c8; margin:20px 0; }
  .section-title { font-size:11px; letter-spacing:.12em; text-transform:uppercase; color:#b08878; margin-bottom:12px; }
  .alarm-pills { display:flex; flex-direction:column; gap:8px; }
  .alarm-pill { display:flex; align-items:center; justify-content:space-between; background:#ede5dc; border-radius:10px; padding:11px 12px 11px 14px; gap:10px; }
  .alarm-pill.active { background:#e8ddd0; }
  .alarm-info { min-width:0; flex:1; }
  .atime { font-size:17px; font-weight:500; }
  .alarm-num { font-size:10px; color:#b08878; text-transform:uppercase; letter-spacing:.06em; }
  .alarm-name { font-size:13px; color:#6c5448; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; margin-top:2px; }
  .badge { font-size:10px; letter-spacing:.08em; text-transform:uppercase; padding:5px 9px; border-radius:20px; background:#d8cdc4; color:#8a7060; border:none; cursor:pointer; }
  .badge.on { background:#c8a882; color:#fff; }
  .box { background:#ede5dc; border-radius:12px; padding:14px; }
  .form-row { display:flex; gap:9px; margin-bottom:11px; align-items:flex-end; }
  .field { flex:1; min-width:0; }
  .field.wide { flex:2; }
  .field label { display:block; font-size:10px; letter-spacing:.08em; text-transform:uppercase; color:#b08878; margin-bottom:5px; }
  .field input, .field select { width:100%; padding:10px 11px; border:1.5px solid #d8cdc4; border-radius:8px; background:#faf6f2; color:#2c2420; font-size:15px; font-family:inherit; outline:none; }
  .btn { width:100%; padding:12px; background:#9a6a4a; color:#faf6f2; border:none; border-radius:9px; font-size:14px; font-family:inherit; cursor:pointer; }
  .btn:active { background:#7a4a2a; }
  .status { margin-top:8px; padding:9px 10px; border-radius:8px; background:#f0e7df; color:#6c5448; font-size:11px; text-align:center; }
  .hint { font-size:10px; color:#9a8276; margin-top:8px; text-align:center; line-height:1.4; }
  .checkbox { display:flex; align-items:center; gap:7px; margin:9px 0 3px; color:#6c5448; font-size:11px; }
  .checkbox input { width:auto; }
  .msg { margin-top:12px; padding:10px 12px; background:#e8d8c8; color:#6a4030; border-radius:9px; font-size:13px; text-align:center; }
  @media (max-width:360px) { .form-row { flex-wrap:wrap; } .field.wide { flex-basis:100%; } }
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
    page += "<div class='alarm-info'><div class='alarm-num'>Alarm " + String(i + 1) + "</div>";
    page += "<div class='atime'>" + String(alarms[i].h) + ":" + (alarms[i].m < 10 ? "0" : "") + String(alarms[i].m) + "</div>";
    page += "<div class='alarm-name'>" + htmlEscape(alarms[i].name) + "</div></div>";
    page += "<form method='GET' style='margin:0'><input type='hidden' name='action' value='toggle'><input type='hidden' name='slot' value='" + String(i) + "'>";
    page += "<button class='badge" + String(alarms[i].active ? " on" : "") + "' type='submit'>" + String(alarms[i].active ? "ON" : "OFF") + "</button></form></div>";
  }

  page += R"(</div>

  <div class='divider'></div>
  <div class='section-title'>Edit alarm</div>
  <div class='box'>
    <form method='GET'>
      <input type='hidden' name='action' value='save'>
      <div class='form-row'>
        <div class='field wide'><label>Name</label><input type='text' name='name' maxlength='32' placeholder='e.g. WAKE UP!!'></div>
        <div class='field'><label>Slot</label><select name='slot'>)";

  for (int i = 0; i < MAX_ALARMS; i++) page += "<option value='" + String(i) + "'>Alarm " + String(i + 1) + "</option>";

  page += R"(</select></div>
      </div>
      <div class='form-row'>
        <div class='field'><label>Hour</label><input type='number' name='h' min='0' max='23' placeholder='7'></div>
        <div class='field'><label>Minute</label><input type='number' name='m' min='0' max='59' placeholder='00'></div>
      </div>
      <button class='btn' type='submit'>Save alarm</button>
    </form>
    <div class='hint'>Saving an alarm turns it ON. Use the ON/OFF button above to disable it.</div>
  </div>

  <div class='divider'></div>
  <div class='section-title'>Weather settings</div>
  <div class='box'>
    <form method='POST'>
      <input type='hidden' name='action' value='settings'>
      <div class='field' style='margin-bottom:11px'>
        <label>Latitude</label>
        <input type='text' name='lat' maxlength='32' value=')";
  page += htmlEscape(lat);
  page += R"(' placeholder='e.g. 32'>
      </div>
      <div class='field'>
        <label>Longitude</label>
        <input type='text' name='longitude' placeholder=')";
  page += (lat != -999) || (lon != -999) ? "Saved — leave blank to keep it" : "Input coordinates";
  page += R"(' autocomplete='off'>
      </div>
      <label class='checkbox'><input type='checkbox' name='clearCoord' value='1'> Clear the saved coordinates</label>
      <button class='btn' type='submit'>Save Weather settings</button>
    </form>
    <div class='status'>)";
  page += (lat >= -90 && lat<=90) || (lon >= 0 && lon <= 360) ? "Weather configured" : "Weather not configured";
  page += R"(</div>
    <div class='hint'>Use your coordinates to set the weather report in SMAC. Input latitude between [-90, 90] and longitude between [0, 360].</div>
  </div>)";

  page += R"(<div class='divider'></div>
  <div class='section-title'>Discord settings</div>
  <div class='box'>
    <form method='POST'>
      <input type='hidden' name='action' value='settings'>
      <div class='field' style='margin-bottom:11px'>
        <label>Your name</label>
        <input type='text' name='userName' maxlength='32' value=')";
  page += htmlEscape(userName);
  page += R"(' placeholder='e.g. Eve Lin'>
      </div>
      <div class='field'>
        <label>Discord webhook URL</label>
        <input type='password' name='webhook' placeholder=')";
  page += discordWebhookUrl.length() > 0 ? "Saved — leave blank to keep it" : "Paste your Discord webhook URL";
  page += R"(' autocomplete='off'>
      </div>
      <label class='checkbox'><input type='checkbox' name='clearWebhook' value='1'> Clear the saved webhook</label>
      <button class='btn' type='submit'>Save Discord settings</button>
    </form>
    <div class='status'>)";
  page += discordWebhookUrl.length() > 0 ? "Webhook configured" : "Webhook not configured";
  page += R"(</div>
    <div class='hint'>The name is used in the Discord callout. The webhook is saved in ESP32 flash and is not displayed back on the page.</div>
  </div>)";

  if (msg != "") page += "<div class='msg'>" + htmlEscape(msg) + "</div>";
  page += R"(
</div>
</body>
</html>)";
  server.send(200, "text/html", page);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleWebPage);
  server.on("/", HTTP_POST, handleWebPost);
  server.begin();
  Serial.print("Web server at: http://");
  Serial.println(WiFi.localIP());
}

// SETUP

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
  loadWebSettings();
  setupWebServer();

  setupI2S();
  xTaskCreatePinnedToCore(alarmTask, "AlarmTask", 4096, NULL, 1, NULL, 0);

  currentScreen = SCREEN_CLOCK;
  Serial.println("SMAC ready!");

  // Set all motor control pins as outputs
  pinMode(LEFT_MOTOR_A, OUTPUT);
  pinMode(LEFT_MOTOR_B, OUTPUT);
  pinMode(RIGHT_MOTOR_A, OUTPUT);
  pinMode(RIGHT_MOTOR_B, OUTPUT);
  
  // Ensure everything starts turned off
  stopMotors();

  pinMode(IR_SENSOR_PIN, INPUT); // Set GPIO 19 as an INPUT pin

  setupADC();

}


// LOOP

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
  else if (currentScreen = SCREEN_WEATHER){
    drawWeather();
    handleWeather();    
  }
  else if (currentScreen == SCREEN_ALARM_ON) {
    drawAlarmOn();
    if (!digitalRead(STOP_BTN)) {
      unsigned long wakeTime = millis() - alarmStartMillis;
      String elapsed = formatElapsed(wakeTime);

      stopMotors();
      delay(50);
      alarmRinging = false;

      // Tell Discord how long the alarm was active before it was stopped.
      String wakeMsg;
      if (userName.length() > 0) {
        wakeMsg = "✅ **" + userName + " has woken up!**\n";
      } else {
        wakeMsg = "✅ **Alarm stopped!**\n";
      }

      if (ringingAlarmIdx >= 0) {
        wakeMsg += "Alarm: **" + alarms[ringingAlarmIdx].name + "**\n";
      }
      wakeMsg += "⏱️ Time to wake up: **" + elapsed + "**";
      sendDiscord(wakeMsg);

      currentScreen = SCREEN_CLOCK;
      ringingAlarmIdx = -1;
    }

    // motors start running
    //detecting if object present at back (IR) or front (ultrasonic)

    // ultrasonic code, deprecated 
    #if 0
    frontDist = readUltrasonicDistance();
    #endif 

    // IR code
    irObstacle = (digitalRead(IR_SENSOR_PIN) == LOW); // LOW means object detected
    if (irObstacle) {
      Serial.println("Object detected!");
    }

    // Setting up operating speeds depending on any object behind the clock
    if (irObstacle) {
      Serial.println("moving backward");
      moveBackward();
      delay(500);

      Serial.println("turning right");
      turnRight();
      delay(500);
    } 
    else {
      Serial.println("moving forward");
      moveForward();
    }

    }


}

// ================= motor code 

// Function for basic forward movement in case of no significant event required (turn left, turn right or speed up)
// Use same function for speed up but set operatingSpeed == midSpeed or fastSpeed (after testing)
void moveForward() {
  //Left motor
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, operatingSpeed*aMotorTrim);
  //Right motor
  analogWrite(LEFT_MOTOR_A, operatingSpeed*bMotorTrim);
  analogWrite(LEFT_MOTOR_B, 0);
  
}

void moveBackward() {
  //Left Motor
  analogWrite(RIGHT_MOTOR_A, operatingSpeed*aMotorTrim);
  analogWrite(RIGHT_MOTOR_B, 0);
  //Right Motor
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, operatingSpeed*bMotorTrim);
  
}

// Function to turn right
void turnRight() {
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_B, operatingSpeed);

  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, operatingSpeed);
  
}

// Stop the motors
void stopMotors() {
  analogWrite(LEFT_MOTOR_A, 0);
  analogWrite(RIGHT_MOTOR_A, 0);
  analogWrite(LEFT_MOTOR_B, 0);
  analogWrite(RIGHT_MOTOR_B, 0);
}

float readUltrasonicDistance() {
  //send the ping
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  //read for time taken by the signal
  long duration = pulseIn(ECHO_PIN, HIGH);
  //that time into speed of sound
  return (duration * 0.034) / 2.0;
}
