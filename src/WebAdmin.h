#pragma once

#include <Arduino.h>

// Web admin portal — the remote-configuration half of the device's
// HTTP surface (the sound-management half lives in SoundUpload.cpp).
// Serves, over the existing home-WiFi connection:
//   /               dashboard (time, wifi, next alarm, firmware version)
//   /alarms         multi-alarm CRUD (time, on/off, days-of-week)
//   /settings       the same values the physical Settings menu edits
//   /system         firmware version, OTA update, config export/import, restart
// Runs in the background whenever WiFi is up — not tied to a display
// mode — but is *suspended* while a mode needs port 80 for itself
// (SoundUpload's upload page, WifiSetup's captive portal), and refuses
// dangerous operations (OTA, config import) while the device is busy
// (alarm ringing).

void initWebAdmin();   // registers all handlers once; begin() is deferred to loop()
void handleWebAdmin(); // call every loop() iteration; no-op when suspended/disconnected

// Port 80 is exclusive: hand it over before entering a mode with its
// own server (UPLOAD / WIFI_SETUP), take it back when leaving.
void webAdminSuspend();
void webAdminResume();

// True while the device shouldn't accept dangerous operations
// (currently only RINGING).
void webAdminSetBusy(bool busy);

// "http://<ip>/" while WiFi is connected and the portal is running,
// "" otherwise — for a future status-line/icon on the clock face.
String getWebAdminUrl();
