#include "SettingsMenu.h"

#include <Preferences.h>
#include <SD.h>

#include "SdCard.h"
#include "UiStrings.h"

namespace {

Preferences prefs;

int snoozeMinutes = 9;
int autoOffMinutes = 10;
int volumePercent = 70;
int fadeInSeconds = 0; // default: no fade-in, matches the behavior before this setting existed
  // Default off (0 = "Off"): no wake-up-light ramp until the user raises
  // this above 0 — this doubles as the master enable (see
  // getLightEnabled()), there's no separate on/off item anymore.
  int lightIntensityPercent = 0;
  int lightDurationMinutes = 20;
  // Default ~matches the old fixed kNightLightWhite=120 (120/255=~47%,
  // nearest 10% step) — chosen so this new setting doesn't itself change
  // what the night light looked like the moment it was introduced.
  int nightlightIntensityPercent = 50;
  // Default exactly reproduces the pre-existing hand-calibrated
  // frontlight color (white=20/red=17 of 255) — see
  // kFrontlightMaxWhite/kFrontlightMaxRed in Light.cpp — so this new
  // setting doesn't itself change how the frontlight looks by default.
  int frontlightIntensityPercent = 10;
  int utcOffsetHours = 1; // matches the old hardcoded Europe/Brussels winter-time default
  // 0 = Auto (the pre-existing on-timer behavior), 1 = Manual (frontlight
  // stays fully on from a short EXIT press until the next alarm dismiss).
  int frontlightManualMode = 0;
  // "Ring mode" (Settings, Alarm tab): 0 = Auto-off (alarm stops after
  // the Auto-off setting's minutes of actual ringing time), 1 =
  // Continuous (rings until manually dismissed; snooze still works and
  // re-fires it). See getRingContinuous()/main.cpp's auto-off gate.
  int ringModeContinuous = 0;
  // Language mirror: the "Language" row edits this through the normal
  // FunctionalInt flow; persist() bridges it to Strings.h's
  // setLanguage() (single NVS key "lang"), and the labels switch on the
  // full redraw main.cpp triggers after committing this row.
  int languageValue = 0;
  // "Battery" (Settings, System tab): 0 = Off (no battery hardware,
  // deep sleep disabled, no battery icon), 1 = On (battery connected,
  // deep sleep + battery % active). Persisted as batteryOn.
  int batteryEnabled = 0;
  // "RTC" (Settings, System tab): 0 = Off (no DS3231 module), 1 = On
  // (DS3231 present — time available immediately at boot without WiFi).
  int rtcEnabled = 0;
  // "Sleep refresh" (Settings, System tab): 0=Off, 1=Static, 30, 60 —
  // see getSleepRefreshMinutes()/Power.h. Only meaningful when battery
  // power is enabled.
  int sleepRefreshValue = 0;

// English labels, matching the Figma design and the rest of this
// app's display text (the wake screen is English too) — short, since
// this design's label column is fixed-width.
const SettingItem kItems[] = {
    // Alarm
    {"Snooze duration", SettingCategory::Alarm, SettingType::FunctionalInt, 1, 30, 1, "min", false, false, false, false},
    {"Auto-off", SettingCategory::Alarm, SettingType::FunctionalInt, 1, 60, 1, "min", false, false, false, false},
    // Continuous/Auto-off — whether the alarm ever stops on its own.
    // Ring-mode-styled 0-1 FunctionalInt (same mechanics as
    // "Frontlight mode"'s Auto/Manual styling).
    {"Ring mode", SettingCategory::Alarm, SettingType::FunctionalInt, 0, 1, 1, "", false, false, false, false, true},
    // Sound (own tab, added 2026-08-01 — was split across Alarm/System before)
    // Minimum 10, not 0 — matches Sound.cpp's kFadeInMinVolumePercent
    // (the quietest setting that's actually audible through this amp);
    // keep the two in sync if either ever changes.
    {"Volume", SettingCategory::Sound, SettingType::FunctionalInt, 10, 100, 10, "%", false, false, false, false},
    {"Fade-in", SettingCategory::Sound, SettingType::FunctionalInt, 0, 180, 30, "sec", false, false, false, false},
    {"Sound", SettingCategory::Sound, SettingType::SoundChoice, 0, 0, 0, "", false, false, false, false},
    {"Upload sound", SettingCategory::Sound, SettingType::Action, 0, 0, 0, "", false, false, false, false},
    // Light (wake-up light, wired up 2026-08-05 — was 3 Placeholders
    // before; the separate "Light on/off" item was folded into
    // Intensity's own "Off" state at 0% and removed, per explicit
    // request — see getLightEnabled()).
    {"Wake-up", SettingCategory::Light, SettingType::FunctionalInt, 0, 100, 10, "%", false, false, true, false},
    {"Duration", SettingCategory::Light, SettingType::FunctionalInt, 0, 60, 5, "min", false, false, false, false},
    // Manual states (EXIT long-press / EXIT short-press), each with a
    // live LED preview while being edited — see isEditingNightlightIntensity()/
    // isEditingFrontlightIntensity() in Light.cpp. Minimum 10, not 0,
    // for both — below that the brightness/calibrated color isn't
    // actually visible (same reasoning as Volume's own 10% floor
    // above), so neither has an "Off" state.
    {"Nightlight", SettingCategory::Light, SettingType::FunctionalInt, 10, 100, 10, "%", false, false, false, false},
    {"Frontlight", SettingCategory::Light, SettingType::FunctionalInt, 10, 100, 10, "%", false, false, false, false},
    // System
    {"Timezone", SettingCategory::System, SettingType::FunctionalInt, -12, 12, 1, "", true, false, false, false},
    // Auto/Manual — see getFrontlightManualMode()/Light.cpp's
    // triggerFrontlight(). A 0-1 FunctionalInt like the old "Light
    // on/off" item used to be, styled via autoManualStyle instead of
    // onOffStyle.
    {"Frontlight mode", SettingCategory::System, SettingType::FunctionalInt, 0, 1, 1, "", false, false, false, true},
    {"WiFi", SettingCategory::System, SettingType::Action, 0, 0, 0, "", false, false, false, false},
    {"Language", SettingCategory::System, SettingType::FunctionalInt, 0, 1, 1, "", false, false, false, false, false, true},
    // Battery on/off (hardware switch): when off, deep sleep and the
    // battery icon are disabled; when on, the ADC reading and sleep
    // refresh setting take effect. See Power.h/.cpp.
    {"Battery", SettingCategory::System, SettingType::FunctionalInt, 0, 1, 1, "", false, false, true, false},
    // RTC on/off (hardware switch): when on, the optional DS3231 module
    // provides time at boot without waiting for WiFi/NTP. See Rtc.h/.cpp.
    {"RTC", SettingCategory::System, SettingType::FunctionalInt, 0, 1, 1, "", false, false, true, false},
    // Sleep refresh (deep-sleep clock-refresh cadence, see Power.h).
    // 0=Off, 1=Static, 30 (30 min), 60 (1 h) — styled via sleepRefreshStyle.
    {"Sleep refresh", SettingCategory::System, SettingType::FunctionalInt, 0, 60, 1, "", false, false, false, false, false, false, true},

};
constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

int selectedIndex = 0;
bool editing = false;
int draftValue = 0;

// Maps a FunctionalInt item's index to its backing variable. Indices
// must match kItems[] above.
int *valueForIndex(int index) {
  if (index == 0) return &snoozeMinutes;          // Snoozeduur aanpassen
  if (index == 1) return &autoOffMinutes;         // Alarm auto-off instellen
  if (index == 2) return &ringModeContinuous;     // Ring mode
  if (index == 3) return &volumePercent;          // Volume
  if (index == 4) return &fadeInSeconds;          // Fade-in
  if (index == 7) return &lightIntensityPercent;       // Wake-up
  if (index == 8) return &lightDurationMinutes;        // Duration
  if (index == 9) return &nightlightIntensityPercent;  // Nightlight
  if (index == 10) return &frontlightIntensityPercent; // Frontlight
  if (index == 11) return &utcOffsetHours;             // Tijdzone instellen
  if (index == 12) return &frontlightManualMode;       // Frontlight mode
  if (index == 14) return &languageValue;              // Language
  if (index == 15) return &batteryEnabled;             // Battery
  if (index == 16) return &rtcEnabled;                 // RTC
  if (index == 17) return &sleepRefreshValue;          // Sleep refresh
  return nullptr;
}

void persist() {
  prefs.putUChar("snoozeMin", snoozeMinutes);
  prefs.putUChar("autoOffMin", autoOffMinutes);
  prefs.putUChar("volumePct", volumePercent);
  prefs.putUChar("fadeInSec", fadeInSeconds);
  prefs.putUChar("lightPct", lightIntensityPercent);
  prefs.putUChar("lightMin", lightDurationMinutes);
  prefs.putUChar("nightPct", nightlightIntensityPercent);
  prefs.putUChar("frontPct", frontlightIntensityPercent);
  // Signed: unlike the durations/volume above, this can be negative.
  prefs.putChar("utcOffset", (int8_t)utcOffsetHours);
  prefs.putUChar("frontMode", frontlightManualMode);
  prefs.putUChar("ringMode", ringModeContinuous);
  setLanguage(static_cast<Lang>(languageValue)); // single NVS key "lang", owned by Strings.h
  prefs.putUChar("batteryOn", batteryEnabled);
  prefs.putUChar("rtcOn", rtcEnabled);
  prefs.putUChar("sleepRefresh", sleepRefreshValue);
}

// "Sound" (SoundChoice) support — index 0 is always the virtual "Tone"
// entry (generated-tone fallback, no file); indices 1..kBuiltinSoundCount
// are the built-in ambient loops (Birds/Brook/Ocean/Rain, see
// Sound.cpp's isBuiltinSoundName()/startBuiltinSoundPlayback()). Since
// Phase 3.5 these live on the SD card as /sounds/_builtin_<name>.pcm
// (raw PCM — see tools/install_builtin_sounds_to_sd.py); if a file is
// missing, Sound.cpp falls back to the always-available embedded Tone.
// Indices after that mirror whatever is currently in /sounds/ on the SD
// card, snapshotted once per edit session (settingsBeginEdit()) so
// browsing is stable even if the SD directory changes mid-edit via the
// web upload portal.
constexpr const char *kBuiltinSoundNames[] = {"Birds", "Brook", "Ocean", "Rain"};
constexpr int kBuiltinSoundCount = 4;
constexpr int kMaxSoundChoices = 37; // "Tone" + 4 built-in + up to 32 uploaded files
constexpr int kSoundLabelMaxChars = 16; // display cap for the settings row's value column (name only, pill-internal)

String soundChoiceFiles[kMaxSoundChoices]; // [0] unused (Tone has no filename)
int soundChoiceCount = 1; // starts at 1 (just "Tone") until refreshed
String selectedSoundFile; // "" = Tone
char soundLabelBuf[kSoundLabelMaxChars + 4];
char soundCounterBuf[12];

const char *truncateForDisplay(const String &name) {
  String label = name.length() ? name : String("Tone");
  if ((int)label.length() > kSoundLabelMaxChars) {
    label = label.substring(0, kSoundLabelMaxChars - 2) + "..";
  }
  strncpy(soundLabelBuf, label.c_str(), sizeof(soundLabelBuf) - 1);
  soundLabelBuf[sizeof(soundLabelBuf) - 1] = '\0';
  return soundLabelBuf;
}

// "i/N", 1-based (so "Tone" at internal index 0 shows as "1/N") —
// drawn as a separate element to the right of the pill by
// EpaperDisplay.cpp, only while this row is being edited, not baked
// into the name text itself.
const char *soundCounterText(int index, int count) {
  snprintf(soundCounterBuf, sizeof(soundCounterBuf), "%d/%d", index + 1, count);
  return soundCounterBuf;
}

void refreshSoundChoiceList() {
  soundChoiceCount = 1;
  for (int i = 0; i < kBuiltinSoundCount; i++) {
    soundChoiceFiles[soundChoiceCount] = String(kBuiltinSoundNames[i]);
    soundChoiceCount++;
  }
  // Locked (SdCard.h): Sound.cpp's background audio-preload task can
  // touch the SD card at any time nothing is ringing — same time this
  // function can run from a Settings-menu interaction — so the two
  // must not be allowed to hit the SD card at the same instant.
  sdLock();
  File dir = SD.open("/sounds");
  if (!dir) {
    sdUnlock();
    return;
  }
  File entry = dir.openNextFile();
  while (entry && soundChoiceCount < kMaxSoundChoices) {
    if (!entry.isDirectory()) {
      soundChoiceFiles[soundChoiceCount] = String(entry.name());
      soundChoiceCount++;
    }
    entry = dir.openNextFile();
  }
  dir.close();
  sdUnlock();
}

} // namespace

void initSettingsMenu() {
  prefs.begin("paperwake", false);
  snoozeMinutes = prefs.getUChar("snoozeMin", 9);
  autoOffMinutes = prefs.getUChar("autoOffMin", 10);
  volumePercent = prefs.getUChar("volumePct", 70);
  if (volumePercent < 10) volumePercent = 10; // migrate a pre-existing value below the new minimum
  fadeInSeconds = prefs.getUChar("fadeInSec", 0);
  lightIntensityPercent = prefs.getUChar("lightPct", 0);
  // Migrate a pre-existing value that isn't a multiple of the new 10%
  // step (the old step was 5%) down to the nearest one below it — the
  // old separate "lightOn" on/off key is no longer read at all: if it
  // was on, lightIntensityPercent is already the nonzero value that
  // controlled brightness, so getLightEnabled() (which now just checks
  // lightIntensityPercent > 0) stays consistent with the old combined
  // behavior; if it was off, this can't distinguish that from "on at a
  // low intensity," but there is exactly one device running this code
  // and it was already on at the time of this change.
  lightIntensityPercent = (lightIntensityPercent / 10) * 10;
  lightDurationMinutes = prefs.getUChar("lightMin", 20);
  nightlightIntensityPercent = prefs.getUChar("nightPct", 50);
  nightlightIntensityPercent = (nightlightIntensityPercent / 10) * 10; // align to the 10% step, same reasoning as above
  if (nightlightIntensityPercent < 10) nightlightIntensityPercent = 10; // migrate a pre-existing value below the new minimum
  frontlightIntensityPercent = prefs.getUChar("frontPct", 10);
  frontlightIntensityPercent = (frontlightIntensityPercent / 10) * 10;
  if (frontlightIntensityPercent < 10) frontlightIntensityPercent = 10; // migrate a pre-existing value below the new minimum
  utcOffsetHours = prefs.getChar("utcOffset", 1);
  frontlightManualMode = prefs.getUChar("frontMode", 0);
  ringModeContinuous = prefs.getUChar("ringMode", 0);
  languageValue = static_cast<int>(getLanguage());
  batteryEnabled = prefs.getUChar("batteryOn", 0);
  rtcEnabled = prefs.getUChar("rtcOn", 0);
  sleepRefreshValue = prefs.getUChar("sleepRefresh", 0);
  selectedSoundFile = prefs.getString("soundFile", "");
}

int getSettingsItemCount() { return kItemCount; }

const SettingItem &getSettingsItem(int index) { return kItems[index]; }

// PT labels for the rows, indexed exactly like kItems[] above. Kept
// here (not in Strings.h) so the menu table and its translations stay
// side by side. Accent-free — see the font note in Strings.h. The
// web portal matches on item.label (canonical EN), never on these.
const char *const kItemLabelsPt[] = {
    "Soneca",                                   // Snooze duration
    "Desligar",                                 // Auto-off
    "Toque",                                    // Ring mode
    "Volume",                                   // Volume
    "Fade-in",                                  // Fade-in
    "Som",                                      // Sound
    "Enviar som",                               // Upload sound
    "Luz",                                      // Wake-up
    "Duracao",                                  // Duration
    "Noturna",                                  // Nightlight
    "Frente",                                   // Frontlight
    "Fuso",                                     // Timezone
    "Modo frente",                              // Frontlight mode
    "WiFi",                                     // WiFi
    "Idioma",                                   // Language
    "Bateria",                                  // Battery
    "RTC",                                      // RTC
    "Refresh sono",                             // Sleep refresh (only shown when ENABLE_BATTERY)
};
static_assert(sizeof(kItemLabelsPt) / sizeof(kItemLabelsPt[0]) == kItemCount,
              "kItemLabelsPt must have one label per kItems row");

const char *getSettingsItemLabel(int index) {
  if (index < 0 || index >= kItemCount) return "";
  return getLanguage() == Lang::PT ? kItemLabelsPt[index] : kItems[index].label;
}

int getSettingsSelectedIndex() { return selectedIndex; }

void settingsSelectNext() { selectedIndex = (selectedIndex + 1) % kItemCount; }

void settingsSelectPrev() { selectedIndex = (selectedIndex - 1 + kItemCount) % kItemCount; }

int getSettingsCurrentValue(int index) {
  int *value = valueForIndex(index);
  return value ? *value : 0;
}

bool isSettingsEditing() { return editing; }

int getSettingsDraftValue() { return draftValue; }

void settingsBeginEdit() {
  const SettingItem &item = kItems[selectedIndex];
  if (item.type == SettingType::SoundChoice) {
    refreshSoundChoiceList();
    draftValue = 0;
    for (int i = 1; i < soundChoiceCount; i++) {
      if (soundChoiceFiles[i] == selectedSoundFile) {
        draftValue = i;
        break;
      }
    }
    editing = true;
    return;
  }
  if (item.type != SettingType::FunctionalInt) return;
  draftValue = getSettingsCurrentValue(selectedIndex);
  editing = true;
}

void settingsAdjustDraft(int deltaSteps) {
  const SettingItem &item = kItems[selectedIndex];
  if (item.type == SettingType::SoundChoice) {
    // Wraps like a picker/carousel rather than clamping like a plain
    // numeric value — the wrapped modulo below is correct for any
    // deltaSteps magnitude (main.cpp can now pass more than ±1 at once
    // if several encoder detents piled up between two polls), no
    // special-casing needed.
    draftValue = ((draftValue + deltaSteps) % soundChoiceCount + soundChoiceCount) % soundChoiceCount;
    return;
  }
  draftValue += deltaSteps * item.step;
  if (draftValue < item.minValue) draftValue = item.minValue;
  if (draftValue > item.maxValue) draftValue = item.maxValue;
}

void settingsCommitEdit() {
  const SettingItem &item = kItems[selectedIndex];
  if (item.type == SettingType::SoundChoice) {
    selectedSoundFile = (draftValue == 0) ? String("") : soundChoiceFiles[draftValue];
    prefs.putString("soundFile", selectedSoundFile);
    editing = false;
    return;
  }
  int *value = valueForIndex(selectedIndex);
  if (value) *value = draftValue;
  persist();
  editing = false;
}

void settingsCancelEdit() { editing = false; }

int getSnoozeMinutes() { return snoozeMinutes; }

int getAutoOffMinutes() { return autoOffMinutes; }

int getVolumePercent() { return volumePercent; }

int getFadeInSeconds() { return fadeInSeconds; }

bool getRingContinuous() { return ringModeContinuous != 0; }

int getSleepRefreshMinutes() {
  if (sleepRefreshValue == 0) return 0;   // Off — never deep-sleeps
  if (sleepRefreshValue == 1) return -1;  // Static — sleep until next alarm/ramp
  if (sleepRefreshValue <= 30) return 30;
  return 60;
}

int getSettingValue(int index) {
  int *value = valueForIndex(index);
  return value ? *value : 0;
}

bool setSettingValue(int index, int value) {
  int *target = valueForIndex(index);
  if (!target) return false; // Action/SoundChoice/Placeholder rows
  const SettingItem &item = kItems[index];
  if (value < item.minValue) value = item.minValue;
  if (value > item.maxValue) value = item.maxValue;
  *target = value;
  persist();
  return true;
}

bool getLightEnabled() { return lightIntensityPercent > 0; }

int getLightIntensityPercent() { return lightIntensityPercent; }

int getLightDurationMinutes() { return lightDurationMinutes; }

int getNightlightIntensityPercent() { return nightlightIntensityPercent; }

int getFrontlightIntensityPercent() { return frontlightIntensityPercent; }

bool isEditingLightIntensity() { return editing && valueForIndex(selectedIndex) == &lightIntensityPercent; }

int getLightIntensityPreviewPercent() { return draftValue; }

bool isEditingNightlightIntensity() { return editing && valueForIndex(selectedIndex) == &nightlightIntensityPercent; }

int getNightlightIntensityPreviewPercent() { return draftValue; }

bool isEditingFrontlightIntensity() { return editing && valueForIndex(selectedIndex) == &frontlightIntensityPercent; }

int getFrontlightIntensityPreviewPercent() { return draftValue; }

int getUtcOffsetHours() { return utcOffsetHours; }

bool getFrontlightManualMode() { return frontlightManualMode != 0; }

bool getBatteryEnabled() { return batteryEnabled != 0; }

bool getRtcEnabled() { return rtcEnabled != 0; }

String getSelectedSoundFile() { return selectedSoundFile; }

void setSelectedSoundFile(const String &filename) {
  selectedSoundFile = filename;
  prefs.putString("soundFile", selectedSoundFile);
}

const char *getSelectedSoundLabel() { return truncateForDisplay(selectedSoundFile); }

const char *getSoundChoiceLabel(int index) {
  if (index <= 0 || index >= soundChoiceCount) return truncateForDisplay("");
  return truncateForDisplay(soundChoiceFiles[index]);
}

// Only meaningful while editing (relies on the snapshot taken by
// settingsBeginEdit()) — the counter itself is only ever drawn during
// editing anyway, see EpaperDisplay.cpp's drawSettingsRow().
const char *getSoundChoiceCounter(int index) {
  if (index < 0) index = 0;
  if (index >= soundChoiceCount) index = soundChoiceCount - 1;
  return soundCounterText(index, soundChoiceCount);
}
