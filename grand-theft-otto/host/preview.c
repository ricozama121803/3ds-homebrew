// Renders sample frames of the game on the PC (software rasteriser) so they can be inspected.
#ifndef HOST_PREVIEW
#define HOST_PREVIEW 1
#endif
#include "c2d_shim.h"
#include "../source/game.h"
#include "../source/render.h"
#include "../source/world.c"        // one translation unit so the shim framebuffer is shared
#include "../source/nav.c"
#include "../source/game.c"
#include "../source/car.c"
#include "../source/ped.c"
#include "../source/combat.c"
#include "../source/render.c"
#include <stdlib.h>
#include <string.h>

static const char *outdir = ".";
static Game G;

static void dump(const char *name, const char *suffix, int w, int h, float eye, float slider, int bottom)
{
	char path[512];
	shim_begin(w, h, C2D_Color32(20, 24, 40, 255));
	snprintf(path, sizeof path, "%s/%s_%s.txt", outdir, name, suffix);
	shim_textlog = fopen(path, "w");
	if (bottom) render_bottom(&G); else { render_top(&G, eye, slider); render_top_hud(&G, eye, slider); }
	fclose(shim_textlog); shim_textlog = NULL;
	printf("%-12s %s: %ld draws\n", name, suffix, shim_tris);
	snprintf(path, sizeof path, "%s/%s_%s.ppm", outdir, name, suffix);
	shim_write_ppm(path);
}

static void snap(const char *name, float slider)
{
	render_prepare(&G);
	dump(name, "L", 400, 240, -1, slider, 0);
	if (slider > 0) dump(name, "R", 400, 240, +1, slider, 0);
	dump(name, "B", 320, 240, 0, 0, 1);
}

static void run(float seconds, Input in)
{
	for (float t = 0; t < seconds; t += 1.0f / 30.0f) { game_update(&G, &in, 1.0f / 30.0f); in.y_p = in.x_p = in.tap = in.start_p = false; }
}

static void place(int tx, int ty, float heading)
{
	G.p.x = tx * TILE + 8; G.p.y = ty * TILE + 8; G.p.heading = heading; G.p.vx = G.p.vy = 0;
	G.camx = G.p.x - 200; G.camy = G.p.y - 120;
	float maxx = G.world.w * TILE - 400.0f, maxy = G.world.h * TILE - 240.0f;
	if (G.camx < 0) G.camx = 0; if (G.camy < 0) G.camy = 0; if (G.camx > maxx) G.camx = maxx; if (G.camy > maxy) G.camy = maxy;
}

static void fresh(const unsigned char *map, size_t n)
{
	game_init(&G, map, n, NULL);
	G.p.cash = 1240; G.p.has[W_SMG] = true; G.p.ammo[W_SMG] = 180; G.p.armor = 60;
}

int main(int argc, char **argv)
{
	if (argc > 1) outdir = argv[1];
	shim_gfxdir = outdir;
	static unsigned char mapdata[1 << 20];
	char mp[512]; snprintf(mp, sizeof mp, "%s/map.bin", outdir);
	FILE *f = fopen(mp, "rb"); if (!f) { fprintf(stderr, "no map.bin in %s\n", outdir); return 1; }
	size_t n = fread(mapdata, 1, sizeof mapdata, f); fclose(f);
	render_init();
	Input none; memset(&none, 0, sizeof none);

	// 1. a busy Hollywood Blvd: traffic and pedestrians
	fresh(mapdata, n); place(92, 57, 3.14159f);
	run(20, none); snap("street", 0.7f);

	// 2. stealing and driving a car
	fresh(mapdata, n); place(90, 100, 1.5708f);
	int ci = car_spawn(&G, 4, 90 * 16 + 40, 100 * 16 + 48, 1.5708f, CS_PARKED, DRV_NONE);
	G.p.x = G.cars[ci].x - 20 + 8; G.p.y = G.cars[ci].y - 12; G.camx = G.p.x - 200; G.camy = G.p.y - 120;
	{ Input in = none; in.y_p = true; run(0.1f, in); }
	{ Input in = none; in.a = true; run(1.6f, in); }
	snap("driving", 0.7f);

	// 3. a shootout: a pedestrian shot, blood, cops arriving
	fresh(mapdata, n); place(92, 57, 1.5708f);
	int pi_ = ped_spawn(&G, PK_CIV, 2, G.p.x + 50, G.p.y + 2); G.peds[pi_].turnT = 1000;
	int pj = ped_spawn(&G, PK_CIV, 4, G.p.x + 80, G.p.y - 14); G.peds[pj].turnT = 1000;
	{ Input in = none; in.a = true; for (int i = 0; i < 26; i++) { game_update(&G, &in, 1.0f / 30.0f); G.peds[pi_].x = G.p.x + 50; G.peds[pi_].y = G.p.y + 2; } }
	snap("shootout", 0.7f);

	// 4. a 4-star chase: police cars with flashing lights, cops on foot
	fresh(mapdata, n); place(100, 92, 3.14159f);
	G.p.hp = 100; G.p.weapon = W_SMG;
	add_heat(&G, 10.5f);
	{ Input in = none; in.mx = 1; in.run = true; G.p.x = 92 * 16 + 8; G.p.y = 57 * 16 + 8; run(9, in); }
	snap("chase", 0.7f);

	// 5. a car explodes
	fresh(mapdata, n); place(90, 100, 0.0f);
	int e = car_spawn(&G, 5, 90 * 16 + 60, 100 * 16 + 48, 1.5708f, CS_PARKED, DRV_NONE);
	G.cars[e].hitT = 3;
	{ Input in = none; in.a = true; G.p.heading = 1.5708f; G.p.x = 90 * 16 + 8; G.p.y = 100 * 16 + 48; run(0.3f, in); }
	damage_car(&G, e, 999, 0);
	run(0.28f, none); snap("explosion", 0.7f);
	run(1.5f, none); snap("aftermath", 0.7f);

	// 6. dead
	fresh(mapdata, n); place(92, 57, 1.5708f);
	damage_player(&G, 500, 1, 0);
	run(1.2f, none); snap("wasted", 0.0f);

	// 7. the Pay 'n' Spray and a driving player with wanted level
	fresh(mapdata, n); place(107, 68, 3.14159f);
	int sc = car_spawn(&G, 2, 107 * 16 + 8, 68 * 16 + 8, 3.14159f, CS_PARKED, DRV_NONE);
	G.p.x = G.cars[sc].x + 14; G.p.y = G.cars[sc].y; G.camx = G.p.x - 200; G.camy = G.p.y - 120;
	{ Input in = none; in.y_p = true; run(0.1f, in); }
	add_heat(&G, 4.0f);
	snap("spray", 0.7f);

	// 8. an assault rifle in Otto's hands, mid-burst
	fresh(mapdata, n); place(92, 57, 1.5708f);
	G.p.has[W_RIFLE] = true; G.p.ammo[W_RIFLE] = 300; G.p.weapon = W_RIFLE;
	{ Input in = none; in.a = true; run(0.45f, in); }
	snap("rifle", 0.7f);

	// 9. a helicopter climbing over Hollywood, with its rocket on the way
	fresh(mapdata, n); place(90, 73, 0.0f);
	{ int hc = car_spawn(&G, NCARVARS - 1, G.p.x, G.p.y - 16, 0.0f, CS_PARKED, DRV_NONE);
	  Input in = none; in.y_p = true; run(0.1f, in);
	  in = none; in.a = true; run(2.2f, in); in.x_p = true; in.l = true; run(0.4f, in);
	  (void)hc; }
	snap("heli", 0.7f);

	// 10. an RPG blast
	fresh(mapdata, n); place(92, 57, 1.5708f);
	G.p.has[W_RPG] = true; G.p.ammo[W_RPG] = 3; G.p.weapon = W_RPG;
	{ int a1 = ped_spawn(&G, PK_CIV, 1, G.p.x + 110, G.p.y); (void)a1; Input in = none; in.a = true; run(0.1f, in); in.a = false; run(0.5f, in); }
	snap("rpg", 0.7f);

	render_exit();
	return 0;
}
