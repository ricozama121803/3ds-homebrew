// Grand Theft Otto II - traffic and pedestrians: computer-driven cars on lanes with traffic lights, and people walking the pavements.
#include "game.h"
#include "otto_gait.h"

#define LANE      3.6f                       // lane centre, metres from the grid line (one lane each way, right-hand traffic)
#define STOP_DIST 13.5f                      // stop line, metres before the junction's centre line
#define WALK_SPEED 1.25f
#define CROSS_SPEED 1.8f
#define FLEE_SPEED 4.4f
#define PAVEMENT  11.9f                     // pavement centre line, metres inside each block edge (the kerb is at 11, the buildings at 12.8)

static unsigned trnd(Game *g) { return rnd_next(&g->trng); }
static float trndf(Game *g) { return rndf(&g->trng); }
static float approach(float v, float target, float up, float down, float dt)
{
	if (v < target) { v += up * dt; if (v > target) v = target; }
	else { v -= down * dt; if (v < target) v = target; }
	return v;
}

// ---- traffic lights -----------------------------------------------------------------------------------------------------------------
int signal_state(const Game *g, int jx, int jz, int axis)
{
	float t = fmodf(g->time + (float)((jx * 7 + jz * 13) % 22), 22.0f);
	if (axis == 0) return t < 8.0f ? 2 : t < 10.0f ? 1 : 0;
	return (t >= 11.5f && t < 19.5f) ? 2 : (t >= 19.5f && t < 21.5f) ? 1 : 0;
}

bool car_is_moving_traffic(const Car *c) { return c->ai.on && c->ai.v > 3.0f; }

// ---- cars ------------------------------------------------------------------------------------------------------------------------------
static void lane_pos(int axis, int dir, int line, float s, float *x, float *z, float *yaw)
{
	if (axis == 0) { *x = s; *z = line * PITCH + dir * LANE; *yaw = dir > 0 ? PI_F / 2 : -PI_F / 2; }
	else { *x = line * PITCH - dir * LANE; *z = s; *yaw = dir > 0 ? PI_F : 0.0f; }
}
static float nextcross(float s, int dir)
{
	float m = floorf(s / PITCH), c = m * PITCH;
	if (dir > 0) return (m + 1) * PITCH;
	return (s - c > 0.01f) ? c : c - PITCH;
}
static float stopspeed(float dist, float decel) { return dist <= 0 ? 0 : sqrtf(2.0f * decel * dist); }

static void turn_vectors(const Ai *a, int plan, float cc, float f2[2], float pc[2], float f[2])
{
	if (a->axis == 0) { f[0] = (float)a->dir; f[1] = 0; pc[0] = cc; pc[1] = a->line * PITCH; }
	else { f[0] = 0; f[1] = (float)a->dir; pc[0] = a->line * PITCH; pc[1] = cc; }
	float r[2] = { -f[1], f[0] };
	if (plan == 2) { f2[0] = r[0]; f2[1] = r[1]; } else { f2[0] = -r[0]; f2[1] = -r[1]; }
}

static bool turn_possible(const Ai *a, int plan, float cc)
{
	float f2[2], pc[2], f[2];
	turn_vectors(a, plan, cc, f2, pc, f);
	int nd = (int)(f2[0] + f2[1] > 0 ? 1 : -1);                         // new direction along the new axis
	return a->line + nd >= 1 && a->line + nd <= NB - 1;
}

static void choose_plan(Game *g, Car *c, float cc)
{
	Ai *a = &c->ai;
	int m = (int)floorf(cc / PITCH + 0.5f);
	bool straight = m + a->dir >= 1 && m + a->dir <= NB - 1, right = turn_possible(a, 2, cc), left = turn_possible(a, 3, cc);
	float wS = straight ? 60 : 0, wR = right ? 20 : 0, wL = left ? 20 : 0, r = trndf(g) * (wS + wR + wL);
	if (wS + wR + wL <= 0) { a->plan = straight ? 1 : right ? 2 : 3; }
	else a->plan = r < wS ? 1 : r < wS + wR ? 2 : 3;
	a->cj = cc;
	if (a->plan >= 2) {
		float f2[2], pc[2], f[2];
		turn_vectors(a, a->plan, cc, f2, pc, f);
		float r2[2] = { -f2[1], f2[0] };
		float q[2] = { pc[0] + (-f[1]) * LANE + r2[0] * LANE, pc[1] + f[0] * LANE + r2[1] * LANE };
		float d = a->plan == 2 ? 8.0f : 10.0f;
		a->q[0] = q[0]; a->q[1] = q[1];
		a->e[0] = q[0] - f[0] * d; a->e[1] = q[1] - f[1] * d;
		a->x[0] = q[0] + f2[0] * d; a->x[1] = q[1] + f2[1] * d;
		a->es = a->axis == 0 ? a->e[0] : a->e[1];
		a->naxis = f2[0] != 0 ? 0 : 1; a->ndir = (f2[0] + f2[1]) > 0 ? 1 : -1; a->nline = m;
		a->ns = a->naxis == 0 ? a->x[0] : a->x[1];
		float len = 0, px = a->e[0], pz = a->e[1];
		for (int i = 1; i <= 8; i++) {                                       // arc length by sampling
			float t = i / 8.0f, u = 1 - t, bx = u * u * a->e[0] + 2 * u * t * a->q[0] + t * t * a->x[0], bz = u * u * a->e[1] + 2 * u * t * a->q[1] + t * t * a->x[1];
			len += sqrtf((bx - px) * (bx - px) + (bz - pz) * (bz - pz)); px = bx; pz = bz;
		}
		a->len = len;
	}
}

// no oncoming car close enough to run into a left turn (cars waiting at a red light of the same phase do not count, cars that are about to go do)
static bool left_turn_clear(const Game *g, const Car *me, float cc, int sig)
{
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *b = &g->car[i];
		if (!b->on || b == me || !b->ai.on) continue;
		if (b->ai.mode == 0 && b->ai.axis == me->ai.axis && b->ai.line == me->ai.line && b->ai.dir == -me->ai.dir) {
			float db = b->ai.dir * (cc - b->ai.s);
			if (db > -4.0f && db < 34.0f && (b->ai.v > 0.5f || sig == 2)) return false;
		}
	}
	return true;
}

// is there room to leave the junction on the lane the manoeuvre ends in? (never block the box)
static bool exit_clear(const Game *g, const Car *me, float cc)
{
	const Ai *a = &me->ai;
	int axis = a->axis, dir = a->dir, line = a->line; float s;
	if (a->plan >= 2) { axis = a->naxis; dir = a->ndir; line = a->nline; s = a->ns + dir * 3.0f; }
	else s = cc + dir * 15.0f;
	float x, z, yaw; lane_pos(axis, dir, line, s, &x, &z, &yaw);
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *b = &g->car[i];
		if (!b->on || b == me) continue;
		float dx = b->x - x, dz = b->z - z;
		if (dx * dx + dz * dz < 5.0f * 5.0f && b->vx * b->vx + b->vz * b->vz < 1.0f * 1.0f) return false;
	}
	return true;
}

// one left-turning car at a time per junction: it takes the slot before it crosses the stop line and gives it back when the turn is done
static bool lock_free(const Game *g, short l)
{
	if (l == 0) return true;
	const Car *h = &g->car[l - 1];
	return !(h->on && h->ai.on && h->ai.plan == 3);
}

static void ai_step(Game *g, int idx, float dt)
{
	Car *c = &g->car[idx];
	Ai *a = &c->ai;
	float vt = a->cruise, vtObs = 1e9f, vtSig = 1e9f;
	float yawBefore = c->yaw;
	// obstacles ahead: other cars, pedestrians, Otto on foot
	{
		float fx = sinf(c->yaw), fz = -cosf(c->yaw), rx = -fz, rz = fx;
		float wide = a->mode == 1 ? 2.6f : 1.9f;
		for (int i = 0; i < MAX_CAR; i++) {
			const Car *b = &g->car[i];
			if (!b->on || i == idx) continue;
			float dx = b->x - c->x, dz = b->z - c->z, gg = dx * fx + dz * fz, l = dx * rx + dz * rz;
			if (gg < 0.5f || gg > 32.0f || fabsf(l) > wide) continue;
			if (a->mode == 1 && b->ai.on && b->ai.mode == 0 && b->ai.v < 0.5f) continue;     // a car in the middle of a turn has right of way over one waiting at the junction mouth
			if (a->ignore > 0 && (fabsf(l) > 1.0f || (a->mode == 1 && b->ai.on))) continue;      // deadlock breaker for crossing traffic; never rams the car in front
			float vb = fmaxf(0.0f, b->vx * fx + b->vz * fz), gap = gg - 4.5f;
			vtObs = fminf(vtObs, vb + stopspeed(gap - 2.5f, 3.5f));
		}
		for (int i = 0; i < MAX_PED + 1; i++) {
			float px, pz;
			if (i < MAX_PED) { const Ped *p = &g->ped[i]; if (!p->on || p->state == PED_DOWN) continue; px = p->x; pz = p->z; }
			else { if (g->p.car >= 0) continue; px = g->p.p[0]; pz = g->p.p[2]; }
			float dx = px - c->x, dz = pz - c->z, gg = dx * fx + dz * fz, l = dx * rx + dz * rz;
			if (gg < 0.5f || gg > 16.0f || fabsf(l) > 2.3f) continue;
			vtObs = fminf(vtObs, stopspeed(gg - 3.6f, 4.5f));
		}
	}
	if (a->ignore > 0) a->ignore -= dt;

	if (a->mode == 0) {
		float cc = nextcross(a->s, a->dir);
		float dist = a->dir * (cc - a->s);
		if (a->cj != cc && dist < 45.0f) choose_plan(g, c, cc);
		int jx = a->axis == 0 ? (int)floorf(cc / PITCH + 0.5f) : a->line, jz = a->axis == 0 ? a->line : (int)floorf(cc / PITCH + 0.5f);
		float stopS = cc - a->dir * STOP_DIST, sd = a->dir * (stopS - a->s);
		int sig = signal_state(g, jx, jz, a->axis);
		if (sig != 2 && sd > -2.0f) {
			bool canStop = sd > a->v * a->v / (2.0f * 6.0f);
			if (sig == 0 || canStop) vtSig = stopspeed(sd - 0.3f, 3.5f);
		}
		if (a->cj == cc && sd > -0.5f && a->hold < 8.0f && !exit_clear(g, c, cc)) {                         // do not enter a junction you cannot leave (for long)
			vtSig = fminf(vtSig, stopspeed(sd - 0.3f, 3.5f));
			if (a->v < 0.3f) a->hold += dt;
		} else if (a->v > 1.0f) a->hold = 0;
		if (a->cj == cc && a->plan >= 2) {
			float sdE = a->dir * (a->es - a->s);
			vt = fminf(vt, a->plan == 2 ? 6.5f : 7.0f);
			if (a->plan == 3) {
				short *lk = &g->jlock[jx][jz];
				bool mine = *lk == idx + 1;
				if (!mine && sd > -0.5f) {                                    // before the stop line: only the first car in line may take the slot, and only into a gap
					if (sd < 5.0f && vtObs > 3.0f && lock_free(g, *lk) && left_turn_clear(g, c, cc, sig)) { *lk = (short)(idx + 1); mine = true; }
					else vtSig = fminf(vtSig, stopspeed(sd - 0.3f, 3.5f));
				}
				if (!mine && sd <= -0.5f) { *lk = (short)(idx + 1); mine = true; }          // already over the line (spawned there): take it
				if (mine && sdE > -0.3f && sdE < 6.0f && !left_turn_clear(g, c, cc, sig)) vtSig = fminf(vtSig, stopspeed(sdE - 0.3f, 4.0f));
			}
			if (sdE <= 0.05f && (a->plan == 2 || left_turn_clear(g, c, cc, sig))) { a->mode = 1; a->t = 0; a->v = fminf(a->v, 6.5f); }
		}
	}
	vt = fminf(vt, fminf(vtObs, vtSig));
	if (vtObs < 0.3f && a->v < 0.3f) { a->stuck += dt; if (a->stuck > (a->mode == 1 ? 3.0f : 7.0f)) { a->ignore = 3.0f; a->stuck = 0; } }
	else a->stuck = fmaxf(0, a->stuck - dt);
	if (a->mode == 1) vt = fminf(vt, a->plan == 2 ? 6.0f : 6.5f);
	c->braking = vt < a->v - 0.5f;
	a->v = approach(a->v, vt, 3.2f, 8.0f, dt);

	if (a->mode == 0) {
		a->s += a->dir * a->v * dt;
		lane_pos(a->axis, a->dir, a->line, a->s, &c->x, &c->z, &c->yaw);
		c->steer += (0 - c->steer) * fminf(1.0f, dt * 8.0f);
	} else {
		a->t += a->v * dt / a->len;
		float t = fminf(a->t, 1.0f), u = 1 - t;
		c->x = u * u * a->e[0] + 2 * u * t * a->q[0] + t * t * a->x[0];
		c->z = u * u * a->e[1] + 2 * u * t * a->q[1] + t * t * a->x[1];
		float dx = 2 * u * (a->q[0] - a->e[0]) + 2 * t * (a->x[0] - a->q[0]), dz = 2 * u * (a->q[1] - a->e[1]) + 2 * t * (a->x[1] - a->q[1]);
		c->yaw = atan2f(dx, -dz);
		float rate = wrapang(c->yaw - yawBefore) / fmaxf(dt, 1e-4f);
		c->steer += (clampf(atanf(2.7f * rate / fmaxf(a->v, 2.0f)), -0.5f, 0.5f) - c->steer) * fminf(1.0f, dt * 8.0f);
		if (a->t >= 1.0f) {
			if (a->plan == 3) { int jx = a->axis == 0 ? a->nline : a->line, jz = a->axis == 0 ? a->line : a->nline; if (g->jlock[jx][jz] == idx + 1) g->jlock[jx][jz] = 0; }
			a->axis = a->naxis; a->dir = a->ndir; a->line = a->nline; a->s = a->ns; a->mode = 0; a->plan = 0; a->cj = -1.0f;
			lane_pos(a->axis, a->dir, a->line, a->s, &c->x, &c->z, &c->yaw);
		}
	}
	float fx = sinf(c->yaw), fz = -cosf(c->yaw);
	c->vx = fx * a->v; c->vz = fz * a->v;
	c->wheel += a->v * dt / 0.33f;
	c->lean += (0 - c->lean) * fminf(1.0f, dt * 6.0f); c->pitch += ((c->braking ? -0.03f : 0.0f) - c->pitch) * fminf(1.0f, dt * 6.0f);
}

// puts a car on a random lane between minFromPlayer and maxFromPlayer metres from Otto; kind < 0 picks a civilian kind; ai: it drives the lanes by itself
int traffic_spawn_car(Game *g, int kind, float minFromPlayer, float maxFromPlayer, bool ai)
{
	for (int tries = 0; tries < 60; tries++) {
		int axis = trnd(g) & 1, dir = (trnd(g) & 2) ? 1 : -1, line = 1 + (int)(trnd(g) % (NB - 1)), m = 1 + (int)(trnd(g) % (NB - 2));
		float s = (m + 0.3f + 0.4f * trndf(g)) * PITCH, x, z, yaw;
		lane_pos(axis, dir, line, s, &x, &z, &yaw);
		float dx = x - g->p.p[0], dz = z - g->p.p[2], d2 = dx * dx + dz * dz;
		if (d2 < minFromPlayer * minFromPlayer || d2 > maxFromPlayer * maxFromPlayer) continue;
		bool free = true;
		for (int i = 0; i < MAX_CAR && free; i++) if (g->car[i].on) { float ex = g->car[i].x - x, ez = g->car[i].z - z; if (ex * ex + ez * ez < 9.0f * 9.0f) free = false; }
		if (!free) continue;
		int k = (int)(trnd(g) % 20), kd = kind >= 0 ? kind : (k < 8 ? CAR_SEDAN : k < 12 ? CAR_SPORTS : k < 15 ? CAR_VAN : CAR_TAXI);
		int ci = game_add_car(g, kd, x, z, yaw);
		if (ci < 0) return -1;
		Ai *a = &g->car[ci].ai;
		memset(a, 0, sizeof *a);
		a->axis = axis; a->dir = dir; a->line = line; a->s = s; a->cruise = 9.0f + 4.0f * trndf(g); a->v = a->cruise * 0.6f; a->cj = -1.0f;
		a->on = ai;
		return ci;
	}
	return -1;
}
static bool spawn_ai_car(Game *g, float minFromPlayer) { return traffic_spawn_car(g, -1, minFromPlayer, 1e9f, true) >= 0; }

// ---- pedestrians ------------------------------------------------------------------------------------------------------------------------
void traffic_node_xy(int id, float *x, float *z)
{
	int k = id & 3, b = id >> 2, i = b % NB, j = b / NB;
	*x = (k == 0 || k == 3) ? i * PITCH + PAVEMENT : (i + 1) * PITCH - PAVEMENT;
	*z = (k == 0 || k == 1) ? j * PITCH + PAVEMENT : (j + 1) * PITCH - PAVEMENT;
}
// the two ways across a street from a corner: corner k of block (i, j) -> corner k2 of block (i2, j2)
static bool cross_node(int id, int which, int *out)
{
	static const int di[4] = { -1, 1, 1, -1 }, dj[4] = { -1, -1, 1, 1 };          // which 0: the block across the street to the east / west, 1: to the north / south
	static const int kh[4] = { 1, 0, 3, 2 }, kv[4] = { 3, 2, 1, 0 };
	int k = id & 3, b = id >> 2, i = b % NB, j = b / NB;
	int ni = which == 0 ? i + di[k] : i, nj = which == 0 ? j : j + dj[k], nk = which == 0 ? kh[k] : kv[k];
	if (ni < 0 || nj < 0 || ni >= NB || nj >= NB) return false;
	*out = ((nj * NB + ni) << 2) | nk;
	return true;
}

static void ped_outfit(Game *g, Ped *p)
{
	p->shirt = (unsigned char)(trnd(g) % 12); p->pants = (unsigned char)(trnd(g) % 6); p->skin = (unsigned char)(trnd(g) % 6);
	p->hair = (unsigned char)(trnd(g) % 7); p->shoe = (unsigned char)(trnd(g) % 3);
	p->scale = 0.93f + 0.14f * trndf(g);
}

static bool spawn_ped(Game *g, float minFromPlayer, float maxFromPlayer)
{
	for (int i = 0; i < MAX_PED; i++) if (!g->ped[i].on) {
		for (int tries = 0; tries < 70; tries++) {
			int id = (int)(trnd(g) % (NB * NB * 4));
			float x, z; traffic_node_xy(id, &x, &z);
			float dx = x - g->p.p[0], dz = z - g->p.p[2], d2 = dx * dx + dz * dz;
			if (d2 < minFromPlayer * minFromPlayer || d2 > maxFromPlayer * maxFromPlayer) continue;
			Ped *p = &g->ped[i];
			memset(p, 0, sizeof *p);
			p->on = true; p->hp = 60.0f; p->state = PED_WALK; p->node = p->prev = id; p->cw = (trnd(g) & 1) != 0;
			p->jx = (trndf(g) - 0.5f) * 0.7f; p->jz = (trndf(g) - 0.5f) * 0.7f;
			p->x = x + p->jx; p->z = z + p->jz; p->yaw = trndf(g) * 2 * PI_F; p->phase = trndf(g);
			ped_outfit(g, p);
			// walk to the next corner
			int nx = (id & ~3) | ((id + (p->cw ? 1 : 3)) & 3);
			p->node = nx;
			return true;
		}
		return false;
	}
	return false;
}

static bool crossing_safe(const Game *g, float x0, float z0, float x1, float z1)
{
	float mx = 0.5f * (x0 + x1), mz = 0.5f * (z0 + z1);
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *c = &g->car[i];
		if (!c->on) continue;
		float dx = c->x - mx, dz = c->z - mz;
		if (dx * dx + dz * dz < 20.0f * 20.0f && c->vx * c->vx + c->vz * c->vz > 2.0f * 2.0f) return false;
	}
	return true;
}

// the pavement graph: neighbours of a node, the nearest node to a point, and the next step of the shortest way between two nodes (breadth first)
static int node_neighbours(int cur, int out[4])
{
	int k = cur & 3, n = 0, to;
	out[n++] = (cur & ~3) | ((k + 1) & 3); out[n++] = (cur & ~3) | ((k + 3) & 3);
	for (int c = 0; c < 2; c++) if (cross_node(cur, c, &to)) out[n++] = to;
	return n;
}
int traffic_nearest_node(const Game *g, float x, float z)
{
	int bi = (int)floorf(x / PITCH), bj = (int)floorf(z / PITCH);
	bi = bi < 0 ? 0 : bi > NB - 1 ? NB - 1 : bi; bj = bj < 0 ? 0 : bj > NB - 1 ? NB - 1 : bj;
	int best = 0, bestClear = -1; float bd = 1e9f, bdc = 1e9f;
	for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
		int i = bi + di, j = bj + dj;
		if (i < 0 || j < 0 || i >= NB || j >= NB) continue;
		for (int k = 0; k < 4; k++) {
			int id = ((j * NB + i) << 2) | k; float nx, nz; traffic_node_xy(id, &nx, &nz);
			float d = (nx - x) * (nx - x) + (nz - z) * (nz - z);
			if (d < bd) { bd = d; best = id; }
			if (d < bdc && los_clear(g, x, 0.9f, z, nx, 0.9f, nz)) { bdc = d; bestClear = id; }
		}
	}
	return bestClear >= 0 ? bestClear : best;
}
int traffic_path_next(int from, int to)
{
	if (from == to) return to;
	static short par[NB * NB * 4], queue[NB * NB * 4];
	int n = NB * NB * 4;
	for (int i = 0; i < n; i++) par[i] = -1;
	int qh = 0, qt = 0;
	queue[qt++] = (short)to; par[to] = (short)to;
	while (qh < qt) {
		int c = queue[qh++], nb[4], cnt = node_neighbours(c, nb);
		for (int i = 0; i < cnt; i++) if (par[nb[i]] < 0) { par[nb[i]] = (short)c; if (nb[i] == from) return c; queue[qt++] = (short)nb[i]; }
	}
	return to;
}

static void ped_arrive(Game *g, Ped *p)
{
	int cur = p->node, k = cur & 3;
	int along1 = (cur & ~3) | ((k + (p->cw ? 1 : 3)) & 3), along2 = (cur & ~3) | ((k + (p->cw ? 3 : 1)) & 3);
	int cands[4], n = 0; float w[4], tot = 0;
	if (along1 != p->prev) { cands[n] = along1; w[n++] = 55; }
	if (along2 != p->prev) { cands[n] = along2; w[n++] = 8; }
	for (int c = 0; c < 2; c++) { int to; if (cross_node(cur, c, &to) && to != p->prev) { cands[n] = to; w[n++] = 14; } }
	if (n == 0) { cands[0] = p->prev; w[0] = 1; n = 1; }
	for (int i = 0; i < n; i++) tot += w[i];
	float r = trndf(g) * tot; int pick = 0;
	for (int i = 0; i < n; i++) { if (r < w[i]) { pick = i; break; } r -= w[i]; pick = i; }
	if (p->state == PED_FLEE) {                                            // running from a shot: take the corner that is farthest from it
		float bd = -1;
		for (int i = 0; i < n; i++) { float nx, nz; traffic_node_xy(cands[i], &nx, &nz); float d = (nx - g->noiseX) * (nx - g->noiseX) + (nz - g->noiseZ) * (nz - g->noiseZ); if (d > bd) { bd = d; pick = i; } }
	}
	int to = cands[pick];
	bool isCross = ((to >> 2) != (cur >> 2));
	if (isCross) {
		float x0, z0, x1, z1; traffic_node_xy(cur, &x0, &z0); traffic_node_xy(to, &x1, &z1);
		if (!crossing_safe(g, x0, z0, x1, z1)) { p->state = PED_IDLE; p->timer = 0.6f + trndf(g); return; }
	}
	if (!isCross && p->state == PED_WALK && trndf(g) < 0.12f) { p->state = PED_IDLE; p->timer = 1.5f + 3.5f * trndf(g); }
	if (along1 == to) { /* keeps the walking direction */ }
	else if (along2 == to) p->cw = !p->cw;
	p->prev = cur; p->node = to;
}

static void ped_step(Game *g, int idx, float dt)
{
	Ped *p = &g->ped[idx];
	if (p->hurtT > 0) p->hurtT -= dt;
	if (p->state == PED_DOWN) {                                              // lying on the ground: dead for good, or only knocked over
		p->timer -= dt;
		float k = expf(-3.0f * dt); p->vx *= k; p->vz *= k;
		p->x += p->vx * dt; p->z += p->vz * dt; p->speed = 0;
		{ float px = p->x, pz = p->z, n[2]; if (city_push_circle(&g->w, &px, &pz, 0.72f, n)) { p->x = px; p->z = pz; p->vx = p->vz = 0; } }      // a knocked-over body does not slide into a wall
		if (p->timer <= 0) { if (p->dead) p->on = false; else { p->state = PED_FLEE; p->timer = 3.0f; } }
		return;
	}
	// cars: startled by fast ones passing close, hurt by any that touch
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *c = &g->car[i];
		if (!c->on || car_flying(c)) continue;
		float dx = c->x - p->x, dz = c->z - p->z;
		if (dx * dx + dz * dz > 8.0f * 8.0f) continue;
		float sp = sqrtf(c->vx * c->vx + c->vz * c->vz);
		float cx[3], cz[3]; car_circles(c, cx, cz);
		for (int q = 0; q < 3; q++) {
			float ex = p->x - cx[q], ez = p->z - cz[q], d2 = ex * ex + ez * ez, r = car_radius(c->kind) + 0.3f;
			if (sp > 2.5f && d2 < r * r && !c->wreck) {
				p->state = PED_DOWN; p->timer = 3.2f; p->vx = c->vx * 0.6f; p->vz = c->vz * 0.6f; p->yaw = atan2f(c->vx, -c->vz);
				damage_ped(g, idx, clampf(sp * 7.0f, 15.0f, 150.0f), c->driven ? 1 : 0);
				return;
			}
		}
		if (sp > 7.0f && dx * dx + dz * dz < 5.5f * 5.5f && p->state != PED_FLEE && !p->cop) { p->state = PED_FLEE; p->timer = 2.5f; }
	}
	if (p->cop && g->stars > 0) return;                                      // officers after Otto are driven by police.c
	p->losT -= dt;
	if (p->losT <= 0 && p->state != PED_IDLE) {                              // once a second: can I walk to my corner in a straight line? if not (squeezed into an alley, say) go back to the pavement
		p->losT = 1.0f;
		float nx, nz; traffic_node_xy(p->node, &nx, &nz);
		if (!los_clear(g, p->x, 0.9f, p->z, nx + p->jx, 0.9f, nz + p->jz)) { p->x = nx + p->jx; p->z = nz + p->jz; }
	}
	if (p->state == PED_IDLE) {
		p->timer -= dt; p->speed = approach(p->speed, 0, 6.0f, 6.0f, dt);
		if (p->timer <= 0) p->state = PED_WALK;
	} else {
		if (p->state == PED_FLEE) { p->timer -= dt; if (p->timer <= 0) p->state = PED_WALK; }
		float tx, tz; traffic_node_xy(p->node, &tx, &tz); tx += p->jx; tz += p->jz;
		float dx = tx - p->x, dz = tz - p->z, d = sqrtf(dx * dx + dz * dz);
		bool crossing = (p->node >> 2) != (p->prev >> 2) || (p->node == p->prev);
		float want = p->state == PED_FLEE ? FLEE_SPEED : crossing ? CROSS_SPEED : WALK_SPEED;
		p->speed = approach(p->speed, want, 5.0f, 8.0f, dt);
		if (d < 0.5f) { ped_arrive(g, p); if (p->state == PED_IDLE) return; }
		else {
			float step = p->speed * dt; if (step > d) step = d;
			p->x += dx / d * step; p->z += dz / d * step;
			float want_yaw = atan2f(dx, -dz);
			p->yaw = wrapang(p->yaw + wrapang(want_yaw - p->yaw) * fminf(1.0f, dt * 9.0f));
		}
	}
	p->vx = sinf(p->yaw) * p->speed; p->vz = -cosf(p->yaw) * p->speed;
	if (p->speed > 0.05f) p->phase += dt * p->speed / gait_stride(p->speed);
}

// ---- public ---------------------------------------------------------------------------------------------------------------------------------
void traffic_init(Game *g)
{
	for (int i = 0; i < MAX_PED; i++) g->ped[i].on = false;
	for (int i = 0; i < 14; i++) spawn_ai_car(g, 30.0f);
	for (int i = 0; i < 22; i++) spawn_ped(g, 12.0f, 1e9f);
	for (int i = 0; i < 10; i++) spawn_ped(g, 22.0f, 100.0f);                // and a crowd close to where Otto starts
}

void traffic_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_CAR; i++) if (g->car[i].on && g->car[i].ai.on) ai_step(g, i, dt);
	for (int i = 0; i < MAX_PED; i++) if (g->ped[i].on) ped_step(g, i, dt);
	// keep the numbers up: new cars appear far from the player, new people near him. The longer he stays put the more people there are around.
	g->stillT = g->p.speed < 1.5f ? g->stillT + dt : 0;
	static float acc, pedAcc;
	acc += dt; pedAcc += dt;
	if (acc > 0.5f) {
		acc = 0;
		int cars = 0;
		for (int i = 0; i < MAX_CAR; i++) cars += g->car[i].on && g->car[i].ai.on;
		if (cars < 14) spawn_ai_car(g, 110.0f);
	}
	if (pedAcc > 0.35f) {
		pedAcc = 0;
		int peds = 0, near = 0;
		for (int i = 0; i < MAX_PED; i++) {
			Ped *p = &g->ped[i];
			if (!p->on || p->cop || p->dead) continue;
			float d = hypotf(p->x - g->p.p[0], p->z - g->p.p[2]);
			if (d > 175.0f && p->state != PED_DOWN) { p->on = false; continue; }               // walked off into the distance: make room near Otto
			peds++; near += d < 110.0f;
		}
		int want = 26 + (g->stillT > 3.0f ? (int)fminf(12.0f, (g->stillT - 3.0f) * 3.0f) : 0);     // standing still for a while: up to 12 more people drift in
		if (near < want) spawn_ped(g, 26.0f, 115.0f);
		else if (peds < 22) spawn_ped(g, 60.0f, 1e9f);
	}
}

void traffic_eject_driver(Game *g, int car)
{
	const Car *c = &g->car[car];
	for (int i = 0; i < MAX_PED; i++) if (!g->ped[i].on) {
		Ped *p = &g->ped[i];
		memset(p, 0, sizeof *p);
		float rx = cosf(c->yaw), rz = sinf(c->yaw);
		p->on = true; p->hp = 60.0f; p->x = c->x - rx * 2.0f; p->z = c->z - rz * 2.0f; p->yaw = c->yaw - PI_F / 2;
		ped_outfit(g, p);
		int best = 0; float bd = 1e9f;                                    // run to the nearest pavement corner
		for (int id = 0; id < NB * NB * 4; id++) { float x, z; traffic_node_xy(id, &x, &z); float d = (x - p->x) * (x - p->x) + (z - p->z) * (z - p->z); if (d < bd) { bd = d; best = id; } }
		p->node = p->prev = best; p->state = PED_FLEE; p->timer = 4.0f; p->cw = (trnd(g) & 1) != 0;
		return;
	}
}
