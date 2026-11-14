#!/usr/bin/env python3
"""Draws the HOME Menu icon (48x48) and banner (256x128) and cuts the 3 s banner jingle from a song.
    python3 tools/make_art.py [song dir]      (default romfs/songs/alv)"""
import os, sys, struct
from PIL import Image, ImageDraw, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
assets = os.path.join(here, "..", "assets")
LANES = [(0x3f, 0xd9, 0x6a), (0xf0, 0x48, 0x48), (0xf5, 0xd0, 0x3a), (0x3d, 0x8c, 0xf5), (0xff, 0x9a, 0x2e), (0xc1, 0x60, 0xf0)]

def font(size):
    for f in ["/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/google-noto/NotoSans-Bold.ttf"]:
        if os.path.exists(f): return ImageFont.truetype(f, size)
    return ImageFont.load_default()

def highway(w, h, scale):
    """Perspective highway with gems, drawn at 4x and downsampled for smooth edges."""
    W, H = w * 4, h * 4
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(18 + 10 * t), int(10 + 4 * t), int(46 - 20 * t)))
    cx, top, bot = W / 2, H * 0.05, H * 0.92
    hwb, hwt = W * 0.46 * scale, W * 0.10 * scale
    d.polygon([(cx - hwt, top), (cx + hwt, top), (cx + hwb, bot), (cx - hwb, bot)], fill=(28, 24, 52))
    for i in range(1, 6):
        f = i / 6
        d.line([(cx - hwt + 2 * hwt * f, top), (cx - hwb + 2 * hwb * f, bot)], fill=(70, 66, 120), width=max(1, W // 200))
    for side in (-1, 1):
        d.line([(cx + side * hwt, top), (cx + side * hwb, bot)], fill=(208, 112, 255), width=max(2, W // 80))
    def gem(lane, z, r):
        s = 1 / (1 + 3 * z)
        y = top + (bot - top) * (s - 0.25) / 0.75
        hw = hwt + (hwb - hwt) * (s - 0.25) / 0.75
        x = cx - hw + 2 * hw * (lane + 0.5) / 6
        rx, ry = r * s, r * s * 0.5
        c = LANES[lane]
        d.ellipse([x - rx - 3, y - ry - 1, x + rx + 3, y + ry + 5], fill=(0, 0, 0))
        d.ellipse([x - rx, y - ry, x + rx, y + ry], fill=tuple(int(v * 0.75) for v in c))
        d.ellipse([x - rx * 0.85, y - ry * 0.95, x + rx * 0.85, y + ry * 0.5], fill=c)
        d.ellipse([x - rx * 0.5, y - ry * 0.8, x + rx * 0.3, y - ry * 0.3], fill=tuple(min(255, v + 110) for v in c))
    return im, d, gem, (cx, bot, hwb)

# icon
im, d, gem, (cx, bot, hwb) = highway(48, 48, 1.0)
for lane, z in [(0, 0.05), (4, 0.25), (2, 0.5), (5, 0.8), (1, 0.95)]:
    gem(lane, z, 30)
im.resize((48, 48), Image.LANCZOS).save(os.path.join(assets, "icon.png"))

# banner
im, d, gem, _ = highway(256, 128, 0.62)
for lane, z in [(0, 0.08), (3, 0.08), (2, 0.35), (5, 0.55), (1, 0.75), (4, 0.95)]:
    gem(lane, z, 70)
f = font(100)
txt = "BEATPAD"
tw = d.textlength(txt, font=f)
x = (256 * 4 - tw) / 2
for i, ch in enumerate(txt):
    c = LANES[i % 6]
    d.text((x + 6, 14 + 6), ch, font=f, fill=(0, 0, 0))
    d.text((x, 14), ch, font=f, fill=tuple(min(255, v + 50) for v in c))
    x += d.textlength(ch, font=f)
im.resize((256, 128), Image.LANCZOS).save(os.path.join(assets, "banner.png"))

# banner jingle: 3 s from the song preview, faded, 16-bit stereo 32 kHz
song = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "romfs", "songs", "alv")
info = dict(l.strip().split("=", 1) for l in open(os.path.join(song, "info.txt")) if "=" in l)
start = int(info.get("preview_ms", 0)) * 32 * 4
n = 3 * 32000
if os.path.exists(os.path.join(song, "audio.bpa")):
    sys.path.insert(0, here); import bpa
    raw = bpa.decode(open(os.path.join(song, "audio.bpa"), "rb").read()).tobytes()[start: start + n * 4]
else:
    with open(os.path.join(song, "audio.pcm"), "rb") as fpcm:
        fpcm.seek(start); raw = fpcm.read(n * 4)
samples = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
fade = 8000
for i in range(len(samples) // 2):
    g = min(1.0, i / 800, (len(samples) // 2 - i) / fade)
    samples[2 * i] = int(samples[2 * i] * g); samples[2 * i + 1] = int(samples[2 * i + 1] * g)
data = struct.pack("<%dh" % len(samples), *samples)
with open(os.path.join(assets, "audio.wav"), "wb") as w:
    w.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, 32000, 128000, 4, 16))
    w.write(b"data" + struct.pack("<I", len(data)) + data)
print("wrote icon.png, banner.png, audio.wav")
