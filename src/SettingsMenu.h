#pragma once

#include <WString.h>

// Settings menu reachable via the CONF button from the clock view.
// A few items are still placeholders for functionality that depends on
// hardware not yet built — shown in the menu but selecting them does
// nothing yet.

// Action: selecting it triggers something (e.g. entering upload mode)
// instead of editing a value — main.cpp intercepts these before the
// normal settingsBeginEdit()/edit-draft flow, which stays FunctionalInt-only.
// SoundChoice: like FunctionalInt (up/down cycles, CONF commits) but the
// set of choices is read from the SD card's /sounds/ directory at
// edit-time rather than being a fixed minValue..maxValue range — see
// getSoundChoiceLabel()/getSelectedSoundFile() below.
enum class SettingType { FunctionalInt, Placeholder, Action, SoundChoice };

// The menu has 4 tabs (Alarm/Sound/Light/System — "Sound" added
// 2026-08-01, no Figma node for it). kItems is ordered so all items of
// one category sit together — that's what lets plain wraparound
// (settingsSelectNext/Prev) double as "jump to the next/previous
// category" at the ends of the list, with no separate category index.
enum class SettingCategory { Alarm, Sound, Light, System };

struct SettingItem {
  const char *label;
  SettingCategory category;
  SettingType type;
  int minValue, maxValue, step; // only meaningful for FunctionalInt
  const char *unit;             // e.g. "min"; "" for the timezone item
  bool showSign;                // true shows "UTC+1"/"UTC-5" instead of "9 min"
  bool onOffStyle;              // true shows "On"/"Off" instead of "1 "/"0 " (a 0-1 FunctionalInt)
  bool zeroIsOff;                // true shows "Off" instead of "0 %"/"0 min" specifically at value 0, "N %"/"N min" otherwise
  bool autoManualStyle;          // true shows "Auto"/"Manual" instead of "1 "/"0 " (a 0-1 FunctionalInt)
  bool ringModeStyle;            // true shows "Continuous"/"Auto-off" instead of "1 "/"0 " (a 0-1 FunctionalInt)
  bool languageStyle;            // true shows the language's own name ("English"/"Portugues") via Strings.h
bool sleepRefreshStyle;        // true shows "Off"/"Static"/"30 min"/"1 h" (a 0/1/30/60 FunctionalInt)
};

// The row label in the active language (Strings.h). item.label stays
// the canonical EN string — it doubles as the stable identifier the
// web portal and main.cpp's Action-item matching key off, so never
// match on getSettingsItemLabel().
const char *getSettingsItemLabel(int index);

void initSettingsMenu();

int getSettingsItemCount();
const SettingItem &getSettingsItem(int index);

int getSettingsSelectedIndex();
void settingsSelectNext();
void settingsSelectPrev();

// Current persisted value of a FunctionalInt item (by index).
int getSettingsCurrentValue(int index);

bool isSettingsEditing();
int getSettingsDraftValue();
void settingsBeginEdit();   // no-op if the selected item is a Placeholder
void settingsAdjustDraft(int deltaSteps);
void settingsCommitEdit();
void settingsCancelEdit();

// Persisted settings used elsewhere (Alarm.cpp / TimeSync.cpp / Sound.cpp).
int getSnoozeMinutes();
int getAutoOffMinutes();
int getVolumePercent();
int getUtcOffsetHours();

// Seconds over which the alarm sound ramps from silence up to the set
// volume when a ringing session starts (or resumes from snooze) — 0
// means no fade-in, full volume immediately. See Sound.cpp.
int getFadeInSeconds();

// "Ring mode" (Settings, Alarm tab): true = the alarm keeps ringing
// until manually dismissed (MENU) — snooze still works, and re-fires
// continuously after each snooze. false = the pre-existing Auto-off
// behavior (stops after the Auto-off setting's minutes of actual
// ringing time). Persisted alongside the other settings.
bool getRingContinuous();

// "Sleep refresh" (Settings, System tab): how often the clock wakes
// from deep sleep to refresh the displayed time. Returns 0 = Off (never
// deep-sleeps — the pre-battery behavior), -1 = Static (sleeps until the
// next alarm/ramp window, the on-screen time stays frozen), 30 or 60 =
// wake every 30/60 min to refresh the minute. Only takes effect when
// battery power is enabled (see Power.h / the [env:battery] target).
int getSleepRefreshMinutes();

// Web-portal access to the exact values the physical menu edits, keyed
// by item index (see getSettingsItem()). getSettingValue() returns 0
// for non-FunctionalInt items. setSettingValue() clamps to the item's
// [min,max] range, persists immediately, and returns false for
// anything that isn't a FunctionalInt (Action/SoundChoice/Placeholder
// rows are managed elsewhere or not at all).
int getSettingValue(int index);
bool setSettingValue(int index, int value);

// Wake-up light (Light.cpp): max brightness (0-100%, in 10% steps) of
// the automatic before-alarm ramp, and how many minutes before the
// alarm time it starts. No separate on/off item — Intensity at 0
// ("Off") *is* the disabled state (getLightEnabled() below derives
// from exactly that), removed as a distinct setting per explicit
// request to fold the two into one.
bool getLightEnabled();
int getLightIntensityPercent();
int getLightDurationMinutes();

// Manual night light (Light.cpp, EXIT long-press toggle): brightness
// (10-100%, in 10% steps — no 0%/"Off", anything dimmer than 10% isn't
// actually visible) it fades to when switched on.
int getNightlightIntensityPercent();

// Manual frontlight (Light.cpp, EXIT short-press trigger): brightness
// (10-100%, in 10% steps — no 0%/"Off", anything dimmer than 10% isn't
// actually visible) of its calibrated warm-white+red color, where 100%
// is defined so that 10% (its minimum) reproduces the original
// hand-calibrated values exactly (white=20, red=17 of 255) — see
// Light.cpp's kFrontlightMaxWhite/kFrontlightMaxRed.
int getFrontlightIntensityPercent();

// "Frontlight mode" (Settings, System tab): false = Auto (the
// EXIT-short-press on-timer behavior above), true = Manual (Light.cpp's
// triggerFrontlight() switches the frontlight fully on with no timer,
// staying lit until the next alarm is dismissed — see
// endFrontlightManualHold()).
bool getFrontlightManualMode();

// "Battery" (Settings, System tab): true = the user has battery hardware
// connected, so the battery icon, percentage reading, and deep-sleep
// logic are active. false = no battery, the device runs on mains only.
bool getBatteryEnabled();

// "RTC" (Settings, System tab): true = the user has a DS3231 module
// connected, so time is available immediately at boot without WiFi.
bool getRtcEnabled();

// True while the Settings menu is actively editing the given item
// specifically (not just selected, not any other item) — lets
// Light.cpp show a live LED preview of the value being dialed in,
// before it's committed. One pair per intensity item; the preview
// getter is only valid while its matching isEditingX() is true, same
// 0-100/10%-step range as the matching committed getter above.
bool isEditingLightIntensity();
int getLightIntensityPreviewPercent();
bool isEditingNightlightIntensity();
int getNightlightIntensityPreviewPercent();
bool isEditingFrontlightIntensity();
int getFrontlightIntensityPreviewPercent();

// The "Sound" (SoundChoice) item's persisted selection: "" means the
// generated-tone fallback ("Tone"), otherwise a filename under
// /sounds/ on the SD card — used by Sound.cpp to build the play path.
String getSelectedSoundFile();

// Sets and persists the selection directly (bypassing the normal
// begin/adjust/commit edit-draft flow) — used by SoundUpload.cpp so
// the web portal's own radio-button selector and rename-preserves-
// selection logic can update the same persisted value the physical
// Settings menu uses, without needing a fake edit session.
void setSelectedSoundFile(const String &filename);

// Screen-ready (truncated) label for the *committed* selection above —
// "Tone" or the filename — used by EpaperDisplay.cpp when the Sound
// row isn't being edited.
const char *getSelectedSoundLabel();

// Screen-ready (truncated) label for index `index` within the file
// list snapshotted at settingsBeginEdit() time (index 0 is always the
// virtual "Tone" entry) — used by EpaperDisplay.cpp while the Sound
// row is being edited, driven by getSettingsDraftValue().
const char *getSoundChoiceLabel(int index);

// "i/N" position counter for index `index` in that same edit-session
// snapshot — drawn by EpaperDisplay.cpp to the right of (outside) the
// pill, only while the Sound row is being edited.
const char *getSoundChoiceCounter(int index);
