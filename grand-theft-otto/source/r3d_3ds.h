// Grand Theft Otto - citro3d backend for the 3D scene (3DS only)
#pragma once
#include <3ds.h>
#include <citro3d.h>
#include "r3d.h"

bool r3d3ds_init(void);                                            // false if the GPU resources could not be set up (the game then stays in 2D)
void r3d3ds_exit(void);
R3DScene *r3d3ds_begin(const Game *g);                             // build this frame's triangles (once per frame, shared by both eyes)
void r3d3ds_draw(const R3DScene *s, float eye, float slider);      // draw into the render target that is currently active
void r3d3ds_end_3d(void);                                          // restore the state citro2d expects
