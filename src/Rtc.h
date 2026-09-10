#pragma once

#include <stdint.h>

// Optional DS3231 RTC module (Fase 6).
// Gated behind -DENABLE_RTC in platformio.ini.
// When disabled, stubs are inlined here — no-ops that make the
// rest of the firmware behave exactly as without RTC.
//
// Wiring (default, change these if your wiring differs):
//   SDA -> GPIO 21
//   SCL -> GPIO 3
//   VCC -> 3.3V
//   GND -> GND
//
// Note: GPIO 3 is a strapping pin on ESP32-S3 (must be low at boot).
// The DS3231 module's I2C pull-ups may fight this. If you see boot
// issues, move SCL to another free GPIO and update the constant below.

#ifdef ENABLE_RTC

#include <Wire.h>

void initRtc();
bool readRtcTime(uint8_t &hours, uint8_t &minutes, uint8_t &seconds);
bool readRtcDate(uint8_t &day, uint8_t &month, uint8_t &year);
void syncRtcFromNtp();
bool isRtcPresent();

#else

inline void initRtc() {}
inline bool readRtcTime(uint8_t &, uint8_t &, uint8_t &) { return false; }
inline bool readRtcDate(uint8_t &, uint8_t &, uint8_t &) { return false; }
inline void syncRtcFromNtp() {}
inline bool isRtcPresent() { return false; }

#endif
