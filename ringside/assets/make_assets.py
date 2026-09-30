#!/usr/bin/env python3
"""Home Menu assets: icon.png (48x48), banner.png (256x128), audio.wav (banner jingle).
The art is cropped from a frame of the PC preview renderer (host/preview.sh), so it shows the real fighters."""
import math, os, subprocess, struct, tempfile, wave
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GOLD, BLACK = (255, 214, 64), (10, 10, 14)

# ---- render preview frames (hi-res) and pick the shots
out = os.path.join(tempfile.gettempdir(), "ringside-assets")
subprocess.run([os.path.join(ROOT, "host", "preview.sh"), out, "hi"], check=True, stdout=subprocess.DEVNULL)
shot = Image.open(os.path.join(out, "02_rear_hook.png")).convert("RGB")      # 800x480

def darken_edges(im, strength=0.55):
    w, h = im.size
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    m = np.clip(1.0 - strength * np.clip(d - 0.55, 0, 1) ** 1.3 * 1.8, 0.35, 1.0)
    a = np.asarray(im).astype(np.float32) * m[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))

# ---- icon: tight crop on the two fighters
icon = darken_edges(shot.crop((225, 70, 625, 470))).resize((48, 48), Image.LANCZOS)
icon.save(os.path.join(HERE, "icon.png"))

# ---- banner
ban = shot.crop((60, 30, 740, 370)).resize((256, 128), Image.LANCZOS)
ban = darken_edges(ban, 0.7).convert("RGBA")
grad = Image.new("RGBA", ban.size, (0, 0, 0, 0))
gd = ImageDraw.Draw(grad)
for y in range(ban.height):
    a = int(170 * (y / ban.height) ** 2)
    gd.line([(0, y), (ban.width, y)], fill=(6, 6, 14, a))
ban.alpha_composite(grad)
d = ImageDraw.Draw(ban)
font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
if os.path.exists(font_path):
    f = ImageFont.truetype(font_path, 34)
    for ox in range(-2, 3):
        for oy in range(-2, 3):
            d.text((10 + ox, 84 + oy), "RINGSIDE", font=f, fill=BLACK)
    d.text((10, 84), "RINGSIDE", font=f, fill=GOLD)
    f2 = ImageFont.truetype(font_path, 11)
    d.text((12, 70), "3D KICKBOXING", font=f2, fill=(230, 230, 240))
ban.convert("RGB").save(os.path.join(HERE, "banner.png"))

# ---- jingle: three boxing-bell dings over a rising crowd murmur, ~2.6 s
RATE = 32000
n = int(RATE * 2.6)
t = np.arange(n) / RATE
sig = np.zeros(n)
for k, t0 in enumerate((0.05, 0.62, 1.19)):
    tt = np.clip(t - t0, 0, None)
    env = np.where(t >= t0, np.exp(-tt * 3.2), 0.0)
    for f0, a in ((880, 1.0), (880 * 2.32, 0.55), (880 * 3.03, 0.4), (880 * 4.17, 0.25)):
        sig += a * env * np.sin(2 * np.pi * f0 * tt) * 0.28
rng = np.random.default_rng(3)
noise = rng.standard_normal(n)
kernel = np.ones(48) / 48                                                     # low-passed noise = crowd wash
crowd = np.convolve(noise, kernel, mode="same") * (0.10 + 0.20 * np.clip(t / 2.0, 0, 1))
sig += crowd
sig *= np.minimum(1.0, t / 0.01) * np.minimum(1.0, (n / RATE - t) / 0.35)
sig = np.clip(sig / max(1e-6, np.abs(sig).max()) * 0.85, -1, 1)
w = wave.open(os.path.join(HERE, "audio.wav"), "wb")
w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
pcm = (sig * 32767).astype("<i2")
w.writeframes(np.column_stack([pcm, pcm]).tobytes())
w.close()
print("home menu assets generated")
