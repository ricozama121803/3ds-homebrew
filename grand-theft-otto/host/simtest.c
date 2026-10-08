// Headless gameplay tests: traffic soak, police chases, driving fuzz, combat and timing. Run on the PC.
#include "../source/world.c"
#include "../source/nav.c"
#include "../source/game.c"
#include "../source/car.c"
#include "../source/ped.c"
#include "../source/combat.c"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned char mapdata[1 << 20];
static Game G;
static int fails = 0;
#define FAIL(...) do { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } while (0)

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static bool solid_at(const Game *g, float x, float y) { return world_flags(&g->world, (int)floorf(x / TILE), (int)floorf(y / TILE)) & F_SOLID; }

// every entity must be in the world, finite, and not embedded in a wall
static int violations(Game *g, const char *where)
{
	int bad = 0;
	for (int i = 0; i < MAX_CARS; i++) {
		Car *c = &g->cars[i];
		if (!c->active) continue;
		if (!isfinite(c->x) || !isfinite(c->y) || !isfinite(c->vx) || !isfinite(c->vy) || !isfinite(c->heading)) { bad++; if (bad < 4) printf("  [%s] car %d not finite\n", where, i); continue; }
		if (c->x < 0 || c->y < 0 || c->x > g->world.w * TILE || c->y > g->world.h * TILE) { bad++; if (bad < 4) printf("  [%s] car %d outside the map\n", where, i); continue; }
		if (solid_at(g, c->x, c->y) && !(c->model == M_HELI && c->alt > 0.3f)) { bad++; if (bad < 4) printf("  [%s] car %d (model %d state %d) centre inside a wall at %.0f,%.0f\n", where, i, c->model, c->state, c->x, c->y); }
	}
	for (int i = 0; i < MAX_PEDS; i++) {
		Ped *p = &g->peds[i];
		if (!p->active) continue;
		if (!isfinite(p->x) || !isfinite(p->y)) { bad++; continue; }
		if (solid_at(g, p->x, p->y)) { bad++; if (bad < 4) printf("  [%s] ped %d (kind %d) inside a wall at %.0f,%.0f\n", where, i, p->kind, p->x, p->y); }
	}
	Player *p = &g->p;
	if (!isfinite(p->x) || !isfinite(p->y) || (p->car < 0 && solid_at(g, p->x, p->y))) { bad++; printf("  [%s] player invalid at %.1f,%.1f\n", where, p->x, p->y); }
	return bad;
}


static int count_cars(const Game *g, CarState st) { int n = 0; for (int i = 0; i < MAX_CARS; i++) if (g->cars[i].active && g->cars[i].state == st) n++; return n; }
static int count_peds(const Game *g, PedKind k) { int n = 0; for (int i = 0; i < MAX_PEDS; i++) if (g->peds[i].active && g->peds[i].kind == k && g->peds[i].state != PS_DEAD) n++; return n; }

// ---------------------------------------------------------------- 1. quiet city soak: 10 game-minutes, player standing still
static void test_soak(size_t mapsize, int where)
{
	game_init(&G, mapdata, mapsize, NULL);
	static const int spots[3][2] = {{92, 57}, {90, 95}, {44, 104}};
	G.p.x = spots[where][0] * 16 + 8; G.p.y = spots[where][1] * 16 + 8;
	Input in = {0};
	int bad = 0, maxcars = 0, maxpeds = 0; long stuck = 0, movingSum = 0, movingN = 0;
	double t0 = now_us();
	const int frames = 30 * 60 * 10;
	for (int f = 0; f < frames; f++) {
		game_update(&G, &in, 1.0f / 30.0f);
		if (f % 15 == 0) bad += violations(&G, "soak");
		int nc = 0, np = 0;
		for (int i = 0; i < MAX_CARS; i++) if (G.cars[i].active) { nc++; if (G.cars[i].state == CS_TRAFFIC) { float v = sqrtf(G.cars[i].vx * G.cars[i].vx + G.cars[i].vy * G.cars[i].vy); movingSum += v > 5; movingN++; if (G.cars[i].stuckT > 5.5f) stuck++; } }
		for (int i = 0; i < MAX_PEDS; i++) if (G.peds[i].active) np++;
		if (nc > maxcars) maxcars = nc; if (np > maxpeds) maxpeds = np;
	}
	double us = (now_us() - t0) / frames;
	printf("soak @%d,%d: %d violations, max %d cars / %d people, traffic cars moving %.0f%% of the time, %ld near-stuck samples, %.0f us/frame\n",
	       spots[where][0], spots[where][1], bad, maxcars, maxpeds, movingN ? 100.0 * movingSum / movingN : 0.0, stuck, us);
	if (bad) FAIL("entities ended up in walls or invalid");
	if (movingN && movingSum * 100 < movingN * 60) FAIL("traffic is mostly not moving (%.0f%%)", 100.0 * movingSum / movingN);
	if (maxcars > MAX_CARS || maxpeds > MAX_PEDS) FAIL("entity caps exceeded");
}

// ---------------------------------------------------------------- 2. a fleeing player on foot vs the police
static void bot_run(Game *g, Input *in, unsigned *rng, float t)
{
	static float dirx, diry, changeT;
	changeT -= 1.0f / 30.0f;
	// look ahead; turn away from walls
	float fx = dirx * 26, fy = diry * 26;
	bool blocked = world_blocked(&g->world, g->p.x + fx, g->p.y + fy, 6.0f);
	if (changeT <= 0 || blocked) {
		*rng = *rng * 1664525u + 1013904223u; float a = ((*rng >> 8) & 0xFFFF) / 65535.0f * 6.2831853f;
		dirx = cosf(a); diry = sinf(a); changeT = 1.0f + ((*rng >> 4) & 7) * 0.4f;
	}
	in->mx = dirx; in->my = diry; in->run = true; (void)t;
}

static void test_foot_chase(size_t mapsize)
{
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 100 * 16 + 8; G.p.y = 92 * 16 + 8;
	add_heat(&G, 10.0f);                                             // 3 stars
	Input in = {0}; unsigned rng = 77;
	float busted_at = -1, wasted_at = -1, maxstars = 0; int cops_seen_max = 0, cars_seen_max = 0, bad = 0;
	const int frames = 30 * 90;
	double t0 = now_us();
	for (int f = 0; f < frames; f++) {
		bot_run(&G, &in, &rng, f / 30.0f);
		if (G.p.status != PL_ALIVE && busted_at < 0 && wasted_at < 0) { if (G.p.status == PL_BUSTED) busted_at = f / 30.0f; else wasted_at = f / 30.0f; }
		game_update(&G, &in, 1.0f / 30.0f);
		if (f % 10 == 0) bad += violations(&G, "foot chase");
		if (G.stars > maxstars) maxstars = G.stars;
		int cops = count_peds(&G, PK_COP) + count_peds(&G, PK_SWAT), cars = count_cars(&G, CS_CHASE);
		if (cops > cops_seen_max) cops_seen_max = cops; if (cars > cars_seen_max) cars_seen_max = cars;
	}
	double us = (now_us() - t0) / frames;
	printf("foot chase (3 stars, player runs): max %d cops on foot, %d cop cars; %s; player %s; %d violations; %.0f us/frame\n", cops_seen_max, cars_seen_max,
	       busted_at >= 0 ? "BUSTED" : wasted_at >= 0 ? "WASTED" : "survived 90 s", "ended alive", bad, us);
	if (cops_seen_max < 2) FAIL("police never showed up");
	if (bad) FAIL("invalid positions during the chase");
}

// ---------------------------------------------------------------- 3. police catch a player who stands still
static void test_bust(size_t mapsize)
{
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 100 * 16 + 8; G.p.y = 92 * 16 + 8; G.p.weapon = W_FISTS;
	add_heat(&G, 10.0f);
	Input in = {0};
	float t_bust = -1;
	for (int f = 0; f < 30 * 120; f++) {
		game_update(&G, &in, 1.0f / 30.0f);
		if (G.p.status == PL_BUSTED && t_bust < 0) { t_bust = f / 30.0f; break; }
		if (G.p.status == PL_DEAD) { t_bust = -2; break; }
	}
	printf("standing still with 3 stars: %s at %.1f s\n", t_bust >= 0 ? "busted" : t_bust == -2 ? "killed" : "NOT caught", t_bust);
	if (t_bust < 0 && t_bust != -2) FAIL("the police never arrested a player standing still");
}

// ---------------------------------------------------------------- 4. driving fuzz: a stolen car, random steering, and the police chasing
static void test_driving(size_t mapsize, int seed)
{
	game_init(&G, mapdata, mapsize, NULL);
	G.rng ^= seed * 2654435761u;
	G.p.x = 100 * 16 + 8; G.p.y = 93 * 16;
	static const int sx[3] = {1400, 700, 2050}, sy[3] = {1648, 1648, 1648};            // three spots on the big east-west street
	int ci = car_spawn(&G, 5, sx[seed % 3], sy[seed % 3], PI_F * 0.5f, CS_PARKED, DRV_NONE);       // a sports car on the road
	G.p.x = G.cars[ci].x; G.p.y = G.cars[ci].y - 12; G.camx = G.p.x - 200; G.camy = G.p.y - 120;
	Input in = {0}; in.y_p = true;
	game_update(&G, &in, 1.0f / 30.0f);
	if (G.p.car < 0) { FAIL("could not get into the car"); return; }
	add_heat(&G, seed % 2 ? 10.0f : 3.0f);
	unsigned rng = 1234 + seed; float steer = 0, steerT = 0, backT = 0; int bad = 0, hits = 0; float maxv = 0, dist = 0; float px = G.cars[G.p.car].x, py = G.cars[G.p.car].y;
	bool exploded = false;
	for (int f = 0; f < 30 * 120; f++) {
		Input k = {0};
		if (G.p.car < 0) break;
		Car *c = &G.cars[G.p.car];
		steerT -= 1.0f / 30.0f;
		if (steerT <= 0) { rng = rng * 1664525u + 1013904223u; steer = (((rng >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 2.0f; steerT = 0.4f + ((rng >> 4) & 7) * 0.2f; }
		float v = sqrtf(c->vx * c->vx + c->vy * c->vy);
		if (backT > 0) { backT -= 1.0f / 30.0f; k.b = true; k.mx = -steer; }
		else { k.a = true; k.mx = steer; if (v < 8.0f && f > 40) { backT = 0.8f; } }
		if (f % 90 == 0) k.l = true;
		game_update(&G, &k, 1.0f / 30.0f);
		if (G.p.car >= 0) { c = &G.cars[G.p.car]; float vv = sqrtf(c->vx * c->vx + c->vy * c->vy); if (vv > maxv) maxv = vv; dist += sqrtf((c->x - px) * (c->x - px) + (c->y - py) * (c->y - py)); px = c->x; py = c->y; }
		if (f % 6 == 0) bad += violations(&G, "driving");
		if (c->state == CS_WRECK) exploded = true;
		if (G.p.status != PL_ALIVE) { exploded = true; break; }
		(void)hits;
	}
	printf("driving fuzz #%d: top speed %.0f px/s, drove %.0f px, %d violations%s\n", seed, maxv, dist, bad, exploded ? ", car wrecked / player caught" : "");
	if (bad) FAIL("driving produced invalid positions");
}

// ---------------------------------------------------------------- 5. combat
static void test_combat(size_t mapsize)
{
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 92 * 16 + 8; G.p.y = 57 * 16 + 8; G.p.heading = PI_F * 0.5f;        // facing east
	int pi_ = ped_spawn(&G, PK_CIV, 0, G.p.x + 60, G.p.y);
	G.peds[pi_].state = PS_WALK; G.peds[pi_].turnT = 1000;
	Input in = {0}; in.a = true;
	int shots = 0; float dead_at = -1;
	for (int f = 0; f < 30 * 6 && dead_at < 0; f++) {
		int before = G.p.ammo[W_PISTOL];
		game_update(&G, &in, 1.0f / 30.0f);
		if (G.p.ammo[W_PISTOL] < before) shots++;
		G.peds[pi_].x = G.p.x + 60; G.peds[pi_].y = G.p.y; G.peds[pi_].vx = G.peds[pi_].vy = 0;   // keep the target in place
		if (G.peds[pi_].state == PS_DEAD) dead_at = f / 30.0f;
	}
	int parts = 0; for (int i = 0; i < MAX_PARTS; i++) if (G.parts[i].active && G.parts[i].type == P_BLOOD) parts++;
	printf("combat: pistol killed a pedestrian with %d shots (expected 2), blood drops in the air: %d, stains: %d, heat %.1f (%d stars)\n", shots, parts, G.stain_n, G.heat, G.stars);
	if (dead_at < 0) FAIL("the pedestrian never died");
	if (shots < 2 || shots > 3) FAIL("unexpected number of shots");
	if (G.stain_n == 0) FAIL("no blood stains");
	if (G.stars < 1) FAIL("killing a pedestrian should raise the wanted level");
	// bullets stop at walls
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 100 * 16 + 8; G.p.y = 92 * 16 + 4 * 16; G.p.heading = 0; G.p.ammo[W_PISTOL] = 30; G.p.weapon = W_PISTOL;
	memset(&in, 0, sizeof in); in.a = true;
	for (int f = 0; f < 60; f++) game_update(&G, &in, 1.0f / 30.0f);
	for (int i = 0; i < MAX_BULLETS; i++) if (G.bullets[i].active && solid_at(&G, G.bullets[i].x, G.bullets[i].y)) FAIL("a bullet is flying inside a wall");
	// armor absorbs damage
	game_init(&G, mapdata, mapsize, NULL);
	G.p.armor = 50; damage_player(&G, 20.0f, 1, 0);
	printf("armor test: 20 damage with 50 armor -> health %.0f, armor %.0f\n", G.p.hp, G.p.armor);
	if (G.p.hp < 96.3f || G.p.hp > 96.5f || G.p.armor > 44.7f || G.p.armor < 44.5f) FAIL("armor math is off");
}

// ---------------------------------------------------------------- 6. stealing a parked car
static void test_steal(size_t mapsize)
{
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 1400; G.p.y = 1648 - 12;                                    // Wilshire Blvd, an open street
	int ci = car_spawn(&G, 0, 1400, 1648, PI_F * 0.5f, CS_PARKED, DRV_NONE);
	G.camx = G.p.x - 200; G.camy = G.p.y - 120;
	Input in = {0}; in.y_p = true;
	game_update(&G, &in, 1.0f / 30.0f);
	printf("steal: player %s the parked car; heat %.1f\n", G.p.car == ci ? "entered" : "could NOT enter", G.heat);
	if (G.p.car != ci) FAIL("could not enter a parked car right next to the player");
	in.y_p = false; in.a = true;
	for (int i = 0; i < 60; i++) game_update(&G, &in, 1.0f / 30.0f);
	Car *c = &G.cars[ci];
	float v = sqrtf(c->vx * c->vx + c->vy * c->vy);
	printf("       after 2 s of gas the car is doing %.0f px/s\n", v);
	if (v < 100) FAIL("car did not accelerate properly");
	// getting out again
	Input out = {0}; out.y_p = true;
	for (int i = 0; i < 90; i++) { Input brk = {0}; brk.b = true; game_update(&G, &brk, 1.0f / 30.0f); }
	game_update(&G, &out, 1.0f / 30.0f);
	printf("       braked to a stop and got out: player %s\n", G.p.car < 0 ? "is on foot" : "is STILL in the car");
	if (G.p.car >= 0) FAIL("could not exit the car");
}

// ---------------------------------------------------------------- 7. healing, new weapons, pickups, helicopters
static void test_new_stuff(size_t mapsize)
{
	// automatic healing: hurt, wait, and health creeps back up (but not while being shot at)
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 1400; G.p.y = 1648 - 12;
	damage_player(&G, 60.0f, 1, 0);
	float hurt = G.p.hp; Input idle = {0};
	for (int i = 0; i < 30 * 3; i++) game_update(&G, &idle, 1.0f / 30.0f);
	float after3 = G.p.hp;
	for (int i = 0; i < 30 * 20; i++) game_update(&G, &idle, 1.0f / 30.0f);
	printf("healing: hp %.1f right after the hit, %.1f after 3 s, %.1f after 23 s\n", hurt, after3, G.p.hp);
	if (after3 > hurt + 0.1f) FAIL("healing started too soon");
	if (G.p.hp < hurt + 10.0f) FAIL("health does not come back by itself");

	// every weapon fires, uses ammo and leaves nothing inside a wall
	for (int w = W_PISTOL; w < W_COUNT; w++) {
		game_init(&G, mapdata, mapsize, NULL);
		G.p.x = 100 * 16 + 8; G.p.y = 92 * 16 + 64; G.p.heading = 0;
		G.p.has[w] = true; G.p.weapon = w; G.p.ammo[w] = weapon_defs[w].maxammo;
		Input in = {0}; in.a = true;
		int before = G.p.ammo[w];
		for (int f = 0; f < 90; f++) { game_update(&G, &in, 1.0f / 30.0f); }
		printf("weapon %-13s: %3d rounds used in 3 s, violations %d\n", weapon_defs[w].name, before - G.p.ammo[w], violations(&G, "weapons"));
		if (G.p.ammo[w] >= before) FAIL("a weapon did not use any ammo");
	}
	// the RPG kills a crowd standing together
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 100 * 16 + 8; G.p.y = 92 * 16 + 64; G.p.heading = 0; G.p.has[W_RPG] = true; G.p.weapon = W_RPG; G.p.ammo[W_RPG] = 3;
	int ids[3];
	for (int k = 0; k < 3; k++) ids[k] = ped_spawn(&G, PK_CIV, 0, G.p.x + (k - 1) * 8.0f, G.p.y - 90.0f);
	Input fire = {0}; fire.a = true;
	for (int f = 0; f < 60; f++) { game_update(&G, &fire, 1.0f / 30.0f); fire.a = false; }
	int dead = 0; for (int k = 0; k < 3; k++) if (ids[k] >= 0 && G.peds[ids[k]].state == PS_DEAD) dead++;
	printf("rpg: %d of 3 pedestrians killed by one rocket\n", dead);
	if (dead < 1) FAIL("the rocket did nothing");

	// a random pickup appears near the player every few seconds
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 100 * 16 + 8; G.p.y = 92 * 16 + 64;
	int base = 0; for (int i = 0; i < MAX_PICKUPS; i++) if (G.pickups[i].active && G.pickups[i].temp) base++;
	for (int f = 0; f < 30 * 30; f++) game_update(&G, &idle, 1.0f / 30.0f);
	int temps = 0; for (int i = 0; i < MAX_PICKUPS; i++) if (G.pickups[i].active && G.pickups[i].temp) temps++;
	printf("pickups: %d random ones lying around after 30 s (was %d)\n", temps, base);
	if (temps < 4) FAIL("random pickups are not turning up");

	// helicopter: get in, take off, fly forward, land, get out
	game_init(&G, mapdata, mapsize, NULL);
	G.p.x = 90 * 16 + 8; G.p.y = 73 * 16 + 10;
	int hi = car_spawn(&G, NCARVARS - 1, G.p.x, G.p.y - 14, 0, CS_PARKED, DRV_NONE);
	Input use = {0}; use.y_p = true;
	game_update(&G, &use, 1.0f / 30.0f);
	if (G.p.car != hi) { FAIL("could not climb into the helicopter"); return; }
	float x0 = G.cars[hi].x, y0 = G.cars[hi].y;
	Input fly = {0}; fly.a = true;
	for (int f = 0; f < 30 * 4; f++) { game_update(&G, &fly, 1.0f / 30.0f); if (f % 10 == 0 && violations(&G, "heli")) break; }
	float dist = hypotf(G.cars[hi].x - x0, G.cars[hi].y - y0);
	printf("heli: altitude %.2f, flew %.0f px north in 4 s\n", G.cars[hi].alt, dist);
	if (G.cars[hi].alt < 0.95f || dist < 150.0f) FAIL("helicopter did not take off and fly");
	int tilex = (int)(G.cars[hi].x / 16), tiley = (int)(G.cars[hi].y / 16);
	G.cars[hi].x = 90 * 16 + 8; G.cars[hi].y = 72 * 16 + 8; G.cars[hi].vx = G.cars[hi].vy = 0;      // back over the open forecourt
	Input land = {0}; land.y_p = true;
	game_update(&G, &land, 1.0f / 30.0f);
	for (int f = 0; f < 30 * 6; f++) { Input brk = {0}; brk.b = true; game_update(&G, &brk, 1.0f / 30.0f); }
	printf("heli: after landing request (over tile %d,%d): player is %s, alt %.2f\n", tilex, tiley, G.p.car < 0 ? "on foot" : "still aboard", G.cars[hi].alt);
	if (G.p.car >= 0 && G.cars[hi].alt > 0.02f) FAIL("helicopter never came down");
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "assets/map.bin", "rb");
	if (!f) { fprintf(stderr, "cannot open map\n"); return 2; }
	size_t n = fread(mapdata, 1, sizeof mapdata, f); fclose(f);
	printf("== soak (traffic + pedestrians, nobody playing) ==\n");
	for (int w = 0; w < 3; w++) test_soak(n, w);
	printf("== steal a car ==\n");   test_steal(n);
	printf("== combat ==\n");        test_combat(n);
	printf("== police ==\n");        test_bust(n); test_foot_chase(n);
	printf("== healing, weapons, pickups, helicopter ==\n"); test_new_stuff(n);
	printf("== driving fuzz ==\n");  for (int s = 0; s < 6; s++) test_driving(n, s);
	printf(fails ? "RESULT: %d problem(s)\n" : "RESULT: all checks passed\n", fails);
	return fails ? 1 : 0;
}
