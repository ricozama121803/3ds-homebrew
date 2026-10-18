// Grand Theft Otto II - the police: wanted level from heat, officers on foot that run to Otto along the pavements and shoot or arrest,
// police cars and SWAT vans that chase along the streets, ram, and unload officers.
#include "game.h"
#include "otto_gait.h"

static const float STAR_HEAT[5] = { 10.0f, 45.0f, 100.0f, 180.0f, 300.0f };       // heat that gives 1 .. 5 stars
static const int FOOT_WANT[6] = { 0, 3, 5, 7, 9, 12 }, CAR_WANT[6] = { 0, 1, 2, 3, 4, 5 }, SWAT_WANT[6] = { 0, 0, 1, 2, 3, 4 };
#define PATROL_COPS 3

static float prnd(Game *g) { return rndf(&g->trng); }
static bool is_police(const Car *c) { return c->kind == CAR_POLICE || c->kind == CAR_SWAT; }

static void target_point(const Game *g, float *x, float *z, float *vx, float *vz, bool *onFoot)
{
	const Player *p = &g->p;
	if (p->car >= 0) { const Car *c = &g->car[p->car]; *x = c->x; *z = c->z; *vx = c->vx; *vz = c->vz; *onFoot = false; }
	else { *x = p->p[0]; *z = p->p[2]; *vx = p->v[0]; *vz = p->v[2]; *onFoot = true; }
}

// Otto is away from the streets (on the airport, out on the pier, in the air): the officers cannot follow the pavements to him and simply run or drive straight at him
static bool player_far(const Game *g, float tx, float tz)
{
	float nx, nz; traffic_node_xy(traffic_nearest_node(g, tx, tz), &nx, &nz);
	return hypotf(nx - tx, nz - tz) > 60.0f || (g->p.car >= 0 && g->car[g->p.car].y > 3.0f);
}

// ---- spawning ---------------------------------------------------------------------------------------------------------------------------------
static int free_ped_slot(Game *g)
{
	for (int i = 0; i < MAX_PED; i++) if (!g->ped[i].on) return i;
	int best = -1; float bd = 50.0f * 50.0f;                                  // none free: take a civilian who is far away
	for (int i = 0; i < MAX_PED; i++) {
		const Ped *p = &g->ped[i];
		if (p->cop) continue;
		float d = (p->x - g->p.p[0]) * (p->x - g->p.p[0]) + (p->z - g->p.p[2]) * (p->z - g->p.p[2]);
		if (d > bd) { bd = d; best = i; }
	}
	if (best < 0) for (int i = 0; i < MAX_PED; i++) {                           // everybody is close: take the furthest civilian anyway
		const Ped *p = &g->ped[i];
		if (p->cop) continue;
		float d = (p->x - g->p.p[0]) * (p->x - g->p.p[0]) + (p->z - g->p.p[2]) * (p->z - g->p.p[2]);
		if (d > bd * 0.1f) { bd = d; best = i; }
	}
	return best;
}

// no free car slot: take the car furthest from Otto that nobody is using (traffic, parked cars, bikes; never aircraft or his own car) so the police can always be sent
static void make_car_room(Game *g)
{
	int best = -1; float bd = 80.0f * 80.0f;
	for (int i = 0; i < MAX_CAR; i++) { if (!g->car[i].on) return; }
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *c = &g->car[i];
		if (!c->on || c->driven || c->bot || c->kind >= CAR_HELI || is_police(c)) continue;
		float d = (c->x - g->p.p[0]) * (c->x - g->p.p[0]) + (c->z - g->p.p[2]) * (c->z - g->p.p[2]);
		if (d > bd) { bd = d; best = i; }
	}
	if (best >= 0) g->car[best].on = false;
}

static int spawn_cop(Game *g, int kind, float x, float z)
{
	int i = free_ped_slot(g);
	if (i < 0) return -1;
	Ped *p = &g->ped[i];
	memset(p, 0, sizeof *p);
	p->on = true; p->cop = (unsigned char)kind; p->hp = kind == 2 ? 230.0f : 130.0f; p->state = PED_WALK; p->scale = kind == 2 ? 1.04f : 1.0f;
	p->x = x; p->z = z; p->yaw = prnd(g) * 2 * PI_F; p->phase = prnd(g);
	p->node = p->prev = traffic_nearest_node(g, x, z); p->cw = (rnd_next(&g->trng) & 1) != 0;
	p->shootCd = 0.6f + prnd(g);
	p->replanT = 0;
	return i;
}

static bool spawn_foot_cop(Game *g, int kind)
{
	for (int tries = 0; tries < 30; tries++) {
		int id = (int)(rnd_next(&g->trng) % (NB * NB * 4));
		float x, z; traffic_node_xy(id, &x, &z);
		float dx = x - g->p.p[0], dz = z - g->p.p[2], d2 = dx * dx + dz * dz;
		if (d2 < 40.0f * 40.0f || d2 > 115.0f * 115.0f) continue;
		bool hidden = !los_clear(g, g->p.p[0], 1.5f, g->p.p[2], x, 1.5f, z);
		if (!hidden && d2 < 70.0f * 70.0f && tries < 24) continue;
		return spawn_cop(g, kind, x, z) >= 0;
	}
	{                                                                         // nowhere suitable (Otto is far from the streets): send one in from the nearest pavement to him
		float tx, tz, vx, vz; bool foot; target_point(g, &tx, &tz, &vx, &vz, &foot);
		float x, z; traffic_node_xy(traffic_nearest_node(g, tx, tz), &x, &z);
		float a = prnd(g) * 2 * PI_F;
		return spawn_cop(g, kind, x + cosf(a) * 6.0f, z + sinf(a) * 6.0f) >= 0;
	}
}

// ---- foot officers --------------------------------------------------------------------------------------------------------------------------------
static void cop_step(Game *g, int idx, float dt)
{
	Ped *p = &g->ped[idx];
	Player *pl = &g->p;
	float tx, tz, tvx, tvz; bool onFoot;
	target_point(g, &tx, &tz, &tvx, &tvz, &onFoot);
	float dx = tx - p->x, dz = tz - p->z, d = sqrtf(dx * dx + dz * dz);
	bool swat = p->cop == 2;
	p->replanT -= dt; p->losT -= dt; p->flashT -= dt;
	if (p->replanT <= 0) {
		p->replanT = 0.25f + 0.1f * prnd(g);
		bool sees = d < 85.0f && pl->ps == PS_ALIVE && los_clear(g, p->x, 1.5f, p->z, tx, 1.3f, tz);
		if (sees) p->losT = 0.7f;
		int from = traffic_nearest_node(g, p->x, p->z), to = traffic_nearest_node(g, tx, tz);
		p->wpNode = traffic_path_next(from, to);
		p->timer = from == to ? 1.0f : 0.0f;                          // (timer is free for officers) at the pavement node nearest Otto: nothing more to follow
		p->prev = from;
	}
	bool sees = p->losT > 0;
	float standoff = g->stars >= 2 ? (swat ? 7.0f : 10.0f) : (g->stars >= 1 ? 6.0f : 0.8f);
	bool hostile = g->stars >= 1;                                           // officers shoot from the first star
	float goalx, goalz;
	bool farAway = player_far(g, tx, tz);
	bool direct = (sees && d < 30.0f) || (farAway && (d < 160.0f || p->timer > 0.5f));
	if (direct) { goalx = tx; goalz = tz; }
	else { traffic_node_xy(p->wpNode, &goalx, &goalz); }
	float gx = goalx - p->x, gz = goalz - p->z, gd = sqrtf(gx * gx + gz * gz);
	float want = (direct && d <= standoff) ? 0.0f : (swat ? 4.8f : 5.4f);
	if (!direct && gd < 0.7f) want = 0.5f;
	p->speed += (want - p->speed) * fminf(1.0f, dt * 7.0f);
	if (gd > 0.05f && p->speed > 0.1f) {
		float sx = gx / gd * p->speed * dt, sz = gz / gd * p->speed * dt;
		p->x += sx; p->z += sz;
		float n[2]; float px = p->x, pz = p->z;                          // officers are wide enough (0.72 m) not to squeeze into the 1.4 m alleys between buildings
		if (city_push_circle(&g->w, &px, &pz, 0.72f, n)) { p->x = px; p->z = pz; }
		p->vx = sx / fmaxf(dt, 1e-4f); p->vz = sz / fmaxf(dt, 1e-4f);
	} else { p->vx = p->vz = 0; }
	float moveYaw = atan2f(gx, -gz), faceYaw = direct && hostile ? atan2f(dx, -dz) : moveYaw;
	if (p->speed < 0.4f && direct) faceYaw = atan2f(dx, -dz);
	p->yaw = wrapang(p->yaw + wrapang(faceYaw - p->yaw) * fminf(1.0f, dt * 10.0f));
	if (p->speed > 0.05f) p->phase += dt * p->speed / gait_stride(p->speed);
	// shooting
	bool aiming = hostile && direct && d < (swat ? 30.0f : 42.0f) && pl->ps == PS_ALIVE;
	p->aimT += ((aiming ? 1.0f : 0.0f) - p->aimT) * fminf(1.0f, dt * 8.0f);
	p->shootCd -= dt;
	if (aiming && p->aimT > 0.7f && p->shootCd <= 0 && fabsf(wrapang(atan2f(dx, -dz) - p->yaw)) < 0.25f) {
		float m[3] = { p->x + sinf(p->yaw) * 0.85f + cosf(p->yaw) * 0.2f, 1.3f, p->z - cosf(p->yaw) * 0.85f + sinf(p->yaw) * 0.2f };
		float aimY = onFoot ? 1.1f : pl->car >= 0 ? g->car[pl->car].y + 1.4f : 1.0f;
		float pitch = atan2f(aimY - 1.3f, fmaxf(d, 1.0f));
		const WeaponDef *w = swat ? &WPN[W_SHOTGUN] : &WPN[W_PISTOL];
		fire_bullet(g, 1, m, atan2f(tx + tvx * 0.15f - m[0], -(tz + tvz * 0.15f - m[2])), pitch, w, swat ? 0.7f : 0.45f);
		p->shootCd = swat ? 1.0f + prnd(g) * 0.4f : 0.55f + prnd(g) * 0.55f; p->flashT = 0.08f;
		spawn_particles(g, PT_SPARK, m[0], m[1], m[2], 2, 2.0f, 0.06f, 0.15f);
	}
	// holding Otto
	bool close = d < (onFoot ? 1.7f : 3.4f);
	if (close && pl->ps == PS_ALIVE && g->stars >= 1) {
		float sp = onFoot ? hypotf(pl->v[0], pl->v[2]) : hypotf(tvx, tvz);
		if (sp < (onFoot ? 4.2f : 2.0f)) pl->arrestT += dt;
	}
}

// ---- police cars ------------------------------------------------------------------------------------------------------------------------------------
static void next_cell(int cx, int cz, int tx, int tz, int *ox, int *oz)
{
	if (cx == tx && cz == tz) { *ox = cx; *oz = cz; return; }
	static signed char px[NB + 1][NB + 1];
	for (int j = 0; j <= NB; j++) for (int i = 0; i <= NB; i++) px[j][i] = -1;
	int qx[64], qz[64], qh = 0, qt = 0;
	qx[qt] = tx; qz[qt++] = tz; px[tz][tx] = (signed char)tx;
	static const int dxs[4] = { 1, -1, 0, 0 }, dzs[4] = { 0, 0, 1, -1 };
	while (qh < qt) {
		int x = qx[qh], z = qz[qh++];
		for (int k = 0; k < 4; k++) {
			int nx = x + dxs[k], nz = z + dzs[k];
			if (nx < 1 || nz < 1 || nx > NB - 1 || nz > NB - 1 || px[nz][nx] >= 0) continue;
			px[nz][nx] = (signed char)x;
			if (nx == cx && nz == cz) { *ox = x; *oz = z; return; }
			qx[qt] = nx; qz[qt++] = nz;
		}
	}
	*ox = tx; *oz = tz;
}

static void unload_officers(Game *g, Car *c)
{
	c->unloaded = true;
	int n = c->kind == CAR_SWAT ? 4 : 2;
	float rx = cosf(c->yaw), rz = sinf(c->yaw);
	for (int k = 0; k < n; k++) {
		float side = (k & 1) ? 2.3f : -2.3f, back = (k >> 1) * -1.2f - 0.4f;
		float x = c->x + rx * side + sinf(c->yaw) * back, z = c->z + rz * side - cosf(c->yaw) * back;
		float tx = x, tz = z, nn[2];
		city_push_circle(&g->w, &tx, &tz, 0.4f, nn);
		spawn_cop(g, c->kind == CAR_SWAT ? 2 : 1, tx, tz);
	}
}

static void police_car_step(Game *g, int idx, float dt)
{
	Car *c = &g->car[idx];
	float tx, tz, tvx, tvz; bool onFoot;
	target_point(g, &tx, &tz, &tvx, &tvz, &onFoot);
	float dx = tx - c->x, dz = tz - c->z, d = sqrtf(dx * dx + dz * dz);
	float spd = hypotf(c->vx, c->vz), fs = c->vx * sinf(c->yaw) - c->vz * cosf(c->yaw);
	c->botReplan -= dt;
	bool direct = (d < 55.0f && los_clear(g, c->x, 1.0f, c->z, tx, 1.0f, tz)) || (player_far(g, tx, tz) && (d < 170.0f || c->botEnd));
	if (c->botReplan <= 0 || hypotf(c->botTx - c->x, c->botTz - c->z) < 14.0f) {
		c->botReplan = 0.4f;
		int ccx = (int)floorf(c->x / PITCH + 0.5f), ccz = (int)floorf(c->z / PITCH + 0.5f), pcx = (int)floorf(tx / PITCH + 0.5f), pcz = (int)floorf(tz / PITCH + 0.5f);
		ccx = (int)clampf((float)ccx, 1, NB - 1); ccz = (int)clampf((float)ccz, 1, NB - 1); pcx = (int)clampf((float)pcx, 1, NB - 1); pcz = (int)clampf((float)pcz, 1, NB - 1);
		int ox, oz; next_cell(ccx, ccz, pcx, pcz, &ox, &oz);
		c->botEnd = ccx == pcx && ccz == pcz;                                 // at the junction nearest Otto
		c->botTx = ox * PITCH; c->botTz = oz * PITCH;
		// a junction we have already passed would make the car turn round: look one more cell ahead along the way instead
		float fx = sinf(c->yaw), fz = -cosf(c->yaw);
		if ((c->botTx - c->x) * fx + (c->botTz - c->z) * fz < -3.0f && spd > 6.0f) { c->botTx = c->x + fx * 40.0f; c->botTz = c->z + fz * 40.0f; }
	}
	float gx, gz;
	if (direct || (d < 22.0f)) { gx = tx + tvx * 0.25f; gz = tz + tvz * 0.25f; } else { gx = c->botTx; gz = c->botTz; }
	float want = atan2f(gx - c->x, -(gz - c->z)), e = wrapang(want - c->yaw);
	float gd = hypotf(gx - c->x, gz - c->z);
	float vt = c->kind == CAR_SWAT ? 27.0f : 34.0f;
	vt *= clampf(1.25f - fabsf(e) / 1.0f, 0.22f, 1.0f);
	if (gd < 25.0f && !direct) vt = fminf(vt, 13.0f);
	// stand next to Otto when he is on foot, ram him when he is in a car
	bool park = onFoot && d < 14.0f;
	if (park) vt = 0;
	if (park && !c->unloaded && spd < 9.0f) unload_officers(g, c);
	if (c->unloaded && onFoot) vt = 0;
	c->botLx = clampf(e * 1.7f, -1.0f, 1.0f);
	c->botGas = fs < vt; c->botBrake = fs > vt + 4.0f || (vt == 0 && spd > 1.0f); c->botHand = false;
	// stuck against something: back up for a moment
	if (c->botStuck > 0) { c->botStuck -= dt; c->botGas = false; c->botBrake = true; c->botLx = -c->botLx; }
	else if (!park && spd < 1.2f && vt > 3.0f) { c->botReplan = fminf(c->botReplan, 0.2f); c->ai.stuck += dt; if (c->ai.stuck > 1.3f) { c->botStuck = 1.1f; c->ai.stuck = 0; } }
	else c->ai.stuck = 0;
	// wedged somewhere for good (and not at Otto's side): take the car off the street, another one will be sent
	if (spd < 1.0f && d > 30.0f && !c->unloaded) { c->botStillT += dt; if (c->botStillT > 9.0f) { c->on = false; c->bot = false; } }
	else if (spd > 2.5f) c->botStillT = 0;
}

// ---- the wanted level ----------------------------------------------------------------------------------------------------------------------------------
void police_reset(Game *g)
{
	for (int i = 0; i < MAX_PED; i++) {
		Ped *p = &g->ped[i];
		if (!p->on || !p->cop) continue;
		p->state = PED_WALK; p->aimT = 0; p->node = p->prev = traffic_nearest_node(g, p->x, p->z); p->speed = 0;
	}
	for (int i = 0; i < MAX_CAR; i++) { Car *c = &g->car[i]; if (c->on && c->bot) { c->bot = false; c->botGas = c->botBrake = false; } }
	g->p.arrestT = 0;
}

void police_init(Game *g)
{
	g->heat = 0; g->stars = 0; g->unseenT = 0;
}

void police_update(Game *g, float dt)
{
	Player *pl = &g->p;
	int old = g->stars;
	// stars from heat
	int s = 0;
	for (int k = 0; k < 5; k++) if (g->heat >= STAR_HEAT[k]) s = k + 1;
	if (s > g->stars) { g->stars = s; g->starFlash = 2.0f; }
	if (g->starFlash > 0) g->starFlash -= dt;
	if (g->stars == 0) {
		if (old > 0) police_reset(g);
		// patrols only: no more than a few officers walk the beat, and abandoned police cars go away
		for (int i = 0; i < MAX_CAR; i++) {
			Car *c = &g->car[i];
			if (!c->on || c->owner != 1 || c->bot || c->driven || c->ai.on) continue;
			float d = hypotf(c->x - pl->p[0], c->z - pl->p[2]);
			if (d > 70.0f) c->on = false;
		}
		if (g->heat > 0 && g->heat < STAR_HEAT[0]) g->heat = fmaxf(0, g->heat - 3.0f * dt);
		return;
	}
	// does anyone see Otto?
	bool seen = false;
	for (int i = 0; i < MAX_PED && !seen; i++) { const Ped *p = &g->ped[i]; if (p->on && p->cop && !p->dead && (p->losT > 0 || hypotf(p->x - pl->p[0], p->z - pl->p[2]) < 55.0f)) seen = true; }
	for (int i = 0; i < MAX_CAR && !seen; i++) { const Car *c = &g->car[i]; if (c->on && c->bot && !c->wreck && hypotf(c->x - pl->p[0], c->z - pl->p[2]) < 45.0f) seen = true; }
	if (seen) g->unseenT = 0; else g->unseenT += dt;
	g->heat -= (g->unseenT > 14.0f ? 5.0f : 0.15f) * dt;                    // the wanted level fades once nobody has had eyes on Otto for a while
	if (g->heat < 0) g->heat = 0;
	int ns = 0;
	for (int k = 0; k < 5; k++) if (g->heat >= STAR_HEAT[k]) ns = k + 1;
	if (ns < g->stars) { g->stars = ns; if (ns == 0) { police_reset(g); toast(g, "Wanted level lost"); return; } }

	// bring the force up to strength, a unit at a time
	static float spawnT;
	spawnT -= dt;
	int foot = 0, cars = 0, swat = 0;
	for (int i = 0; i < MAX_PED; i++) foot += g->ped[i].on && g->ped[i].cop && !g->ped[i].dead;
	for (int i = 0; i < MAX_CAR; i++) {
		Car *c = &g->car[i];
		if (!c->on || c->wreck || !is_police(c)) continue;
		if (c->bot) { cars += c->kind == CAR_POLICE; swat += c->kind == CAR_SWAT; }
		else if (c->ai.on && g->stars >= 2 && hypotf(c->x - pl->p[0], c->z - pl->p[2]) < 260.0f) {      // a patrol car that is already out there joins in
			c->ai.on = false; c->bot = true; c->owner = 1; c->botTx = c->x; c->botTz = c->z; c->botReplan = 0; c->unloaded = false;
			cars += c->kind == CAR_POLICE; swat += c->kind == CAR_SWAT;
		}
	}
	if (spawnT <= 0) {
		spawnT = 0.8f;
		bool did = false;
		if (cars < CAR_WANT[g->stars]) {
			make_car_room(g);
			int ci = traffic_spawn_car(g, CAR_POLICE, 80.0f, 200.0f, false);
			if (ci < 0) ci = traffic_spawn_car(g, CAR_POLICE, 150.0f, 650.0f, false);
			if (ci >= 0) { Car *c = &g->car[ci]; c->bot = true; c->owner = 1; c->botTx = c->x; c->botTz = c->z; c->botReplan = 0; did = true; }
		} else if (swat < SWAT_WANT[g->stars]) {
			make_car_room(g);
			int ci = traffic_spawn_car(g, CAR_SWAT, 90.0f, 220.0f, false);
			if (ci < 0) ci = traffic_spawn_car(g, CAR_SWAT, 150.0f, 650.0f, false);
			if (ci >= 0) { Car *c = &g->car[ci]; c->bot = true; c->owner = 1; c->botTx = c->x; c->botTz = c->z; c->botReplan = 0; did = true; }
		} else did = false;
		if (foot < FOOT_WANT[g->stars] && (!did || (foot < 2))) spawn_foot_cop(g, (g->stars >= 4 && (rnd_next(&g->trng) & 1)) ? 2 : 1);      // a failed car spawn must not starve the officers on foot
	}
	for (int i = 0; i < MAX_PED; i++) if (g->ped[i].on && g->ped[i].cop && !g->ped[i].dead && g->ped[i].state != PED_DOWN) cop_step(g, i, dt);
	for (int i = 0; i < MAX_CAR; i++) if (g->car[i].on && g->car[i].bot && !g->car[i].wreck) police_car_step(g, i, dt);

	// caught?
	if (pl->ps == PS_ALIVE) {
		bool holding = false;
		for (int i = 0; i < MAX_PED && !holding; i++) { const Ped *p = &g->ped[i]; if (p->on && p->cop && !p->dead && p->state != PED_DOWN && hypotf(p->x - pl->p[0], p->z - pl->p[2]) < (pl->car >= 0 ? 3.4f : 1.7f)) holding = true; }
		if (!holding) pl->arrestT = fmaxf(0, pl->arrestT - 1.5f * dt);
		float lim = pl->car >= 0 ? 2.2f : 1.4f;
		if (pl->arrestT > lim) {
			pl->ps = PS_BUSTED; pl->psT = 0; g->timeScale = 0.5f; g->hudT = 3.0f; strcpy(g->hudMsg, "BUSTED");
			if (pl->car >= 0) { g->car[pl->car].driven = false; pl->car = -1; }
		}
	} else pl->arrestT = 0;
}
