// PC preview: renders poses and fight moments to PPM files using the software backend.
// Build/run with host/preview.sh
#include "../source/game.h"
#include "gfx_host.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static Fighter A, B;
static CamState cam;

static void face(Fighter *f, const Fighter *o) { f->yaw = atan2f(-(o->z - f->z), o->x - f->x); }

static void reset(float gap)
{
		Look la = look_player(), lb = look_opponent();
	fighter_init(&A, -gap * 0.5f, 0, 0, -1, &la);
	fighter_init(&B, gap * 0.5f, 0, PI_F, +1, &lb);
	face(&A, &B); face(&B, &A);
	memset(&cam, 0, sizeof cam);
}

static void render(const char *dir, const char *name, float time, float ang_override)
{
	fighter_pose(&A, time); fighter_pose(&B, time);
	Camera c;
	camera_update(&cam, &A, &B, 1.0f, time, &c);
	if (ang_override != 0) {                                 // orbit for a closer look
		float d = v3dist(c.eye, c.target);
		c.eye = v3(c.target.x + sinf(ang_override) * d, c.eye.y, c.target.z + cosf(ang_override) * d);
	}
	Lights lt = scene_lights();
	gfx_host_clear(scene_clear_top(), scene_clear_bottom());
	gfx_scene_begin(&c, &lt, 0);
	scene_draw(&A, &B, time, NULL, 0, CAM_ANGLE);
	gfx_scene_end();
	char path[512];
	snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
	gfx_host_write_ppm(path);
	printf("%s: %ld drawn, %ld submitted\n", name, gfx_host_triangles(), gfx_host_submitted());
}

static void attack(Fighter *f, const Fighter *foe, int id, int at_frame)
{
	const AttackDef *d = &ATTACKS[id];
	f->st = ST_ATTACK; f->atk = id;
	f->atk_t = at_frame >= 0 ? at_frame : d->startup + 1;
	Vec3 target = fight_aim_point(f, foe, id);
	f->strike = target;
}

// Play a real attack through the game logic (so the lunge and aim are the in-game ones) and render the frame it connects.
static void real_attack(const char *dir, const char *name, int id, float gap)
{
	static Game g;
	game_init(&g);
	g.phase = PH_FIGHT; g.phase_t = 200;
	g.f[0].x = -gap * 0.5f; g.f[1].x = gap * 0.5f;
	fighter_pose(&g.f[0], 0); fighter_pose(&g.f[1], 0);        // the rig must match the new positions before the first tick
	g.ai.cooldown = 1 << 20; g.ai.skill = 0;
	unsigned btn = id == ATK_LEAD_PUNCH ? BTN_LEAD_PUNCH : id == ATK_REAR_PUNCH ? BTN_REAR_PUNCH : id == ATK_LEAD_HOOK ? BTN_LEAD_PUNCH | BTN_MOD :
	               id == ATK_REAR_HOOK ? BTN_REAR_PUNCH | BTN_MOD : id == ATK_LEAD_KICK ? BTN_LEAD_KICK : BTN_REAR_KICK;
	FighterInput in = { 0 };
	in.held = btn; in.pressed = btn & ~BTN_MOD;
	for (int t = 0; t < 200; t++) {
		game_step(&g, &in);
		in.pressed = 0;
		if (g.f[0].st == ST_ATTACK && g.f[0].atk_t >= ATTACKS[id].startup + 1) break;
		if (g.f[0].st != ST_ATTACK && t > 3) break;
	}
	A = g.f[0]; B = g.f[1];
	cam = g.cam;
	{ const AttackDef *d = &ATTACKS[id]; Vec3 tip = A.rig.tip[d->limb]; float dh = v3dist(tip, B.rig.head_c), db = v3dist(tip, B.rig.chest_c);
	  Vec3 rt = d->limb < 2 ? A.rig.shoulder[d->limb] : A.rig.pelvis_c;
	  printf("  %s: gap %.2f -> %.2f, hit=%d  tip->head %.3f (need <%.3f) tip->chest %.3f  root->tip %.3f (reach %.2f) atk_t=%d\n", name, gap, fabsf(B.x - A.x), A.atk_hit, dh, d->tip_r + 0.115f, db, v3dist(rt, tip), d->reach, A.atk_t); }
	render(dir, name, g.time, 0);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp/ringside-preview";
	int hi = argc > 2 && !strcmp(argv[2], "hi");
	gfx_host_setup(hi ? 800 : 400, hi ? 480 : 240, hi ? 2 : 3);
	mesh_bank_vertex_count();
	fighter_set_detail(argc > 3 ? atoi(argv[3]) : 1);
	fighter_meshes_build();
	scene_build();

	reset(1.5f);
	render(dir, "01_idle", 0.3f, 0);
	{ extern void gfx_host_dump_notes(void); gfx_host_dump_notes(); }
	render(dir, "01b_idle_side", 0.3f, 1.35f);

	static const char *names[ATK_COUNT] = { "lead_punch", "rear_punch", "lead_hook", "rear_hook", "lead_kick", "rear_kick" };
	for (int i = 0; i < ATK_COUNT; i++) {
		reset(0.95f);
		fighter_pose(&B, 0.3f);
		attack(&A, &B, i, -1);
		char n[64]; snprintf(n, sizeof n, "02_%s", names[i]);
		render(dir, n, 0.3f, 0);
	}

	static const char *rn[ATK_COUNT] = { "jab", "cross", "lead_hook", "rear_hook", "teep", "roundhouse" };
	for (int i = 0; i < ATK_COUNT; i++) {
		char n[64]; snprintf(n, sizeof n, "07_real_%s", rn[i]);
		real_attack(dir, n, i, i >= ATK_LEAD_KICK ? 1.35f : 1.15f);
	}

	reset(1.5f);
	A.guard_amt[0] = 1; render(dir, "03_guard_head", 0.3f, 0);
	A.guard_amt[0] = 0; A.guard_amt[1] = 1; render(dir, "03b_guard_body", 0.3f, 0);
	A.guard_amt[1] = 0; A.duck = 1; render(dir, "04_duck", 0.3f, 0);
	A.duck = 0; A.lean = -1; render(dir, "04b_lean_back", 0.3f, 0);
	A.lean = 0; A.slip = 1; render(dir, "04c_slip", 0.3f, 0);
	A.slip = 0; A.walk_amt = 1; A.walk_phase = 1.0f; render(dir, "05_walk", 0.3f, 0);

	reset(1.5f);
	B.st = ST_KO; B.fall = 1.0f; B.react = 0.0f; render(dir, "06_ko", 0.3f, 0);
	return 0;
}
