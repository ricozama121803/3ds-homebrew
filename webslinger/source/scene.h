// Web Slinger - scene builder: turns the game state into triangle lists (no 3DS dependencies, so the PC preview rasterises the same triangles).
#pragma once
#include "game.h"

typedef struct { float x, y, z, u, v, r, g, b, a; } Vtx;       // 36 bytes (floats: the PICA200 does not normalise byte attributes)

// static world: one range per texture (+ sky groups, shadows), built once. Dynamic: rebuilt every frame, fixed capacity per group.
enum { GRP_SKY = MAT_COUNT, GRP_CLOUD, GRP_SUN, GRP_SHADOW, GRP_COUNT };
enum { D_OPAQUE, D_HERO, D_ALPHA, D_ADD, D_COUNT };
#define CAP_D_OPAQUE 4500
#define CAP_D_HERO   7500
#define CAP_D_ALPHA   200
#define CAP_D_ADD     600
#define CAP_DYN (CAP_D_OPAQUE + CAP_D_HERO + CAP_D_ALPHA + CAP_D_ADD)

typedef struct {
	Vtx *stat; int sfirst[GRP_COUNT], scount[GRP_COUNT];
	Vtx *dyn;  int first[D_COUNT], count[D_COUNT];
	float view[16];            // world -> view (row-major, rigid)
	float skyview[16];         // same without the translation
	float fov;                 // vertical, degrees
	float eye[3], right[3], up[3], fwd[3];
} Scene;

int  scene_build_static(const Game *g, Scene *s, Vtx *buf);       // buf == NULL: just count and return the vertex total; otherwise fill (counts per group in s)
void scene_build(const Game *g, Scene *s);                        // dynamic geometry + camera matrices; s->dyn must be set
bool scene_project(const Scene *s, const float p[3], float *sx, float *sy);    // world point -> top-screen pixel (false if behind the camera)
