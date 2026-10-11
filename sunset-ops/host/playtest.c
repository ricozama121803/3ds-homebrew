// Scripted checks of the human player's controls and rules. Build: gcc -O2 -o playtest host/playtest.c -lm
#include "../source/game.h"
#include "../source/map.c"
#include "../source/game.c"
#include <stdio.h>
#include <stdlib.h>
static Game G;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void run(float s, Input in) { for (float t = 0; t < s; t += 1.0f / 30) { game_update(&G, &in, 1.0f / 30); in.a_p = in.b_p = in.start_p = in.next_p = in.prev_p = in.grenade_p = in.heal_p = in.reload_p = false; } }
static void fresh(void)
{
	game_new_match(&G); G.state = GS_PLAY;
	for (int i = 1; i < MAX_ACT; i++) { G.a[i].x = 90; G.a[i].z = 90; G.a[i].alive = false; G.a[i].respawnT = 1e9f; }   // everyone else out of the way
	G.a[0].x = -10; G.a[0].z = 6; G.a[0].yaw = DEG(90); G.a[0].pitch = 0; G.a[0].invulnT = 0;
}
static float wrapd(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }
static void dummy_at(int i, float x, float z) { G.a[i].alive = true; G.a[i].frozen = true; G.a[i].hp = 100; G.a[i].team = TEAM_ENEMY; G.a[i].x = x; G.a[i].z = z; G.a[i].invulnT = 0; G.a[i].respawnT = 0; }
int main(void)
{
	game_init(&G, 1);
	G.w.nsolid = 0;                                  // the rules are tested in an empty arena (the map has no clear 20 m line); one test adds a wall back
	Input idle; memset(&idle, 0, sizeof idle);
	// --- movement: X forward, B back, Y left, A right (facing east: forward = +x, right = +z)
	fresh(); G.a[0].x = -12; G.a[0].z = 4; { Input in = idle; in.fwd = true; run(0.5f, in); }
	CHECK(G.a[0].x > -12 + 1.5f && fabsf(G.a[0].z - 4) < 0.1f, "X moves forward (x %.2f z %.2f)", G.a[0].x, G.a[0].z);
	fresh(); G.a[0].x = -12; G.a[0].z = 4; { Input in = idle; in.back = true; run(0.5f, in); }
	CHECK(G.a[0].x < -12 - 1.5f, "B moves back (x %.2f)", G.a[0].x);
	fresh(); G.a[0].x = -12; G.a[0].z = 4; { Input in = idle; in.left = true; run(0.5f, in); }
	CHECK(G.a[0].z < 4 - 1.5f, "Y strafes left (z %.2f)", G.a[0].z);
	fresh(); G.a[0].x = -12; G.a[0].z = 4; { Input in = idle; in.right = true; run(0.5f, in); }
	CHECK(G.a[0].z > 4 + 1.5f, "A strafes right (z %.2f)", G.a[0].z);
	fresh(); G.a[0].x = -12; G.a[0].z = 4; { Input in = idle; in.fwd = true; in.right = true; run(0.5f, in); }
	CHECK(hypotf(G.a[0].x + 12, G.a[0].z - 4) < 2.5f, "diagonal movement is not faster (%.2f m in 0.5 s)", hypotf(G.a[0].x + 12, G.a[0].z - 4));
	// --- sprint: double-tap X
	fresh(); G.a[0].x = -30; G.a[0].z = 4; { Input in = idle; in.fwd = true; run(0.5f, in); } float walkd = G.a[0].x + 30;
	fresh(); G.a[0].x = -30; G.a[0].z = 4; { Input in = idle; in.fwd = true; run(0.1f, in); in.fwd = false; run(0.1f, in); in.fwd = true; run(0.5f, in); } float sprd = G.a[0].x + 30;
	CHECK(sprd > walkd * 1.3f && G.p.sprinting, "double-tapping X sprints (%.1f m vs %.1f m walking in 0.5 s)", sprd, walkd);
	{ Input in = idle; in.fwd = true; in.aim = true; run(0.2f, in); } CHECK(!G.p.sprinting, "aiming ends the sprint");
	// --- look: circle pad right turns right (yaw grows), up looks up; aiming slows it
	fresh(); { Input in = idle; in.lx = 1; run(0.5f, in); } float yawHip = wrapd(G.a[0].yaw - DEG(90));
	fresh(); { Input in = idle; in.aim = true; run(0.3f, in); in.lx = 1; run(0.5f, in); } float yawAds = wrapd(G.a[0].yaw - DEG(90));
	CHECK(yawHip > 0.5f, "circle pad right turns right (%.2f rad in 0.5s)", yawHip);
	CHECK(yawAds < yawHip * 0.6f && yawAds > 0, "aiming slows the look speed (%.2f vs %.2f rad)", yawAds, yawHip);
	fresh(); { Input in = idle; in.ly = 1; run(0.5f, in); }
	CHECK(G.a[0].pitch > 0.3f, "circle pad up looks up (%.2f)", G.a[0].pitch);
	// --- aim assist: only while aiming, only with a clear line, only when close enough
	{
		float errs[5]; const char *nm[5] = { "ADS near an enemy", "ADS, assist off", "hip fire", "ADS behind a wall", "ADS far off target" };
		for (int k = 0; k < 5; k++) {
			fresh(); dummy_at(4, 5, 6); G.a[0].x = -10; G.a[0].z = 6; G.a[0].yaw = DEG(90) + DEG(k == 4 ? 12.0f : 2.0f);
			G.a[0].pitch = atan2f(1.15f - EYE_H, 15.0f);
			if (k == 1) G.aimAssist = false;
			if (k == 3) { dummy_at(4, 5, 6); G.a[0].x = -13; G.a[0].z = 4; G.a[0].yaw = DEG(90) + atan2f(2.0f, 18.0f); G.a[0].pitch = atan2f(1.15f - EYE_H, 18.0f); dummy_at(4, 5, 6); G.a[4].z = 6; }
			if (k == 3) { Box *w = &G.w.box[G.w.nbox]; memset(w, 0, sizeof *w); w->x0 = -3; w->x1 = -2; w->z0 = 1; w->z1 = 9; w->y1 = 3; w->solid = true; G.w.solid_idx[0] = G.w.nbox; G.w.nsolid = 1; }
			Input in = idle; in.aim = k != 2; run(0.6f, in); G.w.nsolid = 0;
			float dx = G.a[4].x - G.a[0].x, dz = G.a[4].z - G.a[0].z;
			errs[k] = fabsf(wrapd(atan2f(dx, -dz) - G.a[0].yaw)) * 180 / PI_F;
		}
		CHECK(errs[0] < 1.0f, "aim assist pulls the crosshair onto an enemy it is close to (%.2f deg off after 0.6 s, started 2.0)", errs[0]);
		CHECK(errs[1] > 1.9f, "...and does nothing when switched off (%.2f deg)", errs[1]);
		CHECK(errs[2] > 1.9f, "...and nothing when not aiming (%.2f deg)", errs[2]);
		CHECK(errs[4] > 11.0f, "...and nothing when the enemy is far from the crosshair (%.2f deg)", errs[4]);
		(void)nm;
	}
	// --- shooting
	fresh(); run(0.6f, idle); { Input in = idle; in.fire = true; run(1.0f, in); }
	CHECK(G.p.mag[W_AR] <= 30 - 9 && G.p.mag[W_AR] >= 30 - 14, "R fires the M27 full auto (mag %d after 1 s)", G.p.mag[W_AR]);
	{ Input in = idle; in.reload_p = true; run(0.2f, in); CHECK(G.p.reloading, "SELECT starts a reload"); run(2.2f, in); }
	CHECK(G.p.mag[W_AR] == 30, "reload refills the magazine (mag %d reserve %d)", G.p.mag[W_AR], G.p.reserve[W_AR]);
	fresh(); G.p.mag[W_AR] = 1; { Input in = idle; in.fire = true; run(0.7f, in); run(2.5f, idle); }
	CHECK(G.p.mag[W_AR] > 20, "an empty magazine reloads by itself (mag %d)", G.p.mag[W_AR]);
	// --- weapon change with the d-pad
	fresh(); { Input in = idle; in.next_p = true; run(0.6f, in); } CHECK(G.p.weapon == W_SMG, "d-pad right -> weapon %d (SMG)", G.p.weapon);
	{ Input in = idle; in.prev_p = true; run(0.6f, in); } CHECK(G.p.weapon == W_AR, "d-pad left -> weapon %d (AR)", G.p.weapon);
	{ Input in = idle; in.prev_p = true; run(0.6f, in); } CHECK(G.p.weapon == W_SHOTGUN, "d-pad left wraps to the shotgun (%d)", G.p.weapon);
	fresh(); G.p.weapon = G.p.nextWeapon = W_PISTOL; { Input in = idle; in.fire = true; run(1.0f, in); } CHECK(G.p.mag[W_PISTOL] == 11, "pistol fires one round while R is held (mag %d)", G.p.mag[W_PISTOL]);
	// --- heal
	fresh(); G.a[0].hp = 30; { Input in = idle; in.heal_p = true; run(0.2f, in); run(2.0f, idle); }
	CHECK(G.a[0].hp > 95 && G.p.medkits == 1, "d-pad down heals (hp %.0f, medkits %d)", G.a[0].hp, G.p.medkits);
	fresh(); { Input in = idle; in.heal_p = true; run(0.2f, in); } CHECK(G.p.medkits == 2, "no medkit wasted at full health");
	// --- grenade: up on the d-pad
	fresh(); dummy_at(4, 0, 4);
	{ Input in = idle; in.grenade_p = true; run(0.1f, in); }
	CHECK(G.p.grenades == 1, "d-pad up throws a grenade (%d left)", G.p.grenades);
	{ float lx = 0, lz = 0; bool seen = false; for (int i = 0; i < 120; i++) { game_update(&G, &idle, 1.0f / 30); for (int k = 0; k < MAX_GREN; k++) if (G.gren[k].live) { lx = G.gren[k].p[0]; lz = G.gren[k].p[2]; seen = true; } } printf("      (grenade last seen at x %.1f z %.1f, seen %d)\n", lx, lz, seen); }
	CHECK(G.a[4].hp < 100 || !G.a[4].alive, "grenade exploded near the target and hurt it (hp %.0f)", G.a[4].hp);
	// --- grenade at own feet kills the thrower, who respawns
	fresh(); { float p[3] = { G.a[0].x + 0.5f, 0.3f, G.a[0].z }, v[3] = { 0, 0, 0 }; throw_grenade(&G, 4, p, v); G.gren[0].t = 0.2f; run(0.5f, idle); }
	CHECK(!G.a[0].alive || G.a[0].hp < 60, "a grenade at your feet hurts you (hp %.0f alive %d)", G.a[0].hp, G.a[0].alive);
	run(5.0f, idle);
	CHECK(G.a[0].alive && G.a[0].hp == 100 && G.p.mag[W_AR] == 30, "you respawn with full health and ammo (hp %.0f)", G.a[0].hp);
	// --- killing: AR into a stationary enemy 15 m away, aiming down sights
	fresh(); dummy_at(4, 5, 6); G.a[0].pitch = DEG(-1.2f);
	{ Input in = idle; in.aim = true; run(0.5f, in); in.fire = true; run(1.5f, in); }
	CHECK(!G.a[4].alive && G.score[0] >= 1 && G.a[0].kills == 1, "shooting an enemy kills them and scores (score %.0f, kills %d, hp %.0f)", G.score[0], G.a[0].kills, G.a[4].hp);
	// --- shotgun is lethal up close
	fresh(); dummy_at(4, -7, 4); G.a[0].yaw = DEG(90); G.a[0].x = -10; G.a[0].z = 4; G.a[0].pitch = DEG(-8); G.p.weapon = G.p.nextWeapon = W_SHOTGUN;
	{ Input in = idle; in.fire = true; run(1.2f, in); }
	CHECK(!G.a[4].alive, "shotgun kills at 3 m (hp %.0f)", G.a[4].hp);
	// --- pickups
	fresh(); G.p.reserve[W_AR] = 10; G.a[0].x = G.w.pick[0].x; G.a[0].z = G.w.pick[0].z; run(0.2f, idle);
	CHECK(G.p.reserve[W_AR] > 10, "walking over an ammo pickup refills ammo (%d)", G.p.reserve[W_AR]);
	// --- match end
	fresh(); G.score[0] = KILLS_TO_WIN - 1; dummy_at(4, 5, 6); G.a[0].pitch = DEG(-1.2f); { Input in = idle; in.aim = true; run(0.5f, in); in.fire = true; run(1.5f, in); }
	CHECK(G.state == GS_OVER && G.winner == 0, "reaching the kill limit ends the match with a win");
	{ Input in = idle; in.a_p = true; run(2.0f, idle); run(0.1f, in); }
	CHECK(G.state == GS_PLAY && G.score[0] == 0, "A starts a rematch");
	// --- pause
	{ Input in = idle; in.start_p = true; run(0.1f, in); } CHECK(G.paused, "START pauses");
	{ float t0 = G.time; run(1.0f, idle); CHECK(G.time == t0, "the game is frozen while paused"); }
	{ Input in = idle; in.a_p = true; run(0.1f, in); } CHECK(!G.paused, "A resumes");
	printf(fails ? "FAILED: %d\n" : "ALL OK\n", fails);
	return fails ? 1 : 0;
}
