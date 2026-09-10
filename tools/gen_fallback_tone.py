#!/usr/bin/env python3
# Regenerates include/FallbackTone.h from assets/airbus.mp3.
# Usage: tools/gen_fallback_tone.py
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "airbus.mp3"
OUT = ROOT / "include" / "FallbackTone.h"

BYTES_PER_LINE = 20


def main():
    data = SRC.read_bytes()

    with OUT.open("w") as f:
        f.write("#pragma once\n\n")
        f.write(f"// Generated from assets/{SRC.name} by tools/gen_fallback_tone.py.\n")
        f.write("// Replace the source file and re-run that script to regenerate this array.\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write("const uint8_t kFallbackToneMp3[] PROGMEM = {\n")
        for i in range(0, len(data), BYTES_PER_LINE):
            chunk = data[i : i + BYTES_PER_LINE]
            f.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n\n")
        f.write("constexpr uint32_t kFallbackToneMp3Len = sizeof(kFallbackToneMp3);\n")

    print(f"Wrote {OUT} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
