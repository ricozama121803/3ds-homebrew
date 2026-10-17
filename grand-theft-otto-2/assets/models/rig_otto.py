"""Otto's skeleton, same topology and bone order as Web Slinger's rig (so the same CMU mocap retargeting works), sized for Otto.
Rig space: origin at the pelvis joint, +X = Otto's left, +Y up, +Z forward. Blender model space: X left, Y = -forward, Z up, feet on Z = 0."""
PELVIS_Z = 0.93                                   # height of the pelvis joint above the floor
LEG_LEN = 0.80                                    # thigh + shin, rig units (the mocap is scaled to this)
SOLE = -0.13                                      # ankle to sole
HEEL, TOE = -0.11, 0.30                           # heel / toe tip relative to the ankle, along +Z
BONES = [('pelvis', -1, (0, 0, 0), (0, .14, 0)), ('spine', 0, (0, .14, 0), (0, .30, 0)), ('chest', 1, (0, .30, 0), (0, .56, 0)),
         ('neck', 2, (0, .56, 0), (0, .68, 0)), ('head', 3, (0, .68, 0), (0, .98, 0))]
for side, s in (('L', 1), ('R', -1)):
    def p(x, y, z=0):
        return (s * x, y, z)
    i = len(BONES)
    BONES += [(side + '_clavicle', 2, p(.04, .50), p(.285, .49)), (side + '_upperarm', i, p(.285, .49), p(.395, .22, -.012)),
              (side + '_forearm', i + 1, p(.395, .22, -.012), p(.455, -.015, .04)), (side + '_hand', i + 2, p(.455, -.015, .04), p(.472, -.20, .072))]
    i = len(BONES)
    BONES += [(side + '_thigh', 0, p(.10, 0), p(.10, -.41, .012)), (side + '_shin', i, p(.10, -.41, .012), p(.10, -.80, 0)),
              (side + '_foot', i + 1, p(.10, -.80, 0), p(.10, -.87, .17)), (side + '_toe', i + 2, p(.10, -.87, .17), p(.10, -.885, .30))]
NAMES = [b[0] for b in BONES]
INDEX = {n: i for i, n in enumerate(NAMES)}


def blender(p):                                    # rig -> Blender (absolute, feet on the floor)
    return (p[0], -p[2], p[1] + PELVIS_Z)


def game(p):                                       # Blender (absolute) -> rig
    return (p[0], p[2] - PELVIS_Z, -p[1])
