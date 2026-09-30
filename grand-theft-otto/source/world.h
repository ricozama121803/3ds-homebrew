// Grand Theft Otto - the tile world and collision (no 3DS dependencies)
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gen_atlas.h"

#define TILE 16

typedef struct {
	int w, h;
	const uint8_t *t;          // w*h tile ids (row-major), points into the embedded map
	int spawnx, spawny;        // starting tile
} World;

// map.bin: u16 W, H, spawn x, spawn y (little endian), then W*H tile ids
bool world_init(World *w, const uint8_t *data, size_t size);

static inline int world_tile(const World *w, int tx, int ty)
{
	if (tx < 0 || ty < 0 || tx >= w->w || ty >= w->h) return -1;
	return w->t[ty * w->w + tx];
}
static inline int world_flags(const World *w, int tx, int ty)
{
	int id = world_tile(w, tx, ty);
	return id < 0 ? F_SOLID : tile_flags[id];       // beyond the map edge is a solid wall
}

// Moves a square (half-size `half`, centre x,y) by dx,dy and slides along solid tiles.
void world_move(const World *w, float *x, float *y, float dx, float dy, float half);
// Is any solid tile overlapping that square?
bool world_blocked(const World *w, float x, float y, float half);
