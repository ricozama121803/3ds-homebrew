// Sunset Ops - scene builder. Static world (ground, boxes, desert, sky) is built once into one buffer, sorted by texture;
// everything that moves (soldiers, effects, the first-person gun) is rebuilt each frame into small fixed-size groups.
#include "scene.h"
#include <string.h>
#include <stdlib.h>

// ---- tiny 3x4 matrix helpers -----------------------------------------------------------------------------------------
typedef struct { float m[12]; } M;
static M m_id(void) { M r = {{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }}; return r; }
static M m_mul(M a, M b)
{
	M r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) r.m[i * 4 + j] = a.m[i * 4] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j];
		r.m[i * 4 + 3] = a.m[i * 4] * b.m[3] + a.m[i * 4 + 1] * b.m[7] + a.m[i * 4 + 2] * b.m[11] + a.m[i * 4 + 3];
	}
	return r;
}
static M m_tr(float x, float y, float z) { M r = m_id(); r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
static M m_rx(float a) { float c = cosf(a), s = sinf(a); M r = {{ 1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0 }}; return r; }
static M m_ry(float a) { float c = cosf(a), s = sinf(a); M r = {{ c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0 }}; return r; }
static M m_rz(float a) { float c = cosf(a), s = sinf(a); M r = {{ c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0 }}; return r; }
static void m_pt(const M *m, const float p[3], float o[3])
{
	for (int i = 0; i < 3; i++) o[i] = m->m[i * 4] * p[0] + m->m[i * 4 + 1] * p[1] + m->m[i * 4 + 2] * p[2] + m->m[i * 4 + 3];
}
static void m_dir(const M *m, const float p[3], float o[3])
{
	for (int i = 0; i < 3; i++) o[i] = m->m[i * 4] * p[0] + m->m[i * 4 + 1] * p[1] + m->m[i * 4 + 2] * p[2];
}

static float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// ---- lighting: warm afternoon sun in the west-south-west, cool sky ambient -----------------------------------------------------
#define SUN_X (-0.82f)
#define SUN_Y ( 0.52f)
#define SUN_Z ( 0.48f)
static float SUNV[3];
static bool sun_ready;
void scene_sun_dir(float d[3])
{
	if (!sun_ready) {
		float l = sqrtf(SUN_X * SUN_X + SUN_Y * SUN_Y + SUN_Z * SUN_Z);
		SUNV[0] = SUN_X / l; SUNV[1] = SUN_Y / l; SUNV[2] = SUN_Z / l; sun_ready = true;
	}
	d[0] = SUNV[0]; d[1] = SUNV[1]; d[2] = SUNV[2];
}
static const float EXPO = 1.32f;
static const float L_SKY[3] = { 0.40f, 0.44f, 0.58f }, L_GND[3] = { 0.38f, 0.32f, 0.28f }, L_SUN[3] = { 0.66f, 0.46f, 0.28f };
static void world_light(const float n[3], float sunvis, float out[3])
{
	if (!sun_ready) { float t[3]; scene_sun_dir(t); }
	float d = n[0] * SUNV[0] + n[1] * SUNV[1] + n[2] * SUNV[2]; if (d < 0) d = 0;
	if (n[1] > 0.5f) d = 0.62f * n[1];                        // the low sun still gilds flat ground and roofs (not physical, but it reads right)
	d *= sunvis;
	float h = 0.5f + 0.5f * n[1];
	for (int i = 0; i < 3; i++) out[i] = (L_GND[i] + (L_SKY[i] - L_GND[i]) * h + L_SUN[i] * d) * EXPO;
}

// ---- meshes ------------------------------------------------------------------------------------------------------------
typedef struct { Vtx *v; int n, cap; } Mesh;
typedef struct { float p[3], u, v, c[3], a; } SV;

static void put(Mesh *m, const SV *s)
{
	if (m->v) {
		if (m->n >= m->cap) return;
		Vtx *o = &m->v[m->n];
		o->x = s->p[0]; o->y = s->p[1]; o->z = s->p[2]; o->u = s->u; o->v = s->v;
		o->r = s->c[0]; o->g = s->c[1]; o->b = s->c[2]; o->a = s->a;
	}
	m->n++;
}
static void tri(Mesh *m, const SV *a, const SV *b, const SV *c)
{
	if (m->v && m->n + 3 > m->cap) return;
	put(m, a); put(m, b); put(m, c);
}
static void quad(Mesh *m, const SV *a, const SV *b, const SV *c, const SV *d)
{
	if (m->v && m->n + 6 > m->cap) return;
	put(m, a); put(m, b); put(m, c); put(m, a); put(m, c); put(m, d);
}
static SV sv(float x, float y, float z, float u, float v, const float c[3], float a)
{
	SV s = {{ x, y, z }, u, v, { c[0], c[1], c[2] }, a};
	return s;
}

typedef enum { LM_WORLD, LM_VM } LightMode;

static void light_for(LightMode lm, const float n[3], float out[3])
{
	if (lm == LM_WORLD) { world_light(n, 1.0f, out); return; }
	static const float L[3] = { -0.35f, 0.75f, 0.56f };
	float d = n[0] * L[0] + n[1] * L[1] + n[2] * L[2]; if (d < 0) d = 0;
	float v = 0.42f + 0.62f * d;
	out[0] = v * 1.04f; out[1] = v * 0.96f; out[2] = v * 0.88f;
}


// ---- smooth primitives (ellipsoid, tapered cylinder, rounded box) shared by the static map and the dynamic models ----
static Mesh sm[GRP_COUNT];
static const Game *SG;
struct Box_fwd; static float sun_vis3(const float p[3], const Box *self);

// character / weapon atlas (assets/make_textures.py paints it): regions in pixels (x, y, w, h from the top left) of the 256x256 texture
enum { TX_WHITE, TX_CAMO_A, TX_CAMO_E, TX_VEST_A, TX_VEST_E, TX_FACE, TX_SKIN, TX_HELM_A, TX_HELM_E, TX_BOOT, TX_GLOVE, TX_METAL, TX_POLY, TX_BRONZE, TX_WOOD, TX_SCARF_A, TX_SCARF_E, TX_COUNT };
static const short TXR[TX_COUNT][4] = {
	{ 0, 0, 8, 8 }, { 64, 0, 64, 64 }, { 128, 0, 64, 64 }, { 192, 0, 64, 64 }, { 0, 64, 64, 64 }, { 64, 64, 64, 64 }, { 128, 64, 32, 32 }, { 192, 64, 64, 64 },
	{ 0, 128, 64, 64 }, { 64, 128, 32, 32 }, { 96, 128, 32, 32 }, { 128, 128, 64, 64 }, { 192, 128, 64, 64 }, { 0, 192, 64, 64 }, { 64, 192, 64, 64 }, { 128, 192, 32, 32 }, { 160, 192, 32, 32 } };
static bool g_headmap;
static int g_tx;                                  // current atlas region for dynamic models (0 = flat white, colours come from the tint)
static float g_tint[3] = { 1, 1, 1 };
static void tx_rect(int idx, float r[4])          // u0, v0 (bottom), du, dv with a half-texel inset so filtering never bleeds into the neighbour
{
	const short *q = TXR[idx];
	r[0] = (q[0] + 0.5f) / 256.0f; r[2] = (q[2] - 1.0f) / 256.0f;
	r[3] = (q[3] - 1.0f) / 256.0f; r[1] = 1.0f - (q[1] + q[3] - 0.5f) / 256.0f;
}

typedef struct {
	bool stat;                 // true: static map (planar texture coordinates, baked light); false: dynamic (flat white texture)
	Mesh *dmesh; const M *T; float col[3]; LightMode lm; const float *eye; bool cull;
	int sideMat, topMat; float tile;
	bool nobot;                // skip the underside (static boxes standing on the ground)
	bool capture;              // record model-space vertices (position, normal, base colour) instead of emitting lit ones
	float tx[4];               // atlas rectangle for dynamic models
} EC;

typedef struct { float p[3], n[3], c[3], u, v; } CV;
static CV cap_buf[16384];
static int cap_n;
static bool g_capture, g_camo;
static int g_shade;                     // 1: darken model-space vertices towards the ground (soldiers)

static void vnorm(float v[3]) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; } }

static void qd(const EC *e, const float p[4][3], const float n[4][3], const float uv[4][2])
{
	float P[4][3], N[4][3], fc[3] = { 0, 0, 0 }, nf[3] = { 0, 0, 0 };
	for (int i = 0; i < 4; i++) {
		m_pt(e->T, p[i], P[i]); m_dir(e->T, n[i], N[i]);
		for (int k = 0; k < 3; k++) { fc[k] += P[i][k]; nf[k] += N[i][k]; }
	}
	if (e->capture) {
		static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
		if (cap_n + 6 > (int)(sizeof cap_buf / sizeof cap_buf[0])) return;
		for (int k = 0; k < 6; k++) {
			CV *c = &cap_buf[cap_n++]; int i = idx[k];
			memcpy(c->p, P[i], sizeof c->p); memcpy(c->n, N[i], sizeof c->n);
			c->u = e->tx[0] + uv[i][0] * e->tx[2]; c->v = e->tx[1] + e->tx[3] * (1.0f - uv[i][1]);
			float f = 1.0f;
			if (g_camo && g_tx == 0) {                                           // woodland blotches from a few overlapping sines of the model-space position
				float q = sinf(P[i][0] * 11.0f + sinf(P[i][1] * 8.0f) * 1.7f) + sinf(P[i][1] * 9.0f + P[i][2] * 7.0f) + sinf(P[i][2] * 12.0f + P[i][0] * 6.0f + P[i][1] * 4.0f);
				f = q > 0.9f ? 0.6f : q < -1.0f ? 1.3f : 1.0f + 0.12f * q;
			}
			if (g_shade == 1) f *= 0.82f + 0.18f * sat(P[i][1] / 1.5f);
			for (int q2 = 0; q2 < 3; q2++) c->c[q2] = e->col[q2] * f;
		}
		return;
	}
	if (!e->stat && e->cull) {                                      // back-face cull (sums of four unit normals / positions: no square roots needed)
		float ev[3] = { e->eye[0] * 4 - fc[0], e->eye[1] * 4 - fc[1], e->eye[2] * 4 - fc[2] };
		float dt = nf[0] * ev[0] + nf[1] * ev[1] + nf[2] * ev[2];
		float nn = nf[0] * nf[0] + nf[1] * nf[1] + nf[2] * nf[2], ee = ev[0] * ev[0] + ev[1] * ev[1] + ev[2] * ev[2];
		if (dt < 0 && dt * dt > 0.09f * nn * ee) return;
	}
	if (e->stat) { float l = sqrtf(nf[0] * nf[0] + nf[1] * nf[1] + nf[2] * nf[2]); if (l > 1e-9f) { nf[0] /= l; nf[1] /= l; nf[2] /= l; } }
	SV s[4]; Mesh *m;
	if (e->stat) {
		m = &sm[nf[1] > 0.6f ? e->topMat : e->sideMat];
		float t = e->tile > 0 ? e->tile : 2.0f;
		int ax = fabsf(nf[1]) >= fabsf(nf[0]) && fabsf(nf[1]) >= fabsf(nf[2]) ? 1 : fabsf(nf[0]) >= fabsf(nf[2]) ? 0 : 2;
		for (int i = 0; i < 4; i++) {
			float lit[3], c[3], ao = lerpf(0.76f, 1.0f, sat(P[i][1] / 1.6f));
			world_light(N[i], 1.0f, lit);
			for (int k = 0; k < 3; k++) c[k] = sat(e->col[k] * lit[k] * ao);
			float u = ax == 0 ? P[i][2] : P[i][0], v = ax == 1 ? P[i][2] : P[i][1];
			s[i] = sv(P[i][0], P[i][1], P[i][2], u / t, v / t, c, 1);
		}
	} else {
		m = e->dmesh;
		for (int i = 0; i < 4; i++) {
			float lit[3], c[3];
			light_for(e->lm, N[i], lit);
			for (int k = 0; k < 3; k++) c[k] = sat(e->col[k] * lit[k]);
			s[i] = sv(P[i][0], P[i][1], P[i][2], e->tx[0] + uv[i][0] * e->tx[2], e->tx[1] + e->tx[3] * (1.0f - uv[i][1]), c, 1);
		}
	}
	quad(m, &s[0], &s[1], &s[2], &s[3]);
}

// unit-circle / unit-sphere tables (no trig in the per-frame loops)
#define TBL_MAX 12
static float t_cs[TBL_MAX + 1][TBL_MAX + 1][2];      // [slices][j] -> cos, sin of 2*pi*j/slices
static float t_th[TBL_MAX + 1][TBL_MAX + 1][2];      // [stacks][i] -> sin, cos of pi*i/stacks
static bool t_ok[TBL_MAX + 1];
static void tbl(int n)
{
	if (t_ok[n]) return;
	for (int j = 0; j <= n; j++) { t_cs[n][j][0] = cosf(2 * PI_F * j / n); t_cs[n][j][1] = sinf(2 * PI_F * j / n); t_th[n][j][0] = sinf(PI_F * j / n); t_th[n][j][1] = cosf(PI_F * j / n); }
	t_ok[n] = true;
}
static void tess_ell(const EC *e, const float c[3], const float r[3], int sl, int st)
{
	tbl(sl); tbl(st);
	for (int i = 0; i < st; i++) {
		float s0 = t_th[st][i][0], c0 = t_th[st][i][1], s1 = t_th[st][i + 1][0], c1 = t_th[st][i + 1][1];
		for (int j = 0; j < sl; j++) {
			float ca = t_cs[sl][j][0], sa = t_cs[sl][j][1], cb = t_cs[sl][j + 1][0], sb = t_cs[sl][j + 1][1];
			float u[4][3] = { { s0 * ca, c0, s0 * sa }, { s0 * cb, c0, s0 * sb }, { s1 * cb, c1, s1 * sb }, { s1 * ca, c1, s1 * sa } };
			float p[4][3];
			for (int k = 0; k < 4; k++) for (int q = 0; q < 3; q++) p[k][q] = c[q] + u[k][q] * r[q];
			float uvq[4][2] = { { (float)j / sl, (float)i / st }, { (float)(j + 1) / sl, (float)i / st }, { (float)(j + 1) / sl, (float)(i + 1) / st }, { (float)j / sl, (float)(i + 1) / st } };
			if (g_headmap) {                                           // the face texture covers only the front half of the head (the back is plain skin / hidden by the helmet)
				for (int k = 0; k < 4; k++) { float uu = (k == 0 || k == 3) ? (float)j / sl : (float)(j + 1) / sl; uvq[k][0] = clampf((uu - 0.5f) * 2.0f, 0.0f, 1.0f); }
			}
			qd(e, p, u, uvq);                                         // unit-sphere normals: close enough for squashed ellipsoids
		}
	}
}

// cylinder along local z from c.z-hl to c.z+hl; (rax, ray) at the -z end, (rbx, rby) at the +z end
static void tess_cyl_rows(const EC *e, const float c[3], float hl, float rax, float ray, float rbx, float rby, int sl, bool caps, int rows)
{
	static const float uvc[4][2] = { { 0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.5f } };
	tbl(sl);
	for (int r = 0; r < rows; r++) {
		float t0 = (float)r / rows, t1 = (float)(r + 1) / rows;
		float z0 = c[2] - hl + 2 * hl * t0, z1 = c[2] - hl + 2 * hl * t1;
		float ax0 = lerpf(rax, rbx, t0), ay0 = lerpf(ray, rby, t0), ax1 = lerpf(rax, rbx, t1), ay1 = lerpf(ray, rby, t1);
		for (int j = 0; j < sl; j++) {
			float c0 = t_cs[sl][j][0], s0 = t_cs[sl][j][1], c1 = t_cs[sl][j + 1][0], s1 = t_cs[sl][j + 1][1];
			float p[4][3] = {
				{ c[0] + ax0 * c0, c[1] + ay0 * s0, z0 }, { c[0] + ax0 * c1, c[1] + ay0 * s1, z0 },
				{ c[0] + ax1 * c1, c[1] + ay1 * s1, z1 }, { c[0] + ax1 * c0, c[1] + ay1 * s0, z1 } };
			float n[4][3] = { { c0, s0, 0 }, { c1, s1, 0 }, { c1, s1, 0 }, { c0, s0, 0 } };
			float uvq[4][2] = { { (float)j / sl, t0 }, { (float)(j + 1) / sl, t0 }, { (float)(j + 1) / sl, t1 }, { (float)j / sl, t1 } };
			qd(e, p, n, uvq);
			if (caps && r == 0) {
				float q0[4][3] = { { c[0], c[1], z0 }, { p[0][0], p[0][1], p[0][2] }, { p[1][0], p[1][1], p[1][2] }, { c[0], c[1], z0 } };
				static const float nn0[4][3] = { { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 } };
				qd(e, q0, nn0, uvc);
			}
			if (caps && r == rows - 1) {
				float q1[4][3] = { { c[0], c[1], z1 }, { p[3][0], p[3][1], p[3][2] }, { p[2][0], p[2][1], p[2][2] }, { c[0], c[1], z1 } };
				static const float nn1[4][3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
				qd(e, q1, nn1, uvc);
			}
		}
	}
}
static void tess_cyl(const EC *e, const float c[3], float hl, float rax, float ray, float rbx, float rby, int sl, bool caps)
{
	tess_cyl_rows(e, c, hl, rax, ray, rbx, rby, sl, caps, 1);
}

// box with rounded edges and corners: every face is a 3x3 grid whose outer ring is pushed onto a sphere of radius r around the inner box
static void tess_rbox(const EC *e, const float c[3], const float h[3], float r)
{
	float mn = fminf(h[0], fminf(h[1], h[2]));
	if (r > mn * 0.98f) r = mn * 0.98f;
	for (int a = 0; a < 3; a++) for (int sg = -1; sg <= 1; sg += 2) {
		int u = (a + 1) % 3, v = (a + 2) % 3;
		if (e->nobot && a == 1 && sg < 0) continue;
		if (r < 0.002f) {
			float p[4][3], n[4][3] = { { 0 } };
			for (int k = 0; k < 4; k++) {
				float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv_ = (k >= 2) ? 1.0f : -1.0f;
				p[k][a] = c[a] + sg * h[a]; p[k][u] = c[u] + su * h[u]; p[k][v] = c[v] + sv_ * h[v];
				n[k][a] = (float)sg;
			}
			static const float uvp[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			qd(e, p, n, uvp);
			continue;
		}
		float us[4] = { -h[u], -(h[u] - r), h[u] - r, h[u] }, vs[4] = { -h[v], -(h[v] - r), h[v] - r, h[v] };
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
			float p[4][3], n[4][3], uvq[4][2];
			for (int k = 0; k < 4; k++) {
				float U = us[i + ((k == 1 || k == 2) ? 1 : 0)], V = vs[j + ((k >= 2) ? 1 : 0)];
				uvq[k][0] = (U + h[u]) / (2 * h[u]); uvq[k][1] = (V + h[v]) / (2 * h[v]);
				float q[3]; q[a] = sg * h[a]; q[u] = U; q[v] = V;
				float d[3], in[3];
				for (int t = 0; t < 3; t++) { float lim = h[t] - r; in[t] = q[t] < -lim ? -lim : q[t] > lim ? lim : q[t]; d[t] = q[t] - in[t]; }
				float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				if (l > 1e-6f) { for (int t = 0; t < 3; t++) { n[k][t] = d[t] / l; p[k][t] = c[t] + in[t] + n[k][t] * r; } }
				else { for (int t = 0; t < 3; t++) { p[k][t] = c[t] + q[t]; n[k][t] = t == a ? (float)sg : 0; } }
			}
			qd(e, p, n, uvq);
		}
	}
}


// Surface of revolution about the z axis through stations (z, radius) given in increasing z. Two stations at the same z make a step (a flat ring);
// otherwise the radius runs linearly between them, so barrels get collars, gas blocks, tapers and muzzle devices instead of being one tube.
static void tess_lathe(const EC *e, const float c[3], const float *st, int n, int sl, float sy)
{
	tbl(sl);
	for (int k = 0; k + 1 < n; k++) {
		float z0 = st[2 * k], r0 = st[2 * k + 1], z1 = st[2 * k + 2], r1 = st[2 * k + 3];
		float dz = z1 - z0, slope = fabsf(dz) < 1e-5f ? 0.0f : (r1 - r0) / dz;
		bool step = fabsf(dz) < 1e-5f;
		for (int j = 0; j < sl; j++) {
			float c0 = t_cs[sl][j][0], s0 = t_cs[sl][j][1], c1 = t_cs[sl][j + 1][0], s1 = t_cs[sl][j + 1][1];
			float p[4][3] = { { c[0] + r0 * c0, c[1] + r0 * s0 * sy, c[2] + z0 }, { c[0] + r0 * c1, c[1] + r0 * s1 * sy, c[2] + z0 },
			                  { c[0] + r1 * c1, c[1] + r1 * s1 * sy, c[2] + z1 }, { c[0] + r1 * c0, c[1] + r1 * s0 * sy, c[2] + z1 } };
			float n4[4][3];
			float nzv = step ? (r1 > r0 ? -1.0f : 1.0f) : -slope;
			for (int q = 0; q < 4; q++) {
				float cc = (q == 0 || q == 3) ? c0 : c1, ss = (q == 0 || q == 3) ? s0 : s1;
				if (step) { n4[q][0] = 0; n4[q][1] = 0; n4[q][2] = nzv; }
				else { float l = sqrtf(1.0f + slope * slope); n4[q][0] = cc / l; n4[q][1] = ss / l; n4[q][2] = nzv / l; }
			}
			float uvq[4][2] = { { (float)j / sl, (float)k / (n - 1) }, { (float)(j + 1) / sl, (float)k / (n - 1) }, { (float)(j + 1) / sl, (float)(k + 1) / (n - 1) }, { (float)j / sl, (float)(k + 1) / (n - 1) } };
			qd(e, p, n4, uvq);
		}
	}
}

// A side-view outline (z, y pairs, any winding) pushed out to +-hw along x, with bevelled edges: the faces of receivers, grips, magazines and stocks.
// Normals are smoothed across gentle corners and kept sharp across hard ones.
static void tess_extrude(const EC *e, const float c[3], const float *pts, int np, float hw, float bev)
{
	if (np < 3 || np > 24) return;
	float P[24][2], area = 0, g[2] = { 0, 0 };
	for (int i = 0; i < np; i++) { P[i][0] = pts[2 * i]; P[i][1] = pts[2 * i + 1]; g[0] += P[i][0] / np; g[1] += P[i][1] / np; }
	for (int i = 0; i < np; i++) { int j = (i + 1) % np; area += P[i][0] * P[j][1] - P[j][0] * P[i][1]; }
	if (area < 0) for (int i = 0; i < np / 2; i++) { float a0 = P[i][0], a1 = P[i][1]; P[i][0] = P[np - 1 - i][0]; P[i][1] = P[np - 1 - i][1]; P[np - 1 - i][0] = a0; P[np - 1 - i][1] = a1; }
	float zmin = 1e9f, zmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
	for (int i = 0; i < np; i++) { zmin = fminf(zmin, P[i][0]); zmax = fmaxf(zmax, P[i][0]); ymin = fminf(ymin, P[i][1]); ymax = fmaxf(ymax, P[i][1]); }
	float dzr = fmaxf(zmax - zmin, 1e-6f), dyr = fmaxf(ymax - ymin, 1e-6f);
	float en[24][2], vn[24][2], B[24][2];                       // edge normals (edge i: i -> i+1), averaged vertex normals, inset points
	for (int i = 0; i < np; i++) {
		int j = (i + 1) % np; float dz = P[j][0] - P[i][0], dy = P[j][1] - P[i][1], l = sqrtf(dz * dz + dy * dy) + 1e-9f;
		en[i][0] = dy / l; en[i][1] = -dz / l;                  // (nz, ny), outward for counter-clockwise outlines
	}
	for (int i = 0; i < np; i++) {
		int h = (i + np - 1) % np;
		float sz = en[h][0] + en[i][0], sy2 = en[h][1] + en[i][1], l = sqrtf(sz * sz + sy2 * sy2) + 1e-9f;
		vn[i][0] = sz / l; vn[i][1] = sy2 / l;
		float k = 1.0f / fmaxf(0.35f, vn[i][0] * en[i][0] + vn[i][1] * en[i][1]);          // miter: inset by bev along each edge, not along the averaged normal
		B[i][0] = P[i][0] - vn[i][0] * bev * k; B[i][1] = P[i][1] - vn[i][1] * bev * k;
	}
	float xa = hw - bev; if (xa < 0) xa = 0;
	for (int sgn = -1; sgn <= 1; sgn += 2) {
		for (int i = 0; i < np; i++) {
			int j = (i + 1) % np, h = (i + np - 1) % np, k2 = (j + 1) % np;
			bool smi = en[h][0] * en[i][0] + en[h][1] * en[i][1] > 0.6f, smj = en[i][0] * en[k2][0] + en[i][1] * en[k2][1] > 0.6f;
			float ni[2] = { smi ? vn[i][0] : en[i][0], smi ? vn[i][1] : en[i][1] }, nj[2] = { smj ? vn[j][0] : en[i][0], smj ? vn[j][1] : en[i][1] };
			if (sgn < 0) {                                                  // the wall between the two bevels: once per edge
				float p[4][3] = { { c[0] - xa, c[1] + P[i][1], c[2] + P[i][0] }, { c[0] + xa, c[1] + P[i][1], c[2] + P[i][0] }, { c[0] + xa, c[1] + P[j][1], c[2] + P[j][0] }, { c[0] - xa, c[1] + P[j][1], c[2] + P[j][0] } };
				float n4[4][3] = { { 0, ni[1], ni[0] }, { 0, ni[1], ni[0] }, { 0, nj[1], nj[0] }, { 0, nj[1], nj[0] } };
				float uvq[4][2] = { { (P[i][0] - zmin) / dzr, (P[i][1] - ymin) / dyr }, { (P[i][0] - zmin) / dzr, (P[i][1] - ymin) / dyr }, { (P[j][0] - zmin) / dzr, (P[j][1] - ymin) / dyr }, { (P[j][0] - zmin) / dzr, (P[j][1] - ymin) / dyr } };
				qd(e, p, n4, uvq);
			}
			float xs = sgn * hw, xc = sgn * xa;
			float pb[4][3] = { { c[0] + xc, c[1] + P[i][1], c[2] + P[i][0] }, { c[0] + xc, c[1] + P[j][1], c[2] + P[j][0] }, { c[0] + xs, c[1] + B[j][1], c[2] + B[j][0] }, { c[0] + xs, c[1] + B[i][1], c[2] + B[i][0] } };
			float nb[4][3] = { { sgn * 0.7f, ni[1] * 0.7f, ni[0] * 0.7f }, { sgn * 0.7f, nj[1] * 0.7f, nj[0] * 0.7f }, { sgn * 0.7f, nj[1] * 0.7f, nj[0] * 0.7f }, { sgn * 0.7f, ni[1] * 0.7f, ni[0] * 0.7f } };
			float ub[4][2] = { { (P[i][0] - zmin) / dzr, (P[i][1] - ymin) / dyr }, { (P[j][0] - zmin) / dzr, (P[j][1] - ymin) / dyr }, { (B[j][0] - zmin) / dzr, (B[j][1] - ymin) / dyr }, { (B[i][0] - zmin) / dzr, (B[i][1] - ymin) / dyr } };
			qd(e, pb, nb, ub);
			float pc[4][3] = { { c[0] + xs, c[1] + g[1], c[2] + g[0] }, { c[0] + xs, c[1] + B[i][1], c[2] + B[i][0] }, { c[0] + xs, c[1] + B[j][1], c[2] + B[j][0] }, { c[0] + xs, c[1] + g[1], c[2] + g[0] } };
			float nc[4][3] = { { (float)sgn, 0, 0 }, { (float)sgn, 0, 0 }, { (float)sgn, 0, 0 }, { (float)sgn, 0, 0 } };
			float uc[4][2] = { { (g[0] - zmin) / dzr, (g[1] - ymin) / dyr }, { (B[i][0] - zmin) / dzr, (B[i][1] - ymin) / dyr }, { (B[j][0] - zmin) / dzr, (B[j][1] - ymin) / dyr }, { (g[0] - zmin) / dzr, (g[1] - ymin) / dyr } };
			qd(e, pc, nc, uc);
		}
	}
}

static M m_basis_z(const float from[3], const float to[3])         // frame at `from` whose z axis points to `to`
{
	float z[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
	vnorm(z);
	float up[3] = { 0, 1, 0 };
	if (fabsf(z[1]) > 0.95f) { up[0] = 1; up[1] = 0; }
	float x[3] = { up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0] };
	vnorm(x);
	float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
	M r = {{ x[0], y[0], z[0], from[0], x[1], y[1], z[1], from[1], x[2], y[2], z[2], from[2] }};
	return r;
}

// ================================================================================================================
// STATIC WORLD
// ================================================================================================================

static void shade3(const float base[3], const float lit[3], float mul, float o[3])
{
	for (int i = 0; i < 3; i++) o[i] = sat(base[i] * lit[i] * mul);
}

static void emit_shape_static(const Box *b)
{
	float c[3] = { (b->x0 + b->x1) * 0.5f, (b->y0 + b->y1) * 0.5f, (b->z0 + b->z1) * 0.5f };
	float h[3] = { (b->x1 - b->x0) * 0.5f, (b->y1 - b->y0) * 0.5f, (b->z1 - b->z0) * 0.5f };
	EC e = { true, NULL, NULL, { b->r, b->g, b->b }, LM_WORLD, NULL, false, b->side, b->top, b->tile, b->y0 < 0.05f, false, { 0, 0, 0, 0 } };
	M T;
	float hm = fmaxf(h[0], fmaxf(h[1], h[2])), rmax = fmaxf(h[0], h[2]);
	int csl = rmax < 0.3f ? 6 : 10, esl = hm < 0.4f ? 6 : 8, est = hm < 0.4f ? 3 : 5;
	switch (b->shape) {
	case 1: T = m_id(); e.T = &T; tess_rbox(&e, c, h, b->bev < 0.12f ? 0.0f : b->bev); break;
	case 2: T = m_mul(m_tr(c[0], c[1], c[2]), m_rx(-PI_F * 0.5f)); e.T = &T; { float z[3] = { 0, 0, 0 }; tess_cyl(&e, z, h[1], h[0], h[2], h[0], h[2], csl, true); } break;
	case 3: T = m_mul(m_tr(c[0], c[1], c[2]), m_ry(PI_F * 0.5f)); e.T = &T; { float z[3] = { 0, 0, 0 }; tess_cyl(&e, z, h[0], h[2], h[1], h[2], h[1], csl, true); } break;
	case 4: T = m_tr(c[0], c[1], c[2]); e.T = &T; { float z[3] = { 0, 0, 0 }; tess_cyl(&e, z, h[2], h[0], h[1], h[0], h[1], csl, true); } break;
	default: T = m_tr(c[0], c[1], c[2]); e.T = &T; { float z[3] = { 0, 0, 0 }; tess_ell(&e, z, h, esl, est); } break;
	}
}

static void emit_box_static(const Box *b)
{
	if (b->shape || b->bev > 0) { if (b->shape == 0) { Box t = *b; t.shape = 1; emit_shape_static(&t); } else emit_shape_static(b); return; }
	const float base[3] = { b->r, b->g, b->b };
	float t = b->tile > 0 ? b->tile : 2.0f;
	bool win = b->side == MAT_WINDOW;
	// four walls: normal, along-axis; corners listed bottom-left, bottom-right, top-right, top-left as seen from outside
	for (int f = 0; f < 4; f++) {
		float n[3] = { 0, 0, 0 }, lit[3];
		float ax, bx, az, bz;        // the face's two horizontal end points
		float u0, u1;
		switch (f) {
		case 0: n[2] = -1; ax = b->x0; az = b->z0; bx = b->x1; bz = b->z0; u0 = b->x0 / t; u1 = b->x1 / t; break;
		case 1: n[2] =  1; ax = b->x1; az = b->z1; bx = b->x0; bz = b->z1; u0 = -b->x1 / t; u1 = -b->x0 / t; break;
		case 2: n[0] = -1; ax = b->x0; az = b->z1; bx = b->x0; bz = b->z0; u0 = -b->z1 / t; u1 = -b->z0 / t; break;
		default: n[0] = 1; ax = b->x1; az = b->z0; bx = b->x1; bz = b->z1; u0 = b->z0 / t; u1 = b->z1 / t; break;
		}
		if (b->y1 - b->y0 < 0.03f) continue;                        // flat decals: top only
		float sunvis = 1.0f;
		{ float sd[3]; scene_sun_dir(sd); float nd = n[0] * sd[0] + n[2] * sd[2];
		  float th = fminf(b->x1 - b->x0, b->z1 - b->z0);
		  if (!b->solid && th < 0.25f && nd > 0.02f) { float pt[3] = { (b->x0 + b->x1) * 0.5f + n[0] * 0.15f, (b->y0 + b->y1) * 0.5f, (b->z0 + b->z1) * 0.5f + n[2] * 0.15f }; sunvis = sun_vis3(pt, NULL); }
		  if (b->solid && b->y1 - b->y0 > 3.0f && nd > 0.05f && b->side != MAT_WINDOW) {        // a big wall facing the sun: shade it on a grid (neighbour shadows + the shadow under the roof overhang)
			int cols = (int)ceilf(sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az)) / 1.5f), rows = (int)ceilf((b->y1 - b->y0) / 1.5f);
			if (cols < 1) cols = 1;
			if (rows < 1) rows = 1;
			float len = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
			for (int rr = 0; rr <= rows; rr++) for (int cc = 0; cc <= cols; cc++) { (void)len; }
			static float gcol[16][64][3];
			if (cols <= 63 && rows <= 15) {
				for (int rr = 0; rr <= rows; rr++) for (int cc = 0; cc <= cols; cc++) {
					float fu = (float)cc / cols, fv = (float)rr / rows, y = b->y0 + (b->y1 - b->y0) * fv;
					float pt[3] = { ax + (bx - ax) * fu + n[0] * 0.12f, y, az + (bz - az) * fu + n[2] * 0.12f };
					float vis = sun_vis3(pt, b);
					float over = sat((y - (b->y1 - 1.6f)) / 1.6f);                       // the roof slab shades the top of the wall
					vis *= 1.0f - 0.75f * over;
					float li[3]; world_light(n, vis, li);
					float ao = (b->y0 < 0.1f ? lerpf(0.72f, 1.0f, sat(y / 2.0f)) : 1.0f) * (1.0f - 0.18f * over);
					for (int k = 0; k < 3; k++) gcol[rr][cc][k] = sat(base[k] * li[k] * ao);
				}
				for (int rr = 0; rr < rows; rr++) for (int cc = 0; cc < cols; cc++) {
					float f0 = (float)cc / cols, f1 = (float)(cc + 1) / cols, g0 = (float)rr / rows, g1 = (float)(rr + 1) / rows;
					float y0 = b->y0 + (b->y1 - b->y0) * g0, y1 = b->y0 + (b->y1 - b->y0) * g1;
					SV q0 = sv(ax + (bx - ax) * f0, y0, az + (bz - az) * f0, u0 + (u1 - u0) * f0, y0 / t, gcol[rr][cc], 1), q1 = sv(ax + (bx - ax) * f1, y0, az + (bz - az) * f1, u0 + (u1 - u0) * f1, y0 / t, gcol[rr][cc + 1], 1);
					SV q2 = sv(ax + (bx - ax) * f1, y1, az + (bz - az) * f1, u0 + (u1 - u0) * f1, y1 / t, gcol[rr + 1][cc + 1], 1), q3 = sv(ax + (bx - ax) * f0, y1, az + (bz - az) * f0, u0 + (u1 - u0) * f0, y1 / t, gcol[rr + 1][cc], 1);
					quad(&sm[b->side], &q0, &q1, &q2, &q3);
				}
				continue;
			}
		  }
		}
		world_light(n, sunvis, lit);
		float v0 = b->y0 / t, v1 = b->y1 / t;
		if (win) { u0 = 0; u1 = 1; v0 = 0; v1 = 1; }
		float ao0 = b->y0 < 0.1f ? 0.72f : 0.9f, ao1 = lerpf(0.72f, 1.0f, sat(b->y1 / 3.0f));
		if (b->y1 - b->y0 < 1.5f) ao1 = lerpf(ao0, 1.0f, 0.6f);
		float c0[3], c1[3];
		shade3(base, lit, ao0, c0); shade3(base, lit, ao1, c1);
		SV p0 = sv(ax, b->y0, az, u0, v0, c0, 1), p1 = sv(bx, b->y0, bz, u1, v0, c0, 1), p2 = sv(bx, b->y1, bz, u1, v1, c1, 1), p3 = sv(ax, b->y1, az, u0, v1, c1, 1);
		quad(&sm[b->side], &p0, &p1, &p2, &p3);
	}
	// top
	{
		float n[3] = { 0, 1, 0 }, lit[3], c[3];
		world_light(n, 1.0f, lit);
		shade3(base, lit, b->top == MAT_ROOF ? 0.95f : 1.0f, c);
		float u0 = b->x0 / t, u1 = b->x1 / t, v0 = b->z0 / t, v1 = b->z1 / t;
		if (b->top == MAT_WINDOW) { u0 = 0; u1 = 1; v0 = 0; v1 = 1; }
		SV p0 = sv(b->x0, b->y1, b->z0, u0, v0, c, 1), p1 = sv(b->x1, b->y1, b->z0, u1, v0, c, 1), p2 = sv(b->x1, b->y1, b->z1, u1, v1, c, 1), p3 = sv(b->x0, b->y1, b->z1, u0, v1, c, 1);
		quad(&sm[b->top], &p0, &p1, &p2, &p3);
	}
	// underside of raised slabs (roof overhangs)
	if (b->y0 > 1.0f && !b->solid) {
		float n[3] = { 0, -1, 0 }, lit[3], c[3];
		world_light(n, 0, lit);
		shade3(base, lit, 0.8f, c);
		SV p0 = sv(b->x0, b->y0, b->z0, 0, 0, c, 1), p1 = sv(b->x1, b->y0, b->z0, 1, 0, c, 1), p2 = sv(b->x1, b->y0, b->z1, 1, 1, c, 1), p3 = sv(b->x0, b->y0, b->z1, 0, 1, c, 1);
		quad(&sm[b->side], &p0, &p3, &p2, &p1);
	}
}

// ---- ground ------------------------------------------------------------------------------------------------------------
static int s_cast[MAX_BOX], s_ncast;                      // boxes that cast sun shadows: solids, and tree canopies
static void build_casters(void)
{
	s_ncast = 0;
	for (int i = 0; i < SG->w.nbox; i++) { const Box *b = &SG->w.box[i]; if (b->solid || (b->shape == 5 && b->y0 > 1.5f)) s_cast[s_ncast++] = i; }
}
// 1 when the sun reaches point p (a box passed as `self` is ignored), 0 when something is in the way
static float sun_vis3(const float p[3], const Box *self)
{
	float s[3]; scene_sun_dir(s);
	for (int i = 0; i < s_ncast; i++) {
		const Box *b = &SG->w.box[s_cast[i]];
		if (b == self) continue;
		float lo[3] = { b->x0, b->y0, b->z0 }, hi[3] = { b->x1, b->y1, b->z1 };
		if (b->shape == 5) for (int a = 0; a < 3; a++) { float c = (lo[a] + hi[a]) * 0.5f, h = (hi[a] - lo[a]) * 0.5f * 0.8f; lo[a] = c - h; hi[a] = c + h; }   // canopies are round: shrink the box
		float tmin = 0, tmax = 1e9f; bool ok = true;
		for (int a = 0; a < 3 && ok; a++) {
			if (fabsf(s[a]) < 1e-6f) { if (p[a] < lo[a] || p[a] > hi[a]) ok = false; continue; }
			float t1 = (lo[a] - p[a]) / s[a], t2 = (hi[a] - p[a]) / s[a];
			if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
			if (t1 > tmin) tmin = t1;
			if (t2 < tmax) tmax = t2;
			if (tmin > tmax) ok = false;
		}
		if (ok && tmax > 0.01f) return 0.0f;
	}
	return 1.0f;
}
static bool sun_visible(float x, float z) { float p[3] = { x, 0.03f, z }; return sun_vis3(p, NULL) > 0.5f; }
static float ao_at(float x, float z)
{
	float best = 1.0f;
	for (int i = 0; i < SG->w.nsolid; i++) {
		const Box *b = &SG->w.box[SG->w.solid_idx[i]];
		if (b->y1 < 0.7f) continue;
		float cx = x < b->x0 ? b->x0 : x > b->x1 ? b->x1 : x, cz = z < b->z0 ? b->z0 : z > b->z1 ? b->z1 : z;
		float d = sqrtf((x - cx) * (x - cx) + (z - cz) * (z - cz));
		float a = lerpf(0.7f, 1.0f, sat(d / 1.1f));
		if (a < best) best = a;
	}
	return best;
}
static int ground_mat(float x, float z) { return map_ground(x, z); }
static float ground_tile(int m) { return m == MAT_DECK ? 2.0f : m == MAT_GRASS ? 3.0f : m == MAT_CONCRETE ? 3.0f : m == MAT_SAND ? 6.0f : 4.0f; }

typedef struct { float c[3]; } GC;
static GC gcache[2 * ARENA_Z + 1][2 * ARENA_X + 1];     // lighting at every integer lattice point (the ground mesh only ever samples those)
static bool gcache_ok;
static GC ground_colour(float x, float z)
{
	int ix = (int)floorf(x + ARENA_X + 0.5f), iz = (int)floorf(z + ARENA_Z + 0.5f);
	if (!gcache_ok) {
		static const float up[3] = { 0, 1, 0 };
		for (int j = 0; j <= 2 * ARENA_Z; j++) for (int i = 0; i <= 2 * ARENA_X; i++) {
			float px = (float)(i - ARENA_X), pz = (float)(j - ARENA_Z), lit[3];
			world_light(up, sun_visible(px, pz) ? 1.0f : 0.0f, lit);
			float ao = ao_at(px, pz);
			for (int k = 0; k < 3; k++) gcache[j][i].c[k] = sat(lit[k] * ao);
		}
		gcache_ok = true;
	}
	return gcache[iz][ix];
}
static void ground_quad(float x0, float z0, float x1, float z1, int mat, const GC c[4])
{
	float t = ground_tile(mat);
	SV a = sv(x0, 0, z0, x0 / t, z0 / t, c[0].c, 1), b = sv(x1, 0, z0, x1 / t, z0 / t, c[1].c, 1), cc = sv(x1, 0, z1, x1 / t, z1 / t, c[2].c, 1), d = sv(x0, 0, z1, x0 / t, z1 / t, c[3].c, 1);
	quad(&sm[mat], &a, &b, &cc, &d);
}
static void build_ground(void)
{
	// 2 m macro cells; split into 1 m cells wherever the lighting or the material changes inside
	for (int cz = -ARENA_Z; cz < ARENA_Z; cz += 2)
		for (int cx = -ARENA_X; cx < ARENA_X; cx += 2) {
			float x0 = (float)cx, z0 = (float)cz, x1 = x0 + 2, z1 = z0 + 2;
			int m00 = ground_mat(x0 + 0.5f, z0 + 0.5f), m10 = ground_mat(x0 + 1.5f, z0 + 0.5f), m01 = ground_mat(x0 + 0.5f, z0 + 1.5f), m11 = ground_mat(x0 + 1.5f, z0 + 1.5f);
			GC g[3][3];
			for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) g[j][i] = ground_colour(x0 + i, z0 + j);
			bool flat = m00 == m10 && m00 == m01 && m00 == m11;
			if (flat) {
				for (int j = 0; j < 3 && flat; j++) for (int i = 0; i < 3 && flat; i++) {
					// compare with bilinear interpolation of the four corners
					float u = i * 0.5f, v = j * 0.5f;
					for (int k = 0; k < 3; k++) {
						float e = lerpf(lerpf(g[0][0].c[k], g[0][2].c[k], u), lerpf(g[2][0].c[k], g[2][2].c[k], u), v);
						if (fabsf(e - g[j][i].c[k]) > 0.06f) flat = false;
					}
				}
			}
			if (flat) {
				GC c[4] = { g[0][0], g[0][2], g[2][2], g[2][0] };
				ground_quad(x0, z0, x1, z1, m00, c);
			} else {
				for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
					float fx = x0 + i, fz = z0 + j;
					int q[4] = { ground_mat(fx + 0.25f, fz + 0.25f), ground_mat(fx + 0.75f, fz + 0.25f), ground_mat(fx + 0.75f, fz + 0.75f), ground_mat(fx + 0.25f, fz + 0.75f) };
					GC c[4] = { g[j][i], g[j][i + 1], g[j + 1][i + 1], g[j + 1][i] };
					if (q[0] == q[1] && q[0] == q[2] && q[0] == q[3]) { ground_quad(fx, fz, fx + 1, fz + 1, q[0], c); continue; }
					for (int b = 0; b < 2; b++) for (int a = 0; a < 2; a++) {       // an edge between materials runs through this cell: half-metre quads
						GC h[4];
						static const float cu[4] = { 0, 0.5f, 0.5f, 0 }, cv[4] = { 0, 0, 0.5f, 0.5f };
						for (int k = 0; k < 4; k++) {
							float u = a * 0.5f + cu[k], v = b * 0.5f + cv[k];
							for (int ch = 0; ch < 3; ch++) h[k].c[ch] = lerpf(lerpf(c[0].c[ch], c[1].c[ch], u), lerpf(c[3].c[ch], c[2].c[ch], u), v);
						}
						ground_quad(fx + a * 0.5f, fz + b * 0.5f, fx + a * 0.5f + 0.5f, fz + b * 0.5f + 0.5f, q[b * 2 + (b ? 1 - a : a)], h);
					}
				}
			}
		}
}

static void build_sea(void)
{
	// concentric rings of desert sand, fading to the horizon haze with distance
	static const float R[] = { 0, 70, 220, 600, 1300 };
	static const float haze[3] = { 0.92f, 0.70f, 0.58f };
	const float y = -0.02f, tile = 36.0f;
	for (int k = 0; k < 4; k++) {
		float r0x = k == 0 ? ARENA_X + 0.4f : R[k], r0z = k == 0 ? ARENA_Z + 0.4f : R[k] * 0.72f;
		float r1x = R[k + 1], r1z = R[k + 1] * 0.72f;
		if (k == 0) { r1x = R[1] * 1.2f; r1z = R[1] * 0.9f; }
		if (k == 1) { r0x = R[1] * 1.2f; r0z = R[1] * 0.9f; r1x = R[2]; r1z = R[2] * 0.72f; }
		float f0 = k == 0 ? 0.0f : sat((r0x - 60) / 900.0f), f1 = sat((r1x - 60) / 900.0f);
		float lit[3]; static const float up[3] = { 0, 1, 0 }; world_light(up, 0.8f, lit);
		float c0[3], c1[3];
		for (int i = 0; i < 3; i++) { c0[i] = sat(lit[i]); c1[i] = c0[i]; c0[i] = lerpf(c0[i], haze[i], sqrtf(f0) * 0.9f); c1[i] = lerpf(c1[i], haze[i], sqrtf(f1) * 0.9f); }
		// four trapezoids around the inner rectangle: south, north, west, east
		struct { float ix0, iz0, ix1, iz1, ox0, oz0, ox1, oz1; } q[4] = {
			{ -r0x,  r0z,  r0x,  r0z, -r1x,  r1z,  r1x,  r1z },
			{  r0x, -r0z, -r0x, -r0z,  r1x, -r1z, -r1x, -r1z },
			{ -r0x, -r0z, -r0x,  r0z, -r1x, -r1z, -r1x,  r1z },
			{  r0x,  r0z,  r0x, -r0z,  r1x,  r1z,  r1x, -r1z },
		};
		for (int s = 0; s < 4; s++) {
			SV a = sv(q[s].ix0, y, q[s].iz0, q[s].ix0 / tile, q[s].iz0 / tile, c0, 1), b = sv(q[s].ix1, y, q[s].iz1, q[s].ix1 / tile, q[s].iz1 / tile, c0, 1);
			SV cc = sv(q[s].ox1, y, q[s].oz1, q[s].ox1 / tile, q[s].oz1 / tile, c1, 1), d = sv(q[s].ox0, y, q[s].oz0, q[s].ox0 / tile, q[s].oz0 / tile, c1, 1);
			quad(&sm[MAT_SAND], &a, &b, &cc, &d);
		}
	}
}

static void build_sky(void)
{
	static const float el[] = { -18, 0, 5, 12, 22, 36, 54, 75, 90 };
	const int NE = 9, NS = 32;
	const float R = 900.0f;
	float S[3]; scene_sun_dir(S);
	static float col[9][32][3], pos[9][32][3];
	for (int i = 0; i < NE; i++) for (int j = 0; j < NS; j++) {
		float e = el[i] * PI_F / 180.0f, a = j * 2 * PI_F / NS;
		float d[3] = { sinf(a) * cosf(e), sinf(e), -cosf(a) * cosf(e) };
		for (int k = 0; k < 3; k++) pos[i][j][k] = d[k] * R;
		float ee = sat(el[i] / 90.0f);
		static const float hor[3] = { 0.96f, 0.76f, 0.60f }, mid[3] = { 0.62f, 0.72f, 0.88f }, top[3] = { 0.24f, 0.42f, 0.72f };
		float c[3];
		float t = sat(el[i] / 30.0f), t2 = sat((el[i] - 15) / 75.0f);
		for (int k = 0; k < 3; k++) c[k] = el[i] <= 0 ? hor[k] : lerpf(lerpf(hor[k], mid[k], sqrtf(t)), top[k], t2);
		(void)ee;
		float s = d[0] * S[0] + d[1] * S[1] + d[2] * S[2]; if (s < 0) s = 0;
		float g1 = powf(s, 6.0f), g2 = powf(s, 40.0f);
		c[0] += g1 * 0.55f + g2 * 0.5f; c[1] += g1 * 0.28f + g2 * 0.42f; c[2] += g1 * 0.08f + g2 * 0.25f;
		for (int k = 0; k < 3; k++) col[i][j][k] = sat(c[k]);
	}
	for (int i = 0; i + 1 < NE; i++) for (int j = 0; j < NS; j++) {
		int j2 = (j + 1) % NS;
		SV a = sv(pos[i][j][0], pos[i][j][1], pos[i][j][2], 0.5f, 0.5f, col[i][j], 1), b = sv(pos[i][j2][0], pos[i][j2][1], pos[i][j2][2], 0.5f, 0.5f, col[i][j2], 1);
		SV c = sv(pos[i + 1][j2][0], pos[i + 1][j2][1], pos[i + 1][j2][2], 0.5f, 0.5f, col[i + 1][j2], 1), d = sv(pos[i + 1][j][0], pos[i + 1][j][1], pos[i + 1][j][2], 0.5f, 0.5f, col[i + 1][j], 1);
		quad(&sm[GRP_SKY], &a, &b, &c, &d);
	}
	// clouds: long soft streaks, warm on the sun side
	for (int i = 0; i < 12; i++) {
		float a = i * 2 * PI_F / 12 + (i * 37 % 7) * 0.08f, e = DEG(10 + (i * 5 % 4) * 7 + (i % 2) * 6);
		float d[3] = { sinf(a) * cosf(e), sinf(e), -cosf(a) * cosf(e) };
		float up[3] = { 0, 1, 0 }, r[3] = { up[1] * d[2] - up[2] * d[1], up[2] * d[0] - up[0] * d[2], up[0] * d[1] - up[1] * d[0] };
		float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); for (int k = 0; k < 3; k++) r[k] /= rl;
		float u[3] = { d[1] * r[2] - d[2] * r[1], d[2] * r[0] - d[0] * r[2], d[0] * r[1] - d[1] * r[0] };
		float w = 320.0f + (i * 53 % 5) * 70.0f, h = 46.0f + (i * 29 % 4) * 14.0f, rr = 800.0f;
		float s = d[0] * S[0] + d[1] * S[1] + d[2] * S[2]; s = sat(s); s = s * s;
		float c[3] = { lerpf(0.78f, 1.0f, s), lerpf(0.76f, 0.62f, s), lerpf(0.86f, 0.46f, s) };
		float P[4][3];
		for (int k = 0; k < 4; k++) { float sx = (k == 0 || k == 3) ? -1.0f : 1.0f, sy = (k < 2) ? -1.0f : 1.0f; for (int q = 0; q < 3; q++) P[k][q] = d[q] * rr + r[q] * w * sx + u[q] * h * sy; }
		SV a0 = sv(P[0][0], P[0][1], P[0][2], 0, 0, c, 0.92f), b0 = sv(P[1][0], P[1][1], P[1][2], 1, 0, c, 0.92f), c0 = sv(P[2][0], P[2][1], P[2][2], 1, 1, c, 0.92f), d0 = sv(P[3][0], P[3][1], P[3][2], 0, 1, c, 0.92f);
		quad(&sm[GRP_CLOUD], &a0, &b0, &c0, &d0);
	}
	// sun glow
	{
		float up[3] = { 0, 1, 0 }, r[3] = { up[1] * S[2] - up[2] * S[1], up[2] * S[0] - up[0] * S[2], up[0] * S[1] - up[1] * S[0] };
		float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); for (int k = 0; k < 3; k++) r[k] /= rl;
		float u[3] = { S[1] * r[2] - S[2] * r[1], S[2] * r[0] - S[0] * r[2], S[0] * r[1] - S[1] * r[0] };
		static const struct { float size; float c[3]; float a; } layers[2] = { { 300.0f, { 1.0f, 0.62f, 0.3f }, 0.55f }, { 90.0f, { 1.0f, 0.95f, 0.8f }, 1.0f } };
		for (int l = 0; l < 2; l++) {
			float P[4][3];
			for (int k = 0; k < 4; k++) { float sx = (k == 0 || k == 3) ? -1.0f : 1.0f, sy = (k < 2) ? -1.0f : 1.0f; for (int q = 0; q < 3; q++) P[k][q] = S[q] * 850.0f + r[q] * layers[l].size * sx + u[q] * layers[l].size * sy; }
			SV a0 = sv(P[0][0], P[0][1], P[0][2], 0, 0, layers[l].c, layers[l].a), b0 = sv(P[1][0], P[1][1], P[1][2], 1, 0, layers[l].c, layers[l].a), c0 = sv(P[2][0], P[2][1], P[2][2], 1, 1, layers[l].c, layers[l].a), d0 = sv(P[3][0], P[3][1], P[3][2], 0, 1, layers[l].c, layers[l].a);
			quad(&sm[GRP_SUN], &a0, &b0, &c0, &d0);
		}
	}
}

static void build_static_pass(void)
{
	build_sky();
	build_sea();
	build_ground();
	for (int i = 0; i < SG->w.nbox; i++) emit_box_static(&SG->w.box[i]);
}

int scene_build_static(const Game *g, Scene *s, Vtx *buf)
{
	SG = g;
	if (!buf) gcache_ok = false;
	build_casters();
	for (int i = 0; i < GRP_COUNT; i++) { sm[i].v = NULL; sm[i].n = 0; sm[i].cap = 0x7FFFFFFF; }
	build_static_pass();
	int total = 0, counts[GRP_COUNT];
	for (int i = 0; i < GRP_COUNT; i++) { counts[i] = sm[i].n; total += sm[i].n; }
	if (!buf) return total;
	int off = 0;
	for (int i = 0; i < GRP_COUNT; i++) { s->sfirst[i] = off; s->scount[i] = counts[i]; sm[i].v = buf + off; sm[i].n = 0; sm[i].cap = counts[i]; off += counts[i]; }
	build_static_pass();
	s->stat = buf;
	return total;
}

// ================================================================================================================
// DYNAMIC
// ================================================================================================================
static Mesh dm[D_COUNT];
static float EYE[3];
static float CR[3], CU[3];                 // camera right/up (billboards)

// oriented box: M maps the box's local frame to output space; c = centre in local space, h = half sizes; only faces turned to `eye` are emitted
static void obox(Mesh *m, const M *T, const float c[3], const float h[3], const float col[3], LightMode lm, const float eye[3])
{
	float P[8][3];
	for (int i = 0; i < 8; i++) {
		float l[3] = { c[0] + ((i & 1) ? h[0] : -h[0]), c[1] + ((i & 2) ? h[1] : -h[1]), c[2] + ((i & 4) ? h[2] : -h[2]) };
		m_pt(T, l, P[i]);
	}
	static const int F[6][4] = { { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 } };
	static const float N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	for (int f = 0; f < 6; f++) {
		float n[3]; m_dir(T, N[f], n);
		float fc[3] = { (P[F[f][0]][0] + P[F[f][2]][0]) * 0.5f, (P[F[f][0]][1] + P[F[f][2]][1]) * 0.5f, (P[F[f][0]][2] + P[F[f][2]][2]) * 0.5f };
		if (n[0] * (eye[0] - fc[0]) + n[1] * (eye[1] - fc[1]) + n[2] * (eye[2] - fc[2]) <= 0) continue;
		float lit[3], cc[3]; light_for(lm, n, lit);
		for (int k = 0; k < 3; k++) cc[k] = sat(col[k] * lit[k]);
		const float wu = 0.008f, wv = 0.99f;                                // inside the white patch of the atlas
		SV a = sv(P[F[f][0]][0], P[F[f][0]][1], P[F[f][0]][2], wu, wv, cc, 1), b = sv(P[F[f][1]][0], P[F[f][1]][1], P[F[f][1]][2], wu, wv, cc, 1);
		SV c2 = sv(P[F[f][2]][0], P[F[f][2]][1], P[F[f][2]][2], wu, wv, cc, 1), d = sv(P[F[f][3]][0], P[F[f][3]][1], P[F[f][3]][2], wu, wv, cc, 1);
		quad(m, &a, &b, &c2, &d);
	}
}

// camera-facing quad (glow texture): centre, half size, rotation, colour, alpha
static void bb(Mesh *m, const float c[3], float size, float rot, const float col[3], float a, const float R[3], const float U[3])
{
	float cr = cosf(rot) * size, sr = sinf(rot) * size;
	float rx[3], ux[3];
	for (int k = 0; k < 3; k++) { rx[k] = R[k] * cr + U[k] * sr; ux[k] = -R[k] * sr + U[k] * cr; }
	float P[4][3];
	for (int k = 0; k < 3; k++) {
		P[0][k] = c[k] - rx[k] - ux[k]; P[1][k] = c[k] + rx[k] - ux[k]; P[2][k] = c[k] + rx[k] + ux[k]; P[3][k] = c[k] - rx[k] + ux[k];
	}
	SV s0 = sv(P[0][0], P[0][1], P[0][2], 0, 0, col, a), s1 = sv(P[1][0], P[1][1], P[1][2], 1, 0, col, a), s2 = sv(P[2][0], P[2][1], P[2][2], 1, 1, col, a), s3 = sv(P[3][0], P[3][1], P[3][2], 0, 1, col, a);
	quad(m, &s0, &s1, &s2, &s3);
}
// flat quad lying on the ground
static void flat(Mesh *m, float x, float y, float z, float size, const float col[3], float a)
{
	SV s0 = sv(x - size, y, z - size, 0, 0, col, a), s1 = sv(x + size, y, z - size, 1, 0, col, a), s2 = sv(x + size, y, z + size, 1, 1, col, a), s3 = sv(x - size, y, z + size, 0, 1, col, a);
	quad(m, &s0, &s1, &s2, &s3);
}
// streak between two points, camera facing
static void streak(Mesh *m, const float a[3], const float b[3], float width, const float col[3], float alpha)
{
	float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	float mid[3] = { (a[0] + b[0]) * 0.5f - EYE[0], (a[1] + b[1]) * 0.5f - EYE[1], (a[2] + b[2]) * 0.5f - EYE[2] };
	float s[3] = { d[1] * mid[2] - d[2] * mid[1], d[2] * mid[0] - d[0] * mid[2], d[0] * mid[1] - d[1] * mid[0] };
	float l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (l < 1e-6f) return;
	for (int k = 0; k < 3; k++) s[k] *= width / l;
	SV p0 = sv(a[0] - s[0], a[1] - s[1], a[2] - s[2], 0, 0.5f, col, alpha), p1 = sv(a[0] + s[0], a[1] + s[1], a[2] + s[2], 1, 0.5f, col, alpha);
	SV p2 = sv(b[0] + s[0], b[1] + s[1], b[2] + s[2], 1, 0.5f, col, alpha), p3 = sv(b[0] - s[0], b[1] - s[1], b[2] - s[2], 0, 0.5f, col, alpha);
	quad(m, &p0, &p1, &p2, &p3);
}

// ---- soldiers: smooth ellipsoid / tapered-cylinder bodies -------------------------------------------------------------------
static EC dyn_ec(Mesh *m, const M *T, const float col[3], LightMode lm, const float *eye)
{
	EC e = { false, m, T, { col[0], col[1], col[2] }, lm, eye, !g_capture, 0, 0, 0, false, g_capture, { 0, 0, 0, 0 } };
	tx_rect(g_tx, e.tx);
	if (g_tx) { e.col[0] = g_tint[0]; e.col[1] = g_tint[1]; e.col[2] = g_tint[2]; }          // textured parts take their colour from the atlas
	return e;
}
static void ell_at(Mesh *m, const M *B, const float c[3], const float r[3], const float col[3], int sl, int st)
{
	EC e = dyn_ec(m, B, col, LM_WORLD, EYE); tess_ell(&e, c, r, sl, st);
}
static void box_at(Mesh *m, const M *B, const float c[3], const float h[3], const float col[3], float rad)
{
	EC e = dyn_ec(m, B, col, LM_WORLD, EYE); tess_rbox(&e, c, h, rad);
}
// tapered cylinder from point A to point B (in the frame of B0), radius ra at A and rb at B
static void limb2(Mesh *m, const M *B0, const float A[3], const float Bp[3], float ra, float rb, const float col[3], int sl, int rows, bool caps)
{
	float len = sqrtf((Bp[0] - A[0]) * (Bp[0] - A[0]) + (Bp[1] - A[1]) * (Bp[1] - A[1]) + (Bp[2] - A[2]) * (Bp[2] - A[2]));
	M T = m_mul(*B0, m_basis_z(A, Bp));
	EC e = dyn_ec(m, &T, col, LM_WORLD, EYE);
	const float c[3] = { 0, 0, len * 0.5f };
	tess_cyl_rows(&e, c, len * 0.5f, ra, ra, rb, rb, sl, caps, rows);
}
static void limb(Mesh *m, const M *B0, const float A[3], const float Bp[3], float ra, float rb, const float col[3], int sl)
{
	limb2(m, B0, A, Bp, ra, rb, col, sl, 1, false);
}

// The geometry of one soldier in the frame of `body` (live, or captured once into a model-space cache).
// detail 0 = close-up (belts, pouches, plates, goggles, ears, fingers...), 1 = medium, 2 = far. Parts are textured from the character atlas.
#define TXS(i, r, g_, b_) do { g_tx = (i); g_tint[0] = (r); g_tint[1] = (g_); g_tint[2] = (b_); } while (0)
static void soldier_parts(bool ally, const M *bodyp, float phase, float sw, int detail)
{
	const float dark[3] = { 0.07f, 0.07f, 0.08f }, lens[3] = { 0.55f, 0.75f, 0.9f };
	const int CAMO = ally ? TX_CAMO_A : TX_CAMO_E, VEST = ally ? TX_VEST_A : TX_VEST_E, HELM = ally ? TX_HELM_A : TX_HELM_E, SCARF = ally ? TX_SCARF_A : TX_SCARF_E;
	const M body = *bodyp;
	Mesh *m = &dm[D_OPAQUE];
	const bool hi = detail == 0;
	const int sl = detail == 0 ? 8 : detail == 1 ? 7 : 5, st = detail == 0 ? 5 : detail == 1 ? 4 : 3, rows = hi ? 2 : 1, tiny = hi ? 5 : 4;
	g_shade = 1;
	for (int s = -1; s <= 1; s += 2) {                                   // legs: thigh + calf with a knee, boots
		float ph = phase + (s < 0 ? 0 : PI_F);
		float a1 = sinf(ph) * 0.7f * sw, flex = fmaxf(0.0f, cosf(ph)) * 0.9f * sw, a2 = a1 - flex;
		float hip[3] = { s * 0.11f, 0.9f, 0 };
		float knee[3] = { hip[0], hip[1] - 0.45f * cosf(a1), hip[2] - 0.45f * sinf(a1) };
		float ank[3] = { hip[0], knee[1] - 0.43f * cosf(a2), knee[2] - 0.43f * sinf(a2) };
		TXS(CAMO, 0.92f, 0.92f, 0.92f);
		limb2(m, &body, hip, knee, 0.098f, 0.077f, dark, sl, rows, false);
		limb2(m, &body, knee, ank, 0.074f, 0.058f, dark, sl, rows, false);
		float bc[3] = { ank[0], ank[1] - 0.035f, ank[2] - 0.05f }, br[3] = { 0.062f, 0.055f, 0.125f };
		TXS(TX_BOOT, 1.0f, 1.0f, 1.0f);
		ell_at(m, &body, bc, br, dark, sl, st - 1);
		if (hi) {
			const float sole[3] = { bc[0], ank[1] - 0.085f, ank[2] - 0.06f }, sh[3] = { 0.065f, 0.014f, 0.135f };
			TXS(TX_BOOT, 0.45f, 0.45f, 0.45f);
			box_at(m, &body, sole, sh, dark, 0.0f);
			const float kp[3] = { knee[0], knee[1] - 0.02f, knee[2] - 0.075f }, kr[3] = { 0.085f, 0.075f, 0.04f };
			TXS(VEST, 0.8f, 0.8f, 0.8f);
			ell_at(m, &body, kp, kr, dark, tiny, 3);
		}
	}
	TXS(CAMO, 0.92f, 0.92f, 0.92f);
	{ const float c[3] = { 0, 0.94f, 0 }, r[3] = { 0.205f, 0.13f, 0.135f }; ell_at(m, &body, c, r, dark, sl, st - 1); }
	TXS(VEST, 1.0f, 1.0f, 1.0f);
	{ const float A[3] = { 0, 0.95f, 0 }, B[3] = { 0, 1.45f, 0 };          // torso: elliptical cylinder, wider at the chest
	  float len = 0.5f; M T = m_mul(body, m_basis_z(A, B)); EC e = dyn_ec(m, &T, dark, LM_WORLD, EYE);
	  const float c[3] = { 0, 0, len * 0.5f };
	  tess_cyl_rows(&e, c, len * 0.5f, 0.11f, 0.17f, 0.13f, 0.23f, sl + (hi ? 3 : 0), false, hi ? 3 : 1); }
	TXS(CAMO, 0.92f, 0.92f, 0.92f);
	{ const float c[3] = { 0, 1.42f, 0 }, r[3] = { 0.25f, 0.09f, 0.14f }; ell_at(m, &body, c, r, dark, sl, st - 1); }
	TXS(VEST, 0.75f, 0.75f, 0.75f);
	{ const float c[3] = { 0, 1.2f, -0.115f }, h[3] = { 0.15f, 0.12f, 0.035f }; box_at(m, &body, c, h, dark, 0.0f); }
	TXS(VEST, 0.9f, 0.9f, 0.9f);
	{ const float c[3] = { 0, 1.18f, 0.17f }, h[3] = { 0.13f, 0.2f, 0.07f }; box_at(m, &body, c, h, dark, 0.0f); }
	TXS(TX_SKIN, 1.0f, 1.0f, 1.0f);
	{ const float A[3] = { 0, 1.44f, 0 }, B[3] = { 0, 1.54f, 0 }; limb(m, &body, A, B, 0.062f, 0.058f, dark, sl); }
	TXS(SCARF, 1.0f, 1.0f, 1.0f);
	{ const float A[3] = { 0, 1.4f, 0 }, B[3] = { 0, 1.5f, 0 }; limb(m, &body, A, B, 0.15f, 0.12f, dark, sl); }
	TXS(TX_FACE, 1.0f, 1.0f, 1.0f); g_headmap = true;
	{ const float c[3] = { 0, 1.63f, -0.01f }, r[3] = { 0.093f, 0.115f, 0.105f }; ell_at(m, &body, c, r, dark, sl + (hi ? 2 : 0), st + (hi ? 1 : 0)); }
	g_headmap = false;
	TXS(HELM, 1.0f, 1.0f, 1.0f);
	{ const float c[3] = { 0, 1.67f, 0.005f }, r[3] = { 0.116f, 0.092f, 0.127f }; ell_at(m, &body, c, r, dark, sl + (hi ? 2 : 0), st); }
	const float R[3] = { 0.05f, 0.05f, 0.055f };
	{
		const float S[3] = { 0.26f, 1.38f, 0 }, E[3] = { 0.3f, 1.17f, -0.12f }, H[3] = { 0.12f, 1.21f, -0.3f };
		TXS(CAMO, 0.92f, 0.92f, 0.92f);
		limb2(m, &body, S, E, 0.068f, 0.056f, dark, sl, rows, false); limb2(m, &body, E, H, 0.056f, 0.046f, dark, sl, rows, false);
		TXS(TX_GLOVE, 1.0f, 1.0f, 1.0f);
		ell_at(m, &body, H, R, dark, tiny, 3);
		const float S2[3] = { -0.26f, 1.38f, 0 }, E2[3] = { -0.27f, 1.15f, -0.27f }, H2[3] = { -0.08f, 1.26f, -0.58f };
		TXS(CAMO, 0.92f, 0.92f, 0.92f);
		limb2(m, &body, S2, E2, 0.068f, 0.056f, dark, sl, rows, false); limb2(m, &body, E2, H2, 0.056f, 0.046f, dark, sl, rows, false);
		TXS(TX_GLOVE, 1.0f, 1.0f, 1.0f);
		ell_at(m, &body, H2, R, dark, tiny, 3);
		if (hi) {                                                        // elbow pads and curled fingers
			const float ep[3] = { 0.3f, 1.17f, -0.12f }, eq[3] = { -0.27f, 1.15f, -0.27f }, er[3] = { 0.06f, 0.06f, 0.06f };
			TXS(VEST, 0.8f, 0.8f, 0.8f);
			ell_at(m, &body, ep, er, dark, tiny, 3); ell_at(m, &body, eq, er, dark, tiny, 3);
			TXS(TX_GLOVE, 1.0f, 1.0f, 1.0f);
			for (int f = 0; f < 3; f++) {
				const float fa[3] = { 0.12f, 1.2f - f * 0.018f, -0.33f }, fb[3] = { 0.1f, 1.16f - f * 0.018f, -0.36f };
				limb(m, &body, fa, fb, 0.012f, 0.01f, dark, 5);
				const float ga[3] = { -0.08f, 1.26f - f * 0.016f + 0.02f, -0.61f }, gb[3] = { -0.07f, 1.22f - f * 0.016f, -0.64f };
				limb(m, &body, ga, gb, 0.012f, 0.01f, dark, 5);
			}
		}
	}
	if (hi) {
		// head details: nose, ears, eyes, goggles on the helmet, chin strap, helmet rim
		TXS(TX_SKIN, 1.0f, 1.0f, 1.0f);
		{ const float c[3] = { 0, 1.62f, -0.108f }, r[3] = { 0.018f, 0.026f, 0.025f }; ell_at(m, &body, c, r, dark, tiny, 3); }
		for (int s = -1; s <= 1; s += 2) { const float ear[3] = { s * 0.094f, 1.625f, 0.0f }, er[3] = { 0.016f, 0.03f, 0.022f }; ell_at(m, &body, ear, er, dark, tiny, 3); }
		g_tx = 0;
		for (int s = -1; s <= 1; s += 2) {
			const float gl[3] = { s * 0.045f, 1.745f, -0.095f }, glr[3] = { 0.04f, 0.026f, 0.03f }; ell_at(m, &body, gl, glr, lens, tiny, 3);
		}
		{ const float c[3] = { 0, 1.745f, -0.075f }, h[3] = { 0.1f, 0.016f, 0.05f }; box_at(m, &body, c, h, dark, 0.0f); }
		for (int s = -1; s <= 1; s += 2) { const float c[3] = { s * 0.075f, 1.56f, -0.03f }, h[3] = { 0.006f, 0.05f, 0.012f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(HELM, 0.8f, 0.8f, 0.8f);
		{ const float A[3] = { 0, 1.655f, 0 }, B[3] = { 0, 1.68f, 0 }; limb(m, &body, A, B, 0.122f, 0.124f, dark, 10); }
		// vest: mag pouches, shoulder straps, belt with pouches, radio, rolled mat, side pouches, holster
		TXS(VEST, 1.25f, 1.25f, 1.25f);
		for (int i = 0; i < 3; i++) { const float c[3] = { -0.085f + i * 0.085f, 1.1f, -0.16f }, h[3] = { 0.034f, 0.05f, 0.026f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(VEST, 0.75f, 0.75f, 0.75f);
		for (int s = -1; s <= 1; s += 2) { const float c[3] = { s * 0.12f, 1.43f, -0.02f }, h[3] = { 0.03f, 0.012f, 0.14f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(TX_POLY, 1.2f, 1.2f, 1.2f);
		{ const float A[3] = { 0, 0.965f, 0 }, B[3] = { 0, 1.01f, 0 }; limb2(m, &body, A, B, 0.2f, 0.2f, dark, 10, 1, false); }
		TXS(VEST, 1.25f, 1.25f, 1.25f);
		for (int s = -1; s <= 1; s += 2) { const float c[3] = { s * 0.19f, 0.99f, -0.05f }, h[3] = { 0.03f, 0.05f, 0.045f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(TX_POLY, 1.0f, 1.0f, 1.0f);
		{ const float c[3] = { 0.2f, 1.34f, 0.0f }, r[3] = { 0.03f, 0.05f, 0.035f }; ell_at(m, &body, c, r, dark, tiny, 3); }
		{ const float A[3] = { 0.2f, 1.37f, 0 }, B[3] = { 0.2f, 1.5f, 0 }; limb(m, &body, A, B, 0.006f, 0.005f, dark, 5); }
		TXS(CAMO, 0.8f, 0.8f, 0.8f);
		{ const float A[3] = { -0.15f, 1.32f, 0.2f }, B[3] = { 0.15f, 1.32f, 0.2f }; limb2(m, &body, A, B, 0.055f, 0.055f, dark, 8, 1, true); }
		TXS(VEST, 1.25f, 1.25f, 1.25f);
		for (int s = -1; s <= 1; s += 2) { const float c[3] = { s * 0.1f, 1.0f, 0.2f }, h[3] = { 0.03f, 0.07f, 0.035f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(TX_POLY, 1.0f, 1.0f, 1.0f);
		{ const float c[3] = { 0.19f, 0.78f, -0.02f }, h[3] = { 0.03f, 0.09f, 0.06f }; box_at(m, &body, c, h, dark, 0.0f); }
	}
	// the rifle
	TXS(TX_METAL, 0.85f, 0.85f, 0.85f);
	{ const float c[3] = { 0.1f, 1.255f, -0.3f }, h[3] = { 0.028f, 0.05f, 0.28f }; box_at(m, &body, c, h, dark, 0.0f); }
	TXS(TX_POLY, 1.0f, 1.0f, 1.0f);
	{ const float c[3] = { 0.1f, 1.145f, -0.28f }, h[3] = { 0.02f, 0.07f, 0.03f }; box_at(m, &body, c, h, dark, 0.0f); }
	{ const float c[3] = { 0.11f, 1.24f, 0.1f }, h[3] = { 0.025f, 0.05f, 0.1f }; box_at(m, &body, c, h, dark, 0.0f); }
	TXS(TX_METAL, 1.0f, 1.0f, 1.0f);
	{ const float A[3] = { 0.1f, 1.27f, -0.55f }, B[3] = { 0.1f, 1.27f, -0.9f }; limb(m, &body, A, B, 0.013f, 0.013f, dark, hi ? 8 : 6); }
	if (hi) {
		for (int i = 0; i < 6; i++) { const float c[3] = { 0.1f, 1.31f, -0.1f - i * 0.085f }, h[3] = { 0.016f, 0.008f, 0.02f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(TX_POLY, 0.7f, 0.7f, 0.7f);
		{ const float c[3] = { 0.1f, 1.33f, -0.22f }, h[3] = { 0.02f, 0.022f, 0.045f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(TX_METAL, 0.6f, 0.6f, 0.6f);
		{ const float A[3] = { 0.1f, 1.27f, -0.88f }, B[3] = { 0.1f, 1.27f, -0.97f }; limb2(m, &body, A, B, 0.02f, 0.02f, dark, 8, 1, true); }
		TXS(TX_METAL, 1.0f, 1.0f, 1.0f);
		{ const float c[3] = { 0.1f, 1.2f, -0.52f }, h[3] = { 0.034f, 0.034f, 0.14f }; box_at(m, &body, c, h, dark, 0.0f); }
		TXS(TX_POLY, 1.0f, 1.0f, 1.0f);
		{ const float A[3] = { 0.1f, 1.1f, -0.26f }, B[3] = { 0.1f, 1.05f, -0.3f }; limb2(m, &body, A, B, 0.02f, 0.022f, dark, 6, 1, true); }
	}
	g_tx = 0; g_tint[0] = g_tint[1] = g_tint[2] = 1.0f;
	g_shade = 0;
}

// ---- model cache: soldiers are tessellated once per team / detail / walk pose into model space, then each frame only transformed and lit
typedef struct { CV *v; int n; } Model;
static int g_quality = 1, g_hi_used;
static Model s_sold[2][3][9];                  // [ally][detail][pose]: pose 0 = standing, 1..8 = walk cycle

static Model capture_model(void)
{
	Model md = { NULL, cap_n };
	md.v = (CV *)malloc(sizeof(CV) * (size_t)(cap_n > 0 ? cap_n : 1));
	if (md.v) memcpy(md.v, cap_buf, sizeof(CV) * (size_t)cap_n); else md.n = 0;
	return md;
}
static const Model *soldier_model(bool ally, int lod, int pose)
{
	Model *md = &s_sold[ally ? 1 : 0][lod][pose];
	if (md->v) return md;
	M id = m_id();
	g_capture = true; cap_n = 0;
	soldier_parts(ally, &id, pose ? (pose - 1) * 2 * PI_F / 8 + PI_F / 8 : 0, pose ? 1.0f : 0.0f, lod);
	g_capture = false;
	*md = capture_model();
	return md;
}
// transform a cached model by "stand at (ox, oz) facing yaw" and light it with the sun + sky (the light dot product is rotated into model space once)
static void emit_model(Mesh *m, const Model *md, float ox, float oz, float yaw)
{
	if (!md->v || m->n + md->n > m->cap) return;
	float cy = cosf(-yaw), sy = sinf(-yaw);
	float A = cy * SUNV[0] - sy * SUNV[2], B = sy * SUNV[0] + cy * SUNV[2];
	Vtx *o = m->v + m->n;
	for (int i = 0; i < md->n; i++, o++) {
		const CV *v = &md->v[i];
		o->x = cy * v->p[0] + sy * v->p[2] + ox; o->y = v->p[1]; o->z = -sy * v->p[0] + cy * v->p[2] + oz; o->u = v->u; o->v = v->v;
		float ny = v->n[1], d = v->n[0] * A + ny * SUNV[1] + v->n[2] * B;
		if (ny > 0.5f) d = 0.62f * ny; else if (d < 0) d = 0;
		float h = 0.5f + 0.5f * ny;
		float r = v->c[0] * (L_GND[0] + (L_SKY[0] - L_GND[0]) * h + L_SUN[0] * d) * EXPO, g = v->c[1] * (L_GND[1] + (L_SKY[1] - L_GND[1]) * h + L_SUN[1] * d) * EXPO, b = v->c[2] * (L_GND[2] + (L_SKY[2] - L_GND[2]) * h + L_SUN[2] * d) * EXPO;
		o->r = r > 1 ? 1 : r; o->g = g > 1 ? 1 : g; o->b = b > 1 ? 1 : b; o->a = 1;
	}
	m->n += md->n;
}

static void soldier(const Game *g, const Actor *a)
{
	bool ally = a->team == TEAM_ALLY;
	float dx = a->x - EYE[0], dz = a->z - EYE[2], dist = sqrtf(dx * dx + dz * dz);
	float sp = sqrtf(a->vx * a->vx + a->vz * a->vz);
	if (a->alive) {
		int pose = 0;
		if (sp > 0.8f) { float ph = fmodf(a->walk, 2 * PI_F); if (ph < 0) ph += 2 * PI_F; pose = 1 + ((int)(ph / (2 * PI_F) * 8)) % 8; }
		int lod = dist < 18 ? 1 : 2;                                      // detail 0 (belts, fingers, ears...) only for the two nearest soldiers, and only at High quality
		if (g_quality && dist < 9.0f && g_hi_used < 2) { lod = 0; g_hi_used++; }
		emit_model(&dm[D_OPAQUE], soldier_model(ally, lod, pose), a->x, a->z, a->yaw);
	} else {                                                             // falling / lying: built live, only for a few seconds
		M body = m_mul(m_tr(a->x, 0, a->z), m_ry(-a->yaw));
		float fall = clampf(a->deadT * 3.0f, 0, 1) * DEG(88);
		body = m_mul(body, m_rx(fall));
		soldier_parts(ally, &body, 0, 0, dist < 14 ? 1 : 2);
	}
	if (a->alive || a->deadT < 3.0f) {
		const float k[3] = { 0, 0, 0 };
		flat(&dm[D_ALPHA], a->x, 0.03f, a->z, a->alive ? 0.55f : 0.8f, k, 0.5f);
	}
	if (a->alive && a->flashT > 0) {
		float fwd[3] = { sinf(a->yaw), 0, -cosf(a->yaw) };
		float c[3] = { a->x + fwd[0] * 1.0f, 1.27f, a->z + fwd[2] * 1.0f };
		const float col[3] = { 1.0f, 0.8f, 0.45f };
		bb(&dm[D_ADD], c, 0.42f, g->time * 9, col, 1.0f, CR, CU);
	}
}

// ---- first-person gun ----------------------------------------------------------------------------------------------------
typedef struct { float c[3], h[3], rx, ry; uint8_t col, tag, kind; float rad; const float *prof; int np; } Part;   // tag: 0 body, 1 magazine, 2 pump, 3 left hand/arm, 4 right hand/arm, 6 stock (hidden when aiming); kind: 0 rounded box (rad = corner radius), 1 cylinder along z (h[2] = half length, rad = radius factor at the -z end), 2 ellipsoid, 3 lathe (prof = z, radius stations), 4 extruded side outline (prof = z, y points, h[0] = half width, rad = bevel)
static const int PAL_TX[9] = { TX_METAL, TX_METAL, TX_BRONZE, TX_GLOVE, TX_CAMO_A, TX_WOOD, TX_WHITE, TX_WHITE, TX_POLY };
static const float PAL_TINT[9] = { 0.95f, 0.5f, 1.0f, 1.0f, 0.95f, 1.0f, 1, 1, 1.1f };
static const float PAL[9][3] = {
	{ 0.17f, 0.17f, 0.18f }, { 0.07f, 0.07f, 0.08f }, { 0.36f, 0.27f, 0.17f }, { 0.12f, 0.11f, 0.1f },
	{ 0.24f, 0.27f, 0.2f }, { 0.4f, 0.24f, 0.12f }, { 0.8f, 0.1f, 0.08f }, { 0.9f, 0.9f, 0.9f }, { 0.12f, 0.12f, 0.13f },
};
typedef struct { const Part *parts; int n; float sight[3], adsDist, muzzle[3]; bool dot; float hip[3]; } VMDef;

// Hands and sleeves. rz = z of the grip the right hand holds, fz = z of its forearm, lz = z of the handguard the left hand holds, lfz / lfy = left forearm placement.
// The camera sees the left side of the gun, so fingers and thumbs are modelled on that side; knuckles, cuffs and a wrist watch add volume.
#define HANDS(rz, fz, lz, lfz, lfy) \
	{ { 0.012f, -0.082f, (rz) + 0.014f }, { 0.027f, 0.036f, 0.034f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, \
	{ { -0.021f, -0.06f, (rz) - 0.028f }, { 0.0105f, 0.0115f, 0.02f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, \
	{ { -0.021f, -0.081f, (rz) - 0.024f }, { 0.0105f, 0.0115f, 0.02f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, \
	{ { -0.021f, -0.102f, (rz) - 0.02f }, { 0.0105f, 0.0115f, 0.019f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, \
	{ { -0.02f, -0.122f, (rz) - 0.016f }, { 0.01f, 0.011f, 0.017f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, \
	{ { -0.022f, -0.044f, (rz) + 0.028f }, { 0.011f, 0.011f, 0.028f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, \
	{ { 0.065f, -0.175f, (fz) }, { 0.036f, 0.034f, 0.23f }, 0.2f, -0.12f, 4, 4, 1, 0.66f, NULL, 0 }, \
	{ { -0.035f, -0.052f, (lz) }, { 0.034f, 0.036f, 0.08f }, 0, 0, 3, 3, 2, 0, NULL, 0 }, \
	{ { -0.044f, -0.022f, (lz) + 0.04f }, { 0.011f, 0.017f, 0.017f }, 0, 0, 3, 3, 2, 0, NULL, 0 }, \
	{ { -0.044f, -0.022f, (lz) }, { 0.011f, 0.017f, 0.017f }, 0, 0, 3, 3, 2, 0, NULL, 0 }, \
	{ { -0.044f, -0.022f, (lz) - 0.04f }, { 0.011f, 0.017f, 0.017f }, 0, 0, 3, 3, 2, 0, NULL, 0 }, \
	{ { -0.043f, -0.02f, (lz) - 0.078f }, { 0.01f, 0.016f, 0.016f }, 0, 0, 3, 3, 2, 0, NULL, 0 }, \
	{ { -0.034f, 0.024f, (lz) - 0.02f }, { 0.01f, 0.01f, 0.034f }, 0, 0, 3, 3, 2, 0, NULL, 0 }, \
	{ { -0.13f, -0.15f, (lfz) }, { 0.034f, 0.034f, 0.23f }, 0.2f, (lfy), 4, 3, 1, 0.66f, NULL, 0 }
#define NP(a) ((int)(sizeof(a) / sizeof((a)[0]) / 2))
// M27 side profiles (z, y), gun space: bore on the z axis, muzzle towards -z
static const float PR_UPPER[] = { -0.20f, 0.050f,  0.070f, 0.050f,  0.085f, 0.040f,  0.085f, 0.000f,  -0.20f, 0.000f };
static const float PR_LOWER[] = { -0.11f, 0.000f,  0.085f, 0.000f,  0.085f, -0.012f,  0.070f, -0.040f,  0.010f, -0.044f,  -0.020f, -0.040f,  -0.110f, -0.036f };
static const float PR_GRIP[]  = {  0.025f, -0.035f,  0.060f, -0.035f,  0.098f, -0.136f,  0.068f, -0.145f };
static const float PR_MAG[]   = { -0.080f, -0.036f,  -0.022f, -0.036f,  -0.034f, -0.115f,  -0.050f, -0.190f,  -0.108f, -0.190f,  -0.098f, -0.115f };
static const float PR_STOCK[] = {  0.120f, 0.030f,  0.250f, 0.038f,  0.300f, 0.030f,  0.318f, 0.020f,  0.322f, -0.065f,  0.300f, -0.085f,  0.160f, -0.050f,  0.120f, -0.030f };
static const float PR_BARREL[] = { -0.668f, 0.0120f,  -0.664f, 0.0155f,  -0.600f, 0.0155f,  -0.600f, 0.0105f,  -0.520f, 0.0105f,  -0.520f, 0.0165f,  -0.495f, 0.0165f,  -0.495f, 0.0105f,  -0.470f, 0.0105f };
static const float PR_BUFFER[] = {  0.085f, 0.0185f,  0.125f, 0.0185f,  0.125f, 0.0215f,  0.135f, 0.0215f,  0.135f, 0.0185f,  0.260f, 0.0185f };
#define BOX(cx, cy, cz, hx, hy, hz, col, tag, rad) { { cx, cy, cz }, { hx, hy, hz }, 0, 0, col, tag, 0, rad, NULL, 0 }
static const Part P_AR[] = {
	{ { 0, 0, 0 }, { 0.022f, 0, 0 }, 0, 0, 1, 0, 4, 0.004f, PR_UPPER, NP(PR_UPPER) },                       // upper receiver
	{ { 0, 0, 0 }, { 0.021f, 0, 0 }, 0, 0, 1, 0, 4, 0.004f, PR_LOWER, NP(PR_LOWER) },                       // lower receiver with the magwell
	{ { 0, 0, 0 }, { 0.0175f, 0, 0 }, 0, 0, 8, 0, 4, 0.005f, PR_GRIP, NP(PR_GRIP) },                        // pistol grip
	{ { 0, 0, 0 }, { 0.0145f, 0, 0 }, 0, 0, 8, 1, 4, 0.0035f, PR_MAG, NP(PR_MAG) },                         // curved magazine
	BOX(0, -0.193f, -0.079f, 0.0155f, 0.005f, 0.034f, 8, 1, 0),
	BOX(0, -0.070f, -0.0572f, 0.0152f, 0.0022f, 0.0295f, 8, 1, 0), BOX(0, -0.095f, -0.0617f, 0.0152f, 0.0022f, 0.0295f, 8, 1, 0),
	BOX(0, -0.120f, -0.0663f, 0.0152f, 0.0022f, 0.0295f, 8, 1, 0), BOX(0, -0.145f, -0.0708f, 0.0152f, 0.0022f, 0.0295f, 8, 1, 0),
	BOX(0, -0.030f, -0.052f, 0.0185f, 0.012f, 0.034f, 1, 0, 0),                                          // flared magwell
	BOX(0, -0.054f, -0.037f, 0.004f, 0.016f, 0.004f, 1, 0, 0), BOX(0, -0.070f, -0.012f, 0.004f, 0.004f, 0.027f, 1, 0, 0),   // trigger guard
	BOX(0, -0.057f, 0.014f, 0.004f, 0.014f, 0.004f, 1, 0, 0), BOX(0, -0.052f, -0.005f, 0.003f, 0.012f, 0.0035f, 0, 0, 0),
	BOX(0, 0, -0.27f, 0.0345f, 0.0345f, 0.065f, 2, 0, 0.01f), BOX(0, 0, -0.395f, 0.029f, 0.029f, 0.075f, 2, 0, 0.008f),   // free-float quad-rail handguard, in two steps
	BOX(0, 0, -0.205f, 0.0375f, 0.0375f, 0.012f, 1, 0, 0), BOX(0, 0, -0.335f, 0.0362f, 0.0362f, 0.004f, 1, 0, 0), BOX(0, 0, -0.470f, 0.033f, 0.033f, 0.006f, 1, 0, 0),
	{ { 0, 0.0215f, -0.35f }, { 0.0048f, 0.0048f, 0.16f }, 0, 0, 1, 0, 1, 0, NULL, 0 },                         // gas tube sticking out of the handguard
	BOX(-0.0295f, 0.012f, -0.44f, 0.0012f, 0.006f, 0.011f, 1, 0, 0), BOX(-0.0295f, -0.006f, -0.44f, 0.0012f, 0.006f, 0.011f, 1, 0, 0), BOX(-0.0295f, 0.012f, -0.41f, 0.0012f, 0.006f, 0.006f, 1, 0, 0),   // vents
	BOX(0, 0.0555f, -0.20f, 0.0112f, 0.0075f, 0.27f, 1, 0, 0),                                                // top rail and its teeth
	BOX(0, 0.0645f, 0.050f, 0.0112f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, 0.010f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.030f, 0.0105f, 0.002f, 0.007f, 1, 0, 0),
	BOX(0, 0.0615f, -0.070f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.110f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.150f, 0.0105f, 0.002f, 0.007f, 1, 0, 0),
	BOX(0, 0.0615f, -0.190f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.230f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.270f, 0.0105f, 0.002f, 0.007f, 1, 0, 0),
	BOX(0, 0.0615f, -0.310f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.350f, 0.0105f, 0.002f, 0.007f, 1, 0, 0), BOX(0, 0.0615f, -0.390f, 0.0105f, 0.002f, 0.007f, 1, 0, 0),
	BOX(0, 0.0615f, -0.430f, 0.0105f, 0.002f, 0.007f, 1, 0, 0),
	BOX(0, -0.0385f, -0.36f, 0.0095f, 0.0045f, 0.09f, 1, 0, 0),                                               // bottom rail
	BOX(0, -0.043f, -0.30f, 0.0095f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, -0.043f, -0.336f, 0.0095f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, -0.043f, -0.372f, 0.0095f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, -0.043f, -0.408f, 0.0095f, 0.0015f, 0.006f, 1, 0, 0),
	BOX(-0.0343f, 0, -0.25f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0), BOX(-0.0343f, 0, -0.29f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0), BOX(-0.0343f, 0, -0.33f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0),   // M-LOK slots
	BOX(-0.0343f, 0, -0.37f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0), BOX(-0.0343f, 0, -0.41f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0),
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 0, 0, 3, 0, PR_BARREL, NP(PR_BARREL) },                                  // stepped barrel: muzzle device, gas block, collars
	BOX(-0.0157f, 0, -0.635f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0), BOX(0.0157f, 0, -0.635f, 0.0012f, 0.0045f, 0.013f, 1, 0, 0), BOX(0, 0.0157f, -0.635f, 0.0045f, 0.0012f, 0.013f, 1, 0, 0),   // flash hider slots
	BOX(0, 0.0245f, -0.507f, 0.006f, 0.0115f, 0.012f, 1, 0, 0), BOX(0, 0.043f, -0.507f, 0.0014f, 0.0165f, 0.0014f, 1, 0, 0),                            // front sight
	BOX(-0.0065f, 0.039f, -0.507f, 0.0012f, 0.013f, 0.0075f, 1, 0, 0), BOX(0.0065f, 0.039f, -0.507f, 0.0012f, 0.013f, 0.0075f, 1, 0, 0),
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 0, 6, 3, 0, PR_BUFFER, NP(PR_BUFFER) },                                  // buffer tube
	{ { 0, 0, 0 }, { 0.0215f, 0, 0 }, 0, 0, 8, 6, 4, 0.008f, PR_STOCK, NP(PR_STOCK) },                        // collapsible stock
	BOX(0, -0.022f, 0.324f, 0.006f, 0.047f, 0.0235f, 1, 6, 0),                                           // butt pad
	BOX(0, 0.047f, 0.088f, 0.003f, 0.004f, 0.01f, 1, 0, 0), BOX(0, 0.047f, 0.098f, 0.015f, 0.004f, 0.008f, 1, 0, 0),   // charging handle
	BOX(-0.0225f, -0.012f, 0.035f, 0.0035f, 0.005f, 0.012f, 0, 0, 0), BOX(-0.0225f, -0.004f, 0.0f, 0.0035f, 0.0035f, 0.01f, 0, 0, 0),   // selector, bolt catch
	BOX(-0.0225f, -0.028f, -0.033f, 0.0045f, 0.0055f, 0.0055f, 0, 0, 0), BOX(-0.0195f, -0.038f, 0.082f, 0.0035f, 0.006f, 0.006f, 0, 0, 0),   // mag release, sling mount
	BOX(0, 0.0735f, -0.05f, 0.0235f, 0.0095f, 0.04f, 1, 0, 0.006f),                                             // reflex sight: mount, side posts, hood
	{ { -0.022f, 0.092f, -0.05f }, { 0.003f, 0.03f, 0.034f }, 0, 0, 1, 0, 0, 0, NULL, 0 }, { { 0.022f, 0.092f, -0.05f }, { 0.003f, 0.03f, 0.034f }, 0, 0, 1, 0, 0, 0, NULL, 0 },
	{ { 0, 0.125f, -0.05f }, { 0.024f, 0.004f, 0.034f }, 0, 0, 1, 0, 0, 0, NULL, 0 },
	HANDS(0.062f, 0.27f, -0.34f, -0.1f, 0.62f),
};
// ---- Skorpion-style SMG
static const float PS_BODY[] = { -0.12f, 0.030f,  0.12f, 0.030f,  0.135f, 0.020f,  0.135f, -0.020f,  0.05f, -0.035f,  -0.12f, -0.030f };
static const float PS_MAG[]  = { -0.065f, -0.030f,  -0.020f, -0.030f,  -0.026f, -0.130f,  -0.032f, -0.235f,  -0.080f, -0.235f,  -0.073f, -0.130f };
static const float PS_GRIP[] = {  0.045f, -0.030f,  0.075f, -0.030f,  0.104f, -0.125f,  0.074f, -0.133f };
static const float PS_SUPP[] = { -0.455f, 0.0170f,  -0.445f, 0.0215f,  -0.28f, 0.0215f,  -0.275f, 0.0110f,  -0.12f, 0.0110f };
static const Part P_SMG[] = {
	{ { 0, 0, 0 }, { 0.026f, 0, 0 }, 0, 0, 1, 0, 4, 0.006f, PS_BODY, NP(PS_BODY) },
	{ { 0, 0, 0 }, { 0.0165f, 0, 0 }, 0, 0, 8, 0, 4, 0.005f, PS_GRIP, NP(PS_GRIP) },
	{ { 0, 0, 0 }, { 0.0125f, 0, 0 }, 0, 0, 8, 1, 4, 0.004f, PS_MAG, NP(PS_MAG) },
	BOX(0, -0.238f, -0.0565f, 0.0135f, 0.004f, 0.027f, 8, 1, 0),
	BOX(0, -0.09f, -0.0435f, 0.0132f, 0.002f, 0.025f, 8, 1, 0), BOX(0, -0.14f, -0.0475f, 0.0132f, 0.002f, 0.025f, 8, 1, 0), BOX(0, -0.19f, -0.052f, 0.0132f, 0.002f, 0.025f, 8, 1, 0),
	BOX(0, -0.002f, -0.19f, 0.0225f, 0.027f, 0.065f, 1, 0, 0.009f),                                           // handguard
	BOX(-0.0232f, 0.0f, -0.21f, 0.0012f, 0.006f, 0.012f, 1, 0, 0), BOX(-0.0232f, 0.0f, -0.17f, 0.0012f, 0.006f, 0.012f, 1, 0, 0),   // vent slots
	BOX(0, 0.037f, 0.0f, 0.0115f, 0.005f, 0.12f, 1, 0, 0),                                                    // top rail with teeth
	BOX(0, 0.0415f, 0.09f, 0.0115f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, 0.0415f, 0.05f, 0.0115f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, 0.0415f, 0.01f, 0.0115f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, 0.0415f, -0.03f, 0.0115f, 0.0015f, 0.006f, 1, 0, 0), BOX(0, 0.0415f, -0.07f, 0.0115f, 0.0015f, 0.006f, 1, 0, 0),
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 1, 0, 3, 0, PS_SUPP, NP(PS_SUPP) },                                      // suppressor
	BOX(0, 0.063f, -0.25f, 0.0045f, 0.021f, 0.0045f, 1, 0, 0), BOX(0, 0.052f, -0.25f, 0.011f, 0.009f, 0.012f, 1, 0, 0),    // front sight
	BOX(-0.016f, 0.06f, 0.1f, 0.004f, 0.018f, 0.007f, 1, 0, 0), BOX(0.016f, 0.06f, 0.1f, 0.004f, 0.018f, 0.007f, 1, 0, 0), BOX(0, 0.044f, 0.1f, 0.02f, 0.006f, 0.008f, 1, 0, 0),   // rear sight
	BOX(0, 0.052f, 0.12f, 0.004f, 0.004f, 0.012f, 1, 0, 0),                                                   // charging handle
	BOX(0, -0.057f, 0.014f, 0.004f, 0.014f, 0.004f, 1, 0, 0), BOX(0, -0.07f, -0.01f, 0.004f, 0.004f, 0.027f, 1, 0, 0), BOX(0, -0.054f, -0.037f, 0.004f, 0.016f, 0.004f, 1, 0, 0), BOX(0, -0.052f, -0.008f, 0.003f, 0.012f, 0.0035f, 0, 0, 0),
	BOX(-0.0265f, -0.01f, 0.05f, 0.0035f, 0.005f, 0.012f, 0, 0, 0),
	{ { -0.018f, 0.004f, 0.2f }, { 0.0035f, 0.0035f, 0.075f }, 0, 0, 0, 6, 1, 0, NULL, 0 }, { { 0.018f, 0.004f, 0.2f }, { 0.0035f, 0.0035f, 0.075f }, 0, 0, 0, 6, 1, 0, NULL, 0 },   // folding wire stock
	BOX(0, 0.0f, 0.283f, 0.0225f, 0.03f, 0.007f, 1, 6, 0.003f), BOX(0, 0.0f, 0.132f, 0.022f, 0.006f, 0.008f, 0, 6, 0),
	HANDS(0.075f, 0.26f, -0.19f, 0.0f, 0.62f),
};
// ---- pistol
static const float PP_SLIDE[] = { -0.118f, 0.052f,  0.046f, 0.052f,  0.052f, 0.046f,  0.052f, 0.008f,  -0.118f, 0.008f,  -0.126f, 0.02f };
static const float PP_FRAME[] = { -0.095f, 0.008f,  0.052f, 0.008f,  0.056f, -0.010f,  0.02f, -0.020f,  -0.02f, -0.018f,  -0.095f, -0.004f };
static const float PP_GRIP[]  = {  0.012f, -0.012f,  0.054f, -0.012f,  0.074f, -0.116f,  0.032f, -0.12f };
static const float PP_BARREL[] = { -0.19f, 0.0075f,  -0.185f, 0.0095f,  -0.126f, 0.0095f };
static const Part P_PISTOL[] = {
	{ { 0, 0, 0 }, { 0.0155f, 0, 0 }, 0, 0, 0, 0, 4, 0.004f, PP_SLIDE, NP(PP_SLIDE) },
	{ { 0, 0, 0 }, { 0.0145f, 0, 0 }, 0, 0, 1, 0, 4, 0.004f, PP_FRAME, NP(PP_FRAME) },
	{ { 0, 0, 0 }, { 0.0165f, 0, 0 }, 0, 0, 8, 0, 4, 0.005f, PP_GRIP, NP(PP_GRIP) },
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 0, 0, 3, 0, PP_BARREL, NP(PP_BARREL) },
	BOX(0, -0.121f, 0.053f, 0.0145f, 0.004f, 0.023f, 1, 0, 0),
	BOX(-0.0158f, 0.03f, 0.015f, 0.0012f, 0.017f, 0.004f, 1, 0, 0), BOX(-0.0158f, 0.03f, 0.025f, 0.0012f, 0.017f, 0.004f, 1, 0, 0), BOX(-0.0158f, 0.03f, 0.035f, 0.0012f, 0.017f, 0.004f, 1, 0, 0),   // slide serrations
	BOX(0.0f, 0.056f, 0.045f, 0.0105f, 0.0055f, 0.006f, 1, 0, 0), BOX(0, 0.0575f, -0.178f, 0.0035f, 0.0075f, 0.006f, 1, 0, 0),   // rear and front sights
	BOX(-0.012f, 0.0575f, 0.045f, 0.0025f, 0.0045f, 0.0045f, 0, 0, 0), BOX(0.012f, 0.0575f, 0.045f, 0.0025f, 0.0045f, 0.0045f, 0, 0, 0),
	BOX(0, -0.027f, -0.018f, 0.0035f, 0.012f, 0.004f, 1, 0, 0), BOX(0, -0.04f, 0.0f, 0.0035f, 0.004f, 0.022f, 1, 0, 0), BOX(0, -0.03f, 0.022f, 0.0035f, 0.012f, 0.004f, 1, 0, 0),   // trigger guard
	BOX(0, -0.022f, 0.006f, 0.003f, 0.01f, 0.003f, 0, 0, 0),
	BOX(0.0f, 0.0f, 0.09f, 0.0032f, 0.012f, 0.0075f, 1, 0, 0),                                                // hammer
	{ { 0.0f, -0.075f, 0.058f }, { 0.04f, 0.048f, 0.04f }, 0, 0, 3, 4, 2, 0, NULL, 0 },
	{ { 0.05f, -0.14f, 0.25f }, { 0.036f, 0.034f, 0.21f }, 0.2f, -0.1f, 4, 4, 1, 0.66f, NULL, 0 },
	{ { -0.022f, -0.09f, 0.0f }, { 0.036f, 0.04f, 0.05f }, 0, 0.2f, 3, 3, 2, 0, NULL, 0 },
	{ { -0.11f, -0.14f, 0.13f }, { 0.032f, 0.032f, 0.21f }, 0.2f, 0.45f, 4, 3, 1, 0.66f, NULL, 0 },
	{ { -0.021f, -0.06f, 0.034f }, { 0.0105f, 0.0115f, 0.02f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, { { -0.021f, -0.082f, 0.038f }, { 0.0105f, 0.0115f, 0.02f }, 0, 0, 3, 4, 2, 0, NULL, 0 }, { { -0.02f, -0.103f, 0.042f }, { 0.01f, 0.011f, 0.018f }, 0, 0, 3, 4, 2, 0, NULL, 0 },
};
// ---- pump shotgun
static const float PG_BODY[]  = { -0.12f, 0.040f,  0.10f, 0.040f,  0.125f, 0.028f,  0.125f, -0.030f,  0.0f, -0.045f,  -0.12f, -0.032f };
static const float PG_STOCK[] = {  0.12f, 0.035f,  0.28f, 0.042f,  0.38f, 0.034f,  0.405f, 0.0f,  0.385f, -0.078f,  0.30f, -0.095f,  0.12f, -0.045f };
static const float PG_BARREL[] = { -0.785f, 0.0110f,  -0.775f, 0.0135f,  -0.70f, 0.0135f,  -0.70f, 0.0110f,  -0.12f, 0.0110f };
static const float PG_TUBE[]   = { -0.70f, 0.0125f,  -0.69f, 0.0150f,  -0.65f, 0.0150f,  -0.65f, 0.0125f,  -0.12f, 0.0125f };
static const Part P_SHOTGUN[] = {
	{ { 0, 0, 0 }, { 0.0235f, 0, 0 }, 0, 0, 1, 0, 4, 0.006f, PG_BODY, NP(PG_BODY) },
	{ { 0, 0, 0 }, { 0.0195f, 0, 0 }, 0, 0, 5, 6, 4, 0.008f, PG_STOCK, NP(PG_STOCK) },
	BOX(0, -0.02f, 0.41f, 0.006f, 0.058f, 0.025f, 1, 6, 0.003f),
	{ { 0, 0.0f, 0 }, { 0, 0, 0 }, 0, 0, 0, 0, 3, 0, PG_BARREL, NP(PG_BARREL) },
	{ { 0, -0.0255f, 0 }, { 0, 0, 0 }, 0, 0, 0, 0, 3, 0, PG_TUBE, NP(PG_TUBE) },
	BOX(0, 0.0475f, -0.44f, 0.004f, 0.0035f, 0.34f, 1, 0, 0),                                                 // ventilated rib
	BOX(0, 0.052f, -0.77f, 0.0028f, 0.0065f, 0.0028f, 1, 0, 0), BOX(0, 0.054f, -0.12f, 0.0035f, 0.005f, 0.012f, 1, 0, 0),   // bead and rear sight
	BOX(0, -0.05f, -0.04f, 0.017f, 0.012f, 0.035f, 1, 0, 0.004f),                                             // loading port
	BOX(0, -0.03f, -0.352f, 0.0275f, 0.0265f, 0.085f, 5, 2, 0.013f),                                          // wooden pump grip
	BOX(0, -0.03f, -0.29f, 0.0285f, 0.0275f, 0.004f, 1, 2, 0), BOX(0, -0.03f, -0.325f, 0.0285f, 0.0275f, 0.004f, 1, 2, 0), BOX(0, -0.03f, -0.36f, 0.0285f, 0.0275f, 0.004f, 1, 2, 0), BOX(0, -0.03f, -0.395f, 0.0285f, 0.0275f, 0.004f, 1, 2, 0),   // grip grooves
	BOX(0, -0.03f, -0.44f, 0.0165f, 0.0165f, 0.008f, 1, 0, 0.005f),
	BOX(0, -0.05f, 0.02f, 0.004f, 0.016f, 0.004f, 1, 0, 0), BOX(0, -0.065f, 0.045f, 0.004f, 0.004f, 0.027f, 1, 0, 0), BOX(0, -0.05f, 0.072f, 0.004f, 0.016f, 0.004f, 1, 0, 0), BOX(0, -0.05f, 0.044f, 0.003f, 0.012f, 0.0035f, 0, 0, 0),   // trigger guard and trigger
	BOX(-0.0245f, 0.01f, 0.0f, 0.0012f, 0.012f, 0.05f, 1, 0, 0),                                              // receiver flat
	HANDS(0.07f, 0.27f, -0.352f, -0.12f, 0.62f),
};
static const VMDef VMD[W_COUNT] = {
	{ P_AR, sizeof P_AR / sizeof P_AR[0], { 0, 0.094f, -0.05f }, 0.30f, { 0, 0, -0.67f }, true, { 0.16f, -0.185f, -0.40f } },
	{ P_SMG, sizeof P_SMG / sizeof P_SMG[0], { 0, 0.07f, 0.1f }, 0.26f, { 0, 0.004f, -0.42f }, false, { 0.16f, -0.18f, -0.34f } },
	{ P_PISTOL, sizeof P_PISTOL / sizeof P_PISTOL[0], { 0, 0.064f, 0.045f }, 0.28f, { 0, 0.025f, -0.2f }, false, { 0.12f, -0.15f, -0.34f } },
	{ P_SHOTGUN, sizeof P_SHOTGUN / sizeof P_SHOTGUN[0], { 0, 0.052f, 0.0f }, 0.3f, { 0, 0.02f, -0.78f }, false, { 0.16f, -0.18f, -0.42f } },
};

// ---- first-person gun cache: the parts are tessellated once per weapon / variant in gun space, then transformed and lit each frame
static Model s_vm[W_COUNT * 8];
static const Model *vm_model(int weapon, int flags)
{
	Model *md = &s_vm[weapon * 8 + flags];
	if (md->v) return md;
	const VMDef *vd = &VMD[weapon];
	g_capture = true; cap_n = 0;
	static const float O[3] = { 0, 0, 0 };
	for (int i = 0; i < vd->n; i++) {
		const Part *pt = &vd->parts[i];
		if (pt->tag == 2) continue;                                     // pump: live
		if (pt->tag == 1 && (flags & 2)) continue;                      // magazine out while reloading
		if (pt->tag == 6 && (flags & 1)) continue;                      // stock hidden when aiming
		if (pt->tag == 3 && (flags & 4)) continue;                      // left arm busy while healing
		M T = m_mul(m_tr(pt->c[0], pt->c[1], pt->c[2]), m_mul(m_ry(pt->ry), m_rx(pt->rx)));
		static const float zero[3] = { 0, 0, 0 };
		g_tx = PAL_TX[pt->col]; g_tint[0] = g_tint[1] = g_tint[2] = PAL_TINT[pt->col];
		EC e = dyn_ec(&dm[D_VM], &T, PAL[pt->col], LM_VM, O);
		if (pt->kind == 0) tess_rbox(&e, zero, pt->h, pt->rad);
		else if (pt->kind == 1) { float k = pt->rad > 0 ? pt->rad : 1.0f; tess_cyl(&e, zero, pt->h[2], pt->h[0] * k, pt->h[1] * k, pt->h[0], pt->h[1], 10, pt->h[2] < 0.12f); }
		else if (pt->kind == 3) tess_lathe(&e, zero, pt->prof, pt->np, 10, 1.0f);
		else if (pt->kind == 4) tess_extrude(&e, zero, pt->prof, pt->np, pt->h[0], pt->rad);
		else { bool small = pt->h[0] < 0.025f && pt->h[2] < 0.03f; tess_ell(&e, zero, pt->h, small ? 5 : 8, small ? 3 : 5); }
	}
	g_capture = false; g_tx = 0; g_tint[0] = g_tint[1] = g_tint[2] = 1.0f;
	*md = capture_model();
	return md;
}
static void emit_vm_model(Mesh *m, const Model *md, const M *G)
{
	if (!md->v || m->n + md->n > m->cap) return;
	static const float Lv[3] = { -0.35f, 0.75f, 0.56f };
	float Lg[3];
	for (int j = 0; j < 3; j++) Lg[j] = G->m[j] * Lv[0] + G->m[4 + j] * Lv[1] + G->m[8 + j] * Lv[2];   // light direction in gun space
	Vtx *o = m->v + m->n;
	for (int i = 0; i < md->n; i++, o++) {
		const CV *v = &md->v[i];
		o->x = G->m[0] * v->p[0] + G->m[1] * v->p[1] + G->m[2] * v->p[2] + G->m[3];
		o->y = G->m[4] * v->p[0] + G->m[5] * v->p[1] + G->m[6] * v->p[2] + G->m[7];
		o->z = G->m[8] * v->p[0] + G->m[9] * v->p[1] + G->m[10] * v->p[2] + G->m[11];
		o->u = v->u; o->v = v->v;
		float d = v->n[0] * Lg[0] + v->n[1] * Lg[1] + v->n[2] * Lg[2]; if (d < 0) d = 0;
		float l = 0.42f + 0.62f * d;
		float r = v->c[0] * l * 1.04f, g = v->c[1] * l * 0.96f, b = v->c[2] * l * 0.88f;
		o->r = r > 1 ? 1 : r; o->g = g > 1 ? 1 : g; o->b = b > 1 ? 1 : b; o->a = 1;
	}
	m->n += md->n;
}

int scene_dbg_vm;
float scene_dbg_x = 0.17f, scene_dbg_d = 0.68f;                                                    // preview only: 1 = show the gun side-on, 2 = from above-behind (for judging the models)
static void viewmodel(const Game *g, const Scene *s)
{
	const Player *p = &g->p;
	if (!g->a[0].alive) return;
	const WeaponDef *wd = &weapons[p->weapon];
	const VMDef *vd = &VMD[p->weapon];
	const float O[3] = { 0, 0, 0 };
	float ads = p->ads * p->ads * (3 - 2 * p->ads);
	float fire = sat(p->kick / DEG(5.0f));
	float rec = p->flashT > 0 ? 1.0f : 0;
	// pose: hip <-> ads
	float adspos[3] = { -vd->sight[0], -vd->sight[1], -vd->adsDist - vd->sight[2] };
	float pos[3];
	for (int i = 0; i < 3; i++) pos[i] = lerpf(vd->hip[i], adspos[i], ads);
	float mv = p->moveFrac * (1.0f - ads * 0.85f);
	pos[0] += cosf(p->bob * 0.5f) * 0.007f * mv; pos[1] += fabsf(sinf(p->bob)) * 0.009f * mv - 0.004f * mv;
	pos[2] += (0.03f * rec + fire * 0.03f) * (1.0f - 0.4f * ads);
	pos[1] += fire * 0.006f;
	float pitch = fire * 0.05f + p->lookVel[1] * 0.025f, yaw = -p->lookVel[0] * 0.03f + (1 - ads) * 0.045f, roll = (1 - ads) * -0.02f - p->lookVel[0] * 0.03f;
	// swap / reload / throw / heal animations
	float swap = 0, rel = 0;
	if (p->swapT > 0) swap = sinf(PI_F * (1 - p->swapT / 0.34f));
	if (p->reloading) { float u = 1 - p->reloadT / wd->reload; rel = sinf(PI_F * sat(u)); }
	float magOut = (p->reloading && (1 - p->reloadT / wd->reload) > 0.25f && (1 - p->reloadT / wd->reload) < 0.62f) ? 1.0f : 0.0f;
	float dip = swap * 0.3f + rel * 0.14f, thr = 0, heal = 0;
	if (p->throwT > 0) thr = sinf(PI_F * (1 - p->throwT / 0.55f));
	if (p->healT > 0) heal = 1.0f;
	dip += thr * 0.2f + heal * 0.5f + p->sprint * 0.07f;
	pos[1] -= dip; roll += rel * 0.35f + swap * 0.3f - p->sprint * 0.45f; pitch += rel * 0.25f - swap * 0.2f - p->sprint * 0.35f; yaw += p->sprint * 0.5f; pos[0] -= p->sprint * 0.05f;
	M G = m_mul(m_mul(m_tr(pos[0], pos[1], pos[2]), m_mul(m_ry(yaw), m_rx(pitch))), m_rz(roll));
	if (scene_dbg_vm == 1) G = m_mul(m_tr(scene_dbg_x, 0.0f, -scene_dbg_d), m_mul(m_ry(1.5708f), m_rx(0.0f)));
	if (scene_dbg_vm == 2) G = m_mul(m_tr(0.05f, -0.08f, -0.8f), m_mul(m_ry(-0.55f), m_rx(0.45f)));
	float pump = 0;
	if (p->weapon == W_SHOTGUN) { float u = 1 - sat(g->a[0].fireT / wd->rate); pump = u < 0.2f ? 0 : sinf(PI_F * sat((u - 0.2f) / 0.6f)); }
	{
		int flags = (ads > 0.55f ? 1 : 0) | (magOut > 0.5f ? 2 : 0) | (heal > 0 ? 4 : 0);
		const Model *md = vm_model(p->weapon, flags);
		emit_vm_model(&dm[D_VM], md, &G);
		for (int i = 0; i < vd->n; i++) {                              // the shotgun pump slides back and forth: the only part built live
			const Part *pt = &vd->parts[i];
			if (pt->tag != 2) continue;
			const float zero[3] = { 0, 0, 0 };
			M T = m_mul(G, m_mul(m_tr(pt->c[0], pt->c[1], pt->c[2] + pump * 0.12f), m_mul(m_ry(pt->ry), m_rx(pt->rx))));
			g_tx = PAL_TX[pt->col]; g_tint[0] = g_tint[1] = g_tint[2] = PAL_TINT[pt->col];
			EC e = dyn_ec(&dm[D_VM], &T, PAL[pt->col], LM_VM, O);
			tess_rbox(&e, zero, pt->h, pt->rad);
			g_tx = 0; g_tint[0] = g_tint[1] = g_tint[2] = 1.0f;
		}
	}
	// red dot in the sight window (a glow on the sight plane)
	if (vd->dot) {
		float sp[3] = { vd->sight[0], vd->sight[1] - 0.001f, vd->sight[2] - 0.0f }, w[3];
		m_pt(&G, sp, w);
		const float red[3] = { 1.0f, 0.12f, 0.1f };
		static const float R[3] = { 1, 0, 0 }, U[3] = { 0, 1, 0 };
		bb(&dm[D_VMADD], w, 0.0045f + 0.0015f * ads, 0, red, 1.0f, R, U);
	}
	// muzzle flash: crossed glow quads in front of the barrel
	if (p->flashT > 0) {
		float mzl[3] = { vd->muzzle[0], vd->muzzle[1], vd->muzzle[2] - 0.03f }, w[3];
		m_pt(&G, mzl, w);
		static const float R[3] = { 1, 0, 0 }, U[3] = { 0, 1, 0 };
		const float c1[3] = { 1.0f, 0.78f, 0.38f }, c2[3] = { 1.0f, 0.95f, 0.8f };
		float rot = g->time * 77.0f;
		float big = p->weapon == W_SHOTGUN ? 0.2f : 0.12f;
		bb(&dm[D_VMADD], w, big * (1 - ads * 0.5f), rot, c1, 1.0f, R, U);
		bb(&dm[D_VMADD], w, big * 0.5f * (1 - ads * 0.5f), rot + 0.8f, c2, 1.0f, R, U);
		float w2[3] = { w[0], w[1], w[2] + 0.05f };
		bb(&dm[D_VMADD], w2, big * 0.55f, rot * 0.5f, c1, 0.8f, R, U);
	}
	// grenade in hand while throwing, medkit while healing
	if (thr > 0 && p->throwT > 0.25f) {
		float u = 1 - p->throwT / 0.55f;
		float gp[3] = { 0.02f + u * 0.05f, -0.12f + u * 0.25f, -0.32f - u * 0.15f };
		M T = m_mul(m_tr(gp[0], gp[1], gp[2]), m_rx(u * 2));
		const float zero[3] = { 0, 0, 0 }, r1[3] = { 0.034f, 0.046f, 0.034f }, r2[3] = { 0.048f, 0.048f, 0.052f }, green[3] = { 0.15f, 0.22f, 0.12f };
		{ EC e = dyn_ec(&dm[D_VM], &T, green, LM_VM, O); tess_ell(&e, zero, r1, 8, 5); }
		const float cap[3] = { 0, 0.05f, 0 }, hc[3] = { 0.014f, 0.012f, 0.014f }, metal[3] = { 0.5f, 0.5f, 0.5f };
		{ EC e = dyn_ec(&dm[D_VM], &T, metal, LM_VM, O); tess_ell(&e, cap, hc, 6, 3); }
		M Hd = m_tr(gp[0] - 0.01f, gp[1] - 0.04f, gp[2] + 0.02f);
		{ EC e = dyn_ec(&dm[D_VM], &Hd, PAL[3], LM_VM, O); tess_ell(&e, zero, r2, 8, 5); }
	}
	if (heal > 0) {
		float u = 1 - p->healT / 1.5f;
		float lift = sinf(PI_F * sat(u * 1.1f));
		M T = m_mul(m_tr(0.02f + u * 0.02f, -0.16f + lift * 0.1f, -0.33f), m_rz(0.15f));
		const float zero[3] = { 0, 0, 0 }, hk[3] = { 0.075f, 0.045f, 0.04f };
		{ EC e = dyn_ec(&dm[D_VM], &T, PAL[7], LM_VM, O); tess_rbox(&e, zero, hk, 0.012f); }
		const float hx[3] = { 0.03f, 0.008f, 0.008f }, hy[3] = { 0.008f, 0.03f, 0.008f }, cx[3] = { 0, 0, 0.043f };
		obox(&dm[D_VM], &T, cx, hx, PAL[6], LM_VM, O); obox(&dm[D_VM], &T, cx, hy, PAL[6], LM_VM, O);
		M Hd = m_tr(0.02f + u * 0.02f, -0.19f + lift * 0.1f, -0.27f);
		const float hh[3] = { 0.05f, 0.045f, 0.05f };
		{ EC e = dyn_ec(&dm[D_VM], &Hd, PAL[3], LM_VM, O); tess_ell(&e, zero, hh, 8, 5); }
	}
	// ejected shells (view space)
	for (int i = 0; i < MAX_SHELL; i++) {
		const Shell *sh = &g->shell[i];
		if (sh->life <= 0) continue;
		M T2 = m_mul(m_tr(sh->p[0], sh->p[1], sh->p[2]), m_mul(m_ry(sh->rot), m_rx(sh->rot * 0.7f)));
		const float zero[3] = { 0, 0, 0 };
		EC e = dyn_ec(&dm[D_VM], &T2, PAL[2], LM_VM, O); tess_cyl(&e, zero, 0.014f, 0.0055f, 0.0055f, 0.0055f, 0.0055f, 5, true);
	}
	(void)s;
}

// ---- main dynamic build ---------------------------------------------------------------------------------------------------
void scene_build(const Game *g, Scene *s)
{
	// camera
	float cy = cosf(g->cam.yaw), sy = sinf(g->cam.yaw), cp = cosf(g->cam.pitch), sp = sinf(g->cam.pitch);
	float f[3] = { sy * cp, sp, -cy * cp };
	float r0[3] = { cy, 0, sy };
	float u0[3] = { r0[1] * f[2] - r0[2] * f[1], r0[2] * f[0] - r0[0] * f[2], r0[0] * f[1] - r0[1] * f[0] };
	float cr = cosf(g->cam.roll), sr = sinf(g->cam.roll);
	float R[3], U[3];
	for (int k = 0; k < 3; k++) { R[k] = r0[k] * cr + u0[k] * sr; U[k] = -r0[k] * sr + u0[k] * cr; }
	memcpy(s->eye, g->cam.eye, sizeof s->eye); memcpy(s->right, R, sizeof R); memcpy(s->up, U, sizeof U); memcpy(s->fwd, f, sizeof f);
	for (int k = 0; k < 3; k++) {
		s->view[k] = R[k]; s->view[4 + k] = U[k]; s->view[8 + k] = -f[k];
		s->skyview[k] = R[k]; s->skyview[4 + k] = U[k]; s->skyview[8 + k] = -f[k];
		s->skyview[k * 4 + 3] = 0;
	}
	s->view[3] = -(R[0] * s->eye[0] + R[1] * s->eye[1] + R[2] * s->eye[2]);
	s->view[7] = -(U[0] * s->eye[0] + U[1] * s->eye[1] + U[2] * s->eye[2]);
	s->view[11] = (f[0] * s->eye[0] + f[1] * s->eye[1] + f[2] * s->eye[2]);
	s->view[12] = s->view[13] = s->view[14] = 0; s->view[15] = 1;
	s->skyview[3] = s->skyview[7] = s->skyview[11] = 0; s->skyview[12] = s->skyview[13] = s->skyview[14] = 0; s->skyview[15] = 1;
	s->fov = g->cam.fov; s->vmfov = 58.0f; s->focus = 6.0f;
	memcpy(EYE, s->eye, sizeof EYE); memcpy(CR, R, sizeof CR); memcpy(CU, U, sizeof CU);

	static const int caps[D_COUNT] = { CAP_D_OPAQUE, CAP_D_ALPHA, CAP_D_ADD, CAP_D_VM, CAP_D_VMADD };
	int off = 0;
	for (int i = 0; i < D_COUNT; i++) { dm[i].v = s->dyn + off; dm[i].n = 0; dm[i].cap = caps[i]; s->first[i] = off; off += caps[i]; }

	// soldiers (nearest first so the high-detail budget goes to the closest ones)
	g_quality = g->qualityActive; g_hi_used = 0;
	for (int i = 1; i < MAX_ACT; i++) {
		const Actor *a = &g->a[i];
		if (!a->alive && a->deadT > 3.0f) continue;
		float dx = a->x - EYE[0], dz = a->z - EYE[2];
		if (dx * dx + dz * dz > 100.0f * 100.0f) continue;
		{ float fl = sqrtf(s->fwd[0] * s->fwd[0] + s->fwd[2] * s->fwd[2]) + 1e-6f, d2 = dx * dx + dz * dz;       // outside the view cone (60 deg each side)
		  if (d2 > 16.0f && (dx * s->fwd[0] + dz * s->fwd[2]) / fl < 0.5f * sqrtf(d2)) continue; }
		soldier(g, a);
	}
	// pickups
	for (int i = 0; i < g->w.npick; i++) {
		const Pickup *pk = &g->w.pick[i];
		if (pk->respawn > 0) continue;
		float bob = 0.55f + sinf(g->time * 2.0f + i) * 0.07f;
		M T = m_mul(m_tr(pk->x, bob, pk->z), m_ry(g->time * 1.5f));
		const float zero[3] = { 0, 0, 0 };
		float glow[3];
		if (pk->type == 0) {
			const float h[3] = { 0.22f, 0.13f, 0.14f }, col[3] = { 0.25f, 0.3f, 0.2f }, band[3] = { 0.9f, 0.75f, 0.2f };
			box_at(&dm[D_OPAQUE], &T, zero, h, col, 0.03f);
			const float hb[3] = { 0.225f, 0.03f, 0.145f }; box_at(&dm[D_OPAQUE], &T, zero, hb, band, 0.0f);
			glow[0] = 1.0f; glow[1] = 0.8f; glow[2] = 0.3f;
		} else if (pk->type == 1) {
			const float h[3] = { 0.2f, 0.14f, 0.14f }, col[3] = { 0.92f, 0.92f, 0.92f }, red[3] = { 0.85f, 0.1f, 0.1f };
			box_at(&dm[D_OPAQUE], &T, zero, h, col, 0.03f);
			const float hx[3] = { 0.12f, 0.03f, 0.145f }, hy[3] = { 0.03f, 0.12f, 0.145f };
			box_at(&dm[D_OPAQUE], &T, zero, hx, red, 0.0f); box_at(&dm[D_OPAQUE], &T, zero, hy, red, 0.0f);
			glow[0] = 0.4f; glow[1] = 1.0f; glow[2] = 0.45f;
		} else {
			const float h[3] = { 0.07f, 0.1f, 0.07f }, col[3] = { 0.15f, 0.25f, 0.12f };
			const float c1[3] = { -0.09f, 0, 0 }, c2[3] = { 0.09f, 0, 0 };
			{ const float rr[3] = { 0.06f, 0.1f, 0.06f }; ell_at(&dm[D_OPAQUE], &T, c1, rr, col, 7, 4); ell_at(&dm[D_OPAQUE], &T, c2, rr, col, 7, 4); }
			glow[0] = 0.9f; glow[1] = 0.9f; glow[2] = 0.9f;
		}
		const float gl[3] = { glow[0] * 0.5f, glow[1] * 0.5f, glow[2] * 0.5f };
		flat(&dm[D_ADD], pk->x, 0.04f, pk->z, 0.6f, gl, 0.9f);
	}
	// grenades
	for (int i = 0; i < MAX_GREN; i++) {
		const Grenade *n = &g->gren[i];
		if (!n->live) continue;
		M T = m_mul(m_tr(n->p[0], n->p[1], n->p[2]), m_mul(m_ry(g->time * 9 + i), m_rx(g->time * 7)));
		const float zero[3] = { 0, 0, 0 }, h[3] = { 0.055f, 0.075f, 0.055f }, col[3] = { 0.16f, 0.24f, 0.13f };
		ell_at(&dm[D_OPAQUE], &T, zero, h, col, 8, 5);
		const float k[3] = { 0, 0, 0 };
		flat(&dm[D_ALPHA], n->p[0], 0.03f, n->p[2], 0.25f, k, 0.4f);
		if (n->t < 0.6f && ((int)(n->t * 12) & 1)) { const float rr[3] = { 1.0f, 0.2f, 0.1f }; bb(&dm[D_ADD], n->p, 0.15f, 0, rr, 1.0f, CR, CU); }
	}
	// tracers: a short bright streak racing from the muzzle to the impact point
	for (int i = 0; i < MAX_TRACER; i++) {
		const Tracer *t = &g->tr[i];
		if (t->life <= 0) continue;
		float u = 1.0f - t->life / 0.07f, len = 0, d[3] = { t->b[0] - t->a[0], t->b[1] - t->a[1], t->b[2] - t->a[2] };
		len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (len < 1.5f) continue;
		float head = u * len + 1.0f, tail = head - 7.0f;
		if (tail < 0.8f) tail = 0.8f;
		if (head > len) head = len;
		if (tail >= head) continue;
		float a[3], b[3];
		for (int k = 0; k < 3; k++) { a[k] = t->a[k] + d[k] / len * tail; b[k] = t->a[k] + d[k] / len * head; }
		const float col[3] = { 1.0f, 0.85f, 0.5f };
		streak(&dm[D_ADD], a, b, 0.018f, col, 0.9f);
	}
	// bullet holes
	for (int i = 0; i < MAX_DECAL; i++) {
		const Decal *d = &g->dec[i];
		if (d->life <= 0) continue;
		float a = sat(d->life / 3.0f) * 0.7f;
		const float k[3] = { 0, 0, 0 };
		float rr[3], uu[3];
		if (fabsf(d->n[1]) > 0.5f) { rr[0] = 1; rr[1] = 0; rr[2] = 0; uu[0] = 0; uu[1] = 0; uu[2] = 1; }
		else if (fabsf(d->n[0]) > 0.5f) { rr[0] = 0; rr[1] = 0; rr[2] = 1; uu[0] = 0; uu[1] = 1; uu[2] = 0; }
		else { rr[0] = 1; rr[1] = 0; rr[2] = 0; uu[0] = 0; uu[1] = 1; uu[2] = 0; }
		bb(&dm[D_ALPHA], d->p, 0.07f, 0, k, a, rr, uu);
	}
	// particles
	for (int i = 0; i < g->fx_n; i++) {
		const Fx *f2 = &g->fx[i];
		float t = f2->life / f2->max;
		float col[3] = { f2->r, f2->g, f2->b };
		if (f2->kind == 1) bb(&dm[D_ALPHA], f2->p, f2->size, i * 1.3f, col, t * 0.55f, CR, CU);
		else if (f2->kind == 2) { float c[3] = { col[0] * t, col[1] * t, col[2] * t }; bb(&dm[D_ADD], f2->p, f2->size, 0, c, 1.0f, CR, CU); }
		else { float c[3] = { col[0] * t, col[1] * t, col[2] * t }; bb(&dm[D_ADD], f2->p, f2->size, i * 0.7f, c, 1.0f, CR, CU); }
	}
	viewmodel(g, s);
	for (int i = 0; i < D_COUNT; i++) s->count[i] = dm[i].n;
}

void scene_prebuild(int quality)
{
	for (int team = 0; team < 2; team++) for (int lod = quality ? 0 : 1; lod < 3; lod++) for (int pose = 0; pose < 9; pose++) soldier_model(team != 0, lod, pose);
	for (int w = 0; w < W_COUNT; w++) { vm_model(w, 0); vm_model(w, 1); }
}

bool scene_project(const Scene *s, const float p[3], float *sx, float *sy)
{
	const float *m = s->view;
	float x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
	float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
	float z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
	if (z > -0.3f) return false;
	float f = 1.0f / tanf(s->fov * 0.5f * PI_F / 180.0f), asp = 400.0f / 240.0f;
	*sx = (x * f / asp / -z * 0.5f + 0.5f) * 400.0f;
	*sy = (1.0f - (y * f / -z * 0.5f + 0.5f)) * 240.0f;
	return true;
}
