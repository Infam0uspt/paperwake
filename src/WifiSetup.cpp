#include "WifiSetup.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "EpaperDisplay.h"
#include "TimeSync.h"
#include "UiStrings.h"
#include "WebPortalStyle.h"

namespace {

constexpr unsigned long kConnectTimeoutMs = 15000; // matches TimeSync.cpp's boot-connect timeout

WebServer server(80);
DNSServer dnsServer;
bool handlersRegistered = false;

String statusLine = strings().wifiWaiting;
String lastScanHtml; // populated by handleScan(); empty until the user scans at least once
bool connecting = false;
unsigned long connectStartMs = 0;
String pendingSsid;
String pendingPassword;
bool connectedFlag = false;

void handleRoot() {
  String html = String("<html><head>") + kWebPortalHead + "</head><body><h3>PaperWake - Connect to wifi</h3><p>" +
                 statusLine + "</p>";
  if (connecting) {
    // No JS polling needed — the browser just reloads itself every 2s
    // until handleWifiSetup()'s state machine (WifiSetup.cpp) resolves
    // the attempt and statusLine changes to "Connected!"/"Connection failed".
    html = String("<html><head><meta http-equiv='refresh' content='2'>") + kWebPortalHead +
           "</head><body>"
           "<h3>PaperWake - Connect to wifi</h3><p>" +
           statusLine + "</p></body></html>";
    server.send(200, "text/html", html);
    return;
  }

  html += "<p><a href='/scan'>Scan for networks</a></p>";
  html += lastScanHtml;
  html +=
      "<form method='POST' action='/connect'>"
      "<input type='text' name='ssid' id='ssid' placeholder='Network name' required>"
      "<input type='password' name='password' placeholder='Password'>"
      "<input type='submit' value='Connect'>"
      "</form></body></html>";
  server.send(200, "text/html", html);
}

// Blocking, like TimeSync.cpp's own connectWifi() — a scan takes a few
// seconds regardless, same accepted tradeoff already used there.
void handleScan() {
  Serial.println("[WifiSetup] Scanning...");
  unsigned long scanStartMs = millis();
  int count = WiFi.scanNetworks();
  Serial.printf("[WifiSetup] Scan done: count=%d, took %lums\n", count, millis() - scanStartMs);
  if (count <= 0) {
    lastScanHtml = "<p>No networks found.</p>";
  } else {
    String html = "<ul>";
    for (int i = 0; i < count; i++) {
      // Clicking a result just fills the SSID field (same small
      // inline-onclick-JS pattern SoundUpload.cpp uses for its rename
      // popup) — no escaping of the SSID text, consistent with how
      // filenames are handled unescaped there too on this
      // trusted-local-network-only portal.
      html += "<li><button type='button' onclick=\"document.getElementById('ssid').value='" + WiFi.SSID(i) +
              "'\">" + WiFi.SSID(i) + "</button><span>" + String(WiFi.RSSI(i)) + " dBm</span></li>";
    }
    html += "</ul>";
    lastScanHtml = html;
  }
  WiFi.scanDelete();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleConnect() {
  if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  pendingSsid = server.arg("ssid");
  pendingPassword = server.arg("password");
  // Non-blocking: WiFi.begin() itself returns immediately, the actual
  // connect result is polled in handleWifiSetup() below. A blocking
  // wait here (like TimeSync.cpp's connectWifi()) would freeze
  // dnsServer/server.handleClient() for the whole attempt, breaking
  // the captive portal for whoever's connected to the AP.
  WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
  connecting = true;
  connectStartMs = millis();
  statusLine = String(strings().wifiConnectingPrefix) + pendingSsid + "...";
  updateWifiSetupStatusPartial();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Diagnostic only — logs where a phone/computer's connection to our AP
// actually gets stuck (never associates at all vs. associates but
// never completes DHCP), since neither the phone's own "Obtaining IP
// address..." spinner nor this module's other logging show that.
void onWifiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("[WifiSetup] A client associated with the AP");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("[WifiSetup] A client disassociated from the AP");
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.println("[WifiSetup] Assigned an IP address to a client (DHCP done)");
      break;
    default:
      break;
  }
}

} // namespace

void beginWifiSetup() {
  statusLine = strings().wifiWaiting;
  connecting = false;
  connectedFlag = false;
  lastScanHtml = "";

  WiFi.onEvent(onWifiEvent);

  // AP+STA together: the AP must stay up so the phone/computer stays
  // connected to the board while STA attempts the newly-entered
  // network in the background.
  WiFi.mode(WIFI_AP_STA);
  // If this mode was entered right after a failed boot-time connect
  // (TimeSync.cpp's connectWifi()), the underlying WiFi driver keeps
  // silently retrying that (already known-bad) connection in the
  // background — confirmed live: it blocks WiFi.scanNetworks() outright
  // (instant WIFI_SCAN_FAILED) and its channel-hopping visibly delayed
  // a phone's own association with the AP. Cancel it so the radio sits
  // idle, serving only the AP, until the user's own "Connect" submit
  // starts a real STA attempt.
  WiFi.disconnect();
  WiFi.softAP(kWifiSetupApSsid, kWifiSetupApPassword);
  dnsServer.start(53, "*", WiFi.softAPIP());

  if (!handlersRegistered) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/scan", HTTP_GET, handleScan);
    server.on("/connect", HTTP_POST, handleConnect);
    // Catches captive-portal probe URLs phones/laptops request
    // automatically (e.g. /generate_204, /hotspot-detect.html) so they
    // pop up this page instead of reporting "no internet".
    server.onNotFound(handleRoot);
    handlersRegistered = true;
  }
  server.begin();
  Serial.printf("[WifiSetup] AP '%s' started at %s\n", kWifiSetupApSsid, WiFi.softAPIP().toString().c_str());
}

void handleWifiSetup() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (!connecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    saveWifiCredentials(pendingSsid, pendingPassword);
    statusLine = strings().wifiConnected;
    connecting = false;
    connectedFlag = true;
    updateWifiSetupStatusPartial();
    Serial.printf("[WifiSetup] Connected to %s\n", pendingSsid.c_str());
  } else if (millis() - connectStartMs >= kConnectTimeoutMs) {
    statusLine = strings().wifiFailed;
    connecting = false;
    WiFi.disconnect(); // stop the failed STA attempt; the AP itself stays up
    updateWifiSetupStatusPartial();
    Serial.printf("[WifiSetup] Failed to connect to %s\n", pendingSsid.c_str());
  }
}

void endWifiSetup() {
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

String getWifiSetupStatusLine() { return statusLine; }

bool isWifiSetupConnected() { return connectedFlag; }

String getWifiSetupApQrPayload() {
  return String("WIFI:T:WPA;S:") + kWifiSetupApSsid + ";P:" + kWifiSetupApPassword + ";;";
}
