"""Cycles turnaround of otto.blend. Usage: blender-venv/python render_otto.py out_prefix"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/otto"
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "otto.blend"))
sc, cam = stage()
views = [("front", 0, 0.0, 4.6), ("three", 35, 0.1, 4.6), ("side", 90, 0.0, 4.6), ("back", 180, 0.0, 4.6), ("head", 25, 0.0, 1.6)]
for name, ang, h, d in views:
    a = math.radians(ang)
    tz = 0.93 if name != "head" else 1.7
    eye = (math.sin(a) * -d * -1 if False else math.sin(a) * d, -math.cos(a) * d, tz + h * d + 0.05)
    shoot(sc, cam, f"{out}_{name}.png", eye, (0, 0, tz), res=(480, 640), lens=70 if name != "head" else 85)
