#!/usr/bin/env python3
"""Textures for the 3D renderer. Needs the 16x16 tile PNGs from make_art.py. Writes:
   assets/gfx3d/world.png    1024x512 atlas of 32x32 cells: every ground tile (upscaled), then building facades, then a white cell
   assets/gfx3d/sprites.png  256x256 atlas of billboards, decals and pickups
   source/gen3d.h            tile kinds / facade sets / cell and sprite tables for source/r3d.c
Run:  ../tools/venv/bin/python assets/make_world3d.py"""
import math, os, random, zlib
from PIL import Image, ImageDraw
import tileset as ts

HERE = os.path.dirname(os.path.abspath(__file__))
GFX = os.path.join(HERE, "gfx")
OUT = os.path.join(HERE, "gfx3d")
os.makedirs(OUT, exist_ok=True)

CELL, COLS, ROWS = 32, 32, 16
AW, AH = CELL * COLS, CELL * ROWS
def rng(name): return random.Random(zlib.crc32(name.encode()))
def clamp(v): return max(0, min(255, int(v)))
def shade(c, d): return tuple(clamp(x + d) for x in c[:3])
def mix(a, b, t): return tuple(clamp(a[i] + (b[i] - a[i]) * t) for i in range(3))

# ------------------------------------------------------------------ tile kinds
K_GROUND, K_RAISED, K_BUILDING, K_TREE, K_PALM, K_BUSH, K_WATER, K_SIGN, K_FERRIS, K_STREAM, K_BARRIER, K_TOWER = range(12)
def kind_of(name, flags):
    if name.startswith("tower_"): return K_TOWER
    if name.startswith("ferris_"): return K_FERRIS
    if name.startswith("hsign_"): return K_SIGN
    if flags & ts.F_BUILDING: return K_BUILDING
    if name in ("tree0", "tree1"): return K_TREE
    if name == "palm": return K_PALM
    if name == "bush": return K_BUSH
    if name == "stream": return K_STREAM
    if name.startswith("ocean") or name.startswith("water") or name == "pool0": return K_WATER
    if name in ("sidewalk0", "sidewalk1", "star_walk"): return K_RAISED
    if name in ("fw_barrier", "fw_barrier_h"): return K_BARRIER
    return K_GROUND

# facade sets: name, base colour, style (0 plain windows, 1 glass curtain wall, 2 stone, 3 garage, 4 shop)
SETS = []
ROOFCOL = {"gray": (128, 132, 140), "tan": (198, 172, 122), "brown": (122, 88, 64), "red": (178, 74, 60), "blue": (72, 102, 162), "teal": (62, 152, 162),
           "white": (226, 228, 232), "dark": (72, 74, 86), "green": (78, 132, 92), "orange": (238, 134, 46)}
for c, col in ROOFCOL.items(): SETS.append((f"roof_{c}", col, 4 if c in ("tan", "white", "orange", "blue") else 0))
SETS += [("tower_glass", (66, 118, 178), 1), ("tower_dark", (40, 48, 72), 1), ("tower_stone", (206, 196, 174), 2),
         ("garage", (124, 70, 156), 3), ("police", (36, 52, 104), 0), ("station", (176, 84, 60), 2), ("theatre", (172, 44, 44), 2),
         ("stand", (150, 152, 160), 2), ("white", (232, 234, 238), 0), ("sign", (240, 240, 244), 2)]
SETIDX = {n: i for i, (n, _, _) in enumerate(SETS)}
def set_of(name):
    if name.startswith("roof_"):
        base = name[5:-1] if name[-1] in "0123" else name[5:]
        return SETIDX.get(f"roof_{base}", SETIDX["roof_gray"]) if base in ROOFCOL else SETIDX.get(f"roof_{base}", SETIDX["garage"] if base == "garage" else SETIDX[base] if base in SETIDX else SETIDX["roof_gray"])
    if name.startswith("tower_"): return SETIDX[name[:-1]]
    if name.startswith("hosp_") or name.startswith("obs_"): return SETIDX["white"]
    if name.startswith("hsign_"): return SETIDX["sign"]
    if name == "stand": return SETIDX["stand"]
    return 255

# ------------------------------------------------------------------ atlas
atlas = Image.new("RGBA", (AW, AH), (255, 255, 255, 255))
def cell_xy(i): return (i % COLS) * CELL, (i // COLS) * CELL

# 1. ground / roof tiles, 16 -> 32 px, with a little grain so they don't look flat at a perspective angle
for name in ts.TILE_NAMES:
    im = Image.open(os.path.join(GFX, name + ".png")).convert("RGBA")
    big = im.resize((CELL, CELL), Image.BICUBIC)
    r = rng("g" + name); px = big.load()
    amt = 0 if name.startswith(("ocean", "water", "pool")) else 3
    for y in range(CELL):
        for x in range(CELL):
            p = px[x, y]
            d = r.randint(-amt, amt)
            px[x, y] = (clamp(p[0] + d), clamp(p[1] + d), clamp(p[2] + d), 255)
    if name.startswith("hsign_"):                              # the Hollywood sign letters stand up in 3D: only the white pixels stay
        for y in range(CELL):
            for x in range(CELL):
                p = px[x, y]
                if min(p[:3]) < 170: px[x, y] = (255, 255, 255, 0)
    atlas.paste(big, cell_xy(ts.TILE_ID[name]))

# 2. facades: for each set, a ground-floor cell and two upper-floor variants
def facade(base, style, ground, variant, name, night=False):
    r = rng(f"{name}{ground}{variant}")
    LIT = 0.6 if night else 1.0                                 # at night most windows are lit
    im = Image.new("RGBA", (CELL, CELL)); d = ImageDraw.Draw(im)
    wall = shade(base, -34)                                    # walls are in shade: darker than the roof
    for y in range(CELL):
        for x in range(CELL):
            g = r.randint(-3, 3) - y // 6
            im.putpixel((x, y), shade(wall, g) + (255,))
    sky, glass_dark = (120, 150, 190), (26, 36, 58)
    def window(x0, y0, x1, y1, lit_p=0.12):
        lit = r.random() < (min(0.85, lit_p * 4.5 + 0.25) if night else lit_p)
        top = (255, 226, 150) if lit else mix(glass_dark, sky, 0.5)
        bot = (232, 190, 110) if lit else glass_dark
        for yy in range(y0, y1 + 1):
            t = (yy - y0) / max(1, y1 - y0)
            for xx in range(x0, x1 + 1): im.putpixel((xx, yy), mix(top, bot, t) + (255,))
        d.rectangle([x0 - 1, y0 - 1, x1 + 1, y1 + 1], outline=shade(wall, -26) + (255,))
        d.line([(x0 - 1, y1 + 1), (x1 + 1, y1 + 1)], fill=shade(wall, 22) + (255,))      # sill catches the light
    if ground:
        if style == 3:                                         # garage: roller doors
            for x0 in (3, 18): d.rectangle([x0, 8, x0 + 11, 30], fill=(190, 192, 200, 255), outline=(60, 60, 70, 255)); [d.line([(x0, y), (x0 + 11, y)], fill=(150, 152, 160, 255)) for y in range(11, 30, 3)]
        else:
            d.rectangle([0, 28, 31, 31], fill=shade(wall, -26) + (255,))                       # plinth
            awn = r.choice([(200, 60, 60), (60, 120, 190), (230, 190, 60), (70, 160, 100), (240, 240, 240)])
            if style == 4:                                     # shop front: big window, door, striped awning
                window(3, 12, 17, 26, 0.5); window(21, 12, 29, 26, 0.0)
                d.rectangle([20, 14, 28, 28], fill=(60, 44, 36, 255), outline=(20, 16, 14, 255)); d.rectangle([22, 17, 26, 28], fill=(120, 170, 200, 255))
                for x in range(0, 32, 4): d.rectangle([x, 4, x + 1, 10], fill=awn + (255,)); d.rectangle([x + 2, 4, x + 3, 10], fill=(245, 245, 245, 255))
            else:
                for x0 in (3, 12, 21): window(x0, 10, x0 + 6, 25, 0.3)
                d.rectangle([13, 18, 18, 30], fill=(48, 36, 30, 255))
    elif style == 1:                                           # glass curtain wall
        for y in range(CELL):
            for x in range(CELL):
                t = (x + y) / 62.0
                col = mix(shade(base, 26), shade(base, -22), 0.5 + 0.5 * math.sin(t * 6.0 + variant))
                im.putpixel((x, y), shade(col, r.randint(-4, 4)) + (255,))
        for k in (0, 10, 21): d.line([(k, 0), (k, 31)], fill=shade(base, -64) + (255,))
        for k in (0, 15): d.line([(0, k), (31, k)], fill=shade(base, -64) + (255,))
        for _ in range(7 if night else 1):
            if r.random() < (0.9 if night else 0.3):
                x0 = r.choice([2, 12, 23]); y0 = r.choice([3, 17]); d.rectangle([x0, y0, x0 + 6, y0 + 9], fill=(255, 226, 150, 255))
    elif style == 2:                                           # stone: pilasters and tall slit windows
        for x in (0, 15, 16, 31): d.line([(x, 0), (x, 31)], fill=shade(wall, 18) + (255,))
        for x0 in (4, 20): window(x0, 5, x0 + 7, 26, 0.12)
    else:                                                      # ordinary windows, two or three across
        xs = (4, 20) if variant == 0 else (3, 12, 21)
        w = 8 if variant == 0 else 6
        for x0 in xs: window(x0, 7, x0 + w - 1, 22, 0.14)
    d.line([(0, 31), (31, 31)], fill=shade(wall, -40) + (255,))        # floor line
    return im

WALLCELL = {}                                                  # (set, 0 ground / 1 upper a / 2 upper b) -> cell index
WALLCELL_N = {}                                                # the same facades at night, with the windows lit
nxt = ts.NTILES
for si, (n, col, style) in enumerate(SETS):
    for k in range(3):
        WALLCELL[(si, k)] = nxt
        atlas.paste(facade(col, style, k == 0, k - 1 if k else 0, n), cell_xy(nxt)); nxt += 1
for si, (n, col, style) in enumerate(SETS):
    for k in range(3):
        WALLCELL_N[(si, k)] = nxt
        atlas.paste(facade(col, style, k == 0, k - 1 if k else 0, n, True), cell_xy(nxt)); nxt += 1
assert nxt < COLS * ROWS - 1, nxt
CELL_WHITE = COLS * ROWS - 1
atlas.paste(Image.new("RGBA", (CELL, CELL), (255, 255, 255, 255)), cell_xy(CELL_WHITE))
atlas.save(os.path.join(OUT, "world.png"))

# ------------------------------------------------------------------ sprite atlas (billboards, decals, pickups)
SPR_NAMES = (["glow_white", "glow_red", "glow_blue", "muzzle", "spark", "blood_dot", "bullet", "rocket", "shadow"]
             + [f"smoke{i}" for i in range(3)] + [f"fire{i}" for i in range(3)] + [f"boom{i}" for i in range(6)] + [f"stain{i}" for i in range(4)]
             + ["pk_health", "pk_armor", "pk_pistol", "pk_smg", "pk_shotgun", "pk_cash", "pk_rifle", "pk_mg", "pk_sniper", "pk_rpg", "pk_ammo"])
SW = SH = 256
sheet = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
rects = {}
x = y = rowh = 0
def make_glow():
    im = Image.new("RGBA", (32, 32))
    for y in range(32):
        for x in range(32):
            d = math.hypot(x - 15.5, y - 15.5) / 15.5
            a = max(0.0, 1.0 - d); im.putpixel((x, y), (255, 255, 255, int(255 * a * a)))
    return im
def spr_img(n): return make_glow() if n == "glow_white" else Image.open(os.path.join(GFX, n + ".png")).convert("RGBA")
for n in sorted(SPR_NAMES, key=lambda n: -spr_img(n).height):
    im = spr_img(n)
    if x + im.width + 2 > SW: x, y, rowh = 0, y + rowh + 2, 0
    assert y + im.height <= SH, "sprite sheet full"
    sheet.paste(im, (x, y)); rects[n] = (x, y, im.width, im.height)
    x += im.width + 2; rowh = max(rowh, im.height)
sheet.save(os.path.join(OUT, "sprites.png"))

# ------------------------------------------------------------------ generated header
with open(os.path.join(HERE, "..", "source", "gen3d.h"), "w") as f:
    f.write("// GENERATED by assets/make_world3d.py - do not edit\n#pragma once\n#include <stdint.h>\n#include \"gen_atlas.h\"\n\n")
    f.write(f"#define WORLD_ATLAS_W {AW}\n#define WORLD_ATLAS_H {AH}\n#define ATLAS_CELL {CELL}\n#define ATLAS_COLS {COLS}\n#define CELL_WHITE {CELL_WHITE}\n")
    f.write(f"#define SPRITE_ATLAS_W {SW}\n#define SPRITE_ATLAS_H {SH}\n\n")
    f.write("enum { K_GROUND, K_RAISED, K_BUILDING, K_TREE, K_PALM, K_BUSH, K_WATER, K_SIGN, K_FERRIS, K_STREAM, K_BARRIER, K_TOWER };\n")
    f.write("__attribute__((unused)) static const uint8_t tile_kind[NTILES] = {" + ",".join(str(kind_of(n, fl)) for n, fl in ts.TILES) + "};\n")
    f.write("__attribute__((unused)) static const uint8_t tile_wallset[NTILES] = {" + ",".join(str(set_of(n)) if kind_of(n, fl) in (K_BUILDING, K_TOWER, K_SIGN) else "255" for n, fl in ts.TILES) + "};\n")
    f.write(f"#define NWALLSETS {len(SETS)}\n")
    f.write("__attribute__((unused)) static const uint16_t wallset_cell[NWALLSETS][3] = {" + ",".join("{%d,%d,%d}" % tuple(WALLCELL[(i, k)] for k in range(3)) for i in range(len(SETS))) + "};\n")
    f.write("__attribute__((unused)) static const uint16_t wallset_cell_night[NWALLSETS][3] = {" + ",".join("{%d,%d,%d}" % tuple(WALLCELL_N[(i, k)] for k in range(3)) for i in range(len(SETS))) + "};\n")
    for n in SPR_NAMES: f.write(f"#define SPR_{n.upper()} {SPR_NAMES.index(n)}\n")
    f.write(f"#define SPR_COUNT {len(SPR_NAMES)}\n")
    f.write("typedef struct { uint16_t x, y, w, h; } SprRect;\n__attribute__((unused)) static const SprRect spr_rects[SPR_COUNT] = {" + ",".join("{%d,%d,%d,%d}" % rects[n] for n in SPR_NAMES) + "};\n")
    # colours for the 3D car and pedestrian models (RGB 0xRRGGBB)
    cc = {(0, 0): (214, 54, 54), (0, 1): (64, 120, 210), (1, 0): (238, 238, 240), (1, 1): (150, 158, 170), (1, 2): (44, 58, 100), (2, 0): (250, 208, 40),
          (2, 1): (34, 34, 40), (3, 0): (236, 236, 238), (4, 0): (248, 198, 30), (5, 0): (28, 28, 34), (6, 0): (54, 58, 68), (7, 0): (214, 218, 226)}
    peds = [((64, 112, 204), (72, 50, 30), (238, 190, 150), (238, 238, 244)), ((84, 172, 92), (20, 18, 18), (204, 152, 112), (60, 60, 70)),
            ((152, 92, 192), (30, 20, 20), (142, 98, 68), (238, 238, 244)), ((242, 212, 62), (204, 164, 72), (240, 200, 160), (60, 60, 70)),
            ((226, 228, 232), (92, 92, 104), (224, 178, 138), (120, 90, 60)), ((52, 172, 172), (124, 52, 42), (152, 108, 78), (238, 238, 244)),
            ((30, 50, 112), (20, 26, 54), (228, 184, 144), (18, 18, 22)), ((44, 48, 56), (14, 14, 18), (204, 162, 122), (18, 18, 22))]
    f.write("// pedestrian colours: shirt, hair/hat, skin, shoes\n__attribute__((unused)) static const uint32_t ped_rgb[NPEDTYPES][4] = {" + ",".join("{" + ",".join("0x%02X%02X%02X" % c for c in p) + "}" for p in peds) + "};\n")
    f.write("__attribute__((unused)) static const uint32_t carvar_rgb[NCARVARS] = {" + ",".join("0x%02X%02X%02X" % cc[v] for v in ts.CARVARS) + "};\n")
print(f"world atlas: {len(ts.TILE_NAMES)} tiles + {len(SETS) * 6} facade cells; sprite atlas: {len(SPR_NAMES)} sprites")
