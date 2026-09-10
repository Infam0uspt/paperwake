#include "ConfigBackup.h"

#include <ArduinoJson.h>

#include "AlarmSettings.h"
#include "SettingsMenu.h"
#include "Sound.h"

namespace {
constexpr int kConfigSchema = 1;
}

String exportConfigJson() {
  JsonDocument doc;
  doc["schema"] = kConfigSchema;
  doc["type"] = "paperwake-config";

  JsonObject settings = doc["settings"].to<JsonObject>();
  for (int i = 0; i < getSettingsItemCount(); i++) {
    const SettingItem &item = getSettingsItem(i);
    if (item.type != SettingType::FunctionalInt) continue;
    // Keyed by the item's label — the label is the stable human-facing
    // identifier used by both the physical menu and this portal.
    settings[item.label] = getSettingValue(i);
  }

  JsonArray alarms = doc["alarms"].to<JsonArray>();
  for (int i = 0; i < getAlarmCount(); i++) {
    AlarmTime a = getAlarmAt(i);
    JsonObject o = alarms.add<JsonObject>();
    o["hour"] = a.hour;
    o["minute"] = a.minute;
    o["enabled"] = a.enabled;
    o["daysMask"] = a.daysMask;
  }

  doc["sound"] = getSelectedSoundFile();

  String out;
  serializeJsonPretty(doc, out);
  return out;
}

bool applyConfigJson(const String &json, String &summaryOut) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    summaryOut = "Parse error: " + String(err.c_str());
    return false;
  }
  int schema = doc["schema"] | 0;
  const char *type = doc["type"] | "";
  if (strcmp(type, "paperwake-config") != 0 || schema != kConfigSchema) {
    summaryOut = "Not a PaperWake config (or unsupported schema version)";
    return false;
  }

  int settingsApplied = 0;
  JsonObject settings = doc["settings"];
  for (JsonPair kv : settings) {
    // Find the settings item by label so a reordered/reduced menu
    // doesn't silently apply values to the wrong row.
    for (int i = 0; i < getSettingsItemCount(); i++) {
      if (strcmp(getSettingsItem(i).label, kv.key().c_str()) == 0) {
        if (setSettingValue(i, kv.value().as<int>())) settingsApplied++;
        break;
      }
    }
  }

  int alarmsApplied = 0;
  JsonArray alarms = doc["alarms"];
  if (!alarms.isNull()) {
    // Shrink the live store back down to just slot 0 (never removed),
    // then refill — making the imported set *replace*, not merge.
    while (getAlarmCount() > 1) removeAlarm(getAlarmCount() - 1);
    bool first = true;
    for (JsonObject o : alarms) {
      AlarmTime a;
      a.hour = o["hour"] | 7;
      a.minute = o["minute"] | 0;
      a.enabled = o["enabled"] | false;
      a.daysMask = o["daysMask"] | kAllDaysMask;
      if (first) {
        setAlarmAt(0, a);
        first = false;
      } else {
        int idx = addAlarm();
        if (idx < 0) break;
        setAlarmAt(idx, a);
      }
      alarmsApplied++;
    }
  }

  const char *sound = doc["sound"] | "";
  setSelectedSoundFile(String(sound));
  invalidatePreloadedSound();

  summaryOut = "Applied " + String(settingsApplied) + " settings, " + String(alarmsApplied) + " alarms";
  return true;
}