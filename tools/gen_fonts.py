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
  python tools/gen_fonts.py
"""

import json
import os
import freetype

DPI = 141

WEIGHT_MAP = {
    "Thin": 100, "ExtraLight": 200, "Light": 300,
    "Regular": 400, "Medium": 500, "SemiBold": 600,
    "Bold": 700, "ExtraBold": 800, "Black": 900,
}


def get_axis_index(face, axis_tag):
    """Find the index of a given axis tag in a variable font."""
    if not (face.face_flags & freetype.FT_FACE_FLAG_VARIABLE):
        return None
    for i in range(face.num_axes):
        axis = face.design_axes[i]
        tag_str = axis.name.decode("ascii") if isinstance(axis.name, bytes) else axis.name
        if tag_str == axis_tag:
            return i
    return None


def set_weight(face, weight_name):
    """Set the weight axis of a variable font to the desired value."""
    target = WEIGHT_MAP.get(weight_name, 400)
    idx = get_axis_index(face, "wght")
    if idx is None:
        return
    coords = [face.design_axes[i].def for i in range(face.num_axes)]
    coords[idx] = target
    face.set_var_design_coordinates(coords)


def render_glyph_mono(face, char_code):
    """Load and render a single glyph in mono mode."""
    face.load_char(char_code, freetype.FT_LOAD_TARGET_MONO)
    face.glyph.render(freetype.FT_RENDER_MODE_MONO)

    bitmap = face.glyph.bitmap
    width = bitmap.width
    rows = bitmap.rows
    pitch = bitmap.pitch

    out = bytearray()
    bit_pos = 0
    current_byte = 0
    for y in range(rows):
        row_offset = y * pitch
        for x in range(width):
            byte_idx = x // 8
            bit_mask = 0x80 >> (x & 7)
            if bitmap.buffer[row_offset + byte_idx] & bit_mask:
                current_byte |= (0x80 >> bit_pos)
            bit_pos += 1
            if bit_pos == 8:
                out.append(current_byte)
                current_byte = 0
                bit_pos = 0

    if bit_pos > 0:
        out.append(current_byte)

    metrics = {
        'width': width,
        'height': rows,
        'xAdvance': int(face.glyph.advance.x >> 6),
        'xOffset': face.glyph.bitmap_left,
        'yOffset': 1 - face.glyph.bitmap_top,
    }
    return bytes(out), metrics


def generate_font(ttf_path, output_name, size, first, last, weight_name, out_dir):
    """Generate a WideFont header from a TTF file."""
    face = freetype.Face(ttf_path)
    set_weight(face, weight_name)
    face.set_char_size(size * 64, 0, DPI, 0)

    glyphs = []
    bitmap_data = bytearray()
    bitmap_offset = 0

    for char_code in range(first, last + 1):
        try:
            data, metrics = render_glyph_mono(face, char_code)
        except Exception as e:
            print(f"  Warning: failed to render char 0x{char_code:02X}: {e}")
            data = b'\x00'
            metrics = {'width': 0, 'height': 0, 'xAdvance': size // 2, 'xOffset': 0, 'yOffset': 0}

        glyphs.append({
            'bitmapOffset': bitmap_offset,
            'width': metrics['width'],
            'height': metrics['height'],
            'xAdvance': metrics['xAdvance'],
            'xOffset': metrics['xOffset'],
            'yOffset': metrics['yOffset'],
        })
        bitmap_data.extend(data)
        bitmap_offset += len(data)

    y_advance = int(face.size.metrics.height >> 6) if face.size.metrics.height > 0 else size

    lines = []
    lines.append('#pragma once')
    lines.append('#include "../WideFont.h"')
    lines.append('')

    lines.append(f'const uint8_t {output_name}Bitmaps[] PROGMEM = {{')
    for i in range(0, len(bitmap_data), 12):
        chunk = bitmap_data[i:i+12]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append(f'  {hex_str},')
    lines[-1] = lines[-1].rstrip(',')
    lines.append('};')
    lines.append('')

    lines.append(f'const WideGlyph {output_name}Glyphs[] PROGMEM = {{')
    for i, g in enumerate(glyphs):
        char_code = first + i
        comment = f'   // 0x{char_code:02X}'
        if 32 <= char_code <= 126:
            comment += f" '{chr(char_code)}'"
        comma = ',' if i < len(glyphs) - 1 else ' '
        lines.append(f'  {{ {g["bitmapOffset"]:5d}, {g["width"]:3d}, {g["height"]:3d}, {g["xAdvance"]:3d}, {g["xOffset"]:4d}, {g["yOffset"]:4d} }}{comma}{comment}')
    last_char = last
    last_comment = f' // 0x{last_char:02X}'
    if 32 <= last_char <= 126:
        last_comment += f" '{chr(last_char)}'"
    lines[-1] = lines[-1].rstrip() + last_comment
    lines.append('};')
    lines.append('')

    lines.append(f'const WideFont {output_name} PROGMEM = {{')
    lines.append(f'  (uint8_t  *){output_name}Bitmaps,')
    lines.append(f'  (WideGlyph *){output_name}Glyphs,')
    lines.append(f'  0x{first:02X}, 0x{last:02X}, {y_advance} }};')
    lines.append('')
    lines.append(f'// Approx. {len(bitmap_data) + len(glyphs) * 7 + 7} bytes')

    out_path = os.path.join(out_dir, f'{output_name}.h')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    print(f'  Generated {out_path} ({len(glyphs)} glyphs, {len(bitmap_data)} bytes bitmap)')
    return out_path


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
        if 'first' in font_config:
            first = font_config['first']
        if 'last' in font_config:
            last = font_config['last']

        print(f'\nProcessing {output_name} ({ttf_name}, size={font_config["size"]}, weight={font_config["weight_name"]}, range=0x{first:02X}-0x{last:02X})')

        generate_font(
            ttf_path=ttf_path,
            output_name=output_name,
            size=font_config['size'],
            first=first,
            last=last,
            weight_name=font_config['weight_name'],
            out_dir=out_dir,
        )

    print('\nDone!')


if __name__ == '__main__':
    main()
