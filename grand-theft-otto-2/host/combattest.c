// Combat, wanted level and police: scripted scenarios plus a random soak.
// Build: gcc -O2 -o /tmp/combattest host/combattest.c -lm && /tmp/combattest
#define TEST_LOADOUT 0                       // these scenarios are about real ammo and the police taking weapons away
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/traffic.c"
#include "../source/combat.c"
#include "../source/police.c"
#include <stdio.h>
#include <stdlib.h>

static Game G;
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void clear_world(void)
{
	for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false;
	for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
}
static Input idle(void) { Input in; memset(&in, 0, sizeof in); return in; }
static void run(float s, Input in) { for (float t = 0; t < s; t += 1.0f / 60) { game_update(&G, &in, 1.0f / 60); in.enter_p = in.x_p = in.select_p = false; in.weap = 0; } }
static int add_ped(float x, float z)
{
	for (int i = 0; i < MAX_PED; i++) if (!G.ped[i].on) {
		Ped *p = &G.ped[i]; memset(p, 0, sizeof *p); p->on = true; p->hp = 60; p->x = x; p->z = z; p->state = PED_IDLE; p->timer = 100; p->node = p->prev = traffic_nearest_node(&G, x, z);
		return i;
	}
	return -1;
}

static bool inside(float x, float z, float r) { float t = x, u = z, n[2]; return city_push_circle(&G.w, &t, &u, r, n) && (fabsf(t - x) + fabsf(u - z) > 0.05f); }

int main(void)
{
	game_init(&G, false);
	const float SX = 4 * PITCH, SZ = 4 * PITCH + 20;                         // on the north-south street at x = 256

	printf("1. shooting a pedestrian\n");
	clear_world(); game_start(&G); clear_world();
	G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;                // facing north (-z)
	int v = add_ped(SX + 3, SZ - 9);
	Input in = idle(); in.gas = true;
	int ammo0 = G.p.ammo[W_PISTOL];
	run(1.6f, in);
	CHECK(G.ped[v].dead, "the pedestrian should be dead (hp %.0f)", G.ped[v].hp);
	CHECK(G.p.ammo[W_PISTOL] < ammo0, "ammo should go down (%d -> %d)", ammo0, G.p.ammo[W_PISTOL]);
	int cash = 0, decals = 0; for (int i = 0; i < MAX_PICKUP; i++) if (G.pick[i].on && !G.pick[i].fixed && G.pick[i].type == PK_CASH) cash++; for (int i = 0; i < MAX_DECAL; i++) decals += G.decal[i].on;
	CHECK(cash >= 1, "a dead pedestrian should drop cash");
	CHECK(decals >= 1, "there should be a blood stain");
	CHECK(G.heat > 20.0f && G.stars >= 1, "killing someone should give a wanted level (heat %.0f, stars %d)", G.heat, G.stars);
	printf("   ped dead, %d cash drop(s), %d stain(s), heat %.0f, %d star(s), ammo %d -> %d\n", cash, decals, G.heat, G.stars, ammo0, G.p.ammo[W_PISTOL]);

	printf("2. picking up the cash\n");
	Pickup *drop = NULL; for (int i = 0; i < MAX_PICKUP; i++) if (G.pick[i].on && !G.pick[i].fixed) drop = &G.pick[i];
	int c0 = G.p.cash;
	G.p.p[0] = drop->x; G.p.p[2] = drop->z;
	run(0.2f, idle());
	CHECK(G.p.cash > c0, "cash should go up (%d -> %d)", c0, G.p.cash);
	printf("   cash %d -> %d\n", c0, G.p.cash);

	printf("3. the police come (2 stars, Otto stands still for 150 s)\n");
	clear_world(); game_start(&G); clear_world();
	G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.heat = 250; run(0.1f, idle());
	CHECK(G.stars == 1, "the first star comes at once, got %d", G.stars);
	run(15.0f, idle()); CHECK(G.stars >= 2, "and the next one within 15 s, got %d", G.stars);
	float firstNear = -1, busted = -1, wastedAt = -1; int prevBusted = 0;
	int maxFoot = 0, maxCars = 0;
	for (float t = 0; t < 150.0f; t += 1.0f / 60) {
		game_update(&G, &(Input){ 0 }, 1.0f / 60);
		int foot = 0, cars = 0, near = 0;
		for (int i = 0; i < MAX_PED; i++) if (G.ped[i].on && G.ped[i].cop && !G.ped[i].dead) { foot++; if (hypotf(G.ped[i].x - G.p.p[0], G.ped[i].z - G.p.p[2]) < 25) near++; }
		for (int i = 0; i < MAX_CAR; i++) cars += G.car[i].on && G.car[i].bot;
		if (foot > maxFoot) maxFoot = foot; if (cars > maxCars) maxCars = cars;
		if (near && firstNear < 0) firstNear = t;
		if (G.p.ps == PS_BUSTED && !prevBusted) { busted = t; prevBusted = 1; }
		if (G.p.ps == PS_WASTED && wastedAt < 0) wastedAt = t;
		if (busted >= 0 && G.p.ps == PS_ALIVE) break;
		if (wastedAt >= 0 && G.p.ps == PS_ALIVE) break;
	}
	CHECK(firstNear >= 0, "officers should reach Otto within 150 s");
	CHECK(busted >= 0 || wastedAt >= 0, "Otto should be caught or shot eventually");
	printf("   officers up to %d on foot and %d cars; first within 25 m after %.0f s; busted at %.0f s, wasted at %.0f s; after that: ps %d stars %d cash %d at (%.0f, %.0f)\n", maxFoot, maxCars, firstNear, busted, wastedAt, G.p.ps, G.stars, G.p.cash, G.p.p[0], G.p.p[2]);
	if (busted >= 0 || wastedAt >= 0) {
		bool atStation = hypotf(G.p.p[0] - POLICE_X, G.p.p[2] - POLICE_Z) < 14, atHospital = hypotf(G.p.p[0] - HOSPITAL_X, G.p.p[2] - HOSPITAL_Z) < 14;
		CHECK(G.stars == 0 && G.p.ps == PS_ALIVE, "after the respawn the wanted level is gone");
		CHECK(busted >= 0 ? atStation : atHospital, "respawn at the %s", busted >= 0 ? "police station" : "hospital");
	}

	printf("4. WASTED and the hospital\n");
	clear_world(); game_start(&G); clear_world();
	G.p.cash = 1000; G.p.hp = 5; damage_player(&G, 500);
	CHECK(G.p.ps == PS_WASTED, "hp 0 is WASTED");
	run(5.0f, idle());
	CHECK(G.p.ps == PS_ALIVE && G.p.hp > PLAYER_MAX_HP - 1, "respawned healthy (ps %d hp %.0f)", G.p.ps, G.p.hp);
	CHECK(G.p.cash == 900, "10%% of the cash is lost (%d)", G.p.cash);
	CHECK(hypotf(G.p.p[0] - HOSPITAL_X, G.p.p[2] - HOSPITAL_Z) < 14, "at the hospital");
	printf("   cash %d, back at (%.0f, %.0f)\n", G.p.cash, G.p.p[0], G.p.p[2]);

	printf("5. a car blows up\n");
	clear_world(); game_start(&G); clear_world();
	G.p.p[0] = SX + 3; G.p.p[2] = SZ;
	int ci = game_add_car(&G, CAR_SEDAN, SX + 6.4f, SZ - 14, 0);
	int near = add_ped(SX + 3, SZ - 14), far = add_ped(SX + 3, SZ + 40);
	G.p.face = 0; G.cam.yaw = 0;
	damage_car(&G, ci, 200, 1);
	CHECK(G.car[ci].wreck, "the car should be a wreck");
	CHECK(G.ped[near].dead, "a pedestrian next to the car should die (hp %.0f)", G.ped[near].hp);
	CHECK(!G.ped[far].dead, "one far away should not");
	run(2.0f, idle());
	CHECK(G.stars >= 1, "blowing up a car is a crime");
	printf("   wreck %d, near ped dead %d, far ped dead %d, stars %d\n", G.car[ci].wreck, G.ped[near].dead, G.ped[far].dead, G.stars);

	printf("6. the shotgun and the SMG empty their magazines sensibly\n");
	clear_world(); game_start(&G); clear_world();
	G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.has[W_SMG] = G.p.has[W_SHOTGUN] = true; G.p.ammo[W_SMG] = 30; G.p.ammo[W_SHOTGUN] = 8;
	for (int w = W_SMG; w <= W_SHOTGUN; w++) {
		G.p.weapon = w; int a0 = G.p.ammo[w]; in = idle(); in.gas = true; run(1.0f, in);
		printf("   %s: %d rounds in 1 s\n", WPN[w].name, a0 - G.p.ammo[w]);
		CHECK(a0 - G.p.ammo[w] >= 1, "%s should fire", WPN[w].name);
	}

	printf("7. police cars chase a driver (3 stars; Otto drives a sports car in loops for 3 minutes)\n");
	clear_world(); game_start(&G); clear_world();
	{
		int sc = game_add_car(&G, CAR_SPORTS, SX + 3.6f, SZ, 0); G.p.car = sc; G.car[sc].driven = true; G.p.p[0] = G.car[sc].x; G.p.p[2] = G.car[sc].z;
		G.heat = 250; G.stars = 3;
		float closest = 1e9f, stuckMax = 0; static float stuck[MAX_CAR]; int rammed = 0; float hp0 = G.car[sc].hp;
		unsigned rs2 = 99; Input d = idle(); d.gas = true;
		for (float tt = 0; tt < 180.0f; tt += 1.0f / 60) {
			if (((int)(tt * 60)) % 90 == 0) { d.lx = rndf(&rs2) * 1.6f - 0.8f; d.brake = rndf(&rs2) < 0.1f; }
			G.heat = fmaxf(G.heat, 250);                                             // keep the wanted level up so the test measures the chase
			if (G.p.ps != PS_ALIVE) { G.p.ps = PS_ALIVE; G.p.hp = PLAYER_MAX_HP; G.timeScale = 1; }
			if (G.p.car < 0) { G.p.car = sc; G.car[sc].driven = true; }
			G.car[sc].hp = 100;
			game_update(&G, &d, 1.0f / 60);
			for (int i = 0; i < MAX_CAR; i++) {
				const Car *c = &G.car[i];
				if (!c->on || !c->bot || c->wreck) { stuck[i] = 0; continue; }
				float dd = hypotf(c->x - G.car[sc].x, c->z - G.car[sc].z);
				if (dd < closest) closest = dd;
				if (hypotf(c->vx, c->vz) < 1.0f && dd > 25.0f) stuck[i] += 1.0f / 60; else stuck[i] = 0;
				if (stuck[i] > stuckMax) stuckMax = stuck[i];
				if (stuck[i] > 30.0f && stuck[i] < 30.02f) printf("   car %d (kind %d) stuck at (%.1f, %.1f) yaw %.2f target (%.0f,%.0f) botStuck %.1f gas %d brake %d lx %.2f unloaded %d dist %.0f hp %.0f\n", i, c->kind, c->x, c->z, c->yaw, c->botTx, c->botTz, c->botStuck, c->botGas, c->botBrake, c->botLx, c->unloaded, dd, c->hp);
			}
		}
		int bots = 0; for (int i = 0; i < MAX_CAR; i++) bots += G.car[i].on && G.car[i].bot && !G.car[i].wreck;
		printf("   closest police car %.1f m, longest a chasing car sat still far from Otto %.0f s, %d police cars still chasing\n", closest, stuckMax, bots);
		CHECK(closest < 12.0f, "a police car should catch up with the driver (closest %.1f m)", closest);
		CHECK(stuckMax < 25.0f, "chasing police cars should not sit stuck (%.0f s)", stuckMax);
		(void)rammed; (void)hp0;
	}

	printf("8. random play for 20 minutes (walking, driving, shooting, the police)\n");
	game_start(&G);
	unsigned rs = 777; int wasted = 0, busts = 0, maxStars = 0, kills = 0;
	in = idle();
	for (int frame = 0; frame < 60 * 60 * 20; frame++) {
		if (frame % 45 == 0) {
			in.lx = rndf(&rs) * 2 - 1; in.ly = rndf(&rs) * 2 - 1; in.gas = rndf(&rs) < 0.6f; in.brake = rndf(&rs) < 0.12f; in.hand = rndf(&rs) < 0.1f; in.run = rndf(&rs) < 0.5f;
			in.cx = rndf(&rs) < 0.2f ? rndf(&rs) * 2 - 1 : 0; in.x_p = rndf(&rs) < 0.1f;
		}
		in.enter_p = (frame % 400 == 0);
		if (frame % 7200 == 3600) G.heat = fmaxf(G.heat, 250.0f);                 // keep the police busy
		int ps = G.p.ps;
		game_update(&G, &in, 1.0f / 60); in.x_p = false; in.enter_p = false;
		if (G.p.ps != ps) { if (G.p.ps == PS_WASTED) wasted++; if (G.p.ps == PS_BUSTED) busts++; }
		if (G.stars > maxStars) maxStars = G.stars;
		float vals[] = { G.p.p[0], G.p.p[2], G.p.hp, G.p.armor, G.cam.eye[0], G.cam.eye[1], G.cam.eye[2], G.heat };
		for (int i = 0; i < 8; i++) if (!isfinite(vals[i])) { printf("  FAIL: frame %d: NaN in player/camera/heat\n", frame); return 1; }
		CHECK(G.p.hp >= 0 && G.p.hp <= PLAYER_MAX_HP + 0.01f && G.p.armor >= 0, "hp %.1f armor %.1f out of range at frame %d", G.p.hp, G.p.armor, frame);
		CHECK(G.heat >= 0 && G.heat <= 900.01f, "heat %.1f out of range", G.heat);
		{ static float hist[600][MAX_PED][6]; static int hn, shown;
		  for (int i = 0; i < MAX_PED; i++) { const Ped *w = &G.ped[i]; float *h = hist[hn % 600][i]; h[0] = w->x; h[1] = w->z; h[2] = (float)w->state; h[3] = (float)G.stars; h[4] = w->on ? (float)w->cop : -1; h[5] = w->speed; }
		  hn++;
		  for (int i = 0; i < MAX_PED && !shown; i++) { const Ped *w = &G.ped[i]; if (w->on && w->cop && !w->dead && inside(w->x, w->z, 0.05f) && hn > 700) { shown = 1;
		      printf("  officer %d is inside a building at (%.1f, %.1f); its last seconds (every 6 frames, oldest first; state, stars, speed):\n", i, w->x, w->z);
		      for (int k = 120; k >= 0; k -= 6) { float *h = hist[(hn - 1 - k + 6000) % 600][i]; printf("    x %.2f z %.2f state %.0f stars %.0f cop %.0f speed %.1f\n", h[0], h[1], h[2], h[3], h[4], h[5]); } } }
		}
		for (int i = 0; i < MAX_PED; i++) { const Ped *p = &G.ped[i]; if (!p->on) continue; if (!isfinite(p->x) || !isfinite(p->z)) { printf("  FAIL: ped NaN\n"); return 1; } if (p->cop && !p->dead && inside(p->x, p->z, 0.05f)) { if (fails < 8) printf("  FAIL: frame %d: officer %d inside a building at (%.1f, %.1f) state %d cop %d dead %d speed %.1f node %d wp %d losT %.1f player (%.0f,%.0f) car %d\n", frame, i, p->x, p->z, p->state, p->cop, p->dead, p->speed, p->node, p->wpNode, p->losT, G.p.p[0], G.p.p[2], G.p.car); fails++; } }
		for (int i = 0; i < MAX_CAR; i++) { const Car *c = &G.car[i]; if (c->on && (!isfinite(c->x) || !isfinite(c->z) || !isfinite(c->vx))) { printf("  FAIL: car NaN\n"); return 1; } }
		if (fails > 20) break;
	}
	for (int i = 0; i < MAX_PED; i++) kills += G.ped[i].on && G.ped[i].dead;
	printf("   wasted %d times, busted %d times, highest wanted level %d, cash %d, bodies now %d\n", wasted, busts, maxStars, G.p.cash, kills);
	printf("%s (%d failure%s)\n", fails ? "FAILED" : "all combat checks passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
