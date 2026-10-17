// Walking limits: the beach stops at the water, the pier carries on over it, and neither lets Otto leave the world.
// Build: gcc -O2 -o /tmp/walktest host/walktest.c -lm && /tmp/walktest
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/traffic.c"
#include "../source/combat.c"
#include "../source/police.c"
#include <stdio.h>

static Game G;
static void walk(float x, float z, float yaw, float seconds, const char *name, float expectX, float tol)
{
	game_start(&G);
	for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false;
	for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
	G.p.p[0] = x; G.p.p[2] = z; G.cam.yaw = yaw;
	Input in; memset(&in, 0, sizeof in); in.ly = 1;
	for (float t = 0; t < seconds; t += 1.0f / 60) { G.cam.yaw = yaw; G.cam.manualT = 5; in.b_p = (t < 0.01f || (t > 0.1f && t < 0.12f)); game_update(&G, &in, 1.0f / 60); }      // two quick taps of B: sprint
	printf("%-34s ends at x %.1f z %.1f  (expected x near %.0f): %s\n", name, G.p.p[0], G.p.p[2], expectX, fabsf(G.p.p[0] - expectX) <= tol ? "ok" : "FAIL");
}

int main(void)
{
	game_init(&G, false);
	walk(-30, 100, DEG(-90), 15, "beach, walking west to the sea", BEACH_X, 1.5f);
	walk(-60, 256, DEG(-90), 80, "pier, walking west to its end", PIER_X, 1.5f);
	walk(-200, 256, DEG(90), 170, "pier, walking east back to the beach", CITY - 1.0f, 3.0f);
	walk(-200, 256, DEG(0), 10, "pier, pushing north into the rail", -200, 1.0f);
	return 0;
}
