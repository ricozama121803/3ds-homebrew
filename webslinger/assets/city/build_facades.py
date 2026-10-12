"""Renders the building facade textures from real geometry with Cycles (recessed windows, sills, lintels, brick courses, mullions), so the
shadows and depth are baked into the texture. Each style is modelled as a 3x3 grid of tiles and only the middle tile is kept, so it repeats cleanly.
Output: ../gfx/{red,cream,tan,glass,store}.png (256x256; the game maps 12 m x 7.6 m (glass 10 x 15.2, store 12 x 5) onto them).
Run: ../../../tools/blender-venv/bin/python build_facades.py"""
import math, os, sys, bpy, bmesh
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "gfx")
STYLES = {}


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.samples = 48; sc.cycles.device = "CPU"
    sc.cycles.use_denoising = False
    sc.view_settings.view_transform = "Standard"
    w = bpy.data.worlds.new("w"); w.use_nodes = True
    bg = w.node_tree.nodes["Background"]; bg.inputs[0].default_value = (0.75, 0.85, 1.0, 1); bg.inputs[1].default_value = 0.45
    sc.world = w
    return sc


def mat(name, color, rough=0.8, metal=0.0, brick=False, emit=0.0, scale=1.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    color = tuple(c ** 2.2 for c in color[:3]) + (1,)                 # colours are written as display (sRGB-ish) values
    n = m.node_tree.nodes; l = m.node_tree.links
    b = n["Principled BSDF"]
    b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if brick:                                    # mortar lines + per-brick tone variation, evaluated on object coordinates
        tc = n.new("ShaderNodeTexCoord"); br = n.new("ShaderNodeTexBrick")
        br.inputs["Color1"].default_value = color
        br.inputs["Color2"].default_value = tuple(c * 0.7 for c in color[:3]) + (1,)
        br.inputs["Mortar"].default_value = (0.5, 0.46, 0.4, 1)
        br.inputs["Scale"].default_value = 0.85; br.inputs["Mortar Size"].default_value = 0.05
        br.inputs["Brick Width"].default_value = 0.5; br.inputs["Row Height"].default_value = 0.25
        br.inputs["Bias"].default_value = 0.0
        l.new(tc.outputs["Object"], br.inputs["Vector"]); l.new(br.outputs["Color"], b.inputs["Base Color"])
    else:
        b.inputs["Base Color"].default_value = color
    if emit:
        b.inputs["Emission Color"].default_value = color; b.inputs["Emission Strength"].default_value = emit
    return m


def box(name, x0, x1, y0, y1, z0, z1, m):
    """Axis-aligned box; x across the facade, z up, y out of the facade (toward the camera at -y)."""
    me = bpy.data.meshes.new(name); ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x = x0 if v.co.x < 0 else x1; v.co.y = y0 if v.co.y < 0 else y1; v.co.z = z0 if v.co.z < 0 else z1
    bm.to_mesh(me); bm.free()
    ob.data.materials.append(m)
    return ob


def tile_loop(w, h, fn):
    for ix in (-1, 0, 1):
        for iz in (-1, 0, 1):
            fn(ix * w, iz * h)


def render(name, w, h, res=(512, 512)):
    sc = bpy.context.scene
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3.0; sun.data.angle = math.radians(6)
    sun.rotation_euler = (math.radians(58), math.radians(8), math.radians(28)); bpy.context.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); bpy.context.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    # frame exactly the middle tile (the surrounding tiles only provide shadows)
    cam.data.ortho_scale = max(w, h)
    cam.data.sensor_fit = "AUTO"
    sc.render.resolution_x = res[0]; sc.render.resolution_y = res[1]
    if w >= h: cam.data.ortho_scale = w
    else: cam.data.ortho_scale = h
    cam.location = (0, -30, 0); cam.rotation_euler = (math.radians(90), 0, 0)
    # keep the aspect of the tile: the camera sees `ortho_scale` along the longer side
    sc.render.resolution_x = res[0]; sc.render.resolution_y = int(round(res[0] * h / w)) if w >= h else res[1]
    if h > w: sc.render.resolution_x = int(round(res[1] * w / h))
    sc.camera = cam
    sc.render.filepath = os.path.join("/tmp", f"facade_{name}.png")
    bpy.ops.render.render(write_still=True)
    return sc.render.filepath


def window(ox, oz, ww, wh, depth, frame, glass, sill, lintel, pane_split=True):
    """A recessed window: reveal depth is carved by building the wall from pieces around it (done by the caller); this adds frame, glass, sill, lintel."""
    x0, x1, z0, z1 = ox - ww / 2, ox + ww / 2, oz - wh / 2, oz + wh / 2
    box("glass", x0, x1, depth - 0.02, depth + 0.02, z0, z1, glass)
    f = 0.09
    box("fl", x0, x0 + f, depth - 0.08, depth + 0.05, z0, z1, frame); box("fr", x1 - f, x1, depth - 0.08, depth + 0.05, z0, z1, frame)
    box("ft", x0, x1, depth - 0.08, depth + 0.05, z1 - f, z1, frame); box("fb", x0, x1, depth - 0.08, depth + 0.05, z0, z0 + f, frame)
    if pane_split:
        box("mv", ox - 0.035, ox + 0.035, depth - 0.08, depth + 0.05, z0, z1, frame)
        box("mh", x0, x1, depth - 0.08, depth + 0.05, oz + wh * 0.12 - 0.035, oz + wh * 0.12 + 0.035, frame)
    if sill: box("sill", x0 - 0.18, x1 + 0.18, -0.34, depth + 0.05, z0 - 0.16, z0 + 0.0, sill)
    if lintel: box("lintel", x0 - 0.16, x1 + 0.16, -0.22, depth + 0.05, z1, z1 + 0.28, lintel)


def punched_wall(w, h, ox, oz, holes, wallmat, y_front=0.0, y_back=0.55):
    """A wall slab with rectangular window openings (holes = [(cx, cz, ww, wh)]) built from boxes, so the openings are real reveals."""
    x0, x1, z0, z1 = ox - w / 2, ox + w / 2, oz - h / 2, oz + h / 2
    # slice the wall into horizontal strips at hole edges, and each strip into solid segments between holes
    zs = sorted({z0, z1} | {c - hh / 2 for (_, c, _, hh) in holes} | {c + hh / 2 for (_, c, _, hh) in holes})
    for za, zb in zip(zs[:-1], zs[1:]):
        zm = 0.5 * (za + zb)
        row = sorted([(cx - ww / 2, cx + ww / 2) for (cx, cz, ww, hh) in holes if cz - hh / 2 < zm < cz + hh / 2])
        x = x0
        for (ha, hb) in row:
            if ha > x + 1e-4: box("w", x, ha, y_front, y_back, za, zb, wallmat)
            x = hb
        if x1 > x + 1e-4: box("w", x, x1, y_front, y_back, za, zb, wallmat)
    # the back of each opening (the glass sits in it)
    box("back", x0, x1, y_back, y_back + 0.1, z0, z1, wallmat)


def build_brick(base, glass_col, name, trim_col, bays=4, storeys=2):
    W, H = 12.0, 7.6
    wallm = mat(name + "_wall", base, 0.9)
    trim = mat(name + "_trim", trim_col, 0.7)
    frame = mat(name + "_frame", (0.09, 0.08, 0.07, 1), 0.5)
    glass = mat(name + "_glass", glass_col, 0.12, 0.2)
    holes = []
    for k in range(storeys):
        for b in range(bays):
            holes.append(((b + 0.5) * W / bays - W / 2, (k + 0.5) * H / storeys - H / 2 + 0.15, 1.35, 2.15))

    def make(ox, oz):
        hs = [(cx + ox, cz + oz, ww, wh) for (cx, cz, ww, wh) in holes]
        punched_wall(W, H, ox, oz, hs, wallm)
        for (cx, cz, ww, wh) in hs: window(cx, cz, ww, wh, 0.4, frame, glass, trim, trim)
        for k in range(storeys):                                       # floor-line band
            zc = oz + (k) * H / storeys - H / 2
            box("band", ox - W / 2, ox + W / 2, -0.14, 0.2, zc - 0.12, zc + 0.12, trim)
    tile_loop(W, H, make)
    return W, H


def build_tan(base, name):
    W, H = 12.0, 7.6
    wallm = mat(name + "_wall", base, 0.95)
    trim = mat(name + "_trim", tuple(min(1, c * 1.12) for c in base[:3]) + (1,), 0.8)
    frame = mat(name + "_frame", (0.12, 0.10, 0.08, 1), 0.5)
    glass = mat(name + "_glass", (0.16, 0.22, 0.33, 1), 0.1, 0.2)
    holes = []
    for k in range(2):
        for b in range(4):
            holes.append(((b + 0.5) * 3.0 - 6.0, k * 3.8 - 3.8 + 1.9 + 0.1, 1.3, 2.3))

    def make(ox, oz):
        hs = [(cx + ox, cz + oz, ww, wh) for (cx, cz, ww, wh) in holes]
        punched_wall(W, H, ox, oz, hs, wallm)
        for (cx, cz, ww, wh) in hs: window(cx, cz, ww, wh, 0.4, frame, glass, trim, trim)
        for b in range(5):                                             # shallow pilasters between the bays
            px = ox - W / 2 + b * 3.0
            box("pil", px - 0.2, px + 0.2, -0.12, 0.3, oz - H / 2, oz + H / 2, trim)
        box("band", ox - W / 2, ox + W / 2, -0.2, 0.3, oz - H / 2 - 0.15, oz - H / 2 + 0.15, trim)
    tile_loop(W, H, make)
    return W, H


def build_glass():
    W, H = 10.0, 15.2
    frame = mat("g_frame", (0.12, 0.14, 0.17, 1), 0.35, 0.6)
    glass_a = mat("g_a", (0.38, 0.58, 0.76, 1), 0.04, 0.0)
    glass_b = mat("g_b", (0.55, 0.74, 0.9, 1), 0.04, 0.0)
    spand = mat("g_s", (0.07, 0.09, 0.12, 1), 0.3, 0.5)

    def make(ox, oz):
        cols, rows = 4, 8
        cw, rh = W / cols, H / rows
        for c in range(cols):
            for r in range(rows):
                cx, cz = ox - W / 2 + (c + 0.5) * cw, oz - H / 2 + (r + 0.5) * rh
                box("pane", cx - cw / 2, cx + cw / 2, 0.08, 0.12, cz - rh / 2 + 0.28, cz + rh / 2, glass_a if (c * 3 + r * 5) % 4 else glass_b)
                box("spandrel", cx - cw / 2, cx + cw / 2, 0.02, 0.14, cz - rh / 2, cz - rh / 2 + 0.28, spand)
        for c in range(cols + 1):
            x = ox - W / 2 + c * cw
            box("mullion", x - 0.05, x + 0.05, -0.18, 0.15, oz - H / 2, oz + H / 2, frame)
        for r in range(rows + 1):
            z = oz - H / 2 + r * rh
            box("trans", ox - W / 2, ox + W / 2, -0.1, 0.15, z - 0.04, z + 0.04, frame)
    tile_loop(W, H, make)
    return W, H


def build_store():
    W, H = 12.0, 5.0
    wallm = mat("s_wall", (0.62, 0.56, 0.48, 1), 0.85)
    frame = mat("s_frame", (0.05, 0.05, 0.06, 1), 0.4, 0.6)
    glass = mat("s_glass", (0.45, 0.62, 0.78, 1), 0.04, 0.0)
    glow = mat("s_glow", (1.0, 0.85, 0.55, 1), 0.5, emit=1.3)
    awn = [mat("s_awn%d" % i, c, 0.9) for i, c in enumerate([(0.7, 0.12, 0.1, 1), (0.1, 0.4, 0.22, 1), (0.14, 0.24, 0.6, 1)])]
    stone = mat("s_stone", (0.78, 0.74, 0.66, 1), 0.9)

    def make(ox, oz):
        box("wall", ox - W / 2, ox + W / 2, 0.0, 0.5, oz - H / 2, oz + H / 2, wallm)
        box("plinth", ox - W / 2, ox + W / 2, -0.12, 0.5, oz - H / 2, oz - H / 2 + 0.7, stone)
        for b in range(3):                                            # three shop bays 4 m wide
            cx = ox - W / 2 + (b + 0.5) * 4.0
            box("shopglass", cx - 1.7, cx + 1.7, 0.15, 0.2, oz - H / 2 + 0.9, oz + 0.7, glow if b != 1 else glass)
            for dx in (-1.7, 1.7): box("sf", cx + dx - 0.08, cx + dx + 0.08, -0.05, 0.25, oz - H / 2 + 0.7, oz + 0.8, frame)
            box("sh", cx - 1.8, cx + 1.8, -0.05, 0.25, oz + 0.7, oz + 0.82, frame)
            box("sh2", cx - 1.8, cx + 1.8, -0.05, 0.25, oz - H / 2 + 0.7, oz - H / 2 + 0.95, frame)
            a = awn[b % 3]                                            # sloped canopy as a wedge
            me = bpy.data.meshes.new("awn"); ob = bpy.data.objects.new("awn", me); bpy.context.collection.objects.link(ob)
            zt, zb2 = oz + 1.5, oz + 0.9
            vs = [(cx - 1.9, 0.0, zt), (cx + 1.9, 0.0, zt), (cx + 1.9, -0.9, zb2), (cx - 1.9, -0.9, zb2),
                  (cx - 1.9, 0.0, zt - 0.05), (cx + 1.9, 0.0, zt - 0.05), (cx + 1.9, -0.9, zb2 - 0.05), (cx - 1.9, -0.9, zb2 - 0.05)]
            me.from_pydata(vs, [], [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]); ob.data.materials.append(a)
        for dx in (-2.0, 2.0, 6.0): box("pier", ox + dx - 1.0 + 0.0, ox + dx - 0.8, -0.1, 0.5, oz - H / 2 + 0.9, oz + 1.9, stone) if False else None
        box("cornice", ox - W / 2, ox + W / 2, -0.25, 0.5, oz + H / 2 - 0.5, oz + H / 2, stone)
    tile_loop(W, H, make)
    return W, H


BRICKY = {"red": 0.55, "cream": 0.28, "tan": 0.0}


def bricks(im, name):
    """Brick courses painted over the wall (a real brick is sub-pixel at this size, so courses are exaggerated); skips the dark window pixels."""
    import random
    from PIL import ImageDraw
    random.seed(7)
    px = im.load(); w, h = im.size; strength = BRICKY[name]
    out = im.copy(); op = out.load()
    rows = 32; rh = h // rows; bw = 14
    for r in range(rows):
        off = (r % 2) * bw // 2
        for x in range(0, w + bw, bw):
            tone = 1.0 + random.uniform(-0.12, 0.12) * (strength + 0.4)
            for yy in range(r * rh, (r + 1) * rh):
                for xx in range(x - off, x - off + bw):
                    xx %= w
                    p = px[xx, yy]
                    lum = (p[0] + p[1] + p[2]) / 3
                    if lum < 60 or (p[2] > p[0] + 25): continue                # window / shadow-dark pixels stay as they are
                    k = tone
                    if yy == r * rh + rh - 1: k *= 1.0 - 0.28 * min(1, strength + 0.3)
                    elif xx == (x - off) % w: k *= 1.0 - 0.2 * min(1, strength + 0.3)
                    op[xx, yy] = tuple(max(0, min(255, int(c * k))) for c in p)
    return out


def run(name, builder, args=(), res=(512, 512)):
    sc = reset()
    W, H = builder(*args)
    path = render(name, W, H, res)
    from PIL import Image
    im = Image.open(path).convert("RGB")
    # camera frames only the middle tile (ortho_scale == the longer side): crop to it, resize to 256x256
    if W >= H:
        im = im.crop((0, 0, im.width, im.height))
    out = im.resize((256, 256), Image.LANCZOS)
    if name in BRICKY: out = bricks(out, name)
    out.save(os.path.join(OUT, name + ".png"))
    print("wrote", name)


if __name__ == "__main__":
    only = sys.argv[1:] or ["red", "cream", "tan", "glass", "store"]
    jobs = {
        "red": lambda: run("red", build_brick, ((0.50, 0.19, 0.12, 1), (0.14, 0.2, 0.3, 1), "red", (0.8, 0.75, 0.66, 1))),
        "cream": lambda: run("cream", build_brick, ((0.88, 0.8, 0.64, 1), (0.16, 0.22, 0.33, 1), "cream", (0.95, 0.9, 0.78, 1))),
        "tan": lambda: run("tan", build_tan, ((0.76, 0.6, 0.38, 1), "tan")),
        "glass": lambda: run("glass", build_glass),
        "store": lambda: run("store", build_store, res=(512, 512)),
    }
    for k in only: jobs[k]()
