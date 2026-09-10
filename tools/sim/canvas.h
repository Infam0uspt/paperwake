#pragma once
// Minimal offline reimplementation of the handful of Adafruit_GFX
// drawing primitives EpaperDisplay.cpp actually uses, so we can
// render a preview PNG of the clock face without any hardware.
//
// The GFXfont glyph-blit loop mirrors Adafruit_GFX::drawChar's real
// algorithm (continuous MSB-first bitstream, not row-padded) so text
// layout matches the firmware exactly. fillCircle uses a simple
// distance test rather than Adafruit's Bresenham fill — visually
// identical for our purposes (small dots), not bit-for-bit identical.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "stubs/Adafruit_GFX.h"

class Canvas {
 public:
  Canvas(int w, int h) : width_(w), height_(h), pixels_(w * h, false) {}

  void fillScreen(bool black) { std::fill(pixels_.begin(), pixels_.end(), black); }

  void setPixel(int x, int y, bool black) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pixels_[y * width_ + x] = black;
  }

  // WideFont.h's drawWideChar/drawWideText call drawPixel(), matching
  // Adafruit_GFX's method name, so the same template works against
  // both this Canvas and the real GxEPD2 display object.
  void drawPixel(int x, int y, bool black) { setPixel(x, y, black); }

  // Matches Adafruit_GFX::drawBitmap's 1bpp format: row-major, MSB
  // first, each row padded to a byte boundary.
  void drawBitmap(int x, int y, const uint8_t *bitmap, int w, int h, bool black) {
    int bytesPerRow = (w + 7) / 8;
    for (int yy = 0; yy < h; yy++) {
      for (int xx = 0; xx < w; xx++) {
        uint8_t byteVal = bitmap[yy * bytesPerRow + xx / 8];
        if (byteVal & (0x80 >> (xx % 8))) setPixel(x + xx, y + yy, black);
      }
    }
  }

  void fillCircle(int cx, int cy, int r, bool black) {
    for (int yy = -r; yy <= r; yy++)
      for (int xx = -r; xx <= r; xx++)
        if (xx * xx + yy * yy <= r * r) setPixel(cx + xx, cy + yy, black);
  }

  // A thin ~1px ring via a distance test — matches Adafruit's 1px
  // drawCircle() closely enough that callers can draw two concentric
  // ones (radius, radius-1) for a thicker outline, same as the
  // firmware does.
  void drawCircle(int cx, int cy, int r, bool black) {
    for (int yy = -r; yy <= r; yy++) {
      for (int xx = -r; xx <= r; xx++) {
        double d = std::sqrt(static_cast<double>(xx * xx + yy * yy));
        if (d >= r - 0.5 && d <= r + 0.5) setPixel(cx + xx, cy + yy, black);
      }
    }
  }

  void fillRect(int x, int y, int w, int h, bool black) {
    for (int yy = 0; yy < h; yy++)
      for (int xx = 0; xx < w; xx++) setPixel(x + xx, y + yy, black);
  }

  // Matches Adafruit_GFX::fillRoundRect visually: a rect with the 4
  // corners replaced by quarter circles of the given radius.
  void fillRoundRect(int x, int y, int w, int h, int r, bool black) {
    fillRect(x + r, y, w - 2 * r, h, black);
    fillRect(x, y + r, r, h - 2 * r, black);
    fillRect(x + w - r, y + r, r, h - 2 * r, black);
    auto quarter = [&](int cx, int cy, int sx, int sy) {
      for (int yy = 0; yy <= r; yy++)
        for (int xx = 0; xx <= r; xx++)
          if (xx * xx + yy * yy <= r * r) setPixel(cx + sx * xx, cy + sy * yy, black);
    };
    quarter(x + r, y + r, -1, -1);
    quarter(x + w - r - 1, y + r, 1, -1);
    quarter(x + r, y + h - r - 1, -1, 1);
    quarter(x + w - r - 1, y + h - r - 1, 1, 1);
  }

  void drawRect(int x, int y, int w, int h, bool black) {
    fillRect(x, y, w, 1, black);
    fillRect(x, y + h - 1, w, 1, black);
    fillRect(x, y, 1, h, black);
    fillRect(x + w - 1, y, 1, h, black);
  }

  void setFont(const GFXfont *f) { font_ = f; }
  void setCursor(int x, int y) { cursorX_ = x; cursorY_ = y; }
  int getCursorX() const { return cursorX_; }

  void print(const std::string &s) {
    for (char c : s) drawChar(static_cast<unsigned char>(c));
  }

  bool writePPM(const char *path) const {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "P2\n%d %d\n255\n", width_, height_);
    for (int y = 0; y < height_; y++) {
      for (int x = 0; x < width_; x++) {
        fprintf(f, "%d ", pixels_[y * width_ + x] ? 0 : 255);
      }
      fprintf(f, "\n");
    }
    fclose(f);
    return true;
  }

 private:
  void drawChar(unsigned char c) {
    if (!font_ || c < font_->first || c > font_->last) return;
    const GFXglyph &g = font_->glyph[c - font_->first];
    uint16_t bo = g.bitmapOffset;
    uint8_t bits = 0, bit = 0;
    for (int yy = 0; yy < g.height; yy++) {
      for (int xx = 0; xx < g.width; xx++) {
        if (!(bit++ & 7)) bits = font_->bitmap[bo++];
        if (bits & 0x80) setPixel(cursorX_ + g.xOffset + xx, cursorY_ + g.yOffset + yy, true);
        bits <<= 1;
      }
    }
    cursorX_ += g.xAdvance;
  }

  int width_, height_;
  std::vector<bool> pixels_;
  const GFXfont *font_ = nullptr;
  int cursorX_ = 0, cursorY_ = 0;
};
