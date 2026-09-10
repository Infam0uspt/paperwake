#pragma once

#include <time.h>

#include "AlarmSettings.h"

void initDisplay();

// One-shot full-screen boot splash: the bunny logo (Icons.h),
// centered on both axes, sized to occupy about 2/3 of the screen's
// height. Call once, right after initDisplay() and before the other
// (potentially slow — WiFi/time sync) setup() steps, so it's visible
// immediately at power-on rather than after those finish. Overwritten
// by the first drawFullClockFace()/drawWifiSetupScreen() call.
void drawBootLogo();

// Full-screen redraw: clock face + date + alarm line. Slower, but
// resets any partial-refresh ghosting. Call on boot and periodically.
// `powerLossWarning` draws a warning banner when the device just detected
// a mains->battery transition (power loss).
void drawFullClockFace(const struct tm &now, int alarmHour, int alarmMinute, bool alarmEnabled, bool powerLossWarning);

// Fast partial refresh of just the HH:MM area. Call on every minute
// change between full refreshes.
void updateTimePartial(const struct tm &now);

// Fast partial refresh of just the bell icon + alarm time (+ the
// countdown line below it). Call when the alarm is toggled on/off, or
// while its time is being adjusted with the rotary encoder. `bold`
// shows the time in a heavier weight while an edit is in progress
// (see main.cpp's `editingAlarm`); while bold, `underlineHour` also
// draws a short underline below the hour digits (true) or minute
// digits (false), showing which field the encoder currently adjusts
// (see main.cpp's `editingAlarmHour`) — ignored while not bold.
// `showCountdown` shows the "time until alarm" countdown line — not
// during editing, only for a brief window right after a new time is
// saved or the alarm is (re)enabled (see main.cpp's
// `showingAlarmCountdown`); only actually drawn if `alarmEnabled` is
// also true, regardless of `showCountdown`, since a disabled alarm has
// no meaningful "time until" it. `now` is needed to compute that
// countdown.
void updateAlarmIconPartial(const struct tm &now, int alarmHour, int alarmMinute, bool alarmEnabled, bool bold,
                             bool showCountdown, bool underlineHour);

// Full-screen "wake up" alert, shown once when the alarm first
// triggers (snoozeCount is always 0 at this point — the ZZZ icon row
// only appears once a snooze has happened, via
// updateWakeSnoozeIconsPartial() below).
void drawAlarmScreen(const struct tm &now, int snoozeCount);

// Fast partial refresh of just the wake screen's current-time text.
// Call on every minute change while the wake screen is showing —
// it stays on screen through the whole ringing/snoozing session.
void updateWakeTimePartial(const struct tm &now);

// Fast partial refresh of just the ZZZ icon row: draws snoozeCount
// icons (0 hides them). Call whenever the snooze count changes.
void updateWakeSnoozeIconsPartial(int snoozeCount);

// Fast partial refresh of just the "no wifi" icon (bottom-right
// corner of the clock face). Call whenever isWifiConnected()
// (TimeSync.h) changes, while in Mode::CLOCK.
void updateWifiIconPartial(bool connected);

// Settings menu (see SettingsMenu.h), Figma-matched: title + a 3-tab
// bar (Alarm/Light/System, bold = current category) + a row list.
// drawSettingsScreen() is a full-screen redraw; call it once when
// Settings mode is entered.
void drawSettingsScreen();

// Fast partial refresh of just the tab bar. Call whenever navigation
// crosses into a different category (so a different tab needs to go
// bold), in addition to updateSettingsRowsAreaPartial() below.
void updateSettingsTabsPartial();

// Fast partial refresh of the whole visible row area (only the
// current category's items are ever shown — 10 items at the full row
// pitch would overflow the screen). Call this instead of
// updateSettingsRowPartial() when navigation crosses into a different
// category, since which rows are visible at all changes, not just
// which one is selected.
void updateSettingsRowsAreaPartial();

// Fast partial refresh of exactly one row (not the header/tabs, not
// the other rows). Call once per row that actually changed on every
// subsequent navigation/edit state change while already in Settings —
// refreshing more than needed blocks loop() long enough to corrupt
// the button auto-repeat timing (see EpaperDisplay.cpp).
void updateSettingsRowPartial(int index);

// Like updateSettingsRowPartial(), but draws "Loading..." after the
// row's value instead of that item's normal editing state — for the
// brief window where entering the "Sound" row has to wait for the SD
// lock (see main.cpp and SdCard.h's sdTryLock()) because the
// background audio-preload task currently holds it. Call once right
// before that wait starts; the eventual updateSettingsRowPartial()
// once the wait ends naturally overwrites this.
void updateSettingsRowLoadingPartial(int index);

// Fast partial refresh of exactly two rows in one combined e-paper
// update, rather than two separate updateSettingsRowPartial() calls.
// Use this for normal up/down navigation within a category, where the
// old row's icon needs erasing and the new row's icon needs drawing —
// one flash instead of two reads noticeably more fluid (e-paper can't
// truly animate a smooth slide between positions).
void updateSettingsTwoRowsPartial(int indexA, int indexB);

// "Upload sound" screen (System tab's Action item): plain functional
// text layout, not Figma-matched — title, the URL to browse to
// (SoundUpload.h), and a status line. Call drawUploadScreen() once
// when entering Mode::UPLOAD, updateUploadStatusPartial() whenever the
// status line text changes (not on every loop() iteration).
void drawUploadScreen();
void updateUploadStatusPartial();

// "Connect to wifi" screen (System tab's "WiFi" Action item, or an
// automatic boot-time fallback — see main.cpp/WifiSetup.h): same plain
// functional layout style as the upload screen above — title, SSID/
// password of the board's own AP, a status line, and a QR code (join-
// network payload instead of a URL). Call drawWifiSetupScreen() once
// when entering Mode::WIFI_SETUP, updateWifiSetupStatusPartial()
// whenever the status line text changes.
void drawWifiSetupScreen();
void updateWifiSetupStatusPartial();

