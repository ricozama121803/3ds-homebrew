"""Otto, the player: a chunky low-poly criminal in an orange jumpsuit, black beanie and a loot sack. ~1000 triangles.
Usage: ../../../tools/blender-venv/bin/python build_otto.py   (writes otto.blend next to this script)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
from rig_otto import PELVIS_Z


def smooth(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def chain(s, bones, bounds, hw):
    """Blend weights along a bone chain by the parameter s: bones in ascending order, bounds[i] is where bones[i] hands over to bones[i+1], hw[i] the blend half-width"""
    w = [0.0] * len(bones)
    w[0] = 1.0
    for i, (bd, h) in enumerate(zip(bounds, hw)):
        t = smooth((s - (bd - h)) / (2 * h))
        w[i + 1] += w[i] * t
        w[i] *= 1 - t
    return list(zip(bones, w))


TORSO = lambda p: chain(p.z, ['pelvis', 'spine', 'chest', 'neck', 'head'], [1.07, 1.23, 1.50, 1.62], [0.09, 0.08, 0.05, 0.03])
BONE = lambda name: (lambda p: [(name, 1.0)])


def LEG(side):
    return lambda p: chain(-p.z, [side + '_thigh', side + '_shin'], [-0.52], [0.07])


def ARM(side):
    return lambda p: chain(-p.z, [side + '_upperarm', side + '_forearm'], [-1.15], [0.06])


def W_BOOT(side):
    return lambda p: chain(-p.y, [side + '_foot', side + '_toe'], [0.17], [0.04])



def build(name="otto"):
    ORANGE = material("suit", "#F2680C", 0.8)
    ORANGE_D = material("suit_dark", "#B94A08", 0.8)
    PANTS = material("pants", "#F2680C", 0.8)
    PANTS_D = material("pants_dark", "#B94A08", 0.8)
    SKIN = material("skin", "#E2AA83", 0.7)
    SKIN_D = material("skin_dark", "#C48B66", 0.7)
    BLACK = material("black", "#17171B", 0.7)
    BEANIE = material("beanie", "#1D1D22", 0.95)
    CUFF = material("cuff", "#3C3C44", 0.95)
    BOOT = material("boot", "#2B2420", 0.6)
    SACK = material("sack", "#BC9B6A", 0.95)
    TIE = material("tie", "#6E5230", 0.9)
    WHITE = material("white", "#EFEFEA", 0.6)
    GREEN = material("dollar", "#2E8B4A", 0.7)
    b = Builder(name)
    HS = 1.2                                                           # the head and beanie are scaled up a little for a chunkier, more readable character
    hz = lambda z: 1.6 + (z - 1.6) * HS
    hb = lambda x, y, z: (x * HS, y * HS, hz(z))
    hh = lambda x, y, z: (x * HS, y * HS, z * HS)

    b.wfn = TORSO
    # torso: hips -> belt -> waist -> chest -> shoulders -> neck base (rows: z, half width, half depth, y centre)
    b.vloft([(0.86, 0.165, 0.105, 0.0), (0.95, 0.178, 0.112, 0.0), (1.00, 0.176, 0.110, 0.0), (1.05, 0.176, 0.110, 0.0),
             (1.22, 0.186, 0.114, 0.0), (1.38, 0.228, 0.126, 0.0), (1.46, 0.272, 0.114, 0.005, 3.2), (1.52, 0.170, 0.092, 0.01), (1.57, 0.064, 0.064, 0.0)],
            [ORANGE, BLACK, ORANGE, ORANGE, ORANGE, ORANGE, ORANGE, ORANGE], N=12)
    # neck + collar
    b.vloft([(1.55, 0.052, 0.052, 0.0), (1.63, 0.050, 0.050, 0.0)], SKIN, N=8, caps=(False, False))
    b.vloft([(1.535, 0.128, 0.100, 0.0), (1.605, 0.074, 0.068, 0.0)], ORANGE_D, N=10, caps=(False, False))

    for s in (-1, 1):
        side = 'L' if s == 1 else 'R'
        b.wfn = LEG(side)
        # legs: hip -> knee -> ankle (jumpsuit), boots
        b.tube([(V(s * 0.095, 0.0, 0.93), 0.108, 0.110), (V(s * 0.100, -0.012, 0.52), 0.088, 0.092), (V(s * 0.100, 0.0, 0.13), 0.070, 0.072)], PANTS)
        b.tube([(V(s * 0.100, 0.0, 0.17), 0.074, 0.076), (V(s * 0.100, 0.0, 0.10), 0.080, 0.082)], PANTS_D, caps=(False, True))
        b.wfn = W_BOOT(side)
        rings = []
        for y, a_, h_, zc in ((0.110, 0.066, 0.082, 0.085), (-0.01, 0.076, 0.092, 0.095), (-0.16, 0.082, 0.068, 0.070), (-0.30, 0.060, 0.044, 0.048)):
            rings.append(b.ring(V(s * 0.100, y, zc), V(1, 0, 0), V(0, 0, 1), a_, h_, 8, 2.6))
        b.loft(rings, BOOT)
        # arms (A-pose): shoulder -> elbow -> wrist, hand
        b.wfn = ARM(side)
        b.tube([(V(s * 0.285, 0.0, 1.42), 0.088, 0.092), (V(s * 0.395, 0.012, 1.15), 0.070, 0.074), (V(s * 0.455, -0.040, 0.915), 0.058, 0.058)], ORANGE)
        b.tube([(V(s * 0.455, -0.040, 0.93), 0.062, 0.062), (V(s * 0.459, -0.045, 0.885), 0.064, 0.064)], ORANGE_D, caps=(False, True))
        b.wfn = BONE(side + '_hand')
        b.tube([(V(s * 0.46, -0.048, 0.89), 0.054, 0.036), (V(s * 0.468, -0.062, 0.80), 0.052, 0.034), (V(s * 0.472, -0.072, 0.73), 0.036, 0.024)], SKIN)
        b.box((s * 0.435, -0.085, 0.84), (0.014, 0.014, 0.034), SKIN)                                     # thumb

    b.wfn = BONE('head')
    # head
    b.vloft([(hz(z), a_ * HS, d_ * HS, y_ * HS) for z, a_, d_, y_ in ((1.600, 0.055, 0.070, -0.022), (1.640, 0.085, 0.095, -0.006), (1.700, 0.098, 0.106, 0.0), (1.770, 0.100, 0.108, 0.0),
             (1.830, 0.075, 0.085, 0.004), (1.860, 0.030, 0.035, 0.005))], SKIN, N=10)
    b.box(hb(0, -0.118, 1.685), hh(0.016, 0.018, 0.026), SKIN_D)                                          # nose
    for s in (-1, 1):
        b.box(hb(s * 0.104, 0.0, 1.69), hh(0.010, 0.022, 0.036), SKIN_D)                                  # ears
        b.box(hb(s * 0.040, -0.106, 1.722), hh(0.019, 0.004, 0.011), WHITE)                               # eyes
        b.box(hb(s * 0.040, -0.109, 1.722), hh(0.009, 0.005, 0.010), BLACK)
        b.box(hb(s * 0.040, -0.110, 1.752), hh(0.026, 0.004, 0.005), BLACK)                               # brows
    b.box(hb(0, -0.101, 1.648), hh(0.030, 0.004, 0.005), SKIN_D)                                          # mouth
    b.box(hb(0, -0.098, 1.66), hh(0.034, 0.006, 0.014), SKIN_D)                                           # chin / stubble shadow
    # beanie: cuff, then the cap
    b.vloft([(hz(1.738), 0.107 * HS, 0.115 * HS, 0.002), (hz(1.790), 0.107 * HS, 0.115 * HS, 0.002)], CUFF, N=10, caps=(False, False))
    b.vloft([(hz(z), a_ * HS, d_ * HS, y_ * HS) for z, a_, d_, y_ in ((1.790, 0.105, 0.113, 0.002), (1.830, 0.098, 0.106, 0.004), (1.875, 0.074, 0.084, 0.006), (1.905, 0.030, 0.036, 0.008))], BEANIE, N=10, caps=(False, True))

    b.wfn = TORSO
    # chest patch + zip + pocket
    b.box((0.105, -0.128, 1.31), (0.052, 0.004, 0.034), WHITE)
    b.box((0.105, -0.131, 1.33), (0.038, 0.002, 0.005), BLACK)
    b.box((0.105, -0.131, 1.30), (0.030, 0.002, 0.004), BLACK)
    b.box((0.0, -0.126, 1.26), (0.004, 0.003, 0.17), ORANGE_D)
    b.box((-0.105, -0.127, 1.33), (0.045, 0.004, 0.04), ORANGE_D)

    # loot sack on the back, tied at the top, with straps over the shoulders
    b.vloft([(1.02, 0.14, 0.07, 0.20), (1.17, 0.205, 0.115, 0.205), (1.34, 0.19, 0.11, 0.20), (1.46, 0.10, 0.07, 0.18), (1.54, 0.034, 0.034, 0.17)], SACK, N=10)
    b.box((0.0, 0.17, 1.575), (0.05, 0.05, 0.03), TIE)
    b.box((0.0, 0.322, 1.19), (0.07, 0.004, 0.07), GREEN)                                                 # the dollar patch
    b.box((0.0, 0.324, 1.19), (0.012, 0.003, 0.05), WHITE)
    for s in (-1, 1):
        b.box((s * 0.14, 0.07, 1.47), (0.025, 0.11, 0.008), TIE)
    return b.build()


if __name__ == "__main__":
    reset()
    ob = build()
    print("Otto triangles:", tri_count([ob]))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "otto.blend"))
