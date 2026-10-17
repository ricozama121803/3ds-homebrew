// The control scheme: R shoots / accelerates, X gets in and out of vehicles, taps of B sprint (more taps, faster), hold L to lock on (flick to switch), SELECT opens the weapon wheel.
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
static void run(float s, Input in) { for (float t = 0; t < s; t += 1.0f / 60) { game_update(&G, &in, 1.0f / 60); in.enter_p = in.x_p = in.b_p = in.select_p = in.a_p = in.start_p = false; in.weap = 0; } }

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

	printf("faster sprint: keep tapping B\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ + 60; G.p.face = 0; G.cam.yaw = 0;
	run(1.0f, w); run(0.05f, tap); run(0.15f, w); run(0.05f, tap); run(0.6f, w);
	float s1 = G.p.speed;
	for (int i = 0; i < 6; i++) { run(0.05f, tap); run(0.15f, w); }
	run(0.3f, w);
	float s2 = G.p.speed;
	printf("   two taps %.1f m/s, after eight more %.1f m/s\n", s1, s2);
	CHECK(s2 > s1 + 1.5f, "repeated taps should make the sprint faster (%.1f -> %.1f)", s1, s2);
	CHECK(s2 > 7.0f, "the fast sprint should pass 7 m/s (%.1f)", s2);

	printf("lock-on: hold L, flick the stick to switch\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
	for (int k = 0; k < 2; k++) {
		Ped *q = &G.ped[k]; memset(q, 0, sizeof *q); q->on = true; q->hp = 20; q->x = SX + 3 + (k ? 4.0f : -3.0f); q->z = SZ - 7; q->state = PED_IDLE; q->timer = 100; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z);
	}
	Input lk = idle(); lk.aim = true;
	run(0.4f, lk);
	printf("   locked on ped %d (left one is 0), aim stance %.2f\n", G.p.lock, G.p.aimT);
	CHECK(G.p.lock == 0, "L should lock onto the nearest person in front of the camera (%d)", G.p.lock);
	CHECK(G.p.aimT > 0.5f, "L raises the gun");
	Input fl = lk; fl.lx = 1; run(0.3f, fl);
	CHECK(G.p.lock == 1, "flicking right should switch to the person on the right (%d)", G.p.lock);
	run(0.3f, lk); fl.lx = -1; run(0.3f, fl);
	CHECK(G.p.lock == 0, "flicking left should switch back (%d)", G.p.lock);
	run(0.3f, lk);
	int hp0n = G.ped[0].hp > 0; (void)hp0n;
	Input fire = lk; fire.gas = true; float maxHit = 0, maxKill = 0;
	for (int i = 0; i < 90; i++) { run(1.0f / 60, fire); maxHit = fmaxf(maxHit, G.hitT); maxKill = fmaxf(maxKill, G.killT); }
	printf("   locked shots: ped 0 hp %.0f dead %d, hit marker %.2f, kill marker %.2f, ammo %d\n", G.ped[0].hp, G.ped[0].dead, maxHit, maxKill, G.p.ammo[G.p.weapon]);
	CHECK(G.ped[0].dead, "R while locked shoots the locked person");
	CHECK(maxHit > 0.1f, "a hit shows the hit marker");
	CHECK(maxKill > 0.5f, "a kill shows the kill marker");
	CHECK(!G.ped[1].dead || G.p.lock != 0, "the other person is not the one shot at");
	run(0.3f, idle()); CHECK(G.p.lock < 0, "letting go of L drops the lock");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0.8f;
	Input fr0 = idle(); fr0.aim = true; run(0.5f, fr0);
	CHECK(G.p.lock < 0 && fabsf(wrapang(G.p.face - G.cam.yaw)) < 0.2f, "with nobody to lock onto Otto aims where the camera looks (face %.2f, camera %.2f)", G.p.face, G.cam.yaw);

	printf("weapon wheel: SELECT, then the D-pad or circle pad\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
	CHECK(G.p.has[W_FISTS] && G.p.has[W_PISTOL] && G.p.has[W_SMG] && G.p.has[W_SHOTGUN], "Otto starts with every weapon");
	Input sel = idle(); sel.select_p = true; run(0.1f, sel);
	CHECK(G.wheel, "SELECT opens the wheel");
	Input dp = idle(); dp.dpx = 1; run(0.1f, dp); CHECK(G.p.weapon == W_PISTOL, "D-pad right = pistol (%d)", G.p.weapon);
	dp = idle(); dp.dpy = -1; run(0.1f, dp); CHECK(G.p.weapon == W_SMG, "D-pad down = SMG (%d)", G.p.weapon);
	dp = idle(); dp.lx = -1; run(0.1f, dp); CHECK(G.p.weapon == W_SHOTGUN, "stick left = shotgun (%d)", G.p.weapon);
	dp = idle(); dp.ly = 1; run(0.1f, dp); CHECK(G.p.weapon == W_FISTS, "stick up = fists (%d)", G.p.weapon);
	float zb = G.p.p[2]; Input mv = idle(); mv.ly = 1; run(0.5f, mv); CHECK(fabsf(G.p.p[2] - zb) < 0.3f, "Otto does not walk while choosing (moved %.2f)", zb - G.p.p[2]);
	run(0.1f, sel); CHECK(!G.wheel, "SELECT closes the wheel");
	Input ps = idle(); ps.start_p = true; run(0.02f, ps); CHECK(G.paused, "START pauses"); ps = idle(); ps.select_p = true; run(0.02f, ps);
	CHECK(!G.paused, "SELECT on the pause screen restarts and unpauses");
	G.p.weapon = W_SMG; G.p.ammo[W_SMG] = 5; for (int i = 0; i < 40; i++) { Input rr = idle(); rr.gas = true; run(1.0f / 30, rr); }
	CHECK(G.p.ammo[W_SMG] == 5, "ammo is infinite in the test build (%d)", G.p.ammo[W_SMG]);

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
	int ammo = G.p.ammo[W_PISTOL]; float shot0 = G.p.lastShotT; (void)ammo;
	Input r = idle(); r.gas = true; run(0.5f, r);
	float camDiff = fabsf(wrapang(G.cam.yaw - G.p.face));
	printf("   target 45 degrees to his right: face %.2f, camera %.2f, shots fired %d\n", G.p.face, G.cam.yaw, ammo - G.p.ammo[W_PISTOL]);
	CHECK(G.cam.yaw > 0.45f, "the camera should have turned toward the target (yaw %.2f)", G.cam.yaw);
	CHECK(camDiff < 0.35f, "the camera should have swung to the shooting direction (%.2f rad apart)", camDiff);
	CHECK(G.p.lastShotT > shot0, "R should shoot");
	Input a = idle(); shot0 = G.p.lastShotT; run(0.5f, a); CHECK(G.p.lastShotT == shot0, "A no longer shoots");
	Input aButton = idle(); aButton.hand = true; run(0.3f, aButton); CHECK(G.p.lastShotT == shot0, "the handbrake input does not shoot either");

	printf("motorcycle\n");
	game_start(&G); clear_world();
	int bi = game_add_car(&G, CAR_BIKE, SX + 6.4f, SZ - 10, 0); G.p.p[0] = SX + 4.5f; G.p.p[2] = SZ - 10; G.p.face = 0;
	Input bx = idle(); bx.enter_p = true; run(0.1f, bx);
	CHECK(G.p.car == bi, "X gets Otto onto the motorcycle");
	Input bgas = idle(); bgas.gas = true; run(1.5f, bgas);
	float bv = hypotf(G.car[bi].vx, G.car[bi].vz);
	CHECK(bv > 20.0f, "the motorcycle accelerates (%.1f m/s)", bv);
	Input bturn = bgas; bturn.lx = 1; run(0.8f, bturn);
	printf("   speed %.1f m/s, lean %.2f\n", bv, G.car[bi].lean);
	CHECK(G.car[bi].lean < -0.05f, "it leans into a right turn (%.2f)", G.car[bi].lean);
	Input bbrake = idle(); bbrake.brake = true; for (int i = 0; i < 900 && hypotf(G.car[bi].vx, G.car[bi].vz) > 2.0f; i++) run(1.0f / 60, bbrake);
	run(0.2f, idle()); Input bout = idle(); bout.enter_p = true; run(0.1f, bout);
	CHECK(G.p.car < 0, "X gets Otto off the motorcycle");

	printf("helicopter\n");
	game_start(&G); clear_world();
	int hi = game_add_car(&G, CAR_HELI, -38.0f, 140.0f, 1.57f); G.p.p[0] = -34.0f; G.p.p[2] = 140.0f; G.p.face = 0;
	Input hx = idle(); hx.enter_p = true; run(0.1f, hx);
	CHECK(G.p.car == hi, "X gets Otto into the helicopter");
	Input up = idle(); up.gas = true; run(1.0f, up);
	CHECK(G.car[hi].y < 1.0f, "the rotor needs time to spool up before it lifts (y %.1f after 1 s)", G.car[hi].y);
	run(3.0f, up);
	printf("   after 4 s of climbing: %.1f m up\n", G.car[hi].y);
	CHECK(G.car[hi].y > 15.0f, "R climbs (%.1f m)", G.car[hi].y);
	run(1.5f, idle()); float yc = G.car[hi].y; run(2.0f, idle()); CHECK(fabsf(G.car[hi].y - yc) < 3.0f, "with nothing pressed it hovers (%.1f -> %.1f)", yc, G.car[hi].y);
	Input out2 = idle(); out2.enter_p = true; run(0.1f, out2); CHECK(G.p.car == hi, "no jumping out of a helicopter in the air");
	float x0 = G.car[hi].x, z0 = G.car[hi].z; Input fw = idle(); fw.ly = 1; run(4.0f, fw);
	float fly = hypotf(G.car[hi].x - x0, G.car[hi].z - z0);
	printf("   4 s of forward: %.0f m, speed %.1f m/s\n", fly, hypotf(G.car[hi].vx, G.car[hi].vz));
	CHECK(fly > 40.0f, "forward flight covers ground (%.0f m)", fly);
	// a roof: fly low into a tall building and get stopped; high over it and pass
	game_start(&G); clear_world();
	float bx0 = 0, bz0 = 0, btop = 0;
	for (int i = 0; i < G.w.nbox; i++) if (G.w.box[i].y1 > 60.0f) { const Box *bb = &G.w.box[i]; bx0 = 0.5f * (bb->x0 + bb->x1); bz0 = bb->z0 - 60.0f; btop = bb->y1; break; }
	for (int pass = 0; pass < 2; pass++) {
		int h2 = game_add_car(&G, CAR_HELI, bx0, bz0, PI_F); G.car[h2].y = pass ? btop + 10.0f : 12.0f; G.car[h2].rpm = 1; G.p.car = h2; G.car[h2].driven = true; G.p.p[0] = bx0; G.p.p[2] = bz0;
		Input go = idle(); go.ly = 1; run(6.0f, go);
		float zi = G.car[h2].z; printf("   %s a %.0f m tower: stopped at z %.0f (started %.0f)\n", pass ? "over" : "into", btop, zi, bz0);
		if (pass) CHECK(zi > bz0 + 100.0f, "a helicopter above the roof flies over it"); else CHECK(zi < bz0 + 100.0f, "a helicopter at 12 m cannot fly through a %.0f m tower", btop);
		G.car[h2].on = false; G.p.car = -1;
	}
	game_start(&G); clear_world();
	hi = game_add_car(&G, CAR_HELI, -38.0f, 140.0f, 1.57f); G.p.p[0] = -34.0f; G.p.p[2] = 140.0f; hx = idle(); hx.enter_p = true; run(0.1f, hx);
	run(4.0f, up); run(1.0f, idle()); Input dn = idle(); dn.brake = true; for (int i = 0; i < 1200 && G.car[hi].y > 0.05f; i++) run(1.0f / 60, dn);
	run(0.5f, idle()); out2 = idle(); out2.enter_p = true; run(0.1f, out2);
	CHECK(G.p.car < 0, "after landing X gets Otto out (y %.2f)", G.car[hi].y);

	printf("survivability\n");
	game_start(&G); clear_world();
	CHECK(G.p.hp == PLAYER_MAX_HP, "starts at %.0f hp", PLAYER_MAX_HP);
	float before = G.p.hp; damage_player(&G, 40); printf("   40 damage costs %.1f of %.0f hp\n", before - G.p.hp, PLAYER_MAX_HP);
	CHECK(before - G.p.hp < 31.0f, "Otto takes less than 40 of a 40 hit");
	run(12.0f, idle()); CHECK(G.p.hp > before - 5.0f, "he recovers within a few seconds (%.0f)", G.p.hp);

	printf("%s (%d failure%s)\n", fails ? "FAILED" : "all control checks passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
