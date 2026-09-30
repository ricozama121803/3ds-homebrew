#include "world.h"
#include <math.h>

bool world_init(World *w, const uint8_t *data, size_t size)
{
	if (size < 8) return false;
	w->w = data[0] | (data[1] << 8);
	w->h = data[2] | (data[3] << 8);
	w->spawnx = data[4] | (data[5] << 8);
	w->spawny = data[6] | (data[7] << 8);
	if (size < 8 + (size_t)w->w * w->h) return false;
	w->t = data + 8;
	return true;
}

bool world_blocked(const World *w, float x, float y, float half)
{
	int tx0 = (int)floorf((x - half) / TILE), tx1 = (int)floorf((x + half - 0.001f) / TILE);
	int ty0 = (int)floorf((y - half) / TILE), ty1 = (int)floorf((y + half - 0.001f) / TILE);
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++)
			if (world_flags(w, tx, ty) & F_SOLID) return true;
	return false;
}

// Axis-by-axis movement: no ghost bumps on tile seams, and it can never tunnel through a wall.
void world_move(const World *w, float *x, float *y, float dx, float dy, float half)
{
	// keep each step small enough that a tile can't be skipped over
	float len = fmaxf(fabsf(dx), fabsf(dy));
	int steps = (int)ceilf(len / 4.0f);
	if (steps < 1) steps = 1;
	dx /= steps; dy /= steps;
	for (int s = 0; s < steps; s++) {
		*x += dx;
		if (world_blocked(w, *x, *y, half)) {
			if (dx > 0) *x = floorf((*x + half) / TILE) * TILE - half - 0.002f;
			else if (dx < 0) *x = (floorf((*x - half) / TILE) + 1) * TILE + half + 0.002f;
			dx = 0;
		}
		*y += dy;
		if (world_blocked(w, *x, *y, half)) {
			if (dy > 0) *y = floorf((*y + half) / TILE) * TILE - half - 0.002f;
			else if (dy < 0) *y = (floorf((*y - half) / TILE) + 1) * TILE + half + 0.002f;
			dy = 0;
		}
	}
}
