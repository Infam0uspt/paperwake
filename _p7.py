import freetype
face = freetype.Face('tools/fonts/GoogleSansCode_var.ttf')
info = face.get_variation_info()
print("num_axis:", info.num_axis)
for i in range(info.num_axis):
    a = info.axis[i]
    print(f"axis {i}: tag={a.tag if hasattr(a,'tag') else a.width_id} min={a.minimum} max={a.maximum} def={a.def_ if hasattr(a,'def_') else a.default}")
