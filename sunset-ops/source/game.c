// Sunset Ops - simulation: player, weapons, bots, grenades, effects, match rules.
#include "game.h"
#include <string.h>
#include <stdio.h>

const WeaponDef weapons[W_COUNT] = {
	//  name             mode         auto   mag res pel  dmg  rate   reload hip  ads   kick  fov  r0  r1  zoomsens
	{ "M27 AR",         "FULL-AUTO", true,  30, 180, 1,  22, 0.085f, 2.0f, 1.9f, 0.22f, 0.85f, 40, 45, 85, 0.40f },
	{ "SKORPION SMG",   "FULL-AUTO", true,  32, 192, 1,  16, 0.060f, 1.7f, 2.8f, 0.60f, 0.55f, 44, 22, 55, 0.45f },
	{ "B23R PISTOL",    "SEMI-AUTO", false, 12,  72, 1,  34, 0.150f, 1.3f, 1.3f, 0.15f, 1.40f, 46, 30, 65, 0.55f },
	{ "R870 SHOTGUN",   "PUMP",      false,  6,  30, 9,  14, 0.800f, 2.3f, 4.8f, 3.40f, 5.00f, 46,  8, 24, 0.50f },
};

static const char *names[MAX_ACT] = { "RICO_Z", "Rook", "Ghost9", "Mako", "ViperOne", "DustDevil", "K_NE24", "Bandit" };
const char *callsign(int id) { return names[id & 7]; }

float frand(Game *g)
{
	g->rng ^= g->rng << 13; g->rng ^= g->rng >> 17; g->rng ^= g->rng << 5;
	return (g->rng & 0xFFFFFF) / 16777216.0f;
}
static float frange(Game *g, float a, float b) { return a + (b - a) * frand(g); }
static float wrap_pi(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

// ---- collision --------------------------------------------------------------------------------------------------
bool world_blocked(const World *w, float x, float z, float r)
{
	if (x < -ARENA_X + r || x > ARENA_X - r || z < -ARENA_Z + r || z > ARENA_Z - r) return true;
	for (int i = 0; i < w->nsolid; i++) {
		const Box *b = &w->box[w->solid_idx[i]];
		if (b->y0 > 1.8f) continue;
		float cx = x < b->x0 ? b->x0 : x > b->x1 ? b->x1 : x;
		float cz = z < b->z0 ? b->z0 : z > b->z1 ? b->z1 : z;
		float dx = x - cx, dz = z - cz;
		if (dx * dx + dz * dz < r * r) return true;
	}
	return false;
}

static bool ray_box(const float o[3], const float d[3], const Box *b, float *tout, float n[3])
{
	const float lo[3] = { b->x0, b->y0, b->z0 }, hi[3] = { b->x1, b->y1, b->z1 };
	float tmin = 0, tmax = 1e9f; int axis = -1; float sgn = 0;
	for (int i = 0; i < 3; i++) {
		if (fabsf(d[i]) < 1e-8f) { if (o[i] < lo[i] || o[i] > hi[i]) return false; continue; }
		float inv = 1.0f / d[i], t1 = (lo[i] - o[i]) * inv, t2 = (hi[i] - o[i]) * inv, s = -1.0f;
		if (t1 > t2) { float t = t1; t1 = t2; t2 = t; s = 1.0f; }
		if (t1 > tmin) { tmin = t1; axis = i; sgn = s; }
		if (t2 < tmax) tmax = t2;
		if (tmin > tmax) return false;
	}
	if (axis < 0) return false;                       // started inside
	*tout = tmin;
	n[0] = n[1] = n[2] = 0; n[axis] = sgn;
	return true;
}

float ray_world(const World *w, const float o[3], const float d[3], float maxd, float n[3])
{
	float best = maxd; n[0] = n[1] = n[2] = 0; n[1] = 1;
	if (d[1] < -1e-6f) { float t = -o[1] / d[1]; if (t > 0 && t < best) best = t; }
	for (int i = 0; i < w->nsolid; i++) {
		float t, nn[3];
		if (ray_box(o, d, &w->box[w->solid_idx[i]], &t, nn) && t < best) { best = t; n[0] = nn[0]; n[1] = nn[1]; n[2] = nn[2]; }
	}
	return best;
}

// distance along the ray to the actor, or -1; head set when the head sphere was hit
static float ray_actor(const Actor *a, const float o[3], const float d[3], float maxd, bool *head)
{
	float best = -1; *head = false;
	// head: sphere r=0.2 at y=1.58
	{
		float cx = a->x - o[0], cy = 1.58f - o[1], cz = a->z - o[2];
		float b = cx * d[0] + cy * d[1] + cz * d[2], c = cx * cx + cy * cy + cz * cz - 0.2f * 0.2f;
		float disc = b * b - c;
		if (disc >= 0) { float t = b - sqrtf(disc); if (t > 0 && t < maxd) { best = t; *head = true; } }
	}
	// body: vertical cylinder r=0.36, y in [0, 1.42]
	{
		float ox = o[0] - a->x, oz = o[2] - a->z;
		float A = d[0] * d[0] + d[2] * d[2], B = ox * d[0] + oz * d[2], C = ox * ox + oz * oz - 0.36f * 0.36f;
		if (A > 1e-8f) {
			float disc = B * B - A * C;
			if (disc >= 0) {
				float t = (-B - sqrtf(disc)) / A;
				float y = o[1] + d[1] * t;
				if (t > 0 && t < maxd && y >= 0 && y <= 1.42f && (best < 0 || t < best)) { best = t; *head = false; }
			}
		}
	}
	return best;
}

// ---- effects ----------------------------------------------------------------------------------------------------
static void fx_add(Game *g, int kind, const float p[3], const float v[3], float life, float size, float grow, float r, float gg, float b)
{
	Fx *f;
	if (g->fx_n < MAX_FX) f = &g->fx[g->fx_n++]; else f = &g->fx[(int)(frand(g) * MAX_FX) % MAX_FX];
	f->p[0] = p[0]; f->p[1] = p[1]; f->p[2] = p[2]; f->v[0] = v[0]; f->v[1] = v[1]; f->v[2] = v[2];
	f->life = f->max = life; f->size = size; f->grow = grow; f->r = r; f->g = gg; f->b = b; f->kind = (uint8_t)kind;
}
static void puff(Game *g, int kind, const float p[3], float speed, int n, float life, float size, float grow, float r, float gg, float b)
{
	for (int i = 0; i < n; i++) {
		float v[3] = { frange(g, -1, 1) * speed, frange(g, -0.2f, 1) * speed, frange(g, -1, 1) * speed };
		fx_add(g, kind, p, v, life * frange(g, 0.7f, 1.2f), size * frange(g, 0.7f, 1.2f), grow, r, gg, b);
	}
}
static void add_popup(Game *g, const char *t, unsigned col)
{
	memmove(&g->pop[1], &g->pop[0], sizeof(Popup) * 2);
	snprintf(g->pop[0].text, sizeof g->pop[0].text, "%s", t); g->pop[0].t = 2.2f; g->pop[0].col = col;
}

// ---- damage and kills -------------------------------------------------------------------------------------------
static void spawn_actor(Game *g, int id);

static void hurt(Game *g, int victim, float dmg, int attacker, int weapon, bool head)
{
	Actor *v = &g->a[victim];
	if (!v->alive || v->invulnT > 0 || g->state != GS_PLAY) return;
	v->hp -= dmg; v->hitT = 0.2f; v->lastAttacker = attacker;
	if (victim == 0) {
		g->p.lastDmgT = 0; g->p.shake = clampf(g->p.shake + 0.25f, 0, 1);
		Actor *a = &g->a[attacker];
		float ang = atan2f(a->x - v->x, -(a->z - v->z)) - v->yaw;          // bearing of the attacker relative to where we look
		int slot = 0; for (int i = 1; i < MAX_HITS; i++) if (g->p.dmgT[i] < g->p.dmgT[slot]) slot = i;
		g->p.dmgDir[slot] = wrap_pi(ang); g->p.dmgT[slot] = 1.6f;
		if (attacker == victim) g->p.dmgT[slot] = 0;
	}
	if (v->hp > 0) return;
	v->hp = 0; v->alive = false; v->deadT = 0; v->respawnT = victim == 0 ? 3.5f : 3.0f; v->deaths++;
	if (attacker != victim) { g->a[attacker].kills++; g->score[g->a[attacker].team] += 1; }
	// feed
	memmove(&g->feed[1], &g->feed[0], sizeof(Feed) * (MAX_FEED - 1));
	g->feed[0].killer = attacker; g->feed[0].victim = victim; g->feed[0].weapon = weapon; g->feed[0].t = 6.0f;
	if (attacker == 0 && victim != 0) {
		g->p.killMarkT = 0.4f; g->p.streak++;
		char b[40]; snprintf(b, sizeof b, head ? "HEADSHOT  +150" : "ENEMY KILLED  +100"); add_popup(g, b, head ? 0xFF40D0FF : 0xFFFFFFFF);
		if (g->p.streak == 3) add_popup(g, "3 KILL STREAK", 0xFF40D0FF);
		if (g->p.streak == 5) add_popup(g, "5 KILL STREAK!", 0xFF40D0FF);
	}
	if (victim == 0) { g->p.streak = 0; g->p.reloading = false; g->p.healT = 0; }
}

// one shot (or pellet) from `o` along `d`; returns 1 on an actor hit, 2 on a kill... (kill detected by caller via alive flag)
static int shoot_ray(Game *g, int shooter, const float o[3], const float d[3], int weapon, float dmgmul, const float muzzle[3])
{
	const WeaponDef *wd = &weapons[weapon];
	float n[3];
	float tw = ray_world(&g->w, o, d, 120.0f, n);
	float best = tw; int hit = -1; bool hhead = false;
	for (int i = 0; i < MAX_ACT; i++) {
		const Actor *t = &g->a[i];
		if (i == shooter || !t->alive || t->team == g->a[shooter].team) continue;
		bool head; float ta = ray_actor(t, o, d, best, &head);
		if (ta >= 0 && ta < best) { best = ta; hit = i; hhead = head; }
	}
	float p[3] = { o[0] + d[0] * best, o[1] + d[1] * best, o[2] + d[2] * best };
	// tracer
	if (g->tr_n >= MAX_TRACER) g->tr_n = 0;
	Tracer *tr = &g->tr[g->tr_n++];
	tr->a[0] = muzzle[0]; tr->a[1] = muzzle[1]; tr->a[2] = muzzle[2]; tr->b[0] = p[0]; tr->b[1] = p[1]; tr->b[2] = p[2]; tr->life = 0.07f;
	if (hit >= 0) {
		float dist = best, fall = 1.0f;
		if (dist > wd->range0) fall = lerpf(1.0f, 0.45f, clampf((dist - wd->range0) / (wd->range1 - wd->range0), 0, 1));
		float dmg = wd->dmg * dmgmul * fall * (hhead ? (weapon == W_SHOTGUN ? 1.4f : 2.0f) : 1.0f);
		const float up[3] = { 0, 1.5f, 0 };
		puff(g, 0, p, 1.6f, 4, 0.25f, 0.18f, -0.3f, 0.9f, 0.05f, 0.04f);       // blood
		(void)up;
		hurt(g, hit, dmg, shooter, weapon, hhead);
		return 1 + (g->a[hit].alive ? 0 : 1);
	}
	if (best < 119.0f) {                                    // world impact: dust puff, sparks, bullet hole
		float v[3] = { n[0] * 1.5f, n[1] * 1.5f + 0.3f, n[2] * 1.5f };
		fx_add(g, 1, p, v, 0.5f, 0.12f, 0.5f, 0.75f, 0.66f, 0.55f);
		if (frand(g) < 0.6f) puff(g, 2, p, 2.5f, 2, 0.35f, 0.04f, 0, 1.0f, 0.8f, 0.4f);
		Decal *dc = &g->dec[g->dec_n++ % MAX_DECAL];
		dc->p[0] = p[0] + n[0] * 0.02f; dc->p[1] = p[1] + n[1] * 0.02f; dc->p[2] = p[2] + n[2] * 0.02f;
		dc->n[0] = n[0]; dc->n[1] = n[1]; dc->n[2] = n[2]; dc->life = 25.0f;
	}
	return 0;
}

static void look_dir(float yaw, float pitch, float d[3])
{
	d[0] = sinf(yaw) * cosf(pitch); d[1] = sinf(pitch); d[2] = -cosf(yaw) * cosf(pitch);
}

// ---- grenades ---------------------------------------------------------------------------------------------------
static void throw_grenade(Game *g, int owner, const float p[3], const float v[3])
{
	for (int i = 0; i < MAX_GREN; i++) if (!g->gren[i].live) {
		Grenade *n = &g->gren[i];
		memcpy(n->p, p, sizeof n->p); memcpy(n->v, v, sizeof n->v);
		n->t = 2.6f; n->owner = owner; n->live = true;
		return;
	}
}

static void explode(Game *g, const float p[3], int owner)
{
	fx_add(g, 0, p, (float[3]){ 0, 0, 0 }, 0.35f, 2.2f, 9.0f, 1.0f, 0.75f, 0.35f);
	fx_add(g, 0, p, (float[3]){ 0, 0.5f, 0 }, 0.22f, 1.4f, 14.0f, 1.0f, 0.95f, 0.7f);
	puff(g, 0, p, 6.0f, 10, 0.5f, 0.9f, 1.5f, 1.0f, 0.55f, 0.15f);
	puff(g, 1, p, 3.0f, 12, 1.8f, 0.8f, 2.0f, 0.3f, 0.28f, 0.26f);
	puff(g, 2, p, 9.0f, 14, 0.9f, 0.07f, 0, 1.0f, 0.7f, 0.3f);
	float c[3] = { p[0], p[1] + 0.3f, p[2] };
	for (int i = 0; i < MAX_ACT; i++) {
		Actor *a = &g->a[i];
		if (!a->alive) continue;
		if (a->team == g->a[owner].team && i != owner) continue;
		float tp[3] = { a->x, 0.9f, a->z };
		float dx = tp[0] - c[0], dy = tp[1] - c[1], dz = tp[2] - c[2];
		float d = sqrtf(dx * dx + dy * dy + dz * dz);
		if (d > 8.0f) continue;
		float dir[3] = { dx / (d + 1e-4f), dy / (d + 1e-4f), dz / (d + 1e-4f)}, n[3];
		float t = ray_world(&g->w, c, dir, d, n);
		float dmg = 170.0f * (1.0f - d / 8.0f) * (t < d - 0.3f ? 0.3f : 1.0f);
		if (dmg > 3) hurt(g, i, dmg, owner, -1, false);
	}
	float pd = sqrtf((p[0] - g->a[0].x) * (p[0] - g->a[0].x) + (p[2] - g->a[0].z) * (p[2] - g->a[0].z));
	g->p.shake = clampf(g->p.shake + clampf(1.2f - pd / 18.0f, 0, 1), 0, 1.4f);
}

static void grenades_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_GREN; i++) {
		Grenade *n = &g->gren[i];
		if (!n->live) continue;
		n->t -= dt;
		for (int s = 0; s < 2; s++) {
			float h = dt * 0.5f;
			n->v[1] -= 14.0f * h;
			for (int k = 0; k < 3; k++) n->p[k] += n->v[k] * h;
			if (n->p[1] < 0.1f) { n->p[1] = 0.1f; if (n->v[1] < 0) n->v[1] = -n->v[1] * 0.35f; n->v[0] *= 0.6f; n->v[2] *= 0.6f; if (fabsf(n->v[1]) < 0.8f) { n->v[1] = 0; float fr = fmaxf(0.0f, 1.0f - 4.0f * h); n->v[0] *= fr; n->v[2] *= fr; } }
			for (int j = 0; j < g->w.nsolid; j++) {
				const Box *b = &g->w.box[g->w.solid_idx[j]];
				const float r = 0.12f;
				if (n->p[0] < b->x0 - r || n->p[0] > b->x1 + r || n->p[1] < b->y0 - r || n->p[1] > b->y1 + r || n->p[2] < b->z0 - r || n->p[2] > b->z1 + r) continue;
				float pen[6] = { n->p[0] - (b->x0 - r), (b->x1 + r) - n->p[0], n->p[1] - (b->y0 - r), (b->y1 + r) - n->p[1], n->p[2] - (b->z0 - r), (b->z1 + r) - n->p[2] };
				int m = 0; for (int q = 1; q < 6; q++) if (pen[q] < pen[m]) m = q;
				int ax = m >> 1; float sgn = (m & 1) ? 1.0f : -1.0f;
				n->p[ax] += sgn * pen[m];
				n->v[ax] = -n->v[ax] * 0.45f;
				for (int q = 0; q < 3; q++) if (q != ax) n->v[q] *= 0.85f;
			}
			if (n->p[0] < -ARENA_X + 0.2f || n->p[0] > ARENA_X - 0.2f) { n->v[0] = -n->v[0] * 0.4f; n->p[0] = clampf(n->p[0], -ARENA_X + 0.2f, ARENA_X - 0.2f); }
			if (n->p[2] < -ARENA_Z + 0.2f || n->p[2] > ARENA_Z - 0.2f) { n->v[2] = -n->v[2] * 0.4f; n->p[2] = clampf(n->p[2], -ARENA_Z + 0.2f, ARENA_Z - 0.2f); }
		}
		if (n->t <= 0) { n->live = false; explode(g, n->p, n->owner); }
	}
}

// ---- movement + navigation ---------------------------------------------------------------------------------------
static void move_actor(Game *g, Actor *a, float dx, float dz)
{
	float nx = a->x + dx;
	if (!world_blocked(&g->w, nx, a->z, PLAYER_R)) a->x = nx;
	float nz = a->z + dz;
	if (!world_blocked(&g->w, a->x, nz, PLAYER_R)) a->z = nz;
}
static void separate(Game *g, int id)
{
	Actor *a = &g->a[id];
	for (int i = 0; i < MAX_ACT; i++) {
		if (i == id || !g->a[i].alive) continue;
		float dx = a->x - g->a[i].x, dz = a->z - g->a[i].z, d2 = dx * dx + dz * dz;
		if (d2 < 0.64f * 0.64f && d2 > 1e-6f) { float d = sqrtf(d2), push = (0.64f - d) * 0.5f; move_actor(g, a, dx / d * push, dz / d * push); }
	}
}

static void nav_build(Game *g)
{
	for (int z = 0; z < GRID_H; z++)
		for (int x = 0; x < GRID_W; x++) g->free_cell[z][x] = !world_blocked(&g->w, x + 0.5f - ARENA_X, z + 0.5f - ARENA_Z, 0.62f);
	static uint16_t queue[GRID_W * GRID_H];
	for (int k = 0; k < g->w.nnav; k++) {
		uint16_t (*d)[GRID_W] = g->navd[k];
		for (int z = 0; z < GRID_H; z++) for (int x = 0; x < GRID_W; x++) d[z][x] = 0xFFFF;
		int cx = (int)(g->w.nav[k].x + ARENA_X), cz = (int)(g->w.nav[k].z + ARENA_Z);
		if (cx < 0 || cz < 0 || cx >= GRID_W || cz >= GRID_H || !g->free_cell[cz][cx]) continue;
		int qh = 0, qt = 0;
		d[cz][cx] = 0; queue[qt++] = (uint16_t)(cz * GRID_W + cx);
		while (qh < qt) {
			int cur = queue[qh++], x = cur % GRID_W, z = cur / GRID_W;
			static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
			for (int q = 0; q < 4; q++) {
				int nx = x + dx[q], nz = z + dz[q];
				if (nx < 0 || nz < 0 || nx >= GRID_W || nz >= GRID_H || !g->free_cell[nz][nx] || d[nz][nx] != 0xFFFF) continue;
				d[nz][nx] = (uint16_t)(d[z][x] + 1);
				queue[qt++] = (uint16_t)(nz * GRID_W + nx);
			}
		}
	}
}

// direction to walk from (x,z) towards nav point k; returns false when already there
static bool nav_dir(const Game *g, float x, float z, int k, float *ox, float *oz)
{
	int cx = (int)floorf(x + ARENA_X), cz = (int)floorf(z + ARENA_Z);
	if (cx < 0) cx = 0; if (cz < 0) cz = 0; if (cx >= GRID_W) cx = GRID_W - 1; if (cz >= GRID_H) cz = GRID_H - 1;
	const uint16_t (*d)[GRID_W] = (const uint16_t (*)[GRID_W])g->navd[k];
	float tx = g->w.nav[k].x - x, tz = g->w.nav[k].z - z;
	if (tx * tx + tz * tz < 1.2f * 1.2f) return false;
	int best = 0xFFFF, bx = cx, bz = cz;
	if (!g->free_cell[cz][cx]) {                                  // squeezed into a tight spot: walk to the nearest cell the navigation knows
		float bd = 1e9f;
		for (int dz = -3; dz <= 3; dz++)
			for (int dx = -3; dx <= 3; dx++) {
				int nx = cx + dx, nz = cz + dz;
				if (nx < 0 || nz < 0 || nx >= GRID_W || nz >= GRID_H || !g->free_cell[nz][nx] || d[nz][nx] == 0xFFFF) continue;
				float dd = (float)(dx * dx + dz * dz);
				if (dd < bd) { bd = dd; bx = nx; bz = nz; }
			}
		if (bd < 1e8f) { *ox = bx + 0.5f - ARENA_X - x; *oz = bz + 0.5f - ARENA_Z - z; }
		else { *ox = tx; *oz = tz; }                                // nothing known nearby: just head for the point
		float l = sqrtf(*ox * *ox + *oz * *oz);
		if (l > 1e-4f) { *ox /= l; *oz /= l; return true; }
	}
	for (int dz = -1; dz <= 1; dz++)
		for (int dx = -1; dx <= 1; dx++) {
			if (!dx && !dz) continue;
			int nx = cx + dx, nz = cz + dz;
			if (nx < 0 || nz < 0 || nx >= GRID_W || nz >= GRID_H || d[nz][nx] == 0xFFFF) continue;
			if (dx && dz && (!g->free_cell[cz][nx] || !g->free_cell[nz][cx])) continue;       // no corner cutting
			if (d[nz][nx] < best) { best = d[nz][nx]; bx = nx; bz = nz; }
		}
	if (d[cz][cx] != 0xFFFF && best >= d[cz][cx]) best = d[cz][cx];
	if (best <= 1 && d[cz][cx] <= 2) { *ox = tx; *oz = tz; }
	else { *ox = bx + 0.5f - ARENA_X - x; *oz = bz + 0.5f - ARENA_Z - z; }
	float l = sqrtf(*ox * *ox + *oz * *oz);
	if (l < 1e-4f) return false;
	*ox /= l; *oz /= l;
	return true;
}

// ---- spawning ---------------------------------------------------------------------------------------------------
static void spawn_actor(Game *g, int id)
{
	Actor *a = &g->a[id];
	int best = 0; float bestd = -1;
	for (int i = 0; i < g->w.nspawn; i++) {
		float px = g->w.spawn[i].x, pz = g->w.spawn[i].z, md = 1e9f;
		for (int j = 0; j < MAX_ACT; j++) {
			const Actor *o = &g->a[j];
			if (j == id || !o->alive) continue;
			float d = sqrtf((o->x - px) * (o->x - px) + (o->z - pz) * (o->z - pz));
			if (o->team != a->team) d *= 1.0f; else d = d * 1.0f + 6.0f;                    // prefer far from enemies, not on top of friends
			if (o->team != a->team && d < md) md = d;
			if (o->team == a->team && d < 6.5f) md = d * 0.2f;
		}
		md += frand(g) * 6.0f;
		if (md > bestd) { bestd = md; best = i; }
	}
	a->x = g->w.spawn[best].x; a->z = g->w.spawn[best].z;
	a->vx = a->vz = 0; a->hp = 100; a->alive = true; a->deadT = 0; a->invulnT = 2.0f;
	a->yaw = a->team == TEAM_ALLY ? DEG(90) : DEG(-90); a->pitch = 0;
	a->target = -1; a->fireT = id == 0 ? 0.15f : 0.5f; a->burstLeft = 0; a->reactT = 0; a->thinkT = frand(g) * 0.2f; a->stuckT = 0; a->weapon = W_AR;
	a->wp = (int)(frand(g) * g->w.nnav) % g->w.nnav; a->grenT = frange(g, 4, 10);
	a->lastX = a->x; a->lastZ = a->z; a->hesitate = 0;
	if (id == 0) {
		Player *p = &g->p;
		for (int w = 0; w < W_COUNT; w++) { p->mag[w] = weapons[w].mag; p->reserve[w] = weapons[w].reserve; }
		p->weapon = p->nextWeapon = W_AR; p->grenades = 2; if (p->medkits < 2) p->medkits = 2;
		p->reloading = false; p->reloadT = p->swapT = p->healT = p->healLeft = p->throwT = 0; p->kick = p->kickYaw = 0; p->ads = 0; p->eyeh = EYE_H;
		for (int i = 0; i < MAX_HITS; i++) g->p.dmgT[i] = 0;
	}
}

void game_new_match(Game *g)
{
	unsigned seed = g->rng;
	Pickup keep[MAX_PICK]; memcpy(keep, g->w.pick, sizeof keep);
	memset(g->a, 0, sizeof g->a); memset(&g->p, 0, sizeof g->p);
	memset(g->gren, 0, sizeof g->gren); memset(g->tr, 0, sizeof g->tr); memset(g->dec, 0, sizeof g->dec);
	memset(g->shell, 0, sizeof g->shell); memset(g->feed, 0, sizeof g->feed); memset(g->pop, 0, sizeof g->pop);
	g->fx_n = g->dec_n = g->tr_n = g->shell_n = 0;
	g->rng = seed ? seed : 12345u;
	for (int i = 0; i < MAX_ACT; i++) { g->a[i].team = i < 4 ? TEAM_ALLY : TEAM_ENEMY; g->a[i].callsign = i; g->a[i].alive = false; }
	g->p.medkits = 2;
	for (int i = 0; i < MAX_ACT; i++) {
		Actor *a = &g->a[i];
		int sp = a->team == TEAM_ALLY ? i : i + 1;
		a->alive = false;
		spawn_actor(g, i);
		a->x = g->w.spawn[sp].x; a->z = g->w.spawn[sp].z; a->invulnT = 1.0f;
		a->wp = (i * 5) % g->w.nnav;
	}
	g->score[0] = g->score[1] = 0; g->timeLeft = MATCH_TIME; g->time = 0; g->overT = 0; g->winner = -1;
	for (int i = 0; i < g->w.npick; i++) g->w.pick[i].respawn = 0;
	g->cam.fov = g->fovHip;
}

void game_init(Game *g, int quality)
{
	memset(g, 0, sizeof *g);
	g->quality = g->qualityActive = quality;
	g->rng = 0x2545F491u;
	map_build(&g->w, quality);
	nav_build(g);
	g->fovHip = 58.0f;
	g->sens = 1.0f;
	g->aimAssist = true;
	game_new_match(g);
}

// ---- bots -------------------------------------------------------------------------------------------------------
static bool see(const Game *g, const Actor *a, const Actor *t)
{
	float o[3] = { a->x, 1.55f, a->z }, d[3] = { t->x - a->x, 1.2f - 1.55f, t->z - a->z };
	float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	d[0] /= len; d[1] /= len; d[2] /= len;
	float n[3];
	return ray_world(&g->w, o, d, len, n) >= len - 0.05f;
}

static void bot_pick_waypoint(Game *g, Actor *a)
{
	int k = (int)(frand(g) * g->w.nnav) % g->w.nnav;
	if (frand(g) < 0.55f) {                                         // hunt: head for the nav point nearest to a random living enemy
		int tries = 0, e;
		do { e = (int)(frand(g) * MAX_ACT) % MAX_ACT; tries++; } while ((g->a[e].team == a->team || !g->a[e].alive) && tries < 12);
		if (g->a[e].team != a->team && g->a[e].alive) {
			float bd = 1e9f;
			for (int i = 0; i < g->w.nnav; i++) {
				float d = (g->w.nav[i].x - g->a[e].x) * (g->w.nav[i].x - g->a[e].x) + (g->w.nav[i].z - g->a[e].z) * (g->w.nav[i].z - g->a[e].z);
				if (d < bd) { bd = d; k = i; }
			}
		}
	}
	a->wp = k;
}

static void bot_update(Game *g, int id, float dt)
{
	Actor *a = &g->a[id];
	const float speed = 3.7f;
	a->thinkT -= dt; a->fireT -= dt; a->reactT -= dt; a->strafeT -= dt; a->grenT -= dt; a->burstT -= dt;
	if (a->thinkT <= 0) {
		a->thinkT = 0.18f + frand(g) * 0.12f;
		int best = -1; float bd = 1e9f;
		for (int i = 0; i < MAX_ACT; i++) {
			const Actor *t = &g->a[i];
			if (t->team == a->team || !t->alive) continue;
			float dx = t->x - a->x, dz = t->z - a->z, d = sqrtf(dx * dx + dz * dz);
			if (d > 55) continue;
			float fwdx = sinf(a->yaw), fwdz = -cosf(a->yaw);
			float dot = (dx * fwdx + dz * fwdz) / (d + 1e-4f);
			bool noticed = dot > 0.35f || d < 7 || t->spottedT > 0;
			if (noticed && d < bd && see(g, a, t)) { bd = d; best = i; }
		}
		if (best != a->target) { a->target = best; if (best >= 0) { a->reactT = frange(g, 0.3f, 0.65f); a->burstLeft = 0; } }
		if (best < 0 && a->lastAttacker != id && a->hitT > 0 && g->a[a->lastAttacker].alive && g->a[a->lastAttacker].team != a->team) {       // turn to whoever shot us
			a->yaw += 0; a->target = a->lastAttacker; a->reactT = 0.4f;
		}
	}
	Actor *t = a->target >= 0 ? &g->a[a->target] : NULL;
	if (t && !t->alive) { a->target = -1; t = NULL; }

	float wishx = 0, wishz = 0, wishYaw = a->yaw;
	float navx, navz; bool navigating = nav_dir(g, a->x, a->z, a->wp, &navx, &navz);
	if (!navigating) { bot_pick_waypoint(g, a); navigating = nav_dir(g, a->x, a->z, a->wp, &navx, &navz); }
	float sp = speed;
	if (t) {
		float dx = t->x - a->x, dz = t->z - a->z, d = sqrtf(dx * dx + dz * dz);
		wishYaw = atan2f(dx, -dz);
		bool hold = d < 26.0f;                                    // close enough: stop advancing, strafe instead
		if (a->strafeT <= 0) { a->strafeT = frange(g, 0.6f, 1.6f); a->strafeDir = frand(g) < 0.5f ? -1.0f : 1.0f; a->hesitate = frand(g) < 0.25f ? 1.0f : 0.0f; }
		if (hold) {
			float rx = -dz / (d + 1e-4f), rz = dx / (d + 1e-4f);
			if (a->hesitate > 0) { wishx = wishz = 0; } else { wishx = rx * a->strafeDir * 0.8f; wishz = rz * a->strafeDir * 0.8f; if (d < 7) { wishx -= dx / d * 0.5f; wishz -= dz / d * 0.5f; } }
			sp = 2.6f;
		} else if (navigating) { wishx = navx; wishz = navz; }
	} else if (navigating) {
		wishx = navx; wishz = navz; wishYaw = atan2f(navx, -navz);
	}
	// stuck detection
	a->stuckT += dt;
	if (a->stuckT > 1.0f) {
		float mx = a->x - a->lastX, mz = a->z - a->lastZ;
		bool still = mx * mx + mz * mz < 0.5f * 0.5f;
		a->stillT = still ? a->stillT + 1.0f : 0;
		if (still && (wishx != 0 || wishz != 0)) {
			if (t) { a->strafeDir = -a->strafeDir; a->strafeT = 1.2f; }              // strafing into a wall: go the other way
			else bot_pick_waypoint(g, a);
		}
		if (a->stillT >= 4.0f) { a->target = -1; a->stillT = 0; a->hesitate = 0; bot_pick_waypoint(g, a); }   // give up on a stalemate
		a->lastX = a->x; a->lastZ = a->z; a->stuckT = 0;
	}
	// accelerate and move
	float tvx = wishx * sp, tvz = wishz * sp, k = fminf(1.0f, 12.0f * dt);
	a->vx += (tvx - a->vx) * k; a->vz += (tvz - a->vz) * k;
	move_actor(g, a, a->vx * dt, a->vz * dt);
	separate(g, id);
	a->walk += sqrtf(a->vx * a->vx + a->vz * a->vz) * dt * 1.7f;
	// turn
	float dy = wrap_pi(wishYaw - a->yaw), maxturn = (t ? 5.5f : 3.0f) * dt;
	a->yaw += clampf(dy, -maxturn, maxturn);
	if (t) {
		float dx = t->x - a->x, dz = t->z - a->z, dist = sqrtf(dx * dx + dz * dz);
		bool lined = fabsf(wrap_pi(atan2f(dx, -dz) - a->yaw)) < DEG(9);
		if (a->reactT <= 0 && lined && a->burstT <= 0 && a->fireT <= 0 && a->hitT >= 0 && see(g, a, t)) {
			if (a->burstLeft <= 0) { a->burstLeft = 3 + (int)(frand(g) * 5); }
			if (a->burstLeft > 0) {
				const WeaponDef *wd = &weapons[W_AR];
				float o[3] = { a->x, 1.5f, a->z };
				bool headshot = dist < 30 && frand(g) < 0.15f;
				float tx = t->x, ty = headshot ? 1.55f : 1.1f, tz = t->z;
				float dir[3] = { tx - o[0], ty - o[1], tz - o[2] };
				float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
				float err = (a->team == TEAM_ENEMY ? 2.4f : 2.0f) * (1.0f + dist / 18.0f);
				float ex = frange(g, -1, 1), ey = frange(g, -1, 1);
				if (ex * ex + ey * ey > 1) { ex *= 0.6f; ey *= 0.6f; }
				float yaw = atan2f(dir[0], -dir[2]) + DEG(err) * ex, pitch = asinf(dir[1] / l) + DEG(err) * 0.7f * ey;
				look_dir(yaw, pitch, dir);
				float mz[3] = { o[0] + sinf(a->yaw) * 0.7f, 1.35f, o[2] - cosf(a->yaw) * 0.7f };
				shoot_ray(g, id, o, dir, W_AR, a->team == TEAM_ENEMY ? 0.55f : 0.6f, mz);
				a->flashT = 0.06f; a->spottedT = 1.8f;
				a->fireT = wd->rate * 1.15f; a->burstLeft--;
				if (a->burstLeft <= 0) a->burstT = frange(g, 0.35f, 0.9f);
			}
		}
		if (a->grenT <= 0 && dist > 11 && dist < 28 && frand(g) < 0.03f) {              // lob a grenade
			float pp[3] = { a->x + sinf(a->yaw) * 0.6f, 1.6f, a->z - cosf(a->yaw) * 0.6f };
			float vel = sqrtf(dist * 14.0f / 0.98f);
			float vv[3] = { sinf(a->yaw) * vel * cosf(DEG(40)), vel * sinf(DEG(40)), -cosf(a->yaw) * vel * cosf(DEG(40)) };
			throw_grenade(g, id, pp, vv);
			a->grenT = frange(g, 14, 24);
		}
	}
	a->flashT -= dt; a->hitT -= dt; a->spottedT -= dt;
}

// ---- the human player ---------------------------------------------------------------------------------------------
static void player_shoot(Game *g)
{
	Player *p = &g->p; Actor *a = &g->a[0];
	const WeaponDef *wd = &weapons[p->weapon];
	p->mag[p->weapon]--; a->fireT = wd->rate; p->flashT = 0.055f;
	float pitch = a->pitch + p->kick, yaw = a->yaw + p->kickYaw;
	float spread = lerpf(wd->spread_hip, wd->spread_ads, p->ads) * (1.0f + 0.5f * p->moveFrac * (1.0f - p->ads));
	float o[3] = { a->x, p->eyeh, a->z };
	float mz[3];
	{ float f[3], r[3] = { cosf(yaw), 0, sinf(yaw) }; look_dir(yaw, pitch, f);
	  float side = lerpf(0.18f, 0.0f, p->ads), down = lerpf(0.2f, 0.09f, p->ads);
	  for (int i = 0; i < 3; i++) mz[i] = o[i] + f[i] * 1.2f + r[i] * side; mz[1] -= down; }
	int hits = 0, kills = 0;
	for (int i = 0; i < wd->pellets; i++) {
		float rx = frange(g, -1, 1), ry = frange(g, -1, 1);
		if (rx * rx + ry * ry > 1) { rx *= 0.7f; ry *= 0.7f; }
		float d[3]; look_dir(yaw + DEG(spread) * rx, pitch + DEG(spread) * ry, d);
		int r = shoot_ray(g, 0, o, d, p->weapon, 1.0f, mz);
		if (r >= 1) hits++;
		if (r == 2) kills++;
	}
	if (hits) p->hitMarkT = 0.18f;
	p->kick = clampf(p->kick + DEG(wd->kick) * frange(g, 0.7f, 1.3f) * (1.0f - 0.45f * p->ads), 0, DEG(7));
	p->kickYaw += DEG(wd->kick) * frange(g, -0.5f, 0.5f) * 0.6f;
	p->shake = clampf(p->shake + 0.05f * wd->kick, 0, 1);
	if (g->shell_n >= MAX_SHELL) g->shell_n = 0;
	Shell *s = &g->shell[g->shell_n++];
	s->p[0] = 0.07f; s->p[1] = -0.1f; s->p[2] = -0.45f;
	s->v[0] = frange(g, 1.4f, 2.2f); s->v[1] = frange(g, 0.9f, 1.6f); s->v[2] = frange(g, 0.2f, 0.7f); s->life = 0.7f; s->rot = frange(g, 0, 6);
	if (p->weapon == W_SHOTGUN) s->life = 0;
	(void)kills;
}

// ---- aim assist (aiming down sights only): when the crosshair is "close enough" to a visible enemy the look speed eases and the aim is nudged towards their chest
static float assist_target(const Game *g, float *ey, float *ep)
{
	const Actor *a = &g->a[0]; const Player *p = &g->p;
	float best = 0, bestErr = 1e9f;
	for (int i = 1; i < MAX_ACT; i++) {
		const Actor *t = &g->a[i];
		if (!t->alive || t->team == a->team) continue;
		float dx = t->x - a->x, dz = t->z - a->z, dist = sqrtf(dx * dx + dz * dz);
		if (dist < 2.0f || dist > 60.0f) continue;
		float dy = 1.15f - p->eyeh;
		float yawT = atan2f(dx, -dz), pitchT = atan2f(dy, dist);
		float y = wrap_pi(yawT - a->yaw) * cosf(a->pitch), pp = pitchT - a->pitch;
		float err = sqrtf(y * y + pp * pp);
		float cone = clampf(DEG(7.0f) - dist * DEG(0.09f), DEG(2.5f), DEG(7.0f));
		if (err >= cone || err >= bestErr) continue;
		float o[3] = { a->x, p->eyeh, a->z }, d[3] = { dx, dy, dz }, n[3];
		float l = sqrtf(dx * dx + dy * dy + dz * dz); d[0] /= l; d[1] /= l; d[2] /= l;
		if (ray_world(&g->w, o, d, l, n) < l - 0.1f) continue;                 // behind cover: no help
		bestErr = err; best = 1.0f - err / cone; *ey = wrap_pi(yawT - a->yaw); *ep = pp;
	}
	return best;
}

static void player_update(Game *g, const Input *in, float dt)
{
	Actor *a = &g->a[0]; Player *p = &g->p;
	const WeaponDef *wd = &weapons[p->weapon];
	a->fireT -= dt; p->flashT -= dt; p->hitMarkT -= dt; p->killMarkT -= dt; p->lastDmgT += dt;
	for (int i = 0; i < MAX_HITS; i++) p->dmgT[i] -= dt;
	p->shake = fmaxf(0, p->shake - dt * 2.2f);
	if (a->hitT > 0) a->hitT -= dt;
	if (a->invulnT > 0) a->invulnT -= dt;
	if (!a->alive) {
		a->deadT += dt; a->respawnT -= dt;
		p->eyeh = fmaxf(0.35f, p->eyeh - dt * 2.5f);
		p->ads = fmaxf(0, p->ads - dt * 5);
		if (a->respawnT <= 0) { spawn_actor(g, 0); }
		return;
	}
	// ---- look: circle pad (curve for precision near the centre), slower while aiming down sights
	float sens = lerpf(1.0f, wd->zoomsens, p->ads) * g->sens;
	float lx = in->lx, ly = in->ly;
	float aey = 0, aep = 0, assist = 0;
	if (g->aimAssist && p->ads > 0.4f) { assist = assist_target(g, &aey, &aep) * p->ads; sens *= 1.0f - 0.35f * assist; }
	float vx = (lx < 0 ? -1.0f : 1.0f) * powf(fabsf(lx), 1.5f), vy = (ly < 0 ? -1.0f : 1.0f) * powf(fabsf(ly), 1.5f);
	a->yaw += vx * 3.3f * sens * dt;
	a->pitch = clampf(a->pitch + (g->invertY ? -vy : vy) * 2.3f * sens * dt, -DEG(80), DEG(80));
	if (assist > 0) {                                                 // gentle pull towards the target, weaker the harder you push the stick
		float stick = fminf(1.0f, sqrtf(lx * lx + ly * ly)), pull = fminf(1.0f, 3.0f * assist * (1.0f - 0.7f * stick) * dt);
		a->yaw += aey * pull; a->pitch = clampf(a->pitch + aep * pull, -DEG(80), DEG(80));
	}
	if (a->yaw > PI_F) a->yaw -= 2 * PI_F; if (a->yaw < -PI_F) a->yaw += 2 * PI_F;
	p->lookVel[0] += (vx * sens - p->lookVel[0]) * fminf(1, 10 * dt); p->lookVel[1] += (vy * sens - p->lookVel[1]) * fminf(1, 10 * dt);

	// ---- ads
	bool canAds = in->aim && p->healT <= 0 && p->throwT <= 0 && p->swapT <= 0;
	p->ads = clampf(p->ads + (canAds ? dt / 0.16f : -dt / 0.12f), 0, 1);

	// ---- move (X forward, B back, Y left, A right)
	float fw = (in->fwd ? 1.0f : 0) - (in->back ? 1.0f : 0), st = (in->right ? 1.0f : 0) - (in->left ? 1.0f : 0);
	float mag = sqrtf(fw * fw + st * st); if (mag > 1) { fw /= mag; st /= mag; }
	// double-tap X to sprint; keep holding X. Aiming, firing, throwing, healing or letting go of X ends it
	p->tapT -= dt;
	if (in->fwd && !p->fwdPrev) { if (p->tapT > 0 && p->ads < 0.3f) p->sprinting = true; p->tapT = 0.3f; }
	p->fwdPrev = in->fwd;
	if (!in->fwd || in->aim || in->fire || p->healT > 0 || p->throwT > 0 || in->back) p->sprinting = false;
	p->sprint = clampf(p->sprint + (p->sprinting ? dt / 0.18f : -dt / 0.15f), 0, 1);
	float speed = lerpf(4.7f, 2.4f, p->ads); if (p->healT > 0) speed *= 0.8f;
	if (p->sprinting) { speed = 7.4f; fw = 1.0f; st *= 0.3f; }
	float fx = sinf(a->yaw), fz = -cosf(a->yaw), rx = cosf(a->yaw), rz = sinf(a->yaw);
	float tvx = (fx * fw + rx * st) * speed, tvz = (fz * fw + rz * st) * speed, k = fminf(1.0f, 14.0f * dt);
	a->vx += (tvx - a->vx) * k; a->vz += (tvz - a->vz) * k;
	move_actor(g, a, a->vx * dt, a->vz * dt);
	separate(g, 0);
	float sp = sqrtf(a->vx * a->vx + a->vz * a->vz);
	p->moveFrac = clampf(sp / 4.7f, 0, 1.4f);
	p->bob += sp * dt * 1.9f; a->walk = p->bob;

	// ---- weapon swap (d-pad left/right), reload (select / empty), heal (d-pad down), grenade (d-pad up)
	if (p->swapT > 0) {
		p->swapT -= dt;
		if (p->swapT < 0.16f && p->weapon != p->nextWeapon) { p->weapon = p->nextWeapon; p->reloading = false; wd = &weapons[p->weapon]; }
	} else if (in->next_p || in->prev_p) {
		p->nextWeapon = (p->weapon + (in->next_p ? 1 : W_COUNT - 1)) % W_COUNT;
		p->swapT = 0.34f; p->reloading = false; p->reloadT = 0;
	}
	if (p->throwT > 0) p->throwT -= dt;
	if (in->grenade_p && p->grenades > 0 && p->throwT <= 0 && p->healT <= 0) {
		p->grenades--; p->throwT = 0.55f; p->reloading = false;
		float pitch = a->pitch + 0.12f, d[3]; look_dir(a->yaw, pitch, d);
		float pp[3] = { a->x + d[0] * 0.5f, p->eyeh - 0.15f, a->z + d[2] * 0.5f };
		float vv[3] = { d[0] * 12.5f + a->vx * 0.6f, d[1] * 12.5f + 3.0f, d[2] * 12.5f + a->vz * 0.6f };
		throw_grenade(g, 0, pp, vv);
	}
	if (p->healT > 0) {
		float step = p->healLeft * fminf(1.0f, dt / p->healT);
		a->hp = fminf(100.0f, a->hp + step); p->healLeft -= step; p->healT -= dt;
		if (p->healT <= 0) p->healLeft = 0;
	} else if (in->heal_p && p->medkits > 0 && a->hp < 100.0f && p->throwT <= 0) {
		p->medkits--; p->healT = 1.5f; p->healLeft = 70.0f; p->reloading = false;
		add_popup(g, "HEALING", 0xFF70E070);
	}
	if (p->reloading) {
		p->reloadT -= dt;
		if (p->reloadT <= 0) {
			int need = wd->mag - p->mag[p->weapon], take = need < p->reserve[p->weapon] ? need : p->reserve[p->weapon];
			p->mag[p->weapon] += take; p->reserve[p->weapon] -= take; p->reloading = false;
		}
	} else if ((in->reload_p || p->mag[p->weapon] == 0) && p->mag[p->weapon] < wd->mag && p->reserve[p->weapon] > 0 && p->swapT <= 0 && p->healT <= 0 && p->throwT <= 0) {
		p->reloading = true; p->reloadT = wd->reload;
	}

	// ---- fire (R)
	if (in->fire && !p->fireHeld) p->fireBuf = 0.25f;                // semi-auto weapons remember a press that came a moment early
	p->fireHeld = in->fire;
	p->fireBuf -= dt;
	bool trigger = wd->automatic ? in->fire : p->fireBuf > 0;
	if (trigger && p->sprint < 0.3f && !p->reloading && p->swapT <= 0 && p->healT <= 0 && p->throwT <= 0 && a->fireT <= 0 && p->mag[p->weapon] > 0) { player_shoot(g); p->fireBuf = 0; }
	p->kick -= p->kick * fminf(1.0f, 7.0f * dt);
	p->kickYaw -= p->kickYaw * fminf(1.0f, 6.0f * dt);

	// ---- ejected shells (view space)
	for (int i = 0; i < MAX_SHELL; i++) {
		Shell *s = &g->shell[i];
		if (s->life <= 0) continue;
		s->life -= dt; s->v[1] -= 6.0f * dt; s->rot += dt * 18;
		for (int q = 0; q < 3; q++) s->p[q] += s->v[q] * dt;
	}

	// ---- pickups
	for (int i = 0; i < g->w.npick; i++) {
		Pickup *pk = &g->w.pick[i];
		if (pk->respawn > 0) { pk->respawn -= dt; continue; }
		float dx = pk->x - a->x, dz = pk->z - a->z;
		if (dx * dx + dz * dz > 1.3f * 1.3f) continue;
		if (pk->type == 0) {
			bool need = false;
			for (int w = 0; w < W_COUNT; w++) if (p->reserve[w] < weapons[w].reserve) need = true;
			if (!need) continue;
			for (int w = 0; w < W_COUNT; w++) p->reserve[w] = (int)fminf((float)weapons[w].reserve, (float)(p->reserve[w] + weapons[w].mag * 2));
			add_popup(g, "AMMO", 0xFFFFE080);
		} else if (pk->type == 1) {
			if (p->medkits >= 3) continue;
			p->medkits++; add_popup(g, "MEDKIT", 0xFF70E070);
		} else {
			if (p->grenades >= 4) continue;
			p->grenades = p->grenades + 2 > 4 ? 4 : p->grenades + 2; add_popup(g, "GRENADES", 0xFFE0E0E0);
		}
		pk->respawn = 25.0f;
	}
}

// ---- main update ------------------------------------------------------------------------------------------------
static void update_fx(Game *g, float dt)
{
	for (int i = 0; i < g->fx_n; ) {
		Fx *f = &g->fx[i];
		f->life -= dt;
		if (f->life <= 0) { *f = g->fx[--g->fx_n]; continue; }
		for (int k = 0; k < 3; k++) f->p[k] += f->v[k] * dt;
		if (f->kind == 2) { f->v[1] -= 12.0f * dt; if (f->p[1] < 0.02f) { f->p[1] = 0.02f; f->v[1] = 0; f->v[0] *= 0.5f; f->v[2] *= 0.5f; } }
		else if (f->kind == 1) { f->v[0] *= 1.0f - 1.8f * dt; f->v[1] = f->v[1] * (1.0f - 1.2f * dt) + 0.3f * dt; f->v[2] *= 1.0f - 1.8f * dt; }
		else { f->v[0] *= 1.0f - 2.0f * dt; f->v[1] *= 1.0f - 2.0f * dt; f->v[2] *= 1.0f - 2.0f * dt; }
		f->size += f->grow * dt;
		if (f->size < 0.01f) f->size = 0.01f;
		i++;
	}
	for (int i = 0; i < MAX_TRACER; i++) g->tr[i].life -= dt;
	for (int i = 0; i < MAX_DECAL; i++) g->dec[i].life -= dt;
	for (int i = 0; i < MAX_FEED; i++) g->feed[i].t -= dt;
	for (int i = 0; i < 3; i++) g->pop[i].t -= dt;
}

static void update_camera(Game *g)
{
	const Actor *a = &g->a[0]; const Player *p = &g->p;
	const WeaponDef *wd = &weapons[p->weapon];
	float bobv = sinf(p->bob) * 0.035f * p->moveFrac * (1.0f - p->ads * 0.8f);
	float bobh = cosf(p->bob * 0.5f) * 0.02f * p->moveFrac * (1.0f - p->ads * 0.8f);
	float sh = p->shake * p->shake;
	float t = g->time * 47.0f;
	g->cam.eye[0] = a->x + cosf(a->yaw) * bobh; g->cam.eye[2] = a->z + sinf(a->yaw) * bobh;
	g->cam.eye[1] = p->eyeh + bobv + sinf(t * 1.3f) * 0.03f * sh;
	g->cam.yaw = a->yaw + p->kickYaw + sinf(t) * 0.012f * sh;
	g->cam.pitch = clampf(a->pitch + p->kick + cosf(t * 1.1f) * 0.012f * sh, -DEG(85), DEG(85));
	float roll = 0;
	if (!a->alive) roll = clampf(a->deadT * 1.2f, 0, 1) * DEG(55);
	else roll = -p->lookVel[0] * DEG(1.2f);
	g->cam.roll = roll;
	g->cam.fov = lerpf(g->fovHip, wd->ads_fov * (g->fovHip / 58.0f), p->ads) + p->sprint * 5.0f;
}

void game_update(Game *g, const Input *in, float dt)
{
	if (dt > 0.05f) dt = 0.05f;
	if (g->state == GS_TITLE) {
		g->time += dt; g->rng ^= (unsigned)(g->time * 1000);
		if (in->a_p) { game_new_match(g); g->state = GS_PLAY; }
		g->cam.eye[0] = sinf(g->time * 0.12f) * 6; g->cam.eye[1] = 5.0f; g->cam.eye[2] = 14.5f;
		g->cam.yaw = sinf(g->time * 0.12f + 1.0f) * 0.45f; g->cam.pitch = -0.12f; g->cam.roll = 0; g->cam.fov = g->fovHip;
		g->p.ads = 0;
		return;
	}
	if (in->start_p && g->state == GS_PLAY) g->paused = !g->paused;
	if (g->paused) {
		if (in->a_p) { g->paused = false; g->saveNeeded = true; }
		if (in->b_p) g->quit = true;
		if (in->next_p) g->sens = fminf(2.0f, g->sens + 0.1f);
		if (in->prev_p) g->sens = fmaxf(0.4f, g->sens - 0.1f);
		if (in->x_p) g->invertY = !g->invertY;
		if (in->y_p) g->aimAssist = !g->aimAssist;
		if (in->r_p) { g->quality = !g->quality; g->saveNeeded = true; }
		if (in->grenade_p) { g->noHaze = !g->noHaze; g->saveNeeded = true; }
		if (in->l_p) { g->showFps = !g->showFps; g->saveNeeded = true; }
		return;
	}
	if (g->state == GS_OVER) {
		g->overT += dt;
		Input none; memset(&none, 0, sizeof none);
		update_fx(g, dt); grenades_update(g, dt);
		for (int i = 1; i < MAX_ACT; i++) if (g->a[i].alive) { g->a[i].vx *= 0.9f; g->a[i].vz *= 0.9f; }
		update_camera(g);
		if (g->overT > 1.5f && in->a_p) { game_new_match(g); g->state = GS_PLAY; }
		return;
	}

	g->time += dt;
	g->timeLeft -= dt;
	if (g->autoplay) { Actor *p0 = &g->a[0]; if (p0->alive) { bot_update(g, 0, dt); if (p0->invulnT > 0) p0->invulnT -= dt; } else { p0->deadT += dt; p0->respawnT -= dt; if (p0->respawnT <= 0) spawn_actor(g, 0); } }
	else player_update(g, in, dt);
	for (int i = 1; i < MAX_ACT; i++) {
		Actor *a = &g->a[i];
		if (a->alive && a->frozen) { if (a->invulnT > 0) a->invulnT -= dt; }
		else if (a->alive) { bot_update(g, i, dt); if (a->invulnT > 0) a->invulnT -= dt; }
		else {
			a->deadT += dt; a->respawnT -= dt;
			if (a->respawnT <= 0) spawn_actor(g, i);
		}
	}
	for (int i = 0; i < MAX_ACT; i++) if (g->a[i].spottedT > 0 && i == 0) g->a[i].spottedT -= dt;
	grenades_update(g, dt);
	update_fx(g, dt);
	// player muzzle light/smoke
	if (g->p.flashT > 0.04f) {
		float f[3]; look_dir(g->cam.yaw, g->cam.pitch, f);
		float pp[3] = { g->cam.eye[0] + f[0] * 0.9f, g->cam.eye[1] - 0.12f + f[1] * 0.9f, g->cam.eye[2] + f[2] * 0.9f };
		(void)pp;
	}
	update_camera(g);
	if (g->score[0] >= KILLS_TO_WIN || g->score[1] >= KILLS_TO_WIN || g->timeLeft <= 0) {
		g->state = GS_OVER; g->overT = 0;
		g->winner = g->score[0] > g->score[1] ? 0 : g->score[1] > g->score[0] ? 1 : -1;
		if (g->a[0].kills > g->bestKills) g->bestKills = g->a[0].kills;
		g->saveNeeded = true;
	}
}
