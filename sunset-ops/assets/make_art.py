#!/usr/bin/env python3
"""Home Menu assets: icon.png (48x48), banner.png (256x128), audio.wav (banner jingle). Run: ../tools/venv/bin/python make_art.py"""
import math, os, struct, wave
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FP = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def sunset(w, h):
    img = Image.new("RGB", (w, h)); d = ImageDraw.Draw(img)
    stops = [(0.0, (46, 70, 130)), (0.45, (140, 120, 170)), (0.7, (250, 150, 90)), (1.0, (255, 214, 140))]
    for y in range(h):
        t = y / (h - 1)
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                u = (t - t0) / (t1 - t0); c = tuple(int(a + (b - a) * u) for a, b in zip(c0, c1)); break
        d.line([(0, y), (w, y)], fill=c)
    return img

def soldier(d, x, y, s, col):
    d.rectangle([x - 5 * s, y - 24 * s, x + 5 * s, y - 8 * s], fill=col)          # torso
    d.ellipse([x - 4 * s, y - 33 * s, x + 4 * s, y - 24 * s], fill=col)           # head
    d.rectangle([x - 5 * s, y - 8 * s, x - 1 * s, y + 10 * s], fill=col); d.rectangle([x + 1 * s, y - 8 * s, x + 5 * s, y + 10 * s], fill=col)
    d.polygon([(x + 3 * s, y - 20 * s), (x + 30 * s, y - 25 * s), (x + 30 * s, y - 21 * s), (x + 3 * s, y - 14 * s)], fill=col)   # rifle

def crosshair(d, cx, cy, r, gap, col, w=2):
    for sx in (-1, 1):
        for sy in (-1, 1):
            d.line([(cx + sx * gap, cy + sy * gap), (cx + sx * r, cy + sy * r)], fill=col, width=w)

# icon
icon = sunset(48, 48); d = ImageDraw.Draw(icon)
d.ellipse([20, 22, 40, 42], fill=(255, 238, 190))
d.rectangle([0, 38, 48, 48], fill=(26, 48, 70))
for x0, w, h in [(0, 9, 14), (10, 7, 20), (31, 8, 16), (40, 8, 24)]: d.rectangle([x0, 38 - h, x0 + w, 38], fill=(40, 34, 52))
crosshair(d, 24, 20, 17, 6, (255, 255, 255), 2)
f = ImageFont.truetype(FP, 11) if os.path.exists(FP) else ImageFont.load_default()
d.text((7, 34), "OPS", font=f, fill=(255, 255, 255), stroke_width=1, stroke_fill=(20, 20, 30))
icon.save(os.path.join(HERE, "icon.png"))

# banner
ban = sunset(256, 128); d = ImageDraw.Draw(ban)
d.ellipse([140, 54, 196, 110], fill=(255, 240, 200))
d.rectangle([0, 96, 256, 128], fill=(24, 44, 70))
for i in range(0, 256, 6): d.line([(i, 98 + (i * 7) % 9), (i + 3, 98 + (i * 7) % 9)], fill=(200, 140, 110))
for x0, w, h in [(0, 20, 40), (22, 14, 60), (38, 24, 34), (190, 16, 46), (208, 22, 70), (232, 24, 38)]: d.rectangle([x0, 96 - h, x0 + w, 96], fill=(46, 38, 60))
soldier(d, 70, 96, 1.6, (18, 18, 26)); soldier(d, 120, 100, 1.2, (18, 18, 26))
f1 = ImageFont.truetype(FP, 34) if os.path.exists(FP) else ImageFont.load_default()
f2 = ImageFont.truetype(FP, 14) if os.path.exists(FP) else ImageFont.load_default()
for t, y, fnt, col in (("SUNSET OPS", 8, f1, (255, 240, 205)), ("FIRST-PERSON TEAM DEATHMATCH", 52, f2, (255, 255, 255))):
    w = d.textlength(t, font=fnt)
    d.text(((256 - w) / 2, y), t, font=fnt, fill=col, stroke_width=2, stroke_fill=(30, 20, 40))
ban.save(os.path.join(HERE, "banner.png"))

# jingle: a short rising brass-like chord sequence
sr = 32000
notes = [(220.0, 0.0), (277.2, 0.35), (329.6, 0.7), (440.0, 1.05), (554.4, 1.4)]
n = int(sr * 2.6); buf = [0.0] * n
for f0, t0 in notes:
    for i in range(int(sr * 1.2)):
        t = i / sr; k = int((t0 + t) * sr)
        if k >= n: break
        env = min(1, t * 40) * math.exp(-t * 2.6)
        buf[k] += env * sum(math.sin(2 * math.pi * f0 * h * t) / h for h in (1, 2, 3, 4)) * 0.16
with wave.open(os.path.join(HERE, "audio.wav"), "wb") as wf:
    wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
    wf.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 30000)) for v in buf))
print("icon, banner, audio written")
