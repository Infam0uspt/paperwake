#!/usr/bin/env python3
# Regenerates include/BuiltinSounds.h from assets/{Birds,Brook,Ocean,Rain}.wav
# (each already processed into a seamlessly-loopable, 16kHz mono 16-bit
# PCM clip — see tools/gen_builtin_sound_loops.py, which produced these
# from the original recordings via a crossfaded head/tail splice).
# Embeds the *raw PCM data* (WAV header stripped), not a codec — see
# gen_builtin_sound_loops.py's docstring for why MP3 specifically was
# ruled out here (block-codec encoder/decoder padding breaks the
# precisely-aligned loop seam).
# Usage: tools/gen_builtin_sounds.py
import pathlib
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
OUT = ROOT / "include" / "BuiltinSounds.h"

NAMES = ["Birds", "Brook", "Ocean", "Rain"]
BYTES_PER_LINE = 20


def main():
    with OUT.open("w") as f:
        f.write("#pragma once\n\n")
        f.write("// Generated from assets/{Birds,Brook,Ocean,Rain}.wav by\n")
        f.write("// tools/gen_builtin_sounds.py. Replace the source files and\n")
        f.write("// re-run that script to regenerate this header.\n")
        f.write("//\n")
        f.write("// Raw 16-bit mono PCM (WAV header stripped) at kBuiltinSoundSampleRate\n")
        f.write("// (Sound.cpp) each — cast the byte array to `const int16_t*` to use,\n")
        f.write("// safe on this little-endian target. Deliberately not MP3 — see\n")
        f.write("// tools/gen_builtin_sound_loops.py's docstring.\n\n")
        f.write("#include <Arduino.h>\n\n")
        for name in NAMES:
            src = ASSETS / f"{name}.wav"
            with wave.open(str(src), "rb") as w:
                assert w.getsampwidth() == 2, f"{src}: expected 16-bit PCM"
                assert w.getnchannels() == 1, f"{src}: expected mono"
                sample_count = w.getnframes()
                sample_rate = w.getframerate()
                data = w.readframes(sample_count)
            f.write(f"const uint8_t k{name}Pcm[] PROGMEM = {{\n")
            for i in range(0, len(data), BYTES_PER_LINE):
                chunk = data[i : i + BYTES_PER_LINE]
                f.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            f.write("};\n")
            f.write(f"constexpr uint32_t k{name}SampleCount = {sample_count};\n\n")
            print(f"{name}: {sample_count} samples @ {sample_rate}Hz, {len(data)} bytes")

    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
