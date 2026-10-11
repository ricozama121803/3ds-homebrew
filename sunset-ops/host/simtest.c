// Headless checks: map/nav sanity, then bot-vs-bot matches (the "player" is a bot too) to see that the match finishes, scores are balanced
// and nobody gets stuck. Build: gcc -O2 -o simtest host/simtest.c -lm
#include "../source/game.h"
#include "../source/map.c"
#include "../source/game.c"
#include <stdio.h>
#include <stdlib.h>
static Game G;
int main(int argc, char **argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 6, bad = 0;
	game_init(&G, 1);
	printf("boxes %d (solid %d), nav %d, spawns %d, pickups %d\n", G.w.nbox, G.w.nsolid, G.w.nnav, G.w.nspawn, G.w.npick);
	int freec = 0; for (int z = 0; z < GRID_H; z++) for (int x = 0; x < GRID_W; x++) freec += G.free_cell[z][x];
	printf("free cells %d / %d\n", freec, GRID_W * GRID_H);
	for (int i = 0; i < G.w.nnav; i++) {
		int cx = (int)(G.w.nav[i].x + ARENA_X), cz = (int)(G.w.nav[i].z + ARENA_Z);
		if (!G.free_cell[cz][cx]) { printf("nav %d (%.0f,%.0f) is not on a free cell\n", i, G.w.nav[i].x, G.w.nav[i].z); bad++; continue; }
		for (int j = 0; j < G.w.nnav; j++) {
			int sx = (int)(G.w.nav[j].x + ARENA_X), sz = (int)(G.w.nav[j].z + ARENA_Z);
			if (G.navd[i][sz][sx] == 0xFFFF) { printf("nav %d unreachable from nav %d\n", i, j); bad++; }
		}
	}
	for (int i = 0; i < G.w.nspawn; i++) {
		if (world_blocked(&G.w, G.w.spawn[i].x, G.w.spawn[i].z, PLAYER_R)) { printf("spawn %d blocked\n", i); bad++; }
		int cx = (int)(G.w.spawn[i].x + ARENA_X), cz = (int)(G.w.spawn[i].z + ARENA_Z);
		if (G.navd[0][cz][cx] == 0xFFFF) { printf("spawn %d not connected\n", i); bad++; }
	}
	for (int i = 0; i < G.w.npick; i++) if (world_blocked(&G.w, G.w.pick[i].x, G.w.pick[i].z, 0.3f)) { printf("pickup %d blocked\n", i); bad++; }
	Input in; memset(&in, 0, sizeof in);
	for (int r = 0; r < rounds; r++) {
		G.rng = 1000 + r * 7919; game_new_match(&G); G.autoplay = true; G.state = GS_PLAY;
		float t = 0; int frames = 0; float still[MAX_ACT] = {0}; float maxstill = 0; float lx[MAX_ACT], lz[MAX_ACT];
		for (int i = 0; i < MAX_ACT; i++) { lx[i] = G.a[i].x; lz[i] = G.a[i].z; }
		while (G.state == GS_PLAY && t < 700) {
			game_update(&G, &in, 1.0f / 30.0f); t += 1.0f / 30.0f; frames++;
			if (frames % 90 == 0) for (int i = 0; i < MAX_ACT; i++) {
				if (!G.a[i].alive) { still[i] = 0; continue; }
				float d = hypotf(G.a[i].x - lx[i], G.a[i].z - lz[i]);
				still[i] = d < 0.4f ? still[i] + 3 : 0;
				if (still[i] > maxstill) maxstill = still[i]; if (still[i] == 15) printf("  stuck: actor %d at (%.1f,%.1f) wp %d target %d t=%.0f\n", i, G.a[i].x, G.a[i].z, G.a[i].wp, G.a[i].target, t);
				lx[i] = G.a[i].x; lz[i] = G.a[i].z;
			}
			for (int i = 0; i < MAX_ACT; i++) if (!isfinite(G.a[i].x) || !isfinite(G.a[i].z) || world_blocked(&G.w, G.a[i].x, G.a[i].z, PLAYER_R * 0.8f)) { printf("actor %d in a wall / NaN at t=%.1f (%.2f,%.2f)\n", i, t, G.a[i].x, G.a[i].z); bad++; G.a[i].x = G.w.spawn[0].x; G.a[i].z = G.w.spawn[0].z; }
		}
		printf("round %d: %.0fs  allies %.0f - enemies %.0f  | kills/deaths:", r, t, G.score[0], G.score[1]);
		for (int i = 0; i < MAX_ACT; i++) printf(" %d/%d", G.a[i].kills, G.a[i].deaths);
		printf("  | longest stand-still %.0fs\n", maxstill);
		if (maxstill > 25) bad++;
	}
	printf(bad ? "FAIL (%d)\n" : "OK\n", bad);
	return bad != 0;
}
