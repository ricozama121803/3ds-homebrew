#!/usr/bin/env python3
"""Draws the HOME Menu icon (48x48) and banner (256x128) and synthesises the 3 s banner jingle.
    python3 tools/make_art.py"""
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

# banner jingle: an original 3 s riff, synthesised (kick / snare / hats under a rising square-wave arpeggio
# that lands on a held chord), 16-bit stereo 32 kHz
import numpy as np
SR, BPM = 32000, 128
N = 3 * SR
t = np.arange(N) / SR
beat = 60 / BPM
mix = np.zeros((N, 2))

def place(sig, at, pan=0.0, gain=1.0):
    i = int(at * SR)
    if i >= N: return
    sig = sig[: N - i] * gain
    mix[i: i + len(sig), 0] += sig * (1 - max(0, pan))
    mix[i: i + len(sig), 1] += sig * (1 + min(0, pan))

def env(n, a=0.004, d=0.18):
    tt = np.arange(n) / SR
    return np.minimum(1, tt / a) * np.exp(-tt / d)

def square(f, dur, duty=0.5, d=0.18):
    n = int(dur * SR); tt = np.arange(n) / SR
    w = np.where((tt * f) % 1 < duty, 1.0, -1.0) + 0.5 * np.where((tt * f * 2.003) % 1 < 0.5, 1.0, -1.0)
    k = np.ones(9) / 9                                       # soften the edges a little
    return np.convolve(w, k, "same") * env(n, d=d) * 0.5

def kick():
    n = int(0.25 * SR); tt = np.arange(n) / SR
    f = 50 + 110 * np.exp(-tt * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 14)

rng = np.random.default_rng(3)
def noise(dur, d, hp=True):
    n = int(dur * SR); x = rng.uniform(-1, 1, n)
    if hp: x = np.diff(np.concatenate([[0], x]))
    return x * env(n, 0.001, d)

midi = lambda m: 440 * 2 ** ((m - 69) / 12)
# drums: four-on-the-floor kick, snare on 2 and 4, 8th hats, a fill into the last bar's downbeat
for b in range(6):
    place(kick(), b * beat, gain=0.9)
    if b % 2 == 1: place(noise(0.2, 0.07, False) * 0.5 + square(190, 0.2, d=0.05) * 0.3, b * beat, gain=0.55)
    for h in range(2): place(noise(0.05, 0.012), (b + h / 2) * beat, pan=0.3 if h else -0.3, gain=0.25)
for k, x in enumerate([5.5, 5.625, 5.75, 5.875]): place(noise(0.1, 0.04, False), x * beat, gain=0.25 + 0.08 * k)
# lead: rising arpeggio in 16ths, two bars of I - vi - IV - V feel, then a held major chord
arp = [60, 64, 67, 72, 57, 60, 64, 69, 53, 57, 60, 65, 55, 59, 62, 67, 60, 64, 67, 72, 74, 76, 79, 84]
for i, m in enumerate(arp):
    place(square(midi(m + 12), 0.2, duty=0.25, d=0.09), i * beat / 4, pan=0.25 * np.sin(i), gain=0.32)
end = 6 * beat
for m, pan in ((60, -0.4), (64, 0.0), (67, 0.4), (72, 0.0)):
    place(square(midi(m + 12), 3 - end, duty=0.5, d=0.6), end - 0.75 * beat, pan=pan, gain=0.2)
place(square(midi(36), 1.0, duty=0.5, d=0.35), end - 0.75 * beat, gain=0.45)          # bass hit on the chord
# bass line under the arpeggio
for i, m in enumerate([36, 36, 33, 33, 29, 29, 31, 31]):
    place(square(midi(m), beat / 2, d=0.12), i * beat / 2, gain=0.35)
# master: fade the tail, normalise to -1 dBFS
fade = np.minimum(1, (N - np.arange(N)) / (0.35 * SR))
mix *= fade[:, None]
mix *= 0.89 / np.abs(mix).max()
data = (mix * 32767).astype("<i2").tobytes()
with open(os.path.join(assets, "audio.wav"), "wb") as w:
    w.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, SR, SR * 4, 4, 16))
    w.write(b"data" + struct.pack("<I", len(data)) + data)
print("wrote icon.png, banner.png, audio.wav")
