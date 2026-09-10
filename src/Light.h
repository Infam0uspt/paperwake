#pragma once

#include <time.h>

// Single WS2805 (RGB+CCT) LED strip split into two pixel segments (see
// Light.cpp): a wake-up light (ramps up automatically before the
// alarm, per the Settings menu's Light tab — On/Off, Intensity,
// Duration — and doubles as a manual night light via EXIT long-press)
// and a frontlight (lights the e-paper panel, EXIT short-press for
// 30s). Same self-contained-module pattern as Sound.cpp.

void initLight();

// Call once per loop() iteration (like updateAlarmSound()) — resolves
// both segments' state and pushes it to the strip only when something
// actually changed. `frontlightKeepAlive` should be true whenever the
// user is actively in a UI state where the frontlight should stay lit
// regardless of its own on-timer — Settings open, the alarm time being
// edited, or the "x min from now" countdown showing (main.cpp) — so it
// doesn't dim out from under them mid-interaction; only holds an
// *already* active frontlight on (see triggerFrontlight()), never
// turns one on from fully off. Once it goes false again, the
// frontlight starts fading after a short (few-second) grace period —
// see kFrontlightGraceMs in Light.cpp.
void updateLight(const struct tm &now, bool ringing, bool frontlightKeepAlive);

// EXIT short-press in Mode::CLOCK. Behavior depends on the "Frontlight
// mode" Settings item (System tab):
// - Auto: (re)arms the frontlight segment at full brightness for
//   kFrontlightOnMs (restarts the window if already lit), then fades
//   out over kFrontlightFadeMs — unless frontlightKeepAlive (see
//   updateLight()) is holding it on for longer.
// - Manual: switches the frontlight segment fully on with no timer at
//   all — it stays on until endFrontlightManualHold() is called.
void triggerFrontlight();

// EXIT long-press in Mode::CLOCK — toggles the wake-up-light segment
// on/off as a manual night light (independent of the automatic ramp).
void toggleNightLight();

// Called from main.cpp's dismissAlarm() — forces the wake-up-light
// segment off, including clearing a manually-toggled night light, so a
// dismissed alarm never leaves it lingering at some earlier state.
void stopWakeupLight();

// Called from main.cpp's dismissAlarm() — ends a manual-mode frontlight
// hold (see triggerFrontlight()) if one is active, per explicit
// request that "Manual" keeps the frontlight on until the next alarm
// is dismissed. No-op if the frontlight isn't currently in a manual
// hold (e.g. it's in Auto mode, or was never triggered). Starts the
// normal kFrontlightFadeMs fade-out rather than cutting instantly, same
// as every other light transition in this module.
void endFrontlightManualHold();
