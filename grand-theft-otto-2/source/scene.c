// Grand Theft Otto II - scene builder. The static world (sky, ground, water, buildings, shadows, trees) is built once into one buffer sorted by
// texture; Otto and the cars are rebuilt every frame. Lighting is baked into vertex colours (sun + sky ambient).
#include "scene.h"
#include "props_mesh.h"
#include "palm_mesh.h"
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
#define DETAIL_RANGE 120.0f                                   // props and cornices are drawn only this close (chunk edge distance)
static Mesh sm[GRP_COUNT][NCH + 1];
static int curChunk = NCH;
#define SM(g) sm[g][curChunk]
static void chunk_at(float x, float z)
{
	int i = (int)(x / PITCH) / CH_BLOCKS, j = (int)(z / PITCH) / CH_BLOCKS;
	if (x < 0 || z < 0 || i >= CH_N || j >= CH_N) { curChunk = NCH; return; }
	curChunk = j * CH_N + i;
}
static void chunk_info(int c, float *cx, float *cz, float *cy, float *rv, float *rh);       // scenery.h
bool scene_chunk_visible(const Scene *s, int c)
{
	if (c >= NCH) return true;
	if (c == EX_HILLS && s->fwd[2] > 0.62f) return false;                                      // looking away from the hills
	float cx, cz, cy, r, rh;
	chunk_info(c, &cx, &cz, &cy, &r, &rh);
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
static void prop_m(const PropMesh *pm, float x, float y, float z, float yaw, float sc, const float tint[3])
{
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

static void prop(int id, float x, float y, float z, float yaw, float sc, const float tint[3]) { prop_m(&PROPS[id], x, y, z, yaw, sc, tint); }

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
			if (hf(h >> 5) < 0.24f) {
				float tn[3] = { 0.85f + 0.3f * hf(h >> 7), 0.9f + 0.2f * hf(h >> 9), 0.85f + 0.3f * hf(h >> 11) };
				if (hf(h >> 15) < 0.8f) { float px = horiz ? x0 + u1 + (hf(h >> 4) - 0.5f) * 8.0f : x0 + fixed, pz = horiz ? z0 + fixed : z0 + u1 + (hf(h >> 4) - 0.5f) * 8.0f; prop_m(&PALMS[(int)(hf(h >> 17) * 1.99f)], px, 0.0f, pz, hf(h >> 8) * 6.28f, 0.9f + 0.4f * hf(h >> 6), tn); }
				else PUT(PR_TREE0 + (int)(hf(h >> 13) * 2.99f), u1 + (hf(h >> 4) - 0.5f) * 6.0f, 0.0f, 0.75f + 0.45f * hf(h >> 6), hf(h >> 8) * 6.28f, tn);
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
			if (i == 2 && j == 3) {                      // a pond in the middle of the park
				float px0 = x0 + 0.3f * PITCH, px1 = x0 + 0.7f * PITCH, pz0 = z0 + 0.3f * PITCH, pz1 = z0 + 0.7f * PITCH, pc[3] = { 0.9f, 0.95f, 1.0f };
				SV q0 = sv(px0, 0.16f, pz0, px0 / 20, pz0 / 20, pc, 1), q1 = sv(px1, 0.16f, pz0, px1 / 20, pz0 / 20, pc, 1), q2 = sv(px1, 0.16f, pz1, px1 / 20, pz1 / 20, pc, 1), q3 = sv(px0, 0.16f, pz1, px0 / 20, pz1 / 20, pc, 1);
				quad(&SM(MAT_WATER), &q0, &q1, &q2, &q3);
			}
		}
	}
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
				if (i == 2 && j == 3 && x > (i + 0.28f) * PITCH && x < (i + 0.72f) * PITCH && z > (j + 0.28f) * PITCH && z < (j + 0.72f) * PITCH) continue;   // pond
				float tn[3] = { 0.85f + 0.3f * rndf(&st), 0.9f + 0.2f * rndf(&st), 0.85f + 0.3f * rndf(&st) };
				if (SG->lowq) tree(&SM(MAT_WHITE), x, z, 1.1f + rndf(&st) * 0.9f, pal[rnd_next(&st) % 5]);
				else if (rndf(&st) < 0.4f) prop_m(&PALMS[(int)(rndf(&st) * 1.99f)], x, 0.0f, z, rndf(&st) * 6.28f, 1.0f + rndf(&st) * 0.5f, tn);
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
		if (cx < -120.0f || cz < -120.0f || (cx > CITY + 40.0f && cx < CITY + 140.0f)) continue;                // not in the sea, not on the hills, not on the freeway
		Box b; memset(&b, 0, sizeof b);
		b.x0 = cx - w / 2; b.x1 = cx + w / 2; b.z0 = cz - d / 2; b.z1 = cz + d / 2; b.y0 = b.yb = 0; b.y1 = h;
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

#include "scenery.h"

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
	scenery_static();
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
// dynamic: Otto and the cars (rebuilt every frame from oriented prisms; vertex colours only, white texture)
// =============================================================================================================================
static Mesh dm[D_COUNT];
static V3 CAMR, CAMU;                                                      // the camera's right and up vectors, for facing quads (set in scene_build)

// a prism between points a and b: the cross-section is a rectangle with half-extents (w0, d0) at a and (w1, d1) at b, spanned by the side vector s and the depth vector f
static void prism(Mesh *m, V3 a, V3 b, V3 s, V3 f, float w0, float d0, float w1, float d1, V3 col, bool capA)
{
	V3 P[2][4];
	static const int sx[4] = { -1, 1, 1, -1 }, sf[4] = { -1, -1, 1, 1 };
	V3 mid = mul(add(a, b), 0.5f);
	for (int i = 0; i < 4; i++) {
		P[0][i] = add(a, add(mul(s, sx[i] * w0), mul(f, sf[i] * d0)));
		P[1][i] = add(b, add(mul(s, sx[i] * w1), mul(f, sf[i] * d1)));
	}
	for (int i = 0; i < 4; i++) {
		int j = (i + 1) & 3;
		V3 n = norm(cross(sub(P[0][j], P[0][i]), sub(P[1][i], P[0][i])));
		V3 fc = mul(add(add(P[0][i], P[0][j]), add(P[1][i], P[1][j])), 0.25f);
		if (dot(n, sub(fc, mid)) < 0) n = mul(n, -1);
		float k = shadev(n), kb = k * 0.8f;
		float c0[3] = { col.x * kb, col.y * kb, col.z * kb }, c1[3] = { col.x * k, col.y * k, col.z * k };
		SV A = sv(P[0][i].x, P[0][i].y, P[0][i].z, 0.5f, 0.5f, c0, 1), B = sv(P[0][j].x, P[0][j].y, P[0][j].z, 0.5f, 0.5f, c0, 1);
		SV C = sv(P[1][j].x, P[1][j].y, P[1][j].z, 0.5f, 0.5f, c1, 1), D = sv(P[1][i].x, P[1][i].y, P[1][i].z, 0.5f, 0.5f, c1, 1);
		quad(m, &A, &B, &C, &D);
	}
	V3 up = norm(sub(b, a));
	for (int e = 0; e < 2; e++) {
		if (e == 0 && !capA) continue;
		V3 n = e ? up : mul(up, -1);
		float k = shadev(n) * (e ? 1.0f : 0.75f);
		float c[3] = { col.x * k, col.y * k, col.z * k };
		SV A = sv(P[e][0].x, P[e][0].y, P[e][0].z, 0.5f, 0.5f, c, 1), B = sv(P[e][1].x, P[e][1].y, P[e][1].z, 0.5f, 0.5f, c, 1);
		SV C = sv(P[e][2].x, P[e][2].y, P[e][2].z, 0.5f, 0.5f, c, 1), D = sv(P[e][3].x, P[e][3].y, P[e][3].z, 0.5f, 0.5f, c, 1);
		quad(m, &A, &B, &C, &D);
	}
}
// ---- Otto: CPU linear-blend skinning of the Blender model, posed from the retargeted CMU mocap (walk / jog / sprint) ------------------------------
#include "otto_gait.h"
typedef struct { float w, x, y, z; } HQuat;
typedef struct { V3 p, x, y, z; } BonePose;                              // bone head in the world and the bone's axes in the world
static BonePose bones[OTTO_NB];
static V3 skin_pos[OTTO_NV];
static V3 arr3(const float *a) { return v3(a[0], a[1], a[2]); }
static V3 bone_rot(const BonePose *b, V3 p) { return add(mul(b->x, p.x), add(mul(b->y, p.y), mul(b->z, p.z))); }
static HQuat hq(const float *q) { return (HQuat){ q[0], q[1], q[2], q[3] }; }
static HQuat qblend(HQuat a, HQuat b, float t)
{
	float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z, s = d < 0 ? -1 : 1;
	HQuat q = { a.w * (1 - t) + b.w * t * s, a.x * (1 - t) + b.x * t * s, a.y * (1 - t) + b.y * t * s, a.z * (1 - t) + b.z * t * s };
	float k = 1 / sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	q.w *= k; q.x *= k; q.y *= k; q.z *= k;
	return q;
}
static HQuat qmul(HQuat a, HQuat b) { return (HQuat){ a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w }; }
static HQuat qaxis(float angle, int axis) { HQuat q = { cosf(angle * .5f), 0, 0, 0 }; if (axis == 0) q.x = sinf(angle * .5f); else if (axis == 1) q.y = sinf(angle * .5f); else q.z = sinf(angle * .5f); return q; }
static BonePose qframe(HQuat q)                                          // the rotation matrix of q, as three axis vectors
{
	return (BonePose){ v3(0, 0, 0), v3(1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.w * q.z), 2 * (q.x * q.z - q.w * q.y)),
		v3(2 * (q.x * q.y - q.w * q.z), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z + q.w * q.x)),
		v3(2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y)) };
}
static const float OTTO_BOOST = 1.32f;
#include "ped_mesh.h"

// an orthonormal frame whose Y axis is `y`, with X as close as possible to `xref` (Z = X x Y)
static void frame_of(V3 y, V3 xref, V3 fallback, V3 *X, V3 *Y, V3 *Z)
{
	y = norm(y);
	V3 x = sub(xref, mul(y, dot(xref, y)));
	if (vlen(x) < 0.2f) x = sub(fallback, mul(y, dot(fallback, y)));
	x = norm(x);
	*X = x; *Y = y; *Z = cross(x, y);
}
// put bone i between world points h (its head) and t (its tail), keeping the sideways axis near `left`
static void bone_between(int i, V3 h, V3 t, V3 left, V3 forward)
{
	V3 x, y, z, rx, ry, rz;
	frame_of(sub(t, h), left, forward, &x, &y, &z);
	frame_of(sub(arr3(otto_bones[i].tail), arr3(otto_bones[i].head)), v3(1, 0, 0), v3(0, 0, 1), &rx, &ry, &rz);
	bones[i] = (BonePose){ h, add(mul(x, rx.x), add(mul(y, ry.x), mul(z, rz.x))), add(mul(x, rx.y), add(mul(y, ry.y), mul(z, rz.y))), add(mul(x, rx.z), add(mul(y, ry.z), mul(z, rz.z))) };
}
static V3 GUN_POS, GUN_DIR, GUN_LEFT;                                       // where the right hand holds a gun, and which way it points (set by pose_skeleton)

// two-bone IK: the middle joint between root and target, bending toward hint (the target is pulled in if it is out of reach)
static V3 ik_joint(V3 root, V3 target, float l1, float l2, V3 hint, V3 *reached)
{
	V3 d = sub(target, root);
	float dd = vlen(d);
	if (dd > l1 + l2 - 0.01f) dd = l1 + l2 - 0.01f;
	if (dd < 0.05f) dd = 0.05f;
	V3 dn = norm(d);
	float a = (l1 * l1 - l2 * l2 + dd * dd) / (2 * dd), hh = sqrtf(fmaxf(0, l1 * l1 - a * a));
	V3 perp = sub(hint, mul(dn, dot(hint, dn)));
	perp = vlen(perp) > 1e-4f ? norm(perp) : cross(dn, v3(0, 1, 0));
	*reached = add(root, mul(dn, dd));
	return add(add(root, mul(dn, a)), mul(perp, hh));
}
static float seg_len(int bone) { return vlen(sub(arr3(otto_bones[bone].tail), arr3(otto_bones[bone].head))); }

// How Otto (or an officer) stands and holds himself when fighting. aim 0..1 = how much of the stance; gun = holding a gun (else fists);
// punchU 0..1 runs through a punch (wind-up, strike, recovery) with punchSide 0 = right jab, 1 = left cross.
typedef struct { float aim; bool gun; int weapon; float punchU; int punchSide; float crouch; } Stance;

static float punch_reach(float u)                                          // how far the fist is from the shoulder, 0..1 of the arm
{
	if (u < 0.25f) return 0.22f - 0.12f * (u / 0.25f);                    // pull back
	if (u < 0.45f) { float k = (u - 0.25f) / 0.2f; return 0.10f + 0.9f * (1 - (1 - k) * (1 - k)); }   // snap out
	if (u < 0.58f) return 1.0f;                                           // hold
	float k = (u - 0.58f) / 0.42f; return 1.0f - 0.8f * k * k * (3 - 2 * k);   // back to guard
}
static float punch_twist(float u)                                          // torso rotation: back, then through the punch
{
	if (u < 0.25f) return -0.35f * (u / 0.25f);
	if (u < 0.5f) { float k = (u - 0.25f) / 0.25f; return -0.35f + 1.05f * k; }
	float k = fminf(1.0f, (u - 0.5f) / 0.5f); return 0.7f * (1 - k);
}

// Poses the skeleton in the world. P = feet, face = heading, speed / phase pick the gait frame, fall = 0..1 lying down.
static void pose_skeleton(V3 P, float face, float speed, float phase, float fall, float time, const Stance *st)
{
	V3 up = v3(0, 1, 0), f = v3(sinf(face), 0, -cosf(face));
	V3 R = norm(cross(f, up));
	float amp = gait_ease(speed / 0.9f) * (1 - fall);
	float wUp = st->aim * (1 - fall);                                       // the upper body takes the stance whether or not Otto is walking
	float wLeg = wUp * (1 - amp);                                           // the legs only when he stands still
	float wCr = st->crouch * (1 - fall) * (1 - amp);                        // crouched behind cover
	bool punching = !st->gun && st->punchU > 0;
	float twistSide = st->punchSide ? -1.0f : 1.0f;
	float twist = 0;                                                        // torso rotation about the vertical: + brings the right shoulder forward
	if (wUp > 0.02f) twist = wUp * (st->gun ? 0.38f : (punching ? punch_twist(st->punchU) * twistSide : 0.3f));
	int ca, cb; float blend = gait_blend(speed, &ca, &cb);
	float key = (phase - floorf(phase)) * GAIT_KEYS; int k = (int)key, n = (k + 1) % GAIT_KEYS; float t = key - k;
	const GaitKey *a = &gait_clips[ca].key[k], *a1 = &gait_clips[ca].key[n], *b = &gait_clips[cb].key[k], *b1 = &gait_clips[cb].key[n];
	float height = lerpf(lerpf(a->height, a1->height, t), lerpf(b->height, b1->height, t), blend);
	float lean = amp * (0.02f + 0.07f * gait_ease((speed - 2.0f) / 3.0f)) + fall * 1.5f;
	V3 L = mul(R, -1), u = norm(add(mul(up, cosf(lean)), mul(f, sinf(lean)))), fw = norm(cross(L, u));
	float breathe = 0.004f * sinf(time * 2.2f) * (1 - amp);
	float pelvis = lerpf(lerpf(0.93f, height, amp) + breathe - 0.075f * wLeg - 0.40f * wCr, 0.22f, fall);     // knees bend a little in the stance, a lot in cover
	BonePose world = { add(P, mul(up, pelvis)), L, u, fw };
	for (int i = 0; i < OTTO_NB; i++) {
		HQuat q = qblend(qblend(hq(a->q[i]), hq(a1->q[i]), t), qblend(hq(b->q[i]), hq(b1->q[i]), t), blend), idle = { 1, 0, 0, 0 };
		q = qblend(idle, q, amp);
		if (twist != 0) {                                                   // the spine turns the shoulders, the head turns back to look ahead
			if (i == OB_SPINE) q = qmul(qaxis(twist * 0.42f, 1), q);
			else if (i == OB_CHEST) q = qmul(qaxis(twist * 0.58f, 1), q);
			else if (i == OB_HEAD) q = qmul(qaxis(-twist * 1.1f, 1), q);
			else if (i == OB_NECK) q = qmul(qaxis(-twist * 0.2f, 1), q);
		}
		if (wCr > 0.02f) {                                                  // hunched forward in cover
			if (i == OB_SPINE) q = qmul(qaxis(0.28f * wCr, 0), q);
			else if (i == OB_CHEST) q = qmul(qaxis(0.22f * wCr, 0), q);
			else if (i == OB_HEAD) q = qmul(qaxis(-0.3f * wCr, 0), q);
		}
		BonePose local = qframe(q);
		int par = otto_bones[i].parent;
		BonePose *parent = par < 0 ? &world : &bones[par];
		V3 offset = par < 0 ? v3(0, 0, 0) : sub(arr3(otto_bones[i].head), arr3(otto_bones[par].head));
		bones[i] = (BonePose){ add(parent->p, bone_rot(parent, offset)), bone_rot(parent, local.x), bone_rot(parent, local.y), bone_rot(parent, local.z) };
	}
	// legs: a staggered stance (the left foot forward and the feet apart; wider and more forward for a boxer) when Otto stands still
	if ((wLeg > 0.02f || wCr > 0.02f) && fall < 0.3f) {
		bool cr = wCr > wLeg;
		float w = gait_ease(cr ? wCr : wLeg);
		float fwd = cr ? 0.12f : st->gun ? 0.24f : 0.32f, back = cr ? 0.1f : st->gun ? 0.15f : 0.24f, wide = cr ? 0.15f : st->gun ? 0.16f : 0.2f;
		for (int sd = 0; sd < 2; sd++) {                                    // 0 = left leg, 1 = right leg
			int th = sd ? OB_R_THIGH : OB_L_THIGH, sh = th + 1, ft = th + 2, tt = th + 3;
			float side = sd ? -1.0f : 1.0f;
			V3 A = add(P, add(mul(L, side * wide), add(mul(f, sd ? -back : fwd), v3(0, 0.13f, 0))));
			V3 H = bones[th].p, Kfk = bones[sh].p, Afk = bones[ft].p, Tfk = bones[tt].p, Tip = add(bones[tt].p, bone_rot(&bones[tt], sub(arr3(otto_bones[tt].tail), arr3(otto_bones[tt].head))));
			V3 reach;
			V3 K = ik_joint(H, A, seg_len(th), seg_len(sh), add(mul(f, 1.0f), mul(L, side * 0.15f)), &reach);
			V3 toe0 = add(A, add(mul(f, 0.17f), v3(0, -0.07f, 0))), tip0 = add(toe0, add(mul(f, 0.13f), v3(0, -0.015f, 0)));
			V3 K2 = lerp3(Kfk, K, w), A2 = lerp3(Afk, reach, w), T2 = lerp3(Tfk, add(reach, add(mul(f, 0.17f), v3(0, -0.07f, 0))), w), P2 = lerp3(Tip, add(reach, add(mul(f, 0.30f), v3(0, -0.085f, 0))), w);
			(void)toe0; (void)tip0;
			bone_between(th, H, K2, L, f); bone_between(sh, K2, A2, L, f); bone_between(ft, A2, T2, L, f); bone_between(tt, T2, P2, L, f);
		}
	}
	if (fall < 0.5f) {                                                      // keep the soles out of the floor (clip blends can sink a foot a little)
		float low = 1e9f;
		for (int s = 0; s < 2; s++) {
			int i = s ? OB_R_FOOT : OB_L_FOOT;
			for (int z = 0; z < 2; z++) { V3 sole = add(bones[i].p, bone_rot(&bones[i], v3(0, -0.13f, z ? 0.30f : -0.11f))); low = fminf(low, dot(sub(sole, P), up)); }
		}
		if (low < 0) for (int i = 0; i < OTTO_NB; i++) bones[i].p = sub(bones[i].p, mul(up, low));
	}
	// arms
	GUN_POS = v3(0, 0, 0); GUN_DIR = f; GUN_LEFT = L;
	if (wUp > 0.02f && fall < 0.3f) {
		float w = gait_ease(wUp);
		V3 dir = norm(add(f, mul(up, 0.04f)));
		V3 S[2] = { bones[OB_L_UPPERARM].p, bones[OB_R_UPPERARM].p };
		float l1 = seg_len(OB_R_UPPERARM), l2 = seg_len(OB_R_FOREARM), l3 = seg_len(OB_R_HAND);
		V3 Wt[2], hint[2];                                                  // wrist targets and the direction each elbow bends toward
		bool arm[2] = { true, true };
		if (st->gun) {
			// right hand holds the grip with the arm almost straight out, the left hand supports the front of the gun
			Wt[1] = add(S[1], mul(dir, (l1 + l2) * 0.9f)); hint[1] = add(mul(up, -1.0f), mul(L, 0.35f));
			V3 gunAt = add(Wt[1], mul(dir, l3 * 0.65f));
			float sup = st->weapon == W_SHOTGUN ? 0.40f : st->weapon == W_SMG ? 0.26f : 0.03f;
			V3 grip = add(gunAt, add(mul(dir, sup), v3(0, -0.04f, 0)));
			hint[0] = add(mul(up, -1.0f), mul(L, 0.9f));
			V3 Wd0 = dir, wr0;                                               // the hand lies along the forearm: find the wrist that puts the middle of the hand on the grip
			for (int it = 0; it < 3; it++) { Wt[0] = sub(grip, mul(Wd0, l3 * 0.5f)); V3 e = ik_joint(S[0], Wt[0], l1, l2, hint[0], &wr0); Wd0 = norm(sub(wr0, e)); }
		} else {
			// boxing: the punching arm goes through its reach, the other hand stays up by the chin
			int pa = st->punchSide ? 0 : 1, ga = 1 - pa;                     // the arm that punches, the arm that guards (indices as above: 1 = right)
			float reach = punching ? punch_reach(st->punchU) : 0.2f;
			V3 pdir = norm(add(f, mul(L, (pa ? -1.0f : 1.0f) * 0.12f)));
			Wt[pa] = add(S[pa], add(mul(pdir, 0.12f + (l1 + l2 - 0.12f) * 0.98f * reach), v3(0, -0.04f + 0.1f * (1 - reach), 0)));
			hint[pa] = add(mul(up, -1.0f), mul(L, (pa ? -1.0f : 1.0f) * (0.6f + 0.8f * (1 - reach))));
			Wt[ga] = add(S[ga], add(mul(f, 0.26f), add(mul(L, (ga ? 1.0f : -1.0f) * -0.04f), v3(0, 0.08f, 0)))); hint[ga] = add(mul(up, -1.0f), mul(L, (ga ? -1.0f : 1.0f) * 0.9f));
		}
		for (int sd = 0; sd < 2; sd++) {
			int ua = sd ? OB_R_UPPERARM : OB_L_UPPERARM, fa = ua + 1, hd = ua + 2;
			if (!arm[sd]) continue;
			V3 E0 = bones[fa].p, W0 = bones[hd].p, T0 = add(W0, bone_rot(&bones[hd], sub(arr3(otto_bones[hd].tail), arr3(otto_bones[hd].head))));
			V3 Wr; V3 E1 = ik_joint(S[sd], Wt[sd], l1, l2, hint[sd], &Wr);
			V3 Wd = norm(sub(Wr, E1));
			V3 T1 = add(Wr, mul(sd == 1 && st->gun ? dir : Wd, l3));
			V3 E = lerp3(E0, E1, w), W = lerp3(W0, Wr, w), T = lerp3(T0, T1, w);
			bone_between(ua, S[sd], E, L, f); bone_between(fa, E, W, L, f); bone_between(hd, W, T, L, f);
			if (sd == 1 && st->gun) { GUN_POS = add(W, mul(dir, l3 * 0.65f)); GUN_DIR = norm(lerp3(norm(sub(T0, W0)), dir, w)); }
		}
	}
}

// a gun held in the right hand: pistol, SMG or shotgun, built from a few dark prisms along GUN_DIR
static void draw_gun(int weapon, V3 *muzzleOut)
{
	if (weapon == W_FISTS) return;
	static const V3 steel = { 0.55f, 0.56f, 0.6f }, dark = { 0.2f, 0.2f, 0.22f }, wood = { 0.55f, 0.36f, 0.2f };
	V3 d = GUN_DIR, L = GUN_LEFT, up = norm(cross(L, d));                    // up from the hand's left and the barrel direction
	if (up.y < 0) up = mul(up, -1);
	V3 p = GUN_POS;
	Mesh *m = &dm[D_OPAQUE];
	#define GBOX(a0, a1, hw, hh, col) prism(m, add(p, mul(d, a0)), add(p, mul(d, a1)), L, up, hw, hh, hw, hh, col, true)
	if (weapon == W_PISTOL) {
		GBOX(-0.07f, 0.26f, 0.03f, 0.045f, steel);
		prism(m, add(p, add(mul(d, -0.04f), mul(up, -0.02f))), add(p, add(mul(d, -0.09f), mul(up, -0.15f))), L, d, 0.028f, 0.04f, 0.028f, 0.04f, dark, true);     // grip
		*muzzleOut = add(p, mul(d, 0.28f));
	} else if (weapon == W_SMG) {
		GBOX(-0.14f, 0.40f, 0.035f, 0.06f, dark);
		prism(m, add(p, add(mul(d, 0.10f), mul(up, -0.03f))), add(p, add(mul(d, 0.10f), mul(up, -0.2f))), L, d, 0.024f, 0.04f, 0.024f, 0.04f, steel, true);          // magazine
		GBOX(0.40f, 0.52f, 0.014f, 0.016f, steel);
		*muzzleOut = add(p, mul(d, 0.54f));
	} else {
		GBOX(-0.26f, 0.12f, 0.04f, 0.06f, wood);
		GBOX(0.12f, 0.86f, 0.03f, 0.036f, dark);
		GBOX(0.22f, 0.52f, 0.04f, 0.03f, wood);
		*muzzleOut = add(p, mul(d, 0.88f));
	}
	#undef GBOX
}

// Skin a mesh with the current pose and emit it flat-shaded; colours[role] gives each role's colour
static void skin_mesh(const OttoVert *verts, int nv, const OttoTri *tris, int nt, const float (*colors)[3], V3 P, float scale, bool hideBag)
{
	Mesh *m = &dm[D_OPAQUE];
	for (int i = 0; i < nv; i++) {
		const OttoVert *v = &verts[i];
		V3 q = v3(0, 0, 0);
		for (int j = 0; j < 4; j++) if (v->w[j] > 0) {
			int bi = v->b[j];
			q = add(q, mul(add(bones[bi].p, bone_rot(&bones[bi], sub(arr3(v->p), arr3(otto_bones[bi].head)))), v->w[j]));
		}
		skin_pos[i] = scale == 1.0f ? q : add(P, mul(sub(q, P), scale));
	}
	for (int i = 0; i < nt; i++) {
		const OttoTri *tr = &tris[i];
		if (hideBag && tr->role >= RL_SACK && tr->role <= RL_DOLLAR && tr->role != RL_WHITE) continue;
		V3 A = skin_pos[tr->v[0]], B = skin_pos[tr->v[1]], C = skin_pos[tr->v[2]];
		V3 nn = cross(sub(B, A), sub(C, A));
		float l = vlen(nn);
		if (l < 1e-9f) continue;
		nn = mul(nn, 1.0f / l);
		float sh = shade(nn.x, nn.y, nn.z);
		const float *c = colors[tr->role];
		V3 pt[3] = { A, B, C };
		for (int q = 0; q < 3; q++) {
			float ao = 0.78f + 0.22f * sat((pt[q].y - P.y) / 1.5f), kk = sh * OTTO_BOOST * ao, col[3] = { c[0] * kk, c[1] * kk, c[2] * kk };
			SV s = sv(pt[q].x, pt[q].y, pt[q].z, 0.5f, 0.5f, col, 1);
			put(m, &s);
		}
	}
}

static void blob_shadow(float x, float y, float z, float r, float a)
{
	const float kc[3] = { 0, 0, 0 };
	SV A = sv(x - r, y, z - r, 0, 0, kc, a), B = sv(x + r, y, z - r, 1, 0, kc, a), C = sv(x + r, y, z + r, 1, 1, kc, a), D = sv(x - r, y, z + r, 0, 1, kc, a);
	quad(&dm[D_ALPHA], &A, &B, &C, &D);
}

static void muzzle_flash(V3 at);
static void draw_otto(const Game *g)
{
	const Player *p = &g->p;
	if (p->car >= 0) return;
	V3 P = v3(p->p[0], p->p[1], p->p[2]);
	bool dead = p->ps == PS_WASTED;
	float fall = dead ? sat(p->psT / 0.35f) : 0;
	bool armed = p->weapon != W_FISTS;
	Stance st = { dead ? 0 : p->aimT, armed && p->punchT <= 0, p->weapon, p->punchT > 0 ? 1.0f - p->punchT / 0.34f : 0.0f, p->punchSide, dead ? 0 : p->coverT * (1.0f - 0.8f * p->aimT) };
	if (!armed || p->punchT > 0) st.gun = false;
	pose_skeleton(P, p->face, p->speed, p->phase, fall, g->time, &st);
	skin_mesh(otto_verts, OTTO_NV, otto_tris, OTTO_NT, otto_colors, P, 1.0f, false);
	if (!dead && st.gun && p->aimT > 0.25f) { V3 mz; draw_gun(p->weapon, &mz); if (p->flashT > 0) muzzle_flash(mz); }
	blob_shadow(p->p[0], p->p[1] + 0.07f, p->p[2], 0.62f, 0.5f);
}

// ---- pedestrians: the low-poly model in an outfit made from the person's random choices ----------------------------------------------------
static const float SHIRTS[12][3] = { { .85f, .2f, .2f }, { .2f, .4f, .8f }, { .95f, .95f, .9f }, { .2f, .6f, .3f }, { .95f, .8f, .2f }, { .55f, .25f, .6f }, { .9f, .5f, .15f }, { .1f, .1f, .12f }, { .5f, .7f, .85f }, { .85f, .45f, .6f }, { .4f, .4f, .45f }, { .2f, .55f, .55f } };
static const float PANTSC[6][3] = { { .18f, .27f, .5f }, { .12f, .12f, .14f }, { .62f, .55f, .38f }, { .4f, .4f, .43f }, { .88f, .88f, .85f }, { .2f, .3f, .22f } };
static const float SKINS[6][3] = { { .93f, .72f, .55f }, { .8f, .58f, .42f }, { .62f, .42f, .3f }, { .45f, .3f, .22f }, { .96f, .8f, .66f }, { .72f, .5f, .36f } };
static const float HAIRS[7][3] = { { .08f, .07f, .07f }, { .3f, .18f, .1f }, { .8f, .65f, .3f }, { .6f, .25f, .1f }, { .6f, .6f, .62f }, { .8f, .15f, .15f }, { .15f, .3f, .7f } };
static const float SHOES[3][3] = { { .15f, .13f, .12f }, { .9f, .9f, .88f }, { .4f, .25f, .15f } };
static void outfit(const Ped *p, float col[RL_COUNT][3])
{
	for (int r = 0; r < RL_COUNT; r++) for (int k = 0; k < 3; k++) col[r][k] = 0.5f;
	static const float NAVY[3] = { 0.13f, 0.22f, 0.55f }, NAVY_D[3] = { 0.07f, 0.1f, 0.26f }, BLACKISH[3] = { 0.1f, 0.1f, 0.12f }, SWATC[3] = { 0.14f, 0.15f, 0.18f };
	for (int k = 0; k < 3; k++) {
		bool cop = p->cop == 1, swat = p->cop == 2;
		col[RL_SUIT][k] = swat ? SWATC[k] : cop ? NAVY[k] : SHIRTS[p->shirt % 12][k]; col[RL_SUIT_DARK][k] = col[RL_SUIT][k] * 0.7f;
		col[RL_PANTS][k] = swat ? SWATC[k] * 0.8f : cop ? NAVY_D[k] : PANTSC[p->pants % 6][k]; col[RL_PANTS_DARK][k] = col[RL_PANTS][k] * 0.7f;
		col[RL_SKIN][k] = SKINS[p->skin % 6][k]; col[RL_SKIN_DARK][k] = SKINS[p->skin % 6][k] * 0.85f;
		col[RL_BLACK][k] = swat ? 0.55f : cop ? 0.9f : 0.08f;                                              // belt / badge: bright on officers so the uniform reads
		col[RL_BEANIE][k] = swat || cop ? BLACKISH[k] : HAIRS[p->hair % 7][k]; col[RL_CUFF][k] = col[RL_BEANIE][k] * 1.15f;
		col[RL_BOOT][k] = swat || cop ? 0.08f : SHOES[p->shoe % 3][k]; col[RL_WHITE][k] = 0.9f;
	}
}

// a crowd member far from the camera: seven blocks (legs, arms, torso, head, hair), walking by swinging them; about 70 triangles
static void draw_ped_cheap(const Ped *p)
{
	float col[RL_COUNT][3];
	outfit(p, col);
	Mesh *m = &dm[D_OPAQUE];
	float sc = p->scale, sy = sinf(p->yaw), cy = cosf(p->yaw);
	V3 up = v3(0, 1, 0), f = v3(sy, 0, -cy), R = v3(cy, 0, sy);
	V3 shirt = mul(arr3(col[RL_SUIT]), OTTO_BOOST), pants = mul(arr3(col[RL_PANTS]), OTTO_BOOST), skin = mul(arr3(col[RL_SKIN]), OTTO_BOOST), hair = mul(arr3(col[RL_BEANIE]), OTTO_BOOST), shoe = mul(arr3(col[RL_BOOT]), OTTO_BOOST);
	V3 base = v3(p->x, 0, p->z);
	if (p->state == PED_DOWN) {                                               // lying on the ground: the same blocks laid down along the heading
		V3 o = add(base, v3(0, 0.12f * sc, 0));
		prism(m, add(o, mul(f, -0.9f * sc)), add(o, mul(f, -0.3f * sc)), R, up, 0.14f * sc, 0.1f * sc, 0.14f * sc, 0.1f * sc, pants, true);
		prism(m, add(o, mul(f, -0.3f * sc)), add(o, mul(f, 0.35f * sc)), R, up, 0.2f * sc, 0.12f * sc, 0.2f * sc, 0.12f * sc, shirt, true);
		prism(m, add(o, mul(f, 0.35f * sc)), add(o, mul(f, 0.62f * sc)), R, up, 0.1f * sc, 0.1f * sc, 0.1f * sc, 0.1f * sc, skin, true);
		return;
	}
	float swing = sinf(p->phase * 2 * PI_F) * 0.55f * clampf(p->speed / 1.2f, 0, 1);
	float hipH = 0.92f * sc, legL = 0.9f * sc;
	for (int sd = -1; sd <= 1; sd += 2) {
		float a = swing * sd;
		V3 hip = add(add(base, mul(R, sd * 0.1f * sc)), mul(up, hipH));
		V3 foot = add(sub(hip, mul(up, cosf(a) * legL)), mul(f, sinf(a) * legL));
		prism(m, foot, hip, R, f, 0.08f * sc, 0.09f * sc, 0.1f * sc, 0.11f * sc, pants, false);
		prism(m, foot, add(foot, v3(0, 0.07f * sc, 0)), R, f, 0.08f * sc, 0.1f * sc, 0.08f * sc, 0.1f * sc, shoe, true);
		V3 sh = add(add(base, mul(R, sd * 0.27f * sc)), mul(up, 1.42f * sc));
		float b = -swing * sd * 1.1f;
		V3 hand = add(sub(sh, mul(up, cosf(b) * 0.62f * sc)), mul(f, sinf(b) * 0.62f * sc));
		prism(m, hand, sh, R, f, 0.06f * sc, 0.07f * sc, 0.08f * sc, 0.09f * sc, shirt, false);
	}
	prism(m, add(base, mul(up, 0.92f * sc)), add(base, mul(up, 1.5f * sc)), R, f, 0.2f * sc, 0.12f * sc, 0.25f * sc, 0.13f * sc, shirt, false);
	prism(m, add(base, mul(up, 1.6f * sc)), add(base, mul(up, 1.82f * sc)), R, f, 0.1f * sc, 0.11f * sc, 0.1f * sc, 0.11f * sc, skin, true);
	prism(m, add(base, mul(up, 1.76f * sc)), add(base, mul(up, 1.9f * sc)), R, f, 0.115f * sc, 0.125f * sc, 0.1f * sc, 0.11f * sc, hair, true);
}

static void draw_peds(const Game *g, const Scene *s)
{
	int order[MAX_PED], n = 0;
	float dist[MAX_PED];
	for (int i = 0; i < MAX_PED; i++) {
		const Ped *p = &g->ped[i];
		if (!p->on) continue;
		float dx = p->x - s->eye[0], dz = p->z - s->eye[2], d2 = dx * dx + dz * dz, lim = g->lowq ? 56.0f : 86.0f;
		if (d2 > lim * lim || dx * s->fwd[0] + dz * s->fwd[2] < -4.0f) continue;
		order[n] = i; dist[i] = d2; n++;
	}
	for (int i = 1; i < n; i++) { int k = order[i], j = i - 1; while (j >= 0 && dist[order[j]] > dist[k]) { order[j + 1] = order[j]; j--; } order[j + 1] = k; }
	int maxDraw = g->lowq ? 3 : 6, maxCheap = g->lowq ? 8 : 22, skinned = 0, cheap = 0;
	float nearLim = g->lowq ? 26.0f : 40.0f, farLim = g->lowq ? 55.0f : 85.0f;
	for (int q = 0; q < n; q++) {
		const Ped *pc = &g->ped[order[q]];
		float d2 = dist[order[q]];
		if (skinned >= maxDraw || d2 > nearLim * nearLim) {                         // the crowd further away: blocks instead of skinned bodies
			if (cheap >= maxCheap || d2 > farLim * farLim) { if (d2 > farLim * farLim) break; continue; }
			cheap++;
			draw_ped_cheap(pc);
			blob_shadow(pc->x, 0.07f, pc->z, 0.5f, 0.4f);
			continue;
		}
		skinned++;
		const Ped *p = &g->ped[order[q]];
		float col[RL_COUNT][3];
		outfit(p, col);
		V3 P = v3(p->x, 0, p->z);
		float fall = p->state == PED_DOWN ? (p->dead ? 1.0f : fminf(sat((3.2f - p->timer) / 0.3f), sat(p->timer / 0.35f))) : 0;
		Stance cs = { p->cop ? p->aimT : 0, true, p->cop == 2 ? W_SHOTGUN : W_PISTOL, 0, 0, 0 };
		pose_skeleton(P, p->yaw, p->speed, p->phase, fall, g->time + order[q], &cs);
		skin_mesh(ped_verts, PED_NV, ped_tris, PED_NT, (const float (*)[3])col, P, p->scale, true);
		if (p->cop && p->aimT > 0.25f && fall < 0.3f) { V3 mz; draw_gun(p->cop == 2 ? W_SHOTGUN : W_PISTOL, &mz); if (p->flashT > 0) muzzle_flash(mz); }
		blob_shadow(p->x, 0.07f, p->z, 0.55f, 0.45f);
	}
}

// ---- cars: the Blender body mesh (flat-shaded triangles with a tag) + four wheels that roll and steer --------------------------------------
#include "car_mesh.h"
typedef struct { V3 x, y, z, o; } CFrame;                                 // x = the car's left, y = up, z = forward (right-handed, like Otto's)
static V3 cf_pt(const CFrame *f, const float v[3]) { return add(f->o, add(mul(f->x, v[0]), add(mul(f->y, v[1]), mul(f->z, v[2])))); }
static V3 cf_dir(const CFrame *f, V3 d) { return add(mul(f->x, d.x), add(mul(f->y, d.y), mul(f->z, d.z))); }

static void draw_car(const Game *g, int idx, bool wheels)
{
	const Car *c = &g->car[idx];
	const CarModel *cm = &car_models[c->kind];
	Mesh *m = &dm[D_OPAQUE];
	float sy = sinf(c->yaw), cy = cosf(c->yaw);
	V3 F = v3(sy, 0, -cy), R = v3(cy, 0, sy), U = v3(0, 1, 0);
	float cl = cosf(c->lean), sl = sinf(c->lean), cp = cosf(c->pitch), sp = sinf(c->pitch);
	CFrame fr;
	fr.x = mul(add(mul(R, cl), mul(U, sl)), -1);                             // roll about the forward axis (the left axis is -right)
	fr.z = add(mul(F, cp), mul(U, sp));                                      // pitch
	fr.y = norm(cross(fr.z, fr.x));
	fr.o = v3(c->x, 0, c->z);
	float flash = ((int)(g->time * 5.0f)) & 1;
	// body
	static V3 vp[400];
	static float vao[400];
	for (int i = 0; i < cm->nv; i++) { vp[i] = cf_pt(&fr, cm->v[i]); vao[i] = 0.74f + 0.26f * sat((cm->v[i][1] - 0.22f) / 0.8f); }
	for (int i = 0; i < cm->nt; i++) {
		const CarTri *t = &cm->t[i];
		V3 nn = cf_dir(&fr, v3(t->n[0], t->n[1], t->n[2]));
		const float *pc = car_palette[t->col];
		float col[3], sh = shade(nn.x, nn.y, nn.z) * OTTO_BOOST, lit;
		switch (t->tag) {
		case 1: col[0] = c->col[0]; col[1] = c->col[1]; col[2] = c->col[2]; lit = sh; break;
		case 2: lit = c->braking ? 2.2f : 0.75f; col[0] = pc[0]; col[1] = pc[1] * (c->braking ? 1.0f : 0.8f); col[2] = pc[2] * (c->braking ? 1.0f : 0.8f); break;
		case 3: lit = 1.7f; col[0] = pc[0]; col[1] = pc[1]; col[2] = pc[2]; break;
		case 4: lit = c->bot ? (flash ? 2.4f : 0.5f) : 0.7f; col[0] = pc[0]; col[1] = pc[1]; col[2] = pc[2]; break;
		case 5: lit = c->bot ? (flash ? 0.5f : 2.4f) : 0.7f; col[0] = pc[0]; col[1] = pc[1]; col[2] = pc[2]; break;
		default: col[0] = pc[0]; col[1] = pc[1]; col[2] = pc[2]; lit = sh; break;
		}
		if (c->wreck) { lit = t->tag >= 2 ? 0.0f : sh * 0.28f; col[0] = col[1] = col[2] = 0.35f; if (t->tag >= 2) { col[0] = col[1] = col[2] = 0.05f; lit = 1.0f; } }
		for (int q = 0; q < 3; q++) {
			float kk = lit * (t->tag == 0 || t->tag == 1 ? vao[t->v[q]] : 1.0f), cc[3] = { col[0] * kk, col[1] * kk, col[2] * kk };
			SV s = sv(vp[t->v[q]].x, vp[t->v[q]].y, vp[t->v[q]].z, 0.5f, 0.5f, cc, 1);
			put(m, &s);
		}
	}
	// wheels (not drawn on distant cars)
	float cr = cosf(c->wheel), sr = sinf(c->wheel);
	for (int w = 0; w < (wheels ? 4 : 0); w++) {
		bool front = w >= 2; int side = (w & 1) ? 1 : -1;                    // +1 = left (+X)
		float steer = front ? c->steer : 0, cs = cosf(steer), ss = sinf(steer);
		float cx = side * cm->track, cz = front ? cm->front : cm->rear, R0 = cm->wheelR;
		static V3 wp[WHEEL_NV];
		for (int i = 0; i < WHEEL_NV; i++) {
			float ax = wheel_v[i][0] * side, ry = wheel_v[i][1], rz = wheel_v[i][2];
			float y2 = (ry * cr - rz * sr) * R0, z2 = (ry * sr + rz * cr) * R0;     // roll about the axle
			float lx = ax * cs - z2 * ss, lz = ax * ss + z2 * cs;                    // steer about the vertical
			float v[3] = { cx + lx, R0 + y2, cz + lz };
			wp[i] = cf_pt(&fr, v);
		}
		for (int i = 0; i < WHEEL_NT; i++) {
			const CarTri *t = &wheel_t[i];
			V3 nn = norm(cross(sub(wp[t->v[1]], wp[t->v[0]]), sub(wp[t->v[2]], wp[t->v[0]])));
			const float *pc = car_palette[t->col];
			float sh = shade(fabsf(nn.x), nn.y, nn.z) * OTTO_BOOST * 0.9f, cc[3] = { pc[0] * sh, pc[1] * sh, pc[2] * sh };
			for (int q = 0; q < 3; q++) { SV s = sv(wp[t->v[q]].x, wp[t->v[q]].y, wp[t->v[q]].z, 0.5f, 0.5f, cc, 1); put(m, &s); }
		}
	}
	// soft shadow under the car
	{
		float hl = cm->hl * 1.12f, hw = cm->hw * 1.2f; const float k[3] = { 0, 0, 0 };
		V3 o = v3(c->x, 0.07f, c->z);
		V3 p0 = add(o, add(mul(R, -hw), mul(F, -hl))), p1 = add(o, add(mul(R, hw), mul(F, -hl))), p2 = add(o, add(mul(R, hw), mul(F, hl))), p3 = add(o, add(mul(R, -hw), mul(F, hl)));
		SV A = sv(p0.x, p0.y, p0.z, 0, 0, k, 0.55f), B = sv(p1.x, p1.y, p1.z, 1, 0, k, 0.55f), C = sv(p2.x, p2.y, p2.z, 1, 1, k, 0.55f), D = sv(p3.x, p3.y, p3.z, 0, 1, k, 0.55f);
		quad(&dm[D_ALPHA], &A, &B, &C, &D);
	}
}

// traffic lights: a pole on the far right corner of each approach, a dark head and a lamp in the signal's colour
static void draw_signals(const Game *g, const Scene *s)
{
	static const int cornerx[4] = { 1, -1, -1, 1 }, cornerz[4] = { 1, -1, 1, -1 };       // approach: east-bound, west-bound, south-bound, north-bound -> corner
	static const float fdx[4] = { 1, -1, 0, 0 }, fdz[4] = { 0, 0, 1, -1 };               // travel direction of each approach
	for (int jz = 1; jz < NB; jz++) for (int jx = 1; jx < NB; jx++) {
		float cx = jx * PITCH, cz = jz * PITCH, dx = cx - s->eye[0], dz = cz - s->eye[2];
		if (dx * dx + dz * dz > 85.0f * 85.0f || dx * s->fwd[0] + dz * s->fwd[2] < -14.0f) continue;
		for (int k = 0; k < 4; k++) {
			float px = cx + cornerx[k] * 11.9f, pz = cz + cornerz[k] * 11.9f;
			int st = signal_state(g, jx, jz, k < 2 ? 0 : 1);
			V3 col = st == 2 ? v3(0.2f, 1.9f, 0.5f) : st == 1 ? v3(2.0f, 1.6f, 0.2f) : v3(2.1f, 0.25f, 0.2f);
			V3 sx = v3(1, 0, 0), sz = v3(0, 0, 1);
			prism(&dm[D_OPAQUE], v3(px, 0, pz), v3(px, 4.3f, pz), sx, sz, 0.07f, 0.07f, 0.06f, 0.06f, v3(0.45f, 0.45f, 0.48f), false);
			prism(&dm[D_OPAQUE], v3(px, 3.9f, pz), v3(px, 4.6f, pz), sx, sz, 0.17f, 0.17f, 0.17f, 0.17f, v3(0.18f, 0.18f, 0.2f), true);
			V3 lc = v3(px - fdx[k] * 0.2f, 4.25f, pz - fdz[k] * 0.2f);
			prism(&dm[D_OPAQUE], add(lc, v3(0, -0.1f, 0)), add(lc, v3(0, 0.1f, 0)), sx, sz, 0.1f, 0.1f, 0.1f, 0.1f, col, false);
		}
	}
}

// ---- moving scenery: the Ferris wheel at the end of the pier, cars on the elevated freeway -----------------------------------------------------------
static void beam(V3 a, V3 b, float half, V3 col)
{
	V3 d = norm(sub(b, a)), sdir = v3(1, 0, 0), f = norm(cross(d, sdir));
	prism(&dm[D_OPAQUE], a, b, sdir, f, half, half, half, half, col, true);
}
static void draw_scenery_dynamic(const Game *g, const Scene *s)
{
	float t = g->time;
	// Ferris wheel facing along the pier: a rim of 12 beams, 6 spokes, 12 cabins that stay upright, and two A-frames holding the hub
	{
		V3 c = v3(PIER_X + 22.0f, 18.0f, 256.0f);
		float dx = c.x - s->eye[0], dz = c.z - s->eye[2];
		if (dx * dx + dz * dz < 380.0f * 380.0f && dx * s->fwd[0] + dz * s->fwd[2] > -30.0f) {
			const float R = 13.5f;
			float rot = t * 0.22f;
			V3 rim = v3(1.6f, 1.6f, 1.55f), steel = v3(1.3f, 1.3f, 1.35f), cabin[4] = { v3(2.0f, 0.4f, 0.3f), v3(2.0f, 1.6f, 0.3f), v3(0.4f, 0.9f, 2.0f), v3(0.4f, 1.8f, 0.7f) };
			V3 pts[12];
			for (int i = 0; i < 12; i++) { float a = rot + i * 2 * PI_F / 12; pts[i] = v3(c.x, c.y + sinf(a) * R, c.z + cosf(a) * R); }
			for (int i = 0; i < 12; i++) beam(pts[i], pts[(i + 1) % 12], 0.22f, rim);
			for (int i = 0; i < 12; i += 2) beam(c, pts[i], 0.12f, v3(1.3f, 1.3f, 1.3f));
			for (int i = 0; i < 12; i++) {
				V3 p = sub(pts[i], v3(0, 1.15f, 0));
				prism(&dm[D_OPAQUE], sub(p, v3(0, 0.55f, 0)), add(p, v3(0, 0.55f, 0)), v3(1, 0, 0), v3(0, 0, 1), 0.9f, 0.7f, 0.8f, 0.62f, cabin[i & 3], true);
			}
			for (int side = -1; side <= 1; side += 2) for (int leg = -1; leg <= 1; leg += 2)
				beam(v3(c.x + side * 2.2f, 0, c.z + leg * 8.0f), v3(c.x + side * 0.9f, c.y, c.z), 0.3f, steel);
			prism(&dm[D_OPAQUE], sub(c, v3(2.4f, 0, 0)), add(c, v3(2.4f, 0, 0)), v3(0, 1, 0), v3(0, 0, 1), 0.9f, 0.9f, 0.9f, 0.9f, v3(0.4f, 0.4f, 0.45f), true);
		}
	}
	// freeway traffic: 20 cars on six lanes, drawn only when they are near and ahead
	{
		const float L = FWY_Z1 - FWY_Z0;
		static const V3 paint[6] = { { 1.2f, 0.2f, 0.18f }, { 0.25f, 0.4f, 0.9f }, { 1.4f, 1.4f, 1.35f }, { 0.2f, 0.2f, 0.24f }, { 1.4f, 1.1f, 0.2f }, { 0.4f, 1.0f, 0.5f } };
		int shown = 0;
		for (int i = 0; i < 20 && shown < 9; i++) {
			int lane = i % 6, dir = lane < 3 ? 1 : -1;
			float x = FWY_X0 + (lane < 3 ? 2.2f + lane * 4.0f : 17.0f + (lane - 3) * 4.0f), speed = 22.0f + (i % 4) * 3.0f;
			float z = FWY_Z0 + fmodf(i * 173.0f + dir * speed * t + L * 20.0f, L);
			float dx = x - s->eye[0], dz = z - s->eye[2];
			if (dx * dx + dz * dz > 330.0f * 330.0f || dx * s->fwd[0] + dz * s->fwd[2] < -20.0f) continue;
			shown++;
			V3 c0 = paint[(i * 5) % 6], dark = v3(0.15f, 0.2f, 0.28f);
			float y = FWY_Y + 0.15f;
			prism(&dm[D_OPAQUE], v3(x, y, z), v3(x, y + 0.75f, z), v3(1, 0, 0), v3(0, 0, 1), 0.88f, 2.1f, 0.84f, 2.0f, c0, true);
			prism(&dm[D_OPAQUE], v3(x, y + 0.75f, z - dir * 0.2f), v3(x, y + 1.3f, z - dir * 0.2f), v3(1, 0, 0), v3(0, 0, 1), 0.74f, 1.15f, 0.62f, 0.85f, dark, false);
			float zr = z - dir * 2.12f;                                                             // the rear: red lights
			prism(&dm[D_OPAQUE], v3(x - 0.55f, y + 0.45f, zr), v3(x - 0.55f, y + 0.6f, zr), v3(1, 0, 0), v3(0, 0, 1), 0.2f, 0.03f, 0.2f, 0.03f, v3(2.2f, 0.2f, 0.2f), false);
			prism(&dm[D_OPAQUE], v3(x + 0.55f, y + 0.45f, zr), v3(x + 0.55f, y + 0.6f, zr), v3(1, 0, 0), v3(0, 0, 1), 0.2f, 0.03f, 0.2f, 0.03f, v3(2.2f, 0.2f, 0.2f), false);
		}
	}
}

// ---- combat effects: muzzle flashes, bullets, blood, smoke, fire, blasts, pickups, the beacons over the hospital / police station / Pay 'n' Spray -----------------------
static void billboard(int grp, V3 c, float size, const float col[3], float alpha)
{
	V3 r = mul(CAMR, size), u = mul(CAMU, size);
	V3 p0 = sub(sub(c, r), u), p1 = sub(add(c, r), u), p2 = add(add(c, r), u), p3 = add(sub(c, r), u);
	SV A = sv(p0.x, p0.y, p0.z, 0, 0, col, alpha), B = sv(p1.x, p1.y, p1.z, 1, 0, col, alpha), C = sv(p2.x, p2.y, p2.z, 1, 1, col, alpha), D = sv(p3.x, p3.y, p3.z, 0, 1, col, alpha);
	quad(&dm[grp], &A, &B, &C, &D);
}
static void ground_quad(int grp, float x, float z, float half, float y, const float col[3], float alpha)
{
	SV A = sv(x - half, y, z - half, 0, 0, col, alpha), B = sv(x + half, y, z - half, 1, 0, col, alpha), C = sv(x + half, y, z + half, 1, 1, col, alpha), D = sv(x - half, y, z + half, 0, 1, col, alpha);
	quad(&dm[grp], &A, &B, &C, &D);
}
static void muzzle_flash(V3 at)
{
	static const float c[3] = { 1.0f, 0.82f, 0.4f };
	billboard(D_ADD, at, 0.42f, c, 1.0f);
}

static void draw_pickup(const Game *g, const Pickup *k)
{
	float t = g->time + k->x * 0.13f, bob = 0.95f + 0.12f * sinf(t * 3.0f), spin = t * 2.0f;
	V3 s = v3(cosf(spin), 0, sinf(spin)), f = v3(-sinf(spin), 0, cosf(spin)), up = v3(0, 1, 0), at = v3(k->x, bob, k->z);
	Mesh *m = &dm[D_OPAQUE];
	static const float glow[PK_COUNT][3] = { { 0.3f, 1.0f, 0.4f }, { 1.0f, 0.3f, 0.3f }, { 0.35f, 0.5f, 1.0f }, { 1.0f, 0.85f, 0.3f }, { 1.0f, 0.85f, 0.3f }, { 1.0f, 0.85f, 0.3f } };
	ground_quad(D_ADD, k->x, k->z, 0.9f, 0.05f, glow[k->type], 0.5f);
	(void)up;
	switch (k->type) {
	case PK_CASH:
		prism(m, add(at, v3(0, -0.04f, 0)), add(at, v3(0, 0.04f, 0)), s, f, 0.34f, 0.17f, 0.34f, 0.17f, v3(0.35f, 1.35f, 0.5f), true);
		prism(m, add(at, v3(0, 0.04f, 0)), add(at, v3(0, 0.05f, 0)), s, f, 0.1f, 0.06f, 0.1f, 0.06f, v3(1.6f, 1.6f, 1.5f), false);
		break;
	case PK_HEALTH:
		prism(m, add(at, v3(0, -0.22f, 0)), add(at, v3(0, 0.22f, 0)), s, f, 0.28f, 0.28f, 0.28f, 0.28f, v3(1.5f, 1.5f, 1.5f), true);
		prism(m, add(at, v3(0, 0.22f, 0)), add(at, v3(0, 0.25f, 0)), s, f, 0.2f, 0.06f, 0.2f, 0.06f, v3(2.0f, 0.2f, 0.2f), false);
		prism(m, add(at, v3(0, 0.22f, 0)), add(at, v3(0, 0.25f, 0)), s, f, 0.06f, 0.2f, 0.06f, 0.2f, v3(2.0f, 0.2f, 0.2f), false);
		break;
	case PK_ARMOR:
		prism(m, add(at, v3(0, -0.28f, 0)), add(at, v3(0, 0.3f, 0)), s, f, 0.3f, 0.12f, 0.32f, 0.12f, v3(0.4f, 0.65f, 1.7f), true);
		break;
	default: {
		prism(m, add(at, v3(0, -0.2f, 0)), add(at, v3(0, 0.12f, 0)), s, f, 0.3f, 0.3f, 0.3f, 0.3f, v3(1.5f, 0.95f, 0.3f), true);
		float len = k->type == PK_PISTOL ? 0.28f : k->type == PK_SMG ? 0.4f : 0.6f;
		prism(m, add(at, v3(0, 0.12f, 0)), add(at, v3(0, 0.22f, 0)), s, f, len * 0.5f, 0.06f, len * 0.5f, 0.06f, v3(0.45f, 0.45f, 0.5f), false);
	} }
}

// wind streaks flying past a fast car: lines fixed in the world (so they sweep by), along the direction of travel
static void draw_speed_streaks(const Game *g, const Scene *s)
{
	if (g->p.car < 0) return;
	const Car *c = &g->car[g->p.car];
	float spd = sqrtf(c->vx * c->vx + c->vz * c->vz);
	if (spd < 28.0f) return;
	float k = clampf((spd - 28.0f) / 24.0f, 0, 1);
	V3 d = norm(v3(c->vx, 0, c->vz)), side = v3(-d.z, 0, d.x), eye = v3(s->eye[0], s->eye[1], s->eye[2]);
	float along0 = c->x * d.x + c->z * d.z;
	const float spacing = 7.0f;
	long cell0 = (long)floorf(along0 / spacing);
	static const float white[3] = { 1.0f, 1.0f, 1.0f };
	for (int i = -7; i <= 9; i++) {
		long cell = cell0 + i;
		unsigned h = hash2((int)cell, 31 + (int)(d.x * 3) * 7);
		float lat = (hf(h) - 0.5f) * 2.0f * 11.0f;
		if (fabsf(lat) < 2.4f) lat += lat < 0 ? -3.0f : 3.0f;
		float height = 0.25f + hf(h >> 8) * 3.6f, len = 2.0f + spd * (0.12f + 0.1f * hf(h >> 5)), along = cell * spacing + hf(h >> 12) * spacing;
		V3 mid = add(v3(c->x, height, c->z), add(mul(d, along - along0), mul(side, lat)));
		V3 a0 = sub(mid, mul(d, len * 0.5f)), a1 = add(mid, mul(d, len * 0.5f));
		V3 perp = norm(cross(d, sub(eye, mid)));
		V3 w = mul(perp, 0.035f + 0.03f * k);
		float al = (0.18f + 0.35f * k) * (hf(h >> 3) * 0.5f + 0.5f);
		SV A = sv(a0.x - w.x, a0.y - w.y, a0.z - w.z, 0, 0.5f, white, 0.0f), B = sv(a1.x - w.x, a1.y - w.y, a1.z - w.z, 1, 0.5f, white, 0.0f);
		SV C = sv(a1.x + w.x, a1.y + w.y, a1.z + w.z, 1, 0.5f, white, al), D = sv(a0.x + w.x, a0.y + w.y, a0.z + w.z, 0, 0.5f, white, al);
		(void)A; (void)B;
		SV m0 = sv(a0.x, a0.y, a0.z, 0, 0.5f, white, 0.0f), m1 = sv(a1.x, a1.y, a1.z, 1, 0.5f, white, 0.0f);
		(void)m0; (void)m1;
		quad(&dm[D_ADD], &A, &B, &C, &D);
	}
}

static void draw_effects(const Game *g, const Scene *s)
{
	const float *eye = s->eye;
	// blood on the ground
	for (int i = 0; i < MAX_DECAL; i++) {
		const Decal *d = &g->decal[i];
		if (!d->on || hypotf(d->x - eye[0], d->z - eye[2]) > 90.0f) continue;
		float a = d->age > 18.0f ? 0.85f * (1 - (d->age - 18.0f) / 10.0f) : 0.85f;
		static const float red[3] = { 0.38f, 0.02f, 0.02f };
		ground_quad(D_ALPHA, d->x, d->z, d->size, 0.06f, red, a);
	}
	// pickups
	for (int i = 0; i < MAX_PICKUP; i++) { const Pickup *k = &g->pick[i]; if (k->on && hypotf(k->x - eye[0], k->z - eye[2]) < 85.0f) draw_pickup(g, k); }
	// particles
	for (int i = 0; i < MAX_PART; i++) {
		const Particle *p = &g->part[i];
		if (!p->on) continue;
		float dx = p->p[0] - eye[0], dz = p->p[2] - eye[2];
		if (dx * dx + dz * dz > 110.0f * 110.0f) continue;
		float k = p->life / p->maxLife;
		V3 c = v3(p->p[0], p->p[1], p->p[2]);
		switch (p->kind) {
		case PT_BLOOD: { static const float col[3] = { 0.55f, 0.03f, 0.03f }; billboard(D_ALPHA, c, p->size, col, 0.9f * fminf(1.0f, k * 2)); break; }
		case PT_SMOKE: { static const float col[3] = { 0.16f, 0.16f, 0.18f }; billboard(D_ALPHA, c, p->size * (1.0f + (1 - k) * 2.2f), col, 0.55f * k); break; }
		case PT_FIRE: { static const float col[3] = { 1.0f, 0.5f, 0.12f }; billboard(D_ADD, c, p->size * (0.6f + 0.6f * k), col, k); break; }
		default: { static const float col[3] = { 1.0f, 0.85f, 0.4f }; billboard(D_ADD, c, p->size, col, k); break; }
		}
	}
	// bullets: short bright streaks
	for (int i = 0; i < MAX_BULLET; i++) {
		const Bullet *b = &g->bullet[i];
		if (!b->on) continue;
		V3 e = v3(b->p[0], b->p[1], b->p[2]), v = v3(b->v[0], b->v[1], b->v[2]), tail = sub(e, mul(v, 0.016f));
		V3 d = norm(v), sx = norm(cross(d, v3(0, 1, 0)));
		if (vlen(sx) < 0.1f) sx = v3(1, 0, 0);
		V3 sy = norm(cross(d, sx));
		prism(&dm[D_ADD], tail, e, sx, sy, 0.025f, 0.025f, 0.025f, 0.025f, b->owner == 0 ? v3(1.0f, 0.85f, 0.4f) : v3(1.0f, 0.45f, 0.3f), true);
	}
	// blasts: a fireball and a flash
	for (int i = 0; i < MAX_BLAST; i++) {
		const Blast *b = &g->blast[i];
		if (!b->on) continue;
		float k = b->t / 1.5f, r = 2.0f + 9.5f * sqrtf(k), a = (1 - k) * (1 - k);
		static const float orange[3] = { 1.0f, 0.5f, 0.15f }, white[3] = { 1.0f, 0.95f, 0.75f };
		billboard(D_ADD, v3(b->x, b->y + 1.5f * k * 3.0f, b->z), r, orange, a);
		if (b->t < 0.5f) billboard(D_ADD, v3(b->x, b->y + 0.8f, b->z), r * 0.55f, white, 1 - b->t / 0.5f);
		ground_quad(D_ALPHA, b->x, b->z, 3.0f + 5.0f * k, 0.07f, (const float[3]){ 0.05f, 0.04f, 0.04f }, 0.5f * (1 - k * 0.5f));
	}
	// beacons: a pale column of light over the hospital, the police station and the spray shop
	static const struct { float x, z; float c[3]; } BEACON[3] = { { HOSPITAL_X, HOSPITAL_Z, { 1.0f, 0.3f, 0.3f } }, { POLICE_X, POLICE_Z, { 0.3f, 0.5f, 1.0f } }, { SPRAY_X, SPRAY_Z, { 0.85f, 0.3f, 1.0f } } };
	for (int i = 0; i < 3; i++) {
		float dx = BEACON[i].x - eye[0], dz = BEACON[i].z - eye[2], d = sqrtf(dx * dx + dz * dz);
		if (d > 260.0f || dx * s->fwd[0] + dz * s->fwd[2] < -15.0f) continue;
		float pulse = 0.35f + 0.15f * sinf(g->time * 3.0f + i);
		ground_quad(D_ADD, BEACON[i].x, BEACON[i].z, i == 2 ? SPRAY_R : 4.5f, 0.08f, BEACON[i].c, pulse + 0.2f);
		for (int k = 0; k < 2; k++) {                                                               // two crossed vertical quads
			V3 side = k ? v3(1, 0, 0) : v3(0, 0, 1);
			V3 a0 = sub(v3(BEACON[i].x, 0, BEACON[i].z), mul(side, 2.2f)), a1 = add(v3(BEACON[i].x, 0, BEACON[i].z), mul(side, 2.2f));
			SV A = sv(a0.x, 0, a0.z, 0, 0, BEACON[i].c, pulse), B = sv(a1.x, 0, a1.z, 1, 0, BEACON[i].c, pulse), C = sv(a1.x, 45.0f, a1.z, 1, 1, BEACON[i].c, 0.0f), D = sv(a0.x, 45.0f, a0.z, 0, 1, BEACON[i].c, 0.0f);
			quad(&dm[D_ADD], &A, &B, &C, &D);
		}
	}
}

// a gold arrow over the car Otto can get into
static void draw_marker(const Game *g)
{
	if (g->p.car >= 0) return;
	int i = game_nearest_car(g, g->p.p[0], g->p.p[2], 12.0f);
	if (i < 0) return;
	const Car *c = &g->car[i];
	float bob = 0.25f * sinf(g->time * 5.0f);
	V3 U = v3(0, 1, 0), tip = v3(c->x, 2.7f + bob, c->z), top = v3(c->x, 3.6f + bob, c->z);
	float spin = g->time * 2.0f;
	V3 s = v3(cosf(spin), 0, sinf(spin)), f = v3(-sinf(spin), 0, cosf(spin));
	(void)U;
	prism(&dm[D_OPAQUE], tip, top, s, f, 0.0f, 0.0f, 0.42f, 0.42f, v3(1.7f, 1.35f, 0.25f), false);
}

void scene_build(const Game *g, Scene *s)
{
	const Cam *cam = &g->cam;
	float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
	float f[3] = { sy * cp, sp, -cy * cp };
	float R[3] = { cy, 0, sy };
	float U[3] = { R[1] * f[2] - R[2] * f[1], R[2] * f[0] - R[0] * f[2], R[0] * f[1] - R[1] * f[0] };
	CAMR = v3(R[0], R[1], R[2]); CAMU = v3(U[0], U[1], U[2]);
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
		float cx = 0, cz = 0, cy, rv, rh = 0;
		if (c < NCH) chunk_info(c, &cx, &cz, &cy, &rv, &rh);
		s->near[c] = c < NCH && s->vis[c] && sqrtf((cx - s->eye[0]) * (cx - s->eye[0]) + (cz - s->eye[2]) * (cz - s->eye[2])) - rh < DETAIL_RANGE;
	}

	static const int caps[D_COUNT] = { CAP_D_OPAQUE, CAP_D_ALPHA, CAP_D_ADD };
	int off = 0;
	for (int i = 0; i < D_COUNT; i++) { dm[i].v = s->dyn + off; dm[i].n = 0; dm[i].cap = caps[i]; s->first[i] = off; off += caps[i]; }

	draw_otto(g);
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *c = &g->car[i];
		if (!c->on) continue;
		float dx = c->x - cam->eye[0], dz = c->z - cam->eye[2];
		if (dx * dx + dz * dz > (g->lowq ? 130.0f : 170.0f) * (g->lowq ? 130.0f : 170.0f)) continue;
		if (dx * s->fwd[0] + dz * s->fwd[2] < -6.0f) continue;                       // behind the camera
		draw_car(g, i, dx * dx + dz * dz < (g->lowq ? 45.0f * 45.0f : 75.0f * 75.0f));
	}
	draw_peds(g, s);
	draw_signals(g, s);
	draw_scenery_dynamic(g, s);
	draw_marker(g);
	draw_effects(g, s);
	draw_speed_streaks(g, s);
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
