#!/usr/bin/env python3
"""Seamless tiling textures for the beach and the pier: gfx/sand.png and gfx/plank.png (128x128). Run: ../tools/venv/bin/python make_textures.py"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
N = 128
rng = np.random.default_rng(7)


def tile_noise(scale):
    """smooth noise that wraps around (value noise on a coarse periodic grid, bilinearly upsampled)"""
    g = rng.random((scale, scale))
    xs = np.linspace(0, scale, N, endpoint=False)
    x0 = np.floor(xs).astype(int)
    f = xs - x0
    f = f * f * (3 - 2 * f)
    a = g[np.ix_(x0 % scale, x0 % scale)]
    b = g[np.ix_(x0 % scale, (x0 + 1) % scale)]
    c = g[np.ix_((x0 + 1) % scale, x0 % scale)]
    d = g[np.ix_((x0 + 1) % scale, (x0 + 1) % scale)]
    fx = f[None, :]
    fy = f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


# sand: warm beige, soft dunes, fine grain
n = 0.55 * tile_noise(4) + 0.3 * tile_noise(8) + 0.15 * tile_noise(16)
grain = rng.random((N, N))
base = np.array([226, 205, 156], float)
sand = base[None, None, :] * (0.88 + 0.16 * n[..., None]) * (0.96 + 0.08 * grain[..., None])
Image.fromarray(np.clip(sand, 0, 255).astype(np.uint8)).save(os.path.join(HERE, "gfx/sand.png"))

# planks: boards running along the texture's x axis with dark seams, each board its own shade, grain streaks, nail dots at the ends
img = np.zeros((N, N, 3), float)
boards = 8
h = N // boards
for b in range(boards):
    shade = 0.78 + 0.28 * rng.random()
    col = np.array([150, 112, 74], float) * shade
    y0, y1 = b * h, (b + 1) * h
    streak = tile_noise(16)[y0:y1, :]
    img[y0:y1, :, :] = col[None, None, :] * (0.9 + 0.2 * streak[..., None])
    img[y0:y0 + 2, :, :] *= 0.45                                                # seam
    cut = int(rng.integers(20, N - 20))
    img[y0:y1, cut:cut + 2, :] *= 0.5                                           # end joint
    for x in (cut - 4, cut + 6):
        img[y0 + h // 2, x % N, :] = (60, 50, 40)                               # nails
Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(os.path.join(HERE, "gfx/plank.png"))
print("wrote gfx/sand.png and gfx/plank.png")
