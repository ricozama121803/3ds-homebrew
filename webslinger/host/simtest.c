// Headless check of the swing physics: the title-screen bot (holds R, steers between random waypoints) plays for a few minutes.
// Prints how much of the time it spent swinging / on the ground / in the water, and fails on NaNs or a bot that never leaves the street.
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include <stdio.h>
#include <stdlib.h>

static Game G;
int main(int argc, char **argv)
{
	int runs = argc > 1 ? atoi(argv[1]) : 6;
	float secs = argc > 2 ? (float)atof(argv[2]) : 120;
	printf("boxes %d, buildings %d\n", G.w.nbox, G.w.nbld);
	int bad = 0;
	for (int r = 0; r < runs; r++) {
		game_init(&G, false); G.rng ^= (unsigned)r * 7919u;
		game_respawn(&G); G.p.p[0] = 4 * PITCH + 3; G.p.p[2] = 3 * PITCH;
		Input in; memset(&in, 0, sizeof in);
		float altSum = 0, tLow = 0, tRope = 0, tGround = 0, tWall = 0, maxv = 0, dist = 0, minAlt = 1e9f, tNoRope = 0; int splashes = 0, ticks = 0;
		float ox = G.p.p[0], oz = G.p.p[2];
		for (float t = 0; t < secs; t += 1 / 30.0f, ticks++) {
			float px = G.p.p[0], pz = G.p.p[2], py = G.p.p[1];
			game_update(&G, &in, 1 / 30.0f);
			Player *p = &G.p;
			if (!isfinite(p->p[0] + p->p[1] + p->p[2] + p->v[0] + p->v[1] + p->v[2])) { printf("NaN at t=%.2f\n", t); bad++; break; }
			if (p->p[1] < py - 100) splashes++;
			if (p->state == ST_GROUND) tGround += 1 / 30.0f; else if (p->state == ST_WALL) tWall += 1 / 30.0f; else if (p->rope) tRope += 1 / 30.0f; else tNoRope += 1 / 30.0f;
			float v = len3(p->v); if (v > maxv) maxv = v;
			dist += sqrtf((p->p[0] - px) * (p->p[0] - px) + (p->p[2] - pz) * (p->p[2] - pz));
			if (p->state != ST_GROUND && p->p[1] < minAlt) minAlt = p->p[1];
			altSum += p->p[1]; if (p->p[1] < 8) tLow += 1 / 30.0f;
			if (G.state == GS_TITLE && G.pop[0].t > 2.1f && strstr(G.pop[0].text, "Splash")) splashes++;
		}
		printf("wall bumps while swinging: %d\n", G.p.bumps);
		printf("run %d: roped %.0fs  free-fall %.0fs  ground %.0fs  wall %.0fs | max speed %.1f m/s, travelled %.0f m (net %.0f), min alt %.1f, avg alt %.0f, time under 8 m %.0fs, splashes %d\n",
			r, tRope, tNoRope, tGround, tWall, maxv, dist, sqrtf((G.p.p[0] - ox) * (G.p.p[0] - ox) + (G.p.p[2] - oz) * (G.p.p[2] - oz)), minAlt, altSum / ticks, tLow, splashes);
		if (tRope < secs * 0.35f) { printf("  FAIL: bot hardly swung\n"); bad++; }
	}
	return bad ? 1 : 0;
}
