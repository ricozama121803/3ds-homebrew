#!/usr/bin/env python3
"""Home Menu assets: icon.png (48x48) and banner.png (256x128). The banner jingle (audio.wav) is shared with the other games. Run: ../tools/venv/bin/python make_art.py"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FP = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def scene(w, h):
    img = Image.new("RGB", (w, h)); d = ImageDraw.Draw(img)
    for y in range(h):                                   # LA sunset sky
        t = y / (h - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip((250, 120, 60), (255, 210, 120)))
        d.line([(0, y), (w, y)], fill=c)
    base = int(h * 0.72)
    x, s = 0, 3
    while x < w:                                         # skyline
        bw = max(4, w // 16 + (s * 7) % (w // 12)); bh = h // 6 + (s * 13) % (h // 3)
        d.rectangle([x, base - bh, x + bw, base], fill=(70, 40, 70)); x += bw + 1; s += 1
    d.rectangle([0, base, w, h], fill=(50, 50, 58))      # road
    d.rectangle([0, base + (h - base) // 2 - 1, w, base + (h - base) // 2 + 1], fill=(230, 190, 40))
    return img, base

def otto(d, cx, by, k):
    d.rectangle([cx - 3 * k, by - 12 * k, cx + 3 * k, by], fill=(255, 124, 20))          # body + legs
    d.rectangle([cx - 4 * k, by - 22 * k, cx + 4 * k, by - 11 * k], fill=(255, 124, 20))
    d.rectangle([cx - 3 * k, by - 28 * k, cx + 3 * k, by - 22 * k], fill=(240, 184, 140))
    d.rectangle([cx - 3 * k, by - 31 * k, cx + 3 * k, by - 27 * k], fill=(20, 20, 24))

def car(d, x, by, k, col):
    d.rectangle([x, by - 6 * k, x + 26 * k, by - 1 * k], fill=col)
    d.polygon([(x + 6 * k, by - 6 * k), (x + 9 * k, by - 11 * k), (x + 19 * k, by - 11 * k), (x + 22 * k, by - 6 * k)], fill=(40, 70, 110))
    for wx in (x + 5 * k, x + 20 * k): d.ellipse([wx - 2 * k, by - 3 * k, wx + 2 * k, by + k], fill=(15, 15, 18))

icon, base = scene(48, 48); d = ImageDraw.Draw(icon)
car(d, 18, 42, 1, (200, 30, 30)); otto(d, 12, 42, 0.5 if False else 1)
f = ImageFont.truetype(FP, 13); d.text((3, 2), "GTO", font=f, fill=(255, 255, 255), stroke_width=1, stroke_fill=(120, 30, 0)); d.text((30, 2), "II", font=f, fill=(255, 220, 90), stroke_width=1, stroke_fill=(120, 30, 0))
icon.save(os.path.join(HERE, "icon.png"))

ban, base = scene(256, 128); d = ImageDraw.Draw(ban)
car(d, 140, 110, 3, (200, 30, 30)); car(d, 40, 100, 2, (240, 190, 20)); otto(d, 215, 116, 2)
f = ImageFont.truetype(FP, 30); d.text((128, 6), "GRAND THEFT OTTO", font=f if False else ImageFont.truetype(FP, 24), fill=(255, 255, 255), anchor="ma", stroke_width=2, stroke_fill=(120, 30, 0))
d.text((128, 34), "II", font=ImageFont.truetype(FP, 30), fill=(255, 220, 90), anchor="ma", stroke_width=2, stroke_fill=(120, 30, 0))
ban.save(os.path.join(HERE, "banner.png"))
