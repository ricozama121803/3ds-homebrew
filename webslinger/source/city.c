// Web Slinger - the city: procedural Manhattan-style blocks, plus ray casts and player collision against the building boxes.
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

static void add_building(World *w, unsigned *s, float x0, float z0, float x1, float z1, float center)
{
	float m = fminf(x1 - x0, z1 - z0);
	float h = 18.0f + RF() * 36.0f + center * (50.0f + RF() * 150.0f);
	if (h > 30.0f + m * 7.0f) h = 30.0f + m * 7.0f;
	h = floorf(h / FLOOR_H) * FLOOR_H;
	if (h < 3 * FLOOR_H) h = 3 * FLOOR_H;
	float r = RF();
	int mat;
	if (h > 100) mat = r < 0.5f ? MAT_GLASS : r < 0.75f ? MAT_CREAM : r < 0.9f ? MAT_TAN : MAT_RED;
	else if (h > 50) mat = r < 0.2f ? MAT_GLASS : r < 0.45f ? MAT_CREAM : r < 0.7f ? MAT_TAN : MAT_RED;
	else mat = r < 0.1f ? MAT_GLASS : r < 0.3f ? MAT_CREAM : r < 0.6f ? MAT_TAN : MAT_RED;
	float tint[3], tb = 0.9f + 0.17f * RF();
	for (int k = 0; k < 3; k++) tint[k] = tb + (RF() - 0.5f) * 0.08f;

	int n = 1;
	if (h > 170 && m > 26) n = 4; else if (h > 110 && m > 22) n = 3; else if (h > 60 && m > 16) n = 2; else if (h > 34 && m > 14 && RF() < 0.3f) n = 2;
	static const float frac[5][4] = { { 0 }, { 1 }, { 0.62f, 1 }, { 0.5f, 0.78f, 1 }, { 0.42f, 0.68f, 0.86f, 1 } };
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
	if (topy > 60 && q < 0.3f) top->roof = RF_PYRAMID;
	else if (topy > 120 && q < 0.6f) top->roof = RF_SPIRE;
	else if (center > 0.3f && q > 0.93f && tw > 12 && td > 12) {
		float rr = 0.38f * fminf(tw, td), cx = 0.5f * (top->x0 + top->x1), cz = 0.5f * (top->z0 + top->z1);
		Box *b = add_box(w, cx - rr * 0.8f, topy, cz - rr * 0.8f, cx + rr * 0.8f, topy + rr * 0.55f, cz + rr * 0.8f, MAT_COPPER, BK_DOME);
		if (b) b->tint[0] = b->tint[1] = b->tint[2] = rr;               // the dome's radius travels in the tint (see scene.c)
	} else if (q > 0.62f && tw > 10 && td > 10) {
		float cx = top->x0 + 3 + RF() * (tw - 6), cz = top->z0 + 3 + RF() * (td - 6);
		add_box(w, cx - 1.8f, topy, cz - 1.8f, cx + 1.8f, topy + 7.5f, cz + 1.8f, MAT_ROOF, BK_TOWER);
	}
	w->nbld++;
}

static void add_landmark(World *w, float cx, float cz)
{
	static const float half[4] = { 19.2f, 13.5f, 8.5f, 4.2f }, top[4] = { 98.8f, 167.2f, 220.4f, 258.4f };
	float yb = 0;
	for (int k = 0; k < 4; k++) {
		Box *b = add_box(w, cx - half[k], 0, cz - half[k], cx + half[k], top[k], cz + half[k], MAT_CREAM, BK_BLDG);
		if (!b) return;
		b->yb = yb; yb = top[k];
		b->tint[0] = 1.0f; b->tint[1] = 0.97f; b->tint[2] = 0.9f;
		if (k == 3) b->roof = RF_SPIRE;
	}
	w->nbld++;
}

void city_generate(World *w, unsigned seed)
{
	unsigned st = seed, *s = &st;
	memset(w, 0, sizeof *w);
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) w->park[j][i] = (i == MID + 3 && j >= MID - 2 && j <= MID) || (i == MID - 4 && j >= MID + 2 && j <= MID + 4);
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		w->bfirst[j][i] = w->nbox;
		if (w->park[j][i]) continue;
		float x0 = (i + BLOCK_LO) * PITCH, x1 = (i + BLOCK_HI) * PITCH, z0 = (j + BLOCK_LO) * PITCH, z1 = (j + BLOCK_HI) * PITCH;
		if (i == MID && j == MID) { add_landmark(w, 0.5f * (x0 + x1), 0.5f * (z0 + z1)); w->bcount[j][i] = w->nbox - w->bfirst[j][i]; continue; }
		float dx = (float)(i - MID), dz = (float)(j - MID), center = 1.0f - sqrtf(dx * dx + dz * dz) / (0.5f * NB);
		{ float ex = (float)i - 0.25f * NB, ez = (float)j - 0.75f * NB, c2 = 0.75f * (1.0f - sqrtf(ex * ex + ez * ez) / (0.2f * NB)); if (c2 > center) center = c2; }      // a second, smaller downtown
		if (center < 0) center = 0;
		center *= 0.7f + RF() * 0.5f;
		float cols[2][2]; int nc = 1;
		cols[0][0] = x0; cols[0][1] = x1;
		if (RF() < 0.7f) {
			float a = x0 + (x1 - x0) * (0.35f + 0.3f * RF());
			cols[0][1] = a - 0.7f; cols[1][0] = a + 0.7f; cols[1][1] = x1; nc = 2;
		}
		for (int c = 0; c < nc; c++) {
			if (RF() < 0.6f) {
				float a = z0 + (z1 - z0) * (0.35f + 0.3f * RF());
				add_building(w, s, cols[c][0], z0, cols[c][1], a - 0.7f, center);
				add_building(w, s, cols[c][0], a + 0.7f, cols[c][1], z1, center);
			} else add_building(w, s, cols[c][0], z0, cols[c][1], z1, center);
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
static void near_range(float x, float z, int *i0, int *i1, int *j0, int *j1)
{
	*i0 = (int)floorf((x - 1.5f) / PITCH); *i1 = (int)floorf((x + 1.5f) / PITCH);
	*j0 = (int)floorf((z - 1.5f) / PITCH); *j1 = (int)floorf((z + 1.5f) / PITCH);
	*i0 = *i0 < 0 ? 0 : *i0; *j0 = *j0 < 0 ? 0 : *j0; *i1 = *i1 > NB - 1 ? NB - 1 : *i1; *j1 = *j1 > NB - 1 ? NB - 1 : *j1;
}
#define FOR_NEAR(w, x, z, b) \
	for (int i0_, i1_, j0_, j1_, jj_ = (near_range(x, z, &i0_, &i1_, &j0_, &j1_), j0_); jj_ <= j1_; jj_++) \
	for (int ii_ = i0_; ii_ <= i1_; ii_++) \
	for (const Box *b = &(w)->box[(w)->bfirst[jj_][ii_]], *e_ = b + (w)->bcount[jj_][ii_]; b < e_; b++)

bool city_overlap(const World *w, const float p[3])
{
	FOR_NEAR(w, p[0], p[2], b) if (body_hits(b, p[0], p[1], p[2])) return true;
	return false;
}

float city_ground_below(const World *w, const float p[3])
{
	float h = (p[0] >= 0 && p[0] <= CITY && p[2] >= 0 && p[2] <= CITY) ? 0.0f : WATER_Y;
	FOR_NEAR(w, p[0], p[2], b)
		if (p[0] + BODY_HW > b->x0 && p[0] - BODY_HW < b->x1 && p[2] + BODY_HW > b->z0 && p[2] - BODY_HW < b->z1 && b->y1 <= p[1] + 0.6f && b->y1 > h) h = b->y1;
	return h;
}

void city_move(const World *w, float p[3], const float d[3], Contact *c)
{
	Contact k; memset(&k, 0, sizeof k);
	p[0] += d[0];
	if (d[0] != 0) {
		FOR_NEAR(w, p[0], p[2], b) if (body_hits(b, p[0], p[1], p[2])) { p[0] = d[0] > 0 ? b->x0 - BODY_HW - 0.001f : b->x1 + BODY_HW + 0.001f; k.hitx = true; k.nx = d[0] > 0 ? -1.0f : 1.0f; k.nz = 0; }
	}
	p[2] += d[2];
	if (d[2] != 0) {
		FOR_NEAR(w, p[0], p[2], b) if (body_hits(b, p[0], p[1], p[2])) { p[2] = d[2] > 0 ? b->z0 - BODY_HW - 0.001f : b->z1 + BODY_HW + 0.001f; k.hitz = true; k.nz = d[2] > 0 ? -1.0f : 1.0f; k.nx = 0; }
	}
	p[1] += d[1];
	if (d[1] != 0) {
		FOR_NEAR(w, p[0], p[2], b) if (body_hits(b, p[0], p[1], p[2])) {
			if (d[1] < 0) { p[1] = b->y1; k.landed = true; } else { p[1] = b->y0 - BODY_H - 0.001f; k.head = true; }
		}
	}
	if (p[1] < 0 && p[0] >= 0 && p[0] <= CITY && p[2] >= 0 && p[2] <= CITY && d[1] <= 0) { p[1] = 0; k.landed = true; }
	if (c) *c = k;
}
