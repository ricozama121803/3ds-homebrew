#!/usr/bin/env python3
"""Cuts the campus photos into transparent PNG sprites for the roadside landmarks (gfx/*.png).

Needs numpy + opencv + pillow (installed in ../../tools/venv).
Sources:  pollack.jpeg (Pollak Library), TSU.jpg (Titan Student Union), elephant statue.jpeg (Tuffy statue).
"""
import os
import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "gfx")
os.makedirs(OUT, exist_ok=True)

def load(name):
    img = cv2.imread(os.path.join(HERE, name))
    assert img is not None, name
    return img

def save_rgba(bgr, alpha, name, size):
    """Resize (keeping aspect) so the longest side fits `size` = (max_w, max_h) and write an RGBA PNG."""
    h, w = bgr.shape[:2]
    s = min(size[0] / w, size[1] / h)
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    bgr = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_AREA)
    alpha = cv2.resize(alpha, (nw, nh), interpolation=cv2.INTER_AREA)
    # 2px transparent margin (edge colour, alpha 0) so neighbouring images in the texture atlas never bleed in
    pad = 2
    bgr = cv2.copyMakeBorder(bgr, pad, pad, pad, pad, cv2.BORDER_REPLICATE)
    alpha = cv2.copyMakeBorder(alpha, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    nw, nh = nw + 2 * pad, nh + 2 * pad
    rgba = np.dstack([bgr, alpha])
    cv2.imwrite(os.path.join(OUT, name), rgba)
    return nw, nh

def largest_component(mask):
    n, lab, stats, _ = cv2.connectedComponentsWithStats((mask > 0).astype(np.uint8), 8)
    if n <= 1:
        return mask
    k = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    return np.where(lab == k, 255, 0).astype(np.uint8)

def fill_holes(mask):
    inv = 255 - mask
    n, lab, stats, _ = cv2.connectedComponentsWithStats((inv > 0).astype(np.uint8), 4)
    out = mask.copy()
    for k in range(1, n):
        x, y, w, h, a = stats[k]
        if x > 0 and y > 0 and x + w < mask.shape[1] and y + h < mask.shape[0]:   # not touching the border
            out[lab == k] = 255
    return out

def soften(mask):
    m = cv2.GaussianBlur(mask, (0, 0), 0.9)
    return np.clip((m.astype(np.float32) - 60) * 255 / 135, 0, 255).astype(np.uint8)

# ---------------------------------------------------------------- Pollak Library
# the facade is a clean rectangle: just crop it (sky on the right, lawn below are excluded)
img = load("pollack.jpeg")
facade = img[80:412, 0:329]
print("pollak", save_rgba(facade, np.full(facade.shape[:2], 255, np.uint8), "pollak.png", (192, 192)))

# ---------------------------------------------------------------- Titan Student Union + TITANS letters
img = load("TSU.jpg")
crop = img[0:590, 150:1890]
hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
b, g, r = [crop[..., i].astype(int) for i in range(3)]
blue_sky = (b - r > 18) & (b > 140) & (hsv[..., 1] > 25)                      # saturated blue
h, w = crop.shape[:2]
yy = np.arange(h)[:, None] * np.ones((1, w))
cloud = (hsv[..., 1] < 60) & (hsv[..., 2] > 215) & (yy < 150)                  # white cloud, only up high
cand = (blue_sky | cloud).astype(np.uint8)
n, lab = cv2.connectedComponents(cand, connectivity=4)
sky = np.zeros_like(cand)
for k in range(1, n):                                                         # keep blobs that touch the image border
    comp = lab == k
    if comp[0, :].any() or comp[:, 0].any() or comp[:, -1].any():
        sky[comp] = 1
sky = cv2.dilate(sky, np.ones((3, 3), np.uint8), iterations=1)
alpha = np.where(sky > 0, 0, 255).astype(np.uint8)
alpha = soften(alpha)
print("tsu", save_rgba(crop, alpha, "tsu.png", (448, 224)))

# ---------------------------------------------------------------- Tuffy statue
img = load("elephant statue.jpeg")
h, w = img.shape[:2]
gc = np.zeros((h, w), np.uint8)
bgm, fgm = np.zeros((1, 65)), np.zeros((1, 65))
cv2.grabCut(img, gc, (100, 30, 285, 335), bgm, fgm, 8, cv2.GC_INIT_WITH_RECT)
m = np.where((gc == cv2.GC_FGD) | (gc == cv2.GC_PR_FGD), 255, 0).astype(np.uint8)
m[100:149, 205:236] = 0                       # the little pine tree behind Tuffy's ear
m[100:190, 282:331] = 0                       # the tall pine tree behind his back
lum = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
m[240:, :200][lum[240:, :200] > 95] = 0     # left of the front leg only: bright pixels there are the building behind
m[268:335, 220:262] = 0                       # gap between the front legs (fence and shrubs behind)
def poly(pts):                                # points are given as (x, y) on a 3x zoomed debug view
    return np.array([(120 + x / 3, 150 + y / 3) for x, y in pts], np.int32)
m[cv2.fillPoly(np.zeros_like(m), [poly([(538, 398), (600, 412), (645, 472), (538, 472)])], 255) > 0] = 0   # fence between the hind legs
hind = np.zeros_like(m)                       # grab-cut lost the lower half of the far hind leg: add it back
cv2.fillPoly(hind, [poly([(596, 425), (690, 395), (692, 440), (705, 490), (730, 530), (750, 560), (748, 578),
                          (700, 580), (688, 572), (668, 545), (650, 500), (625, 455)])], 255)
m = np.maximum(m, hind)
m[100:260, :140] = 0                          # building and trees visible left of the raised trunk
m[182:215, 168:191] = 0                       # window strip between the cheek and the chest
m[295:, :235][lum[295:, :235] > 105] = 0      # concrete step beside the front foot
cv2.fillPoly(m, [np.array([(138, 206), (172, 206), (190, 226), (190, 255), (138, 255)], np.int32)], 0)   # building under the cheek
m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
m = fill_holes(largest_component(m))
ys, xs = np.where(m > 0)
x0, x1, y0, y1 = xs.min() - 3, xs.max() + 4, ys.min() - 3, ys.max() + 4
print("statue", save_rgba(img[y0:y1, x0:x1], soften(m[y0:y1, x0:x1]), "statue.png", (224, 224)))

if os.environ.get("DEBUG_STATUE"):      # writes the mask outlined on the source photo, with a coordinate grid
    S = 3
    big = cv2.resize(img, (w * S, h * S), interpolation=cv2.INTER_CUBIC)
    cs, _ = cv2.findContours(m, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    cv2.drawContours(big, [c * S for c in cs], -1, (0, 255, 255), 1)
    for x in range(0, w, 20):
        cv2.line(big, (x * S, 0), (x * S, h * S), (255, 255, 255), 1)
        cv2.putText(big, str(x), (x * S + 2, 12), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 255, 0), 1)
    for y in range(0, h, 20):
        cv2.line(big, (0, y * S), (w * S, y * S), (255, 255, 255), 1)
        cv2.putText(big, str(y), (2, y * S - 2), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 255, 0), 1)
    cv2.imwrite(os.environ["DEBUG_STATUE"], big[150 * S:360 * S, 120 * S:390 * S])
