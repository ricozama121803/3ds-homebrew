// Grand Theft Otto - the 3D scene builder (no 3DS dependencies, so the PC preview can rasterise the same triangles).
// r3d_build() turns the game state into triangle lists; r3d_3ds.c draws them with citro3d, host/preview3d.c with a software rasteriser.
#pragma once
#include "game.h"
#include "gen3d.h"

typedef struct { float x, y, z, u, v, r, g, b, a; } Vtx;       // 36 bytes: position, texture coordinate, colour (floats: the PICA does not normalise byte attributes)

enum { G_OPAQUE, G_ALPHA, G_SPRITE, G_COUNT };                   // opaque (alpha-tested) world texture / blended world texture / blended sprite texture
#define CAP_OPAQUE  44000
#define CAP_ALPHA    6000
#define CAP_SPRITE  12000
#define CAP_TOTAL   (CAP_OPAQUE + CAP_ALPHA + CAP_SPRITE)

typedef struct {
	Vtx *buf;                                                    // CAP_TOTAL vertices; the three groups live at fixed offsets
	int first[G_COUNT], count[G_COUNT];
	float eye[3], target[3], fov_deg, focus;                     // camera (world coordinates: x east, y up, z south)
	float view[16];                                              // world -> view, row-major
	float sky[3];                                                // clear colour (the haze distant ground fades into)
} R3DScene;

void r3d_init(const World *w);                                   // once per game: building heights and the baked shadow map
void r3d_build(const Game *g, R3DScene *s);                      // fill s->buf; set s->buf first
