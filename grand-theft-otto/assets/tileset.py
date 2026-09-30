"""Catalog of every image in the texture atlas. Single source of truth shared by make_art.py (draws them),
make_map.py (lays out the city by tile name) and the generated C header (source/gen_atlas.h)."""

F_SOLID, F_BUILDING, F_TREE, F_WATER, F_ROAD, F_TALL, F_SPRAY = 1, 2, 4, 8, 16, 32, 64

ROOF_COLORS = ["gray", "tan", "brown", "red", "blue", "teal", "white", "dark", "green", "orange"]
SIGN_ROWS, SIGN_COLS = 3, 18            # the Hollywood sign, made of 54 tiles

TILES = []          # (name, flags)
def T(name, flags=0):
    TILES.append((name, flags))

# ---- ground
for i in range(3): T(f"grass{i}")
for i in range(2): T(f"dirt{i}")
for i in range(2): T(f"scrub{i}")
for i in range(2): T(f"sand{i}")
T("wetsand")
for i in range(2): T(f"sidewalk{i}")
for i in range(2): T(f"plaza{i}")
T("star_walk")
for i in range(2): T(f"wood{i}")
for i in range(2): T(f"concrete{i}")
T("court0")
T("spray_pad", F_SPRAY)                  # Pay 'n' Spray: drive in here to lose the cops
# ---- roads
T("road", F_ROAD)
for n in ["road_hy_b", "road_hy_t", "road_hw_b", "road_hw_t", "road_vy_r", "road_vy_l", "road_vw_r", "road_vw_l", "cw_v", "cw_h"]:
    T(n, F_ROAD)
T("parking_v"); T("parking_h")
for n in ["fw", "fw_dash_l", "fw_dash_r", "fw_edge_l", "fw_edge_r"]:
    T(n, F_ROAD)
T("fw_barrier", F_SOLID)
for n in ["fw_edge_t", "fw_edge_b", "fw_dash_t", "fw_dash_b"]:
    T(n, F_ROAD)
T("fw_barrier_h", F_SOLID)
T("rail_h"); T("rail_v"); T("rail_x_v", F_ROAD); T("rail_x_h", F_ROAD)
T("runway", F_ROAD); T("runway_dash")
# ---- water
for i in range(2): T(f"ocean{i}", F_SOLID | F_WATER)
for i in range(2): T(f"water{i}", F_SOLID | F_WATER)
T("stream", F_WATER)                     # shallow water in the LA River channel: you can wade through it
T("pool0", F_SOLID | F_WATER)
# ---- roofs
for c in ROOF_COLORS:
    for v in range(4):
        T(f"roof_{c}{v}", F_SOLID | F_BUILDING)
for n in ["roof_theatre", "roof_garage", "roof_police", "roof_station", "stand"]:
    T(n, F_SOLID | F_BUILDING)
for i in range(9): T(f"hosp_{i}", F_SOLID | F_BUILDING)
for n in ["tower_glass", "tower_dark", "tower_stone"]:
    for v in range(2): T(f"{n}{v}", F_SOLID | F_BUILDING | F_TALL)
for i in range(9): T(f"obs_{i}", F_SOLID | F_BUILDING)                       # Griffith Observatory
for i in range(16): T(f"ferris_{i}", F_SOLID | F_BUILDING | F_TALL)          # Santa Monica Ferris wheel
T("field0"); T("field1"); T("track")
# ---- trees and the sign
T("tree0", F_SOLID | F_TREE); T("tree1", F_SOLID | F_TREE); T("palm", F_SOLID | F_TREE); T("bush", F_SOLID)
for r in range(SIGN_ROWS):
    for c in range(SIGN_COLS): T(f"hsign_{r}_{c}", F_SOLID)

TILE_NAMES = [n for n, _ in TILES]
TILE_ID = {n: i for i, n in enumerate(TILE_NAMES)}
TILE_FLAGS = {n: f for n, f in TILES}
NTILES = len(TILES)
assert NTILES <= 250, NTILES

# Other atlas images (drawn by make_art.py). Sprites have a 2px transparent margin.
DIRS = 16                      # pre-rotated directions for the player and cars (0 = up, clockwise)
PED_DIRS = 8                   # pedestrians get 8 directions
WALK_FRAMES = 4                # player walk cycle
PED_FRAMES = 2
OVERLAYS = ["edge_n", "edge_e", "edge_s", "edge_w"]          # dark outline drawn over building roofs
# car variants: (model, colour). Models: 0 compact, 1 sedan, 2 sports, 3 van, 4 taxi, 5 police, 6 SWAT
CARVARS = [(0, 0), (0, 1), (1, 0), (1, 1), (1, 2), (2, 0), (2, 1), (3, 0), (4, 0), (5, 0), (6, 0)]
PEDTYPES = 8                   # 0-5 civilians, 6 cop, 7 SWAT
EFFECTS = (["muzzle", "bullet", "spark", "blood_dot", "glow_red", "glow_blue", "shadow_car"]
           + [f"stain{i}" for i in range(4)] + [f"boom{i}" for i in range(6)] + [f"smoke{i}" for i in range(3)] + [f"fire{i}" for i in range(3)]
           + ["pk_health", "pk_armor", "pk_pistol", "pk_smg", "pk_shotgun", "pk_cash"]
           + ["ic_fists", "ic_pistol", "ic_smg", "ic_shotgun"])
SPRITES = ["canopy0", "canopy1", "palmtop", "shadow"]
SPRITES += [f"player_{f}_{d}" for f in range(WALK_FRAMES) for d in range(DIRS)]
SPRITES += [f"car_{v}_{d}" for v in range(len(CARVARS)) for d in range(DIRS)]
SPRITES += [f"ped_{t}_{f}_{d}" for t in range(PEDTYPES) for f in range(PED_FRAMES) for d in range(PED_DIRS)]
SPRITES += [f"corpse_{t}_{k}" for t in range(PEDTYPES) for k in range(4)]
SPRITES += EFFECTS
EXTRAS = ["minimap"]

ALL_IMAGES = TILE_NAMES + OVERLAYS + SPRITES + EXTRAS
IMG_INDEX = {n: i for i, n in enumerate(ALL_IMAGES)}
