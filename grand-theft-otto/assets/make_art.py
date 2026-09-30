#!/usr/bin/env python3
"""Draws all tile/sprite art (pixel art, by script) into assets/gfx/*.png and writes
assets/atlas.t3s + source/gen_atlas.h.   Run:  ../tools/venv/bin/python assets/make_art.py"""
import math, os, random, zlib
from PIL import Image, ImageDraw
import tileset as ts

HERE = os.path.dirname(os.path.abspath(__file__))
GFX = os.path.join(HERE, "gfx")
os.makedirs(GFX, exist_ok=True)

def rng(name): return random.Random(zlib.crc32(name.encode()))
def clamp(v): return max(0, min(255, int(v)))
def shade(c, d): return tuple(clamp(x + d) for x in c[:3]) + ((c[3],) if len(c) > 3 else ())

def tile(base, name, amt=5):
    """16x16 tile filled with `base` plus per-pixel noise."""
    r = rng(name)
    im = Image.new("RGBA", (16, 16))
    px = im.load()
    for y in range(16):
        for x in range(16):
            d = r.randint(-amt, amt)
            px[x, y] = shade(base, d) + (255,)
    return im

def hline(im, y, x0, x1, col):
    for x in range(x0, x1 + 1): im.putpixel((x, y), col)
def vline(im, x, y0, y1, col):
    for y in range(y0, y1 + 1): im.putpixel((x, y), col)

images = {}          # name -> PIL image

# ------------------------------------------------------------------ ground
GRASS = (78, 142, 62)
for i in range(3):
    im = tile(GRASS, f"grass{i}", 6)
    r = rng(f"blades{i}")
    for _ in range(9):
        x, y = r.randint(0, 15), r.randint(0, 14)
        im.putpixel((x, y), shade(GRASS, 18) + (255,)); im.putpixel((x, y + 1), shade(GRASS, -14) + (255,))
    images[f"grass{i}"] = im
for i in range(2):
    im = tile((156, 118, 74), f"dirt{i}", 7)
    r = rng(f"peb{i}")
    for _ in range(6):
        x, y = r.randint(0, 14), r.randint(0, 14)
        im.putpixel((x, y), (190, 160, 118, 255)); im.putpixel((x + 1, y), (120, 90, 56, 255))
    images[f"dirt{i}"] = im

SW = (198, 194, 184)
for i in range(2):
    im = tile(SW, f"sidewalk{i}", 3)
    joint = shade(SW, -30) + (255,)
    if i == 0: vline(im, 15, 0, 15, joint); hline(im, 15, 0, 15, joint)
    else:      vline(im, 7, 0, 15, joint);  hline(im, 15, 0, 15, joint)
    images[f"sidewalk{i}"] = im
PL = (214, 190, 152)
for i in range(2):
    im = tile(PL, f"plaza{i}", 4)
    joint = shade(PL, -34) + (255,)
    hline(im, 0, 0, 15, joint); hline(im, 8, 0, 15, joint)
    off = 0 if i == 0 else 4
    vline(im, off, 0, 7, joint); vline(im, (off + 8) % 16, 8, 15, joint)
    images[f"plaza{i}"] = im

ASPH = (72, 74, 80)
YEL, WHT = (240, 200, 40, 255), (232, 232, 226, 255)
images["road"] = tile(ASPH, "road", 4)
def road_line(name, y0, y1, col, dashed=False):
    im = tile(ASPH, name, 4)
    for y in range(y0, y1 + 1):
        for x in range(16):
            if dashed and (x // 4) % 2: continue
            im.putpixel((x, y), col)
    return im
images["road_hy_b"] = road_line("hy_b", 13, 14, YEL)
images["road_hy_t"] = road_line("hy_t", 1, 2, YEL)
images["road_hw_b"] = road_line("hw_b", 14, 14, WHT, True)
images["road_hw_t"] = road_line("hw_t", 1, 1, WHT, True)
for a, b in [("road_vy_r", "road_hy_b"), ("road_vy_l", "road_hy_t"), ("road_vw_r", "road_hw_b"), ("road_vw_l", "road_hw_t")]:
    images[a] = images[b].transpose(Image.TRANSPOSE)
cw = tile(ASPH, "cw", 3)
for x in range(16):
    if x % 4 < 2:
        for y in range(16): cw.putpixel((x, y), WHT)
images["cw_v"] = cw
images["cw_h"] = cw.transpose(Image.TRANSPOSE)
pk = tile((88, 90, 96), "parking", 4)
vline(pk, 0, 0, 15, WHT)
images["parking_v"] = pk
images["parking_h"] = pk.transpose(Image.TRANSPOSE)

FW = (58, 60, 66)
images["fw"] = tile(FW, "fw", 3)
def fw_line(name, x, col, dashed):
    im = tile(FW, name, 3)
    for y in range(16):
        if dashed and (y // 5) % 2: continue
        im.putpixel((x, y), col)
    return im
images["fw_dash_l"] = fw_line("fwdl", 0, WHT, True)
images["fw_dash_r"] = fw_line("fwdr", 15, WHT, True)
images["fw_edge_l"] = fw_line("fwel", 2, WHT, False)
images["fw_edge_r"] = fw_line("fwer", 13, WHT, False)
im = tile((176, 174, 166), "barrier", 3)
vline(im, 7, 0, 15, (120, 118, 112, 255)); vline(im, 8, 0, 15, (206, 204, 196, 255))
vline(im, 0, 0, 15, (90, 88, 84, 255)); vline(im, 15, 0, 15, (90, 88, 84, 255))
images["fw_barrier"] = im

def rails(base, name):
    im = tile(base, name, 5)
    TIE, RAIL, RAILH = (92, 64, 40, 255), (168, 174, 182, 255), (218, 222, 228, 255)
    for x in range(16):
        if x % 4 in (1, 2):
            for y in range(2, 14): im.putpixel((x, y), TIE)
    for y0 in (4, 10):
        hline(im, y0, 0, 15, RAIL); hline(im, y0 + 1, 0, 15, shade(RAIL, -40)); hline(im, y0 - 1 if y0 > 0 else 0, 0, 15, RAILH)
    return im
images["rail_h"] = rails((122, 112, 100), "rail")
rx = rails(ASPH, "railx")
images["rail_x_v"] = rx
images["runway"] = tile((64, 66, 72), "runway", 3)
rw = tile((64, 66, 72), "runwaydash", 3)
for x in range(8): 
    for y in (7, 8): rw.putpixel((x, y), WHT)
images["runway_dash"] = rw

WATER = (66, 122, 204)
for i in range(2):
    im = tile(WATER, f"water{i}", 5)
    r = rng(f"wave{i}")
    for _ in range(7):
        x, y = r.randint(0, 12), r.randint(0, 15)
        hline(im, y, x, x + 2, (150, 200, 255, 255))
    images[f"water{i}"] = im

# ------------------------------------------------------------------ roofs
ROOF = {"gray": (128, 132, 140), "tan": (198, 172, 122), "brown": (122, 88, 64), "red": (178, 74, 60),
        "blue": (72, 102, 162), "teal": (62, 152, 162), "white": (226, 228, 232), "dark": (72, 74, 86),
        "green": (78, 132, 92), "orange": (238, 134, 46)}
for c, col in ROOF.items():
    seam = shade(col, -22) + (255,)
    for v in range(4):
        base = shade(col, 14) if v == 2 else col                     # v2: lit side of a gable roof
        im = tile(base, f"roof_{c}{v}", 4)
        hline(im, 0, 0, 15, seam); vline(im, 0, 0, 15, seam)
        if v == 1:                                                    # rooftop vents / AC unit
            r = rng(f"vent{c}")
            x, y = r.randint(3, 9), r.randint(3, 9)
            for dy in range(4):
                for dx in range(4):
                    im.putpixel((x + dx, y + dy), shade(col, -34 if dx and dy and dx < 3 and dy < 3 else 16) + (255,))
        if v == 2:                                                    # ridge line along the bottom edge
            hline(im, 15, 0, 15, shade(col, -40) + (255,))
        if v == 3:                                                    # solar panels
            for y in (3, 4, 5, 8, 9, 10):
                for x in range(2, 14): im.putpixel((x, y), (40, 70, 130, 255) if (x + y) % 5 else (120, 150, 210, 255))
        images[f"roof_{c}{v}"] = im

# Pay 'n' Spray garage (violet + white stripes), police (navy), train station (terracotta), stadium stands
im = tile((124, 70, 156), "garage", 3)
for y in range(16):
    for x in range(16):
        if (x + y) % 8 < 3: im.putpixel((x, y), (236, 230, 244, 255))
images["roof_garage"] = im
im = tile((36, 52, 104), "police", 3)
hline(im, 0, 0, 15, (26, 38, 80, 255)); vline(im, 0, 0, 15, (26, 38, 80, 255))
images["roof_police"] = im
im = tile((176, 84, 60), "station", 4)
for y in range(0, 16, 4): hline(im, y, 0, 15, shade((176, 84, 60), -30) + (255,))
images["roof_station"] = im
im = tile((150, 152, 160), "stand", 3)
for y in range(16):
    for x in range(16):
        if y % 4 < 2 and (x + y // 4) % 3: im.putpixel((x, y), (30, 60, 130, 255) if (y // 4) % 2 == 0 else (238, 134, 46, 255))
images["stand"] = im
# hospital: white roof with a red cross across 3x3 tiles
big = Image.new("RGBA", (48, 48))
r = rng("hosp")
for y in range(48):
    for x in range(48): big.putpixel((x, y), shade((232, 234, 238), r.randint(-3, 3)) + (255,))
d = ImageDraw.Draw(big)
d.rectangle([18, 6, 29, 41], fill=(206, 44, 44, 255)); d.rectangle([6, 18, 41, 29], fill=(206, 44, 44, 255))
d.rectangle([0, 0, 47, 47], outline=(196, 200, 208, 255))
for i in range(9):
    images[f"hosp_{i}"] = big.crop(((i % 3) * 16, (i // 3) * 16, (i % 3) * 16 + 16, (i // 3) * 16 + 16))
# stadium field and running track
for i in range(2):
    im = Image.new("RGBA", (16, 16))
    r = rng(f"field{i}")
    for y in range(16):
        for x in range(16):
            band = ((x + (8 if i else 0)) // 8) % 2
            im.putpixel((x, y), shade((70, 150, 60) if band else (84, 168, 72), r.randint(-3, 3)) + (255,))
    images[f"field{i}"] = im
im = tile((190, 88, 60), "track", 5)
hline(im, 15, 0, 15, (240, 236, 230, 255))
images["track"] = im

# ------------------------------------------------------------------ trees, bushes, statue (on grass)
def on_grass(name):
    return tile(GRASS, name, 5)
for n in ("tree0", "tree1", "palm"):
    im = on_grass(n)
    d = ImageDraw.Draw(im)
    d.ellipse([3, 5, 14, 14], fill=(40, 90, 40, 255))          # shadow of the canopy on the ground
    d.rectangle([7, 7, 8, 9], fill=(96, 64, 40, 255))          # trunk
    images[n] = im
im = on_grass("bush")
d = ImageDraw.Draw(im)
d.ellipse([1, 2, 14, 14], fill=(24, 60, 26, 255)); d.ellipse([2, 2, 13, 12], fill=(56, 134, 58, 255)); d.ellipse([4, 3, 8, 6], fill=(96, 176, 88, 255))
for (x, y) in [(9, 8), (5, 9), (11, 5)]: im.putpixel((x, y), (236, 130, 50, 255))
images["bush"] = im
# ------------------------------------------------------------------ building outline overlays
EDGE, HI = (18, 18, 30, 210), (255, 255, 255, 70)
def overlay(kind):
    im = Image.new("RGBA", (16, 16))
    if kind == "n": hline(im, 0, 0, 15, EDGE); hline(im, 1, 0, 15, HI)
    if kind == "s": hline(im, 15, 0, 15, EDGE); hline(im, 14, 0, 15, (0, 0, 0, 60))
    if kind == "e": vline(im, 15, 0, 15, EDGE); vline(im, 14, 0, 15, (0, 0, 0, 60))
    if kind == "w": vline(im, 0, 0, 15, EDGE); vline(im, 1, 0, 15, HI)
    return im
for k in "nesw": images[f"edge_{k}"] = overlay(k)

# ------------------------------------------------------------------ sprites (2px transparent margin)
def sprite_canvas(w, h): return Image.new("RGBA", (w + 4, h + 4))
def outline(im, col=(12, 12, 16, 255)):
    """1px dark outline around opaque pixels."""
    src = im.load(); out = im.copy(); o = out.load()
    for y in range(im.height):
        for x in range(im.width):
            if src[x, y][3] < 128:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < im.width and 0 <= ny < im.height and src[nx, ny][3] >= 128:
                        o[x, y] = col; break
    return out
def hard_alpha(im, thr=110):
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255 if a > thr else 0)
    return im

def canopy(name, size, seed, dark, mid, light):
    S = 4
    big = Image.new("RGBA", ((size + 4) * S,) * 2)
    d = ImageDraw.Draw(big)
    r = rng(seed)
    c = (size + 4) * S / 2
    def blob(cx, cy, rad, col): d.ellipse([c + (cx - rad) * S, c + (cy - rad) * S, c + (cx + rad) * S, c + (cy + rad) * S], fill=col)
    blobs = [(0, 0, size * 0.42)] + [(r.uniform(-1, 1) * size * 0.25, r.uniform(-1, 1) * size * 0.25, size * r.uniform(0.22, 0.3)) for _ in range(5)]
    for (x, y, rad) in blobs: blob(x, y, rad, dark)
    for (x, y, rad) in blobs: blob(x - 1, y - 1.5, rad * 0.82, mid)
    for (x, y, rad) in blobs[1:4]: blob(x - 2, y - 3, rad * 0.4, light)
    im = big.resize((size + 4, size + 4), Image.LANCZOS)
    im = outline(hard_alpha(im))
    images[name] = im
canopy("canopy0", 30, "c0", (30, 84, 36, 255), (52, 128, 54, 255), (110, 186, 96, 255))
canopy("canopy1", 26, "c1", (36, 92, 40, 255), (66, 144, 62, 255), (132, 200, 108, 255))
# palm: radiating fronds
S = 4; size = 40
big = Image.new("RGBA", ((size + 4) * S,) * 2); d = ImageDraw.Draw(big); c = (size + 4) * S / 2
for k in range(9):
    a = k * math.pi * 2 / 9
    pts = [(c + math.cos(a + o) * w * S, c + math.sin(a + o) * w * S) for (w, o) in [(2, 0.5), (16, 0.16), (19, 0), (16, -0.16), (2, -0.5)]]
    d.polygon(pts, fill=(38, 112, 52, 255))
    d.line([(c, c), (c + math.cos(a) * 18 * S, c + math.sin(a) * 18 * S)], fill=(120, 190, 90, 255), width=S)
d.ellipse([c - 3 * S, c - 3 * S, c + 3 * S, c + 3 * S], fill=(96, 64, 40, 255))
images["palmtop"] = outline(hard_alpha(big.resize((size + 4, size + 4), Image.LANCZOS)))
sh = sprite_canvas(18, 10)
ImageDraw.Draw(sh).ellipse([2, 2, 19, 11], fill=(0, 0, 0, 84))
images["shadow"] = sh


# ------------------------------------------------------------------ LA additions: ground
SCRUB = (154, 142, 88)
for i in range(2):
    im = tile(SCRUB, f"scrub{i}", 7)
    r = rng(f"chap{i}")
    for _ in range(8):
        x, y = r.randint(0, 14), r.randint(0, 14)
        im.putpixel((x, y), (96, 112, 60, 255)); im.putpixel((x + 1, y), (120, 132, 70, 255)); im.putpixel((x, y + 1), (78, 92, 50, 255))
    images[f"scrub{i}"] = im
for i in range(2):
    im = tile((234, 216, 164), f"sand{i}", 5)
    r = rng(f"sandspeck{i}")
    for _ in range(6): im.putpixel((r.randint(0, 15), r.randint(0, 15)), (206, 186, 132, 255))
    images[f"sand{i}"] = im
images["wetsand"] = tile((190, 170, 126), "wetsand", 5)
im = tile((40, 40, 46), "star_walk", 3)                                   # Hollywood Walk of Fame terrazzo star
pts = []
for k in range(10):
    a = -math.pi / 2 + k * math.pi / 5
    rr = 6.6 if k % 2 == 0 else 2.8
    pts.append((8 + math.cos(a) * rr, 8.4 + math.sin(a) * rr))
d = ImageDraw.Draw(im); d.polygon(pts, fill=(236, 84, 150, 255), outline=(232, 190, 70, 255))
images["star_walk"] = im
for i in range(2):                                                        # boardwalk / pier planks
    im = tile((156, 114, 72), f"wood{i}", 5)
    for y in range(0, 16, 4): hline(im, y, 0, 15, (104, 74, 44, 255))
    r = rng(f"plank{i}")
    for y in range(0, 16, 4):
        x = r.randint(2, 13); im.putpixel((x, y + 1), (90, 62, 38, 255)); im.putpixel((x, y + 2), (90, 62, 38, 255))
    images[f"wood{i}"] = im
for i in range(2):                                                        # concrete river channel
    im = tile((178, 180, 182), f"conc{i}", 4)
    hline(im, 0, 0, 15, (140, 142, 146, 255)); vline(im, 0 if i == 0 else 8, 0, 15, (140, 142, 146, 255))
    if i == 1:
        r = rng("graf")
        for _ in range(3):
            x, y = r.randint(2, 12), r.randint(2, 12)
            hline(im, y, x, x + 2, (200, 80, 120, 255))
    images[f"concrete{i}"] = im
im = tile((196, 98, 58), "court", 4)                                      # Venice Beach basketball court
hline(im, 0, 0, 15, (238, 232, 226, 255)); vline(im, 0, 0, 15, (238, 232, 226, 255))
images["court0"] = im
for a, b in [("fw_edge_t", "fw_edge_l"), ("fw_edge_b", "fw_edge_r"), ("fw_dash_t", "fw_dash_l"), ("fw_dash_b", "fw_dash_r"), ("fw_barrier_h", "fw_barrier")]:
    images[a] = images[b].transpose(Image.TRANSPOSE)       # horizontal freeways are the vertical ones turned on their side
images["rail_v"] = images["rail_h"].transpose(Image.TRANSPOSE)
images["rail_x_h"] = images["rail_x_v"].transpose(Image.TRANSPOSE)
OCEAN = (32, 92, 176)
for i in range(2):
    im = tile(OCEAN, f"ocean{i}", 5)
    r = rng(f"swell{i}")
    for _ in range(6):
        x, y = r.randint(0, 11), r.randint(0, 15)
        hline(im, y, x, x + 3, (110, 170, 230, 255))
    images[f"ocean{i}"] = im
im = tile((118, 166, 196), "stream", 4)
for (x, y) in [(2, 4), (9, 10), (5, 13)]: hline(im, y, x, x + 3, (184, 220, 240, 255))
images["stream"] = im
im = tile((84, 204, 232), "pool", 4)
d = ImageDraw.Draw(im); d.rectangle([0, 0, 15, 15], outline=(240, 246, 250, 255))
for y in (5, 10): hline(im, y, 1, 14, (150, 226, 246, 255))
images["pool0"] = im

# ---- terracotta tile pattern on the red roofs
for v in range(4):
    im = images[f"roof_red{v}"]
    for y in range(1, 16, 2):
        for x in range(1, 16):
            if (x + (y // 2) * 2) % 4 == 0: im.putpixel((x, y), shade((178, 74, 60), -28) + (255,))

# ---- Chinese Theatre pagoda roof, tall glass / dark / stone towers
im = tile((172, 44, 44), "theatre", 4)
for y in range(16):
    for x in range(16):
        if x in (0, 15) or y in (0, 15): im.putpixel((x, y), (238, 190, 62, 255))
        if y == 7 or y == 8: im.putpixel((x, y), (238, 190, 62, 255))
images["roof_theatre"] = im
def tower(name, base, line, glint, v):
    im = tile(base, name + str(v), 3)
    for k in range(0, 16, 4):
        hline(im, k, 0, 15, line); vline(im, k, 0, 15, line)
    for t in range(16):
        x = (t + 2) % 16
        if t % 5 < 3: im.putpixel((x, t), glint)
    if v == 1:                                                            # mechanical penthouse / antenna base
        d = ImageDraw.Draw(im)
        d.rectangle([5, 5, 10, 10], fill=shade(base, -28) + (255,), outline=shade(base, -60) + (255,)); im.putpixel((7, 7), (255, 80, 60, 255))
    return im
for v in range(2):
    images[f"tower_glass{v}"] = tower("tg", (66, 118, 178), (34, 66, 112, 255), (170, 214, 244, 255), v)
    images[f"tower_dark{v}"] = tower("td", (34, 42, 68), (18, 22, 40, 255), (94, 158, 200, 255), v)
    images[f"tower_stone{v}"] = tower("ts", (206, 196, 174), (168, 158, 138, 255), (236, 230, 214, 255), v)

# ---- Griffith Observatory: white building with copper-green domes (3x3 tiles)
big = Image.new("RGBA", (48, 48)); r = rng("obs")
for y in range(48):
    for x in range(48): big.putpixel((x, y), shade((234, 234, 238), r.randint(-3, 3)) + (255,))
d = ImageDraw.Draw(big)
d.rectangle([0, 0, 47, 47], outline=(176, 180, 190, 255))
for (cx, cy, rad, col) in [(24, 24, 13, (92, 172, 146)), (7, 24, 6, (92, 172, 146)), (41, 24, 6, (92, 172, 146))]:
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=shade(col, -40) + (255,))
    d.ellipse([cx - rad + 1, cy - rad + 1, cx + rad - 2, cy + rad - 2], fill=col + (255,))
    d.ellipse([cx - rad // 2, cy - rad // 2 - 1, cx - rad // 6, cy - rad // 6], fill=(190, 236, 214, 255))
for i in range(9):
    images[f"obs_{i}"] = big.crop(((i % 3) * 16, (i // 3) * 16, (i % 3) * 16 + 16, (i // 3) * 16 + 16))

# ---- Santa Monica Ferris wheel (4x4 tiles) standing on the pier deck
big = Image.new("RGBA", (64, 64)); r = rng("ferris")
for y in range(64):
    for x in range(64):
        big.putpixel((x, y), shade((156, 114, 72), r.randint(-5, 5)) + (255,))
for y in range(0, 64, 4): hline(big, y, 0, 63, (104, 74, 44, 255))
d = ImageDraw.Draw(big)
d.ellipse([3, 3, 60, 60], outline=(40, 44, 56, 255), width=3)
d.ellipse([12, 12, 51, 51], outline=(96, 104, 120, 255), width=2)
for k in range(12):
    a = k * math.pi / 6
    d.line([(32, 32), (32 + math.cos(a) * 28, 32 + math.sin(a) * 28)], fill=(196, 202, 214, 255), width=1)
    gx, gy = 32 + math.cos(a) * 28, 32 + math.sin(a) * 28
    d.rectangle([gx - 3, gy - 3, gx + 3, gy + 3], fill=[(236, 70, 70), (250, 200, 60), (70, 170, 240), (110, 210, 120)][k % 4] + (255,), outline=(20, 20, 24, 255))
d.ellipse([27, 27, 37, 37], fill=(206, 50, 50, 255), outline=(20, 20, 24, 255))
for i in range(16):
    images[f"ferris_{i}"] = big.crop(((i % 4) * 16, (i // 4) * 16, (i % 4) * 16 + 16, (i // 4) * 16 + 16))

# ---- the Hollywood sign: 9 white letters on a hillside, 18 x 3 tiles
from PIL import ImageFont
cols, rows = ts.SIGN_COLS, ts.SIGN_ROWS
big = Image.new("RGBA", (cols * 16, rows * 16)); r = rng("hsign")
for y in range(rows * 16):
    for x in range(cols * 16): big.putpixel((x, y), shade((154, 142, 88), r.randint(-7, 7)) + (255,))
fp = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
d = ImageDraw.Draw(big)
font = ImageFont.truetype(fp, 90) if os.path.exists(fp) else ImageFont.load_default()
step = cols * 16 / 9.0
for k, ch in enumerate("HOLLYWOOD"):
    tmp = Image.new("RGBA", (120, 120)); ImageDraw.Draw(tmp).text((10, 4), ch, font=font, fill=(244, 244, 246, 255))
    bb = tmp.getbbox()
    letter = tmp.crop(bb).resize((24, 40), Image.LANCZOS)                 # each letter is squashed narrow and stretched tall
    sh = Image.new("RGBA", letter.size, (96, 84, 50, 255)); sh.putalpha(letter.getchannel("A"))
    x = int(k * step + (step - 24) / 2)
    big.alpha_composite(sh, (x + 1, 4)); big.alpha_composite(letter, (x, 2))
for rr in range(rows):
    for cc in range(cols):
        images[f"hsign_{rr}_{cc}"] = big.crop((cc * 16, rr * 16, cc * 16 + 16, rr * 16 + 16))

# ------------------------------------------------------------------ people: the player, pedestrians, cops, SWAT (seen from above)
S = 4; U = 26                                   # sprite is U x U units (incl. 2 unit margin)
BLK = (14, 14, 18)
def human_master(frame, jump, hat, skin, shoes=(238, 238, 244), bag=None, walk_frames=4, stripe=True):
    """One walk frame of a person facing UP, drawn at 4x. `jump` = torso/sleeve colour, `hat` = beanie/hair/cap colour."""
    jumpd = shade(jump, -44)
    im = Image.new("RGBA", (U * S, U * S))
    d = ImageDraw.Draw(im)
    def ell(cx, cy, w, h, fill, edge=True, ew=0.9):
        cx += 1; cy += 1
        if edge: d.ellipse([(cx - w / 2 - ew) * S, (cy - h / 2 - ew) * S, (cx + w / 2 + ew) * S, (cy + h / 2 + ew) * S], fill=BLK)
        d.ellipse([(cx - w / 2) * S, (cy - h / 2) * S, (cx + w / 2) * S, (cy + h / 2) * S], fill=fill)
    if walk_frames == 4: sw = [-2.2, 0, 2.2, 0][frame]      # legs and arms swing opposite to each other (up is negative)
    else: sw = [-2.2, 2.2][frame]
    ell(9.4, 18.0 + sw, 4.2, 5.6, shoes); ell(14.6, 18.0 - sw, 4.2, 5.6, shoes)          # shoes poking out under the body
    if bag: ell(12, 19.0, 7.4, 4.4, bag)                                                    # loot bag on his back, peeking out below
    ell(4.6, 13.2 - sw * 0.8, 3.6, 7.6, jump); ell(19.4, 13.2 + sw * 0.8, 3.6, 7.6, jump)   # sleeves
    ell(4.4, 17.6 - sw * 0.8, 3.0, 3.0, skin); ell(19.6, 17.6 + sw * 0.8, 3.0, 3.0, skin)   # hands
    ell(12, 13.0, 13.6, 9.8, jump)                                                          # torso / shoulders
    ell(12, 15.4, 9.6, 4.6, jumpd, False)
    ell(12, 10.2, 6.6, 6.6, skin)                                                           # head (the face peeks out at the front)
    ell(12, 10.9, 6.4, 5.6, hat)                                                            # hat / hair
    if stripe: d.rectangle([(9.4 + 1) * S, (11.3 + 1) * S, (14.6 + 1) * S, (12.0 + 1) * S], fill=shade(hat, 90))
    return im

def rotate_small(master, k, n, size):
    rot = master.rotate(-360.0 * k / n, resample=Image.BICUBIC)         # PIL rotates counter-clockwise; we want clockwise
    return hard_alpha(rot.resize((size, size), Image.LANCZOS))

JUMP, BEANIE, SKIN, BAG = (240, 122, 32), (30, 30, 38), (238, 190, 150), (206, 170, 110)
for f in range(ts.WALK_FRAMES):
    master = human_master(f, JUMP, BEANIE, SKIN, bag=BAG, stripe=True)
    for k in range(ts.DIRS): images[f"player_{f}_{k}"] = rotate_small(master, k, ts.DIRS, U)

PEDS = [  # jumpsuit/shirt, hat/hair, skin, shoes
    ((64, 112, 204), (72, 50, 30), (238, 190, 150), (238, 238, 244)),
    ((84, 172, 92), (20, 18, 18), (204, 152, 112), (60, 60, 70)),
    ((152, 92, 192), (30, 20, 20), (142, 98, 68), (238, 238, 244)),
    ((242, 212, 62), (204, 164, 72), (240, 200, 160), (60, 60, 70)),
    ((226, 228, 232), (92, 92, 104), (224, 178, 138), (120, 90, 60)),
    ((52, 172, 172), (124, 52, 42), (152, 108, 78), (238, 238, 244)),
    ((30, 50, 112), (20, 26, 54), (228, 184, 144), (18, 18, 22)),       # cop
    ((44, 48, 56), (14, 14, 18), (204, 162, 122), (18, 18, 22)),        # SWAT
]
for t, (jc, hc, sk, sh_) in enumerate(PEDS):
    for f in range(ts.PED_FRAMES):
        master = human_master(f, jc, hc, sk, shoes=sh_, walk_frames=2, stripe=(t >= 6))
        for k in range(ts.PED_DIRS): images[f"ped_{t}_{f}_{k}"] = rotate_small(master, k, ts.PED_DIRS, U)

def corpse_master(jc, hc, sk):
    im = Image.new("RGBA", (U * S, U * S)); d = ImageDraw.Draw(im)
    def ell(cx, cy, w, h, fill, ew=0.9):
        cx += 1; cy += 1
        d.ellipse([(cx - w / 2 - ew) * S, (cy - h / 2 - ew) * S, (cx + w / 2 + ew) * S, (cy + h / 2 + ew) * S], fill=BLK)
        d.ellipse([(cx - w / 2) * S, (cy - h / 2) * S, (cx + w / 2) * S, (cy + h / 2) * S], fill=fill)
    ell(19.5, 10.5, 6, 3.4, jc); ell(19.5, 16.0, 6, 3.4, jc)                     # legs, splayed
    ell(9.5, 8.0, 3.0, 6.2, jc); ell(9.5, 18.5, 3.0, 6.2, jc)                    # arms
    ell(13, 13.2, 9.4, 12.4, jc)                                                 # torso
    ell(6.2, 13.2, 6.2, 6.2, sk); ell(6.4, 13.4, 5.6, 5.0, hc)                   # head
    return im
for t, (jc, hc, sk, sh_) in enumerate(PEDS):
    master = corpse_master(jc, hc, sk)
    for k in range(4): images[f"corpse_{t}_{k}"] = rotate_small(master, k, 4, U)

# ------------------------------------------------------------------ cars (seen from above, drawn facing UP, then pre-rotated)
CS = 38
CAR_DIMS = {0: (12, 22), 1: (13, 26), 2: (13, 25), 3: (15, 30), 4: (13, 26), 5: (13, 26), 6: (15, 30)}
CAR_COLORS = {(0, 0): (214, 54, 54), (0, 1): (64, 120, 210), (1, 0): (238, 238, 240), (1, 1): (150, 158, 170), (1, 2): (44, 58, 100),
              (2, 0): (250, 208, 40), (2, 1): (34, 34, 40), (3, 0): (236, 236, 238), (4, 0): (248, 198, 30), (5, 0): (28, 28, 34), (6, 0): (54, 58, 68)}
GLASS = (36, 48, 70)
def car_master(model, body):
    w, l = CAR_DIMS[model]
    im = Image.new("RGBA", (CS * S, CS * S)); d = ImageDraw.Draw(im); c = CS / 2
    def rr(x0, y0, x1, y1, fill, rad=0.8):
        d.rounded_rectangle([(c + x0) * S, (c + y0) * S, (c + x1) * S, (c + y1) * S], radius=rad * S, fill=fill)
    dark, light = shade(body, -46), shade(body, 30)
    for sx in (-1, 1):                                                          # wheels peek out at the sides
        for fy in (-0.3, 0.3): rr(sx * (w / 2 + 0.3) - 1.0, fy * l - 2.4, sx * (w / 2 + 0.3) + 1.0, fy * l + 2.4, (18, 18, 22), 0.6)
    rr(-w / 2 - 0.9, -l / 2 - 0.9, w / 2 + 0.9, l / 2 + 0.9, BLK, 2.6)          # outline
    rr(-w / 2, -l / 2, w / 2, l / 2, body, 2.2)
    if model == 3:                                                              # van: boxy roof with a short cab
        rr(-w / 2 + 1.0, -l * 0.06, w / 2 - 1.0, l / 2 - 0.9, light, 1.0)
        rr(-w / 2 + 1.6, -l * 0.30, w / 2 - 1.6, -l * 0.18, GLASS, 0.6)
        rr(-w / 2 + 1.0, l / 2 - 3.6, w / 2 - 1.0, l / 2 - 3.2, dark, 0.2)
    else:
        rr(-w / 2 + 1.3, -l * 0.20, w / 2 - 1.3, l * 0.22, light, 1.2)          # cabin roof
        rr(-w / 2 + 1.6, -l * 0.26, w / 2 - 1.6, -l * 0.17, GLASS, 0.6)         # windscreen
        rr(-w / 2 + 1.6, l * 0.17, w / 2 - 1.6, l * 0.26, GLASS, 0.6)           # rear window
    d.line([((c - w / 2 + 0.5) * S, (c - l * 0.30) * S), ((c + w / 2 - 0.5) * S, (c - l * 0.30) * S)], fill=dark, width=S // 2)   # hood crease
    rr(-w / 2 + 0.5, -l / 2 + 0.4, -w / 2 + 2.3, -l / 2 + 1.8, (255, 242, 160), 0.4); rr(w / 2 - 2.3, -l / 2 + 0.4, w / 2 - 0.5, -l / 2 + 1.8, (255, 242, 160), 0.4)   # headlights
    rr(-w / 2 + 0.5, l / 2 - 1.8, -w / 2 + 2.3, l / 2 - 0.4, (230, 40, 40), 0.4); rr(w / 2 - 2.3, l / 2 - 1.8, w / 2 - 0.5, l / 2 - 0.4, (230, 40, 40), 0.4)           # tail lights
    if model == 2:                                                              # sports car: racing stripes
        for x in (-1.6, 0.6): rr(x, -l / 2 + 1.0, x + 1.0, l / 2 - 1.0, (240, 240, 244), 0.2)
        rr(-w / 2 + 1.3, -l * 0.20, w / 2 - 1.3, l * 0.22, shade(body, -10), 1.2); rr(-w / 2 + 1.6, -l * 0.26, w / 2 - 1.6, -l * 0.17, GLASS, 0.6); rr(-w / 2 + 1.6, l * 0.17, w / 2 - 1.6, l * 0.26, GLASS, 0.6)
    if model == 4:                                                              # taxi: roof sign + checker line
        rr(-2.6, -1.0, 2.6, 1.0, (255, 250, 200), 0.4); rr(-1.6, -0.5, 1.6, 0.5, (40, 40, 44), 0.2)
        for k in range(6): rr(-w / 2 - 0.0, -l * 0.20 + k * 1.6, -w / 2 + 1.0, -l * 0.20 + k * 1.6 + 0.8, (20, 20, 24), 0.1)
    if model == 5:                                                              # police: white doors and a light bar
        rr(-w / 2 + 0.3, -l * 0.18, w / 2 - 0.3, l * 0.24, (238, 238, 244), 0.6)
        rr(-w / 2 + 1.3, -l * 0.10, w / 2 - 1.3, l * 0.12, (32, 32, 40), 1.0)
        rr(-w / 2 + 1.2, -0.9, 0.0, 0.9, (226, 30, 40), 0.3); rr(0.0, -0.9, w / 2 - 1.2, 0.9, (40, 90, 240), 0.3)
    if model == 6:                                                              # SWAT: armoured, bull bar, roof turret
        rr(-w / 2 + 0.5, -l / 2 - 1.3, w / 2 - 0.5, -l / 2 + 0.6, (30, 32, 38), 0.4)
        rr(-w / 2 + 1.0, -l * 0.06, w / 2 - 1.0, l / 2 - 0.9, (40, 44, 52), 1.0)
        d.ellipse([(c - 2.2) * S, (c + l * 0.05 - 2.2) * S, (c + 2.2) * S, (c + l * 0.05 + 2.2) * S], fill=(20, 22, 26), outline=(120, 124, 132), width=S // 2)
        rr(-w / 2 + 0.3, -l * 0.12, -w / 2 + 1.1, l * 0.18, (240, 240, 244), 0.2); rr(w / 2 - 1.1, -l * 0.12, w / 2 - 0.3, l * 0.18, (240, 240, 244), 0.2)
    return im
for v, (m, ci) in enumerate(ts.CARVARS):
    master = car_master(m, CAR_COLORS[(m, ci)])
    for k in range(ts.DIRS): images[f"car_{v}_{k}"] = rotate_small(master, k, ts.DIRS, CS)

# ------------------------------------------------------------------ effects
def blank(w, h): return Image.new("RGBA", (w + 4, h + 4))
def fx_star(name, size, colors):
    im = blank(size, size); d = ImageDraw.Draw(im); c = (size + 4) / 2
    for r, col in colors:
        pts = [(c + math.cos(k * math.pi / 4) * (r if k % 2 == 0 else r * 0.45), c + math.sin(k * math.pi / 4) * (r if k % 2 == 0 else r * 0.45)) for k in range(8)]
        d.polygon(pts, fill=col)
    images[name] = im
fx_star("muzzle", 10, [(5, (255, 170, 40, 255)), (3.4, (255, 240, 140, 255)), (1.6, (255, 255, 255, 255))])
fx_star("spark", 5, [(2.6, (255, 220, 100, 255)), (1.2, (255, 255, 255, 255))])
im = blank(5, 5); d = ImageDraw.Draw(im); d.ellipse([3, 3, 7, 7], fill=(255, 190, 60, 255)); d.ellipse([4, 4, 6, 6], fill=(255, 255, 230, 255)); images["bullet"] = im
im = blank(3, 3); ImageDraw.Draw(im).ellipse([2, 2, 5, 5], fill=(196, 16, 26, 255)); images["blood_dot"] = im
for name, col in [("glow_red", (255, 50, 50)), ("glow_blue", (70, 120, 255))]:
    im = blank(11, 11); px = im.load()
    for y in range(15):
        for x in range(15):
            dd = math.hypot(x - 7, y - 7)
            if dd < 6.5: a = max(0, 1 - dd / 6.5); px[x, y] = col + (int(255 * a * a + (60 if dd < 2 else 0)),) if dd >= 2 else (255, 255, 255, 255)
    images[name] = im
im = blank(26, 14); ImageDraw.Draw(im).ellipse([2, 2, 29, 17], fill=(0, 0, 0, 70)); images["shadow_car"] = im
for i in range(4):                                                              # blood stains on the ground
    r = rng(f"stain{i}"); size = [12, 15, 18, 22][i]
    im = blank(size, size); d = ImageDraw.Draw(im); c = (size + 4) / 2
    for _ in range(7):
        ox, oy, rad = r.uniform(-size * 0.22, size * 0.22), r.uniform(-size * 0.22, size * 0.22), r.uniform(size * 0.12, size * 0.28)
        d.ellipse([c + ox - rad, c + oy - rad, c + ox + rad, c + oy + rad], fill=(150, 12, 20, 235))
    for _ in range(4):
        ox, oy, rad = r.uniform(-size * 0.18, size * 0.18), r.uniform(-size * 0.18, size * 0.18), r.uniform(size * 0.06, size * 0.14)
        d.ellipse([c + ox - rad, c + oy - rad, c + ox + rad, c + oy + rad], fill=(196, 22, 30, 240))
    for _ in range(5):
        a = r.uniform(0, 6.28); dd = size * r.uniform(0.32, 0.5)
        d.ellipse([c + math.cos(a) * dd - 1, c + math.sin(a) * dd - 1, c + math.cos(a) * dd + 1, c + math.sin(a) * dd + 1], fill=(150, 12, 20, 235))
    images[f"stain{i}"] = im
for i in range(6):                                                              # explosion animation
    size = [16, 28, 40, 48, 46, 40][i]
    im = blank(size, size); d = ImageDraw.Draw(im); c = (size + 4) / 2
    stages = [[(0.5, (255, 200, 60, 255)), (0.3, (255, 250, 200, 255))],
              [(0.5, (255, 120, 30, 255)), (0.4, (255, 200, 60, 255)), (0.2, (255, 250, 210, 255))],
              [(0.5, (230, 70, 20, 255)), (0.42, (255, 140, 40, 255)), (0.28, (255, 210, 80, 255))],
              [(0.5, (150, 40, 20, 255)), (0.42, (230, 90, 30, 255)), (0.3, (255, 160, 50, 255))],
              [(0.5, (70, 50, 46, 220)), (0.4, (150, 60, 30, 230)), (0.26, (230, 110, 40, 230))],
              [(0.5, (60, 56, 54, 170)), (0.36, (90, 70, 60, 180))]][i]
    for k, (rr_, col) in enumerate(stages):
        rad = size * rr_
        d.ellipse([c - rad, c - rad, c + rad, c + rad], fill=col)
    images[f"boom{i}"] = im
for i in range(3):                                                              # smoke puffs
    size = [12, 16, 20][i]
    im = blank(size, size); d = ImageDraw.Draw(im); c = (size + 4) / 2
    d.ellipse([c - size / 2, c - size / 2, c + size / 2, c + size / 2], fill=(70, 70, 76, 150)); d.ellipse([c - size * 0.35, c - size * 0.4, c + size * 0.3, c + size * 0.25], fill=(110, 110, 118, 130))
    images[f"smoke{i}"] = im
for i in range(3):                                                              # flames
    im = blank(12, 14); d = ImageDraw.Draw(im); lean = [-1.5, 0, 1.5][i]
    d.polygon([(8 + lean * 2, 2), (13, 10), (11, 16), (5, 16), (3, 10)], fill=(240, 90, 20, 255))
    d.polygon([(8 + lean, 6), (11, 12), (9.5, 15), (6.5, 15), (5, 12)], fill=(255, 190, 50, 255))
    d.polygon([(8, 10), (9.5, 13), (6.5, 13)], fill=(255, 250, 200, 255))
    images[f"fire{i}"] = im
def pickup(name, draw_fn):
    im = blank(14, 14); d = ImageDraw.Draw(im)
    d.ellipse([2, 2, 17, 17], fill=(0, 0, 0, 90)); draw_fn(d); images[name] = im
def pk_health(d): d.ellipse([3, 3, 16, 16], fill=(232, 240, 232, 255), outline=BLK); d.rectangle([8, 5, 11, 14], fill=(40, 170, 70, 255)); d.rectangle([5, 8, 14, 11], fill=(40, 170, 70, 255))
def pk_armor(d): d.polygon([(4, 4), (15, 4), (15, 11), (9.5, 16), (4, 11)], fill=(70, 130, 240, 255), outline=BLK); d.line([(9.5, 6), (9.5, 13)], fill=(220, 236, 255, 255)); d.line([(6, 8), (13, 8)], fill=(220, 236, 255, 255))
def pk_cash(d): d.rectangle([3, 5, 16, 14], fill=(96, 190, 96, 255), outline=BLK); d.ellipse([7, 7, 12, 12], fill=(200, 240, 200, 255)); d.line([(9.5, 7), (9.5, 12)], fill=(30, 100, 40, 255))
def gun_icon(d, kind, col, ox=0, oy=0):
    if kind == "pistol":
        d.rectangle([ox + 3, oy + 6, ox + 15, oy + 9], fill=col, outline=BLK); d.rectangle([ox + 3, oy + 9, ox + 7, oy + 14], fill=col, outline=BLK)
    if kind == "smg":
        d.rectangle([ox + 2, oy + 6, ox + 17, oy + 9], fill=col, outline=BLK); d.rectangle([ox + 5, oy + 9, ox + 8, oy + 15], fill=col, outline=BLK); d.rectangle([ox + 11, oy + 9, ox + 13, oy + 12], fill=col, outline=BLK)
    if kind == "shotgun":
        d.rectangle([ox + 1, oy + 6, ox + 18, oy + 8], fill=col, outline=BLK); d.rectangle([ox + 1, oy + 8, ox + 9, oy + 11], fill=(150, 100, 60, 255), outline=BLK)
pickup("pk_health", pk_health); pickup("pk_armor", pk_armor); pickup("pk_cash", pk_cash)
for kind in ("pistol", "smg", "shotgun"): pickup(f"pk_{kind}", lambda d, k=kind: (d.ellipse([3, 3, 16, 16], fill=(250, 232, 120, 255), outline=BLK), gun_icon(d, k, (60, 60, 68, 255))))
def hud_icon(name, fn):
    im = Image.new("RGBA", (28 + 4, 16 + 4)); fn(ImageDraw.Draw(im)); images[name] = im
def ic_fists(d):
    for (x, y) in [(6, 5), (14, 5), (10, 9)]: d.ellipse([x + 2, y + 2, x + 9, y + 9], fill=(255, 255, 255, 255), outline=BLK)
hud_icon("ic_fists", ic_fists)
for kind in ("pistol", "smg", "shotgun"): hud_icon(f"ic_{kind}", lambda d, k=kind: gun_icon(d, k, (255, 255, 255, 255), 4, 0))

# ------------------------------------------------------------------ Pay 'n' Spray pad
im = tile((92, 52, 128), "spray", 3)
d = ImageDraw.Draw(im); d.rectangle([0, 0, 15, 15], outline=(236, 220, 250, 255))
d.ellipse([4, 4, 11, 11], outline=(255, 240, 120, 255)); im.putpixel((8, 8), (255, 240, 120, 255))
images["spray_pad"] = im

# ------------------------------------------------------------------ write everything
for name in ts.TILE_NAMES + ts.OVERLAYS + ts.SPRITES:
    im = images[name]
    if name in ts.TILE_NAMES or name in ts.OVERLAYS: assert im.size == (16, 16), (name, im.size)
    im.save(os.path.join(GFX, name + ".png"))

with open(os.path.join(HERE, "atlas.t3s"), "w") as f:
    f.write("--atlas -f rgba -z auto\n")
    for name in ts.ALL_IMAGES: f.write(f"gfx/{name}.png\n")

with open(os.path.join(HERE, "..", "source", "gen_atlas.h"), "w") as f:
    f.write("// GENERATED by assets/make_art.py - do not edit\n#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define NTILES {ts.NTILES}\n#define IMG_COUNT {len(ts.ALL_IMAGES)}\n")
    f.write("#define F_SOLID 1\n#define F_BUILDING 2\n#define F_TREE 4\n#define F_WATER 8\n#define F_ROAD 16\n#define F_TALL 32\n#define F_SPRAY 64\n\n")
    for n, i in ts.TILE_ID.items(): f.write(f"#define TILE_{n.upper()} {i}\n")
    f.write("\n")
    for n in ts.OVERLAYS + ["canopy0", "canopy1", "palmtop", "shadow", "minimap"]:
        f.write(f"#define IMG_{n.upper()} {ts.IMG_INDEX[n]}\n")
    f.write(f"#define IMG_PLAYER_BASE {ts.IMG_INDEX['player_0_0']}\n")
    f.write(f"#define IMG_PLAYER(frame, dir) (IMG_PLAYER_BASE + (frame) * {ts.DIRS} + (dir))\n")
    f.write(f"#define DIRS {ts.DIRS}\n#define WALK_FRAMES {ts.WALK_FRAMES}\n#define PED_DIRS {ts.PED_DIRS}\n#define PED_FRAMES {ts.PED_FRAMES}\n\n")
    f.write(f"#define NCARVARS {len(ts.CARVARS)}\n#define IMG_CAR_BASE {ts.IMG_INDEX['car_0_0']}\n#define IMG_CAR(var, dir) (IMG_CAR_BASE + (var) * {ts.DIRS} + (dir))\n")
    f.write("__attribute__((unused)) static const uint8_t carvar_model[NCARVARS] = {" + ",".join(str(m) for m, _ in ts.CARVARS) + "};\n")
    f.write(f"#define NPEDTYPES {ts.PEDTYPES}\n#define IMG_PED_BASE {ts.IMG_INDEX['ped_0_0_0']}\n#define IMG_PED(t, f, dir) (IMG_PED_BASE + ((t) * {ts.PED_FRAMES} + (f)) * {ts.PED_DIRS} + (dir))\n")
    f.write(f"#define IMG_CORPSE_BASE {ts.IMG_INDEX['corpse_0_0']}\n#define IMG_CORPSE(t, k) (IMG_CORPSE_BASE + (t) * 4 + (k))\n")
    for n in ts.EFFECTS: f.write(f"#define IMG_{n.upper()} {ts.IMG_INDEX[n]}\n")
    f.write("\n")
    f.write("__attribute__((unused)) static const uint8_t tile_flags[NTILES] = {" + ",".join(str(ts.TILES[i][1]) for i in range(ts.NTILES)) + "};\n\n")
    f.write("#ifdef HOST_PREVIEW\n__attribute__((unused)) static const char *const atlas_names[IMG_COUNT] = {\n")
    for n in ts.ALL_IMAGES: f.write(f'\t"{n}",\n')
    f.write("};\n#endif\n")
print(f"{len(ts.TILE_NAMES)} tiles, {len(ts.ALL_IMAGES)} atlas images")
