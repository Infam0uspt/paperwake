#!/usr/bin/env python3
# Regenerates include/WakeSubtitles.h from assets/wake_subtitles.txt.
# Usage: tools/gen_wake_subtitles.py
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "wake_subtitles.txt"
OUT = ROOT / "include" / "WakeSubtitles.h"


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    lines = [l.strip() for l in SRC.read_text().splitlines() if l.strip()]

    with OUT.open("w") as f:
        f.write("#pragma once\n\n")
        f.write("// Generated from assets/wake_subtitles.txt by tools/gen_wake_subtitles.py.\n")
        f.write("// Edit the txt file and re-run that script to regenerate this list.\n\n")
        f.write("const char *const kWakeSubtitles[] PROGMEM = {\n")
        for line in lines:
            f.write(f'  "{escape(line)}",\n')
        f.write("};\n\n")
        f.write("constexpr int kWakeSubtitlesCount = sizeof(kWakeSubtitles) / sizeof(kWakeSubtitles[0]);\n")

    print(f"Wrote {OUT} ({len(lines)} lines)")


if __name__ == "__main__":
    main()
