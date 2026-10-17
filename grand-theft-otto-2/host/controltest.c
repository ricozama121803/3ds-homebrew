// The control scheme: R shoots / accelerates, X gets in and out of cars, two quick taps of B sprint, L takes cover, the camera follows the aim.
// Build: gcc -O2 -o /tmp/controltest host/controltest.c -lm && /tmp/controltest
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/traffic.c"
#include "../source/combat.c"
#include "../source/police.c"
#include <stdio.h>

static Game G;
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static Input idle(void) { Input in; memset(&in, 0, sizeof in); return in; }
static void clear_world(void) { for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; }
static void run(float s, Input in) { for (float t = 0; t < s; t += 1.0f / 60) { game_update(&G, &in, 1.0f / 60); in.enter_p = in.x_p = in.b_p = false; in.weap = 0; } }

int main(void)
{
	game_init(&G, false);
	const float SX = 4 * PITCH, SZ = 4 * PITCH + 24;

	printf("sprint: two quick taps of B\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ + 30; G.p.face = 0; G.cam.yaw = 0;
	Input w = idle(); w.ly = 1;
	run(1.5f, w); float walk = G.p.speed;
	Input tap = w; tap.b_p = true; tap.brake = true;
	run(0.05f, tap); run(0.15f, w); run(0.05f, tap); run(1.2f, w);
	float sprint = G.p.speed;
	printf("   walking %.1f m/s, after two taps %.1f m/s\n", walk, sprint);
	CHECK(walk < 3.5f, "plain walking should be a jog at most (%.1f)", walk);
	CHECK(sprint > 5.0f, "two quick taps of B should start a sprint (%.1f)", sprint);
	run(0.6f, idle()); run(0.2f, w);
	CHECK(G.p.speed < 4.0f, "letting go of the stick ends the sprint (%.1f)", G.p.speed);
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ + 30; G.cam.yaw = 0;
	run(1.0f, w); run(0.05f, tap); run(1.0f, w); run(0.05f, tap); run(1.0f, w);
	CHECK(G.p.speed < 3.5f, "two slow taps do not sprint (%.1f)", G.p.speed);

	printf("cover: hold L next to a wall\n");
	game_start(&G); clear_world();
	float wallx = SX + 12.8f;                                                   // the west face of the building east of the street
	G.p.p[0] = wallx - 1.6f; G.p.p[2] = 4 * PITCH + 34; G.p.face = 0; G.cam.yaw = 0;
	Input cv = idle(); cv.cover = true;
	run(0.8f, cv);
	printf("   in cover %d at x %.2f (wall at %.2f)\n", G.p.cover, G.p.p[0], wallx);
	CHECK(G.p.cover, "Otto should take cover next to the wall");
	CHECK(fabsf(G.p.p[0] - (wallx - 0.58f)) < 0.2f, "Otto stands close to the wall (x %.2f)", G.p.p[0]);
	float z0 = G.p.p[2]; cv.ly = 1; run(1.0f, cv); CHECK(fabsf(G.p.p[2] - z0) > 0.8f && fabsf(G.p.p[0] - (wallx - 0.58f)) < 0.25f, "the stick slides him along the wall (moved %.1f m, x %.2f)", G.p.p[2] - z0, G.p.p[0]);
	// ducked, a bullet hurts much less
	float hp0 = G.p.hp; damage_player(&G, 40.0f); float ducked = hp0 - G.p.hp;
	run(0.1f, idle()); G.p.hp = hp0; G.p.cover = false; damage_player(&G, 40.0f); float open = hp0 - G.p.hp;
	printf("   40 damage costs %.1f hp in cover, %.1f in the open\n", ducked, open);
	CHECK(ducked < open * 0.6f, "cover should soften hits");
	run(0.3f, idle()); CHECK(!G.p.cover, "letting go of L leaves cover");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ + 40; Input c2 = idle(); c2.cover = true; run(0.3f, c2);
	printf("   in the middle of the street, L does nothing (cover %d)\n", G.p.cover);
	CHECK(!G.p.cover, "no cover in the middle of the street");

	printf("cars: X gets in and out, R accelerates, A is the handbrake\n");
	game_start(&G); clear_world();
	int ci = game_add_car(&G, CAR_SEDAN, SX + 6.4f, SZ - 10, 0); G.p.p[0] = SX + 3.5f; G.p.p[2] = SZ - 10; G.p.face = 0;
	Input x = idle(); x.enter_p = true; run(0.1f, x);
	CHECK(G.p.car == ci, "X should get Otto into the car");
	Input gas = idle(); gas.gas = true; run(2.0f, gas);
	float v = hypotf(G.car[ci].vx, G.car[ci].vz);
	CHECK(v > 15.0f, "R should accelerate the car (%.1f m/s)", v);
	Input hb = idle(); hb.hand = true; run(1.0f, hb);
	CHECK(hypotf(G.car[ci].vx, G.car[ci].vz) < v, "the handbrake slows the car");
	Input brake = idle(); brake.brake = true; for (int i = 0; i < 600 && hypotf(G.car[ci].vx, G.car[ci].vz) > 2.0f; i++) run(1.0f / 60, brake);
	run(0.1f, idle()); Input out = idle(); out.enter_p = true; run(0.1f, out);
	CHECK(G.p.car < 0, "X gets Otto out when the car has stopped (speed %.1f)", hypotf(G.car[ci].vx, G.car[ci].vz));

	printf("shooting: R shoots and the camera swings to the aim\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
	Ped *p = &G.ped[0]; memset(p, 0, sizeof *p); p->on = true; p->hp = 60; p->x = SX + 3 + 8; p->z = SZ - 8; p->state = PED_IDLE; p->timer = 100; p->node = p->prev = traffic_nearest_node(&G, p->x, p->z);   // 45 degrees to his right
	int ammo = G.p.ammo[W_PISTOL];
	Input r = idle(); r.gas = true; run(0.5f, r);
	float camDiff = fabsf(wrapang(G.cam.yaw - G.p.face));
	printf("   target 45 degrees to his right: face %.2f, camera %.2f, shots fired %d\n", G.p.face, G.cam.yaw, ammo - G.p.ammo[W_PISTOL]);
	CHECK(G.cam.yaw > 0.45f, "the camera should have turned toward the target (yaw %.2f)", G.cam.yaw);
	CHECK(camDiff < 0.35f, "the camera should have swung to the shooting direction (%.2f rad apart)", camDiff);
	CHECK(G.p.ammo[W_PISTOL] < ammo, "R should shoot");
	Input a = idle(); G.p.ammo[W_PISTOL] = 30; run(0.5f, a); CHECK(G.p.ammo[W_PISTOL] == 30, "A no longer shoots");
	Input aButton = idle(); aButton.hand = true; run(0.3f, aButton); CHECK(G.p.ammo[W_PISTOL] == 30, "the handbrake input does not shoot either");

	printf("survivability\n");
	game_start(&G); clear_world();
	CHECK(G.p.hp == PLAYER_MAX_HP, "starts at %.0f hp", PLAYER_MAX_HP);
	float before = G.p.hp; damage_player(&G, 40); printf("   40 damage costs %.1f of %.0f hp\n", before - G.p.hp, PLAYER_MAX_HP);
	CHECK(before - G.p.hp < 31.0f, "Otto takes less than 40 of a 40 hit");
	run(12.0f, idle()); CHECK(G.p.hp > before - 5.0f, "he recovers within a few seconds (%.0f)", G.p.hp);

	printf("%s (%d failure%s)\n", fails ? "FAILED" : "all control checks passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
