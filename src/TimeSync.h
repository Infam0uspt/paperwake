#pragma once

#include <time.h>
#include <WString.h>

// Connects to WiFi and starts SNTP time sync. Blocks until either
// connected+synced or the attempt times out (logged via Serial).
void beginWifiAndTime();

// Reconnects WiFi if disconnected and re-syncs SNTP. Cheap to call
// often; only does work when needed. Call periodically from loop().
void maintainWifiAndTime();

// Re-applies the current UTC-offset setting (from SettingsMenu.h) and
// re-syncs immediately. Call right after the user changes the
// timezone setting — otherwise it wouldn't take effect until the
// next hourly resync.
void syncTime();

bool isTimeSynced();

// Persists WiFi credentials to NVS so future boots (and the normal
// reconnect logic in connectWifi()) use them instead of the
// compiled-in secrets.h defaults. Called by WifiSetup.cpp once a
// manually-entered network connects successfully.
void saveWifiCredentials(const String &ssid, const String &password);

// Live WiFi radio state (WiFi.status() == WL_CONNECTED), independent
// of isTimeSynced() — the clock keeps trusting its last-known time
// while disconnected (see maintainWifiAndTime()), so this is the
// signal EpaperDisplay.cpp uses to show/hide the "no wifi" icon.
bool isWifiConnected();

// Wrapper around getLocalTime(). Returns false (and leaves outTime
// untouched) if time isn't available yet. When a DS3231 RTC is
// enabled and NTP hasn't synced yet, falls back to the RTC's time.
bool getCurrentTime(struct tm &outTime);

// Initializes the optional DS3231 RTC (if the RTC setting is On).
// Called from setup() after SettingsMenu is ready.
void initRtc();

