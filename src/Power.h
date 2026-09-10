#pragma once
#include <stdint.h>

// Battery / power management module (Fase 4).
// Gated behind -DENABLE_BATTERY in platformio.ini [env:battery].
// When disabled, stubs are inlined here — no-ops that make the
// rest of the firmware behave exactly as the pre-battery code.

#ifdef ENABLE_BATTERY

#include "SettingsMenu.h"

void initPower();

// Battery voltage divider: Li-Ion (4.2V max) -> voltage divider
// (100k + 100k) -> ADC1 channel 0 = GPIO 38.
// ADC1_CH0 is on GPIO 38 (per ESP32-S3 datasheet, ADC1_CH0 = GPIO38).
// Returns battery percentage (0-100), or 0 if on mains or if the
// "Battery" setting is Off.
int getBatteryPercent();

// True if running on battery (VBUS low) AND the Battery setting is On.
// False if on 5V USB/input or if the Battery setting is Off.
bool isOnBattery();

// True while a deep-sleep cycle requested by prepareDeepSleep()
// is in progress — used by web admin to reject OTA, and by
// TimeSync to skip WiFi reconnection during sleep.
bool isSleeping();

// Enter deep sleep. Wakes up after `wakeInSeconds` or on any
// RTC-capable button press (EXIT/MENU/encoder). Must be called
// with the e-paper already showing the static clock face,
// LEDs off, SD powered down, WiFi off.
// Returns false if conditions to sleep aren't met (e.g. not on
// battery, alarm ringing, etc.)
bool prepareDeepSleep(int wakeInSeconds);

// Called at boot — restores state after a deep-sleep wakeup.
// Returns the wakeup cause for diagnostics.
int handleWakeup();

#else

// Stubs when battery support is disabled
inline void initPower() {}
inline int getBatteryPercent() { return 0; }
inline bool isOnBattery() { return false; }
inline bool isSleeping() { return false; }
inline bool prepareDeepSleep(int wakeInSeconds) { (void)wakeInSeconds; return false; }
inline int handleWakeup() { return 0; }

#endif