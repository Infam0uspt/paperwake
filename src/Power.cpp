#include "Power.h"

#ifdef ENABLE_BATTERY

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <WiFi.h>

#include "TimeSync.h"       // isTimeSynced
#include "Sound.h"          // updateAlarmSound
#include "Light.h"          // stopWakeupLight / frontlight off
#include "PinConfig.h"      // SD_PWR
#include "SettingsMenu.h"   // getBatteryEnabled

// Battery voltage divider: Vbat --[R1=100k]-- ADC --[R2=100k]-- GND
// ADC reads Vbat * R2/(R1+R2) = Vbat/2.
// ADC1_CH0 = GPIO 38 on ESP32-S3.
// Note: GPIO 21 is used for RTC SDA when -DENABLE_RTC is set.
constexpr int kBatteryPin = 38;
constexpr float kAdcReference = 3.3f;
constexpr float kAdcMaxCount = 4095.0f;
constexpr float kDividerRatio = 2.0f;    // R1=R2 -> Vbat = ADC * 2

// RTC_DATA (retained during deep sleep)
RTC_DATA_ATTR bool wasOnMains = true;     // assume mains at cold boot
RTC_DATA_ATTR int wakeupCauseCache = 0;

namespace {
bool g_sleeping = false;
}

void initPower() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // 0-3.6V range for 4.2V battery
}

bool isOnBattery() {
  if (!getBatteryEnabled()) return false;
  int raw = analogRead(kBatteryPin);
  float voltage = (raw / kAdcMaxCount) * kAdcReference * kDividerRatio;
  // 3.5V+ (7.0V scaled) = on mains/USB; lower = battery
  return voltage < 3.5f;
}

int getBatteryPercent() {
  if (!isOnBattery()) return 0;
  int raw = analogRead(kBatteryPin);
  float voltage = (raw / kAdcMaxCount) * kAdcReference * kDividerRatio;
  // Map 3.0V-4.2V to 0-100% (simple linear; upgradeable with MAX17048)
  int pct = (int)((voltage - 3.0f) / (4.2f - 3.0f) * 100.0f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

bool isSleeping() { return g_sleeping; }

bool prepareDeepSleep(int wakeInSeconds) {
  // Don't enter sleep if already sleeping, or if not on battery, or if
  // the Battery setting is Off.
  if (g_sleeping || !isOnBattery()) return false;

  // Ensure peripherals off (in the right order)
  updateAlarmSound(false);     // stop alarm/audio playback
  stopWakeupLight();           // turn off LED segments
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, LOW);

  WiFi.mode(WIFI_OFF);
  btStop();

  // Configure wakeup sources
  // Timer wakeup
  esp_sleep_enable_timer_wakeup((uint64_t)wakeInSeconds * 1000000ULL);

  // Button wakeups (EXIT on GPIO1 — RTC-capable).
  // EXIT and MENU are both RTC-capable on ESP32-S3, so either can wake.
  // Using ext0 on GPIO1 (EXIT) which is pulled low on button press.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)1, 0);  // GPIO1, low level = pressed

  wasOnMains = !isOnBattery();
  g_sleeping = true;

  Serial.printf("[Power] Entering deep sleep for %d seconds\n", wakeInSeconds);

  // Ensure any pending serial output completes
  delay(10);
  Serial.flush();

  esp_deep_sleep_start();
  return true;  // never reached — device restarts
}

int handleWakeup() {
  int wakeupCause = (int)esp_sleep_get_wakeup_cause();
  wakeupCauseCache = wakeupCause;
  g_sleeping = false;

  if (wakeupCause == ESP_SLEEP_WAKEUP_TIMER) {
    wasOnMains = false;
    // Time is available immediately via RTC — continue to clock face
    // (main.cpp handles NTP resync opportunistically in background)
  } else if (wakeupCause == ESP_SLEEP_WAKEUP_EXT0) {
    // Button wakeup — full power restore
    wasOnMains = !isOnBattery();
    pinMode(SD_PWR, OUTPUT);
    digitalWrite(SD_PWR, HIGH);
  } else {
    // Cold boot or other cause
    wasOnMains = !isOnBattery();
  }

  return wakeupCause;
}

#endif  // ENABLE_BATTERY