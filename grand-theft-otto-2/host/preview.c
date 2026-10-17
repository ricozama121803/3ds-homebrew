// Renders frames of the 3D view on the PC: the same triangles scene.c builds for the 3DS, drawn by a small software rasteriser
// (perspective-correct textures with mip selection, z-buffer, alpha/additive blending, near clipping).
// Usage: preview <dir with <texture>.rgba files> <out dir>
#define HOST_PREVIEW 1
#include "../source/game.h"
#include "../source/city.c"
#include "../source/game.c"
#include "../source/traffic.c"
#include "../source/combat.c"
#include "../source/police.c"
#include "../source/scene.c"
#include "../source/hud.c"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static Game G;
static Scene S;
static Vtx *stat_buf, *dyn_buf;

static const char *texname[MAT_COUNT] = { "white", "road", "glass", "cream", "red", "tan", "copper", "roof", "grass", "water", "glow", "cloud", "store", "sand", "plank" };
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
#define FOG_END 1300.0f
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
	for (int m = 0; m < MAT_COUNT; m++) if (m != MAT_GLOW && m != MAT_CLOUD) for (int c = 0; c <= NCH; c++) if (s->vis[c] && s->ccount[m][c]) draw_range(&wc, s->stat, s->cfirst[m][c], s->ccount[m][c], m, BM_OPAQUE, true, true, m == MAT_WHITE);
	for (int c = 0; c < NCH; c++) if (s->near[c] && s->ccount[GRP_DETAIL][c]) draw_range(&wc, s->stat, s->cfirst[GRP_DETAIL][c], s->ccount[GRP_DETAIL][c], MAT_WHITE, BM_OPAQUE, true, true, true);
	draw_range(&wc, s->dyn, s->first[D_OPAQUE], s->count[D_OPAQUE], MAT_WHITE, BM_OPAQUE, true, true, true);
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
	for (float t = 0; t < seconds - 0.000001f;) {
		float dt = fminf(1.0f / 60.0f, seconds - t); t += dt;
		game_update(&G, &in, dt);
		in.enter_p = in.start_p = in.select_p = in.up_p = in.down_p = false;
	}
}
static void snapfree(const char *dir, const char *name, float x, float y, float z, float yaw, float pitch, float fov)
{
	G.cam.eye[0] = x; G.cam.eye[1] = y; G.cam.eye[2] = z; G.cam.yaw = yaw; G.cam.pitch = pitch; G.cam.fov = fov;
	scene_build(&G, &S); draw_scene(&S);
	printf("%-14s opaque %5d  alpha %4d\n", name, S.count[D_OPAQUE], S.count[D_ALPHA]);
	save(dir, name); hud_capture(dir, name);
}
// look at a point from an angle (a = bearing the camera sits at, relative to north) and distance
static void snaplook(const char *dir, const char *name, const float t[3], float bearing, float dist, float height, float fov)
{
	float ex = t[0] + sinf(bearing) * dist, ez = t[2] - cosf(bearing) * dist, ey = t[1] + height;
	float dx = t[0] - ex, dy = t[1] - ey, dz = t[2] - ez;
	snapfree(dir, name, ex, ey, ez, atan2f(dx, -dz), atan2f(dy, sqrtf(dx * dx + dz * dz)), fov);
}
static void drive_to_car(int i)                                  // put Otto next to car i and get in
{
	Car *c = &G.car[i];
	G.p.p[0] = c->x + cosf(c->yaw) * 2.6f; G.p.p[2] = c->z + sinf(c->yaw) * 2.6f; G.p.car = -1;
	Input in = idle; in.enter_p = true; run(0.05f, in);
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

	// pedestrians: close-ups of a few and a street view among them
	{
		run(25.0f, idle); int shown = 0;
		for (int i = 0; i < MAX_PED && shown < 4; i++) if (G.ped[i].on && G.ped[i].state == PED_WALK) {
			char nm[32]; snprintf(nm, sizeof nm, "pd%d_ped", shown++);
			snaplook(out, nm, (float[3]){ G.ped[i].x, 1.0f, G.ped[i].z }, G.ped[i].yaw + 0.9f, 3.4f, 0.2f, 42);
		}
		float best = 1e9f; int bi = -1; for (int i = 0; i < MAX_PED; i++) if (G.ped[i].on) { float d = hypotf(G.ped[i].x - G.p.p[0], G.ped[i].z - G.p.p[2]); if (d < best) { best = d; bi = i; } }
		if (bi >= 0) { G.p.p[0] = G.ped[bi].x + 8; G.p.p[2] = G.ped[bi].z + 8; }
		run(0.2f, idle); snap(out, "pd9_street");
		game_start(&G); G.hintT = 0;
	}
	// 1: at the start
	run(1.0f, idle); snap(out, "p1_start");
	// Otto walking, seen from the side (filmstrip) and from behind
	{
		Input w = idle; w.ly = 1; w.run = true; G.hintT = 0;
		run(1.2f, w);
		for (int k = 0; k < 4; k++) { run(0.08f, w); char nm[32]; snprintf(nm, sizeof nm, "o%d_run", k); snaplook(out, nm, (float[3]){ G.p.p[0], G.p.p[1] + 1.0f, G.p.p[2] }, G.p.face + PI_F / 2, 3.6f, 0.2f, 40); }
		snap(out, "p2_run");
		Input s = idle; s.ly = 0.35f; run(0.8f, s); snaplook(out, "o4_walk", (float[3]){ G.p.p[0], G.p.p[1] + 1.0f, G.p.p[2] }, G.p.face + PI_F / 2, 3.6f, 0.2f, 40);
		run(1.0f, idle); snaplook(out, "o5_front", (float[3]){ G.p.p[0], G.p.p[1] + 1.0f, G.p.p[2] }, G.p.face + PI_F, 3.2f, 0.3f, 40);
	}
	// the cars, one of each kind, 3/4 view
	{
		int shown = 0; bool seen[CAR_KINDS] = { 0 };
		for (int i = 0; i < MAX_CAR && shown < CAR_KINDS; i++) if (G.car[i].on && !seen[G.car[i].kind]) {
			seen[G.car[i].kind] = true; shown++;
			char nm[32]; snprintf(nm, sizeof nm, "c%d_kind", G.car[i].kind);
			snaplook(out, nm, (float[3]){ G.car[i].x, 0.8f, G.car[i].z }, G.car[i].yaw + 0.9f, 7.5f, 1.8f, 45);
		}
	}
	// get into the first car and drive
	game_start(&G); G.hintT = 0;
	{
		Input w = idle; w.lx = 0.0f; w.ly = 1; run(0.8f, w);
		int near = game_nearest_car(&G, G.p.p[0], G.p.p[2], 12.0f); printf("nearest car %d\n", near);
		snap(out, "d0_prompt");
		drive_to_car(0); printf("in car %d\n", G.p.car);
		Input d = idle; d.gas = true; run(2.0f, d); snap(out, "d1_accel");
		d.lx = -0.6f; run(1.0f, d); snap(out, "d2_turn");
		d.lx = 0.8f; d.hand = true; run(0.7f, d); snap(out, "d3_drift");
		d.hand = false; d.lx = 0; d.gas = false; d.brake = true; run(2.0f, d); snap(out, "d4_brake");
		printf("speed after braking %.1f\n", G.p.speed);
		Input e = idle; e.enter_p = true; run(0.1f, e); printf("after exit: car %d\n", G.p.car); run(0.5f, idle); snap(out, "d5_exited");
	}
	// combat: guns in hand, a shootout, the police, an explosion, WASTED, pickups
	{
		const float SX = 4 * PITCH, SZ = 4 * PITCH + 24;
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
		G.hintT = 0; G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0; G.p.has[W_SMG] = G.p.has[W_SHOTGUN] = true; G.p.ammo[W_SMG] = 60; G.p.ammo[W_SHOTGUN] = 16;
		for (int w = W_PISTOL; w <= W_SHOTGUN; w++) {
			G.p.weapon = w; G.p.aimT = 1; G.p.lastShotT = G.time; G.p.flashT = w == W_SMG ? 0.05f : 0;
			char nm[32]; snprintf(nm, sizeof nm, "w%d_aim", w); run(0.05f, idle); G.p.aimT = 1;
			snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F / 2, 3.4f, 0.25f, 42);
			snprintf(nm, sizeof nm, "w%d_aim3", w); snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F * 0.75f, 3.4f, 0.3f, 42);
			snprintf(nm, sizeof nm, "w%d_close", w); snaplook(out, nm, (float[3]){ G.p.p[0] + sinf(G.p.face) * 0.5f, 1.3f, G.p.p[2] - cosf(G.p.face) * 0.5f }, G.p.face + PI_F * 0.62f, 1.9f, 0.15f, 40);
			snprintf(nm, sizeof nm, "w%d_aimf", w); snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F, 3.4f, 0.2f, 42);
		}
		{                                                                          // in cover behind a wall
			game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
			G.p.p[0] = SX + 12.8f - 1.6f; G.p.p[2] = 4 * PITCH + 34; G.p.face = 0; G.cam.yaw = 0;
			Input cv = idle; cv.cover = true; run(0.8f, cv);
			snap(out, "cv3_cover_game");
			cv.gas = true; run(0.5f, cv); snap(out, "cv4_cover_pop"); cv.gas = false; run(0.6f, cv);
			snaplook(out, "cv1_cover_side", (float[3]){ G.p.p[0], 0.7f, G.p.p[2] }, G.p.face - PI_F / 2, 3.2f, 0.3f, 42);
			snaplook(out, "cv2_cover_front", (float[3]){ G.p.p[0], 0.7f, G.p.p[2] }, G.p.face + PI_F * 0.8f, 3.2f, 0.3f, 42);
			game_start(&G); G.hintT = 0; for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
			G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0; G.p.has[W_SMG] = G.p.has[W_SHOTGUN] = true; G.p.ammo[W_SMG] = 60; G.p.ammo[W_SHOTGUN] = 16;
		}
		G.p.weapon = W_FISTS;
		for (int k = 0; k < 8; k++) {                                          // a jab and a cross, frame by frame
			float u = 0.06f + 0.12f * (k % 4); G.p.punchSide = k / 4; G.p.punchT = 0.34f * (1 - u); G.p.aimT = 1; run(0.001f, idle); G.p.aimT = 1; G.p.punchT = 0.34f * (1 - u);
			char nm[32]; snprintf(nm, sizeof nm, "w0_punch%d", k);
			snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F * (k / 4 ? 0.7f : 0.3f), 3.0f, 0.25f, 42);
		}
		// a shootout
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
		for (int k = 0; k < 3; k++) { Ped *p = &G.ped[k]; memset(p, 0, sizeof *p); p->on = true; p->hp = 60; p->x = SX + 1 + k * 2.2f; p->z = SZ - 8 - k * 3; p->state = PED_IDLE; p->timer = 100; p->scale = 1; p->node = p->prev = traffic_nearest_node(&G, p->x, p->z); p->shirt = (unsigned char)(k * 4 + 1); p->pants = (unsigned char)k; p->skin = (unsigned char)k; p->hair = (unsigned char)(k + 1); }
		Input fire = idle; fire.gas = true; G.p.weapon = W_PISTOL; run(0.32f, fire); snap(out, "x1_shoot");
		run(0.5f, fire); snap(out, "x2_hit");
		run(1.2f, fire); snap(out, "x3_aftermath");
		// the police at 4 stars
		game_start(&G); G.hintT = 0; G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.heat = 260; G.p.armor = 100;
		run(20.0f, idle); snap(out, "x4_police");
		run(14.0f, idle); snap(out, "x5_police_close");
		// an explosion
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = SX + 3; G.p.p[2] = SZ + 6; G.p.face = 0; G.cam.yaw = 0;
		{ int ci = game_add_car(&G, CAR_SPORTS, SX + 6.4f, SZ - 10, 0); damage_car(&G, ci, 500, 1); }
		run(0.25f, idle); snap(out, "x6_boom1"); run(0.6f, idle); snap(out, "x7_boom2"); run(2.5f, idle); snap(out, "x8_wreck");
		// a fast drive: speed lines, streaks, the wide view
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		{ int ci = game_add_car(&G, CAR_SPORTS, 0.5f * PITCH + 20, 4 * PITCH + 3.6f, DEG(90)); G.p.car = ci; G.car[ci].driven = true; G.p.p[0] = G.car[ci].x; G.p.p[2] = G.car[ci].z; G.cam.yaw = DEG(90); G.cam.manualT = 0;
		  Input d = idle; d.gas = true; run(8.0f, d); snap(out, "z1_fast"); printf("fast drive: %.0f km/h\n", hypotf(G.car[ci].vx, G.car[ci].vz) * 3.6f); }
		// a crowd: Otto stands still for 25 s
		game_start(&G); G.hintT = 0; G.p.p[0] = 2 * PITCH + 3; G.p.p[2] = 3 * PITCH + 20; G.p.face = 0; G.cam.yaw = DEG(20);
		{ float nx, nz; traffic_node_xy((((3 * NB + 3) << 2) | 3), &nx, &nz); G.p.p[0] = nx + 0.5f; G.p.p[2] = nz - 2; G.p.face = DEG(180); G.cam.yaw = DEG(180); }
		run(30.0f, idle); snap(out, "z2_crowd");
		{ int near = 0, all = 0; for (int i = 0; i < MAX_PED; i++) if (G.ped[i].on && !G.ped[i].cop) { all++; if (hypotf(G.ped[i].x - G.p.p[0], G.ped[i].z - G.p.p[2]) < 85.0f) near++; } printf("crowd after 30 s standing still: %d people, %d within 85 m\n", all, near); }
		// WASTED
		game_start(&G); G.hintT = 0; G.p.hp = 1; damage_player(&G, 20); run(1.2f, idle); snap(out, "x9_wasted");
		// pickups and the beacons
		game_start(&G); G.hintT = 0; for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
		float px, pz; traffic_node_xy((((3 * NB + 3) << 2) | 0), &px, &pz);
		G.p.p[0] = px + 4; G.p.p[2] = pz + 10; G.p.face = DEG(180); G.cam.yaw = DEG(180); run(0.3f, idle); snap(out, "y1_pickups");
		G.p.p[0] = HOSPITAL_X + 6; G.p.p[2] = HOSPITAL_Z + 22; G.p.face = DEG(0); G.cam.yaw = 0; run(0.3f, idle); snap(out, "y2_hospital");
		game_start(&G); G.hintT = 0;
	}
	// scenery: the beach, the pier, the sign, the observatory, the freeway, downtown
	G.hintT = 0;
	snapfree(out, "a1_overview", CITY * 0.5f, 250, CITY + 250, PI_F * 0.0f + 0.0f, -0.52f, 62);
	snapfree(out, "b1_boardwalk", 3.0f, 2.4f, 232.0f, DEG(-62), 0.02f, 66);
	snapfree(out, "b2_beach", -30.0f, 3.0f, 120.0f, DEG(-100), 0.0f, 68);
	snapfree(out, "b3_pier", BEACH_X + 10.0f, 2.8f, 256.0f, DEG(-90), 0.03f, 66);
	snapfree(out, "b4_wheel", PIER_X + 80.0f, 3.5f, 256.0f, DEG(-90), 0.12f, 62);
	snapfree(out, "b5_wheel_close", PIER_X + 45.0f, 5.0f, 250.0f, DEG(-75), 0.2f, 60);
	{ float ex = 5 * PITCH, ez = 2 * PITCH, dx = PATCH[0].x - ex, dz = PATCH[0].z - ez; snapfree(out, "h1_sign", ex, 3.0f, ez, atan2f(dx, -dz), atan2f(PATCH[0].y + 15.0f, sqrtf(dx * dx + dz * dz)) - 0.02f, 64); printf("sign at %.0f %.0f %.0f, distance %.0f\n", PATCH[0].x, PATCH[0].y, PATCH[0].z, sqrtf(dx * dx + dz * dz)); }
	{ float ex = 64.0f, ez = 2 * PITCH, dx = PATCH[1].x - ex, dz = PATCH[1].z - ez; snapfree(out, "h2_obs", ex, 3.0f, ez, atan2f(dx, -dz), atan2f(PATCH[1].y + 8.0f, sqrtf(dx * dx + dz * dz)), 58); }
	snapfree(out, "f1_freeway", CITY - 6.0f, 3.0f, 230.0f, DEG(92), 0.12f, 66);
	snapfree(out, "d1_downtown", 4.8f * PITCH, 4.0f, 3.4f * PITCH, DEG(20), 0.16f, 66);
	snapfree(out, "d2_street", 4 * PITCH + 3, 2.2f, 4 * PITCH + 40, DEG(0), 0.02f, 66);
	G.paused = true; snap(out, "u1_pause");
	return 0;
}
