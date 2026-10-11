#!/usr/bin/env python3
"""Procedural textures for the 3D view (power-of-two, tileable): assets/gfx/<name>.png. Run: ../tools/venv/bin/python make_textures.py"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "gfx")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)

def save(name, arr):
    arr = np.clip(arr, 0, 255).astype(np.uint8)
    mode = "RGBA" if arr.shape[2] == 4 else "RGB"
    Image.fromarray(arr, mode).save(os.path.join(OUT, name + ".png"))

def noise(n, amp, blur=0):
    a = rng.normal(0, amp, (n, n))
    if blur:
        img = Image.fromarray(((a / (amp * 6) + 0.5).clip(0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur))
        a = (np.asarray(img).astype(float) / 255 - 0.5) * amp * 6
    return a

def base(n, rgb):
    return np.ones((n, n, 3)) * np.array(rgb, float)

# white (flat colour)
save("white", np.full((8, 8, 3), 255))

# asphalt: dark grey-brown grain with a few cracks and lighter patches
n = 128
a = base(n, (116, 112, 110))
for c in range(3): a[:, :, c] += noise(n, 6) + noise(n, 7, 2)[:, :] * 1.5
img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
d = ImageDraw.Draw(img)
for _ in range(7):
    x, y = rng.integers(0, n, 2); pts = [(x, y)]
    for _ in range(6):
        x = (x + rng.integers(-9, 10)) % n; y = (y + rng.integers(4, 14)) % n; pts.append((x, y))
    d.line(pts, fill=(88, 86, 86), width=1)
save("asphalt", np.asarray(img))

# deck: teak planks running along u, dark seams, wood grain
n = 128
a = np.zeros((n, n, 3))
plank = 16
for i in range(n // plank):
    tone = rng.uniform(0.88, 1.08)
    col = np.array((176, 128, 80)) * tone
    a[i * plank:(i + 1) * plank, :, :] = col
    a[i * plank + 0, :, :] *= 0.45; a[i * plank + plank - 1, :, :] *= 0.8
    for _ in range(3):
        xs = rng.integers(0, n)
        a[i * plank + 1:(i + 1) * plank - 1, xs, :] *= 0.55
    grain = noise(n, 1)[:plank - 2, :] if False else rng.normal(0, 5, (plank - 2, n))
    for c in range(3): a[i * plank + 1:i * plank + plank - 1, :, c] += grain
    a[i * plank + 1:i * plank + plank - 1, :, :] += (np.sin(np.arange(n) * 0.19 + i * 2.3) * 5)[None, :, None]
save("deck", a)

# concrete: pale, speckled, with expansion joints
n = 128
a = base(n, (178, 176, 170))
for c in range(3): a[:, :, c] += noise(n, 6) + noise(n, 5, 3) * 1.6
a[0, :, :] *= 0.7; a[:, 0, :] *= 0.7; a[64, :, :] *= 0.85
save("concrete", a)

# panel: white modern cladding, subtle horizontal reveals and a faint vertical rhythm
n = 128
a = base(n, (236, 232, 224))
for y in range(0, n, 32): a[y, :, :] *= 0.80; a[y + 1, :, :] *= 0.92
for x in range(0, n, 64): a[:, x, :] *= 0.9
for c in range(3): a[:, :, c] += noise(n, 2.5) + noise(n, 3, 4) * 1.5
save("panel", a)

# window: a single pane (stretched once over the quad): blue-teal glass with reflection, dark frame, central mullion
n = 128
img = Image.new("RGB", (n, n), (30, 34, 40)); d = ImageDraw.Draw(img)
for y in range(8, n - 8):
    t = (y - 8) / (n - 16)
    c = (int(110 - 60 * t), int(150 - 60 * t), int(176 - 50 * t))
    d.line([(8, y), (n - 9, y)], fill=c)
for k in range(-n, n, 22):
    d.polygon([(8 + k, n - 9), (24 + k, n - 9), (60 + k, 8), (44 + k, 8)], fill=(150, 185, 205))
img2 = img.copy(); d2 = ImageDraw.Draw(img2)
d2.rectangle([n // 2 - 2, 8, n // 2 + 1, n - 9], fill=(34, 38, 44))
d2.rectangle([8, n // 2 - 1, n - 9, n // 2], fill=(34, 38, 44))
save("window", np.asarray(Image.blend(img, img2, 1.0)))

# stucco: warm cream, speckled
n = 128
a = base(n, (232, 218, 188))
for c in range(3): a[:, :, c] += noise(n, 7) + noise(n, 5, 2) * 1.5
save("stucco", a)

# container: corrugated metal (vertical ribs along u)
n = 128
a = np.zeros((n, n, 3))
for x in range(n):
    s = 0.78 + 0.22 * np.sin(x / 4.0 * np.pi)
    a[:, x, :] = np.array((190, 190, 186)) * s
a[:6, :, :] *= 0.6; a[-6:, :, :] *= 0.6
for c in range(3): a[:, :, c] += noise(n, 5) + noise(n, 4, 3) * 1.4
save("container", a)

# wood: vertical boards (crates / fences / doors)
n = 128
a = np.zeros((n, n, 3))
bw = 21
for i in range(0, n, bw):
    tone = rng.uniform(0.85, 1.1)
    a[:, i:i + bw, :] = np.array((184, 146, 98)) * tone
    a[:, i, :] *= 0.5
    for _ in range(2):
        y0 = rng.integers(0, n); a[y0:y0 + 3, i + 3:i + bw - 3, :] *= 0.7
    a[:, i:i + bw, :] += (np.sin(np.arange(n) * 0.35 + i)[:, None, None] * 6)
for c in range(3): a[:, :, c] += noise(n, 5)
a[:6, :, :] *= 0.6; a[-6:, :, :] *= 0.6
save("wood", a)

# roof: tar and gravel
n = 128
a = base(n, (120, 116, 112))
for c in range(3): a[:, :, c] += noise(n, 14) + noise(n, 8, 1) * 1.3
save("roof", a)

# grass / hedge
n = 128
a = base(n, (112, 162, 78))
for c in range(3): a[:, :, c] += noise(n, 14) * (0.8 if c != 1 else 1.2) + noise(n, 9, 2) * 1.5
save("grass", a)

# sand: warm desert ground with ripples, grit and the odd pebble
n = 256
a = base(n, (204, 170, 120))
nz = noise(n, 1.0, 3)
for c, k in enumerate((10, 9, 8)): a[:, :, c] += nz * k * 4
rip = np.sin((np.arange(n)[None, :] * 0.8 + np.arange(n)[:, None] * 0.35) * 2 * np.pi / 32 + noise(n, 3, 5) * 0.08)
for c, k in enumerate((7, 6, 5)): a[:, :, c] += rip * k
for c, k in enumerate((8, 7, 6)): a[:, :, c] += (rng.random((n, n)) - 0.5) * k
peb = (rng.random((n, n)) > 0.9985).astype(float)
peb = np.asarray(Image.fromarray((peb * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.0))).astype(float) / 255
for c, k in enumerate((-60, -62, -55)): a[:, :, c] += peb * k * 3
save("sand", a)

# glow: white radial falloff in alpha (particles, shadows, sun)
n = 64
yy, xx = np.mgrid[0:n, 0:n]
r = np.sqrt((xx - (n - 1) / 2) ** 2 + (yy - (n - 1) / 2) ** 2) / (n / 2)
al = np.clip(1 - r, 0, 1) ** 1.6
a = np.zeros((n, n, 4)); a[:, :, :3] = 255; a[:, :, 3] = al * 255
save("glow", a)

# cloud: elongated soft streaks, alpha only
n = 128
yy, xx = np.mgrid[0:n, 0:n]
fy = (yy - n / 2) / (n / 2); fx = (xx - n / 2) / (n / 2)
env = np.clip(1 - (fx ** 2 + (fy * 2.4) ** 2), 0, 1) ** 1.2
nz = noise(n, 1.0, 6) * 4 + noise(n, 1.0, 12) * 6
al = np.clip(env * (0.55 + nz), 0, 1)
a = np.zeros((n, n, 4)); a[:, :, :3] = 255; a[:, :, 3] = al * 255
save("cloud", a)
print("textures written to", OUT)

# ================= character / weapon atlas (256x256): regions are placed by hand; scene.c has the same table =================
def vnoise(n, cells, seed):
    r = np.random.default_rng(seed)
    g = r.random((cells + 1, cells + 1)); g[-1, :] = g[0, :]; g[:, -1] = g[:, 0]       # tileable
    img = Image.fromarray((g * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC)
    return np.asarray(img).astype(float) / 255.0

def fbm(n, seed, octs=((3, 1.0), (6, 0.5), (12, 0.25), (24, 0.12))):
    t = sum(vnoise(n, c, seed + c) * w for c, w in octs)
    return t / sum(w for _, w in octs)

ATLAS = Image.new("RGBA", (256, 256), (255, 255, 255, 255))

def put(x, y, arr):
    arr = np.clip(arr, 0, 255).astype(np.uint8)
    ATLAS.paste(Image.fromarray(arr, "RGB" if arr.shape[2] == 3 else "RGBA"), (x, y))

def camo(seed, cols, n=64):
    f = fbm(n, seed); g = fbm(n, seed + 99, ((4, 1.0), (9, 0.6), (18, 0.2)))
    out = np.zeros((n, n, 3))
    out[:] = cols[0]
    out[f > 0.46] = cols[1]
    out[(g > 0.58) & (f < 0.62)] = cols[2]
    out[(f > 0.62) | (g < 0.30)] = cols[3]
    grain = rng.normal(0, 7, (n, n, 1))
    weave = (np.add.outer(np.arange(n) % 2, np.arange(n) % 2) % 2)[:, :, None] * 5
    return out + grain - weave

def webbing(base, n=64, seed=3):
    out = np.ones((n, n, 3)) * np.array(base, float)
    out += rng.normal(0, 5, (n, n, 1))
    out[::2, :, :] *= 0.94                                   # ripstop weave
    out[:, ::8, :] *= 0.9
    for y in range(6, n, 14):                               # MOLLE webbing rows: a strap with stitched loops
        out[y:y + 4, :, :] = np.array(base) * 0.62
        out[y + 4:y + 5, :, :] = np.array(base) * 0.45
        for x in range(2, n, 7): out[y:y + 4, x:x + 1, :] = np.array(base) * 0.4
    return out

def face():
    n = 64
    out = np.zeros((n, n, 3))
    yy, xx = np.mgrid[0:n, 0:n]
    skin = np.array((204, 156, 122), float)
    out[:] = skin
    out *= (0.86 + 0.14 * (1 - np.abs(xx - 32) / 32.0))[:, :, None]              # darker towards the temples / ears
    out += rng.normal(0, 4, (n, n, 1))
    img = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(img)
    for sx in (-1, 1):
        cx = 32 + sx * 12
        d.ellipse([cx - 5, 26, cx + 5, 32], fill=(238, 232, 226))                  # eye
        d.ellipse([cx - 2, 26, cx + 2, 32], fill=(60, 44, 34)); d.point((cx - 1, 27), fill=(250, 250, 250))
        d.line([(cx - 7, 21 + (1 if sx < 0 else 0)), (cx + 7, 21 + (0 if sx < 0 else 1))], fill=(52, 38, 30), width=2)   # brow
        d.line([(cx - 5, 33), (cx + 5, 33)], fill=(176, 126, 98), width=1)         # eye bag
    d.line([(32, 30), (30, 42)], fill=(176, 124, 98), width=2); d.line([(30, 43), (35, 43)], fill=(168, 114, 90), width=2)   # nose
    d.line([(24, 51), (40, 51)], fill=(120, 60, 54), width=2); d.line([(26, 53), (38, 53)], fill=(184, 120, 104), width=1)   # mouth
    a = np.asarray(img).astype(float)
    a[44:, 14:50, :] *= 0.95; a[:14, :, :] *= 0.9                                   # jaw and brow shading
    return a

def metal(n=64, base=(52, 54, 58), seed=11):
    r = np.random.default_rng(seed)
    out = np.ones((n, n, 3)) * np.array(base, float)
    col = r.normal(0, 4, (1, n, 1)); out += col                                     # brushed streaks
    out += rng.normal(0, 3, (n, n, 1))
    img = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(img)
    for _ in range(16):                                                              # scratches and edge wear
        x, y = r.integers(0, n, 2); l = r.integers(4, 18); a = r.random() * 3.14
        d.line([(x, y), (x + np.cos(a) * l, y + np.sin(a) * l)], fill=(104, 106, 110), width=1)
    for _ in range(4):
        x, y = r.integers(4, n - 4, 2); d.ellipse([x - 1, y - 1, x + 1, y + 1], fill=(20, 20, 22))      # rivet / screw
    return np.asarray(img).astype(float)

def poly(n=64):
    out = np.ones((n, n, 3)) * np.array((38, 38, 40), float)
    out += rng.normal(0, 3, (n, n, 1)) + (vnoise(n, 16, 4)[:, :, None] - 0.5) * 8
    return out

def bronze(n=64):
    out = np.ones((n, n, 3)) * np.array((122, 92, 58), float)
    out += rng.normal(0, 5, (n, n, 1)) + (fbm(n, 21)[:, :, None] - 0.5) * 40
    out[::16, :, :] *= 0.8
    return out

def woodtex(n=64):
    yy, xx = np.mgrid[0:n, 0:n]
    w = np.sin((yy * 0.55 + fbm(n, 31) * 9) * 1.0) * 0.5 + 0.5
    out = np.ones((n, n, 3)) * np.array((120, 72, 36), float) * (0.78 + 0.34 * w)[:, :, None]
    out += rng.normal(0, 4, (n, n, 1))
    return out

def leather(base, n=32):
    out = np.ones((n, n, 3)) * np.array(base, float)
    out += rng.normal(0, 5, (n, n, 1)) + (fbm(n, 41, ((3, 1.0), (7, 0.6)))[:, :, None] - 0.5) * 30
    out[10:12, :, :] *= 0.55; out[20:22, :, :] *= 0.6                                # creases
    return out

def glovetex(n=32):
    out = np.ones((n, n, 3)) * np.array((34, 32, 30), float)
    out += rng.normal(0, 5, (n, n, 1))
    for x in range(0, n, 8): out[:, x:x + 1, :] *= 0.6                               # seams
    out[:6, :, :] = np.array((58, 56, 52))                                           # knuckle pad
    return out

def scarf(a, b, n=32):
    out = np.ones((n, n, 3)) * np.array(a, float)
    for y in range(0, n, 8):
        for x in range(0, n, 8):
            if (x // 8 + y // 8) % 2 == 0: out[y:y + 8, x:x + 8, :] = np.array(b, float)
    out += rng.normal(0, 4, (n, n, 1)); out[::4, :, :] *= 0.92
    return out

ATLAS.paste(Image.new("RGBA", (8, 8), (255, 255, 255, 255)), (0, 0))
put(64, 0, camo(5, [(58, 74, 44), (36, 50, 32), (92, 96, 62), (24, 28, 22)]))        # ally woodland
put(128, 0, camo(8, [(158, 134, 96), (120, 98, 66), (186, 164, 120), (84, 70, 50)]))   # enemy desert
put(192, 0, webbing((54, 66, 56)))
put(0, 64, webbing((130, 108, 78)))
put(64, 64, face())
put(128, 64, leather((196, 150, 118), 32))                                           # plain skin (neck, ears, nose)
put(192, 64, camo(17, [(46, 62, 40), (30, 42, 28), (70, 82, 50), (20, 24, 20)]))       # helmet covers
put(0, 128, camo(23, [(136, 112, 78), (104, 84, 58), (160, 140, 100), (70, 58, 42)]))
put(64, 128, leather((46, 34, 26), 32))                                              # boots
put(96, 128, glovetex())
put(128, 128, metal(64))
put(192, 128, poly(64))
put(0, 192, bronze(64))
put(64, 192, woodtex(64))
put(128, 192, scarf((40, 110, 60), (28, 80, 44)))
put(160, 192, scarf((170, 26, 22), (235, 230, 220)))
ATLAS.save(os.path.join(OUT, "chars.png"))
print("character atlas written")
