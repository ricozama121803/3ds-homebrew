// Grand Theft Otto - pedestrians and police on foot
#include "game.h"
#include "gen_atlas.h"
#include <math.h>
#include <string.h>

bool ped_tile_ok(const World *w, int tx, int ty)
{
	int id = world_tile(w, tx, ty);
	if (id < 0) return false;
	int fl = tile_flags[id];
	if (fl & (F_SOLID | F_WATER)) return false;
	if ((fl & F_ROAD) && id != TILE_CW_V && id != TILE_CW_H) return false;      // people stay off the street except at crosswalks
	return true;
}

int ped_spawn(Game *g, PedKind kind, int type, float x, float y)
{
	for (int i = 0; i < MAX_PEDS; i++) {
		Ped *p = &g->peds[i];
		if (p->active) continue;
		memset(p, 0, sizeof *p);
		p->active = true; p->kind = kind; p->type = type;
		p->state = kind == PK_CIV ? PS_WALK : PS_ATTACK;
		p->x = x; p->y = y;
		p->heading = g_rndf(g, -PI_F, PI_F);
		p->hp = kind == PK_CIV ? 40.0f : kind == PK_COP ? 70.0f : 115.0f;
		p->turnT = g_rndf(g, 0.5f, 3.0f);
		p->walkT = g_rndf(g, 0, 6);
		return i;
	}
	return -1;
}

void ped_remove(Game *g, int i) { g->peds[i].active = false; }

static bool ahead_ok(const Game *g, const Ped *p, float heading, float dist)
{
	float x = p->x + sinf(heading) * dist, y = p->y - cosf(heading) * dist;
	int tx = (int)floorf(x / TILE), ty = (int)floorf(y / TILE);
	return p->kind != PK_CIV ? !(world_flags(&g->world, tx, ty) & F_SOLID) : ped_tile_ok(&g->world, tx, ty);
}

static void pick_heading(Game *g, Ped *p)
{
	for (int k = 0; k < 8; k++) {
		float h = k == 0 ? p->heading + g_rndf(g, -0.7f, 0.7f) : g_rndf(g, -PI_F, PI_F);
		if (ahead_ok(g, p, h, 12.0f)) { p->heading = wrap_pi(h); return; }
	}
}

static void ped_step(Game *g, Ped *p, float speed, float dt)
{
	float dx = sinf(p->heading) * speed * dt, dy = -cosf(p->heading) * speed * dt;
	world_move(&g->world, &p->x, &p->y, dx, dy, 4.0f);
	p->walkT += speed * dt * 0.1f;
}

// the police weapons (people shoot with these, the player has weapon_defs)
static const WeaponDef cop_pistol = {"Cop pistol", 7.0f, 0.95f, 0.10f, 380.0f, 0.45f, 1, 0};
static const WeaponDef swat_smg   = {"SWAT SMG",   5.0f, 0.17f, 0.15f, 400.0f, 0.45f, 1, 0};

static void cop_ai(Game *g, Ped *p, float dt)
{
	Player *pl = &g->p;
	float tx = pl->car >= 0 ? g->cars[pl->car].x : pl->x, ty = pl->car >= 0 ? g->cars[pl->car].y : pl->y;
	float dx = tx - p->x, dy = ty - p->y, dist = sqrtf(dx * dx + dy * dy);
	bool swat = p->kind == PK_SWAT;
	if (g->stars == 0 || pl->status != PL_ALIVE) {                 // nothing to do: wander off
		p->turnT -= dt;
		if (p->turnT <= 0) { pick_heading(g, p); p->turnT = g_rndf(g, 1.0f, 3.0f); }
		if (ahead_ok(g, p, p->heading, 10.0f)) ped_step(g, p, 24.0f, dt); else pick_heading(g, p);
		return;
	}
	bool los = dist < 170.0f && los_clear(g, p->x, p->y, tx, ty);
	float speed = swat ? 58.0f : 66.0f;
	float aim = atan2f(dx, -dy);

	if (los && dist < 140.0f && g->stars >= 2) {                   // 2+ stars: in sight, stop at a distance and shoot (1 star: they try to arrest you)
		p->heading = aim;
		float stop = swat ? 60.0f : 70.0f;
		if (dist > stop) ped_step(g, p, speed * 0.8f, dt);
		p->fireT -= dt;
		if (p->fireT <= 0) {
			const WeaponDef *w = swat ? &swat_smg : &cop_pistol;
			p->fireT = w->rate * g_rndf(g, 0.8f, 1.4f);
			float a = aim + g_rndf(g, -w->spread, w->spread);
			spawn_bullet(g, p->x + sinf(a) * 7.0f, p->y - cosf(a) * 7.0f, a, w, 1, 1.0f);
			fx_part(g, P_MUZZLE, p->x + sinf(a) * 10.0f, p->y - cosf(a) * 10.0f, 0, 0, 0.05f, 0);
		}
	} else {                                                       // follow the flow field toward the player
		float nx, ny;
		if (field_dir(&g->foot_field, p->x, p->y, &nx, &ny)) {
			p->heading = atan2f(nx - p->x, -(ny - p->y));
			ped_step(g, p, speed, dt);
		} else if (dist > 1.0f) {
			p->heading = aim;
			ped_step(g, p, speed, dt);
		}
	}
}

void peds_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_PEDS; i++) {
		Ped *p = &g->peds[i];
		if (!p->active) continue;
		float dxp = p->x - g->p.x, dyp = p->y - g->p.y, d2 = dxp * dxp + dyp * dyp;

		if (p->state == PS_DEAD) {
			p->deadT += dt;
			float f = expf(-5.0f * dt);
			if (fabsf(p->vx) + fabsf(p->vy) > 1.0f) { world_move(&g->world, &p->x, &p->y, p->vx * dt, p->vy * dt, 3.0f); p->vx *= f; p->vy *= f; }
			if (p->deadT > 28.0f || d2 > 500.0f * 500.0f) ped_remove(g, i);
			continue;
		}
		if (d2 > 520.0f * 520.0f && p->kind == PK_CIV) { ped_remove(g, i); continue; }
		if (p->kind != PK_CIV && g->stars == 0 && d2 > 320.0f * 320.0f) { ped_remove(g, i); continue; }
		if (p->kind != PK_CIV && d2 > 700.0f * 700.0f) { ped_remove(g, i); continue; }

		// knocked around by a car
		if (fabsf(p->vx) + fabsf(p->vy) > 3.0f) {
			world_move(&g->world, &p->x, &p->y, p->vx * dt, p->vy * dt, 4.0f);
			float f = expf(-6.0f * dt); p->vx *= f; p->vy *= f;
		}

		if (p->kind != PK_CIV) { cop_ai(g, p, dt); continue; }

		if (p->state == PS_FLEE) {
			p->stateT -= dt;
			float away = atan2f(p->x - p->threatX, -(p->y - p->threatY));
			float want = away + g_rndf(g, -0.15f, 0.15f);
			if (!ahead_ok(g, p, want, 10.0f)) { pick_heading(g, p); } else p->heading = wrap_pi(want);
			if (ahead_ok(g, p, p->heading, 8.0f)) ped_step(g, p, 70.0f, dt); else pick_heading(g, p);
			if (p->stateT <= 0) { p->state = PS_WALK; p->turnT = 1.0f; }
		} else {
			// on a crosswalk: walk straight across the street instead of dithering in the traffic
			int id = world_tile(&g->world, (int)floorf(p->x / TILE), (int)floorf(p->y / TILE));
			if (id == TILE_CW_V || id == TILE_CW_H) {
				if (id == TILE_CW_V) p->heading = (sinf(p->heading) >= 0) ? PI_F * 0.5f : -PI_F * 0.5f;        // vertical street: cross east/west
				else                 p->heading = (cosf(p->heading) <= 0) ? 0.0f : PI_F;                       // horizontal street: cross north/south
				p->turnT = 1.0f;
				ped_step(g, p, 30.0f, dt);
				continue;
			}
			p->turnT -= dt;
			if (p->turnT <= 0) { pick_heading(g, p); p->turnT = g_rndf(g, 1.5f, 5.0f); }
			if (ahead_ok(g, p, p->heading, 10.0f)) ped_step(g, p, 22.0f, dt);
			else { pick_heading(g, p); }
		}
	}
}

// ---------------------------------------------------------------- spawning (always out of the player's view)
static bool offscreen(const Game *g, float x, float y)
{
	return x < g->camx - 40 || x > g->camx + SCREEN_W + 40 || y < g->camy - 40 || y > g->camy + SCREEN_H + 40;
}

static bool find_walk_spot(Game *g, float dmin, float dmax, float *ox, float *oy, bool civ)
{
	for (int tries = 0; tries < 30; tries++) {
		float a = g_rndf(g, 0, 2 * PI_F), d = g_rndf(g, dmin, dmax);
		float x = g->p.x + cosf(a) * d, y = g->p.y + sinf(a) * d;
		int tx = (int)floorf(x / TILE), ty = (int)floorf(y / TILE);
		if (!(civ ? ped_tile_ok(&g->world, tx, ty) : !(world_flags(&g->world, tx, ty) & F_SOLID))) continue;
		if (!offscreen(g, x, y)) continue;
		*ox = tx * (float)TILE + 8.0f; *oy = ty * (float)TILE + 8.0f;
		return true;
	}
	return false;
}

void spawn_civilian(Game *g)
{
	float x, y;
	if (!find_walk_spot(g, 130.0f, 300.0f, &x, &y, true)) return;
	ped_spawn(g, PK_CIV, g_rndi(g, 6), x, y);
}

void spawn_cop_foot(Game *g, bool swat)
{
	float x, y;
	if (!find_walk_spot(g, 200.0f, 320.0f, &x, &y, false)) return;
	ped_spawn(g, swat ? PK_SWAT : PK_COP, swat ? 7 : 6, x, y);
}
