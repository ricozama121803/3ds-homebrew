#include "game.h"
#include "gen_map.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void cars_spawn_logic(Game *g, float dt);
void player_drive_input(Game *g, const Input *in);

// ---------------------------------------------------------------- helpers
unsigned g_rnd(Game *g)
{
	unsigned x = g->rng;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return g->rng = x;
}
float g_rndf(Game *g, float a, float b) { return a + (b - a) * ((g_rnd(g) & 0xFFFF) / 65535.0f); }
int g_rndi(Game *g, int n) { return n <= 0 ? 0 : (int)(g_rnd(g) % (unsigned)n); }

int game_zone_at(int tx, int ty)
{
	int z = 0;
	for (int i = 0; i < NZONES; i++)
		if (tx >= zones[i].x0 && tx <= zones[i].x1 && ty >= zones[i].y0 && ty <= zones[i].y1) z = i;   // later entries win
	return z;
}

void popup(Game *g, const char *text, unsigned color)
{
	// newest at the top: shift the older ones down
	for (int i = MAX_POPUPS - 1; i > 0; i--) g->popups[i] = g->popups[i - 1];
	snprintf(g->popups[0].text, sizeof g->popups[0].text, "%s", text);
	g->popups[0].color = color; g->popups[0].t = 2.6f;
}

bool los_clear(const Game *g, float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
	int n = (int)(d / 6.0f);
	for (int i = 1; i < n; i++) {
		float t = (float)i / n;
		if (world_flags(&g->world, (int)floorf((x0 + dx * t) / TILE), (int)floorf((y0 + dy * t) / TILE)) & F_SOLID) return false;
	}
	return true;
}

static int stars_for_heat(float h) { return h >= 15 ? 5 : h >= 10 ? 4 : h >= 6 ? 3 : h >= 3 ? 2 : h >= 1 ? 1 : 0; }

void add_heat(Game *g, float amount)
{
	if (g->p.status != PL_ALIVE) return;
	int before = g->stars;
	g->heat = fminf(g->heat + amount, 24.0f);
	g->stars = stars_for_heat(g->heat);
	g->unseenT = 0;
	if (g->stars > before) {
		char b[24]; snprintf(b, sizeof b, "Wanted level %d", g->stars);
		popup(g, b, 0xFF6060FF);
	}
}

static void clear_cops(Game *g)
{
	for (int i = 0; i < MAX_PEDS; i++) if (g->peds[i].active && g->peds[i].kind != PK_CIV) g->peds[i].active = false;
	for (int i = 0; i < MAX_CARS; i++) if (g->cars[i].active && g->cars[i].state == CS_CHASE) car_remove(g, i);
	for (int i = 0; i < MAX_BULLETS; i++) g->bullets[i].active = false;
}

static void respawn_at(Game *g, int tx, int ty)
{
	Player *p = &g->p;
	p->x = tx * (float)TILE + 8.0f; p->y = ty * (float)TILE + 8.0f;
	p->vx = p->vy = 0; p->hp = 100.0f; p->armor = 0; p->status = PL_ALIVE; p->statusT = 0;
	p->car = -1; p->bustT = 0; p->hurtT = 0; p->heading = PI_F;
	g->heat = 0; g->stars = 0; g->unseenT = 0;
	clear_cops(g);
	g->camx = clampf(p->x - SCREEN_W * 0.5f, 0, g->world.w * TILE - SCREEN_W);
	g->camy = clampf(p->y - SCREEN_H * 0.5f, 0, g->world.h * TILE - SCREEN_H);
}

void player_wasted(Game *g)
{
	Player *p = &g->p;
	if (p->status != PL_ALIVE) return;
	if (p->car >= 0) { Car *c = &g->cars[p->car]; c->state = CS_PARKED; c->driver = DRV_NONE; p->car = -1; }
	p->status = PL_DEAD; p->statusT = 0;
	fx_blood(g, p->x, p->y, 0, 1, 12);
	fx_stain(g, p->x, p->y, 3);
}

void player_busted(Game *g)
{
	Player *p = &g->p;
	if (p->status != PL_ALIVE) return;
	if (p->car >= 0) { Car *c = &g->cars[p->car]; c->state = CS_PARKED; c->driver = DRV_NONE; p->car = -1; }
	p->status = PL_BUSTED; p->statusT = 0;
}

// ---------------------------------------------------------------- setup
void game_save_data(const Game *g, SaveData *out)
{
	out->cash = g->p.cash;
	for (int i = 0; i < W_COUNT; i++) { out->has[i] = g->p.has[i]; out->ammo[i] = g->p.ammo[i]; }
}

bool game_init(Game *g, const uint8_t *map, size_t size, const SaveData *save)
{
	memset(g, 0, sizeof *g);
	if (!world_init(&g->world, map, size)) return false;
	g->rng = 0x9E3779B9u ^ (unsigned)size;
	roads_init();
	Player *p = &g->p;
	p->x = g->world.spawnx * (float)TILE + 8.0f; p->y = g->world.spawny * (float)TILE + 8.0f;
	p->heading = PI_F; p->hp = 100.0f; p->car = -1; p->status = PL_ALIVE;
	p->has[W_FISTS] = true; p->has[W_PISTOL] = true; p->ammo[W_PISTOL] = 30; p->weapon = W_PISTOL;
	if (save) {
		p->cash = save->cash;
		for (int i = 0; i < W_COUNT; i++) { if (save->has[i]) { p->has[i] = true; p->ammo[i] = save->ammo[i]; } }
		if (p->ammo[W_PISTOL] < 30) p->ammo[W_PISTOL] = 30;
	}
	for (int i = 0; i < MAX_PARK; i++) g->park_car[i] = -1;
	for (int i = 0; i < NPICKUPS && i < MAX_PICKUPS; i++) {
		Pickup *k = &g->pickups[i];
		k->active = true; k->x = pickup_spots[i].tx * (float)TILE + 8.0f; k->y = pickup_spots[i].ty * (float)TILE + 8.0f;
		k->kind = pickup_spots[i].kind; k->amount = 100 + g_rndi(g, 4) * 100;
	}
	g->zone = game_zone_at(g->world.spawnx, g->world.spawny);
	g->zoneT = 3.5f;
	g->camx = clampf(p->x - SCREEN_W * 0.5f, 0, g->world.w * TILE - SCREEN_W);
	g->camy = clampf(p->y - SCREEN_H * 0.5f, 0, g->world.h * TILE - SCREEN_H);
	g->spawnCarT = 0.2f;
	return true;
}

// ---------------------------------------------------------------- the player on foot
static void player_foot(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	float mx = in->mx, my = in->my, mag = sqrtf(mx * mx + my * my);
	if (mag > 1.0f) { mx /= mag; my /= mag; mag = 1.0f; }
	float top = in->run ? RUN_SPEED : WALK_SPEED;
	float k = fminf(1.0f, dt * 14.0f);
	p->vx += (mx * top - p->vx) * k;
	p->vy += (my * top - p->vy) * k;

	float bx = p->x, by = p->y;
	world_move(&g->world, &p->x, &p->y, p->vx * dt, p->vy * dt, PLAYER_HALF);
	if (fabsf(p->x - bx) < fabsf(p->vx * dt) * 0.5f) p->vx *= 0.5f;
	if (fabsf(p->y - by) < fabsf(p->vy * dt) * 0.5f) p->vy *= 0.5f;

	// cars are solid for people on foot
	for (int i = 0; i < MAX_CARS; i++) {
		const Car *c = &g->cars[i];
		if (!c->active) continue;
		float dx = p->x - c->x, dy = p->y - c->y;
		float lim = car_defs[c->model].len * 0.5f + PLAYER_HALF + 2.0f;
		if (dx * dx + dy * dy > lim * lim) continue;
		float cx[3], cy[3], r;
		car_circles(c, cx, cy, &r);
		for (int m = 0; m < 3; m++) {
			float ddx = p->x - cx[m], ddy = p->y - cy[m], d = sqrtf(ddx * ddx + ddy * ddy), need = r + PLAYER_HALF - 1.0f;
			if (d < need && d > 0.001f) {
				float nx = p->x + ddx / d * (need - d), ny = p->y + ddy / d * (need - d);
				if (!world_blocked(&g->world, nx, ny, PLAYER_HALF)) { p->x = nx; p->y = ny; }
			}
		}
	}

	float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
	p->moving = speed > 8.0f;
	if (mag > 0.15f && !(in->a && p->weapon != W_FISTS && false)) {
		float want = atan2f(mx, -my);
		p->heading = wrap_pi(p->heading + wrap_pi(want - p->heading) * fminf(1.0f, dt * 16.0f));
	}
	if (p->moving) p->walkT += speed * dt * 0.1f;
}

static void pickups_update(Game *g, float dt)
{
	Player *p = &g->p;
	for (int i = 0; i < MAX_PICKUPS; i++) {
		Pickup *k = &g->pickups[i];
		if (!k->active) {
			if (!k->temp && k->respawnT > 0) { k->respawnT -= dt; if (k->respawnT <= 0) k->active = true; }
			continue;
		}
		if (k->temp) { k->lifeT -= dt; if (k->lifeT <= 0) { k->active = false; continue; } }
		if (p->status != PL_ALIVE || p->car >= 0) continue;
		float dx = k->x - p->x, dy = k->y - p->y;
		if (dx * dx + dy * dy > 10.0f * 10.0f) continue;
		char b[28];
		switch (k->kind) {
		case 0: if (p->hp >= 100) continue; p->hp = fminf(100.0f, p->hp + 50.0f); popup(g, "Health", 0xFF60FF60); break;
		case 1: if (p->armor >= 100) continue; p->armor = fminf(100.0f, p->armor + 50.0f); popup(g, "Armor", 0xFFFFA060); break;
		case 2: case 3: case 4: {
			int w = k->kind - 1, add = w == W_PISTOL ? 30 : w == W_SMG ? 80 : 14;
			p->has[w] = true; p->ammo[w] = (int)fminf((float)weapon_defs[w].maxammo, (float)(p->ammo[w] + add));
			snprintf(b, sizeof b, "%s +%d", weapon_defs[w].name, add); popup(g, b, 0xFFFFFFFF);
			if (!(p->ammo[p->weapon] > 0 && p->weapon != W_FISTS)) p->weapon = w;
			g->saveNeeded = true;
			break;
		}
		case 5: p->cash += k->amount; snprintf(b, sizeof b, "+$%d", k->amount); popup(g, b, 0xFF60E060); g->saveNeeded = true; break;
		}
		k->active = false;
		k->respawnT = k->temp ? 0 : 50.0f;
	}
}

// ---------------------------------------------------------------- police, heat and Pay 'n' Spray
static void wanted_update(Game *g, float dt)
{
	Player *p = &g->p;
	float px = p->car >= 0 ? g->cars[p->car].x : p->x, py = p->car >= 0 ? g->cars[p->car].y : p->y;

	bool seen = false;
	for (int i = 0; i < MAX_PEDS && !seen; i++) {
		const Ped *q = &g->peds[i];
		if (!q->active || q->kind == PK_CIV || q->state == PS_DEAD) continue;
		float dx = q->x - px, dy = q->y - py;
		if (dx * dx + dy * dy < 260.0f * 260.0f && los_clear(g, q->x, q->y, px, py)) seen = true;
	}
	for (int i = 0; i < MAX_CARS && !seen; i++) {
		const Car *c = &g->cars[i];
		if (!c->active || c->state != CS_CHASE) continue;
		float dx = c->x - px, dy = c->y - py;
		if (dx * dx + dy * dy < 260.0f * 260.0f && los_clear(g, c->x, c->y, px, py)) seen = true;
	}
	g->cops_see = seen;
	if (g->stars > 0 && p->status == PL_ALIVE) {
		if (seen) g->unseenT = 0; else g->unseenT += dt;
		if (g->unseenT > 5.0f) {
			int before = g->stars;
			g->heat = fmaxf(0, g->heat - 0.30f * dt);
			g->stars = stars_for_heat(g->heat);
			if (g->stars == 0 && before > 0) popup(g, "Wanted level lost", 0xFF80FF80);
		}
	}

	// busted: a cop catches you while you're slow
	bool caught = false;
	if (g->stars > 0 && p->status == PL_ALIVE) {
		float speed = p->car >= 0 ? sqrtf(g->cars[p->car].vx * g->cars[p->car].vx + g->cars[p->car].vy * g->cars[p->car].vy) : sqrtf(p->vx * p->vx + p->vy * p->vy);
		for (int i = 0; i < MAX_PEDS; i++) {
			const Ped *q = &g->peds[i];
			if (!q->active || q->kind == PK_CIV || q->state == PS_DEAD) continue;
			float dx = q->x - px, dy = q->y - py, lim = p->car >= 0 ? 22.0f : 13.0f;
			if (dx * dx + dy * dy < lim * lim && speed < (p->car >= 0 ? 25.0f : 50.0f)) { caught = true; break; }
		}
	}
	if (caught) { p->bustT += dt; if (p->bustT > 1.5f) player_busted(g); }
	else p->bustT = fmaxf(0, p->bustT - dt * 2.0f);

	// Pay 'n' Spray: sit on the pad in a car
	bool on_pad = false;
	if (p->car >= 0 && p->status == PL_ALIVE) {
		const Car *c = &g->cars[p->car];
		int tx = (int)floorf(c->x / TILE), ty = (int)floorf(c->y / TILE);
		if (world_flags(&g->world, tx, ty) & F_SPRAY) on_pad = true;
	}
	if (on_pad) {
		p->sprayT += dt;
		if (p->sprayT > 1.6f) {
			Car *c = &g->cars[p->car];
			c->hp = car_defs[c->model].hp;
			int model = c->model;                                                    // new paint job: pick another variant of the same model
			for (int v = 0; v < NCARVARS; v++) if (carvar_model[v] == model && v != c->variant) { c->variant = v; break; }
			if (g->stars > 0) popup(g, "Wanted level cleared!", 0xFF80FF80); else popup(g, "Freshly sprayed", 0xFFE0A0FF);
			g->heat = 0; g->stars = 0; g->unseenT = 0; p->sprayT = -6.0f;             // cooldown so it doesn't repeat immediately
			clear_cops(g);
		}
	} else if (p->sprayT > 0) p->sprayT = 0;
	else if (p->sprayT < 0) p->sprayT = fminf(0, p->sprayT + dt);
}

static void police_spawning(Game *g, float dt)
{
	g->spawnCopT -= dt;
	g->spawnPedT -= dt;
	int civ = 0, foot = 0, swat = 0;
	for (int i = 0; i < MAX_PEDS; i++) {
		const Ped *q = &g->peds[i];
		if (!q->active || q->state == PS_DEAD) continue;
		if (q->kind == PK_CIV) civ++; else if (q->kind == PK_COP) foot++; else swat++;
	}
	if (g->spawnPedT <= 0) { g->spawnPedT = 0.5f; if (civ < 20) spawn_civilian(g); }
	if (g->stars > 0 && g->p.status == PL_ALIVE && g->spawnCopT <= 0) {
		static const int foot_target[6] = {0, 3, 4, 4, 5, 6};
		if (foot < foot_target[g->stars]) { spawn_cop_foot(g, false); g->spawnCopT = 1.2f; }
		else if (g->stars >= 4 && swat < 2) { spawn_cop_foot(g, true); g->spawnCopT = 2.0f; }
	}
}

// keep the flow fields fresh while there is a chase
static void fields_update(Game *g, float dt)
{
	if (g->stars == 0) return;
	g->fieldT -= dt;
	int tx = (int)floorf(g->p.x / TILE), ty = (int)floorf(g->p.y / TILE);
	if (g->p.car >= 0) { tx = (int)floorf(g->cars[g->p.car].x / TILE); ty = (int)floorf(g->cars[g->p.car].y / TILE); }
	if (g->fieldT <= 0 && !g->foot_field.building && !g->car_field.building) {
		field_begin(&g->foot_field, &g->world, NAV_FOOT, tx, ty, 70);
		field_begin(&g->car_field, &g->world, NAV_CAR, tx, ty, 120);
		g->fieldT = 0.35f;
	}
	field_step(&g->foot_field, &g->world, 500);
	field_step(&g->car_field, &g->world, 700);
}

// ---------------------------------------------------------------- HUD buttons on the touch screen (layout shared with render.c)
static bool hit_weapon_button(float tx, float ty, int *w)
{
	static const float bx[4] = {238, 278, 238, 278}, by[4] = {96, 96, 126, 126};
	for (int i = 0; i < 4; i++)
		if (tx >= bx[i] && tx < bx[i] + 38 && ty >= by[i] && ty < by[i] + 26) { *w = i; return true; }
	return false;
}

// ---------------------------------------------------------------- frame update
static void step(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	if (p->status == PL_ALIVE) {
		if (p->car >= 0) {
			player_drive_input(g, in);
			if (in->y_p) player_use(g);
		} else {
			player_foot(g, in, dt);
			if (in->y_p) player_use(g);
			player_fire(g, in, dt);
		}
	} else {
		p->statusT += dt;
		if (p->statusT > 3.2f) {
			if (p->status == PL_DEAD) {
				p->cash -= p->cash / 10;
				respawn_at(g, HOSPITAL_X, HOSPITAL_Y);
				popup(g, "Patched up at the hospital", 0xFFC0C0C0);
			} else {
				p->cash -= p->cash / 5;
				for (int i = 0; i < W_COUNT; i++) { if (i != W_FISTS) { p->has[i] = false; p->ammo[i] = 0; } }
				p->has[W_PISTOL] = true; p->ammo[W_PISTOL] = 12; p->weapon = W_FISTS;
				respawn_at(g, POLICE_X, POLICE_Y);
				popup(g, "Released. They kept your guns.", 0xFFC0C0C0);
			}
			g->saveNeeded = true;
		}
	}

	cars_update(g, dt);
	peds_update(g, dt);
	bullets_update(g, dt);
	particles_update(g, dt);
	pickups_update(g, dt);
	wanted_update(g, dt);
	fields_update(g, dt);
	police_spawning(g, dt);
	cars_spawn_logic(g, dt);
}

void game_update(Game *g, const Input *in, float dt)
{
	if (in->start_p) g->paused = !g->paused;
	if (g->paused) { if (in->select_p) g->quit = true; return; }
	if (dt > 0.05f) dt = 0.05f;
	g->time += dt;

	Player *p = &g->p;
	if (p->status == PL_ALIVE) {                                   // weapon selection
		if (p->car < 0) {
			int dir = in->x_p ? 1 : in->l_p ? -1 : 0;
			if (dir) {
				for (int k = 1; k <= W_COUNT; k++) {
					int w = ((p->weapon + dir * k) % W_COUNT + W_COUNT) % W_COUNT;
					if (p->has[w]) { p->weapon = w; break; }
				}
			}
			int w;
			if (in->tap && hit_weapon_button(in->tx, in->ty, &w) && p->has[w]) p->weapon = w;
		}
	}

	int n = (int)ceilf(dt / (1.0f / 60.0f)); if (n < 1) n = 1;
	for (int s = 0; s < n; s++) {
		Input sub = *in;
		if (s > 0) { sub.y_p = sub.x_p = sub.l_p = sub.tap = false; }          // one-shot presses only count once
		step(g, &sub, dt / n);
	}

	// popups, flashes, banner
	for (int i = 0; i < MAX_POPUPS; i++) if (g->popups[i].t > 0) g->popups[i].t -= dt;
	g->flashT = fmaxf(0, g->flashT - dt);
	if (p->hurtT > 0) p->hurtT -= dt;
	int tx = (int)floorf(p->x / TILE), ty = (int)floorf(p->y / TILE);
	int z = game_zone_at(tx, ty);
	if (z != g->zone) { g->zone = z; g->zoneT = 3.0f; }
	if (g->zoneT > 0) g->zoneT -= dt;

	// camera: follow, and look further ahead when driving fast
	float fx = p->x, fy = p->y, lvx = p->vx, lvy = p->vy, look = 0.4f;
	if (p->car >= 0) { const Car *c = &g->cars[p->car]; fx = c->x; fy = c->y; lvx = c->vx; lvy = c->vy; look = 0.55f; }
	float maxx = g->world.w * TILE - SCREEN_W, maxy = g->world.h * TILE - SCREEN_H;
	float tcx = clampf(fx + lvx * look - SCREEN_W * 0.5f, 0, maxx), tcy = clampf(fy + lvy * look - SCREEN_H * 0.5f, 0, maxy);
	float c = fminf(1.0f, dt * (p->car >= 0 ? 7.0f : 6.0f));
	g->camx += (tcx - g->camx) * c;
	g->camy += (tcy - g->camy) * c;

	g->saveT += dt;
}
