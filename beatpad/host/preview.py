#!/usr/bin/env python3
"""Turns the PPM frames + text logs from preview.c into PNGs (text drawn with a real font, standing in for
the 3DS system font) and a contact sheet of top + bottom screens."""
import glob, os, sys
from PIL import Image, ImageDraw, ImageFont

outdir = sys.argv[1]
FONT = next((f for f in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
                         "/usr/share/fonts/google-noto/NotoSans-Bold.ttf", "/usr/share/fonts/google-droid-sans-fonts/DroidSans-Bold.ttf"]
             if os.path.exists(f)), None)

def col(c):
    return (c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255)

def render(base):
    im = Image.open(base + ".ppm").convert("RGBA")
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    lines = open(base + ".txt").read().splitlines() if os.path.exists(base + ".txt") else []
    for ln in lines:
        align, x, y, sx, sy, c, s = ln.split("\t", 6)
        size = max(6, int(round(25 * float(sy))))          # system font: ~30 px line, ~25 px glyphs at scale 1
        f = ImageFont.truetype(FONT, size)
        for i, part in enumerate(s.split("\\n") if "\\n" in s else s.split("\n")):
            w = d.textlength(part, font=f)
            xx = float(x) - (w / 2 if align == "1" else w if align == "2" else 0)
            d.text((xx, float(y) + 2 * float(sy) + i * 30 * float(sy)), part, font=f, fill=col(int(c)))
    im = Image.alpha_composite(im, layer).convert("RGB")
    im.save(base + ".png")
    return im

names = sorted({os.path.basename(p).rsplit("_", 1)[0] for p in glob.glob(outdir + "/*_T.ppm")})
for n in names:
    T = render(f"{outdir}/{n}_T"); B = render(f"{outdir}/{n}_B")
    sheet = Image.new("RGB", (400, 480), (0, 0, 0))
    sheet.paste(T, (0, 0)); sheet.paste(B, (40, 240))
    sheet.save(f"{outdir}/{n}.png")
    print(f"{outdir}/{n}.png")
