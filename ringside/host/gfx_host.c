// PC stand-in for the 3DS renderer: a small software rasteriser that runs the same per-vertex lighting
// as source/shader.v.pica, so previews match what the console draws. Development only.
#include "../source/gfx.h"
#include "gfx_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int W = 400, H = 240, SS = 3;              // output size and supersampling factor
static float *fb, *zb;                              // (W*SS)*(H*SS)*3 colour, and 1/w depth
static Mat4 g_view, g_proj;
static Lights g_lt;                                 // lights already in view space
static Vec3 g_upv;
static long g_tris, g_sub;
static float g_outline;

void gfx_host_setup(int w, int h, int ss)
{
	W = w; H = h; SS = ss;
	free(fb); free(zb);
	fb = calloc((size_t)W * H * SS * SS * 3, sizeof(float));
	zb = calloc((size_t)W * H * SS * SS, sizeof(float));
}

void gfx_host_clear(Vec3 top, Vec3 bottom)
{
	int w = W * SS, h = H * SS;
	for (int y = 0; y < h; y++) {
		float t = (float)y / (float)(h - 1);
		for (int x = 0; x < w; x++) {
			float *p = fb + ((size_t)y * w + x) * 3;
			p[0] = lerpf(top.x, bottom.x, t); p[1] = lerpf(top.y, bottom.y, t); p[2] = lerpf(top.z, bottom.z, t);
			zb[(size_t)y * w + x] = 0;
		}
	}
	g_tris = 0; g_sub = 0;
}

long gfx_host_triangles(void) { return g_tris; }
long gfx_host_submitted(void) { return g_sub; }

// Filled rectangle in output pixels (HUD stand-in).
void gfx_host_rect(float x0, float y0, float x1, float y1, Vec3 c, float a)
{
	for (int y = (int)(y0 * SS); y < (int)(y1 * SS); y++)
		for (int x = (int)(x0 * SS); x < (int)(x1 * SS); x++) {
			if (x < 0 || y < 0 || x >= W * SS || y >= H * SS) continue;
			float *p = fb + ((size_t)y * W * SS + x) * 3;
			p[0] += (c.x - p[0]) * a; p[1] += (c.y - p[1]) * a; p[2] += (c.z - p[2]) * a;
		}
}

void gfx_host_write_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float acc[3] = { 0, 0, 0 };
			for (int sy = 0; sy < SS; sy++)
				for (int sx = 0; sx < SS; sx++) {
					const float *p = fb + ((size_t)(y * SS + sy) * W * SS + x * SS + sx) * 3;
					acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
				}
			for (int c = 0; c < 3; c++) fputc((int)clampf(acc[c] / (SS * SS) * 255.0f + 0.5f, 0, 255), f);
		}
	fclose(f);
}

void gfx_init(void) { if (!fb) gfx_host_setup(W, H, SS); }
void gfx_exit(void) {}
void gfx_upload_meshes(void) {}

static Mat4 persp(float fov_deg, float aspect, float n, float f)
{
	float t = 1.0f / tanf(fov_deg * PI_F / 360.0f);
	Mat4 r = { { t / aspect, 0, 0, 0,  0, t, 0, 0,  0, 0, (f + n) / (n - f), 2 * f * n / (n - f),  0, 0, -1, 0 } };
	return r;
}

void gfx_scene_begin(const Camera *cam, const Lights *lt, int eye)
{
	Vec3 fwd = v3norm(v3sub(cam->target, cam->eye));
	Vec3 right = v3norm(v3cross(fwd, v3(0, 1, 0)));
	Vec3 e = v3madd(cam->eye, right, cam->iod * (float)eye * 0.5f);   // toe-in free parallax shift
	g_view = mat4_lookat(e, v3madd(cam->target, right, cam->iod * (float)eye * 0.5f), v3(0, 1, 0));
	g_proj = persp(cam->fov_deg, (float)W / (float)H, 0.1f, 60.0f);

	g_lt = *lt;                                    // convert to view space the way gfx_3ds.c does
	for (int i = 0; i < 2; i++) g_lt.dir[i] = v3norm(mat4_dir(&g_view, lt->dir[i]));
	g_lt.ppos = mat4_point(&g_view, lt->ppos);
	g_upv = v3norm(mat4_dir(&g_view, v3(0, 1, 0)));
}

void gfx_outline(float width) { g_outline = width; }
void gfx_scene_end(void) { g_outline = 0; }

typedef struct { float x, y, w; Vec3 c; } SV;      // screen-space vertex

static inline float edge(float ax, float ay, float bx, float by, float px, float py)
{
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void raster(SV a, SV b, SV c, float alpha)
{
	float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
	if (area <= 0) return;                         // culling (screen y is down, so front faces wind clockwise here)
	int w = W * SS, h = H * SS;
	int x0 = (int)floorf(fminf(a.x, fminf(b.x, c.x))), x1 = (int)ceilf(fmaxf(a.x, fmaxf(b.x, c.x)));
	int y0 = (int)floorf(fminf(a.y, fminf(b.y, c.y))), y1 = (int)ceilf(fmaxf(a.y, fmaxf(b.y, c.y)));
	x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > w - 1 ? w - 1 : x1; y1 = y1 > h - 1 ? h - 1 : y1;
	float ia = 1.0f / a.w, ib = 1.0f / b.w, ic = 1.0f / c.w;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = edge(b.x, b.y, c.x, c.y, px, py), w1 = edge(c.x, c.y, a.x, a.y, px, py), w2 = edge(a.x, a.y, b.x, b.y, px, py);
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			w0 /= area; w1 /= area; w2 /= area;
			float invw = w0 * ia + w1 * ib + w2 * ic;
			size_t idx = (size_t)y * w + x;
			if (invw <= zb[idx]) continue;
			float pr = (w0 * a.c.x * ia + w1 * b.c.x * ib + w2 * c.c.x * ic) / invw;
			float pg = (w0 * a.c.y * ia + w1 * b.c.y * ib + w2 * c.c.y * ic) / invw;
			float pb = (w0 * a.c.z * ia + w1 * b.c.z * ib + w2 * c.c.z * ic) / invw;
			float *p = fb + idx * 3;
			if (alpha >= 1.0f) { p[0] = pr; p[1] = pg; p[2] = pb; zb[idx] = invw; }
			else { p[0] += (pr - p[0]) * alpha; p[1] += (pg - p[1]) * alpha; p[2] += (pb - p[2]) * alpha; }
		}
	g_tris++;
}

// Mirrors source/shader.v.pica.
static Vec3 shade(Vec3 vp, Vec3 n, const Material *m, const float *tint)
{
	Vec3 v = v3norm(v3mul(vp, -1.0f));
	float w = m->wrap, iw = 1.0f / (1.0f + w);
	float kd0 = fmaxf((v3dot(n, g_lt.dir[0]) + w) * iw, 0), kd1 = fmaxf((v3dot(n, g_lt.dir[1]) + w) * iw, 0);
	float hu = 0.5f + 0.5f * v3dot(n, g_upv);
	Vec3 hemi = v3lerp(g_lt.ground, g_lt.sky, hu);
	Vec3 pl = v3sub(g_lt.ppos, vp);
	float d2 = v3dot(pl, pl), inv = 1.0f / sqrtf(d2 + 1e-6f);
	float att = fmaxf(1.0f - d2 * g_lt.pinv, 0); att *= att;
	float kp = fmaxf(v3dot(n, v3mul(pl, inv)), 0) * att;
	Vec3 dif = hemi;
	dif = v3madd(dif, g_lt.col[0], kd0); dif = v3madd(dif, g_lt.col[1], kd1); dif = v3madd(dif, g_lt.pcol, kp);
	Vec3 h = v3norm(v3add(g_lt.dir[0], v));
	float s = fmaxf(v3dot(n, h), 0); s *= s; s *= s; s *= s; s *= s;
	float f = 1.0f - fmaxf(v3dot(n, v), 0);
	float rim = f * f * f * m->rim * fmaxf(v3dot(n, g_lt.dir[1]) + 0.4f, 0);
	Vec3 base = v3(m->r * tint[0], m->g * tint[1], m->b * tint[2]);
	Vec3 out = v3(base.x * dif.x, base.y * dif.y, base.z * dif.z);
	out = v3madd(out, g_lt.col[0], s * m->spec);
	out = v3madd(out, g_lt.col[1], rim);
	return out;
}

void gfx_draw(const Mesh *mesh, const Mat4 *model, const Material *mat)
{
	Mat4 mv = mat4_mul(&g_view, model);
	g_sub += mesh->nidx / 3;
	{ extern void gfx_host_note(int ioff, int tris); gfx_host_note(mesh->ioff, mesh->nidx / 3); }
	const Vertex *vb = mesh_bank_verts();
	const uint16_t *ib = mesh_bank_indices();
	int w = W * SS, h = H * SS;
	static SV cache[30000]; static unsigned char have[30000];
	static int stamp = 0; static int stampv[30000];
	stamp++;
	for (int i = 0; i + 2 < mesh->nidx; i += 3) {
		SV tri[3]; int bad = 0;
		for (int k = 0; k < 3; k++) {
			int vi = ib[mesh->ioff + i + k];
			if (stampv[vi] != stamp) {
				stampv[vi] = stamp;
				Vec3 lp = v3(vb[vi].pos[0] + vb[vi].nrm[0] * g_outline, vb[vi].pos[1] + vb[vi].nrm[1] * g_outline, vb[vi].pos[2] + vb[vi].nrm[2] * g_outline);
				Vec3 p = mat4_point(&mv, lp);
				Vec3 n = v3norm(mat4_dir(&mv, v3(vb[vi].nrm[0], vb[vi].nrm[1], vb[vi].nrm[2])));
				float cw = -p.z;
				have[vi] = cw > 0.1f;
				if (have[vi]) {
					float cx = g_proj.m[0] * p.x, cy = g_proj.m[5] * p.y;
					cache[vi].x = (cx / cw * 0.5f + 0.5f) * w;
					cache[vi].y = (1.0f - (cy / cw * 0.5f + 0.5f)) * h;
					cache[vi].w = cw;
					cache[vi].c = shade(p, n, mat, vb[vi].col);
				}
			}
			if (!have[vi]) bad = 1;
			tri[k] = cache[vi];
		}
		if (!bad) {                                                // screen y flip reverses the winding; outlines keep the back faces
			if (g_outline > 0) raster(tri[0], tri[1], tri[2], mat->alpha);
			else raster(tri[0], tri[2], tri[1], mat->alpha);
		}
	}
}

// ---- 2D overlay: rectangles are exact; text is drawn as a tinted box of roughly the right size (the console uses its own font)
void gfx2d_frame_begin(void) {}

void gfx2d_rect(float x, float y, float w, float h, unsigned rgba)
{
	float k = (float)W / 400.0f;
	gfx_host_rect(x * k, y * k, (x + w) * k, (y + h) * k, v3(((rgba >> 24) & 255) / 255.0f, ((rgba >> 16) & 255) / 255.0f, ((rgba >> 8) & 255) / 255.0f), (rgba & 255) / 255.0f);
}

void gfx2d_text(float x, float y, float scale, unsigned rgba, int align, const char *s)
{
	float w = (float)strlen(s) * 30.0f * scale * 0.50f, h = 30.0f * scale * 0.62f;
	if (align == GFX_ALIGN_CENTER) x -= w * 0.5f; else if (align == GFX_ALIGN_RIGHT) x -= w;
	gfx2d_rect(x, y + 30.0f * scale * 0.2f, w, h, (rgba & 0xFFFFFF00u) | ((rgba & 255) * 6 / 10 > 200 ? 200 : (rgba & 255) * 6 / 10));
}

// ---- debug: which meshes cost the most triangles this frame
static struct { int ioff, tris, calls; } g_note[512];
static int g_nnote;
void gfx_host_note(int ioff, int tris)
{
	for (int i = 0; i < g_nnote; i++) if (g_note[i].ioff == ioff) { g_note[i].calls++; return; }
	if (g_nnote < 512) { g_note[g_nnote].ioff = ioff; g_note[g_nnote].tris = tris; g_note[g_nnote].calls = 1; g_nnote++; }
}
void gfx_host_dump_notes(void)
{
	for (int pass = 0; pass < 22; pass++) {
		int best = -1;
		for (int i = 0; i < g_nnote; i++) if (g_note[i].calls > 0 && (best < 0 || g_note[i].tris * g_note[i].calls > g_note[best].tris * g_note[best].calls)) best = i;
		if (best < 0) break;
		printf("  mesh@%-6d %5d tris x %-3d = %6d\n", g_note[best].ioff, g_note[best].tris, g_note[best].calls, g_note[best].tris * g_note[best].calls);
		g_note[best].calls = 0;
	}
}
