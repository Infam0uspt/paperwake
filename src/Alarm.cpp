#include "Alarm.h"

#include "AlarmSettings.h"
#include "SettingsMenu.h"

namespace {
int lastTriggeredMinuteOfDay = -1;
int snoozeUntilMinuteOfDay = -1; // -1 = no active snooze
// Index of the alarm the current ringing session (or pending snooze)
// belongs to — informational for now, but lets future code (e.g. a
// per-alarm sound choice) know whose alarm is ringing.
int triggeredAlarmIndex = 0;
} // namespace

bool checkAlarmTrigger(const struct tm &now) {
  int minuteOfDay = now.tm_hour * 60 + now.tm_min;

  if (snoozeUntilMinuteOfDay >= 0) {
    if (minuteOfDay != snoozeUntilMinuteOfDay) return false;
    snoozeUntilMinuteOfDay = -1;
    lastTriggeredMinuteOfDay = minuteOfDay;
    return true;
  }

  // The once-per-minute guard is global (not per alarm): if two alarms
  // coincide on the same minute, only one ringing session starts —
  // main.cpp's checkAlarmTrigger() also fires from two separate places
  // within the same minute, which a per-alarm guard would defeat.
  if (minuteOfDay == lastTriggeredMinuteOfDay) return false;

  int count = getAlarmCount();
  for (int i = 0; i < count; i++) {
    AlarmTime alarm = getAlarmAt(i);
    if (!alarm.enabled) continue;
    if (now.tm_hour != alarm.hour || now.tm_min != alarm.minute) continue;
    if (now.tm_wday < 0 || now.tm_wday > 6) continue;
    if (!(alarm.daysMask & (1 << now.tm_wday))) continue;

    lastTriggeredMinuteOfDay = minuteOfDay;
    triggeredAlarmIndex = i;
    // One-shot alarm (daysMask == 0): fire once, then disable itself
    // so the next day stays quiet until re-enabled.
    if (alarm.daysMask == 0) setAlarmEnabledAt(i, false);
    return true;
  }
  return false;
}

void snoozeAlarm(const struct tm &now) {
  int minuteOfDay = now.tm_hour * 60 + now.tm_min;
  snoozeUntilMinuteOfDay = (minuteOfDay + getSnoozeMinutes()) % (24 * 60);
}

void cancelSnooze() {
  snoozeUntilMinuteOfDay = -1;
}
