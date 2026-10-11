// Renders frames of the 3D view on the PC: the same triangles scene.c builds for the 3DS, drawn by a small software rasteriser
// (perspective-correct textures with mip selection, z-buffer, alpha/additive blending, near clipping).
// Usage: preview <dir with <texture>.rgba files> <out dir>
#define HOST_PREVIEW 1
#include "../source/game.h"
#include "../source/map.c"
#include "../source/game.c"
#include "../source/scene.c"
#include "../source/hud.c"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern int scene_dbg_vm; extern float scene_dbg_x, scene_dbg_d;
static Game G;
static Scene S;
static Vtx *stat_buf, *dyn_buf;

static const char *texname[MAT_COUNT] = { "white", "asphalt", "deck", "concrete", "panel", "window", "stucco", "container", "wood", "roof", "grass", "sand", "glow", "cloud", "chars" };
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

typedef struct { const float *view; float f, asp, n, fa; } Cam;
static const float HAZE[3] = { 0.92f, 0.73f, 0.59f };
#define FOG_START 22.0f
#define FOG_END 190.0f
static bool g_fog;

static PV project(const Cam *c, const Vtx *v)
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
static void draw_tri(const Cam *cam, const Vtx *v0, const Vtx *v1, const Vtx *v2, int mat, int bm, bool dt, bool dw, bool clampuv, bool at)
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

static void draw_range(const Cam *cam, const Vtx *buf, int first, int count, int mat, int bm, bool dt, bool dw, bool clampuv)
{
	for (int i = 0; i + 2 < count; i += 3) draw_tri(cam, &buf[first + i], &buf[first + i + 1], &buf[first + i + 2], mat, bm, dt, dw, clampuv, false);
}

static void draw_scene(const Scene *s)
{
	float horizon[3] = { 0.9f, 0.7f, 0.55f };
	for (int i = 0; i < FW * FH; i++) { zbuf[i] = 0; fb[i * 3] = horizon[0]; fb[i * 3 + 1] = horizon[1]; fb[i * 3 + 2] = horizon[2]; }
	float asp = (float)FW / FH;
	Cam sky = { s->skyview, 1.0f / tanf(s->fov * 0.5f * PI_F / 180.0f), asp, 8.0f, 1500.0f };
	Cam wc = { s->view, sky.f, asp, 0.25f, 1500.0f };
	// sky dome, clouds, sun (no depth)
	draw_range(&sky, s->stat, s->sfirst[GRP_SKY], s->scount[GRP_SKY], MAT_WHITE, BM_OPAQUE, false, false, true);
	draw_range(&sky, s->stat, s->sfirst[GRP_CLOUD], s->scount[GRP_CLOUD], MAT_CLOUD, BM_ALPHA, false, false, true);
	draw_range(&sky, s->stat, s->sfirst[GRP_SUN], s->scount[GRP_SUN], MAT_GLOW, BM_ADD, false, false, true);
	g_fog = true;
	for (int m = 0; m < MAT_COUNT; m++) if (m != MAT_GLOW && m != MAT_CLOUD && m != MAT_CHARS && s->scount[m]) draw_range(&wc, s->stat, s->sfirst[m], s->scount[m], m, BM_OPAQUE, true, true, false);
	g_fog = false;
	g_fog = true;
	draw_range(&wc, s->dyn, s->first[D_OPAQUE], s->count[D_OPAQUE], MAT_CHARS, BM_OPAQUE, true, true, true);
	g_fog = false;
	draw_range(&wc, s->dyn, s->first[D_ALPHA], s->count[D_ALPHA], MAT_GLOW, BM_ALPHA, true, false, true);
	draw_range(&wc, s->dyn, s->first[D_ADD], s->count[D_ADD], MAT_GLOW, BM_ADD, true, false, true);
	// view model: own projection, depth cleared
	for (int i = 0; i < FW * FH; i++) zbuf[i] = 0;
	float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	Cam vm = { id, 1.0f / tanf(s->vmfov * 0.5f * PI_F / 180.0f), asp, 0.05f, 4.0f };
	draw_range(&vm, s->dyn, s->first[D_VM], s->count[D_VM], MAT_CHARS, BM_OPAQUE, true, true, true);
	draw_range(&vm, s->dyn, s->first[D_VMADD], s->count[D_VMADD], MAT_GLOW, BM_ADD, true, false, true);
}

static void hud_capture(const char *dir, const char *name)
{
	// top screen: the 3D frame downsampled to 400x240, then the HUD on top (text is logged and drawn by preview.py)
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

static float aer[6]; static int aer_on;                      // optional free camera: x, y, z, yaw, pitch, fov
static void snap(const char *dir, const char *name)
{
	if (aer_on) { G.cam.eye[0] = aer[0]; G.cam.eye[1] = aer[1]; G.cam.eye[2] = aer[2]; G.cam.yaw = aer[3]; G.cam.pitch = aer[4]; G.cam.fov = aer[5]; G.cam.roll = 0; }
	scene_build(&G, &S);
	draw_scene(&S);
	printf("%-12s opaque %5d  alpha %4d  add %4d  vm %4d  vmadd %3d\n", name, S.count[D_OPAQUE], S.count[D_ALPHA], S.count[D_ADD], S.count[D_VM], S.count[D_VMADD]);
	save(dir, name);
	hud_capture(dir, name);
}

static void run(float seconds, Input in)
{
	for (float t = 0; t < seconds; t += 1.0f / 30.0f) { game_update(&G, &in, 1.0f / 30.0f); in.a_p = in.start_p = in.next_p = in.prev_p = in.grenade_p = in.heal_p = in.reload_p = false; }
}
static void place(float x, float z, float yaw, float pitch)
{
	G.a[0].x = x; G.a[0].z = z; G.a[0].yaw = yaw; G.a[0].pitch = pitch; G.a[0].vx = G.a[0].vz = 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".", *out = argc > 2 ? argv[2] : dir;
	for (int i = 0; i < MAT_COUNT; i++) load_tex(dir, i);
	game_init(&G, 1);
	int total = scene_build_static(&G, &S, NULL);
	stat_buf = malloc(sizeof(Vtx) * total); dyn_buf = malloc(sizeof(Vtx) * CAP_DYN);
	scene_build_static(&G, &S, stat_buf);
	S.dyn = dyn_buf;
	printf("static vertices %d (%d triangles, %.1f MB)\n", total, total / 3, total * 36 / 1048576.0);
	for (int i = 0; i < GRP_COUNT; i++) if (S.scount[i]) printf("  group %2d: %6d\n", i, S.scount[i]);
	Input idle; memset(&idle, 0, sizeof idle);
	G.state = GS_PLAY;

	// 1: spawn, looking east across the west yard
	game_new_match(&G); G.state = GS_PLAY;
	for (int i = 1; i < MAX_ACT; i++) if (G.a[i].team == TEAM_ENEMY) { G.a[i].invulnT = 99; }
	place(-36.5f, -3, DEG(80), 0); run(0.5f, idle); snap(out, "s1_spawn");
	// 2: toward the sun from the street's dead end
	place(1.5f, 11.5f, DEG(-20), DEG(4)); run(0.3f, idle); snap(out, "s2_street");
	// 3: the street looking north: bus, truck, the backdrop house
	place(0.5f, 12.5f, DEG(0), DEG(1)); run(0.3f, idle); snap(out, "s3_north");
	place(-4.5f, 14, DEG(-30), DEG(3)); run(0.3f, idle); snap(out, "n1_teal");
	place(6.5f, 12, DEG(40), DEG(3)); run(0.3f, idle); snap(out, "n2_yellow");
	place(-8, -4.5f, DEG(-90), DEG(0)); run(0.3f, idle); snap(out, "n3_teal_in");
	place(-12.5f, -6.5f, DEG(180), DEG(-4)); run(0.3f, idle); snap(out, "n4_living");
	place(24.5f, 1.5f, DEG(-90), DEG(0)); run(0.3f, idle); snap(out, "n5_yellow_in");
	place(30, 1, DEG(-90), DEG(0)); run(0.3f, idle); snap(out, "n6_east_yard");
	aer_on = 1;
	aer[0] = 1; aer[1] = 70; aer[2] = 9; aer[3] = 0; aer[4] = -DEG(80); aer[5] = 62; snap(out, "a1_top");
	aer[0] = 1; aer[1] = 26; aer[2] = 36; aer[3] = 0; aer[4] = -DEG(30); aer[5] = 60; snap(out, "a2_air");
	aer[0] = -60; aer[1] = 8; aer[2] = 20; aer[3] = DEG(60); aer[4] = DEG(2); aer[5] = 60; snap(out, "a3_desert");
	aer_on = 0;
	// 4: a gunfight: aim down sights at an enemy and fire
	game_new_match(&G); G.state = GS_PLAY;
	place(0.5f, 14, DEG(0), DEG(-1)); G.a[4].x = 0.5f; G.a[4].z = -7; G.a[4].yaw = DEG(180); G.a[4].invulnT = 99; G.a[4].target = -1;
	for (int i = 5; i < MAX_ACT; i++) { G.a[i].x = 100; G.a[i].z = 100; G.a[i].alive = false; G.a[i].respawnT = 99; }
	for (int i = 1; i < 4; i++) { G.a[i].x = 2 + i; G.a[i].z = 14.5f; G.a[i].alive = true; }
	{ Input in = idle; in.aim = true; in.fire = true; run(0.6f, in); snap(out, "s4_ads_fire"); }
	// 5: hip fire at close range with a different weapon, mid-street, explosion
	game_new_match(&G); G.state = GS_PLAY; G.p.weapon = G.p.nextWeapon = W_SMG;
	place(-5, 8, DEG(60), 0);
	G.a[4].x = 6; G.a[4].z = 2; G.a[4].yaw = DEG(-120); G.a[5].x = 3; G.a[5].z = 11; G.a[5].yaw = DEG(-60);
	G.a[4].invulnT = G.a[5].invulnT = 99;
	{ float p[3] = { 8, 0.3f, 6 }, v[3] = { 0, 0, 0 }; throw_grenade(&G, 4, p, v); G.gren[0].t = 0.12f; }
	{ Input in = idle; in.fire = true; run(0.25f, in); snap(out, "s5_smg_blast"); run(0.15f, in); snap(out, "s5b_smoke"); }
	// 6: pistol, shotgun, grenade-throw and heal poses (hip)
	for (int w = W_PISTOL; w <= W_SHOTGUN; w++) {
		game_new_match(&G); G.state = GS_PLAY; G.p.weapon = G.p.nextWeapon = w;
		place(-31, 2, DEG(70), 0); run(0.4f, idle);
		char nm[40]; snprintf(nm, sizeof nm, "s6_weapon%d", w); snap(out, nm);
		Input in = idle; in.aim = true; run(0.4f, in); snprintf(nm, sizeof nm, "s6_weapon%d_ads", w); snap(out, nm);
	}
	game_new_match(&G); G.state = GS_PLAY; place(-31, 2, DEG(70), 0); G.a[0].hp = 40;
	{ Input in = idle; in.heal_p = true; run(0.7f, in); snap(out, "s7_heal"); }
	game_new_match(&G); G.state = GS_PLAY; place(-31, 2, DEG(70), 0);
	{ Input in = idle; in.grenade_p = true; run(0.2f, in); snap(out, "s8_throw"); }
	// 9: reload
	game_new_match(&G); G.state = GS_PLAY; place(-31, 2, DEG(70), 0);
	{ Input in = idle; in.reload_p = true; run(0.7f, in); snap(out, "s9_reload"); }
	// close-up: a soldier of each team and the rifle, for judging the models
	game_new_match(&G); G.state = GS_PLAY;
	for (int i = 1; i < MAX_ACT; i++) { G.a[i].x = 100; G.a[i].z = 100; G.a[i].alive = false; G.a[i].respawnT = 99; }
	place(-10, 6, DEG(90), DEG(-2));
	G.a[4].alive = true; G.a[4].frozen = true; G.a[4].x = -5.2f; G.a[4].z = 6.4f; G.a[4].yaw = DEG(-60); G.a[4].invulnT = 99; G.a[4].vx = 2; G.a[4].walk = 1.2f; G.a[4].flashT = 0.05f;
	G.a[1].alive = true; G.a[1].frozen = true; G.a[1].x = -5.5f; G.a[1].z = 3.6f; G.a[1].yaw = DEG(-130); G.a[1].invulnT = 99; G.a[1].vx = 1; G.a[1].walk = 3.1f;
	G.a[5].alive = false; G.a[5].x = -4.2f; G.a[5].z = 8.4f; G.a[5].yaw = DEG(-70); G.a[5].deadT = 1.0f; G.a[5].respawnT = 99;
	run(0.3f, idle); snap(out, "c1_closeup");
	// gun showcase: the rifle side-on and from above-behind, large
	for (int sc = 1; sc <= 2; sc++) {
		game_new_match(&G); G.state = GS_PLAY; place(-31, 2, DEG(70), 0); scene_dbg_vm = sc; run(0.4f, idle);
		char nm[40]; snprintf(nm, sizeof nm, "g%d_rifle", sc); snap(out, nm); scene_dbg_vm = 0;
	}
	for (int w = 0; w < W_COUNT; w++) {                                // every gun side-on
		static const float dx[4] = { 0.17f, 0.12f, 0.03f, 0.2f }, dd[4] = { 0.68f, 0.55f, 0.34f, 0.95f };
		game_new_match(&G); G.state = GS_PLAY; place(-31, 2, DEG(70), 0); G.p.weapon = G.p.nextWeapon = w; scene_dbg_vm = 1; scene_dbg_x = dx[w]; scene_dbg_d = dd[w]; run(0.3f, idle);
		char nm[40]; snprintf(nm, sizeof nm, "g3_gun%d", w); snap(out, nm); scene_dbg_vm = 0;
	}
	// menus and overlays
	game_new_match(&G); place(-31, 2, DEG(70), 0); G.state = GS_TITLE; G.time = 3; run(0.1f, idle); snap(out, "t1_title");
	G.state = GS_PLAY; run(0.1f, idle); G.score[0] = 31; G.score[1] = 28; G.timeLeft = 123;
	{ Input in = idle; in.start_p = true; run(0.05f, in); } snap(out, "t2_pause"); G.paused = false;
	G.a[0].kills = 14; G.a[0].deaths = 6; G.winner = 0; G.state = GS_OVER; G.overT = 3; snap(out, "t3_over");
	game_new_match(&G); G.state = GS_PLAY; place(-31, 2, DEG(70), 0); G.a[0].hp = 0; G.a[0].alive = false; G.a[0].respawnT = 2; G.a[0].lastAttacker = 4; G.a[0].deadT = 1; G.p.eyeh = 0.5f; G.feed[0].killer = 4; G.feed[0].victim = 0; G.feed[0].t = 3; G.feed[1].killer = 1; G.feed[1].victim = 5; G.feed[1].t = 3;
	snap(out, "t4_dead");
	return 0;
}
