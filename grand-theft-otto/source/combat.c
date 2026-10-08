// Grand Theft Otto - weapons, bullets, blood, explosions and damage
#include "game.h"
#include "gen_atlas.h"
#include <math.h>
#include <string.h>

//                                   name       dmg   rate   spread speed life  pellets ammo
const WeaponDef weapon_defs[W_COUNT] = {
	{"Fists",   14.0f, 0.36f, 0.00f,   0.0f, 0.00f, 1,   0},
	{"Pistol",  22.0f, 0.28f, 0.035f, 420.0f, 0.50f, 1, 200},
	{"SMG",     11.0f, 0.085f, 0.11f, 440.0f, 0.50f, 1, 500},
	{"Shotgun", 14.0f, 0.85f, 0.22f,  380.0f, 0.26f, 5,  60},
	{"Assault Rifle", 17.0f, 0.105f, 0.055f, 540.0f, 0.62f, 1, 400},
	{"Machine Gun", 13.0f, 0.052f, 0.10f, 500.0f, 0.55f, 1, 900},
	{"Sniper Rifle", 95.0f, 1.05f, 0.0f, 950.0f, 0.95f, 1, 40},
	{"RPG",     45.0f, 1.30f, 0.0f,  270.0f, 1.70f, 1, 12},
};

// ---------------------------------------------------------------- effects
void fx_part(Game *g, int type, float x, float y, float vx, float vy, float life, int frame)
{
	for (int i = 0; i < MAX_PARTS; i++) {
		Particle *p = &g->parts[i];
		if (p->active) continue;
		*p = (Particle){true, type, frame, x, y, vx, vy, life, life};
		return;
	}
}

void fx_stain(Game *g, float x, float y, int size)
{
	Stain *s = &g->stains[g->stain_n % MAX_STAINS];
	s->x = x; s->y = y; s->img = size < 0 ? 0 : size > 3 ? 3 : size;
	g->stain_n++;
}

// red blood: a spray of drops flying away from the hit, plus a small stain where it lands
void fx_blood(Game *g, float x, float y, float dx, float dy, int amount)
{
	float len = sqrtf(dx * dx + dy * dy);
	if (len > 0.001f) { dx /= len; dy /= len; }
	for (int i = 0; i < amount; i++) {
		float a = g_rndf(g, -0.9f, 0.9f), s = g_rndf(g, 25.0f, 90.0f);
		float c = cosf(a), sn = sinf(a);
		float vx = (dx * c - dy * sn) * s + g_rndf(g, -10, 10), vy = (dx * sn + dy * c) * s + g_rndf(g, -10, 10);
		fx_part(g, P_BLOOD, x, y, vx, vy, g_rndf(g, 0.25f, 0.6f), 0);
	}
	if (amount >= 2) fx_stain(g, x + dx * 4 + g_rndf(g, -3, 3), y + dy * 4 + g_rndf(g, -3, 3), amount >= 7 ? 2 : 0);
}

void fx_boom(Game *g, float x, float y)
{
	fx_part(g, P_BOOM, x, y, 0, 0, 0.6f, 0);
	for (int i = 0; i < 8; i++) fx_part(g, P_FIRE, x + g_rndf(g, -10, 10), y + g_rndf(g, -10, 10), g_rndf(g, -30, 30), g_rndf(g, -30, 30), g_rndf(g, 0.5f, 1.1f), g_rndi(g, 3));
	for (int i = 0; i < 6; i++) fx_part(g, P_SMOKE, x + g_rndf(g, -8, 8), y + g_rndf(g, -8, 8), g_rndf(g, -22, 22), g_rndf(g, -30, -8), g_rndf(g, 0.8f, 1.6f), g_rndi(g, 3));
	fx_stain(g, x, y, 3);
}

void particles_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_PARTS; i++) {
		Particle *p = &g->parts[i];
		if (!p->active) continue;
		p->life -= dt;
		if (p->life <= 0) { p->active = false; continue; }
		p->x += p->vx * dt; p->y += p->vy * dt;
		float drag = p->type == P_BLOOD ? 5.0f : 1.5f;
		p->vx -= p->vx * drag * dt; p->vy -= p->vy * drag * dt;
		if (p->type == P_BOOM) p->frame = (int)((1.0f - p->life / p->maxlife) * 5.99f);
		if (p->type == P_FIRE) p->frame = (p->frame + 1) % 3;
		if (p->type == P_SMOKE) { p->vy -= 14.0f * dt; }
	}
}

// ---------------------------------------------------------------- damage
void drop_cash(Game *g, float x, float y, int amount)
{
	for (int i = 0; i < MAX_PICKUPS; i++) {
		Pickup *k = &g->pickups[i];
		if (k->active) continue;
		*k = (Pickup){true, x, y, 5, amount, 0, 40.0f, true};
		return;
	}
}

void damage_player(Game *g, float dmg, float dx, float dy)
{
	Player *p = &g->p;
	if (p->status != PL_ALIVE) return;
	dmg *= 0.45f;                                  // Otto is tougher than he looks
	p->calmT = 0;
	if (p->armor > 0) {
		float absorb = fminf(p->armor, dmg * 0.6f);
		p->armor -= absorb; dmg -= absorb;
	}
	p->hp -= dmg;
	p->hurtT = 0.25f; g->flashT = 0.3f;
	fx_blood(g, p->x, p->y, dx, dy, 3 + (int)(dmg / 5));
	if (p->hp <= 0) { p->hp = 0; player_wasted(g); }
}

void damage_ped(Game *g, int i, float dmg, float dx, float dy, int source)
{
	Ped *p = &g->peds[i];
	if (!p->active || p->state == PS_DEAD) return;
	p->hp -= dmg;
	fx_blood(g, p->x, p->y, dx, dy, 3 + (int)(dmg / 6));
	if (source == 0) {
		if (p->kind == PK_CIV) add_heat(g, 0.4f);
		else add_heat(g, 1.3f);
	}
	if (p->hp > 0) {
		if (p->kind == PK_CIV) { p->state = PS_FLEE; p->stateT = g_rndf(g, 3.0f, 6.0f); p->threatX = g->p.x; p->threatY = g->p.y; }
		return;
	}
	p->state = PS_DEAD; p->deadT = 0; p->vx = dx * 20; p->vy = dy * 20;
	fx_blood(g, p->x, p->y, dx, dy, 9);
	fx_stain(g, p->x, p->y, 2);
	if (source == 0 || source == 3) {
		if (p->kind == PK_CIV) add_heat(g, source == 3 ? 1.0f : 0.9f);
		else add_heat(g, p->kind == PK_SWAT ? 3.5f : 2.6f);
	}
	if (p->kind == PK_CIV) drop_cash(g, p->x, p->y, 20 + g_rndi(g, 80));
	else drop_cash(g, p->x, p->y, 60 + g_rndi(g, 140));
}

void damage_car(Game *g, int i, float dmg, int source)
{
	Car *c = &g->cars[i];
	if (!c->active || c->state == CS_WRECK) return;
	if (source == 0 || source == 3) c->hitT = 6.0f;
	c->hp -= dmg;
	if (c->hp <= 0) explode_car(g, i);
}

// a rocket going off: hurts everything near it, more the closer it is
void blast(Game *g, float x, float y, float R, float maxdmg, int owner)
{
	fx_boom(g, x, y);
	for (int k = 0; k < MAX_PEDS; k++) {
		Ped *p = &g->peds[k];
		if (!p->active || p->state == PS_DEAD) continue;
		float dx = p->x - x, dy = p->y - y, d = sqrtf(dx * dx + dy * dy);
		if (d < R) damage_ped(g, k, maxdmg * (1.0f - d / R) * 1.4f, dx, dy, owner == 0 ? 0 : 2);
	}
	if (g->p.status == PL_ALIVE && g->p.car < 0) {
		float dx = g->p.x - x, dy = g->p.y - y, d = sqrtf(dx * dx + dy * dy);
		if (d < R) damage_player(g, maxdmg * 0.7f * (1.0f - d / R), dx, dy);
	}
	for (int k = 0; k < MAX_CARS; k++) {
		Car *o = &g->cars[k];
		if (!o->active || o->state == CS_WRECK || (owner == 0 && o->state == CS_PLAYER)) continue;      // your own rocket doesn't hurt your own ride
		float dx = o->x - x, dy = o->y - y, d = sqrtf(dx * dx + dy * dy);
		if (d < R + 10) { if (owner == 0) o->hitT = 6.0f; damage_car(g, k, maxdmg * 1.3f * (1.0f - d / (R + 10)), owner == 0 ? 3 : 2); }
	}
	if (owner == 0) add_heat(g, 1.0f);
}

void explode_car(Game *g, int i)
{
	Car *c = &g->cars[i];
	if (!c->active || c->state == CS_WRECK) return;
	bool byPlayer = c->hitT > 0 || c->state == CS_PLAYER;
	Driver drv = c->driver;
	bool playerInside = (c->state == CS_PLAYER);
	c->alt = 0;
	c->hp = 0; c->state = CS_WRECK; c->wreckT = 0; c->burnT = 12.0f; c->driver = DRV_NONE;
	c->vx *= 0.3f; c->vy *= 0.3f; c->throttle = c->brake = c->steer = 0;
	fx_boom(g, c->x, c->y);

	const float R = 54.0f;
	for (int k = 0; k < MAX_PEDS; k++) {
		Ped *p = &g->peds[k];
		if (!p->active || p->state == PS_DEAD) continue;
		float dx = p->x - c->x, dy = p->y - c->y, d = sqrtf(dx * dx + dy * dy);
		if (d < R) damage_ped(g, k, 130.0f * (1.0f - d / R), dx, dy, byPlayer ? 0 : 2);
	}
	if (playerInside) {
		Player *p = &g->p;
		p->car = -1; p->x = c->x; p->y = c->y; p->vx = p->vy = 0;
		bool fell = c->model == M_HELI && c->alt > 0.5f;
		damage_player(g, fell ? 190.0f : 45.0f, 0, -1);
		popup(g, c->model == M_HELI ? "Your helicopter went down!" : "Your car blew up!", 0xFF60C0FF);
	} else if (g->p.status == PL_ALIVE && g->p.car < 0) {
		float dx = g->p.x - c->x, dy = g->p.y - c->y, d = sqrtf(dx * dx + dy * dy);
		if (d < R) damage_player(g, 75.0f * (1.0f - d / R), dx, dy);
	}
	for (int k = 0; k < MAX_CARS; k++) {
		Car *o = &g->cars[k];
		if (k == i || !o->active || o->state == CS_WRECK) continue;
		float dx = o->x - c->x, dy = o->y - c->y, d = sqrtf(dx * dx + dy * dy);
		if (d < R + 8) { if (byPlayer) o->hitT = 6.0f; damage_car(g, k, 80.0f * (1.0f - d / (R + 8)), byPlayer ? 3 : 2); }
	}
	if (drv == DRV_CIV && byPlayer) add_heat(g, 1.0f);
	else if ((drv == DRV_COP || drv == DRV_SWAT) && byPlayer) add_heat(g, 3.0f);
}

// ---------------------------------------------------------------- bullets
void spawn_bullet(Game *g, float x, float y, float ang, const WeaponDef *w, int owner, float dmgmul)
{
	for (int i = 0; i < MAX_BULLETS; i++) {
		Bullet *b = &g->bullets[i];
		if (b->active) continue;
		b->active = true; b->x = x; b->y = y;
		b->vx = sinf(ang) * w->speed; b->vy = -cosf(ang) * w->speed;
		b->life = w->life; b->owner = owner; b->dmg = w->dmg * dmgmul; b->rocket = (w == &weapon_defs[W_RPG]);
		return;
	}
}

static bool seg_hits_circle(float x0, float y0, float x1, float y1, float cx, float cy, float r)
{
	float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
	float t = l2 > 0 ? ((cx - x0) * dx + (cy - y0) * dy) / l2 : 0;
	t = clampf(t, 0, 1);
	float px = x0 + dx * t - cx, py = y0 + dy * t - cy;
	return px * px + py * py <= r * r;
}

void bullets_update(Game *g, float dt)
{
	for (int i = 0; i < MAX_BULLETS; i++) {
		Bullet *b = &g->bullets[i];
		if (!b->active) continue;
		float dist = sqrtf(b->vx * b->vx + b->vy * b->vy) * dt;
		int steps = (int)ceilf(dist / 4.0f); if (steps < 1) steps = 1;
		float sx = b->vx * dt / steps, sy = b->vy * dt / steps;
		bool dead = false;
		for (int s = 0; s < steps && !dead; s++) {
			float x0 = b->x, y0 = b->y;
			b->x += sx; b->y += sy;
			if (world_flags(&g->world, (int)floorf(b->x / TILE), (int)floorf(b->y / TILE)) & F_SOLID) {
				fx_part(g, P_SPARK, x0, y0, 0, 0, 0.12f, 0);
				dead = true; break;
			}
			if (b->owner == 0) {
				for (int k = 0; k < MAX_PEDS && !dead; k++) {
					Ped *p = &g->peds[k];
					if (!p->active || p->state == PS_DEAD) continue;
					if (seg_hits_circle(x0, y0, b->x, b->y, p->x, p->y, 5.5f)) { damage_ped(g, k, b->dmg, sx, sy, 0); dead = true; }
				}
			} else if (g->p.status == PL_ALIVE) {
				if (g->p.car < 0 && seg_hits_circle(x0, y0, b->x, b->y, g->p.x, g->p.y, 6.0f)) { damage_player(g, b->dmg, sx, sy); dead = true; }
			}
			if (dead) break;
			for (int k = 0; k < MAX_CARS && !dead; k++) {
				Car *c = &g->cars[k];
				if (!c->active || (b->owner == 1 && c->state == CS_CHASE) || (b->owner == 0 && c->state == CS_PLAYER)) continue;
				float cx[3], cy[3], r;
				car_circles(c, cx, cy, &r);
				for (int m = 0; m < 3; m++) {
					if (!seg_hits_circle(x0, y0, b->x, b->y, cx[m], cy[m], r)) continue;
					fx_part(g, P_SPARK, b->x, b->y, 0, 0, 0.12f, 0);
					if (c->state == CS_PLAYER) { damage_player(g, b->dmg * 0.35f, sx, sy); damage_car(g, k, b->dmg * 0.5f, 1); }
					else damage_car(g, k, b->dmg * 0.6f, b->owner == 0 ? 0 : 1);
					dead = true; break;
				}
			}
		}
		b->life -= dt;
		if (b->rocket && b->active && !dead) fx_part(g, P_SMOKE, b->x - b->vx * 0.02f, b->y - b->vy * 0.02f, g_rndf(g, -6, 6), g_rndf(g, -6, 6), 0.5f, g_rndi(g, 2));
		if (dead || b->life <= 0) {
			b->active = false;
			if (b->rocket) blast(g, b->x, b->y, 64.0f, 170.0f, b->owner);
		}
	}
}

// ---------------------------------------------------------------- the helicopter's weapons: L = nose guns, X = rocket
void heli_fire(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	Car *c = &g->cars[p->car];
	c->fireT -= dt; p->fireT -= dt;
	if (c->alt < 0.6f || p->status != PL_ALIVE) return;
	float fx = sinf(c->heading), fy = -cosf(c->heading), rx = cosf(c->heading), ry = sinf(c->heading);
	if (in->l && p->fireT <= 0) {
		p->fireT = 0.07f;
		float side = ((int)(g->time * 14.0f) & 1) ? 4.5f : -4.5f;
		float ox = c->x + fx * 15.0f + rx * side, oy = c->y + fy * 15.0f + ry * side;
		spawn_bullet(g, ox, oy, c->heading + g_rndf(g, -0.04f, 0.04f), &weapon_defs[W_MG], 0, 1.1f);
		fx_part(g, P_MUZZLE, ox + fx * 3.0f, oy + fy * 3.0f, 0, 0, 0.05f, 0);
		add_heat(g, 0.04f);
	}
	if (in->x_p && c->fireT <= 0) {
		c->fireT = 1.1f;
		spawn_bullet(g, c->x + fx * 16.0f, c->y + fy * 16.0f, c->heading, &weapon_defs[W_RPG], 0, 1.0f);
		for (int k = 0; k < 4; k++) fx_part(g, P_SMOKE, c->x + fx * 12.0f, c->y + fy * 12.0f, g_rndf(g, -25, 25), g_rndf(g, -25, 25), 0.5f, g_rndi(g, 3));
		add_heat(g, 0.8f);
	}
}

// ---------------------------------------------------------------- the player's weapon
static float angle_to(float dx, float dy) { return atan2f(dx, -dy); }        // clockwise from "up"

void player_fire(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	p->fireT -= dt;
	if (p->punchT > 0) p->punchT -= dt;
	if (p->car >= 0 || p->status != PL_ALIVE || !in->a || p->fireT > 0) return;

	const WeaponDef *w = &weapon_defs[p->weapon];
	if (w->maxammo && p->ammo[p->weapon] <= 0) { p->fireT = 0.3f; return; }

	// aim assist: turn toward the closest living person roughly in front of us
	float aim = p->heading, best = 1e9f;
	int target = -1;
	for (int k = 0; k < MAX_PEDS; k++) {
		Ped *q = &g->peds[k];
		if (!q->active || q->state == PS_DEAD) continue;
		float dx = q->x - p->x, dy = q->y - p->y, d = sqrtf(dx * dx + dy * dy);
		float range = p->weapon == W_FISTS ? 26.0f : p->weapon == W_SNIPER ? 300.0f : p->weapon == W_RPG ? 220.0f : 150.0f;
		if (d > range || d < 1) continue;
		float diff = fabsf(wrap_pi(angle_to(dx, dy) - p->heading));
		if (diff > 0.75f) continue;
		if (d + diff * 40.0f < best) { best = d + diff * 40.0f; target = k; aim = angle_to(dx, dy); }
	}
	if (target >= 0) p->heading = aim;

	p->fireT = w->rate;
	float fx = sinf(aim), fy = -cosf(aim);
	if (p->weapon == W_FISTS) {                                   // punch whoever is right in front
		p->punchT = 0.18f;
		for (int k = 0; k < MAX_PEDS; k++) {
			Ped *q = &g->peds[k];
			if (!q->active || q->state == PS_DEAD) continue;
			float dx = q->x - p->x, dy = q->y - p->y, d = sqrtf(dx * dx + dy * dy);
			if (d < 18.0f && (dx * fx + dy * fy) > 0.3f * d) { damage_ped(g, k, w->dmg, fx, fy, 0); break; }
		}
		return;
	}
	p->ammo[p->weapon]--;
	for (int k = 0; k < w->pellets; k++) {
		float a = aim + g_rndf(g, -w->spread, w->spread);
		spawn_bullet(g, p->x + fx * 8.0f, p->y + fy * 8.0f, a, w, 0, 1.0f);
	}
	{
		float tip = p->weapon == W_PISTOL ? 12.0f : p->weapon == W_SMG ? 13.0f : p->weapon == W_SNIPER || p->weapon == W_RPG ? 17.0f : 15.0f;
		fx_part(g, P_MUZZLE, p->x + fx * tip, p->y + fy * tip, 0, 0, 0.07f, 0);
		if (p->weapon != W_PISTOL && p->weapon != W_SMG) fx_part(g, P_MUZZLE, p->x + fx * (tip + 4.0f), p->y + fy * (tip + 4.0f), 0, 0, 0.05f, 0);
		if (p->weapon == W_RPG) for (int k = 0; k < 4; k++) fx_part(g, P_SMOKE, p->x - fx * 8.0f, p->y - fy * 8.0f, -fx * g_rndf(g, 20, 60) + g_rndf(g, -15, 15), -fy * g_rndf(g, 20, 60) + g_rndf(g, -15, 15), 0.6f, g_rndi(g, 3));
	}
	// gunfire in front of witnesses is a crime
	for (int k = 0; k < MAX_PEDS; k++) {
		Ped *q = &g->peds[k];
		if (!q->active || q->state == PS_DEAD) continue;
		float dx = q->x - p->x, dy = q->y - p->y;
		if (dx * dx + dy * dy < 170.0f * 170.0f) {
			if (q->kind == PK_CIV) { q->state = PS_FLEE; q->stateT = g_rndf(g, 4.0f, 7.0f); q->threatX = p->x; q->threatY = p->y; }
			add_heat(g, p->weapon == W_SHOTGUN || p->weapon == W_SNIPER ? 0.3f : p->weapon == W_RPG ? 1.2f : 0.1f);
			break;
		}
	}
	if (p->ammo[p->weapon] <= 0) popup(g, "Out of ammo", 0xFFB0B0B0);
}
