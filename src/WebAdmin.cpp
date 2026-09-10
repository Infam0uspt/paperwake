#include "WebAdmin.h"

#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "AlarmSettings.h"
#include "ConfigBackup.h"
#include "SettingsMenu.h"
#include "Sound.h"
#include "SoundUpload.h"
#include "TimeSync.h"
#include "Version.h"
#include "WebPortalStyle.h"
#include "secrets.h"

namespace {

WebServer server(80);
bool handlersRegistered = false;
bool serverRunning = false;   // server.begin() succeeded and not suspended
bool suspended = false;       // UPLOAD / WIFI_SETUP own port 80 while active
bool deviceBusy = false;      // true while RINGING — dangerous ops refused
bool mdnsRunning = false;     // MDNS.begin("paperwake") succeeded

// OTA/config-import upload state (same pattern as SoundUpload.cpp's
// uploadFile handling): handleClient() processes the entire upload in
// one blocking call, so these are only touched inside the upload
// callback chain.
bool otaActive = false;
bool otaOk = false;
String otaMessage;
bool importActive = false;
String importJson;
bool importResult = false;
String importSummary;

// ---- WiFi network switch (the /wifi page) ----
// The ESP32 has one radio: connecting to the new network necessarily
// drops the current one, so the portal goes offline during the
// attempt. The switch is therefore async: POST /wifi/connect kicks it
// off and answers immediately, and handleWebAdmin() drives the state
// machine on every loop() iteration. On success the new credentials
// are persisted; on timeout the old ones are re-applied (and were
// never overwritten), so the portal comes back on the previous network
// — worst case after maintainWifiAndTime()'s own retry kicks in.
enum class WifiSwitchState { Idle, Trying, Restoring };
WifiSwitchState wifiSwitchState = WifiSwitchState::Idle;
String wifiSwitchNewSsid, wifiSwitchNewPass;
String wifiSwitchOldSsid, wifiSwitchOldPass;
unsigned long wifiSwitchStartMs = 0;
constexpr unsigned long kWifiSwitchTimeoutMs = 30000;
constexpr unsigned long kWifiSwitchRestoreTimeoutMs = 30000;

// Same NVS namespace/keys TimeSync.cpp's saveWifiCredentials() uses —
// reading through here (with secrets.h as the fallback default)
// reflects exactly what the next connectWifi() would use.
Preferences wifiPrefs;

String savedWifiSsid() { return wifiPrefs.getString("wifiSsid", WIFI_SSID); }
String savedWifiPass() { return wifiPrefs.getString("wifiPass", WIFI_PASSWORD); }

const char *kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

String htmlPage(const String &title, const String &body) {
  String html = String("<html><head><title>") + title + "</title>" + kWebPortalHead +
                "<style>"
                "body{font-family:system-ui,sans-serif;margin:16px;max-width:560px}"
                "nav a{margin-right:14px}h3{margin:14px 0 6px}"
                "form{display:inline-block;margin:2px 0}"
                "table{border-collapse:collapse}td,th{padding:2px 10px 2px 0;text-align:left}"
                ".warn{color:#b00}"
                "</style></head><body>"
                "<nav><a href='/'>Status</a><a href='/alarms'>Alarms</a>"
                "<a href='/settings'>Settings</a><a href='/wifi'>WiFi</a>"
                "<a href='/system'>System</a>"
                "<a href='/upload-portal'>Sounds</a></nav><hr>" +
                body + "</body></html>";
  return html;
}

// ---------------- Dashboard ----------------

void handleDashboard() {
  String body = String("<h3>PaperWake v") + kFirmwareVersion + "</h3><table>";
  struct tm now = {}; // zeroed: getNextAlarm() reads tm_wday even when getCurrentTime() failed
  if (getCurrentTime(now)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
    body += String("<tr><th>Time</th><td>") + buf + "</td></tr>";
  } else {
    body += "<tr><th>Time</th><td class='warn'>not synced</td></tr>";
  }
  body += String("<tr><th>WiFi</th><td>") + (isWifiConnected() ? "connected" : "disconnected") + "</td></tr>";

  AlarmTime next;
  int minutesUntil;
  if (getNextAlarm(now, next, minutesUntil)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d in %dh%02dm (%s)", next.hour, next.minute, minutesUntil / 60,
             minutesUntil % 60, next.enabled ? "on" : "off");
    body += String("<tr><th>Next alarm</th><td>") + buf + "</td></tr>";
  } else {
    body += "<tr><th>Next alarm</th><td>none enabled</td></tr>";
  }
  body += "</table>";
  server.send(200, "text/html", htmlPage("PaperWake", body));
}

// ---------------- Alarms ----------------

// Shared alarms-table renderer: rows from index `from` to `to`, each
// with an inline edit form (hour/minute, on/off, day checkboxes).
void appendAlarmRows(String &html, int from, int to) {
  html += "<table><tr><th>#</th><th>Time</th><th>On</th><th>Days</th><th></th></tr>";
  for (int i = from; i < to && i < getAlarmCount(); i++) {
    AlarmTime a = getAlarmAt(i);
    html += String("<tr><td>") + i + "</td><td>"
            "<form method='POST' action='/alarm/save'>"
            "<input type='hidden' name='id' value='" + i + "'>"
            "<input type='number' name='hour' min='0' max='23' value='" + a.hour + "' style='width:52px'>:"
            "<input type='number' name='minute' min='0' max='59' value='" + a.minute + "' style='width:52px'>";
    html += "<label><input type='checkbox' name='enabled' value='1'";
    if (a.enabled) html += " checked";
    html += ">on</label><br>";
    for (int d = 0; d < 7; d++) {
      html += String("<label><input type='checkbox' name='d") + d + "' value='1'";
      if (a.daysMask & (1 << d)) html += " checked";
      html += ">" + String(kDays[d]) + "</label> ";
    }
    html += " <input type='submit' value='Save'></form></td>";
    if (i > 0) {
      html += String("<td><form method='POST' action='/alarm/delete'>"
                     "<input type='hidden' name='id' value='") + i +
             "'><input type='submit' value='Delete'></form></td>";
    } else {
      html += "<td></td>"; // slot 0 is permanent
    }
    html += "</tr>";
  }
  html += "</table>";
}

void handleAlarms() {
  String body = "<h3>Alarms</h3>";
  appendAlarmRows(body, 0, getAlarmCount());
  if (getAlarmCount() < kMaxAlarms) {
    body += "<form method='POST' action='/alarm/add'><input type='submit' value='Add alarm'></form>";
  } else {
    body += "<p>(maximum of 8 alarms reached)</p>";
  }
  server.send(200, "text/html", htmlPage("PaperWake alarms", body));
}

void handleAlarmAdd() {
  addAlarm();
  server.sendHeader("Location", "/alarms");
  server.send(303);
}

void handleAlarmDelete() {
  int id = server.arg("id").toInt();
  removeAlarm(id);
  server.sendHeader("Location", "/alarms");
  server.send(303);
}

void handleAlarmSave() {
  int id = server.arg("id").toInt();
  AlarmTime a;
  a.hour = server.arg("hour").toInt();
  a.minute = server.arg("minute").toInt();
  a.enabled = server.arg("enabled") == "1";
  a.daysMask = 0;
  for (int d = 0; d < 7; d++) {
    if (server.arg("d" + String(d)) == "1") a.daysMask |= (1 << d);
  }
  setAlarmAt(id, a);
  server.sendHeader("Location", "/alarms");
  server.send(303);
}

// ---------------- Settings ----------------

void handleSettings() {
  String body = "<h3>Settings</h3><p>Same values as the on-device "
                "Settings menu. Sound selection is managed on the "
                "<a href='/upload-portal'>Sounds</a> page.</p><table>";
  for (int i = 0; i < getSettingsItemCount(); i++) {
    const SettingItem &item = getSettingsItem(i);
    if (item.type != SettingType::FunctionalInt) continue;
    body += String("<tr><td>") + item.label + "</td><td>"
            "<form method='POST' action='/setting/set'>"
            "<input type='hidden' name='index' value='" + i + "'>"
            "<input type='number' name='value' min='" + item.minValue + "' max='" + item.maxValue +
            "' step='" + item.step + "' value='" + getSettingValue(i) + "' style='width:64px'>";
    if (item.unit[0] != '\0') body += String(" ") + item.unit;
    body += " <input type='submit' value='Set'></form></td></tr>";
  }
  body += "</table>";
  server.send(200, "text/html", htmlPage("PaperWake settings", body));
}

void handleSettingSet() {
  int index = server.arg("index").toInt();
  int value = server.arg("value").toInt();
  if (setSettingValue(index, value)) {
    // Same side effects the physical menu's commit path triggers.
    syncTime();                 // in case the timezone setting was it
    invalidatePreloadedSound(); // in case Volume/Fade-in were it (stale preload parameters)
  }
  server.sendHeader("Location", "/settings");
  server.send(303);
}

// ---------------- System / OTA / backup ----------------

void handleSystem() {
  String body = String("<h3>System</h3><p>Firmware: <b>") + kFirmwareVersion + "</b></p>";

  body += "<h4>Update firmware (OTA)</h4>"
          "<p>Upload a <code>.bin</code> built for this project "
          "(PlatformIO: <code>pio run -e esp32-s3-devkitc-1</code>, file at "
          "<code>.pio/build/esp32-s3-devkitc-1/firmware.bin</code>).</p>"
          "<form method='POST' action='/ota' enctype='multipart/form-data'>"
          "<input type='file' name='firmware' accept='.bin'> "
          "<input type='submit' value='Update'></form>"
          "<p class='warn'>Do not power off the clock while updating.</p>";

  body += "<h4>Configuration backup</h4>"
          "<p><a href='/config/export'>Download configuration</a> (JSON) &middot; "
          "Restore: <form method='POST' action='/config/import' enctype='multipart/form-data'>"
          "<input type='file' name='config' accept='.json'> "
          "<input type='submit' value='Restore'></form></p>";

  body += "<h4>Network</h4>"
          "<p>Connected to: <b>" + WiFi.SSID() + "</b> &middot; "
          "<a href='/wifi'>Switch WiFi network</a></p>";

  body += "<h4>Restart</h4>"
          "<form method='POST' action='/restart'><input type='submit' value='Restart clock'></form>";

  server.send(200, "text/html", htmlPage("PaperWake system", body));
}

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaActive = true;
    otaOk = false;
    otaMessage = "";
    // The upload happens inside one blocking handleClient() call, so
    // deviceBusy can't change under us mid-transfer — checked once here.
    if (deviceBusy) {
      otaMessage = "Device is busy (alarm ringing)";
      otaActive = false;
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaMessage = "Not enough space: " + String(Update.errorString());
      otaActive = false;
    }
  } else if (otaActive && upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaMessage = "Write failed: " + String(Update.errorString());
      otaActive = false;
      Update.abort();
    }
  } else if (otaActive && upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      otaOk = true;
      otaMessage = "Update complete, restarting...";
    } else {
      otaMessage = "Update failed: " + String(Update.errorString());
    }
    otaActive = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (otaActive) Update.abort();
    otaActive = false;
    otaMessage = "Upload aborted";
  }
}

void handleOtaDone() {
  if (otaOk) {
    server.send(200, "text/html", htmlPage("PaperWake OTA", "<h3>" + otaMessage + "</h3>"));
    delay(1000); // let the response drain before the reset
    ESP.restart();
  } else {
    server.send(500, "text/html",
                htmlPage("PaperWake OTA", String("<h3 class='warn'>OTA failed</h3><p>") + otaMessage + "</p>"));
  }
}

void handleConfigExport() {
  server.sendHeader("Content-Disposition", "attachment; filename=paperwake-config.json");
  server.send(200, "application/json", exportConfigJson());
}

void handleConfigImportUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    importActive = true;
    importResult = false;
    importSummary = "";
    importJson = "";
  } else if (importActive && upload.status == UPLOAD_FILE_WRITE) {
    importJson.concat((const char *)upload.buf, upload.currentSize);
  } else if (importActive && upload.status == UPLOAD_FILE_END) {
    importResult = applyConfigJson(importJson, importSummary);
    importActive = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    importActive = false;
    importSummary = "Upload aborted";
  }
}

void handleConfigImportDone() {
  int code = importResult ? 200 : 400;
  server.send(code, "text/html",
              htmlPage("PaperWake config", String("<h3>Restore ") + (importResult ? "ok" : "failed") + "</h3><p>" +
                                               importSummary + "</p><p><a href='/alarms'>Alarms</a> &middot; "
                                               "<a href='/settings'>Settings</a></p>"));
}

void handleRestart() {
  server.send(200, "text/html",
              htmlPage("PaperWake", "<h3>Restarting...</h3><p>The clock will be back in a few seconds.</p>"));
  delay(500);
  ESP.restart();
}

// Sounds page link target: the sound portal lives in SoundUpload.cpp,
// on the same origin, but is only served while the clock is in
// UPLOAD mode — explain that instead of letting the link fail.
void handleUploadPortalRedirect() {
  server.send(200, "text/html",
              htmlPage("PaperWake sounds",
                       "<h3>Sound management</h3><p>The sound upload/manage page is served while the clock is in "
                       "<b>Settings &rarr; Sound &rarr; Upload sound</b> mode (the clock's screen shows the address). "
                       "That keeps SD-card transfers off the always-on admin portal.</p>"));
}

// ---------------- WiFi network switch ----------------

String wifiScanOptionsHtml() {
  // Synchronous scan (~2-3s while connected — the radio hops channels).
  // One-off per page load, acceptable; results feed a datalist so the
  // user can still type a hidden SSID by hand.
  String options;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    options += String("<option value='") + WiFi.SSID(i) + "'>";
  }
  WiFi.scanDelete();
  return options;
}

void handleWifiPage() {
  String body = "<h3>WiFi</h3><table>"
                "<tr><th>Currently connected</th><td>" + WiFi.SSID() + "</td></tr>"
                "<tr><th>Saved network</th><td>" + savedWifiSsid() + "</td></tr>"
                "</table>"
                "<h4>Switch network</h4>"
                "<p>The clock has a single WiFi radio: while it tries the new network, this portal goes offline. "
                "If the new network doesn't work, the clock automatically falls back to <b>" +
                savedWifiSsid() + "</b> within ~30 seconds and this portal comes back on the old address.</p>"
                "<form method='POST' action='/wifi/connect'>"
                "Network: <input list='nets' name='ssid' required><datalist id='nets'>" +
                wifiScanOptionsHtml() + "</datalist><br>"
                "Password: <input type='password' name='pass'><br>"
                "<input type='submit' value='Connect'></form>";
  server.send(200, "text/html", htmlPage("PaperWake wifi", body));
}

void handleWifiConnect() {
  if (wifiSwitchState != WifiSwitchState::Idle) {
    server.send(409, "text/html",
                htmlPage("PaperWake wifi", "<h3>Switch already in progress</h3>"
                                           "<p>Wait for the current attempt to finish (it falls back automatically).</p>"));
    return;
  }
  wifiSwitchNewSsid = server.arg("ssid");
  wifiSwitchNewPass = server.arg("pass");
  if (wifiSwitchNewSsid.length() == 0) {
    server.send(400, "text/html", htmlPage("PaperWake wifi", "<h3>Missing network name</h3>"));
    return;
  }
  wifiSwitchOldSsid = savedWifiSsid();
  wifiSwitchOldPass = savedWifiPass();
  wifiSwitchStartMs = millis();
  wifiSwitchState = WifiSwitchState::Trying;
  // Single-radio reality: this drops the current connection (and this
  // very response's TCP session — that's why the answer below is best-
  // effort and the state machine is async).
  WiFi.begin(wifiSwitchNewSsid.c_str(), wifiSwitchNewPass.c_str());
  server.send(200, "text/html",
              htmlPage("PaperWake wifi",
                       "<h3>Trying \"" + wifiSwitchNewSsid + "\"...</h3>"
                       "<p>The portal will be unreachable for up to ~30 seconds. If the new network works, "
                       "the clock reconnects there and <a href='/'>this portal</a> comes back on the new address. "
                       "If not, the clock falls back to <b>" +
                       wifiSwitchOldSsid + "</b> automatically — just reload this page in a minute.</p>"));
}

void startMdns() {
  if (mdnsRunning) return;
  if (!WiFi.isConnected()) return;
  if (MDNS.begin("paperwake")) {
    mdnsRunning = true;
    Serial.println("[WebAdmin] mDNS started: http://paperwake.local/");
  } else {
    Serial.println("[WebAdmin] mDNS begin failed");
  }
}

void stopMdns() {
  if (!mdnsRunning) return;
  MDNS.end();
  mdnsRunning = false;
}

// Drives the async network switch; runs on every handleWebAdmin()
// call, BEFORE the connectivity/server bookkeeping (while Trying or
// Restoring, the radio is intentionally disconnected and the portal is
// down, but the state machine must keep running).
void handleWifiSwitchPoll() {
  if (wifiSwitchState == WifiSwitchState::Idle) return;

  if (wifiSwitchState == WifiSwitchState::Trying) {
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == wifiSwitchNewSsid) {
      // Success: persist the new credentials (TimeSync.cpp's
      // saveWifiCredentials uses the same NVS keys, so its connectWifi()
      // retries pick them up from here on).
      saveWifiCredentials(wifiSwitchNewSsid, wifiSwitchNewPass);
      wifiSwitchState = WifiSwitchState::Idle;
      stopMdns();
      startMdns();
      Serial.printf("[WebAdmin] WiFi switched to '%s'\n", wifiSwitchNewSsid.c_str());
    } else if (millis() - wifiSwitchStartMs >= kWifiSwitchTimeoutMs) {
      // Failure: restore the previous network. The old credentials were
      // never overwritten, so even if this immediate re-begin fails,
      // maintainWifiAndTime()'s retry loop re-establishes them.
      Serial.printf("[WebAdmin] WiFi switch to '%s' failed, restoring '%s'\n", wifiSwitchNewSsid.c_str(),
                    wifiSwitchOldSsid.c_str());
      WiFi.disconnect(true);
      delay(100);
      WiFi.begin(wifiSwitchOldSsid.c_str(), wifiSwitchOldPass.c_str());
      wifiSwitchStartMs = millis();
      wifiSwitchState = WifiSwitchState::Restoring;
    }
    return;
  }

  // Restoring: wait for the old network (or give up and let
  // maintainWifiAndTime()'s periodic retry finish the job).
  if (WiFi.status() == WL_CONNECTED || millis() - wifiSwitchStartMs >= kWifiSwitchRestoreTimeoutMs) {
    wifiSwitchState = WifiSwitchState::Idle;
  }
}

} // namespace

// ---------------- Lifecycle ----------------

void initWebAdmin() {
  if (handlersRegistered) return;
  wifiPrefs.begin("paperwake", true); // read-only: writes go through TimeSync's saveWifiCredentials()
  server.on("/", HTTP_GET, handleDashboard);
  server.on("/alarms", HTTP_GET, handleAlarms);
  server.on("/alarm/add", HTTP_POST, handleAlarmAdd);
  server.on("/alarm/delete", HTTP_POST, handleAlarmDelete);
  server.on("/alarm/save", HTTP_POST, handleAlarmSave);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/setting/set", HTTP_POST, handleSettingSet);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/system", HTTP_GET, handleSystem);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.on("/config/export", HTTP_GET, handleConfigExport);
  server.on("/config/import", HTTP_POST, handleConfigImportDone, handleConfigImportUpload);
  server.on("/restart", HTTP_POST, handleRestart);
  server.on("/upload-portal", HTTP_GET, handleUploadPortalRedirect);
  handlersRegistered = true;
}

void handleWebAdmin() {
  if (suspended || !handlersRegistered) return;
  // The WiFi-switch state machine runs unconditionally: while Trying/
  // Restoring, the radio is intentionally off the current network and
  // everything below (server bookkeeping) is a no-op until it lands
  // somewhere.
  handleWifiSwitchPoll();
  bool connected = isWifiConnected();
  // Self-healing begin(): a reconnecting WiFi invalidates the old
  // listening socket, so re-begin whenever the radio is up and the
  // server isn't (and stop while it's down so we don't hold stale
  // sockets).
  if (connected && !serverRunning) {
    // ESP32's WebServer::begin() returns void (it re-listens lazily
    // per-connection), so "running" just tracks our own bookkeeping.
    server.begin();
    serverRunning = true;
    startMdns();
  } else if (!connected && serverRunning) {
    server.stop();
    serverRunning = false;
    stopMdns();
  } else if (connected && serverRunning && !mdnsRunning) {
    // WiFi came back but mDNS didn't restart (e.g. after a transient
    // disconnection where the server socket survived).
    startMdns();
  }
  if (!serverRunning) return;
  server.handleClient();
}

void webAdminSuspend() {
  suspended = true;
  if (serverRunning) {
    server.stop();
    serverRunning = false;
  }
  stopMdns();
}

void webAdminResume() {
  suspended = false;
  // mDNS will be restarted by handleWebAdmin() on the next loop()
  // iteration once WiFi is up and the server is re-started.
}

void webAdminSetBusy(bool busy) { deviceBusy = busy; }

String getWebAdminUrl() {
  if (suspended || !serverRunning || !isWifiConnected()) return "";
  if (mdnsRunning) return "http://paperwake.local/";
  return "http://" + WiFi.localIP().toString() + "/";
}