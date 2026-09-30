#!/usr/bin/env python3
"""Lays out the 160x160-tile city (a stylised, squeezed Los Angeles) and writes:
   assets/map.bin          tile ids (header: u16 W, H, spawn x, spawn y, then W*H bytes)
   assets/gfx/minimap.png  1 pixel per tile, for the bottom-screen map
   source/gen_map.h        district names and landmark labels
Run:  python assets/make_map.py   (needs only assets/tileset.py)"""
import os, random, struct
from PIL import Image
import tileset as ts

HERE = os.path.dirname(os.path.abspath(__file__))
W = H = 160
R = random.Random(90210)
tiles = [["grass0"] * W for _ in range(H)]
kind = [[None] * W for _ in range(H)]       # 'road' / 'fw' / 'rail' for tiles that count as street

def inside(x, y): return 0 <= x < W and 0 <= y < H
def put(x, y, n):
    if inside(x, y) and kind[y][x] is None: tiles[y][x] = n          # streets, freeways, rails and the river are never overwritten
def get(x, y): return tiles[y][x] if inside(x, y) else None
def rect(x0, y0, x1, y1, n):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1): put(x, y, n)
def rect_any(x0, y0, x1, y1, names):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1): put(x, y, R.choice(names))
GRASSES = ("grass0", "grass1", "grass2")
LAND = GRASSES + ("dirt0", "dirt1", "scrub0", "scrub1", "sand0", "sand1")
def free(x, y, names=LAND): return inside(x, y) and tiles[y][x] in names

# ---------------------------------------------------------------- terrain: ocean, beach, hills
def coast(y): return 10 if y < 40 else 18 if y < 100 else 22          # the shoreline bends like the real one
for y in range(H):
    c = coast(y)
    for x in range(W):
        if x < c: tiles[y][x] = R.choice(["ocean0", "ocean1"])
        elif x < c + 2: tiles[y][x] = "wetsand"
        elif x < c + 8 or (x < 28 and y >= 20): tiles[y][x] = R.choice(["sand0", "sand1"])
        elif y < 20 and x < 138: tiles[y][x] = R.choice(["scrub0", "scrub1"])        # the Hollywood Hills

# ---------------------------------------------------------------- streets
# (y0, y1, x0, x1) / (x0, x1, y0, y1); every street is 8 wide (2 lanes each way) except the minor ones (4 wide)
HROADS = [(20, 27, 24, 159), (40, 47, 24, 137), (58, 65, 24, 159), (96, 103, 24, 159), (140, 147, 24, 137)]
VROADS = [(28, 35, 20, 147), (76, 83, 0, 147), (96, 103, 20, 147), (116, 123, 20, 147)]
FREEWAYS = [("v", 60, 68, 0, 159),       # the 405
            ("h", 118, 126, 22, 137),    # the 10
            ("h", 78, 86, 69, 137),      # the 101
            ("v", 147, 155, 0, 159)]     # the 5
RIVER = (138, 145)                       # the concrete LA River channel

HMARK = {8: ["road", "road_hw_b", "road", "road_hy_b", "road_hy_t", "road_hw_b", "road", "road"], 4: ["road", "road_hy_b", "road_hy_t", "road"]}
VNAME = {"road": "road", "road_hw_b": "road_vw_r", "road_hy_b": "road_vy_r", "road_hy_t": "road_vy_l", "road_hw_t": "road_vw_l"}

hmask = [[None] * W for _ in range(H)]
vmask = [[None] * W for _ in range(H)]
for (y0, y1, x0, x1) in HROADS:
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1): hmask[y][x] = (y - y0, y1 - y0 + 1)
for (x0, x1, y0, y1) in VROADS:
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1): vmask[y][x] = (x - x0, x1 - x0 + 1)
for y in range(H):
    for x in range(W):
        h, v = hmask[y][x], vmask[y][x]
        if h and v: tiles[y][x] = "road"; kind[y][x] = "road"
        elif h: tiles[y][x] = HMARK[h[1]][h[0]]; kind[y][x] = "road"
        elif v: tiles[y][x] = VNAME[HMARK[v[1]][v[0]]]; kind[y][x] = "road"

# crosswalks just before/after every intersection
for (y0, y1, hx0, hx1) in HROADS:
    for (x0, x1, vy0, vy1) in VROADS:
        if not (vy0 <= y0 and y1 <= vy1 and hx0 <= x0 and x1 <= hx1): continue
        for y in range(y0, y1 + 1):
            for x in (x0 - 1, x1 + 1):
                if inside(x, y) and hmask[y][x] and not vmask[y][x] and tiles[y][x] != "road": tiles[y][x] = "cw_h"
        for x in range(x0, x1 + 1):
            for y in (y0 - 1, y1 + 1):
                if inside(x, y) and vmask[y][x] and not hmask[y][x] and tiles[y][x] != "road": tiles[y][x] = "cw_v"

# the LA River: a concrete channel with a trickle of water down the middle
for y in range(H):
    for x in range(RIVER[0], RIVER[1] + 1):
        if hmask[y][x] or vmask[y][x]: continue                    # streets cross it on bridges
        tiles[y][x] = "stream" if x in (141, 142) else R.choice(["concrete0", "concrete1"])
        kind[y][x] = "river"

# freeways (9 wide, concrete barrier in the middle). Streets that cross them simply bridge over.
FWV = ["fw_edge_l", "fw", "fw_dash_r", "fw", "fw_barrier", "fw", "fw_dash_l", "fw", "fw_edge_r"]
FWH = ["fw_edge_t", "fw", "fw_dash_b", "fw", "fw_barrier_h", "fw", "fw_dash_t", "fw", "fw_edge_b"]
for (d, a0, a1, b0, b1) in FREEWAYS:
    for b in range(b0, b1 + 1):
        for i, a in enumerate(range(a0, a1 + 1)):
            x, y = (a, b) if d == "v" else (b, a)
            if not inside(x, y) or hmask[y][x] or vmask[y][x]: continue
            tiles[y][x] = (FWV if d == "v" else FWH)[i]; kind[y][x] = "fw"

# railway: a north-south rail yard beside Union Station, crossing the streets on level crossings
for y in range(93, 118):
    for x in (132, 133):
        if kind[y][x] == "fw": continue
        tiles[y][x] = "rail_x_h" if hmask[y][x] else "rail_v"
        kind[y][x] = "rail"

# sidewalks beside every street (only on ground, never on water or sand of the beach proper)
def near_road(x, y):
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if inside(x + dx, y + dy) and kind[y + dy][x + dx] == "road": return True
    return False
for y in range(H):
    for x in range(W):
        if kind[y][x] is None and near_road(x, y) and tiles[y][x] in GRASSES + ("scrub0", "scrub1", "dirt0", "dirt1", "sand0", "sand1"):
            tiles[y][x] = "sidewalk0"

# ---------------------------------------------------------------- filler helpers
TREES = ["tree0", "tree1", "tree0", "palm", "palm"]
def trees(x0, y0, x1, y1, density, names=TREES, on=GRASSES + ("scrub0", "scrub1")):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if free(x, y, on) and R.random() < density: put(x, y, R.choice(names))

def hills(x0, y0, x1, y1, density=0.05):
    rect_any(x0, y0, x1, y1, ["scrub0", "scrub1"])
    for _ in range(max(1, (x1 - x0) * (y1 - y0) // 50)):
        cx, cy, rad = R.randint(x0, x1), R.randint(y0, y1), R.randint(2, 4)
        for y in range(cy - rad, cy + rad + 1):
            for x in range(cx - rad, cx + rad + 1):
                if x0 <= x <= x1 and y0 <= y <= y1 and (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad: put(x, y, R.choice(["dirt0", "dirt1"]))
    trees(x0, y0, x1, y1, density, ["bush", "tree0", "palm"], on=("scrub0", "scrub1"))

def houses(x0, y0, x1, y1, roofs=("red", "tan", "white", "red", "teal", "brown")):
    for ly in range(y0, y1 - 4, 8):
        for lx in range(x0, x1 - 4, 8):
            w, h = R.choice([4, 5]), R.choice([3, 4])
            roof = R.choice(roofs)
            for y in range(ly + 2, ly + 2 + h):
                for x in range(lx + 2, lx + 2 + w):
                    upper = (y - (ly + 2)) < (h + 1) // 2
                    put(x, y, f"roof_{roof}{2 if upper else (3 if R.random() < 0.12 else 0)}")
            for _ in range(2):
                tx, ty = lx + R.choice([0, 6, 7]), ly + R.choice([0, 6, 7])
                if x0 <= tx <= x1 and y0 <= ty <= y1 and free(tx, ty, GRASSES): put(tx, ty, R.choice(["palm", "palm", "tree1", "bush"]))
            if R.random() < 0.3 and free(lx + 2, ly + 6, GRASSES) and free(lx + 3, ly + 6, GRASSES):     # a backyard pool
                if x0 <= lx + 3 <= x1 and y0 <= ly + 6 <= y1: put(lx + 2, ly + 6, "pool0"); put(lx + 3, ly + 6, "pool0")

def estates(x0, y0, x1, y1):
    """Beverly Hills: big houses with pools and lots of palms."""
    rect_any(x0, y0, x1, y1, GRASSES)
    for ly in range(y0, y1 - 8, 11):
        for lx in range(x0, x1 - 8, 12):
            w, h = R.choice([7, 8]), R.choice([4, 5])
            roof = R.choice(["tan", "white", "red", "white"])
            for y in range(ly + 2, ly + 2 + h):
                for x in range(lx + 2, lx + 2 + w):
                    upper = (y - (ly + 2)) < (h + 1) // 2
                    put(x, y, f"roof_{roof}{2 if upper else 0}")
            for y in range(ly + h + 3, ly + h + 5):                               # pool in the yard
                for x in range(lx + 3, lx + 6): put(x, y, "pool0")
            for (px, py) in [(lx, ly), (lx + 10, ly), (lx, ly + 9), (lx + 10, ly + 9), (lx + 9, ly + 5)]:
                if free(px, py, GRASSES) and x0 <= px <= x1 and y0 <= py <= y1: put(px, py, "palm")

def roof_color_at(x, y):
    n = get(x, y)
    if n and n.startswith("tower_"): return n[:-1]
    return n[5:-1] if n and n.startswith("roof_") and n[-1] in "0123" else None

def shops(x0, y0, x1, y1, colors=("tan", "brown", "gray", "red", "blue", "white", "dark"), paving="sidewalk1"):
    y = y0
    while y + 4 <= y1:
        depth = min(R.choice([6, 7, 8]), y1 - y + 1)
        if y1 - (y + depth + 2) < 5: depth = y1 - y + 1        # last row reaches the block edge
        x = x0
        while x + 3 <= x1:
            w = min(R.choice([5, 6, 7, 8, 9]), x1 - x + 1)
            avoid = {roof_color_at(x - 1, y), roof_color_at(x, y - 1), roof_color_at(x + w - 1, y - 1)}
            c = R.choice([k for k in colors if k not in avoid] or list(colors))
            for yy in range(y, y + depth):
                for xx in range(x, x + w): put(xx, yy, f"roof_{c}{1 if R.random() < 0.22 else (3 if R.random() < 0.04 else 0)}")
            x += w + (2 if R.random() < 0.3 else 0)
        y += depth + 2
    for yy in range(y0, y1 + 1):
        for xx in range(x0, x1 + 1):
            if free(xx, yy): put(xx, yy, paving)

def towers(x0, y0, x1, y1):
    """Downtown: skyscrapers (tall buildings) mixed with mid-rises and plazas."""
    y = y0
    while y + 4 <= y1:
        depth = min(R.choice([6, 7, 8]), y1 - y + 1)
        if y1 - (y + depth + 2) < 5: depth = y1 - y + 1
        x = x0
        while x + 3 <= x1:
            w = min(R.choice([5, 6, 7, 8]), x1 - x + 1)
            avoid = {roof_color_at(x - 1, y), roof_color_at(x, y - 1), roof_color_at(x + w - 1, y - 1)}
            kinds = [k for k in ("tower_glass", "tower_dark", "tower_stone", "tower_glass", "roof_gray", "roof_white", "roof_blue") if k not in avoid and k.replace("roof_", "") not in avoid]
            k = R.choice(kinds or ["tower_glass"])
            for yy in range(y, y + depth):
                for xx in range(x, x + w):
                    v = 1 if R.random() < 0.15 else 0
                    put(xx, yy, f"{k}{v}" if k.startswith("tower_") else f"{k}{v}")
            x += w + (2 if R.random() < 0.4 else 0)
        y += depth + 2
    for yy in range(y0, y1 + 1):
        for xx in range(x0, x1 + 1):
            if free(xx, yy): put(xx, yy, "sidewalk1")

def parking(x0, y0, x1, y1, vertical=True):
    rect(x0, y0, x1, y1, "parking_v" if vertical else "parking_h")

def bld(x0, y0, x1, y1, name, vary=True):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            n = name
            if vary and name.startswith("roof_") and name[-1] in "0123": n = name[:-1] + ("1" if R.random() < 0.2 else "0")
            put(x, y, n)

def park(x0, y0, x1, y1, pond=None, palms=False):
    rect_any(x0, y0, x1, y1, list(GRASSES))
    cx0, cy0, cx1, cy1 = x0 + 3, y0 + 3, x1 - 3, y1 - 3
    for x in range(cx0, cx1 + 1): put(x, cy0, "sidewalk1"); put(x, cy1, "sidewalk1")
    for y in range(cy0, cy1 + 1): put(cx0, y, "sidewalk1"); put(cx1, y, "sidewalk1")
    mx, my = (x0 + x1) // 2, (y0 + y1) // 2
    for x in range(cx0, cx1 + 1): put(x, my, "sidewalk1")
    for y in range(cy0, cy1 + 1): put(mx, y, "sidewalk1")
    if pond:
        px, py, rx, ry = pond
        for y in range(py - ry, py + ry + 1):
            for x in range(px - rx, px + rx + 1):
                if ((x - px) / rx) ** 2 + ((y - py) / ry) ** 2 <= 1.0: put(x, y, R.choice(["water0", "water1"]))
    trees(x0, y0, x1, y1, 0.07, ["palm", "tree0", "tree1"] if palms else TREES)

def stadium(x0, y0, x1, y1, ring=3):
    bld(x0, y0, x1, y1, "stand", False)
    rect(x0 + ring, y0 + ring, x1 - ring, y1 - ring, "track")
    for y in range(y0 + ring + 2, y1 - ring - 1):
        for x in range(x0 + ring + 2, x1 - ring - 1): put(x, y, "field0" if ((x - x0) // 4) % 2 == 0 else "field1")
    gx = (x0 + x1) // 2
    rect(gx - 2, y1 - ring, gx + 2, y1, "plaza1")                                   # gate on the south side

# ---------------------------------------------------------------- districts
# --- north: Hollywood Hills, Griffith Park, the sign, the observatory
hills(28, 0, 75, 19, 0.03)
for y0, y1 in [(6, 17)]: houses(36, y0, 75, y1, ("white", "tan", "dark", "teal"))     # hillside houses
hills(76, 0, 137, 19, 0.05)
trees(104, 0, 137, 19, 0.12, ["tree0", "tree1", "palm", "bush"], on=("scrub0", "scrub1"))
for i in range(ts.SIGN_ROWS):                                                       # the Hollywood sign on the hillside
    for j in range(ts.SIGN_COLS): put(86 + j, 4 + i, f"hsign_{i}_{j}")
rect(84, 8, 105, 9, "dirt0")
for i in range(9): put(120 + i % 3, 10 + i // 3, f"obs_{i}")                        # Griffith Observatory
rect(118, 13, 124, 15, "plaza0")
for y in range(13, 20): put(121, y, "sidewalk1")
# --- west coast: Santa Monica pier + Ferris wheel, Venice boardwalk and beach
for x in range(6, 28):
    for y in range(62, 65): put(x, y, R.choice(["wood0", "wood1"]))                 # pier deck (over the ocean)
for x in range(5, 15):
    for y in (61, 65): put(x, y, R.choice(["wood0", "wood1"]))                      # wider at the end
for i in range(16): put(7 + i % 4, 62 + i // 4 - 1, f"ferris_{i}")
bld(12, 61, 14, 61, "roof_red0", False); bld(20, 61, 21, 61, "roof_white0", False)
for y in range(72, 102):                                                            # Venice Beach boardwalk + beach shops
    put(25, y, R.choice(["wood0", "wood1"])); put(26, y, R.choice(["wood0", "wood1"]))
    if y % 6 in (0, 1, 2): 
        for x in (27,): put(x, y, "roof_orange0" if y % 12 < 6 else "roof_teal0")
rect(20, 78, 23, 83, "court0"); rect(20, 88, 23, 93, "court0")                      # basketball courts
trees(18, 20, 23, 147, 0.02, ["palm"], on=("sand0", "sand1"))
# --- Beverly Hills: estates with pools, Rodeo Drive shops, west-side houses
estates(37, 29, 58, 56)
shops(37, 67, 58, 77, colors=("white", "tan", "orange", "blue", "white", "red"), paving="plaza1")    # Rodeo Drive
houses(37, 88, 58, 94)
rect(37, 88, 58, 94, "sidewalk1"); bld(38, 89, 48, 93, "roof_police", False); rect(50, 89, 57, 93, "parking_v")     # police station
# --- Venice canals
rect(37, 105, 58, 116, "sidewalk1")
for lx in range(38, 58, 5):
    for ly in (105, 109, 113): bld(lx, ly, lx + 3, ly + 1, f"roof_{R.choice(['tan', 'white', 'blue', 'teal', 'orange'])}0", False)
for y in (107, 111): rect(37, y, 58, y, "water0")
for x in (44, 51): rect(x, 105, x, 116, "water0")
for y in (107, 111):
    for x in (41, 47, 54): rect(x, y, x + 1, y, "wood0")                            # small wooden bridges
for y in range(105, 117):
    for x in (44, 51):
        if y in (107, 111): put(x, y, "water0")
for x in (44, 51):
    for y in (106, 110): put(x, y, "water0")
# --- west of the 405 / north of the 10 and south: houses (Inglewood-ish)
houses(37, 128, 58, 138)
rect(41, 131, 50, 138, "sidewalk1"); bld(42, 132, 48, 134, "roof_garage", False); rect(42, 135, 48, 137, "spray_pad")     # Pay 'n' Spray (Inglewood)
# --- Hollywood: Hollywood Blvd with the Walk of Fame, Chinese Theatre, Capitol Records
for x in range(84, 116):
    for y in (57, 66):
        if tiles[y][x] in ("sidewalk0", "sidewalk1"): tiles[y][x] = "star_walk" if x % 3 else "sidewalk0"
bld(86, 67, 95, 70, "roof_theatre", False); rect(86, 71, 95, 73, "plaza0")           # TCL Chinese Theatre + forecourt
for i in range(3):
    for j in range(3): put(111 + j, 70 + i, "tower_stone0" if (i, j) != (1, 1) else "tower_stone1")     # Capitol Records tower
shops(70, 67, 75, 77, colors=("white", "blue", "orange", "tan", "teal"), paving="sidewalk1")
shops(84, 74, 95, 77, colors=("white", "blue", "orange", "tan", "teal"), paving="sidewalk1")
shops(104, 67, 115, 77, colors=("brown", "gray", "red", "white"), paving="sidewalk1")
rect(104, 67, 109, 77, "sidewalk1"); bld(105, 70, 109, 72, "roof_garage", False); rect(105, 67, 109, 69, "spray_pad")     # Pay 'n' Spray (Hollywood)
# --- Sunset Strip + West Hollywood (clubs, billboards) and Silver Lake
shops(70, 29, 75, 39, colors=("orange", "blue", "teal", "white")); shops(84, 29, 95, 39, colors=("orange", "teal", "blue", "white", "red"))
shops(70, 48, 75, 57, colors=("tan", "white", "gray")); 
park(85, 49, 94, 56, pond=(90, 52, 3, 2), palms=True)                                # Silver Lake reservoir
houses(70, 29, 75, 39, ("red", "tan", "white"))
# --- Elysian Park (north-east of downtown) and Dodger Stadium
park(105, 29, 114, 56, pond=None, palms=False)
stadium(125, 29, 136, 44); parking(125, 45, 136, 56)
# --- downtown
towers(85, 88, 94, 94); towers(105, 88, 114, 94); towers(85, 105, 94, 116); towers(105, 105, 114, 116)
for i in range(4):
    for j in range(4): put(108 + j, 89 + i, "tower_stone0" if (i in (1, 2) and j in (1, 2)) else "tower_stone1")     # City Hall
bld(125, 88, 136, 92, "roof_station", False); rect(125, 93, 131, 95, "plaza0"); rect(134, 93, 136, 95, "plaza0")    # Union Station
parking(125, 105, 131, 116); shops(134, 104, 136, 116, colors=("gray", "dark", "orange"))                           # rail yard neighbours
# --- industrial along the river and the freeway
shops(125, 67, 136, 77, colors=("gray", "dark", "tan", "orange"), paving="sidewalk1")
shops(156, 0, 159, 159, colors=("gray", "dark", "tan", "orange"), paving="sidewalk1")
# --- mid-city: Koreatown apartments, strip malls, hospital
shops(70, 88, 75, 116, colors=("tan", "white", "blue", "orange"))
shops(70, 128, 75, 138, colors=("tan", "white", "blue", "orange", "teal"))
parking(84, 128, 95, 138, False)
bld(85, 129, 94, 135, "roof_white0", False)
for i in range(9): put(88 + i % 3, 130 + i // 3, f"hosp_{i}")                                   # hospital with a red cross
# --- Exposition Park: Coliseum
stadium(105, 128, 114, 138, ring=2)
park(125, 128, 136, 138, pond=(130, 133, 2, 1), palms=True)
# --- LAX
for y in range(148, 152):
    for x in range(24, 137): put(x, y, "parking_v" if (x // 8) % 2 == 0 else "plaza1")     # (put() leaves the freeway alone)
for x0_ in (40, 62, 84, 104):
    bld(x0_, 148, x0_ + 12, 150, f"roof_{R.choice(['white', 'gray'])}0")                             # terminals
for i in range(3):
    for j in range(3): put(72 + j, 148 + i, "tower_stone0")                                          # the Theme Building
for y in range(153, 157):
    for x in range(24, 137): put(x, y, "runway")
for x in range(24, 137):
    if (x // 8) % 2 == 0: put(x, 154, "runway_dash"); put(x, 155, "runway_dash")
rect(24, 157, 137, 159, "grass0"); rect(24, 157, 137, 157, "runway")
rect(28, 153, 32, 152, "runway")
# north-of-Hollywood x 96..103 etc. are streets; fill the gaps between: apartments near the 101 and 10
shops(37, 66, 58, 66, colors=("white",))
# --- palm-lined boulevards (Beverly Hills, Hollywood, Wilshire)
PALM_ZONES = [(36, 28, 59, 95), (69, 28, 115, 77), (36, 104, 59, 139)]
for (px0, py0, px1, py1) in PALM_ZONES:
    for y in range(py0, py1 + 1):
        for x in range(px0, px1 + 1):
            if free(x, y, GRASSES) and (x + y) % 3 == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if get(x + dx, y + dy) == "sidewalk0": put(x, y, "palm"); break

# ---------------------------------------------------------------- final touches
for y in range(H):
    for x in range(W):
        n = tiles[y][x]
        if n == "sidewalk0": tiles[y][x] = R.choice(["sidewalk0", "sidewalk0", "sidewalk1"])
        elif n in GRASSES: tiles[y][x] = R.choice(list(GRASSES))
        elif n == "dirt0": tiles[y][x] = R.choice(["dirt0", "dirt1"])

def solid(x, y): return not inside(x, y) or bool(ts.TILE_FLAGS[tiles[y][x]] & ts.F_SOLID)
for _ in range(8):                                   # open up any tiny walkable pocket that got walled in by trees/pools
    seen = [[False] * W for _ in range(H)]
    changed = False
    for sy in range(H):
        for sx in range(W):
            if seen[sy][sx] or solid(sx, sy): continue
            comp, stack = [], [(sx, sy)]
            seen[sy][sx] = True
            while stack:
                x, y = stack.pop(); comp.append((x, y))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if inside(nx, ny) and not seen[ny][nx] and not solid(nx, ny): seen[ny][nx] = True; stack.append((nx, ny))
            if len(comp) < 60:
                for (x, y) in comp:
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if inside(nx, ny) and tiles[ny][nx] in ("tree0", "tree1", "palm", "bush", "pool0", "water0", "water1"):
                            tiles[ny][nx] = "grass0" if tiles[ny][nx] in ("tree0", "tree1", "palm", "bush") else "sidewalk1"; changed = True
    if not changed: break

for y in range(H):
    for x in range(W):
        assert tiles[y][x] in ts.TILE_ID, (x, y, tiles[y][x])

SPAWN = (92, 57)                                     # Hollywood Blvd sidewalk, by the Walk of Fame
if ts.TILE_FLAGS[tiles[SPAWN[1]][SPAWN[0]]] & ts.F_SOLID:
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            if not (ts.TILE_FLAGS[tiles[SPAWN[1] + dy][SPAWN[0] + dx]] & ts.F_SOLID): SPAWN = (SPAWN[0] + dx, SPAWN[1] + dy); break
        else: continue
        break

# ---------------------------------------------------------------- districts and landmark labels (for the HUD)
ZONES = [   # name, x0, y0, x1, y1 (tiles); later entries win
    ("Los Angeles", 0, 0, 159, 159),
    ("Pacific Ocean", 0, 0, 15, 159),
    ("Malibu", 10, 0, 40, 19),
    ("Hollywood Hills", 28, 0, 137, 19),
    ("Griffith Park", 104, 0, 137, 19),
    ("Santa Monica", 16, 28, 35, 70),
    ("Venice Beach", 16, 71, 35, 117),
    ("Beverly Hills", 36, 28, 68, 57),
    ("Rodeo Drive", 36, 66, 68, 77),
    ("West Hollywood", 69, 28, 95, 47),
    ("Silver Lake", 84, 48, 95, 57),
    ("Hollywood", 69, 48, 115, 77),
    ("Elysian Park", 104, 28, 123, 57),
    ("Dodger Stadium", 124, 28, 137, 57),
    ("Downtown LA", 84, 87, 123, 117),
    ("Union Station", 124, 87, 137, 117),
    ("Venice Canals", 36, 104, 59, 117),
    ("Mid-City", 69, 87, 83, 117),
    ("Koreatown", 69, 127, 95, 139),
    ("Inglewood", 24, 127, 59, 139),
    ("Exposition Park", 96, 127, 137, 139),
    ("LAX", 22, 148, 137, 159),
    ("LA River", 138, 0, 146, 159),
    ("Freeway 405", 60, 0, 68, 159),
    ("Freeway 5", 147, 0, 155, 159),
    ("Industrial", 156, 0, 159, 159),
]
LANDMARKS = [   # label, tile x, tile y
    ("Santa Monica Pier", 20, 60), ("Pacific Park", 8, 59), ("Venice Boardwalk", 22, 74), ("Muscle Beach", 21, 86), ("Venice Canals", 48, 103),
    ("Rodeo Drive", 48, 66), ("Hollywood Sign", 95, 2), ("Griffith Observatory", 121, 8), ("Chinese Theatre", 90, 66), ("Walk of Fame", 100, 55),
    ("Capitol Records", 111, 66), ("Silver Lake", 90, 48), ("Dodger Stadium", 130, 28), ("City Hall", 110, 87), ("Union Station", 130, 86),
    ("LA River", 141, 40), ("LA Coliseum", 110, 127), ("LAX", 94, 147), ("Theme Building", 73, 147), ("Sunset Blvd", 60, 38),
]

# ---------------------------------------------------------------- gameplay data
HOSPITAL_RESPAWN = (89, 137)
POLICE_RESPAWN = (43, 95)
SPRAY_PADS = [(105, 67, 109, 69), (42, 135, 48, 137)]
for (x, y) in (HOSPITAL_RESPAWN, POLICE_RESPAWN):
    assert not (ts.TILE_FLAGS[tiles[y][x]] & ts.F_SOLID), (x, y, tiles[y][x])
def walkable_floor(x, y):
    return inside(x, y) and tiles[y][x] in ("sidewalk0", "sidewalk1", "plaza0", "plaza1", "grass0", "grass1", "grass2", "sand0", "sand1", "star_walk", "wood0", "wood1", "scrub0", "scrub1")
PR = random.Random(4242)
pickups = []
def add_pickups(kind, count, min_dist=9):
    tries = 0
    while count and tries < 4000:
        tries += 1
        x, y = PR.randint(24, 155), PR.randint(2, 156)
        if not walkable_floor(x, y): continue
        if any(abs(x - px) + abs(y - py) < min_dist for (px, py, _) in pickups): continue
        pickups.append((x, y, kind)); count -= 1
add_pickups(0, 10); add_pickups(1, 8); add_pickups(2, 6); add_pickups(3, 6); add_pickups(4, 5); add_pickups(5, 16)
parked = []
for y in range(H):
    for x in range(W):
        n = tiles[y][x]
        if n == "parking_v" and x % 2 == 0 and y % 3 == 0 and tiles[y + 1][x] == "parking_v" and len(parked) < 140:
            parked.append((x, y, PR.choice([0, 8])))
        elif n == "parking_h" and x % 3 == 0 and y % 2 == 0 and tiles[y][x + 1] == "parking_h" and len(parked) < 140:
            parked.append((x, y, PR.choice([4, 12])))

# ---------------------------------------------------------------- write outputs
with open(os.path.join(HERE, "map.bin"), "wb") as f:
    f.write(struct.pack("<HHHH", W, H, *SPAWN))
    f.write(bytes(ts.TILE_ID[tiles[y][x]] for y in range(H) for x in range(W)))

def mm_color(n):
    if n.startswith("hsign_"): return (240, 240, 244)
    if n.startswith("hosp_"): return (230, 60, 60)
    if n.startswith("obs_"): return (120, 200, 170)
    if n.startswith("ferris_"): return (250, 200, 60)
    if n.startswith("tower_"): return {"tower_glass": (86, 140, 200), "tower_dark": (52, 62, 92), "tower_stone": (206, 196, 174)}[n[:-1]]
    if n == "stand": return (150, 150, 170)
    if n == "roof_theatre": return (200, 60, 60)
    if n.startswith("roof_"):
        c = n[5:-1] if n[-1] in "0123" else n[5:]
        return {"gray": (112, 114, 122), "tan": (180, 156, 110), "brown": (118, 84, 62), "red": (176, 74, 60), "blue": (84, 112, 170),
                "teal": (70, 150, 160), "white": (206, 208, 214), "dark": (66, 68, 80), "green": (80, 130, 92), "orange": (238, 134, 46),
                "police": (40, 56, 110), "station": (176, 84, 60), "garage": (124, 70, 156)}.get(c, (120, 120, 120))
    if n.startswith("ocean"): return (30, 90, 170)
    if n.startswith("water") or n == "pool0": return (84, 150, 230)
    if n == "stream": return (110, 160, 200)
    if n.startswith("concrete"): return (170, 172, 176)
    if n.startswith("road") or n in ("cw_v", "cw_h", "rail_x_v", "rail_x_h"): return (58, 60, 66)
    if n.startswith("fw"): return (34, 34, 40)
    if n.startswith("runway"): return (50, 52, 58)
    if n == "star_walk": return (236, 84, 150)
    if n.startswith("sidewalk"): return (180, 176, 166)
    if n.startswith("plaza"): return (214, 190, 152)
    if n.startswith("parking"): return (86, 88, 94)
    if n.startswith("rail"): return (110, 96, 80)
    if n == "spray_pad": return (150, 90, 200)
    if n.startswith("wood"): return (150, 110, 70)
    if n.startswith("sand") or n == "wetsand": return (228, 210, 160) if n != "wetsand" else (190, 170, 126)
    if n.startswith("scrub"): return (150, 140, 86)
    if n.startswith("tree") or n == "palm": return (36, 100, 44)
    if n == "bush": return (52, 120, 56)
    if n.startswith("dirt"): return (156, 118, 74)
    if n.startswith("field"): return (76, 160, 66)
    if n == "track": return (190, 88, 60)
    if n == "court0": return (196, 98, 58)
    return (78, 142, 62)

mm = Image.new("RGB", (W, H))
for y in range(H):
    for x in range(W): mm.putpixel((x, y), mm_color(tiles[y][x]))
mm.convert("RGBA").save(os.path.join(HERE, "gfx", "minimap.png"))

with open(os.path.join(HERE, "..", "source", "gen_map.h"), "w") as f:
    f.write("// GENERATED by assets/make_map.py - do not edit\n#pragma once\n\n")
    f.write(f"#define MAP_W {W}\n#define MAP_H {H}\n\n")
    f.write("typedef struct { const char *name; int x0, y0, x1, y1; } Zone;\n")
    f.write("__attribute__((unused)) static const Zone zones[] = {\n" + "".join(f'\t{{"{n}", {a}, {b}, {c}, {d}}},\n' for (n, a, b, c, d) in ZONES) + "};\n")
    f.write("#define NZONES (int)(sizeof zones / sizeof zones[0])\n\n")
    f.write("typedef struct { const char *name; int tx, ty; } Landmark;\n")
    f.write("__attribute__((unused)) static const Landmark landmarks[] = {\n" + "".join(f'\t{{"{n}", {x}, {y}}},\n' for (n, x, y) in LANDMARKS) + "};\n")
    f.write("#define NLANDMARKS (int)(sizeof landmarks / sizeof landmarks[0])\n\n")
    f.write("// streets: a0..a1 = tiles across the street, b0..b1 = tiles along it. hroads run along x, vroads along y.\n")
    f.write("typedef struct { int a0, a1, b0, b1; } RoadDef;\n")
    f.write("__attribute__((unused)) static const RoadDef hroads[] = {\n" + "".join(f"\t{{{y0}, {y1}, {x0}, {x1}}},\n" for (y0, y1, x0, x1) in HROADS) + "};\n")
    f.write("__attribute__((unused)) static const RoadDef vroads[] = {\n" + "".join(f"\t{{{x0}, {x1}, {y0}, {y1}}},\n" for (x0, x1, y0, y1) in VROADS) + "};\n")
    f.write("#define NHROADS (int)(sizeof hroads / sizeof hroads[0])\n#define NVROADS (int)(sizeof vroads / sizeof vroads[0])\n\n")
    f.write(f"#define HOSPITAL_X {HOSPITAL_RESPAWN[0]}\n#define HOSPITAL_Y {HOSPITAL_RESPAWN[1]}\n#define POLICE_X {POLICE_RESPAWN[0]}\n#define POLICE_Y {POLICE_RESPAWN[1]}\n\n")
    f.write("typedef struct { int x0, y0, x1, y1; } SprayPad;\n__attribute__((unused)) static const SprayPad spray_pads[] = {\n" + "".join(f"\t{{{a}, {b}, {c}, {d}}},\n" for (a, b, c, d) in SPRAY_PADS) + "};\n#define NSPRAY (int)(sizeof spray_pads / sizeof spray_pads[0])\n\n")
    f.write("typedef struct { int tx, ty, kind; } PickupSpot;      // kind: 0 health, 1 armor, 2 pistol, 3 smg, 4 shotgun, 5 cash\n")
    f.write("__attribute__((unused)) static const PickupSpot pickup_spots[] = {\n" + "".join(f"\t{{{x}, {y}, {k}}},\n" for (x, y, k) in pickups) + "};\n#define NPICKUPS (int)(sizeof pickup_spots / sizeof pickup_spots[0])\n\n")
    f.write("typedef struct { int tx, ty, dir; } ParkSpot;         // dir: heading in 1/16 turns clockwise from up\n")
    f.write("__attribute__((unused)) static const ParkSpot park_spots[] = {\n" + "".join(f"\t{{{x}, {y}, {d}}},\n" for (x, y, d) in parked) + "};\n#define NPARK (int)(sizeof park_spots / sizeof park_spots[0])\n")

mm.resize((W * 6, H * 6), Image.NEAREST).save(os.path.join(HERE, "map_preview.png"))
print("map written; spawn", SPAWN)
