"""Renders turnaround views of hero.blend with Cycles (CPU). Single continuous weighted skin.
Usage: ../../../tools/blender-venv/bin/python render_hero.py out_prefix"""
import sys, math, os, bpy
from mathutils import Vector
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/hero"
HERE = os.environ.get("HERO_OUT", os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "hero.blend"))
sc = bpy.context.scene
sc.render.engine = "CYCLES"; sc.cycles.samples = 24; sc.cycles.device = "CPU"
sc.render.resolution_x = 480; sc.render.resolution_y = 640
w = bpy.data.worlds.new("w"); w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.7, 0.9, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.0; sc.world = w
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3.5; sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35)); bpy.context.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); cam.data.lens = 70; bpy.context.collection.objects.link(cam); sc.camera = cam
for name, ang, h, d in (("front", 0, 0.05, 6.0), ("side", 90, 0.05, 6.0), ("three", 40, 0.15, 6.0), ("back", 180, 0.05, 6.0), ("headclose", 25, 0.62, 2.2), ("shoulders", 0, 0.3, 3.2), ("feet", 90, 0.1, 2.6), ("feet3", 40, 0.35, 2.6)):
    a = math.radians(ang); tgt = Vector((0, 0, -0.1 if name not in ("headclose", "shoulders", "feet", "feet3") else (0.9 if name == "headclose" else (-1.0 if name.startswith("feet") else 0.45))))
    cam.location = Vector((math.sin(a) * -d, -math.cos(a) * d, tgt.z + h * d)) * 1.0
    cam.location.x = -math.sin(a) * d; cam.location.y = -math.cos(a) * d
    cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = f"{out}_{name}.png"; bpy.ops.render.render(write_still=True)
