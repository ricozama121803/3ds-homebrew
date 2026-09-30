#include "game.h"
#include <string.h>

#define MIN_GAP        0.64f     // fighters cannot get closer than this (m)
#define HEAD_R         0.115f
#define BODY_R         0.19f
#define GUARD_MOVE     0.45f     // walking speed factor while blocking

static float angle_diff(float a, float b)
{
	float d = fmodf(a - b + PI_F, 2.0f * PI_F);
	if (d < 0) d += 2.0f * PI_F;
	return d - PI_F;
}

static float approach(float v, float target, float rate)
{
	if (v < target) return fminf(v + rate, target);
	return fmaxf(v - rate, target);
}

static void to_idle(Fighter *f)
{
	f->st = ST_IDLE; f->st_t = 0; f->atk = ATK_NONE; f->atk_t = 0;
}

static void enter(Fighter *f, FState st, int len)
{
	f->st = st; f->st_t = 0; f->st_len = len;
	if (st != ST_ATTACK) { f->atk = ATK_NONE; f->atk_t = 0; }
}

void game_init(Game *g)
{
	memset(g, 0, sizeof *g);
	Look la = look_player(), lb = look_opponent();
	fighter_init(&g->f[0], -1.1f, 0, 0, -1, &la);              // orthodox: left hand leads
	fighter_init(&g->f[1], 1.1f, 0, PI_F, +1, &lb);            // southpaw, so both chests face the camera
	ai_init(&g->ai, 0x9e3779b9u, 0.55f);
	g->phase = PH_INTRO; g->phase_t = 0;
	g->round_time = ROUND_SECONDS;
	g->winner = -1;
	g->hit_text_kind = -1;
	for (int i = 0; i < MAX_FX; i++) g->fx[i].t = 9.0f;               // all slots free
	g->hp_trail[0] = g->hp_trail[1] = 100;
	fighter_pose(&g->f[0], 0); fighter_pose(&g->f[1], 0);
}

void game_restart(Game *g)
{
	unsigned seed = g->ai.rng + 1;
	float skill = g->ai.skill;
	game_init(g);
	g->ai.rng = seed; g->ai.skill = skill;
}

// ---- fighter step -------------------------------------------------------------------------
static int head_side(const Fighter *f, const Fighter *foe)    // +1 if the opponent is on our +X side
{
	(void)f; return foe->x >= f->x ? 1 : -1;
}

Vec3 fight_aim_point(const Fighter *f, const Fighter *foe, int atk)
{
	const AttackDef *d = &ATTACKS[atk];
	int head = d->aim_head && f->duck < 0.5f;
	Vec3 c = head ? foe->rig.head_c : foe->rig.chest_c;
	if (atk == ATK_LEAD_HOOK || atk == ATK_REAR_HOOK) {              // hooks land on the outside of the head (or ribs)
		float side = (atk == ATK_LEAD_HOOK ? (float)f->lead : -(float)f->lead) * (head ? 0.13f : 0.15f);
		Mat4 ry = mat4_roty(f->yaw);
		return v3add(c, mat4_dir(&ry, v3(0, 0, side)));
	}
	Vec3 root = d->limb < 2 ? f->rig.shoulder[d->limb] : f->rig.pelvis_c;
	Vec3 dir = v3norm(v3sub(c, root));
	return v3madd(c, dir, -(head ? 0.16f : 0.14f));                // the glove/foot ends just inside the surface
}

// How far to step in (or bounce out) during the wind-up so the limb is nearly fully extended on contact.
static float attack_lunge(const Fighter *f, int atk)
{
	const AttackDef *d = &ATTACKS[atk];
	Vec3 root = d->limb < 2 ? f->rig.shoulder[d->limb] : f->rig.pelvis_c;
	Vec3 to = v3sub(f->strike, root);
	float dy = to.y;
	float horiz = sqrtf(to.x * to.x + to.z * to.z);
	// Distance at which the limb is nearly straight. The torso turns during the swing, which moves the shoulder
	// forward (cross, rear hook, roundhouse) or back (lead hook) relative to where it starts; account for that.
	static const float shoulder_shift[ATK_COUNT] = { 0.04f, 0.17f, -0.10f, 0.15f, 0.0f, 0.10f };
	float want = d->reach * 0.94f;
	float want_h = sqrtf(fmaxf(want * want - dy * dy, 0.0f)) + shoulder_shift[atk];
	return clampf(horiz - want_h, -0.14f, 0.30f + d->step) / (float)d->startup;
}

static void step_fighter(Game *g, Fighter *f, const Fighter *foe, const FighterInput *in, int alive_ctl)
{
	const float dt = TICK_DT;
	f->st_t++;

	// face the opponent
	float want = atan2f(-(foe->z - f->z), foe->x - f->x);
	if (f->st != ST_KO && f->st != ST_KNOCKDOWN) f->yaw += clampf(angle_diff(want, f->yaw), -14.0f * dt, 14.0f * dt);

	// knock-back velocity decays
	f->x += f->kx * dt; f->z += f->kz * dt;
	f->kx *= expf(-dt * 10.0f); f->kz *= expf(-dt * 10.0f);

	f->react = fmaxf(0.0f, f->react - dt * 4.5f);

	unsigned held = alive_ctl ? in->held : 0;
	int can_act = (f->st == ST_IDLE || f->st == ST_BLOCKSTUN) && alive_ctl;

	// stamina regeneration
	if (f->st == ST_IDLE || f->st == ST_BLOCKSTUN)
		f->stamina = fminf(100.0f, f->stamina + (f->guard ? 6.0f : 13.0f) * dt);

	// guard (R = head, L+R = body); can only guard while free to act
	int gwant = 0;
	if (can_act && (held & BTN_GUARD)) gwant = (held & BTN_MOD) ? 2 : 1;
	f->guard = gwant;
	f->guard_amt[0] = approach(f->guard_amt[0], gwant == 1 ? 1.0f : 0.0f, dt * 12.0f);
	f->guard_amt[1] = approach(f->guard_amt[1], gwant == 2 ? 1.0f : 0.0f, dt * 12.0f);

	// head movement from the D-pad. Left/right are screen directions, so map them to back/forward.
	float lean_t = 0, duck_t = 0, slip_t = 0;
	if (can_act || f->st == ST_ATTACK) {
		int side = head_side(f, foe);
		int back = side > 0 ? (held & BTN_DP_LEFT) : (held & BTN_DP_RIGHT);
		int fwd  = side > 0 ? (held & BTN_DP_RIGHT) : (held & BTN_DP_LEFT);
		lean_t = back ? -1.0f : fwd ? 0.55f : 0.0f;
		duck_t = (held & BTN_DP_DOWN) ? 1.0f : 0.0f;
		slip_t = (held & BTN_DP_UP) ? (cosf(f->yaw) > 0 ? -1.0f : 1.0f) : 0.0f;    // slip away from the camera
	}
	float hm = 1.0f - expf(-dt * 20.0f);
	f->lean += (lean_t - f->lean) * hm; f->duck += (duck_t - f->duck) * hm; f->slip += (slip_t - f->slip) * hm;

	// attacks
	if (can_act && f->st == ST_IDLE && !gwant) {
		int atk = ATK_NONE;
		unsigned pr = in->pressed;
		if (pr & BTN_LEAD_PUNCH) atk = (held & BTN_MOD) ? ATK_LEAD_HOOK : ATK_LEAD_PUNCH;
		else if (pr & BTN_REAR_PUNCH) atk = (held & BTN_MOD) ? ATK_REAR_HOOK : ATK_REAR_PUNCH;
		else if (pr & BTN_LEAD_KICK) atk = ATK_LEAD_KICK;
		else if (pr & BTN_REAR_KICK) atk = ATK_REAR_KICK;
		if (atk != ATK_NONE && f->stamina >= 4.0f) {
			enter(f, ST_ATTACK, 0);
			f->atk = atk; f->atk_t = 0; f->atk_hit = 0;
			f->stamina = fmaxf(0.0f, f->stamina - ATTACKS[atk].stamina);
			f->strike = fight_aim_point(f, foe, atk);
			f->lunge = attack_lunge(f, atk);
		}
	}

	// movement
	float speed_scale = 1.0f;
	float mx = 0, mz = 0;
	if (f->st == ST_IDLE || f->st == ST_BLOCKSTUN) {
		mx = in->mx; mz = in->mz;
		if (!alive_ctl) mx = mz = 0;
		float len = sqrtf(mx * mx + mz * mz);
		if (len > 1.0f) { mx /= len; mz /= len; }
		if (f->guard) speed_scale = GUARD_MOVE;
		if (f->duck > 0.4f) speed_scale *= 0.6f;
	}
	{
		// moving away from the opponent is slower than closing in
		float towards = (foe->x - f->x) * mx + (foe->z - f->z) * mz;
		float k = towards >= 0 ? 1.7f : 1.35f;
		float tvx = mx * k * speed_scale, tvz = mz * k * speed_scale * 0.85f;
		float a = 1.0f - expf(-dt * 12.0f);
		f->vx += (tvx - f->vx) * a; f->vz += (tvz - f->vz) * a;
		f->x += f->vx * dt; f->z += f->vz * dt;
	}

	if (f->st == ST_ATTACK) {
		const AttackDef *d = &ATTACKS[f->atk];
		int total = d->startup + d->active + d->recovery;
		int lock = d->startup / 4 + 1;
		if (f->atk_t == lock) f->strike = fight_aim_point(f, foe, f->atk);   // lock the aim; dodging after this makes it miss
		if (f->atk_t < d->startup) {                                   // step in (or bounce out) during the wind-up
			float dir_x = foe->x - f->x, dir_z = foe->z - f->z;
			float l = sqrtf(dir_x * dir_x + dir_z * dir_z);
			if (l > MIN_GAP + 0.02f || f->lunge < 0) {
				f->x += dir_x / l * f->lunge; f->z += dir_z / l * f->lunge;
			}
		}
		f->atk_t++;
		if (f->atk_t >= total) to_idle(f);
	} else if (f->st == ST_HITSTUN || f->st == ST_BLOCKSTUN || f->st == ST_GUARDBREAK) {
		if (f->st_t >= f->st_len) to_idle(f);
	} else if (f->st == ST_KNOCKDOWN) {
		if (f->st_t < 24) f->fall = fminf(1.0f, f->fall + 1.0f / 24.0f);
		else if (f->st_t > 24 + 70) {
			f->fall = fmaxf(0.0f, f->fall - 1.0f / 34.0f);
			if (f->fall <= 0.0f) { to_idle(f); f->stamina = fmaxf(f->stamina, 40.0f); }
		}
	} else if (f->st == ST_KO) {
		f->fall = fminf(1.0f, f->fall + 1.0f / 40.0f);
	}

	// walk cycle follows the speed
	float spd = sqrtf(f->vx * f->vx + f->vz * f->vz);
	float target = clampf(spd / 1.4f, 0.0f, 1.0f);
	f->walk_amt += (target - f->walk_amt) * (1.0f - expf(-dt * 10.0f));
	f->walk_phase += spd * dt * 6.5f;
	(void)g;
}

// ---- hit resolution -----------------------------------------------------------------------
static float seg_dist(Vec3 p, Vec3 a, Vec3 b)
{
	Vec3 ab = v3sub(b, a);
	float t = clampf(v3dot(v3sub(p, a), ab) / fmaxf(v3dot(ab, ab), 1e-6f), 0, 1);
	return v3dist(p, v3madd(a, ab, t));
}

static void spawn_fx(Game *g, Vec3 pos, int kind, float size)
{
	int slot = 0;
	for (int i = 0; i < MAX_FX; i++) if (g->fx[i].t < 0 || g->fx[i].t > 0.7f) { slot = i; break; }
	g->fx[slot].pos = pos; g->fx[slot].t = 0; g->fx[slot].kind = kind; g->fx[slot].size = size;
}

static void resolve(Game *g, int ai_idx, int di_idx)
{
	Fighter *a = &g->f[ai_idx], *d = &g->f[di_idx];
	if (a->st != ST_ATTACK || a->atk_hit) return;
	const AttackDef *def = &ATTACKS[a->atk];
	int t = a->atk_t;                                                // the rig was posed with this timer value
	if (t < def->startup || t >= def->startup + def->active) return;
	if (d->st == ST_KO || d->st == ST_KNOCKDOWN) return;

	Vec3 tip = a->rig.tip[def->limb];
	int zone = -1;
	if (v3dist(tip, d->rig.head_c) < def->tip_r + HEAD_R) zone = 1;
	else if (seg_dist(tip, v3madd(d->rig.pelvis_c, v3(0, 1, 0), 0.05f), d->rig.chest_c) < def->tip_r + BODY_R) zone = 0;
	if (zone < 0) return;

	a->atk_hit = 1;
	Vec3 contact = zone == 1 ? d->rig.head_c : d->rig.chest_c;
	contact = v3lerp(tip, contact, 0.5f);
	int free_to_guard = d->st == ST_IDLE || d->st == ST_BLOCKSTUN;
	int blocked = free_to_guard && ((zone == 1 && d->guard == 1) || (zone == 0 && d->guard == 2));
	float stam_scale = 0.65f + 0.35f * a->stamina / 100.0f;
	Vec3 dir = v3norm(v3(d->x - a->x, 0, d->z - a->z));
	g->hit_text_kind = a->atk; g->hit_text_block = blocked; g->hit_text_t = 0.9f; g->last_hit_side = di_idx;

	if (blocked) {
		d->stamina -= def->damage * 1.5f;
		d->hp = fmaxf(1.0f, d->hp - def->damage * 0.06f);
		d->kx += dir.x * def->push * 2.5f; d->kz += dir.z * def->push * 2.5f;
		if (d->stamina <= 0.0f) {
			d->stamina = 0; enter(d, ST_GUARDBREAK, 46); d->guard = 0;
			g->hitstop = 8; g->cam.shake += 0.6f;
		} else {
			enter(d, ST_BLOCKSTUN, def->blockstun);
			g->hitstop = 3;
		}
		g->cam.shake += 0.15f;
		spawn_fx(g, contact, 1, 0.55f);
		if (di_idx == 1) g->combo = 0;
		return;
	}

	float dmg = def->damage * stam_scale * (zone == 1 ? 1.0f : 0.85f);
	if (d->duck > 0.5f && zone == 1) dmg *= 1.15f;
	d->hp -= dmg;
	d->hit_taken++;
	d->react = 1.0f; d->react_zone = zone; d->react_dir = dir;
	d->kx += dir.x * def->push * 6.0f; d->kz += dir.z * def->push * 6.0f;
	g->hitstop = 4 + (int)(dmg * 0.25f);
	g->cam.shake += 0.25f + dmg * 0.03f; g->cam.zoom_pulse = fminf(1.0f, g->cam.zoom_pulse + 0.4f + dmg * 0.02f);
	g->flash = fminf(1.0f, dmg * 0.05f);
	spawn_fx(g, contact, 0, 0.55f + dmg * 0.045f);
	if (ai_idx == 0) { g->combo = (g->combo_t > 0) ? g->combo + 1 : 1; g->combo_t = 75; } else g->combo = 0;

	if (d->hp <= 0.0f) {
		d->hp = 0; enter(d, ST_KO, 0); d->guard = 0;
		g->hitstop = 14; g->cam.shake += 0.7f;
		g->phase = PH_KO; g->phase_t = 0; g->winner = ai_idx;
		spawn_fx(g, contact, 2, 1.7f);
		return;
	}
	if (dmg >= 11.0f && d->hp < 55.0f && zone == 1) {               // heavy head shot: down
		enter(d, ST_KNOCKDOWN, 0); d->guard = 0; g->hitstop += 4;
		return;
	}
	enter(d, ST_HITSTUN, def->hitstun);
}

// ---- step ---------------------------------------------------------------------------------
static void separate(Fighter *a, Fighter *b)
{
	float dx = b->x - a->x, dz = b->z - a->z, l = sqrtf(dx * dx + dz * dz);
	if (l < MIN_GAP && l > 1e-4f) {
		float push = (MIN_GAP - l) * 0.5f;
		a->x -= dx / l * push; a->z -= dz / l * push; b->x += dx / l * push; b->z += dz / l * push;
	} else if (l <= 1e-4f) { a->x -= 0.01f; b->x += 0.01f; }
	// keep the pair roughly lined up across the screen so neither hides the other (as fighting games do)
	float dzz = b->z - a->z;
	if (fabsf(dzz) > 0.55f) {
		float pull = (fabsf(dzz) - 0.55f) * 0.5f, sgn = dzz > 0 ? 1.0f : -1.0f;
		a->z += sgn * pull; b->z -= sgn * pull;
	}
	Fighter *fs[2] = { a, b };
	for (int i = 0; i < 2; i++) {
		fs[i]->x = clampf(fs[i]->x, -RING_HALF, RING_HALF);
		fs[i]->z = clampf(fs[i]->z, -1.6f, 1.6f);
	}
}

void game_step(Game *g, const FighterInput *player)
{
	g->phase_t++;
	g->flash = fmaxf(0.0f, g->flash - TICK_DT * 3.5f);
	for (int i = 0; i < MAX_FX; i++) if (g->fx[i].t >= 0) g->fx[i].t += TICK_DT;
	for (int i = 0; i < 2; i++) {                                    // HUD: the recent-damage flash lags behind the real health
		if (g->hp_trail[i] < g->f[i].hp) g->hp_trail[i] = g->f[i].hp;
		else g->hp_trail[i] = fmaxf(g->f[i].hp, g->hp_trail[i] - TICK_DT * 14.0f * (g->hit_text_t > 0.5f ? 0.0f : 1.0f));
	}
	g->hit_text_t = fmaxf(0.0f, g->hit_text_t - TICK_DT);
	if (g->combo_t > 0 && --g->combo_t == 0) g->combo = 0;

	if (g->hitstop > 0) {                                            // impact freeze: only the camera settles
		g->hitstop--;
		g->time += TICK_DT * 0.15f;
		return;
	}
	g->frame++;
	g->time += TICK_DT;

	int fighting = g->phase == PH_FIGHT;
	if (g->phase == PH_INTRO && g->phase_t > 100) { g->phase = PH_FIGHT; g->phase_t = 0; fighting = 1; }

	FighterInput none;
	memset(&none, 0, sizeof none);
	FighterInput ai_in = fighting ? ai_think(&g->ai, &g->f[1], &g->f[0]) : none;
	const FighterInput *pin = fighting ? player : &none;

	step_fighter(g, &g->f[0], &g->f[1], pin, fighting);
	step_fighter(g, &g->f[1], &g->f[0], &ai_in, fighting);
	separate(&g->f[0], &g->f[1]);
	fighter_pose(&g->f[0], g->time); fighter_pose(&g->f[1], g->time);
	fighter_record_trail(&g->f[0]); fighter_record_trail(&g->f[1]);

	if (fighting) {
		resolve(g, 0, 1);
		resolve(g, 1, 0);
		g->round_time -= TICK_DT;
		if (g->round_time <= 0 && g->phase == PH_FIGHT) {
			g->round_time = 0; g->phase = PH_TIMEUP; g->phase_t = 0;
			float d = g->f[0].hp - g->f[1].hp;
			g->winner = d > 0.5f ? 0 : d < -0.5f ? 1 : 2;
		}
	}
	if (g->phase == PH_KO && g->phase_t == 1) g->cam.zoom_pulse = 1.0f;
}
