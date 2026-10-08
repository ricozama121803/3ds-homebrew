// Grand Theft Otto - navigation: flow fields (for cops chasing the player) and the road lane network (for traffic)
#pragma once
#include "world.h"

#define NAV_W 256
#define NAV_H 256
#define NAV_CELLS (NAV_W * NAV_H)

// A flow field: distance (in tiles) from a source tile, built a few hundred cells per frame so it never causes a hitch.
// Two buffers: agents read the finished one while the next one is being built.
typedef struct {
	uint16_t d[2][NAV_CELLS];
	int32_t q[NAV_CELLS];
	int front, qh, qt, mode, limit;
	bool building, ready;
} Field;

enum { NAV_FOOT, NAV_CAR };
void field_begin(Field *f, const World *w, int mode, int sx, int sy, int limit);
void field_step(Field *f, const World *w, int budget);
// Where should something standing at (x,y) head next to get closer to the source? Returns false if unreachable/unknown.
bool field_dir(const Field *f, float x, float y, float *tx, float *ty);
bool nav_car_ok(const World *w, int tx, int ty);
bool nav_foot_ok(const World *w, int tx, int ty);

// ---- roads: every street is a rectangle with lanes on both sides (right-hand traffic)
typedef struct { int axis; int a0, a1, b0, b1; } Road;      // axis 0: runs along x, 1: runs along y. a = across (tiles), b = along (tiles)
extern Road roads[64];
extern int nroads;
void roads_init(void);
int road_lanes(const Road *r);                              // lanes per direction (2 on wide streets, 1 on narrow)
// forward direction (+1 / -1 along the axis) of the given half of the road
int road_half_dir(const Road *r, int half);
// centre of a lane across the street, in pixels (lane 0 = the one beside the curb)
float road_lane_centre(const Road *r, int half, int lane);
