#include <Arduino.h>
#include <string.h>

#include "Alarm.h"
#include "AlarmSettings.h"
#include "Controls.h"
#include "EpaperDisplay.h"
#include "Light.h"
#include "PinConfig.h"
#include "SdCard.h"
#include "SettingsMenu.h"
#include "Sound.h"
#include "SoundUpload.h"
#include "TimeSync.h"
#include "UiStrings.h"
#include "WebAdmin.h"
#include "WifiSetup.h"
#include "Power.h"
#include "Rtc.h"


namespace {

constexpr unsigned long kFullRefreshIntervalMs = 60UL * 60UL * 1000UL; // 1 hour
constexpr unsigned long kAlarmEditTimeoutMs = 6000;
constexpr unsigned long kAlarmCountdownDisplayMs = 2000;
constexpr unsigned long kTimeTickIntervalMs = 1000;
// How long the wifi-setup screen keeps showing "Connected!" before
// automatically switching to the clock face — same kind of brief,
// timed display window as kAlarmCountdownDisplayMs above.
constexpr unsigned long kWifiSetupConnectedDisplayMs = 2000;

// Alarm-time editing step sizes. Rotating always steps by exactly one
// of these — no more turning-speed-dependent jump size (used to also
// snap to the next quarter-hour on a fast turn, removed per explicit
// feedback: fine adjustment near a quarter-hour boundary kept
// accidentally triggering the coarse jump). Which one applies depends
// on editingAlarmHour, toggled by pressing the encoder/CONF button
// while editing — see the Mode::CLOCK block below.
constexpr int kAlarmEditMinuteStepMinutes = 1;
constexpr int kAlarmEditHourStepMinutes = 60;

enum class Mode { CLOCK, RINGING, SETTINGS, UPLOAD, WIFI_SETUP,
               };

// loop() iteration while idle.
String lastDrawnUploadStatus;

// True only when Mode::WIFI_SETUP was entered via the Settings menu
// (an existing, working clock to fall back to) — false when it's the
// automatic boot-time fallback (no valid time yet, nothing to cancel
// back to), in which case EXIT is simply ignored while in this mode.
bool wifiSetupCancelable = false;
// Timed "Connected!" display window before auto-returning to
// Mode::CLOCK — same pattern as showingAlarmCountdown below.
bool wifiSetupShowingConnected = false;
unsigned long wifiSetupConnectedAtMillis = 0;

Mode mode = Mode::CLOCK;
int lastDrawnMinute = -1;

// Tracks the previous loop's battery state so a mains->battery
// transition (power loss) can be detected exactly once.
bool lastLoopWasOnBattery = false;
// Set true when a power-loss transition is detected; cleared after the
// next full clock-face draw shows the warning.
bool showPowerLossWarning = false;



// Alarm shown on the clock face: the next *occurrence* of any enabled
// alarm (honoring each one's daysMask — the multi-alarm rework), not
// blindly alarm 0. Falls back to alarm 0 itself when nothing is
// enabled, so its time still shows next to the crossed-out bell.
// Note the physical controls (clock-face edit, MENU toggle) always act
// on alarm 0 regardless of what this displays — see AlarmSettings.h.
AlarmTime displayedAlarm(const struct tm &now) {
  AlarmTime next;
  int minutesUntil;
  if (getNextAlarm(now, next, minutesUntil)) return next;
  return getAlarm();
}

// Tracks the "no wifi" icon's last-drawn state (Mode::CLOCK only) so
// it's only touched with a partial refresh on an actual change, not
// re-drawn every timeTick — mirrors lastDrawnMinute's pattern. Synced
// to the live state wherever drawFullClockFace() runs (that already
// draws the icon itself), so no redundant partial refresh follows.
bool lastDrawnWifiConnected = true;
unsigned long lastFullRefreshMillis = 0;
unsigned long lastTimeTickMillis = 0;
// For the auto-off timeout: ringingSessionStartMillis marks the start
// of the *current* ringing segment (reset both when ringing first
// starts and each time it resumes after a snooze), while
// ringingAccumulatedMs holds the sum of all previous segments' elapsed
// time. Together they measure time actually spent ringing, excluding
// snooze pauses — a long snooze must not count toward auto-off, only
// active ringing time should. Explicit request.
unsigned long ringingSessionStartMillis = 0;
unsigned long ringingAccumulatedMs = 0;

// The alarm time is edited in place, next to the bell icon (bold text
// while adjusting, partial refresh only) rather than via a separate
// full-screen view.
bool editingAlarm = false;
// Which field rotating the encoder currently adjusts — false (minute)
// whenever a fresh edit begins; toggled by pressing the encoder/CONF
// button while editing. Shown to the user via an underline (see
// EpaperDisplay.cpp's updateAlarmIconPartial()).
bool editingAlarmHour = false;
unsigned long lastAlarmEditActivityMillis = 0;

// The wake screen (Figma "Wake up screen") stays up for the whole
// ringing session — through any number of snoozes — until dismissed
// with MENU; only the ticking time and the ZZZ icon row change along
// the way. isSnoozing tracks which of those two sub-states we're in;
// snoozeCount is cumulative across the whole session and only resets
// when a new session starts (see enterRinging()).
bool isSnoozing = false;
int snoozeCount = 0;

// The "time until alarm" countdown only shows briefly right after a
// new alarm time is saved (not while still editing) — see main.cpp's
// alarm-editing block below and ClockLayout.h's kAlarmCountdown*.
bool showingAlarmCountdown = false;
unsigned long alarmCountdownShownAtMillis = 0;

void enterRinging(const struct tm &now) {
  mode = Mode::RINGING;
  isSnoozing = false;
  snoozeCount = 0;
  ringingSessionStartMillis = millis();
  ringingAccumulatedMs = 0;
  digitalWrite(POWER_LED, HIGH); // stand-in for sound until Fase 2 (I2S amp)
  // Started here, explicitly, before the full-screen draw below — that
  // draw blocks for ~2s (measured), and the generic once-per-loop()
  // updateAlarmSound() call at the bottom of loop() only runs *after*
  // this function returns, so sound used to only start once the whole
  // screen had already finished drawing. For an alarm clock, a 2s-late
  // *screen* is far less noticeable than a 2s-late *sound*.
  updateAlarmSound(true);
  // Wait (briefly, bounded) for the audio task to get real decoded
  // audio into the I2S DMA queue before starting the refresh above —
  // it and the audio task share core 1 at equal FreeRTOS priority
  // (see Sound.cpp), so without this, the ~2s refresh below could win
  // enough of the scheduler race against the audio task's *first* fill
  // to audibly delay when sound actually starts (confirmed live,
  // specific to SD-file playback's heavier per-iteration workload).
  // Bounded at kAudioStartTimeoutMs so a failure here (e.g. no valid
  // source at all) can't hang the alarm screen forever.
  constexpr unsigned long kAudioStartTimeoutMs = 300;
  unsigned long audioWaitStartMs = millis();
  while (!isAudioAudible() && millis() - audioWaitStartMs < kAudioStartTimeoutMs) {
    delay(2);
  }
  drawAlarmScreen(now, snoozeCount);
  lastDrawnMinute = now.tm_min;
}

void dismissAlarm(const struct tm &now) {
  mode = Mode::CLOCK;
  cancelSnooze(); // a pending snooze must not fire the alarm again later
  digitalWrite(POWER_LED, LOW);
  // Stopped here explicitly, not left to the generic updateAlarmSound()
  // call at the bottom of loop() — that call happens *after*
  // drawFullClockFace() below, which is a full refresh that blocks for
  // up to ~2s, so relying on it left the tone audibly playing for the
  // whole refresh instead of cutting off immediately on dismissal.
  updateAlarmSound(false);
  AlarmTime shown = displayedAlarm(now);
  drawFullClockFace(now, shown.hour, shown.minute, shown.enabled, showPowerLossWarning);
  showPowerLossWarning = false;
  // Called *after* drawFullClockFace(), unlike updateAlarmSound() above
  // — opposite reasoning: stopWakeupLight() starts a 1s fade-out that
  // only actually animates once loop() is free to call updateLight()
  // again, so starting it before the ~2s blocking refresh would just
  // burn through the whole fade window unseen (confirmed live: the
  // light appeared to cut instantly, since by the time loop() resumed
  // the 1s window had already fully elapsed during the refresh).
  // Calling it here instead means the light stays at full brightness
  // through the refresh, then visibly fades over the following second.
  stopWakeupLight();
  // Ends a Manual-mode frontlight hold (see Light.h's triggerFrontlight()),
  // if one was active — per explicit request that "Manual" keeps the
  // frontlight on until the next alarm is dismissed. No-op otherwise
  // (Auto mode, or the frontlight was never triggered).
  endFrontlightManualHold();
  lastDrawnMinute = now.tm_min;
  lastDrawnWifiConnected = isWifiConnected();
  lastFullRefreshMillis = millis();
}

// Shared by both places MENU (Alarm on/off) leaves Settings for the
// clock face: the normal browsing-state exit, and mid-edit (which
// commits the in-progress draft first — see the isSettingsEditing()
// branch below — rather than discarding it the way EXIT/cancel does).
void exitSettingsToClock(const struct tm &now) {
  mode = Mode::CLOCK;
  AlarmTime shown = displayedAlarm(now);
  drawFullClockFace(now, shown.hour, shown.minute, shown.enabled, showPowerLossWarning);
  showPowerLossWarning = false;
  lastDrawnMinute = now.tm_min;
  lastDrawnWifiConnected = isWifiConnected();
  lastFullRefreshMillis = millis();
}

} // namespace

void setup() {
  Serial.begin(115200);

  // Power module must init first (enables ADC for battery detection)
  initPower();

  pinMode(POWER_LED, OUTPUT);
  digitalWrite(POWER_LED, LOW);
  initDisplay();
  drawBootLogo();
  initControls();
  initAlarmSettings();
  initSettingsMenu();
  initRtc();
  // Before initSdCard()/initSound(): initSound() starts the audio task
  // on core 1 (same core setup()/loop() run on), which begins
  // background-preloading the selected sound almost immediately,
  // including a real decode warm-up (primeAudioBuffer()) — measured
  // live to intermittently disrupt beginWifiAndTime()'s own blocking,
  // timing-sensitive connect attempt right below when both compete for
  // the same core at boot (device got stuck in AP-provisioning mode).
  // WiFi connecting first, before the audio task exists at all,
  // avoids that contention entirely — delaying when the background
  // preload starts by a few seconds doesn't matter in practice, it
  // just needs to finish sometime before the first alarm.
  beginWifiAndTime();
  // Before initSound(): the audio task it starts begins background-
  // preloading the selected sound almost immediately (see Sound.h),
  // which needs the SD card already mounted — otherwise that very
  // first preload attempt would find no card and settle for the
  // embedded fallback tone even when a real file is selected.
  initSdCard();
  initSound();
  initLight();
  // Web admin portal: registers its routes once; the server itself
  // (re)starts lazily in handleWebAdmin() whenever WiFi is connected.
  initWebAdmin();

#ifdef ENABLE_BATTERY
  int wakeupCause = handleWakeup();
  if (wakeupCause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("[Power] Woke from timer (deep sleep)");
  } else if (wakeupCause == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[Power] Woke from button press");
  }

  struct tm now;
  if (getCurrentTime(now)) {
    AlarmTime shown = displayedAlarm(now);
    drawFullClockFace(now, shown.hour, shown.minute, shown.enabled, showPowerLossWarning);
    showPowerLossWarning = false;
    lastDrawnMinute = now.tm_min;
    lastDrawnWifiConnected = isWifiConnected();
  } else {
    // No RTC on this board — if boot never obtained a valid time (bad
    // credentials, network out of reach, etc.), there is no way to
    // recover on its own. Drop straight into the AP-provisioning
    // screen instead of a blank/frozen clock, so the user has an
    // actual path to fix it from a phone.
    mode = Mode::WIFI_SETUP;
    wifiSetupCancelable = false; // nothing to cancel back to yet
    webAdminSuspend(); // the captive portal needs port 80 for itself
    beginWifiSetup();
    drawWifiSetupScreen();
  }
#endif
  lastFullRefreshMillis = millis();
}

void loop() {
  bool nowOnBattery = isOnBattery();
  if (nowOnBattery && !lastLoopWasOnBattery) {
    showPowerLossWarning = true;
  }
  lastLoopWasOnBattery = nowOnBattery;

  // Web admin portal: served in the background in every mode except
  // the ones that own port 80 themselves (UPLOAD/WIFI_SETUP — handled
  // via webAdminSuspend()/Resume() at their entry/exit points).
  // Dangerous portal operations (OTA, config import) are additionally
  // refused while the alarm is ringing.
  webAdminSetBusy(mode == Mode::RINGING);
  handleWebAdmin();

  // Buttons are polled every loop iteration (fast, ~10ms cadence) so
  // short presses are never missed. Everything time/WiFi/display
  // related is throttled separately via millis(), not via delay().
  int upSteps = rotateUpSteps();
  int downSteps = rotateDownSteps();
  bool up = upSteps > 0;
  bool down = downSteps > 0;
  bool menu = menuButtonPressed();
  bool menuLongPressed = menuButtonLongPressed();
  bool exitPressed = exitButtonPressed();
  bool exitLongPressed = exitButtonLongPressed();
  bool exitShortReleased = exitButtonShortReleased();
  bool confPressed = rotateConfPressed();

  bool timeTick = millis() - lastTimeTickMillis >= kTimeTickIntervalMs;
  if (timeTick) {
    lastTimeTickMillis = millis();
    // Skipped while actively provisioning wifi — the user is entering
    // fresh credentials by hand there; the old background retry (which
    // would be against a network that's already known not to work)
    // would only fight with that, and both attempt WiFi.begin().
    if (mode != Mode::WIFI_SETUP) maintainWifiAndTime();
  }

  // Handled before the getCurrentTime() gate below: this mode is the
  // one place in the app that must keep running (its webserver/DNS
  // captive portal) *without* a valid time — that's exactly the
  // situation it exists to recover from. The normal gate would
  // otherwise block it forever whenever entered via the boot-time
  // fallback in setup().
  if (mode == Mode::WIFI_SETUP) {
    handleWifiSetup();
    if (isWifiSetupConnected()) {
      if (!wifiSetupShowingConnected) {
        wifiSetupShowingConnected = true;
        wifiSetupConnectedAtMillis = millis();
      } else if (millis() - wifiSetupConnectedAtMillis >= kWifiSetupConnectedDisplayMs) {
        wifiSetupShowingConnected = false;
        endWifiSetup();
        webAdminResume(); // home-WiFi is up again — port 80 free for the admin portal
        syncTime();
        struct tm connectedNow;
        if (getCurrentTime(connectedNow)) {
          mode = Mode::CLOCK;
          AlarmTime shown = displayedAlarm(connectedNow);
          drawFullClockFace(connectedNow, shown.hour, shown.minute, shown.enabled, showPowerLossWarning);
          showPowerLossWarning = false;
          lastDrawnMinute = connectedNow.tm_min;
          lastDrawnWifiConnected = isWifiConnected();
          lastFullRefreshMillis = millis();
        }
      }
    } else if (wifiSetupCancelable && exitPressed) {
      endWifiSetup();
      webAdminResume();
      mode = Mode::SETTINGS;
      drawSettingsScreen();
    }
    return; // no delay(10): keep servicing the portal as fast as possible, same as Mode::UPLOAD
  }

  struct tm now;
  if (!getCurrentTime(now)) {
    delay(10);
    return;
  }

  // The alarm can trigger from CLOCK or SETTINGS (not while already
  // RINGING). If it fires while the settings menu is open, Settings is
  // abandoned immediately (any uncommitted edit is simply discarded)
  // and ringing starts right away.
  if (timeTick && mode != Mode::RINGING && checkAlarmTrigger(now)) {
    enterRinging(now);
  }

  if (mode == Mode::RINGING) {
    // MENU dismisses for good (no reschedule); EXIT snoozes. Next
    // phase these swap to their final roles (MENU = alarm on/off,
    // EXIT = snooze) once dedicated hardware buttons exist.
    if (menu) {
      dismissAlarm(now);
    } else if (exitPressed && !isSnoozing) {
      // Bank the just-finished ringing segment before the snooze pause
      // starts — see ringingAccumulatedMs's comment above.
      ringingAccumulatedMs += millis() - ringingSessionStartMillis;
      snoozeAlarm(now);
      snoozeCount++;
      isSnoozing = true;
      digitalWrite(POWER_LED, LOW);
      updateAlarmSound(false); // same reasoning as dismissAlarm() — stop before the partial refresh below
      updateWakeSnoozeIconsPartial(snoozeCount);
    }

    if (timeTick) {
      if (checkAlarmTrigger(now)) {
        // Snooze interval elapsed: resume ringing, hide the ZZZ icons.
        isSnoozing = false;
        ringingSessionStartMillis = millis(); // start a fresh ringing segment
        digitalWrite(POWER_LED, HIGH);
        updateWakeSnoozeIconsPartial(0);
      }
      if (now.tm_min != lastDrawnMinute) {
        updateWakeTimePartial(now);
        lastDrawnMinute = now.tm_min;
      }
      // Only counts down while actually ringing — a long snooze must
      // not trigger auto-off on its own. Explicit request/bugfix: this
      // used to measure time since the whole session started
      // (including snooze pauses), so snoozing past the auto-off
      // interval would dismiss the alarm even though it wasn't ringing
      // at that moment.
      // "Ring mode" = Continuous skips auto-off entirely: the alarm
      // keeps ringing (and re-rings after every snooze) until MENU
      // dismisses it. Explicit request.
      if (!isSnoozing && !getRingContinuous()) {
        unsigned long autoOffMs = (unsigned long)getAutoOffMinutes() * 60000UL;
        unsigned long ringingElapsedMs = ringingAccumulatedMs + (millis() - ringingSessionStartMillis);
        if (ringingElapsedMs >= autoOffMs) {
          dismissAlarm(now);
        }
      }
    }
  } else if (mode == Mode::SETTINGS) {
    if (isSettingsEditing()) {
      // Only the selected row's displayed value changes here — the
      // selection itself doesn't move — so a single-row refresh
      // suffices.
      int index = getSettingsSelectedIndex();
      if (up) {
        // Reversed per explicit feedback — "up"/clockwise now
        // decreases the value while editing (opposite of row
        // navigation's up/down, which stays as-is). upSteps can be >1
        // if several encoder detents piled up while the previous
        // partial refresh below was blocking — applied in one shot
        // rather than one refresh per detent.
        settingsAdjustDraft(-upSteps);
        updateSettingsRowPartial(index);
      } else if (down) {
        settingsAdjustDraft(downSteps);
        updateSettingsRowPartial(index);
      } else if (confPressed) {
        settingsCommitEdit();
        syncTime();               // in case the timezone setting was the one just committed
        invalidatePreloadedSound(); // in case the Sound setting was the one just committed
        // Language row (index 14): committing it changes every other
        // row's label/tab/values language, so a single-row refresh isn't
        // enough — redraw the whole screen (labels + tabs + values all
        // switch together).
        if (index == 14) {
          drawSettingsScreen();
        } else {
          updateSettingsRowPartial(index);
        }
      } else if (exitPressed) {
        settingsCancelEdit();
        updateSettingsRowPartial(index);
      } else if (menu) {
        // MENU leaves Settings entirely (same as the browsing-state
        // handler below) — but commits the in-progress edit first,
        // same as CONF would, rather than discarding it the way
        // EXIT/cancel does. Explicit request: the current value should
        // be saved, not lost, if MENU is pressed mid-edit.
        settingsCommitEdit();
        syncTime();
        invalidatePreloadedSound();
        exitSettingsToClock(now); // full redraw anyway — flags all labels/date in the new language
      }
    } else {
      if (up || down) {
        int previousIndex = getSettingsSelectedIndex();
        SettingCategory previousCategory = getSettingsItem(previousIndex).category;
        // Clockwise ("up") feels natural as "next item" on a rotary
        // dial (confirmed backwards by the user when it instead moved
        // to the previous item, even though "up" correctly increases
        // values during editing) — so clockwise advances the index,
        // even though that's the opposite of moving up the screen.
        if (up) settingsSelectNext(); else settingsSelectPrev();
        if (getSettingsItem(getSettingsSelectedIndex()).category != previousCategory) {
          // Crossing a category boundary changes which rows are even
          // visible (not just which one is selected) — redraw the
          // whole row area + tab bar rather than just 1-2 rows.
          updateSettingsTabsPartial();
          updateSettingsRowsAreaPartial();
        } else {
          updateSettingsTwoRowsPartial(previousIndex, getSettingsSelectedIndex());
        }
      } else if (confPressed) {
        const SettingItem &selectedItem = getSettingsItem(getSettingsSelectedIndex());
        if (selectedItem.type == SettingType::Action) {
          // Two Action items now ("Upload sound", "WiFi") — both
          // intercepted here, before settingsBeginEdit() (which only
          // understands FunctionalInt), since they trigger a mode
          // switch instead of an edit-draft. Distinguished by label,
          // the simplest option for just two cases.
          if (strcmp(selectedItem.label, "WiFi") == 0) {
            mode = Mode::WIFI_SETUP;
            wifiSetupCancelable = true; // entered from a working Settings screen, EXIT can cancel back to it
            webAdminSuspend(); // the captive portal needs port 80 for itself
            beginWifiSetup();
            drawWifiSetupScreen();
          } else {
            mode = Mode::UPLOAD;
            webAdminSuspend(); // the upload page needs port 80 for itself
            beginSoundUpload();
            lastDrawnUploadStatus = getSoundUploadStatusLine();
            drawUploadScreen();
          }
        } else {
          // The "Sound" row's settingsBeginEdit() scans /sounds/ on the
          // SD card (SettingsMenu.cpp's refreshSoundChoiceList()),
          // guarded by the same sdLock() the background audio-preload
          // task (Sound.cpp) can be holding at any time nothing's
          // ringing. Try a non-blocking lock first so a genuinely free
          // card (the common case) shows no loading state at all — only
          // announce the wait when there actually is one, then let the
          // real (blocking) settingsBeginEdit() proceed once it's free.
          bool selectedIsSoundRow = selectedItem.type == SettingType::SoundChoice;
          if (selectedIsSoundRow) {
            if (sdTryLock()) {
              sdUnlock(); // was free — release immediately, refreshSoundChoiceList() below re-acquires it itself
            } else {
              updateSettingsRowLoadingPartial(getSettingsSelectedIndex());
            }
          }
          settingsBeginEdit();
          if (isSettingsEditing()) updateSettingsRowPartial(getSettingsSelectedIndex()); // no-op for Placeholder items
        }
      } else if (menu) {
        // MENU (Alarm on/off), not EXIT (Snooze) — exiting Settings
        // used to be bound to EXIT; moved here per explicit request.
        // No suppressExitButtonRelease()-style guard needed: unlike
        // EXIT, menuButtonPressed() is a plain press-edge trigger with
        // no release/long-press-based action of its own it could
        // spuriously re-fire once back in Mode::CLOCK (see Controls.h).
        exitSettingsToClock(now);
      }
    }
  } else if (mode == Mode::UPLOAD) {
    handleSoundUpload();
    String status = getSoundUploadStatusLine();
    if (status != lastDrawnUploadStatus) {
      lastDrawnUploadStatus = status;
      updateUploadStatusPartial();
    }
    if (exitPressed) {
      endSoundUpload();
      webAdminResume(); // port 80 back to the always-on admin portal
      // A file may have been uploaded or deleted while this screen was
      // open — the currently-selected sound's underlying file (or the
      // preloaded session already holding it open) could now be stale.
      invalidatePreloadedSound();
      mode = Mode::SETTINGS;
      drawSettingsScreen();
    }
  } else { // Mode::CLOCK
    if (confPressed && !editingAlarm) {
      mode = Mode::SETTINGS;
      drawSettingsScreen();
    } else {
      if (!editingAlarm && menuLongPressed) {
        // Long-press MENU (>=800ms): force the WiFi config portal
        // (AP + captive portal) even though the clock already has a
        // working connection — how the clock is moved to a new network
        // without reflashing. The short-press edge for the same hold
        // already ran the alarm toggle below earlier in this
        // iteration; that side effect is accepted (documented in
        // Controls.h).
        mode = Mode::WIFI_SETUP;
        wifiSetupCancelable = true;
        webAdminSuspend(); // the captive portal needs port 80 for itself
        beginWifiSetup();
        drawWifiSetupScreen();
      } else if (menu) {
        bool wasEnabled = getAlarm().enabled;
        setAlarmEnabled(!wasEnabled);
        // Turning the alarm ON from the clock face (not mid-edit) shows
        // the same "time until alarm" countdown a freshly committed
        // time would, per explicit request — turning it OFF doesn't
        // (nothing meaningful to count down to), and updateAlarmIconPartial()'s
        // own alarmEnabled gate would hide it either way even if left set.
        if (!editingAlarm && !wasEnabled && getAlarm().enabled) {
          showingAlarmCountdown = true;
          alarmCountdownShownAtMillis = millis();
        }
        AlarmTime shown = editingAlarm ? getAlarmDraft() : displayedAlarm(now);
        bool shownEnabled = editingAlarm ? getAlarm().enabled : shown.enabled;
        updateAlarmIconPartial(now, shown.hour, shown.minute, shownEnabled, editingAlarm,
                                showingAlarmCountdown, editingAlarmHour);
      }

      if (up || down) {
        if (!editingAlarm) {
          beginAlarmEdit();
          editingAlarm = true;
          editingAlarmHour = false; // every fresh edit starts on minutes
          showingAlarmCountdown = false; // a fresh edit always hides it, even mid-display-window
        }
        int step = editingAlarmHour ? kAlarmEditHourStepMinutes : kAlarmEditMinuteStepMinutes;
        // Reversed per explicit feedback — "up"/clockwise now
        // decreases the alarm time. upSteps/downSteps can be >1 if
        // several encoder detents piled up while the previous partial
        // refresh below was blocking — applied in one shot (e.g. +3
        // minutes) rather than one refresh per detent.
        int steps = up ? upSteps : downSteps;
        adjustAlarmEditMinutes((up ? -step : step) * steps);
        lastAlarmEditActivityMillis = millis();
        AlarmTime draft = getAlarmDraft();
        updateAlarmIconPartial(now, draft.hour, draft.minute, getAlarm().enabled, true, false, editingAlarmHour);
      } else if (editingAlarm && confPressed) {
        // Toggle which field rotating the encoder adjusts — see
        // editingAlarmHour's comment. Counts as edit activity too, so
        // just switching fields doesn't let the auto-commit timeout
        // expire mid-toggle.
        editingAlarmHour = !editingAlarmHour;
        lastAlarmEditActivityMillis = millis();
        AlarmTime draft = getAlarmDraft();
        updateAlarmIconPartial(now, draft.hour, draft.minute, getAlarm().enabled, true, false, editingAlarmHour);
      } else if (editingAlarm && exitPressed) {
        cancelAlarmEdit();
        editingAlarm = false;
        AlarmTime shown = displayedAlarm(now);
        updateAlarmIconPartial(now, shown.hour, shown.minute, shown.enabled, false, false, false);
      } else if (!editingAlarm && exitLongPressed) {
        // Long-press (≥800ms, fires once while still held): toggle the
        // wake-up light as a manual night light.
        toggleNightLight();
      } else if (!editingAlarm && exitShortReleased) {
        // Fires on release, and only for a hold that never reached the
        // long-press threshold above — mutually exclusive with it for
        // the same physical press, unlike exitPressed (which fires
        // immediately on the press edge, before it's known whether the
        // hold will turn out to be long). (Re)arms the frontlight for 30s.
        triggerFrontlight();
      } else if (editingAlarm && millis() - lastAlarmEditActivityMillis >= kAlarmEditTimeoutMs) {
        commitAlarmEdit();
        editingAlarm = false;
        showingAlarmCountdown = true;
        alarmCountdownShownAtMillis = millis();
        AlarmTime shown = displayedAlarm(now);
        updateAlarmIconPartial(now, shown.hour, shown.minute, shown.enabled, false, true, false);
      } else if (showingAlarmCountdown && millis() - alarmCountdownShownAtMillis >= kAlarmCountdownDisplayMs) {
        showingAlarmCountdown = false;
        AlarmTime shown = displayedAlarm(now);
        updateAlarmIconPartial(now, shown.hour, shown.minute, shown.enabled, false, false, false);
      }

      if (timeTick) {
        // lastDrawnMinute == -1 means the clock face has never been
        // fully drawn yet — happens when boot had no time available
        // (setup() skips drawFullClockFace() in that case) and time
        // only became available later, once WiFi/NTP came through in
        // the background. Without this, the very first draw would go
        // through the plain minute-only partial refresh below, leaving
        // the date/alarm-row/wifi-icon never drawn at all.
        bool dueForFullRefresh = lastDrawnMinute == -1 ||
                                  millis() - lastFullRefreshMillis >= kFullRefreshIntervalMs;
        if (dueForFullRefresh) {
          AlarmTime shown = displayedAlarm(now);
          drawFullClockFace(now, shown.hour, shown.minute, shown.enabled, showPowerLossWarning);
          showPowerLossWarning = false;
          lastFullRefreshMillis = millis();
          lastDrawnMinute = now.tm_min;
          lastDrawnWifiConnected = isWifiConnected();
        } else {
          if (now.tm_min != lastDrawnMinute) {
            updateTimePartial(now);
            lastDrawnMinute = now.tm_min;
          }
          bool wifiConnected = isWifiConnected();
          if (wifiConnected != lastDrawnWifiConnected) {
            updateWifiIconPartial(wifiConnected);
            lastDrawnWifiConnected = wifiConnected;
          }
        }
      }
    }
  }

  // One central call per iteration, mirroring the POWER_LED stand-in
  // right next to it (unchanged) — both track the same "should be
  // audibly/visibly alerting right now" condition.
  bool ringingNow = mode == Mode::RINGING;
  updateAlarmSound(ringingNow && !isSnoozing);
  // Keeps an already-lit frontlight from dimming out while the user is
  // actively in one of these states — Settings open, the Upload
  // sound/WiFi setup screens (both reached from Settings and, like it,
  // often used in the dark), the alarm time being edited, or the
  // post-edit/post-enable "x min from now" countdown showing — per
  // explicit report that it used to fade out from under them
  // mid-interaction. See Light.h/.cpp for how the grace period once
  // this goes false again works.
bool frontlightKeepAlive = mode == Mode::SETTINGS || mode == Mode::UPLOAD || mode == Mode::WIFI_SETUP || editingAlarm || showingAlarmCountdown;
  updateLight(now, ringingNow, frontlightKeepAlive);

  // Skipped during an active upload: handleSoundUpload() only makes
  // progress on the transfer once per loop() iteration, so a fixed
  // 10ms delay here caps upload throughput to roughly one TCP chunk
  // per 10ms regardless of how fast the network/SD card could
  // actually go — button-debounce timing (the reason for the delay
  // elsewhere) doesn't matter in this mode, only EXIT is polled.
    if (mode != Mode::UPLOAD) delay(10);
#ifdef ENABLE_BATTERY
  // Deep-sleep entry: idle on battery, not alerting/editing/settings
  if (isOnBattery() && !ringingNow && !isSnoozing &&
      mode == Mode::CLOCK && !editingAlarm && !frontlightKeepAlive) {
    int sleepRefresh = getSleepRefreshMinutes();
    if (sleepRefresh > 0) {
      AlarmTime nextAlarm;
      int minUntilNext;
      if (getNextAlarm(now, nextAlarm, minUntilNext) && nextAlarm.enabled) {
        int sleepSeconds = sleepRefresh * 60;
        if (minUntilNext < sleepRefresh) sleepSeconds = minUntilNext * 60;
        if (sleepSeconds >= 60) prepareDeepSleep(sleepSeconds);
      }
    }
  }
#endif
}
