#!/usr/bin/env python3
"""Turns the PPM frames + text logs from preview.c into PNGs (drawing the text with a real font)."""
import sys, glob, os
from PIL import Image, ImageDraw, ImageFont

outdir = sys.argv[1]

if len(sys.argv) > 2 and sys.argv[2] == "--raw":
    # sprites for the C preview: 8-byte header (w, h) followed by raw RGBA
    import struct
    gfx = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "gfx")
    for f in glob.glob(gfx + "/*.png"):
        im = Image.open(f).convert("RGBA")
        with open(os.path.join(outdir, os.path.basename(f)[:-4] + ".rgba"), "wb") as o:
            o.write(struct.pack("<ii", *im.size)); o.write(im.tobytes())
    sys.exit(0)

FONT = None
for cand in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
    if os.path.exists(cand):
        FONT = cand; break

def col(c):
    return (c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255)

def render(base):
    im = Image.open(base + ".ppm").convert("RGBA")
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    try:
        lines = open(base + ".txt").read().splitlines()
    except FileNotFoundError:
        lines = []
    for ln in lines:
        center, x, y, sx, sy, c, s = ln.split("\t", 6)
        size = max(6, int(round(24 * float(sy))))          # 3DS system font is ~ 24-30 px tall at scale 1
        f = ImageFont.truetype(FONT, size)
        w = d.textlength(s, font=f) * (float(sx) / float(sy))
        x = float(x) - (w / 2 if center == "1" else 0)
        d.text((x, float(y)), s, font=f, fill=col(int(c)))
    im = Image.alpha_composite(im, layer).convert("RGB")
    im.save(base + ".png")
    return im

names = sorted({os.path.basename(p).rsplit("_", 1)[0] for p in glob.glob(outdir + "/*_L.ppm")})
for n in names:
    L = render(f"{outdir}/{n}_L")
    B = render(f"{outdir}/{n}_B")
    sheet = Image.new("RGB", (400 + 320 + 20, 240), (30, 30, 30))
    sheet.paste(L, (0, 0)); sheet.paste(B, (420, 0))
    if os.path.exists(f"{outdir}/{n}_R.ppm"):
        R = render(f"{outdir}/{n}_R")
        # red/cyan anaglyph so the 3D depth is visible in a flat image
        import numpy as np
        a, b = np.array(L), np.array(R)
        ana = np.dstack([a[..., 0], b[..., 1], b[..., 2]])
        Image.fromarray(ana).save(f"{outdir}/{n}_anaglyph.png")
    sheet.save(f"{outdir}/{n}.png")
    print(n)
