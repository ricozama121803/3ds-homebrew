// Web Slinger - scene builder. The static world (sky, ground, water, buildings, shadows, trees) is built once into one buffer sorted by
// texture; the hero, web, cars and beacon are rebuilt every frame. Lighting is baked into vertex colours (sun + sky ambient).
#include "scene.h"
#include "hero_mesh.h"
#include "props_mesh.h"
#include <stdlib.h>

// ---- meshes ---------------------------------------------------------------------------------------------------------------
typedef struct { Vtx *v; int n, cap; } Mesh;
typedef struct { float p[3], u, v, c[3], a; } SV;
typedef struct { float x, y, z; } V3;

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

static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static inline V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float vlen(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = vlen(a); return l > 1e-6f ? mul(a, 1.0f / l) : v3(0, 1, 0); }
static inline V3 lerp3(V3 a, V3 b, float t) { return add(a, mul(sub(b, a), t)); }
static inline float sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// ---- lighting: sun high in the south-west, blue sky ambient --------------------------------------------------------------------
static const float SUN[3] = { -0.55f, 0.68f, -0.49f };
static float shade(float nx, float ny, float nz)
{
	float d = nx * SUN[0] + ny * SUN[1] + nz * SUN[2];
	if (d < 0) d = 0;
	float s = 0.62f + 0.08f * ny + 0.52f * d;
	return s > 1.08f ? 1.08f : s;
}
static float shadev(V3 n) { return shade(n.x, n.y, n.z); }

// =============================================================================================================================
// static world
// =============================================================================================================================
static const Game *SG;
#define DETAIL_RANGE 150.0f                                   // props and cornices are drawn only this close (chunk edge distance)
static Mesh sm[GRP_COUNT][NCH + 1];
static int curChunk = NCH;
#define SM(g) sm[g][curChunk]
static void chunk_at(float x, float z)
{
	int i = (int)(x / PITCH) / CH_BLOCKS, j = (int)(z / PITCH) / CH_BLOCKS;
	if (x < 0 || z < 0 || i >= CH_N || j >= CH_N) { curChunk = NCH; return; }
	curChunk = j * CH_N + i;
}
bool scene_chunk_visible(const Scene *s, int c)
{
	if (c >= NCH) return true;
	float cx = ((c % CH_N) + 0.5f) * CH_BLOCKS * PITCH, cz = ((c / CH_N) + 0.5f) * CH_BLOCKS * PITCH, cy = 120.0f, r = 0.5f * CH_BLOCKS * PITCH * 1.42f + 135.0f;
	float vx = cx - s->eye[0], vy = cy - s->eye[1], vz = cz - s->eye[2], d = sqrtf(vx * vx + vy * vy + vz * vz);
	if (d < r) return true;
	if (d - r > 820.0f) return false;
	float ca = (vx * s->fwd[0] + vy * s->fwd[1] + vz * s->fwd[2]) / d;
	float half = atanf(tanf(s->fov * 0.5f * PI_F / 180.0f) * 1.667f);                          // vertical -> horizontal half angle (400x240), the corner of the screen is a bit more
	float lim = sqrtf(half * half + (s->fov * 0.5f * PI_F / 180.0f) * (s->fov * 0.5f * PI_F / 180.0f)) + 0.12f + asinf(r / d);
	return acosf(clampf(ca, -1.0f, 1.0f)) < lim;
}

static void wall(const Box *b, int side)
{
	float ax, az, bx, bz, nx = 0, nz = 0;
	switch (side) {
	case 0: ax = b->x0; bx = b->x1; az = bz = b->z0; nz = -1; break;
	case 1: ax = b->x0; bx = b->x1; az = bz = b->z1; nz = 1; break;
	case 2: ax = bx = b->x0; az = b->z0; bz = b->z1; nx = -1; break;
	default: ax = bx = b->x1; az = b->z0; bz = b->z1; nx = 1; break;
	}
	float us, vs;
	switch (b->mat) { case MAT_GLASS: us = 1 / 10.0f; vs = 1 / 15.2f; break; case MAT_COPPER: us = vs = 0.1f; break; default: us = 1 / 12.0f; vs = 1 / 7.6f; }
	float ca = side < 2 ? ax : az, cb = side < 2 ? bx : bz;
	float uoff = (b->tint[0] * 37.0f - floorf(b->tint[0] * 37.0f)) * 3.0f;
	float sh = shade(nx, 0, nz);
	bool shop = b->kind == BK_BLDG && b->yb == 0.0f && b->y1 > 8.0f && b->mat != MAT_COPPER && b->bld >= 0 && SG && (b->bld % 3) != 0;   // two buildings in three have a shopfront
	float ys[4]; int ny = 0;
	float yo = shop ? 5.0f : 0.0f;
	ys[ny++] = b->yb;
	if (shop) ys[ny++] = 5.0f;
	if (b->yb < 24.0f && b->y1 > 24.0f && 24.0f > ys[ny - 1]) ys[ny++] = 24.0f;
	ys[ny] = b->y1;
	for (int i = 0; i < ny; i++) {
		float y0 = ys[i], y1 = ys[i + 1];
		int mt = (shop && i == 0) ? MAT_STORE : b->mat;
		float usx = mt == MAT_STORE ? 1 / 12.0f : us, vsx = mt == MAT_STORE ? 1 / 5.0f : vs, voff = mt == MAT_STORE ? 0.0f : yo;
		float k0 = sh * (0.58f + 0.42f * sat(y0 / 24.0f)), k1 = sh * (0.58f + 0.42f * sat(y1 / 24.0f));
		float c0[3] = { b->tint[0] * k0, b->tint[1] * k0, b->tint[2] * k0 }, c1[3] = { b->tint[0] * k1, b->tint[1] * k1, b->tint[2] * k1 };
		if (b->mat == MAT_COPPER) { for (int k = 0; k < 3; k++) { c0[k] = k0; c1[k] = k1; } }
		SV a = sv(ax, y0, az, (ca * usx) + uoff, (y0 - voff) * vsx, c0, 1), bb = sv(bx, y0, bz, (cb * usx) + uoff, (y0 - voff) * vsx, c0, 1);
		SV c = sv(bx, y1, bz, (cb * usx) + uoff, (y1 - voff) * vsx, c1, 1), d = sv(ax, y1, az, (ca * usx) + uoff, (y1 - voff) * vsx, c1, 1);
		quad(&SM(mt), &a, &bb, &c, &d);
	}
}

static void roof_top(const Box *b)
{
	float k = shade(0, 1, 0) * 0.93f, c[3] = { k * b->tint[0], k * b->tint[1], k * b->tint[2] };
	SV a = sv(b->x0, b->y1, b->z0, b->x0 / 8, b->z0 / 8, c, 1), bb = sv(b->x1, b->y1, b->z0, b->x1 / 8, b->z0 / 8, c, 1);
	SV cc = sv(b->x1, b->y1, b->z1, b->x1 / 8, b->z1 / 8, c, 1), d = sv(b->x0, b->y1, b->z1, b->x0 / 8, b->z1 / 8, c, 1);
	quad(&SM(MAT_ROOF), &a, &bb, &cc, &d);
}

// a pyramid (cap or spire): apex above the middle of the base rectangle
static void pyramid(int mat, float x0, float z0, float x1, float z1, float y, float h, const float tint[3], bool textured)
{
	float cx = 0.5f * (x0 + x1), cz = 0.5f * (z0 + z1);
	float bx[4] = { x0, x1, x1, x0 }, bz[4] = { z0, z0, z1, z1 };
	for (int i = 0; i < 4; i++) {
		int j = (i + 1) % 4;
		V3 e1 = v3(bx[j] - bx[i], 0, bz[j] - bz[i]), e2 = v3(cx - bx[i], h, cz - bz[i]), n = norm(cross(e1, e2));
		if (n.y < 0) n = mul(n, -1);
		float sh = shadev(n), c[3] = { sh * tint[0], sh * tint[1], sh * tint[2] }, ct[3] = { sh * tint[0] * 1.15f, sh * tint[1] * 1.15f, sh * tint[2] * 1.15f };
		SV a = sv(bx[i], y, bz[i], bx[i] / 8, y / 8, c, 1), b = sv(bx[j], y, bz[j], bx[j] / 8, y / 8, c, 1), t = sv(cx, y + h, cz, cx / 8, (y + h) / 8, textured ? ct : c, 1);
		if (!textured) { a.u = a.v = b.u = b.v = t.u = t.v = 0.5f; }
		tri(&SM(mat), &a, &b, &t);
	}
}

static void water_tower(const Box *b)
{
	float cx = 0.5f * (b->x0 + b->x1), cz = 0.5f * (b->z0 + b->z1), y = b->y0, r = 1.7f;
	static const float wood[3] = { 0.42f, 0.28f, 0.19f }, dark[3] = { 0.24f, 0.24f, 0.27f };
	for (int i = 0; i < 4; i++) {                                    // legs
		float lx = cx + ((i & 1) ? 1.2f : -1.2f), lz = cz + ((i & 2) ? 1.2f : -1.2f);
		float c[3] = { 0.25f * 1.4f, 0.22f * 1.4f, 0.2f * 1.4f };
		SV a = sv(lx - 0.12f, y, lz, 0.5f, 0.5f, c, 1), bb = sv(lx + 0.12f, y, lz, 0.5f, 0.5f, c, 1), cc = sv(lx + 0.12f, y + 3.0f, lz, 0.5f, 0.5f, c, 1), d = sv(lx - 0.12f, y + 3.0f, lz, 0.5f, 0.5f, c, 1);
		quad(&SM(MAT_WHITE), &a, &bb, &cc, &d);
	}
	const int N = 8;
	for (int i = 0; i < N; i++) {
		float a0 = i * 2 * PI_F / N, a1 = (i + 1) * 2 * PI_F / N, am = 0.5f * (a0 + a1);
		float sh = shade(cosf(am), 0, sinf(am)), c[3] = { wood[0] * sh * 1.7f, wood[1] * sh * 1.7f, wood[2] * sh * 1.7f };
		SV p0 = sv(cx + r * cosf(a0), y + 3.0f, cz + r * sinf(a0), 0.5f, 0.5f, c, 1), p1 = sv(cx + r * cosf(a1), y + 3.0f, cz + r * sinf(a1), 0.5f, 0.5f, c, 1);
		SV p2 = sv(cx + r * cosf(a1), y + 6.0f, cz + r * sinf(a1), 0.5f, 0.5f, c, 1), p3 = sv(cx + r * cosf(a0), y + 6.0f, cz + r * sinf(a0), 0.5f, 0.5f, c, 1);
		quad(&SM(MAT_WHITE), &p0, &p1, &p2, &p3);
		float sh2 = shade(cosf(am) * 0.5f, 0.85f, sinf(am) * 0.5f), c2[3] = { dark[0] * sh2 * 1.6f, dark[1] * sh2 * 1.6f, dark[2] * sh2 * 1.6f };
		SV t0 = sv(cx + (r + 0.1f) * cosf(a0), y + 6.0f, cz + (r + 0.1f) * sinf(a0), 0.5f, 0.5f, c2, 1), t1 = sv(cx + (r + 0.1f) * cosf(a1), y + 6.0f, cz + (r + 0.1f) * sinf(a1), 0.5f, 0.5f, c2, 1), t2 = sv(cx, y + 7.5f, cz, 0.5f, 0.5f, c2, 1);
		tri(&SM(MAT_WHITE), &t0, &t1, &t2);
	}
}

static void dome(const Box *b)
{
	float r = b->tint[0], cx = 0.5f * (b->x0 + b->x1), cz = 0.5f * (b->z0 + b->z1), y = b->y0;
	const int N = 10, R = 3;
	static const float ring_r[4] = { 1.0f, 0.87f, 0.5f, 0.0f }, ring_h[4] = { 0.0f, 0.5f, 0.87f, 1.0f };
	for (int k = 0; k < R; k++) for (int i = 0; i < N; i++) {
		float a0 = i * 2 * PI_F / N, a1 = (i + 1) * 2 * PI_F / N, am = 0.5f * (a0 + a1);
		float hm = 0.5f * (ring_h[k] + ring_h[k + 1]);
		float sh = shade(cosf(am) * (1 - hm), hm, sinf(am) * (1 - hm)), c[3] = { sh, sh, sh };
		float r0 = ring_r[k] * r, r1 = ring_r[k + 1] * r, y0 = y + ring_h[k] * r, y1 = y + ring_h[k + 1] * r;
		SV p0 = sv(cx + r0 * cosf(a0), y0, cz + r0 * sinf(a0), (cx + r0 * cosf(a0)) / 8, y0 / 8, c, 1), p1 = sv(cx + r0 * cosf(a1), y0, cz + r0 * sinf(a1), (cx + r0 * cosf(a1)) / 8, y0 / 8, c, 1);
		SV p2 = sv(cx + r1 * cosf(a1), y1, cz + r1 * sinf(a1), (cx + r1 * cosf(a1)) / 8, y1 / 8, c, 1), p3 = sv(cx + r1 * cosf(a0), y1, cz + r1 * sinf(a0), (cx + r1 * cosf(a0)) / 8, y1 / 8, c, 1);
		if (k == R - 1) tri(&SM(MAT_COPPER), &p0, &p1, &p2); else quad(&SM(MAT_COPPER), &p0, &p1, &p2, &p3);
	}
}


// ---- props from the Blender kit (props_mesh.h): placed with a yaw and a uniform scale, drawn with the white texture and baked colours --------
static void prop(int id, float x, float y, float z, float yaw, float sc, const float tint[3])
{
	const PropMesh *pm = &PROPS[id];
	float cy = cosf(yaw), sy = sinf(yaw);
	Mesh *m = &SM(GRP_DETAIL);
	for (int t = 0; t < pm->ni; t++) {
		const float *v = pm->v + pm->i[t] * 9;
		float nx = v[3] * cy + v[5] * sy, nz = -v[3] * sy + v[5] * cy;
		float k = shade(nx, v[4], nz) * (0.82f + 0.18f * sat(v[1] / 4.0f));            // a little darker toward the ground (fake ambient occlusion)
		float c[3] = { v[6] * k * tint[0] * 1.5f, v[7] * k * tint[1] * 1.5f, v[8] * k * tint[2] * 1.5f };
		for (int q = 0; q < 3; q++) if (c[q] > 1.2f) c[q] = 1.2f;
		SV a = sv(x + (v[0] * cy + v[2] * sy) * sc, y + v[1] * sc, z + (-v[0] * sy + v[2] * cy) * sc, 0.5f, 0.5f, c, 1);
		put(m, &a);
	}
}

// a stone ledge that overhangs the top of a building (outer faces + the ring on top, no underside)
static void cornice(const Box *b)
{
	const float o = 0.35f, h = 0.7f;
	float x0 = b->x0 - o, x1 = b->x1 + o, z0 = b->z0 - o, z1 = b->z1 + o, y1 = b->y1, y0 = y1 - h;
	float t = 0.93f, sh0 = shade(0, 0, -1), sh1 = shade(0, 0, 1), sh2 = shade(-1, 0, 0), sh3 = shade(1, 0, 0), st = shade(0, 1, 0);
	float tc[3] = { 0.9f * t, 0.88f * t, 0.82f * t };
	#define LC(k) (const float[3]){ tc[0] * (k), tc[1] * (k), tc[2] * (k) }
	SV a, b2, c, d;
	a = sv(x0, y0, z0, 0.5f, 0.5f, LC(sh0 * 0.75f), 1); b2 = sv(x1, y0, z0, 0.5f, 0.5f, LC(sh0 * 0.75f), 1); c = sv(x1, y1, z0, 0.5f, 0.5f, LC(sh0), 1); d = sv(x0, y1, z0, 0.5f, 0.5f, LC(sh0), 1); quad(&SM(GRP_DETAIL), &a, &b2, &c, &d);
	a = sv(x1, y0, z1, 0.5f, 0.5f, LC(sh1 * 0.75f), 1); b2 = sv(x0, y0, z1, 0.5f, 0.5f, LC(sh1 * 0.75f), 1); c = sv(x0, y1, z1, 0.5f, 0.5f, LC(sh1), 1); d = sv(x1, y1, z1, 0.5f, 0.5f, LC(sh1), 1); quad(&SM(GRP_DETAIL), &a, &b2, &c, &d);
	a = sv(x0, y0, z1, 0.5f, 0.5f, LC(sh2 * 0.75f), 1); b2 = sv(x0, y0, z0, 0.5f, 0.5f, LC(sh2 * 0.75f), 1); c = sv(x0, y1, z0, 0.5f, 0.5f, LC(sh2), 1); d = sv(x0, y1, z1, 0.5f, 0.5f, LC(sh2), 1); quad(&SM(GRP_DETAIL), &a, &b2, &c, &d);
	a = sv(x1, y0, z0, 0.5f, 0.5f, LC(sh3 * 0.75f), 1); b2 = sv(x1, y0, z1, 0.5f, 0.5f, LC(sh3 * 0.75f), 1); c = sv(x1, y1, z1, 0.5f, 0.5f, LC(sh3), 1); d = sv(x1, y1, z0, 0.5f, 0.5f, LC(sh3), 1); quad(&SM(GRP_DETAIL), &a, &b2, &c, &d);
	const float *tp = LC(st);
	float X[4] = { x0, b->x0, b->x1, x1 }, Z[4] = { z0, b->z0, b->z1, z1 };
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {                       // the 8 ring cells of the 3x3 grid (the middle one is the roof itself)
		if (i == 1 && j == 1) continue;
		a = sv(X[i], y1, Z[j], 0.5f, 0.5f, tp, 1); b2 = sv(X[i + 1], y1, Z[j], 0.5f, 0.5f, tp, 1); c = sv(X[i + 1], y1, Z[j + 1], 0.5f, 0.5f, tp, 1); d = sv(X[i], y1, Z[j + 1], 0.5f, 0.5f, tp, 1);
		quad(&SM(GRP_DETAIL), &a, &b2, &c, &d);
	}
	#undef LC
}

static unsigned hash2(int a, int b) { unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static float hf(unsigned h) { return (h & 0xFFFF) / 65536.0f; }

// street furniture: lamps near the corners and trees along the pavements (the pavement line is 11 m inside each tile edge)
static void street_props(void)
{
	const World *w = &SG->w;
	static const float lampT[3] = { 1, 1, 1 };
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		if (w->park[j][i]) continue;
		float x0 = i * PITCH, z0 = j * PITCH, e = 0.174f * PITCH;
		chunk_at(x0 + 1, z0 + 1);
		for (int s = 0; s < 4; s++) {
			unsigned h = hash2(i * 4 + s, j * 7 + 1);
			bool horiz = s < 2;                                                   // sides 0/1 run along x (north/south), 2/3 along z
			float fixed = s == 0 || s == 2 ? e : PITCH - e;
			float yaw = s == 0 ? 0 : s == 1 ? PI_F : s == 2 ? -PI_F / 2 : PI_F / 2;     // lamp arms reach toward the street
			float u0 = 0.2f * PITCH, u1 = 0.5f * PITCH, u2 = 0.8f * PITCH;
			#define PUT(id, u, y, sc, yw, tn) do { float px = horiz ? x0 + (u) : x0 + fixed, pz = horiz ? z0 + fixed : z0 + (u); prop(id, px, y, pz, yw, sc, tn); } while (0)
			if (hf(h) < 0.33f) PUT(PR_LAMP, u0 + (hf(h >> 3) - 0.5f) * 4.0f, 0.0f, 1.0f, yaw, lampT);
			if (hf(h >> 5) < 0.17f) {
				float tn[3] = { 0.85f + 0.3f * hf(h >> 7), 0.9f + 0.2f * hf(h >> 9), 0.85f + 0.3f * hf(h >> 11) };
				PUT(PR_TREE0 + (int)(hf(h >> 13) * 2.99f), u1 + (hf(h >> 4) - 0.5f) * 6.0f, 0.0f, 0.75f + 0.45f * hf(h >> 6), hf(h >> 8) * 6.28f, tn);
			}
			if (hf(h >> 10) < 0.04f) PUT(PR_HYDRANT, u2, 0.0f, 1.0f, 0.0f, lampT);
			#undef PUT
		}
	}
}

// rooftop clutter: air-conditioning units and vents, placed deterministically per building top
static void roof_props(const Box *b)
{
	float tw = b->x1 - b->x0, td = b->z1 - b->z0;
	if (tw < 9 || td < 9 || b->roof != RF_FLAT) return;
	unsigned h = hash2((int)(b->x0 * 7), (int)(b->z0 * 11));
	static const float one[3] = { 1, 1, 1 };
	if (hf(h) < 0.22f) prop(PR_AC, b->x0 + 2.5f + hf(h >> 3) * (tw - 5.0f), b->y1, b->z0 + 2.5f + hf(h >> 5) * (td - 5.0f), hf(h >> 7) < 0.5f ? 0.0f : PI_F / 2, 1.0f, one);
	if (hf(h >> 9) < 0.2f) prop(PR_VENT, b->x0 + 1.5f + hf(h >> 11) * (tw - 3.0f), b->y1, b->z0 + 1.5f + hf(h >> 13) * (td - 3.0f), 0.0f, 1.0f, one);
}

// ---- shadows: each building's silhouette projected along the sun onto the street --------------------------------------------------
typedef struct { float x, z; } P2;
static int hull(P2 *pts, int n, P2 *out)
{
	for (int i = 1; i < n; i++) { P2 k = pts[i]; int j = i - 1; while (j >= 0 && (pts[j].x > k.x || (pts[j].x == k.x && pts[j].z > k.z))) { pts[j + 1] = pts[j]; j--; } pts[j + 1] = k; }
	int k = 0;
	#define CR2(o, a, b) (((a).x - (o).x) * ((b).z - (o).z) - ((a).z - (o).z) * ((b).x - (o).x))
	for (int i = 0; i < n; i++) { while (k >= 2 && CR2(out[k - 2], out[k - 1], pts[i]) <= 0) k--; out[k++] = pts[i]; }
	for (int i = n - 2, t = k + 1; i >= 0; i--) { while (k >= t && CR2(out[k - 2], out[k - 1], pts[i]) <= 0) k--; out[k++] = pts[i]; }
	return k - 1;
}
static int clip_axis(P2 *in, int n, int axis, float val, bool keepGreater, P2 *out)
{
	int m = 0;
	for (int i = 0; i < n; i++) {
		P2 a = in[i], b = in[(i + 1) % n];
		float va = (axis ? a.z : a.x) - val, vb = (axis ? b.z : b.x) - val;
		if (!keepGreater) { va = -va; vb = -vb; }
		if (va >= 0) out[m++] = a;
		if ((va >= 0) != (vb >= 0)) { float t = va / (va - vb); out[m++] = (P2){ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t }; }
	}
	return m;
}
static void shadows(void)
{
	const World *w = &SG->w;
	float kx = -SUN[0] / SUN[1], kz = -SUN[2] / SUN[1];
	for (int i = 0; i < w->nbox;) {
		int j = i; P2 pts[40]; int n = 0;
		chunk_at(0.5f * (w->box[i].x0 + w->box[i].x1), 0.5f * (w->box[i].z0 + w->box[i].z1));
		while (j < w->nbox && w->box[j].bld == w->box[i].bld) {
			const Box *b = &w->box[j];
			if (b->kind == BK_BLDG && n + 8 <= 40) {
				float xs[2] = { b->x0, b->x1 }, zs[2] = { b->z0, b->z1 };
				for (int a = 0; a < 2; a++) for (int c = 0; c < 2; c++) {
					pts[n++] = (P2){ xs[a], zs[c] };
					pts[n++] = (P2){ xs[a] + kx * b->y1, zs[c] + kz * b->y1 };
				}
			}
			j++;
		}
		i = j;
		if (n < 3) continue;
		P2 h[82], a[82], b2[82];
		int hn = hull(pts, n, h);
		if (hn < 3) continue;
		int m = clip_axis(h, hn, 0, 0, true, a); if (m < 3) continue;
		m = clip_axis(a, m, 0, CITY, false, b2); if (m < 3) continue;
		m = clip_axis(b2, m, 1, 0, true, a); if (m < 3) continue;
		m = clip_axis(a, m, 1, CITY, false, b2); if (m < 3) continue;
		const float col[3] = { 0.02f, 0.05f, 0.14f };
		for (int t = 1; t + 1 < m; t++) {
			SV p0 = sv(b2[0].x, 0.12f, b2[0].z, 0.5f, 0.5f, col, 0.36f), p1 = sv(b2[t].x, 0.12f, b2[t].z, 0.5f, 0.5f, col, 0.36f), p2 = sv(b2[t + 1].x, 0.12f, b2[t + 1].z, 0.5f, 0.5f, col, 0.36f);
			tri(&SM(GRP_SHADOW), &p0, &p1, &p2);
		}
	}
}

// ---- ground, water, trees, backdrop ------------------------------------------------------------------------------------------------
static void ground(void)
{
	curChunk = NCH;
	const float c[3] = { 0.92f, 0.92f, 0.92f };
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		float x0 = i * PITCH, x1 = x0 + PITCH, z0 = j * PITCH, z1 = z0 + PITCH;
		chunk_at(x0 + 1, z0 + 1);
		SV a = sv(x0, 0, z0, 0, 0, c, 1), b = sv(x1, 0, z0, 1, 0, c, 1), cc = sv(x1, 0, z1, 1, 1, c, 1), d = sv(x0, 0, z1, 0, 1, c, 1);
		quad(&SM(MAT_ROAD), &a, &b, &cc, &d);
		if (SG->w.park[j][i]) {
			float gx0 = x0 + 0.148f * PITCH, gx1 = x0 + 0.852f * PITCH, gz0 = z0 + 0.148f * PITCH, gz1 = z0 + 0.852f * PITCH, gc[3] = { 0.85f, 0.88f, 0.85f };
			SV g0 = sv(gx0, 0.1f, gz0, gx0 / 12, gz0 / 12, gc, 1), g1 = sv(gx1, 0.1f, gz0, gx1 / 12, gz0 / 12, gc, 1), g2 = sv(gx1, 0.1f, gz1, gx1 / 12, gz1 / 12, gc, 1), g3 = sv(gx0, 0.1f, gz1, gx0 / 12, gz1 / 12, gc, 1);
			quad(&SM(MAT_GRASS), &g0, &g1, &g2, &g3);
			if (j == MID - 1 || j == MID + 3) {                      // a pond in the middle of the park
				float px0 = x0 + 0.3f * PITCH, px1 = x0 + 0.7f * PITCH, pz0 = z0 + 0.3f * PITCH, pz1 = z0 + 0.7f * PITCH, pc[3] = { 0.9f, 0.95f, 1.0f };
				SV q0 = sv(px0, 0.16f, pz0, px0 / 20, pz0 / 20, pc, 1), q1 = sv(px1, 0.16f, pz0, px1 / 20, pz0 / 20, pc, 1), q2 = sv(px1, 0.16f, pz1, px1 / 20, pz1 / 20, pc, 1), q3 = sv(px0, 0.16f, pz1, px0 / 20, pz1 / 20, pc, 1);
				quad(&SM(MAT_WATER), &q0, &q1, &q2, &q3);
			}
		}
	}
	curChunk = NCH;
	// the sea: a coarse grid so the per-vertex haze still works, and a quay wall around the island
	const int G = 24; const float ext = 1800.0f, cx = CITY * 0.5f, cell = 2 * ext / G;
	const float wc[3] = { 0.85f, 0.92f, 1.0f };
	for (int j = 0; j < G; j++) for (int i = 0; i < G; i++) {
		float x0 = cx - ext + i * cell, x1 = x0 + cell, z0 = cx - ext + j * cell, z1 = z0 + cell;
		SV a = sv(x0, WATER_Y, z0, x0 / 50, z0 / 50, wc, 1), b = sv(x1, WATER_Y, z0, x1 / 50, z0 / 50, wc, 1), cc = sv(x1, WATER_Y, z1, x1 / 50, z1 / 50, wc, 1), d = sv(x0, WATER_Y, z1, x0 / 50, z1 / 50, wc, 1);
		quad(&SM(MAT_WATER), &a, &b, &cc, &d);
	}
	Box q; memset(&q, 0, sizeof q);
	q.x0 = 0; q.z0 = 0; q.x1 = CITY; q.z1 = CITY; q.y0 = q.yb = WATER_Y; q.y1 = 0; q.mat = MAT_ROOF; q.tint[0] = q.tint[1] = q.tint[2] = 0.7f;
	for (int s = 0; s < 4; s++) wall(&q, s);
}

static void tree(Mesh *m, float x, float z, float size, const float col[3])
{
	static const float trunk[3] = { 0.30f, 0.20f, 0.12f };
	float h = 2.2f * size, r = 2.3f * size, cy = h + 1.6f * size;
	for (int i = 0; i < 2; i++) {
		float dx = i ? 0.14f : 0, dz = i ? 0 : 0.14f;
		SV a = sv(x - dx, 0, z - dz, 0.5f, 0.5f, trunk, 1), b = sv(x + dx, 0, z + dz, 0.5f, 0.5f, trunk, 1), c = sv(x + dx, h, z + dz, 0.5f, 0.5f, trunk, 1), d = sv(x - dx, h, z - dz, 0.5f, 0.5f, trunk, 1);
		quad(m, &a, &b, &c, &d);
	}
	for (int i = 0; i < 4; i++) {
		float a0 = i * PI_F / 2 + 0.4f, a1 = a0 + PI_F / 2, am = 0.5f * (a0 + a1);
		float e0x = x + r * cosf(a0), e0z = z + r * sinf(a0), e1x = x + r * cosf(a1), e1z = z + r * sinf(a1);
		float st = shade(cosf(am) * 0.5f, 0.8f, sinf(am) * 0.5f), sb = shade(cosf(am) * 0.8f, -0.4f, sinf(am) * 0.8f) * 0.8f;
		float ct[3] = { col[0] * st, col[1] * st, col[2] * st }, cb[3] = { col[0] * sb, col[1] * sb, col[2] * sb }, cm[3] = { col[0] * 0.55f, col[1] * 0.55f, col[2] * 0.55f };
		SV p0 = sv(e0x, cy, e0z, 0.5f, 0.5f, ct, 1), p1 = sv(e1x, cy, e1z, 0.5f, 0.5f, ct, 1), top = sv(x, cy + 2.2f * size, z, 0.5f, 0.5f, ct, 1);
		SV q0 = sv(e0x, cy, e0z, 0.5f, 0.5f, cb, 1), q1 = sv(e1x, cy, e1z, 0.5f, 0.5f, cb, 1), bot = sv(x, h * 0.9f, z, 0.5f, 0.5f, cm, 1);
		tri(m, &p0, &p1, &top); tri(m, &q0, &q1, &bot);
	}
}

static void trees(void)
{
	static const float pal[5][3] = { { 0.30f, 0.55f, 0.20f }, { 0.26f, 0.50f, 0.18f }, { 0.88f, 0.72f, 0.12f }, { 0.86f, 0.46f, 0.10f }, { 0.42f, 0.58f, 0.16f } };
	unsigned st = 0x7733u;
	const World *w = &SG->w;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		if (w->park[j][i]) {
			chunk_at(i * PITCH + 1, j * PITCH + 1);
			for (int k = 0; k < (SG->lowq ? 16 : 9); k++) {
				float x = (i + 0.2f + 0.6f * rndf(&st)) * PITCH, z = (j + 0.2f + 0.6f * rndf(&st)) * PITCH;
				if ((j == MID - 1 || j == MID + 3) && x > (i + 0.28f) * PITCH && x < (i + 0.72f) * PITCH && z > (j + 0.28f) * PITCH && z < (j + 0.72f) * PITCH) continue;   // pond
				float tn[3] = { 0.85f + 0.3f * rndf(&st), 0.9f + 0.2f * rndf(&st), 0.85f + 0.3f * rndf(&st) };
				if (SG->lowq) tree(&SM(MAT_WHITE), x, z, 1.1f + rndf(&st) * 0.9f, pal[rnd_next(&st) % 5]);
				else prop(PR_TREE0 + (int)(rndf(&st) * 2.99f), x, 0.0f, z, rndf(&st) * 6.28f, 1.0f + rndf(&st) * 0.7f, tn);
			}
			continue;
		}
	}
}

static void backdrop(void)
{
	curChunk = NCH;
	unsigned st = 0xBACD0u;
	for (int i = 0; i < 34; i++) {
		float a = i * 2 * PI_F / 34 + rndf(&st) * 0.1f, r = CITY * 0.5f + 330 + rndf(&st) * 330, cx = CITY * 0.5f + cosf(a) * r, cz = CITY * 0.5f + sinf(a) * r;
		float w = 30 + rndf(&st) * 50, d = 30 + rndf(&st) * 50, h = 50 + rndf(&st) * 170;
		Box b; memset(&b, 0, sizeof b);
		b.x0 = cx - w / 2; b.x1 = cx + w / 2; b.z0 = cz - d / 2; b.z1 = cz + d / 2; b.y0 = b.yb = WATER_Y; b.y1 = h;
		int r2 = (int)(rnd_next(&st) % 4); b.mat = r2 == 0 ? MAT_GLASS : r2 == 1 ? MAT_CREAM : r2 == 2 ? MAT_TAN : MAT_RED;
		float t = 0.85f + 0.2f * rndf(&st); b.tint[0] = b.tint[1] = b.tint[2] = t;
		for (int s = 0; s < 4; s++) wall(&b, s);
		roof_top(&b);
	}
}

// ---- sky -----------------------------------------------------------------------------------------------------------------------
static void build_sky(void)
{
	curChunk = NCH;
	static const float el[] = { -18, 0, 5, 12, 22, 36, 54, 75, 90 };
	const int NE = 9, NS = 32;
	const float R = 900.0f;
	static float col[9][32][3], pos[9][32][3];
	for (int i = 0; i < NE; i++) for (int j = 0; j < NS; j++) {
		float e = el[i] * PI_F / 180.0f, a = j * 2 * PI_F / NS;
		float d[3] = { sinf(a) * cosf(e), sinf(e), -cosf(a) * cosf(e) };
		for (int k = 0; k < 3; k++) pos[i][j][k] = d[k] * R;
		static const float hor[3] = { 0.80f, 0.88f, 0.97f }, mid[3] = { 0.46f, 0.67f, 0.94f }, top[3] = { 0.18f, 0.40f, 0.82f };
		float c[3];
		float t = sat(el[i] / 30.0f), t2 = sat((el[i] - 15) / 75.0f);
		for (int k = 0; k < 3; k++) c[k] = el[i] <= 0 ? hor[k] : lerpf(lerpf(hor[k], mid[k], sqrtf(t)), top[k], t2);
		float s = d[0] * SUN[0] + d[1] * SUN[1] + d[2] * SUN[2]; if (s < 0) s = 0;
		float g1 = powf(s, 8.0f), g2 = powf(s, 60.0f);
		c[0] += g1 * 0.22f + g2 * 0.4f; c[1] += g1 * 0.16f + g2 * 0.35f; c[2] += g1 * 0.06f + g2 * 0.2f;
		for (int k = 0; k < 3; k++) col[i][j][k] = sat(c[k]);
	}
	for (int i = 0; i + 1 < NE; i++) for (int j = 0; j < NS; j++) {
		int j2 = (j + 1) % NS;
		SV a = sv(pos[i][j][0], pos[i][j][1], pos[i][j][2], 0.5f, 0.5f, col[i][j], 1), b = sv(pos[i][j2][0], pos[i][j2][1], pos[i][j2][2], 0.5f, 0.5f, col[i][j2], 1);
		SV c = sv(pos[i + 1][j2][0], pos[i + 1][j2][1], pos[i + 1][j2][2], 0.5f, 0.5f, col[i + 1][j2], 1), d = sv(pos[i + 1][j][0], pos[i + 1][j][1], pos[i + 1][j][2], 0.5f, 0.5f, col[i + 1][j], 1);
		quad(&SM(GRP_SKY), &a, &b, &c, &d);
	}
	unsigned st = 0xC10D5u;
	for (int i = 0; i < 16; i++) {                                    // cloud puffs
		float a = i * 2 * PI_F / 16 + rndf(&st) * 0.3f, e = DEG(14 + rndf(&st) * 40);
		float d[3] = { sinf(a) * cosf(e), sinf(e), -cosf(a) * cosf(e) };
		float up[3] = { 0, 1, 0 }, r[3] = { up[1] * d[2] - up[2] * d[1], up[2] * d[0] - up[0] * d[2], up[0] * d[1] - up[1] * d[0] };
		float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); for (int k = 0; k < 3; k++) r[k] /= rl;
		float u[3] = { d[1] * r[2] - d[2] * r[1], d[2] * r[0] - d[0] * r[2], d[0] * r[1] - d[1] * r[0] };
		float w = 150.0f + rndf(&st) * 170.0f, h = w * 0.45f, rr = 800.0f;
		const float c[3] = { 1.0f, 1.0f, 1.0f };
		float P[4][3];
		for (int k = 0; k < 4; k++) { float sx = (k == 0 || k == 3) ? -1.0f : 1.0f, sy = (k < 2) ? -1.0f : 1.0f; for (int q = 0; q < 3; q++) P[k][q] = d[q] * rr + r[q] * w * sx + u[q] * h * sy; }
		SV a0 = sv(P[0][0], P[0][1], P[0][2], 0, 0, c, 0.95f), b0 = sv(P[1][0], P[1][1], P[1][2], 1, 0, c, 0.95f), c0 = sv(P[2][0], P[2][1], P[2][2], 1, 1, c, 0.95f), d0 = sv(P[3][0], P[3][1], P[3][2], 0, 1, c, 0.95f);
		quad(&SM(GRP_CLOUD), &a0, &b0, &c0, &d0);
	}
	{                                                                    // sun glow
		const float *S = SUN;
		float up[3] = { 0, 1, 0 }, r[3] = { up[1] * S[2] - up[2] * S[1], up[2] * S[0] - up[0] * S[2], up[0] * S[1] - up[1] * S[0] };
		float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); for (int k = 0; k < 3; k++) r[k] /= rl;
		float u[3] = { S[1] * r[2] - S[2] * r[1], S[2] * r[0] - S[0] * r[2], S[0] * r[1] - S[1] * r[0] };
		static const struct { float size; float c[3]; float a; } layers[2] = { { 260.0f, { 1.0f, 0.9f, 0.6f }, 0.35f }, { 60.0f, { 1.0f, 0.98f, 0.9f }, 1.0f } };
		for (int l = 0; l < 2; l++) {
			float P[4][3];
			for (int k = 0; k < 4; k++) { float sx = (k == 0 || k == 3) ? -1.0f : 1.0f, sy = (k < 2) ? -1.0f : 1.0f; for (int q = 0; q < 3; q++) P[k][q] = S[q] * 850.0f + r[q] * layers[l].size * sx + u[q] * layers[l].size * sy; }
			SV a0 = sv(P[0][0], P[0][1], P[0][2], 0, 0, layers[l].c, layers[l].a), b0 = sv(P[1][0], P[1][1], P[1][2], 1, 0, layers[l].c, layers[l].a), c0 = sv(P[2][0], P[2][1], P[2][2], 1, 1, layers[l].c, layers[l].a), d0 = sv(P[3][0], P[3][1], P[3][2], 0, 1, layers[l].c, layers[l].a);
			quad(&SM(GRP_SUN), &a0, &b0, &c0, &d0);
		}
	}
}

static bool is_top_tier(int i)                                                    // the highest tier box of its building (domes and water towers sit on top of it)
{
	const World *w = &SG->w;
	return i + 1 >= w->nbox || w->box[i + 1].bld != w->box[i].bld || w->box[i + 1].kind != BK_BLDG;
}

static void build_static_pass(void)
{
	build_sky();
	ground();
	for (int i = 0; i < SG->w.nbox; i++) {
		const Box *b = &SG->w.box[i];
		chunk_at(0.5f * (b->x0 + b->x1), 0.5f * (b->z0 + b->z1));
		if (b->kind == BK_TOWER) { water_tower(b); continue; }
		if (b->kind == BK_DOME) { dome(b); continue; }
		for (int s = 0; s < 4; s++) wall(b, s);
		roof_top(b);
		if (!SG->lowq && b->kind == BK_BLDG && b->y1 > 8.0f && is_top_tier(i)) cornice(b);     // top tier of a building
		if (!SG->lowq && b->kind == BK_BLDG && is_top_tier(i)) roof_props(b);
		if (b->roof == RF_PYRAMID) pyramid(MAT_COPPER, b->x0, b->z0, b->x1, b->z1, b->y1, 5.0f + 0.3f * fminf(b->x1 - b->x0, b->z1 - b->z0), (const float[3]){ 0.95f, 1.0f, 0.95f }, true);
		else if (b->roof == RF_SPIRE) {
			float cx = 0.5f * (b->x0 + b->x1), cz = 0.5f * (b->z0 + b->z1), hw = fminf(1.4f, 0.12f * (b->x1 - b->x0)), h = fminf(48.0f, 0.2f * b->y1 + 10.0f);
			pyramid(MAT_WHITE, cx - hw, cz - hw, cx + hw, cz + hw, b->y1, h, (const float[3]){ 0.8f, 0.82f, 0.86f }, false);
		}
	}
	shadows();
	if (!SG->lowq) street_props();
	trees();
	backdrop();
}

int scene_build_static(const Game *g, Scene *s, Vtx *buf)
{
	SG = g;
	for (int i = 0; i < GRP_COUNT; i++) for (int c = 0; c <= NCH; c++) { sm[i][c].v = NULL; sm[i][c].n = 0; sm[i][c].cap = 0; }
	build_static_pass();
	int total = 0;
	for (int i = 0; i < GRP_COUNT; i++) for (int c = 0; c <= NCH; c++) { s->cfirst[i][c] = total; s->ccount[i][c] = sm[i][c].n; total += sm[i][c].n; if (c == NCH) { s->sfirst[i] = s->cfirst[i][c]; s->scount[i] = s->ccount[i][c]; } }
	if (!buf) return total;
	s->stat = buf;
	for (int i = 0; i < GRP_COUNT; i++) for (int c = 0; c <= NCH; c++) { sm[i][c].v = buf + s->cfirst[i][c]; sm[i][c].cap = s->ccount[i][c]; sm[i][c].n = 0; }
	build_static_pass();
	return total;
}

// =============================================================================================================================
// dynamic: hero, web, cars, beacon
// =============================================================================================================================
static Mesh dm[D_COUNT];
static V3 EYE, CR, CU;

static void dtri(Mesh *m, V3 a, V3 b, V3 c, V3 col, float k, float alpha)
{
	float cc[3] = { col.x * k, col.y * k, col.z * k };
	SV A = sv(a.x, a.y, a.z, 0.5f, 0.5f, cc, alpha), B = sv(b.x, b.y, b.z, 0.5f, 0.5f, cc, alpha), C = sv(c.x, c.y, c.z, 0.5f, 0.5f, cc, alpha);
	tri(m, &A, &B, &C);
}
static void dquad(Mesh *m, V3 a, V3 b, V3 c, V3 d, V3 col, float k)
{
	dtri(m, a, b, c, col, k, 1); dtri(m, a, c, d, col, k, 1);
}

static float SC = 1.38f;
static V3 PIV;                                                     // the hero's pivot (feet) for scaling
static V3 Q(V3 x) { return add(PIV, mul(sub(x, PIV), SC)); }

// CPU linear blend skinning. scene_build runs once in gfx_begin; both stereo eyes
// reuse the resulting dynamic buffer. UV seams never duplicate skinning work.
typedef struct { V3 p,x,y,z; } BonePose;
static BonePose bones[HERO_NB];
static V3 arr3(const float *a) { return v3(a[0],a[1],a[2]); }
static V3 bone_rot(const BonePose *b,V3 p) { return add(mul(b->x,p.x),add(mul(b->y,p.y),mul(b->z,p.z))); }
static void bone_frame(int i,V3 p,V3 x,V3 y,V3 z) { bones[i]=(BonePose){p,x,y,z}; }
static void frame_of(V3 y,V3 xref,V3 fallback,V3 *X,V3 *Y,V3 *Z);
static void bone_between(int i,V3 h,V3 t,V3 left,V3 forward)
{
	V3 x,y,z,rx,ry,rz;
	frame_of(sub(t,h),left,forward,&x,&y,&z);
	frame_of(sub(arr3(hero_bones[i].tail),arr3(hero_bones[i].head)),v3(1,0,0),v3(0,0,1),&rx,&ry,&rz);
	bone_frame(i,h,add(mul(x,rx.x),add(mul(y,ry.x),mul(z,rz.x))),
		add(mul(x,rx.y),add(mul(y,ry.y),mul(z,rz.y))),add(mul(x,rx.z),add(mul(y,ry.z),mul(z,rz.z))));
}
static V3 skin_position[HERO_NV],skin_normal[HERO_NV];
static void hero_skin(void)
{
	static float light[HERO_NV];
	for(int i=0;i<HERO_NV;i++) {
		const HeroVertex *v=&hero_vertices[i]; V3 p=v3(0,0,0),n=p;
		for(int j=0;j<4;j++) if(v->w[j]>0) {
			int b=v->b[j]; V3 q=add(bones[b].p,bone_rot(&bones[b],sub(arr3(v->p),arr3(hero_bones[b].head))));
			p=add(p,mul(q,v->w[j]));n=add(n,mul(bone_rot(&bones[b],arr3(v->n)),v->w[j]));
		}
		skin_position[i]=Q(p);skin_normal[i]=norm(n);
		light[i]=fminf(1,shadev(skin_normal[i]))*v->ao;
	}
	Mesh *m=&dm[D_HERO];
	for(int i=0;i<HERO_NI;i++) {
		const HeroCorner *c=&hero_corners[i];V3 p=skin_position[c->v];float k=light[c->v],rgb[3]={k,k,k};
		SV v=sv(p.x,p.y,p.z,c->uv[0],c->uv[1],rgb,1);put(m,&v);
	}
}
// an orthonormal frame whose Y axis is `y`, with X as close as possible to `xref` (and Z = X x Y: forward for an upright hero)
static void frame_of(V3 y, V3 xref, V3 fallback, V3 *X, V3 *Y, V3 *Z)
{
	y = norm(y);
	V3 x = sub(xref, mul(y, dot(xref, y)));
	if (vlen(x) < 0.2f) x = sub(fallback, mul(y, dot(fallback, y)));
	x = norm(x);
	*X = x; *Y = y; *Z = cross(x, y);
}

// two-bone IK: the middle joint between root and target, bending toward hint
static V3 ik(V3 root, V3 target, float l1, float l2, V3 hint)
{
	V3 d = sub(target, root); float dd = vlen(d);
	if (dd > l1 + l2 - 0.01f) dd = l1 + l2 - 0.01f;
	if (dd < 0.05f) dd = 0.05f;
	V3 dn = norm(d);
	float a = (l1 * l1 - l2 * l2 + dd * dd) / (2 * dd), hh = sqrtf(fmaxf(0, l1 * l1 - a * a));
	V3 perp = sub(hint, mul(dn, dot(hint, dn)));
	perp = vlen(perp) > 1e-4f ? norm(perp) : cross(dn, v3(0, 1, 0));
	return add(add(root, mul(dn, a)), mul(perp, hh));
}
static V3 within_reach(V3 root, V3 target, float reach)
{
	V3 d = sub(target, root); float len = vlen(d);
	return len > reach ? add(root, mul(d, reach / len)) : target;
}

static V3 g_hand;                                                  // where the web leaves the hero's hand (for the rope)

// ---- animation layer: critically-damped springs so poses blend, lag and overshoot a little instead of snapping ------------------------
static void spring3(V3 *x, V3 *v, V3 target, float w, float z, float dt)
{
	int n = (int)(dt / 0.008f) + 1; float h = dt / n;
	for (int i = 0; i < n; i++) {
		V3 acc = sub(mul(sub(target, *x), w * w), mul(*v, 2 * z * w));
		*v = add(*v, mul(acc, h)); *x = add(*x, mul(*v, h));
	}
}
static void spring1(float *x, float *v, float target, float w, float z, float dt)
{
	int n = (int)(dt / 0.008f) + 1; float h = dt / n;
	for (int i = 0; i < n; i++) { float acc = (target - *x) * w * w - *v * 2 * z * w; *v += acc * h; *x += *v * h; }
}
typedef struct {
	bool init; float lastT, bank, bankV, lean, leanV;
	int gaitMode; float gait;
	bool planted[2]; V3 contact[2], footForward[2];
	V3 prevVel, prevPos, dH, dHv, u, uv, R, Rv;
	V3 hand[2], handV[2], foot[2], footV[2], ah[2], ahV[2], lh[2], lhV[2];            // limb targets and knee/elbow hints in the body's local frame
} Anim;
static Anim an;
#ifdef HOST_PREVIEW
static struct { V3 hip[2], knee[2], ankle[2], shoulder[2], elbow[2], wrist[2]; } heroPose;
#endif
static V3 to_local(V3 w, V3 R, V3 u, V3 F) { return v3(dot(w, R), dot(w, u), dot(w, F)); }
static V3 from_local(V3 l, V3 R, V3 u, V3 F) { return add(mul(R, l.x), add(mul(u, l.y), mul(F, l.z))); }
static void basis_of(V3 u, V3 Rin, V3 *R, V3 *U, V3 *F)                              // orthonormal (right, up, front) from an up axis and a rough right
{
	V3 uu = norm(u), r = sub(Rin, mul(uu, dot(Rin, uu)));
	r = vlen(r) < 1e-3f ? cross(uu, v3(0, 0, 1)) : r; r = norm(r);
	*R = r; *U = uu; *F = cross(uu, r);
}

static float ease(float t) { t=sat(t); return t*t*(3-2*t); }
#include "hero_anim.h"
typedef struct { float w,x,y,z; } HQuat;
static HQuat hq(const float *q) { return (HQuat){q[0],q[1],q[2],q[3]}; }
static HQuat qblend(HQuat a,HQuat b,float t)
{
	float d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z,s=d<0?-1:1;
	HQuat q={a.w*(1-t)+b.w*t*s,a.x*(1-t)+b.x*t*s,a.y*(1-t)+b.y*t*s,a.z*(1-t)+b.z*t*s};
	float k=1/sqrtf(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);q.w*=k;q.x*=k;q.y*=k;q.z*=k;return q;
}
static HQuat qmul(HQuat a,HQuat b) { return (HQuat){a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w}; }
static HQuat qaxis(float angle,int axis) { HQuat q={cosf(angle*.5f),0,0,0};if(axis==0)q.x=sinf(angle*.5f);else if(axis==1)q.y=sinf(angle*.5f);else q.z=sinf(angle*.5f);return q; }
static BonePose qframe(HQuat q)
{
	return (BonePose){v3(0,0,0),v3(1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)),
		v3(2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)),
		v3(2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y))};
}
static float gait_blend(float speed,int *a,int *b)
{
	if(speed<4.7f){*a=0;*b=1;return ease((speed-2.0f)/2.7f);}
	*a=1;*b=2;return ease((speed-4.7f)/2.4f);
}
static void ground_pose(const Game *g,V3 P,V3 up,V3 f,V3 R,float speed,float dt,bool snap,bool wall)
{
	const Player *p=&g->p;int ca,cb;float blend=gait_blend(speed,&ca,&cb),amp=ease(speed/.85f);
	float stride=lerpf(gait_clips[ca].stride,gait_clips[cb].stride,blend);
	int mode=wall?2:1;
	if(snap || an.gaitMode!=mode){an.gait=0;an.planted[0]=an.planted[1]=false;}
	an.gaitMode=mode;an.gait+=speed*dt/(SC*stride);an.gait-=floorf(an.gait);
	float key=an.gait*GAIT_KEYS;int k=(int)key,n=(k+1)%GAIT_KEYS;float t=key-k;
	const GaitKey *a=&gait_clips[ca].key[k],*a1=&gait_clips[ca].key[n],*b=&gait_clips[cb].key[k],*b1=&gait_clips[cb].key[n];
	float height=lerpf(lerpf(a->height,a1->height,t),lerpf(b->height,b1->height,t),blend);
	float crouch=fmaxf(sat(p->landT/.18f)*.6f,p->charge*.95f);
	float lean=amp*(.015f+.075f*ease((speed-2)/6))+an.lean+(wall?.12f:0);
	V3 L=mul(R,-1),u=norm(add(mul(up,cosf(lean)),mul(f,sinf(lean)))),fw=norm(cross(L,u));
	BonePose world={add(P,mul(up,lerpf(1.135f,height,amp)-.24f*crouch)),L,u,fw};
	for(int i=0;i<HERO_NB;i++) {
		HQuat q=qblend(qblend(hq(a->q[i]),hq(a1->q[i]),t),qblend(hq(b->q[i]),hq(b1->q[i]),t),blend),idle={1,0,0,0};
		if(i==HB_L_UPPERARM || i==HB_R_UPPERARM)idle=qmul(qaxis(i==HB_L_UPPERARM?-.59f:.59f,2),qaxis(-.10f,0));
		if(i==HB_L_FOREARM || i==HB_R_FOREARM)idle=qaxis(-.18f,0);
		q=qblend(idle,q,amp);
		if(i==HB_L_THIGH || i==HB_R_THIGH)q=qmul(qaxis(-.65f*crouch,0),q);
		if(i==HB_L_SHIN || i==HB_R_SHIN)q=qmul(qaxis(1.3f*crouch,0),q);
		if(i==HB_L_FOOT || i==HB_R_FOOT)q=qmul(qaxis(-.65f*crouch,0),q);
		BonePose local=qframe(q);int par=hero_bones[i].parent;BonePose *parent=par<0?&world:&bones[par];
		V3 offset=par<0?v3(0,0,0):sub(arr3(hero_bones[i].head),arr3(hero_bones[par].head));
		bone_frame(i,add(parent->p,bone_rot(parent,offset)),bone_rot(parent,local.x),bone_rot(parent,local.y),bone_rot(parent,local.z));
	}
	// The neck absorbs chest pitch; retain a little captured head motion.
	bones[HB_HEAD].x=norm(lerp3(bones[HB_HEAD].x,L,.7f));bones[HB_HEAD].y=norm(lerp3(bones[HB_HEAD].y,up,.7f));bones[HB_HEAD].z=norm(cross(bones[HB_HEAD].x,bones[HB_HEAD].y));bones[HB_HEAD].x=cross(bones[HB_HEAD].y,bones[HB_HEAD].z);
	// A small root lift prevents retargeting/clip blends from pushing soles into the floor.
	float low=1e9f;
	for(int s=0;s<2;s++) {int i=s?HB_R_FOOT:HB_L_FOOT;for(int z=0;z<2;z++){V3 sole=add(bones[i].p,bone_rot(&bones[i],v3(0,-.115f,z?.195f:-.045f)));low=fminf(low,dot(sub(sole,P),up));}}
	if(low<0)for(int i=0;i<HERO_NB;i++)bones[i].p=sub(bones[i].p,mul(up,low));
	// Contact correction is bounded to 6cm in model space. All primary leg motion
	// comes from the recorded joint angles, including recovery and knee drive.
	for(int s=0;s<2;s++) {
		int leg=s?HB_R_THIGH:HB_L_THIGH,foot=leg+2;
		bool contact=(blend<.5f?a->contact[s]:b->contact[s]) && speed>.08f && crouch<.1f;
		V3 sole=add(bones[foot].p,bone_rot(&bones[foot],v3(0,-.115f,.045f)));
		if(contact && !an.planted[s])an.contact[s]=Q(sole);
		if(contact) {
			V3 target=add(PIV,mul(sub(an.contact[s],PIV),1/SC)),cor=sub(target,sole);cor=sub(cor,mul(up,dot(cor,up)));float len=vlen(cor);
			if(len>.06f)cor=mul(cor,.06f/len);
			V3 hip=bones[leg].p,ankle=add(bones[foot].p,cor),oldKnee=bones[leg+1].p;
			V3 knee=ik(hip,ankle,vlen(sub(arr3(hero_bones[leg].tail),arr3(hero_bones[leg].head))),vlen(sub(arr3(hero_bones[leg+1].tail),arr3(hero_bones[leg+1].head))),sub(oldKnee,hip));
			bone_between(leg,hip,knee,L,f);bone_between(leg+1,knee,ankle,L,f);bones[foot].p=ankle;bones[foot+1].p=add(bones[foot+1].p,cor);
		}
		an.planted[s]=contact;
#ifdef HOST_PREVIEW
		heroPose.hip[s]=bones[leg].p;heroPose.knee[s]=bones[leg+1].p;heroPose.ankle[s]=bones[foot].p;
		int arm=s?HB_R_UPPERARM:HB_L_UPPERARM;heroPose.shoulder[s]=bones[arm].p;heroPose.elbow[s]=bones[arm+1].p;heroPose.wrist[s]=bones[arm+2].p;
#endif
	}
	an.init=true;an.dH=sub(bones[0].p,P);an.u=u;an.R=R;
	hero_skin();g_hand=Q(bones[p->hand?HB_R_HAND:HB_L_HAND].p);
}

static void draw_hero(const Game *g)
{
	const Player *p = &g->p;
	V3 P = v3(p->p[0], p->p[1], p->p[2]); PIV = P;
	V3 up = v3(0, 1, 0), f = v3(sinf(p->face), 0, -cosf(p->face));
	V3 R = norm(cross(f, up));
	V3 vel = v3(p->v[0], p->v[1], p->v[2]);
	float sp = vlen(vel), hs = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
	float dt = g->time - an.lastT; an.lastT = g->time;
	bool snap = !an.init || dt > 0.25f || dt < 0 || vlen(sub(vel, an.prevVel)) > 80.0f
	         || vlen(sub(sub(P, an.prevPos), mul(vel, fmaxf(dt, 0)))) > 3.0f;
	an.prevPos = P;
	if (dt > 0.05f) dt = 0.05f;
	if (dt < 0) dt = 0;
	// banking and lean from the acceleration (into turns, into speed-ups)
	float latA = 0, fwdA = 0;
	if (dt > 0.0005f && !snap) { V3 acc = mul(sub(vel, an.prevVel), 1.0f / dt); latA = dot(acc, R); fwdA = dot(acc, f); }
	an.prevVel = vel;
	float bankT = hs > 3.0f ? sat(fabsf(latA) / 60.0f) * (latA < 0 ? -0.55f : 0.55f) : 0, leanT = p->state == ST_GROUND ? fminf(0.2f, fmaxf(-0.12f, fwdA * 0.01f)) : 0;
	if (snap) { an.bank = bankT; an.bankV = 0; an.lean = leanT; an.leanV = 0; }
	else { spring1(&an.bank, &an.bankV, bankT, 7.0f, 1.0f, dt); spring1(&an.lean, &an.leanV, leanT, 8.0f, 1.0f, dt); }
	float bank = an.bank;
	bool wallRun = p->state == ST_WALL && p->wallRun;
	if (wallRun) {                                                          // running on a wall: the ground run cycle, with the wall as the floor
		V3 n = v3(p->wn[0], 0, p->wn[1]);
		V3 mv = sub(vel, mul(n, dot(vel, n)));
		float ms = vlen(mv);
		up = n; P = sub(P, mul(n, 0.30f));
		f = ms > 1.0f ? mul(mv, 1.0f / ms) : v3(0, 1, 0);
		R = norm(cross(f, up)); hs = ms;
	}

	V3 u = up, H, hand[2], foot[2], armHint[2], legHint[2];                // index 0 = left (-R), 1 = right (+R)
	float sd[2] = { -1, 1 };
	float ph = p->phase, tm = g->time;
	bool roped = p->state == ST_AIR && p->rope;
	bool rolling = p->state == ST_GROUND && p->rollT > 0;
	bool locomotion = (p->state == ST_GROUND && !rolling) || wallRun;
	if (locomotion) { ground_pose(g,P,up,f,R,hs,dt,snap,wallRun); return; }
	// Traversal retains its existing target poses; the same continuous skin follows them.
	if (p->state == ST_WALL) {
		V3 n = v3(p->wn[0], 0, p->wn[1]);
		f = mul(n, -1); R = norm(cross(f, up));
		u = norm(add(mul(up, 0.95f), mul(f, 0.25f)));
		H = add(P, mul(up, 0.72f));
		float surface = BODY_HW / SC;                              // contact depth is in model units, not scaled world units
		float amp = sat((fabsf(p->v[1]) + sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2])) / 5.0f);
		for (int s = 0; s < 2; s++) {
			float a = ph + (s ? PI_F : 0), wv = sinf(a) * 0.24f * amp;
			hand[s] = add(add(P, mul(f, surface - 0.035f)), add(mul(up, 1.5f + wv + 0.06f * sinf(tm * 1.5f + s)), mul(R, sd[s] * 0.38f)));
			foot[s] = add(add(P, mul(f, surface - 0.10f)), add(mul(up, 0.25f - wv * 0.8f), mul(R, sd[s] * 0.3f)));
			armHint[s] = add(mul(R, sd[s]), mul(f, -0.3f));
			legHint[s] = add(mul(R, sd[s] * 0.9f), mul(f, 0.4f));
		}
	} else {
		V3 vd = sp > 1 ? mul(vel, 1.0f / sp) : up;
		V3 ropeDir = up;
		if (roped) { V3 c = add(P, v3(0, 1.4f, 0)); ropeDir = norm(sub(v3(p->avis[0], p->avis[1], p->avis[2]), c)); }
		float k = roped ? 0.75f : 0.45f;
		u = norm(add(mul(up, roped ? 0.38f : 0.55f), mul(vd, k)));
		if (!roped && sp > 25 && vel.y < -10) u = norm(add(mul(up, 0.2f), mul(vd, 0.8f)));
		u = norm(add(u, mul(R, bank * 0.7f)));
		bool wing = p->wing && !roped && !rolling;
		if (wing) { V3 hv = v3(vel.x, 0, vel.z); hv = vlen(hv) > 1 ? norm(hv) : f; u = norm(add(add(mul(vd, 0.8f), mul(hv, 0.3f)), mul(up, 0.1f))); u = norm(add(u, mul(R, bank * 0.7f))); }
		if (rolling) u = up;
		V3 M0 = add(P, wing ? mul(up, 1.0f) : mul(u, rolling ? 0.62f : 1.05f));                                                       // body centre: tricks rotate the hero about it
		bool trick = rolling || (p->trickT > 0 && !roped);
		int ttype = rolling ? 1 : p->trickType;
		if (trick) {
			float kk = rolling ? 1.0f - p->rollT / ROLL_DUR : 1.0f - p->trickT / TRICK_DUR; kk = kk * kk * (3.0f - 2.0f * kk);
			float th = 2.0f * PI_F * kk, c = cosf(th), sn = sinf(th);
			V3 u0 = u, f0 = f;
			if (ttype == 1 || ttype == 2) { float sg = ttype == 1 ? 1.0f : -1.0f; u = add(mul(u0, c), mul(f0, sn * sg)); f = sub(mul(f0, c), mul(u0, sn * sg)); }
			else if (ttype == 5) { f = add(mul(f0, c), mul(R, sn)); R = sub(mul(R, c), mul(f0, sn)); }                 // corkscrew: spin about the body axis
			else { float sg = ttype == 4 ? 1.0f : -1.0f; u = add(mul(u0, c), mul(R, sn * sg)); R = sub(mul(R, c), mul(u0, sn * sg)); }
		}
		H = sub(M0, mul(u, 0.15f));
		float flap = sinf(tm * 3.1f) * 0.05f;                                                // small life in the limbs while airborne
		bool screw = trick && !rolling && ttype == 5;                          // corkscrew: arms thrown overhead, legs together, spinning like a drill
		float tuck = trick && !screw ? 1.0f : 0.0f;
		bool dive = !roped && !wing && !trick && sp > 22.0f && vel.y < -6.0f;   // swan dive: arms swept back, head first
		float spread = sat((sp - 14.0f) / 14.0f) * (!roped && !wing && !trick && !dive ? 1.0f : 0.0f);   // coasting fast: arms and legs flung wide
		int rs = p->hand;                                                   // the hand that holds the web
		for (int s = 0; s < 2; s++) {
			V3 sh = add(add(H, mul(u, 0.52f)), mul(R, sd[s] * 0.25f));
			if (roped) {
				if (s == rs) { hand[s] = add(sh, mul(ropeDir, 0.62f)); armHint[s] = add(mul(R, sd[s]), mul(f, -0.5f)); }
				else { hand[s] = add(add(sh, mul(u, -0.35f)), add(mul(f, 0.12f + flap), mul(R, sd[s] * 0.2f))); armHint[s] = add(mul(f, -0.4f), mul(R, sd[s])); }
				// legs trail behind the swing, one knee bent more than the other, swaying with the sideways acceleration
				float sway = clampf(latA * 0.004f, -0.25f, 0.25f);
				foot[s] = add(add(H, mul(u, -0.78f + 0.2f * s - 0.06f * (1 - s))), add(mul(R, sd[s] * 0.14f - sway), mul(f, -0.1f - 0.16f * (1 - s))));
				legHint[s] = add(mul(f, -0.4f + 0.7f * s), mul(R, sd[s] * 0.6f));
			} else if (wing) {                                                  // superman pose, arms and legs spread to stretch the wing membrane
				hand[s] = add(add(sh, mul(R, sd[s] * 0.95f)), add(mul(u, 0.12f), mul(f, 0.0f)));
				armHint[s] = add(mul(f, -0.6f), mul(R, sd[s] * 0.2f));
				foot[s] = add(add(H, mul(u, -0.86f)), mul(R, sd[s] * 0.5f));
				legHint[s] = add(mul(f, -0.5f), mul(R, sd[s] * 0.2f));
			} else if (screw) {
				hand[s] = add(add(sh, mul(u, 0.62f)), mul(R, sd[s] * 0.07f));
				armHint[s] = add(mul(f, -0.4f), mul(R, sd[s] * 0.6f));
				foot[s] = add(add(H, mul(u, -0.88f)), mul(R, sd[s] * 0.07f));
				legHint[s] = add(mul(f, -0.5f), mul(R, sd[s] * 0.3f));
			} else if (dive) {
				hand[s] = add(add(sh, mul(u, -0.5f)), add(mul(R, sd[s] * 0.2f), mul(f, -0.1f)));
				armHint[s] = add(mul(R, sd[s]), mul(f, -0.3f));
				foot[s] = add(add(H, mul(u, -0.9f)), add(mul(R, sd[s] * 0.07f), mul(f, -0.04f)));
				legHint[s] = add(mul(f, -0.5f), mul(R, sd[s] * 0.2f));
			} else {
				hand[s] = add(add(sh, mul(R, sd[s] * (0.6f - 0.25f * tuck + 0.35f * spread))), add(mul(u, 0.2f - 0.7f * tuck - 0.1f * spread), mul(f, 0.15f + 0.35f * tuck - 0.1f * spread + flap * sd[s])));
				armHint[s] = add(mul(up, -0.5f), mul(R, sd[s] * 0.3f));
				foot[s] = add(add(H, mul(u, -0.62f + 0.34f * tuck - 0.2f * spread)), add(mul(R, sd[s] * (0.15f + 0.2f * spread)), mul(f, (0.2f - 0.3f * s) * (1 - tuck) * (1 - spread) + 0.42f * tuck - 0.15f * spread)));
				legHint[s] = add(f, mul(R, sd[s] * 0.2f));
			}
		}
	}

	if (!locomotion) { an.gaitMode = 0; an.planted[0] = an.planted[1] = false; }

	// ---- smooth the pose: body axes and root offset in world space, limbs in the body's own frame --------------------------------
	V3 Rr, Ur, Fr;
	basis_of(u, R, &Rr, &Ur, &Fr);                                                           // the raw body frame
	V3 l_hand[2], l_foot[2], l_ah[2], l_lh[2];
	for (int s = 0; s < 2; s++) {
		l_hand[s] = to_local(sub(hand[s], H), Rr, Ur, Fr); l_foot[s] = to_local(sub(foot[s], H), Rr, Ur, Fr);
		l_ah[s] = to_local(armHint[s], Rr, Ur, Fr); l_lh[s] = to_local(legHint[s], Rr, Ur, Fr);
	}
	V3 dH = sub(H, P);
	bool trickNow = rolling || (p->trickT > 0 && !roped);
	if (snap) {
		an.init = true; an.dH = dH; an.dHv = v3(0, 0, 0); an.u = Ur; an.uv = v3(0, 0, 0); an.R = Rr; an.Rv = v3(0, 0, 0);
		for (int s = 0; s < 2; s++) { an.hand[s] = l_hand[s]; an.foot[s] = l_foot[s]; an.ah[s] = l_ah[s]; an.lh[s] = l_lh[s]; an.handV[s] = an.footV[s] = an.ahV[s] = an.lhV[s] = v3(0, 0, 0); }
	} else {
		bool runPose = locomotion;                                 // the run cycle is fast: stiff springs so feet stay planted
		float wb = trickNow ? 40.0f : 15.0f;
		spring3(&an.dH, &an.dHv, dH, runPose ? 40.0f : 20.0f, 0.9f, dt);
		spring3(&an.u, &an.uv, Ur, wb, 1.0f, dt); spring3(&an.R, &an.Rv, Rr, wb, 1.0f, dt);
		for (int s = 0; s < 2; s++) {
			spring3(&an.hand[s], &an.handV[s], l_hand[s], runPose ? 55.0f : 22.0f, 0.85f, dt); spring3(&an.foot[s], &an.footV[s], l_foot[s], runPose ? 80.0f : 26.0f, 0.9f, dt);
			spring3(&an.ah[s], &an.ahV[s], l_ah[s], 12.0f, 1.0f, dt); spring3(&an.lh[s], &an.lhV[s], l_lh[s], 12.0f, 1.0f, dt);
		}
	}
	V3 sR, sU, sF;
	basis_of(an.u, an.R, &sR, &sU, &sF);
	H = add(P, an.dH); u = sU; R = sR; f = sF;
	bool onGround = p->state == ST_GROUND;
	for (int s = 0; s < 2; s++) {
		hand[s] = add(H, from_local(an.hand[s], sR, sU, sF)); foot[s] = add(H, from_local(an.foot[s], sR, sU, sF));
		armHint[s] = from_local(an.ah[s], sR, sU, sF); legHint[s] = from_local(an.lh[s], sR, sU, sF);
		if (onGround && foot[s].y < P.y) foot[s].y = P.y;                                    // springs never push a foot through the street
	}

	// pose the Blender parts: left = -R, so a part's +x points to the hero's left (limbs for the right side are mirrored)
	V3 fw = norm(sub(f, mul(u, dot(f, u))));
	V3 side = cross(fw, u); side = vlen(side) > 1e-4f ? norm(side) : R;
	V3 L = mul(side, -1.0f);
	V3 X, Y, Z;
	frame_of(u, L, fw, &X, &Y, &Z);
	for (int b=HB_PELVIS;b<=HB_HEAD;b++) bone_frame(b,add(H,add(mul(X,hero_bones[b].head[0]),add(mul(Y,hero_bones[b].head[1]),mul(Z,hero_bones[b].head[2])))),X,Y,Z);
	bone_frame(HB_HEAD,add(H,mul(u,.73f)),X,Y,Z);
	bool ground = p->state == ST_GROUND || wallRun;
	for (int s = 0; s < 2; s++) {
		int ab=s?HB_R_CLAVICLE:HB_L_CLAVICLE,lb=s?HB_R_THIGH:HB_L_THIGH;                                                                    // s = 0 is the hero's left
		V3 hip = add(H, mul(R, sd[s] * 0.12f)), ankle = add(foot[s], mul(up, 0.10f));
		if (!locomotion) ankle = within_reach(hip, ankle, 0.99f);
		foot[s] = sub(ankle, mul(up, 0.10f));                       // wing membrane uses the resolved ankle
		V3 knee = ik(hip, ankle, 0.50f, 0.50f, legHint[s]);
		bone_between(lb,hip,knee,L,fw);
		bone_between(lb+1,knee,ankle,L,fw);
		if (p->state == ST_WALL && !wallRun) frame_of(mul(f, -1.0f), L, up, &X, &Y, &Z);                  // toes up the wall, soles on it
		else frame_of(ground ? up : norm(add(sub(knee, ankle), mul(up, 0.15f))), L, fw, &X, &Y, &Z);
		if (p->state == ST_WALL && !wallRun) { Z = up; X = L; Y = cross(Z, X); }
		bone_frame(lb+2,ankle,X,Y,Z); bone_frame(lb+3,add(ankle,add(mul(Y,-.07f),mul(Z,.145f))),X,Y,Z);
		V3 sh = add(add(H, mul(u, 0.53f)), mul(R, sd[s] * 0.255f));
		hand[s] = within_reach(sh, hand[s], 0.58f);                 // keep hands attached, including rope and wing endpoints
		V3 elbow = ik(sh, hand[s], 0.3f, 0.29f, armHint[s]);
#ifdef HOST_PREVIEW
		heroPose.hip[s] = hip; heroPose.knee[s] = knee; heroPose.ankle[s] = ankle;
		heroPose.shoulder[s] = sh; heroPose.elbow[s] = elbow; heroPose.wrist[s] = hand[s];
#endif
		bone_between(ab,add(H,add(mul(u,.54f),mul(R,sd[s]*.04f))),sh,L,fw); bone_between(ab+1,sh,elbow,L,fw);
		bone_between(ab+2,elbow,hand[s],L,fw);
		bones[ab+3]=bones[ab+2];bones[ab+3].p=hand[s];
	}
	hero_skin();
	g_hand = Q(hand[p->hand]);
	if (p->wing && p->state == ST_AIR && !p->rope && !rolling) {                         // the wing membrane: shoulder - hand - ankle - hip on each side, and a tail between the legs
		static const V3 cA = { 0.78f, 0.1f, 0.12f }, cB = { 0.1f, 0.14f, 0.5f };
		V3 shp[2], hip[2], ank[2], hnd[2];
		for (int s = 0; s < 2; s++) {
			shp[s] = Q(add(add(H, mul(u, 0.50f)), mul(R, sd[s] * 0.22f))); hip[s] = Q(add(H, mul(R, sd[s] * 0.1f)));
			ank[s] = Q(add(foot[s], mul(up, 0.1f))); hnd[s] = Q(hand[s]);
			V3 nrm = norm(cross(sub(hnd[s], shp[s]), sub(ank[s], shp[s])));
			float k = fmaxf(shade(nrm.x, nrm.y, nrm.z), shade(-nrm.x, -nrm.y, -nrm.z)) * 0.95f;
			dquad(&dm[D_OPAQUE], shp[s], hnd[s], ank[s], hip[s], cB, k);
			V3 m1 = lerp3(shp[s], hnd[s], 0.5f), m2 = lerp3(hip[s], ank[s], 0.5f);                            // a red panel down the middle of each wing
			dquad(&dm[D_OPAQUE], lerp3(shp[s], m1, 0.3f), lerp3(shp[s], hnd[s], 0.85f), lerp3(hip[s], ank[s], 0.85f), lerp3(hip[s], m2, 0.3f), cA, k * 0.95f);
		}
		dquad(&dm[D_OPAQUE], hip[0], hip[1], ank[1], ank[0], cB, 0.9f);
	}
}

static void build_rope(const Game *g)
{
	const Player *p = &g->p;
	if (!(p->state == ST_AIR && p->rope)) return;
	V3 a = g_hand, b = v3(p->avis[0], p->avis[1], p->avis[2]);
	float t = sat(p->webT);
	V3 e = lerp3(a, b, t);
	V3 d = sub(e, a), mid = mul(add(a, e), 0.5f);
	V3 side = cross(d, sub(EYE, mid)); if (vlen(side) < 1e-5f) return; side = norm(side);
	V3 dn = norm(d), side2 = norm(cross(dn, side));                                    // two axes perpendicular to the rope (side faces the camera)
	float len = vlen(d);
	int N = (int)fminf(24.0f, fmaxf(4.0f, len / 3.0f));
	static const V3 core = { 0.96f, 0.98f, 1.0f }, thread = { 0.78f, 0.86f, 1.0f };
	float tm = g->time, wob = (1.0f - t) * 1.2f + (p->zip ? 0.0f : 0.0f);
	V3 prev[3]; bool have = false;
	for (int i = 0; i <= N; i++) {
		float u = (float)i / N, taper = 0.5f + 0.5f * sinf(PI_F * u);                  // the strand narrows toward both ends
		V3 c = lerp3(a, e, u);
		c = add(c, mul(side2, sinf(u * 9.0f - tm * 25.0f) * wob * 0.35f * taper));       // the shot web ripples as it flies out
		float dist = vlen(sub(c, EYE));
		float w = (0.006f * dist + 0.012f) * (0.45f + 0.55f * taper);
		V3 cur[3];
		cur[0] = c;                                                                   // core strand
		for (int s = 0; s < 2; s++) {                                                  // two fine threads twisting around it
			float ph = u * len * 1.6f + (s ? PI_F : 0.0f) - tm * 3.0f, r = w * 1.6f;
			cur[1 + s] = add(c, add(mul(side, cosf(ph) * r), mul(side2, sinf(ph) * r)));
		}
		if (have) {
			for (int k = 0; k < 3; k++) {
				float ww = k == 0 ? w : w * 0.45f;
				dquad(&dm[D_OPAQUE], sub(prev[k], mul(side, ww)), add(prev[k], mul(side, ww)), add(cur[k], mul(side, ww)), sub(cur[k], mul(side, ww)), k == 0 ? core : thread, k == 0 ? 0.98f : 0.9f);
			}
		}
		memcpy(prev, cur, sizeof prev); have = true;
		if (i > 0 && i < N && (i & 1) == 0) {                                          // tiny cross-fibres, like silk catching the light
			float fl = w * 7.0f * taper;
			dquad(&dm[D_OPAQUE], sub(c, mul(side, fl)), sub(add(c, mul(dn, w * 0.8f)), mul(side, fl)), add(add(c, mul(dn, w * 0.8f)), mul(side, fl)), add(c, mul(side, fl)), thread, 0.85f);
		}
	}
	// the splat where it sticks
	if (t >= 1) {
		float r = 0.9f + 0.004f * vlen(sub(b, EYE));
		V3 q0 = sub(sub(b, mul(CR, r)), mul(CU, r)), q1 = sub(add(b, mul(CR, r)), mul(CU, r)), q2 = add(add(b, mul(CR, r)), mul(CU, r)), q3 = add(sub(b, mul(CR, r)), mul(CU, r));
		float c[3] = { 0.9f, 0.95f, 1.0f };
		SV A = sv(q0.x, q0.y, q0.z, 0, 0, c, 0.5f), B = sv(q1.x, q1.y, q1.z, 1, 0, c, 0.5f), C = sv(q2.x, q2.y, q2.z, 1, 1, c, 0.5f), D = sv(q3.x, q3.y, q3.z, 0, 1, c, 0.5f);
		quad(&dm[D_ADD], &A, &B, &C, &D);
	}
}

static void build_blob_shadow(const Game *g)
{
	const Player *p = &g->p;
	float h = city_ground_below(&g->w, p->p), alt = p->p[1] - h;
	if (alt > 70 || h < -1) return;
	float r = 0.9f + alt * 0.02f, a = 0.5f * (1 - alt / 70.0f), y = h + 0.07f;
	const float c[3] = { 0, 0, 0 };
	SV A = sv(p->p[0] - r, y, p->p[2] - r, 0, 0, c, a), B = sv(p->p[0] + r, y, p->p[2] - r, 1, 0, c, a), C = sv(p->p[0] + r, y, p->p[2] + r, 1, 1, c, a), D = sv(p->p[0] - r, y, p->p[2] + r, 0, 1, c, a);
	quad(&dm[D_ALPHA], &A, &B, &C, &D);
}

static void box_lit(V3 c, V3 half, V3 col, int axis)                        // axis 0: long side along x, 1: along z
{
	(void)axis;
	V3 lo = sub(c, half), hi = add(c, half);
	V3 p[8] = { v3(lo.x, lo.y, lo.z), v3(hi.x, lo.y, lo.z), v3(hi.x, lo.y, hi.z), v3(lo.x, lo.y, hi.z), v3(lo.x, hi.y, lo.z), v3(hi.x, hi.y, lo.z), v3(hi.x, hi.y, hi.z), v3(lo.x, hi.y, hi.z) };
	dquad(&dm[D_OPAQUE], p[4], p[5], p[6], p[7], col, shade(0, 1, 0));
	dquad(&dm[D_OPAQUE], p[0], p[1], p[5], p[4], col, shade(0, 0, -1));
	dquad(&dm[D_OPAQUE], p[3], p[2], p[6], p[7], col, shade(0, 0, 1));
	dquad(&dm[D_OPAQUE], p[0], p[3], p[7], p[4], col, shade(-1, 0, 0));
	dquad(&dm[D_OPAQUE], p[1], p[2], p[6], p[5], col, shade(1, 0, 0));
}

static void build_cars(const Game *g)
{
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *c = &g->car[i];
		float pos = c->speed > 0 ? c->along : CITY - c->along;
		float x = c->axis == 0 ? c->line * PITCH + c->off : pos, z = c->axis == 0 ? pos : c->line * PITCH + c->off;
		float dx = x - EYE.x, dz = z - EYE.z;
		if (dx * dx + dz * dz > 260.0f * 260.0f) continue;
		V3 half = c->axis == 0 ? v3(0.9f, 0.55f, 2.2f) : v3(2.2f, 0.55f, 0.9f), cab = c->axis == 0 ? v3(0.8f, 0.3f, 1.1f) : v3(1.1f, 0.3f, 0.8f);
		V3 col = v3(c->col[0], c->col[1], c->col[2]);
		box_lit(v3(x, 0.75f, z), half, col, c->axis);
		box_lit(v3(x, 1.6f, z), cab, v3(0.25f, 0.3f, 0.36f), c->axis);
	}
}

static void build_beacon(const Game *g)
{
	if (!g->bc.on) return;
	V3 c = v3(g->bc.p[0], g->bc.p[1], g->bc.p[2]);
	float pulse = 0.85f + 0.15f * sinf(g->bc.t * 4.0f), dist = vlen(sub(c, EYE)), R = 8.0f, wd = 0.7f;
	const float col[3] = { 0.3f, 0.95f, 1.0f }, gold[3] = { 1.0f, 0.85f, 0.3f };
	// billboard ring
	const int N = 20;
	for (int i = 0; i < N; i++) {
		float a0 = i * 2 * PI_F / N + g->bc.t * 0.8f, a1 = (i + 1) * 2 * PI_F / N + g->bc.t * 0.8f;
		V3 o0 = add(mul(CR, cosf(a0)), mul(CU, sinf(a0))), o1 = add(mul(CR, cosf(a1)), mul(CU, sinf(a1)));
		V3 p0 = add(c, mul(o0, R - wd)), p1 = add(c, mul(o0, R + wd)), p2 = add(c, mul(o1, R + wd)), p3 = add(c, mul(o1, R - wd));
		const float *cc = (i & 1) ? col : gold;
		SV A = sv(p0.x, p0.y, p0.z, 0.5f, 0.5f, cc, 0.95f * pulse), B = sv(p1.x, p1.y, p1.z, 0.5f, 0.5f, cc, 0.95f * pulse), C = sv(p2.x, p2.y, p2.z, 0.5f, 0.5f, cc, 0.95f * pulse), D = sv(p3.x, p3.y, p3.z, 0.5f, 0.5f, cc, 0.95f * pulse);
		quad(&dm[D_ADD], &A, &B, &C, &D);
	}
	// glow in the middle
	{
		float r = R * 1.1f;
		V3 q0 = sub(sub(c, mul(CR, r)), mul(CU, r)), q1 = sub(add(c, mul(CR, r)), mul(CU, r)), q2 = add(add(c, mul(CR, r)), mul(CU, r)), q3 = add(sub(c, mul(CR, r)), mul(CU, r));
		SV A = sv(q0.x, q0.y, q0.z, 0, 0, col, 0.35f * pulse), B = sv(q1.x, q1.y, q1.z, 1, 0, col, 0.35f * pulse), C = sv(q2.x, q2.y, q2.z, 1, 1, col, 0.35f * pulse), D = sv(q3.x, q3.y, q3.z, 0, 1, col, 0.35f * pulse);
		quad(&dm[D_ADD], &A, &B, &C, &D);
	}
	// light beam far up into the sky and down to the street, always facing the camera
	V3 side = cross(v3(0, 1, 0), sub(EYE, c)); if (vlen(side) < 1e-4f) return; side = norm(side);
	float bw = 1.4f + dist * 0.004f, top = c.y + 260.0f, bot = 0;
	V3 l0 = sub(v3(c.x, bot, c.z), mul(side, bw)), l1 = add(v3(c.x, bot, c.z), mul(side, bw)), l2 = add(v3(c.x, top, c.z), mul(side, bw)), l3 = sub(v3(c.x, top, c.z), mul(side, bw));
	V3 m0 = v3(c.x, bot, c.z), m1 = v3(c.x, top, c.z);
	float ca = 0.55f * pulse;
	SV a0 = sv(l0.x, l0.y, l0.z, 0.5f, 0.5f, col, 0), b0 = sv(m0.x, m0.y, m0.z, 0.5f, 0.5f, col, ca * 0.5f), c0 = sv(m1.x, m1.y, m1.z, 0.5f, 0.5f, col, 0), d0 = sv(l3.x, l3.y, l3.z, 0.5f, 0.5f, col, 0);
	quad(&dm[D_ADD], &a0, &b0, &c0, &d0);
	SV a1 = sv(m0.x, m0.y, m0.z, 0.5f, 0.5f, col, ca * 0.5f), b1 = sv(l1.x, l1.y, l1.z, 0.5f, 0.5f, col, 0), c1 = sv(l2.x, l2.y, l2.z, 0.5f, 0.5f, col, 0), d1 = sv(m1.x, m1.y, m1.z, 0.5f, 0.5f, col, 0);
	quad(&dm[D_ADD], &a1, &b1, &c1, &d1);
}

void scene_build(const Game *g, Scene *s)
{
	// camera
	const Cam *cam = &g->cam;
	float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
	float f[3] = { sy * cp, sp, -cy * cp };
	float R[3] = { cy, 0, sy };
	float U[3] = { R[1] * f[2] - R[2] * f[1], R[2] * f[0] - R[0] * f[2], R[0] * f[1] - R[1] * f[0] };
	memcpy(s->eye, cam->eye, sizeof s->eye); memcpy(s->right, R, sizeof R); memcpy(s->up, U, sizeof U); memcpy(s->fwd, f, sizeof f);
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
	s->fov = cam->fov;
	for (int c = 0; c <= NCH; c++) {
		s->vis[c] = scene_chunk_visible(s, c);
		float cx = ((c % CH_N) + 0.5f) * CH_BLOCKS * PITCH, cz = ((c / CH_N) + 0.5f) * CH_BLOCKS * PITCH;
		s->near[c] = c < NCH && s->vis[c] && sqrtf((cx - s->eye[0]) * (cx - s->eye[0]) + (cz - s->eye[2]) * (cz - s->eye[2])) - 0.5f * CH_BLOCKS * PITCH * 1.42f < DETAIL_RANGE;
	}
	EYE = v3(s->eye[0], s->eye[1], s->eye[2]); CR = v3(R[0], R[1], R[2]); CU = v3(U[0], U[1], U[2]);

	static const int caps[D_COUNT] = { CAP_D_OPAQUE, CAP_D_HERO, CAP_D_ALPHA, CAP_D_ADD };
	int off = 0;
	for (int i = 0; i < D_COUNT; i++) { dm[i].v = s->dyn + off; dm[i].n = 0; dm[i].cap = caps[i]; s->first[i] = off; off += caps[i]; }

	draw_hero(g);
	build_rope(g);
	build_blob_shadow(g);
	if (!g->lowq) build_cars(g);
	build_beacon(g);
	for (int i = 0; i < D_COUNT; i++) s->count[i] = dm[i].n;
}

bool scene_project(const Scene *s, const float p[3], float *sx, float *sy)
{
	const float *m = s->view;
	float x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
	float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
	float z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
	if (z > -0.2f) return false;
	float th = tanf(s->fov * 0.5f * PI_F / 180.0f), asp = 400.0f / 240.0f;
	*sx = 200.0f * (1.0f + x / (-z * th * asp));
	*sy = 120.0f * (1.0f - y / (-z * th));
	return true;
}
