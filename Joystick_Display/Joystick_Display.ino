#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLED ──
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Joystick ──
#define JOY_X   32
#define JOY_Y   33
#define JOY_BTN 15

// ── Joystick thresholds ──
#define JOY_HIGH 3000  // pushed up/right
#define JOY_LOW  1000  // pushed down/left

// ── Screens ──
#define SCREEN_CLOCK     0
#define SCREEN_MENU      1
#define SCREEN_SET_TIME  2
#define SCREEN_SET_ALARM 3
#define SCREEN_ALARM_ON  4

int currentScreen = SCREEN_CLOCK;
int menuIndex = 0;
const int MENU_ITEMS = 3;
String menuOptions[] = {"Clock", "Set Time", "Set Alarm"};

// ── Time ──
int clockH = 7, clockM = 30, clockS = 0;
unsigned long lastTick = 0;

// ── Alarm ──
int alarmH = 7, alarmM = 0;
bool alarmSet = false;
int editField = 0; // 0 = hours, 1 = minutes

// ── Joystick debounce ──
unsigned long lastJoyMove = 0;
#define JOY_DEBOUNCE 200

unsigned long lastBtnPress = 0;
#define BTN_DEBOUNCE 300

// ── Helper: print two digits ──
void printTwo(int val) {
  if (val < 10) display.print("0");
  display.print(val);
}

// ── Joystick reading ──
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

bool getBtnPress() {
  if (millis() - lastBtnPress < BTN_DEBOUNCE) return false;
  if (!digitalRead(JOY_BTN)) {
    lastBtnPress = millis();
    return true;
  }
  return false;
}

// ── Tick clock ──
void tickClock() {
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    clockS++;
    if (clockS >= 60) { clockS = 0; clockM++; }
    if (clockM >= 60) { clockM = 0; clockH++; }
    if (clockH >= 24) { clockH = 0; }

    // Check alarm
    if (alarmSet && clockH == alarmH && clockM == alarmM && clockS == 0) {
      currentScreen = SCREEN_ALARM_ON;
    }
  }
}

// ── Draw clock screen ──
void drawClock() {
  display.clearDisplay();

  // Title
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(30, 0);
  display.println("SMAC Clock");

  // Divider
  display.drawLine(0, 10, 128, 10, WHITE);

  // Big time
  display.setTextSize(2);
  display.setCursor(14, 20);
  printTwo(clockH);
  display.print(":");
  printTwo(clockM);
  display.print(":");
  printTwo(clockS);

  // Alarm status
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

  // Menu hint
  display.setCursor(90, 56);
  display.print("[MENU]");

  display.display();
}

// ── Draw menu screen ──
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

// ── Draw set time screen ──
void drawSetTime() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(25, 0);
  display.println("Set Time");
  display.drawLine(0, 10, 128, 10, WHITE);

  display.setTextSize(2);
  display.setCursor(14, 22);

  // Highlight selected field
  if (editField == 0) {
    display.setTextColor(BLACK, WHITE); // inverted
    printTwo(clockH);
    display.setTextColor(WHITE);
  } else {
    printTwo(clockH);
  }

  display.print(":");

  if (editField == 1) {
    display.setTextColor(BLACK, WHITE);
    printTwo(clockM);
    display.setTextColor(WHITE);
  } else {
    printTwo(clockM);
  }

  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print("UP/DN:change LR:switch");

  display.display();
}

// ── Draw set alarm screen ──
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

// ── Draw alarm ringing screen ──
void drawAlarmOn() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20, 10);
  display.println("WAKE UP!");
  display.setTextSize(1);
  display.setCursor(10, 40);
  display.println("Press BTN to stop");
  display.display();
}

// ── Handle menu navigation ──
void handleMenu() {
  String dir = getJoyDir();

  if (dir == "UP")   menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
  if (dir == "DOWN") menuIndex = (menuIndex + 1) % MENU_ITEMS;

  if (getBtnPress()) {
    if (menuIndex == 0) currentScreen = SCREEN_CLOCK;
    if (menuIndex == 1) { currentScreen = SCREEN_SET_TIME; editField = 0; }
    if (menuIndex == 2) { currentScreen = SCREEN_SET_ALARM; editField = 0; }
  }
}

// ── Handle set time navigation ──
void handleSetTime() {
  String dir = getJoyDir();

  if (dir == "LEFT")  editField = 0;
  if (dir == "RIGHT") editField = 1;

  if (editField == 0) {
    if (dir == "UP")   clockH = (clockH + 1) % 24;
    if (dir == "DOWN") clockH = (clockH - 1 + 24) % 24;
  } else {
    if (dir == "UP")   clockM = (clockM + 1) % 60;
    if (dir == "DOWN") clockM = (clockM - 1 + 60) % 60;
  }

  if (getBtnPress()) currentScreen = SCREEN_CLOCK;
}

// ── Handle set alarm navigation ──
void handleSetAlarm() {
  String dir = getJoyDir();

  if (dir == "LEFT")  editField = 0;
  if (dir == "RIGHT") editField = 1;

  if (editField == 0) {
    if (dir == "UP")   alarmH = (alarmH + 1) % 24;
    if (dir == "DOWN") alarmH = (alarmH - 1 + 24) % 24;
  } else {
    if (dir == "UP")   alarmM = (alarmM + 1) % 60;
    if (dir == "DOWN") alarmM = (alarmM - 1 + 60) % 60;
  }

  if (getBtnPress()) {
    alarmSet = true;
    currentScreen = SCREEN_CLOCK;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // Splash screen
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 10);
  display.println("S.M.A.C");
  display.setTextSize(1);
  display.setCursor(15, 40);
  display.println("Smart Moving Alarm");
  display.display();
  delay(2000);
}

void loop() {
  tickClock();

  // Clock screen — button opens menu
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

  else if (currentScreen == SCREEN_SET_TIME) {
    drawSetTime();
    handleSetTime();
  }

  else if (currentScreen == SCREEN_SET_ALARM) {
    drawSetAlarm();
    handleSetAlarm();
  }

  else if (currentScreen == SCREEN_ALARM_ON) {
    drawAlarmOn();
    if (getBtnPress()) {
      currentScreen = SCREEN_CLOCK;
      alarmSet = false;
    }
  }
}
