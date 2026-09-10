#pragma once

#include "Alarm.h"

void initAlarmSettings(); // loads persisted alarms from NVS (or migrates the pre-multi-alarm single-alarm keys)

// Multi-alarm store. Slot 0 is the "primary" alarm — the one the
// physical controls (clock-face edit, MENU on/off toggle) always act
// on. Slots beyond 0 are created/edited/removed via the web admin
// portal (WebAdmin, Phase 2); addAlarm()/removeAlarm()/setAlarmAt()
// below are its write API.
int getAlarmCount(); // always >= 1 — slot 0 exists even when disabled

AlarmTime getAlarm();               // the persisted, active primary alarm (slot 0)
AlarmTime getAlarmAt(int index);    // out-of-range index returns slot 0
void setAlarmEnabled(bool enabled); // slot 0, persists immediately
void setAlarmEnabledAt(int index, bool enabled); // any slot, persists immediately

// Web-portal write API:
int addAlarm(); // appends a disabled 07:00 alarm, returns its index (-1 if full)
bool removeAlarm(int index); // removes a slot, compacting the rest; slot 0 can't be removed
void setAlarmAt(int index, const AlarmTime &entry); // full write (time/days/enabled), persists

// Next future occurrence of any *enabled* alarm, honoring each one's
// daysMask. Returns false when no alarm is enabled. `minutesUntil` is
// 1..7*1440 — strictly in the future, so an alarm matching the current
// minute (already firing) is not returned; use checkAlarmTrigger() for
// that. Used by the ramp (Light.cpp) and the clock-face alarm line
// (main.cpp's displayedAlarm()).
bool getNextAlarm(const struct tm &now, AlarmTime &outAlarm, int &minutesUntil);

// Editing session (always on slot 0): adjustments happen on a draft
// copy and are only applied to the real alarm (and persisted) on commit.
void beginAlarmEdit();
void adjustAlarmEditMinutes(int deltaMinutes); // wraps across the whole day
void cancelAlarmEdit();
void commitAlarmEdit();
AlarmTime getAlarmDraft();
