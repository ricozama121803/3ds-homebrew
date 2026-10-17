"""One shot with Otto standing next to a sedan and a taxi, for scale. Usage: blender-venv/python render_scale.py out.png"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/scale.png"
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "cars.blend"))
sc, cam = stage()
with bpy.data.libraries.load(os.path.join(HERE, "otto.blend")) as (src, dst):
    dst.objects = ["otto"]
otto = dst.objects[0]
bpy.context.collection.objects.link(otto)
otto.location = (-1.9, -3.4, 0); otto.rotation_euler = (0, 0, math.radians(-25))
for n in ("sports", "van", "police"): bpy.data.objects[n].hide_render = True
bpy.data.objects["sedan"].location = (0, 0, 0)
bpy.data.objects["taxi"].location = (-6.0, 1.5, 0); bpy.data.objects["taxi"].rotation_euler = (0, 0, math.radians(8))
shoot(sc, cam, out, (6.2, -9.2, 2.4), (-1.5, -0.5, 0.9), res=(1000, 560), lens=45)
