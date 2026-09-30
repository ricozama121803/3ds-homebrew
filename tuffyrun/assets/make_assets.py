#!/usr/bin/env python3
"""Generates icon.png (48x48), banner.png (256x128) and audio.wav (banner jingle) for Tuffy Run."""
import math, struct, wave, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
NAVY, NAVY2, ORANGE = (20, 62, 122), (10, 28, 66), (245, 126, 28)
GRAY, GRAYD, BLACK, WHITE = (226, 226, 228), (168, 168, 176), (14, 14, 16), (255, 255, 255)
SS = 8   # supersampling factor for smooth edges

def elephant_face(size):
    """Front view of an angry elephant with a navy shirt collar, drawn on a transparent square."""
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = S / 100.0
    def ell(cx, cy, rx, ry, fill, w=3.2):
        d.ellipse([(cx - rx) * u - w * u, (cy - ry) * u - w * u, (cx + rx) * u + w * u, (cy + ry) * u + w * u], fill=BLACK)
        d.ellipse([(cx - rx) * u, (cy - ry) * u, (cx + rx) * u, (cy + ry) * u], fill=fill)
    # shirt
    ell(50, 104, 40, 22, NAVY)
    d.rectangle([12 * u, 90 * u, 88 * u, 93 * u], fill=ORANGE)
    # ears, head
    ell(17, 42, 17, 27, GRAY); ell(83, 42, 17, 27, GRAY)
    ell(17, 42, 9, 16, GRAYD, 0); ell(83, 42, 9, 16, GRAYD, 0)
    ell(50, 42, 30, 32, GRAY)
    # trunk
    d.rounded_rectangle([(50 - 9) * u - 3.2 * u, 46 * u - 3.2 * u, (50 + 9) * u + 3.2 * u, 88 * u + 3.2 * u], radius=9 * u, fill=BLACK)
    d.rounded_rectangle([(50 - 9) * u, 46 * u, (50 + 9) * u, 88 * u], radius=9 * u, fill=GRAY)
    for y in (58, 68, 78):
        d.line([(43 * u, y * u), (57 * u, y * u)], fill=GRAYD, width=int(1.6 * u))
    # angry eyes and brows
    for sx in (-1, 1):
        ex = 50 + sx * 12
        d.ellipse([(ex - 4) * u, 36 * u, (ex + 4) * u, 46 * u], fill=WHITE, outline=BLACK, width=int(1.4 * u))
        d.ellipse([(ex - 2 - sx * 0.5) * u, 39 * u, (ex + 2 - sx * 0.5) * u, 45 * u], fill=BLACK)
        d.line([((ex + sx * 8) * u), 29 * u, ((ex - sx * 5) * u), 37 * u], fill=BLACK, width=int(3.2 * u))
    return im.resize((size, size), Image.LANCZOS)

def gradient(w, h, top, bottom):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        t = y / (h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return im

# ---- icon (48x48) ----
icon = gradient(48, 48, (36, 90, 170), NAVY2)
d = ImageDraw.Draw(icon)
d.rectangle([0, 44, 47, 47], fill=ORANGE)
face = elephant_face(44)
icon.paste(face, (2, 3), face)
icon.save(os.path.join(HERE, "icon.png"))

# ---- banner (256x128) ----
ban = gradient(256, 128, (40, 96, 176), NAVY2)
d = ImageDraw.Draw(ban)
d.rectangle([0, 118, 255, 127], fill=ORANGE)
face = elephant_face(112)
ban.paste(face, (10, 8), face)
font_path = next((p for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"] if os.path.exists(p)), None)
if font_path:
    big = ImageFont.truetype(font_path, 31)
    for i, word in enumerate(("TUFFY", "RUN")):
        x, y = 126, 24 + i * 38
        for ox in (-2, -1, 0, 1, 2):
            for oy in (-2, -1, 0, 1, 2):
                d.text((x + ox, y + oy), word, font=big, fill=BLACK)
        d.text((x, y), word, font=big, fill=ORANGE)
ban.save(os.path.join(HERE, "banner.png"))

# ---- banner jingle (about 2.4 s, 32 kHz stereo) ----
RATE = 32000
notes = [(523.25, 0.16), (659.25, 0.16), (783.99, 0.16), (1046.5, 0.32),
         (783.99, 0.16), (1046.5, 0.16), (1318.5, 0.6)]
frames = bytearray()
for freq, dur in notes:
    n = int(RATE * dur)
    for i in range(n):
        t = i / RATE
        env = min(1.0, i / 300) * math.exp(-3.2 * t / dur)
        tri = 2 / math.pi * math.asin(math.sin(2 * math.pi * freq * t))
        v = int(9000 * env * (0.75 * tri + 0.25 * math.sin(2 * math.pi * freq * 2 * t)))
        frames += struct.pack("<hh", v, v)
w = wave.open(os.path.join(HERE, "audio.wav"), "wb")
w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
w.writeframes(bytes(frames)); w.close()
print("assets generated")
