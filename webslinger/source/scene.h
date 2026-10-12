// Web Slinger - scene builder: turns the game state into triangle lists (no 3DS dependencies, so the PC preview rasterises the same triangles).
#pragma once
#include "game.h"

typedef struct { float x, y, z, u, v, r, g, b, a; } Vtx;       // 36 bytes (floats: the PICA200 does not normalise byte attributes)

// static world: one range per texture (+ sky groups, shadows), built once. Dynamic: rebuilt every frame, fixed capacity per group.
#define CH_BLOCKS 3                                           // static geometry is split into chunks of CH_BLOCKS x CH_BLOCKS blocks, culled by distance and view cone
#define CH_N      ((NB + CH_BLOCKS - 1) / CH_BLOCKS)
#define NCH       (CH_N * CH_N)                               // chunk index NCH = always drawn (sea, backdrop)
enum { GRP_SKY = MAT_COUNT, GRP_CLOUD, GRP_SUN, GRP_SHADOW, GRP_DETAIL, GRP_COUNT };                       // GRP_DETAIL: props and cornices (white texture, vertex colours), drawn only for nearby chunks
enum { D_OPAQUE, D_HERO, D_ALPHA, D_ADD, D_COUNT };
#define CAP_D_OPAQUE 4500
#define CAP_D_HERO   7500
#define CAP_D_ALPHA   200
#define CAP_D_ADD     600
#define CAP_DYN (CAP_D_OPAQUE + CAP_D_HERO + CAP_D_ALPHA + CAP_D_ADD)

typedef struct {
	Vtx *stat; int sfirst[GRP_COUNT], scount[GRP_COUNT];       // sky groups (and the totals of the always-drawn chunk)
	int cfirst[GRP_COUNT][NCH + 1], ccount[GRP_COUNT][NCH + 1];
	Vtx *dyn;  int first[D_COUNT], count[D_COUNT];
	unsigned char vis[NCH + 1], near[NCH + 1];  // chunks to draw this frame (set by scene_build)
	float view[16];            // world -> view (row-major, rigid)
	float skyview[16];         // same without the translation
	float fov;                 // vertical, degrees
	float eye[3], right[3], up[3], fwd[3];
} Scene;

int  scene_build_static(const Game *g, Scene *s, Vtx *buf);       // buf == NULL: just count and return the vertex total; otherwise fill (counts per group in s)
void scene_build(const Game *g, Scene *s);                        // dynamic geometry + camera matrices; s->dyn must be set
bool scene_project(const Scene *s, const float p[3], float *sx, float *sy);    // world point -> top-screen pixel (false if behind the camera)
bool scene_chunk_visible(const Scene *s, int chunk);                 // chunk in range and inside the view cone (chunk NCH: always)
