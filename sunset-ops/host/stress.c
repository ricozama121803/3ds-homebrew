// Random-input stress: a "player" mashing every button for several matches while the bots fight, checking that nothing goes NaN, nobody ends up
// inside a wall, and that the scene builder never overflows a vertex group. Build: gcc -O2 -o stress host/stress.c -lm
#include "../source/game.h"
#include "../source/map.c"
#include "../source/game.c"
#include "../source/scene.c"
#include <stdio.h>
#include <stdlib.h>
static Game G; static Scene S;
int main(int argc, char **argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 8, bad = 0;
	game_init(&G, 1);
	int total = scene_build_static(&G, &S, NULL);
	Vtx *stat = malloc(sizeof(Vtx) * total), *dyn = malloc(sizeof(Vtx) * CAP_DYN);
	scene_build_static(&G, &S, stat); S.dyn = dyn;
	for (int i = 0; i < total; i++) { const Vtx *v = &stat[i]; if (!isfinite(v->x + v->y + v->z + v->u + v->v + v->r + v->g + v->b + v->a)) { bad++; break; } }
	printf("static %d vertices\n", total);
	int maxc[D_COUNT] = { 0 };
	static const int caps[D_COUNT] = { CAP_D_OPAQUE, CAP_D_ALPHA, CAP_D_ADD, CAP_D_VM, CAP_D_VMADD };
	unsigned r = 99;
	#define RND() (r = r * 1664525u + 1013904223u, (r >> 8) & 0xFFFF)
	for (int round = 0; round < rounds; round++) {
		G.rng = 4000 + round * 31; game_new_match(&G); G.state = GS_PLAY;
		Input in; memset(&in, 0, sizeof in);
		int hold = 0;
		for (int f = 0; f < 30 * 120 && G.state == GS_PLAY; f++) {
			if (--hold <= 0) {
				hold = 5 + RND() % 40;
				in.fwd = RND() & 1; in.back = !(RND() & 3); in.left = !(RND() & 3); in.right = !(RND() & 3);
				in.fire = RND() & 1; in.aim = !(RND() & 3);
				in.lx = (RND() / 32768.0f) - 1; in.ly = (RND() / 32768.0f - 1) * 0.5f;
			}
			Input i2 = in; unsigned k = RND() % 90;
			i2.next_p = k == 1; i2.prev_p = k == 2; i2.grenade_p = k == 3; i2.heal_p = k == 4; i2.reload_p = k == 5; i2.start_p = false;
			game_update(&G, &i2, 1.0f / 30);
			for (int i = 0; i < MAX_ACT; i++) {
				const Actor *a = &G.a[i];
				if (!isfinite(a->x + a->z + a->yaw + a->hp)) { bad++; printf("NaN actor %d\n", i); }
				if (a->alive && world_blocked(&G.w, a->x, a->z, PLAYER_R * 0.7f)) { bad++; printf("actor %d inside a wall (%.2f,%.2f) round %d f %d\n", i, a->x, a->z, round, f); G.a[i].x = G.w.spawn[0].x; G.a[i].z = G.w.spawn[0].z; }
			}
			if (f % 6 == 0) {
				scene_build(&G, &S);
				for (int g = 0; g < D_COUNT; g++) {
					if (S.count[g] > maxc[g]) maxc[g] = S.count[g];
					if (S.count[g] >= caps[g]) { bad++; printf("group %d full\n", g); }
					for (int v = 0; v < S.count[g]; v++) { const Vtx *p = &S.dyn[S.first[g] + v]; if (!isfinite(p->x + p->y + p->z + p->u + p->v + p->r + p->g + p->b + p->a)) { bad++; printf("bad vertex group %d\n", g); break; } }
				}
			}
		}
	}
	printf("max vertices: opaque %d/%d  alpha %d/%d  add %d/%d  vm %d/%d  vmadd %d/%d\n", maxc[0], caps[0], maxc[1], caps[1], maxc[2], caps[2], maxc[3], caps[3], maxc[4], caps[4]);
	printf(bad ? "FAIL (%d)\n" : "OK\n", bad);
	return bad != 0;
}
