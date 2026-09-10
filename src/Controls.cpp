#include "Controls.h"

#include <Arduino.h>

#include "PinConfig.h"

namespace {

constexpr unsigned long kDebounceMs = 30;
// How long EXIT must be held before it counts as a "long press" (night
// light toggle) rather than a plain press (frontlight) — see
// exitButtonLongPressed(). Comfortably above a normal tap, below the
// point of feeling unresponsive.
constexpr unsigned long kLongPressThresholdMs = 800;
// A single physical press of this dial mechanically holds the
// contact closed for ~500-600ms (measured), well longer than a
// typical tactile-button tap — kRepeatDelayMs must clear that or
// every single press also fires one auto-repeat, i.e. always +2.
constexpr unsigned long kRepeatDelayMs = 750;    // hold time before auto-repeat kicks in
constexpr unsigned long kRepeatIntervalMs = 150; // time between repeat fires once held

struct Button {
  Button(int p, const char *n, bool repeat = false) : pin(p), name(n), autoRepeat(repeat) {}

  int pin;
  const char *name;
  bool autoRepeat; // keep firing pending=true while held, not just on press
  bool lastStable = true; // buttons are active-low (pull-up)
  bool lastReading = true;
  unsigned long lastChangeMs = 0;
  // Accumulates (see consume() below) rather than a plain flag — for
  // upBtn/downBtn this can reach >1 (multiple encoder detents piled up
  // between two consumes); every other button only ever reaches 1.
  int pending = 0;
  unsigned long pressStartMs = 0;
  unsigned long lastRepeatMs = 0;
  // Long-press support (independent of autoRepeat above, a different
  // feature: this fires once per hold, not repeatedly) — see
  // exitButtonLongPressed().
  bool longPressPending = false;
  bool longPressFired = false;
  // Fires on release, but only for a hold that never became a long
  // press — see exitButtonShortReleased().
  bool shortPressPending = false;
  // Whether the pending firing currently queued is a sustained/fast
  // one (auto-repeat continuation, or a quick encoder detent) rather
  // than a single deliberate press — see rotateStepWasFast() below.
  bool lastStepFast = false;
};

Button menuBtn{BTN_MENU, "MENU"};
Button exitBtn{BTN_EXIT, "EXIT"};
Button upBtn{ROTATE_UP, "UP", true};
Button downBtn{ROTATE_DOWN, "DOWN", true};
Button confBtn{ROTATE_CONF, "CONF"};
// The loose EC11's push button — debounced the same simple way as
// CONF, no auto-repeat. Merged into rotateConfPressed() below.
Button encoderSwBtn{ENCODER_SW, "ENCODER_SW"};
// Dedicated external "Snooze"/"Alarm on/off" buttons — merged with
// exitBtn/menuBtn respectively (same pattern as encoderSwBtn above) in
// exitButtonPressed()/exitButtonLongPressed()/exitButtonShortReleased()/
// menuButtonPressed(), so main.cpp can't tell which physical button fired.
Button snoozeBtn{SNOOZE_BTN, "SNOOZE"};
Button alarmToggleBtn{ALARM_TOGGLE_BTN, "ALARM_TOGGLE"};

Button *allButtons[] = {&menuBtn, &exitBtn, &upBtn, &downBtn, &confBtn, &encoderSwBtn, &snoozeBtn, &alarmToggleBtn};

// Quadrature decoding for the loose EC11 rotary encoder (Stap 1) —
// wired alongside, not instead of, the rotate switch above. Standard
// table-based decode: indexed by (previous 2-bit AB reading << 2 |
// current 2-bit AB reading), yields +1/-1 per valid quarter-step, 0
// for a repeated/bounced reading. A full mechanical detent is 4
// quarter-steps on a typical EC11 — kQuarterStepsPerDetent below is
// the thing to change first if empirical testing (once this is
// physically wired) shows it detents differently.
constexpr int8_t kQuadratureTable[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
};
constexpr int32_t kQuarterStepsPerDetent = 4;

// Two detents completing within this long of each other counts as a
// "fast" turn (see rotateStepWasFast()). Must be measured right here,
// inside the ISR — measuring it later in pollAll()/main.cpp would be
// corrupted by e-paper partial refreshes, which block loop() for
// hundreds of ms after every step and would make every step look
// "slow" no matter how fast the dial is actually spinning (the same
// class of bug documented for button auto-repeat timing).
constexpr unsigned long kEncoderFastDetentGapMs = 220;

volatile uint8_t encoderPrevAB = 0;
volatile int32_t encoderRawSteps = 0;
volatile int32_t encoderDetentCountAtLastTimestamp = 0;
volatile unsigned long encoderLastDetentMs = 0;
volatile unsigned long encoderPrevDetentMs = 0;
int32_t encoderConsumedSteps = 0;
int32_t encoderLastDetentSign = 0; // sign of the most recently registered detent; 0 = none yet

// IRAM_ATTR: ESP32 interrupt handlers must live in IRAM, not flash,
// so they stay callable even while flash is busy (e.g. during a
// write) — a plain function here would crash unpredictably.
void IRAM_ATTR onEncoderChange() {
  // A/B swapped on purpose (not a wiring change) — measured on real
  // hardware that clockwise produced DOWN with A/B in schematic order;
  // swapping here flips the sign so clockwise = UP as expected.
  uint8_t currAB = (digitalRead(ENCODER_B) << 1) | digitalRead(ENCODER_A);
  int8_t delta = kQuadratureTable[(encoderPrevAB << 2) | currAB];
  encoderPrevAB = currAB;
  if (delta == 0) return;
  encoderRawSteps += delta;
  // Truncating division: fine here, this is only used to notice when
  // a detent boundary was just crossed, not as the authoritative step
  // count (pollAll() computes that separately from the raw total).
  int32_t detentCount = encoderRawSteps / kQuarterStepsPerDetent;
  if (detentCount != encoderDetentCountAtLastTimestamp) {
    encoderPrevDetentMs = encoderLastDetentMs;
    encoderLastDetentMs = millis();
    encoderDetentCountAtLastTimestamp = detentCount;
  }
}

void pollButton(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    Serial.printf("[Controls] %s (GPIO%d) raw -> %d\n", b.name, b.pin, reading);
    b.lastReading = reading;
    b.lastChangeMs = millis();
  }
  if (millis() - b.lastChangeMs > kDebounceMs && reading != b.lastStable) {
    b.lastStable = reading;
    if (b.lastStable == LOW) { // press edge (active-low)
      b.pending += 1;
      b.lastStepFast = false; // a fresh press is always "slow" (fine step)
      b.pressStartMs = millis();
      b.lastRepeatMs = b.pressStartMs;
      b.longPressFired = false; // fresh press, long-press hasn't fired yet this hold
    } else {
      // Release edge: a "short press" fires here, not on the press
      // edge — and only if this hold never reached the long-press
      // threshold, which is what keeps the two mutually exclusive for
      // the same physical press (see exitButtonShortReleased()).
      if (!b.longPressFired) b.shortPressPending = true;
    }
  }
  // Gated on the fresh raw `reading`, not the debounced `b.lastStable`:
  // debounce needs a follow-up poll >=kDebounceMs later to confirm a
  // transition, so right after loop() was blocked for a while (an
  // e-paper refresh, e.g.), the very first poll can see the button
  // already physically released (reading == HIGH) while b.lastStable
  // hasn't caught up yet — checking b.lastStable here would fire one
  // phantom repeat in that gap using a stale, since-elapsed
  // pressStartMs. Reading the live pin state instead reflects reality
  // immediately and costs nothing during a genuine hold, since
  // reading stays LOW the whole time regardless.
  if (b.autoRepeat && reading == LOW) {
    unsigned long now = millis();
    if (now - b.pressStartMs >= kRepeatDelayMs && now - b.lastRepeatMs >= kRepeatIntervalMs) {
      b.pending += 1;
      b.lastStepFast = true; // sustained hold = a "fast" step (coarse jump)
      b.lastRepeatMs = now;
    }
  }

  // Long-press: fires once per hold, independent of autoRepeat (a
  // separate feature — a long-pressable button need not auto-repeat,
  // and vice versa). Requires BOTH the debounce-confirmed b.lastStable
  // AND the live `reading` to currently read LOW — two different bugs,
  // found live, each ruled out by requiring the other:
  // - Using only live `reading` (like autoRepeat does): in the ~30ms
  //   window before debounce confirms a *fresh* press edge (which is
  //   what actually updates pressStartMs), `reading` already reads LOW
  //   while pressStartMs still holds whatever value it had from the
  //   *previous* press — often long ago — so `millis() - pressStartMs`
  //   instantly blows past the threshold on what was really a short
  //   tap. b.lastStable alone rules this out (still HIGH in that window).
  // - Using only b.lastStable (the earlier fix for the bug above): if a
  //   blocking call elsewhere (e.g. drawFullClockFace()'s ~2s full
  //   e-paper refresh, triggered by the very press this button is
  //   mid-debouncing) delays polling long enough that the *real*
  //   physical release happens during that block, the first poll
  //   afterward sees live `reading` already HIGH (truly released) but
  //   b.lastStable is still LOW (debounce hasn't had its ~30ms to
  //   confirm the release yet) — millis() - pressStartMs has ballooned
  //   past the threshold purely from the elapsed block time, not a
  //   real hold, and fires a spurious long-press on release. Live
  //   `reading` alone rules this out (already HIGH by then).
  // Requiring both together is immune to either failure mode, and a
  // genuine 800ms+ hold satisfies both long before either matters.
  if (!b.longPressFired && b.lastStable == LOW && reading == LOW &&
      millis() - b.pressStartMs >= kLongPressThresholdMs) {
    b.longPressPending = true;
    b.longPressFired = true;
    Serial.printf("[Controls] %s long-press fired after %lums held\n", b.name, millis() - b.pressStartMs);
  }
}

// Returns the accumulated count (0 if none pending) and resets it —
// callers that only care whether it fired at all can treat the
// returned int as a bool (implicit nonzero -> true), same as before.
int consume(Button &b) {
  int n = b.pending;
  b.pending = 0;
  return n;
}

bool consumeLongPress(Button &b) {
  if (!b.longPressPending) return false;
  b.longPressPending = false;
  return true;
}

bool consumeShortPress(Button &b) {
  if (!b.shortPressPending) return false;
  b.shortPressPending = false;
  return true;
}

} // namespace

void initControls() {
  for (Button *b : allButtons) {
    pinMode(b->pin, INPUT_PULLUP);
    b->lastStable = digitalRead(b->pin);
    b->lastReading = b->lastStable;
  }

  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  encoderPrevAB = (digitalRead(ENCODER_B) << 1) | digitalRead(ENCODER_A); // swapped, see onEncoderChange()
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_B), onEncoderChange, CHANGE);
}

namespace {
void pollAll() {
  for (Button *b : allButtons) pollButton(*b);

  // Fold any newly-completed encoder detent(s) into the same
  // upBtn/downBtn.pending counters the rotate switch already drives —
  // rotateUpSteps()/rotateDownSteps() below don't need to know which
  // physical input actually produced the step(s).
  noInterrupts();
  int32_t rawSteps = encoderRawSteps;
  unsigned long detentMs = encoderLastDetentMs;
  unsigned long prevDetentMs = encoderPrevDetentMs;
  interrupts();
  int32_t diff = rawSteps - encoderConsumedSteps;
  // Strict, full-cycle count by default: only a complete 4-quarter-step
  // cycle registers as a detent. This is what prevents double-firing —
  // pollAll() runs far more often than a human can turn the knob, so
  // it regularly catches raw mid-turn (not-yet-resting) values; a
  // looser threshold here previously fired early on those transient
  // states, sometimes twice per physical click.
  int32_t detents = diff / kQuarterStepsPerDetent;
  if (detents == 0) {
    // Exception, only right after a direction reversal: on this
    // hardware the first click after reversing often produces just
    // ~half the usual raw count (contact bounce right at the reversal
    // point eats a transition), so strict counting alone would swallow
    // that first reversed click entirely. Resync straight to the
    // observed raw value (not by the usual fixed +/-4) so no phantom
    // debt carries into the next click.
    int32_t sign = (diff > 0) - (diff < 0);
    if (sign != 0 && sign != encoderLastDetentSign && (diff >= kQuarterStepsPerDetent / 2 ||
                                                         diff <= -kQuarterStepsPerDetent / 2)) {
      detents = sign;
      encoderConsumedSteps = rawSteps;
    }
  } else {
    encoderConsumedSteps += detents * kQuarterStepsPerDetent;
  }
  if (detents != 0) {
    encoderLastDetentSign = detents > 0 ? 1 : -1;
    // Gap between the two most recent detents, both timestamped inside
    // the ISR — accurate regardless of how late pollAll() itself gets
    // called (e.g. after loop() was blocked by a display refresh).
    bool fast = prevDetentMs != 0 && (detentMs - prevDetentMs) < kEncoderFastDetentGapMs;
    Serial.printf("[Encoder] rawSteps=%ld detents=%ld gap=%lums -> %s (%s)\n", (long)rawSteps, (long)detents,
                  detentMs - prevDetentMs, detents > 0 ? "UP" : "DOWN", fast ? "fast" : "slow");
    // Accumulates (+=), not just set — a fast flick producing >1
    // detent between two polls (loop() runs ~every 10ms, or far less
    // often right after a blocking e-paper partial refresh) piles up
    // here instead of dropping every detent past the first; the next
    // rotateUpSteps()/rotateDownSteps() call returns however many
    // landed since it last consumed.
    if (detents > 0) {
      upBtn.pending += detents;
      upBtn.lastStepFast = fast;
    } else {
      downBtn.pending += -detents;
      downBtn.lastStepFast = fast;
    }
  }
}
} // namespace

namespace {
bool lastConsumedStepFast = false;
} // namespace

int rotateUpSteps() {
  pollAll();
  int n = consume(upBtn);
  if (n > 0) lastConsumedStepFast = upBtn.lastStepFast;
  return n;
}

int rotateDownSteps() {
  pollAll();
  int n = consume(downBtn);
  if (n > 0) lastConsumedStepFast = downBtn.lastStepFast;
  return n;
}

// Call right after rotateUpSteps()/rotateDownSteps() returned nonzero,
// to find out whether the most recently folded-in step was a "fast"
// one (sustained rotate-switch hold, or a quickly-turned encoder
// detent). Currently unused — see the declaration in Controls.h.
bool rotateStepWasFast() { return lastConsumedStepFast; }

bool rotateConfPressed() {
  pollAll();
  // Both consume() calls must run unconditionally (not short-circuited
  // by ||) or a pending press on whichever button is second in the
  // expression would sit stale for a whole extra poll cycle.
  bool fromRotateSwitch = consume(confBtn);
  bool fromEncoder = consume(encoderSwBtn);
  return fromRotateSwitch || fromEncoder;
}

bool menuButtonPressed() {
  pollAll();
  // Both consume() calls must run unconditionally (not short-circuited
  // by ||) — same reasoning as rotateConfPressed() above.
  bool fromBuiltIn = consume(menuBtn);
  bool fromExternal = consume(alarmToggleBtn);
  return fromBuiltIn || fromExternal;
}

bool exitButtonPressed() {
  pollAll();
  bool fromBuiltIn = consume(exitBtn);
  bool fromExternal = consume(snoozeBtn);
  return fromBuiltIn || fromExternal;
}

bool exitButtonLongPressed() {
  pollAll();
  bool fromBuiltIn = consumeLongPress(exitBtn);
  bool fromExternal = consumeLongPress(snoozeBtn);
  return fromBuiltIn || fromExternal;
}

bool menuButtonLongPressed() {
  pollAll();
  // Same merging as menuButtonPressed(): the built-in MENU button and
  // the external "Alarm on/off" button are indistinguishable here.
  bool fromBuiltIn = consumeLongPress(menuBtn);
  bool fromExternal = consumeLongPress(alarmToggleBtn);
  return fromBuiltIn || fromExternal;
}

bool exitButtonShortReleased() {
  pollAll();
  bool fromBuiltIn = consumeShortPress(exitBtn);
  bool fromExternal = consumeShortPress(snoozeBtn);
  return fromBuiltIn || fromExternal;
}
