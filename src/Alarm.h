#pragma once

#include <time.h>

// Maximum number of stored alarms (was exactly 1 before the
// multi-alarm rework). Bounded by UI space, not by NVS — the blob is
// tiny. The web admin portal (WebAdmin, Phase 2) creates/removes
// alarms; the physical controls always edit alarm 0 (the "primary").
constexpr int kMaxAlarms = 8;

// Days-of-week bitmask: bit 0 = Sunday .. bit 6 = Saturday (matches
// struct tm's tm_wday numbering, so the test is just
// `daysMask & (1 << now.tm_wday)`).
// 0x7F = repeats every day (the pre-multi-alarm behavior).
// 0    = one-shot: fires once, then auto-disables itself.
constexpr uint8_t kAllDaysMask = 0x7F;

struct AlarmTime {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  uint8_t daysMask;
};

// Returns true exactly once per matching minute (guards against
// re-triggering every loop iteration within the same minute). Checks
// all persisted alarms from AlarmSettings, honoring each one's
// daysMask. One-shot alarms (daysMask == 0) disable themselves here.
bool checkAlarmTrigger(const struct tm &now);

// Suppresses the normal schedule for kSnoozeMinutes and triggers
// checkAlarmTrigger() once instead when that time is reached. The
// snooze re-fires regardless of the originating alarm's daysMask —
// the user explicitly asked for more sleep.
void snoozeAlarm(const struct tm &now);

// Cancels a pending snooze without triggering it. Call when the alarm
// is dismissed for good while a snooze is still scheduled — otherwise
// that stale snooze would still fire checkAlarmTrigger() later.
void cancelSnooze();
