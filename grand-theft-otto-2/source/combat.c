// Grand Theft Otto II - combat: weapons and auto-aim, bullets, blood, damage, cash and pickups, burning cars and explosions, WASTED and BUSTED.
#include "game.h"
#include <stdio.h>

const WeaponDef WPN[W_COUNT] = {
	//  name       damage pellets rate spread range speed
	{ "Fists",     14.0f, 1, 2.6f, 0.00f, 1.8f,   0.0f, 0 },
	{ "Pistol",    34.0f, 1, 3.2f, 0.020f, 75.0f, 100.0f, 0 },
	{ "SMG",       17.0f, 1, 9.0f, 0.050f, 60.0f, 105.0f, 0 },
	{ "Shotgun",   15.0f, 6, 1.1f, 0.100f, 30.0f, 90.0f, 0 },
};

static float crand(Game *g) { return rndf(&g->trng); }
static float srand_(Game *g) { return crand(g) * 2.0f - 1.0f; }

void toast(Game *g, const char *text) { strncpy(g->toast, text, sizeof g->toast - 1); g->toast[sizeof g->toast - 1] = 0; g->toastT = 2.2f; }

void add_heat(Game *g, float a) { g->heat += a; if (g->heat > 500.0f) g->heat = 500.0f; }

bool los_clear(const Game *g, float ax, float ay, float az, float bx, float by, float bz)
{
	float d[3] = { bx - ax, by - ay, bz - az }, o[3] = { ax, ay, az }, n[3];
	float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len < 0.01f) return true;
	for (int i = 0; i < 3; i++) d[i] /= len;
	float t = city_ray(&g->w, o, d, len, n);
	return t < 0 || t >= len - 0.2f;
}

// ---- particles, decals, cash ------------------------------------------------------------------------------------------------------------------
void spawn_particles(Game *g, int kind, float x, float y, float z, int n, float speed, float size, float life)
{
	for (int k = 0; k < n; k++) {
		Particle *p = NULL;
		for (int i = 0; i < MAX_PART; i++) if (!g->part[i].on) { p = &g->part[i]; break; }
		if (!p) return;
		p->on = true; p->kind = kind; p->p[0] = x + srand_(g) * 0.1f; p->p[1] = y; p->p[2] = z + srand_(g) * 0.1f;
		p->v[0] = srand_(g) * speed; p->v[1] = (kind == PT_BLOOD || kind == PT_SPARK ? 0.4f + crand(g) : 0.8f + crand(g) * 0.6f) * speed * (kind == PT_BLOOD ? 0.8f : 1.0f); p->v[2] = srand_(g) * speed;
		p->maxLife = p->life = life * (0.7f + 0.6f * crand(g)); p->size = size * (0.7f + 0.6f * crand(g));
	}
}
static void add_decal(Game *g, float x, float z, float size)
{
	Decal *best = &g->decal[0];
	for (int i = 0; i < MAX_DECAL; i++) { if (!g->decal[i].on) { best = &g->decal[i]; break; } if (g->decal[i].age > best->age) best = &g->decal[i]; }
	best->on = true; best->x = x; best->z = z; best->age = 0; best->size = size;
}
static void add_pickup(Game *g, int type, int amount, float x, float z, bool fixed)
{
	for (int i = 0; i < MAX_PICKUP; i++) if (!g->pick[i].on && (fixed || !g->pick[i].fixed)) {
		Pickup *p = &g->pick[i];
		p->on = true; p->type = type; p->amount = amount; p->x = x; p->z = z; p->t = 0; p->respawn = fixed ? 0 : 60.0f; p->fixed = fixed;
		return;
	}
}
void drop_cash(Game *g, float x, float z, int amount) { add_pickup(g, PK_CASH, amount, x, z, false); }

// ---- damage ---------------------------------------------------------------------------------------------------------------------------------------
static void wasted(Game *g)
{
	Player *p = &g->p;
	p->ps = PS_WASTED; p->psT = 0; g->timeScale = 0.35f; g->hudT = 4.0f; strcpy(g->hudMsg, "WASTED");
}
void damage_player(Game *g, float amount)
{
	Player *p = &g->p;
	if (p->ps != PS_ALIVE || amount <= 0) return;
	p->hurtT = 0.45f;
	amount *= 0.75f;                                                       // Otto is tougher than a pedestrian
	if (p->armor > 0) { float take = fminf(p->armor, amount); p->armor -= take; amount -= take; }
	if (amount > 0) {
		p->hp -= amount;
		spawn_particles(g, PT_BLOOD, p->p[0], p->p[1] + 1.2f, p->p[2], 4, 2.2f, 0.14f, 0.7f);
	}
	if (p->hp <= 0) { p->hp = 0; wasted(g); }
}

static void blood_burst(Game *g, const Ped *p, int n) { spawn_particles(g, PT_BLOOD, p->x, 1.25f, p->z, n, 2.6f, 0.15f, 0.8f); }

void damage_ped(Game *g, int idx, float amount, int byPlayer)
{
	Ped *p = &g->ped[idx];
	if (!p->on || p->dead) return;
	p->hp -= amount; p->hurtT = 0.3f;
	blood_burst(g, p, 4);
	if (byPlayer) { add_heat(g, p->cop ? 14.0f : 6.0f); g->hitT = 0.22f; }
	if (p->hp <= 0) {
		if (byPlayer) g->killT = 0.9f;                                         // the crosshair confirms the kill
		p->dead = true; p->state = PED_DOWN; p->timer = 14.0f; p->speed = 0;
		blood_burst(g, p, 10);
		add_decal(g, p->x, p->z, 1.0f + 0.8f * crand(g));
		int amt = p->cop ? 30 + (int)(crand(g) * 60) : 5 + (int)(crand(g) * 55);
		drop_cash(g, p->x + srand_(g) * 0.6f, p->z + srand_(g) * 0.6f, amt);
		if (byPlayer) add_heat(g, p->cop ? 85.0f : 30.0f);
	} else if (!p->cop && p->state != PED_DOWN) {
		p->state = PED_FLEE; p->timer = 5.0f;
		if (byPlayer) { g->noiseX = g->p.p[0]; g->noiseZ = g->p.p[2]; }
	}
}

static void explode_car(Game *g, int idx, int byPlayer)
{
	Car *c = &g->car[idx];
	if (c->wreck) return;
	c->wreck = true; c->wreckT = 0; c->hp = 0; c->bot = false; c->ai.on = false; c->driven = false; c->burn = 0;
	spawn_blast(g, c->x, c->y + 0.8f, c->z, byPlayer);
	if (g->p.car == idx) {                                                   // Otto was driving it
		g->p.car = -1;
		g->p.p[0] = c->x + cosf(c->yaw) * 2.6f; g->p.p[1] = 0; g->p.p[2] = c->z + sinf(c->yaw) * 2.6f; g->p.v[0] = g->p.v[2] = 0;
		damage_player(g, 85.0f + c->y);                                      // the higher the helicopter was, the harder the fall
		byPlayer = 1;
	}
}
void damage_car(Game *g, int idx, float amount, int byPlayer)
{
	Car *c = &g->car[idx];
	if (!c->on || c->wreck) return;
	c->hp -= amount;
	if (byPlayer && (c->kind == CAR_POLICE || c->kind == CAR_SWAT)) add_heat(g, 4.0f);
	if (c->hp <= 0) explode_car(g, idx, byPlayer);
}

void spawn_blast(Game *g, float x, float y, float z, int byPlayer)
{
	for (int i = 0; i < MAX_BLAST; i++) if (!g->blast[i].on) { g->blast[i] = (Blast){ true, x, y, z, 0 }; break; }
	spawn_particles(g, PT_FIRE, x, y, z, 14, 5.0f, 0.9f, 0.9f);
	spawn_particles(g, PT_SMOKE, x, y + 0.5f, z, 10, 3.0f, 1.3f, 2.4f);
	g->noiseX = x; g->noiseZ = z; g->noiseT = 0;
	// everything close gets hurt; the player's wanted level goes up if the player started it
	const float R = 9.5f;
	for (int i = 0; i < MAX_PED; i++) {
		Ped *p = &g->ped[i];
		if (!p->on || p->dead) continue;
		float d = hypotf(p->x - x, p->z - z);
		if (d < R) { damage_ped(g, i, 150.0f * (1 - d / R), byPlayer); p->vx += (p->x - x) / (d + 0.5f) * 6.0f; p->vz += (p->z - z) / (d + 0.5f) * 6.0f; }
		else if (d < 40.0f && !p->cop && p->state != PED_DOWN) { p->state = PED_FLEE; p->timer = 4.0f; }
	}
	for (int i = 0; i < MAX_CAR; i++) {
		Car *c = &g->car[i];
		if (!c->on || c->wreck) continue;
		float d = hypotf(c->x - x, c->z - z);
		if (d < R && d > 0.1f) {
			damage_car(g, i, 45.0f * (1 - d / R), byPlayer);
			if (!c->wreck && c->hp > 0) { c->vx += (c->x - x) / d * 7.0f; c->vz += (c->z - z) / d * 7.0f; c->ai.on = false; }
		}
	}
	Player *p = &g->p;
	if (p->car < 0 && p->ps == PS_ALIVE) { float d = hypotf(p->p[0] - x, p->p[2] - z); if (d < R) damage_player(g, 90.0f * (1 - d / R)); }
	if (byPlayer) add_heat(g, 35.0f);
}

// ---- bullets ----------------------------------------------------------------------------------------------------------------------------------------
void player_muzzle(const Game *g, float out[3])
{
	const Player *p = &g->p;
	float sy = sinf(p->face), cy = cosf(p->face);
	out[0] = p->p[0] + sy * 0.85f + cy * 0.2f; out[1] = p->p[1] + 1.3f; out[2] = p->p[2] - cy * 0.85f + sy * 0.2f;
}

void fire_bullet(Game *g, int owner, const float from[3], float yaw, float pitch, const WeaponDef *w, float dmgScale)
{
	for (int k = 0; k < w->pellets; k++) {
		Bullet *b = NULL;
		for (int i = 0; i < MAX_BULLET; i++) if (!g->bullet[i].on) { b = &g->bullet[i]; break; }
		if (!b) return;
		float yy = yaw + srand_(g) * w->spread, pp = pitch + srand_(g) * w->spread * 0.4f;
		b->on = true; b->owner = owner; b->dmg = (int)(w->dmg * dmgScale);
		memcpy(b->p, from, sizeof b->p);
		b->v[0] = sinf(yy) * cosf(pp) * w->speed; b->v[1] = sinf(pp) * w->speed; b->v[2] = -cosf(yy) * cosf(pp) * w->speed;
		b->life = w->range / w->speed;
	}
}

// squared distance in the ground plane from point (px, pz) to the segment a -> b, and where along it (0..1) the closest point is
static float seg_dist2(float ax, float az, float bx, float bz, float px, float pz, float *t)
{
	float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
	float u = l2 > 1e-9f ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0;
	u = clampf(u, 0, 1); *t = u;
	float cx = ax + dx * u - px, cz = az + dz * u - pz;
	return cx * cx + cz * cz;
}

static void bullets_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_BULLET; i++) {
		Bullet *b = &g->bullet[i];
		if (!b->on) continue;
		float a[3] = { b->p[0], b->p[1], b->p[2] };
		float nx = a[0] + b->v[0] * dt, ny = a[1] + b->v[1] * dt, nz = a[2] + b->v[2] * dt;
		b->life -= dt;
		float sp = sqrtf(b->v[0] * b->v[0] + b->v[1] * b->v[1] + b->v[2] * b->v[2]), seg = sp * dt;
		float best = 2.0f; int what = 0, who = -1;                          // earliest hit along this step: 1 building, 2 ped, 3 car, 4 player
		float dir[3] = { b->v[0] / sp, b->v[1] / sp, b->v[2] / sp }, n[3];
		float tb = city_ray(&g->w, a, dir, seg, n);
		if (tb >= 0 && tb / seg < best) { best = tb / seg; what = 1; }
		if (ny < 0.05f) { best = fminf(best, a[1] > 0 ? a[1] / (a[1] - ny) : 0); what = 1; }
		for (int k = 0; k < MAX_PED; k++) {
			const Ped *p = &g->ped[k];
			if (!p->on || p->dead || (b->owner == 1 && p->cop)) continue;
			if (b->owner == 1) continue;                                     // officers aim at Otto only
			float t, d2 = seg_dist2(a[0], a[2], nx, nz, p->x, p->z, &t);
			float y = a[1] + (ny - a[1]) * t;
			if (d2 < 0.45f * 0.45f && y > 0.0f && y < 1.95f && t < best) { best = t; what = 2; who = k; }
		}
		for (int k = 0; k < MAX_CAR; k++) {
			const Car *c = &g->car[k];
			if (!c->on || (b->owner == 0 && g->p.car == k) || (b->owner == 1 && g->p.car != k)) continue;
			float cx[3], cz[3]; car_circles(c, cx, cz);
			float cr = car_radius(c->kind) * 0.95f, top = c->kind == CAR_HELI ? 3.0f : c->kind == CAR_BIKE ? 1.5f : 1.7f;
			for (int q = 0; q < 3; q++) {
				float t, d2 = seg_dist2(a[0], a[2], nx, nz, cx[q], cz[q], &t), y = a[1] + (ny - a[1]) * t - c->y;
				if (d2 < cr * cr && y > 0.15f && y < top && t < best) { best = t; what = 3; who = k; }
			}
		}
		if (b->owner == 1 && g->p.car < 0 && g->p.ps == PS_ALIVE) {
			float t, d2 = seg_dist2(a[0], a[2], nx, nz, g->p.p[0], g->p.p[2], &t), y = a[1] + (ny - a[1]) * t;
			if (d2 < 0.5f * 0.5f && y > 0.0f && y < 1.9f && t < best) { best = t; what = 4; }
		}
		if (what) {
			float hx = a[0] + (nx - a[0]) * best, hy = a[1] + (ny - a[1]) * best, hz = a[2] + (nz - a[2]) * best;
			if (what == 1) spawn_particles(g, PT_SPARK, hx, hy, hz, 3, 3.0f, 0.07f, 0.25f);
			else if (what == 2) { damage_ped(g, who, (float)b->dmg, b->owner == 0); Ped *p = &g->ped[who]; p->vx += b->v[0] * 0.02f; p->vz += b->v[2] * 0.02f; }
			else if (what == 3) {
				spawn_particles(g, PT_SPARK, hx, hy, hz, 3, 3.0f, 0.07f, 0.25f);
				if (b->owner == 0) g->hitT = 0.15f;
				damage_car(g, who, (float)b->dmg * (b->owner == 0 ? 0.2f : 0.09f), b->owner == 0);       // a car takes a lot of bullets: about 12 pistol shots, 25 SMG rounds, 5 shotgun blasts
				if (b->owner == 1) damage_player(g, (float)b->dmg * 0.25f);
			} else if (what == 4) damage_player(g, (float)b->dmg);
			b->on = false;
			continue;
		}
		b->p[0] = nx; b->p[1] = ny; b->p[2] = nz;
		if (b->life <= 0) b->on = false;
	}
}

// ---- particles, decals, blasts, pickups -------------------------------------------------------------------------------------------------------
static void effects_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_PART; i++) {
		Particle *p = &g->part[i];
		if (!p->on) continue;
		p->life -= dt;
		if (p->life <= 0) { p->on = false; continue; }
		if (p->kind == PT_BLOOD || p->kind == PT_SPARK) {
			p->v[1] -= 13.0f * dt;
			if (p->p[1] < 0.03f) { if (p->kind == PT_BLOOD && crand(g) < 0.35f) add_decal(g, p->p[0], p->p[2], 0.35f + 0.4f * crand(g)); p->on = false; continue; }
		} else { p->v[1] += (p->kind == PT_FIRE ? 1.5f : 0.6f) * dt; p->v[0] *= expf(-1.2f * dt); p->v[2] *= expf(-1.2f * dt); }
		p->p[0] += p->v[0] * dt; p->p[1] += p->v[1] * dt; p->p[2] += p->v[2] * dt;
	}
	for (int i = 0; i < MAX_DECAL; i++) { Decal *d = &g->decal[i]; if (d->on) { d->age += dt; if (d->age > 28.0f) d->on = false; } }
	for (int i = 0; i < MAX_BLAST; i++) { Blast *b = &g->blast[i]; if (b->on) { b->t += dt; if (b->t > 1.5f) b->on = false; } }
}

static void pickups_update(Game *g, float dt)
{
	Player *p = &g->p;
	for (int i = 0; i < MAX_PICKUP; i++) {
		Pickup *k = &g->pick[i];
		if (!k->on) { if (k->fixed && k->respawn > 0) { k->respawn -= dt; if (k->respawn <= 0) k->on = true; } continue; }
		k->t += dt;
		if (!k->fixed) { k->respawn -= dt; if (k->respawn <= 0) { k->on = false; continue; } }
		if (p->ps != PS_ALIVE) continue;
		float d = hypotf(k->x - p->p[0], k->z - p->p[2]);
		if (d > (p->car >= 0 ? 3.0f : 1.7f)) continue;
		char buf[28];
		switch (k->type) {
		case PK_CASH: p->cash += k->amount; snprintf(buf, sizeof buf, "+$%d", k->amount); toast(g, buf); g->saveNeeded = true; break;
		case PK_HEALTH: if (p->hp >= PLAYER_MAX_HP) continue; p->hp = fminf(PLAYER_MAX_HP, p->hp + 75.0f); toast(g, "Health"); break;
		case PK_ARMOR: if (p->armor >= 100.0f) continue; p->armor = fminf(100.0f, p->armor + 50.0f); toast(g, "Body armor"); break;
		default: {
			int w = k->type - PK_PISTOL + W_PISTOL, add = w == W_PISTOL ? 24 : w == W_SMG ? 60 : 16;
			p->has[w] = true; p->ammo[w] += add; snprintf(buf, sizeof buf, "%s +%d", WPN[w].name, add); toast(g, buf);
			if (p->weapon == W_FISTS) p->weapon = w;
		} }
		k->on = false;
		if (k->fixed) k->respawn = 35.0f;
	}
}

static void cars_damage_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_CAR; i++) {
		Car *c = &g->car[i];
		if (!c->on) continue;
		if (c->wreck) {
			c->wreckT += dt; c->vx *= expf(-2.0f * dt); c->vz *= expf(-2.0f * dt);
			c->rpm = 0;
			if (c->y > 0) {                                                        // a wrecked helicopter drops out of the sky
				c->vy -= 22.0f * dt; c->y += c->vy * dt;
				if (c->y <= 0) { if (c->vy < -6.0f) spawn_blast(g, c->x, 0.8f, c->z, 0); c->y = 0; c->vy = 0; }
			}
			if (crand(g) < dt * 6.0f) spawn_particles(g, PT_SMOKE, c->x + srand_(g) * 0.8f, 1.0f, c->z + srand_(g) * 0.8f, 1, 1.0f, 0.9f, 1.8f);
			if (c->wreckT < 12.0f && crand(g) < dt * 8.0f) spawn_particles(g, PT_FIRE, c->x + srand_(g) * 0.7f, 0.8f, c->z + srand_(g) * 0.7f, 1, 0.8f, 0.6f, 0.7f);
			float d = hypotf(c->x - g->p.p[0], c->z - g->p.p[2]);
			if (c->wreckT > 45.0f && d > 40.0f) c->on = false;
			continue;
		}
		if (c->hp < 30.0f && crand(g) < dt * 5.0f) spawn_particles(g, PT_SMOKE, c->x + sinf(c->yaw) * 1.6f, 1.0f, c->z - cosf(c->yaw) * 1.6f, 1, 1.0f, 0.6f, 1.4f);
		if (c->hp < 10.0f) {
			c->hp -= 6.0f * dt;                                                // burning: it will go up in a couple of seconds
			if (crand(g) < dt * 9.0f) spawn_particles(g, PT_FIRE, c->x + sinf(c->yaw) * 1.4f, 0.9f, c->z - cosf(c->yaw) * 1.4f, 1, 0.8f, 0.5f, 0.6f);
			if (c->hp <= 0) explode_car(g, i, g->p.car == i);
		}
	}
}

// ---- the player's weapons ---------------------------------------------------------------------------------------------------------------------
static void next_weapon(Player *p, int dir)
{
	for (int k = 1; k <= W_COUNT; k++) {
		int w = ((p->weapon + dir * k) % W_COUNT + W_COUNT) % W_COUNT;
		if (p->has[w] && (w == W_FISTS || p->ammo[w] > 0)) { p->weapon = w; return; }
	}
}

// pick the thing to shoot at: the best living person within range, preferring the ones closest to the heading `ref` (Otto's, or the camera's)
static int find_target(const Game *g, float range, float ref, float *yawOut)
{
	const Player *p = &g->p;
	int best = -1; float bs = 1e9f;
	for (int i = 0; i < MAX_PED; i++) {
		const Ped *q = &g->ped[i];
		if (!q->on || q->dead) continue;
		float dx = q->x - p->p[0], dz = q->z - p->p[2], d = sqrtf(dx * dx + dz * dz);
		if (d > range || d < 0.3f) continue;
		float want = atan2f(dx, -dz), diff = fabsf(wrapang(want - ref));
		if (diff > DEG(62) && d > 5.0f) continue;
		if (!los_clear(g, p->p[0], 1.3f, p->p[2], q->x, 1.2f, q->z)) continue;
		float score = d + diff * 6.0f + (q->state == PED_DOWN ? 8.0f : 0) - (q->cop && g->stars > 0 ? 4.0f : 0);
		if (score < bs) { bs = score; best = i; *yawOut = want; }
	}
	return best;
}

// Lock-on (hold L): the nearest person in front of the camera; flick the circle pad toward another person to switch to them.
static int switch_target(const Game *g, int cur, float stx, float sty, float range)
{
	const Player *p = &g->p;
	float cy = cosf(g->cam.yaw), sy = sinf(g->cam.yaw);
	const Ped *c = &g->ped[cur];
	float r0 = (c->x - p->p[0]) * cy + (c->z - p->p[2]) * sy, f0 = (c->x - p->p[0]) * sy - (c->z - p->p[2]) * cy;       // the current target on the screen's right / forward axes
	int best = -1; float bs = -1e9f;
	for (int i = 0; i < MAX_PED; i++) {
		const Ped *q = &g->ped[i];
		if (i == cur || !q->on || q->dead) continue;
		float dx = q->x - p->p[0], dz = q->z - p->p[2];
		if (dx * dx + dz * dz > range * range) continue;
		float vr = dx * cy + dz * sy - r0, vf = dx * sy - dz * cy - f0, l = sqrtf(vr * vr + vf * vf);
		if (l < 0.5f) continue;
		float align = (vr * stx + vf * sty) / l;
		if (align < 0.45f) continue;
		if (!los_clear(g, p->p[0], 1.3f, p->p[2], q->x, 1.2f, q->z)) continue;
		float score = align * 10.0f - l * 0.2f;
		if (score > bs) { bs = score; best = i; }
	}
	return best;
}

static void lock_update(Game *g, const Input *in, float dt, float range)
{
	Player *p = &g->p;
	p->lockCd -= dt;
	if (p->lock >= 0) {
		const Ped *q = &g->ped[p->lock];
		if (!q->on || q->dead || hypotf(q->x - p->p[0], q->z - p->p[2]) > range + 8.0f) p->lock = -1;
		else if (!los_clear(g, p->p[0], 1.3f, p->p[2], q->x, 1.2f, q->z)) { p->lockLosT += dt; if (p->lockLosT > 1.2f) p->lock = -1; }
		else p->lockLosT = 0;
	}
	if (p->lock < 0 && p->lockCd <= 0) {
		float y;
		p->lock = find_target(g, range, g->cam.yaw, &y); p->lockLosT = 0;
		if (p->lock < 0) p->lockCd = 0.12f;
	}
	float mag = hypotf(in->lx, in->ly);
	if (p->lock >= 0 && mag > 0.75f && p->lockMag <= 0.75f && p->lockCd <= 0) {
		int n = switch_target(g, p->lock, in->lx / mag, in->ly / mag, range);
		if (n >= 0) { p->lock = n; p->lockCd = 0.25f; p->lockLosT = 0; }
	}
	p->lockMag = mag;
}

static void startle(Game *g, float x, float z, float radius)
{
	g->noiseX = x; g->noiseZ = z; g->noiseT = 0;
	for (int i = 0; i < MAX_PED; i++) {
		Ped *q = &g->ped[i];
		if (!q->on || q->dead || q->cop || q->state == PED_DOWN) continue;
		if (hypotf(q->x - x, q->z - z) < radius) { q->state = PED_FLEE; q->timer = 4.5f; }
	}
}

static void player_weapons(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	if (in->x_p) next_weapon(p, 1);
	if (in->weap > 0 && in->weap <= W_COUNT && p->has[in->weap - 1] && (in->weap - 1 == W_FISTS || p->ammo[in->weap - 1] > 0)) p->weapon = in->weap - 1;
	if (p->weapon != W_FISTS && p->ammo[p->weapon] <= 0) next_weapon(p, 1);
	p->fireCd -= dt; p->flashT -= dt; p->punchT -= dt; p->hurtT -= dt;
	bool onFoot = p->car < 0, shoot = in->gas && onFoot, aimHeld = in->aim && onFoot, gun = p->weapon != W_FISTS;
	const WeaponDef *w = &WPN[p->weapon];
	if (aimHeld) lock_update(g, in, dt, gun ? fminf(w->range * 0.8f, 40.0f) : 6.0f);
	else { p->lock = -1; p->lockMag = 0; p->lockCd = 0; }
	p->target = -1;
	float aimTarget = 0;
	if (shoot || aimHeld) {
		float ty = p->face;
		int t = -1;
		if (aimHeld) {                                                            // L: face the locked person, or where the camera looks
			t = p->lock;
			ty = t >= 0 ? atan2f(g->ped[t].x - p->p[0], -(g->ped[t].z - p->p[2])) : g->cam.yaw;
		} else t = find_target(g, gun ? fminf(w->range * 0.6f, 32.0f) : 2.4f, p->face, &ty);       // R alone: auto-aim
		p->target = t;
		p->aimYaw = t >= 0 || aimHeld ? ty : p->face;
		p->face = wrapang(p->face + wrapang(p->aimYaw - p->face) * fminf(1.0f, dt * 14.0f));
		aimTarget = 1;
		if (shoot && gun) {
			if (p->aimT > 0.55f && p->fireCd <= 0 && p->ammo[p->weapon] > 0) {
				float m[3]; player_muzzle(g, m);
				float pitch = 0;
				if (t >= 0) { const Ped *q = &g->ped[t]; float d = hypotf(q->x - m[0], q->z - m[2]); pitch = atan2f(1.15f - m[1], fmaxf(d, 1.0f)); }
				fire_bullet(g, 0, m, p->face, pitch, w, 1.0f);
				if (!TEST_LOADOUT) p->ammo[p->weapon]--;
				p->fireCd = 1.0f / w->rate; p->flashT = 0.07f; p->lastShotT = g->time;
				spawn_particles(g, PT_SPARK, m[0], m[1], m[2], 2, 2.0f, 0.06f, 0.15f);
				startle(g, p->p[0], p->p[2], 42.0f);
				if (p->ammo[p->weapon] == 0) toast(g, "Out of ammo");
			}
		} else if (shoot && p->fireCd <= 0) {                                       // a punch: it lands when the fist is fully out
			p->punchT = 0.34f; p->punchSide ^= 1; p->fireCd = 1.0f / w->rate; p->punchHit = true;
		}
	}
	if (p->punchHit && p->punchT <= 0.34f * 0.58f) {
		p->punchHit = false;
		float ty = p->face;
		int t = find_target(g, 2.4f, p->face, &ty);
		if (t >= 0) {
			p->target = t; p->aimYaw = ty;
			Ped *q = &g->ped[t];
			if (hypotf(q->x - p->p[0], q->z - p->p[2]) < 2.1f) { damage_ped(g, t, WPN[W_FISTS].dmg, 1); q->vx += sinf(p->face) * 2.5f; q->vz += -cosf(p->face) * 2.5f; if (!q->dead && q->state != PED_DOWN && !q->cop) { q->state = PED_DOWN; q->timer = 2.0f; } }
		}
	}
	if (p->punchT > 0) aimTarget = 1;
	if (gun && g->time - p->lastShotT < 0.7f) aimTarget = 1;
	p->aimT += (aimTarget - p->aimT) * fminf(1.0f, dt * (aimTarget > p->aimT ? 12.0f : 6.0f));
	// the shooting itself is a crime if anybody is around to see it
	if (g->time - p->lastShotT < 0.25f) {
		for (int i = 0; i < MAX_PED; i++) { const Ped *q = &g->ped[i]; if (q->on && !q->dead && hypotf(q->x - p->p[0], q->z - p->p[2]) < 45.0f) { add_heat(g, 9.0f * dt); break; } }
	}
	if (p->hurtT <= -3.5f && p->hp < PLAYER_MAX_HP && p->hp > 0) { p->hp = fminf(PLAYER_MAX_HP, p->hp + 5.0f * dt); }       // recovery after a few quiet seconds
}

// ---- respawn -------------------------------------------------------------------------------------------------------------------------------------
static void give_weapons(Player *p)                                                 // testing loadout: every weapon, ammo never runs out
{
	for (int w = 0; w < W_COUNT; w++) { p->has[w] = true; p->ammo[w] = TEST_AMMO; }
}

static void respawn(Game *g, bool arrested)
{
	Player *p = &g->p;
	p->cash -= (int)(p->cash * (arrested ? 0.2f : 0.1f));
	if (arrested && !TEST_LOADOUT) { for (int w = W_PISTOL; w < W_COUNT; w++) { p->has[w] = false; p->ammo[w] = 0; } p->weapon = W_FISTS; }
	p->hp = PLAYER_MAX_HP; p->armor = 0; p->ps = PS_ALIVE; p->psT = 0; p->arrestT = 0; p->hurtT = 0; p->aimT = 0; p->sprint = false; p->lock = -1;
	p->car = -1; p->v[0] = p->v[2] = 0; p->speed = 0;
	float rx = arrested ? POLICE_X : HOSPITAL_X, rz = arrested ? POLICE_Z : HOSPITAL_Z;
	p->p[0] = rx + 8.0f; p->p[2] = rz; p->face = DEG(arrested ? 270 : 90);
	g->cam.yaw = p->face; g->cam.manualT = 0;
	g->timeScale = 1.0f; g->hudT = 0; g->heat = 0; g->stars = 0; g->unseenT = 0;
	police_reset(g);
	toast(g, arrested ? (TEST_LOADOUT ? "Busted" : "Busted: weapons confiscated") : "Out of hospital");
	g->saveNeeded = true;
}

void combat_init(Game *g)
{
	Player *p = &g->p;
	int cash = p->cash;
	p->hp = PLAYER_MAX_HP; p->armor = 0; p->ps = PS_ALIVE; p->cash = cash;
	memset(p->has, 0, sizeof p->has); memset(p->ammo, 0, sizeof p->ammo);
	p->has[W_FISTS] = true; p->has[W_PISTOL] = true; p->ammo[W_PISTOL] = 36; p->weapon = W_PISTOL; p->target = -1; p->lock = -1;
	if (TEST_LOADOUT) give_weapons(p);
	memset(g->bullet, 0, sizeof g->bullet); memset(g->part, 0, sizeof g->part); memset(g->decal, 0, sizeof g->decal); memset(g->pick, 0, sizeof g->pick); memset(g->blast, 0, sizeof g->blast);
	g->heat = 0; g->stars = 0; g->timeScale = 1.0f; g->hudT = 0; g->toastT = 0;
	// fixed pickups on the pavements: (block i, block j, corner, type)
	static const int spots[][4] = {
		{ 3, 3, 0, PK_HEALTH }, { 5, 2, 2, PK_HEALTH }, { 1, 5, 1, PK_HEALTH }, { 6, 6, 3, PK_HEALTH },
		{ 2, 2, 1, PK_ARMOR }, { 5, 5, 0, PK_ARMOR }, { 1, 1, 2, PK_ARMOR },
		{ 4, 3, 2, PK_SMG }, { 0, 4, 1, PK_SMG }, { 7, 1, 3, PK_SMG },
		{ 3, 6, 0, PK_SHOTGUN }, { 6, 2, 1, PK_SHOTGUN }, { 1, 3, 3, PK_SHOTGUN },
		{ 4, 5, 3, PK_PISTOL }, { 2, 0, 2, PK_PISTOL }, { 7, 5, 0, PK_PISTOL }, { 5, 7, 1, PK_PISTOL },
		// more weapons, spread so that one is never far away
		{ 2, 3, 2, PK_PISTOL }, { 6, 4, 3, PK_PISTOL }, { 0, 1, 0, PK_PISTOL }, { 3, 4, 1, PK_PISTOL }, { 5, 3, 0, PK_PISTOL }, { 1, 7, 1, PK_PISTOL },
		{ 5, 1, 2, PK_SMG }, { 2, 6, 3, PK_SMG }, { 6, 5, 1, PK_SMG }, { 3, 2, 0, PK_SMG }, { 7, 3, 2, PK_SMG }, { 4, 7, 3, PK_SMG },
		{ 0, 3, 3, PK_SHOTGUN }, { 4, 1, 0, PK_SHOTGUN }, { 7, 6, 1, PK_SHOTGUN }, { 2, 5, 2, PK_SHOTGUN }, { 5, 6, 0, PK_SHOTGUN },
		{ 6, 1, 0, PK_HEALTH }, { 0, 5, 2, PK_HEALTH }, { 4, 2, 3, PK_ARMOR }, { 7, 4, 0, PK_ARMOR },
		{ 4, 4, 1, PK_CASH }, { 2, 4, 3, PK_CASH }, { 6, 4, 0, PK_CASH }, { 3, 1, 2, PK_CASH }, { 5, 0, 3, PK_CASH }, { 0, 6, 1, PK_CASH } };
	for (unsigned i = 0; i < sizeof spots / sizeof spots[0]; i++) {
		float x, z; traffic_node_xy((((spots[i][1] * NB + spots[i][0]) << 2) | spots[i][2]), &x, &z);
		add_pickup(g, spots[i][3], spots[i][3] == PK_CASH ? 100 : 0, x, z, true);
	}
	g->pickT = 6.0f;
}

// a new pickup now and then on a pavement near Otto, so there is nearly always a weapon within reach
static void temp_pickups(Game *g, float dt)
{
	g->pickT -= dt;
	if (g->pickT > 0) return;
	g->pickT = 14.0f;
	int near = 0;
	for (int i = 0; i < MAX_PICKUP; i++) { const Pickup *k = &g->pick[i]; if (k->on && k->type >= PK_PISTOL && hypotf(k->x - g->p.p[0], k->z - g->p.p[2]) < 110.0f) near++; }
	if (near >= 3) return;
	for (int tries = 0; tries < 20; tries++) {
		int id = (int)(rnd_next(&g->trng) % (NB * NB * 4));
		float x, z; traffic_node_xy(id, &x, &z);
		float d = hypotf(x - g->p.p[0], z - g->p.p[2]);
		if (d < 30.0f || d > 95.0f) continue;
		float r = crand(g);
		int type = r < 0.28f ? PK_PISTOL : r < 0.55f ? PK_SMG : r < 0.78f ? PK_SHOTGUN : r < 0.9f ? PK_HEALTH : PK_ARMOR;
		for (int i = 0; i < MAX_PICKUP; i++) if (!g->pick[i].on && !g->pick[i].fixed) { Pickup *k = &g->pick[i]; k->on = true; k->type = type; k->amount = 0; k->x = x; k->z = z; k->t = 0; k->respawn = 100.0f; k->fixed = false; break; }
		return;
	}
}

void combat_update(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	temp_pickups(g, dt);
	if (g->toastT > 0) g->toastT -= dt;
	if (g->hitT > 0) g->hitT -= dt;
	if (g->killT > 0) g->killT -= dt;
	if (g->hudT > 0) g->hudT -= dt;
	g->noiseT += dt;
	if (p->ps == PS_ALIVE) player_weapons(g, in, dt);
	else {
		p->psT += dt / fmaxf(g->timeScale, 0.01f);                            // real seconds
		p->aimT *= expf(-6.0f * dt);
		if (p->psT > (p->ps == PS_WASTED ? 3.4f : 2.6f)) respawn(g, p->ps == PS_BUSTED);
	}
	bullets_update(g, dt);
	effects_update(g, dt);
	pickups_update(g, dt);
	cars_damage_update(g, dt);
	// Pay 'n' Spray: a car on the pad is repainted and the police forget about it
	if (p->car >= 0 && p->ps == PS_ALIVE && hypotf(g->car[p->car].x - SPRAY_X, g->car[p->car].z - SPRAY_Z) < SPRAY_R && g->pnsT <= 0) {
		static const float PAINT[6][3] = { { .8f, .12f, .1f }, { .12f, .3f, .7f }, { .92f, .92f, .9f }, { .1f, .1f, .12f }, { .2f, .55f, .28f }, { .55f, .2f, .6f } };
		memcpy(g->car[p->car].col, PAINT[(int)(crand(g) * 5.99f)], sizeof g->car[p->car].col);
		g->car[p->car].hp = 100.0f; g->car[p->car].burn = 0;
		if (g->stars > 0) { g->heat = 0; g->stars = 0; police_reset(g); toast(g, "Wanted level cleared"); } else toast(g, "Fresh paint");
		g->pnsT = 8.0f;
	}
	if (g->pnsT > 0) g->pnsT -= dt;
}
