// Grand Theft Otto II - the city: a small LA-style grid (low-rise blocks, a downtown cluster, parks), plus ray casts and collision against the building boxes.
#include "game.h"

unsigned rnd_next(unsigned *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
float rndf(unsigned *s) { return (rnd_next(s) & 0xFFFFFF) / 16777216.0f; }
#define RF() rndf(s)

static Box *add_box(World *w, float x0, float y0, float z0, float x1, float y1, float z1, int mat, int kind)
{
	if (w->nbox >= MAX_BOX) return NULL;
	Box *b = &w->box[w->nbox++];
	memset(b, 0, sizeof *b);
	b->x0 = x0; b->y0 = y0; b->z0 = z0; b->x1 = x1; b->y1 = y1; b->z1 = z1; b->yb = y0;
	b->mat = (unsigned char)mat; b->kind = (unsigned char)kind; b->bld = (short)w->nbld;
	b->tint[0] = b->tint[1] = b->tint[2] = 1;
	return b;
}

static void add_building(World *w, unsigned *s, float x0, float z0, float x1, float z1, float tall)
{
	float m = fminf(x1 - x0, z1 - z0);
	float h = tall > 0.5f ? 40.0f + RF() * 90.0f * tall : 6.5f + RF() * 11.0f;           // downtown towers / low-rise LA
	if (h > 24.0f + m * 5.0f) h = 24.0f + m * 5.0f;
	h = floorf(h / FLOOR_H) * FLOOR_H;
	if (h < 2 * FLOOR_H) h = 2 * FLOOR_H;
	float r = RF();
	int mat;
	if (tall > 0.5f) mat = r < 0.6f ? MAT_GLASS : r < 0.8f ? MAT_CREAM : MAT_TAN;
	else mat = r < 0.05f ? MAT_GLASS : r < 0.35f ? MAT_CREAM : r < 0.7f ? MAT_TAN : MAT_RED;
	float tint[3], tb = 0.92f + 0.15f * RF();
	for (int k = 0; k < 3; k++) tint[k] = tb + (RF() - 0.5f) * 0.08f;

	int n = 1;
	if (h > 90 && m > 22) n = 3; else if (h > 50 && m > 16) n = 2;
	static const float frac[4][3] = { { 0 }, { 1 }, { 0.65f, 1 }, { 0.5f, 0.78f, 1 } };
	float ax = x0, bx = x1, az = z0, bz = z1, ybase = 0, d = 0.1f * m;
	Box *top = NULL;
	for (int k = 0; k < n; k++) {
		if (k > 0) {
			float nax = ax + d * (0.3f + RF()), nbx = bx - d * (0.3f + RF()), naz = az + d * (0.3f + RF()), nbz = bz - d * (0.3f + RF());
			if (nbx - nax < 6.0f || nbz - naz < 6.0f) break;
			ax = nax; bx = nbx; az = naz; bz = nbz;
		}
		float y1 = floorf(h * frac[n][k] / FLOOR_H + 0.5f) * FLOOR_H;
		if (y1 <= ybase + 1) continue;
		Box *b = add_box(w, ax, 0, az, bx, y1, bz, mat, BK_BLDG);
		if (!b) return;
		b->yb = ybase; memcpy(b->tint, tint, sizeof tint);
		ybase = y1; top = b;
	}
	if (!top) return;
	float tw = top->x1 - top->x0, td = top->z1 - top->z0, topy = top->y1;
	float q = RF();
	if (tall > 0.5f && topy > 80 && q < 0.4f) top->roof = RF_SPIRE;
	else if (tall <= 0.5f && q > 0.6f && tw > 10 && td > 10) {
		float cx = top->x0 + 3 + RF() * (tw - 6), cz = top->z0 + 3 + RF() * (td - 6);
		add_box(w, cx - 1.8f, topy, cz - 1.8f, cx + 1.8f, topy + 7.5f, cz + 1.8f, MAT_ROOF, BK_TOWER);
	}
	w->nbld++;
}

// a stepped glass tower with a spire: half widths and tops of its tiers
static void add_landmark(World *w, float cx, float cz, const float half[4], const float top[4], int tiers)
{
	float yb = 0;
	for (int k = 0; k < tiers; k++) {
		Box *b = add_box(w, cx - half[k], 0, cz - half[k], cx + half[k], top[k], cz + half[k], MAT_GLASS, BK_BLDG);
		if (!b) return;
		b->yb = yb; yb = top[k];
		b->tint[0] = 0.96f; b->tint[1] = 1.0f; b->tint[2] = 1.04f;
		if (k == tiers - 1) b->roof = RF_SPIRE;
	}
	w->nbld++;
}

void city_generate(World *w, unsigned seed)
{
	unsigned st = seed, *s = &st;
	memset(w, 0, sizeof *w);
	w->downtown0 = NB - 3; w->downtown1 = 3;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) w->park[j][i] = (i == 2 && j == 3) || (i == 5 && j == 6);
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		w->bfirst[j][i] = w->nbox;
		if (w->park[j][i]) continue;
		float x0 = (i + BLOCK_LO) * PITCH, x1 = (i + BLOCK_HI) * PITCH, z0 = (j + BLOCK_LO) * PITCH, z1 = (j + BLOCK_HI) * PITCH;
		if (i == 6 && j == 1) { static const float hf[4] = { 17.0f, 12.5f, 8.5f, 4.5f }, tp[4] = { 78.0f, 128.0f, 170.0f, 204.0f }; add_landmark(w, 0.5f * (x0 + x1), 0.5f * (z0 + z1), hf, tp, 4); w->bcount[j][i] = w->nbox - w->bfirst[j][i]; continue; }
		if (i == 5 && j == 2) { static const float hf[4] = { 15.0f, 11.0f, 7.0f, 0 }, tp[4] = { 70.0f, 112.0f, 146.0f, 0 }; add_landmark(w, 0.5f * (x0 + x1), 0.5f * (z0 + z1), hf, tp, 3); w->bcount[j][i] = w->nbox - w->bfirst[j][i]; continue; }
		float tall = (i >= w->downtown0 && j < w->downtown1) ? 1.0f - 0.25f * ((NB - 1 - i) + j) * 0.5f : 0.0f;
		float cols[2][2]; int nc = 1;
		cols[0][0] = x0; cols[0][1] = x1;
		if (tall < 0.5f && RF() < 0.7f) {
			float a = x0 + (x1 - x0) * (0.35f + 0.3f * RF());
			cols[0][1] = a - 0.7f; cols[1][0] = a + 0.7f; cols[1][1] = x1; nc = 2;
		}
		for (int c = 0; c < nc; c++) {
			if (tall < 0.5f && RF() < 0.6f) {
				float a = z0 + (z1 - z0) * (0.35f + 0.3f * RF());
				add_building(w, s, cols[c][0], z0, cols[c][1], a - 0.7f, tall);
				add_building(w, s, cols[c][0], a + 0.7f, cols[c][1], z1, tall);
			} else add_building(w, s, cols[c][0], z0, cols[c][1], z1, tall);
		}
		w->bcount[j][i] = w->nbox - w->bfirst[j][i];
	}
}

// ---- ray cast -------------------------------------------------------------------------------------------------------------
float city_ray(const World *w, const float o[3], const float d[3], float tmax, float n[3])
{
	float best = tmax; bool hit = false;
	for (int i = 0; i < w->nbox; i++) {
		const Box *b = &w->box[i];
		const float lo[3] = { b->x0, b->y0, b->z0 }, hi[3] = { b->x1, b->y1, b->z1 };
		float t0 = 0, t1 = best; int ax = -1, sg = 0;
		bool ok = true;
		for (int k = 0; k < 3 && ok; k++) {
			if (fabsf(d[k]) < 1e-6f) { if (o[k] < lo[k] || o[k] > hi[k]) ok = false; continue; }
			float ta = (lo[k] - o[k]) / d[k], tb = (hi[k] - o[k]) / d[k]; int sgn = -1;
			if (ta > tb) { float t = ta; ta = tb; tb = t; sgn = 1; }
			if (ta > t0) { t0 = ta; ax = k; sg = sgn; }
			if (tb < t1) t1 = tb;
			if (t0 > t1) ok = false;
		}
		if (!ok || ax < 0 || t0 <= 0.001f || t0 >= best) continue;
		best = t0; hit = true;
		if (n) { n[0] = n[1] = n[2] = 0; n[ax] = (float)sg; }
	}
	return hit ? best : -1.0f;
}

// ---- collision --------------------------------------------------------------------------------------------------------------
static inline bool body_hits(const Box *b, float x, float y, float z)
{
	return x + BODY_HW > b->x0 && x - BODY_HW < b->x1 && z + BODY_HW > b->z0 && z - BODY_HW < b->z1 && y + BODY_H > b->y0 && y < b->y1;
}
static void near_range(float x, float z, float r, int *i0, int *i1, int *j0, int *j1)
{
	*i0 = (int)floorf((x - r) / PITCH); *i1 = (int)floorf((x + r) / PITCH);
	*j0 = (int)floorf((z - r) / PITCH); *j1 = (int)floorf((z + r) / PITCH);
	*i0 = *i0 < 0 ? 0 : *i0; *j0 = *j0 < 0 ? 0 : *j0; *i1 = *i1 > NB - 1 ? NB - 1 : *i1; *j1 = *j1 > NB - 1 ? NB - 1 : *j1;
}
#define FOR_NEAR(w, x, z, r, b) \
	for (int i0_, i1_, j0_, j1_, jj_ = (near_range(x, z, r, &i0_, &i1_, &j0_, &j1_), j0_); jj_ <= j1_; jj_++) \
	for (int ii_ = i0_; ii_ <= i1_; ii_++) \
	for (const Box *b = &(w)->box[(w)->bfirst[jj_][ii_]], *e_ = b + (w)->bcount[jj_][ii_]; b < e_; b++)

void city_move(const World *w, float p[3], const float d[3], Contact *c)
{
	Contact k; memset(&k, 0, sizeof k);
	p[0] += d[0];
	if (d[0] != 0) {
		FOR_NEAR(w, p[0], p[2], 1.5f, b) if (body_hits(b, p[0], p[1], p[2])) { p[0] = d[0] > 0 ? b->x0 - BODY_HW - 0.001f : b->x1 + BODY_HW + 0.001f; k.hitx = true; k.nx = d[0] > 0 ? -1.0f : 1.0f; k.nz = 0; }
	}
	p[2] += d[2];
	if (d[2] != 0) {
		FOR_NEAR(w, p[0], p[2], 1.5f, b) if (body_hits(b, p[0], p[1], p[2])) { p[2] = d[2] > 0 ? b->z0 - BODY_HW - 0.001f : b->z1 + BODY_HW + 0.001f; k.hitz = true; k.nz = d[2] > 0 ? -1.0f : 1.0f; k.nx = 0; }
	}
	p[1] += d[1];
	if (p[1] < 0) { p[1] = 0; k.landed = true; }
	if (c) *c = k;
}

bool city_push_circle(const World *w, float *x, float *z, float r, float n[2])
{
	bool hit = false;
	n[0] = n[1] = 0;
	FOR_NEAR(w, *x, *z, r + 0.5f, b) {
		float cx = clampf(*x, b->x0, b->x1), cz = clampf(*z, b->z0, b->z1);
		float dx = *x - cx, dz = *z - cz, d2 = dx * dx + dz * dz;
		if (d2 >= r * r) continue;
		float nx, nz, pen;
		if (d2 > 1e-8f) { float d = sqrtf(d2); nx = dx / d; nz = dz / d; pen = r - d; }
		else {                                                          // centre inside the box: leave through the nearest face
			float l = *x - b->x0, rr = b->x1 - *x, t = *z - b->z0, bt = b->z1 - *z, m = fminf(fminf(l, rr), fminf(t, bt));
			nx = nz = 0; if (m == l) nx = -1; else if (m == rr) nx = 1; else if (m == t) nz = -1; else nz = 1;
			pen = m + r;
		}
		*x += nx * pen; *z += nz * pen; n[0] += nx; n[1] += nz; hit = true;
	}
	if (hit) { float l = sqrtf(n[0] * n[0] + n[1] * n[1]); if (l > 1e-6f) { n[0] /= l; n[1] /= l; } }
	return hit;
}
