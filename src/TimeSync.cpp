#include "TimeSync.h"

#include <Preferences.h>
#include <WiFi.h>
#include "SettingsMenu.h"
#include "secrets.h"
#include "Rtc.h"

namespace {

constexpr const char *kNtpServer = "pool.ntp.org";

// Same NVS namespace SettingsMenu.cpp already uses for its own keys —
// no collision (distinct key names), just shared storage.
Preferences prefs;

constexpr unsigned long kWifiConnectTimeoutMs = 15000;
constexpr unsigned long kResyncIntervalMs = 60UL * 60UL * 1000UL; // 1 hour

// Retry cadence once a drop is detected mid-run — deliberately shorter/
// less aggressive than the initial boot connect: kWifiRetryTimeoutMs
// keeps each blocking retry attempt brief, and kWifiRetryIntervalMs
// stops loop() from re-attempting (and re-blocking) every single tick
// while the network stays down.
constexpr unsigned long kWifiRetryTimeoutMs = 5000;
constexpr unsigned long kWifiRetryIntervalMs = 60UL * 1000UL;

bool timeSynced = false;
unsigned long lastResyncMillis = 0;
// Underflows on purpose so the very first retry after boot isn't
// delayed by kWifiRetryIntervalMs.
unsigned long lastWifiRetryMillis = 0 - kWifiRetryIntervalMs;
// Only used to log the disconnect/reconnect edges once, instead of
// spamming every retry attempt.
bool wasConnected = true;

bool connectWifi(unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;

  // Credentials saved via the "Connect to wifi" portal (WifiSetup.cpp)
  // take priority once they exist — secrets.h is only the fallback for
  // a fresh flash that's never been provisioned through the portal.
  String ssid = prefs.getString("wifiSsid", WIFI_SSID);
  String password = prefs.getString("wifiPass", WIFI_PASSWORD);

  WiFi.mode(WIFI_STA);
  // Clears any lingering state from a previous attempt (this boot's or
  // a prior retry's) before starting a fresh one — a well-known
  // mitigation for the ESP32 WiFi driver's intermittent connect
  // failures (measured live: ~40-50% of boots failed to connect within
  // kWifiConnectTimeoutMs, with no other distinguishing factor in the
  // log between a boot that succeeded and one that didn't).
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

} // namespace

void syncTime() {
  // A fixed offset, manually set by the user in the settings menu —
  // no automatic DST. The user shifts it by 1 when summer/winter time
  // changes, rather than relying on a hardcoded region's DST rule.
  // Public (not file-private) so main.cpp can re-apply it immediately
  // after the setting changes, instead of waiting for the next hourly
  // resync.
  configTime(getUtcOffsetHours() * 3600, 0, kNtpServer);

  struct tm t;
  timeSynced = getLocalTime(&t, kWifiConnectTimeoutMs);
  lastResyncMillis = millis();
  if (timeSynced) {
    syncRtcFromNtp();
  }
}

void beginWifiAndTime() {
  prefs.begin("paperwake", false);
  if (!connectWifi(kWifiConnectTimeoutMs)) {
    Serial.println("[TimeSync] WiFi connect failed, will keep retrying");
    return;
  }
  Serial.println("[TimeSync] WiFi connected, syncing time...");
  syncTime();
  Serial.println(timeSynced ? "[TimeSync] Time synced" : "[TimeSync] Time sync failed");
}

void maintainWifiAndTime() {
  if (WiFi.status() != WL_CONNECTED) {
    // Deliberately NOT clearing timeSynced here: the ESP32's system
    // clock keeps ticking on its own hardware timer once set, WiFi/NTP
    // only corrects drift periodically. So a dropped connection no
    // longer freezes the clock face or blocks alarm checks (loop()
    // bails out early whenever getCurrentTime() fails) — it just runs
    // on the last-known time until the next successful resync.
    if (wasConnected) {
      Serial.println("[TimeSync] WiFi lost, keeping last-known time");
      wasConnected = false;
    }
    if (millis() - lastWifiRetryMillis < kWifiRetryIntervalMs) return;
    lastWifiRetryMillis = millis();
    if (!connectWifi(kWifiRetryTimeoutMs)) {
      Serial.println("[TimeSync] WiFi reconnect attempt failed, will retry");
      return;
    }
    Serial.println("[TimeSync] WiFi reconnected");
    wasConnected = true;
  }

  if (!timeSynced || millis() - lastResyncMillis >= kResyncIntervalMs) {
    syncTime();
  }
}

bool isTimeSynced() { return timeSynced; }

bool isWifiConnected() { return WiFi.status() == WL_CONNECTED; }

void saveWifiCredentials(const String &ssid, const String &password) {
  prefs.putString("wifiSsid", ssid);
  prefs.putString("wifiPass", password);
}

bool getCurrentTime(struct tm &outTime) {
  if (timeSynced) {
    return getLocalTime(&outTime, 0);
  }
  if (isRtcPresent()) {
    uint8_t h, m, s;
    if (readRtcTime(h, m, s)) {
      memset(&outTime, 0, sizeof(outTime));
      outTime.tm_hour = h;
      outTime.tm_min = m;
      outTime.tm_sec = s;
      return true;
    }
  }
  return false;
}
