#include "Rtc.h"

#ifdef ENABLE_RTC

#include <Arduino.h>

// Default pins — override in PinConfig.h if you wire differently.
#ifndef RTC_SDA_PIN
constexpr int kRtcSdaPin = 21;
#endif
#ifndef RTC_SCL_PIN
constexpr int kRtcSclPin = 38;
#endif

constexpr uint8_t kRtcI2cAddr = 0x68;
constexpr uint8_t kRtcRegSeconds = 0x00;
constexpr uint8_t kRtcRegControl = 0x0E;

namespace {
bool g_present = false;
TwoWire *g_wire = nullptr;
}

void initRtc() {
  g_wire = &Wire;
  g_wire->begin(kRtcSdaPin, kRtcSclPin);
  g_wire->setClock(100000);
  delay(10);

  g_wire->beginTransmission(kRtcI2cAddr);
  g_present = g_wire->endTransmission() == 0;

  if (g_present) {
    // Ensure the DS3231 is running (clear any OSF and enable battery-backed mode).
    g_wire->beginTransmission(kRtcI2cAddr);
    g_wire->write(kRtcRegControl);
    g_wire->endTransmission();
    g_wire->requestFrom(kRtcI2cAddr, 1);
    uint8_t ctrl = g_wire->read();
    // Clear the Enable Oscillator (EOSC) bit so the DS3231 keeps time on battery.
    ctrl &= ~(1 << 7);
    g_wire->beginTransmission(kRtcI2cAddr);
    g_wire->write(kRtcRegControl);
    g_wire->write(ctrl);
    g_wire->endTransmission();
    Serial.println("[RTC] DS3231 detected");
  } else {
    Serial.println("[RTC] No DS3231 found");
  }
}

bool isRtcPresent() { return g_present; }

static uint8_t bcdToDec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t decToBcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

bool readRtcTime(uint8_t &hours, uint8_t &minutes, uint8_t &seconds) {
  if (!g_present || !g_wire) return false;
  g_wire->beginTransmission(kRtcI2cAddr);
  g_wire->write(kRtcRegSeconds);
  if (g_wire->endTransmission() != 0) return false;
  g_wire->requestFrom(kRtcI2cAddr, 3);
  if (g_wire->available() < 3) return false;
  seconds = bcdToDec(g_wire->read() & 0x7F);
  minutes = bcdToDec(g_wire->read());
  hours = bcdToDec(g_wire->read() & 0x3F);
  return true;
}

bool readRtcDate(uint8_t &day, uint8_t &month, uint8_t &year) {
  if (!g_present || !g_wire) return false;
  g_wire->beginTransmission(kRtcI2cAddr);
  g_wire->write(0x03);
  if (g_wire->endTransmission() != 0) return false;
  g_wire->requestFrom(kRtcI2cAddr, 3);
  if (g_wire->available() < 3) return false;
  day = bcdToDec(g_wire->read());
  month = bcdToDec(g_wire->read() & 0x1F);
  year = bcdToDec(g_wire->read());
  return true;
}

void syncRtcFromNtp() {
  if (!g_present || !g_wire) return;
  struct tm now;
  if (!getLocalTime(&now, 0)) return;
  g_wire->beginTransmission(kRtcI2cAddr);
  g_wire->write(kRtcRegSeconds);
  g_wire->write(decToBcd(now.tm_sec));
  g_wire->write(decToBcd(now.tm_min));
  g_wire->write(decToBcd(now.tm_hour));
  g_wire->write(decToBcd(now.tm_wday + 1));
  g_wire->write(decToBcd(now.tm_mday));
  g_wire->write(decToBcd(now.tm_mon + 1));
  g_wire->write(decToBcd(now.tm_year % 100));
  g_wire->endTransmission();
  Serial.println("[RTC] Synced from NTP");
}

#endif
