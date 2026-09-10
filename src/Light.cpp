#include "Light.h"

#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>

#include "AlarmSettings.h"
#include "PinConfig.h"
#include "SettingsMenu.h"

namespace {

constexpr uint16_t kLedCount = 17;
constexpr uint16_t kWakeupSegmentStart = 0;
constexpr uint16_t kWakeupSegmentCount = 11;
constexpr uint16_t kFrontlightSegmentStart = 11;
constexpr uint16_t kFrontlightSegmentCount = 6;

constexpr unsigned long kFrontlightOnMs = 10000;   // full brightness duration after a manual trigger
constexpr unsigned long kFrontlightFadeMs = 5000;  // fade-out duration after that
constexpr unsigned long kNightLightFadeMs = 1000;  // fade duration on each on/off toggle
// Grace period the frontlight stays at full brightness for after
// frontlightKeepAlive (see updateLight()) drops back to false — e.g.
// leaving Settings back to the plain clock face — before it starts
// fading out, same as if it had just been (re)triggered that far in
// advance. Also reused as the interval frontlightKeepAlive re-arms
// itself by while it's true (see updateLight()), which is what keeps
// the light held on for as long as that stays true, however much
// longer than kFrontlightOnMs that ends up being.
constexpr unsigned long kFrontlightGraceMs = 3000;

// The two manual states' brightness are each their own Settings item
// now ("Nightlight intensity"/"Frontlight intensity", 0-100% in 10%
// steps) — see getNightlightIntensityPercent()/
// getFrontlightIntensityPercent() (SettingsMenu.h), independent of the
// wake-up ramp's own Intensity setting.
//
// Frontlight color *shape* (the white:red ratio) was user-calibrated
// by eye on the real panel via hw_tests/led_strip_test.cpp's "front"
// target — warmer and dimmer than a plain white value alone — at the
// values white=20/red=17 (of 255), which is defined here as the
// *100%* endpoint scaled down to reproduce those exact numbers at the
// Frontlight intensity setting's 10% mark, its minimum (200*0.10=20,
// 170*0.10=17) — per explicit request that 10% (not 20%) match the
// pre-existing calibration exactly, and that 10% be the floor since
// anything dimmer isn't actually visible (same reasoning as Volume's
// own 10% floor, SettingsMenu.cpp).
constexpr uint8_t kFrontlightMaxWhite = 200;
constexpr uint8_t kFrontlightMaxRed = 170;

// SK1612 (RGBW, 4 bytes/pixel) — swapped in for the WS2805 (RGBCCT)
// originally planned here. Two things are unverified until tested on
// real hardware (same as the WS2805's color order was):
// - Color order: NeoGrbwFeature (G,R,B,W) is the common wire order for
//   this WS2812-family style of RGBW chip — swap to NeoRgbwFeature (or
//   another Neo4ByteFeature permutation) if channels come out wrong.
// - Protocol timing: no dedicated "Sk1612" method exists in NeoPixelBus
//   (unlike Sk6812, which has its own distinct timing profile) and no
//   public datasheet was found for this specific chip, so this
//   defaults to NeoWs2812xMethod (the common 800kHz WS2812-family
//   timing most RGBW chips of this style use) — if the strip flickers
//   or shows garbled colors, try NeoSk6812Method instead (see
//   hw_tests/led_strip_test.cpp, which has both readily swappable for
//   exactly this kind of on-hardware verification).
//
// NeoPixelBusLg (not plain NeoPixelBus) specifically so this applies
// the same NeoGammaEquationMethod gamma correction that
// hw_tests/led_strip_test.cpp's identical NeoPixelBusLg<...> gets by
// default (even at full luminance) — without it, raw values dialed in
// on that test tool (which are gamma-corrected before hitting the
// LEDs) looked far dimmer there than the same raw numbers sent
// uncorrected through a plain NeoPixelBus here. Luminance is left at
// its default 255 (no additional dimming) so gamma is the only active
// correction, matching the test tool's default state.
NeoPixelBusLg<NeoGrbwFeature, NeoWs2812xMethod> strip(kLedCount, LED_STRIP_DATA);

// 0 = inactive (never triggered, or its fade-out fully completed).
// Otherwise the millis() deadline full brightness holds until, after
// which a kFrontlightFadeMs fade to 0 begins — set to
// millis()+kFrontlightOnMs by triggerFrontlight(), and continuously
// pushed kFrontlightGraceMs further into the future for as long as
// frontlightKeepAlive stays true (see updateLight()), which is what
// lets an active UI state hold the light on far longer than
// kFrontlightOnMs while never resurrecting one that's already off.
unsigned long frontlightHoldUntilMs = 0;
// True while "Frontlight mode" (Settings, System tab) is Manual and a
// short EXIT press has switched the frontlight fully on — stays true,
// ignoring frontlightHoldUntilMs entirely, until endFrontlightManualHold()
// is called (main.cpp's dismissAlarm()). Irrelevant in Auto mode, where
// triggerFrontlight() never sets it.
bool frontlightManualOn = false;
bool nightLightOn = false;
// millis() at the most recent toggleNightLight()/stopWakeupLight() —
// underflowed on purpose so nightLightActive() reads false from boot
// (an un-underflowed 0 would look like "just toggled off" for
// nightLightActive()'s first kNightLightFadeMs after boot, since
// millis() also starts near 0, causing a spurious brief fade-out flash).
unsigned long nightLightToggledAtMs = 0 - kNightLightFadeMs;

// Held at full dismissFadeFromColor for this long after stopWakeupLight()
// is called, before the kDismissFadeMs fade-out itself begins —
// explicit request, to put a little daylight between "screen refresh
// just finished" and "fade starts", independent of whatever exact
// cause made the fade look choppy right up against that refresh.
constexpr unsigned long kDismissHoldMs = 500;
constexpr unsigned long kDismissFadeMs = 1000; // fade-out duration once the hold above ends
// 0 = inactive (never dismissed yet, or the hold+fade fully
// completed). Otherwise the millis() timestamp stopWakeupLight() was
// called at — updateLight() holds dismissFadeFromColor steady for
// kDismissHoldMs, then fades it down to black over the following
// kDismissFadeMs, both measured from that timestamp.
unsigned long dismissFadeStartMs = 0;
RgbwColor dismissFadeFromColor(0);

// Last-sent state per segment — .Show() is a real RMT transfer, only
// call it when something actually changed, same reasoning as
// EpaperDisplay.cpp only partial-refreshing on an actual change. (Not
// much of a saving while a fade is actively animating — the value
// changes most ticks anyway — but avoids needless Show() calls while
// idle/off or holding steady.)
RgbwColor lastFrontlightColor(1, 1, 1, 1); // impossible-to-start-at value forces the first Show()
RgbwColor lastWakeupColor(1, 1, 1, 1);

// Same color progression as hw_tests/led_strip_test.cpp's `sunrise`
// command (dark-orange -> deepening orange -> golden amber -> warm
// daylight white), duplicated here rather than shared across the two
// separate PlatformIO envs — re-applied to the main firmware's wake-up
// ramp per explicit request, after an earlier explicit request had
// reverted this same effect back to a plain white brightness fade (see
// git history/project memory) — if this is ever revisited again, that
// prior back-and-forth is worth rereading first.
struct SunriseStage {
  uint8_t r, g, b, w;
};
constexpr SunriseStage kSunriseStages[] = {
    {25, 12, 0, 0},        // pre-dawn: dark orange, dim
    {70, 30, 0, 0},        // deeper orange
    {150, 60, 0, 0},       // orange, brighter
    {220, 90, 0, 5},       // bright orange
    {255, 110, 0, 10},     // orange-amber
    {255, 130, 15, 40},    // amber
    {255, 150, 30, 100},   // golden amber
    {255, 180, 90, 180},   // warm yellow-white
    {255, 210, 150, 255},  // warm daylight white
};
constexpr int kSunriseStageCount = sizeof(kSunriseStages) / sizeof(kSunriseStages[0]);

// Interpolated color at progress `fraction` (0.0 = pre-dawn start,
// 1.0 = full warm daylight white) through the stage table above —
// same interpolation as led_strip_test.cpp's runSunrise(), just
// evaluated at a single point in time instead of stepped through
// continuously, since here it's driven by how close `now` is to the
// alarm rather than a fixed 20s demo duration.
RgbwColor sunriseColor(float fraction) {
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  float stagePos = fraction * (kSunriseStageCount - 1);
  int stageIndex = (int)stagePos;
  if (stageIndex >= kSunriseStageCount - 1) stageIndex = kSunriseStageCount - 2;
  float stageFraction = stagePos - stageIndex;
  const SunriseStage &a = kSunriseStages[stageIndex];
  const SunriseStage &b = kSunriseStages[stageIndex + 1];
  return RgbwColor((uint8_t)(a.r + (b.r - a.r) * stageFraction), (uint8_t)(a.g + (b.g - a.g) * stageFraction),
                    (uint8_t)(a.b + (b.b - a.b) * stageFraction), (uint8_t)(a.w + (b.w - a.w) * stageFraction));
}

// How often the automatic before-alarm ramp's brightness is
// recomputed — was implicitly once a minute (minutesUntil() only had
// minute resolution), throttled explicitly to this instead now that
// secondsUntil() can update every real second, per explicit request
// for a smoother-looking ramp without recomputing/Show()-ing on every
// single loop() iteration.
constexpr unsigned long kRampUpdateIntervalMs = 10000;
unsigned long lastRampUpdateMs = 0;
RgbwColor cachedRampColor(0);
// The alarm the ramp is currently building toward, from getNextAlarm()
// (next occurrence of any enabled alarm) — refreshed together with the
// cached color on each kRampUpdateIntervalMs tick.
AlarmTime nextRampAlarm = {7, 0, true, kAllDaysMask};
int nextRampMinutes = 0;

void setSegment(uint16_t start, uint16_t count, RgbwColor color) {
  for (uint16_t i = 0; i < count; i++) strip.SetPixelColor(start + i, color);
}

// 1.0 until frontlightHoldUntilMs, then a linear fade to 0 over
// kFrontlightFadeMs, then inactive (0) from then on. Clears its own
// state once the fade fully completes, so triggering later starts a
// clean new cycle rather than resuming a stale one. Returned as a
// 0..1 fraction (rather than a single brightness byte) so it can scale
// both the white and red channels of the calibrated frontlight color
// together.
float frontlightFraction() {
  if (frontlightHoldUntilMs == 0) return 0.0f;
  long remaining = (long)(frontlightHoldUntilMs - millis());
  if (remaining > 0) return 1.0f;
  // Below a visibility threshold, skip the fade-out entirely and cut
  // straight to off — a 5s fade that dim reads as a glitch/flicker
  // rather than a smooth transition. Explicit request.
  if (getFrontlightIntensityPercent() < 40) {
    frontlightHoldUntilMs = 0;
    return 0.0f;
  }
  unsigned long fadeElapsed = (unsigned long)(-remaining);
  if (fadeElapsed >= kFrontlightFadeMs) {
    frontlightHoldUntilMs = 0;
    return 0.0f;
  }
  return 1.0f - (float)fadeElapsed / (float)kFrontlightFadeMs;
}

// Linear fade between 0 and the "Nightlight intensity" Settings item
// (0-100% -> 0-255) over kNightLightFadeMs, in whichever direction
// nightLightOn's last toggle set as the target. Toggling again before
// a fade finishes jumps to the new fade's own start point rather than
// continuing smoothly from the interrupted value — a minor edge case,
// not worth tracking the live interrupted value for. Also doesn't
// track a live *intensity setting* change mid-fade — same reasoning.
uint8_t nightLightBrightness() {
  unsigned long elapsed = millis() - nightLightToggledAtMs;
  uint8_t target = (uint8_t)(getNightlightIntensityPercent() / 100.0f * 255.0f);
  uint8_t from = nightLightOn ? 0 : target;
  uint8_t to = nightLightOn ? target : 0;
  // Below a visibility threshold, skip the fade entirely — in *both*
  // directions, symmetrically — and cut straight to the target: a
  // fade that dim reads as a glitch, not a transition, same reasoning
  // as the frontlight's own threshold above. (An earlier version
  // disabled fade-in unconditionally regardless of intensity, which
  // wasn't quite right — corrected per explicit feedback: fade-in
  // should follow the same threshold fade-out already does, not be
  // permanently instant.)
  if (getNightlightIntensityPercent() < 30) return to;
  if (elapsed >= kNightLightFadeMs) return to;
  float fraction = (float)elapsed / (float)kNightLightFadeMs;
  return (uint8_t)(from + (to - from) * fraction);
}

// Whether the night light should still be driving the wake-up segment
// right now — true while it's the target state, and for a further
// kNightLightFadeMs after being turned off (so the fade-out actually
// gets to run before the automatic ramp/off takes back over).
bool nightLightActive() {
  return nightLightOn || (millis() - nightLightToggledAtMs < kNightLightFadeMs);
}

} // namespace

void initLight() {
  strip.Begin();
  strip.ClearTo(RgbwColor(0));
  strip.Show();
}

void triggerFrontlight() {
  if (getFrontlightManualMode()) {
    frontlightManualOn = true;
  } else {
    frontlightHoldUntilMs = millis() + kFrontlightOnMs;
  }
}

void endFrontlightManualHold() {
  if (!frontlightManualOn) return;
  frontlightManualOn = false;
  frontlightHoldUntilMs = millis(); // frontlightFraction() starts its normal fade-out from here
}

void toggleNightLight() {
  nightLightOn = !nightLightOn;
  nightLightToggledAtMs = millis();
}

void stopWakeupLight() {
  // Backdating the toggle timestamp makes nightLightActive()
  // immediately report false (in case a manual night light happened to
  // be on).
  nightLightOn = false;
  nightLightToggledAtMs = millis() - kNightLightFadeMs;
  // Fade out from whatever was last actually showing (typically the
  // ringing color) over kDismissFadeMs, rather than cutting instantly
  // — explicit request. (An earlier version of this function cut
  // instantly on purpose; that decision is now superseded.)
  dismissFadeFromColor = lastWakeupColor;
  dismissFadeStartMs = millis();
}

void updateLight(const struct tm &now, bool ringing, bool frontlightKeepAlive) {
  bool changed = false;

  // Frontlight segment: independent on-timer + fade-out, no priority
  // list — except frontlightKeepAlive, which (only while the light is
  // already active — see frontlightHoldUntilMs's comment) keeps
  // re-arming a fresh kFrontlightGraceMs window so it can't dim out
  // while e.g. Settings is open, however much longer than the normal
  // kFrontlightOnMs that ends up taking. The moment it goes false
  // again, whatever window was most recently armed (this, or a plain
  // triggerFrontlight()) plays out normally.
  if (frontlightKeepAlive && frontlightHoldUntilMs != 0) {
    frontlightHoldUntilMs = millis() + kFrontlightGraceMs;
  }
  RgbwColor frontlightColor;
  if (isEditingFrontlightIntensity()) {
    // Live preview: full "on" (ignore the on/off timer/fraction
    // entirely) at whatever intensity is currently being dialed in,
    // using the *draft* value, not the committed one — same idea as
    // the wake-up-light previews below.
    float previewFrac = getFrontlightIntensityPreviewPercent() / 100.0f;
    frontlightColor = RgbwColor((uint8_t)(kFrontlightMaxRed * previewFrac), 0, 0,
                                 (uint8_t)(kFrontlightMaxWhite * previewFrac));
  } else if (ringing) {
    // Full "on" for the whole ringing session, through any number of
    // snoozes (mode stays Mode::RINGING the whole time — see
    // main.cpp's isSnoozing comment — so `ringing` alone already covers
    // that), until dismissed. Explicit request: the frontlight should
    // always be lit while the alarm is going off, independent of
    // whether/when it was last manually triggered. Ignores
    // frontlightHoldUntilMs entirely on purpose, same as the
    // wake-up-light segment's own ringing branch below — whatever timer
    // state it's in resumes normally once ringing ends.
    float frontlightIntensityFrac = getFrontlightIntensityPercent() / 100.0f;
    frontlightColor = RgbwColor((uint8_t)(kFrontlightMaxRed * frontlightIntensityFrac), 0, 0,
                                 (uint8_t)(kFrontlightMaxWhite * frontlightIntensityFrac));
  } else if (frontlightManualOn) {
    // Manual mode's hold — full "on", no timer, until
    // endFrontlightManualHold() is called at the next dismiss.
    float frontlightIntensityFrac = getFrontlightIntensityPercent() / 100.0f;
    frontlightColor = RgbwColor((uint8_t)(kFrontlightMaxRed * frontlightIntensityFrac), 0, 0,
                                 (uint8_t)(kFrontlightMaxWhite * frontlightIntensityFrac));
  } else {
    float frontlightFrac = frontlightFraction();
    float frontlightIntensityFrac = getFrontlightIntensityPercent() / 100.0f;
    frontlightColor = RgbwColor((uint8_t)(kFrontlightMaxRed * frontlightIntensityFrac * frontlightFrac), 0, 0,
                                 (uint8_t)(kFrontlightMaxWhite * frontlightIntensityFrac * frontlightFrac));
  }
  if (!(frontlightColor == lastFrontlightColor)) {
    setSegment(kFrontlightSegmentStart, kFrontlightSegmentCount, frontlightColor);
    lastFrontlightColor = frontlightColor;
    changed = true;
  }

  // Wake-up-light segment: ringing > Settings live-preview (Wake-up
  // intensity or Nightlight intensity, whichever is actively being
  // edited) > dismiss fade-out (incl. its own 1s tail, see
  // stopWakeupLight()) > manual night light (incl. its own fade-out
  // tail) > automatic before-alarm ramp > off, in that priority order.
  // Uses the same color-changing sunrise progression
  // as hw_tests/led_strip_test.cpp's `sunrise` command (sunriseColor()
  // above) — re-added per explicit request, after an earlier explicit
  // request had reverted this same effect back to a plain white
  // brightness fade (see sunriseColor()'s comment).
  //
  // Both the ringing-brightness and the ramp are gated on
  // getLightEnabled() — that setting means "the automatic wake-up
  // light is off," full stop, not just "don't start the ramp early."
  // The manual night light stays independent of it on purpose: it's a
  // deliberate manual override, not part of the automatic behavior the
  // setting controls.
  RgbwColor wakeupColor(0);
  if (ringing && getLightEnabled()) {
    // Capped by Intensity, same as the ramp — the ramp's own last
    // color (at fraction=1.0, right as the alarm fires) is already
    // exactly sunriseColor(1.0f) dimmed by the same intensityByte, so
    // this keeps ringing perfectly continuous with wherever the ramp
    // left off instead of jumping to full brightness regardless of
    // Intensity.
    uint8_t intensityByte = (uint8_t)(getLightIntensityPercent() / 100.0f * 255.0f);
    wakeupColor = sunriseColor(1.0f).Dim(intensityByte);
    dismissFadeStartMs = 0; // cancel any pending dismiss hold/fade-out if ringing resumes
  } else if (isEditingLightIntensity()) {
    // Live preview of the final (fully-ramped, right-as-the-alarm-
    // fires) wake-up light color at whatever intensity is currently
    // being dialed in on the Settings row — explicit request, so the
    // result is visible directly instead of having to guess from the
    // percentage alone. Takes priority over everything below (dismiss
    // fade/night light/ramp) while actively editing this one item.
    uint8_t previewByte = (uint8_t)(getLightIntensityPreviewPercent() / 100.0f * 255.0f);
    wakeupColor = sunriseColor(1.0f).Dim(previewByte);
  } else if (isEditingNightlightIntensity()) {
    // Live preview of the manual night light's brightness at whatever
    // intensity is currently being dialed in — same idea as the
    // Wake-up-intensity preview above, just plain white (the night
    // light has no color progression) and full "on" (ignoring its own
    // fade timer) so the draft value is immediately visible.
    uint8_t previewByte = (uint8_t)(getNightlightIntensityPreviewPercent() / 100.0f * 255.0f);
    wakeupColor = RgbwColor(0, 0, 0, previewByte);
  } else if (dismissFadeStartMs != 0 && millis() - dismissFadeStartMs < kDismissHoldMs + kDismissFadeMs) {
    unsigned long elapsed = millis() - dismissFadeStartMs;
    if (elapsed < kDismissHoldMs) {
      wakeupColor = dismissFadeFromColor; // hold steady, fade hasn't started yet
    } else {
      float fadeFraction = (float)(elapsed - kDismissHoldMs) / (float)kDismissFadeMs;
      wakeupColor = dismissFadeFromColor.Dim((uint8_t)(255.0f * (1.0f - fadeFraction)));
    }
  } else if (nightLightActive()) {
    wakeupColor = RgbwColor(0, 0, 0, nightLightBrightness());
  } else if (getLightEnabled() && getNextAlarm(now, nextRampAlarm, nextRampMinutes)) {
    // getNextAlarm(), not just getLightEnabled() ("Wake-up" > 0%)
    // — aborts the ramp (not faded, cuts on the very next frame) the
    // moment there's no enabled alarm left to build toward, since the
    // ramp builds toward a specific alarm that, disabled, is no longer
    // going to fire. Explicit request.
    // Targets the *next occurrence* of any enabled alarm (honoring its
    // daysMask) rather than fixed alarm 0 — multi-alarm rework.
    int durationMin = getLightDurationMinutes();
    if (durationMin > 0) {
      // Recomputed at most every kRampUpdateIntervalMs, not every
      // loop() iteration/every real second — plenty smooth (see its
      // comment) without needlessly recalculating and re-sending to
      // the strip far more often than the eye can tell apart.
      if (millis() - lastRampUpdateMs >= kRampUpdateIntervalMs) {
        lastRampUpdateMs = millis();
        long secondsUntilAlarm = (long)nextRampMinutes * 60L - now.tm_sec;
        long durationSec = (long)durationMin * 60L;
        Serial.printf("[Light] ramp check: secondsUntilAlarm=%ld durationSec=%ld alarm=%02d:%02d now=%02d:%02d:%02d\n",
                      secondsUntilAlarm, durationSec, nextRampAlarm.hour, nextRampAlarm.minute, now.tm_hour, now.tm_min,
                      now.tm_sec);
        if (secondsUntilAlarm <= durationSec) {
          float elapsedFraction = 1.0f - (float)secondsUntilAlarm / (float)durationSec;
          if (elapsedFraction < 0.0f) elapsedFraction = 0.0f;
          uint8_t intensityByte = (uint8_t)(getLightIntensityPercent() / 100.0f * 255.0f);
          cachedRampColor = sunriseColor(elapsedFraction).Dim(intensityByte);
        } else {
          cachedRampColor = RgbwColor(0);
        }
      }
      wakeupColor = cachedRampColor;
    }
  }
  if (!(wakeupColor == lastWakeupColor)) {
    setSegment(kWakeupSegmentStart, kWakeupSegmentCount, wakeupColor);
    lastWakeupColor = wakeupColor;
    changed = true;
  }

  if (changed) strip.Show();
}
