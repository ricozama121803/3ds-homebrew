// Web Slinger - game logic: pendulum web swinging, running, wall climbing, web zip, third-person camera, beacon run.
#include "game.h"
#include <stdio.h>

#define GRAV        30.0f
#define MAX_ROPE    95.0f
#define RUN_SPEED   8.5f
#define SPRINT_SPEED 15.0f
#define JUMP_V      12.5f
#define JUMP_CHARGE 24.0f                  // extra launch speed at full charge (about 19 m of height)
#define CHARGE_TIME 0.65f
#define TOP_SWING   42.0f
#define MAX_SPEED   54.0f
#ifndef AVOID_LOOK
#define AVOID_LOOK (30.0f + vh * 0.8f)
#define AVOID_ACC  45.0f
#endif
#define BEACON_R    14.0f
#define BEACON_H    26.0f

static float wrap_pi(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
static float len3(const float a[3]) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static float yaw_of(float x, float z) { return atan2f(x, -z); }

void game_popup(Game *g, const char *text, unsigned col)
{
	for (int i = 2; i > 0; i--) g->pop[i] = g->pop[i - 1];
	snprintf(g->pop[0].text, sizeof g->pop[0].text, "%s", text);
	g->pop[0].t = 2.2f; g->pop[0].col = col;
}

// ---- setup --------------------------------------------------------------------------------------------------------------
static void init_cars(Game *g)
{
	static const float pal[6][3] = { { 0.95f, 0.75f, 0.1f }, { 0.95f, 0.75f, 0.1f }, { 0.85f, 0.15f, 0.12f }, { 0.15f, 0.2f, 0.5f }, { 0.85f, 0.85f, 0.85f }, { 0.12f, 0.12f, 0.14f } };
	for (int i = 0; i < MAX_CAR; i++) {
		Car *c = &g->car[i];
		c->axis = i & 1; c->line = 1 + (int)(rnd_next(&g->rng) % (NB - 1));
		float side = (rnd_next(&g->rng) & 1) ? 1.0f : -1.0f;
		c->off = side * 3.6f; c->along = rndf(&g->rng) * CITY; c->speed = side * (7.0f + rndf(&g->rng) * 7.0f);
		memcpy(c->col, pal[rnd_next(&g->rng) % 6], sizeof c->col);
	}
}

void game_init(Game *g, bool lowq)
{
	memset(g, 0, sizeof *g);
	g->rng = 0x1234ABCDu;
	g->sens = 1.0f; g->lowq = lowq;
	city_generate(&g->w, 0x5EED5u);
	init_cars(g);
	g->state = GS_TITLE;
	game_respawn(g);
	g->p.state = ST_AIR; g->p.p[1] = 90; g->p.v[0] = 20; g->p.v[2] = -4;
}

static void spawn_beacon(Game *g)
{
	for (int tries = 0; tries < 12; tries++) {
		int axis = rnd_next(&g->rng) & 1, line = 1 + (int)(rnd_next(&g->rng) % (NB - 1));
		float along = 30.0f + rndf(&g->rng) * (CITY - 60.0f), jitter = (rndf(&g->rng) - 0.5f) * 6.0f;
		float x = axis == 0 ? line * PITCH + jitter : along, z = axis == 0 ? along : line * PITCH + jitter;
		float cx = x - CITY * 0.5f, cz = z - CITY * 0.5f, near = 1.0f - sqrtf(cx * cx + cz * cz) / 330.0f;
		float y = 20.0f + rndf(&g->rng) * 36.0f + (near > 0 ? near * 60.0f * rndf(&g->rng) : 0);
		float dx = x - g->p.p[0], dz = z - g->p.p[2];
		float d2 = dx * dx + dz * dz;
		if (tries < 11 && (d2 < 90.0f * 90.0f || d2 > 240.0f * 240.0f)) continue;       // not too close, not across the whole city
		g->bc.on = true; g->bc.p[0] = x; g->bc.p[1] = y; g->bc.p[2] = z; g->bc.t = 0;
		return;
	}
}

void game_respawn(Game *g)
{
	Player *p = &g->p;
	float x = p->p[0], z = p->p[2];
	float sx = clampf(floorf(x / PITCH + 0.5f) * PITCH, 0, CITY), sz = clampf(floorf(z / PITCH + 0.5f) * PITCH, 0, CITY);
	x = clampf(x, 6, CITY - 6); z = clampf(z, 6, CITY - 6);
	if (fabsf(x - sx) < fabsf(z - sz)) x = sx; else z = sz;
	if (g->state == GS_TITLE && p->p[1] == 0) { x = 4 * PITCH; z = 3 * PITCH; }
	float keep = p->face;
	memset(p, 0, sizeof *p);
	p->p[0] = x; p->p[1] = 0; p->p[2] = z; p->state = ST_GROUND;
	p->face = keep;
	g->chain = 1; g->chainT = 0;
}

void game_start(Game *g, int mode)
{
	g->mode = mode; g->state = GS_PLAY; g->paused = false; g->showHelp = false;
	g->score = 0; g->beacons = 0; g->chain = 1; g->chainT = 0; g->flown = 0; g->topSpeed = 0; g->time = 0; g->stateT = 0;
	g->timeLeft = 90.0f; g->hintT = 14.0f;
	memset(g->pop, 0, sizeof g->pop);
	Player *p = &g->p;
	memset(p, 0, sizeof *p);
	p->p[0] = 4 * PITCH; p->p[2] = 3 * PITCH; p->state = ST_GROUND;
	p->face = yaw_of(0.5f * PITCH, 1.5f * PITCH);
	g->cam.yaw = p->face; g->cam.pitch = -0.2f; g->cam.dist = 9.5f; g->cam.fov = 62; g->cam.manualT = 0;
	spawn_beacon(g);
}

// ---- web anchors ----------------------------------------------------------------------------------------------------------
static bool pick_anchor(const Game *g, bool zip, const float head[2], float out[3], float vis[3])
{
	const Player *p = &g->p;
	static const float pit[5] = { 50, 42, 60, 34, 70 }, yoff[7] = { 0, 10, -10, 25, -25, 60, -60 };
	const float o[3] = { p->p[0], p->p[1] + 1.4f, p->p[2] };
	float best = -1e9f; bool found = false;
	bool standing = p->state != ST_AIR;
	for (int pass = 0; pass < (standing ? 4 : 3) && !found; pass++) {                                    // pass 1: ropes at least 16 m; pass 2 (hugging a wall): anything from 6 m
	for (int yi = 0; yi < (pass == 0 ? 3 : pass == 1 ? 5 : 7); yi++) {                                        // first pass: only anchors close to straight ahead, so swings don't pull sideways into a wall
		float ca = cosf(DEG(yoff[yi])), sa = sinf(DEG(yoff[yi]));
		float hx = head[0] * ca - head[1] * sa, hz = head[0] * sa + head[1] * ca;
		for (int pi = 0; pi < 5; pi++) {
			float pr = DEG(pit[pi]), d[3] = { hx * cosf(pr), sinf(pr), hz * cosf(pr) }, n[3];
			float t = city_ray(&g->w, o, d, MAX_ROPE, n);
			if (t < (pass == 3 ? 6.0f : zip ? 10.0f : 16.0f)) continue;
			float ay = o[1] + d[1] * t - p->p[1];
			if (ay < 10.0f) continue;
			float sc = -fabsf(pit[pi] - 50.0f) * 0.25f - fabsf(yoff[yi]) * 0.3f + fminf(ay, 45.0f) * 0.3f - fabsf(n[0] * head[1] - n[2] * head[0]) * 6.0f;   // and prefer faces we approach head-on over side faces
			sc -= zip ? fabsf(t - 34.0f) * 0.15f - ay * 0.1f : fabsf(t - 42.0f) * 0.35f;
			if (!zip) { float low = o[1] + d[1] * t - t - 6.0f; if (low < 0) sc += low * 0.45f; }      // the arc's lowest point (anchor height - rope length) should clear the street
			if (sc > best) { best = sc; found = true; for (int k = 0; k < 3; k++) { vis[k] = o[k] + d[k] * t + n[k] * 0.05f; out[k] = vis[k] + (zip ? 0.0f : n[k] * 6.0f); } }
		}
	}
	}
	return found;
}

static float s_padx;                                           // this frame's lateral pad input (set by player_update)
static void heading_of(const Game *g, float mx, float mz, float mag, float head[2])
{
	const Player *p = &g->p;
	float vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
	bool locked = p->state == ST_AIR && (p->aimDir[0] != 0 || p->aimDir[1] != 0) && vh > 4.0f && fabsf(s_padx) < 0.12f;
	if (locked) { head[0] = p->aimDir[0]; head[1] = p->aimDir[1]; }
	else if (mag > 0.25f) { head[0] = mx / mag; head[1] = mz / mag; }
	else if (p->state == ST_AIR && (p->aimDir[0] != 0 || p->aimDir[1] != 0) && vh > 4.0f) { head[0] = p->aimDir[0]; head[1] = p->aimDir[1]; }       // keep going the way we were
	else if (vh > 4.0f) { head[0] = p->v[0] / vh; head[1] = p->v[2] / vh; }
	else { head[0] = sinf(g->cam.yaw); head[1] = -cosf(g->cam.yaw); }
}

static bool fire_web(Game *g, bool zip, float mx, float mz, float mag)
{
	Player *p = &g->p;
	float head[2], a[3], av[3];
	heading_of(g, mx, mz, mag, head);
	if (!pick_anchor(g, zip, head, a, av)) {
		if (p->noAnchorT <= 0) { game_popup(g, "No web target", 0xFF70B0FF); p->noAnchorT = 1.2f; }
		p->retryT = 0.2f;
		return false;
	}
	const float c[3] = { p->p[0], p->p[1] + 1.0f, p->p[2] };
	memcpy(p->anchor, a, sizeof a); memcpy(p->avis, av, sizeof av);
	p->ropeLen = sqrtf((a[0] - c[0]) * (a[0] - c[0]) + (a[1] - c[1]) * (a[1] - c[1]) + (a[2] - c[2]) * (a[2] - c[2]));
	p->trickT = 0;
	p->rope = true; p->zip = zip; p->zipT = 0; p->webT = 0; p->hand ^= 1; p->fireCd = 0.15f; p->swingT = 0;
	memcpy(p->aimDir, head, sizeof head);
	return true;
}

static void release_rope(Player *p) { if (p->rope && !p->zip) p->boostWin = 0.45f; p->rope = false; p->zip = false; }

// ---- player ----------------------------------------------------------------------------------------------------------------
static void enter_wall(Player *p, const Contact *k)
{
	p->state = ST_WALL; p->wn[0] = k->nx; p->wn[1] = k->nz;
	p->v[0] = p->v[1] = p->v[2] = 0;
	release_rope(p);
	p->face = yaw_of(-k->nx, -k->nz);
}

static void player_update(Game *g, const Input *in, float dt, bool allowCam)
{
	Player *p = &g->p;
	float cy = cosf(g->cam.yaw), sy = sinf(g->cam.yaw);
	float mx = sy * in->ly + cy * in->lx, mz = -cy * in->ly + sy * in->lx;       // camera-relative move vector
	float mag = sqrtf(mx * mx + mz * mz);
	if (mag > 1) { mx /= mag; mz /= mag; mag = 1; }
	s_padx = in->lx;
	(void)allowCam;
	if (p->fireCd > 0) p->fireCd -= dt;
	if (p->retryT > 0) p->retryT -= dt;
	if (p->noAnchorT > 0) p->noAnchorT -= dt;
	if (p->wallCd > 0) p->wallCd -= dt;
	if (p->landT > 0) p->landT -= dt;
	if (p->boostWin > 0) p->boostWin -= dt;
	if (p->aTapT > 0) p->aTapT -= dt;
	if (p->trickT > 0) p->trickT -= dt;
	if (p->webT < 1) p->webT += dt * 12.0f;

	if (p->state == ST_GROUND) {
		bool doJump = false;
		if (in->jump_p && p->landT <= 0) p->charge = 0.001f;                                   // press A: crouch; hold to charge; release to jump
		if (p->charge > 0) { if (in->swing) p->charge = 0; else if (in->a_held) p->charge = fminf(1.0f, p->charge + dt / CHARGE_TIME); else doJump = true; }
		float spd = (in->sprint ? SPRINT_SPEED : RUN_SPEED) * (p->landT > 0 ? 0.35f : 1.0f) * (1.0f - 0.65f * p->charge);
		float tx = mx * spd, tz = mz * spd, dx = tx - p->v[0], dz = tz - p->v[2], dl = sqrtf(dx * dx + dz * dz), step = (mag > 0.05f ? 60.0f : 45.0f) * dt;
		if (dl > step) { dx *= step / dl; dz *= step / dl; }
		p->v[0] += dx; p->v[2] += dz; p->v[1] = -4.0f;
		float vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
		if (vh > 0.8f) { float want = yaw_of(p->v[0], p->v[2]); p->face += wrap_pi(want - p->face) * fminf(1.0f, dt * 12.0f); p->phase += 4.4f * sqrtf(vh) * dt; }
		float d[3] = { p->v[0] * dt, p->v[1] * dt, p->v[2] * dt };
		Contact k; city_move(&g->w, p->p, d, &k);
		p->airT = 0;
		bool push = (k.hitx || k.hitz) && mag > 0.4f && p->wallCd <= 0 && -(mx * (k.hitx ? k.nx : 0) + mz * (k.hitz ? k.nz : 0)) / mag > 0.6f;
		p->wallPush = push ? p->wallPush + dt : 0;
		if (p->wallPush > 0.15f) {                                                             // run into a building: start running up it
			Contact w2 = k; if (!k.hitx) w2.nx = 0; if (!k.hitz) w2.nz = 0;
			enter_wall(p, &w2); p->wallPush = 0;
		} else if (doJump) {
			float ch = p->charge; p->charge = 0;
			p->v[1] = JUMP_V + JUMP_CHARGE * ch; p->v[0] *= 1.0f + 0.3f * ch; p->v[2] *= 1.0f + 0.3f * ch;
			p->state = ST_AIR; p->airT = 0; p->aTapT = 0.35f; p->trickCount = 0;
			if (ch > 0.85f) game_popup(g, "Super jump!", 0xFF40D8FF);
		}
		else if (in->zip_p && p->fireCd <= 0) {
			if (fire_web(g, true, mx, mz, mag)) { p->v[1] = 5.0f; p->state = ST_AIR; p->airT = 0; }
		} else if (in->swing && p->fireCd <= 0 && p->retryT <= 0) {
			if (fire_web(g, false, mx, mz, mag)) {
				p->v[1] = 14.0f; p->v[0] += p->aimDir[0] * 6.0f; p->v[2] += p->aimDir[1] * 6.0f; p->state = ST_AIR; p->airT = 0;
			}
		} else if (!k.landed) { p->state = ST_AIR; p->airT = 0; }
	} else if (p->state == ST_AIR) {
		p->airT += dt;
		if (in->swing && !p->rope && p->fireCd <= 0 && p->retryT <= 0) fire_web(g, false, mx, mz, mag);
		if (in->zip_p && !p->zip && p->fireCd <= 0) { release_rope(p); if (fire_web(g, true, mx, mz, mag)) p->zipT = 0; }
		if (!in->swing && p->rope && !p->zip) release_rope(p);
		if (in->jump_p) {
			bool canBoost = (p->rope && !p->zip) || (p->boostWin > 0 && !p->rope && p->trickT <= 0);
			if (canBoost) {                                                                    // A at the end of a swing (or just after the web lets go): launch
				release_rope(p); p->boostWin = 0;
				float vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
				if (vh > 1) { float k = fminf(1.0f + 14.0f / vh, 1.45f); p->v[0] *= k; p->v[2] *= k; }
				p->v[1] = fmaxf(p->v[1], 0.0f) + 15.0f; p->fireCd = 0.25f; p->retryT = 0.35f; p->aTapT = 0.35f;
				if (len3(p->v) > 62.0f) { float k = 62.0f / len3(p->v); for (int q = 0; q < 3; q++) p->v[q] *= k; }
			} else if (!p->rope && p->aTapT > 0 && p->trickT <= 0) {                         // second tap: a trick, flip direction from the stick
				float fx = sinf(g->cam.yaw), fz = -cosf(g->cam.yaw), df = mx * fx + mz * fz, ds = mx * cosf(g->cam.yaw) + mz * sinf(g->cam.yaw);   // relative to what the camera shows
				const char *nm; 
				if (mag < 0.3f || (fabsf(df) >= fabsf(ds) && df >= 0)) { p->trickType = 1; nm = "Front flip"; }
				else if (fabsf(df) >= fabsf(ds)) { p->trickType = 2; nm = "Back flip"; }
				else if (ds < 0) { p->trickType = 3; nm = "Roll left"; } else { p->trickType = 4; nm = "Roll right"; }
				p->trickT = TRICK_DUR; p->trickCount++;
				if (mag > 0.3f) { p->v[0] += mx * 5.0f; p->v[2] += mz * 5.0f; }
				p->v[1] = fmaxf(p->v[1], 0.0f) + 6.0f; p->aTapT = 0.35f;
				char buf[40]; snprintf(buf, sizeof buf, p->trickCount > 1 ? "%s  x%d" : "%s", nm, p->trickCount); game_popup(g, buf, 0xFFFFE080);
				g->score += 75 * p->trickCount;
			} else p->aTapT = 0.35f;
		}
		float c[3] = { p->p[0], p->p[1] + 1.0f, p->p[2] }, rn[3] = { 0, 0, 0 }, dist = 0;
		if (p->rope) {
			float r[3] = { p->anchor[0] - c[0], p->anchor[1] - c[1], p->anchor[2] - c[2] };
			dist = len3(r); if (dist < 0.01f) dist = 0.01f;
			for (int k = 0; k < 3; k++) rn[k] = r[k] / dist;
		}
		if (p->rope && p->zip) {
			p->zipT += dt;
			p->ropeLen = fmaxf(0.0f, p->ropeLen - 60.0f * dt);
			for (int k = 0; k < 3; k++) p->v[k] += (rn[k] * 50.0f - p->v[k]) * fminf(1.0f, 7.0f * dt);
			if (dist < 4.5f || p->zipT > 1.5f) { release_rope(p); p->v[1] = fmaxf(p->v[1], 6.0f); p->fireCd = 0.2f; }
		} else {
			float gr = (GRAV + (in->dive && !p->rope ? 40.0f : 0.0f)) * (p->trickT > 0 ? 0.6f : 1.0f);
			p->v[1] -= gr * dt;
			float acc = p->rope ? 16.0f : 11.0f;
			p->v[0] += mx * acc * dt; p->v[2] += mz * acc * dt;
			float sp = len3(p->v);
			if (p->rope) {
				p->swingT += dt;
				{                                                                                           // reel in so the arc's lowest point stays well above the roofs/street
					float gnd = city_ground_below(&g->w, p->p), maxLen = fmaxf(30.0f, p->anchor[1] - (gnd + 14.0f));
					if (p->p[1] - gnd < 22.0f && p->ropeLen > maxLen) p->ropeLen = fmaxf(maxLen, p->ropeLen - 40.0f * dt);
				}
				{                                                                                           // keep the swing on its line
					float vhh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
					if (vhh > 4.0f && (fabsf(in->lx) > 0.12f || (p->aimDir[0] == 0 && p->aimDir[1] == 0))) { p->aimDir[0] = p->v[0] / vhh; p->aimDir[1] = p->v[2] / vhh; }      // steering sets the line: bleed off sideways drift unless you steer
					float ax = p->aimDir[0], az = p->aimDir[1], side = p->v[0] * -az + p->v[2] * ax, k = 3.0f * dt * (1.0f - fminf(1.0f, fabsf(in->lx) * 2.0f));
					p->v[0] -= -az * side * k; p->v[2] -= ax * side * k;
				}
				if (sp > 1 && sp < TOP_SWING) { float b = 8.0f * dt / sp; for (int k = 0; k < 3; k++) p->v[k] += p->v[k] * b; }
				if (in->swing && dist >= p->ropeLen - 1.0f) {                                              // web tug: near the street the rope hauls you up toward the anchor
					float alt = p->p[1] - city_ground_below(&g->w, p->p), k = 1.0f - clampf((alt - 8.0f) / 50.0f, 0, 1);
					for (int q = 0; q < 3; q++) p->v[q] += rn[q] * 26.0f * k * dt;
				}
				if (dist >= p->ropeLen - 0.05f) { float vr = p->v[0] * rn[0] + p->v[1] * rn[1] + p->v[2] * rn[2]; if (vr < 0) for (int k = 0; k < 3; k++) p->v[k] -= vr * rn[k]; }
			}
			sp = len3(p->v);
			float drag = 1.0f - fminf(0.5f, (p->rope ? 0.03f : 0.08f) * dt);
			if (sp > MAX_SPEED) drag *= MAX_SPEED / sp;
			for (int k = 0; k < 3; k++) p->v[k] *= drag;
			if (p->v[1] < -75.0f) p->v[1] = -75.0f;
		}
#ifndef NO_AVOID
		if (p->rope && !p->zip) {                                                                    // wall avoidance: look ahead and veer away from buildings unless the player steers into them
			float vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
			if (vh > 8.0f) {
				float ox[3] = { p->p[0], p->p[1] + 1.0f, p->p[2] }, base = atan2f(p->v[0], p->v[2]);
				float look = AVOID_LOOK;
				for (int q = -1; q <= 1; q++) {
					float a = base + q * 0.35f, dd[3] = { sinf(a), 0, cosf(a) }, n[3];
					float t = city_ray(&g->w, ox, dd, look, n);
					if (t < 0 || fabsf(n[1]) > 0.5f) continue;
					float s = 1.0f - t / look; s *= s;
					if (mag > 0.3f && -(mx * n[0] + mz * n[2]) / mag > 0.5f) continue;              // you are steering at it on purpose
					float acc = AVOID_ACC * s * clampf(vh / 35.0f, 0.5f, 1.3f);
					p->v[0] += n[0] * acc * dt; p->v[2] += n[2] * acc * dt;
				}
			}
		}
#endif
		float d[3] = { p->v[0] * dt, p->v[1] * dt, p->v[2] * dt };
		Contact k; city_move(&g->w, p->p, d, &k);
		if (p->rope && !p->zip) {                                                                    // the rope is a hard limit: pull the body back inside it
			float c2[3] = { p->p[0], p->p[1] + 1.0f, p->p[2] }, r2[3] = { p->anchor[0] - c2[0], p->anchor[1] - c2[1], p->anchor[2] - c2[2] };
			float d2 = len3(r2);
			if (d2 > p->ropeLen && d2 > 0.01f) { float s = (d2 - p->ropeLen) / d2, corr[3] = { r2[0] * s, r2[1] * s, r2[2] * s }; Contact k2; city_move(&g->w, p->p, corr, &k2); k.hitx |= k2.hitx; k.hitz |= k2.hitz; if (k2.hitx) k.nx = k2.nx; if (k2.hitz) k.nz = k2.nz; k.landed |= k2.landed; }
			// the swing is over once the anchor is well behind and not far above: let go (and swing again at once while R is held)
			float r3[3] = { p->anchor[0] - p->p[0], p->anchor[1] - p->p[1] - 1.0f, p->anchor[2] - p->p[2] };
			float rh = sqrtf(r3[0] * r3[0] + r3[2] * r3[2]), vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
			if (p->swingT > 0.8f && len3(p->v) < 5.0f) { release_rope(p); p->retryT = 0.1f; p->fireCd = 0.0f; p->stall++; }       // hanging against a wall: drop and swing again
			else if (len3(p->v) > 12.0f) p->stall = 0;
			if (vh > 6 && rh > 1 && p->swingT > 1.2f && p->rope) {
				float back = -(r3[0] * p->v[0] + r3[2] * p->v[2]) / (rh * vh), elev = atan2f(r3[1], rh);
				if ((back > 0.25f && elev < DEG(22)) || p->swingT > 9.0f) { release_rope(p); p->retryT = 0.05f; p->fireCd = 0.0f; if (p->v[1] > 0) { p->v[0] *= 1.03f; p->v[2] *= 1.03f; } }
			}
		}
		if (k.head && p->v[1] > 0) p->v[1] = 0;
		if (k.landed) {
			if (p->v[1] < -32.0f) p->landT = 0.4f;
			p->v[1] = 0; release_rope(p); p->state = ST_GROUND; p->swingT = 0; p->trickT = 0; p->trickCount = 0; p->boostWin = 0;
		} else if (k.hitx || k.hitz) {
			float nx = k.hitx ? k.nx : 0, nz = k.hitz ? k.nz : 0;
			float into = -(p->v[0] * nx + p->v[2] * nz);
			bool cling = !in->swing && p->wallCd <= 0 && (p->zip || into > 5.0f || (mag > 0.3f && -(mx * nx + mz * nz) / mag > 0.5f));
			if (cling) enter_wall(p, &(Contact){ .nx = nx, .nz = nz });
			else {                                                                                      // slide along the wall
				if (p->rope) p->bumps++;
				if (k.hitx) p->v[0] = 0;
				if (k.hitz) p->v[2] = 0;
			}
		}
		float vh2 = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
		if (vh2 > 2.0f) p->face += wrap_pi(yaw_of(p->v[0], p->v[2]) - p->face) * fminf(1.0f, dt * 8.0f);
	} else {                                                                                            // ST_WALL
		float nx = p->wn[0], nz = p->wn[1], tx = -nz, tz = nx;
		float side = (mx * tx + mz * tz);
		float climb = in->sprint ? 11.0f : 6.5f;
		p->v[0] = tx * side * climb - nx * 1.5f; p->v[2] = tz * side * climb - nz * 1.5f; p->v[1] = in->ly * climb;
		float d[3] = { p->v[0] * dt, p->v[1] * dt, p->v[2] * dt };
		Contact k; city_move(&g->w, p->p, d, &k);
		p->face = yaw_of(-nx, -nz);
		float sp = fabsf(side) + fabsf(in->ly);
		if (sp > 0.1f) p->phase += sp * climb * dt * 0.5f;
		if (k.landed && in->ly < 0) { p->state = ST_GROUND; }
		else if (!(k.hitx || k.hitz)) {                                                                 // ran out of wall: vault onto the roof or just drop
			p->state = ST_AIR; p->wallCd = 0.35f;
			if (in->ly > 0.2f) { p->v[0] = -nx * 5.0f; p->v[2] = -nz * 5.0f; p->v[1] = 8.0f; } else { p->v[0] = p->v[1] = p->v[2] = 0; }
		}
		if (in->jump_p) { p->state = ST_AIR; p->wallCd = 0.4f; p->v[0] = nx * 10.0f + mx * 4.0f; p->v[2] = nz * 10.0f + mz * 4.0f; p->v[1] = 11.0f; }
		else if (in->swing && p->fireCd <= 0) {
			if (fire_web(g, false, mx, mz, mag)) { p->state = ST_AIR; p->wallCd = 0.4f; p->v[0] = nx * 6.0f; p->v[2] = nz * 6.0f; p->v[1] = 8.0f; }
		} else if (in->dive) { p->state = ST_AIR; p->wallCd = 0.4f; p->v[0] = nx * 3.0f; p->v[2] = nz * 3.0f; p->v[1] = 0; }
	}
	float spd = len3(p->v);
	p->speed += (spd - p->speed) * fminf(1.0f, dt * 6.0f);
	if (p->state != ST_GROUND) g->flown += spd * dt;
	if (spd > g->topSpeed) g->topSpeed = spd;

	// into the water: back to the street
	if (p->p[1] < WATER_Y - 2.0f || p->p[1] < -40 || p->p[0] < -300 || p->p[0] > CITY + 300 || p->p[2] < -300 || p->p[2] > CITY + 300) {
		game_popup(g, "Splash! Back to the street", 0xFFFFC080);
		game_respawn(g);
	}
}

// ---- bot (attract mode on the title screen, and the host tests) --------------------------------------------------------------
void game_bot(Game *g, Input *in, float dt, const float *target)
{
	static float wpx, wpz, wpt;
	memset(in, 0, sizeof *in);
	wpt -= dt;
	const Player *p = &g->p;
	if (target) { wpx = target[0]; wpz = target[2]; wpt = 5.0f; }
	else if (wpt <= 0 || (wpx - p->p[0]) * (wpx - p->p[0]) + (wpz - p->p[2]) * (wpz - p->p[2]) < 50.0f * 50.0f) {
		wpx = 70 + rndf(&g->rng) * (CITY - 140); wpz = 70 + rndf(&g->rng) * (CITY - 140); wpt = 12.0f;
	}
	float tx = wpx, tz = wpz;
	if (p->p[0] < 90 || p->p[0] > CITY - 90 || p->p[2] < 90 || p->p[2] > CITY - 90) { tx = tz = CITY * 0.5f; }          // stay away from the water
	float want = yaw_of(tx - p->p[0], tz - p->p[2]);
	float vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
	float cur = vh > 3 ? yaw_of(p->v[0], p->v[2]) : g->cam.yaw;
	float err = wrap_pi(want - cur);
	in->lx = sinf(err); in->ly = cosf(err);                       // the pad points where we want to go, relative to where the camera looks
	in->swing = true;
	if (p->state == ST_GROUND) in->sprint = true;
}

// ---- camera --------------------------------------------------------------------------------------------------------------------
static void cam_update(Game *g, const Input *in, float dt, bool manual)
{
	Cam *c = &g->cam; const Player *p = &g->p;
	float kx = in->cx, ky = in->cy, inv = g->invertY ? -1.0f : 1.0f;
	bool moved = fabsf(kx) > 0.05f || fabsf(ky) > 0.05f || in->tdx != 0 || in->tdy != 0;
	if (manual && moved) {
		c->yaw += (kx * 2.6f * dt + in->tdx * 0.007f) * g->sens;
		c->pitch += (ky * 1.6f * dt - in->tdy * 0.006f) * g->sens * inv;
		c->manualT = 1.4f;
	}
	if (c->manualT > 0) c->manualT -= dt;
	float vh = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
	if (in->recenter_p) { c->yaw = p->face; c->pitch = -0.2f; c->manualT = 0; }
	else if (c->manualT <= 0 && vh > (p->state == ST_GROUND ? 12.0f : 5.0f) && p->state != ST_WALL) {
		float want = yaw_of(p->v[0], p->v[2]);
		c->yaw += wrap_pi(want - c->yaw) * fminf(1.0f, dt * (p->state == ST_GROUND ? 1.2f : 1.6f + vh * 0.03f));
	}
	if (p->state == ST_WALL && c->manualT <= 0) c->yaw += wrap_pi(yaw_of(-p->wn[0], -p->wn[1]) - c->yaw) * fminf(1.0f, dt * 2.0f);
	if (c->manualT <= 0) {
		float want = g->state == GS_TITLE ? -0.3f : p->state == ST_AIR ? -0.22f + 0.4f * clampf(p->v[1] / 45.0f, -1, 1) : p->state == ST_WALL ? -0.12f : -0.2f;
		c->pitch += (want - c->pitch) * fminf(1.0f, dt * 1.5f);
	}
	c->pitch = clampf(c->pitch, -1.25f, 0.9f);
	c->yaw = wrap_pi(c->yaw);
	float wantFov = 62.0f + clampf(p->speed - 12.0f, 0, 36.0f) * 0.55f;
	c->fov += (wantFov - c->fov) * fminf(1.0f, dt * 3.0f);
	float wantD = (g->state == GS_TITLE ? 17.0f : 8.0f) + clampf(p->speed - 10.0f, 0, 36.0f) * 0.07f;
	c->dist += (wantD - c->dist) * fminf(1.0f, dt * 2.0f);

	if (g->state == GS_TITLE) {                                                                       // slow orbit around the skyline, from over the water
		float a = g->time * 0.12f + 0.6f, r = 470.0f, cx = CITY * 0.5f;
		c->eye[0] = cx + cosf(a) * r; c->eye[1] = 150.0f; c->eye[2] = cx + sinf(a) * r;
		float dx = cx - c->eye[0], dy = 95.0f - c->eye[1], dz = cx - c->eye[2];
		c->yaw = yaw_of(dx, dz); c->pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
		c->fov += (52.0f - c->fov) * fminf(1.0f, dt * 3.0f);
		return;
	}
	float cp = cosf(c->pitch), spn = sinf(c->pitch);
	float f[3] = { sinf(c->yaw) * cp, spn, -cosf(c->yaw) * cp };
	float T[3] = { p->p[0], p->p[1] + 1.2f, p->p[2] };
	float back[3] = { -f[0], -f[1], -f[2] }, n[3];
	float t = city_ray(&g->w, T, back, c->dist + 0.8f, n);
	float dd = t > 0 ? fmaxf(1.4f, t - 0.8f) : c->dist;
	for (int k = 0; k < 3; k++) c->eye[k] = T[k] + back[k] * dd;
	float floor_y = (c->eye[0] >= 0 && c->eye[0] <= CITY && c->eye[2] >= 0 && c->eye[2] <= CITY) ? 0.6f : WATER_Y + 1.0f;
	if (c->eye[1] < floor_y) c->eye[1] = floor_y;
}

// ---- main update -------------------------------------------------------------------------------------------------------------------
static void update_world(Game *g, float dt)
{
	for (int i = 0; i < MAX_CAR; i++) { Car *c = &g->car[i]; c->along += fabsf(c->speed) * dt; if (c->along > CITY) c->along -= CITY; }
	for (int i = 0; i < 3; i++) if (g->pop[i].t > 0) g->pop[i].t -= dt;
	if (g->hintT > 0) g->hintT -= dt;
	if (g->bc.on) g->bc.t += dt;
}

static void check_beacon(Game *g)
{
	Player *p = &g->p;
	if (!g->bc.on) return;
	float dx = g->bc.p[0] - p->p[0], dy = g->bc.p[1] - (p->p[1] + 1.0f), dz = g->bc.p[2] - p->p[2];
	if (dx * dx + dz * dz > BEACON_R * BEACON_R || fabsf(dy) > BEACON_H) return;       // the beam is tall: touch it within BEACON_H of the ring
	g->beacons++;
	if (g->chainT > 0 && g->chain < 5) g->chain++; else if (g->chainT <= 0) g->chain = 1;
	g->chainT = 14.0f;
	int pts = 100 * g->chain;
	g->score += pts;
	char buf[40]; snprintf(buf, sizeof buf, g->mode == MODE_RUN ? "+%d  x%d  +15s" : "+%d  x%d", pts, g->chain);
	game_popup(g, buf, 0xFF40E0FF);
	if (g->mode == MODE_RUN) g->timeLeft = fminf(99.0f, g->timeLeft + 15.0f);
	spawn_beacon(g);
}

void game_title_camera(Game *g, float dt) { (void)g; (void)dt; }

void game_update(Game *g, const Input *in, float dt)
{
	g->time += dt; g->stateT += dt;
	if (g->state == GS_TITLE) {
		Input bi; game_bot(g, &bi, dt, NULL);
		player_update(g, &bi, dt, false);
		cam_update(g, &bi, dt, false);
		update_world(g, dt);
		if (g->showHelp) { if (in->a_p || in->b_p || in->start_p || in->touch_p) g->showHelp = false; return; }
		if (in->start_p) g->quit = true;
		if (in->up_p) g->menuSel = (g->menuSel + 2) % 3;
		if (in->down_p) g->menuSel = (g->menuSel + 1) % 3;
		int pick = in->a_p ? g->menuSel : -1;
		if (in->touch_p && in->tx >= 50 && in->tx <= 270) for (int i = 0; i < 3; i++) if (in->ty >= 78 + i * 46 && in->ty < 78 + i * 46 + 40) { g->menuSel = i; pick = i; }
		if (pick == 0) game_start(g, MODE_FREE);
		else if (pick == 1) game_start(g, MODE_RUN);
		else if (pick == 2) g->showHelp = true;
		return;
	}
	if (g->state == GS_OVER) {
		if (g->stateT > 0.8f && (in->a_p || in->touch_p)) game_start(g, g->mode);
		else if (g->stateT > 0.8f && in->b_p) { g->state = GS_TITLE; g->stateT = 0; game_respawn(g); g->p.state = ST_AIR; g->p.p[1] = 100; g->p.v[0] = 18; }
		Input z; memset(&z, 0, sizeof z);
		cam_update(g, &z, dt, false);
		update_world(g, dt);
		return;
	}
	if (in->start_p) g->paused = !g->paused;
	if (g->paused) {
		if (in->b_p) { g->paused = false; g->state = GS_TITLE; g->stateT = 0; game_respawn(g); g->p.state = ST_AIR; g->p.p[1] = 100; g->p.v[0] = 18; }
		if (in->down_p) { g->invertY = !g->invertY; g->saveNeeded = true; }
		if (in->up_p) { g->showFps = !g->showFps; g->saveNeeded = true; }
		return;
	}
	if (in->select_p) { game_respawn(g); game_popup(g, "Back to the street", 0xFFFFFFFF); }
	player_update(g, in, dt, true);
	cam_update(g, in, dt, true);
	update_world(g, dt);
	check_beacon(g);
	if (g->chainT > 0) { g->chainT -= dt; if (g->chainT <= 0) g->chain = 1; }
	if (g->mode == MODE_RUN) {
		g->timeLeft -= dt;
		if (g->timeLeft <= 0) {
			g->timeLeft = 0; g->state = GS_OVER; g->stateT = 0;
			if (g->score > g->best) { g->best = g->score; g->saveNeeded = true; }
		}
	}
}
