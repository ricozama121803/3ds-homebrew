"""Cars for GTO II: a station-lofted body (hood, windshield, roof, rear glass all come from one cross-section per station), wheels,
lights, bumpers, mirrors. ~400 triangles each. Usage: blender-venv/python build_cars.py  (writes cars.blend with every model side by side)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

GLASS = lambda: material("glass", "#1E2C3C", 0.15)
DARK = lambda: material("trim", "#18181C", 0.6)
TIRE = lambda: material("tire", "#141416", 0.9)
HUB = lambda: material("hub", "#B9BBC2", 0.35)
HEAD = lambda: material("headlight", "#FFF4C8", 0.2, 4.0)
TAIL = lambda: material("taillight", "#E0141C", 0.3, 2.5)
CHROME = lambda: material("chrome", "#C9CCD2", 0.25)
WHITE = lambda: material("white", "#F2F2EE", 0.5)

# station: (y, [bottom (w,z)], [lower side (w,z)], [belt (w,z)], [roof edge (w,z)]) running front (-Y) to rear (+Y)
SPECS = {
    "sedan": dict(
        L=4.5, wheelR=0.33, front=-1.40, rear=1.35, track=0.86, glass_sides=(2, 5), windshield=(2, 3), rear_glass=(4, 5),
        st=[(-2.25, (.78, .30), (.88, .44), (.84, .66), (.70, .68)),
            (-1.70, (.86, .28), (.93, .48), (.90, .86), (.72, .88)),
            (-0.95, (.90, .28), (.94, .52), (.93, .92), (.80, .94)),
            (-0.25, (.90, .28), (.94, .52), (.93, .94), (.70, 1.46)),
            (0.95, (.90, .28), (.94, .52), (.93, .94), (.68, 1.46)),
            (1.45, (.90, .28), (.94, .52), (.93, .95), (.78, .97)),
            (2.10, (.88, .28), (.94, .50), (.92, .95), (.80, .96)),
            (2.25, (.80, .30), (.89, .44), (.85, .88), (.74, .90))]),
    "sports": dict(
        L=4.4, wheelR=0.335, front=-1.38, rear=1.32, track=0.90, glass_sides=(2, 5), windshield=(2, 3), rear_glass=(4, 5),
        st=[(-2.20, (.80, .22), (.92, .36), (.88, .54), (.70, .56)),
            (-1.60, (.90, .20), (.98, .40), (.94, .68), (.70, .70)),
            (-0.60, (.95, .20), (1.0, .44), (.97, .80), (.82, .82)),
            (0.05, (.95, .20), (1.0, .44), (.97, .82), (.66, 1.22)),
            (0.80, (.95, .20), (1.0, .44), (.97, .84), (.64, 1.20)),
            (1.55, (.95, .20), (1.0, .44), (.98, .90), (.80, .92)),
            (2.05, (.93, .22), (.99, .42), (.95, .92), (.80, .94)),
            (2.20, (.84, .24), (.92, .40), (.88, .84), (.72, .86))]),
    "van": dict(
        L=4.9, wheelR=0.35, front=-1.55, rear=1.50, track=0.90, glass_sides=(2, 4), windshield=(2, 3), rear_glass=None,
        st=[(-2.45, (.80, .30), (.90, .50), (.88, .96), (.78, 1.0)),
            (-2.00, (.92, .30), (.97, .56), (.95, 1.06), (.80, 1.10)),
            (-1.55, (.96, .30), (.99, .60), (.97, 1.12), (.86, 1.16)),
            (-0.95, (.96, .30), (.99, .60), (.97, 1.14), (.90, 2.02)),
            (-0.50, (.96, .30), (.99, .60), (.97, 1.14), (.90, 2.04)),
            (2.30, (.96, .30), (.99, .60), (.97, 1.14), (.90, 2.04)),
            (2.45, (.90, .32), (.96, .60), (.94, 1.10), (.84, 1.96))]),
}
SPECS["taxi"] = SPECS["sedan"]
SPECS["police"] = SPECS["sedan"]
SPECS["swat"] = SPECS["van"]


def body(b, spec, bodymat, kind):
    """Loft the stations into one body: window faces get the glass material, the underside the dark one."""
    G, D = GLASS(), DARK()
    st = spec["st"]
    rings = []
    for y, bot, low, belt, roof in st:
        pts = [(bot[0], bot[1]), (low[0], low[1]), (belt[0], belt[1]), (roof[0], roof[1]), (-roof[0], roof[1]), (-belt[0], belt[1]), (-low[0], low[1]), (-bot[0], bot[1])]
        rings.append([V(x, y, z) for x, z in pts])
    bm = b.bm
    vs = [[bm.verts.new(p) for p in r] for r in rings]
    gs, ws, rg = spec["glass_sides"], spec["windshield"], spec["rear_glass"]
    for i in range(len(rings) - 1):
        for k in range(8):
            m = bodymat
            if k == 7:
                m = D
            elif k in (2, 4) and gs and gs[0] <= i < gs[1]:
                m = G
            elif k == 3 and ((ws and ws[0] == i) or (rg and rg[0] == i)):
                m = G
            if kind == "police" and k in (0, 1, 5, 6) and 2 <= i < 5:                       # white doors on the black-and-white
                m = WHITE()
            f = bm.faces.new((vs[i][k], vs[i][(k + 1) % 8], vs[i + 1][(k + 1) % 8], vs[i + 1][k]))
            f.material_index = b.mi(m)
    for ring, mm in ((vs[0], bodymat), (vs[-1], bodymat)):
        f = bm.faces.new(ring); f.material_index = b.mi(mm)


WHEEL_R = 1.0                                     # the wheel mesh has radius 1 and half width 0.13 (scaled by the car's wheel radius at draw time); its hub faces +X


def wheel_mesh():
    """tire (8-sided), hub plate and a bar across it so the rolling shows"""
    b = Builder("wheel")
    u, v = V(0, 1, 0), V(0, 0, 1)
    b.loft([b.ring(V(-0.13, 0, 0), u, v, 1, 1, 8), b.ring(V(0.13, 0, 0), u, v, 1, 1, 8)], TIRE(), caps=(False, True))
    hubr = 0.60
    hub = [V(0.135, math.cos(k * math.tau / 8) * hubr, math.sin(k * math.tau / 8) * hubr) for k in range(8)]
    f = b.bm.faces.new([b.vert(p) for p in hub]); f.material_index = b.mi(HUB())
    bar = [V(0.139, -hubr * 0.9, -0.09), V(0.139, hubr * 0.9, -0.09), V(0.139, hubr * 0.9, 0.09), V(0.139, -hubr * 0.9, 0.09)]
    f = b.bm.faces.new([b.vert(p) for p in bar]); f.material_index = b.mi(DARK())
    return b.build()


def wheel_spots(spec):
    """[(x, y, z, side)] for the four wheels; side +1 = left (+X in Blender)"""
    R = spec["wheelR"]
    return [(s * spec["track"], fy, R, s) for fy in (spec["front"], spec["rear"]) for s in (-1, 1)]


def details(b, spec, kind):
    st = spec["st"]
    nose, tail = st[0][0], st[-1][0]
    wN, wT = st[0][3][0], st[-1][3][0]
    zN = st[0][3][1]
    zT = st[-1][3][1]
    D, H, T, C = DARK(), HEAD(), TAIL(), CHROME()
    van = kind == "van"
    # bumpers
    b.box((0, nose - 0.02, st[0][1][1] + 0.12), (wN + 0.07, 0.07, 0.11), D)
    b.box((0, tail + 0.02, st[-1][1][1] + 0.12), (wT + 0.06, 0.07, 0.11), D)
    # grille + headlights
    b.box((0, nose - 0.012, zN - 0.12), (0.30, 0.012, 0.06), D)
    for s in (-1, 1):
        b.box((s * (wN - 0.22), nose - 0.012, zN - 0.04), (0.16, 0.014, 0.065), H)
        b.box((s * (wT - 0.22), tail + 0.012, zT - 0.05), (0.20, 0.014, 0.06), T)
        b.box((s * (wT - 0.05), tail + 0.01, zT - 0.12), (0.03, 0.012, 0.03), HEAD() if False else material("amber", "#FF9A1C", 0.3, 1.5))
    b.box((0, tail + 0.016, zT - 0.24), (0.20, 0.010, 0.055), WHITE())                       # plate
    # mirrors
    wi = st[spec["windshield"][0]]
    for s in (-1, 1):
        b.box((s * (wi[2][0] + 0.07), wi[0] + 0.10, wi[2][1] + 0.03), (0.07, 0.04, 0.045), D)
    # door handles
    for s in (-1, 1):
        for y in ((-0.3, 0.8) if not van else (-1.0,)):
            b.box((s * (st[3][3][0] + 0.005), y, st[3][3][1] - 0.1), (0.008, 0.10, 0.015), C)


def build_car(kind, color, name=None):
    spec = SPECS[kind]
    b = Builder(name or kind)
    bodymat = material("paint", color, 0.35)                       # 'paint' faces are tinted with the car's colour in game
    if kind == "taxi":
        bodymat = material("fixed_taxi", "#F4C20D", 0.35)
    if kind == "police":
        bodymat = material("fixed_police", "#15161A", 0.35)
    if kind == "swat":
        bodymat = material("fixed_swat", "#1F232C", 0.4)
    body(b, spec, bodymat, kind)
    details(b, spec, kind)
    st = spec["st"]
    if kind == "taxi":
        roofz = st[3][4][1]
        b.box((0, 0.35, roofz + 0.09), (0.30, 0.14, 0.09), material("sign", "#FFF1B0", 0.4, 2.0))
        b.box((0, 0.35, roofz + 0.005), (0.20, 0.10, 0.012), DARK())
        for i in range(8):                                                           # checker stripe along the belt line
            for s in (-1, 1):
                if i % 2 == 0:
                    b.box((s * 0.935, -0.9 + i * 0.28, st[3][3][1] - 0.13), (0.006, 0.13, 0.05), DARK())
    if kind == "police":
        roofz = st[3][4][1]
        b.box((-0.2, 0.2, roofz + 0.06), (0.17, 0.11, 0.055), material("lightR", "#FF2020", 0.3, 3.0))
        b.box((0.2, 0.2, roofz + 0.06), (0.17, 0.11, 0.055), material("lightB", "#2050FF", 0.3, 3.0))
        b.box((0, 0.2, roofz + 0.02), (0.38, 0.12, 0.02), DARK())
    if kind == "swat":
        roofz = st[3][4][1]
        b.box((-0.35, -0.2, roofz + 0.06), (0.24, 0.12, 0.055), material("lightR", "#FF2020", 0.3, 3.0))
        b.box((0.35, -0.2, roofz + 0.06), (0.24, 0.12, 0.055), material("lightB", "#2050FF", 0.3, 3.0))
        b.box((0, -0.2, roofz + 0.02), (0.64, 0.14, 0.02), DARK())
        for s in (-1, 1):                                                            # white POLICE band along both sides and a stripe on the roof
            b.box((s * 0.985, 0.9, 1.05), (0.006, 1.5, 0.12), WHITE())
        b.box((0, 1.0, roofz + 0.012), (0.3, 1.2, 0.01), WHITE())
    if kind == "van":
        for s in (-1, 1):                                                            # a stripe so the cargo body isn't one slab
            b.box((s * 0.975, 0.9, 0.95), (0.006, 1.2, 0.03), DARK())
    return b.build()


LINEUP = [("sedan", "#C8281E"), ("sports", "#F0B400"), ("van", "#E8E8E4"), ("taxi", "#F4C20D"), ("police", None), ("swat", None)]

def add_wheels(kind, car):
    spec = SPECS[kind]
    me = bpy.data.objects["wheel"].data if "wheel" in bpy.data.objects else wheel_mesh().data
    for i, (x, y, z, side) in enumerate(wheel_spots(spec)):
        w = bpy.data.objects.new(f"{kind}_wheel{i}", me)
        bpy.context.collection.objects.link(w)
        w.parent = car
        w.location = (x, y, z)
        w.scale = (side, spec["wheelR"], spec["wheelR"])                 # mirrored for the left side so the hub faces outward
    return me


if __name__ == "__main__":
    reset()
    objs = []
    wm = None
    for i, (k, c) in enumerate(LINEUP):
        o = build_car(k, c or "#FFFFFF")
        o.location = (i * 3.6, 0, 0)
        objs.append(o)
        print(k, "triangles:", tri_count([o]))
        add_wheels(k, o)
    wo = bpy.data.objects["wheel"]
    wo.hide_render = True
    wo.hide_viewport = True
    print("wheel triangles:", tri_count([wo]))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "cars.blend"))
