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

// Los Angeles City Hall: a wide white base, a stepped tower and a pyramid roof (half widths along x / z and tops of its tiers)
static void add_city_hall(World *w, float cx, float cz)
{
	static const float hx[5] = { 17.5f, 14.0f, 8.5f, 6.2f, 3.8f }, hz[5] = { 15.0f, 11.5f, 8.5f, 6.2f, 3.8f }, top[5] = { 17.0f, 34.0f, 98.0f, 119.0f, 136.0f };
	float yb = 0;
	for (int k = 0; k < 5; k++) {
		Box *b = add_box(w, cx - hx[k], 0, cz - hz[k], cx + hx[k], top[k], cz + hz[k], MAT_CREAM, BK_BLDG);
		if (!b) return;
		b->yb = yb; yb = top[k];
		b->tint[0] = 1.12f; b->tint[1] = 1.1f; b->tint[2] = 1.04f;
		if (k == 4) b->roof = RF_PYRAMID;
	}
	w->nbld++;
}

// the city: low-rise LA blocks, a downtown cluster in the north-east, two parks, three landmarks
static void gen_city(World *w, unsigned *s)
{
	w->downtown0 = NB - 3; w->downtown1 = 3;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) w->park[j][i] = (i == 2 && j == 3) || (i == 5 && j == 6);
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		w->bfirst[j][i] = w->nbox;
		if (w->park[j][i]) continue;
		float x0 = (i + BLOCK_LO) * PITCH, x1 = (i + BLOCK_HI) * PITCH, z0 = (j + BLOCK_LO) * PITCH, z1 = (j + BLOCK_HI) * PITCH;
		if (i == 6 && j == 1) { static const float hf[4] = { 17.0f, 12.5f, 8.5f, 4.5f }, tp[4] = { 78.0f, 128.0f, 170.0f, 204.0f }; add_landmark(w, 0.5f * (x0 + x1), 0.5f * (z0 + z1), hf, tp, 4); w->bcount[j][i] = w->nbox - w->bfirst[j][i]; continue; }
		if (i == 6 && j == 2) { add_city_hall(w, 0.5f * (x0 + x1), 0.5f * (z0 + z1)); w->bcount[j][i] = w->nbox - w->bfirst[j][i]; continue; }
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

static void gen_props(World *w, unsigned *s)
{
	// street furniture along the pavements, a few per block side (between the corners, which are kept clear)
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		w->pfirst[j][i] = (short)w->nprop;
		if (w->park[j][i] || w->field[j][i] || w->lot[j][i]) continue;
		for (int side = 0; side < 4; side++) {
			int n = RF() < 0.3f ? 0 : RF() < 0.7f ? 1 : 2;
			float last = -99.0f;
			for (int k = 0; k < n && w->nprop < MAX_PROP; k++) {
				float u = 20.0f + RF() * (PITCH - 40.0f);
				if (fabsf(u - last) < 9.0f) continue;
				last = u;
				float r = RF(), x, z;
				int type = r < 0.45f ? 0 : r < 0.78f ? 1 : 2;
				float off = type == 2 ? 11.5f : 11.6f;                                  // against the kerb: the pavement between them and the buildings stays wide enough to walk
				bool alongZ = side < 2;                                                 // 0 west, 1 east pavement run along z; 2 north, 3 south along x
				if (side == 0) { x = i * PITCH + off; z = j * PITCH + u; }
				else if (side == 1) { x = (i + 1) * PITCH - off; z = j * PITCH + u; }
				else if (side == 2) { x = i * PITCH + u; z = j * PITCH + off; }
				else { x = i * PITCH + u; z = (j + 1) * PITCH - off; }
				Prop *p = &w->prop[w->nprop++];
				float hw = type == 0 ? 0.38f : type == 1 ? 0.27f : 0.5f, hl = type == 2 ? 0.95f : hw;      // half width across the pavement, half length along it
				p->x = x; p->z = z; p->hx = alongZ ? hw : hl; p->hz = alongZ ? hl : hw;
				p->h = type == 0 ? 1.0f : type == 1 ? 1.1f : 1.25f; p->type = (unsigned char)type;
				p->face = (unsigned char)(side == 0 ? 1 : side == 1 ? 0 : side == 2 ? 3 : 2);
			}
		}
		w->pcount[j][i] = (short)(w->nprop - w->pfirst[j][i]);
	}
}



static void add_slab(World *w, float x0, float z0, float x1, float z1, float h, int mat, const float tint[3])
{
	Box *b = add_box(w, x0, 0, z0, x1, h, z1, mat, BK_BLDG);
	if (!b) return;
	memcpy(b->tint, tint, 3 * sizeof(float));
	w->nbld++;
}

// Sunset Hills: a desert suburb. Low houses with red tile roofs on big lots, bare desert blocks, a mission church, a water tower and a radio mast.
static void add_house(World *w, unsigned *s, float x0, float z0, float x1, float z1)
{
	float fw = 10.5f + RF() * 5.0f, fd = 9.5f + RF() * 4.5f;
	fw = fminf(fw, x1 - x0 - 1.0f); fd = fminf(fd, z1 - z0 - 1.0f);
	float cx = 0.5f * (x0 + x1) + (RF() - 0.5f) * (x1 - x0 - fw) * 0.7f, cz = 0.5f * (z0 + z1) + (RF() - 0.5f) * (z1 - z0 - fd) * 0.7f;
	float r = RF(), tb = 0.95f + 0.15f * RF(), tint[3] = { tb * 1.06f, tb, tb * 0.88f };
	int mat = r < 0.5f ? MAT_CREAM : r < 0.85f ? MAT_TAN : MAT_RED;
	float h = (RF() < 0.3f ? 2.0f : 1.0f) * FLOOR_H;
	Box *b = add_box(w, cx - fw / 2, 0, cz - fd / 2, cx + fw / 2, h, cz + fd / 2, mat, BK_BLDG);
	if (!b) return;
	b->roof = RF_TILE; memcpy(b->tint, tint, sizeof tint);
	if (RF() < 0.45f) {                                                    // a garage at the side
		float gw = 5.2f, gx = cx + fw / 2 + 0.2f;
		if (gx + gw < x1 + 1.0f) { Box *g = add_box(w, gx, 0, cz - 3.0f, gx + gw, FLOOR_H * 0.8f, cz + 3.0f, mat, BK_BLDG); if (g) { g->bld = w->nbld; memcpy(g->tint, tint, sizeof tint); } }
	}
	w->nbld++;
}

static void gen_hills(World *w, unsigned *s)
{
	w->downtown0 = NB + 1; w->downtown1 = 0;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) w->field[j][i] = ((i == 6 && j == 2) || (i == 1 && j == 5) || (i == 4 && j == 6)) ? true : (i == 3 && j == 3) ? false : RF() < 0.2f;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		w->bfirst[j][i] = w->nbox;
		float x0 = (i + BLOCK_LO) * PITCH, x1 = (i + BLOCK_HI) * PITCH, z0 = (j + BLOCK_LO) * PITCH, z1 = (j + BLOCK_HI) * PITCH, cx = 0.5f * (x0 + x1), cz = 0.5f * (z0 + z1);
		if (i == 6 && j == 2) { Box *m = add_box(w, cx - 1.6f, 0, cz - 1.6f, cx + 1.6f, 58.0f, cz + 1.6f, MAT_WHITE, BK_MAST); if (m) w->nbld++; }       // the radio mast
		else if (i == 4 && j == 6) { Box *t = add_box(w, cx - 2.0f, 0, cz - 2.0f, cx + 2.0f, 7.5f, cz + 2.0f, MAT_WHITE, BK_TOWER); if (t) w->nbld++; }        // the water tower
		else if (i == 3 && j == 3) {                                       // the mission: a long nave with a bell tower
			static const float cream[3] = { 1.15f, 1.1f, 1.0f };
			Box *n = add_box(w, cx - 7.0f, 0, cz - 13.0f, cx + 7.0f, 3 * FLOOR_H, cz + 13.0f, MAT_CREAM, BK_BLDG);
			if (n) { n->roof = RF_TILE; memcpy(n->tint, cream, sizeof cream); }
			Box *t = add_box(w, cx - 3.0f, 0, cz + 13.0f, cx + 3.0f, 6 * FLOOR_H, cz + 19.0f, MAT_CREAM, BK_BLDG);
			if (t) { t->bld = w->nbld; t->roof = RF_TILE; memcpy(t->tint, cream, sizeof cream); }
			w->nbld++;
		} else if (!w->field[j][i]) {
			float r = RF();
			if (r < 0.15f) add_house(w, s, x0, z0, x1, z1);                                                    // an estate
			else if (r < 0.4f) { add_house(w, s, x0, z0, x1, cz - 1.0f); add_house(w, s, x0, cz + 1.0f, x1, z1); }
			else { float mx = cx, mz = cz; add_house(w, s, x0, z0, mx - 1.0f, mz - 1.0f); add_house(w, s, mx + 1.0f, z0, x1, mz - 1.0f); add_house(w, s, x0, mz + 1.0f, mx - 1.0f, z1); add_house(w, s, mx + 1.0f, mz + 1.0f, x1, z1); }
		}
		w->bcount[j][i] = w->nbox - w->bfirst[j][i];
	}
}

// hotels and offices for the airport: glass and pale concrete with a cool tint, instead of the city's brick and stucco
static void restyle_modern(World *w, unsigned *s, int from)
{
	for (int i = from; i < w->nbox; i++) {
		Box *b = &w->box[i];
		if (b->kind != BK_BLDG) continue;
		b->mat = RF() < 0.55f ? MAT_GLASS : MAT_CREAM;
		b->tint[0] *= 0.9f; b->tint[1] *= 0.97f; b->tint[2] *= 1.08f;
	}
}

// The airport district: hotels and parking garages by the entrance, car rental lots in the middle, cargo warehouses at the back.
static void gen_airport(World *w, unsigned *s)
{
	w->downtown0 = NB + 1; w->downtown1 = 0;
	static const float garage[3] = { 0.86f, 0.86f, 0.9f }, ware[3] = { 0.98f, 0.96f, 0.9f }, ware2[3] = { 0.85f, 0.9f, 1.0f };
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		w->bfirst[j][i] = w->nbox;
		float x0 = (i + BLOCK_LO) * PITCH, x1 = (i + BLOCK_HI) * PITCH, z0 = (j + BLOCK_LO) * PITCH, z1 = (j + BLOCK_HI) * PITCH, r = RF();
		if (j <= 1) {
			if (r < 0.4f) { int from = w->nbox; add_building(w, s, x0, z0, x1, z1, 0.0f); restyle_modern(w, s, from); }
			else if (r < 0.6f) add_slab(w, x0, z0, x1, z1, 4 * FLOOR_H, MAT_CREAM, garage);
			else if (r < 0.85f) w->lot[j][i] = true;
			else w->park[j][i] = true;
		} else if (j <= 4) {
			if (r < 0.45f) w->lot[j][i] = true;
			else if (r < 0.65f) { int from = w->nbox; add_building(w, s, x0, z0, x1, z1, 0.0f); restyle_modern(w, s, from); }
			else if (r < 0.88f) { add_slab(w, x0, z0, x1, 0.5f * (z0 + z1) - 1.5f, 2.0f * FLOOR_H, MAT_TAN, ware); add_slab(w, x0, 0.5f * (z0 + z1) + 1.5f, x1, z1, 2.0f * FLOOR_H, MAT_TAN, ware); }
			else w->park[j][i] = true;
		} else {
			if (r < 0.15f) w->lot[j][i] = true;
			else if (r < 0.6f) { add_slab(w, x0, z0, x1, z1, 2.0f * FLOOR_H, MAT_TAN, ware); }
			else { add_slab(w, x0, z0, 0.5f * (x0 + x1) - 1.5f, z1, 2.0f * FLOOR_H, MAT_CREAM, ware2); add_slab(w, 0.5f * (x0 + x1) + 1.5f, z0, x1, z1, 2.0f * FLOOR_H, MAT_CREAM, ware2); }
		}
		w->bcount[j][i] = w->nbox - w->bfirst[j][i];
	}
}

void city_generate(World *w, int region)
{
	unsigned st = REGIONS[region].seed, *s = &st;
	memset(w, 0, sizeof *w);
	w->region = region;
	for (int k = 0; k < 4; k++) w->nbr[k] = -1;
	// where Otto may walk and cars may drive: the town, plus the gate roads (and the beach / pier / airfield)
	#define RECT(arr, n, a, b, c, d) do { arr[n][0] = (a); arr[n][1] = (b); arr[n][2] = (c); arr[n][3] = (d); n++; } while (0)
	#define GATE(a, b, c, d, dr, ds) do { w->gate[w->ngate++] = (Gate){ (a), (b), (c), (d), (dr), (ds) }; RECT(w->rectF, w->nrectF, (a), (b), (c), (d)); RECT(w->rectC, w->nrectC, (a), (b), (c), (d)); } while (0)
	if (region == REG_CITY) {
		RECT(w->rectF, w->nrectF, BEACH_X, 1, CITY - 1, CITY - 1); RECT(w->rectF, w->nrectF, PIER_X, PIER_Z0, BEACH_X, PIER_Z1); RECT(w->rectC, w->nrectC, BEACH_X + 3, 1.5f, CITY - 1.5f, CITY - 1.5f);
		GATE(220, CITY - 1.5f, 300, CITY + 58, 2, REG_AIRPORT); GATE(CITY - 1.5f, 220, CITY + 58, 300, 0, REG_HILLS);
		w->nbr[0] = REG_HILLS; w->nbr[2] = REG_AIRPORT;
		gen_city(w, s);
	} else if (region == REG_AIRPORT) {
		RECT(w->rectF, w->nrectF, 1, 1, CITY - 1, CITY - 1); RECT(w->rectF, w->nrectF, AIR_X0 - 20.0f, CITY - 1.5f, AIR_X1, AIR_Z1); RECT(w->rectC, w->nrectC, 1.5f, 1.5f, CITY - 1.5f, CITY - 1.5f); RECT(w->rectC, w->nrectC, AIR_X0, CITY - 1.5f, AIR_X1, AIR_Z1 - 3.0f);
		GATE(220, -58, 300, 1.5f, 3, REG_CITY);
		w->nbr[3] = REG_CITY;
		gen_airport(w, s);
	} else {
		RECT(w->rectF, w->nrectF, 1, 1, CITY - 1, CITY - 1); RECT(w->rectC, w->nrectC, 1.5f, 1.5f, CITY - 1.5f, CITY - 1.5f);
		GATE(-58, 220, 1.5f, 300, 1, REG_CITY);
		w->nbr[1] = REG_CITY;
		gen_hills(w, s);
	}
	#undef RECT
	#undef GATE
	gen_props(w, s);
}

// ---- ray cast -------------------------------------------------------------------------------------------------------------
static bool ray_aabb(const float lo[3], const float hi[3], const float o[3], const float d[3], float *best, float n[3])
{
	float t0 = 0, t1 = *best; int ax = -1, sg = 0;
	for (int k = 0; k < 3; k++) {
		if (fabsf(d[k]) < 1e-6f) { if (o[k] < lo[k] || o[k] > hi[k]) return false; continue; }
		float ta = (lo[k] - o[k]) / d[k], tb = (hi[k] - o[k]) / d[k]; int sgn = -1;
		if (ta > tb) { float t = ta; ta = tb; tb = t; sgn = 1; }
		if (ta > t0) { t0 = ta; ax = k; sg = sgn; }
		if (tb < t1) t1 = tb;
		if (t0 > t1) return false;
	}
	if (ax < 0 || t0 <= 0.001f || t0 >= *best) return false;
	*best = t0;
	if (n) { n[0] = n[1] = n[2] = 0; n[ax] = (float)sg; }
	return true;
}

float city_ray(const World *w, const float o[3], const float d[3], float tmax, float n[3])
{
	float best = tmax; bool hit = false;
	{                                                                      // only the blocks the ray passes over (every box lies inside its block)
		float te = fminf(tmax, 1500.0f), ex = o[0] + d[0] * te, ez = o[2] + d[2] * te;
		int i0 = (int)floorf(fminf(o[0], ex) / PITCH), i1 = (int)floorf(fmaxf(o[0], ex) / PITCH), j0 = (int)floorf(fminf(o[2], ez) / PITCH), j1 = (int)floorf(fmaxf(o[2], ez) / PITCH);
		i0 = i0 < 0 ? 0 : i0; j0 = j0 < 0 ? 0 : j0; i1 = i1 > NB - 1 ? NB - 1 : i1; j1 = j1 > NB - 1 ? NB - 1 : j1;
		for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++)
			for (int k = w->bfirst[j][i], ke = k + w->bcount[j][i]; k < ke; k++) {
				const Box *b = &w->box[k];
				const float lo[3] = { b->x0, b->y0, b->z0 }, hi[3] = { b->x1, b->y1, b->z1 };
				if (ray_aabb(lo, hi, o, d, &best, n)) hit = true;
			}
	}
	if (w->region == REG_AIRPORT && (o[2] > AIR_Z0 - 120.0f || o[2] + d[2] * tmax > AIR_Z0 - 120.0f))
		for (int i = 0; i < N_AIRBOX; i++) {
			const float lo[3] = { AIRBOX[i].x0, 0, AIRBOX[i].z0 }, hi[3] = { AIRBOX[i].x1, AIRBOX[i].h, AIRBOX[i].z1 };
			if (ray_aabb(lo, hi, o, d, &best, n)) hit = true;
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

// the airport's solid parts (terminal, tower, hangars, parked planes) against a ground circle whose height span is ylo..yhi
static bool air_push(const World *w, float *x, float *z, float r, float ylo, float yhi, float n[2])
{
	bool hit = false;
	if (w->region != REG_AIRPORT || *z < AIR_Z0 - r - 1.0f) return false;
	for (int i = 0; i < N_AIRBOX; i++) {
		const AirBox *b = &AIRBOX[i];
		if (b->h < ylo || yhi < 0) continue;
		float cx = clampf(*x, b->x0, b->x1), cz = clampf(*z, b->z0, b->z1);
		float dx = *x - cx, dz = *z - cz, d2 = dx * dx + dz * dz;
		if (d2 >= r * r) continue;
		float nx, nz, pen;
		if (d2 > 1e-8f) { float d = sqrtf(d2); nx = dx / d; nz = dz / d; pen = r - d; }
		else { float l = *x - b->x0, rr = b->x1 - *x, t = *z - b->z0, bt = b->z1 - *z, m = fminf(fminf(l, rr), fminf(t, bt)); nx = nz = 0; if (m == l) nx = -1; else if (m == rr) nx = 1; else if (m == t) nz = -1; else nz = 1; pen = m + r; }
		*x += nx * pen; *z += nz * pen; n[0] += nx; n[1] += nz; hit = true;
	}
	return hit;
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
	if (air_push(w, x, z, r, 0, 1e9f, n)) hit = true;
	if (hit) { float l = sqrtf(n[0] * n[0] + n[1] * n[1]); if (l > 1e-6f) { n[0] /= l; n[1] /= l; } }
	return hit;
}

bool city_prop_push(const World *w, float *x, float *z, float r, float n[2])
{
	bool hit = false;
	n[0] = n[1] = 0;
	int i0, i1, j0, j1;
	near_range(*x, *z, r + 1.0f, &i0, &i1, &j0, &j1);
	for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) for (int k = w->pfirst[j][i]; k < w->pfirst[j][i] + w->pcount[j][i]; k++) {
		const Prop *p = &w->prop[k];
		float cx = clampf(*x, p->x - p->hx, p->x + p->hx), cz = clampf(*z, p->z - p->hz, p->z + p->hz);
		float dx = *x - cx, dz = *z - cz, d2 = dx * dx + dz * dz;
		if (d2 >= r * r) continue;
		if (d2 < 1e-8f) {                                                          // centre inside: leave through the nearest face
			float l = *x - (p->x - p->hx), rr = (p->x + p->hx) - *x, t = *z - (p->z - p->hz), bt = (p->z + p->hz) - *z, m = fminf(fminf(l, rr), fminf(t, bt));
			float nx = 0, nz = 0; if (m == l) nx = -1; else if (m == rr) nx = 1; else if (m == t) nz = -1; else nz = 1;
			*x += nx * (m + r); *z += nz * (m + r); n[0] += nx; n[1] += nz; hit = true; continue;
		}
		float d = sqrtf(d2);
		*x += dx / d * (r - d); *z += dz / d * (r - d); n[0] += dx / d; n[1] += dz / d; hit = true;
	}
	if (hit) { float l = sqrtf(n[0] * n[0] + n[1] * n[1]); if (l > 1e-6f) { n[0] /= l; n[1] /= l; } }
	return hit;
}

float city_prop_seg(const World *w, const float a[3], const float b[3])
{
	float mx = 0.5f * (a[0] + b[0]), mz = 0.5f * (a[2] + b[2]), half = 0.5f * hypotf(b[0] - a[0], b[2] - a[2]) + 1.0f;
	int i0, i1, j0, j1;
	near_range(mx, mz, half, &i0, &i1, &j0, &j1);
	float best = 2.0f;
	for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) for (int k = w->pfirst[j][i]; k < w->pfirst[j][i] + w->pcount[j][i]; k++) {
		const Prop *p = &w->prop[k];
		const float lo[3] = { p->x - p->hx, 0, p->z - p->hz }, hi[3] = { p->x + p->hx, p->h, p->z + p->hz };
		float t0 = 0, t1 = 1; bool ok = true;
		for (int q = 0; q < 3 && ok; q++) {
			float d = b[q] - a[q];
			if (fabsf(d) < 1e-6f) { if (a[q] < lo[q] || a[q] > hi[q]) ok = false; continue; }
			float ta = (lo[q] - a[q]) / d, tb = (hi[q] - a[q]) / d;
			if (ta > tb) { float t = ta; ta = tb; tb = t; }
			if (ta > t0) t0 = ta;
			if (tb < t1) t1 = tb;
			if (t0 > t1) ok = false;
		}
		if (ok && t0 < best) best = t0;
	}
	return best <= 1.0f ? best : -1.0f;
}

// a helicopter's circle against the buildings it is not above: boxes whose top is below ylo (or whose base is above yhi) do not count
bool city_push_circle_y(const World *w, float *x, float *z, float r, float ylo, float yhi, float n[2])
{
	bool hit = false;
	n[0] = n[1] = 0;
	FOR_NEAR(w, *x, *z, r + 0.5f, b) {
		if (b->y1 < ylo || b->y0 > yhi) continue;
		float cx = clampf(*x, b->x0, b->x1), cz = clampf(*z, b->z0, b->z1);
		float dx = *x - cx, dz = *z - cz, d2 = dx * dx + dz * dz;
		if (d2 >= r * r) continue;
		float nx, nz, pen;
		if (d2 > 1e-8f) { float d = sqrtf(d2); nx = dx / d; nz = dz / d; pen = r - d; }
		else {
			float l = *x - b->x0, rr = b->x1 - *x, t = *z - b->z0, bt = b->z1 - *z, m = fminf(fminf(l, rr), fminf(t, bt));
			nx = nz = 0; if (m == l) nx = -1; else if (m == rr) nx = 1; else if (m == t) nz = -1; else nz = 1;
			pen = m + r;
		}
		*x += nx * pen; *z += nz * pen; n[0] += nx; n[1] += nz; hit = true;
	}
	if (air_push(w, x, z, r, ylo, yhi, n)) hit = true;
	if (hit) { float l = sqrtf(n[0] * n[0] + n[1] * n[1]); if (l > 1e-6f) { n[0] /= l; n[1] /= l; } }
	return hit;
}

// the top of the highest building under a circle (0 over the street): a helicopter can set down on a roof
float city_roof_h(const World *w, float x, float z, float r)
{
	float top = 0;
	FOR_NEAR(w, x, z, r + 0.5f, b) {
		if (x + r > b->x0 && x - r < b->x1 && z + r > b->z0 && z - r < b->z1 && b->y1 > top) top = b->y1;
	}
	if (w->region == REG_AIRPORT && z + r > AIR_Z0) for (int i = 0; i < N_AIRBOX; i++) { const AirBox *b = &AIRBOX[i]; if (x + r > b->x0 && x - r < b->x1 && z + r > b->z0 && z - r < b->z1 && b->h > top) top = b->h; }
	return top;
}
