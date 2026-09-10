#!/usr/bin/env python3
"""
Turns a raw ambient recording into a seamlessly-loopable clip: crossfades
the clip's own tail into its own head over CROSSFADE_SEC (equal-power),
keeps only that blended seam plus the untouched middle, downmixes to
mono, and downsamples to TARGET_SR. Output is a plain 16-bit PCM .wav —
deliberately NOT re-encoded to MP3 (see tools/gen_builtin_sounds.py,
which turns the *output* of this script into a C header): MP3 is a
block-based codec (1152 samples/frame) that pads the stream with a few
hundred to ~1-2k samples of encoder/decoder delay, which shifts the
carefully-aligned crossfade seam this script produces and reintroduces
an audible click/hiccup at the loop point — measured live (~1,825
extra samples after an MP3 round-trip that should have been exactly
513,120). Raw PCM has no such padding, so the seam stays sample-exact.

When the result is repeated back-to-back, the wrap point falls entirely
inside the crossfaded seam, so there's no click/gap at the loop
boundary — the two internal splice points (seam->middle, middle->wrap)
are each between originally-adjacent samples, so they're inherently
continuous too.

IMPORTANT: run this against RAW (not yet looped) source recordings.
Running it again on its own output would crossfade an already-blended
seam into itself, corrupting the loop point. assets/{Birds,Brook,Ocean,
Rain}.mp3 currently hold an *already-processed* (crossfaced, but MP3-
round-tripped and therefore imprecise) loop-ready version — there is no
pristine raw backup checked in (assets/ is gitignored). Point --src at
wherever the original recordings actually live if this ever needs to
be re-run with a different crossfade duration or sample rate — a 48kHz
decode of the originals is also fine as input even when targeting a
lower TARGET_SR, ffmpeg resamples in the same step.

Usage: tools/gen_builtin_sound_loops.py <src_dir> <out_dir>
  <src_dir>: directory containing raw Birds.*/Brook.*/Ocean.*/Rain.*
             (any ffmpeg-readable format — .mp3, .wav, ...)
  <out_dir>: where to write the processed, loop-ready .wav files
"""
import subprocess
import sys
import wave
import array
import math
import pathlib

CROSSFADE_SEC = 10.0
TARGET_SR = 16000  # kept low deliberately — this is *raw* PCM now, embedded directly in flash (no MP3), so every kHz costs real flash budget across all 4 clips
NAMES = ["Birds", "Brook", "Ocean", "Rain"]


def decode_to_wav(src_audio, wav_path):
    subprocess.run(
        ["ffmpeg", "-y", "-i", str(src_audio), "-ac", "1", "-ar", str(TARGET_SR), str(wav_path)],
        check=True, capture_output=True,
    )


def make_loop(wav_in, wav_out, crossfade_sec):
    with wave.open(str(wav_in), "rb") as w:
        assert w.getsampwidth() == 2, "expected 16-bit PCM"
        n = w.getnframes()
        sr = w.getframerate()
        raw = w.readframes(n)
    samples = array.array("h")
    samples.frombytes(raw)
    L = len(samples)
    C = int(crossfade_sec * sr)
    if C * 2 > L:
        raise SystemExit(f"crossfade ({crossfade_sec}s) too long for clip length ({L / sr:.2f}s)")

    head = samples[0:C]
    tail = samples[L - C:L]
    middle = samples[C:L - C]

    blended = array.array("h", [0]) * C
    for i in range(C):
        t = i / C
        fade_out = math.cos(t * math.pi / 2)  # equal-power, avoids a loudness dip mid-crossfade
        fade_in = math.sin(t * math.pi / 2)
        v = tail[i] * fade_out + head[i] * fade_in
        if v > 32767:
            v = 32767
        if v < -32768:
            v = -32768
        blended[i] = int(v)

    out = array.array("h")
    out.extend(blended)
    out.extend(middle)

    with wave.open(str(wav_out), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(out.tobytes())

    return len(out) / sr, L / sr


def find_source(src_dir, name):
    for ext in (".mp3", ".wav", ".m4a", ".flac"):
        p = src_dir / f"{name}{ext}"
        if p.exists():
            return p
    raise SystemExit(f"no source file found for '{name}' in {src_dir}")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        raise SystemExit(1)
    src_dir = pathlib.Path(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        src = find_source(src_dir, name)
        raw_wav = out_dir / f"{name}_raw.wav"
        out_wav = out_dir / f"{name}.wav"
        decode_to_wav(src, raw_wav)
        loop_dur, orig_dur = make_loop(raw_wav, out_wav, CROSSFADE_SEC)
        raw_wav.unlink()
        size = out_wav.stat().st_size
        print(f"{name}: {orig_dur:.2f}s -> looped {loop_dur:.2f}s, wav={size} bytes ({size / 1024:.1f} KB)")


if __name__ == "__main__":
    main()
