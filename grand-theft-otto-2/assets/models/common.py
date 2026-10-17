"""Shared Blender helpers for the GTO II models: flat-colour materials, lofted rings, boxes, a render rig.
Blender axes: X right, Y forward is -Y (models face -Y, like the default front view), Z up. Units are metres.
Run with ../../../tools/blender-venv/bin/python (pip bpy)."""
import bpy, bmesh, math, os
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _mats.clear()


_mats = {}


def srgb(h):
    """'#RRGGBB' -> linear rgb tuple"""
    h = h.lstrip('#')
    return tuple(((int(h[i:i + 2], 16) / 255.0) ** 2.2) for i in (0, 2, 4))


def material(name, hexcol, rough=0.6, emit=0.0):
    key = (name, hexcol, rough, emit)
    if key in _mats:
        return _mats[key]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    rgb = srgb(hexcol)
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Roughness"].default_value = rough
    if emit:
        b.inputs["Emission Color"].default_value = (*rgb, 1)
        b.inputs["Emission Strength"].default_value = emit
    m.diffuse_color = (*rgb, 1)
    _mats[key] = m
    return m


def V(x, y, z):
    return Vector((x, y, z))


class Builder:
    """Collects geometry into one bmesh with per-face materials."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.mats = []
        self.dl = self.bm.verts.layers.deform.verify()
        self.groups = []                       # vertex group names in order of first use
        self.wfn = None                        # position -> [(bone name, weight), ...] applied to every vertex created from now on

    def vert(self, p):
        v = self.bm.verts.new(p)
        if self.wfn:
            for name, w in self.wfn(p):
                if w <= 1e-4:
                    continue
                if name not in self.groups:
                    self.groups.append(name)
                v[self.dl][self.groups.index(name)] = w
        return v

    def mi(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def ring(self, c, u, v, a, b, N=10, sq=2.0):
        """N points around a (super)ellipse in the plane spanned by u, v at centre c (half-extents a, b; sq > 2 = boxier)"""
        q = 2.0 / sq
        pts = []
        for k in range(N):
            t = k * math.tau / N
            cs, sn = math.cos(t), math.sin(t)
            pts.append(c + u * (a * math.copysign(abs(cs) ** q, cs)) + v * (b * math.copysign(abs(sn) ** q, sn)))
        return pts

    def loft(self, rings, mat, caps=(True, True)):
        """Quads between consecutive rings (same point count). mat: one material or a list with one per band."""
        bm = self.bm
        vs = [[self.vert(p) for p in r] for r in rings]
        N = len(rings[0])
        faces = []
        for i in range(len(rings) - 1):
            m = mat[i] if isinstance(mat, (list, tuple)) else mat
            for k in range(N):
                f = bm.faces.new((vs[i][k], vs[i][(k + 1) % N], vs[i + 1][(k + 1) % N], vs[i + 1][k]))
                f.material_index = self.mi(m)
                faces.append(f)
        m0 = mat[0] if isinstance(mat, (list, tuple)) else mat
        m1 = mat[-1] if isinstance(mat, (list, tuple)) else mat
        if caps[0]:
            f = bm.faces.new(vs[0]); f.material_index = self.mi(m0); faces.append(f)
        if caps[1]:
            f = bm.faces.new(vs[-1]); f.material_index = self.mi(m1); faces.append(f)
        return faces

    def vloft(self, rows, mat, N=10, sq=2.6, caps=(True, True)):
        """Vertical loft: rows of (z, half width x, half depth y, y centre[, squareness])"""
        rings = []
        for r in rows:
            z, a, b, yc = r[:4]
            s = r[4] if len(r) > 4 else sq
            rings.append(self.ring(V(0, yc, z), V(1, 0, 0), V(0, 1, 0), a, b, N, s))
        return self.loft(rings, mat, caps)

    def tube(self, pts, mat, N=8, sq=2.4, ref=V(1, 0, 0), caps=(True, True)):
        """Tapered tube through points [(position, rx, ry), ...]; rx runs along `ref`, ry across it"""
        rings = []
        for i, (p, a, b) in enumerate(pts):
            p0 = pts[max(i - 1, 0)][0]
            p1 = pts[min(i + 1, len(pts) - 1)][0]
            d = (p1 - p0).normalized()
            u = (ref - d * ref.dot(d)).normalized()
            w = d.cross(u).normalized()
            rings.append(self.ring(p, u, w, a, b, N, sq))
        return self.loft(rings, mat, caps)

    def box(self, c, h, mat, sq=None):
        """Axis-aligned box centred at c with half-sizes h"""
        c = Vector(c)
        pts0 = [V(c.x + sx * h[0], c.y + sy * h[1], c.z - h[2]) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        pts1 = [V(p.x, p.y, c.z + h[2]) for p in pts0]
        return self.loft([pts0, pts1], mat)

    def build(self, parent=None):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        self.bm.free()
        for m in self.mats:
            me.materials.append(m)
        for p in me.polygons:
            p.use_smooth = False
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.collection.objects.link(ob)
        for g in self.groups:
            ob.vertex_groups.new(name=g)
        return ob


def tri_count(objs):
    n = 0
    for o in objs:
        o.data.calc_loop_triangles()
        n += len(o.data.loop_triangles)
    return n


def stage(sky=(0.55, 0.72, 0.95), ground="#44464c"):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 40
    sc.cycles.device = "CPU"
    sc.cycles.use_denoising = True
    sc.view_settings.view_transform = "Standard"
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (*[c ** 2.2 for c in sky], 1)
    bg.inputs[1].default_value = 1.1
    sc.world = w
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 4.0
    sun.data.angle = math.radians(6)
    sun.rotation_euler = (math.radians(52), math.radians(8), math.radians(-38))
    bpy.context.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, 0))
    g = bpy.context.object
    g.data.materials.append(material("ground", ground, 0.9))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 55
    bpy.context.collection.objects.link(cam)
    sc.camera = cam
    return sc, cam


def shoot(sc, cam, path, eye, target, res=(640, 560), lens=55):
    cam.data.lens = lens
    cam.location = Vector(eye)
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
