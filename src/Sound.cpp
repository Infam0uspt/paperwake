#include "Sound.h"

#include <AudioFileSourceBuffer.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>
#include <SD.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "FallbackTone.h"
#include "PinConfig.h"
#include "SdCard.h"
#include "SettingsMenu.h"

namespace {

// Only used to prime the I2S peripheral at boot (see initSound()) —
// actual playback (file or fallback) runs at whatever rate the source
// declares; AudioOutputI2S reconfigures the I2S hardware per session
// via ESP8266Audio's own I2S driver handling, which (unlike the
// Arduino I2S wrapper class used before switching to ESP8266Audio)
// handles rates like 44.1/48kHz natively.
constexpr int kSampleRate = 16000;

// Volume-percent -> linear-gain curve exponent (gain = volumeFraction
// ^ kVolumeCurveExponent). Was a plain cube (3) — reported live as
// "everything under 25% volume is inaudible", i.e. the cube made the
// bottom third of the dial useless. Re-solved so the new 10% setting
// reproduces the exact same gain the old cube gave at 25% (still
// steep enough to sound smoothly graduated — human loudness perception
// is roughly logarithmic, the original reason a straight linear scale
// wasn't used — just not so steep that a third of the range is
// silence): gain(10%) = gain_old(25%) = 0.25^3 = 1/64 ->
// 0.1^n = 1/64 -> n = ln(1/64) / ln(0.1) ≈ 1.806. 0% and 100% are
// unaffected (any exponent keeps 0->0 and 1->1); only what happens
// between them changes.
constexpr float kVolumeCurveExponent = 1.806f;

// Smallest volume percent that's actually audible through this amp —
// matches SettingsMenu.cpp's Volume item minimum (also 10, keep the
// two in sync if either ever changes). Used as the fade-in ramp's
// starting point below, instead of 0%: a fade-in that ramps *gain*
// linearly from 0 spends most of its early duration below the audible
// floor (reported live: "de ramp-functie... begint waarschijnlijk
// vanaf 0%, maar het geluid is dan niet hoorbaar") — silent, then an
// abrupt jump once it crosses into audible range, not a smooth fade at
// all. Ramping *volume percent* from this floor up to the target
// setting instead means the very start of a fade-in is already
// audible, and — when the target setting *is* this floor (the user's
// volume is already at the minimum) — the "ramp" is flat from the
// first sample, playing at that volume immediately rather than fading
// into it, per explicit request.
constexpr float kFadeInMinVolumePercent = 10.0f;

// RAM buffer AudioFileSourceBuffer reads ahead into — this is what
// actually fixed the intermittent stutter/repeat that persisted
// through several earlier hand-rolled fixes (bigger I2S DMA buffer,
// sample-accounting bugs, etc.): occasional SD read latency spikes
// (e.g. crossing a FAT cluster boundary) no longer stall the audio
// feed directly, since there's several hundred ms of pre-read audio
// to draw from while a slow read catches up in the background.
constexpr uint32_t kFileBufferBytes = 8192;

bool soundInitialized = false;

// dma_buf_count bumped from the library default (8, ~23-64ms of
// buffering depending on sample rate) to 32 — gives the I2S DMA more
// headroom to ride out loop() being blocked by an e-paper partial
// refresh (measured live: up to ~540ms) without audibly dropping out.
// Doesn't fully cover the longest refreshes, but meaningfully reduces
// how often/how audible the resulting stutter is. See
// project_paperwake_backlog.md for the underlying architectural issue
// (any blocking display call starves audio feeding) this only mitigates.
AudioOutputI2S audioOut(0, AudioOutputI2S::EXTERNAL_I2S, 32);

// Sample rate all 4 built-in sound loops were recorded at (see
// tools/gen_builtin_sound_loops.py's TARGET_SR) — matches
// BuiltinSounds.h's raw PCM data, handed to AudioGeneratorPCMLoop
// unchanged.
constexpr int kBuiltinSoundSampleRate = 16000;

// Plays a mono 16-bit PCM stream (raw frames, no WAV header) on an
// endless loop, wrapping straight from the last sample back to the
// first with no gap and no restart — unlike AudioGeneratorMP3/WAV,
// loop() never returns false (there is no "end of clip" from the
// caller's perspective), so audioTaskFn()'s usual EOF-restart logic
// never fires for this generator. This is what actually keeps the
// built-in ambient loops (Birds/Brook/Ocean/Rain) gapless: their
// *content* was already engineered to loop seamlessly
// (tools/gen_builtin_sound_loops.py's crossfade splice), and this
// class preserves the seam by wrapping itself instead of tearing the
// source down.
//
// Phase 3.5 (BT flash budget): the clips live on the SD card
// (/sounds/_builtin_<name>.pcm, header-stripped PCM — the same bytes
// the old embedded arrays held), so this class reads from an
// AudioFileSourceSD rather than a flash array. A small read-ahead
// buffer bounces whole SD reads instead of 2-byte per-sample calls
// (per-call SD/FAT overhead at 16kHz would starve the feed), and the
// wraparound is an explicit seek(0) — same gapless guarantee, no flash
// cost.
class AudioGeneratorPCMLoop : public AudioGenerator {
 public:
  bool begin(AudioFileSource *s, int sampleRate, AudioOutput *out) {
    if (!s || !s->isOpen() || !out) return false;
    source = s;
    sampleCount = source->getSize() / 2; // 16-bit mono: 2 bytes/sample
    pos = 0;
    bufPos = bufLen = 0;
    output = out;
    output->SetRate(sampleRate);
    output->SetBitsPerSample(16);
    output->SetChannels(1);
    if (!output->begin()) return false;
    running = true;
    return true;
  }
  bool loop() override {
    if (!running) return false;
    int16_t sample[2];
    while (running) {
      if (!readSample(&sample[AudioOutput::LEFTCHANNEL])) break;
      sample[AudioOutput::RIGHTCHANNEL] = 0;
      if (!output->ConsumeSample(sample)) break; // I2S momentarily full — retry next call, not an error
    }
    return true;
  }
  bool stop() override {
    if (!running) return true;
    running = false;
    output->stop();
    return true;
  }
  bool isRunning() override { return running; }

 private:
  AudioFileSource *source = nullptr;
  size_t sampleCount = 0;
  size_t pos = 0;
  static constexpr size_t kBufInt16 = 256;
  int16_t buf[kBufInt16];
  size_t bufPos = 0, bufLen = 0;

  void rewind() {
    source->seek(0, SEEK_SET);
    pos = 0;
    bufPos = bufLen = 0;
  }

  bool readSample(int16_t *out) {
    if (pos >= sampleCount) rewind();
    if (bufPos >= bufLen) {
      if (pos >= sampleCount) rewind();
      uint32_t need = (uint32_t)((sampleCount - pos) * 2);
      if (need > sizeof(buf)) need = sizeof(buf);
      bufLen = source->read(buf, need) / 2;
      bufPos = 0;
      // Defensive EOF (short/inconsistent file): restart cleanly rather
      // than emit a trailing partial sample silently.
      if (bufLen == 0) {
        rewind();
        return false;
      }
    }
    uint32_t avail = bufLen - bufPos;
    if (avail == 0) return false;
    *out = buf[bufPos++];
    pos++;
    return true;
  }
};

// AudioFileSource base pointer — holds either an AudioFileSourceSD
// (uploaded /sounds/<file>) or an AudioFileSourcePROGMEM (the embedded
// kFallbackToneMp3 clip below), depending on which session is active.
// Every variable below this point is touched exclusively from
// audioTaskFn() (see below) — updateAlarmSound(), still called from
// main.cpp's loop() on every iteration, only ever writes the one
// atomic flag further down, so none of this needs a lock.
AudioFileSource *fileSource = nullptr;
AudioFileSourceBuffer *bufferedSource = nullptr;
AudioGeneratorWAV *wavGenerator = nullptr;
AudioGeneratorMP3 *mp3Generator = nullptr;
AudioGeneratorPCMLoop *pcmLoopGenerator = nullptr; // built-in sounds only, see startBuiltinSoundPlayback()
// The SD source feeding pcmLoopGenerator (Phase 3.5: built-in ambient
// clips live on the SD card, not in flash). Owned separately from the
// generator so teardown can stop the generator first, then close the
// file.
AudioFileSourceSD *pcmLoopSdSource = nullptr;
AudioGenerator *activeGenerator = nullptr; // points at whichever of the above is in use, or null when not playing
bool activeIsMp3 = false;
String activeFilePath; // "/sounds/<file>" currently playing, or "" (embedded clip — Tone or a built-in sound)
// Which built-in embedded sound (Birds/Brook/Ocean/Rain) is currently
// playing, or "" if none — distinguishes that case from the true Tone
// fallback, which also leaves activeFilePath empty. Purely informational
// (activeSourceLabel() below) now that AudioGeneratorPCMLoop makes the
// EOF-restart path below unreachable for built-in sounds.
String activeBuiltinSound;

// Written by updateAlarmSound() on the main/loop() task, read by
// audioTaskFn() on its own dedicated task/core — the only state shared
// between the two. A single bool is already atomic at the hardware
// level on ESP32; std::atomic here is just to stop the compiler from
// caching/reordering the read across loop iterations, not because a
// torn read/write is otherwise possible.
std::atomic<bool> desiredPlaying{false};
// Set false the moment a new session starts, true only once real
// decoded audio has actually been pushed into the I2S DMA queue (see
// audioTaskFn's eager first-fill call below) — lets updateAlarmSound()
// callers wait for genuinely audible playback, not just "the session
// object exists," before doing something CPU-heavy that could starve
// the audio task's critical first fill (see isAudioAudible()).
std::atomic<bool> audioAudible{false};
// Starts true so the very first idle cycle after boot preloads
// whatever's currently selected, with no explicit trigger needed.
// Set true again by invalidatePreloadedSound() whenever the selection
// (or the set of files on SD) might have changed. Consumed (exchanged
// back to false) by audioTaskFn's idle-state preload logic below.
std::atomic<bool> preloadNeeded{true};
TaskHandle_t audioTaskHandle = nullptr;

// Current output gain for a session that's been playing `elapsedSec`
// seconds — shared by both places gain gets set (the per-iteration
// path in audioTaskFn, and the fallback direct-start path's
// pre-primeAudioBuffer() gain-preset, which needs to compute the exact
// same value for elapsedSec≈0 as the per-iteration path would on its
// first real tick). See kVolumeCurveExponent/kFadeInMinVolumePercent's
// comments above for the reasoning behind the curve and the fade-in
// floor.
float computeGain(float elapsedSec) {
  int targetVolumePercent = getVolumePercent();
  float effectiveVolumePercent = (float)targetVolumePercent;
  int fadeInSeconds = getFadeInSeconds();
  if (fadeInSeconds > 0) {
    float fadeFraction = elapsedSec / (float)fadeInSeconds;
    if (fadeFraction < 0.0f) fadeFraction = 0.0f;
    if (fadeFraction < 1.0f) {
      effectiveVolumePercent =
          kFadeInMinVolumePercent + (targetVolumePercent - kFadeInMinVolumePercent) * fadeFraction;
    }
  }
  return powf(effectiveVolumePercent / 100.0f, kVolumeCurveExponent);
}

void teardownFilePlayback() {
  if (activeGenerator) activeGenerator->stop(); // also stops/tears down audioOut's I2S channel
  delete wavGenerator;
  wavGenerator = nullptr;
  delete mp3Generator;
  mp3Generator = nullptr;
  delete pcmLoopGenerator; // stops+tears down audioOut's I2S channel
  pcmLoopGenerator = nullptr;
  delete pcmLoopSdSource; // close the built-in clip's SD file (owned separately from the generator)
  pcmLoopSdSource = nullptr;
  delete bufferedSource;
  bufferedSource = nullptr;
  delete fileSource;
  fileSource = nullptr;
  activeGenerator = nullptr;
}

// For log lines only — describes whichever of the three sources
// (SD file / built-in embedded loop / true Tone fallback) is active.
const char *activeSourceLabel() {
  if (activeFilePath.length()) return activeFilePath.c_str();
  if (activeBuiltinSound.length()) return activeBuiltinSound.c_str();
  return "embedded-tone";
}

// (Re)starts playback of `path` from the beginning. Used both for the
// initial start of a ringing session and to loop back to the start
// once a file finishes — an uploaded alarm clip is typically much
// shorter than a full ringing session, so this needs to happen
// repeatedly. Recreating the source/buffer/generator fresh each time
// is simpler and more robust than trying to rewind an in-flight
// decoder, and only happens once per clip duration (at minimum a few
// seconds), so the overhead is negligible.
bool startFilePlayback(const char *path, bool isMp3) {
  unsigned long t0 = millis();
  fileSource = new AudioFileSourceSD(path);
  if (!fileSource->isOpen()) {
    delete fileSource;
    fileSource = nullptr;
    return false;
  }
  unsigned long t1 = millis();
  bufferedSource = new AudioFileSourceBuffer(fileSource, kFileBufferBytes);
  unsigned long t2 = millis();
  if (isMp3) {
    mp3Generator = new AudioGeneratorMP3();
    activeGenerator = mp3Generator;
  } else {
    wavGenerator = new AudioGeneratorWAV();
    activeGenerator = wavGenerator;
  }
  activeIsMp3 = isMp3;
  unsigned long t3 = millis();
  bool ok = activeGenerator->begin(bufferedSource, &audioOut);
  unsigned long t4 = millis();
  Serial.printf("[Sound] startFilePlayback timing: open=%lums buffer=%lums newGen=%lums begin=%lums (ok=%d)\n",
                t1 - t0, t2 - t1, t3 - t2, t4 - t3, ok);
  if (!ok) teardownFilePlayback();
  return ok;
}

// (Re)starts playback of an embedded (flash-resident, PROGMEM) MP3
// clip — used for both the fallback tone and the built-in ambient
// loops below. `label` is only for the timing log line.
bool startEmbeddedPlayback(const uint8_t *data, uint32_t len, const char *label) {
  unsigned long t0 = millis();
  fileSource = new AudioFileSourcePROGMEM(data, len);
  unsigned long t1 = millis();
  bufferedSource = new AudioFileSourceBuffer(fileSource, kFileBufferBytes);
  unsigned long t2 = millis();
  mp3Generator = new AudioGeneratorMP3();
  activeGenerator = mp3Generator;
  activeIsMp3 = true;
  unsigned long t3 = millis();
  bool ok = activeGenerator->begin(bufferedSource, &audioOut);
  unsigned long t4 = millis();
  Serial.printf("[Sound] startEmbeddedPlayback(%s) timing: source=%lums buffer=%lums newGen=%lums begin=%lums (ok=%d)\n",
                label, t1 - t0, t2 - t1, t3 - t2, t4 - t3, ok);
  if (!ok) teardownFilePlayback();
  return ok;
}

// Plays the embedded fallback clip (kFallbackToneMp3, see
// include/FallbackTone.h — generated from assets/airbus.mp3 by
// tools/gen_fallback_tone.py). Used whenever the "Sound" setting is
// "Tone", or the selected SD file is missing/fails to open — replaces
// the earlier hand-generated square/sine-wave tone, which crackled
// audibly through the amp even after several rounds of tuning; a short
// real audio clip has no such artifact and, being embedded in flash
// rather than read from SD, keeps the same "always available, no SD
// card required" guarantee the generated tone used to provide.
bool startFallbackTonePlayback() { return startEmbeddedPlayback(kFallbackToneMp3, kFallbackToneMp3Len, "Tone"); }

bool isBuiltinSoundName(const String &name) {
  return name == "Birds" || name == "Brook" || name == "Ocean" || name == "Rain";
}

// The 4 built-in ambient loops (Birds/Brook/Ocean/Rain) — since Phase
// 3.5 they live as raw PCM files on the SD card at
// /sounds/_builtin_<name>.pcm (the same header-stripped 16k mono PCM
// the old embedded arrays held; see tools/install_builtin_sounds_to_sd.py
// and the AudioGeneratorPCMLoop comment), played back gapless via
// AudioGeneratorPCMLoop. Checked *before* the SD-file lookup in
// loadSelectedSound() below, by exact name match against
// getSelectedSoundFile() — "Birds" etc. are synthetic entries in the
// Sound picker, not real SD filenames (see SettingsMenu.cpp's
// refreshSoundChoiceList()). If the SD file is missing, returns false
// and the caller falls back to the always-available embedded Tone —
// freeing the ~8.8MB those clips used to occupy in flash (BT budget).
bool startBuiltinSoundPlayback(const String &name) {
  if (!isBuiltinSoundName(name)) return false;
  String path = "/sounds/_builtin_" + name + ".pcm";
  pcmLoopSdSource = new AudioFileSourceSD(path.c_str());
  if (!pcmLoopSdSource->isOpen()) {
    // Missing SD file — caller's Tone fallback handles it. Delete the
    // half-open source we just created.
    delete pcmLoopSdSource;
    pcmLoopSdSource = nullptr;
    return false;
  }
  pcmLoopGenerator = new AudioGeneratorPCMLoop();
  activeGenerator = pcmLoopGenerator;
  bool ok = pcmLoopGenerator->begin(pcmLoopSdSource, kBuiltinSoundSampleRate, &audioOut);
  Serial.printf("[Sound] startBuiltinSoundPlayback(%s) ok=%d\n", name.c_str(), ok);
  if (!ok) {
    delete pcmLoopGenerator;
    pcmLoopGenerator = nullptr;
    delete pcmLoopSdSource;
    pcmLoopSdSource = nullptr;
  }
  return ok;
}

// Repeatedly pumps activeGenerator->loop() right after (re)starting a
// session, instead of relying on this task's normal per-iteration
// cadence to gradually warm the pipeline up. A *single* loop() call
// isn't enough to get genuinely continuous, gap-free output: on the
// very first call, AudioFileSourceBuffer's own RAM prebuffer is still
// close to empty (it only fills lazily, as it's consumed, a bit more
// on each subsequent call), so the decoder has barely any compressed
// data to work with yet and stops far short of filling the I2S DMA
// queue — confirmed live: a single-call version of this still left an
// audible delay before real sound started, both when entering ringing
// and on the end-of-clip restart further down. Looping here for a
// short bounded stretch instead lets the SD-read/decode/push pipeline
// actually warm up before returning control, so whatever CPU-heavy
// work happens right after (e.g. main.cpp's ~2s wake-screen refresh,
// running at equal FreeRTOS priority — see initSound()) has much less
// chance of catching this session still nearly empty. Bounded at
// kPrimeTimeoutMs as a safety net (e.g. a corrupt file that never
// produces samples), not because that long is normally needed.
void primeAudioBuffer() {
  if (!activeGenerator) return;
  constexpr unsigned long kPrimeTimeoutMs = 150;
  unsigned long primeStartMs = millis();
  while (millis() - primeStartMs < kPrimeTimeoutMs) {
    // Per-iteration timing (not just the overall kPrimeTimeoutMs
    // bound, which can't interrupt a single call already in progress):
    // this caught a real, reproducible multi-second single-call stall
    // live (~8.6s, twice, both times on the *second* loop() call after
    // a fresh SD file open) — a genuine slow SD read, not a scheduling
    // artifact. Tracked as its own backlog item (see
    // project_paperwake_backlog.md); this log line is what would catch
    // it recurring, so it stays rather than being temp-only.
    unsigned long iterStartMs = millis();
    bool stillRunning = activeGenerator->loop();
    unsigned long iterMs = millis() - iterStartMs;
    if (iterMs > 50) {
      Serial.printf("[Sound] primeAudioBuffer: slow loop() call took %lums\n", iterMs);
    }
    if (!stillRunning) break; // EOF/failure — nothing more to prime
  }
}

// Opens and primes whichever sound is currently selected in Settings —
// same file-resolution logic updateAlarmSound() always used inline
// before this was extracted, now shared between the background
// preload and the direct-start fallback path in audioTaskFn (see both
// call sites below).
//
// Locked with sdLock()/sdUnlock() (SdCard.h) for its entire duration,
// including primeAudioBuffer()'s repeated loop() calls (which
// themselves read more SD data as needed) — this function is the one
// place a *new* SD file gets opened for playback, and unlike the
// ongoing per-tick reads during an already-active ringing session
// (safe without a lock: Mode::RINGING is mutually exclusive with
// Settings/Upload's own SD use by main.cpp's mode machine), this one
// runs during the background preload too, which happens whenever
// nothing is ringing — i.e. exactly when the user could be in
// Settings or Upload mode touching the SD card from the main task.
// Worst case this holds the lock as long as primeAudioBuffer() takes
// to return, including its own rare multi-second-stall failure mode
// (see project_paperwake_backlog.md) — accepted as a correctness vs.
// liveness trade-off: a rare, brief Settings/Upload-screen stall is
// far preferable to two tasks corrupting the SD filesystem by racing
// on it.
void loadSelectedSound() {
  sdLock();
  activeFilePath = "";
  activeBuiltinSound = "";
  // getSelectedSoundFile() is just a bare filename under /sounds/ (see
  // SettingsMenu.h) — "" means the user picked "Tone", i.e. the
  // embedded fallback clip below. A handful of exact names (Birds/
  // Brook/Ocean/Rain) are synthetic entries meaning one of the
  // built-in embedded ambient loops instead — checked first, before
  // ever touching the SD card, same as "Tone".
  String selected = getSelectedSoundFile();
  if (isBuiltinSoundName(selected)) {
    if (startBuiltinSoundPlayback(selected)) {
      activeBuiltinSound = selected;
    } else {
      Serial.println("[Sound] Failed to start built-in sound — falling back to embedded tone clip");
      startFallbackTonePlayback();
    }
    primeAudioBuffer();
    sdUnlock();
    return;
  }
  bool isMp3 = false;
  if (selected.length()) {
    String candidatePath = "/sounds/" + selected;
    if (SD.exists(candidatePath)) {
      activeFilePath = candidatePath;
      isMp3 = selected.endsWith(".mp3") || selected.endsWith(".MP3");
    } else {
      Serial.printf("[Sound] Selected sound '%s' no longer on SD — using embedded tone clip instead\n",
                    selected.c_str());
    }
  }

  if (activeFilePath.length() && !startFilePlayback(activeFilePath.c_str(), isMp3)) {
    Serial.println("[Sound] Failed to open selected file — falling back to embedded tone clip");
    activeFilePath = "";
  }
  if (!activeFilePath.length()) startFallbackTonePlayback();
  primeAudioBuffer();
  sdUnlock();
}

// Runs forever on its own FreeRTOS task, pinned to the ESP32-S3's
// other core from the Arduino main loop() task (see initSound()).
// Exists because e-paper partial/full refreshes (GxEPD2) are fully
// blocking — up to ~540ms measured live for a single partial window —
// and this feed loop used to just be another step inside loop()
// itself, so a display refresh starved it for that whole window,
// audible as a stutter on the current alarm sound. Running it
// independently means a blocked loop() (e.g. redrawing the ringing
// screen's minute digits once a minute) no longer has any effect on
// how often audio actually gets pushed to the I2S DMA buffer.
//
// Every module-local variable above (fileSource, the generators,
// activeFilePath, ...) plus wasPlaying/envelopeStartMs below are now
// touched exclusively from this one task — updateAlarmSound(), the
// only entry point still called from loop(), does nothing but flip
// the `desiredPlaying` atomic, so there's no shared mutable audio
// state left to guard with a lock.
//
// getSelectedSoundFile()/getVolumePercent()/getFadeInSeconds() below
// are still owned by SettingsMenu.cpp and read cross-task here — safe
// because: the two int getters are simple word-sized reads (no torn-
// read risk even if they changed mid-read, which they can't during
// ringing anyway); getSelectedSoundFile() returns a String, whose
// underlying value only ever changes while Mode::SETTINGS is being
// edited on the main task (SettingsMenu.cpp's settingsCommitEdit()) —
// and Mode::SETTINGS/Mode::RINGING are mutually exclusive in
// main.cpp's mode state machine, so that value can never actually
// change while a ringing session is starting here. The one place this
// task touches the SD card (AudioFileSourceSD, SD.exists() below) has
// the same guarantee for the same reason: nothing else in this
// codebase performs SD I/O while Mode::RINGING is active (SoundUpload/
// WifiSetup's own SD-touching work only runs in their own distinct
// modes, and — per the existing "alarm can be silently missed during
// an active upload" backlog item — an in-progress upload transfer
// actively blocks checkAlarmTrigger() from ever entering
// Mode::RINGING in the first place, so the two can't overlap even at
// the boundary).
// Priority while actively ringing — needs to preempt a "blocked"
// loopTask (spinning in a display SPI refresh) on demand, see the long
// comment at this task's xTaskCreatePinnedToCore() call in initSound()
// for the full history of why.
constexpr UBaseType_t kAudioTaskRingingPriority = 2;
// Priority the rest of the time (idle, background-preloading) — the
// SAME as loopTask's (1), NOT FreeRTOS's idle-task level (0). 0 was
// tried first and made sense on paper (a task at that level only runs
// when literally nothing else on the core wants the CPU, so it could
// never delay loopTask's own work by even a tick) — but broke actual
// SD reads: confirmed live, an alarm went completely silent, forever,
// after this was introduced. Root cause (not measured with a scope,
// but consistent with the failure and with how SPI-based SD access
// behaves generally): SD/SPI transactions have their own timing
// expectations at the protocol level, and this task being preemptable
// by literally anything for unbounded stretches (priority 0 has no
// floor at all below it except the true idle task) let a transaction
// get interrupted badly enough to stall permanently instead of just
// running slowly — a materially different, worse failure mode than
// merely being slow. Priority 1 doesn't fully prevent loopTask
// contention during preload (some of the delay this was meant to
// fully eliminate can still occur), but it's the lowest priority this
// task can safely run SD I/O at without risking a hang — verified
// safe earlier in this same investigation (equal priority completed
// every preload attempt, including the ~8.6s worst case, never hung).
//
// Switching dynamically between the two priorities (via
// vTaskPrioritySet(NULL, ...) at each playing/idle transition below)
// replaced an earlier, more fragile attempt at fixing the loopTask-
// contention problem with a fixed delay before the preload could
// start (a "cooldown" after a ringing session ended) — that only
// masked one specific coincidence (dismiss/snooze's own screen update
// racing the reload it triggers) and a *different* stall reappeared
// once the delayed preload actually ran, since it was still priority
// 2 the whole time it ran. Dropping priority during preload (to 1,
// per above) addresses the general case, not just that one coincidence.
constexpr UBaseType_t kAudioTaskIdlePriority = 1;

void audioTaskFn(void *) {
  bool wasPlaying = false;
  // A session is open, primed, and paused (gain 0, no further loop()
  // calls) — see the idle-state preload block below and the "fast
  // path" it enables in the !wasPlaying block further down.
  bool preloaded = false;
  unsigned long envelopeStartMs = 0;

  for (;;) {
    bool playing = desiredPlaying.load(std::memory_order_relaxed);

    if (!playing) {
      if (wasPlaying) {
        // A ringing session just ended (dismiss/snooze/auto-off).
        // Drop back to idle priority *before* doing anything else —
        // in particular before teardownFilePlayback() below, whose
        // stop()/delete calls are themselves not free — so none of
        // this cleanup work can delay whatever screen update
        // dismissAlarm()/the snooze branch in main.cpp is about to do
        // on loopTask right after calling updateAlarmSound(false).
        vTaskPrioritySet(NULL, kAudioTaskIdlePriority);
        teardownFilePlayback();
        wasPlaying = false;
        preloaded = false;
        preloadNeeded.store(true, std::memory_order_relaxed);
      }

      // Background preload: keeps a session open+primed+silent so the
      // next time `playing` flips true, starting is instant — no
      // SD.exists()/open()/decode-warm-up on the critical path. Runs
      // at kAudioTaskIdlePriority (see above), so this — including its
      // rare multi-second slow-SD-read worst case (see
      // project_paperwake_backlog.md) — can never delay the UI.
      if (preloadNeeded.exchange(false, std::memory_order_relaxed)) {
        if (preloaded) teardownFilePlayback(); // stale — selection changed since the last preload
        preloaded = false;
        audioOut.SetGain(0.0f); // silent while preloaded — real gain gets set on actual resume
        loadSelectedSound();
        preloaded = true;
        Serial.printf("[Sound] preloaded (source=%s)\n",
                      activeSourceLabel());
      }

      vTaskDelay(pdMS_TO_TICKS(20)); // nothing urgent to do — no need to poll tightly while idle
      continue;
    }

    if (!wasPlaying) {
      // Boost to ringing priority *before* anything else below — see
      // kAudioTaskRingingPriority's comment. Needed even on the fast
      // (preloaded) path: preloading itself always runs at idle
      // priority, so this task is still at idle priority right up
      // until this exact point, every time.
      vTaskPrioritySet(NULL, kAudioTaskRingingPriority);
      audioAudible.store(false, std::memory_order_relaxed);
      envelopeStartMs = millis();
      if (preloaded) {
        // Fast path: already open and primed from the background
        // preload above, just needs its real starting gain — set by
        // the per-iteration gain block right below, same iteration —
        // and to be marked audible. No SD access happens here, which
        // is the entire point.
        preloaded = false; // consumed
      } else {
        // Fallback: no preload was ready yet (e.g. the very first
        // alarm shortly after boot, before the background preload
        // above got a chance to run at all) — load synchronously here
        // instead, same as before this feature existed. Set the
        // correct starting gain *before* loadSelectedSound()'s eager
        // primeAudioBuffer() call, so that priming doesn't go out at
        // whatever gain was left over from a previous session.
        // elapsedSec≈0 here, matching what the per-iteration block
        // below would compute on its first real tick.
        audioOut.SetGain(computeGain(0.0f));
        loadSelectedSound();
      }
      audioAudible.store(true, std::memory_order_relaxed);
      Serial.printf("[Sound] playback session started at %lu (source=%s)\n", millis(),
                    activeSourceLabel());
    }
    wasPlaying = true;

    // Perceived loudness isn't linear with sample amplitude, and a
    // fade-in ramps from kFadeInMinVolumePercent (not 0%) up to the
    // set volume over getFadeInSeconds() — see computeGain()'s comment
    // and kVolumeCurveExponent/kFadeInMinVolumePercent above for the
    // full reasoning. Timed off the same envelopeStartMs reset at the
    // start of every playing session, including a snooze resume, so
    // each resumption fades in again too.
    float elapsedSec = (millis() - envelopeStartMs) / 1000.0f;
    audioOut.SetGain(computeGain(elapsedSec));

    if (activeGenerator) {
      // The generator's own loop() return value is the correct "still
      // producing audio" signal — NOT isRunning() checked afterwards.
      // AudioGeneratorMP3::loop() (ESP8266Audio 1.9.9) returns false
      // on reaching end-of-stream (Input()==MAD_FLOW_STOP) without
      // clearing its internal `running` flag first, a library bug
      // confirmed live: isRunning() stayed true forever past EOF, so
      // this restart never fired, and loop()'s unconditional first
      // line kept re-feeding the last-decoded sample to I2S every
      // tick — an endless repeated buzz/tick instead of either
      // restarting or falling silent. AudioGeneratorWAV doesn't have
      // this bug (it calls stop() itself at EOF, which does clear
      // `running`), but checking loop()'s return value directly is
      // correct for both either way.
      if (!activeGenerator->loop()) {
        // Reached the end of the clip — restart it so it keeps
        // sounding for the whole ringing session, not just once. Only
        // SD files and the true Tone fallback ever reach this branch —
        // built-in sounds (AudioGeneratorPCMLoop) never do, since their
        // loop() never returns false in the first place (see its
        // comment): the seamless repeat happens *inside* that
        // generator via ring-buffer wraparound, with no restart at
        // all, which is what actually made the built-in loops gapless
        // (their audio content was already seamless — see
        // tools/gen_builtin_sound_loops.py — but recreating the
        // decoder here every ~11s reintroduced an audible pause of its
        // own, reported live, before AudioGeneratorPCMLoop existed).
        String path = activeFilePath;
        bool isMp3 = activeIsMp3;
        bool wasFallback = !path.length();
        teardownFilePlayback();
        if (wasFallback) {
          startFallbackTonePlayback();
        } else if (!startFilePlayback(path.c_str(), isMp3)) {
          activeFilePath = ""; // give up on file playback for the rest of this session
          startFallbackTonePlayback();
        }
        // Same reasoning as the initial session-start above: without
        // this, the loop-back-to-start restart also had a small but
        // noticeable gap before sound resumed (reported live).
        primeAudioBuffer();
      }
    }

    // Short yield, not a real wait — audio must be fed far more often
    // than the 20ms idle poll above. 2ms leaves ample headroom below
    // a single WAV/MP3 chunk's playtime at any supported sample rate
    // while still yielding to lower-priority tasks every iteration.
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

} // namespace

void initSound() {
  // Pin numbers persist as member fields across stop()/begin() cycles
  // (verified from the library's own begin() implementation), so this
  // only needs to run once, same as the old Arduino-I2S-class version.
  bool pinsOk = audioOut.SetPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
  soundInitialized = pinsOk;
  Serial.printf("[Sound] SetPinout()=%d BCLK=%d LRC=%d DIN=%d\n", pinsOk, I2S_BCLK, I2S_LRC, I2S_DIN);

  // Prime the I2S peripheral once at boot with a real begin()/stop()
  // cycle. SetPinout() above only *stores* the pin numbers — BCLK/LRC/
  // DIN aren't actually routed to the I2S peripheral (via the GPIO
  // matrix) until begin() runs, and stay floating until then. Reported
  // symptom this fixes: an audible buzz through the amp specifically
  // while the LED strip (src/Light.cpp) was fading, but only before
  // the alarm had ever played a sound — consistent with floating
  // BCLK/LRC/DIN picking up crosstalk from the strip's nearby data
  // toggling, and consistent with i2s_driver_uninstall() (called by
  // stop()) tearing down the DMA/driver but not the GPIO matrix
  // routing itself, so once primed the pins stay actively driven
  // (not floating) even while idle between alarms. Unverified against
  // a datasheet/scope — a hypothesis from the reported behavior,
  // confirm live that the buzz is gone even on the very first ever
  // alarm after a fresh boot.
  if (soundInitialized) {
    audioOut.SetChannels(2);
    audioOut.SetRate(kSampleRate);
    audioOut.begin();
    audioOut.stop();

    // Pinned to core 1 — the SAME core Arduino's loopTask (running
    // main.cpp's loop(), and therefore every blocking e-paper refresh)
    // runs on. Originally pinned to core 0 instead, reasoning it'd be
    // more "free" than core 1 — wrong in practice: core 0 already
    // carries the ESP32-S3 WiFi stack's own background tasks, and
    // adding continuous decode work there was enough to starve that
    // core's idle task for 5+ straight seconds during real playback,
    // panicking the task watchdog and rebooting the board mid-alarm
    // (confirmed live via serial: repeated "task_wdt: ... IDLE0
    // (CPU 0) ... Tasks currently running: CPU 0: AudioFeed" followed
    // by a reboot). Core 1 doesn't have this problem: its idle task
    // isn't even watchdog-monitored in this SDK config (only
    // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is set, confirmed from
    // the installed sdkconfig.h).
    //
    // Priority is *dynamic*, not fixed — see kAudioTaskRingingPriority
    // and kAudioTaskIdlePriority (just above audioTaskFn) for the
    // current, final answer, and the values themselves for the full
    // history of how this was arrived at (three earlier fixed-priority
    // attempts, each confirmed live, each traded one starvation
    // problem for a different one — dynamic priority was what actually
    // resolved it: high only while genuinely needed — ringing —, idle
    // otherwise). Created here at the idle priority, since the task
    // always starts in the not-ringing state.
    //
    // 8192 bytes of stack: MP3 decoding via libmad is comparatively
    // stack-hungry, matching the stack size ESP8266Audio's own
    // examples use for a dedicated decode task.
    BaseType_t taskOk =
        xTaskCreatePinnedToCore(audioTaskFn, "AudioFeed", 8192, nullptr, kAudioTaskIdlePriority, &audioTaskHandle, 1);
    Serial.printf("[Sound] audio feed task created=%d\n", taskOk == pdPASS);
  }
}

void updateAlarmSound(bool playing) {
  if (!soundInitialized) return;
  desiredPlaying.store(playing, std::memory_order_relaxed);
}

bool isAudioAudible() { return audioAudible.load(std::memory_order_relaxed); }

void invalidatePreloadedSound() { preloadNeeded.store(true, std::memory_order_relaxed); }
