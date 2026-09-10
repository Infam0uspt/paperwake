#pragma once
// Minimal stand-in for Adafruit_GFX.h, used only by the offline
// layout preview tool (tools/sim) so the project's real GFXfont
// header files (include/Fonts/*.h) can be #included unmodified on a
// desktop build, without pulling in the Arduino framework.
//
// Not used by the firmware build (PlatformIO resolves the real
// Adafruit GFX Library there); this only matters when compiling
// tools/sim/render_preview.cpp.

#include <stdint.h>

#define PROGMEM

typedef struct {
  uint16_t bitmapOffset;
  uint8_t width, height;
  uint8_t xAdvance;
  int8_t xOffset, yOffset;
} GFXglyph;

typedef struct {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first, last;
  uint8_t yAdvance;
} GFXfont;
