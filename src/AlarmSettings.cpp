#include "AlarmSettings.h"

#include <Preferences.h>
#include <string.h>

namespace {
Preferences prefs;
// Named storedAlarms, not "alarms" — "alarm" collides with POSIX
// unsigned alarm(unsigned) pulled in transitively via unistd.h.
constexpr const char *kPrefsNamespace = "paperwake";
constexpr const char *kBlobKey = "alarms";
constexpr const char *kCountKey = "alarmCount";

AlarmTime storedAlarms[kMaxAlarms];
int alarmCount = 1;
AlarmTime draft = {7, 0, true, kAllDaysMask};

AlarmTime clampEntry(const AlarmTime &entry) {
  AlarmTime out = entry;
  if (out.hour > 23) out.hour = 23;
  if (out.minute > 59) out.minute = 59;
  out.daysMask &= kAllDaysMask;
  return out;
}

void persist() {
  prefs.putBytes(kBlobKey, storedAlarms, sizeof(storedAlarms));
  prefs.putUChar(kCountKey, (uint8_t)alarmCount);
}

AlarmTime alarmAt(int index) {
  if (index < 0 || index >= alarmCount) index = 0;
  return storedAlarms[index];
}
} // namespace

void initAlarmSettings() {
  prefs.begin(kPrefsNamespace, false);

  for (int i = 0; i < kMaxAlarms; i++) storedAlarms[i] = {7, 0, i == 0, kAllDaysMask};
  alarmCount = 1;

  size_t blobLen = prefs.getBytesLength(kBlobKey);
  if (blobLen == sizeof(storedAlarms)) {
    // Multi-alarm blob already present (or written by a previous boot
    // after migration) — load it as-is.
    prefs.getBytes(kBlobKey, storedAlarms, sizeof(storedAlarms));
    uint8_t count = prefs.getUChar(kCountKey, 1);
    if (count < 1) count = 1;
    if (count > kMaxAlarms) count = kMaxAlarms;
    alarmCount = count;
    // Defensive sanitize of whatever's in flash (partial NVS writes,
    // downgrades/versions with different struct sizes, etc.).
    for (int i = 0; i < alarmCount; i++) storedAlarms[i] = clampEntry(storedAlarms[i]);
  } else {
    // Migration from the pre-multi-alarm single-alarm keys
    // (hour/minute/enabled). One alarm, repeating every day — exactly
    // the old behavior. The old keys are left in place (harmless
    // residue) rather than removed: removing them would make a
    // downgrade to a pre-multi-alarm firmware reset the alarm instead
    // of restoring its last-known value.
    storedAlarms[0].hour = prefs.getUChar("hour", 7);
    storedAlarms[0].minute = prefs.getUChar("minute", 0);
    storedAlarms[0].enabled = prefs.getBool("enabled", true);
    storedAlarms[0].daysMask = kAllDaysMask;
    alarmCount = 1;
    persist();
  }

  draft = storedAlarms[0];
}

int getAlarmCount() { return alarmCount; }

AlarmTime getAlarm() { return alarmAt(0); }

AlarmTime getAlarmAt(int index) { return alarmAt(index); }

void setAlarmEnabled(bool enabled) { setAlarmEnabledAt(0, enabled); }

void setAlarmEnabledAt(int index, bool enabled) {
  if (index < 0 || index >= alarmCount) return;
  storedAlarms[index].enabled = enabled;
  persist();
}

int addAlarm() {
  if (alarmCount >= kMaxAlarms) return -1;
  storedAlarms[alarmCount] = {7, 0, false, kAllDaysMask};
  alarmCount++;
  persist();
  return alarmCount - 1;
}

bool removeAlarm(int index) {
  if (index <= 0 || index >= alarmCount) return false; // slot 0 is permanent
  for (int i = index; i < alarmCount - 1; i++) storedAlarms[i] = storedAlarms[i + 1];
  alarmCount--;
  // Keep the discarded tail out of the blob so a later count-bump
  // can't resurrect stale data.
  storedAlarms[alarmCount] = {7, 0, false, kAllDaysMask};
  persist();
  return true;
}

void setAlarmAt(int index, const AlarmTime &entry) {
  if (index < 0 || index >= alarmCount) return;
  storedAlarms[index] = clampEntry(entry);
  persist();
}

bool getNextAlarm(const struct tm &now, AlarmTime &outAlarm, int &minutesUntil) {
  int wday = now.tm_wday;
  if (wday < 0 || wday > 6) return false; // invalid tm — no sane answer
  int nowMinuteOfDay = now.tm_hour * 60 + now.tm_min;

  // Scan forward day by day from today; dayOffset 0 only accepts
  // strictly-later minutes today (an alarm matching *right now* is the
  // trigger logic's job, not this function's).
  for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
    uint8_t dayBit = (uint8_t)(1 << ((wday + dayOffset) % 7));
    for (int i = 0; i < alarmCount; i++) {
      const AlarmTime &a = storedAlarms[i];
      if (!a.enabled) continue;
      if (!(a.daysMask & dayBit)) continue;
      int target = a.hour * 60 + a.minute;
      int delta = target - nowMinuteOfDay;
      if (dayOffset == 0 && delta <= 0) continue;
      outAlarm = a;
      minutesUntil = dayOffset * 1440 + delta;
      return true;
    }
  }
  return false;
}

void beginAlarmEdit() { draft = storedAlarms[0]; }

void adjustAlarmEditMinutes(int deltaMinutes) {
  int totalMinutes = draft.hour * 60 + draft.minute;
  totalMinutes = ((totalMinutes + deltaMinutes) % 1440 + 1440) % 1440;
  draft.hour = totalMinutes / 60;
  draft.minute = totalMinutes % 60;
}

void cancelAlarmEdit() {
  // Draft simply discarded; persisted alarm is untouched.
}

void commitAlarmEdit() {
  storedAlarms[0].hour = draft.hour;
  storedAlarms[0].minute = draft.minute;
  persist();
}

AlarmTime getAlarmDraft() { return draft; }
