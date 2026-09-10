// Standalone hardware bring-up test for the SK1612 (RGBW, 4-channel)
// addressable LED strip. Separate from the main PaperWake firmware —
// build/flash it with its own PlatformIO environment:
//
//   pio run -e led_strip_test -t upload -t monitor
//
// Then type a command + Enter in the serial monitor (115200 baud):
//
//   r<0-255>   red channel        e.g. r255
//   g<0-255>   green channel      e.g. g128
//   b<0-255>   blue channel       e.g. b0
//   w<0-255>   white channel      e.g. w255
//   i<0-255>   overall brightness (luminance) — whole strip, not just
//              the current target (NeoPixelBusLg's SetLuminance() is a
//              global scalar), e.g. i128
//   red / green / blue / white / off   quick single-channel presets
//   sunrise                      20s simulated sunrise (RGB + white ramp)
//   wake / front / all           select which segment subsequent color
//                                commands (r/g/b/w, presets, sunrise)
//                                apply to — matches the real firmware's
//                                split (Light.cpp): wake = pixels
//                                0-10 (wake-up light), front = pixels
//                                11-16 (frontlight). Pixels outside the
//                                selected target keep whatever color
//                                they last had — this is what makes the
//                                two segments independently addressable
//                                for testing.
//   help                         reprint this list

#include <Arduino.h>
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include "PinConfig.h"

namespace {

// Matches the real strip described in PinConfig.h (17 pixels total).
// Change if testing a different length before it's cut to size.
constexpr uint16_t kPixelCount = 17;

// Segment split matches Light.cpp exactly — keep the two in sync if
// either ever changes.
constexpr uint16_t kWakeupSegmentStart = 0;
constexpr uint16_t kWakeupSegmentCount = 11;
constexpr uint16_t kFrontlightSegmentStart = 11;
constexpr uint16_t kFrontlightSegmentCount = 6;

// Two things are unverified until tested on real hardware:
// - Color order: NeoGrbwFeature (G,R,B,W) is the common wire order for
//   this WS2812-family style of RGBW chip — if a channel lights the
//   wrong color (e.g. "r255" shows green), switch to NeoRgbwFeature.
// - Protocol timing: no dedicated "Sk1612" method exists in NeoPixelBus
//   and no public datasheet was found for this specific chip, so this
//   defaults to NeoWs2812xMethod (the common 800kHz WS2812-family
//   timing most RGBW chips of this style use) — if the strip flickers
//   or shows garbled colors, try NeoSk6812Method instead (SK6812 has
//   its own distinct timing profile in NeoPixelBus).
using LedStrip = NeoPixelBusLg<NeoGrbwFeature, NeoWs2812xMethod>;

LedStrip strip(kPixelCount, LED_STRIP_DATA);

uint8_t red = 0, green = 0, blue = 0, white = 0;
uint8_t brightness = 255;

enum class Target { All, Wakeup, Frontlight };
Target target = Target::All;

const char *targetName(Target t) {
  switch (t) {
    case Target::Wakeup:
      return "wake";
    case Target::Frontlight:
      return "front";
    default:
      return "all";
  }
}

void targetRange(Target t, uint16_t &start, uint16_t &count) {
  switch (t) {
    case Target::Wakeup:
      start = kWakeupSegmentStart;
      count = kWakeupSegmentCount;
      break;
    case Target::Frontlight:
      start = kFrontlightSegmentStart;
      count = kFrontlightSegmentCount;
      break;
    default:
      start = 0;
      count = kPixelCount;
      break;
  }
}

void printState() {
  Serial.printf("target=%s  R=%3u G=%3u B=%3u W=%3u  brightness=%3u\n", targetName(target), red, green, blue, white,
                brightness);
}

void printHelp() {
  Serial.println(F("Commands: r<0-255> g<0-255> b<0-255> w<0-255> i<0-255>"));
  Serial.println(F("Presets: red green blue white off sunrise"));
  Serial.println(F("Target select: wake front all (applies to subsequent color commands)"));
  Serial.println(F("help - show this again"));
}

// Rough real-sunrise color progression (dark orange -> deepening orange
// -> golden amber -> warm daylight white), RGB+white only (this chip has
// no separate cool-white channel). Values chosen by eye, not measured
// against a real sunrise's spectrum — adjust freely.
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
constexpr unsigned long kSunriseDurationMs = 20000;
constexpr unsigned long kSunriseStepMs = 20; // ~1000 steps over 20s

void handleCommand(String cmd); // forward decl: runSunrise() can hand off to it on early-abort

// Non-blocking: returns true (with `outLine` filled) once a complete
// Enter-terminated line has been typed, false otherwise — buffers
// characters across calls in between. Replaces Serial.readStringUntil(
// '\n'), whose fixed ~1s per-call timeout would give up and return
// whatever had arrived *so far* if the user paused between keystrokes
// longer than that, sending a partial command (e.g. just "r2" of
// "r255") before Enter was actually pressed. This has no timeout at
// all — a command is only ever handled once its terminator arrives, no
// matter how slowly it was typed.
bool readLine(String &outLine) {
  static String buffer;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        outLine = buffer;
        buffer = "";
        return true;
      }
      // else: the lone \r or \n of a \r\n pair, or an empty Enter press — ignore
    } else {
      buffer += c;
    }
  }
  return false;
}

// Blocking (like the rest of this simple tester — no need for a
// non-blocking state machine in a bring-up tool). Typing a full line +
// Enter during the ramp aborts it and runs that line as a normal
// command instead (so e.g. "off" both stops the ramp and turns the
// strip off) — partial input without Enter yet doesn't abort it, same
// readLine() buffering as the main loop() below. Only touches the
// currently selected target's pixel range, same as apply() below — so
// e.g. "wake" then "sunrise" ramps just the wake-up segment while the
// frontlight segment keeps whatever it last had.
void runSunrise() {
  Serial.printf("Sunrise gestart op target=%s (20s, stuur eender welk commando om af te breken)...\n",
                targetName(target));
  uint16_t start, count;
  targetRange(target, start, count);
  unsigned long startMs = millis();
  unsigned long lastPrintMs = 0;
  strip.SetLuminance(brightness);

  while (true) {
    unsigned long elapsed = millis() - startMs;
    if (elapsed > kSunriseDurationMs) elapsed = kSunriseDurationMs;
    float fraction = (float)elapsed / (float)kSunriseDurationMs;

    float stagePos = fraction * (kSunriseStageCount - 1);
    int stageIndex = (int)stagePos;
    if (stageIndex >= kSunriseStageCount - 1) stageIndex = kSunriseStageCount - 2;
    float stageFraction = stagePos - stageIndex;

    const SunriseStage &a = kSunriseStages[stageIndex];
    const SunriseStage &b = kSunriseStages[stageIndex + 1];
    red = (uint8_t)(a.r + (b.r - a.r) * stageFraction);
    green = (uint8_t)(a.g + (b.g - a.g) * stageFraction);
    blue = (uint8_t)(a.b + (b.b - a.b) * stageFraction);
    white = (uint8_t)(a.w + (b.w - a.w) * stageFraction);

    RgbwColor color(red, green, blue, white);
    for (uint16_t i = 0; i < count; i++) strip.SetPixelColor(start + i, color);
    strip.Show();

    if (millis() - lastPrintMs >= 1000) {
      lastPrintMs = millis();
      printState();
    }

    if (elapsed >= kSunriseDurationMs) break;

    String nextCmd;
    if (readLine(nextCmd)) {
      Serial.println(F("Sunrise afgebroken."));
      handleCommand(nextCmd);
      return;
    }
    delay(kSunriseStepMs);
  }
  Serial.println(F("Sunrise klaar (volledig daglicht-wit bereikt)."));
}

// Applies the current red/green/blue/white to the currently selected
// target's pixel range only — pixels outside it keep whatever color
// they were last set to, which is what makes "wake" and "front"
// independently addressable without needing separate per-segment
// color state.
void apply() {
  uint16_t start, count;
  targetRange(target, start, count);
  RgbwColor color(red, green, blue, white);
  for (uint16_t i = 0; i < count; i++) strip.SetPixelColor(start + i, color);
  // Global to the whole strip, not just the selected target —
  // NeoPixelBusLg's SetLuminance() is a single scalar applied at
  // Show() time across the entire buffer, there's no per-segment
  // variant. Worth knowing if "i" seems to also dim the other segment.
  strip.SetLuminance(brightness);
  strip.Show();
  printState();
}

void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  red = r;
  green = g;
  blue = b;
  white = w;
  apply();
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  String lower = cmd;
  lower.toLowerCase();

  if (lower == "help" || lower == "?") {
    printHelp();
    return;
  }
  if (lower == "off") return setColor(0, 0, 0, 0);
  if (lower == "red") return setColor(255, 0, 0, 0);
  if (lower == "green") return setColor(0, 255, 0, 0);
  if (lower == "blue") return setColor(0, 0, 255, 0);
  if (lower == "white") return setColor(0, 0, 0, 255);
  if (lower == "sunrise") return runSunrise();
  if (lower == "wake") {
    target = Target::Wakeup;
    Serial.println(F("Target: wake-up light (pixels 0-10)"));
    printState();
    return;
  }
  if (lower == "front") {
    target = Target::Frontlight;
    Serial.println(F("Target: frontlight (pixels 11-16)"));
    printState();
    return;
  }
  if (lower == "all") {
    target = Target::All;
    Serial.println(F("Target: whole strip (pixels 0-16)"));
    printState();
    return;
  }

  char channel = lower[0];
  int value = constrain(lower.substring(1).toInt(), 0, 255);

  switch (channel) {
    case 'r':
      red = value;
      break;
    case 'g':
      green = value;
      break;
    case 'b':
      blue = value;
      break;
    case 'w':
      white = value;
      break;
    case 'i':
      brightness = value;
      break;
    default:
      Serial.printf("Onbekend commando: %s\n", cmd.c_str());
      return;
  }
  apply();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  // The MAX98357A amp's I2S pins (BCLK/LRC/DIN) are otherwise left
  // completely unconfigured by this LED-only test sketch — floating,
  // they pick up crosstalk from the strip's own nearby RMT-driven data
  // toggling, audible as a hum through the amp. Same root cause as a
  // bug already found and fixed in the main firmware (Sound.cpp primes
  // the real I2S peripheral at boot for this exact reason); this
  // sketch has no I2S/audio code at all to do that with, so just
  // holding the pins low as plain GPIO output is enough to stop them
  // floating and silence the hum.
  pinMode(I2S_BCLK, OUTPUT);
  digitalWrite(I2S_BCLK, LOW);
  pinMode(I2S_LRC, OUTPUT);
  digitalWrite(I2S_LRC, LOW);
  pinMode(I2S_DIN, OUTPUT);
  digitalWrite(I2S_DIN, LOW);

  strip.Begin();
  apply();
  printHelp();
}

void loop() {
  String line;
  if (readLine(line)) handleCommand(line);
}
