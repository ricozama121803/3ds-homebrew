// Headless fight simulation: an AI-driven "player" fights the opponent AI. Reports how the fights go and
// (with a directory argument) renders frames at the moment of impact.  Usage: simtest [fights] [outdir]
#include "../source/game.h"
#include "../source/hud.h"
#include "gfx_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void render_game(const Game *g, const char *dir, const char *name)
{
	Camera c;
	CamState cs = g->cam;
	camera_update(&cs, &g->f[0], &g->f[1], TICK_DT, g->time, &c);
	Lights lt = scene_lights();
	gfx_host_clear(scene_clear_top(), scene_clear_bottom());
	gfx_scene_begin(&c, &lt, 0);
	scene_draw(&g->f[0], &g->f[1], g->time, g->fx, MAX_FX, CAM_ANGLE);
	gfx_scene_end();
	hud_top(g);
	char path[512];
	snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
	gfx_host_write_ppm(path);
}

int main(int argc, char **argv)
{
	int fights = argc > 1 ? atoi(argv[1]) : 20;
	const char *dir = argc > 2 ? argv[2] : NULL;
	if (dir) { gfx_host_setup(400, 240, 3); }
	fighter_meshes_build();
	scene_build();

	int kos[3] = { 0, 0, 0 }, timeups = 0;
	long total_ticks = 0;
	int hits[2] = { 0, 0 }, blocks[2] = { 0, 0 }, atks[2] = { 0, 0 }, downs = 0, gbreaks = 0;
	int shots = 0;
	float first_ko_hp = 0;
	(void)first_ko_hp;

	for (int n = 0; n < fights; n++) {
		Game *g = malloc(sizeof *g);
		game_init(g);
		g->ai.rng = 1234567u + n * 7919u;
		Ai bot; ai_init(&bot, 99991u + n * 104729u, 0.35f + 0.5f * (n % 3) / 2.0f);
		int prev_hp[2] = { 100, 100 }, prev_atk[2] = { 0, 0 };
		int prev_hit_taken[2] = { 0, 0 };
		unsigned prev_held = 0;
		int pending = 0;
		(void)prev_held;
		for (int t = 0; t < 60 * (ROUND_SECONDS + 10); t++) {
			FighterInput pin = g->phase == PH_FIGHT ? ai_think(&bot, &g->f[0], &g->f[1]) : (FighterInput){ 0 };
			game_step(g, &pin);
			total_ticks++;
			for (int i = 0; i < 2; i++) {
				int a = g->f[i].st == ST_ATTACK && g->f[i].atk_t == 1;
				if (a) atks[i]++;
				prev_atk[i] = a;
				if (g->f[i].hit_taken != prev_hit_taken[i]) {
					hits[1 - i]++;
					prev_hit_taken[i] = g->f[i].hit_taken;
					if (dir && shots < 14 && n == 0 && pending == 0) pending = 5;         // render a few ticks in, when the spark has bloomed
				}
				if (g->f[i].st == ST_BLOCKSTUN && g->f[i].st_t == 1) blocks[i]++;
				if (g->f[i].st == ST_KNOCKDOWN && g->f[i].st_t == 1) downs++;
				if (g->f[i].st == ST_GUARDBREAK && g->f[i].st_t == 1) gbreaks++;
				prev_hp[i] = (int)g->f[i].hp;
			}
			if (pending > 0 && --pending == 0) {
				char nm[64]; snprintf(nm, sizeof nm, "hit_%02d", shots++);
				g->cam.shake = 0;
				render_game(g, dir, nm);
			}
			if (g->phase == PH_KO || g->phase == PH_TIMEUP) {
				if (g->phase_t > 60) break;
			}
		}
		if (g->phase == PH_KO) kos[g->winner]++;
		else timeups++;
		free(g);
	}
	printf("fights=%d  avg length=%.1fs  KOs: player %d, opponent %d  timeups %d\n", fights, total_ticks / 60.0f / fights, kos[0], kos[1], timeups);
	printf("attacks thrown: player-side %d, opponent %d | hits landed by player-side %d, by opponent %d\n", atks[0], atks[1], hits[0], hits[1]);
	printf("blocked hits: on player-side %d, on opponent %d | knockdowns %d | guard breaks %d\n", blocks[0], blocks[1], downs, gbreaks);
	if (dir) {
		printf("frames written to %s\n", dir);
	}
	return 0;
}
