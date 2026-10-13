# Hero model and locomotion work

## CURRENT STATUS (supersedes the sections below where they disagree)

- **Model:** one continuous watertight skinned body, 2,780 triangles, 1,392 vertices, 21 bones (`assets/hero/rig.py`), up to 4 weights per vertex. `assets/hero/build_hero.py` generates it in Blender (`make hero`): a lofted torso/head profile (hips, waist, ribs, chest, shoulder line, neck, with per-section squareness for angular planes), tapered limb tubes rooted inside the torso, a lofted flat-soled boot, voxel remesh + decimation to the budget, automatic weights, baked per-vertex AO, and the suit texture.
- **Suit texture:** painted procedurally in object space (`paint_v`) at 2048 and averaged down to a 1024x1024 atlas (`assets/gfx/hero.png`): crimson / navy suit, V-shaped chest panel, belt, antialiased silver radial web with arm / head / boot variants, spider emblems, lenses, soft muscle shading. `HERO_OUT` and `HERO_STYLE` env vars let you build variants into another directory without touching the shipped files.
- **Animation:** CPU linear-blend skinning in `source/scene.c` once per frame (both stereo eyes reuse it). Walk / jog / sprint come from CMU mocap clips imported by `assets/hero/import_mocap.py` into `source/hero_anim.h`; traversal poses (swing, wingsuit, dive, flips, corkscrew, wall climb) are procedural bone rotations on the same skeleton.
- **Gameplay changes made alongside:** wall-run camera tilts up the facade (`Cam.wall`), release flourish (tuck flip / corkscrew) when letting go of a web at speed, swan-dive and spread coast poses.
- **Verification:** `host/preview.sh <dir>` renders the in-game frames and the contact sheets (`sheet_*.png`); `movetest` and `simtest` pass; the 3DS build compiles. Run `assets/hero/render_hero.py` with `HERO_OUT` for Cycles turnarounds.
- **Known gaps:** `host/animtest.c` is stale (it still references the old rigid `HERO_PARTS`) and does not build; hands are mitts without fingers; nothing here has been run on a real 3DS (frame time, the 4 MB hero texture, stereo comfort are unmeasured).

---

# (earlier notes from the rigid-parts pass follow)

## 1. Baseline inspection

Rebuilt with `source ../env.sh && make hero`, rendered all five Cycles
turnarounds using `../tools/blender-venv/bin/python assets/hero/render_hero.py
/tmp/hero-before`, and ran `host/preview.sh /tmp/hero-preview`.

The three-quarter turnaround shows tubular shoulders/arms, a long mask and
neck, a boxy pelvis, and flat blue panels. The f0–f7 run frames show flat
feet without ankle roll and limited pelvis articulation; arm bend changes
too much. Bent poses expose rigid joint boundaries and some detached hands
and feet. The preview close-ups crop some poses, and the initial trick
snapshots look suspiciously similar: improve those fixtures in step 4.

Baseline: 1,752 rendered triangles (240 torso, 216 head, mirrored limbs).
Atlas: 256×256. Hero capacity: 7,500 vertices; scratch limit: 700 per part.

Viewed: all turnarounds; f0–f7_run, h1_swing, h2_run, h2b_run, h3_idle,
h4_jump, k0–k3_trick, k6a–k6c_air, wr0–wr5_wallrun, w2_wing_side,
r0–r9_roll (contact sheets in /tmp/before-sheet0.jpg through sheet4.jpg).

`movetest`: all moves ok (0 failures). `simtest`: baseline splash counts
recorded in /tmp/hero-before-sim.log (the last run has 3; baseline is not
zero). `make`: .3dsx and .cia built, with an existing GNU-stack linker warning.

Animation reference: Rusty Animator's illustrated contact/down/passing/up
run-cycle tutorial, https://rustyanimator.com/run-cycle/ . Compare support
leg compression, early heel recovery, opposite arm drive, hip/chest
counter-rotation, and two flight intervals per full running stride.

All visual checks are host software/Cycles renders. Hardware appearance,
stereo, frame time, and texture filtering cannot be verified here.

## 2. Model and texture

Rebuilt anatomical profiles, smaller mask, rounded overlapping joints, 0.50-unit
thigh/shin bones, relaxed curled hands/thumbs, and flat-soled shaped shoes.
Added broad painted muscle highlights, collar/joint darkening and panel seams;
the atlas remains 256×256 and the builder remains the source of truth.

2,744 rendered triangles; 8,232 emitted vertices. Largest indexed part: torso,
236 vertices (limit 700). CAP_D_HERO is now 8,400 (2,800 triangles).
CAP_DYN grows from 12,800 to 13,700 vertices: 493,200 bytes per buffer,
64,800 additional bytes total for the two buffers in gfx3ds.c. Stereo still
submits the hero twice. The exporter now rejects either capacity overflow.

Viewed model three-quarter/side turnarounds and model-sheet0–4.jpg, including
all eight run frames and the baseline pose set. Joint overlaps improve the
silhouette, but unreachable hand targets in the old poses still separate
hands from wrists; that requires a posing fix in step 4.

Model-stage movetest passes; simtest is byte-identical to baseline (splashes
0,0,0,0,0,3); .3dsx/.cia build succeeds. Logs: /tmp/hero-model-*.log;
previews: /tmp/hero-preview-model, /tmp/hero-model_*.png.

The user subsequently requested no Git additions or commits; no commits were made.

## 3. Ground locomotion

Replaced the sine-driven run with speed-dependent walk/run timing, double
support in the walk and two flight intervals in a run. Stance anchors are
stored in rendered world space (accounting for SC=1.38), so torso springs
cannot drag the shoes. Added heel/midfoot contact, toe-off, early heel
recovery, knee drive, pelvis bounce/weight shift/twist, a cheap two-frame
CPU torso deformation for counter-rotation, head stabilisation, and bent
opposing arm swings. Wall running uses the same surface-relative gait.
Gameplay physics, controls, and the game camera are unchanged.

Fixed host inspection camera feedback/cropping and exact 0.07-second preview
sampling: the old 30 Hz helper silently rounded that to 0.10 seconds.
Viewed gait-sheet0–4.jpg from /tmp/hero-preview-gait. Compared the filmstrip
with Rusty Animator's side contact and up images (downloaded and opened),
including support/flight, heel recovery and opposite arm drive. The wall
fixture's artificial height resets invalidate its planted foot anchors;
remove those in the final pose pass. Existing unreachable hand targets also
need correction there.

Added host/animtest.c to exercise the actual rendered hero at 30/60/120 Hz,
slow walk through sprint, plus wall run: full mesh, finite vertices, rigid
bone lengths, fixed rendered flat contacts, walking support, running flight,
and identical repeated draws at one timestamp. Pass: contact drift at most
0.0000076 world units, bone error under printed precision. movetest passes;
simtest is byte-identical to baseline; make builds .3dsx/.cia.

## 4. Other poses and final verification

Clamped unreachable IK targets before posing rigid meshes, which fixes the
old detached hands/feet in swing, wingsuit, corkscrew and roll. Rope and wing
endpoints now use those resolved joints. Corrected wall-climb contact depth
for the 1.38 hero scale; the old targets penetrated the facade. Raised the
walking/idle pelvis for straighter support legs, with a reach-based height
limit that preserves planted contacts. Fixed a stale swing-foot anchor when
stopping. No gameplay/physics/control/camera source was changed.

The host preview now preserves its gameplay camera, frames actual mesh
bounds, samples exact intervals, avoids a roundoff-sized extra physics step,
uses a verified wall for climbing, and renders complete trick/roll sequences.
The old height resets in the wall-run fixture are gone. `host/hero_sheets.py`
runs automatically after preview rendering and makes 400×240-per-frame PNG
sheets. Host-only pose diagnostics add no 3DS runtime storage.

Final visual evidence, all under /tmp/hero-preview:

- sheet_run.png: f0–f7_run, examined in order. f0 is recovery/flight, f1
  support with the other heel recovered, f2 flight with knee drive, f3
  compressed support, f4 late support, f5 flight, f6 approaching contact,
  f7 support with the other leg passing. Checked ankle angle changes,
  knee compression, pelvis height and opposing bent arms against the
  Rusty Animator contact/up images and contact/down/passing/up guide.
- sheet_walk.png (g0–g7_walk), sheet_start.png, sheet_stop.png: slow support,
  passing, acceleration, deceleration and settled standing. The late stop
  frames keep both feet down and the torso settles upright.
- sheet_poses.png: h1_swing, h2_run, h2b_run, h3_idle, h4_jump, h5_climb,
  k5_crouch, w2_wing_side. Inspected h5_climb directly after its depth fix.
- sheet_air.png: k0–k3_trick, k6a–k6c_air, wingsuit above; also viewed
  w3_wing_front and w4_wing_above for membrane attachment.
- sheet_wall.png: wr0–wr5_wallrun, continuous ascending contacts; preview
  log confirms ST_WALL and wallRun, including the camera sequence.
- sheet_roll.png: r0–r13_roll, including recovery back into locomotion.
- sheet_trick1.png through sheet_trick5.png: eight frames each of front
  flip, back flip, left roll, right roll, and corkscrew.
- p1_start.png: ordinary game-camera suit readability. Before/after front,
  side, three-quarter, back and mask-close turnarounds are collected in
  /tmp/hero-turnarounds.jpg; Cycles PNGs are /tmp/hero-before_*.png and
  /tmp/hero-model_*.png.

Final commands/results:

```
gcc -O2 -Isource -Wno-misleading-indentation -o /tmp/movetest host/movetest.c -lm
/tmp/movetest                         # all moves ok (0 failures)
gcc -O2 -Isource -Wno-misleading-indentation -o /tmp/simtest host/simtest.c -lm
/tmp/simtest                          # byte-identical to baseline; splashes 0,0,0,0,0,3
gcc -O2 -Isource -Wno-misleading-indentation -o /tmp/animtest host/animtest.c -lm
/tmp/animtest                         # 0 failures
source ../env.sh && make              # .3dsx and .cia built
host/preview.sh /tmp/hero-preview     # completed; every logged hero has 8,232 vertices
```

Animation tests cover 30/60/120 fps, slow walk, jog, sprint, wall run,
start/turn/stop/crouch, climb, roll, swing, wing, dive, all five tricks, mesh
capacity, finite output, bone lengths and identical repeated stereo draws.
Maximum measured flat-contact drift: 0.0000076 world units; maximum bone
length error rounds to 0.00000. Logs: /tmp/hero-final-{moves,sim,anim,build,preview}.log.

Final rendered budget remains 2,744 triangles (5,488 submitted for two eyes),
8,232/8,400 hero vertices, largest part 236/700 indexed vertices, one 256×256
atlas. Both game binaries are rebuilt in webslinger/. The builder regenerates
all model/texture assets. No apt, sudo, commits or pushes were used.

Not verified: real 3DS appearance, PICA texture filtering, stereoscopic
comfort, CPU/GPU frame time or memory behavior on hardware. The model still
uses rigid overlapping limb parts; this is not a fully skinned anatomical
character. Host render checks establish pose continuity and attachment, not
hardware performance or motion-capture accuracy.
