#!/usr/bin/env python3
import json, os, freetype, time

DPI = 141
WEIGHTS = {"Regular":400,"Medium":500,"SemiBold":600,"Bold":700}

base = os.path.dirname(os.path.abspath(__file__))
proj = os.path.dirname(base)
cfg = json.load(open(os.path.join(base, "ttf_mapping.json")))

font = cfg["fonts"][0]
out_name = font["output"]
ttf_path = os.path.join(base, "ttf_downloads", font["ttf"])
first = font.get("first", 0x20)
last = font.get("last", 0xFF)
size = font["size"]
font = cfg["fonts"][3]  # Use smaller font (Regular12pt)

weight = font.get("weight_name", "Regular")
print(f"Processing {out_name} (size={size}, w={weight}, 0x{first:02X}-0x{last:02X})")
# Limit to small range for testing
first = 0x41
last = 0x5A


face = freetype.Face(ttf_path)
print("face created")

# Set weight
info = face.get_variation_info()
axes = info.axes
coords = []
target = WEIGHTS.get(weight, 400)
for a in axes:
    tag = a.tag.decode('ascii') if isinstance(a.tag, bytes) else a.tag
    if tag == "wght":
        coords.append(float(target))
    else:
        coords.append(float(a.default))
face.set_var_design_coords(tuple(coords))
print("weight set")

face.set_char_size(size * 64, 0, DPI, 0)
print("char size set")

glyphs = []
bitmap_data = bytearray()
offset = 0

t0 = time.time()
for ch in range(first, last + 1):
    try:
        face.load_char(ch, freetype.FT_LOAD_TARGET_MONO)
        face.glyph.render(freetype.FT_RENDER_MODE_MONO)
        bm = face.glyph.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        data = bytearray()
        bp = 0; cb = 0
        for y in range(h):
            for x in range(w):
                if bm.buffer[y*pitch + x//8] & (0x80 >> (x & 7)):
                    cb |= (0x80 >> bp)
                bp += 1
                if bp == 8:
                    data.append(cb); cb = 0; bp = 0
        if bp > 0:
            data.append(cb)
        g = {"off":offset,"w":w,"h":h,
             "xa":int(face.glyph.advance.x >> 6),
             "xo":face.glyph.bitmap_left,
             "yo":1-face.glyph.bitmap_top}
        glyphs.append(g)
        bitmap_data.extend(data)
        offset += len(data)
    except Exception as e:
        glyphs.append({"off":offset,"w":0,"h":0,"xa":size//2,"xo":0,"yo":0})
        print(f"  warn: 0x{ch:02X}: {e}")
t1 = time.time()

print(f"rendered {len(glyphs)} glyphs in {t1-t0:.2f}s")
print(f"bitmap_data: {len(bitmap_data)} bytes")

ya = int(face.size.height >> 6) if face.size.height > 0 else size
print(f"yAdvance: {ya}")

# Build header
L = ["#pragma once", '#include "../WideFont.h"', ""]
L.append(f"const uint8_t {out_name}Bitmaps[] PROGMEM = {{")
for i in range(0, len(bitmap_data), 12):
    chunk = bitmap_data[i:i+12]
    L.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
L[-1] = L[-1].rstrip(",")
L.append("};")
L.append("")
L.append(f"const WideGlyph {out_name}Glyphs[] PROGMEM = {{")
for i, g in enumerate(glyphs):
    ch = first + i
    cmt = f"   // 0x{ch:02X}"
    if 32 <= ch <= 126:
        cmt += f" '{chr(ch)}'"
    comma = "," if i < len(glyphs) - 1 else ""
    s = f"  {{ {g['off']:5d}, {g['w']:3d}, {g['h']:3d}, {g['xa']:3d}, {g['xo']:4d}, {g['yo']:4d} }}{comma}{cmt}"
    L.append(s)
L.append("};")
L.append("")
L.append(f"const WideFont {out_name} PROGMEM = {{")
L.append(f"  (uint8_t  *){out_name}Bitmaps,")
L.append(f"  (WideGlyph *){out_name}Glyphs,")
L.append(f"  0x{first:02X}, 0x{last:02X}, {ya} }};")
L.append("")
L.append(f"// Approx. {len(bitmap_data) + len(glyphs) * 7 + 7} bytes")

out_path = os.path.join(proj, "include", "Fonts", f"{out_name}.h")
with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(L) + "\n")
print(f"-> {out_path}")
print("Done.")
