// Renders sample frames of the game on the PC (software rasteriser) so they can be inspected.
#ifndef HOST_PREVIEW
#define HOST_PREVIEW 1
#endif
#include "c2d_shim.h"
#include "../source/game.h"
#include "../source/render.h"
#include "../source/game.c"      // one translation unit so the shim framebuffer is shared
#include "../source/render.c"
#define main simtest_main
#include "simtest.c"
#undef main
#include <stdlib.h>

static const char *outdir = ".";

static void dump(const char *name, const char *suffix, int w, int h, u32 clear, void (*draw)(const Game *, float), const Game *g, float eye, float slider)
{
	char path[512];
	shim_begin(w, h, clear);
	snprintf(path, sizeof path, "%s/%s_%s.txt", outdir, name, suffix);
	shim_textlog = fopen(path, "w");
	(void)draw;
	if (h == 240 && w == 400) render_top(g, eye, slider); else render_bottom(g);
	fclose(shim_textlog); shim_textlog = NULL;
	printf("%-10s %s: %ld triangles\n", name, suffix, shim_tris);
	snprintf(path, sizeof path, "%s/%s_%s.ppm", outdir, name, suffix);
	shim_write_ppm(path);
}

static void snap(const char *name, Game *g, float slider)
{
	render_prepare(g);
	u32 haze = render_haze(g);
	dump(name, "L", 400, 240, haze, NULL, g, -1, slider);
	if (slider > 0) dump(name, "R", 400, 240, haze, NULL, g, +1, slider);
	dump(name, "B", 320, 240, C2D_Color32(0, 0, 0, 255), NULL, g, 0, 0);
}

// advance the bot until something interesting is `ahead` units away in front of Tuffy
static void run_until_obstacle(Game *g, ObjType t, float ahead, float maxT)
{
	float tt = 0;
	while (g->state == ST_PLAY && tt < maxT) {
		bot(g); game_update(g, 1.0f / 60.0f); tt += 1.0f / 60.0f;
		for (int i = 0; i < g->nobjs; i++)
			if (g->objs[i].type == t && g->objs[i].z > PLAYER_Z + ahead - 0.1f && g->objs[i].z < PLAYER_Z + ahead + 0.1f) return;
	}
}


// world distance at which a landmark of `kind` sits `zTarget` units ahead
static float dist_for_landmark(int kind, float zTarget)
{
	int dizzy = kind >= LM_CAMPUS_COUNT;
	for (float d = 0; d < 6000; d += 0.25f) {
		int n0 = (int)floorf(d / 7.0f);
		for (int n = n0; n <= n0 + 11; n++) {
			int k, sd;
			if (landmark_for_slot(n, dizzy, &k, &sd) && k == kind && fabsf(n * 7.0f - d - zTarget) < 0.15f) return d;
		}
	}
	return 0;
}

static void add_scooter(Game *g, int lane, float eta)
{
	g->objs[g->nobjs++] = (Obj){ OBJ_SCOOTER, lane, PLAYER_Z - SCOOTER_SPEED * eta, 0 };
}

int main(int argc, char **argv)
{
	if (argc > 1) outdir = argv[1];
	shim_gfxdir = outdir;
	render_init();
	Game g;

	game_init(&g, 4210, 42);
	snap("title", &g, 0.0f);

	game_init(&g, 4210, 7); game_start(&g);
	for (int i = 0; i < 60 * 12; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
	snap("play_a", &g, 0.0f);
	snap("play_a3d", &g, 1.0f);

	run_until_obstacle(&g, OBJ_ROCK, 9.0f, 60);   snap("rock", &g, 0.6f);
	run_until_obstacle(&g, OBJ_HURDLE, 7.0f, 60); snap("hurdle", &g, 0.6f);
	run_until_obstacle(&g, OBJ_BEAM, 7.0f, 90);   snap("beam", &g, 0.6f);

	game_action(&g, ACT_JUMP);
	for (int i = 0; i < 20; i++) game_update(&g, 1.0f / 60.0f);
	snap("jump", &g, 0.6f);
	g.y = 0; g.vy = 0; g.slideT = 0.6f;
	snap("slide", &g, 0.6f);


	// scooter rider coming from behind: warning, nearly there, and after he has passed
	game_init(&g, 4210, 11); game_start(&g);
	for (int i = 0; i < 60 * 9; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
	g.nobjs = 0;
	int sl = g.lane == 1 ? -1 : 1;
	add_scooter(&g, sl, 1.25f); g.time = 9.0f;  snap("scoot_warn", &g, 0.5f);
	g.nobjs = 0; add_scooter(&g, sl, 1.0f);     snap("scoot_warn2", &g, 0.0f);
	g.nobjs = 0; add_scooter(&g, sl, 0.24f);    snap("scoot_near", &g, 0.5f);
	g.nobjs = 0; add_scooter(&g, sl, 0.13f);    snap("scoot_near2", &g, 0.5f);
	g.nobjs = 0; add_scooter(&g, sl, 0.05f);    snap("scoot_near3", &g, 0.5f);
	g.nobjs = 0; add_scooter(&g, sl, -0.3f);    snap("scoot_pass", &g, 0.5f);
	g.nobjs = 0; add_scooter(&g, g.lane, 0.05f);
	for (int i = 0; i < 12 && g.state == ST_PLAY; i++) game_update(&g, 1.0f / 60.0f);
	g.deadT = 0.7f; snap("scoot_hit", &g, 0.5f);

	// campus landmarks
	static const char *lmname[LM_COUNT] = { "lm_pollak", "lm_tsu", "lm_statue", "lm_mihaylo", "lm_gym", "lm_parking", "lm_pac",
	                                        "lm_arboretum", "lm_stadium", "lm_csuf", "lm_castle", "lm_matter", "lm_dumpo",
	                                        "lm_wheel", "lm_mookey", "lm_space", "lm_teacup", "lm_street" };
	for (int k = 0; k < LM_COUNT; k++) {
		game_init(&g, 4210, 21 + k); game_start(&g);
		for (int i = 0; i < 60 * 6; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
		g.nobjs = 0;
		if (k >= LM_CAMPUS_COUNT) { g.evKind = EV_DISNEY; g.evStart = -3000.0f; g.evEnd = 1e7f; }   // theme park everywhere
		else g.evKind = EV_NONE;
		g.dist = dist_for_landmark(k, k == 2 ? 11.0f : 14.0f);
		snap(lmname[k], &g, 0.6f);
	}

	// turns: the whole road bends 90 degrees (left, then right), then gets two extra lanes
	for (int run = 0; run < 2; run++) {
		static const float marks[8] = { -30, 40, 85, 140, 205, 235, 280, 335 };
		game_init(&g, 4210, 31 + run); game_start(&g);
		for (int i = 0; i < 60 * 10; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
		g.evKind = EV_TURN; g.evSide = run ? 1 : -1; g.evStart = g.dist + 120.0f; g.evEnd = g.evStart + TURN_TOTAL;
		for (int m = 0; m < 8; m++) {
			while (g.state == ST_PLAY && g.dist + PLAYER_Z < g.evStart + marks[m]) { bot(&g); game_update(&g, 1.0f / 60.0f); }
			char nm[32]; snprintf(nm, sizeof nm, "turn%d_%d", run, m);
			snap(nm, &g, 0.5f);
		}
	}

	// the university gate at the start of a run, and a petitioner and squirrels close up
	game_init(&g, 4210, 51); game_start(&g);
	g.time = 0; g.dist = 3.0f;    snap("start_gate0", &g, 0.6f);
	g.dist = 16.0f;               snap("start_gate1", &g, 0.6f);
	g.dist = 27.0f;               snap("start_gate2", &g, 0.6f);
	{
		game_init(&g, 4210, 52); game_start(&g);
		for (int i = 0; i < 60 * 8; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
		g.nobjs = 0; g.evKind = EV_NONE;
		g.objs[g.nobjs++] = (Obj){ OBJ_PETITION, 0, 12.0f, 0 };
		g.objs[g.nobjs++] = (Obj){ OBJ_PETITION, 1, 16.0f, 0 };
		g.objs[g.nobjs++] = (Obj){ OBJ_PETITION, -1, 26.0f, 0 };
		g.lane = -1; g.x = -LANE_W;
		snap("petition", &g, 0.6f);
		g.nobjs = 0; g.objs[g.nobjs++] = (Obj){ OBJ_PETITION, 0, 7.0f, 0 };
		snap("petition_near", &g, 0.6f);
		// find a moment with a squirrel mid-road
		for (float d = g.dist; d < g.dist + 400.0f; d += 0.5f) {
			g.dist = d; g.nobjs = 0;
			for (int n = (int)(d / 7.0f); n < (int)(d / 7.0f) + 12; n++) {
				if (n & 3) continue;
				unsigned sh = hash((unsigned)n * 131u + 17u);
				float z = n * 7.0f - d, u = (42.0f - z) / 37.0f;
				if (sh % 100 < 45 && u > 0.4f && u < 0.6f && z > 14.0f) { snap("squirrel", &g, 0.6f); d = 1e9f; break; }
			}
		}
	}

	// the fake theme park: gate, middle, exit gate
	{
		static const float marks[6] = { -45, 10, 60, 200, 330, 395 };
		game_init(&g, 4210, 41); game_start(&g);
		for (int i = 0; i < 60 * 10; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
		g.evKind = EV_DISNEY; g.evStart = g.dist + 100.0f; g.evEnd = g.evStart + DIZZY_LEN;
		for (int m = 0; m < 6; m++) {
			while (g.state == ST_PLAY && g.dist + PLAYER_Z < g.evStart + marks[m]) { bot(&g); game_update(&g, 1.0f / 60.0f); }
			char nm[32]; snprintf(nm, sizeof nm, "dizzy_%d", m);
			snap(nm, &g, 0.5f);
		}
	}

	// the ball park: approach, Big A, gate, bases, home, launch, landing
	{
		static const float marks[18] = { -30, 40, 62, 120, 160, 168, 200, 235, 262, 320, 355, 410, 445, 495, 528, 552, 580, 620 };
		game_init(&g, 4210, 61); game_start(&g);
		for (int i = 0; i < 60 * 10; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
		g.evKind = EV_STADIUM; g.evSide = -1; g.evStart = g.dist + 100.0f; g.evEnd = g.evStart + STAD_TOTAL;
		for (int m = 0; m < 18; m++) {
			while (g.state == ST_PLAY && g.dist + PLAYER_Z < g.evStart + marks[m]) { bot(&g); game_update(&g, 1.0f / 60.0f); }
			char nm[32]; snprintf(nm, sizeof nm, "stad_%02d", m);
			snap(nm, &g, 0.5f);
		}
		printf("state after stadium: %d\n", g.state);
	}

	// crash
	game_init(&g, 4210, 99); game_start(&g);
	while (g.state == ST_PLAY) game_update(&g, 1.0f / 60.0f);
	g.deadT = 0.6f; snap("dead", &g, 0.6f);
	g.deadT = 0.05f; snap("dead_hit", &g, 0.0f);

	game_init(&g, 4210, 5); game_start(&g);
	for (int i = 0; i < 60 * 5; i++) { bot(&g); game_update(&g, 1.0f / 60.0f); }
	g.state = ST_PAUSED; snap("pause", &g, 0.0f);

	render_exit();
	return 0;
}
