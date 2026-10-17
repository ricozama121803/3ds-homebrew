// Headless soak test: random inputs for a few simulated minutes. Checks that nothing goes NaN, nobody ends up inside a building,
// and everything stays inside the city. Build: gcc -O2 -o /tmp/simtest host/simtest.c -lm && /tmp/simtest
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

int main(void)
{
	game_init(&G, false);
	unsigned rs = 12345; int fails = 0, enters = 0, exits = 0;
	Input in; memset(&in, 0, sizeof in);
	for (int frame = 0; frame < 60 * 60 * 6; frame++) {
		if (frame % 40 == 0) {                                   // pick a new random "stick position" every ~0.7 s
			in.lx = rndf(&rs) * 2 - 1; in.ly = rndf(&rs) * 2 - 1;
			in.gas = rndf(&rs) < 0.7f; in.brake = rndf(&rs) < 0.15f; in.hand = rndf(&rs) < 0.15f; in.run = rndf(&rs) < 0.5f;
			in.cx = rndf(&rs) < 0.2f ? rndf(&rs) * 2 - 1 : 0;
		}
		in.enter_p = (frame % 300 == 0);
		int before = G.p.car;
		game_update(&G, &in, 1.0f / 60);
		if (before < 0 && G.p.car >= 0) enters++;
		if (before >= 0 && G.p.car < 0) exits++;
		float vals[] = { G.p.p[0], G.p.p[2], G.cam.eye[0], G.cam.eye[1], G.cam.eye[2], G.cam.yaw };
		for (int i = 0; i < 6; i++) if (!isfinite(vals[i])) { printf("frame %d: NaN in player/camera\n", frame); return 1; }
		bool flying = G.p.car >= 0 && car_flying(&G.car[G.p.car]);                 // a helicopter pilot may leave the city and fly over buildings
		if (!flying && (G.p.p[0] < 0 || G.p.p[0] > CITY || G.p.p[2] < 0 || G.p.p[2] > CITY)) { printf("frame %d: player left the city (%.1f, %.1f)\n", frame, G.p.p[0], G.p.p[2]); fails++; }
		for (int i = 0; i < MAX_CAR; i++) {
			Car *c = &G.car[i]; if (!c->on) continue;
			if (!isfinite(c->x) || !isfinite(c->z) || !isfinite(c->yaw)) { printf("frame %d: car %d NaN\n", frame, i); return 1; }
			float cx[3], cz[3]; car_circles(c, cx, cz);
			for (int k = 0; k < 3 && c->y < 0.5f; k++) if (inside(cx[k], cz[k], car_radius(c->kind) * 0.8f)) { if (fails < 10) printf("frame %d: car %d inside a building (%.1f, %.1f)\n", frame, i, cx[k], cz[k]); fails++; break; }
		}
		if (G.p.car < 0 && !flying && inside(G.p.p[0], G.p.p[2], 0.3f)) { if (fails < 10) printf("frame %d: Otto inside a building (%.1f, %.1f)\n", frame, G.p.p[0], G.p.p[2]); fails++; }
	}
	printf("entered %d times, exited %d times, failures %d\n", enters, exits, fails);
	return fails ? 1 : 0;
}
