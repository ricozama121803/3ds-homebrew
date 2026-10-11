// Sunset Ops - the map: a 1950s suburban cul-de-sac in the desert ("Nuketown"-style). Two enterable houses facing each other across
// a round-ended street with a bus and a truck, fenced yards, a backdrop house, and desert all round. Everything is axis-aligned boxes.
#include "game.h"
#include <string.h>
#include <stdlib.h>

static World *W;
static int HIGH;                                   // high quality: extra decorative props (all non-solid unless noted)
static const float K = 0.0f;

static Box *add(float x0, float y0, float z0, float x1, float y1, float z1, int side, int top, float r, float g, float b, float tile, bool solid)
{
	if (W->nbox >= MAX_BOX) return &W->box[MAX_BOX - 1];
	Box *bx = &W->box[W->nbox++];
	bx->x0 = x0; bx->y0 = y0; bx->z0 = z0; bx->x1 = x1; bx->y1 = y1; bx->z1 = z1;
	bx->side = (uint8_t)side; bx->top = (uint8_t)top; bx->solid = solid; bx->r = r; bx->g = g; bx->b = b; bx->tile = tile;
	return bx;
}
static Box *shp(Box *b, int shape, float bev) { b->shape = (uint8_t)shape; b->bev = bev; return b; }
// footprint x0..x1, z0..z1, height h
static Box *block(float x0, float z0, float x1, float z1, float h, int side, int top, float r, float g, float b, float tile)
{
	return add(x0, 0, z0, x1, h, z1, side, top, r, g, b, tile, true);
}
static void crate(float x, float z, float s, float h)
{
	shp(add(x - s * 0.5f, 0, z - s * 0.5f, x + s * 0.5f, h, z + s * 0.5f, MAT_WOOD, MAT_WOOD, 1.0f, 0.95f, 0.88f, 1.2f, true), 0, 0);
}
static void barrier(float x, float z, bool alongx)
{
	float lx = alongx ? 1.6f : 0.35f, lz = alongx ? 0.35f : 1.6f;
	shp(add(x - lx, 0, z - lz, x + lx, 0.95f, z + lz, MAT_CONCRETE, MAT_CONCRETE, 0.95f, 0.93f, 0.9f, 2.0f, true), 1, 0.3f);
}
static void hedge(float x0, float z0, float x1, float z1)
{
	shp(add(x0, 0, z0, x1, 1.25f, z1, MAT_GRASS, MAT_GRASS, 0.55f, 0.8f, 0.5f, 1.5f, true), 1, 0.45f);
}
static void fence(float x0, float z0, float x1, float z1)
{
	add(x0, 0, z0, x1, 1.7f, z1, MAT_WOOD, MAT_WOOD, 1.05f, 0.9f, 0.75f, 1.4f, true);
}
// windows: thin bands on a wall (face: 0 = -z side, 1 = +z, 2 = -x, 3 = +x)
static void windows(float x0, float z0, float x1, float z1, int face, float y0, float y1, float len)
{
	const float o = 0.04f;
	if (face < 2) {
		float z = face == 0 ? z0 - o : z1;
		for (float x = x0 + 1.2f; x + len < x1 - 0.8f; x += len + 1.4f) {
			add(x, y0, z, x + len, y1, z + o, MAT_WINDOW, MAT_WINDOW, 1, 1, 1, 1.4f, false);
			if (HIGH) { float zz = face == 0 ? z - 0.1f : z; add(x - 0.08f, y0 - 0.07f, zz, x + len + 0.08f, y0, zz + 0.14f, MAT_CONCRETE, MAT_CONCRETE, 0.92f, 0.9f, 0.88f, 2.0f, false); }
		}
	} else {
		float x = face == 2 ? x0 - o : x1;
		for (float z = z0 + 1.2f; z + len < z1 - 0.8f; z += len + 1.4f) {
			add(x, y0, z, x + o, y1, z + len, MAT_WINDOW, MAT_WINDOW, 1, 1, 1, 1.4f, false);
			if (HIGH) { float xx = face == 2 ? x - 0.1f : x; add(xx, y0 - 0.07f, z - 0.08f, xx + 0.14f, y0, z + len + 0.08f, MAT_CONCRETE, MAT_CONCRETE, 0.92f, 0.9f, 0.88f, 2.0f, false); }
		}
	}
}
static void car(float cx, float cz, bool alongx, float r, float g, float b)
{
	float lx = alongx ? 2.15f : 0.9f, lz = alongx ? 0.9f : 2.15f;
	shp(add(cx - lx, 0.28f, cz - lz, cx + lx, 1.05f, cz + lz, MAT_WHITE, MAT_WHITE, r, g, b, 4.0f, true), 1, 0.3f);
	float cxl = alongx ? 1.05f : 0.78f, czl = alongx ? 0.78f : 1.05f;
	shp(add(cx - cxl, 0.98f, cz - czl, cx + cxl, 1.6f, cz + czl, MAT_WHITE, MAT_WHITE, r * 0.92f, g * 0.92f, b * 0.92f, 4.0f, true), 1, 0.26f);
	if (HIGH) {
		const float sgn[2] = { -1, 1 };
		for (int i = 0; i < 2; i++) {
			if (alongx) {
				shp(add(cx + 0.7f, 1.05f, cz + sgn[i] * 1.0f - 0.06f, cx + 0.9f, 1.18f, cz + sgn[i] * 1.0f + 0.06f, MAT_WHITE, MAT_WHITE, r * 0.8f, g * 0.8f, b * 0.8f, 1.0f, false), 1, 0.04f);
				shp(add(cx + 2.1f, 0.5f, cz + sgn[i] * 0.6f - 0.15f, cx + 2.25f, 0.72f, cz + sgn[i] * 0.6f + 0.15f, MAT_WHITE, MAT_WHITE, 1.0f, 0.95f, 0.75f, 1.0f, false), 5, 0);
				shp(add(cx - 2.25f, 0.55f, cz + sgn[i] * 0.65f - 0.14f, cx - 2.1f, 0.72f, cz + sgn[i] * 0.65f + 0.14f, MAT_WHITE, MAT_WHITE, 0.9f, 0.1f, 0.08f, 1.0f, false), 5, 0);
			} else {
				shp(add(cx + sgn[i] * 1.0f - 0.06f, 1.05f, cz - 0.9f, cx + sgn[i] * 1.0f + 0.06f, 1.18f, cz - 0.7f, MAT_WHITE, MAT_WHITE, r * 0.8f, g * 0.8f, b * 0.8f, 1.0f, false), 1, 0.04f);
				shp(add(cx + sgn[i] * 0.6f - 0.15f, 0.5f, cz + 2.1f, cx + sgn[i] * 0.6f + 0.15f, 0.72f, cz + 2.25f, MAT_WHITE, MAT_WHITE, 1.0f, 0.95f, 0.75f, 1.0f, false), 5, 0);
				shp(add(cx + sgn[i] * 0.65f - 0.14f, 0.55f, cz - 2.25f, cx + sgn[i] * 0.65f + 0.14f, 0.72f, cz - 2.1f, MAT_WHITE, MAT_WHITE, 0.9f, 0.1f, 0.08f, 1.0f, false), 5, 0);
			}
		}
		if (alongx) { shp(add(cx + 2.05f, 0.3f, cz - 0.9f, cx + 2.25f, 0.5f, cz + 0.9f, MAT_WHITE, MAT_WHITE, 0.12f, 0.12f, 0.13f, 1.0f, false), 1, 0.06f); shp(add(cx - 2.25f, 0.3f, cz - 0.9f, cx - 2.05f, 0.5f, cz + 0.9f, MAT_WHITE, MAT_WHITE, 0.12f, 0.12f, 0.13f, 1.0f, false), 1, 0.06f); }
		else { shp(add(cx - 0.9f, 0.3f, cz + 2.05f, cx + 0.9f, 0.5f, cz + 2.25f, MAT_WHITE, MAT_WHITE, 0.12f, 0.12f, 0.13f, 1.0f, false), 1, 0.06f); shp(add(cx - 0.9f, 0.3f, cz - 2.25f, cx + 0.9f, 0.5f, cz - 2.05f, MAT_WHITE, MAT_WHITE, 0.12f, 0.12f, 0.13f, 1.0f, false), 1, 0.06f); }
	}
	// glass band + wheels
	if (alongx) {
		add(cx - 0.95f, 1.1f, cz - 0.8f, cx + 0.95f, 1.52f, cz + 0.8f, MAT_WINDOW, MAT_WINDOW, 0.5f, 0.55f, 0.6f, 1.4f, false);
		for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
			shp(add(cx + sx * 1.35f - 0.33f, 0, cz + sz * 0.84f - 0.13f, cx + sx * 1.35f + 0.33f, 0.66f, cz + sz * 0.84f + 0.13f, MAT_WHITE, MAT_WHITE, 0.08f, 0.08f, 0.09f, 4.0f, false), 4, 0);
	} else {
		add(cx - 0.8f, 1.1f, cz - 0.95f, cx + 0.8f, 1.52f, cz + 0.95f, MAT_WINDOW, MAT_WINDOW, 0.5f, 0.55f, 0.6f, 1.4f, false);
		for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
			shp(add(cx + sx * 0.84f - 0.13f, 0, cz + sz * 1.35f - 0.33f, cx + sx * 0.84f + 0.13f, 0.66f, cz + sz * 1.35f + 0.33f, MAT_WHITE, MAT_WHITE, 0.08f, 0.08f, 0.09f, 4.0f, false), 3, 0);
	}
}
static void bus(float x0, float z0, float x1, float z1);
static void truck(float x0, float z0);

// ---- ground ----------------------------------------------------------------------------------------------------------
const float MAP_RECTS[][4] = { { -40, -16, 40, 9 }, { -12, 9, 14, 13 }, { -7, 13, 9, 18 } };
const int MAP_NRECT = 3;
static bool street(float x, float z, float hw)             // the street: straight, with a round end to the north
{
	float dx = fabsf(x - 1.0f);
	if (z >= -4) return dx <= hw;
	float dz = z + 4;
	return dx * dx + dz * dz <= hw * hw;
}
static bool inrect(float x, float z, float a, float b, float c, float d) { return x >= a && x <= c && z >= b && z <= d; }
int map_ground(float x, float z)
{
	bool in = false;
	for (int i = 0; i < MAP_NRECT; i++) if (inrect(x, z, MAP_RECTS[i][0], MAP_RECTS[i][1], MAP_RECTS[i][2], MAP_RECTS[i][3])) in = true;
	if (!in) return MAT_SAND;
	if (street(x, z, 6.5f)) return MAT_ASPHALT;
	if (street(x, z, 8.0f)) return MAT_CONCRETE;
	if (inrect(x, z, -34, 3, -29, 8)) return MAT_DECK;                              // the wooden patio behind the teal house
	if (inrect(x, z, -13, 1, -8, 6) || inrect(x, z, -9.5f, -5.4f, -7, -3.6f)) return MAT_CONCRETE;        // driveway, path to the front door
	if (inrect(x, z, 8, 0.6f, 11, 2.4f) || inrect(x, z, 23, 0.6f, 26, 2.4f) || inrect(x, z, 11, 6, 23, 9)) return MAT_CONCRETE;
	if (inrect(x, z, -28, 1.6f, -21, 5.4f)) return MAT_CONCRETE;                    // the car pad
	return MAT_GRASS;
}

// ---- buildings -------------------------------------------------------------------------------------------------------
typedef struct { int face; float c, w; int kind; } Opening;       // face 0 = -z wall, 1 = +z, 2 = -x, 3 = +x; centre along the wall; kind 0 door, 1 window, 2 wide opening
static const float OPY0[3] = { 0, 0.95f, 0 }, OPY1[3] = { 2.25f, 2.2f, 2.55f };
#define WALL_T 0.3f
#define WALL_H 2.9f
static struct { int om, im; float o[3], i[3]; } ST;
static void style(int om, float a, float b, float c, int im, float d, float e, float f)
{
	ST.om = om; ST.o[0] = a; ST.o[1] = b; ST.o[2] = c; ST.im = im; ST.i[0] = d; ST.i[1] = e; ST.i[2] = f;
}
// one run of wall (two skins: the outside material and the room-side material)
static void wseg(int face, float x0, float z0, float x1, float z1, float a0, float a1, float y0, float y1)
{
	if (a1 - a0 < 0.02f || y1 - y0 < 0.02f) return;
	const float O = 0.12f;
	float ox0, oz0, ox1, oz1, ix0, iz0, ix1, iz1;
	switch (face) {
	case 0:  ox0 = ix0 = a0; ox1 = ix1 = a1; oz0 = z0; oz1 = z0 + O; iz0 = z0 + O; iz1 = z0 + WALL_T; break;
	case 1:  ox0 = ix0 = a0; ox1 = ix1 = a1; oz0 = z1 - O; oz1 = z1; iz0 = z1 - WALL_T; iz1 = z1 - O; break;
	case 2:  oz0 = iz0 = a0; oz1 = iz1 = a1; ox0 = x0; ox1 = x0 + O; ix0 = x0 + O; ix1 = x0 + WALL_T; break;
	default: oz0 = iz0 = a0; oz1 = iz1 = a1; ox0 = x1 - O; ox1 = x1; ix0 = x1 - WALL_T; ix1 = x1 - O; break;
	}
	add(ox0, y0, oz0, ox1, y1, oz1, ST.om, ST.om, ST.o[0], ST.o[1], ST.o[2], 3.0f, true);
	add(ix0, y0, iz0, ix1, y1, iz1, ST.im, ST.im, ST.i[0], ST.i[1], ST.i[2], 3.0f, true);
}
// the four outer walls of a ground floor with door / window openings; `omit` bit f leaves face f out (to join two shells)
static void shell(float x0, float z0, float x1, float z1, const Opening *op, int n, int omit)
{
	for (int f = 0; f < 4; f++) {
		if (omit & (1 << f)) continue;
		float lo, hi;
		if (f < 2) { lo = x0; hi = x1; } else { lo = z0 + ((omit & 1) ? 0 : WALL_T); hi = z1 - ((omit & 2) ? 0 : WALL_T); }
		int idx[12], m = 0;
		for (int i = 0; i < n; i++) if (op[i].face == f) { int j = m++; while (j > 0 && op[idx[j - 1]].c > op[i].c) { idx[j] = idx[j - 1]; j--; } idx[j] = i; }
		float cur = lo;
		for (int k = 0; k < m; k++) {
			const Opening *o = &op[idx[k]];
			float a = o->c - o->w * 0.5f, b = o->c + o->w * 0.5f, y0 = OPY0[o->kind], y1 = OPY1[o->kind];
			wseg(f, x0, z0, x1, z1, cur, a, 0, WALL_H);
			if (y0 > 0) wseg(f, x0, z0, x1, z1, a, b, 0, y0);
			wseg(f, x0, z0, x1, z1, a, b, y1, WALL_H);
			if (o->kind == 1) {                               // window sill ledge
				float s0 = y0 - 0.07f;
				if (f == 0) add(a - 0.08f, s0, z0 - 0.1f, b + 0.08f, y0, z0, MAT_CONCRETE, MAT_CONCRETE, 0.95f, 0.95f, 0.92f, 2.0f, false);
				if (f == 1) add(a - 0.08f, s0, z1, b + 0.08f, y0, z1 + 0.1f, MAT_CONCRETE, MAT_CONCRETE, 0.95f, 0.95f, 0.92f, 2.0f, false);
				if (f == 2) add(x0 - 0.1f, s0, a - 0.08f, x0, y0, b + 0.08f, MAT_CONCRETE, MAT_CONCRETE, 0.95f, 0.95f, 0.92f, 2.0f, false);
				if (f == 3) add(x1, s0, a - 0.08f, x1 + 0.1f, y0, b + 0.08f, MAT_CONCRETE, MAT_CONCRETE, 0.95f, 0.95f, 0.92f, 2.0f, false);
			}
			cur = b;
		}
		wseg(f, x0, z0, x1, z1, cur, hi, 0, WALL_H);
	}
}
// interior partition with door gaps (centres along the wall, 1.5 m wide)
static void part(bool alongx, float fixed, float a, float b, const float *gaps, int ng, float r, float g, float bl)
{
	float cur = a;
	for (int i = 0; i <= ng; i++) {
		float g0 = i < ng ? gaps[i] - 0.75f : b, g1 = i < ng ? gaps[i] + 0.75f : b;
		if (g0 - cur > 0.02f) {
			if (alongx) add(cur, 0, fixed - 0.1f, g0, WALL_H, fixed + 0.1f, MAT_STUCCO, MAT_STUCCO, r, g, bl, 3.0f, true);
			else add(fixed - 0.1f, 0, cur, fixed + 0.1f, WALL_H, g0, MAT_STUCCO, MAT_STUCCO, r, g, bl, 3.0f, true);
		}
		if (i < ng) {
			if (alongx) add(g0, 2.25f, fixed - 0.1f, g1, WALL_H, fixed + 0.1f, MAT_STUCCO, MAT_STUCCO, r, g, bl, 3.0f, true);
			else add(fixed - 0.1f, 2.25f, g0, fixed + 0.1f, WALL_H, g1, MAT_STUCCO, MAT_STUCCO, r, g, bl, 3.0f, true);
		}
		cur = g1;
	}
}
static void floor_(float x0, float z0, float x1, float z1, int mat, float r, float g, float b)
{
	add(x0, 0, z0, x1, 0.03f, z1, mat, mat, r, g, b, 2.0f, false);
}
static void ceiling(float x0, float z0, float x1, float z1)
{
	add(x0, 2.8f, z0, x1, WALL_H, z1, MAT_STUCCO, MAT_STUCCO, 0.95f, 0.92f, 0.8f, 3.0f, false);
	if (HIGH) {
		float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
		shp(add(cx - 0.4f, 2.7f, cz - 0.3f, cx + 0.4f, 2.8f, cz + 0.3f, MAT_WHITE, MAT_WHITE, 1.0f, 0.97f, 0.85f, 1.0f, false), 5, 0);         // ceiling light
	}
}
// stepped slate roof
static void roof(float x0, float z0, float x1, float z1, float h, float r, float g, float b)
{
	for (int k = 0; k < 3; k++) {
		float o = 0.6f - k * 1.5f;
		float ax0 = x0 - o, az0 = z0 - o, ax1 = x1 + o, az1 = z1 + o;
		if (ax1 - ax0 < 1.0f || az1 - az0 < 1.0f) break;
		add(ax0, h + k * 0.35f, az0, ax1, h + (k + 1) * 0.35f, az1, MAT_ROOF, MAT_ROOF, r, g, b, 2.5f, false);
	}
}
static void upper(float x0, float z0, float x1, float z1, float h, int om, float r, float g, float b)
{
	add(x0, WALL_H, z0, x1, h, z1, om, MAT_ROOF, r, g, b, 3.0f, true);
}
static void furn(float x0, float z0, float x1, float z1, float h, float r, float g, float b, float bev)
{
	Box *bx = block(x0, z0, x1, z1, h, MAT_STUCCO, MAT_STUCCO, r, g, b, 1.5f);
	if (bev > 0) shp(bx, 1, bev);
}
static void stairs(float x0, float z0, int n, bool alongz, bool up_pos, float wd)       // a decorative staircase rising to the ceiling
{
	for (int i = 0; i < n; i++) {
		float h = 0.4f * (i + 1), a = i * 0.5f;
		float p0 = up_pos ? a : (n - 1 - i) * 0.5f;
		if (alongz) block(x0, z0 + p0, x0 + wd, z0 + p0 + 0.5f, h, MAT_DECK, MAT_DECK, 0.62f, 0.75f, 0.52f, 1.5f);
		else block(x0 + p0, z0, x0 + p0 + 0.5f, z0 + wd, h, MAT_DECK, MAT_DECK, 0.62f, 0.75f, 0.52f, 1.5f);
	}
}
static void tvset(float x, float z, bool faceE)                  // retro TV cabinet
{
	if (faceE) { shp(block(x, z, x + 0.7f, z + 2.0f, 1.0f, MAT_STUCCO, MAT_STUCCO, 0.93f, 0.9f, 0.78f, 1.5f), 1, 0.12f); add(x + 0.65f, 0.55f, z + 0.5f, x + 0.72f, 0.95f, z + 1.5f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.6f, 1.0f, false); }
	else { shp(block(x, z, x + 2.0f, z + 0.7f, 1.0f, MAT_STUCCO, MAT_STUCCO, 0.93f, 0.9f, 0.78f, 1.5f), 1, 0.12f); add(x + 0.5f, 0.55f, z + 0.65f, x + 1.5f, 0.95f, z + 0.72f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.6f, 1.0f, false); }
}

// ---- outdoor props ---------------------------------------------------------------------------------------------------
static void bush(float x0, float z0, float x1, float z1, float h)
{
	shp(add(x0, 0, z0, x1, h, z1, MAT_GRASS, MAT_GRASS, 0.5f, 0.78f, 0.45f, 1.5f, true), 1, 0.5f);
}
static void pole(float x, float z)
{
	shp(add(x - 0.15f, 0, z - 0.15f, x + 0.15f, 9.0f, z + 0.15f, MAT_WOOD, MAT_WOOD, 0.55f, 0.45f, 0.35f, 1.2f, true), 2, 0);
	add(x - 1.4f, 8.4f, z - 0.06f, x + 1.4f, 8.55f, z + 0.06f, MAT_WOOD, MAT_WOOD, 0.5f, 0.4f, 0.3f, 1.2f, false);
	if (HIGH) {
		shp(add(x + 0.2f, 6.8f, z - 0.3f, x + 0.8f, 7.8f, z + 0.3f, MAT_CONTAINER, MAT_CONTAINER, 0.6f, 0.62f, 0.6f, 1.5f, false), 2, 0);         // transformer
	}
}
static void tower(float cx, float cz)                              // lattice pylon on the desert horizon
{
	const float ys[4] = { 0, 12, 24, 36 }, hs[3] = { 3.6f, 2.5f, 1.5f };
	for (int s = 0; s < 3; s++) {
		float h = hs[s], h2 = s < 2 ? hs[s + 1] : 0.9f, hm = (h + h2) * 0.5f;
		for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
			add(cx + sx * hm - 0.22f, ys[s], cz + sz * hm - 0.22f, cx + sx * hm + 0.22f, ys[s + 1], cz + sz * hm + 0.22f, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
		for (int k = 0; k <= 3; k++) {
			float y = ys[s] + k * 3.0f, hh = lerpf(h, h2, k / 4.0f) + 0.2f;
			add(cx - hh, y, cz - hh - 0.1f, cx + hh, y + 0.25f, cz - hh + 0.1f, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
			add(cx - hh, y, cz + hh - 0.1f, cx + hh, y + 0.25f, cz + hh + 0.1f, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
			add(cx - hh - 0.1f, y, cz - hh, cx - hh + 0.1f, y + 0.25f, cz + hh, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
			add(cx + hh - 0.1f, y, cz - hh, cx + hh + 0.1f, y + 0.25f, cz + hh, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
		}
	}
	add(cx - 3.0f, 36, cz - 0.2f, cx + 3.0f, 36.4f, cz + 0.2f, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
	add(cx - 2.0f, 38, cz - 0.2f, cx + 2.0f, 38.4f, cz + 0.2f, MAT_CONTAINER, MAT_CONTAINER, 0.45f, 0.35f, 0.3f, 3.0f, false);
	shp(add(cx - 0.12f, 36.4f, cz - 0.12f, cx + 0.12f, 42, cz + 0.12f, MAT_CONTAINER, MAT_CONTAINER, 0.5f, 0.4f, 0.35f, 3.0f, false), 2, 0);
}
static void sign(float x, float z0, float z1)                       // the town sign: stone posts with spheres, a red board
{
	float zm = (z0 + z1) * 0.5f;
	for (int i = 0; i < 2; i++) {
		float pz = i ? z1 : z0;
		block(x - 0.45f, pz - 0.45f, x + 0.45f, pz + 0.45f, 2.6f, MAT_CONCRETE, MAT_CONCRETE, 0.6f, 0.55f, 0.5f, 1.2f);
		shp(add(x - 0.4f, 2.6f, pz - 0.4f, x + 0.4f, 3.4f, pz + 0.4f, MAT_CONCRETE, MAT_CONCRETE, 0.8f, 0.78f, 0.72f, 1.5f, false), 5, 0);
	}
	add(x - 0.12f, 1.1f, z0 + 0.4f, x + 0.12f, 2.9f, z1 - 0.4f, MAT_PANEL, MAT_PANEL, 0.62f, 0.2f, 0.14f, 3.0f, true);
	add(x + 0.12f, 2.2f, zm - 2.0f, x + 0.15f, 2.6f, zm + 2.0f, MAT_WHITE, MAT_WHITE, 0.95f, 0.88f, 0.75f, 1.0f, false);                   // "NUKETOWN"
	add(x + 0.12f, 1.75f, zm - 1.4f, x + 0.15f, 1.95f, zm + 1.4f, MAT_WHITE, MAT_WHITE, 0.9f, 0.82f, 0.7f, 1.0f, false);
	add(x + 0.12f, 1.25f, zm - 1.7f, x + 0.15f, 1.45f, zm + 1.7f, MAT_WHITE, MAT_WHITE, 0.2f, 0.18f, 0.16f, 1.0f, false);
}
static void bus(float x0, float z0, float x1, float z1)           // school bus, long axis along z, nose to the north
{
	shp(add(x0, 0.55f, z0, x1, 2.9f, z1, MAT_WHITE, MAT_WHITE, 0.95f, 0.76f, 0.1f, 4.0f, true), 1, 0.3f);
	add(x0 - 0.02f, 1.05f, z0 + 0.4f, x1 + 0.02f, 1.3f, z1 - 0.3f, MAT_WHITE, MAT_WHITE, 0.08f, 0.08f, 0.08f, 1.0f, false);
	add(x0 - 0.03f, 1.75f, z0 + 1.4f, x0 + 0.01f, 2.45f, z1 - 0.5f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.62f, 1.4f, false);
	add(x1 - 0.01f, 1.75f, z0 + 1.4f, x1 + 0.03f, 2.45f, z1 - 0.5f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.62f, 1.4f, false);
	add(x0 + 0.3f, 1.6f, z0 - 0.03f, x1 - 0.3f, 2.5f, z0 + 0.01f, MAT_WINDOW, MAT_WINDOW, 0.5f, 0.55f, 0.58f, 1.4f, false);
	for (int i = 0; i < 2; i++) {
		float wz = i ? z1 - 1.8f : z0 + 2.4f;
		shp(add(x0 - 0.08f, 0, wz - 0.55f, x0 + 0.5f, 1.1f, wz + 0.55f, MAT_WHITE, MAT_WHITE, 0.07f, 0.07f, 0.08f, 4.0f, false), 3, 0);
		shp(add(x1 - 0.5f, 0, wz - 0.55f, x1 + 0.08f, 1.1f, wz + 0.55f, MAT_WHITE, MAT_WHITE, 0.07f, 0.07f, 0.08f, 4.0f, false), 3, 0);
	}
	add(x0 + 0.2f, 0.4f, z0 - 0.1f, x1 - 0.2f, 0.7f, z0 + 0.1f, MAT_WHITE, MAT_WHITE, 0.15f, 0.15f, 0.15f, 1.0f, false);
	if (HIGH) {
		shp(add(x0 + 0.2f, 0.7f, z0 - 0.06f, x0 + 0.5f, 1.0f, z0 + 0.04f, MAT_WHITE, MAT_WHITE, 1.0f, 0.95f, 0.75f, 1.0f, false), 5, 0);
		shp(add(x1 - 0.5f, 0.7f, z0 - 0.06f, x1 - 0.2f, 1.0f, z0 + 0.04f, MAT_WHITE, MAT_WHITE, 1.0f, 0.95f, 0.75f, 1.0f, false), 5, 0);
	}
}
static void truck(float x0, float z0)                              // white box trailer (z0 .. z0+6.2), red cab and hood to the south
{
	float x1 = x0 + 2.8f, zt = z0 + 6.2f;
	shp(add(x0, 0.9f, z0, x1, 3.7f, zt, MAT_CONTAINER, MAT_CONTAINER, 0.92f, 0.92f, 0.9f, 2.5f, true), 1, 0.1f);
	shp(add(x0 + 0.1f, 0.9f, zt + 0.1f, x1 - 0.1f, 3.3f, zt + 2.5f, MAT_WHITE, MAT_WHITE, 0.78f, 0.12f, 0.1f, 4.0f, true), 1, 0.3f);
	shp(add(x0 + 0.2f, 0.9f, zt + 2.5f, x1 - 0.2f, 2.3f, zt + 4.1f, MAT_WHITE, MAT_WHITE, 0.78f, 0.12f, 0.1f, 4.0f, true), 1, 0.25f);
	add(x0 + 0.25f, 2.1f, zt + 2.45f, x1 - 0.25f, 3.0f, zt + 2.52f, MAT_WINDOW, MAT_WINDOW, 0.5f, 0.55f, 0.58f, 1.4f, false);
	add(x0 + 0.3f, 0.45f, zt + 4.05f, x1 - 0.3f, 1.2f, zt + 4.15f, MAT_WHITE, MAT_WHITE, 0.75f, 0.75f, 0.72f, 1.0f, false);                   // grille
	add(x0 + 0.1f, 0.35f, z0, x1 - 0.1f, 0.9f, zt + 3.8f, MAT_WHITE, MAT_WHITE, 0.1f, 0.1f, 0.1f, 1.0f, false);
	for (int i = 0; i < 4; i++) {
		float wz = i < 2 ? z0 + 1.2f + i * 1.3f : (i == 2 ? zt + 0.9f : zt + 3.3f);
		shp(add(x0 - 0.1f, 0, wz - 0.5f, x0 + 0.4f, 1.0f, wz + 0.5f, MAT_WHITE, MAT_WHITE, 0.07f, 0.07f, 0.08f, 4.0f, false), 3, 0);
		shp(add(x1 - 0.4f, 0, wz - 0.5f, x1 + 0.1f, 1.0f, wz + 0.5f, MAT_WHITE, MAT_WHITE, 0.07f, 0.07f, 0.08f, 4.0f, false), 3, 0);
	}
	if (HIGH) {
		shp(add(x1 - 0.5f, 3.3f, zt + 0.4f, x1 - 0.25f, 4.6f, zt + 0.65f, MAT_WHITE, MAT_WHITE, 0.6f, 0.6f, 0.62f, 1.0f, false), 2, 0);       // exhaust stack
		shp(add(x0 + 0.3f, 0.6f, zt + 4.05f, x0 + 0.65f, 0.9f, zt + 4.2f, MAT_WHITE, MAT_WHITE, 1.0f, 0.95f, 0.75f, 1.0f, false), 5, 0);
		shp(add(x1 - 0.65f, 0.6f, zt + 4.05f, x1 - 0.3f, 0.9f, zt + 4.2f, MAT_WHITE, MAT_WHITE, 1.0f, 0.95f, 0.75f, 1.0f, false), 5, 0);
	}
}

static Pt free_spot(const World *w, float x, float z, float r)         // nearest free cell centre (the navigation grid tests cell centres)
{
	int cx = (int)floorf(x), cz = (int)floorf(z);
	for (int d = 0; d < 12; d++)
		for (int dz = -d; dz <= d; dz++) for (int dx = -d; dx <= d; dx++) {
			if (abs(dx) != d && abs(dz) != d) continue;
			float px = cx + dx + 0.5f, pz = cz + dz + 0.5f;
			if (map_ground(px, pz) != MAT_SAND && !world_blocked(w, px, pz, r)) { Pt p = { px, pz }; return p; }
		}
	Pt p = { x, z }; return p;
}

void map_build(World *w, int high)
{
	W = w;
	memset(w, 0, sizeof *w);
	HIGH = high;
	const float WALLH = 2.2f;

	// ---- the wall round the compound (a union of rectangles, see MAP_RECTS) and the desert beyond
	#define PF(x0, z0, x1, z1) add(x0, 0, z0, x1, WALLH, z1, MAT_WOOD, MAT_WOOD, 0.95f, 0.85f, 0.7f, 1.6f, true)
	PF(-40.3f, -16.3f, 40.3f, -16);                                   // north
	PF(-40.3f, -16, -40, 9.3f); PF(40, -16, 40.3f, 9.3f);             // west, east
	PF(-40, 9, -12, 9.3f); PF(14, 9, 40, 9.3f);                       // south wings
	PF(-12.3f, 9, -12, 13.3f); PF(14, 9, 14.3f, 13.3f);
	PF(-12, 13, -7, 13.3f); PF(9, 13, 14, 13.3f);
	PF(-7.3f, 13, -7, 18.3f); PF(9, 13, 9.3f, 18.3f); PF(-7.3f, 18, 9.3f, 18.3f);   // the dead-end of the street
	#undef PF

	// ---- the teal house (west): two storeys, enterable ground floor
	{
		style(MAT_PANEL, 0.3f, 0.78f, 0.72f, MAT_STUCCO, 0.86f, 0.82f, 0.58f);
		static const Opening op[] = {
			{ 3, -4.5f, 1.5f, 0 }, { 3, -7.5f, 1.6f, 1 }, { 3, -2.5f, 1.6f, 1 },
			{ 2, -4.5f, 1.5f, 0 }, { 2, -7.5f, 1.6f, 1 }, { 2, -2.5f, 1.6f, 1 },
			{ 0, -18.5f, 1.6f, 1 }, { 0, -11.5f, 1.6f, 1 },
			{ 1, -18.5f, 1.5f, 0 }, { 1, -11.5f, 1.6f, 1 },
		};
		shell(-21, -9, -9, -1, op, 10, 0);
		static const float gaps[] = { -4.5f };
		part(false, -15, -8.7f, -1.3f, gaps, 1, 0.74f, 0.44f, 0.28f);
		floor_(-20.7f, -8.7f, -15.1f, -1.3f, MAT_CONCRETE, 0.9f, 0.85f, 0.72f);
		floor_(-14.9f, -8.7f, -9.3f, -1.3f, MAT_STUCCO, 0.42f, 0.5f, 0.42f);
		ceiling(-20.7f, -8.7f, -15.1f, -1.3f); ceiling(-14.9f, -8.7f, -9.3f, -1.3f);
		upper(-21, -9, -9, -1, 6.2f, MAT_PANEL, 0.92f, 0.94f, 0.94f);
		for (int f = 0; f < 4; f++) windows(-21, -9, -9, -1, f, 3.8f, 5.1f, 1.8f);
		roof(-21, -9, -9, -1, 6.2f, 0.82f, 0.85f, 1.0f);
		add(-9.0f, 2.9f, -9.0f, -7.8f, 3.05f, -1.0f, MAT_CONCRETE, MAT_CONCRETE, 0.95f, 0.95f, 0.92f, 2.0f, false);                 // porch awning over the door side
		// kitchen
		furn(-20.7f, -8.7f, -15.1f, -7.8f, 0.95f, 0.95f, 0.72f, 0.22f, 0.04f);
		furn(-20.7f, -2.6f, -19.9f, -1.5f, 1.9f, 0.9f, 0.88f, 0.8f, 0.05f);
		// living room
		furn(-13.4f, -2.7f, -10.0f, -1.6f, 0.85f, 0.88f, 0.4f, 0.14f, 0.2f);
		tvset(-14.5f, -8.0f, true); 
		floor_(-13.5f, -7.4f, -10.5f, -6.0f, MAT_STUCCO, 0.95f, 0.93f, 0.85f);
		stairs(-10.5f, -8.6f, 6, true, true, 1.2f);
		// garage (west of the road side, open to the east)
		static const Opening gop[] = { { 3, 3.5f, 3.6f, 2 }, { 0, -16.0f, 1.6f, 1 }, { 2, 3.5f, 1.6f, 1 } };
		style(MAT_PANEL, 0.93f, 0.93f, 0.9f, MAT_CONCRETE, 0.78f, 0.78f, 0.74f);
		shell(-19, 1, -13, 6, gop, 3, 0);
		floor_(-18.7f, 1.3f, -13.3f, 5.7f, MAT_CONCRETE, 0.62f, 0.62f, 0.6f);
		ceiling(-18.7f, 1.3f, -13.3f, 5.7f);
		upper(-19, 1, -13, 6, 3.25f, MAT_PANEL, 0.93f, 0.93f, 0.9f);
		roof(-19, 1, -13, 6, 3.25f, 0.8f, 0.82f, 0.95f);
		furn(-18.7f, 1.3f, -17.4f, 3.3f, 1.0f, 0.5f, 0.38f, 0.25f, 0.04f);
	}
	// shed and back-yard fence
	shp(block(-36, -11, -31, -6, 2.7f, MAT_PANEL, MAT_ROOF, 0.3f, 0.78f, 0.72f, 3.0f), 0, 0);
	add(-36.4f, 2.7f, -11.4f, -30.6f, 2.95f, -5.6f, MAT_ROOF, MAT_ROOF, 0.82f, 0.84f, 1.0f, 2.5f, false);

	// ---- the yellow house (east): a T shape, wide front block + narrower back block
	{
		style(MAT_PANEL, 0.96f, 0.85f, 0.35f, MAT_STUCCO, 0.9f, 0.84f, 0.58f);
		static const Opening opA[] = {
			{ 0, 18.5f, 3.0f, 2 },
			{ 2, 1.5f, 1.5f, 0 }, { 2, -1.5f, 1.6f, 1 }, { 2, 4.5f, 1.6f, 1 },
			{ 3, 1.5f, 1.5f, 0 }, { 3, -1.5f, 1.6f, 1 }, { 3, 4.5f, 1.6f, 1 },
			{ 1, 17.5f, 1.5f, 0 }, { 1, 13.5f, 1.6f, 1 }, { 1, 21.5f, 1.6f, 1 },
		};
		shell(11, -3, 23, 6, opA, 10, 0);
		static const Opening opB[] = { { 0, 17.0f, 1.8f, 1 }, { 2, -6.0f, 1.6f, 1 }, { 3, -6.0f, 1.6f, 1 }, { 3, -4.5f, 1.5f, 0 } };
		shell(13, -9, 21, -3, opB, 4, 2);
		static const float gaps[] = { 1.5f };
		part(false, 15, -2.7f, 5.7f, gaps, 1, 0.74f, 0.44f, 0.28f);
		floor_(11.3f, -2.7f, 14.9f, 5.7f, MAT_CONCRETE, 0.9f, 0.85f, 0.7f);
		floor_(15.1f, -2.7f, 22.7f, 5.7f, MAT_STUCCO, 0.42f, 0.5f, 0.42f);
		floor_(13.3f, -8.7f, 20.7f, -3.0f, MAT_STUCCO, 0.45f, 0.5f, 0.4f);
		ceiling(11.3f, -2.7f, 14.9f, 5.7f); ceiling(15.1f, -2.7f, 22.7f, 5.7f); ceiling(13.3f, -8.7f, 20.7f, -3.0f);
		upper(11, -3, 23, 6, 6.0f, MAT_PANEL, 0.96f, 0.85f, 0.35f);
		upper(13, -9, 21, -3, 6.0f, MAT_PANEL, 0.96f, 0.85f, 0.35f);
		windows(11, -3, 23, 6, 1, 3.8f, 5.1f, 1.8f); windows(11, -3, 23, 6, 2, 3.8f, 5.1f, 1.8f); windows(11, -3, 23, 6, 3, 3.8f, 5.1f, 1.8f);
		windows(13, -9, 21, -3, 0, 3.8f, 5.1f, 1.8f); windows(13, -9, 21, -3, 2, 3.8f, 5.1f, 1.8f); windows(13, -9, 21, -3, 3, 3.8f, 5.1f, 1.8f);
		roof(11, -3, 23, 6, 6.0f, 0.8f, 0.82f, 1.0f);
		roof(13, -9, 21, -3, 6.0f, 0.8f, 0.82f, 1.0f);
		// kitchen
		furn(11.3f, 4.8f, 14.9f, 5.7f, 0.95f, 0.98f, 0.72f, 0.2f, 0.04f);
		furn(11.3f, -2.7f, 12.1f, -1.0f, 1.9f, 0.98f, 0.72f, 0.2f, 0.04f);
		// living room
		furn(17.0f, 4.5f, 20.4f, 5.6f, 0.85f, 0.88f, 0.4f, 0.14f, 0.2f);
		tvset(21.9f, -1.0f, true);
		floor_(17.5f, 0.0f, 20.5f, 3.0f, MAT_STUCCO, 0.95f, 0.93f, 0.85f);
		// back room
		stairs(13.4f, -8.6f, 6, false, true, 1.4f);
		furn(17.4f, -8.6f, 20.6f, -7.5f, 0.85f, 0.9f, 0.55f, 0.18f, 0.2f);
	}
	// small shed behind the yellow house
	shp(block(26, -14, 30, -10, 2.5f, MAT_PANEL, MAT_ROOF, 0.96f, 0.85f, 0.35f, 3.0f), 0, 0);
	add(25.6f, 2.5f, -14.4f, 30.4f, 2.75f, -9.6f, MAT_ROOF, MAT_ROOF, 0.8f, 0.82f, 0.98f, 2.5f, false);

	// ---- the house behind the north wall (a backdrop: you can't get in)
	{
		block(-13, -27, 9, -18, 5.4f, MAT_PANEL, MAT_ROOF, 0.9f, 0.62f, 0.64f, 3.0f);
		roof(-13, -27, 9, -18, 5.4f, 0.75f, 0.75f, 0.82f);
		block(-14, -26, -5, -18.5f, 3.2f, MAT_PANEL, MAT_ROOF, 0.9f, 0.62f, 0.64f, 3.0f);
		add(-12.6f, 0, -18.55f, -10.4f, 2.5f, -18.45f, MAT_WHITE, MAT_WHITE, 0.78f, 0.1f, 0.08f, 1.0f, false);                    // red garage doors
		add(-9.6f, 0, -18.55f, -7.4f, 2.5f, -18.45f, MAT_WHITE, MAT_WHITE, 0.78f, 0.1f, 0.08f, 1.0f, false);
		add(-4.0f, 1.2f, -18.05f, -1.6f, 2.5f, -18.0f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.65f, 1.4f, false);
		add(2.0f, 1.2f, -18.05f, 4.4f, 2.5f, -18.0f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.65f, 1.4f, false);
		add(-4.0f, 3.6f, -18.05f, -1.6f, 4.8f, -18.0f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.65f, 1.4f, false);
		add(2.0f, 3.6f, -18.05f, 4.4f, 4.8f, -18.0f, MAT_WINDOW, MAT_WINDOW, 0.55f, 0.6f, 0.65f, 1.4f, false);
	}

	// ---- vehicles in the street
	bus(-3.6f, 3.0f, -1.0f, 12.0f);
	truck(2.0f, -6.0f);
	car(-24.5f, 3.5f, true, 0.45f, 0.62f, 0.52f);
	car(-1.0f, -10.0f, false, 0.8f, 0.12f, 0.1f);
	car(28.0f, 4.5f, true, 0.2f, 0.66f, 0.62f);
	// the dumpster at the dead end, and the rock barricade
	shp(block(-0.7f, 13.6f, 2.7f, 15.2f, 1.3f, MAT_CONTAINER, MAT_CONTAINER, 0.12f, 0.14f, 0.12f, 2.0f), 1, 0.1f);
	for (int i = 0; i < 7; i++) {
		float rx = -5.5f + i * 1.9f, rr = 0.55f + (i * 37 % 5) * 0.08f;
		shp(add(rx - rr - 0.2f, 0, 16.1f - rr, rx + rr + 0.2f, rr * 1.7f, 16.1f + rr, MAT_CONCRETE, MAT_CONCRETE, 0.7f, 0.64f, 0.56f, 1.5f, true), 5, 0);
	}

	// ---- fences, hedges, crates, poles, sign
	fence(-34.1f, -4, -34, 0);  fence(34, -4, 34.1f, 1);
	fence(-23.1f, -9, -23, -6.5f);
	fence(25, -4, 25.1f, -1.2f);    fence(25, -5.2f, 29, -5.1f);
	bush(-26.4f, -15.5f, -23, -12.6f, 1.3f); bush(21.9f, -15, 24.8f, -11.5f, 1.5f); bush(-29.5f, 5.6f, -25.8f, 8.0f, 1.2f);
	bush(-17.2f, -9.9f, -14.8f, -8.0f, 1.2f); bush(13.5f, -11.7f, 15.6f, -9.6f, 1.2f); bush(16.0f, 6.0f, 18.7f, 7.6f, 1.2f);
	bush(30.0f, -15.8f, 31.8f, -10.0f, 1.9f); bush(31.0f, -6.6f, 33.5f, -5.3f, 1.2f); bush(28.5f, 3.4f, 29.6f, 5.6f, 1.2f);
	bush(-36.0f, 7.5f, -34.5f, 9.0f, 1.0f);
	crate(31.0f, 4.8f, 1.3f, 1.3f); crate(32.4f, 5.0f, 1.2f, 1.2f); crate(-36.5f, 12.0f - 4.0f, 1.3f, 1.3f);
	crate(-2.0f, -14.0f, 1.3f, 1.3f); crate(-0.7f, -14.3f, 1.2f, 1.2f);
	barrier(-6.0f, 1.0f, false); barrier(8.0f, 6.0f, false);
	pole(-8.2f, 6.5f); pole(9.6f, -5.0f);
	sign(-8.2f, -14.5f, -10.0f);
	static const Pt trees[] = { { -37, -13.5f }, { -31, 6.5f }, { 36.5f, -13 }, { 36, 5.5f }, { -11, -14 }, { 17, -13.5f } };
	for (unsigned i = 0; i < sizeof trees / sizeof trees[0]; i++) {
		float tx = trees[i].x, tz = trees[i].z, s = 0.85f + (i % 3) * 0.15f;
		shp(add(tx - 0.22f, 0, tz - 0.22f, tx + 0.22f, 3.0f * s, tz + 0.22f, MAT_WOOD, MAT_WOOD, 0.75f, 0.6f, 0.45f, 1.2f, true), 2, 0);
		shp(add(tx - 1.8f * s, 2.3f * s, tz - 1.8f * s, tx + 1.8f * s, 5.3f * s, tz + 1.8f * s, MAT_GRASS, MAT_GRASS, 0.62f, 0.95f, 0.5f, 2.0f, false), 5, 0);
		shp(add(tx - 1.1f * s, 4.2f * s, tz - 1.1f * s + 0.6f, tx + 1.1f * s, 6.0f * s, tz + 1.1f * s + 0.6f, MAT_GRASS, MAT_GRASS, 0.7f, 1.0f, 0.55f, 2.0f, false), 5, 0);
	}
	// ---- street markings
	for (float z = -6; z < 12; z += 3) add(0.9f, 0, z, 1.1f, 0.012f, z + 1.5f, MAT_WHITE, MAT_WHITE, 0.95f, 0.8f, 0.2f, 1.0f, false);

	// ---- the desert: pylon, mountains, scrub
	tower(-58, -34);
	static const float mtn[][5] = { { -160, -170, 95, 52, 40 }, { -50, -200, 120, 50, 30 }, { 80, -190, 105, 55, 44 }, { 175, -120, 85, 55, 34 }, { -200, -60, 65, 75, 28 }, { 205, 10, 65, 65, 25 }, { -190, 80, 65, 65, 27 }, { 40, 210, 120, 40, 20 } };
	for (unsigned i = 0; i < sizeof mtn / sizeof mtn[0]; i++)
		shp(add(mtn[i][0] - mtn[i][2], -mtn[i][4] * 0.35f, mtn[i][1] - mtn[i][3], mtn[i][0] + mtn[i][2], mtn[i][4], mtn[i][1] + mtn[i][3], MAT_STUCCO, MAT_STUCCO, 0.78f, 0.56f, 0.42f, 40.0f, false), 5, 0);
	if (HIGH) {
		static const Pt scrub[] = { { -46, -20 }, { -48, 4 }, { -45, 16 }, { -30, 21 }, { -14, 22 }, { 18, 21 }, { 34, 20 }, { 46, 12 }, { 48, -8 }, { 44, -22 }, { 20, -26 }, { 28, -30 }, { -24, -28 }, { 6, -32 } };
		for (unsigned i = 0; i < sizeof scrub / sizeof scrub[0]; i++) {
			float s = 0.7f + (i * 7 % 5) * 0.18f;
			shp(add(scrub[i].x - 0.9f * s, 0, scrub[i].z - 0.9f * s, scrub[i].x + 0.9f * s, 0.9f * s, scrub[i].z + 0.9f * s, MAT_GRASS, MAT_GRASS, 0.55f, 0.62f, 0.38f, 1.5f, false), 5, 0);
		}
	}

	// ---- solids list
	for (int i = 0; i < w->nbox; i++) if (w->box[i].solid) w->solid_idx[w->nsolid++] = i;

	// ---- navigation points, spawn points, pickups: hints that get snapped to the nearest free cell centre (checked by host/simtest.c: connected)
	static const Pt nav[] = {
		{ 0.5f, -8.5f }, { 0.5f, 0.5f }, { -4.5f, -4.5f }, { 5.5f, -3.5f }, { -4.5f, 7.5f }, { 6.5f, 7.5f }, { 1.5f, 11.5f }, { 4.5f, 13.5f }, { -4.5f, 15.5f },
		{ -37.5f, -13.5f }, { -37.5f, -2.5f }, { -37.5f, 6.5f }, { -30.5f, -13.5f }, { -30.5f, -2.5f }, { -30.5f, 6.5f }, { -26.5f, -9.5f }, { -24.5f, 0.5f }, { -24.5f, 7.5f }, { -12.5f, -11.5f },
		{ -18.5f, -6.5f }, { -11.5f, -3.5f }, { -16.5f, -2.5f }, { -16.5f, 3.5f }, { -11.5f, 7.5f },
		{ 36.5f, -13.5f }, { 36.5f, -2.5f }, { 36.5f, 6.5f }, { 29.5f, -9.5f }, { 29.5f, 0.5f }, { 29.5f, 7.5f }, { 25.5f, 7.5f }, { 21.5f, 7.5f }, { 12.5f, -12.5f },
		{ 19.5f, 3.5f }, { 13.5f, 0.5f }, { 17.5f, -6.5f }, { 24.5f, -1.5f }, { 8.5f, 11.5f },
	};
	for (unsigned i = 0; i < sizeof nav / sizeof nav[0]; i++) w->nav[w->nnav++] = free_spot(w, nav[i].x, nav[i].z, 0.62f);
	static const Pt sp[] = {
		{ -37.5f, -13.5f }, { -37.5f, -7.5f }, { -37.5f, -2.5f }, { -37.5f, 2.5f }, { -37.5f, 6.5f }, { -30.5f, -13.5f },
		{ 36.5f, -13.5f }, { 36.5f, -7.5f }, { 36.5f, -2.5f }, { 36.5f, 2.5f }, { 36.5f, 6.5f }, { 34.5f, -13.5f },
	};
	for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) w->spawn[w->nspawn++] = free_spot(w, sp[i].x, sp[i].z, 0.62f);
	static const struct { float x, z; int t; } pk[] = {
		{ 0.5f, -7.5f, 0 }, { -30.5f, -2.5f, 0 }, { 31.5f, -2.5f, 0 }, { 17.5f, 2.5f, 0 }, { -16.5f, -5.5f, 0 }, { 1.5f, 11.5f, 0 },
		{ -6.0f, 8.5f, 1 }, { 8.0f, 8.5f, 1 }, { -29.5f, 6.5f, 1 }, { 32.5f, -9.5f, 1 },
		{ -12.5f, -12.5f, 2 }, { 14.5f, -12.5f, 2 },
	};
	for (unsigned i = 0; i < sizeof pk / sizeof pk[0]; i++) { Pt q = free_spot(w, pk[i].x, pk[i].z, 0.62f); w->pick[w->npick].x = q.x; w->pick[w->npick].z = q.z; w->pick[w->npick].type = pk[i].t; w->npick++; }
}
