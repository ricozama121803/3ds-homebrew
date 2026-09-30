// Map and movement checks, run on the PC:  reachability of the whole city, and a walking fuzz test
// that verifies Tuffy can never end up inside a wall or outside the map.
#include "../source/world.c"
#include "../source/nav.c"
#include "../source/game.c"
#include "../source/car.c"
#include "../source/ped.c"
#include "../source/combat.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char mapdata[1 << 20];

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "assets/map.bin", "rb");
	if (!f) { fprintf(stderr, "cannot open map\n"); return 2; }
	size_t n = fread(mapdata, 1, sizeof mapdata, f); fclose(f);
	Game g;
	if (!game_init(&g, mapdata, n, NULL)) { fprintf(stderr, "bad map\n"); return 2; }
	const World *w = &g.world;
	int fails = 0;

	// ---- 1. reachability from the spawn (4-connected over non-solid tiles)
	static int comp[MAP_H][MAP_W];
	static int qx[MAP_W * MAP_H], qy[MAP_W * MAP_H];
	int walkable = 0, ncomp = 0, spawn_comp = 0, spawn_size = 0;
	for (int y = 0; y < w->h; y++) for (int x = 0; x < w->w; x++) { comp[y][x] = 0; if (!(world_flags(w, x, y) & F_SOLID)) walkable++; }
	for (int sy = 0; sy < w->h; sy++) for (int sx = 0; sx < w->w; sx++) {
		if ((world_flags(w, sx, sy) & F_SOLID) || comp[sy][sx]) continue;
		ncomp++;
		int head = 0, tail = 0, size = 0, minx = sx, maxx = sx, miny = sy, maxy = sy;
		qx[tail] = sx; qy[tail++] = sy; comp[sy][sx] = ncomp;
		while (head < tail) {
			int x = qx[head], y = qy[head++]; size++;
			if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y;
			static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
			for (int k = 0; k < 4; k++) {
				int nx = x + dx[k], ny = y + dy[k];
				if (nx < 0 || ny < 0 || nx >= w->w || ny >= w->h) continue;
				if ((world_flags(w, nx, ny) & F_SOLID) || comp[ny][nx]) continue;
				comp[ny][nx] = ncomp; qx[tail] = nx; qy[tail++] = ny;
			}
		}
		if (comp[w->spawny][w->spawnx] == ncomp) { spawn_comp = ncomp; spawn_size = size; }
		else if (size >= 4) { printf("  walled-off area of %d tiles at x %d..%d, y %d..%d\n", size, minx, maxx, miny, maxy); }
	}
	printf("walkable tiles: %d, reachable from spawn: %d (%.1f%%), separate areas: %d\n", walkable, spawn_size, 100.0 * spawn_size / walkable, ncomp);
	if (spawn_size < walkable * 0.97) { printf("FAIL: more than 3%% of the walkable city is cut off from the spawn\n"); fails++; }

	// every street must be continuous (cars follow them): each tile of each street rectangle has to be road-flagged
	{
		int broken = 0;
		for (int i = 0; i < NHROADS; i++)
			for (int y = hroads[i].a0; y <= hroads[i].a1; y++) for (int x = hroads[i].b0; x <= hroads[i].b1; x++)
				if (!(world_flags(w, x, y) & F_ROAD)) { if (broken < 5) printf("  street tile %d,%d (horizontal street %d) is not road\n", x, y, i); broken++; }
		for (int i = 0; i < NVROADS; i++)
			for (int y = vroads[i].b0; y <= vroads[i].b1; y++) for (int x = vroads[i].a0; x <= vroads[i].a1; x++)
				if (!(world_flags(w, x, y) & F_ROAD)) { if (broken < 5) printf("  street tile %d,%d (vertical street %d) is not road\n", x, y, i); broken++; }
		printf("streets: %d broken tiles\n", broken);
		if (broken) { printf("FAIL: buildings or scenery were painted over streets\n"); fails++; }
	}

	// key places must be reachable
	static const struct { const char *name; int x, y; } poi[] = {
		{"Hollywood Blvd (spawn)", 92, 57}, {"Griffith Observatory plaza", 121, 14}, {"Santa Monica pier", 16, 63}, {"Venice boardwalk", 25, 88},
		{"Beverly Hills street", 46, 48}, {"downtown street", 100, 92}, {"Dodger Stadium track", 130, 33}, {"LA River bed", 139, 60}, {"LAX runway", 80, 155},
		{"Coliseum track", 109, 131}, {"freeway 405", 62, 50}, {"industrial strip alley", 158, 34}, {"Silver Lake lawn", 90, 49}};
	for (unsigned i = 0; i < sizeof poi / sizeof poi[0]; i++) {
		int ok = comp[poi[i].y][poi[i].x] == spawn_comp;
		if (!ok) { printf("FAIL: %s (%d,%d) is not reachable\n", poi[i].name, poi[i].x, poi[i].y); fails++; }
	}

	// ---- 2. walking fuzz: random stick input, both smooth and hitching frame rates
	unsigned rng = 12345;
	#define RND() (rng = rng * 1664525u + 1013904223u, (rng >> 8) & 0xFFFF)
	long steps = 0, bad = 0, wallhugs = 0;
	for (int run = 0; run < 60; run++) {
		Game t; game_init(&t, mapdata, n, NULL);
		t.p.x = (30 + RND() % 100) * TILE + 8; t.p.y = (20 + RND() % 110) * TILE + 8;
		if (world_blocked(&t.world, t.p.x, t.p.y, PLAYER_HALF)) continue;      // random start landed inside something
		Input in = {0};
		float dt = (run % 3 == 0) ? 0.05f : 1.0f / 60.0f;                       // a third of the runs simulate very slow frames
		for (int s = 0; s < 6000; s++) {
			if (s % 40 == 0) { float a = (RND() / 65535.0f) * 6.2831853f; float m = (RND() % 4 == 0) ? 0.3f : 1.0f; in.mx = cosf(a) * m; in.my = sinf(a) * m; in.run = RND() & 1; }
			game_update(&t, &in, dt);
			steps++;
			if (world_blocked(&t.world, t.p.x, t.p.y, PLAYER_HALF - 0.01f)) { bad++; if (bad < 4) printf("  inside a wall at %.1f, %.1f\n", t.p.x, t.p.y); }
			if (t.p.x < 0 || t.p.y < 0 || t.p.x > w->w * TILE || t.p.y > w->h * TILE) { bad++; }
			if (t.camx < 0 || t.camy < 0 || t.camx > w->w * TILE - SCREEN_W + 0.01f || t.camy > w->h * TILE - SCREEN_H + 0.01f) { bad++; }
		}
		(void)wallhugs;
	}
	printf("walking fuzz: %ld frames simulated, %ld invalid positions\n", steps, bad);
	if (bad) { printf("FAIL: Tuffy or the camera ended up somewhere invalid\n"); fails++; }

	// ---- 3. a straight run into a wall must stop exactly at the wall
	{
		Game t; game_init(&t, mapdata, n, NULL);
		t.p.x = 43.5f * TILE; t.p.y = 95.5f * TILE;                              // sidewalk south of the police station
		Input in; memset(&in, 0, sizeof in); in.my = -1; in.run = true;
		for (int s = 0; s < 240; s++) game_update(&t, &in, 1.0f / 60.0f);
		int ty = (int)floorf((t.p.y - PLAYER_HALF - 0.1f) / TILE);
		printf("ran north into a building: stopped at y=%.1f (tile row %d above is %s)\n", t.p.y, ty, (world_flags(w, 43, ty) & F_SOLID) ? "solid" : "open");
		if (world_blocked(&t.world, t.p.x, t.p.y, PLAYER_HALF - 0.01f)) { printf("FAIL: inside the wall\n"); fails++; }
	}

	printf(fails ? "RESULT: %d problem(s)\n" : "RESULT: all checks passed\n", fails);
	return fails ? 1 : 0;
}
