"""Cycles renders of cars.blend. Usage: blender-venv/python render_cars.py out_prefix"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cars"
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "cars.blend"))
sc, cam = stage()
names = ["sedan", "sports", "van", "taxi", "police", "swat"]
objs = {n: bpy.data.objects[n] for n in names}
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "otto.blend")) if False else None
for i, n in enumerate(names):
    cx = i * 3.6
    for k, o in objs.items(): o.hide_render = (k != n)
    for tag, ang, h in (("front3", -38, 3.0), ("rear3", 145, 3.0)):
        a = math.radians(ang)
        d = 7.6
        eye = (cx + math.sin(a) * d, -math.cos(a) * d, h)
        shoot(sc, cam, f"{out}_{n}_{tag}.png", eye, (cx, 0, 0.75), res=(560, 380), lens=60)
