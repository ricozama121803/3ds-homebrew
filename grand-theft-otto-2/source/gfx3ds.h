// Web Slinger - citro3d backend for the 3D scene (3DS only)
#pragma once
#include <3ds.h>
#include <citro3d.h>
#include "scene.h"

bool gfx_init(const Game *g);                                   // false if GPU resources could not be set up
void gfx_exit(void);
bool gfx_reload(const Game *g);                                  // rebuild the static scene after a region change
Scene *gfx_begin(const Game *g);                                // build this frame's dynamic triangles (once per frame, shared by both eyes)
void gfx_draw(const Scene *s, float eye, float slider);         // draw into the render target that is currently active
void gfx_end_3d(void);                                          // restore the state citro2d expects
