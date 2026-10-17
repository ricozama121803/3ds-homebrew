#!/usr/bin/env python3
"""Turns the PPM frames + text logs from preview.c into PNGs: the 3D frame, the HUD frame (text drawn with a real font) and the bottom screen."""
import sys, glob, os
from PIL import Image, ImageDraw, ImageFont

outdir = sys.argv[1]
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
        size = max(6, int(round(24 * float(sy))))
        f = ImageFont.truetype(FONT, size)
        w = d.textlength(s, font=f) * (float(sx) / float(sy))
        x = float(x) - (w / 2 if center == "1" else 0)
        d.text((x, float(y)), s, font=f, fill=col(int(c)))
    return Image.alpha_composite(im, layer).convert("RGB")

for p in sorted(glob.glob(outdir + "/*.ppm")):
    base = p[:-4]
    if base.endswith("_hud") or base.endswith("_bot"):
        continue
    Image.open(p).save(base + ".png")
    if os.path.exists(base + "_hud.ppm"):
        top = render(base + "_hud"); bot = render(base + "_bot")
        sheet = Image.new("RGB", (400 * 2 + 320 + 20, 480), (30, 30, 30))
        sheet.paste(top.resize((800, 480), Image.LANCZOS), (0, 0)); sheet.paste(bot.resize((320, 240)), (820, 0))
        sheet.save(base + "_screens.png")
