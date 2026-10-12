// Headless checks of the individual moves: beacon run with an autopilot, web zip, wall climb + vault, wall jump, water recovery.
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include <stdio.h>
#include <stdlib.h>

static Game G;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static Input idle;
static void step(Input in, float secs)
{
	for (float t = 0; t < secs; t += 1 / 30.0f) { game_update(&G, &in, 1 / 30.0f); in.jump_p = in.zip_p = in.start_p = in.a_p = in.b_p = in.select_p = false; }
}

int main(void)
{
	memset(&idle, 0, sizeof idle);
	// 1. beacon run: the autopilot chases the beacon
	int totalB = 0;
	for (int r = 0; r < 8; r++) {
		game_init(&G, false); G.rng ^= 0x91u * (r + 1);
		game_start(&G, MODE_RUN);
		float tFirst = -1; int lastB = 0; float tLast = 0, worst = 0;
		for (float t = 0; t < 300 && G.state == GS_PLAY; t += 1 / 30.0f) {
			Input in; game_bot(&G, &in, 1 / 30.0f, G.bc.p);
			game_update(&G, &in, 1 / 30.0f);
			if (G.beacons != lastB) { if (tFirst < 0) tFirst = t; if (t - tLast > worst) worst = t - tLast; lastB = G.beacons; tLast = t; }
		}
		printf("beacon run %d: %d beacons, first after %.1fs, slowest gap %.1fs, time up at %.0fs, score %d\n", r, G.beacons, tFirst, worst, G.time, G.score);
		totalB += G.beacons;
	}
	printf("autopilot average: %.1f beacons per run\n", totalB / 8.0f);
	CHECK(totalB >= 8 * 2, "the autopilot should average at least 2 beacons per run");
	// 2. web zip from the street
	{
		game_init(&G, false); game_start(&G, MODE_FREE); G.cam.yaw = DEG(90);
		step(idle, 0.3f);
		Input z = idle; z.zip_p = true;
		step(z, 0.05f);
		float y0 = G.p.p[1]; int wasRope = G.p.rope;
		step(idle, 2.2f);
		printf("zip: rope after press %d, height gained %.1f m, now state %d at y=%.1f\n", wasRope, G.p.p[1] - y0, G.p.state, G.p.p[1]);
		CHECK(wasRope, "zip should fire a web from the street");
		CHECK(G.p.p[1] > 10, "zip should lift the hero well above the street (y=%.1f)", G.p.p[1]);
	}
	// 3. climb a wall and vault onto the roof: find the first building box and stand next to its south face
	{
		game_init(&G, false); game_start(&G, MODE_FREE);
		const Box *b = &G.w.box[G.w.bfirst[2][2]];
		float x = 0.5f * (b->x0 + b->x1), z = b->z0 - BODY_HW - 0.02f;
		memset(&G.p, 0, sizeof G.p); G.p.p[0] = x; G.p.p[1] = 5; G.p.p[2] = z; G.p.state = ST_WALL; G.p.wn[0] = 0; G.p.wn[1] = -1;
		Input up = idle; up.ly = 1; G.cam.yaw = 0;   // the camera looks north (-z): away from the wall at +z, the pad is camera relative so only up matters
		float top = b->y1; float t = 0; bool vaulted = false;
		for (; t < 40; t += 1 / 30.0f) { game_update(&G, &up, 1 / 30.0f); if (G.p.state == ST_GROUND && G.p.p[1] > 5) { vaulted = true; break; } }
		printf("climb: box top %.1f m, hero reached the roof %s after %.1fs (y=%.1f)\n", top, vaulted ? "yes" : "NO", t, G.p.p[1]);
		CHECK(vaulted, "climbing up a wall should end on the roof");
		CHECK(fabsf(G.p.p[1] - top) < 0.5f || G.p.p[1] > top, "should stand on the roof (y=%.1f vs %.1f)", G.p.p[1], top);
		// wall jump
		memset(&G.p, 0, sizeof G.p); G.p.p[0] = x; G.p.p[1] = 8; G.p.p[2] = z; G.p.state = ST_WALL; G.p.wn[0] = 0; G.p.wn[1] = -1;
		Input j = idle; j.jump_p = true; step(j, 0.1f); step(idle, 0.2f);
		printf("wall jump: z moved %.1f m away from the wall, vy %.1f\n", z - G.p.p[2], G.p.v[1]);
		CHECK(G.p.p[2] < z - 1.0f, "wall jump should push away from the wall");
	}
	// 4. falling in the water recovers
	{
		game_init(&G, false); game_start(&G, MODE_FREE);
		memset(&G.p, 0, sizeof G.p); G.p.p[0] = -80; G.p.p[1] = 10; G.p.p[2] = 200; G.p.state = ST_AIR;
		step(idle, 4.0f);
		printf("water: after 4 s the hero is at (%.0f, %.1f, %.0f) state %d\n", G.p.p[0], G.p.p[1], G.p.p[2], G.p.state);
		CHECK(G.p.p[0] >= -1 && G.p.p[1] >= -0.5f, "hero should be back on the street after falling into the water");
	}
	// 6. jump charge: a tap is a small hop, holding A gives a much higher jump
	for (int hold = 0; hold < 2; hold++) {
		game_init(&G, false); game_start(&G, MODE_FREE); step(idle, 0.3f);
		Input a = idle; a.jump_p = true; a.a_held = true;
		step(a, 0.05f); a.jump_p = false;
		if (hold) step(a, 0.8f);
		Input r = idle; r.a_rel = true;
		float peak = 0;
		for (float t = 0; t < 3; t += 1 / 30.0f) { game_update(&G, &r, 1 / 30.0f); r.a_rel = false; if (G.p.p[1] > peak) peak = G.p.p[1]; }
		printf("jump %s: peak height %.1f m\n", hold ? "held 0.85 s" : "tapped", peak);
		if (hold) CHECK(peak > 14.0f, "a charged jump should reach well over 14 m (%.1f)", peak); else CHECK(peak > 1.5f && peak < 4.5f, "a tap should be a small hop (%.1f)", peak);
	}
	// 7. boost at the end of a swing, and a flip with a double tap
	{
		game_init(&G, false); game_start(&G, MODE_FREE);
		Input sw = idle; sw.swing = true; sw.ly = 1; G.cam.yaw = DEG(90);
		float best = 0; bool done = false;
		for (float t = 0; t < 12 && !done; t += 1 / 30.0f) {
			game_update(&G, &sw, 1 / 30.0f);
			if (G.p.rope && G.p.swingT > 1.4f && G.p.p[1] > 20) {
				float before = len3(G.p.v); Input a = sw; a.jump_p = true; game_update(&G, &a, 1 / 30.0f);
				best = len3(G.p.v); printf("boost: speed %.1f -> %.1f m/s, vy %.1f, rope off %d\n", before, best, G.p.v[1], !G.p.rope);
				CHECK(!G.p.rope && (best > before * 1.15f || best > 52.0f), "A during a swing should boost (%.1f -> %.1f)", before, best);
				// second tap within the window: front flip with the stick forward, then a roll to the left
				int score0 = G.score; Input b = idle; b.ly = 1; b.jump_p = true; step(idle, 0.1f); game_update(&G, &b, 1 / 30.0f);
				printf("trick: type %d, time left %.2f, score +%d\n", G.p.trickType, G.p.trickT, G.score - score0);
				CHECK(G.p.trickT > 0 && G.p.trickType == 1, "double tap with the stick forward should start a front flip");
				step(idle, 0.8f);
				Input c = idle; c.lx = -1; c.jump_p = true; G.p.aTapT = 0.3f; int was = G.p.state; printf("before: state %d trickT %.2f boostWin %.2f rope %d y %.1f vy %.1f\n", G.p.state, G.p.trickT, G.p.boostWin, G.p.rope, G.p.p[1], G.p.v[1]); game_update(&G, &c, 1 / 30.0f);
				printf("trick: type %d (state %d trickT %.2f aTap %.2f rope %d mag-input lx -1)\n", G.p.trickType, G.p.state, G.p.trickT, G.p.aTapT, G.p.rope); CHECK(G.p.trickType == 3 || was != ST_AIR, "stick left should roll left");
				done = true;
			}
		}
		CHECK(done, "never got a swing to boost from");
	}
	// 8. trick direction from the stick, in clear air (camera looks east)
	{
		static const struct { float lx, ly; int want; const char *name; } cases[5] = { { 0, 1, 1, "forward" }, { 0, -1, 2, "back" }, { -1, 0, 3, "left" }, { 1, 0, 4, "right" }, { 0, 0, 1, "neutral" } };
		for (int k = 0; k < 5; k++) {
			game_init(&G, false); game_start(&G, MODE_FREE); G.cam.yaw = DEG(90);
			memset(&G.p, 0, sizeof G.p); G.p.p[0] = 4 * PITCH; G.p.p[1] = 400; G.p.p[2] = 3 * PITCH; G.p.state = ST_AIR; G.p.face = DEG(90); G.p.aTapT = 0.3f;
			Input t = idle; t.lx = cases[k].lx; t.ly = cases[k].ly; t.jump_p = true; game_update(&G, &t, 1 / 30.0f);
			printf("trick with stick %s: type %d (want %d)\n", cases[k].name, G.p.trickType, cases[k].want);
			CHECK(G.p.trickType == cases[k].want, "stick %s should give trick %d", cases[k].name, cases[k].want);
		}
	}
	// 5. determinism / bounds of the world: every box inside its block
	for (int i = 0; i < G.w.nbox; i++) {
		const Box *b = &G.w.box[i];
		if (b->x0 < 0 || b->x1 > CITY || b->z0 < 0 || b->z1 > CITY || b->y1 <= b->y0) { printf("  bad box %d\n", i); fails++; }
	}
	printf("%s (%d failures)\n", fails ? "FAILED" : "all moves ok", fails);
	return fails != 0;
}
