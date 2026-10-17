"""Pedestrian: a low-poly version of Otto on the same skeleton and skin weights (so Otto's animation clips drive it), ~400 triangles.
Colours are roles (shirt, trousers, skin, hair, shoes) that the game fills in per person. Usage: blender-venv/python build_ped.py  (writes ped.blend)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
from build_otto import TORSO, LEG, ARM, W_BOOT, BONE


def build(name="ped"):
    SHIRT = material("suit", "#F2680C", 0.8)
    SHIRT_D = material("suit_dark", "#B94A08", 0.8)
    PANTS = material("pants", "#F2680C", 0.8)
    SKIN = material("skin", "#E2AA83", 0.7)
    BLACK = material("black", "#17171B", 0.7)
    HAIR = material("beanie", "#1D1D22", 0.95)
    BOOT = material("boot", "#2B2420", 0.6)
    b = Builder(name)
    HS = 1.2
    hz = lambda z: 1.6 + (z - 1.6) * HS

    b.wfn = TORSO
    b.vloft([(0.86, 0.165, 0.105, 0.0), (1.00, 0.176, 0.110, 0.0), (1.05, 0.176, 0.110, 0.0), (1.30, 0.205, 0.120, 0.0),
             (1.46, 0.272, 0.114, 0.005, 3.2), (1.57, 0.064, 0.064, 0.0)],
            [SHIRT, BLACK, SHIRT, SHIRT, SHIRT], N=8, caps=(False, True))
    b.vloft([(1.55, 0.052, 0.052, 0.0), (1.63, 0.050, 0.050, 0.0)], SKIN, N=6, caps=(False, False))
    for s in (-1, 1):
        side = 'L' if s == 1 else 'R'
        b.wfn = LEG(side)
        b.tube([(V(s * 0.095, 0.0, 0.93), 0.108, 0.110), (V(s * 0.100, -0.012, 0.52), 0.088, 0.092), (V(s * 0.100, 0.0, 0.13), 0.070, 0.072)], PANTS, N=6, caps=(False, False))
        b.wfn = W_BOOT(side)
        rings = [b.ring(V(s * 0.100, y, zc), V(1, 0, 0), V(0, 0, 1), a_, h_, 6, 2.6) for y, a_, h_, zc in ((0.110, 0.066, 0.082, 0.085), (-0.12, 0.080, 0.080, 0.080), (-0.30, 0.060, 0.044, 0.048))]
        b.loft(rings, BOOT, caps=(True, True))
        b.wfn = ARM(side)
        b.tube([(V(s * 0.285, 0.0, 1.42), 0.088, 0.092), (V(s * 0.395, 0.012, 1.15), 0.070, 0.074), (V(s * 0.455, -0.040, 0.915), 0.058, 0.058)], SHIRT, N=6, caps=(False, False))
        b.wfn = BONE(side + '_hand')
        b.tube([(V(s * 0.46, -0.048, 0.90), 0.054, 0.036), (V(s * 0.472, -0.072, 0.73), 0.036, 0.024)], SKIN, N=6, caps=(False, True))
    b.wfn = BONE('head')
    b.vloft([(hz(z), a_ * HS, d_ * HS, y_ * HS) for z, a_, d_, y_ in ((1.600, 0.055, 0.070, -0.022), (1.700, 0.098, 0.106, 0.0), (1.790, 0.100, 0.108, 0.0), (1.830, 0.075, 0.085, 0.004))], SKIN, N=8, caps=(True, False))
    b.vloft([(hz(z), a_ * HS, d_ * HS, y_ * HS) for z, a_, d_, y_ in ((1.745, 0.106, 0.114, 0.003), (1.830, 0.100, 0.108, 0.004), (1.880, 0.070, 0.080, 0.006), (1.905, 0.030, 0.036, 0.008))], HAIR, N=8, caps=(False, True))
    b.box((0, -0.118 * HS, hz(1.685)), (0.016 * HS, 0.018 * HS, 0.026 * HS), SKIN)                       # nose, so the face has a front
    return b.build()


if __name__ == "__main__":
    reset()
    ob = build()
    print("Pedestrian triangles:", tri_count([ob]))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "ped.blend"))
