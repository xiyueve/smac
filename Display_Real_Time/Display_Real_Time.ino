#include <Wire.h> //Loads the I2C library so ESP32 can talk to the OLED display.
#include <Adafruit_GFX.h> //Graphics library for the OLED
#include <Adafruit_SSD1306.h> //Loads the specific driver for SSD1306 OLED screen.
#include <WiFi.h> //Loads WIFI Library
#include <time.h> // Time

// ── WiFi credentials ──
const char* ssid     = "MyOptimum df858f";
const char* password = "24-rose-3111"; 

// ── NTP settings ──
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset = -18000;  // EST = UTC-5 = -18000 seconds
const int   dstOffset = 3600;    // daylight saving = 1 hour

// ── OLED ──
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Joystick ──
// REMEMBER TO WIRE CORRECTLY
#define JOY_X   32
#define JOY_Y   33
#define JOY_BTN 15

// ── Joystick thresholds ──
#define JOY_HIGH 3000
#define JOY_LOW  1000

// ── Screens ──
#define SCREEN_CLOCK     0
#define SCREEN_MENU      1
#define SCREEN_SET_ALARM 2
#define SCREEN_ALARM_ON  3
#define SCREEN_WIFI      4

int currentScreen = SCREEN_WIFI;
int menuIndex = 0;
const int MENU_ITEMS = 2;
String menuOptions[] = {"Clock", "Set Alarm"};

// ── Time ──
int clockH = 0, clockM = 0, clockS = 0;
bool timeSynced = false;

// ── Alarm ──
int alarmH = 7, alarmM = 0;
bool alarmSet = false;
int editField = 0;

// ── Joystick debounce ──
unsigned long lastJoyMove = 0;
#define JOY_DEBOUNCE 200
unsigned long lastBtnPress = 0;
#define BTN_DEBOUNCE 300

// ── Cat image (22x16 pixels) ──
static const unsigned char PROGMEM cat_bits[] = {
  0x02,0x02,0x00,0x05,0x05,0x00,0x08,0xf8,0x80,0x08,0xa8,0x80,
  0x10,0x88,0x40,0x10,0x00,0x40,0x20,0x00,0x20,0x23,0x06,0x20,
  0x22,0x04,0x20,0xf3,0x06,0x78,0x20,0x00,0x20,0xf0,0x00,0x78,
  0x21,0x24,0x20,0x10,0xd8,0x40,0x0c,0x01,0x80,0x03,0xfe,0x00
};

// ── Helper: print two digits ──
void printTwo(int val) {
  if (val < 10) display.print("0");
  display.print(val);
}

// ── Joystick direction ──
String getJoyDir() {
  if (millis() - lastJoyMove < JOY_DEBOUNCE) return "NONE";
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);
  if (y < JOY_LOW)  { lastJoyMove = millis(); return "UP"; }
  if (y > JOY_HIGH) { lastJoyMove = millis(); return "DOWN"; }
  if (x > JOY_HIGH) { lastJoyMove = millis(); return "RIGHT"; }
  if (x < JOY_LOW)  { lastJoyMove = millis(); return "LEFT"; }
  return "NONE";
}

// ── Button press ──
bool getBtnPress() {
  if (millis() - lastBtnPress < BTN_DEBOUNCE) return false;
  if (!digitalRead(JOY_BTN)) {
    lastBtnPress = millis();
    return true;
  }
  return false;
}

// ── NTP time sync ──
void syncTime() {
  configTime(gmtOffset, dstOffset, ntpServer);
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
    timeSynced = true;
  }
}

// ── Tick clock every second ──
void tickClock() {
  static unsigned long lastTick = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    clockS++;
    if (clockS >= 60) { clockS = 0; clockM++; }
    if (clockM >= 60) { clockM = 0; clockH++; }
    if (clockH >= 24) { clockH = 0; }
    if (alarmSet && clockH == alarmH && clockM == alarmM && clockS == 0) {
      currentScreen = SCREEN_ALARM_ON;
    }
  }
}

// ── WiFi connecting screen ──
void drawWifi(int dots) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 0);
  display.println("Connecting WiFi");
  display.drawLine(0, 10, 128, 10, WHITE);
  display.setCursor(0, 18);
  display.print("Network: ");
  display.println(ssid);
  display.setCursor(0, 35);
  display.print("Please wait");
  for (int i = 0; i < dots; i++) display.print(".");
  display.setCursor(0, 50);
  if (timeSynced) {
    display.println("Time synced!");
  } else {
    display.println("Syncing time...");
  }
  display.display();
}

// ── Clock screen with cats! ──
void drawClock() {
  display.clearDisplay();

  // cat in top left corner
  display.drawBitmap(0, 0, cat_bits, 22, 16, WHITE);

  // cat in top right corner
  display.drawBitmap(106, 0, cat_bits, 22, 16, WHITE);

  // SMAC title centered between cats
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(33, 4);
  display.println("SMAC Clock");

  // divider line below cats
  display.drawLine(0, 17, 128, 17, WHITE);

  // big time display shifted down to make room for cats
  display.setTextSize(2);
  display.setCursor(14, 22);
  printTwo(clockH);
  display.print(":");
  printTwo(clockM);
  display.print(":");
  printTwo(clockS);

  // alarm status at bottom
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

  // menu hint bottom right
  display.setCursor(90, 56);
  display.print("[MENU]");

  display.display();
}

// ── Menu screen ──
void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(40, 0);
  display.println("MENU");
  display.drawLine(0, 10, 128, 10, WHITE);
  for (int i = 0; i < MENU_ITEMS; i++) {
    display.setCursor(10, 16 + (i * 16));
    if (i == menuIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(menuOptions[i]);
  }
  display.display();
}

// ── Set alarm screen ──
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

// ── Alarm ringing screen with cats ──
void drawAlarmOn() {
  display.clearDisplay();

  // cats on alarm screen too!
  display.drawBitmap(0, 0, cat_bits, 22, 16, WHITE);
  display.drawBitmap(106, 0, cat_bits, 22, 16, WHITE);

  display.setTextSize(2);
  display.setCursor(15, 20);
  display.println("WAKE UP!");

  display.setTextSize(1);
  display.setCursor(10, 48);
  display.println("Press BTN to stop");

  display.display();
}

// ── Menu navigation ──
void handleMenu() {
  String dir = getJoyDir();
  if (dir == "UP")   menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
  if (dir == "DOWN") menuIndex = (menuIndex + 1) % MENU_ITEMS;
  if (getBtnPress()) {
    if (menuIndex == 0) currentScreen = SCREEN_CLOCK;
    if (menuIndex == 1) { currentScreen = SCREEN_SET_ALARM; editField = 0; }
  }
}

// ── Set alarm navigation ──
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

// ── Setup ──
void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // splash screen with cats!
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

  // connect to WiFi
  WiFi.begin(ssid, password);
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    drawWifi(dots % 4);
    dots++;
    delay(500);
  }

  // sync time
  syncTime();
  currentScreen = SCREEN_CLOCK;
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// ── Main loop ──
void loop() {
  tickClock();

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
    if (getBtnPress()) {
      alarmSet = false;
      currentScreen = SCREEN_CLOCK;
    }
  }
}
