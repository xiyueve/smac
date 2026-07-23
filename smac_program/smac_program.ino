#include <Wire.h> //I2C library needed so ESP32 can talk to OLED 
#include <Adafruit_GFX.h> //Gives us tools to draw on screens.
#include <Adafruit_SSD1306.h> //Another display library
#include "driver/i2s.h" //Loads the ESP32 built in I2S audio driver
#include "driver/adc.h" //Loads the ESP32 ADC driver
#include <math.h>
#include <WiFi.h>
#include <time.h>


//Need this to connect to the web and find out what the real world time is

const char* ssid     = "Eve";
const char* password = "iloveesp2026";


const char* ntpServer = "pool.ntp.org"; // pool.ntp.org is a free public time 
const long  gmtOffset = -18000; //EST timezone offset 5 hours * 3600 seconds
const int   dstOffset = 3600;  //daylight savings


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


//Joystick Pins

#define JOY_X    ADC1_CHANNEL_4  // GPIO32
#define JOY_Y    ADC1_CHANNEL_5  // GPIO33
#define JOY_BTN  15
#define JOY_HIGH 3000 //if joystick reading is abouve 3000, it is concidered high
#define JOY_LOW  1000 //same thing. Center reading is 2000


//Pins for MAX 98357A

#define I2S_BCLK 12 //sends timing pulses to the MAX98357A amplifie
#define I2S_LRC  14 //tells which audio channel is coming 
#define I2S_DOUT 13 //carries the actual audio data to the amplifier.

#define SAMPLE_RATE  44100  //Audio quality setting 
#define BEEP_FREQ    1000  //pitch in Hz, you can play with this 
#define BEEP_ON_MS   400  //each beep lasts 400 ms
#define BEEP_OFF_MS  200  //each pause lasts 200 ms
#define VOLUME       0.5 //increase in real product

#define STOP_BTN 27


// Screens to track which screen is showing

#define SCREEN_CLOCK     0
#define SCREEN_MENU      1
#define SCREEN_SET_ALARM 2
#define SCREEN_ALARM_ON  3

int currentScreen = SCREEN_CLOCK;

int menuIndex = 0; //this is so the arrow in the menu knows which one to point at

const int MENU_ITEMS = 2;

String menuOptions[] = {"Clock", "Set Alarm"};


//Time

int clockH = 0, clockM = 0, clockS = 0; //sets vareables until NTP syncs


//Alarm

int alarmH = 7, alarmM = 0;
bool alarmSet = false;
bool alarmRinging = false;
int editField = 0; //allows for flashing to know which field is being edited for the alarm


// Debounce - makes sure that one button press does not trigger multiple times

unsigned long lastJoyMove = 0;

#define JOY_DEBOUNCE 200

unsigned long lastBtnPress = 0;

#define BTN_DEBOUNCE 300


// Stores the cat bitmap image in flash memory instead of RAM.

static const unsigned char PROGMEM cat_bits[] = {

  0x02,0x02,0x00,0x05,0x05,0x00,0x08,0xf8,0x80,0x08,0xa8,0x80,

  0x10,0x88,0x40,0x10,0x00,0x40,0x20,0x00,0x20,0x23,0x06,0x20,

  0x22,0x04,0x20,0xf3,0x06,0x78,0x20,0x00,0x20,0xf0,0x00,0x78,

  0x21,0x24,0x20,0x10,0xd8,0x40,0x0c,0x01,0x80,0x03,0xfe,0x00

};


//ADC setup, fixes I2C conflict

void setupADC() {

  adc1_config_width(ADC_WIDTH_BIT_12);

  adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11); // GPIO32

  adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11); // GPIO33

}


int readJoyX() { return adc1_get_raw(JOY_X); }

int readJoyY() { return adc1_get_raw(JOY_Y); }


// I2S AUDIO stolen code off of web 

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

//end of stolen code lol


// I feel like this function is self explanitory aacbcacbcacbcacb

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

  WiFi.begin(ssid, password);
  WiFi.mode(WIFI_STA);

  int dots = 0;

  while (WiFi.status() != WL_CONNECTED) {

    drawWifiScreen(dots % 4);

    Serial.println("...testing");

    dots++;

    delay(500);

  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nConnection failed. Check hotspot configuration.");
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


// HELPERS

void printTwo(int val) {

  if (val < 10) display.print("0");

  display.print(val);

}


String getJoyDir() {

  if (millis() - lastJoyMove < JOY_DEBOUNCE) return "NONE";

  int x = readJoyX();
  Serial.println(x);

  int y = readJoyY();
  Serial.println(y);

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




// CLOCK TICK

void tickClock() {

  static unsigned long lastTick = 0;

  if (millis() - lastTick >= 1000) {

    lastTick = millis();

    clockS++;

    if (clockS >= 60) { clockS = 0; clockM++; }

    if (clockM >= 60) { clockM = 0; clockH++; }

    if (clockH >= 24) { clockH = 0; }

    if (alarmSet && clockH == alarmH && clockM == alarmM && clockS == 0) {

      alarmRinging = true;

      currentScreen = SCREEN_ALARM_ON;

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

  display.setTextSize(1);

  display.setCursor(0, 50);

  if (alarmSet) {

    display.print("Alarm: ");

    printTwo(alarmH);

    display.print(":");

    printTwo(alarmM);

  } else {

    display.print("No alarm set");

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


void drawSetAlarm() {

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(22, 0);

  display.println("Set Alarm");

  display.drawLine(0, 10, 128, 10, WHITE);

  display.setTextSize(2);

  display.setCursor(14, 22);

  if (editField == 0) {

    display.setTextColor(BLACK, WHITE);

    printTwo(alarmH);

    display.setTextColor(WHITE);

  } else {

    printTwo(alarmH);

  }

  display.print(":");

  if (editField == 1) {

    display.setTextColor(BLACK, WHITE);

    printTwo(alarmM);

    display.setTextColor(WHITE);

  } else {

    printTwo(alarmM);

  }

  display.setTextSize(1);

  display.setCursor(0, 50);

  display.print("BTN: confirm alarm");

  display.display();

}


void drawAlarmOn() {

  display.clearDisplay();

  display.drawBitmap(0, 0, cat_bits, 22, 16, WHITE);

  display.drawBitmap(106, 0, cat_bits, 22, 16, WHITE);

  display.setTextSize(2);

  display.setCursor(15, 20);

  display.println("WAKE UP!");

  display.setTextSize(1);

  display.setCursor(10, 48);

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

    if (menuIndex == 1) { currentScreen = SCREEN_SET_ALARM; editField = 0; }

  }

}


void handleSetAlarm() {

  String dir = getJoyDir();

  if (dir == "LEFT")  editField = 0;

  if (dir == "RIGHT") editField = 1;

  if (editField == 0) {

    if (dir == "UP")   alarmH = (alarmH + 1) % 24;

    if (dir == "DOWN") alarmH = (alarmH - 1 + 24) % 24;

  }

  if (editField == 1) {

    if (dir == "UP")   alarmM = (alarmM + 1) % 60;

    if (dir == "DOWN") alarmM = (alarmM - 1 + 60) % 60;

  }

  if (getBtnPress()) {

    alarmSet = true;

    currentScreen = SCREEN_CLOCK;

  }

}


// ────────────────────────────────

// SETUP

// ────────────────────────────────

void setup() {

  Serial.begin(115200);

  pinMode(JOY_BTN, INPUT_PULLUP);

  pinMode(STOP_BTN, INPUT_PULLUP);




  // setup ADC first to avoid I2S conflict

  setupADC();


  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  display.clearDisplay();


  // splash screen

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


  // connect wifi + sync time

  connectWifi();

  syncTime();


  // start audio AFTER ADC is configured

  setupI2S();

  xTaskCreatePinnedToCore(alarmTask, "AlarmTask", 4096, NULL, 1, NULL, 0);


  currentScreen = SCREEN_CLOCK;

  Serial.println("SMAC ready!");

}


// ────────────────────────────────

// LOOP

// ────────────────────────────────

void loop() {

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

  else if (currentScreen == SCREEN_SET_ALARM) {

    drawSetAlarm();

    handleSetAlarm();

  }

  else if (currentScreen == SCREEN_ALARM_ON) {

   drawAlarmOn();

   if (!digitalRead(STOP_BTN)) {

      delay(50); // debounce

      alarmRinging = false;

     alarmSet = false;

     currentScreen = SCREEN_CLOCK;

   }

  }

}