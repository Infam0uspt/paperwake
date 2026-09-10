#!/bin/sh
# Builds the offline clock-face preview tool.
# Usage: tools/sim/build.sh && tools/sim/render_preview [HH MM alarmHH alarmMM on|off out.ppm]
set -e
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
GFX_FONTS="$ROOT/.pio/libdeps/esp32-s3-devkitc-1/Adafruit GFX Library"
c++ -std=c++17 -I. -Istubs -I"$ROOT/include" -I"$GFX_FONTS" \
    render_preview.cpp -o render_preview
echo "Built tools/sim/render_preview"
