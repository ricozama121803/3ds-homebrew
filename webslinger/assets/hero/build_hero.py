#!/usr/bin/env python3
"""Builds the hero in Blender (headless): lofted body parts with UVs, a painted suit texture, and hero.blend for hand-editing.
Run:  ../../../tools/blender-venv/bin/python build_hero.py        (from webslinger/assets/hero)
Outputs: hero.blend, hero_atlas.png (also copied to ../gfx/hero.png by export_hero.py)

Conventions (Blender): Z up, the hero faces -Y, so its LEFT side is +X. Left arm/leg only; the game mirrors them for the right side.
Every part's object origin is its joint, so the exporter can use the mesh as joint-local geometry.
"""
import math, os, sys
import bpy, bmesh
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ATLAS = 256

# ---- part definitions ----------------------------------------------------------------------------------------------------------
# ring = (t, cx, cy, ra, rb): position along the axis, centre offset in the two other axes, semi-axes. Axis y: (a, b) = (x, z) with +z = front.
# axis z: (a, b) = (x, y).
def R(*rows): return [tuple(r) for r in rows]

PARTS = {
 "torso": dict(axis="y", seg=12, origin=(0, 0, 0), warp=60, rect=(0, 0, 128, 120), rings=R(
    (-0.09, 0, 0.000, .150, .108), (0.00, 0, 0.000, .172, .118), (0.10, 0, .004, .150, .108), (0.24, 0, 0.000, .143, .101),
    (0.38, 0, .008, .183, .120), (0.50, 0, .016, .218, .138), (0.58, 0, 0.000, .226, .115), (0.64, 0, -.005, .177, .091),
    (0.69, 0, -.010, .075, .065), (0.74, 0, -.010, .052, .052))),
 "head": dict(axis="y", seg=12, origin=(0, .70, 0), warp=45, gscale=0.84, wscale=1.07, rect=(128, 0, 128, 120), rings=R(
    (-0.03, 0, -.010, .050, .050), (0.06, 0, -.010, .047, .047), (0.085, 0, .020, .060, .065), (0.14, 0, .020, .085, .088),
    (0.21, 0, .016, .103, .111), (0.28, 0, .008, .110, .117), (0.34, 0, 0.000, .108, .118), (0.39, 0, -.008, .086, .098),
    (0.425, 0, -.010, .045, .055))),
 "upperarm": dict(axis="y", seg=8, origin=(.255, .53, 0), warp=0, rect=(0, 120, 64, 56), rings=R(
    (0.075, 0, 0, .018, .024), (0.035, 0, 0, .063, .067), (0.00, 0, 0, .077, .081), (-0.07, 0, .004, .078, .083), (-0.16, 0, .006, .065, .073),
    (-0.25, 0, 0, .056, .058), (-0.30, 0, 0, .050, .052))),
 "forearm": dict(axis="y", seg=8, origin=(.255, .23, 0), warp=0, rect=(64, 120, 64, 56), rings=R(
    (0.00, 0, 0, .050, .052), (-0.06, 0, 0, .058, .060), (-0.16, 0, 0, .048, .050), (-0.25, 0, 0, .040, .040), (-0.29, 0, 0, .034, .034))),
 "hand": dict(axis="y", seg=8, origin=(.255, -.06, 0), warp=0, gscale=1.05, wscale=1.16, rect=(128, 120, 56, 48), rings=R(
    (0.00, 0, 0, .034, .034), (-0.03, 0, 0, .045, .026), (-0.08, 0, 0, .047, .024), (-0.12, 0, 0, .042, .020),
    (-0.15, 0, 0, .030, .016), (-0.175, 0, 0, .014, .010)),
    extra=[dict(seg=6, rings=R((-0.03, -.040, 0, .018, .016), (-0.06, -.052, .004, .016, .014), (-0.09, -.056, .008, .011, .010), (-0.105, -.057, .010, .005, .005)))]),
 "thigh": dict(axis="y", seg=10, origin=(.12, 0, 0), warp=0, rect=(0, 176, 64, 80), rings=R(
    (0.05, 0, 0, .100, .100), (-0.05, 0, 0, .108, .105), (-0.16, 0, 0, .100, .100), (-0.28, 0, 0, .080, .088),
    (-0.40, 0, 0, .065, .070), (-0.46, 0, 0, .058, .060))),
 "shin": dict(axis="y", seg=8, origin=(.12, -.46, 0), warp=0, rect=(64, 176, 64, 80), rings=R(
    (0.00, 0, 0, .058, .060), (-0.06, 0, 0, .060, .068), (-0.16, 0, -.012, .062, .075), (-0.26, 0, 0, .052, .060),
    (-0.38, 0, 0, .043, .046), (-0.46, 0, 0, .040, .042))),
 "foot": dict(axis="z", seg=8, origin=(.12, -.92, 0), warp=0, gscale=1.1, wscale=1.12, rect=(128, 176, 80, 56), rings=R(
    (-0.075, 0, -.045, .036, .050), (-0.03, 0, -.045, .046, .062), (0.04, 0, -.055, .050, .050), (0.12, 0, -.070, .048, .036),
    (0.19, 0, -.078, .036, .026), (0.215, 0, -.080, .020, .016))),
}

def warp_frac(a_deg, w):
    """angle from the front (degrees, + toward the hero's left) -> signed texture fraction (-0.5..0.5). The front +-w degrees gets half of the width."""
    if w <= 0: return a_deg / 360.0
    s = 1 if a_deg >= 0 else -1; a = abs(a_deg)
    f = a / w * 0.25 if a <= w else 0.25 + (a - w) / (180.0 - w) * 0.25
    return s * f

def pix(part, a_deg, t):
    """(angle from front in degrees, position along the axis) -> pixel in the atlas"""
    P = PARTS[part]; x0, y0, w, h = P["rect"]
    f = 0.5 - warp_frac(a_deg, P["warp"])                      # + angle (left) -> smaller u
    ts = [r[0] for r in P["rings"]]; tmin, tmax = min(ts), max(ts)
    k = max(0.0, min(1.0, (t - tmin) / (tmax - tmin)))
    if P["axis"] == "y": row = 1.0 - k
    else: row = 1.0 - k
    pad = 2.0                                                  # keep samples away from the region edge so bilinear filtering never picks up the neighbouring part
    return x0 + pad + f * (w - 2 * pad), y0 + pad + row * (h - 2 * pad)

# ---- geometry ------------------------------------------------------------------------------------------------------------------
def add_loft(bm, uvl, part, seg, rings, axis):
    P = PARTS[part]
    verts = []
    gs, ws = P.get("gscale", 1.0), P.get("wscale", 1.0)           # geometry-only scaling (UVs and painting keep using the unscaled rings)
    for (t, ca, cb, ra, rb) in rings:
        ring = []
        t *= gs; ca *= ws; cb *= ws; ra *= ws; rb *= ws
        for j in range(seg):
            f = j / seg; ang = 2 * math.pi * f
            a = ca + ra * math.sin(ang); b = cb + rb * (-math.cos(ang))
            co = (a, t, b) if axis == "y" else (a, b, t)          # game-local: x side, y up, z front
            ring.append(bm.verts.new(game_to_blender(co)))
        verts.append(ring)
    ts = [r[0] for r in P["rings"]]; tmin, tmax = min(ts), max(ts)
    def uv_of(a_deg_fraction, t):
        f = a_deg_fraction
        # f in [0,1] around the part, 0.5 = front; convert to angle-from-front in degrees (positive toward +x = smaller f)
        ang = (0.5 - f) * 360.0
        # wrap into -180..180
        if ang > 180: ang -= 360
        if ang < -180: ang += 360
        return ang
    def set_uv(loop, j_frac, t, wrap_pos):
        # j_frac: fraction around (j/seg), where frac 0 is the BACK (angle 180 from front); frac .5 is front
        ang_from_front = (0.5 - j_frac) * 360.0                   # +x side (frac .25) -> +90
        if wrap_pos and ang_from_front <= -180 + 1e-6: ang_from_front = -180.0
        if ang_from_front > 180: ang_from_front = 180.0
        px, py = pix(part, ang_from_front, t)
        loop[uvl].uv = (px / ATLAS, 1.0 - py / ATLAS)
    for i in range(len(rings) - 1):
        for j in range(seg):
            j2 = (j + 1) % seg
            f = bm.faces.new((verts[i][j], verts[i][j2], verts[i + 1][j2], verts[i + 1][j]))
            f.smooth = True
            for loop, (jj, ii) in zip(f.loops, ((j, i), (j + 1, i), (j + 1, i + 1), (j, i + 1))):
                set_uv(loop, jj / seg, rings[ii][0], False)
    for end in (0, len(rings) - 1):                                # end caps: a fan around a centre vertex
        t, ca, cb, ra, rb = rings[end]
        co = (ca * ws, t * gs, cb * ws) if axis == "y" else (ca * ws, cb * ws, t * gs)
        c = bm.verts.new(game_to_blender(co))
        for j in range(seg):
            j2 = (j + 1) % seg
            f = bm.faces.new((c, verts[end][j2], verts[end][j]))
            f.smooth = True
            for loop, jj in zip(f.loops, ((j + 0.5), (j + 1), j)):
                set_uv(loop, jj / seg, t, False)

def game_to_blender(p):
    x, y, z = p
    return (x, -z, y)                                              # game z (front) -> Blender -Y

def build_part(name):
    P = PARTS[name]
    mesh = bpy.data.meshes.new("hero_" + name)
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new("UVMap")
    add_loft(bm, uvl, name, P["seg"], P["rings"], P["axis"])
    for e in P.get("extra", []): add_loft(bm, uvl, name, e["seg"], e["rings"], P["axis"])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(mesh); bm.free()
    obj = bpy.data.objects.new("hero_" + name, mesh)
    bpy.context.collection.objects.link(obj)
    # the ring coordinates are already joint-local; the object sits at the joint so the assembled rest pose shows in Blender
    o = game_to_blender(P["origin"])
    obj.location = o
    return obj

# ---- texture ---------------------------------------------------------------------------------------------------------------------
RED = (190, 25, 39); DRED = (110, 15, 29); BLUE = (22, 47, 112); DBLUE = (12, 26, 65); BLACK = (12, 15, 23); WHITE = (235, 247, 250); WEB = (78, 16, 29); WEBB = (18, 36, 83)

def fill_part(d, name, color):
    x, y, w, h = PARTS[name]["rect"]; d.rectangle([x, y, x + w - 1, y + h - 1], fill=color)

def poly(d, part, pts, color, steps=10):
    """polygon given in (angle from front, axis position) space; edges are subdivided so the warp is followed"""
    out = []
    for i in range(len(pts)):
        (a0, t0), (a1, t1) = pts[i], pts[(i + 1) % len(pts)]
        for s in range(steps): out.append(pix(part, a0 + (a1 - a0) * s / steps, t0 + (t1 - t0) * s / steps))
    d.polygon(out, fill=color)

def line(d, part, a0, t0, a1, t1, color, width=1, steps=14):
    pts = [pix(part, a0 + (a1 - a0) * s / steps, t0 + (t1 - t0) * s / steps) for s in range(steps + 1)]
    d.line(pts, fill=color, width=width)

def web(d, part, a_lo, a_hi, t_lo, t_hi, strands, rows, color, sag=0.25, width=1):
    """radial strands along the axis plus scalloped rings between them, over the angular span [a_lo, a_hi] (degrees from front)"""
    for i in range(strands + 1):
        a = a_lo + (a_hi - a_lo) * i / strands
        line(d, part, a, t_lo, a, t_hi, color, width)
    for r in range(rows + 1):
        t = t_lo + (t_hi - t_lo) * r / rows
        for i in range(strands):
            a0 = a_lo + (a_hi - a_lo) * i / strands; a1 = a_lo + (a_hi - a_lo) * (i + 1) / strands
            pts = []
            for s in range(9):
                u = s / 8; pts.append(pix(part, a0 + (a1 - a0) * u, t - (t_hi - t_lo) / rows * sag * math.sin(math.pi * u)))
            d.line(pts, fill=color, width=width)

def paint():
    img = Image.new("RGB", (ATLAS, ATLAS), RED); d = ImageDraw.Draw(img)
    for n in PARTS: fill_part(d, n, RED)
    # Clean blue panels frame a tapered red bib and a narrow waist band.
    fill_part(d, "torso", BLUE)
    poly(d, "torso", [(-180, .57), (180, .57), (180, .74), (-180, .74)], RED)
    for center in (0, -180, 180):
        pts = [(center - 57, .59), (center + 57, .59),
               (center + 43, .36), (center + 25, .12),
               (center - 25, .12), (center - 43, .36)]
        # Clip the rear halves at the UV seam.
        pts = [(max(-180, min(180, a)), t) for a, t in pts]
        poly(d, "torso", pts, RED)
    poly(d, "torso", [(-180, .035), (180, .035), (180, .085), (-180, .085)], RED)
    # Webbing is masked to red fabric; blue panels remain readable at game scale.
    layer = Image.new("RGB", img.size, RED); ld = ImageDraw.Draw(layer)
    web(ld, "torso", -180, 180, .09, .73, 16, 9, WEB, sag=.18)
    for y in range(120):
        for x in range(128):
            if img.getpixel((x, y)) == RED: img.putpixel((x, y), layer.getpixel((x, y)))
    for sign in (-1, 1):
        line(d, "torso", sign * 60, .55, sign * 31, .13, DRED)
    # Compact chest spider with clearly separated bent legs.
    poly(d, "torso", [(-6, .465), (0, .440), (6, .465), (7, .51),
                       (4, .535), (-4, .535), (-7, .51)], BLACK)
    poly(d, "torso", [(-4, .535), (4, .535), (4, .553), (-4, .553)], BLACK)
    for sign in (-1, 1):
        for a, t, b, u, c, v in ((5,.538,21,.565,26,.604),
                                  (6,.520,29,.540,35,.572),
                                  (6,.495,27,.474,33,.430),
                                  (5,.474,18,.438,22,.380)):
            line(d, "torso", sign*a,t,sign*b,u,BLACK,2)
            line(d, "torso", sign*b,u,sign*c,v,BLACK,2)
    # Rear insignia straddles the back seam, with four legs on each side.
    for sign in (-1, 1):
        poly(d, "torso", [(sign*180,.43),(sign*172,.45),(sign*170,.51),
                           (sign*176,.545),(sign*180,.55)], BLACK)
        for t, a, u in ((.53,151,.585),(.51,145,.53),(.485,146,.445),(.46,158,.385)):
            line(d,"torso",sign*176,t,sign*a,u,BLACK,2)
    fill_part(d, "head", RED)
    web(d, "head", -180, 180, .08, .425, 16, 7, WEB, sag=.18)
    for sign in (-1, 1):
        rim = [(sign*3,.246),(sign*34,.315),(sign*45,.294),
               (sign*38,.234),(sign*24,.193),(sign*9,.209)]
        lens = [(sign*8,.246),(sign*33,.299),(sign*39,.286),
                (sign*33,.237),(sign*23,.210),(sign*13,.220)]
        poly(d,"head",rim,BLACK); poly(d,"head",lens,WHITE)
        line(d,"head",sign*13,.221,sign*23,.211,(151,188,209))
    for n in ("upperarm", "forearm", "hand"):
        fill_part(d,n,RED)
        lo = min(r[0] for r in PARTS[n]["rings"])
        hi = max(r[0] for r in PARTS[n]["rings"])
        web(d,n,-180,180,lo,hi,8,5 if n != "hand" else 3,WEB,sag=.18)
    # Blue underarm panel and a slim glove cuff.
    poly(d,"upperarm",[(55,-.30),(150,-.30),(150,-.06),(105,.008),(65,-.06)],BLUE)
    line(d,"forearm",-180,-.262,180,-.262,DRED,2)
    for a in (-65,-25,25,65): line(d,"hand",a,-.075,a,-.145,DRED)
    fill_part(d,"thigh",BLUE)
    for sign in (-1,1):
        poly(d,"thigh",[(sign*75,.05),(sign*115,.05),(sign*110,-.34),
                         (sign*70,-.44),(sign*60,-.38)],DBLUE)
        line(d,"thigh",sign*62,-.08,sign*60,-.35,(35,65,133))
    fill_part(d,"shin",BLUE)
    boot = [(-180,-.15),(-90,-.14),(0,-.09),(90,-.14),(180,-.15),
            (180,-.46),(-180,-.46)]
    poly(d,"shin",boot,RED)
    layer = Image.new("RGB", img.size, RED); ld = ImageDraw.Draw(layer)
    web(ld,"shin",-180,180,-.46,-.085,8,5,WEB,sag=.18)
    x0,y0,w,h = PARTS["shin"]["rect"]
    for y in range(y0,y0+h):
        for x in range(x0,x0+w):
            if img.getpixel((x,y)) == RED: img.putpixel((x,y),layer.getpixel((x,y)))
    for sign in (-1,1):
        line(d,"shin",0,-.09,sign*90,-.14,DRED,2)
        line(d,"shin",sign*90,-.14,sign*180,-.15,DRED,2)
    fill_part(d,"foot",RED)
    web(d,"foot",-180,180,-.075,.215,8,4,WEB,sag=.15)
    # The foot loft runs heel-to-toe: its underside lies at the angular seam.
    for sign in (-1,1):
        poly(d,"foot",[(sign*110,-.075),(sign*180,-.075),
                        (sign*180,.215),(sign*110,.215)],DRED)
        line(d,"foot",sign*120,-.075,sign*120,.215,BLACK)
    # Subtle baked fabric modeling, plus a two-pixel edge extrusion per island.
    # Extrusion also prevents adjacent painted regions from bleeding into UVs.
    for name,P in PARTS.items():
        x0,y0,w,h=P["rect"]
        for y in range(y0+2,y0+h-1):
            for x in range(x0+2,x0+w-1):
                f=(x-x0-2)/(w-4)
                shade=.88+.12*math.sin(math.pi*f)**2
                color=img.getpixel((x,y))
                img.putpixel((x,y),tuple(round(c*shade) for c in color))
        for y in range(y0,y0+h):
            for x in range(x0,x0+w):
                if x < x0+2 or x > x0+w-2 or y < y0+2 or y > y0+h-2:
                    img.putpixel((x,y),img.getpixel((max(x0+2,min(x0+w-2,x)),
                                                     max(y0+2,min(y0+h-2,y)))))
    return img

# ---- main ------------------------------------------------------------------------------------------------------------------------
def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    img = paint(); atlas = os.path.join(HERE, "hero_atlas.png"); img.save(atlas)
    bimg = bpy.data.images.load(atlas); bimg.pack()
    mat = bpy.data.materials.new("hero_suit"); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = bimg; tex.interpolation = "Linear"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"]); bsdf.inputs["Roughness"].default_value = 0.72
    for name in PARTS:
        obj = build_part(name)
        obj.data.materials.append(mat)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "hero.blend"))
    tris = sum(len(o.data.polygons) for o in bpy.data.objects)
    print("hero.blend written:", len(PARTS), "parts,", tris, "faces (triangles ~ %d)" % sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.data.objects))

if __name__ == "__main__":
    main()
