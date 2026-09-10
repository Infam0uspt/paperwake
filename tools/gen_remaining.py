#!/usr/bin/env python3
"""Generate remaining fonts with Latin-1 range."""
import json, os, freetype

DPI = 141
WEIGHT_MAP = {'Thin':100,'ExtraLight':200,'Light':300,'Regular':400,'Medium':500,'SemiBold':600,'Bold':700,'ExtraBold':800,'Black':900}

def get_axis_index(face, axis_tag):
    try:
        info = face.get_variation_info()
    except AttributeError:
        return None
    if info is None:
        return None
    for i, a in enumerate(info.axes):
        if a.tag == axis_tag:
            return i
    return None

def set_weight(face, weight_name):
    target = WEIGHT_MAP.get(weight_name, 400)
    idx = get_axis_index(face, 'wght')
    if idx is None:
        return
    info = face.get_variation_info()
    coords = [a.default for a in info.axes]
    coords[idx] = target
    face.set_var_design_coordinates(coords)

def render_glyph_mono(face, char_code):
    face.load_char(char_code, freetype.FT_LOAD_TARGET_MONO)
    face.glyph.render(freetype.FT_RENDER_MODE_MONO)
    bm = face.glyph.bitmap
    width, rows, pitch = bm.width, bm.rows, bm.pitch
    out = bytearray()
    bit_pos = 0
    cb = 0
    for y in range(rows):
        ro = y * pitch
        for x in range(width):
            if bm.buffer[ro + x // 8] & (0x80 >> (x & 7)):
                cb |= (0x80 >> bit_pos)
            bit_pos += 1
            if bit_pos == 8:
                out.append(cb)
                cb = 0
                bit_pos = 0
    if bit_pos > 0: out.append(cb)
    return bytes(out), {'width':width,'height':rows,'xAdvance':int(face.glyph.advance.x >> 6),'xOffset':face.glyph.bitmap_left,'yOffset':1-face.glyph.bitmap_top}

def gen_font(ttf_path, output_name, size, first, last, weight_name, out_dir):
    face = freetype.Face(ttf_path)
    set_weight(face, weight_name)
    face.set_char_size(size * 64, 0, DPI, 0)
    glyphs = []
    bitmap_data = bytearray()
    bitmap_offset = 0
    for cc in range(first, last + 1):
        try:
            data, m = render_glyph_mono(face, cc)
        except Exception as e:
            data = b'\x00'; m = {'width':0,'height':0,'xAdvance':size//2,'xOffset':0,'yOffset':0}
        glyphs.append({'bitmapOffset':bitmap_offset,'width':m['width'],'height':m['height'],'xAdvance':m['xAdvance'],'xOffset':m['xOffset'],'yOffset':m['yOffset']})
        bitmap_data.extend(data)
        bitmap_offset += len(data)
    y_advance = int(face.size.height >> 6) if face.size and face.size.height > 0 else size
    lines = ['#pragma once','#include "../WideFont.h"','']
    lines.append(f'const uint8_t {output_name}Bitmaps[] PROGMEM = {{')
    for i in range(0, len(bitmap_data), 12):
        lines.append('  ' + ', '.join(f'0x{b:02X}' for b in bitmap_data[i:i+12]) + ',')
    lines[-1] = lines[-1].rstrip(',')
    lines.append('};')
    lines.append('')
    lines.append(f'const WideGlyph {output_name}Glyphs[] PROGMEM = {{')
    for i, g in enumerate(glyphs):
        cc = first + i
        cmt = f'   // 0x{cc:02X}'
        if 32 <= cc <= 126: cmt += f" '{chr(cc)}'"
        comma = ',' if i < len(glyphs) - 1 else ' '
        lines.append(f'  {{ {g["bitmapOffset"]:5d}, {g["width"]:3d}, {g["height"]:3d}, {g["xAdvance"]:3d}, {g["xOffset"]:4d}, {g["yOffset"]:4d} }}{comma}{cmt}')
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
    print(f'OK: {output_name} ({len(glyphs)} glyphs, {len(bitmap_data)} bytes)')

def main():
    sd = os.path.dirname(os.path.abspath(__file__))
    ttf_dir = os.path.join(sd, 'ttf_downloads')
    out_dir = os.path.join(os.path.dirname(sd), 'include', 'Fonts')
    fonts_to_gen = [
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_Regular12pt7b', 12, 0x20, 0xFF, 'Regular'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_BoldSmall', 12, 0x20, 0xFF, 'Bold'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_RegularSettingsRow', 21, 0x20, 0xFF, 'Regular'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_RegularSettingsTab', 25, 0x20, 0xFF, 'Regular'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_SemiBoldSettingsTab', 25, 0x20, 0xFF, 'SemiBold'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_MediumWakeSubtitle', 21, 0x20, 0xFF, 'Medium'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_RegularAlarmCountdown', 17, 0x20, 0xFF, 'Regular'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_RegularAlarmTime', 25, 0x20, 0xFF, 'Regular'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_BoldAlarmTime', 25, 0x20, 0xFF, 'Bold'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_SemiBoldSettingsTitle', 33, 0x20, 0xFF, 'SemiBold'),
        ('GoogleSansFlex-VariableFont_GRAD,ROND,opsz,slnt,wdth,wght.ttf', 'GoogleSansFlex_SemiBoldWakeTitle', 44, 0x20, 0xFF, 'SemiBold'),
        ('GoogleSansCode_wght.ttf', 'GoogleSansCode_MediumDayNumber', 44, 0x30, 0x3A, 'Medium'),
        ('GoogleSansCode_wght.ttf', 'GoogleSansCode_MediumWakeTime', 32, 0x30, 0x3A, 'Medium'),
    ]
    for ttf, name, size, first, last, weight in fonts_to_gen:
        out_path = os.path.join(out_dir, f'{name}.h')
        if os.path.exists(out_path):
            print(f'SKIP (exists): {name}'); continue
        ttf_path = os.path.join(ttf_dir, ttf)
        if not os.path.exists(ttf_path):
            print(f'ERROR: TTF not found {ttf}'); continue
        print(f'Generating {name} ({ttf}, size={size}, weight={weight}, 0x{first:02X}-0x{last:02X})...')
        gen_font(ttf_path, name, size, first, last, weight, out_dir)
    print('\nAll done!')

if __name__ == '__main__':
    main()
