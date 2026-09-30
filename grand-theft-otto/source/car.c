// Grand Theft Otto - cars: physics, collisions, traffic, police chases, stealing
#include "game.h"
#include "gen_atlas.h"
#include "gen_map.h"
#include <math.h>
#include <string.h>

//                     len   wid  maxspd accel steer mass  hp
const CarDef car_defs[M_COUNT] = {
	{22.0f, 12.0f, 175.0f, 250.0f, 2.9f, 1.0f,  90.0f},   // compact
	{26.0f, 13.0f, 190.0f, 235.0f, 2.6f, 1.2f, 110.0f},   // sedan
	{25.0f, 13.0f, 255.0f, 330.0f, 2.9f, 1.0f, 100.0f},   // sports
	{30.0f, 15.0f, 150.0f, 175.0f, 2.0f, 1.9f, 140.0f},   // van
	{26.0f, 13.0f, 185.0f, 235.0f, 2.6f, 1.2f, 110.0f},   // taxi
	{26.0f, 13.0f, 240.0f, 315.0f, 2.8f, 1.3f, 130.0f},   // police
	{30.0f, 15.0f, 205.0f, 255.0f, 2.2f, 2.2f, 240.0f},   // SWAT van
};

static int variant_of_model(int model, int pick)
{
	int n = 0;
	for (int v = 0; v < NCARVARS; v++) if (carvar_model[v] == model) n++;
	if (!n) return 0;
	pick %= n;
	for (int v = 0; v < NCARVARS; v++) if (carvar_model[v] == model && pick-- == 0) return v;
	return 0;
}

static float speed_of(const Car *c) { return sqrtf(c->vx * c->vx + c->vy * c->vy); }

void car_circles(const Car *c, float cx[3], float cy[3], float *r)
{
	const CarDef *d = &car_defs[c->model];
	float rad = d->wid * 0.5f + 0.6f, off = d->len * 0.5f - rad;
	float fx = sinf(c->heading), fy = -cosf(c->heading);
	for (int k = 0; k < 3; k++) { float o = (k - 1) * off; cx[k] = c->x + fx * o; cy[k] = c->y + fy * o; }
	*r = rad;
}

static bool circle_blocked(const World *w, float x, float y, float r)
{
	int tx0 = (int)floorf((x - r) / TILE), tx1 = (int)floorf((x + r) / TILE);
	int ty0 = (int)floorf((y - r) / TILE), ty1 = (int)floorf((y + r) / TILE);
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++) {
			if (!(world_flags(w, tx, ty) & F_SOLID)) continue;
			float nx = clampf(x, tx * (float)TILE, (tx + 1) * (float)TILE), ny = clampf(y, ty * (float)TILE, (ty + 1) * (float)TILE);
			if ((x - nx) * (x - nx) + (y - ny) * (y - ny) < r * r) return true;
		}
	return false;
}

bool car_area_free(const Game *g, float x, float y, float heading, int model, int ignore)
{
	Car probe; memset(&probe, 0, sizeof probe);
	probe.x = x; probe.y = y; probe.heading = heading; probe.model = model;
	float cx[3], cy[3], r;
	car_circles(&probe, cx, cy, &r);
	for (int k = 0; k < 3; k++) {
		if (circle_blocked(&g->world, cx[k], cy[k], r)) return false;
		for (int j = 0; j < MAX_CARS; j++) {
			const Car *o = &g->cars[j];
			if (!o->active || j == ignore) continue;
			float dx = o->x - x, dy = o->y - y;
			if (dx * dx + dy * dy < 46.0f * 46.0f) return false;
		}
	}
	return true;
}

int car_spawn(Game *g, int variant, float x, float y, float heading, CarState st, Driver drv)
{
	for (int i = 0; i < MAX_CARS; i++) {
		Car *c = &g->cars[i];
		if (c->active) continue;
		memset(c, 0, sizeof *c);
		c->active = true; c->variant = variant; c->model = carvar_model[variant];
		c->state = st; c->driver = drv;
		c->x = x; c->y = y; c->heading = heading;
		c->hp = car_defs[c->model].hp;
		c->road = -1; c->spot = -1;
		c->cruise = g_rndf(g, 62.0f, 110.0f);
		return i;
	}
	return -1;
}

void car_remove(Game *g, int i)
{
	Car *c = &g->cars[i];
	if (!c->active) return;
	if (c->spot >= 0 && c->spot < MAX_PARK && g->park_car[c->spot] == i) g->park_car[c->spot] = -1;
	c->active = false;
}

// ---------------------------------------------------------------- physics
static void car_move(Game *g, int i, float dt)
{
	Car *c = &g->cars[i];
	const CarDef *d = &car_defs[c->model];

	// steering first, then split the (unchanged) velocity into forward and sideways parts of the new heading: that's what makes drifts
	float fx = sinf(c->heading), fy = -cosf(c->heading);
	float vf = c->vx * fx + c->vy * fy;
	float turn = c->steer * d->steer * clampf(vf / 70.0f, -1.0f, 1.0f) * (c->hand > 0 ? 1.5f : 1.0f);
	c->heading = wrap_pi(c->heading + turn * dt);
	fx = sinf(c->heading); fy = -cosf(c->heading);
	float rx = cosf(c->heading), ry = sinf(c->heading);
	vf = c->vx * fx + c->vy * fy;
	float vr = c->vx * rx + c->vy * ry;

	if (c->throttle > 0) vf += d->accel * c->throttle * dt * (1.0f - clampf(vf / d->maxspeed, 0, 1));
	if (c->brake > 0) {
		if (vf > 4.0f) vf -= d->accel * 1.6f * c->brake * dt;
		else vf = fmaxf(vf - d->accel * 0.5f * c->brake * dt, -0.35f * d->maxspeed);    // then reverse
	}
	if (c->throttle <= 0 && c->brake <= 0) { float f = fminf(fabsf(vf), 55.0f * dt); vf -= vf > 0 ? f : -f; }
	if (c->hand > 0) { float f = fminf(fabsf(vf), 90.0f * dt); vf -= vf > 0 ? f : -f; }
	vf *= 1.0f - 0.10f * dt;
	vr *= expf(-(c->hand > 0 ? 1.3f : 9.0f) * dt);

	c->vx = fx * vf + rx * vr;
	c->vy = fy * vf + ry * vr;

	// move in small steps, resolving hits with walls as we go
	float dist = sqrtf(c->vx * c->vx + c->vy * c->vy) * dt;
	int steps = (int)ceilf(dist / 3.0f); if (steps < 1) steps = 1;
	for (int s = 0; s < steps; s++) {
		c->x += c->vx * dt / steps; c->y += c->vy * dt / steps;
		float cx[3], cy[3], r;
		car_circles(c, cx, cy, &r);
		for (int k = 0; k < 3; k++) {
			int tx0 = (int)floorf((cx[k] - r) / TILE), tx1 = (int)floorf((cx[k] + r) / TILE);
			int ty0 = (int)floorf((cy[k] - r) / TILE), ty1 = (int)floorf((cy[k] + r) / TILE);
			for (int ty = ty0; ty <= ty1; ty++)
				for (int tx = tx0; tx <= tx1; tx++) {
					if (!(world_flags(&g->world, tx, ty) & F_SOLID)) continue;
					float nx = clampf(cx[k], tx * (float)TILE, (tx + 1) * (float)TILE), ny = clampf(cy[k], ty * (float)TILE, (ty + 1) * (float)TILE);
					float ddx = cx[k] - nx, ddy = cy[k] - ny, dd2 = ddx * ddx + ddy * ddy;
					if (dd2 >= r * r) continue;
					float dd = sqrtf(dd2), nxn, nyn, pen;
					if (dd > 0.001f) { nxn = ddx / dd; nyn = ddy / dd; pen = r - dd; }
					else {                                            // centre is inside the tile: push out along the shortest way
						float l = cx[k] - tx * (float)TILE, rt = (tx + 1) * (float)TILE - cx[k], t = cy[k] - ty * (float)TILE, b = (ty + 1) * (float)TILE - cy[k];
						float m = fminf(fminf(l, rt), fminf(t, b));
						nxn = m == l ? -1.0f : m == rt ? 1.0f : 0; nyn = m == t ? -1.0f : m == b ? 1.0f : 0;
						if (nxn == 0 && nyn == 0) nxn = 1;
						pen = r + m;
					}
					c->x += nxn * pen; c->y += nyn * pen;
					cx[k] += nxn * pen; cy[k] += nyn * pen;
					float vn = c->vx * nxn + c->vy * nyn;
					if (vn < 0) {
						c->vx -= 1.25f * vn * nxn; c->vy -= 1.25f * vn * nyn;
						float impact = -vn;
						if (impact > 55.0f) {
							damage_car(g, i, (impact - 55.0f) * 0.32f / d->mass, c->state == CS_PLAYER ? 3 : 2);
							fx_part(g, P_SPARK, cx[k] - nxn * r, cy[k] - nyn * r, 0, 0, 0.12f, 0);
							if (c->state == CS_PLAYER) g->flashT = fmaxf(g->flashT, 0.08f);
							if (c->state == CS_PLAYER && impact > 120.0f) damage_player(g, (impact - 120.0f) * 0.12f, 0, 0);
						}
					}
				}
		}
	}
}

// ---------------------------------------------------------------- traffic: following lanes
static bool plan_route(Game *g, Car *c)
{
	if (c->road < 0) return false;
	const Road *R = &roads[c->road];
	int dir = road_half_dir(R, c->half);
	float lanec = road_lane_centre(R, c->half, c->lane);
	float s = R->axis == 0 ? c->x : c->y;

	int bq = -1; float bentry = 0, bexit = 0, bd = 1e9f;
	for (int q = 0; q < nroads; q++) {
		const Road *Q = &roads[q];
		if (Q->axis == R->axis) continue;
		if (lanec < Q->b0 * (float)TILE || lanec >= (Q->b1 + 1) * (float)TILE) continue;      // Q doesn't cross our lane
		if (R->a0 < Q->b0 || R->a1 > Q->b1) continue;                                          // ...or doesn't span the street
		float entry = dir > 0 ? Q->a0 * (float)TILE : (Q->a1 + 1) * (float)TILE;
		float exit_ = dir > 0 ? (Q->a1 + 1) * (float)TILE : Q->a0 * (float)TILE;
		if (dir > 0 ? entry < s - 6 : entry > s + 6) continue;
		if (fminf(entry, exit_) < R->b0 * (float)TILE || fmaxf(entry, exit_) > (R->b1 + 1) * (float)TILE) continue;
		float dd = fabsf(entry - s);
		if (dd < bd) { bd = dd; bq = q; bentry = entry; bexit = exit_; }
	}
	if (bq < 0) return false;
	const Road *Q = &roads[bq];

	// which manoeuvres are possible here?
	bool can_straight = (bexit + dir * 96.0f >= R->b0 * (float)TILE) && (bexit + dir * 96.0f <= (R->b1 + 1) * (float)TILE);
	// right-hand vector of travel along R, then the direction along Q that it points
	int tr = R->axis == 0 ? dir : -dir;       // turn "right" as a sign along Q's axis
	int tl = -tr;
	float rfar = (R->a1 + 1) * (float)TILE, rnear = R->a0 * (float)TILE;
	bool can_r = tr > 0 ? rfar + 96 <= (Q->b1 + 1) * (float)TILE : rnear - 96 >= Q->b0 * (float)TILE;
	bool can_l = tl > 0 ? rfar + 96 <= (Q->b1 + 1) * (float)TILE : rnear - 96 >= Q->b0 * (float)TILE;
	int choices[5], n = 0;
	if (can_straight) { choices[n++] = 0; choices[n++] = 0; choices[n++] = 0; }
	if (can_r) choices[n++] = 1;
	if (can_l) choices[n++] = 2;
	if (!n) return false;
	int m = choices[g_rndi(g, n)];

	float p0x = R->axis == 0 ? bentry : lanec, p0y = R->axis == 0 ? lanec : bentry;
	c->px[0] = c->x; c->py[0] = c->y;
	c->px[1] = p0x; c->py[1] = p0y;
	if (m == 0) {
		c->px[2] = R->axis == 0 ? bexit + dir * 34.0f : lanec; c->py[2] = R->axis == 0 ? lanec : bexit + dir * 34.0f;
		c->np = 3;
	} else {
		int t = m == 1 ? tr : tl;
		int qhalf = road_half_dir(Q, 0) == t ? 0 : 1;
		int nl = road_lanes(Q);
		int qlane = m == 1 ? 0 : nl - 1;
		float qc = road_lane_centre(Q, qhalf, qlane);
		float ex = t > 0 ? rfar + 34.0f : rnear - 34.0f;
		if (R->axis == 0) { c->px[2] = qc; c->py[2] = lanec; c->px[3] = qc; c->py[3] = ex; }
		else              { c->px[2] = lanec; c->py[2] = qc; c->px[3] = ex; c->py[3] = qc; }
		c->np = 4;
		c->road = bq; c->half = qhalf; c->lane = qlane;
	}
	c->pi = 1;
	return true;
}

// is something (car, person, the player) close in front of this car?  Returns the distance to it, or 999.
static float obstacle_ahead(const Game *g, int self, float reach, float halfwidth)
{
	const Car *c = &g->cars[self];
	float fx = sinf(c->heading), fy = -cosf(c->heading), best = 999.0f;
	for (int j = 0; j < MAX_CARS; j++) {
		const Car *o = &g->cars[j];
		if (!o->active || j == self) continue;
		float dx = o->x - c->x, dy = o->y - c->y;
		float along = dx * fx + dy * fy;
		float lat = dx * cosf(c->heading) + dy * sinf(c->heading);
		if (along > 0 && along < reach && fabsf(lat) < halfwidth + car_defs[o->model].wid * 0.5f && along < best) best = along;
	}
	for (int j = 0; j < MAX_PEDS; j++) {
		const Ped *p = &g->peds[j];
		if (!p->active || p->state == PS_DEAD) continue;
		float dx = p->x - c->x, dy = p->y - c->y;
		float along = dx * fx + dy * fy, lat = dx * cosf(c->heading) + dy * sinf(c->heading);
		if (along > 0 && along < reach && fabsf(lat) < halfwidth + 4.0f && along < best) best = along;
	}
	if (g->p.status == PL_ALIVE && g->p.car < 0) {
		float dx = g->p.x - c->x, dy = g->p.y - c->y;
		float along = dx * fx + dy * fy, lat = dx * cosf(c->heading) + dy * sinf(c->heading);
		if (along > 0 && along < reach && fabsf(lat) < halfwidth + 4.0f && along < best) best = along;
	}
	return best;
}

static void ai_traffic(Game *g, int i, float dt)
{
	Car *c = &g->cars[i];
	if (c->np < 2 && !plan_route(g, c)) { c->lostT += dt; if (c->lostT > 0.2f) c->wreckT = -1; return; }   // wreckT = -1 marks "remove me"
	float bx = c->px[c->pi], by = c->py[c->pi], ax = c->px[c->pi - 1], ay = c->py[c->pi - 1];
	float sx = bx - ax, sy = by - ay, sl = sqrtf(sx * sx + sy * sy);
	float t = sl > 0.01f ? ((c->x - ax) * sx + (c->y - ay) * sy) / (sl * sl) : 1.0f;
	float ux = sl > 0.01f ? sx / sl : 0, uy = sl > 0.01f ? sy / sl : 0;
	float ahead = clampf(t, 0.0f, 1.0f) * sl + 30.0f;
	float lx = ax + ux * fminf(ahead, sl), ly = ay + uy * fminf(ahead, sl);
	float err = wrap_pi(atan2f(lx - c->x, -(ly - c->y)) - c->heading);
	c->steer = clampf(err * 2.6f, -1.0f, 1.0f);

	float dist_end = sqrtf((bx - c->x) * (bx - c->x) + (by - c->y) * (by - c->y));
	if (dist_end < 22.0f || t >= 1.0f) {
		c->pi++;
		if (c->pi >= c->np) { c->np = 0; c->px[0] = c->x; }
	}

	// speed: cruise, slower in corners and near junctions, stop for whatever is ahead
	float target = c->cruise;
	if (fabsf(err) > 0.5f) target = 48.0f;
	if (c->pi < c->np && c->pi >= 1 && c->np >= 4 && dist_end < 70.0f) target = fminf(target, 52.0f);
	float obs = obstacle_ahead(g, i, 64.0f, 8.0f);
	if (obs < 64.0f) target = obs < 30.0f ? 0.0f : fminf(target, (obs - 28.0f) * 2.2f);
	// waited a while behind something: creep forward (people get out of the way, jams untangle)
	if (obs < 34.0f) c->waitT += dt; else c->waitT = fmaxf(0, c->waitT - dt * 2.0f);
	if (c->waitT > 3.5f) target = 20.0f;
	float v = speed_of(c);
	c->throttle = v < target - 3.0f ? 1.0f : 0.0f;
	c->brake = v > target + 10.0f ? (target < 1.0f ? 1.0f : 0.5f) : 0.0f;

	// stuck for a long time -> the driver gives up and walks off
	if (v < 6.0f && target > 10.0f) c->stuckT += dt; else c->stuckT = fmaxf(0, c->stuckT - dt);
	if (c->stuckT > 7.0f) { c->wreckT = -1; }
}

// ---------------------------------------------------------------- police chase
static void ai_chase(Game *g, int i, float dt)
{
	Car *c = &g->cars[i];
	Player *pl = &g->p;
	float px = pl->car >= 0 ? g->cars[pl->car].x : pl->x, py = pl->car >= 0 ? g->cars[pl->car].y : pl->y;
	float dx = px - c->x, dy = py - c->y, dist = sqrtf(dx * dx + dy * dy);
	bool los = los_clear(g, c->x, c->y, px, py);
	float v = speed_of(c);
	c->sirenT += dt;

	float tx = px, ty = py;
	if (!(los && dist < 150.0f)) {
		float t1x, t1y, t2x, t2y;
		if (field_dir(&g->car_field, c->x, c->y, &t1x, &t1y)) {
			tx = t1x; ty = t1y;
			if (field_dir(&g->car_field, t1x, t1y, &t2x, &t2y)) { tx = t2x; ty = t2y; }
		}
	}
	float err = wrap_pi(atan2f(tx - c->x, -(ty - c->y)) - c->heading);
	c->steer = clampf(err * 2.4f, -1.0f, 1.0f);
	c->throttle = fabsf(err) < 1.0f ? 1.0f : 0.35f;
	c->brake = (fabsf(err) > 1.5f && v > 60.0f) ? 0.7f : 0.0f;

	// stuck against something: back out
	if (c->reverseT > 0) { c->reverseT -= dt; c->throttle = 0; c->brake = 1; c->steer = -c->steer; return; }
	if (v < 10.0f && dist > 40.0f) c->stuckT += dt; else c->stuckT = 0;
	if (c->stuckT > 1.0f) { c->reverseT = 0.9f; c->stuckT = 0; }

	// close to an on-foot suspect: the officers get out
	bool suspect_on_foot = pl->car < 0;
	if (g->stars > 0 && suspect_on_foot && dist < 100.0f && los && v < 90.0f) {
		bool swat = c->driver == DRV_SWAT;
		int n = swat ? 2 : 1;
		for (int k = 0; k < n; k++) {
			float ox = cosf(c->heading) * (k ? -14.0f : 14.0f), oy = sinf(c->heading) * (k ? -14.0f : 14.0f);
			ped_spawn(g, swat ? PK_SWAT : PK_COP, swat ? 7 : 6, c->x + ox, c->y + oy);
		}
		c->state = CS_PARKED; c->driver = DRV_NONE; c->throttle = 0; c->brake = 1;
	}
}

// ---------------------------------------------------------------- collisions between cars, people and cars
static void car_vs_car(Game *g, int i, int j)
{
	Car *a = &g->cars[i], *b = &g->cars[j];
	float ax[3], ay[3], ar, bx[3], by[3], br;
	car_circles(a, ax, ay, &ar); car_circles(b, bx, by, &br);
	float bestpen = 0, nx = 0, ny = 0;
	for (int k = 0; k < 3; k++)
		for (int m = 0; m < 3; m++) {
			float dx = ax[k] - bx[m], dy = ay[k] - by[m], d = sqrtf(dx * dx + dy * dy), pen = ar + br - d;
			if (pen > bestpen) { bestpen = pen; if (d > 0.001f) { nx = dx / d; ny = dy / d; } else { nx = 1; ny = 0; } }
		}
	if (bestpen <= 0) return;
	float ma = car_defs[a->model].mass, mb = car_defs[b->model].mass;
	float wa = mb / (ma + mb), wb = ma / (ma + mb);
	// don't push a car into a wall: only the other one moves if this one is blocked
	a->x += nx * bestpen * wa; a->y += ny * bestpen * wa;
	b->x -= nx * bestpen * wb; b->y -= ny * bestpen * wb;
	float vn = (a->vx - b->vx) * nx + (a->vy - b->vy) * ny;
	if (vn < 0) {
		float jimp = -(1.0f + 0.3f) * vn / (1.0f / ma + 1.0f / mb);
		a->vx += jimp * nx / ma; a->vy += jimp * ny / ma;
		b->vx -= jimp * nx / mb; b->vy -= jimp * ny / mb;
		float impact = -vn;
		if (impact > 40.0f) {
			float dmg = (impact - 40.0f) * 0.30f;
			bool pa = a->state == CS_PLAYER, pb = b->state == CS_PLAYER;
			if (pa) b->hitT = 6.0f;
			if (pb) a->hitT = 6.0f;
			damage_car(g, i, dmg * mb / (ma + mb) * 1.4f, pb ? 0 : 2);
			damage_car(g, j, dmg * ma / (ma + mb) * 1.4f, pa ? 0 : 2);
			fx_part(g, P_SPARK, (ax[1] + bx[1]) * 0.5f, (ay[1] + by[1]) * 0.5f, 0, 0, 0.12f, 0);
			if (pa || pb) { g->flashT = fmaxf(g->flashT, 0.08f); if (impact > 110.0f) damage_player(g, (impact - 110.0f) * 0.1f, nx, ny); }
			// civilians don't like being rammed: they get out and run
			if ((a->driver == DRV_CIV && a->state == CS_TRAFFIC && a->hp < car_defs[a->model].hp * 0.5f)) a->wreckT = -1;
			if ((b->driver == DRV_CIV && b->state == CS_TRAFFIC && b->hp < car_defs[b->model].hp * 0.5f)) b->wreckT = -1;
		}
	}
}

static void car_vs_people(Game *g, int i)
{
	Car *c = &g->cars[i];
	float v = speed_of(c);
	if (v < 22.0f) return;
	float cx[3], cy[3], r;
	car_circles(c, cx, cy, &r);
	int src = c->state == CS_PLAYER ? 3 : 2;
	for (int k = 0; k < MAX_PEDS; k++) {
		Ped *p = &g->peds[k];
		if (!p->active || p->state == PS_DEAD) continue;
		for (int m = 0; m < 3; m++) {
			float dx = p->x - cx[m], dy = p->y - cy[m];
			if (dx * dx + dy * dy > (r + 4.5f) * (r + 4.5f)) continue;
			float vx = c->vx / v, vy = c->vy / v;
			damage_ped(g, k, v * 0.85f, vx, vy, src);
			p->vx = c->vx * 0.6f; p->vy = c->vy * 0.6f;
			if (c->state == CS_PLAYER) c->hitT = 6.0f;
			break;
		}
	}
	if (g->p.car < 0 && g->p.status == PL_ALIVE) {
		for (int m = 0; m < 3; m++) {
			float dx = g->p.x - cx[m], dy = g->p.y - cy[m];
			if (dx * dx + dy * dy > (r + 5.0f) * (r + 5.0f)) continue;
			damage_player(g, v * 0.32f, c->vx, c->vy);
			g->p.vx = c->vx * 0.7f; g->p.vy = c->vy * 0.7f;
			break;
		}
	}
}

// ---------------------------------------------------------------- driver leaves a car (scared, or the car is wrecked)
static void driver_bails(Game *g, Car *c)
{
	if (c->driver == DRV_CIV) {
		float ox = cosf(c->heading) * 12.0f, oy = sinf(c->heading) * 12.0f;
		int p = ped_spawn(g, PK_CIV, g_rndi(g, 6), c->x + ox, c->y + oy);
		if (p >= 0) { g->peds[p].state = PS_FLEE; g->peds[p].stateT = 6.0f; g->peds[p].threatX = g->p.x; g->peds[p].threatY = g->p.y; }
	}
	c->driver = DRV_NONE;
}

// ---------------------------------------------------------------- update
void cars_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_CARS; i++) {
		Car *c = &g->cars[i];
		if (!c->active) continue;
		c->hitT = fmaxf(0, c->hitT - dt);

		switch (c->state) {
		case CS_TRAFFIC: ai_traffic(g, i, dt); break;
		case CS_CHASE:   ai_chase(g, i, dt); break;
		case CS_PARKED:  c->throttle = 0; c->brake = 1; c->steer = 0; break;
		case CS_WRECK:   c->throttle = c->brake = c->steer = 0; c->vx *= 1.0f - 2.5f * dt; c->vy *= 1.0f - 2.5f * dt; c->wreckT += dt; break;
		case CS_PLAYER:  break;                                 // controls were set from input
		}

		car_move(g, i, dt);

		// damage effects
		float frac = c->hp / car_defs[c->model].hp;
		if (c->state != CS_WRECK) {
			if (frac < 0.45f && g_rndf(g, 0, 1) < dt * 6.0f) fx_part(g, P_SMOKE, c->x + g_rndf(g, -4, 4), c->y + g_rndf(g, -4, 4), g_rndf(g, -6, 6), -12.0f, 0.9f, g_rndi(g, 3));
			if (frac < 0.22f) {
				if (g_rndf(g, 0, 1) < dt * 10.0f) fx_part(g, P_FIRE, c->x + g_rndf(g, -4, 4), c->y + g_rndf(g, -5, 5), 0, -10.0f, 0.5f, g_rndi(g, 3));
				damage_car(g, i, 7.0f * dt, 2);                   // burning cars blow up in a few seconds
				if (c->driver == DRV_CIV && c->state != CS_PLAYER) driver_bails(g, c);
			}
		} else {
			c->burnT -= dt;
			if (c->burnT > 0 && g_rndf(g, 0, 1) < dt * 9.0f) fx_part(g, P_FIRE, c->x + g_rndf(g, -6, 6), c->y + g_rndf(g, -8, 8), 0, -8.0f, 0.6f, g_rndi(g, 3));
			if (g_rndf(g, 0, 1) < dt * 3.0f) fx_part(g, P_SMOKE, c->x + g_rndf(g, -5, 5), c->y + g_rndf(g, -6, 6), g_rndf(g, -5, 5), -14.0f, 1.3f, g_rndi(g, 3));
		}

		if (c->state == CS_PLAYER) continue;
		// distance-based cleanup
		float dx = c->x - g->p.x, dy = c->y - g->p.y, d2 = dx * dx + dy * dy;
		bool far_ = d2 > 560.0f * 560.0f;
		bool remove = false;
		if (c->wreckT < 0 && c->state == CS_TRAFFIC) {                // the driver gave up or bailed: leave a parked car behind
			driver_bails(g, c);
			c->state = CS_PARKED; c->wreckT = 0;
			if (d2 > 300.0f * 300.0f) remove = true;
		}
		if (c->state == CS_WRECK && c->wreckT > 40.0f && d2 > 260.0f * 260.0f) remove = true;
		if (far_ && c->state != CS_CHASE) remove = true;
		if (c->state == CS_CHASE && (g->stars == 0 && d2 > 380.0f * 380.0f)) remove = true;
		if (c->state == CS_CHASE && d2 > 700.0f * 700.0f) remove = true;
		if (remove) car_remove(g, i);
	}

	for (int i = 0; i < MAX_CARS; i++) {
		if (!g->cars[i].active) continue;
		car_vs_people(g, i);
		for (int j = i + 1; j < MAX_CARS; j++) {
			if (!g->cars[j].active) continue;
			float dx = g->cars[i].x - g->cars[j].x, dy = g->cars[i].y - g->cars[j].y;
			float reach = (car_defs[g->cars[i].model].len + car_defs[g->cars[j].model].len) * 0.5f + 4.0f;
			if (dx * dx + dy * dy < reach * reach) car_vs_car(g, i, j);
		}
	}
}

// ---------------------------------------------------------------- spawning
static bool lane_point(Game *g, int road, int half, int lane, float s, float *x, float *y, float *heading)
{
	const Road *R = &roads[road];
	float lc = road_lane_centre(R, half, lane);
	int dir = road_half_dir(R, half);
	if (R->axis == 0) { *x = s; *y = lc; *heading = dir > 0 ? PI_F * 0.5f : -PI_F * 0.5f; }
	else              { *x = lc; *y = s; *heading = dir > 0 ? PI_F : 0.0f; }
	(void)g;
	return true;
}

// random point on a random lane, at a given distance range from the player and out of sight
static bool random_lane_spot(Game *g, float dmin, float dmax, float *x, float *y, float *heading, int *road, int *half, int *lane)
{
	for (int tries = 0; tries < 24; tries++) {
		int r = g_rndi(g, nroads);
		const Road *R = &roads[r];
		int hf = g_rndi(g, 2), ln = g_rndi(g, road_lanes(R));
		float s0 = R->b0 * (float)TILE + 40.0f, s1 = (R->b1 + 1) * (float)TILE - 40.0f;
		float s = g_rndf(g, s0, s1);
		float px, py, h;
		lane_point(g, r, hf, ln, s, &px, &py, &h);
		float dx = px - g->p.x, dy = py - g->p.y, d = sqrtf(dx * dx + dy * dy);
		if (d < dmin || d > dmax) continue;
		// keep out of intersections so cars don't appear on top of cross traffic
		bool in_x = false;
		for (int q = 0; q < nroads; q++) {
			const Road *Q = &roads[q];
			if (Q->axis == R->axis) continue;
			float a0 = Q->a0 * (float)TILE - 20.0f, a1 = (Q->a1 + 1) * (float)TILE + 20.0f;
			if (s > a0 && s < a1 && (R->axis == 0 ? py : px) >= Q->b0 * (float)TILE - 1 && (R->axis == 0 ? py : px) < (Q->b1 + 1) * (float)TILE + 1) { in_x = true; break; }
		}
		if (in_x) continue;
		*x = px; *y = py; *heading = h; *road = r; *half = hf; *lane = ln;
		return true;
	}
	return false;
}

void spawn_traffic_car(Game *g)
{
	float x, y, h; int r, hf, ln;
	if (!random_lane_spot(g, 250.0f, 420.0f, &x, &y, &h, &r, &hf, &ln)) return;
	int model = M_COMPACT;
	int roll = g_rndi(g, 100);
	model = roll < 24 ? M_COMPACT : roll < 62 ? M_SEDAN : roll < 74 ? M_SPORTS : roll < 84 ? M_VAN : M_TAXI;
	int v = variant_of_model(model, g_rndi(g, 3));
	if (!car_area_free(g, x, y, h, model, -1)) return;
	int i = car_spawn(g, v, x, y, h, CS_TRAFFIC, DRV_CIV);
	if (i < 0) return;
	Car *c = &g->cars[i];
	c->road = r; c->half = hf; c->lane = ln;
	float sp = c->cruise * 0.9f;
	c->vx = sinf(h) * sp; c->vy = -cosf(h) * sp;
}

void spawn_cop_car(Game *g)
{
	float x, y, h; int r, hf, ln;
	if (!random_lane_spot(g, 230.0f, 380.0f, &x, &y, &h, &r, &hf, &ln)) return;
	bool swat = g->stars >= 4 && g_rndi(g, 3) == 0;
	int model = swat ? M_SWAT : M_POLICE;
	if (!car_area_free(g, x, y, h, model, -1)) return;
	int i = car_spawn(g, variant_of_model(model, 0), x, y, h, CS_CHASE, swat ? DRV_SWAT : DRV_COP);
	if (i < 0) return;
	g->cars[i].road = r; g->cars[i].half = hf; g->cars[i].lane = ln;
	g->cars[i].sirenT = 0.01f;
}

// bring parked cars to life near the player, and take them away again when far
static void update_parked(Game *g)
{
	for (int s = 0; s < NPARK && s < MAX_PARK; s++) {
		float x = park_spots[s].tx * (float)TILE + 8.0f, y = park_spots[s].ty * (float)TILE + 16.0f;
		int idx = g->park_car[s];
		float dx = x - g->p.x, dy = y - g->p.y, d2 = dx * dx + dy * dy;
		if (idx >= 0 && !g->cars[idx].active) { g->park_car[s] = -1; idx = -1; }
		if (idx < 0 && d2 < 250.0f * 250.0f && d2 > 120.0f * 120.0f) {
			float h = park_spots[s].dir * (2.0f * PI_F / DIRS);
			int model = g_rndi(g, 100) < 55 ? M_SEDAN : g_rndi(g, 100) < 50 ? M_COMPACT : g_rndi(g, 100) < 60 ? M_SPORTS : M_VAN;
			if (!car_area_free(g, x, y, h, model, -1)) continue;
			int i = car_spawn(g, variant_of_model(model, g_rndi(g, 3)), x, y, h, CS_PARKED, DRV_NONE);
			if (i >= 0) { g->cars[i].spot = s; g->park_car[s] = i; }
			return;                                            // at most one per frame
		}
	}
}

void cars_spawn_logic(Game *g, float dt);
void cars_spawn_logic(Game *g, float dt)
{
	g->spawnCarT -= dt;
	if (g->spawnCarT <= 0) {
		g->spawnCarT = 0.7f;
		int traffic = 0, chase = 0;
		for (int i = 0; i < MAX_CARS; i++) if (g->cars[i].active) { if (g->cars[i].state == CS_TRAFFIC) traffic++; if (g->cars[i].state == CS_CHASE) chase++; }
		if (traffic < 11) spawn_traffic_car(g);
		int want = g->stars >= 5 ? 5 : g->stars >= 4 ? 4 : g->stars >= 3 ? 3 : g->stars >= 2 ? 2 : 0;
		if (chase < want && g->spawnCopT <= 0) { spawn_cop_car(g); g->spawnCopT = 2.5f; }
	}
	update_parked(g);
}

// ---------------------------------------------------------------- the player and cars
int find_car_near(const Game *g, float x, float y, float radius)
{
	// distance from (x,y) to the car's body (its three collision circles), not to its centre
	int best = -1; float bd = 1e9f;
	for (int i = 0; i < MAX_CARS; i++) {
		const Car *c = &g->cars[i];
		if (!c->active || c->state == CS_WRECK || c->state == CS_PLAYER) continue;
		float cx[3], cy[3], r;
		car_circles(c, cx, cy, &r);
		for (int k = 0; k < 3; k++) {
			float dx = cx[k] - x, dy = cy[k] - y, d = sqrtf(dx * dx + dy * dy) - r;
			if (d < radius && d < bd) { bd = d; best = i; }
		}
	}
	return best;
}

void player_use(Game *g)
{
	Player *p = &g->p;
	if (p->status != PL_ALIVE) return;
	if (p->car >= 0) {                                             // get out
		Car *c = &g->cars[p->car];
		if (speed_of(c) > 130.0f) return;
		float rx = cosf(c->heading), ry = sinf(c->heading), off = car_defs[c->model].wid * 0.5f + 8.0f;
		float cand[4][2] = {{c->x - rx * off, c->y - ry * off}, {c->x + rx * off, c->y + ry * off},
		                    {c->x + sinf(c->heading) * (car_defs[c->model].len * 0.5f + 8), c->y - cosf(c->heading) * (car_defs[c->model].len * 0.5f + 8)},
		                    {c->x - sinf(c->heading) * (car_defs[c->model].len * 0.5f + 8), c->y + cosf(c->heading) * (car_defs[c->model].len * 0.5f + 8)}};
		for (int k = 0; k < 4; k++) {
			if (world_blocked(&g->world, cand[k][0], cand[k][1], PLAYER_HALF)) continue;
			p->x = cand[k][0]; p->y = cand[k][1]; p->vx = p->vy = 0;
			c->state = CS_PARKED; c->driver = DRV_NONE; c->throttle = 0; c->steer = 0; c->hand = 0;
			p->car = -1;
			return;
		}
		return;
	}
	int i = find_car_near(g, p->x, p->y, 9.0f);
	if (i < 0) return;
	Car *c = &g->cars[i];
	Driver drv = c->driver;
	if (drv == DRV_CIV || drv == DRV_COP || drv == DRV_SWAT) {       // carjack: the driver is thrown out
		float ox = cosf(c->heading) * 13.0f, oy = sinf(c->heading) * 13.0f;
		if (drv == DRV_CIV) {
			int q = ped_spawn(g, PK_CIV, g_rndi(g, 6), c->x - ox, c->y - oy);
			if (q >= 0) { g->peds[q].state = PS_FLEE; g->peds[q].stateT = 6.0f; g->peds[q].threatX = p->x; g->peds[q].threatY = p->y; }
			add_heat(g, 1.0f);
		} else {
			int q = ped_spawn(g, drv == DRV_SWAT ? PK_SWAT : PK_COP, drv == DRV_SWAT ? 7 : 6, c->x - ox, c->y - oy);
			(void)q;
			add_heat(g, 3.0f);
		}
		popup(g, "Carjacked!", 0xFF60E0FF);
	} else {
		for (int k = 0; k < MAX_PEDS; k++) {                          // a cop who sees you take a car is a crime
			const Ped *q = &g->peds[k];
			if (q->active && q->kind != PK_CIV && q->state != PS_DEAD && (q->x - p->x) * (q->x - p->x) + (q->y - p->y) * (q->y - p->y) < 200.0f * 200.0f) { add_heat(g, 1.0f); break; }
		}
		popup(g, "Stole a car", 0xFF80FF80);
	}
	if (c->spot >= 0 && c->spot < MAX_PARK && g->park_car[c->spot] == i) g->park_car[c->spot] = -1;
	c->spot = -1;
	c->state = CS_PLAYER; c->driver = DRV_PLAYER; c->wreckT = 0; c->np = 0;
	p->car = i;
	p->x = c->x; p->y = c->y;
}

void player_drive_input(Game *g, const Input *in);
void player_drive_input(Game *g, const Input *in)
{
	Player *p = &g->p;
	if (p->car < 0) return;
	Car *c = &g->cars[p->car];
	float s = in->mx;
	c->steer = fabsf(s) < 0.1f ? 0 : s;
	c->throttle = in->a ? 1.0f : 0.0f;
	c->brake = in->b ? 1.0f : 0.0f;
	c->hand = in->l ? 1.0f : 0.0f;
	p->x = c->x; p->y = c->y;
	p->heading = c->heading;
}
