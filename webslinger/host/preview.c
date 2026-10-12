// Renders frames of the 3D view on the PC: the same triangles scene.c builds for the 3DS, drawn by a small software rasteriser
// (perspective-correct textures with mip selection, z-buffer, alpha/additive blending, near clipping).
// Usage: preview <dir with <texture>.rgba files> <out dir>
#define HOST_PREVIEW 1
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/scene.c"
#include "../source/hud.c"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static Game G;
static Scene S;
static Vtx *stat_buf, *dyn_buf;

static const char *texname[MAT_COUNT] = { "white", "road", "glass", "cream", "red", "tan", "copper", "roof", "grass", "water", "hero", "glow", "cloud", "store" };
typedef struct { int w, h; unsigned char *px; } Level;
typedef struct { Level lv[9]; int n; } Tex;
static Tex tex[MAT_COUNT];

static void load_tex(const char *dir, int i)
{
	char p[600]; snprintf(p, sizeof p, "%s/%s.rgba", dir, texname[i]);
	FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "missing %s\n", p); exit(1); }
	int hdr[2]; if (fread(hdr, 4, 2, f) != 2) exit(1);
	Level *l = &tex[i].lv[0]; l->w = hdr[0]; l->h = hdr[1];
	l->px = malloc((size_t)l->w * l->h * 4);
	if (fread(l->px, 4, (size_t)l->w * l->h, f) != (size_t)l->w * l->h) exit(1);
	fclose(f);
	tex[i].n = 1;
	while (tex[i].lv[tex[i].n - 1].w > 1 && tex[i].n < 9) {
		Level *s = &tex[i].lv[tex[i].n - 1], *d = &tex[i].lv[tex[i].n];
		d->w = s->w / 2; d->h = s->h / 2; d->px = malloc((size_t)d->w * d->h * 4);
		for (int y = 0; y < d->h; y++) for (int x = 0; x < d->w; x++) for (int c = 0; c < 4; c++)
			d->px[((size_t)y * d->w + x) * 4 + c] = (s->px[((size_t)(2 * y) * s->w + 2 * x) * 4 + c] + s->px[((size_t)(2 * y) * s->w + 2 * x + 1) * 4 + c] + s->px[((size_t)(2 * y + 1) * s->w + 2 * x) * 4 + c] + s->px[((size_t)(2 * y + 1) * s->w + 2 * x + 1) * 4 + c]) / 4;
		tex[i].n++;
	}
}

#define FW 800
#define FH 480
static float zbuf[FW * FH];
static float fb[FW * FH * 3];

static void sample(int mat, float lod, bool clampuv, float u, float v, float out[4])
{
	const Tex *t = &tex[mat];
	int li = (int)(lod + 0.5f); if (li < 0) li = 0; if (li >= t->n) li = t->n - 1;
	const Level *l = &t->lv[li];
	float x = u * l->w - 0.5f, y = (1.0f - v) * l->h - 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	for (int c = 0; c < 4; c++) out[c] = 0;
	for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
		int xx = x0 + i, yy = y0 + j;
		if (clampuv) { xx = xx < 0 ? 0 : xx >= l->w ? l->w - 1 : xx; yy = yy < 0 ? 0 : yy >= l->h ? l->h - 1 : yy; }
		else { xx = ((xx % l->w) + l->w) % l->w; yy = ((yy % l->h) + l->h) % l->h; }
		float wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
		const unsigned char *p = l->px + ((size_t)yy * l->w + xx) * 4;
		for (int c = 0; c < 4; c++) out[c] += p[c] * wgt / 255.0f;
	}
}

typedef struct { float x, y, z, w, u, v, r, g, b, a; } PV;      // clip-space vertex

typedef struct { const float *view; float f, asp, n, fa; } RCam;
static const float HAZE[3] = { 0.80f, 0.88f, 0.97f };
#define FOG_START 60.0f
#define FOG_END 900.0f
static bool g_fog;

static PV project(const RCam *c, const Vtx *v)
{
	const float *m = c->view;
	float x = m[0] * v->x + m[1] * v->y + m[2] * v->z + m[3];
	float y = m[4] * v->x + m[5] * v->y + m[6] * v->z + m[7];
	float z = m[8] * v->x + m[9] * v->y + m[10] * v->z + m[11];
	PV o;
	o.x = x * c->f / c->asp; o.y = y * c->f; o.z = (-(c->fa + c->n) * z - 2 * c->fa * c->n) / (c->fa - c->n); o.w = -z;
	o.u = v->u; o.v = v->v; o.r = v->r; o.g = v->g; o.b = v->b; o.a = v->a;
	if (g_fog) { float f = (o.w - FOG_START) / (FOG_END - FOG_START); f = f < 0 ? 0 : f > 1 ? 1 : f; o.a = f * f; }       // what the vertex shader writes into alpha
	return o;
}
static PV lerp_pv(const PV *a, const PV *b, float t)
{
	PV o; const float *pa = &a->x, *pb = &b->x; float *po = &o.x;
	for (int i = 0; i < 10; i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
	return o;
}

enum { BM_OPAQUE, BM_ALPHA, BM_ADD };

static void raster_tri(const PV *a, const PV *b, const PV *c, int mat, int bm, bool depth_test, bool depth_write, bool clampuv, bool alphatest)
{
	const PV *p[3] = { a, b, c };
	float sx[3], sy[3], iw[3];
	for (int i = 0; i < 3; i++) { iw[i] = 1.0f / p[i]->w; sx[i] = (p[i]->x * iw[i] * 0.5f + 0.5f) * FW; sy[i] = (1.0f - (p[i]->y * iw[i] * 0.5f + 0.5f)) * FH; }
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
	if (fabsf(area) < 1e-6f) return;
	// mip level from the ratio of texel area to pixel area
	float uvarea = fabsf((p[1]->u - p[0]->u) * (p[2]->v - p[0]->v) - (p[2]->u - p[0]->u) * (p[1]->v - p[0]->v));
	const Level *l0 = &tex[mat].lv[0];
	float texels = uvarea * l0->w * l0->h, pixels = fabsf(area);
	float lod = (texels > 0 && pixels > 0) ? 0.5f * log2f(texels / pixels) : 0;
	if (lod < 0) lod = 0;
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
			float pw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];      // 1/w, larger = nearer
			float *zb = &zbuf[y * FW + x];
			if (depth_test && pw <= *zb) continue;
			float u = (w0 * p[0]->u * iw[0] + w1 * p[1]->u * iw[1] + w2 * p[2]->u * iw[2]) / pw;
			float v = (w0 * p[0]->v * iw[0] + w1 * p[1]->v * iw[1] + w2 * p[2]->v * iw[2]) / pw;
			float col[4], t[4];
			sample(mat, lod, clampuv, u, v, t);
			col[0] = t[0] * (w0 * p[0]->r + w1 * p[1]->r + w2 * p[2]->r); col[1] = t[1] * (w0 * p[0]->g + w1 * p[1]->g + w2 * p[2]->g);
			col[2] = t[2] * (w0 * p[0]->b + w1 * p[1]->b + w2 * p[2]->b); col[3] = t[3] * (w0 * p[0]->a + w1 * p[1]->a + w2 * p[2]->a);
			if (alphatest && col[3] < 0.5f) continue;
			if (g_fog) { float f = w0 * p[0]->a + w1 * p[1]->a + w2 * p[2]->a; for (int k = 0; k < 3; k++) col[k] = HAZE[k] * f + col[k] / (t[3] > 0.001f ? 1 : 1) * (1 - f); col[3] = 1; }
			float *o = &fb[(y * FW + x) * 3];
			if (bm == BM_OPAQUE) { for (int k = 0; k < 3; k++) o[k] = col[k]; }
			else if (bm == BM_ALPHA) { for (int k = 0; k < 3; k++) o[k] = col[k] * col[3] + o[k] * (1 - col[3]); }
			else { for (int k = 0; k < 3; k++) o[k] += col[k] * col[3]; }
			if (depth_write) *zb = pw;
		}
}

// clip a triangle against the near plane (w > near) and rasterise the result
static void draw_tri(const RCam *cam, const Vtx *v0, const Vtx *v1, const Vtx *v2, int mat, int bm, bool dt, bool dw, bool clampuv, bool at)
{
	PV in[3] = { project(cam, v0), project(cam, v1), project(cam, v2) };
	PV out[4]; int n = 0;
	const float nearw = cam->n;
	for (int i = 0; i < 3; i++) {
		const PV *a = &in[i], *b = &in[(i + 1) % 3];
		bool ina = a->w >= nearw, inb = b->w >= nearw;
		if (ina) out[n++] = *a;
		if (ina != inb) { float t = (nearw - a->w) / (b->w - a->w); out[n++] = lerp_pv(a, b, t); }
	}
	if (n < 3) return;
	for (int i = 1; i + 1 < n; i++) raster_tri(&out[0], &out[i], &out[i + 1], mat, bm, dt, dw, clampuv, at);
}

static void draw_range(const RCam *cam, const Vtx *buf, int first, int count, int mat, int bm, bool dt, bool dw, bool clampuv)
{
	for (int i = 0; i + 2 < count; i += 3) draw_tri(cam, &buf[first + i], &buf[first + i + 1], &buf[first + i + 2], mat, bm, dt, dw, clampuv, false);
}

static void draw_scene(const Scene *s)
{
	for (int i = 0; i < FW * FH; i++) { zbuf[i] = 0; fb[i * 3] = HAZE[0]; fb[i * 3 + 1] = HAZE[1]; fb[i * 3 + 2] = HAZE[2]; }
	float asp = (float)FW / FH;
	RCam sky = { s->skyview, 1.0f / tanf(s->fov * 0.5f * PI_F / 180.0f), asp, 8.0f, 1500.0f };
	RCam wc = { s->view, sky.f, asp, 0.25f, 1500.0f };
	draw_range(&sky, s->stat, s->sfirst[GRP_SKY], s->scount[GRP_SKY], MAT_WHITE, BM_OPAQUE, false, false, true);
	draw_range(&sky, s->stat, s->sfirst[GRP_CLOUD], s->scount[GRP_CLOUD], MAT_CLOUD, BM_ALPHA, false, false, true);
	draw_range(&sky, s->stat, s->sfirst[GRP_SUN], s->scount[GRP_SUN], MAT_GLOW, BM_ADD, false, false, true);
	{ long vt = 0; for (int m = 0; m < MAT_COUNT; m++) for (int c = 0; c <= NCH; c++) if (s->vis[c]) vt += s->ccount[m][c]; for (int c = 0; c < NCH; c++) if (s->near[c]) vt += s->ccount[GRP_DETAIL][c]; if (getenv("VISTRIS")) fprintf(stderr, "visible static triangles %ld\n", vt / 3); }
	g_fog = true;
	for (int m = 0; m < MAT_COUNT; m++) if (m != MAT_GLOW && m != MAT_CLOUD && m != MAT_HERO) for (int c = 0; c <= NCH; c++) if (s->vis[c] && s->ccount[m][c]) draw_range(&wc, s->stat, s->cfirst[m][c], s->ccount[m][c], m, BM_OPAQUE, true, true, m == MAT_WHITE);
	for (int c = 0; c < NCH; c++) if (s->near[c] && s->ccount[GRP_DETAIL][c]) draw_range(&wc, s->stat, s->cfirst[GRP_DETAIL][c], s->ccount[GRP_DETAIL][c], MAT_WHITE, BM_OPAQUE, true, true, true);
	draw_range(&wc, s->dyn, s->first[D_OPAQUE], s->count[D_OPAQUE], MAT_WHITE, BM_OPAQUE, true, true, true);
	draw_range(&wc, s->dyn, s->first[D_HERO], s->count[D_HERO], MAT_HERO, BM_OPAQUE, true, true, true);
	g_fog = false;
	for (int c = 0; c <= NCH; c++) if (s->vis[c] && s->ccount[GRP_SHADOW][c]) draw_range(&wc, s->stat, s->cfirst[GRP_SHADOW][c], s->ccount[GRP_SHADOW][c], MAT_WHITE, BM_ALPHA, true, false, true);
	draw_range(&wc, s->dyn, s->first[D_ALPHA], s->count[D_ALPHA], MAT_GLOW, BM_ALPHA, true, false, true);
	draw_range(&wc, s->dyn, s->first[D_ADD], s->count[D_ADD], MAT_GLOW, BM_ADD, true, false, true);
}


static void hud_capture(const char *dir, const char *name)
{
	shim_begin(400, 240, 0);
	for (int y = 0; y < 240; y++) for (int x = 0; x < 400; x++) for (int c = 0; c < 3; c++) {
		float v = (fb[((2 * y) * FW + 2 * x) * 3 + c] + fb[((2 * y) * FW + 2 * x + 1) * 3 + c] + fb[((2 * y + 1) * FW + 2 * x) * 3 + c] + fb[((2 * y + 1) * FW + 2 * x + 1) * 3 + c]) * 0.25f;
		shim_fb[y][x][c] = (v < 0 ? 0 : v > 1 ? 1 : v) * 255.0f;
	}
	char path[600]; snprintf(path, sizeof path, "%s/%s_hud.txt", dir, name); shim_textlog = fopen(path, "w");
	hud_frame_begin(); hud_top(&G, &S);
	fclose(shim_textlog); shim_textlog = NULL;
	snprintf(path, sizeof path, "%s/%s_hud.ppm", dir, name); shim_write_ppm(path);
	shim_begin(320, 240, C2D_Color32(14, 18, 24, 255));
	snprintf(path, sizeof path, "%s/%s_bot.txt", dir, name); shim_textlog = fopen(path, "w");
	hud_bottom(&G);
	fclose(shim_textlog); shim_textlog = NULL;
	snprintf(path, sizeof path, "%s/%s_bot.ppm", dir, name); shim_write_ppm(path);
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
	scene_build(&G, &S);
	draw_scene(&S);
	printf("%-12s opaque %5d  alpha %4d  add %4d\n", name, S.count[D_OPAQUE], S.count[D_ALPHA], S.count[D_ADD]);
	save(dir, name);
	hud_capture(dir, name);
}

static Input idle;
static void run(float seconds, Input in)
{
	for (float t = 0; t < seconds; t += 1.0f / 30.0f) { game_update(&G, &in, 1.0f / 30.0f); if (S.dyn) scene_build(&G, &S); in.wing_p = in.jump_p = in.zip_p = in.start_p = in.a_p = in.b_p = in.up_p = in.down_p = in.select_p = in.recenter_p = false; }
}
static void place(float x, float y, float z, float yaw)
{
	Player *p = &G.p; memset(p, 0, sizeof *p);
	p->p[0] = x; p->p[1] = y; p->p[2] = z; p->state = y > 0.5f ? ST_AIR : ST_GROUND; p->face = yaw; G.cam.yaw = yaw; G.cam.pitch = -0.2f; G.cam.dist = 9.0f; G.cam.fov = 62;
}
static float aer[6]; static int aer_on;                      // optional free camera: x, y, z, yaw, pitch, fov
static void snapfree(const char *dir, const char *name)
{
	G.cam.eye[0] = aer[0]; G.cam.eye[1] = aer[1]; G.cam.eye[2] = aer[2]; G.cam.yaw = aer[3]; G.cam.pitch = aer[4]; G.cam.fov = aer[5];
	scene_build(&G, &S); draw_scene(&S);
	printf("%-12s opaque %5d  alpha %4d  add %4d\n", name, S.count[D_OPAQUE], S.count[D_ALPHA], S.count[D_ADD]);
	save(dir, name); hud_capture(dir, name);
}

static void hero_close(const char *dir, const char *name, float off)       // off: 0 = from the front, PI/2 = from the side ...
{
	float T[3] = { G.p.p[0], G.p.p[1] + 1.0f, G.p.p[2] }, best[3] = { T[0], T[1] + 4, T[2] };
	for (int k = 0; k < 8; k++) {
		float a = G.p.face + off + k * PI_F / 4, d[3] = { sinf(a), 0.15f, -cosf(a) }, n[3];
		float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); for (int q = 0; q < 3; q++) d[q] /= l;
		if (city_ray(&G.w, T, d, 5.0f, n) < 0) { for (int q = 0; q < 3; q++) best[q] = T[q] + d[q] * 4.2f; break; }
	}
	float dx = T[0] - best[0], dy = T[1] - best[1], dz = T[2] - best[2];
	G.cam.eye[0] = best[0]; G.cam.eye[1] = best[1]; G.cam.eye[2] = best[2]; G.cam.yaw = atan2f(dx, -dz); G.cam.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)); G.cam.fov = 40;
	scene_build(&G, &S); draw_scene(&S);
	save(dir, name); hud_capture(dir, name);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".", *out = argc > 2 ? argv[2] : dir;
	for (int i = 0; i < MAT_COUNT; i++) load_tex(dir, i);
	game_init(&G, false);
	int total = scene_build_static(&G, &S, NULL);
	stat_buf = malloc(sizeof(Vtx) * total); dyn_buf = malloc(sizeof(Vtx) * CAP_DYN);
	scene_build_static(&G, &S, stat_buf);
	S.dyn = dyn_buf;
	printf("boxes %d, static vertices %d (%d triangles, %.1f MB)\n", G.w.nbox, total, total / 3, total * 36 / 1048576.0);
	for (int i = 0; i < GRP_COUNT; i++) { int t = 0; for (int c = 0; c <= NCH; c++) t += S.ccount[i][c]; if (t) printf("  group %2d: %6d\n", i, t); }
	memset(&idle, 0, sizeof idle);

	// title screen (the bot swings in the background)
	run(6, idle); snap(out, "t1_title");
	G.showHelp = false;
	// 1: on the street at the start
	game_start(&G, MODE_RUN); run(0.5f, idle); snap(out, "p1_start");
	// 2: the autopilot swinging (a real run): grab frames mid-swing, a few seconds apart
	{
		game_start(&G, MODE_RUN); G.rng ^= 0x77u; int shots = 0; float last = -9;
		for (float t = 0; t < 120 && shots < 8; t += 1.0f / 30) {
			Input in; game_bot(&G, &in, 1.0f / 30, G.bc.p); game_update(&G, &in, 1.0f / 30);
			if (G.p.rope && G.p.swingT > 0.9f && G.p.p[1] > 30 && t - last > 5) { char nm[32]; snprintf(nm, sizeof nm, "s%d_swing", ++shots); snap(out, nm); last = t; }
		}
	}
	// hero close-ups from the side: swinging, running, jumping, climbing
	{
		game_start(&G, MODE_RUN); G.rng ^= 0x77u; Input in = idle; bool got = false;
		for (float t = 0; t < 60 && !got; t += 1.0f / 30) { Input bi; game_bot(&G, &bi, 1.0f / 30, G.bc.p); game_update(&G, &bi, 1.0f / 30); if (G.p.rope && G.p.swingT > 1.2f && G.p.p[1] > 40) got = true; }
		hero_close(out, "h1_swing", PI_F / 2); hero_close(out, "h1b_swing", 0);
		game_start(&G, MODE_RUN); Input rn = idle; rn.ly = 1; rn.sprint = true; run(1.0f, rn); hero_close(out, "h2_run", PI_F / 2); hero_close(out, "h2b_run", 0); run(0.0f, idle);
		Input st = idle; run(0.5f, st); G.p.v[0] = G.p.v[2] = 0; hero_close(out, "h3_idle", 0.4f);
		game_start(&G, MODE_RUN); in = idle; in.jump_p = true; run(0.45f, in); hero_close(out, "h4_jump", PI_F / 2);
	}
	// tricks and the jump crouch
	{
		static const int types[3] = { 1, 3, 2 }; static const float when[3] = { 0.15f, 0.4f, 0.65f };
		for (int k = 0; k < 4; k++) {
			game_start(&G, MODE_FREE); memset(&G.p, 0, sizeof G.p); G.p.p[0] = 4 * PITCH; G.p.p[1] = 200; G.p.p[2] = 3 * PITCH; G.p.state = ST_AIR; G.p.face = DEG(90); G.cam.yaw = DEG(90);
			G.p.v[0] = 12; G.p.trickType = types[k % 3]; if (k == 3) G.p.trickType = 4; G.p.trickT = TRICK_DUR * (1 - when[k % 3]); G.p.trickCount = 1;
			Input in = idle; run(0.02f, in); char nm[32]; snprintf(nm, sizeof nm, "k%d_trick", k); hero_close(out, nm, PI_F / 2);
		}
		game_start(&G, MODE_FREE); G.p.charge = 0.9f; run(0.0f, idle); hero_close(out, "k5_crouch", PI_F / 2);
	}
	// filmstrips: the run cycle and a swing, side view, every 0.07 s (the animation springs are stepped every frame)
	{
		game_start(&G, MODE_FREE); Input rn = idle; rn.ly = 1; rn.sprint = true; G.hintT = 0; run(1.5f, rn);
		for (int k = 0; k < 8; k++) { run(0.07f, rn); char nm[32]; snprintf(nm, sizeof nm, "f%d_run", k); hero_close(out, nm, PI_F / 2); }
		game_start(&G, MODE_RUN); G.rng ^= 0x77u; int shot = 0;
		for (float t = 0; t < 60 && shot < 8; t += 1.0f / 30) {
			Input bi; game_bot(&G, &bi, 1.0f / 30, G.bc.p); game_update(&G, &bi, 1.0f / 30); scene_build(&G, &S);
			if (G.p.rope && G.p.swingT > 1.5f && G.p.p[1] > 30 && (shot > 0 || G.p.swingT > 1.6f)) { if (shot == 0 || t - 1e9 < 0) {} char nm[32]; snprintf(nm, sizeof nm, "f%d_swing", shot++); hero_close(out, nm, PI_F / 2); for (int q = 0; q < 2; q++) { game_bot(&G, &bi, 1.0f / 30, G.bc.p); game_update(&G, &bi, 1.0f / 30); scene_build(&G, &S); } }
		}
	}
	// wingsuit
	{
		game_start(&G, MODE_FREE); memset(&G.p, 0, sizeof G.p); G.p.p[0] = MID * PITCH; G.p.p[1] = 160; G.p.p[2] = (MID - 2) * PITCH; G.p.state = ST_AIR; G.p.face = DEG(90); G.cam.yaw = DEG(90);
		G.p.v[0] = 30; G.p.airT = 1; Input in = idle; in.wing_p = true; run(0.05f, in); in.wing_p = false; in.lx = 0.3f; run(1.5f, in);
		hero_close(out, "w2_wing_side", PI_F / 2); hero_close(out, "w3_wing_front", PI_F); hero_close(out, "w4_wing_above", 0.6f);
		printf("wing: speed %.1f vy %.1f wing %d\n", len3(G.p.v), G.p.v[1], G.p.wing);
	}
	// running on a wall
	{
		const Box *b = &G.w.box[G.w.bfirst[2][2]];
		game_start(&G, MODE_FREE); memset(&G.p, 0, sizeof G.p); G.p.p[0] = 0.5f * (b->x0 + b->x1); G.p.p[1] = 12; G.p.p[2] = b->z0 - BODY_HW - 0.01f; G.p.state = ST_WALL; G.p.wn[0] = 0; G.p.wn[1] = -1; G.p.face = 0; G.cam.yaw = DEG(90);
		Input in = idle; in.sprint = true; in.ly = 1; run(0.5f, in);
		for (int k = 0; k < 6; k++) { G.p.p[1] = 12; run(0.07f, in); G.p.p[1] = 12; char nm[32]; snprintf(nm, sizeof nm, "wr%d_wallrun", k); hero_close(out, nm, PI_F / 2); }
		printf("wallrun: state %d y %.1f\n", G.p.state, G.p.p[1]);
	}
	// landing roll filmstrip
	{
		game_start(&G, MODE_FREE); memset(&G.p, 0, sizeof G.p); G.p.p[0] = 4 * PITCH + 4; G.p.p[1] = 1.5f; G.p.p[2] = 3 * PITCH + 4; G.p.state = ST_AIR; G.p.face = DEG(90); G.cam.yaw = DEG(90);
		G.p.v[0] = 14; G.p.v[1] = -10; G.p.airT = 1.5f; Input in = idle; in.lx = 1;
		for (int k = 0; k < 10; k++) { run(k ? 0.07f : 0.1f, in); char nm[32]; snprintf(nm, sizeof nm, "r%d_roll", k); hero_close(out, nm, PI_F / 2); }
	}
	// 3: a wall climb
	{
		float wx = 4 * PITCH + 0.2f * PITCH - BODY_HW - 0.01f; place(wx - 0.0f, 20, 4.5f * PITCH + 20, DEG(90)); G.p.state = ST_AIR; G.p.v[0] = 6;
		run(0.4f, idle); Input in = idle; in.ly = 1; run(0.3f, in); snap(out, "w1_wall"); G.hintT = 0;
	}
	// 4: aerial views of the whole city
	aer_on = 1;
	aer[0] = 300; aer[1] = 150; aer[2] = -140; aer[3] = PI_F; aer[4] = -0.3f; aer[5] = 70; place(MID * PITCH, 0, (MID - 1) * PITCH, 0); snapfree(out, "a1_overview");
	aer[0] = 250; aer[1] = 30; aer[2] = 190; aer[3] = DEG(35); aer[4] = 0.05f; aer[5] = 66; snapfree(out, "a2_street");
	aer[0] = 90; aer[1] = 250; aer[2] = 90; aer[3] = DEG(45); aer[4] = -0.8f; aer[5] = 66; snapfree(out, "a3_down");
	aer[0] = 4.5f * PITCH; aer[1] = 3.2f; aer[2] = 2 * PITCH + 4; aer[3] = DEG(25); aer[4] = 0.08f; aer[5] = 70; snapfree(out, "a4_ground");
	aer[0] = 2.2f * PITCH; aer[1] = 2.5f; aer[2] = 3 * PITCH + 11; aer[3] = DEG(80); aer[4] = 0.25f; aer[5] = 70; snapfree(out, "a5_ground");
	aer[0] = 3.2f * PITCH; aer[1] = 70; aer[2] = 2.2f * PITCH; aer[3] = DEG(30); aer[4] = -0.45f; aer[5] = 70; snapfree(out, "a6_roofs");
	aer_on = 0;
	// paused / over screens
	game_start(&G, MODE_RUN); run(0.2f, idle); G.score = 2300; G.chain = 3; G.chainT = 5; game_popup(&G, "+300  x3  +12s", 0xFF40E0FF); G.paused = true; snap(out, "u1_pause"); G.paused = false;
	G.state = GS_OVER; G.stateT = 2; G.best = 2300; snap(out, "u2_over");
	return 0;
}
