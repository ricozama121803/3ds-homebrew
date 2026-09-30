#include "nav.h"
#include "gen_map.h"
#include <string.h>

// ---------------------------------------------------------------- flow fields
bool nav_foot_ok(const World *w, int tx, int ty) { return !(world_flags(w, tx, ty) & F_SOLID); }

bool nav_car_ok(const World *w, int tx, int ty)
{
	int fl = world_flags(w, tx, ty);
	if (fl & (F_SOLID | F_WATER)) return false;
	if (fl & F_ROAD) return true;
	// off-road: only where there's room around the tile
	return !(world_flags(w, tx + 1, ty) & F_SOLID) && !(world_flags(w, tx - 1, ty) & F_SOLID) &&
	       !(world_flags(w, tx, ty + 1) & F_SOLID) && !(world_flags(w, tx, ty - 1) & F_SOLID);
}

static bool ok(const Field *f, const World *w, int tx, int ty)
{
	return f->mode == NAV_FOOT ? nav_foot_ok(w, tx, ty) : nav_car_ok(w, tx, ty);
}

void field_begin(Field *f, const World *w, int mode, int sx, int sy, int limit)
{
	int b = 1 - f->front;
	f->mode = mode; f->limit = limit;
	memset(f->d[b], 0xFF, sizeof f->d[b]);
	f->qh = f->qt = 0;
	// if the source itself isn't passable (e.g. the player standing between trees), search the closest tile that is
	int bx = sx, by = sy;
	if (!ok(f, w, sx, sy)) {
		bool found = false;
		for (int r = 1; r <= 6 && !found; r++)
			for (int dy = -r; dy <= r && !found; dy++)
				for (int dx = -r; dx <= r && !found; dx++)
					if ((dx == -r || dx == r || dy == -r || dy == r) && ok(f, w, sx + dx, sy + dy)) { bx = sx + dx; by = sy + dy; found = true; }
		if (!found) { f->building = false; return; }
	}
	if (bx < 0 || by < 0 || bx >= w->w || by >= w->h) { f->building = false; return; }
	f->d[b][by * w->w + bx] = 0;
	f->q[f->qt++] = by * w->w + bx;
	f->building = true;
}

void field_step(Field *f, const World *w, int budget)
{
	if (!f->building) return;
	int b = 1 - f->front;
	static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
	while (budget-- > 0 && f->qh < f->qt) {
		int cur = f->q[f->qh++];
		int x = cur % w->w, y = cur / w->w;
		uint16_t nd = f->d[b][cur] + 1;
		if (nd > f->limit) continue;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], ny = y + dy[k];
			if (nx < 0 || ny < 0 || nx >= w->w || ny >= w->h) continue;
			int ni = ny * w->w + nx;
			if (f->d[b][ni] != 0xFFFF || !ok(f, w, nx, ny)) continue;
			f->d[b][ni] = nd;
			f->q[f->qt++] = ni;
		}
	}
	if (f->qh >= f->qt) { f->front = b; f->building = false; f->ready = true; }
}

bool field_dir(const Field *f, float x, float y, float *tx, float *ty)
{
	if (!f->ready) return false;
	const uint16_t *d = f->d[f->front];
	int cx = (int)(x / TILE), cy = (int)(y / TILE);
	if (cx < 0 || cy < 0 || cx >= NAV_W || cy >= NAV_H) return false;
	uint16_t here = d[cy * NAV_W + cx];
	if (here == 0xFFFF) return false;
	if (here == 0) return false;                                  // we're on the source tile
	uint16_t best = here; int bx = cx, by = cy;
	for (int oy = -1; oy <= 1; oy++) {
		for (int ox = -1; ox <= 1; ox++) {
			if (!ox && !oy) continue;
			int nx = cx + ox, ny = cy + oy;
			if (nx < 0 || ny < 0 || nx >= NAV_W || ny >= NAV_H) continue;
			uint16_t nd = d[ny * NAV_W + nx];
			if (nd >= best) continue;
			if (ox && oy && (d[cy * NAV_W + nx] == 0xFFFF || d[ny * NAV_W + cx] == 0xFFFF)) continue;   // no cutting corners
			best = nd; bx = nx; by = ny;
		}
	}
	if (best >= here) return false;
	*tx = bx * TILE + TILE * 0.5f; *ty = by * TILE + TILE * 0.5f;
	return true;
}

// ---------------------------------------------------------------- roads
Road roads[64];
int nroads;

void roads_init(void)
{
	nroads = 0;
	for (int i = 0; i < NHROADS; i++) roads[nroads++] = (Road){0, hroads[i].a0, hroads[i].a1, hroads[i].b0, hroads[i].b1};
	for (int i = 0; i < NVROADS; i++) roads[nroads++] = (Road){1, vroads[i].a0, vroads[i].a1, vroads[i].b0, vroads[i].b1};
}

int road_lanes(const Road *r) { return (r->a1 - r->a0 + 1) >= 8 ? 2 : 1; }

// Horizontal streets: north half runs west, south half runs east. Vertical: west half runs south, east half runs north.
int road_half_dir(const Road *r, int half)
{
	if (r->axis == 0) return half == 0 ? -1 : +1;
	return half == 0 ? +1 : -1;
}

float road_lane_centre(const Road *r, int half, int lane)
{
	float w = (float)(r->a1 - r->a0 + 1) * TILE;
	float base = r->a0 * (float)TILE;
	return half == 0 ? base + 16.0f + 32.0f * lane : base + w - 16.0f - 32.0f * lane;
}
