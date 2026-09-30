#include "game.h"
#include <math.h>
#include <string.h>

#define GRAVITY   30.0f
#define JUMP_V    10.5f
#define FAST_FALL 24.0f

// ---- tiny xorshift RNG -------------------------------------------------
static unsigned rnd(Game *g)
{
	unsigned x = g->rng;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return g->rng = x;
}
static int   rndi(Game *g, int n)            { return (int)(rnd(g) % (unsigned)n); }
static float rndf(Game *g, float a, float b) { return a + (b - a) * ((rnd(g) & 0xFFFF) / 65535.0f); }

// ---- objects -----------------------------------------------------------
static void add_obj(Game *g, ObjType t, int lane, float z, float y)
{
	if (g->nobjs >= MAX_OBJS) return;
	g->objs[g->nobjs++] = (Obj){ t, lane, z, y };
}

static void coin_line(Game *g, int lane, float z, int n, float y)
{
	for (int i = 0; i < n; i++) add_obj(g, OBJ_COIN, lane, z + i * 1.5f, y);
}

static int rand_lane(Game *g, int lo, int hi) { return lo + rndi(g, hi - lo + 1); }

static int other_lane(Game *g, int a, int lo, int hi)   // any lane in lo..hi except a (needs two or more lanes)
{
	int l;
	do l = rand_lane(g, lo, hi); while (l == a);
	return l;
}

// Spawns one "row" of the level at distance z, across the lanes lo..hi.
// Every pattern leaves a way through, and never needs more than a lane or two of sideways movement.
// A `calm` row is only coins.
static void spawn_row(Game *g, float z, int lo, int hi, bool calm)
{
	int w = hi - lo + 1;
	int p = rndi(g, 100);
	int a = rand_lane(g, lo, hi);
	if (calm || w < 2) p = 0;
	if (w > 3 && p >= 92) p = 80;                   // the mixed row wants a compact road

	if (p < 18) {                                   // just coins
		coin_line(g, a, z, 5, 0.6f);
	} else if (p < 40) {                            // one rock, coins beside it
		add_obj(g, OBJ_ROCK, a, z, 0);
		coin_line(g, other_lane(g, a, lo, hi), z - 1.5f, 5, 0.6f);
	} else if (p < 54) {                            // several rocks, always with free lanes
		if (w <= 3) {                               // everything but lane a
			for (int l = lo; l <= hi; l++)
				if (l != a) add_obj(g, OBJ_ROCK, l, z, 0);
			coin_line(g, a, z - 1.5f, 5, 0.6f);
		} else {                                    // wide road: every other lane, so each rock has a free lane beside it
			int ph = rndi(g, 2);
			for (int l = lo; l <= hi; l++)
				if (((l - lo) & 1) == ph) add_obj(g, OBJ_ROCK, l, z, 0);
			int free = (w - (1 - ph) + 1) / 2;
			coin_line(g, lo + (1 - ph) + 2 * rndi(g, free), z - 1.5f, 5, 0.6f);
		}
	} else if (p < 67) {                            // hurdles: jump
		int n = 1 + rndi(g, w);
		int start = lo + rndi(g, w - n + 1);
		for (int l = start; l < start + n; l++) {
			add_obj(g, OBJ_HURDLE, l, z, 0);
			for (int i = -1; i <= 1; i++) add_obj(g, OBJ_COIN, l, z + i * 1.6f, 1.35f);
		}
	} else if (p < 80) {                            // petitioners: also jump
		int n = 1 + rndi(g, w < 2 ? w : 2);
		int start = lo + rndi(g, w - n + 1);
		for (int l = start; l < start + n; l++) {
			add_obj(g, OBJ_PETITION, l, z, 0);
			for (int i = -1; i <= 1; i++) add_obj(g, OBJ_COIN, l, z + i * 1.6f, 1.5f);
		}
	} else if (p < 92) {                            // beams: slide
		int n = 1 + rndi(g, w);
		int start = lo + rndi(g, w - n + 1);
		for (int l = start; l < start + n; l++) {
			add_obj(g, OBJ_BEAM, l, z, 0);
			for (int i = -1; i <= 1; i++) add_obj(g, OBJ_COIN, l, z + i * 1.6f, 0.45f);
		}
	} else {                                        // mixed: rock + beam (+ a free lane if there is room)
		int b = other_lane(g, a, lo, hi);
		add_obj(g, OBJ_ROCK, a, z, 0);
		add_obj(g, OBJ_BEAM, b, z, 0);              // a beam can be slid under, so two lanes are still fair
		for (int l = lo; l <= hi; l++)
			if (l != a && l != b) coin_line(g, l, z - 1.5f, 5, 0.6f);
	}
}

// ---- events: turns and the fake theme park -----------------------------
static float smooth01(float x)
{
	if (x <= 0.0f) return 0.0f;
	if (x >= 1.0f) return 1.0f;
	return x * x * (3.0f - 2.0f * x);
}

static bool in_event(const Game *g, EvKind k, float s)
{
	return g->evKind == k && s >= g->evStart && s <= g->evEnd;
}

// A turn's heading swings from 0 to 1 (unit heading) following a smoothstep over `len`.
// turn_h is that heading; turn_phi is its integral, i.e. how far sideways the road has drifted.
static float turn_h(float p, float len) { return smooth01(p / len); }

static float turn_phi(float p, float len)
{
	if (p <= 0.0f) return 0.0f;
	if (p >= len) return len * 0.5f + (p - len);     // afterwards the heading stays at 1
	float x = p / len;
	return len * (x * x * x - 0.5f * x * x * x * x);
}

// The bends of the current event: a turn is one 90 degree swing, the stadium is three (round 1st, 2nd and 3rd).
static int turn_segs(const Game *g, float st[3], float ln[3], float sg[3])
{
	if (g->evKind == EV_TURN) {
		st[0] = g->evStart; ln[0] = TURN_LEN; sg[0] = (float)g->evSide;
		return 1;
	}
	if (g->evKind == EV_STADIUM) {
		for (int i = 0; i < 3; i++) {
			st[i] = g->evStart + STAD_BASE1 + i * STAD_BASE_GAP - STAD_TURN_LEN * 0.5f;
			ln[i] = STAD_TURN_LEN; sg[i] = -1.0f;   // baseball runs anticlockwise
		}
		return 3;
	}
	return 0;
}

float track_heading(const Game *g)
{
	float h = g->baseHeading, st[3], ln[3], sg[3];
	int n = turn_segs(g, st, ln, sg);
	for (int i = 0; i < n; i++) h += sg[i] * TURN_ANGLE * turn_h(g->dist - st[i], ln[i]);
	return h;
}

float track_curve(const Game *g, float z)
{
	float st[3], ln[3], sg[3], sum = 0.0f;
	int n = turn_segs(g, st, ln, sg);
	for (int i = 0; i < n; i++) {
		float p0 = g->dist - st[i], p1 = p0 + z;
		if (p1 <= 0.0f) continue;                   // this bend hasn't started anywhere we can see
		sum += sg[i] * (turn_phi(p1, ln[i]) - turn_phi(p0, ln[i]) - turn_h(p0, ln[i]) * z);   // relative to the way the camera faces
	}
	return sum * TURN_ANGLE * (g->evKind == EV_TURN ? TURN_VIS_GAIN : 1.0f);
}

bool track_bent(const Game *g)
{
	float st[3], ln[3], sg[3];
	int n = turn_segs(g, st, ln, sg);
	for (int i = 0; i < n; i++) {
		float p0 = g->dist - st[i];
		if (p0 < ln[i] && p0 + SPAWN_Z + 10.0f > 0.0f) return true;
	}
	return false;
}

float track_wide(const Game *g, float s)
{
	if (!in_event(g, EV_TURN, s)) return 0.0f;
	float p = s - g->evStart;
	return smooth01((p - (TURN_WIDE_A - 12.0f)) / 12.0f) * (1.0f - smooth01((p - TURN_WIDE_B) / 12.0f));
}

bool lane_exists(const Game *g, int lane, float s)
{
	if (in_event(g, EV_STADIUM, s)) {                   // one lane inside the stadium
		float p = s - g->evStart;
		if (lane == 0) return true;
		return (lane == -1 || lane == 1) && (p < STAD_NARROW_A || p >= STAD_WIDE);
	}
	if (lane >= -1 && lane <= 1) return true;
	if (lane != -2 && lane != 2) return false;
	if (!in_event(g, EV_TURN, s)) return false;
	float p = s - g->evStart;
	return p >= TURN_WIDE_A && p < TURN_WIDE_B;
}

float zone_k(const Game *g, float s)
{
	if (g->evKind != EV_DISNEY) return 0.0f;
	float a = smooth01((s - g->evStart) / DIZZY_FADE);
	float b = smooth01((g->evEnd - s) / DIZZY_FADE);
	return a < b ? a : b;
}

float stadium_k(const Game *g, float s)
{
	if (!in_event(g, EV_STADIUM, s)) return 0.0f;
	float p = s - g->evStart;
	if (p < STAD_GATE + 5.0f) return smooth01((p - (STAD_GATE - 20.0f)) / 25.0f);   // grass and dirt appear as we reach the gate
	if (p < STAD_HOME) return 1.0f;
	return 1.0f - smooth01((p - STAD_HOME) / STAD_FLIGHT);                       // ...and fall away as we are launched out
}

float track_narrow(const Game *g, float s)
{
	if (!in_event(g, EV_STADIUM, s)) return 0.0f;
	float p = s - g->evStart;
	if (p < STAD_NARROW_A) return 0.0f;
	if (p < STAD_GATE) return smooth01((p - STAD_NARROW_A) / (STAD_GATE - STAD_NARROW_A));
	if (p < STAD_LAND - 10.0f) return 1.0f;
	return 1.0f - smooth01((p - (STAD_LAND - 10.0f)) / 20.0f);
}

float flight_y(const Game *g, float s)
{
	if (!in_event(g, EV_STADIUM, s)) return -1.0f;
	float p = s - g->evStart;
	if (p < STAD_HOME || p >= STAD_LAND) return -1.0f;
	float u = (p - STAD_HOME) / STAD_FLIGHT;
	return STAD_FLIGHT_H * 4.0f * u * (1.0f - u);
}

static void schedule_event(Game *g, float from)
{
	static const EvKind kinds[3] = { EV_TURN, EV_DISNEY, EV_STADIUM };
	EvKind prev = g->evKind, k;
	if (prev == EV_NONE || rndi(g, 100) < 25) k = kinds[rndi(g, 3)];
	else do k = kinds[rndi(g, 3)]; while (k == prev);                  // usually something different from last time
	g->evKind = k;
	g->evStart = from + 500.0f + rndf(g, 0.0f, 300.0f);
	g->evEnd = g->evStart + (k == EV_TURN ? TURN_TOTAL : k == EV_DISNEY ? DIZZY_LEN : STAD_TOTAL);
	g->evSide = k == EV_STADIUM ? -1 : (rndi(g, 2) ? 1 : -1);
}

// coins along Tuffy's flight path (he catches them in the air)
static void coin_arc(Game *g, float z)
{
	for (int i = 0; i < 5; i++) {
		float u = (g->dist + z + i * 1.5f - g->evStart - STAD_HOME) / STAD_FLIGHT;
		if (u < 0.0f || u > 1.0f) continue;
		add_obj(g, OBJ_COIN, 0, z + i * 1.5f, STAD_FLIGHT_H * 4.0f * u * (1.0f - u) + 0.5f);
	}
}

// Fills in the level for one row at distance z: 3 lanes normally, 5 while the road is wide after a turn.
static void spawn_level_row(Game *g, float z)
{
	float s = g->dist + z;
	if (in_event(g, EV_STADIUM, s)) {
		float p = s - g->evStart;
		if (p >= STAD_HOME && p < STAD_LAND) { coin_arc(g, z); return; }           // the launch
		if (p >= 130.0f) {                                                          // gate, bases and landing: coins only
			bool one = p >= STAD_NARROW_A && p < STAD_WIDE;
			spawn_row(g, z, one ? 0 : -1, one ? 0 : 1, true);
			return;
		}
	}
	if (in_event(g, EV_TURN, s)) {
		float p = s - g->evStart;
		bool wide = p >= TURN_WIDE_A && p < TURN_WIDE_B;
		bool calm = p >= TURN_CALM_B || (wide && p < TURN_CALM_A);
		spawn_row(g, z, wide ? -2 : -1, wide ? 2 : 1, calm);
	} else {
		spawn_row(g, z, -1, 1, false);
	}
}

static float speed_at(float t) { return 12.0f + fminf(15.0f, t * 0.16f); }

// seconds the road takes to scroll `d` units, starting at game time t0
static float time_to_scroll(float t0, float d)
{
	float t = 0, done = 0;
	while (done < d && t < 60.0f) { done += speed_at(t0 + t) / 30.0f; t += 1.0f / 30.0f; }
	return t;
}

// units the road scrolls in `dur` seconds, starting at game time t0
static float scroll_in(float t0, float dur)
{
	float done = 0;
	for (float t = 0; t < dur; t += 1.0f / 30.0f) done += speed_at(t0 + t) / 30.0f;
	return done;
}

// Sometimes sends a scooter rider up behind Tuffy. The rider's arrival is timed to land in the
// middle of a deliberately widened gap between two obstacle rows, so there is always room to dodge.
// rowZ is the row that was just spawned; returns the spacing to use before the next row.
static float maybe_scooter(Game *g, float rowZ, float gap)
{
	if (g->time < g->nextScooterAt) return gap;
	for (int i = 0; i < g->nobjs; i++)
		if (g->objs[i].type == OBJ_SCOOTER) return gap;         // one at a time
	float rowS = g->dist + rowZ;                                  // no riders while the road is changing width
	if (g->evKind == EV_TURN && rowS > g->evStart + TURN_WIDE_A - 90.0f && rowS < g->evEnd + 40.0f) return gap;
	if (g->evKind == EV_STADIUM && rowS > g->evStart + 40.0f && rowS < g->evEnd + 40.0f) return gap;

	float tRow = time_to_scroll(g->time, rowZ - PLAYER_Z);        // when the row just spawned reaches Tuffy
	float tArrive = tRow + 1.35f;                                 // rider passes Tuffy mid-gap
	add_obj(g, OBJ_SCOOTER, rndi(g, 3) - 1, PLAYER_Z - SCOOTER_SPEED * tArrive, 0);
	g->nextScooterAt = g->time + fmaxf(6.0f, 11.0f - g->time * 0.03f) + rndf(g, 0, 4.0f);
	return scroll_in(g->time + tRow, 2.3f);                       // next row waits until well after he has gone
}

static float row_gap(const Game *g, Game *rw)
{
	// spacing is time-based (0.8 - 1.4 s) so it stays fair as the game speeds up
	return g->speed * rndf(rw, 0.8f, 1.4f);
}

// ---- lifecycle ---------------------------------------------------------
void game_init(Game *g, int best, unsigned seed)
{
	memset(g, 0, sizeof *g);
	g->rng = seed ? seed : 0x1234567u;
	g->best = best;
	g->state = ST_TITLE;
	g->speed = 7.0f;
}

void game_start(Game *g)
{
	int best = g->best;
	unsigned rng = g->rng;
	memset(g, 0, sizeof *g);
	g->rng = rng;
	g->best = best;
	g->state = ST_PLAY;
	g->speed = 12.0f;
	g->nextRow = 30.0f;
	g->nextScooterAt = 14.0f;
	schedule_event(g, -50.0f);                      // first event after roughly 450-750 units
}

int game_score(const Game *g)
{
	return (int)g->dist + g->coins * 25;
}

// ---- input -------------------------------------------------------------
// May Tuffy step into lane `nl` right now? It has to exist.
static bool lane_step_ok(const Game *g, int nl)
{
	return lane_exists(g, nl, g->dist + PLAYER_Z);
}

void game_action(Game *g, Action a)
{
	if (g->state != ST_PLAY) return;
	if (g->flying && (a == ACT_JUMP || a == ACT_SLIDE)) return;        // in mid-air already
	switch (a) {
	case ACT_LEFT:  if (lane_step_ok(g, g->lane - 1)) g->lane--; break;
	case ACT_RIGHT: if (lane_step_ok(g, g->lane + 1)) g->lane++; break;
	case ACT_JUMP:
		if (g->y <= 0.001f) {
			g->vy = JUMP_V;
			g->slideT = 0;
			g->pendingSlide = false;
		}
		break;
	case ACT_SLIDE:
		if (g->y > 0.001f) {                        // in the air: drop fast, then slide
			if (g->vy > -FAST_FALL) g->vy = -FAST_FALL;
			g->pendingSlide = true;
		} else {
			g->slideT = 0.7f;
		}
		break;
	}
}

// ---- update ------------------------------------------------------------
static bool hits(const Game *g, const Obj *o)
{
	float ph   = g->slideT > 0 ? SLIDE_H : STAND_H;
	float top  = g->y + ph;
	float dx   = fabsf(o->lane * LANE_W - g->x);
	float dz   = fabsf(o->z - PLAYER_Z);

	switch (o->type) {
	case OBJ_ROCK:   return dx < 0.85f && dz < 0.75f && g->y < 2.0f;
	case OBJ_HURDLE: return dx < 0.85f && dz < 0.75f && g->y < 0.65f;
	case OBJ_BEAM:   return dx < 0.85f && dz < 0.75f && top > 1.0f && g->y < 1.6f;
	case OBJ_PETITION: return dx < 0.85f && dz < 0.7f && g->y < 1.15f;   // he holds his clipboard out low: jump it
	case OBJ_SCOOTER: return dx < 0.85f && dz < 0.8f;      // can't jump or slide past him
	default:         return false;
	}
}

static void update_player(Game *g, float dt)
{
	// a lane that runs out (the extra lanes when the road narrows again) pushes Tuffy towards the middle
	float sp = g->dist + PLAYER_Z;
	while (g->lane != 0 && !lane_exists(g, g->lane, sp)) g->lane += g->lane > 0 ? -1 : 1;

	// smooth lane change
	float target = g->lane * LANE_W;
	g->x += (target - g->x) * fminf(1.0f, dt * 16.0f);
	if (fabsf(target - g->x) < 0.005f) g->x = target;

	// launched out of the stadium: the flight path is fixed
	float fy = flight_y(g, sp);
	if (fy >= 0.0f) {
		g->flying = true; g->y = fy; g->vy = 0.0f; g->slideT = 0.0f; g->pendingSlide = false;
		return;
	}
	if (g->flying) { g->flying = false; g->y = 0.0f; g->vy = 0.0f; }

	// jump physics
	if (g->y > 0.0f || g->vy != 0.0f) {
		g->vy -= GRAVITY * dt;
		g->y  += g->vy * dt;
		if (g->y <= 0.0f) {
			g->y = 0.0f;
			g->vy = 0.0f;
			if (g->pendingSlide) { g->slideT = 0.7f; g->pendingSlide = false; }
		}
	}
	if (g->slideT > 0) g->slideT -= dt;
}

void game_update(Game *g, float dt)
{
	switch (g->state) {
	case ST_PAUSED:
		return;

	case ST_TITLE:                                  // scenery drifts by, Tuffy jogs
		g->speed = 7.0f;
		g->dist += g->speed * dt;
		g->runPhase += g->speed * dt * 0.95f;
		g->time += dt;
		return;

	case ST_DEAD:
		g->deadT += dt;
		return;

	case ST_PLAY:
		break;
	}

	g->time += dt;
	g->speed = speed_at(g->time);
	float adv = g->speed * dt;
	g->dist += adv;
	g->runPhase += adv * 0.95f;

	update_player(g, dt);

	if (g->evKind != EV_NONE && g->dist > g->evEnd + 10.0f) {
		if (g->evKind == EV_TURN) g->baseHeading = fmodf(g->baseHeading + g->evSide * TURN_ANGLE, 6.2831853f);
		if (g->evKind == EV_STADIUM) g->baseHeading = fmodf(g->baseHeading - 3.0f * TURN_ANGLE, 6.2831853f);
		schedule_event(g, g->evEnd);
	}

	// spawn rows
	g->nextRow -= adv;
	while (g->nextRow <= 0.0f) {
		float rowZ = SPAWN_Z + g->nextRow;
		spawn_level_row(g, rowZ);
		g->nextRow += maybe_scooter(g, rowZ, row_gap(g, g));
	}

	// move objects, handle pickups and crashes
	for (int i = 0; i < g->nobjs; ) {
		Obj *o = &g->objs[i];
		bool remove;
		if (o->type == OBJ_SCOOTER) {              // rides forward past Tuffy and off into the distance
			o->z += SCOOTER_SPEED * dt;
			remove = o->z > SPAWN_Z;
		} else {
			o->z -= adv;
			remove = o->z < 1.2f;
		}

		if (!remove && o->type == OBJ_COIN) {
			float ph = g->slideT > 0 ? SLIDE_H : STAND_H;
			if (fabsf(o->lane * LANE_W - g->x) < 0.8f && fabsf(o->z - PLAYER_Z) < 0.7f &&
			    o->y >= g->y && o->y <= g->y + ph) {
				g->coins++;
				remove = true;
			}
		} else if (!remove && !g->flying && hits(g, o)) {
			g->state = ST_DEAD;
			g->killer = o->type;
			g->deadT = 0;
			if (game_score(g) > g->best) {
				g->best = game_score(g);
				g->newBest = true;
				g->saveNeeded = true;
			}
			return;
		}

		if (remove) g->objs[i] = g->objs[--g->nobjs];
		else i++;
	}
}

bool game_scooter_warning(const Game *g, int *lane, float *eta)
{
	bool found = false;
	float best = 1e9f;
	for (int i = 0; i < g->nobjs; i++) {
		const Obj *o = &g->objs[i];
		if (o->type != OBJ_SCOOTER) continue;
		float e = (PLAYER_Z - o->z) / SCOOTER_SPEED;
		if (e > -0.15f && e <= SCOOTER_WARN && e < best) { best = e; *lane = o->lane; found = true; }
	}
	if (found) *eta = best < 0 ? 0 : best;
	return found;
}
