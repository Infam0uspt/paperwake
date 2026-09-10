import freetype
print("SizeMetrics attrs:", [a for a in dir(freetype.SizeMetrics) if not a.startswith('_')])
face = freetype.Face('tools/fonts/GoogleSansCode_var.ttf')
face.set_char_size(12*64, 0, 141, 0)
print("12pt yAdvance (.height):", face.size.height)
print("has_mm:", face.has_multiple_masters)
print([a for a in dir(face) if 'var' in a.lower() or 'named' in a.lower() or 'mm' in a.lower()])
