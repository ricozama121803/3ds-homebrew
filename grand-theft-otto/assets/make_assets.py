#!/usr/bin/env python3
"""Home Menu assets: icon.png (48x48), banner.png (256x128), audio.wav (banner jingle). Needs gfx/ from make_art.py."""
import math, os, struct, wave
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
G = lambda n: Image.open(os.path.join(HERE, "gfx", n + ".png")).convert("RGBA")
ORANGE, NAVY, BLACK = (246, 128, 30), (22, 64, 124), (14, 14, 18)

def street_scene(w, h, scale):
    """A little top-down street: asphalt with a centre line, sidewalks, and building roofs."""
    tw, th = w // scale + 2, h // scale + 2
    base = Image.new("RGBA", (tw * 16, th * 16))
    for ty in range(th):
        for tx in range(tw):
            if ty in (2, 3, 4, 5): name = "road_hy_b" if ty == 3 else "road_hy_t" if ty == 4 else "road"
            elif ty in (1, 6): name = "sidewalk0"
            elif ty < 1: name = "roof_tan0" if (tx // 4) % 2 else "roof_brown0"
            else: name = "roof_blue0" if (tx // 5) % 2 else "roof_gray0"
            base.paste(G(name), (tx * 16, ty * 16))
    return base.resize((base.width * scale, base.height * scale), Image.NEAREST)

def star(d, cx, cy, r, fill):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    d.polygon(pts, fill=fill, outline=BLACK)

# ---- icon: bold GTO lettering on a night-city gradient
icon = Image.new("RGBA", (48, 48))
d = ImageDraw.Draw(icon)
for y in range(48):
    t = y / 47
    d.line([(0, y), (47, y)], fill=(int(22 + 200 * t * t), int(30 + 60 * t * t), int(70 - 30 * t), 255))
for x0, w, h in [(2, 7, 18), (11, 6, 26), (19, 8, 14), (30, 6, 22), (38, 8, 30)]:         # skyline
    d.rectangle([x0, 48 - h, x0 + w, 48], fill=(14, 16, 30, 255))
    for wy in range(48 - h + 3, 46, 5):
        for wx in range(x0 + 1, x0 + w, 3): d.point((wx, wy), fill=(255, 210, 110, 255))
fp = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
f = ImageFont.truetype(fp, 18) if os.path.exists(fp) else ImageFont.load_default()
tw = d.textlength("GTO", font=f)
x, y = (48 - tw) / 2, 9
for ox in range(-2, 3):
    for oy in range(-2, 3): d.text((x + ox, y + oy), "GTO", font=f, fill=BLACK)
d.text((x, y), "GTO", font=f, fill=ORANGE)
d.text((x, y - 1), "GTO", font=f, fill=(255, 190, 90, 255))
for i in range(3): star(d, 14 + i * 10, 41, 4, (255, 220, 60, 255))
icon.convert("RGB").save(os.path.join(HERE, "icon.png"))

# ---- banner
ban = street_scene(256, 128, 3).crop((0, 0, 256, 128)).convert("RGBA")
ov = Image.new("RGBA", ban.size, (10, 20, 50, 110)); ban.alpha_composite(ov)
d = ImageDraw.Draw(ban)
d.ellipse([150, 60, 240, 116], fill=(176, 16, 28, 190))
for (x, y, r) in [(140, 90, 6), (246, 72, 5), (220, 52, 4), (160, 116, 5)]: d.ellipse([x, y, x + r, y + r], fill=(190, 20, 30, 220))
ban.alpha_composite(G("player_2_1").resize((84, 84), Image.NEAREST), (150, 30))
font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
if os.path.exists(font_path):
    for i, word in enumerate(("GRAND", "THEFT", "OTTO")):
        f = ImageFont.truetype(font_path, 30)
        for ox in range(-2, 3):
            for oy in range(-2, 3): d.text((10 + ox, 6 + i * 34 + oy), word, font=f, fill=BLACK)
        d.text((10, 6 + i * 34), word, font=f, fill=ORANGE)
for i in range(5): star(d, 26 + i * 22, 116, 8, (255, 220, 60, 255))
ban.convert("RGB").save(os.path.join(HERE, "banner.png"))

# ---- jingle: a police-siren wail over a bass line, ~2.5 s
RATE = 32000
frames = bytearray()
n = int(RATE * 2.5)
for i in range(n):
    t = i / RATE
    wail = 760 + 260 * math.sin(2 * math.pi * 1.6 * t)                      # rising and falling
    siren = math.sin(2 * math.pi * wail * t) * 0.6 + math.sin(2 * math.pi * wail * 2 * t) * 0.15
    bass = math.copysign(1, math.sin(2 * math.pi * (55 if (int(t * 4) % 2 == 0) else 73.4) * t)) * 0.35
    env = min(1.0, t / 0.05) * min(1.0, (2.5 - t) / 0.4)
    v = int(9000 * env * (0.7 * siren + 0.5 * bass))
    frames += struct.pack("<hh", v, v)
w = wave.open(os.path.join(HERE, "audio.wav"), "wb")
w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(bytes(frames)); w.close()
print("home menu assets generated")
