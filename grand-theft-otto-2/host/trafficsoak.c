// Traffic and pedestrian soak: Otto stands still for 10 simulated minutes while the computer-driven cars and the pedestrians run.
// Checks that nothing goes NaN, nobody drives or walks into a building, and no car sits still for long (a deadlock at a junction).
// Build: gcc -O2 -o /tmp/trafficsoak host/trafficsoak.c -lm && /tmp/trafficsoak
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/traffic.c"
#include "../source/combat.c"
#include "../source/police.c"
#include <stdio.h>
#include <stdlib.h>

static Game G;
static bool inside(float x, float z, float r) { float t = x, u = z, n[2]; return city_push_circle(&G.w, &t, &u, r, n) && (fabsf(t - x) + fabsf(u - z) > 0.05f); }

int main(int argc, char **argv)
{
	float minutes = argc > 1 ? (float)atof(argv[1]) : 10.0f;
	game_init(&G, false);
	if (argc > 2) { G.rng ^= (unsigned)atoi(argv[2]) * 2654435761u; game_start(&G); }
	{   // carjacking: stop a traffic car, stand next to it and press Y
		int ci = -1; for (int i = 0; i < MAX_CAR; i++) if (G.car[i].on && G.car[i].ai.on) { ci = i; break; }
		Car *c = &G.car[ci]; c->ai.v = 0; c->vx = c->vz = 0;
		int pedsBefore = 0; for (int i = 0; i < MAX_PED; i++) pedsBefore += G.ped[i].on;
		G.p.p[0] = c->x + cosf(c->yaw) * 2.8f; G.p.p[2] = c->z + sinf(c->yaw) * 2.8f; G.p.car = -1;
		Input y; memset(&y, 0, sizeof y); y.enter_p = true;
		game_update(&G, &y, 1.0f / 60);
		int pedsAfter = 0; for (int i = 0; i < MAX_PED; i++) pedsAfter += G.ped[i].on;
		printf("carjack: in car %d (expected %d), traffic flag %d, driver ejected: %s\n", G.p.car, ci, G.car[ci].ai.on, pedsAfter == pedsBefore + 1 ? "yes" : "NO");
		if (G.p.car != ci || G.car[ci].ai.on || pedsAfter != pedsBefore + 1) return 1;
		Input e; memset(&e, 0, sizeof e); e.enter_p = true; game_update(&G, &e, 1.0f / 60); G.p.car = -1; G.car[ci].on = false;                  // take the stolen car off the road so it does not block the lane for the soak
	}
	G.p.p[0] = 2 * PITCH + 32; G.p.p[2] = 3 * PITCH + 32;                   // Otto waits in the park, out of the traffic's way
	Input in; memset(&in, 0, sizeof in);
	static float still[MAX_CAR], px[MAX_CAR], pz[MAX_CAR]; static int prevMode[MAX_CAR];
	float maxStill = 0, dist = 0; int fails = 0, aiCars = 0, peds = 0, downs = 0, turns = 0, minCars = 99, crashes = 0;
	int frames = (int)(60 * 60 * minutes);
	for (int frame = 0; frame < frames; frame++) {
		int before = 0; for (int i = 0; i < MAX_CAR; i++) before += G.car[i].on && G.car[i].ai.on;
		game_update(&G, &in, 1.0f / 60);
		aiCars = 0; peds = 0;
		for (int i = 0; i < MAX_CAR; i++) {
			Car *c = &G.car[i];
			if (!c->on || !c->ai.on) { still[i] = 0; continue; }
			aiCars++;
			if (!isfinite(c->x) || !isfinite(c->z)) { printf("AI car NaN\n"); return 1; }
			float d = sqrtf((c->x - px[i]) * (c->x - px[i]) + (c->z - pz[i]) * (c->z - pz[i]));
			if (d < 5.0f) dist += d;
			px[i] = c->x; pz[i] = c->z;
			if (c->ai.mode == 1 && prevMode[i] == 0) turns++;
			prevMode[i] = c->ai.mode;
			still[i] = c->ai.v < 0.2f ? still[i] + 1.0f / 60 : 0;
			if (still[i] > maxStill) maxStill = still[i];
			if (still[i] > 45.0f) { if (fails < 6) printf("frame %d: AI car %d stopped for %.0f s at (%.0f, %.0f) axis %d dir %d line %d mode %d plan %d\n", frame, i, still[i], c->x, c->z, c->ai.axis, c->ai.dir, c->ai.line, c->ai.mode, c->ai.plan); fails++; still[i] = 0; }
			float cx[3], cz[3]; car_circles(c, cx, cz);
			for (int k = 0; k < 3; k++) if (inside(cx[k], cz[k], CAR_R * 0.8f)) { if (fails < 6) printf("frame %d: AI car %d inside a building (%.1f, %.1f) mode %d\n", frame, i, cx[k], cz[k], c->ai.mode); fails++; break; }
		}
		if (aiCars < before) {
			crashes += before - aiCars;
			static int shown; 
			if (shown < 4) { shown++; printf("frame %d: traffic car(s) dropped out. Cars near each other:\n", frame);
				for (int i = 0; i < MAX_CAR; i++) { Car *a = &G.car[i]; if (!a->on) continue; for (int j = i + 1; j < MAX_CAR; j++) { Car *b = &G.car[j]; if (!b->on) continue; float d = hypotf(a->x - b->x, a->z - b->z); if (d < 6.0f && (a->ai.on || b->ai.on || a->ai.v > 0 || b->ai.v > 0)) printf("  %d (ai %d axis %d dir %d line %d mode %d plan %d v %.1f s %.1f) vs %d (ai %d axis %d dir %d line %d mode %d plan %d v %.1f s %.1f) d %.1f\n", i, a->ai.on, a->ai.axis, a->ai.dir, a->ai.line, a->ai.mode, a->ai.plan, a->ai.v, a->ai.s, j, b->ai.on, b->ai.axis, b->ai.dir, b->ai.line, b->ai.mode, b->ai.plan, b->ai.v, b->ai.s, d); } } }
		}
		if (aiCars < minCars && frame > 600) minCars = aiCars;
		for (int i = 0; i < MAX_PED; i++) {
			Ped *p = &G.ped[i];
			if (!p->on) continue;
			peds++; if (p->state == PED_DOWN) downs++;
			if (!isfinite(p->x) || !isfinite(p->z)) { printf("ped NaN\n"); return 1; }
			if (inside(p->x, p->z, 0.1f)) { if (fails < 6) { float tx, tz; traffic_node_xy(p->node, &tx, &tz); float ux, uz; traffic_node_xy(p->prev, &ux, &uz); printf("frame %d: ped %d inside a building (%.1f, %.1f) state %d node %d (%.1f,%.1f) prev %d (%.1f,%.1f) jit %.2f %.2f\n", frame, i, p->x, p->z, p->state, p->node, tx, tz, p->prev, ux, uz, p->jx, p->jz); } fails++; }
		}
	}
	printf("traffic soak (%.0f min): %d AI cars (fewest %d), mean speed %.1f m/s, %d junction turns, longest stop %.0f s, %d cars left the traffic (crash or taken), %d peds, %d down frames, failures %d\n",
	       minutes, aiCars, minCars, dist / (aiCars ? aiCars : 1) / (minutes * 60.0f), turns, maxStill, crashes, peds, downs, fails);
	return fails ? 1 : 0;
}
