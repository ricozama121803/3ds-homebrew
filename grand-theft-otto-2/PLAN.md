# Grand Theft Otto II - plan

Fully 3D third-person version of Grand Theft Otto (Otto, the orange-jumpsuit criminal, in an LA-style city), built from the Web Slinger
skeleton (gfx3ds.c / main.c / shader / chunked static scene / PC preview + sim tests). Separate project from `grand-theft-otto/` (UniqueId 0xf8a06).

## Decisions
- Smaller, denser map: 8x8 blocks of 64 m (512 m). Downtown towers in the north-east, low-rise elsewhere, two parks. Grow it later if the frame rate allows.
- Same controls as GTO I: Circle Pad move/steer, A gas (shoot later), B brake/reverse, R run, L handbrake, Y get in/out, X/Y weapons later,
  C-stick / D-pad / touch drag = camera, START pause, SELECT restart.
- Cartoon violence with red blood (as GTO I), later milestones.
- Budget: ~15k triangles per frame (the static world is drawn twice with the 3D slider on). Characters ~600 triangles, cars 300-500.

## Milestones (each one is an installable CIA)
1. DONE - Foundation: city, Otto on foot (camera-relative walk/run, collision), cars (arcade drift physics, building + car collisions),
   enter/exit, chase camera, minimap. Otto and the cars are placeholder prism models. Host test: `host/simtest.c`; frames: `host/preview.sh`.
2. DONE (Otto + 5 cars) - Blender models: `assets/models/` (`make models`): build_otto.py + rig_otto.py (21-bone rig, same topology as Web Slinger's so its CMU mocap retargeting works), export_otto.py -> `source/otto_mesh.h`, import_mocap.py -> `source/otto_anim.h` (walk / jog / sprint), build_cars.py + export_cars.py -> `source/car_mesh.h` (sedan, sports, van, taxi, police; flat-shaded, tagged paint / lights). The game skins Otto on the CPU each frame. Still to model: SWAT van, pedestrian variants, helicopter.
3a. DONE - Life on the streets: `source/traffic.c`. 14 computer-driven cars follow one lane per direction along the grid lines, obey traffic lights (22 s cycle, per-junction offset), turn right / left on Bezier arcs (one left-turner per junction at a time, no entering a junction they cannot leave), brake for cars, pedestrians and Otto. 22 pedestrians walk a pavement graph around the blocks, sometimes stop, cross streets when no car is near, get startled by fast cars and are knocked down by cars that touch them. Y on a stopped traffic car steals it and the driver runs off. Pedestrians use `assets/models/build_ped.py` (410 triangles, same rig and mocap as Otto, outfit roles filled per person). Host soak test: `host/trafficsoak.c` (10-30 simulated minutes, several seeds: no crashes, nobody in buildings, longest stop 36 s).
3b. DONE - LA look: `source/scenery.h` (included by scene.c). Pacific on the west with a boardwalk, sand, lifeguard towers, umbrellas and palms; a 272 m pier (walkable, rails, arcade sheds) ending in a rotating Ferris wheel; dry land and hills to the north with a HOLLYWOOD sign (block letters) and an observatory with copper domes; an elevated freeway along the east edge with moving traffic; two glass landmark towers downtown; palm trees (assets/models/build_palm.py, 68 triangles) along the streets. Each piece is its own chunk so it is culled by distance and view. Otto can walk the beach and the pier (`world_walkable`, `host/walktest.c`); cars may use the beach. New textures: assets/make_textures.py (sand, plank). Fog now ends at 1300 m so the hills show.
4. DONE - Combat and police. `source/combat.c`: fists / pistol / SMG / shotgun with auto-aim (A shoots on foot, X or the touch buttons switch weapon), bullets with travel time that hit people, cars and walls, red blood (particles, stains that fade), people and cars with hit points, cash drops, health / armour / weapon pickups on the pavements (respawn after 80 s), cars that smoke, burn and explode and hurt everything near them, WASTED (lose 10% cash, back at the hospital) and BUSTED (lose weapons + 20% cash, back at the police station), Pay 'n' Spray (repaint + lose the stars). `source/police.c`: heat from crimes gives 1-5 stars, which fade after Otto has been out of sight; officers on foot run to Otto along the pavements (BFS), arrest at 1 star, shoot from 2; police cars and SWAT vans (new model) chase along the streets (junction BFS), ram, and unload officers. 4 weapons shown in the hand through an arm IK over the mocap. Host tests: `host/combattest.c` (scripted scenarios + 20 min random play + a car chase), `host/speedtest.c`. Cars also got faster (the old air-drag term capped them at ~56 km/h; now 130-195 km/h, 0-100 in 2-5 s).
5. Helicopter, SWAT, Pay 'n' Spray, saving, HUD.
6. Missions and polish: taxi, delivery, hit list, title screen.

## Not yet verified
Never run on hardware. The user installs the CIA with FBI and reports frame rate / feel (the 3D slider doubles the draw cost).
