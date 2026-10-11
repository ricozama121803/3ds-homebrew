// Sunset Ops - scene builder: turns the game state into triangle lists (no 3DS dependencies, so the PC preview rasterises the same triangles).
#pragma once
#include "game.h"

typedef struct { float x, y, z, u, v, r, g, b, a; } Vtx;       // 36 bytes (floats: the PICA200 does not normalise byte attributes)

// static world: one range per texture (+ sky groups), built once. Dynamic: rebuilt every frame, fixed capacity per group.
enum { GRP_SKY = MAT_COUNT, GRP_CLOUD, GRP_SUN, GRP_COUNT };
enum { D_OPAQUE, D_ALPHA, D_ADD, D_VM, D_VMADD, D_COUNT };
#define CAP_D_OPAQUE 24000
#define CAP_D_ALPHA  3200
#define CAP_D_ADD    3600
#define CAP_D_VM    10000
#define CAP_D_VMADD   300
#define CAP_DYN (CAP_D_OPAQUE + CAP_D_ALPHA + CAP_D_ADD + CAP_D_VM + CAP_D_VMADD)

typedef struct {
	Vtx *stat; int sfirst[GRP_COUNT], scount[GRP_COUNT];
	Vtx *dyn;  int first[D_COUNT], count[D_COUNT];
	float view[16];            // world -> view (row-major, rigid)
	float skyview[16];         // same without the translation
	float fov;                 // vertical, degrees (world)
	float vmfov;               // vertical, degrees (view model)
	float eye[3], right[3], up[3], fwd[3];
	float focus;               // stereo convergence distance
} Scene;

int scene_build_static(const Game *g, Scene *s, Vtx *buf);         // buf == NULL: just count and return the vertex total; otherwise fill (counts per group in s)
void scene_build(const Game *g, Scene *s);          // dynamic geometry + camera matrices; s->dyn must be set
bool scene_project(const Scene *s, const float p[3], float *sx, float *sy);   // world point -> top-screen pixel (false if behind the camera)
void scene_sun_dir(float d[3]);
void scene_prebuild(int quality);                   // tessellate the soldier / gun model caches up front (avoids hitches in the first fight)
