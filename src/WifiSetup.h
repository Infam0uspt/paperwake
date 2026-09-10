#pragma once

#include <WString.h>

// "Connect to wifi" AP-mode provisioning portal. The board becomes its
// own access point ("PaperWake") with a small captive-portal-style web
// UI (scan nearby networks, enter credentials) so the user can get the
// board onto their home wifi from a phone/computer, without ever
// needing to reflash secrets.h. Entered automatically from main.cpp
// when boot never obtains a valid time (see TimeSync.h), or manually
// via the Settings menu's "WiFi" item — same module either way.
// Mirrors the SoundUpload.h/.cpp module's begin/handle/end lifecycle.

constexpr const char *kWifiSetupApSsid = "PaperWake";
constexpr const char *kWifiSetupApPassword = "WakeUpCall2026";

void beginWifiSetup();

// Call every loop() iteration while Mode::WIFI_SETUP is active —
// services the captive-portal DNS redirect, the web UI, and polls the
// non-blocking connect attempt started by the web UI's "Connect"
// button (see WifiSetup.cpp — deliberately not a blocking WiFi.begin()
// wait loop, since that would freeze the web UI/DNS while connecting).
void handleWifiSetup();

// Turns the AP off and returns to plain STA mode. Any STA connection
// already established (e.g. the one just provisioned) is left as-is.
void endWifiSetup();

String getWifiSetupStatusLine();

// True once a connection attempt started via the web UI has actually
// succeeded (credentials already saved via TimeSync.h's
// saveWifiCredentials() at that point) — main.cpp uses this to time a
// brief "Connected!" display window before switching back to
// Mode::CLOCK, the same pattern as the alarm's showingAlarmCountdown.
bool isWifiSetupConnected();

// "WIFI:T:WPA;S:<ssid>;P:<password>;;" — the standard QR payload
// format phones use to auto-join a wifi network, built from this
// module's own AP credentials (not a URL, unlike SoundUpload's QR).
String getWifiSetupApQrPayload();
