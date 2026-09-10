#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ── Wokwi ST7735 wiring ──
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  8
#define TFT_BL   7
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// ── Inputs ──
#define ENC_CLK 15
#define ENC_DT  16
#define BTN_ENC 17
#define BTN_UP  18
#define BTN_DOWN 19

// ── Clock / Alarm State ──
enum Mode { CLOCK, SETTINGS, ALARM_RING };
Mode mode = CLOCK;

struct tm now;
bool alarmEnabled = true;
int alarmHour = 7;
int alarmMinute = 30;
bool alarmRinging = false;
int snoozeCount = 0;
unsigned long snoozeUntil = 0;

// ── Settings Menu State ──
enum SettingsTab { TAB_ALARM, TAB_LIGHT, TAB_SYSTEM };
SettingsTab settingsTab = TAB_ALARM;
int settingsRow = 0;

// ── Display helpers ──
void drawClockFace();
void drawSettingsScreen();
void drawAlarmScreen();
void updateTime();
void handleInput();
void checkAlarm();

int lastEncoderPos = 0;
unsigned long lastTick = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(BTN_ENC, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // Seed fake time: 06:55 on Jan 1
  now.tm_hour = 6;
  now.tm_min = 55;
  now.tm_sec = 0;
  now.tm_mday = 1;
  now.tm_mon = 0;
  now.tm_year = 2026 - 1900;

  drawClockFace();
  Serial.println("PaperWake sim ready");
}

void loop() {
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    now.tm_sec++;
    if (now.tm_sec >= 60) {
      now.tm_sec = 0;
      now.tm_min++;
      if (now.tm_min >= 60) {
        now.tm_min = 0;
        now.tm_hour++;
        if (now.tm_hour >= 24) now.tm_hour = 0;
      }
    }
    updateTime();
    checkAlarm();
  }
  handleInput();
}

// ── Display ──
void drawClockFace() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(10, 20);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", now.tm_hour, now.tm_min);
  tft.print(buf);

  tft.setTextSize(1);
  tft.setCursor(10, 60);
  tft.print("Alarm: ");
  if (alarmEnabled) {
    tft.setTextColor(ST77XX_GREEN);
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.print("OFF");
  }
  tft.print(buf);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 80);
  tft.print("Mode: CLOCK");
}

void drawSettingsScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.print("SETTINGS");

  const char *tab = settingsTab == TAB_ALARM ? "Alarm" : settingsTab == TAB_LIGHT ? "Light" : "System";
  tft.setCursor(0, 12);
  tft.print(tab);

  tft.setCursor(0, 30);
  tft.print("> Alarm ");
  tft.print(alarmEnabled ? "ON" : "OFF");
  tft.setCursor(0, 42);
  tft.print("  Time ");
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
  tft.print(buf);
  tft.setCursor(0, 54);
  tft.print("  Back");
}

void drawAlarmScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(3);
  tft.setCursor(10, 30);
  tft.print("WAKE UP!");
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 70);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
  tft.print(buf);
  tft.setCursor(10, 100);
  tft.print("Snooze: ");
  tft.print(snoozeCount);
}

void updateTime() {
  if (mode == CLOCK) {
    tft.fillRect(10, 20, 120, 24, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(3);
    tft.setCursor(10, 20);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", now.tm_hour, now.tm_min);
    tft.print(buf);
  } else if (mode == ALARM_RING) {
    tft.fillRect(10, 70, 120, 20, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 70);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
    tft.print(buf);
  }
}

void checkAlarm() {
  if (alarmEnabled && !alarmRinging && now.tm_hour == alarmHour && now.tm_min == alarmMinute && now.tm_sec == 0) {
    alarmRinging = true;
    mode = ALARM_RING;
    snoozeCount = 0;
    snoozeUntil = 0;
    drawAlarmScreen();
    Serial.println("ALARM RINGING");
  }
}

// ── Input ──
int readEncoder() {
  static int lastA = HIGH;
  int a = digitalRead(ENC_CLK);
  int b = digitalRead(ENC_DT);
  if (lastA == HIGH && a == LOW) {
    if (b == HIGH) return 1;
    return -1;
  }
  lastA = a;
  return 0;
}

void handleInput() {
  int enc = readEncoder();
  bool btnEnc = digitalRead(BTN_ENC) == LOW;
  bool btnUp = digitalRead(BTN_UP) == LOW;
  bool btnDown = digitalRead(BTN_DOWN) == LOW;

  if (mode == CLOCK) {
    if (btnEnc) {
      delay(200);
      mode = SETTINGS;
      settingsTab = TAB_ALARM;
      settingsRow = 0;
      drawSettingsScreen();
      Serial.println("Enter SETTINGS");
    }
  } else if (mode == SETTINGS) {
    if (enc != 0) {
      settingsRow = (settingsRow + enc + 3) % 3;
      drawSettingsScreen();
    }
    if (btnEnc) {
      delay(200);
      if (settingsRow == 2) {
        mode = CLOCK;
        drawClockFace();
        Serial.println("Back to CLOCK");
      }
    }
    if (btnUp) {
      delay(200);
      if (settingsRow == 0) alarmEnabled = !alarmEnabled;
      drawSettingsScreen();
    }
  } else if (mode == ALARM_RING) {
    if (btnEnc) {
      delay(200);
      snoozeCount++;
      snoozeUntil = millis() + 300000; // 5 min snooze
      Serial.printf("Snooze %d\n", snoozeCount);
    }
    if (millis() >= snoozeUntil && snoozeUntil != 0) {
      alarmRinging = false;
      mode = CLOCK;
      drawClockFace();
      Serial.println("Snooze ended");
    }
  }
}
