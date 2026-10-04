# Synthetic artwork fixtures for the jc1060 UI simulator (no real cover art).
import io
import os
from PIL import Image
def quad():
    im = Image.new("RGB", (80, 80))
    px = im.load()
    for y in range(80):
        for x in range(80):
            px[x, y] = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)][(y >= 40) * 2 + (x >= 40)]
    return im
def enc(im, **kw):
    b = io.BytesIO(); im.save(b, "JPEG", **kw); return b.getvalue()
out = {
    "ART_FIXTURE_QUAD": enc(quad(), quality=90, subsampling=2),                     # 4:2:0 like rekordbox
    "ART_FIXTURE_GRAY": enc(Image.new("L", (240, 240), 128), quality=90),           # a*_m.jpg size, 1 component
    "ART_FIXTURE_PROGRESSIVE": enc(quad(), quality=90, progressive=True),          # TJpgDec: unsupported
}
lines = ["/* Generated synthetic JPEGs by gen_artwork_fixture.py (PIL); not user cover art. */",
         "#ifndef ARTWORK_FIXTURE_H", "#define ARTWORK_FIXTURE_H", "",
         "#include <stdint.h>", ""]
notes = {"ART_FIXTURE_QUAD": "80x80 baseline 4:2:0, quadrants red|green / blue|white",
         "ART_FIXTURE_GRAY": "240x240 baseline grayscale, flat 128",
         "ART_FIXTURE_PROGRESSIVE": "80x80 progressive quadrants: must be rejected"}
for k, v in out.items():
    lines.append(f"/* {notes[k]} */")
    lines.append(f"static const uint8_t {k}[{len(v)}] = {{")
    for i in range(0, len(v), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in v[i:i+16]) + ",")
    lines.append("};")
    lines.append("")
lines.append("#endif")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "artwork_fixture.h"), "w").write("\n".join(lines) + "\n")
print({k: len(v) for k, v in out.items()})
