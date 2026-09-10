#!/usr/bin/env python3
"""
Installs the built-in ambient sounds (Birds/Brook/Ocean/Rain) onto a
microSD card as raw-PCM files that the firmware plays gaplessly.

Since Phase 3.5 the built-in loops no longer ship embedded in flash
(they used ~8.8MB — freed space). Instead they must live on the
SD card at /sounds/_builtin_<name>.pcm, where /sounds/ is the same
directory the web portal manages.

Input: the loop-ready clips produced by tools/gen_builtin_sound_loops.py
(a.k.a. assets/{Birds,Brook,Ocean,Rain}.wav). Output: a directory tree
you can copy straight onto the card:

    tools/install_builtin_sounds_to_sd.py <loops_dir> <out_dir>
      <loops_dir>: dir containing Birds.wav Brook.wav Ocean.wav Rain.wav
                    (16-bit mono PCM, loop-ready)
      <out_dir>  : where /sounds/_builtin_*.pcm get written

Each .pcm is the WAV's payload with the 44-byte header stripped — the
exact raw frames AudioGeneratorPCMLoop streams. Format is unchanged from
what BuiltinSounds.h used to embed.

The wavs are gitignored (assets/), so this script needs you to have
generated/obtained them. If a source wav is missing it's skipped with a
warning; the corresponding sound simply won't be selectable-to-audible
until the file is placed (the firmware falls back to "Tone").
"""
import pathlib
import sys
import wave

NAMES = ["Birds", "Brook", "Ocean", "Rain"]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        raise SystemExit(1)
    src_dir = pathlib.Path(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2])
    sounds_dir = out_dir / "sounds"
    sounds_dir.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        wav = src_dir / f"{name}.wav"
        if not wav.exists():
            print(f"[skip] {name}: no {wav.name} in {src_dir}")
            continue
        with wave.open(str(wav), "rb") as w:
            assert w.getsampwidth() == 2, f"{wav}: expected 16-bit PCM"
            assert w.getnchannels() == 1, f"{wav}: expected mono"
            n = w.getnframes()
            data = w.readframes(n)
        out = sounds_dir / f"_builtin_{name}.pcm"
        out.write_bytes(data)
        print(f"{name}: {n} samples -> {out} ({len(data)} bytes)")


if __name__ == "__main__":
    main()