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
static void run(float s, Input in) { for (float t = 0; t < s; t += 1.0f / 60) { game_update(&G, &in, 1.0f / 60); in.enter_p = in.x_p = in.b_p = in.select_p = in.a_p = in.start_p = in.up_p = in.touch_p = false; in.weap = 0; } }

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

	printf("weapon wheel: D-pad up, then the D-pad or circle pad\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
	for (int w = 0; w < W_COUNT; w++) CHECK(G.p.has[w], "Otto starts with weapon %d", w);
	Input sel = idle(); sel.select_p = true; run(0.1f, sel);
	CHECK(!G.wheel, "SELECT no longer opens the wheel");
	Input upw = idle(); upw.up_p = true; upw.dpy = 1; run(0.1f, upw);
	CHECK(G.wheel && G.p.weapon == W_PISTOL, "D-pad up opens the wheel (and the same press does not pick fists: weapon %d)", G.p.weapon);
	Input dp = idle(); dp.dpx = 1; run(0.1f, dp); CHECK(G.p.weapon == W_SMG, "D-pad right = SMG (%d)", G.p.weapon);
	dp = idle(); dp.dpx = 1; dp.dpy = 1; run(0.1f, dp); CHECK(G.p.weapon == W_PISTOL, "D-pad up-right = pistol (%d)", G.p.weapon);
	dp = idle(); dp.dpx = 1; dp.dpy = -1; run(0.1f, dp); CHECK(G.p.weapon == W_SHOTGUN, "D-pad down-right = shotgun (%d)", G.p.weapon);
	dp = idle(); dp.dpy = -1; run(0.1f, dp); CHECK(G.p.weapon == W_BAT, "D-pad down = bat (%d)", G.p.weapon);
	dp = idle(); dp.lx = -1; ; dp.ly = -1; run(0.1f, dp); CHECK(G.p.weapon == W_RIFLE, "stick down-left = rifle (%d)", G.p.weapon);
	dp = idle(); dp.lx = -1; run(0.1f, dp); CHECK(G.p.weapon == W_SNIPER, "stick left = sniper (%d)", G.p.weapon);
	dp = idle(); dp.lx = -1; dp.ly = 1; run(0.1f, dp); CHECK(G.p.weapon == W_RPG, "stick up-left = rocket launcher (%d)", G.p.weapon);
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
	CHECK(bv > 14.0f, "the motorcycle accelerates (%.1f m/s)", bv);
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
	Input out2 = idle();
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

	printf("new weapons: bat, rifle, sniper, rocket launcher\n");
	game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
	{
		Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 60; q->x = SX + 3; q->z = SZ - 1.8f; q->state = PED_IDLE; q->timer = 100; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z);
		G.p.weapon = W_BAT; Input sw = idle(); sw.gas = true; run(1.2f, sw);
		CHECK(G.ped[0].dead || G.ped[0].hp < 40, "the bat hurts what is in front of Otto (hp %.0f)", G.ped[0].hp);
		G.ped[0].on = false;
		G.ped[1] = G.ped[0]; q = &G.ped[1]; q->on = true; q->dead = false; q->hp = 90; q->x = SX + 3; q->z = SZ - 20; q->state = PED_IDLE; q->timer = 100;
		G.p.weapon = W_RIFLE; Input rf = idle(); rf.aim = true; rf.gas = true; float t0 = G.p.lastShotT; run(0.8f, rf);
		CHECK(G.p.lastShotT > t0 && G.ped[1].hp < 90, "the rifle fires and hits (hp %.0f)", G.ped[1].hp);
		G.ped[1].on = false;
		G.ped[2] = G.ped[0]; q = &G.ped[2]; q->on = true; q->dead = false; q->hp = 100; q->x = SX + 3; q->z = SZ - 55; q->state = PED_IDLE; q->timer = 100;
		G.heat = 0; G.stars = 0; police_reset(&G); for (int k = 3; k < MAX_PED; k++) G.ped[k].on = false;
		G.p.weapon = W_SNIPER; Input sn = idle(); sn.aim = true; run(0.6f, sn); sn.gas = true; run(1.6f, sn);
		CHECK(G.ped[2].dead, "the sniper rifle kills at 55 m in one shot (hp %.0f)", G.ped[2].hp);
		G.ped[2].on = false;
		G.ped[3] = G.ped[0]; q = &G.ped[3]; q->on = true; q->dead = false; q->cop = 1; q->hp = 100; q->x = SX + 3; q->z = SZ - 30; q->state = PED_IDLE; q->timer = 100;
		G.p.weapon = W_RPG; Input rp = idle(); rp.aim = true; for (int k = 4; k < MAX_PED; k++) G.ped[k].on = false; run(0.6f, rp); rp.gas = true; bool boom = false; for (int i = 0; i < 150; i++) { for (int k = 4; k < MAX_PED; k++) G.ped[k].on = false; run(1.0f / 60, rp); for (int k = 0; k < MAX_BLAST; k++) { if (G.blast[k].on && !boom) printf("   blast at %.1f %.1f %.1f t=%.2f (otto %.1f %.1f face %.2f lock %d ped %.1f %.1f)\n", G.blast[k].x, G.blast[k].y, G.blast[k].z, G.blast[k].t, G.p.p[0], G.p.p[2], G.p.face, G.p.lock, G.ped[3].x, G.ped[3].z); boom |= G.blast[k].on; } }
		CHECK(boom && G.ped[3].dead, "the rocket explodes on its target (boom %d, dead %d, hp %.0f at %.1f,%.1f from %.1f,%.1f)", boom, G.ped[3].dead, G.ped[3].hp, G.ped[3].x, G.ped[3].z, G.p.p[0], G.p.p[2]);
	}

	printf("cover: Y behind a wall, slide to the corner, lean out and shoot round it\n");
	{
		game_start(&G); clear_world();
		const Box *wb = NULL;
		for (int i = 0; i < G.w.nbox && !wb; i++) {
			const Box *b = &G.w.box[i];
			if (b->z1 - b->z0 < 14 || b->x1 - b->x0 < 14 || b->y1 < 8 || b->x0 < 40) continue;
			bool clear = true;
			for (int j = 0; j < G.w.nbox; j++) { if (j == i) continue; const Box *o = &G.w.box[j]; if (o->x1 > b->x0 - 12 && o->x0 < b->x0 + 14 && o->z1 > b->z0 - 4 && o->z0 < b->z1 + 14) clear = false; }
			if (clear) wb = b;
		}
		CHECK(wb != NULL, "found a free wall to test with");
		if (wb) {
			float wx = wb->x0, mid = 0.5f * (wb->z0 + wb->z1);
			G.p.p[0] = wx - 1.0f; G.p.p[2] = mid; G.p.face = PI_F / 2; G.cam.yaw = PI_F / 2; G.cam.manualT = 0;
			Input y = idle(); y.x_p = true; run(0.1f, y);
			printf("   cover %d, normal (%.1f, %.1f), spot x %.2f (wall at %.2f)\n", G.p.cover, G.p.coverNx, G.p.coverNz, G.p.p[0], wx);
			CHECK(G.p.cover == 1 && G.p.coverNx < -0.9f, "Y next to a wall takes cover with the wall behind the cover normal");
			CHECK(fabsf(G.p.p[0] - (wx - COVER_WALL)) < 0.08f, "Otto stands half a metre off the wall (%.2f)", wx - G.p.p[0]);
			run(0.6f, idle()); CHECK(G.p.lean > 0.9f, "he leans back on the wall (%.2f)", G.p.lean);
			CHECK(fabsf(wrapang(G.p.face - atan2f(G.p.coverNx, -G.p.coverNz))) < 0.15f, "his back is to the wall, facing the street");
			float z0 = G.p.p[2]; Input sl = idle(); sl.lx = 1; run(1.0f, sl);
			CHECK(G.p.p[2] > z0 + 1.5f && fabsf(G.p.p[0] - (wx - COVER_WALL)) < 0.08f, "the stick slides him along the wall (moved %.2f)", G.p.p[2] - z0);
			for (int i = 0; i < 60 * 12 && G.p.p[2] < wb->z1 - 0.7f; i++) run(1.0f / 60, sl);        // slide until he is at the corner (and stop pushing)
			run(0.4f, idle());
			printf("   end of the wall z1 %.2f, Otto stopped at z %.2f\n", wb->z1, G.p.p[2]);
			CHECK(G.p.cover == 1 && G.p.p[2] < wb->z1 && G.p.p[2] > wb->z1 - 1.0f, "he stops at the corner and stays in cover");
			// somebody on the other side of the corner: hidden from here, visible once he leans out
			Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 20; q->x = wx + 8.0f; q->z = wb->z1 + 3.0f; q->state = PED_IDLE; q->timer = 100; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z);
			CHECK(!los_clear(&G, G.p.p[0], 1.3f, G.p.p[2], q->x, 1.2f, q->z), "the wall hides him from the target");
			Input aim = idle(); aim.aim = true; run(0.15f, aim);
			CHECK(G.p.peek == 1, "L at the south end leans him out of that end (peek %d)", G.p.peek);
			run(0.6f, aim);
			printf("   leaning out: peekT %.2f, z %.2f (wall end %.2f), lock %d\n", G.p.peekT, G.p.p[2], wb->z1, G.p.lock);
			CHECK(G.p.peekT > 0.9f && G.p.p[2] > wb->z1, "he is clear of the corner");
			CHECK(G.p.lock == 0, "he locks onto the person he can now see (lock %d)", G.p.lock);
			Input sh = aim; sh.gas = true; run(1.5f, sh);
			CHECK(G.ped[0].dead, "and shoots them round the corner");
			run(0.6f, idle());
			CHECK(G.p.peek == 0 && G.p.peekT < 0.1f && G.p.cover == 1, "letting go of L puts him back behind the corner (peekT %.2f)", G.p.peekT);
			// somebody behind him (back along the wall): he must not swing round to aim at them
			{ Ped *b = &G.ped[1]; memset(b, 0, sizeof *b); b->on = true; b->hp = 60; b->x = wx - 6.0f; b->z = wb->z1 - 14.0f; b->state = PED_IDLE; b->timer = 100; b->node = b->prev = traffic_nearest_node(&G, b->x, b->z);
			  Input a2 = idle(); a2.aim = true; run(0.9f, a2);
			  printf("   ped behind: lock %d, aim %.2f, cover heading %.2f\n", G.p.lock, G.p.aimYaw, G.p.coverRef);
			  CHECK(G.p.lock != 1, "no lock on somebody behind him");
			  CHECK(fabsf(wrapang(G.p.aimYaw - G.p.coverRef)) <= DEG(101), "he aims in his front area, not behind (%.0f deg off)", fabsf(wrapang(G.p.aimYaw - G.p.coverRef)) * 57.3f);
			  G.ped[1].on = false; run(0.6f, idle()); }
			// the camera looks along the wall's normal, behind Otto, and Otto is in view
			float fx = sinf(G.cam.yaw), fz = -cosf(G.cam.yaw), dx = G.p.p[0] - G.cam.eye[0], dz = G.p.p[2] - G.cam.eye[2], dl = hypotf(dx, dz);
			printf("   camera %.1f m from Otto, angle off centre %.0f deg\n", hypotf(dl, G.p.p[1] + 1.5f - G.cam.eye[1]), acosf((fx * dx + fz * dz) / dl) * 57.3f);
			CHECK(dl < 6.0f && (fx * dx + fz * dz) / dl > cosf(DEG(28)), "the camera keeps Otto in view");
			Input leave = idle(); leave.x_p = true; run(0.1f, leave);
			CHECK(G.p.cover == 0, "Y again leaves the cover");
			// keep pushing at the end of the wall: round the corner onto the next wall, and along it
			G.p.p[0] = wx - 1.0f; G.p.p[2] = wb->z1 - 3.0f; G.p.face = PI_F / 2; G.cam.yaw = PI_F / 2; G.cam.manualT = 0; memset(G.ped, 0, sizeof G.ped);
			{ Input y2 = idle(); y2.x_p = true; run(0.1f, y2); }
			CHECK(G.p.cover == 1 && G.p.coverNx < -0.9f, "cover again on the west face");
			Input push = idle(); push.lx = 1; run(3.0f, push);
			printf("   after pushing on: cover %d, normal (%.1f, %.1f), at (%.1f, %.1f) (wall corner %.1f, %.1f)\n", G.p.cover, G.p.coverNx, G.p.coverNz, G.p.p[0], G.p.p[2], wx, wb->z1);
			CHECK(G.p.cover == 1 && G.p.coverNz > 0.9f, "he went round the corner and now has the south face behind him");
			CHECK(G.p.p[0] > wx + 1.0f && G.p.p[2] > wb->z1 && G.p.p[2] < wb->z1 + 1.0f, "and he is sliding along it (x %.1f z %.1f)", G.p.p[0], G.p.p[2]);
			Input back2 = idle(); back2.lx = -1; run(5.0f, back2);
			CHECK(G.p.cover == 1 && G.p.coverNx < -0.9f && G.p.p[2] < wb->z1 && fabsf(G.p.p[0] - (wx - COVER_WALL)) < 0.3f, "pushing the other way takes him back round the corner to the west face (n %.1f %.1f, at %.1f %.1f)", G.p.coverNx, G.p.coverNz, G.p.p[0], G.p.p[2]);
		}
	}
	printf("cover: behind a car\n");
	{
		game_start(&G); clear_world();
		float cxp = SX + 20.0f, czp = SZ - 20.0f;
		int ci2 = game_add_car(&G, CAR_SEDAN, cxp, czp, 0.0f);
		G.p.p[0] = cxp + 2.2f; G.p.p[2] = czp; G.p.face = -PI_F / 2; G.cam.yaw = -PI_F / 2; G.cam.manualT = 0;
		int w0 = G.p.weapon; Input y = idle(); y.x_p = true; run(0.1f, y);
		printf("   cover %d (car %d), lateral %.2f, crouch %.2f\n", G.p.cover, G.p.coverCar, G.p.p[0] - cxp, G.p.crouch);
		CHECK(G.p.cover == 2 && G.p.coverCar == ci2, "Y next to a car takes cover behind it");
		run(0.5f, idle()); CHECK(G.p.crouch > 0.8f, "he crouches behind a car (%.2f)", G.p.crouch);
		{ Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 20; q->cop = 1; q->x = cxp - 10.0f; q->z = czp; q->state = PED_IDLE; q->timer = 100; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z);
		  Input ai = idle(); ai.aim = true; run(0.7f, ai);
		  printf("   mid-car, aim: peek %d peekT %.2f lock %d\n", G.p.peek, G.p.peekT, G.p.lock);
		  CHECK(G.p.peek == 2 && G.p.peekT > 0.9f, "L behind a car stands him up to aim over the roof (peek %d)", G.p.peek);
		  CHECK(G.p.lock == 0, "and locks onto the person across the car");
		  Input fi = ai; fi.gas = true; float car1 = G.car[ci2].hp; run(1.5f, fi);
		  CHECK(G.ped[0].dead && G.car[ci2].hp >= car1 - 0.5f, "he shoots over it without hitting his own cover (dead %d, car %.0f -> %.0f)", G.ped[0].dead, car1, G.car[ci2].hp);
		  G.ped[0].on = false; memset(G.bullet, 0, sizeof G.bullet); G.heat = 0; G.stars = 0; police_reset(&G); run(0.8f, idle()); CHECK(G.p.peek == 0 && G.p.crouch > 0.8f, "he ducks back down"); }
		{ Input sl = idle(); sl.lx = 1; run(2.5f, sl); Input ai = idle(); ai.aim = true; run(0.6f, ai);
		  printf("   at the car's front: peek %d, peekT %.2f, Otto z %.2f (car z %.2f, its front at %.2f)\n", G.p.peek, G.p.peekT, G.p.p[2], czp, czp - 2.3f);
		  CHECK(G.p.peek == 1 && G.p.peekT > 0.9f, "L at the end of the car leans him out round it");
		  CHECK(G.p.p[2] < czp - 2.4f, "he is clear of the car's end");
		  run(0.6f, idle()); Input back = idle(); back.lx = -1; run(2.5f, back); }
		// the car stops bullets from the far side, and they cost Otto nothing
		float hp0 = G.p.hp, car0 = G.car[ci2].hp; float m[3] = { cxp - 8.0f, 1.0f, czp };
		for (int k = 0; k < 6; k++) { fire_bullet(&G, 1, m, PI_F / 2, 0, &WPN[W_PISTOL], 1.0f); run(0.25f, idle()); }
		printf("   six shots through the car: Otto %.0f -> %.0f hp, car %.0f -> %.0f\n", hp0, G.p.hp, car0, G.car[ci2].hp);
		CHECK(G.p.hp >= hp0 - 0.5f, "bullets from the other side of the car do not reach Otto");
		CHECK(G.car[ci2].hp < car0, "they hit the car instead");
		Input leave = idle(); leave.x_p = true; run(0.1f, leave); CHECK(G.p.cover == 0, "Y leaves the car's cover");
		// the car drives off or burns: the cover is gone
		G.p.p[0] = cxp + 2.2f; run(0.05f, y); CHECK(G.p.cover == 2, "taking cover again");
		G.car[ci2].x += 3.0f; run(0.1f, idle()); CHECK(G.p.cover == 0, "cover ends when the car moves away");
		// in the open Y still cycles the weapon
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ - 30; w0 = G.p.weapon; run(0.1f, y);
		CHECK(G.p.cover == 0 && G.p.weapon != w0, "Y with nothing to hide behind still changes weapon (%d -> %d)", w0, G.p.weapon);
	}

	printf("street furniture as cover\n");
	{
		game_start(&G); clear_world();
		const Prop *pr = NULL;
		for (int k = 0; k < G.w.nprop && !pr; k++) if (G.w.prop[k].type == 0 && G.w.prop[k].face == 1 && G.w.prop[k].x > 100 && G.w.prop[k].z > 100) pr = &G.w.prop[k];
		CHECK(G.w.nprop > 100, "the city has street furniture (%d pieces)", G.w.nprop);
		CHECK(pr != NULL, "found a bin facing west");
		if (pr) {
			G.p.p[0] = pr->x - 1.1f; G.p.p[2] = pr->z; G.p.face = PI_F / 2; G.cam.yaw = PI_F / 2; G.cam.manualT = 0;
			Input y = idle(); y.x_p = true; run(0.1f, y);
			printf("   cover %d at %.2f (bin at %.2f), low %d\n", G.p.cover, G.p.p[0], pr->x, G.p.cover == 2);
			CHECK(G.p.cover == 2 && G.p.coverCar < 0, "Y next to a bin takes low cover");
			run(0.6f, idle()); CHECK(G.p.crouch > 0.8f, "he crouches behind it (%.2f)", G.p.crouch);
			float hp0 = G.p.hp; float m[3] = { pr->x + 5.0f, 0.6f, pr->z };
			for (int k = 0; k < 4; k++) { fire_bullet(&G, 1, m, -PI_F / 2, 0, &WPN[W_PISTOL], 1.0f); run(0.2f, idle()); }
			CHECK(G.p.hp >= hp0 - 0.5f, "low shots stop at the bin (%.0f -> %.0f)", hp0, G.p.hp);
			float m2[3] = { pr->x + 5.0f, 1.3f, pr->z };
			for (int k = 0; k < 4; k++) { fire_bullet(&G, 1, m2, -PI_F / 2, 0, &WPN[W_PISTOL], 1.0f); run(0.2f, idle()); }
			CHECK(G.p.hp >= hp0 - 0.5f, "level shots go over a crouched Otto (%.0f -> %.0f)", hp0, G.p.hp);
			Input lv = idle(); lv.x_p = true; run(0.1f, lv);
			Input walk = idle(); walk.ly = 1; G.cam.yaw = PI_F / 2; float x0 = G.p.p[0]; run(1.5f, walk);
			CHECK(G.p.p[0] < pr->x - 0.3f - 0.3f || fabsf(G.p.p[2] - pr->z) > 0.3f, "a bin is solid (x %.2f -> %.2f)", x0, G.p.p[0]);
		}
	}

	printf("regions: gate roads, carrying the vehicle, flying between regions\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = 260.0f; G.p.p[2] = CITY - 6.0f; G.p.face = PI_F; G.cam.yaw = PI_F; G.cam.manualT = 0;
		CHECK(G.region == REG_CITY, "the game starts in the city");
		Input wk = idle(); wk.ly = 1; run(20.0f, wk);
		printf("   walked south out of the city: region %d (airport %d), at (%.0f, %.0f), fade %.2f\n", G.region, REG_AIRPORT, G.p.p[0], G.p.p[2], G.fade);
		CHECK(G.region == REG_AIRPORT && G.p.p[2] < 20.0f, "the south gate road leads to the airport region (z %.0f)", G.p.p[2]);
		CHECK(G.w.region == REG_AIRPORT && G.w.nbox > 20, "which has its own world (%d boxes)", G.w.nbox);
		run(22.0f, wk);
		CHECK(G.region == REG_AIRPORT && G.p.p[2] > 20.0f, "and he can walk on in it (z %.0f)", G.p.p[2]);
		// a car comes along through the east gate
		game_start(&G); clear_world();
		int cg = game_add_car(&G, CAR_SPORTS, CITY - 40.0f, 255.0f, PI_F / 2); G.p.p[0] = CITY - 36.5f; G.p.p[2] = 255.0f; { Input ex = idle(); ex.enter_p = true; run(0.1f, ex); }
		Input gs = idle(); gs.gas = true; run(6.0f, gs);
		printf("   drove east: region %d (hills %d), in car %d, car at (%.0f, %.0f) speed %.0f\n", G.region, REG_HILLS, G.p.car, G.p.car >= 0 ? G.car[G.p.car].x : 0, G.p.car >= 0 ? G.car[G.p.car].z : 0, G.p.car >= 0 ? hypotf(G.car[G.p.car].vx, G.car[G.p.car].vz) : 0);
		(void)cg;
		CHECK(G.region == REG_HILLS && G.p.car >= 0 && G.car[G.p.car].kind == CAR_SPORTS, "the east gate leads to the hills and the car comes along");
		CHECK(G.p.p[0] < 40.0f, "arriving at the hills' west gate (x %.0f)", G.p.p[0]);
		Input back = idle(); back.gas = true; G.car[G.p.car].yaw = -PI_F / 2; G.car[G.p.car].vx = G.car[G.p.car].vz = 0; run(8.0f, back);
		CHECK(G.region == REG_CITY, "and the same road leads back (region %d)", G.region);
		// by air
		game_start(&G); clear_world();
		int hh = game_add_car(&G, CAR_HELI, -38.0f, 140.0f, 1.57f); G.p.p[0] = -34.0f; G.p.p[2] = 140.0f; { Input ex = idle(); ex.enter_p = true; run(0.1f, ex); }
		G.car[hh].y = 60.0f; G.car[hh].rpm = 1; G.car[hh].x = CITY + 100.0f; G.car[hh].z = 300.0f; G.car[hh].yaw = PI_F / 2;
		Input fw = idle(); fw.ly = 1; run(6.0f, fw);
		printf("   flew a helicopter east: region %d at (%.0f, %.0f) height %.0f\n", G.region, G.p.p[0], G.p.p[2], G.p.p[1]);
		CHECK(G.region == REG_HILLS && G.p.car >= 0 && G.p.p[1] > 20.0f, "flying out of the city's east side arrives in the hills, still in the air");
	}

	printf("world map: toggle, pick a town, fast travel\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.cash = 321;
		Input t = idle(); t.touch_p = true; t.tx = MAP_BTN_X + 10; t.ty = MAP_TOGGLE_Y + 5; run(0.05f, t);
		CHECK(G.mapMode == 1, "the WORLD MAP button switches the map to the world view");
		float x, y, w, h; world_tile(REG_HILLS, &x, &y, &w, &h);
		t = idle(); t.touch_p = true; t.tx = x + w / 2; t.ty = y + h / 2; run(0.05f, t); CHECK(G.mapSel == REG_HILLS, "tapping a town picks it (%d)", G.mapSel);
		world_tile(REG_CITY, &x, &y, &w, &h); t.tx = x + w / 2; t.ty = y + h / 2; run(0.05f, t); CHECK(G.mapSel == -1, "tapping the town you are in picks nothing");
		world_tile(REG_HILLS, &x, &y, &w, &h); t.tx = x + w / 2; t.ty = y + h / 2; run(0.05f, t);
		Input tr = idle(); tr.touch_p = true; tr.tx = MAP_BTN_X + 10; tr.ty = MAP_TRAVEL_Y + 5;
		G.heat = 300.0f; run(0.05f, idle()); run(0.05f, tr); run(1.5f, idle());
		CHECK(G.region == REG_CITY && G.regionGo < 0, "no fast travel while wanted");
		G.heat = 0; G.stars = 0; police_reset(&G); run(0.05f, tr); run(2.5f, idle());
		printf("   fast travel to the hills: region %d at (%.0f, %.0f), cash %d, map mode %d\n", G.region, G.p.p[0], G.p.p[2], G.p.cash, G.mapMode);
		CHECK(G.region == REG_HILLS && G.p.cash == 321 && G.mapMode == 0 && hypotf(G.p.p[0] - (SX + 3), G.p.p[2] - SZ) < 12.0f, "TRAVEL takes him to the picked town");
		// with a car: it comes along; in a helicopter in the air it refuses
		int cc = game_add_car(&G, CAR_SEDAN, SX + 6.4f, SZ - 10, 0); G.p.p[0] = SX + 3.5f; G.p.p[2] = SZ - 10; { Input ex = idle(); ex.enter_p = true; run(0.1f, ex); }
		CHECK(G.p.car == cc, "in a car"); (void)cc;
		t = idle(); t.touch_p = true; t.tx = MAP_BTN_X + 10; t.ty = MAP_TOGGLE_Y + 5; run(0.05f, t);
		world_tile(REG_AIRPORT, &x, &y, &w, &h); t.tx = x + w / 2; t.ty = y + h / 2; run(0.05f, t); run(0.05f, tr); run(2.5f, idle());
		CHECK(G.region == REG_AIRPORT && G.p.car >= 0 && G.car[G.p.car].kind == CAR_SEDAN, "the car travels with him");
		int hh = game_add_car(&G, CAR_HELI, 300.0f, 100.0f, 0); G.p.car = hh; G.car[hh].driven = true; G.car[hh].y = 50.0f; G.car[hh].rpm = 1;
		t = idle(); t.touch_p = true; t.tx = MAP_BTN_X + 10; t.ty = MAP_TOGGLE_Y + 5; run(0.05f, t);
		world_tile(REG_CITY, &x, &y, &w, &h); t.tx = x + w / 2; t.ty = y + h / 2; run(0.05f, t); run(0.05f, tr); run(2.0f, idle());
		CHECK(G.region == REG_AIRPORT, "no fast travel from a helicopter in the air (region %d)", G.region);
	}

	printf("every region works: cops, pickups, respawn, what is carried over\n");
	for (int r = 0; r < REG_COUNT; r++) {
		game_start(&G); G.p.cash = 777; G.p.has[W_RPG] = true; G.p.hp = 5000.0f; region_enter(&G, r);
		G.p.p[0] = 4 * PITCH + 3; G.p.p[2] = 4 * PITCH + 24; run(10.0f, idle());
		int cars = 0, peds = 0, picks = 0; for (int i = 0; i < MAX_CAR; i++) cars += G.car[i].on; for (int i = 0; i < MAX_PED; i++) peds += G.ped[i].on; for (int i = 0; i < MAX_PICKUP; i++) picks += G.pick[i].on;
		CHECK(G.region == r && cars >= 8 && peds >= 8 && picks >= 20 && G.npad >= 1, "region %d is populated (%d cars, %d people, %d pickups, %d helipads)", r, cars, peds, picks, G.npad);
		G.heat = 450.0f; int maxCops = 0; for (int i = 0; i < 40 * 60; i++) { run(1.0f / 60, idle()); if (i % 60 == 0) { int c = 0; for (int k = 0; k < MAX_PED; k++) c += G.ped[k].on && G.ped[k].cop; if (c > maxCops) maxCops = c; } if (G.p.ps != PS_ALIVE) break; }
		CHECK(maxCops >= 4, "the police come in region %d (%d officers)", r, maxCops);
		G.heat = 0; G.stars = 0; G.p.ps = PS_ALIVE; G.p.arrestT = 0; G.timeScale = 1.0f; police_reset(&G); G.p.hp = 1; damage_player(&G, 9999); run(5.0f, idle());
		CHECK(G.p.ps == PS_ALIVE && G.region == r && hypotf(G.p.p[0] - HOSPITAL_X, G.p.p[2] - HOSPITAL_Z) < 20.0f, "wasted in region %d: out of hospital in the same region", r);
		CHECK(G.p.cash > 500 && G.p.has[W_RPG], "cash and weapons are kept (%d)", G.p.cash);
	}

	printf("airport\n");
	{
		game_start(&G); region_enter(&G, REG_AIRPORT); clear_world(); G.p.p[0] = 350.0f; G.p.p[2] = CITY + 70.0f; G.p.face = PI_F; G.cam.yaw = PI_F; G.cam.manualT = 0;
		Input wk = idle(); wk.ly = 1; run(8.0f, wk);
		CHECK(G.p.p[2] < CITY + 90.0f, "the terminal is solid (z %.0f)", G.p.p[2]);
		G.p.p[0] = 200.0f; G.p.p[2] = CITY - 6.0f; run(12.0f, wk);
		CHECK(G.p.p[2] > CITY + 30.0f, "Otto can walk from the airport district onto the airfield (z %.0f)", G.p.p[2]);
		int c4 = game_add_car(&G, CAR_SPORTS, 128.0f, CITY - 40.0f, PI_F); G.p.p[0] = 131.5f; G.p.p[2] = CITY - 40.0f; Input ex = idle(); ex.enter_p = true; run(0.1f, ex);
		Input gs = idle(); gs.gas = true; run(6.0f, gs);
		CHECK(G.car[c4].z > CITY + 20.0f, "a car can drive onto the airfield (z %.0f)", G.car[c4].z);
	}

	printf("police are keener\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.heat = 25.0f; run(0.2f, idle());
		CHECK(G.stars >= 1, "a little crime already gives a star (heat %.0f)", G.heat);
		G.heat = 250.0f; int hp0 = 0; (void)hp0; float h0 = G.p.hp, minH = h0;
		float tDie = -1; for (int i = 0; i < 60 * 60; i++) { run(1.0f / 60, idle()); if (G.p.hp < minH) minH = G.p.hp; if (G.p.ps != PS_ALIVE) { tDie = i / 60.0f; break; } }
		printf("   Otto stood still and was wasted after %.0f s\n", tDie);
		int cops = 0; for (int i = 0; i < MAX_PED; i++) cops += G.ped[i].on && G.ped[i].cop; int pc = 0; for (int i = 0; i < MAX_CAR; i++) pc += G.car[i].on && G.car[i].bot;
		printf("   3 stars, standing still for 40 s: %d officers, %d police cars, Otto %.0f -> %.0f hp (state %d)\n", cops, pc, h0, minH, G.p.ps);
		CHECK(cops >= 4, "plenty of officers arrive (%d)", cops);
		CHECK(minH < h0 - 10.0f || G.p.ps != PS_ALIVE, "and they hurt Otto if he stands there");
	}

	printf("flying an airliner\n");
	{
		game_start(&G); region_enter(&G, REG_AIRPORT); clear_world();
		int pi = game_add_car(&G, CAR_PLANE, 90.0f, RWY2_Z, PI_F / 2); G.p.p[0] = 90.0f; G.p.p[2] = RWY2_Z + 4.0f; G.p.face = 0;
		Input ex = idle(); ex.enter_p = true; run(0.1f, ex);
		CHECK(G.p.car == pi, "X gets Otto into the airliner");
		Input th = idle(); th.gas = true; run(7.0f, th);
		float sp = hypotf(G.car[pi].vx, G.car[pi].vz);
		printf("   after 7 s of throttle: %.0f km/h at x %.0f, y %.1f\n", sp * 3.6f, G.car[pi].x, G.car[pi].y);
		CHECK(sp > 38.0f && G.car[pi].y < 0.5f, "it accelerates down the runway (%.0f m/s)", sp);
		Input pull = th; pull.ly = -1; run(6.0f, pull);
		printf("   pulling back: y %.0f m, pitch %.2f, speed %.0f m/s\n", G.car[pi].y, G.car[pi].pitch, hypotf(G.car[pi].vx, G.car[pi].vz));
		CHECK(G.car[pi].y > 40.0f, "pulling back lifts off and climbs (%.0f m)", G.car[pi].y);
		Input bank = th; bank.lx = 1; float yaw0 = G.car[pi].yaw; run(1.2f, bank);
		CHECK(wrapang(G.car[pi].yaw - yaw0) > 0.3f, "banking right turns right (%.2f rad)", wrapang(G.car[pi].yaw - yaw0));
		Input off = idle(); run(12.0f, off);
		CHECK(G.car[pi].y < 800.0f, "without throttle it settles");
		// a landing: throttle back and ease down
		G.car[pi].y = 60.0f; G.car[pi].pitch = 0; G.car[pi].lean = 0; G.car[pi].x = 20.0f; G.car[pi].z = RWY2_Z; G.car[pi].yaw = PI_F / 2; G.car[pi].vx = 55.0f; G.car[pi].vz = 0;
		for (int i = 0; i < 60 * 60 && G.car[pi].y > 0.05f; i++) {
			float target = G.car[pi].y > 12.0f ? -5.0f : -1.5f; Input in2 = idle(); in2.ly = clampf(-(target - G.car[pi].vy) * 0.15f, -1, 1); in2.gas = hypotf(G.car[pi].vx, G.car[pi].vz) < 50.0f; run(1.0f / 60, in2); }
		printf("   landed at x %.0f, y %.2f, hp %.0f\n", G.car[pi].x, G.car[pi].y, G.car[pi].hp);
		CHECK(G.car[pi].y <= 0.06f && G.car[pi].hp > 150.0f, "a gentle descent lands without wrecking it (hp %.0f)", G.car[pi].hp);
		Input br = idle(); br.brake = true; run(15.0f, br); run(0.2f, idle());
		Input out = idle(); out.enter_p = true; run(0.1f, out); CHECK(G.p.car < 0, "once stopped on the ground Otto climbs out");
	}

	printf("performance changes keep the behaviour\n");
	{
		unsigned st = 77; int bad = 0, hits = 0;
		for (int k = 0; k < 4000; k++) {                                    // the block-restricted ray cast must agree with testing every box
			float o[3] = { rndf(&st) * CITY, rndf(&st) * 60.0f, rndf(&st) * (CITY - 200.0f) }, d[3] = { rndf(&st) - 0.5f, (rndf(&st) - 0.5f) * 0.4f, rndf(&st) - 0.5f };
			float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); for (int q = 0; q < 3; q++) d[q] /= l;
			float len = rndf(&st) < 0.5f ? 8.0f : 150.0f, n1[3], n2[3];
			float a1 = city_ray(&G.w, o, d, len, n1), best = len; bool hit = false;
			for (int i = 0; i < G.w.nbox; i++) { const Box *b = &G.w.box[i]; const float lo[3] = { b->x0, b->y0, b->z0 }, hi[3] = { b->x1, b->y1, b->z1 }; if (ray_aabb(lo, hi, o, d, &best, n2)) hit = true; }
			float a2 = hit ? best : -1.0f; hits += hit;
			if ((a1 < 0) != (a2 < 0) || (a1 >= 0 && fabsf(a1 - a2) > 1e-3f)) bad++;
		}
		printf("   4000 random rays, %d hit something, %d disagreements\n", hits, bad);
		CHECK(bad == 0, "ray casts agree with the brute-force test (%d differ)", bad);
		// a 30 fps frame (one 33 ms step) must still behave: drive, hit a wall, walk into one
		game_start(&G); clear_world();
		int c5 = game_add_car(&G, CAR_SPORTS, SX + 6.4f, SZ - 60.0f, 0); G.p.p[0] = SX + 4.0f; G.p.p[2] = SZ - 60.0f; { Input ex = idle(); ex.enter_p = true; run(0.1f, ex); }
		Input gs = idle(); gs.gas = true; for (int i = 0; i < 30 * 3; i++) { game_update(&G, &gs, 1.0f / 30); gs.enter_p = false; }
		float v = hypotf(G.car[c5].vx, G.car[c5].vz);
		CHECK(v > 25.0f && isfinite(G.car[c5].x) && G.car[c5].z > 0 && G.car[c5].z < CITY, "a car driven at 30 fps behaves (%.0f m/s)", v);
		game_start(&G); clear_world(); G.p.p[0] = 76.3f; G.p.p[2] = 100.0f; G.cam.yaw = PI_F / 2;
		Input wk = idle(); wk.ly = 1; for (int i = 0; i < 30 * 4; i++) game_update(&G, &wk, 1.0f / 30);
		int inside = 0; for (int i = 0; i < G.w.nbox; i++) { const Box *b = &G.w.box[i]; if (G.p.p[0] > b->x0 + 0.1f && G.p.p[0] < b->x1 - 0.1f && G.p.p[2] > b->z0 + 0.1f && G.p.p[2] < b->z1 - 0.1f && G.p.p[1] < b->y1 - 0.1f) inside = 1; }
		CHECK(!inside, "walking into a wall at 30 fps does not get through it");
	}

	printf("wanted level pace\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.hp = 9000.0f; clear_world();
		float t5 = -1;
		G.heat = 900.0f;
		for (int i = 0; i < 100 * 60; i++) { run(1.0f / 60, idle()); if (i == 20 * 60) CHECK(G.stars <= 2, "after 20 s of maximum heat only %d stars", G.stars); if (G.stars >= 5 && t5 < 0) t5 = i / 60.0f; if (G.p.ps != PS_ALIVE) break; }
		printf("   5 stars after %.0f s even with the maximum heat\n", t5);
		CHECK(t5 > 45.0f, "five stars take a while to build (%.0f s)", t5);
		game_start(&G); clear_world(); float h0 = G.heat; G.p.p[0] = SX + 3; G.p.p[2] = SZ;
		for (int k = 0; k < 4; k++) { Ped *q = &G.ped[k]; memset(q, 0, sizeof *q); q->on = true; q->hp = 5; q->x = SX + 3; q->z = SZ - 5 - k; q->state = PED_IDLE; q->timer = 100; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z); }
		for (int k = 0; k < 4; k++) damage_ped(&G, k, 50, 1);
		printf("   four civilians killed: heat %.0f\n", G.heat - h0);
		CHECK(G.heat - h0 < 150.0f, "four killings are not yet a manhunt (heat %.0f)", G.heat - h0);
	}

	printf("respawn from the pause menu\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.cash = 1000; G.p.hp = 40;
		Input ps = idle(); ps.start_p = true; run(0.05f, ps); CHECK(G.paused, "START pauses");
		Input ap = idle(); ap.a_p = true; run(0.05f, ap);
		printf("   after A: paused %d, hp %.0f, cash %d, at (%.0f, %.0f) hospital (%.0f, %.0f)\n", G.paused, G.p.hp, G.p.cash, G.p.p[0], G.p.p[2], HOSPITAL_X, HOSPITAL_Z);
		CHECK(!G.paused && G.p.hp >= PLAYER_MAX_HP - 1 && G.p.cash == 1000 && hypotf(G.p.p[0] - HOSPITAL_X, G.p.p[2] - HOSPITAL_Z) < 15.0f, "A on the pause screen respawns him at the hospital with his cash");
		int cc = game_add_car(&G, CAR_SEDAN, SX + 6.4f, SZ - 10, 0); G.p.p[0] = SX + 3.5f; G.p.p[2] = SZ - 10; { Input ex = idle(); ex.enter_p = true; run(0.1f, ex); }
		CHECK(G.p.car == cc, "in a car"); run(0.05f, ps); run(0.05f, ap);
		CHECK(G.p.car < 0 && !G.car[cc].driven, "respawning from inside a car puts him on foot");
	}

	printf("police in a busy city (every car and pedestrian slot in use)\n");
	for (int trial = 0; trial < 3; trial++) {
		game_start(&G); G.p.p[0] = 120.0f + trial * 130.0f; G.p.p[2] = 200.0f + trial * 80.0f; G.p.hp = 5000.0f;
		run(15.0f, idle());
		int used = 0; for (int i = 0; i < MAX_CAR; i++) used += G.car[i].on;
		G.heat = 450.0f; int maxCops = 0, maxCars = 0; float first = -1;
		for (int i = 0; i < 40 * 60; i++) { run(1.0f / 60, idle()); if (i % 30 == 0) { int cops = 0, cars = 0; for (int k = 0; k < MAX_PED; k++) cops += G.ped[k].on && G.ped[k].cop && !G.ped[k].dead; for (int k = 0; k < MAX_CAR; k++) cars += G.car[k].on && G.car[k].bot; if (cops + cars > 0 && first < 0) first = i / 60.0f; if (cops > maxCops) maxCops = cops; if (cars > maxCars) maxCars = cars; } if (G.p.ps != PS_ALIVE) break; }
		printf("   4 stars in the city (%d/%d car slots in use): up to %d officers and %d police cars, the first after %.1f s\n", used, MAX_CAR, maxCops, maxCars, first);
		CHECK(maxCops >= 3 && maxCars >= 1 && first >= 0 && first < 5.0f, "the police come (officers %d, cars %d)", maxCops, maxCars);
	}

	printf("police when Otto is out on the airport\n");
	{
		game_start(&G); region_enter(&G, REG_AIRPORT); clear_world(); G.p.p[0] = 260.0f; G.p.p[2] = CITY + 250.0f; G.heat = 320.0f; G.p.hp = 5000.0f;
		int maxCops = 0, maxCars = 0;
		for (int i = 0; i < 90 * 60; i++) { run(1.0f / 60, idle()); if (i % 60 == 0) { int cops = 0, cars = 0; for (int k = 0; k < MAX_PED; k++) cops += G.ped[k].on && G.ped[k].cop && hypotf(G.ped[k].x - G.p.p[0], G.ped[k].z - G.p.p[2]) < 120.0f; for (int k = 0; k < MAX_CAR; k++) cars += G.car[k].on && G.car[k].bot && hypotf(G.car[k].x - G.p.p[0], G.car[k].z - G.p.p[2]) < 120.0f; if (cops > maxCops) maxCops = cops; if (cars > maxCars) maxCars = cars; } }
		printf("   5 stars on the airport: up to %d officers and %d police cars within 120 m\n", maxCops, maxCars);
		CHECK(maxCops >= 3, "officers do come to the airport (%d)", maxCops);
		CHECK(maxCars >= 1, "so do police cars (%d)", maxCars);
	}

	printf("skydiving: jumping out of a helicopter and a plane\n");
	{
		game_start(&G); clear_world();
		int h = game_add_car(&G, CAR_HELI, -38.0f, 140.0f, 1.57f); G.p.p[0] = -34.0f; G.p.p[2] = 140.0f; Input ex = idle(); ex.enter_p = true; run(0.1f, ex);
		Input up = idle(); up.gas = true; run(10.0f, up);
		float y0 = G.car[h].y; run(0.5f, idle());
		Input jump = idle(); jump.enter_p = true; run(0.1f, jump);
		CHECK(G.p.car < 0 && G.p.p[1] > 20.0f, "X in the air jumps out (%.0f m up)", y0);
		float hp0 = G.p.hp; float minVy = 0; bool opened = false;
		run(1.5f, idle()); CHECK(G.p.chute == 0 && G.p.hp >= hp0, "the parachute does not open by itself"); { Input px = idle(); px.enter_p = true; run(0.05f, px); }
		for (int i = 0; i < 60 * 60 && G.p.p[1] > 0.05f; i++) { run(1.0f / 60, idle()); if (G.p.v[1] < minVy) minVy = G.p.v[1]; if (G.p.chute > 0.9f) opened = true; }
		printf("   fell from %.0f m: fastest %.0f m/s, chute opened %d, hp %.0f -> %.0f, now y %.2f\n", y0, -minVy, opened, hp0, G.p.hp, G.p.p[1]);
		CHECK(opened && G.p.hp >= hp0 - 0.5f && G.p.p[1] <= 0.06f, "X opens the parachute and he lands unhurt");
		// jumping from a plane costs nothing, and falling all the way without the chute does
		{ int pl = game_add_car(&G, CAR_PLANE, 300.0f, RWY1_Z, PI_F / 2); G.car[pl].y = 120.0f; G.car[pl].vx = 50.0f; G.car[pl].vz = 0; G.car[pl].driven = true; G.p.car = pl; G.p.p[0] = 300.0f; G.p.p[2] = RWY1_Z; G.p.p[1] = 120.0f; G.p.hp = PLAYER_MAX_HP;
		  Input jp = idle(); jp.enter_p = true; run(0.05f, jp); CHECK(G.p.car < 0 && G.p.hp >= PLAYER_MAX_HP - 0.1f, "jumping out of a flying plane does no damage (hp %.0f)", G.p.hp);
		  for (int i = 0; i < 60 * 30 && G.p.p[1] > 0.05f; i++) run(1.0f / 60, idle()); run(0.3f, idle()); CHECK(G.p.ps != PS_ALIVE || G.p.hp < 60.0f, "falling to the ground without opening the parachute is deadly (hp %.0f)", G.p.hp); G.p.hp = PLAYER_MAX_HP; G.p.ps = PS_ALIVE; G.timeScale = 1.0f; G.hudT = 0; G.car[pl].on = false; }
		// too low for a chute
		G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.p[1] = 6.0f; G.p.v[1] = 0; G.p.airT = 0.01f; hp0 = G.p.hp; run(1.5f, idle());
		CHECK(G.p.hp < hp0 + 0.1f && G.p.p[1] <= 0.06f && G.p.chute == 0, "a short drop has no parachute (hp %.0f -> %.0f, y %.2f, chute %.2f, ps %d, car %d cover %d airT %.2f paused %d ts %.2f wheel %d fade %.2f go %d)", hp0, G.p.hp, G.p.p[1], G.p.chute, G.p.ps, G.p.car, G.p.cover, G.p.airT, G.paused, G.timeScale, G.wheel, G.fade, G.regionGo);
		// stepping off a roof is a fall too, landing on one is fine
		float rx = 0, rz = 0, rh = 0; for (int i = 0; i < G.w.nbox && rh == 0; i++) { const Box *b = &G.w.box[i]; if (b->y1 > 20.0f && b->y1 < 40.0f && b->kind == BK_BLDG && b->yb == 0 && city_roof_h(&G.w, 0.5f * (b->x0 + b->x1), 0.5f * (b->z0 + b->z1), 0.3f) == b->y1) { rx = 0.5f * (b->x0 + b->x1); rz = 0.5f * (b->z0 + b->z1); rh = b->y1; } }
		if (rh > 0) { G.p.p[0] = rx; G.p.p[2] = rz; G.p.p[1] = rh + 30.0f; G.p.airT = 0.01f; G.p.v[1] = 0; run(8.0f, idle()); printf("   parachuted onto a %.0f m roof: y %.1f\n", rh, G.p.p[1]); CHECK(fabsf(G.p.p[1] - rh) < 0.2f, "he lands on the roof (y %.1f, roof %.1f)", G.p.p[1], rh); }
	}

	printf("jumping out of a moving car\n");
	{
		game_start(&G); clear_world();
		int c3 = game_add_car(&G, CAR_SPORTS, SX + 6.4f, SZ - 200.0f, 0); G.p.p[0] = SX + 4.0f; G.p.p[2] = SZ - 200.0f; Input ex = idle(); ex.enter_p = true; run(0.1f, ex);
		Input gs = idle(); gs.gas = true; run(3.0f, gs);
		float sp = hypotf(G.car[c3].vx, G.car[c3].vz), hp0 = G.p.hp;
		run(0.05f, ex);
		printf("   jumped out at %.0f km/h: in car %d, hp %.0f -> %.0f\n", sp * 3.6f, G.p.car, hp0, G.p.hp);
		CHECK(sp > 15.0f && G.p.car < 0, "X gets Otto out of a car doing %.0f km/h", sp * 3.6f);
		CHECK(G.p.hp < hp0, "a fast jump hurts");
	}

	printf("D-pad: hold to glance, double-tap to turn\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0; G.cam.manualT = 0;
		Input dn = idle(); dn.dpy = -1; run(0.6f, dn);
		printf("   holding down: look %.2f (pi = %.2f), camera yaw %.2f, face %.2f\n", G.cam.look, PI_F, G.cam.yaw, G.p.face);
		CHECK(fabsf(fabsf(G.cam.look) - PI_F) < 0.1f, "holding down looks back");
		CHECK(fabsf(G.cam.yaw) < 0.05f && fabsf(G.p.face) < 0.05f, "a glance does not turn Otto");
		run(0.6f, idle()); CHECK(fabsf(G.cam.look) < 0.05f, "letting go returns the view (%.2f)", G.cam.look);
		Input rt = idle(); rt.dpx = 1; run(0.6f, rt); CHECK(fabsf(G.cam.look - PI_F / 2) < 0.1f, "holding right looks right (%.2f)", G.cam.look);
		run(0.6f, idle());
		Input lt = idle(); lt.dpx = -1; run(0.6f, lt); CHECK(fabsf(G.cam.look + PI_F / 2) < 0.1f, "holding left looks left (%.2f)", G.cam.look);
		run(0.6f, idle());
		float y0 = G.cam.yaw, f0 = G.p.face;
		run(0.05f, dn); run(0.08f, idle()); run(0.4f, dn);
		printf("   double-tap down: camera yaw %.2f -> %.2f, face %.2f -> %.2f, look %.2f\n", y0, G.cam.yaw, f0, G.p.face, G.cam.look);
		CHECK(fabsf(wrapang(G.p.face - f0 - PI_F)) < 0.1f && fabsf(wrapang(G.cam.yaw - y0 - PI_F)) < 0.1f, "double-tapping down turns Otto and the camera round");
		CHECK(fabsf(G.cam.look) < 0.1f, "and the held button does not also look back (%.2f)", G.cam.look);
		run(0.6f, idle());
		y0 = G.cam.yaw; run(0.05f, rt); run(0.08f, idle()); run(0.05f, rt); run(0.5f, idle());
		CHECK(fabsf(wrapang(G.cam.yaw - y0 - PI_F / 2)) < 0.1f, "double-tapping right turns a quarter to the right (%.2f)", wrapang(G.cam.yaw - y0));
		y0 = G.cam.yaw; run(0.05f, lt); run(0.08f, idle()); run(0.05f, lt); run(0.5f, idle());
		CHECK(fabsf(wrapang(G.cam.yaw - y0 + PI_F / 2)) < 0.1f, "double-tapping left turns a quarter to the left (%.2f)", wrapang(G.cam.yaw - y0));
		y0 = G.cam.yaw; run(0.05f, rt); run(0.6f, idle()); run(0.05f, rt); run(0.5f, idle());
		CHECK(fabsf(wrapang(G.cam.yaw - y0)) < 0.05f, "two slow taps only glance");
	}

	printf("camera follows Otto round when he changes direction (and the stick keeps its direction meanwhile)\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = 100.0f; G.p.p[2] = 256.0f; G.p.face = 0; G.cam.yaw = 0; G.cam.manualT = 0;           // along the main east-west street
		Input rt = idle(); rt.lx = 1; float x0 = G.p.p[0], z0 = G.p.p[2];
		run(0.5f, rt); float yaw05 = G.cam.yaw; run(2.5f, rt);
		printf("   ran right for 3 s: camera yaw %.2f (after 0.5 s %.2f), moved %.1f m east, %.1f m sideways\n", G.cam.yaw, yaw05, G.p.p[0] - x0, G.p.p[2] - z0);
		CHECK(G.cam.yaw > 1.0f, "the camera swings round behind him (%.2f rad)", G.cam.yaw);
		CHECK(yaw05 < 0.5f, "gently, not a snap (%.2f after half a second)", yaw05);
		CHECK(G.p.p[0] - x0 > 8.0f && fabsf(G.p.p[2] - z0) < 4.0f, "and he keeps running the way the stick was pushed");
		run(0.3f, idle()); Input up = idle(); up.ly = 1; x0 = G.p.p[0]; run(1.5f, up);
		CHECK(G.p.p[0] - x0 > 4.0f, "after letting go, forward is where the camera looks (moved %.1f m east)", G.p.p[0] - x0);
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ - 80; G.p.face = 0; G.cam.yaw = 0; G.cam.manualT = 0;
		Input dn = idle(); dn.ly = -1; z0 = G.p.p[2]; run(3.0f, dn);
		printf("   ran toward the camera: yaw %.2f, moved %.1f m south\n", G.cam.yaw, G.p.p[2] - z0);
		CHECK(fabsf(G.cam.yaw) > 1.8f && G.p.p[2] - z0 > 8.0f, "running toward the camera: it swings round behind him and he does not reverse (yaw %.2f)", G.cam.yaw);
		G.cam.manualT = 1.0f; { float yy = G.cam.yaw; Input cs = idle(); cs.ly = 1; G.cam.manualT = 1.5f; run(0.5f, cs); CHECK(fabsf(wrapang(G.cam.yaw - yy)) < 0.05f, "while the player turns the camera himself it is left alone"); }
	}

	printf("motorcycle wheelies\n");
	{
		game_start(&G); clear_world();
		int bw = game_add_car(&G, CAR_BIKE, SX + 6.4f, SZ + 200.0f, 0); G.p.p[0] = SX + 4.5f; G.p.p[2] = SZ + 200.0f; { Input ex = idle(); ex.enter_p = true; run(0.1f, ex); }
		Input gs = idle(); gs.gas = true; run(1.2f, gs);
		Input nb = gs; nb.ly = -1; Input nogas = idle(); nogas.ly = -1;
		float sp0 = hypotf(G.car[bw].vx, G.car[bw].vz); float pn = 0;
		run(0.3f, nogas); pn = G.car[bw].pitch;
		CHECK(pn < 0.1f, "pad back without the throttle is no wheelie (%.2f)", pn);
		run(1.2f, nb); printf("   speed %.0f m/s, pitch in the wheelie %.2f\n", sp0, G.car[bw].pitch);
		CHECK(G.car[bw].pitch > 0.45f && hypotf(G.car[bw].vx, G.car[bw].vz) > sp0 * 0.9f, "pad back with the throttle lifts the front wheel and he keeps going (pitch %.2f)", G.car[bw].pitch);
		float rl = G.car[bw].lean; Input steer = nb; steer.lx = 0.6f; run(0.6f, steer); CHECK(G.car[bw].pitch > 0.4f && G.car[bw].lean != rl, "he can steer during it");
		run(1.5f, gs); CHECK(G.car[bw].pitch < 0.12f, "letting the pad go puts the wheel back down (%.2f)", G.car[bw].pitch);
		G.car[bw].yaw = 0; G.car[bw].vx = 0; G.car[bw].vz = -40.0f; G.car[bw].pitch = 0; run(0.5f, nb); CHECK(G.car[bw].pitch < 0.3f, "no wheelie at top speed (%.2f, speed %.1f, yaw %.2f, vx %.1f vz %.1f)", G.car[bw].pitch, hypotf(G.car[bw].vx, G.car[bw].vz), G.car[bw].yaw, G.car[bw].vx, G.car[bw].vz);
	}

	printf("bat swing\n");
	{
		game_start(&G); clear_world(); G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0; G.p.weapon = W_BAT;
		Input sw = idle(); sw.gas = true; float maxU = 0; for (int i = 0; i < 60; i++) { run(1.0f / 60, sw); maxU = fmaxf(maxU, G.p.punchT); }
		printf("   swing lasts %.2f s\n", maxU);
		CHECK(maxU > 0.45f, "a bat swing is slower than a punch (%.2f s)", maxU);
	}

	printf("tyre smoke and the slower motorcycle\n");
	{
		game_start(&G); clear_world();
		int c2 = game_add_car(&G, CAR_SPORTS, SX + 6.4f, SZ - 200.0f, 0); G.p.p[0] = SX + 4.0f; G.p.p[2] = SZ - 200.0f; Input ex = idle(); ex.enter_p = true; run(0.1f, ex);
		Input gs = idle(); gs.gas = true; run(4.0f, gs);
		int smoke = 0; for (int i = 0; i < MAX_PART; i++) smoke += G.part[i].on && G.part[i].kind == PT_TIRE;
		CHECK(smoke == 0, "no tyre smoke while driving normally (%d)", smoke);
		float v0 = hypotf(G.car[c2].vx, G.car[c2].vz);
		Input br = idle(); br.brake = true; int peak = 0; for (int i = 0; i < 90; i++) { run(1.0f / 60, br); int n = 0; for (int k = 0; k < MAX_PART; k++) n += G.part[k].on && G.part[k].kind == PT_TIRE; if (n > peak) peak = n; }
		printf("   braked from %.0f m/s: %d smoke puffs at most\n", v0, peak);
		CHECK(peak >= 8, "braking hard from speed makes tyre smoke (%d)", peak);
		game_start(&G); clear_world();
		int b2 = game_add_car(&G, CAR_BIKE, SX + 6.4f, SZ - 200.0f, 0); G.p.p[0] = SX + 4.5f; G.p.p[2] = SZ - 200.0f; run(0.1f, ex);
		run(20.0f, gs); printf("   motorcycle top speed %.0f km/h\n", hypotf(G.car[b2].vx, G.car[b2].vz) * 3.6f);
		CHECK(hypotf(G.car[b2].vx, G.car[b2].vz) < 48.0f, "the motorcycle is capped lower than it was (%.0f m/s)", hypotf(G.car[b2].vx, G.car[b2].vz));
	}

	printf("survivability\n");
	game_start(&G); clear_world();
	CHECK(G.p.hp == PLAYER_MAX_HP, "starts at %.0f hp", PLAYER_MAX_HP);
	float before = G.p.hp; damage_player(&G, 40); printf("   40 damage costs %.1f of %.0f hp\n", before - G.p.hp, PLAYER_MAX_HP);
	CHECK(before - G.p.hp < 31.0f, "Otto takes less than 40 of a 40 hit");
	run(12.0f, idle()); CHECK(G.p.hp > before - 5.0f, "he recovers within a few seconds (%.0f)", G.p.hp);

	printf("%s (%d failure%s)\n", fails ? "FAILED" : "all control checks passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
