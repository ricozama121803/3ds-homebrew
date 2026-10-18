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

static int built_region = 0;
static void ensure_static(void)                                  // the static scene belongs to one region: rebuild it when the game is in another
{
	if (built_region == G.region) return;
	int t = scene_build_static(&G, &S, NULL);
	stat_buf = realloc(stat_buf, sizeof(Vtx) * (size_t)t);
	scene_build_static(&G, &S, stat_buf);
	built_region = G.region;
	printf("region %d (%s): static %d triangles, %.1f MB\n", G.region, REGIONS[G.region].name, t / 3, t * 36 / 1048576.0);
}
static void snap(const char *dir, const char *name)
{
	ensure_static();
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
	ensure_static();
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
		int shown = 0; bool seen[CAR_ALL] = { 0 };
		for (int i = 0; i < MAX_CAR && shown < CAR_ALL; i++) if (G.car[i].on && !seen[G.car[i].kind]) {
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
	// motorcycles, a rider, helicopters, lock-on and the weapon wheel
	{
		const float SX = 4 * PITCH, SZ = 4 * PITCH + 24;
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = SX + 3; G.p.p[2] = SZ + 20; G.p.face = 0; G.cam.yaw = 0;
		int bi = game_add_car(&G, CAR_BIKE, SX + 6.4f, SZ - 6, 0); G.car[bi].col[0] = 0.8f; G.car[bi].col[1] = 0.1f; G.car[bi].col[2] = 0.08f;
		snaplook(out, "m1_bike_side", (float[3]){ G.car[bi].x, 0.6f, G.car[bi].z }, G.car[bi].yaw + PI_F / 2, 4.2f, 0.4f, 40);
		snaplook(out, "m2_bike_34", (float[3]){ G.car[bi].x, 0.6f, G.car[bi].z }, G.car[bi].yaw + PI_F * 0.8f, 4.2f, 0.9f, 40);
		snaplook(out, "m3_bike_front", (float[3]){ G.car[bi].x, 0.6f, G.car[bi].z }, G.car[bi].yaw + PI_F * 0.15f, 4.2f, 0.5f, 40);
		G.p.p[0] = G.car[bi].x - 2.4f; G.p.p[2] = G.car[bi].z; { Input x = idle; x.enter_p = true; run(0.05f, x); }
		printf("on the bike: car %d\n", G.p.car);
		snaplook(out, "m4_rider_side", (float[3]){ G.car[bi].x, 0.8f, G.car[bi].z }, G.car[bi].yaw + PI_F / 2, 4.0f, 0.4f, 40);
		snaplook(out, "m5_rider_rear", (float[3]){ G.car[bi].x, 0.9f, G.car[bi].z }, G.car[bi].yaw + PI_F * 0.9f, 3.8f, 0.6f, 40);
		snaplook(out, "m5b_rider_front", (float[3]){ G.car[bi].x, 0.9f, G.car[bi].z }, G.car[bi].yaw + PI_F * 0.2f, 3.8f, 0.5f, 40);
		Input d = idle; d.gas = true; run(2.0f, d); snap(out, "m6_ride");
		d.lx = 0.7f; run(0.7f, d); snap(out, "m7_lean");
		snaplook(out, "m8_lean_side", (float[3]){ G.car[bi].x, 0.8f, G.car[bi].z }, G.car[bi].yaw + PI_F * 0.9f, 4.2f, 0.7f, 40);
		// helicopters
		game_start(&G); G.hintT = 0; for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false;
		int hi = -1; for (int i = 0; i < MAX_CAR; i++) if (G.car[i].on && G.car[i].kind == CAR_HELI) { hi = i; break; }
		Car *h = &G.car[hi];
		snaplook(out, "e1_heli_side", (float[3]){ h->x, 1.4f, h->z }, h->yaw + PI_F / 2, 13.0f, 1.0f, 45);
		snaplook(out, "e2_heli_34", (float[3]){ h->x, 1.4f, h->z }, h->yaw + PI_F * 0.8f, 13.0f, 3.0f, 45);
		snaplook(out, "e3_heli_front", (float[3]){ h->x, 1.4f, h->z }, h->yaw + PI_F * 0.15f, 11.0f, 1.5f, 45);
		G.p.p[0] = h->x + 4.0f; G.p.p[2] = h->z; { Input x = idle; x.enter_p = true; run(0.05f, x); }
		snap(out, "e4_heli_ground");
		Input up = idle; up.gas = true; run(4.0f, up); snap(out, "e5_heli_up");
		Input fw = idle; fw.ly = 1; fw.lx = 0.4f; run(3.0f, fw); snap(out, "e6_heli_fly");
		fw.lx = 0; run(3.0f, fw); fw.gas = true; run(3.0f, fw); snap(out, "e7_heli_high");
		printf("heli at %.0f %.0f %.0f\n", h->x, h->y, h->z);
		// crosshair, lock-on, hit and kill markers
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
		for (int k = 0; k < 2; k++) { Ped *p = &G.ped[k]; memset(p, 0, sizeof *p); p->on = true; p->hp = k ? 60 : 30; p->x = SX + (k ? 5.0f : 1.0f); p->z = SZ - 9; p->state = PED_IDLE; p->timer = 100; p->scale = 1; p->node = p->prev = traffic_nearest_node(&G, p->x, p->z); p->shirt = (unsigned char)(k * 4 + 1); p->pants = (unsigned char)k; p->skin = (unsigned char)k; p->hair = (unsigned char)(k + 1); }
		Input lk = idle; lk.aim = true; run(0.5f, lk); snap(out, "k1_lock");
		Input fl = lk; fl.lx = 1; run(0.3f, fl); run(0.4f, lk); snap(out, "k2_switched");
		fl.lx = -1; run(0.3f, fl); run(0.4f, lk);
		Input fire = lk; fire.gas = true; bool gotHit = false, gotKill = false;
		for (int i = 0; i < 240 && !gotKill; i++) {
			run(1.0f / 60, fire);
			if (G.hitT > 0.15f && !gotHit) { gotHit = true; snap(out, "k3_hit"); }
			if (G.killT > 0.75f) { gotKill = true; snap(out, "k4_kill"); }
		}
		Input sel = idle; sel.select_p = true; run(0.05f, sel); Input dpr = idle; dpr.dpx = 1; run(0.2f, dpr); snap(out, "k5_wheel");
		game_start(&G); G.hintT = 0;
	}
	// combat: guns in hand, a shootout, the police, an explosion, WASTED, pickups
	{
		const float SX = 4 * PITCH, SZ = 4 * PITCH + 24;
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
		G.hintT = 0; G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0; G.p.has[W_SMG] = G.p.has[W_SHOTGUN] = true; G.p.ammo[W_SMG] = 60; G.p.ammo[W_SHOTGUN] = 16;
		for (int w = W_PISTOL; w <= W_RPG; w++) {
			if (w == W_BAT) continue;
			G.p.weapon = w; G.p.aimT = 1; G.p.lastShotT = G.time; G.p.flashT = w == W_SMG ? 0.05f : 0;
			char nm[32]; snprintf(nm, sizeof nm, "w%d_aim", w); run(0.05f, idle); G.p.aimT = 1;
			snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F / 2, 3.4f, 0.25f, 42);
			snprintf(nm, sizeof nm, "w%d_aim3", w); snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F * 0.75f, 3.4f, 0.3f, 42);
			snprintf(nm, sizeof nm, "w%d_close", w); snaplook(out, nm, (float[3]){ G.p.p[0] + sinf(G.p.face) * 0.5f, 1.3f, G.p.p[2] - cosf(G.p.face) * 0.5f }, G.p.face + PI_F * 0.62f, 1.9f, 0.15f, 40);
			snprintf(nm, sizeof nm, "w%d_aimf", w); snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F, 3.4f, 0.2f, 42);
		}
		G.p.weapon = W_FISTS;
		for (int k = 0; k < 8; k++) {                                          // a jab and a cross, frame by frame
			float u = 0.06f + 0.12f * (k % 4); G.p.punchSide = k / 4; G.p.punchT = 0.34f * (1 - u); G.p.aimT = 1; run(0.001f, idle); G.p.aimT = 1; G.p.punchT = 0.34f * (1 - u);
			char nm[32]; snprintf(nm, sizeof nm, "w0_punch%d", k);
			snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F * (k / 4 ? 0.7f : 0.3f), 3.0f, 0.25f, 42);
		}
		// the bat swing
		G.p.weapon = W_BAT;
		G.p.aimT = 1; run(0.001f, idle); snaplook(out, "w4_ready", (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F * 0.5f, 3.2f, 0.3f, 42);
		for (int k = 0; k < 6; k++) { float u = 0.05f + 0.17f * k, T = melee_time(W_BAT); G.p.punchSide = 0; G.p.punchT = T * (1 - u); G.p.aimT = 1; run(0.001f, idle); G.p.aimT = 1; G.p.punchT = T * (1 - u); char nm[32]; snprintf(nm, sizeof nm, "w4_swing%d", k); snaplook(out, nm, (float[3]){ G.p.p[0], 1.0f, G.p.p[2] }, G.p.face + PI_F * 0.5f, 3.2f, 0.3f, 42); }
		// a roll (Y): four moments of the tumble
		{
			G.p.weapon = W_PISTOL; G.p.cover = 0; G.p.aimT = 0; G.p.face = 0; G.cam.yaw = 0; G.p.rollT = 0; G.p.rollCd = 0;
			Input yr = idle; yr.x_p = true; run(0.001f, yr);
			for (int k = 0; k < 4; k++) { G.p.rollT = ROLL_TIME * (1.0f - (0.12f + 0.25f * k)); run(0.0001f, idle); G.p.rollT = ROLL_TIME * (1.0f - (0.12f + 0.25f * k)); char nm[32]; snprintf(nm, sizeof nm, "w5_roll%d", k); snaplook(out, nm, (float[3]){ G.p.p[0], 0.8f, G.p.p[2] }, G.p.face + PI_F * 0.5f, 3.4f, 0.3f, 42); }
			G.p.rollT = 0;
		}
		// cover: a wall corner, then a car
		{
			game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
			const Box *wb = NULL;
			for (int i = 0; i < G.w.nbox && !wb; i++) {
				const Box *b = &G.w.box[i];
				if (b->z1 - b->z0 < 14 || b->x1 - b->x0 < 14 || b->y1 < 8 || b->x0 < 40) continue;
				bool clear = true;
				for (int j = 0; j < G.w.nbox; j++) { if (j == i) continue; const Box *o = &G.w.box[j]; if (o->x1 > b->x0 - 12 && o->x0 < b->x0 + 14 && o->z1 > b->z0 - 4 && o->z0 < b->z1 + 14) clear = false; }
				if (clear) wb = b;
			}
			if (wb) {
				G.p.p[0] = wb->x0 - 1.0f; G.p.p[2] = wb->z1 - 5.0f; G.p.face = PI_F / 2; G.cam.yaw = PI_F / 2; G.cam.manualT = 0;
				{ Input y = idle; y.x_p = true; run(0.05f, y); }
				run(1.0f, idle); snap(out, "c1_wall_take");
				Input sl = idle; sl.lx = 1; run(2.0f, sl); run(1.0f, idle); snap(out, "c2_wall_corner");
				Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 60; q->x = wb->x0 + 8.0f; q->z = wb->z1 + 3.0f; q->state = PED_IDLE; q->timer = 100; q->scale = 1; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z); q->shirt = 1;
				Input aim = idle; aim.aim = true; run(0.5f, aim); snap(out, "c3_wall_peek");
				Input fire = aim; fire.gas = true; run(0.5f, fire); snap(out, "c4_wall_shoot");
				run(1.5f, aim);
				memset(G.ped, 0, sizeof G.ped);
				G.p.p[0] = wb->x0 - 1.0f; G.p.p[2] = wb->z1 - 3.0f; G.p.face = PI_F / 2; G.cam.yaw = PI_F / 2; G.cam.manualT = 0; { Input y2 = idle; y2.x_p = true; run(0.1f, y2); }
				Input pu = idle; pu.lx = 1; run(2.0f, pu); snap(out, "c9_wrap_start"); run(0.5f, pu); snap(out, "c9b_wrap_mid"); run(2.0f, pu); snap(out, "c9c_wrap_after");
			}
			game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
			float cxp = SX + 20.0f, czp = SZ - 20.0f;
			game_add_car(&G, CAR_SEDAN, cxp, czp, 0.0f);
			G.p.p[0] = cxp + 2.2f; G.p.p[2] = czp + 0.5f; G.p.face = -PI_F / 2; G.cam.yaw = -PI_F / 2; G.cam.manualT = 0;
			{ Input y = idle; y.x_p = true; run(0.05f, y); }
			run(1.0f, idle); snap(out, "c5_car_take");
			{ Ped *q0 = &G.ped[0]; memset(q0, 0, sizeof *q0); q0->on = true; q0->hp = 60; q0->x = cxp - 9.0f; q0->z = czp - 1.0f; q0->state = PED_IDLE; q0->timer = 100; q0->scale = 1; q0->node = q0->prev = traffic_nearest_node(&G, q0->x, q0->z); q0->shirt = 4;
			  Input ai = idle; ai.aim = true; run(0.7f, ai); snap(out, "c5b_car_over"); Input fi = ai; fi.gas = true; run(0.4f, fi); snap(out, "c5c_car_over_fire"); run(1.0f, idle); q0->on = false; }
			Input sl = idle; sl.lx = 1; run(2.5f, sl); run(0.5f, idle); snap(out, "c6_car_end");
			Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 60; q->x = cxp - 9.0f; q->z = czp - 6.0f; q->state = PED_IDLE; q->timer = 100; q->scale = 1; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z); q->shirt = 4;
			Input aim = idle; aim.aim = true; run(0.5f, aim); snap(out, "c7_car_peek");
			Input fire = aim; fire.gas = true; run(0.4f, fire); snap(out, "c8_car_shoot");
		}
		// tyre smoke
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		{ int ci = game_add_car(&G, CAR_SPORTS, 0.5f * PITCH + 20, 4 * PITCH + 3.6f, DEG(90)); G.p.car = ci; G.car[ci].driven = true; G.p.p[0] = G.car[ci].x; G.p.p[2] = G.car[ci].z; G.cam.yaw = DEG(90); G.cam.manualT = 0;
		  Input d = idle; d.gas = true; run(5.0f, d); Input b = idle; b.brake = true; run(0.7f, b); snap(out, "t1_smoke");
		  G.cam.yaw = DEG(90) + 2.3f; G.cam.manualT = 5; run(0.3f, b); snaplook(out, "t2_smoke_side", (float[3]){ G.car[ci].x, 0.8f, G.car[ci].z }, DEG(90) + 1.0f, 8.0f, 1.2f, 50); }
		// a shootout
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0;
		for (int k = 0; k < 3; k++) { Ped *p = &G.ped[k]; memset(p, 0, sizeof *p); p->on = true; p->hp = 60; p->x = SX + 1 + k * 2.2f; p->z = SZ - 8 - k * 3; p->state = PED_IDLE; p->timer = 100; p->scale = 1; p->node = p->prev = traffic_nearest_node(&G, p->x, p->z); p->shirt = (unsigned char)(k * 4 + 1); p->pants = (unsigned char)k; p->skin = (unsigned char)k; p->hair = (unsigned char)(k + 1); }
		Input fire = idle; fire.gas = true; G.p.weapon = W_PISTOL; run(0.32f, fire); snap(out, "x1_shoot");
		run(0.5f, fire); snap(out, "x2_hit");
		run(1.2f, fire); snap(out, "x3_aftermath");
		// the police at 4 stars
		game_start(&G); G.hintT = 0; G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.heat = 450; G.p.armor = 100;
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
	// airport, street furniture, over-the-shoulder aim
	G.hintT = 0; region_enter(&G, REG_AIRPORT);
	const float SX = 4 * PITCH, SZ = 4 * PITCH + 24;
	snapfree(out, "ap1_overview", 270.0f, 160.0f, CITY + 20.0f, DEG(0), -0.42f, 62);
	snapfree(out, "ap2_gates", 250.0f, 14.0f, CITY + 245.0f, DEG(10), -0.12f, 62);
	snapfree(out, "ap3_plane_close", 360.0f, 6.0f, AIR_PLANE_Z + 45.0f, DEG(-12), 0.05f, 58);
	G.time = 12.0f - 8.0f + 120.0f; { float x = 60.0f + 0.5f * 3.1f * 144.0f; snaplook(out, "ap4_roll", (float[3]){ x, 3.0f, RWY1_Z }, DEG(-70), 60.0f, 3.0f, 55); }
	G.time = 28.0f - 8.0f + 120.0f; { float x = 60.0f + 0.5f * 3.1f * 256.0f + 50.0f * 12.0f; snaplook(out, "ap5_takeoff", (float[3]){ x, (x - 457.0f) * 0.2f, RWY1_Z }, DEG(-60), 70.0f, 6.0f, 55); }
	G.time = 56.0f - 8.0f + 120.0f; { float x = 1500.0f - 58.0f * 8.0f, y = (x - 440.0f) * 0.095f; snaplook(out, "ap6_landing", (float[3]){ x, y, RWY2_Z }, DEG(70), 90.0f, 8.0f, 55); }
	G.time = 0;
	{
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		const Prop *pr = NULL; for (int k = 0; k < G.w.nprop && !pr; k++) if (G.w.prop[k].type == 1 && G.w.prop[k].x > 150 && G.w.prop[k].z > 150) pr = &G.w.prop[k];
		if (pr) {
			G.p.p[0] = pr->x - 3.0f; G.p.p[2] = pr->z + 6.0f; G.p.face = DEG(90); G.cam.yaw = DEG(90); G.cam.manualT = 0; run(0.6f, idle); snap(out, "pp1_props");
			G.p.p[0] = pr->x - 1.1f; G.p.p[2] = pr->z; { Input y = idle; y.x_p = true; run(0.05f, y); } run(1.2f, idle); snap(out, "pp2_prop_cover");
		}
		game_start(&G); for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; G.hintT = 0;
		G.p.p[0] = SX + 3; G.p.p[2] = SZ; G.p.face = 0; G.cam.yaw = 0; G.p.weapon = W_RIFLE;
		{ Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 600; q->x = SX + 3; q->z = SZ - 20; q->state = PED_IDLE; q->timer = 100; q->scale = 1; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z); q->shirt = 3; }
		Input sh = idle; sh.aim = true; run(1.0f, sh); snap(out, "sh1_aim_shoulder"); sh.gas = true; run(0.3f, sh); snap(out, "sh2_fire_shoulder");
	}
	// aiming close up, a flyable airliner, the fence, spoilers
	{
		const float SX2 = 4 * PITCH, SZ2 = 4 * PITCH + 24;
		game_start(&G); for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; G.hintT = 0;
		G.p.p[0] = SX2 + 3; G.p.p[2] = SZ2; G.p.face = 0; G.cam.yaw = 0; G.p.weapon = W_RIFLE;
		{ Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 900; q->x = SX2 + 3; q->z = SZ2 - 22; q->state = PED_IDLE; q->timer = 100; q->scale = 1; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z); q->shirt = 3; }
		Input a1 = idle; a1.aim = true; run(1.0f, a1); snap(out, "ai1_close_rifle");
		G.p.weapon = W_SMG; run(0.6f, a1); snap(out, "ai2_close_smg");
		G.p.weapon = W_SNIPER; run(0.6f, a1); snap(out, "ai2b_close_sniper");
		G.p.weapon = W_PISTOL; run(0.6f, a1); snap(out, "ai2c_close_pistol");
		run(0.1f, idle);
		game_start(&G); region_enter(&G, REG_AIRPORT); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; G.hintT = 0;
		int pi = -1; for (int i = 0; i < MAX_CAR; i++) if (G.car[i].on && G.car[i].kind == CAR_PLANE) { pi = i; break; }
		if (pi >= 0) {
			G.p.p[0] = G.car[pi].x; G.p.p[2] = G.car[pi].z + 5.5f; { Input x = idle; x.enter_p = true; run(0.05f, x); }
			run(0.5f, idle); snap(out, "pl1_cockpit_ground");
			Input th = idle; th.gas = true; run(7.0f, th); Input pu = th; pu.ly = -1; run(5.0f, pu); snap(out, "pl2_climb");
			Input bk = th; bk.lx = 1; run(3.0f, bk); snap(out, "pl3_bank");
		}
		snapfree(out, "fe1_fence", 300.0f, 2.5f, 6.0f, DEG(15), 0.0f, 62);
		snapfree(out, "fe2_fence_air", 260.0f, 2.5f, AIR_Z1 - 40.0f, DEG(160), 0.0f, 62);
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; G.hintT = 0;
		for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false;
		for (int k = 0; k < 3; k++) { int ci = game_add_car(&G, CAR_SPORTS, 20.0f + k * 12.0f, 100.0f, 0.0f); G.car[ci].col[0] = 0.1f + 0.4f * k; G.car[ci].col[1] = 0.2f; G.car[ci].col[2] = 0.8f - 0.3f * k; char nm[24]; snprintf(nm, sizeof nm, "sp%d_spoiler", ci); (void)nm; }
		for (int i = 0, k = 0; i < MAX_CAR; i++) if (G.car[i].on && G.car[i].kind == CAR_SPORTS) { char nm[24]; snprintf(nm, sizeof nm, "sp%d_spoiler", k++); snaplook(out, nm, (float[3]){ G.car[i].x, 0.6f, G.car[i].z }, PI_F + 0.7f, 6.5f, 1.6f, 48); }
	}
	// skydive, compass, pause menu
	{
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; G.hintT = 0;
		int h = -1; for (int i = 0; i < MAX_CAR; i++) if (G.car[i].on && G.car[i].kind == CAR_HELI) { h = i; break; }
		G.p.p[0] = G.car[h].x + 4.0f; G.p.p[2] = G.car[h].z; { Input x = idle; x.enter_p = true; run(0.05f, x); }
		Input up = idle; up.gas = true; run(9.0f, up); run(0.3f, idle); { Input x = idle; x.enter_p = true; run(0.05f, x); }
		run(0.6f, idle); snap(out, "sk1_freefall"); run(0.8f, idle); { Input px = idle; px.enter_p = true; run(0.05f, px); } run(0.4f, idle); snap(out, "sk2_chute_opening"); run(2.0f, idle); snap(out, "sk3_chute");
		Input dr = idle; dr.ly = 1; run(1.5f, dr); snap(out, "sk4_glide");
		game_start(&G); G.hintT = 0; snap(out, "cm1_compass");
		G.cam.yaw = DEG(120); snap(out, "cm2_compass_se");
		G.paused = true; snap(out, "u2_pause"); G.paused = false;
		region_enter(&G, REG_AIRPORT);
		int pi = -1; for (int i = 0; i < MAX_CAR; i++) if (G.car[i].on && G.car[i].kind == CAR_PLANE) { pi = i; break; }
		G.p.p[0] = G.car[pi].x; G.p.p[2] = G.car[pi].z + 4.0f; { Input x = idle; x.enter_p = true; run(0.05f, x); }
		run(0.5f, idle); snap(out, "pl4_small_plane");
		Input th = idle; th.gas = true; run(4.0f, th); Input pu = th; pu.ly = -1; run(2.5f, pu); Input bk = th; bk.lx = 1; run(1.0f, bk); snap(out, "pl5_turning");
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
	// the landmarks
	snaplook(out, "l1_capitol", (float[3]){ 130.0f, 14.0f, -52.0f }, DEG(160), 70.0f, 8.0f, 55);
	snaplook(out, "l2_dodger", (float[3]){ CITY * 0.82f, 10.0f, -100.0f }, DEG(190), 150.0f, 60.0f, 60);
	snaplook(out, "l2b_dodger_air", (float[3]){ CITY * 0.82f, 4.0f, -100.0f }, DEG(150), 45.0f, 38.0f, 62);
	snaplook(out, "l3_theme", (float[3]){ CITY * 0.38f, 18.0f, CITY + 95.0f }, DEG(10), 85.0f, 8.0f, 55);
	snaplook(out, "l4_venice", (float[3]){ -5.0f, 5.5f, 3 * PITCH }, DEG(75), 38.0f, 1.0f, 60);
	snaplook(out, "l4b_venice_far", (float[3]){ -5.0f, 5.5f, 3 * PITCH }, DEG(80), 110.0f, 4.0f, 50);
	snaplook(out, "l5_cityhall", (float[3]){ 6.5f * PITCH, 60.0f, 2.5f * PITCH }, DEG(215), 140.0f, 5.0f, 55);
	{ const Box *b = &G.w.box[G.w.bfirst[5][3]]; for (int k = 0; k < G.w.bcount[5][3]; k++) if (G.w.box[G.w.bfirst[5][3] + k].y1 > b->y1 && G.w.box[G.w.bfirst[5][3] + k].kind == BK_BLDG) b = &G.w.box[G.w.bfirst[5][3] + k];
	  snaplook(out, "l6_donut", (float[3]){ 0.5f * (b->x0 + b->x1), b->y1 + 4.5f, 0.5f * (b->z0 + b->z1) }, DEG(100), 28.0f, 4.0f, 55); }
	snaplook(out, "l7_pad", (float[3]){ PADS_CITY[0][0], 0.0f, PADS_CITY[0][1] }, DEG(200), 26.0f, 18.0f, 60);
	snaplook(out, "l8_pad_beach", (float[3]){ PADS_CITY[1][0], 0.0f, PADS_CITY[1][1] }, DEG(250), 30.0f, 14.0f, 60);
	snapfree(out, "f1_freeway", CITY - 6.0f, 3.0f, 230.0f, DEG(92), 0.12f, 66);
	snapfree(out, "d1_downtown", 4.8f * PITCH, 4.0f, 3.4f * PITCH, DEG(20), 0.16f, 66);
	snapfree(out, "d2_street", 4 * PITCH + 3, 2.2f, 4 * PITCH + 40, DEG(0), 0.02f, 66);
	G.paused = true; snap(out, "u1_pause"); G.paused = false;
	// locking onto somebody far away with the camera tipped down: the camera frames them, then returns
	{
		const float SXf = 4 * PITCH, SZf = 4 * PITCH + 24;
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = SXf + 3; G.p.p[2] = SZf; G.p.face = 0; G.cam.yaw = 0; G.cam.pitch = -0.75f; G.cam.manualT = 0; G.p.weapon = W_RIFLE;
		{ Ped *q = &G.ped[0]; memset(q, 0, sizeof *q); q->on = true; q->hp = 900; q->x = SXf + 3; q->z = SZf - 34; q->state = PED_IDLE; q->timer = 100; q->scale = 1; q->node = q->prev = traffic_nearest_node(&G, q->x, q->z); q->shirt = 3; }
		run(0.4f, idle); snap(out, "lf0_before");
		Input lk2 = idle; lk2.aim = true; run(1.2f, lk2); snap(out, "lf1_far_lock");
		run(2.5f, idle); snap(out, "lf2_after");
	}
	// a wheelie
	{
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		int bw = game_add_car(&G, CAR_BIKE, 4 * PITCH + 6.4f, 4 * PITCH + 24.0f + 200.0f, 0); G.p.p[0] = 4 * PITCH + 4.5f; G.p.p[2] = 4 * PITCH + 224.0f; { Input x = idle; x.enter_p = true; run(0.1f, x); }
		Input gs = idle; gs.gas = true; run(1.2f, gs); Input nb = gs; nb.ly = -1; run(1.3f, nb);
		snaplook(out, "wl1_wheelie_side", (float[3]){ G.car[bw].x, 0.9f, G.car[bw].z }, DEG(90), 5.0f, 0.5f, 42);
		snaplook(out, "wl2_wheelie_34", (float[3]){ G.car[bw].x, 0.9f, G.car[bw].z }, DEG(150), 5.0f, 1.0f, 42);
		snap(out, "wl3_wheelie_chase");
	}
	// the skydiving poses, from the side and the front
	{
		game_start(&G); for (int i = 0; i < MAX_PED; i++) G.ped[i].on = false; for (int i = 0; i < MAX_CAR; i++) G.car[i].on = false; G.hintT = 0;
		G.p.p[0] = 4 * PITCH + 3; G.p.p[2] = 4 * PITCH + 24; G.p.p[1] = 60.0f; G.p.airT = 1.5f; G.p.face = DEG(0); G.p.v[1] = -30.0f;
		float t[3] = { G.p.p[0], 60.9f, G.p.p[2] };
		snaplook(out, "sd1_free_side", t, DEG(90), 3.2f, 0.4f, 42); snaplook(out, "sd2_free_above", (float[3]){ t[0], 60.0f, t[2] }, DEG(0), 0.2f, 4.5f, 55); snaplook(out, "sd3_free_front", t, DEG(180), 3.4f, 0.2f, 42);
		G.p.chute = 1.0f; t[1] = 60.9f;
		snaplook(out, "sd4_chute_side", t, DEG(90), 6.0f, 1.8f, 48); snaplook(out, "sd5_chute_front", t, DEG(180), 5.0f, 1.2f, 50); snaplook(out, "sd6_chute_close", (float[3]){ t[0], 61.2f, t[2] }, DEG(150), 2.6f, 0.3f, 40);
	}
	// the world map and the parachute prompt
	{
		game_start(&G); G.hintT = 0; G.mapMode = 1; G.mapSel = REG_HILLS; snap(out, "wm1_world_map"); G.mapSel = -1; G.mapMode = 0;
		snap(out, "wm0_town_map");
		game_start(&G); G.hintT = 0; G.p.p[1] = 120.0f; G.p.airT = 1.2f; G.p.v[1] = -25.0f; snap(out, "pc1_prompt");
		G.p.chute = 1.0f; snap(out, "pc2_open");
		G.p.p[1] = 0; G.p.airT = 0; G.p.chute = 0; G.wheel = true; G.wheelSel = 4; snap(out, "wh1_wheel");
	}
	// the regions
	for (int r = 0; r < REG_COUNT; r++) {
		game_start(&G); region_enter(&G, r); G.hintT = 0; G.bannerT = 0; for (int i = 0; i < MAX_PED; i++) if (r == REG_CITY) G.ped[i].on = false;
		char nm[32];
		G.p.p[0] = 4 * PITCH + 3; G.p.p[2] = 4 * PITCH + 24; G.p.face = 0; G.cam.yaw = 0; G.cam.manualT = 0; run(0.5f, idle);
		snprintf(nm, sizeof nm, "rg%d_street", r); snap(out, nm);
		G.bannerT = 2.6f; G.fade = 0; snprintf(nm, sizeof nm, "rg%d_banner", r); snap(out, nm); G.bannerT = 0; G.fade = 0.55f; snprintf(nm, sizeof nm, "rg%d_fade", r); snap(out, nm); G.fade = 0;
		snprintf(nm, sizeof nm, "rg%d_over", r); snapfree(out, nm, CITY * 0.5f, 140.0f, CITY * 0.5f + 190.0f, DEG(0), -0.5f, 62);
		snprintf(nm, sizeof nm, "rg%d_block", r); snapfree(out, nm, 2 * PITCH + 20, 14.0f, 2 * PITCH + 60, DEG(20), -0.2f, 62);
		snprintf(nm, sizeof nm, "rg%d_far", r); snapfree(out, nm, 4 * PITCH + 3, 6.0f, 4 * PITCH + 90, DEG(100), 0.02f, 70);
		for (int k = 0; k < G.w.ngate; k++) { const Gate *gt = &G.w.gate[k]; float ex = gt->dir < 2 ? (gt->dir == 0 ? CITY - 20 : 20) : 256.0f, ez = gt->dir < 2 ? 256.0f : (gt->dir == 2 ? CITY - 20 : 20); float yaw = gt->dir == 0 ? DEG(90) : gt->dir == 1 ? DEG(-90) : gt->dir == 2 ? DEG(180) : 0.0f; snprintf(nm, sizeof nm, "rg%d_gate%d", r, k); snapfree(out, nm, ex, 3.0f, ez, yaw, 0.05f, 64); }
	}
	return 0;
}
