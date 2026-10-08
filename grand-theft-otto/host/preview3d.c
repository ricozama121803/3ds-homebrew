// Renders frames of the 3D view on the PC: the same triangles r3d.c builds for the 3DS, drawn by a small software rasteriser
// (perspective-correct textures, z-buffer, alpha blending). Usage: preview3d <dir with map.bin + world.rgba + sprites.rgba> [out dir]
#define HOST_PREVIEW 1
#include "../source/game.h"
#include "../source/world.c"
#include "../source/nav.c"
#include "../source/game.c"
#include "../source/car.c"
#include "../source/ped.c"
#include "../source/combat.c"
#include "../source/r3d.c"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// the 2D renderer is not part of this build: stubs for the shim symbols game.c does not need
static Game G;
static R3DScene scene;
static unsigned char *tex_world, *tex_sprites;
static int ww, wh, sw_, sh_;

static unsigned char *load_rgba(const char *path, int *w, int *h)
{
	FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "missing %s\n", path); exit(1); }
	int hdr[2]; if (fread(hdr, 4, 2, f) != 2) exit(1);
	*w = hdr[0]; *h = hdr[1];
	unsigned char *p = malloc((size_t)*w * *h * 4);
	if (fread(p, 4, (size_t)*w * *h, f) != (size_t)*w * *h) exit(1);
	fclose(f);
	return p;
}

#define FW 800            // 2x the 3DS top screen, so details can be inspected
#define FH 480
static float zbuf[FW * FH];
static float fb[FW * FH * 3];

static void sample(const unsigned char *t, int w, int h, float u, float v, float out[4])
{
	float x = u * w - 0.5f, y = (1.0f - v) * h - 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	for (int c = 0; c < 4; c++) out[c] = 0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			int xx = x0 + i, yy = y0 + j;
			xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx; yy = yy < 0 ? 0 : yy >= h ? h - 1 : yy;
			float wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
			const unsigned char *p = t + ((size_t)yy * w + xx) * 4;
			for (int c = 0; c < 4; c++) out[c] += p[c] * wgt / 255.0f;
		}
}

typedef struct { float x, y, z, w, u, v, r, g, b, a; } PV;      // screen-space vertex

static void project(const R3DScene *s, const Vtx *v, PV *o)
{
	const float *m = s->view;
	float x = m[0] * v->x + m[1] * v->y + m[2] * v->z + m[3];
	float y = m[4] * v->x + m[5] * v->y + m[6] * v->z + m[7];
	float z = m[8] * v->x + m[9] * v->y + m[10] * v->z + m[11];
	float f = 1.0f / tanf(s->fov_deg * 0.5f * 3.14159265f / 180.0f);
	float asp = (float)FW / FH, n = 8.0f, fa = 1400.0f;
	float cx = x * f / asp, cy = y * f, cz = (-(fa + n) * z - 2 * fa * n) / (fa - n), cw = -z;
	o->w = cw; o->x = cx; o->y = cy; o->z = cz;
	o->u = v->u; o->v = v->v;
	o->r = v->r; o->g = v->g; o->b = v->b; o->a = v->a;
}

static void raster(const PV *a, const PV *b, const PV *c, const unsigned char *tex, int tw, int th, bool blend, bool depth_write, bool atest)
{
	// near-plane clipping is skipped: triangles with any vertex behind the camera are dropped (the builder culls those anyway)
	if (a->w < 1.0f || b->w < 1.0f || c->w < 1.0f) return;
	float sx[3], sy[3], iw[3];
	const PV *p[3] = {a, b, c};
	for (int i = 0; i < 3; i++) { iw[i] = 1.0f / p[i]->w; sx[i] = (p[i]->x * iw[i] * 0.5f + 0.5f) * FW; sy[i] = (1.0f - (p[i]->y * iw[i] * 0.5f + 0.5f)) * FH; }
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
	if (fabsf(area) < 1e-6f) return;
	int minx = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))), maxx = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
	int miny = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))), maxy = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
	if (minx < 0) minx = 0; if (miny < 0) miny = 0; if (maxx > FW - 1) maxx = FW - 1; if (maxy > FH - 1) maxy = FH - 1;
	for (int y = miny; y <= maxy; y++)
		for (int x = minx; x <= maxx; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
			float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			float z = w0 * p[0]->z * iw[0] + w1 * p[1]->z * iw[1] + w2 * p[2]->z * iw[2];     // ndc z (affine in screen space)
			z = w0 * (p[0]->z * iw[0]) + w1 * (p[1]->z * iw[1]) + w2 * (p[2]->z * iw[2]);
			float *zb = &zbuf[y * FW + x];
			if (z >= *zb) continue;
			float pw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
			float u = (w0 * p[0]->u * iw[0] + w1 * p[1]->u * iw[1] + w2 * p[2]->u * iw[2]) / pw;
			float v = (w0 * p[0]->v * iw[0] + w1 * p[1]->v * iw[1] + w2 * p[2]->v * iw[2]) / pw;
			float col[4], t[4];
			sample(tex, tw, th, u, v, t);
			col[0] = t[0] * (w0 * p[0]->r + w1 * p[1]->r + w2 * p[2]->r); col[1] = t[1] * (w0 * p[0]->g + w1 * p[1]->g + w2 * p[2]->g);
			col[2] = t[2] * (w0 * p[0]->b + w1 * p[1]->b + w2 * p[2]->b); col[3] = t[3] * (w0 * p[0]->a + w1 * p[1]->a + w2 * p[2]->a);
			if (atest && col[3] < 0.5f) continue;
			float *o = &fb[(y * FW + x) * 3];
			if (blend) { for (int k = 0; k < 3; k++) o[k] = col[k] * col[3] + o[k] * (1 - col[3]); }
			else { for (int k = 0; k < 3; k++) o[k] = col[k]; }
			if (depth_write) *zb = z;
		}
}

static void draw_scene(const R3DScene *s)
{
	for (int i = 0; i < FW * FH; i++) { zbuf[i] = 1e9f; fb[i * 3] = s->sky[0]; fb[i * 3 + 1] = s->sky[1]; fb[i * 3 + 2] = s->sky[2]; }
	for (int g = 0; g < G_COUNT; g++) {
		const unsigned char *tex = g == G_SPRITE ? tex_sprites : tex_world;
		int tw = g == G_SPRITE ? sw_ : ww, th = g == G_SPRITE ? sh_ : wh;
		for (int i = 0; i + 2 < s->count[g]; i += 3) {
			PV a, b, c;
			project(s, &s->buf[s->first[g] + i], &a); project(s, &s->buf[s->first[g] + i + 1], &b); project(s, &s->buf[s->first[g] + i + 2], &c);
			raster(&a, &b, &c, tex, tw, th, g != G_OPAQUE, g == G_OPAQUE, g == G_OPAQUE);
		}
	}
}

static void save(const char *dir, const char *name)
{
	char path[600]; snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
	FILE *f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", FW, FH);
	for (int i = 0; i < FW * FH * 3; i++) { float v = fb[i]; v = v < 0 ? 0 : v > 1 ? 1 : v; fputc((int)(v * 255 + 0.5f), f); }
	fclose(f);
}

static void snap(const char *dir, const char *name)
{
	scene.buf = malloc(sizeof(Vtx) * CAP_TOTAL);
	r3d_build(&G, &scene);
	draw_scene(&scene);
	printf("%-10s opaque %5d  alpha %4d  sprites %5d vertices\n", name, scene.count[G_OPAQUE], scene.count[G_ALPHA], scene.count[G_SPRITE]);
	save(dir, name);
	free(scene.buf);
}

static void run(float seconds, Input in)
{
	for (float t = 0; t < seconds; t += 1.0f / 30.0f) { game_update(&G, &in, 1.0f / 30.0f); in.y_p = in.x_p = in.tap = in.start_p = false; }
}

static void place(int tx, int ty, float heading)
{
	G.p.x = tx * TILE + 8; G.p.y = ty * TILE + 8; G.p.heading = heading; G.p.vx = G.p.vy = 0;
	G.camx = G.p.x - 200; G.camy = G.p.y - 120; G.camyaw = 0;
}

static unsigned char mapdata[1 << 20];
static void fresh(size_t n)
{
	game_init(&G, mapdata, n, NULL);
	G.view3d = true; r3d_init(&G.world);
	G.p.cash = 1240; G.p.has[W_SMG] = true; G.p.ammo[W_SMG] = 180; G.p.armor = 60;
}

// build the scene all over the map, in all kinds of situations, and check nothing overflows or goes NaN
static int stress(size_t n)
{
	int bad = 0, maxo = 0, maxa = 0, maxs = 0;
	scene.buf = malloc(sizeof(Vtx) * CAP_TOTAL);
	unsigned rng = 12345;
	for (int round = 0; round < 40; round++) {
		fresh(n);
		rng = rng * 1664525u + 1013904223u; int tx = 4 + (rng >> 8) % 248;
		rng = rng * 1664525u + 1013904223u; int ty = 4 + (rng >> 8) % 248;
		place(tx, ty, 0);
		if (world_blocked(&G.world, G.p.x, G.p.y, PLAYER_HALF)) continue;
		G.time = (float)(rng % 480);
		add_heat(&G, (float)(rng % 20));
		Input in; memset(&in, 0, sizeof in);
		for (int f = 0; f < 600; f++) {
			rng = rng * 1664525u + 1013904223u;
			in.mx = ((rng >> 8) & 255) / 128.0f - 1.0f; in.my = ((rng >> 16) & 255) / 128.0f - 1.0f; in.a = (rng >> 5) & 1; in.run = true;
			game_update(&G, &in, 1.0f / 30.0f);
			if (f % 15) continue;
			r3d_build(&G, &scene);
			maxo = scene.count[G_OPAQUE] > maxo ? scene.count[G_OPAQUE] : maxo; maxa = scene.count[G_ALPHA] > maxa ? scene.count[G_ALPHA] : maxa; maxs = scene.count[G_SPRITE] > maxs ? scene.count[G_SPRITE] : maxs;
			for (int g = 0; g < G_COUNT; g++)
				for (int i = 0; i < scene.count[g]; i++) {
					const Vtx *v = &scene.buf[scene.first[g] + i];
					if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) || !isfinite(v->u) || !isfinite(v->v) || fabsf(v->x) > 5000 || fabsf(v->z) > 5000 || v->y < -5 || v->y > 400 || v->u < 0 || v->u > 1 || v->v < 0 || v->v > 1) { if (bad++ < 5) printf("bad vertex group %d #%d: %f %f %f uv %f %f\n", g, i, v->x, v->y, v->z, v->u, v->v); }
				}
		}
	}
	printf("stress: max vertices opaque %d/%d  alpha %d/%d  sprite %d/%d, %d bad\n", maxo, CAP_OPAQUE, maxa, CAP_ALPHA, maxs, CAP_SPRITE, bad);
	free(scene.buf);
	return bad || maxo >= CAP_OPAQUE || maxa >= CAP_ALPHA || maxs >= CAP_SPRITE;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".", *out = argc > 2 ? argv[2] : dir;
	char p[600];
	snprintf(p, sizeof p, "%s/map.bin", dir); FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "no map.bin in %s\n", dir); return 1; }
	size_t n = fread(mapdata, 1, sizeof mapdata, f); fclose(f);
	snprintf(p, sizeof p, "%s/world.rgba", dir); tex_world = load_rgba(p, &ww, &wh);
	snprintf(p, sizeof p, "%s/sprites.rgba", dir); tex_sprites = load_rgba(p, &sw_, &sh_);
	if (argc > 3 && !strcmp(argv[3], "stress")) return stress(n);
	Input none; memset(&none, 0, sizeof none);

	fresh(n); place(92, 57, 3.14159f); run(15, none); snap(out, "s3_street");
	fresh(n); place(120, 14, 0.0f); run(3, none); snap(out, "s3_obs");
	fresh(n); place(90, 100, 1.5708f);
	{ int ci = car_spawn(&G, 4, 90 * 16 + 40, 100 * 16 + 48, 1.5708f, CS_PARKED, DRV_NONE);
	  G.p.x = G.cars[ci].x - 20 + 8; G.p.y = G.cars[ci].y - 12;
	  Input in = none; in.y_p = true; run(0.1f, in); in = none; in.a = true; run(2.0f, in); }
	snap(out, "s3_drive");
	fresh(n); place(90, 73, 0.0f);
	{ car_spawn(&G, NCARVARS - 1, G.p.x, G.p.y - 16, 0.0f, CS_PARKED, DRV_NONE);
	  Input in = none; in.y_p = true; run(0.1f, in); in = none; in.a = true; run(2.4f, in); }
	snap(out, "s3_heli");
	fresh(n); place(92, 57, 1.5708f); G.p.has[W_RIFLE] = true; G.p.ammo[W_RIFLE] = 300; G.p.weapon = W_RIFLE;
	{ ped_spawn(&G, PK_CIV, 2, G.p.x + 60, G.p.y + 2); Input in = none; in.a = true; run(0.6f, in); }
	snap(out, "s3_shoot");
	fresh(n); place(100, 92, 3.14159f); add_heat(&G, 12.0f);
	{ Input in = none; in.mx = 1; in.run = true; run(9, in); }
	snap(out, "s3_chase");
	fresh(n); place(22, 70, 1.0f); snap(out, "s3_beach");
	fresh(n); place(86, 8, 0.0f); snap(out, "s3_sign");
	fresh(n); place(115, 94, 0.0f); snap(out, "s3_downtown");
	fresh(n); place(115, 94, 0.0f); G.time = 300; run(8, none); snap(out, "s3_night");
	fresh(n); place(92, 57, 3.14159f); G.time = 168; run(1, none); snap(out, "s3_dusk");
	fresh(n); place(24, 62, 1.0f); G.time = 5; snap(out, "s3_ferris");
	return 0;
}
