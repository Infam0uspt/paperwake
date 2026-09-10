#pragma once
#include <stdint.h>

// Arduino.h defines PROGMEM already in the firmware build; the
// offline preview tool (tools/sim) has no such definition, so this
// header provides one when needed.
#ifndef PROGMEM
#define PROGMEM
#endif

// A GFXfont-alike format for glyphs too large for Adafruit_GFX's own
// GFXglyph (its xOffset/yOffset are int8_t, width/height uint8_t,
// bitmapOffset uint16_t — all of which silently wrap around for big
// fonts, e.g. a 210px-tall clock digit, with no warning). Generated
// by tools/fontconvert_wide (a fork of Adafruit's fontconvert with
// these fields widened); rendered here instead of via
// display.setFont()/print(), which only understands the classic
// format.

struct WideGlyph {
  uint32_t bitmapOffset;
  uint16_t width, height;
  int16_t xAdvance;
  int16_t xOffset, yOffset;
};

struct WideFont {
  const uint8_t *bitmap;
  const WideGlyph *glyph;
  uint16_t first, last;
  uint16_t yAdvance;
};

// Works with anything exposing drawPixel(x, y, color) — GxEPD2_BW in
// the firmware, or the offline preview Canvas in tools/sim.
template <typename Display, typename Color>
int16_t drawWideChar(Display &d, int16_t x, int16_t y, unsigned char c, const WideFont &font, Color color) {
  if (c < font.first || c > font.last) return 0;
  const WideGlyph &g = font.glyph[c - font.first];
  uint32_t bo = g.bitmapOffset;
  uint8_t bits = 0, bit = 0;
  for (uint16_t yy = 0; yy < g.height; yy++) {
    for (uint16_t xx = 0; xx < g.width; xx++) {
      if (!(bit++ & 7)) bits = font.bitmap[bo++];
      if (bits & 0x80) d.drawPixel(x + g.xOffset + xx, y + g.yOffset + yy, color);
      bits <<= 1;
    }
  }
  return g.xAdvance;
}

// Returns the cursor x position after the last character (mirrors
// Adafruit_GFX::getCursorX() after a print()).
template <typename Display, typename Color>
int16_t drawWideText(Display &d, int16_t x, int16_t y, const char *str, const WideFont &font, Color color) {
  int16_t cursor = x;
  for (const char *p = str; *p; ++p) {
    cursor += drawWideChar(d, cursor, y, static_cast<unsigned char>(*p), font, color);
  }
  return cursor;
}

// Total advance width of `str` in `font`, without drawing anything —
// lets callers center/align text (e.g. switching a string between a
// regular and a bold WideFont of different total widths while
// keeping it visually centered on the same spot).
inline int16_t measureWideText(const char *str, const WideFont &font) {
  int16_t width = 0;
  for (const char *p = str; *p; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (c < font.first || c > font.last) continue;
    width += font.glyph[c - font.first].xAdvance;
  }
  return width;
}
