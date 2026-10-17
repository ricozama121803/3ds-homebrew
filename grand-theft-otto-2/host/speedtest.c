// How fast each car gets: 0-100 km/h time and the speed reached along a straight street.
// Build: gcc -O2 -o /tmp/speedtest host/speedtest.c -lm && /tmp/speedtest
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/traffic.c"
#include "../source/combat.c"
#include "../source/police.c"
#include <stdio.h>
int main(void)
{
	static Game G;
	game_init(&G, false);
	for (int kind = 0; kind < CAR_KINDS; kind++) {
		memset(G.car, 0, sizeof G.car);
		for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false;
		int ci = game_add_car(&G, kind, 0.5f * PITCH, 4 * PITCH + 3.6f, DEG(90));
		G.p.car = ci; G.car[ci].driven = true;
		Input in; memset(&in, 0, sizeof in); in.gas = true;
		float t100 = -1, top = 0;
		for (int i = 0; i < 60 * 40; i++) {
			game_update(&G, &in, 1.0f / 60);
			float sp = hypotf(G.car[ci].vx, G.car[ci].vz);
			if (t100 < 0 && sp > 27.8f) t100 = i / 60.0f;
			if (sp > top) top = sp;
			if (G.car[ci].x > CITY - 20) break;
		}
		printf("kind %d: 0-100 km/h in %.1f s, top speed reached %.0f km/h on a %.0f m run\n", kind, t100, top * 3.6f, G.car[ci].x);
	}
	return 0;
}
