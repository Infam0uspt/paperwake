#!/usr/bin/env python3
"""
regenerate_fonts.py - Convert Google Fonts TTFs to PaperWake WideFont headers
with full Latin-1 character range (0x20-0xFF).

Replicates the logic of tools/fontconvert_wide/fontconvert_wide.c using
freetype-py (no C compiler needed):
  - DPI = 141
  - FT_LOAD_TARGET_MONO + FT_RENDER_MODE_MONO (1-bit)
  - Bit-packed MSB-first, padded to byte boundary

Usage:
  python tools/regenerate_fonts.py
"""

import json
import os
import time
import freetype

DPI = 141

WEIGHT_MAP = {
    "Thin": 100, "ExtraLight": 200, "Light": 300,
    "Regular": 400, "Medium": 500, "SemiBold": 600,
    "Bold": 700, "ExtraBold": 800, "Black": 900,
}


def set_weight(face, weight_name):
    """Set the weight axis of a variable font to the desired value."""
    target = WEIGHT_MAP.get(weight_name, 400)
    try:
        info = face.get_variation_info()
    except AttributeError:
        return  # not a variable font
    axes = info.axes
    coords = []
    for a in axes:
        tag = a.tag
        if tag == "wght":
            coords.append(float(target))
        else:
            coords.append(float(a.default))
        face.set_var_design_coords(tuple(coords))


def render_char(face, char_code):
    """Render a single char in mono. Returns (bitmap_bytes, glyph_metrics)."""
    face.load_char(char_code, freetype.FT_LOAD_TARGET_MONO)
    face.glyph.render(freetype.FT_RENDER_MODE_MONO)
    bm = face.glyph.bitmap
    w, h, pitch = bm.width, bm.rows, bm.pitch
    data = bytearray()
    bp = 0
    cb = 0
    for y in range(h):
        for x in range(w):
            if bm.buffer[y * pitch + x // 8] & (0x80 >> (x & 7)):
                cb |= (0x80 >> bp)
            bp += 1
            if bp == 8:
                data.append(cb)
                cb = 0
                bp = 0
    if bp > 0:
        data.append(cb)

    metrics = {
        'bitmapOffset': 0,  # filled in later
        'width': w,
        'height': h,
        'xAdvance': int(face.glyph.advance.x >> 6),
        'xOffset': face.glyph.bitmap_left,
        'yOffset': 1 - face.glyph.bitmap_top,
    }
    return data, metrics


def generate_font(ttf_path, output_name, size, first, last, weight_name, out_dir):
    """Generate a WideFont header from a TTF file."""
    face = freetype.Face(ttf_path)
    set_weight(face, weight_name)
    face.set_char_size(size * 64, 0, DPI, 0)

    glyphs = []
    bitmap_data = bytearray()
    offset = 0
    t0 = time.time()

    for ch in range(first, last + 1):
        try:
            data, g = render_char(face, ch)
            g['bitmapOffset'] = offset
            glyphs.append(g)
            bitmap_data.extend(data)
            offset += len(data)
        except Exception as e:
            glyphs.append({
                'bitmapOffset': offset, 'width': 0, 'height': 0,
                'xAdvance': size // 2, 'xOffset': 0, 'yOffset': 0
            })
            print(f"  warning: 0x{ch:02X}: {e}")

    t1 = time.time()
    ya = int(face.size.height >> 6) if face.size.height > 0 else size
    print(f"  rendered {len(glyphs)} glyphs in {t1 - t0:.2f}s, bitmap={len(bitmap_data)} bytes, yAdvance={ya}")

    # Build header - identical format to fontconvert_wide.c output
    lines = []
    lines.append('#pragma once')
    lines.append('#include "../WideFont.h"')
    lines.append('')
    lines.append(f'const uint8_t {output_name}Bitmaps[] PROGMEM = {{')
    for i in range(0, len(bitmap_data), 12):
        chunk = bitmap_data[i:i + 12]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        if i + 12 < len(bitmap_data):
            lines.append(f'  {hex_str},')
        else:
            lines[-1] = lines[-1].rstrip(',') if lines[-1] else f'  {hex_str}'
    lines.append('};')
    lines.append('')

    lines.append(f'const WideGlyph {output_name}Glyphs[] PROGMEM = {{')
    for i, g in enumerate(glyphs):
        ch = first + i
        comment = f'   // 0x{ch:02X}'
        if 32 <= ch <= 126:
            comment += f" '{chr(ch)}'"
        comma = ',' if i < len(glyphs) - 1 else ''
        lines.append(f"  {{ {g['bitmapOffset']:5d}, {g['width']:3d}, {g['height']:3d}, {g['xAdvance']:3d}, {g['xOffset']:4d}, {g['yOffset']:4d} }}{comma}{comment}")
    last_comment = f' // 0x{last:02X}'
    if 32 <= last <= 126:
        last_comment += f" '{chr(last)}'"
    lines[-1] = lines[-1].rstrip() + last_comment
    lines.append('};')
    lines.append('')

    lines.append(f'const WideFont {output_name} PROGMEM = {{')
    lines.append(f'  (uint8_t  *){output_name}Bitmaps,')
    lines.append(f'  (WideGlyph *){output_name}Glyphs,')
    lines.append(f'  0x{first:02X}, 0x{last:02X}, {ya} }};')
    lines.append('')
    lines.append(f'// Approx. {len(bitmap_data) + len(glyphs) * 7 + 7} bytes')

    out_path = os.path.join(out_dir, f'{output_name}.h')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    print(f'  -> {out_path}')


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    mapping_path = os.path.join(script_dir, 'ttf_mapping.json')
    ttf_dir = os.path.join(script_dir, 'ttf_downloads')
    out_dir = os.path.join(project_dir, 'include', 'Fonts')

    with open(mapping_path, 'r', encoding='utf-8') as f:
        config = json.load(f)

    print(f'Regenerating {len(config["fonts"])} fonts with Latin-1 range (0x20-0xFF)...')

    for font_config in config['fonts']:
        output_name = font_config['output']
        ttf_name = font_config['ttf']
        ttf_path = os.path.join(ttf_dir, ttf_name)

        if not os.path.exists(ttf_path):
            print(f'  ERROR: TTF not found: {ttf_path}')
            continue

        first = font_config.get('first', 0x20)
        last = font_config.get('last', 0xFF)
        size = font_config['size']
        weight_name = font_config.get('weight_name', 'Regular')

        print(f'\nProcessing {output_name} (size={size}, w={weight_name}, range=0x{first:02X}-0x{last:02X})')

        generate_font(
            ttf_path=ttf_path,
            output_name=output_name,
            size=size,
            first=first,
            last=last,
            weight_name=weight_name,
            out_dir=out_dir,
        )

    print('\nDone!')


if __name__ == '__main__':
    main()
