#!/usr/bin/env python3
"""Home Menu assets: icon.png (48x48) and banner.png (256x128). The banner jingle (audio.wav) is shared with the other games. Run: ../tools/venv/bin/python make_art.py"""
import math, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FP = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def sky(w, h):
    img = Image.new("RGB", (w, h)); d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip((38, 98, 190), (205, 225, 250)))
        d.line([(0, y), (w, y)], fill=c)
    return img

def skyline(d, w, base, seed, col, hmax):
    x = 0
    while x < w:
        bw = 8 + (seed * 7 + x * 3) % 16; bh = 10 + (seed * 13 + x * 5) % hmax
        d.rectangle([x, base - bh, x + bw, base], fill=col); x += bw + 1

def web(d, cx, cy, r, col, w=1):
    for i in range(12):
        a = i * math.pi / 6
        d.line([(cx, cy), (cx + math.cos(a) * r, cy + math.sin(a) * r)], fill=col, width=w)
    for k in range(1, 5):
        rr = r * k / 4.0; pts = []
        for i in range(12):
            a = i * math.pi / 6; q = rr * (0.86 if i % 2 else 1.0) if k < 4 else rr
            pts.append((cx + math.cos(a) * q, cy + math.sin(a) * q))
        d.line(pts + [pts[0]], fill=col, width=w)

def spider(d, cx, cy, s, col):
    d.ellipse([cx - 4 * s, cy - 5 * s, cx + 4 * s, cy + 6 * s], fill=col)
    d.ellipse([cx - 2.5 * s, cy - 9 * s, cx + 2.5 * s, cy - 4 * s], fill=col)
    for sg in (-1, 1):
        for k, (dx, dy) in enumerate([(9, -9), (11, -2), (11, 5), (8, 11)]):
            d.line([(cx + sg * 2 * s, cy + (k - 1.5) * 2 * s), (cx + sg * dx * s, cy + dy * s)], fill=col, width=max(1, int(s)))

icon = sky(48, 48); d = ImageDraw.Draw(icon)
skyline(d, 48, 48, 3, (36, 52, 92), 22)
web(d, 24, 24, 26, (255, 255, 255), 1)
d.ellipse([10, 10, 38, 38], fill=(200, 24, 36))
spider(d, 24, 24, 1.3, (10, 10, 14))
d.rectangle([0, 0, 47, 47], outline=(12, 20, 40))
icon.save(os.path.join(HERE, "icon.png"))

ban = sky(256, 128); d = ImageDraw.Draw(ban)
skyline(d, 256, 128, 5, (96, 128, 176), 60); skyline(d, 256, 128, 9, (46, 66, 112), 44)
web(d, 24, 18, 90, (255, 255, 255), 1)
f1 = ImageFont.truetype(FP, 40) if os.path.exists(FP) else ImageFont.load_default()
f2 = ImageFont.truetype(FP, 14) if os.path.exists(FP) else ImageFont.load_default()
for t, y, fnt, col in (("WEB SLINGER", 22, f1, (230, 30, 40)), ("SWING THROUGH THE CITY", 72, f2, (255, 255, 255))):
    w = d.textlength(t, font=fnt)
    d.text(((256 - w) / 2, y), t, font=fnt, fill=col, stroke_width=3, stroke_fill=(10, 16, 40))
ban.save(os.path.join(HERE, "banner.png"))
