// Grand Theft Otto - the 3D scene builder: camera, textured ground, extruded buildings with a baked sun-shadow map,
// low-poly cars, people, trees and helicopters, billboard effects. Everything is flat-shaded into vertex colours on the CPU
// (the GPU only has to transform and texture), so the PC preview and the 3DS draw exactly the same triangles.
#include "r3d.h"
#include "gen_map.h"
#include <math.h>
#include <string.h>

#define MAXW 256
static const World *W;
static uint8_t bh[MAXW * MAXW];                            // building height per tile (0 = no building)
static uint8_t shadow_map[(MAXW + 1) * (MAXW + 1)];        // how much sun reaches each tile corner (255 = full)
static int32_t flood[MAXW * MAXW];

// ---- sun (towards the light: from the north-west, high up) -------------------------------------------------------
#define SUN_X (-0.52f)
#define SUN_Y (-0.36f)
#define SUN_Z ( 0.78f)
static float sunl(float nx, float ny, float nz)
{
	float d = nx * SUN_X + ny * SUN_Y + nz * SUN_Z;
	return 0.66f + 0.44f * (d > 0 ? d : 0);
}

static unsigned hash2(int a, int b)
{
	unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

// ---- which building a roof tile belongs to -------------------------------------------------------------------------
static int bgroup(int id)
{
	if (id >= TILE_ROOF_GRAY0 && id <= TILE_ROOF_ORANGE3) return TILE_ROOF_GRAY0 + ((id - TILE_ROOF_GRAY0) >> 2) * 4;
	if (id >= TILE_HOSP_0 && id <= TILE_HOSP_8) return TILE_HOSP_0;
	if (id >= TILE_TOWER_GLASS0 && id <= TILE_TOWER_GLASS1) return TILE_TOWER_GLASS0;
	if (id >= TILE_TOWER_DARK0 && id <= TILE_TOWER_DARK1) return TILE_TOWER_DARK0;
	if (id >= TILE_TOWER_STONE0 && id <= TILE_TOWER_STONE1) return TILE_TOWER_STONE0;
	if (id >= TILE_OBS_0 && id <= TILE_OBS_8) return TILE_OBS_0;
	return id;
}
static bool is_building(int id) { return id >= 0 && (tile_kind[id] == K_BUILDING || tile_kind[id] == K_TOWER); }

static int height_for(int id, unsigned h)
{
	if (tile_kind[id] == K_TOWER) return 96 + 16 * (int)(h % 5);
	if (id >= TILE_HOSP_0 && id <= TILE_HOSP_8) return 48;
	if (id >= TILE_OBS_0 && id <= TILE_OBS_8) return 32;
	if (id == TILE_STAND) return 32;
	if (id == TILE_ROOF_THEATRE) return 48;
	if (id == TILE_ROOF_GARAGE || id == TILE_ROOF_POLICE || id == TILE_ROOF_STATION) return 32;
	return 32 + 16 * (int)(h % 3);
}

static inline int tile_at(int tx, int ty) { return world_tile(W, tx, ty); }
static inline int bh_at(int tx, int ty) { return (tx < 0 || ty < 0 || tx >= W->w || ty >= W->h) ? 0 : bh[ty * W->w + tx]; }

void r3d_init(const World *w)
{
	W = w;
	memset(bh, 0, sizeof bh);
	// buildings: every connected region of one roof group gets one height
	for (int y = 0; y < w->h; y++)
		for (int x = 0; x < w->w; x++) {
			int id = tile_at(x, y);
			if (!is_building(id) || bh[y * w->w + x]) continue;
			int grp = bgroup(id), hgt = height_for(id, hash2(x, y));
			int sp = 0;
			flood[sp++] = y * w->w + x; bh[y * w->w + x] = (uint8_t)hgt;
			while (sp) {
				int cur = flood[--sp], cx = cur % w->w, cy = cur / w->w;
				static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
				for (int k = 0; k < 4; k++) {
					int nx = cx + dx[k], ny = cy + dy[k];
					if (nx < 0 || ny < 0 || nx >= w->w || ny >= w->h || bh[ny * w->w + nx]) continue;
					int nid = tile_at(nx, ny);
					if (!is_building(nid) || bgroup(nid) != grp) continue;
					bh[ny * w->w + nx] = (uint8_t)hgt;
					flood[sp++] = ny * w->w + nx;
				}
			}
		}
	// the sun's shadows and a little ambient occlusion, baked once for every tile corner
	int cw = w->w + 1;
	for (int cy = 0; cy <= w->h; cy++)
		for (int cx = 0; cx <= w->w; cx++) {
			float px = cx * (float)TILE, py = cy * (float)TILE, lit = 1.0f;
			for (float t = 3.0f; t <= 156.0f; t += 6.0f) {
				float sx = px + SUN_X / SUN_Z * t - 0.1f, sy = py + SUN_Y / SUN_Z * t - 0.1f;
				int tx = (int)floorf(sx / TILE), ty = (int)floorf(sy / TILE);
				if (tx < 0 || ty < 0 || tx >= w->w || ty >= w->h) break;
				if (bh[ty * w->w + tx] >= t) { lit = 0.62f; break; }
			}
			int n = (bh_at(cx - 1, cy - 1) > 0) + (bh_at(cx, cy - 1) > 0) + (bh_at(cx - 1, cy) > 0) + (bh_at(cx, cy) > 0);
			float ao = 1.0f - 0.05f * n;
			shadow_map[cy * cw + cx] = (uint8_t)(255.0f * lit * ao);
		}
}

// ---- scene state ---------------------------------------------------------------------------------------------------
static R3DScene *S;
static const Game *GG;
static int gn[G_COUNT], gcap[G_COUNT];
static Vtx *gp[G_COUNT];
static float EX, EH, EZ;                                   // camera position: x, height, y (south)
static float CF[3], CR[3], CU[3];                          // camera forward / right / up in world coordinates
static float FOCX, FOCY;
#define NEAR 8.0f
static float SKY[3] = {0.62f, 0.72f, 0.86f};
static float TINT[3] = {1, 1, 1};                          // time-of-day colour of the light
static float G_GLOW;                                       // 1 while emitting lights (they ignore the tint)
static float DAYF = 1.0f, NIGHT = 0.0f;                    // 1 / 0 in full daylight, 0 / 1 in the dead of night
static float TX, TY;                                       // half-extents of the view frustum at distance 1

static bool visible(float x, float y, float h, float r)
{
	float dx = x - EX, dy = h - EH, dz = y - EZ;
	float zc = dx * CF[0] + dy * CF[1] + dz * CF[2];
	if (zc + r < NEAR) return false;
	float xc = dx * CR[0] + dy * CR[1] + dz * CR[2], yc = dx * CU[0] + dy * CU[1] + dz * CU[2];
	return fabsf(xc) <= (zc + r) * TX + r && fabsf(yc) <= (zc + r) * TY + r;
}

static uint32_t pack(float r, float g, float b, float a, float x, float y)
{
	float dx = x - FOCX, dy = y - FOCY, d2 = dx * dx + dy * dy;
	float t = (d2 - 200.0f * 200.0f) / (440.0f * 440.0f - 200.0f * 200.0f);
	t = t < 0 ? 0 : t > 1 ? 1 : t; t *= 0.7f;
	r *= TINT[0] + (1.0f - TINT[0]) * G_GLOW; g *= TINT[1] + (1.0f - TINT[1]) * G_GLOW; b *= TINT[2] + (1.0f - TINT[2]) * G_GLOW;
	r += (SKY[0] - r) * t; g += (SKY[1] - g) * t; b += (SKY[2] - b) * t;
	r = r < 0 ? 0 : r > 1 ? 1 : r; g = g < 0 ? 0 : g > 1 ? 1 : g; b = b < 0 ? 0 : b > 1 ? 1 : b; a = a < 0 ? 0 : a > 1 ? 1 : a;
	return (uint32_t)(r * 255.0f + 0.5f) | ((uint32_t)(g * 255.0f + 0.5f) << 8) | ((uint32_t)(b * 255.0f + 0.5f) << 16) | ((uint32_t)(a * 255.0f + 0.5f) << 24);
}
static uint32_t rgbc(uint32_t rgb, float sh, float a, float x, float y)
{
	return pack(((rgb >> 16) & 255) / 255.0f * sh, ((rgb >> 8) & 255) / 255.0f * sh, (rgb & 255) / 255.0f * sh, a, x, y);
}

static inline void V3(int g, float X, float Y, float Z, float u, float v, uint32_t c)        // world coordinates: x east, y up, z south
{
	if (gn[g] >= gcap[g]) return;
	Vtx *p = &gp[g][gn[g]++];
	p->x = X; p->y = Y; p->z = Z; p->u = u; p->v = v;
	p->r = (float)(c & 255) * (1.0f / 255.0f); p->g = (float)((c >> 8) & 255) * (1.0f / 255.0f); p->b = (float)((c >> 16) & 255) * (1.0f / 255.0f); p->a = (float)(c >> 24) * (1.0f / 255.0f);
}
static inline void V(int g, float x, float y, float h, float u, float v, uint32_t c) { V3(g, x, h, y, u, v, c); }   // 2D map position + height

static void cell_uv(int cell, float *u0, float *u1, float *vt, float *vb)
{
	int cx = cell % ATLAS_COLS, cy = cell / ATLAS_COLS;
	const float e = 0.6f;
	*u0 = (cx * ATLAS_CELL + e) / WORLD_ATLAS_W; *u1 = ((cx + 1) * ATLAS_CELL - e) / WORLD_ATLAS_W;
	*vt = 1.0f - (cy * ATLAS_CELL + e) / WORLD_ATLAS_H; *vb = 1.0f - ((cy + 1) * ATLAS_CELL - e) / WORLD_ATLAS_H;
}
static void white_uv(float *u, float *v)
{
	float u0, u1, vt, vb; cell_uv(CELL_WHITE, &u0, &u1, &vt, &vb);
	*u = (u0 + u1) * 0.5f; *v = (vt + vb) * 0.5f;
}

// a flat-coloured quad in the world texture (corners in order around the quad)
static void cquad(int g, const float p[4][3], uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
	float u, v; white_uv(&u, &v);
	V(g, p[0][0], p[0][1], p[0][2], u, v, c0); V(g, p[1][0], p[1][1], p[1][2], u, v, c1); V(g, p[2][0], p[2][1], p[2][2], u, v, c2);
	V(g, p[0][0], p[0][1], p[0][2], u, v, c0); V(g, p[2][0], p[2][1], p[2][2], u, v, c2); V(g, p[3][0], p[3][1], p[3][2], u, v, c3);
}
static void ctri(int g, float ax, float ay, float ah, float bx, float by, float bh_, float cx, float cy, float ch, uint32_t c)
{
	float u, v; white_uv(&u, &v);
	V(g, ax, ay, ah, u, v, c); V(g, bx, by, bh_, u, v, c); V(g, cx, cy, ch, u, v, c);
}

static bool face_vis(float nx, float ny, float px, float py) { return (EX - px) * nx + (EZ - py) * ny > -2.0f; }

// ---- local frames for models (x right, y forward, both on the ground plane) -----------------------------------------
typedef struct { float x, y, rx, ry, fx, fy, s; } Fr;                 // s: size of the model (people are drawn a bit bigger than life)
static Fr frame(float x, float y, float heading) { Fr f = {x, y, cosf(heading), sinf(heading), sinf(heading), -cosf(heading), 1.0f}; return f; }
static void fpt(const Fr *f, float lr, float lf, float *ox, float *oy) { *ox = f->x + f->rx * lr + f->fx * lf; *oy = f->y + f->ry * lr + f->fy * lf; }

// a box centred at local (lr, lf), w wide (right axis), l long (forward axis), from height z0 up by h. glow 1 = unshaded (lights).
static void fbox(const Fr *f, float lr, float lf, float z0, float w, float l, float h, uint32_t rgb, float glow)
{
	lr *= f->s; lf *= f->s; z0 *= f->s; w *= f->s; l *= f->s; h *= f->s;
	G_GLOW = glow;
	float ax, ay, bx, by, cx, cy, dx, dy;
	fpt(f, lr - w * 0.5f, lf - l * 0.5f, &ax, &ay); fpt(f, lr + w * 0.5f, lf - l * 0.5f, &bx, &by);
	fpt(f, lr + w * 0.5f, lf + l * 0.5f, &cx, &cy); fpt(f, lr - w * 0.5f, lf + l * 0.5f, &dx, &dy);
	float mx = (ax + cx) * 0.5f, my = (ay + cy) * 0.5f, z1 = z0 + h, u, v;
	white_uv(&u, &v);
	float top = sunl(0, 0, 1); top += (1.0f - top) * glow;
	uint32_t ct = rgbc(rgb, top, 1, mx, my);
	V(G_OPAQUE, ax, ay, z1, u, v, ct); V(G_OPAQUE, bx, by, z1, u, v, ct); V(G_OPAQUE, cx, cy, z1, u, v, ct);
	V(G_OPAQUE, ax, ay, z1, u, v, ct); V(G_OPAQUE, cx, cy, z1, u, v, ct); V(G_OPAQUE, dx, dy, z1, u, v, ct);
	struct { float x0, y0, x1, y1, nx, ny; } sd[4] = {{ax, ay, dx, dy, -f->rx, -f->ry}, {bx, by, cx, cy, f->rx, f->ry}, {dx, dy, cx, cy, f->fx, f->fy}, {ax, ay, bx, by, -f->fx, -f->fy}};
	for (int i = 0; i < 4; i++) {
		float px = (sd[i].x0 + sd[i].x1) * 0.5f, py = (sd[i].y0 + sd[i].y1) * 0.5f;
		if (!face_vis(sd[i].nx, sd[i].ny, px, py)) continue;
		float s = sunl(sd[i].nx, sd[i].ny, 0); s += (1.0f - s) * glow;
		uint32_t cc = rgbc(rgb, s, 1, px, py);
		V(G_OPAQUE, sd[i].x0, sd[i].y0, z0, u, v, cc); V(G_OPAQUE, sd[i].x1, sd[i].y1, z0, u, v, cc); V(G_OPAQUE, sd[i].x1, sd[i].y1, z1, u, v, cc);
		V(G_OPAQUE, sd[i].x0, sd[i].y0, z0, u, v, cc); V(G_OPAQUE, sd[i].x1, sd[i].y1, z1, u, v, cc); V(G_OPAQUE, sd[i].x0, sd[i].y0, z1, u, v, cc);
	}
	G_GLOW = 0;
}

// a cone / frustum (trees, bushes)
static void cone(float cx, float cy, float z0, float r0, float z1, float r1, int sides, uint32_t rgb, float rot)
{
	float nz = (r0 - r1) / (z1 - z0), ln = sqrtf(1.0f + nz * nz);
	for (int i = 0; i < sides; i++) {
		float a0 = rot + i * 6.2831853f / sides, a1 = rot + (i + 1) * 6.2831853f / sides, am = (a0 + a1) * 0.5f;
		float nx = cosf(am) / ln, ny = sinf(am) / ln;
		float x0 = cx + cosf(a0) * r0, y0 = cy + sinf(a0) * r0, x1 = cx + cosf(a1) * r0, y1 = cy + sinf(a1) * r0;
		float px = (x0 + x1) * 0.5f, py = (y0 + y1) * 0.5f;
		if (!face_vis(nx, ny, px, py) && nz < 0.5f) continue;
		uint32_t c = rgbc(rgb, sunl(nx, ny, nz / ln), 1, px, py);
		if (r1 > 0.01f) {
			float x2 = cx + cosf(a0) * r1, y2 = cy + sinf(a0) * r1, x3 = cx + cosf(a1) * r1, y3 = cy + sinf(a1) * r1;
			ctri(G_OPAQUE, x0, y0, z0, x1, y1, z0, x3, y3, z1, c); ctri(G_OPAQUE, x0, y0, z0, x3, y3, z1, x2, y2, z1, c);
		} else ctri(G_OPAQUE, x0, y0, z0, x1, y1, z0, cx, cy, z1, c);
	}
}

// ---- billboards and flat sprites (sprite atlas, blended) -----------------------------------------------------------
static void spr_uv(int spr, float *u0, float *u1, float *vt, float *vb)
{
	const SprRect *r = &spr_rects[spr];
	*u0 = (r->x + 0.5f) / SPRITE_ATLAS_W; *u1 = (r->x + r->w - 0.5f) / SPRITE_ATLAS_W;
	*vt = 1.0f - (r->y + 0.5f) / SPRITE_ATLAS_H; *vb = 1.0f - (r->y + r->h - 0.5f) / SPRITE_ATLAS_H;
}
// faces the camera; (x, y) on the map, h above the ground
static void billboard(int spr, float x, float y, float h, float w, float hh, float shade, float alpha)
{
	bool lit = spr == SPR_GLOW_WHITE || spr == SPR_GLOW_RED || spr == SPR_GLOW_BLUE || spr == SPR_MUZZLE || spr == SPR_SPARK || (spr >= SPR_FIRE0 && spr <= SPR_FIRE0 + 2) || (spr >= SPR_BOOM0 && spr <= SPR_BOOM0 + 5) || spr == SPR_BULLET || spr == SPR_ROCKET || (spr >= SPR_PK_HEALTH && spr <= SPR_PK_AMMO);
	G_GLOW = lit ? 1.0f : 0.0f;
	float u0, u1, vt, vb; spr_uv(spr, &u0, &u1, &vt, &vb);
	float X = x, Y = h, Z = y;
	float rx = CR[0] * w * 0.5f, ry = CR[1] * w * 0.5f, rz = CR[2] * w * 0.5f, ux = CU[0] * hh * 0.5f, uy = CU[1] * hh * 0.5f, uz = CU[2] * hh * 0.5f;
	uint32_t c = pack(shade, shade, shade, alpha, x, y);
	V3(G_SPRITE, X - rx + ux, Y - ry + uy, Z - rz + uz, u0, vt, c); V3(G_SPRITE, X + rx + ux, Y + ry + uy, Z + rz + uz, u1, vt, c); V3(G_SPRITE, X + rx - ux, Y + ry - uy, Z + rz - uz, u1, vb, c);
	V3(G_SPRITE, X - rx + ux, Y - ry + uy, Z - rz + uz, u0, vt, c); V3(G_SPRITE, X + rx - ux, Y + ry - uy, Z + rz - uz, u1, vb, c); V3(G_SPRITE, X - rx - ux, Y - ry - uy, Z - rz - uz, u0, vb, c);
	G_GLOW = 0;
}
// lies on the ground, rotated by heading, w across and l along
static void decal_c(int spr, float x, float y, float h, float heading, float w, float l, float cr, float cg, float cb, float alpha, bool lit)
{
	G_GLOW = lit ? 1.0f : 0.0f;
	float u0, u1, vt, vb; spr_uv(spr, &u0, &u1, &vt, &vb);
	Fr f = frame(x, y, heading);
	float p[4][2];
	fpt(&f, -w * 0.5f, -l * 0.5f, &p[0][0], &p[0][1]); fpt(&f, w * 0.5f, -l * 0.5f, &p[1][0], &p[1][1]);
	fpt(&f, w * 0.5f, l * 0.5f, &p[2][0], &p[2][1]); fpt(&f, -w * 0.5f, l * 0.5f, &p[3][0], &p[3][1]);
	uint32_t c = pack(cr, cg, cb, alpha, x, y);
	V(G_SPRITE, p[0][0], p[0][1], h, u0, vb, c); V(G_SPRITE, p[1][0], p[1][1], h, u1, vb, c); V(G_SPRITE, p[2][0], p[2][1], h, u1, vt, c);
	V(G_SPRITE, p[0][0], p[0][1], h, u0, vb, c); V(G_SPRITE, p[2][0], p[2][1], h, u1, vt, c); V(G_SPRITE, p[3][0], p[3][1], h, u0, vt, c);
	G_GLOW = 0;
}
static void decal(int spr, float x, float y, float h, float heading, float w, float l, float shade, float alpha)
{
	decal_c(spr, x, y, h, heading, w, l, shade, shade, shade, alpha, false);
}

// ---- the ground and the buildings ------------------------------------------------------------------------------------
static float ground_h(int id)
{
	if (id < 0) return 0;
	switch (tile_kind[id]) {
	case K_RAISED: return 1.6f;
	case K_BARRIER: return 4.0f;
	case K_WATER: return -1.4f;
	case K_STREAM: return -0.5f;
	default: return 0;
	}
}
static float tile_ground_h(int tx, int ty) { return ground_h(tile_at(tx, ty)); }

static void wall(int tx, int ty, int dir, float z0, float z1, int set)
{
	// dir: 0 north, 1 east, 2 south, 3 west. Split into 16 unit floors, each textured with a facade cell.
	static const float nxs[4] = {0, 1, 0, -1}, nys[4] = {-1, 0, 1, 0};
	float x0 = tx * (float)TILE, y0 = ty * (float)TILE, x1 = x0 + TILE, y1 = y0 + TILE;
	float ax, ay, bx, by;                                      // edge, running left-to-right as seen from outside
	switch (dir) {
	case 0: ax = x1; ay = y0; bx = x0; by = y0; break;
	case 1: ax = x1; ay = y1; bx = x1; by = y0; break;
	case 2: ax = x0; ay = y1; bx = x1; by = y1; break;
	default: ax = x0; ay = y0; bx = x0; by = y1; break;
	}
	float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
	if (!face_vis(nxs[dir], nys[dir], mx, my)) return;
	float sh = sunl(nxs[dir], nys[dir], 0);
	G_GLOW = NIGHT > 0.5f ? 0.6f : 0.0f;                        // lit windows glow through the dark
	for (int k = (int)(z0 / 16.0f); k * 16.0f < z1; k++) {
		float lo = fmaxf(z0, k * 16.0f), hi = fminf(z1, (k + 1) * 16.0f);
		if (hi - lo < 0.01f) continue;
		int cell = (NIGHT > 0.5f ? wallset_cell_night : wallset_cell)[set][k == 0 ? 0 : 1 + (int)(hash2(tx * 4 + dir, ty + k * 977) & 1)];
		float u0, u1, vt, vb; cell_uv(cell, &u0, &u1, &vt, &vb);
		float vlo = vb + (vt - vb) * ((lo - k * 16.0f) / 16.0f), vhi = vb + (vt - vb) * ((hi - k * 16.0f) / 16.0f);
		float shadeb = sh * (0.84f + 0.16f * (lo / (z1 > 1 ? z1 : 1)));           // a touch darker toward the street
		uint32_t cb = pack(shadeb, shadeb, shadeb, 1, mx, my), ct = pack(sh, sh, sh, 1, mx, my);
		V(G_OPAQUE, ax, ay, lo, u0, vlo, cb); V(G_OPAQUE, bx, by, lo, u1, vlo, cb); V(G_OPAQUE, bx, by, hi, u1, vhi, ct);
		V(G_OPAQUE, ax, ay, lo, u0, vlo, cb); V(G_OPAQUE, bx, by, hi, u1, vhi, ct); V(G_OPAQUE, ax, ay, hi, u0, vhi, ct);
	}
	G_GLOW = 0;
}

static void emit_building(int tx, int ty, int id)
{
	float H = (float)bh_at(tx, ty);
	if (H < 1) H = 32;
	float x0 = tx * (float)TILE, y0 = ty * (float)TILE, x1 = x0 + TILE, y1 = y0 + TILE;
	float cx = x0 + 8, cy = y0 + 8;
	float u0, u1, vt, vb; cell_uv(id, &u0, &u1, &vt, &vb);
	float ao = 0.96f;
	uint32_t c = pack(ao, ao, ao, 1, cx, cy);
	V(G_OPAQUE, x0, y0, H, u0, vt, c); V(G_OPAQUE, x1, y0, H, u1, vt, c); V(G_OPAQUE, x1, y1, H, u1, vb, c);
	V(G_OPAQUE, x0, y0, H, u0, vt, c); V(G_OPAQUE, x1, y1, H, u1, vb, c); V(G_OPAQUE, x0, y1, H, u0, vb, c);
	static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
	int set = tile_wallset[id]; if (set == 255) set = 0;
	for (int d = 0; d < 4; d++) {
		int nid = tile_at(tx + dx[d], ty + dy[d]);
		float nh = 0;
		if (is_building(nid)) {
			if (bgroup(nid) == bgroup(id)) continue;
			nh = (float)bh_at(tx + dx[d], ty + dy[d]);
			if (nh >= H) continue;
		} else nh = tile_ground_h(tx + dx[d], ty + dy[d]);
		wall(tx, ty, d, nh < 0 ? 0 : nh, H, set);
	}
}

static void emit_ground_tile(int tx, int ty, int id)
{
	int kind = tile_kind[id];
	float gh = ground_h(id);
	float x0 = tx * (float)TILE, y0 = ty * (float)TILE, x1 = x0 + TILE, y1 = y0 + TILE;
	int cw = W->w + 1;
	float s00 = shadow_map[ty * cw + tx] / 255.0f, s10 = shadow_map[ty * cw + tx + 1] / 255.0f;
	float s01 = shadow_map[(ty + 1) * cw + tx] / 255.0f, s11 = shadow_map[(ty + 1) * cw + tx + 1] / 255.0f;
#define SHD(s) (1.0f - (1.0f - (s)) * DAYF)
	s00 = SHD(s00); s10 = SHD(s10); s01 = SHD(s01); s11 = SHD(s11);
	if (kind == K_FERRIS) id = TILE_WOOD0;
	if (kind == K_WATER) {                                     // shimmer: swap between the two water variants over time
		int phase = (tx * 3 + ty * 5 + (int)(GG->time * 1.6f)) & 1;
		if (id == TILE_OCEAN0 || id == TILE_OCEAN1) id = TILE_OCEAN0 + phase;
		else if (id == TILE_WATER0 || id == TILE_WATER1) id = TILE_WATER0 + phase;
		s00 = s10 = s01 = s11 = 1.0f;
	}
	if (kind == K_SIGN) id = TILE_SCRUB0;
	float u0, u1, vt, vb; cell_uv(id, &u0, &u1, &vt, &vb);
	float mx = x0 + 8, my = y0 + 8;
	V(G_OPAQUE, x0, y0, gh, u0, vt, pack(s00, s00, s00, 1, mx, my)); V(G_OPAQUE, x1, y0, gh, u1, vt, pack(s10, s10, s10, 1, mx, my)); V(G_OPAQUE, x1, y1, gh, u1, vb, pack(s11, s11, s11, 1, mx, my));
	V(G_OPAQUE, x0, y0, gh, u0, vt, pack(s00, s00, s00, 1, mx, my)); V(G_OPAQUE, x1, y1, gh, u1, vb, pack(s11, s11, s11, 1, mx, my)); V(G_OPAQUE, x0, y1, gh, u0, vb, pack(s01, s01, s01, 1, mx, my));
	// sides where the neighbour lies lower (curbs, the barrier in the middle of the freeway, banks down to the water)
	static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
	static const float nxs[4] = {0, 1, 0, -1}, nys[4] = {-1, 0, 1, 0};
	for (int d = 0; d < 4; d++) {
		int nid = tile_at(tx + dx[d], ty + dy[d]);
		if (nid < 0 || is_building(nid)) continue;
		float nh = ground_h(nid);
		if (nh >= gh - 0.05f) continue;
		float ax, ay, bx, by;
		switch (d) { case 0: ax = x0; ay = y0; bx = x1; by = y0; break; case 1: ax = x1; ay = y0; bx = x1; by = y1; break; case 2: ax = x1; ay = y1; bx = x0; by = y1; break; default: ax = x0; ay = y1; bx = x0; by = y0; break; }
		float fx = (ax + bx) * 0.5f, fy = (ay + by) * 0.5f;
		if (!face_vis(nxs[d], nys[d], fx, fy)) continue;
		uint32_t rgb = (kind == K_RAISED || kind == K_BARRIER) ? 0xC8C4B8 : 0x7A6A4C;
		float sh = sunl(nxs[d], nys[d], 0) * (kind == K_BARRIER ? 1.0f : 0.92f);
		uint32_t c = rgbc(rgb, sh, 1, fx, fy);
		float p[4][3] = {{ax, ay, nh}, {bx, by, nh}, {bx, by, gh}, {ax, ay, gh}};
		cquad(G_OPAQUE, p, c, c, c, c);
	}
}

// ---- trees, bushes and the Hollywood sign ------------------------------------------------------------------------------
static void emit_tree(int tx, int ty, int kind)
{
	float x = tx * (float)TILE + 8, y = ty * (float)TILE + 8;
	unsigned h = hash2(tx, ty);
	Fr f = frame(x, y, (h & 255) * 0.0246f);
	if (kind == K_TREE) {
		float sc = 0.9f + (h % 5) * 0.06f;
		fbox(&f, 0, 0, 0, 2.4f, 2.4f, 9.0f * sc, 0x6E4E32, 0);
		cone(x, y, 6.0f * sc, 9.0f * sc, 15.0f * sc, 6.2f * sc, 5, (h & 1) ? 0x2C7438 : 0x347F3E, (h >> 3) * 0.1f);
		cone(x, y, 15.0f * sc, 6.2f * sc, 25.0f * sc, 0.0f, 5, (h & 2) ? 0x4E9A4A : 0x58A650, (h >> 3) * 0.1f);
	} else if (kind == K_PALM) {
		float lean = ((int)(h % 5) - 2) * 0.5f;
		fbox(&f, lean * 0.5f, 0, 0, 1.8f, 1.8f, 15.0f, 0x8A6A44, 0);
		fbox(&f, lean, 0, 15.0f, 1.8f, 1.8f, 7.0f, 0x8A6A44, 0);
		float tx_ = x + lean * 0.9f * f.rx, ty_ = y + lean * 0.9f * f.ry;
		for (int i = 0; i < 6; i++) {
			float a = (h & 7) * 0.3f + i * 1.0472f, ca = cosf(a), sa = sinf(a), px = -sa, py = ca;
			uint32_t c = rgbc((i & 1) ? 0x3C9B52 : 0x2F8A48, sunl(ca * 0.4f, sa * 0.4f, 0.9f), 1, tx_, ty_);
			ctri(G_OPAQUE, tx_ + px * 1.8f, ty_ + py * 1.8f, 22.5f, tx_ - px * 1.8f, ty_ - py * 1.8f, 22.5f, tx_ + ca * 8.0f, ty_ + sa * 8.0f, 24.0f, c);
			ctri(G_OPAQUE, tx_ + ca * 8.0f + px * 1.6f, ty_ + sa * 8.0f + py * 1.6f, 24.0f, tx_ + ca * 8.0f - px * 1.6f, ty_ + sa * 8.0f - py * 1.6f, 24.0f, tx_ + ca * 15.0f, ty_ + sa * 15.0f, 18.0f, c);
			ctri(G_OPAQUE, tx_ + px * 1.8f, ty_ + py * 1.8f, 22.5f, tx_ + ca * 8.0f, ty_ + sa * 8.0f, 24.0f, tx_ + ca * 8.0f + px * 1.6f, ty_ + sa * 8.0f + py * 1.6f, 24.0f, c);
		}
	} else {                                                   // bush
		cone(x, y, 0, 6.5f, 4.0f, 5.0f, 6, 0x2E7A3A, (h & 7) * 0.2f);
		cone(x, y, 4.0f, 5.0f, 6.6f, 0.0f, 6, 0x4A9B4A, (h & 7) * 0.2f);
	}
}

static void emit_sign(int tx, int ty, int id)
{
	int n = id - TILE_HSIGN_0_0, row = n / 18;
	float u0, u1, vt, vb; cell_uv(id, &u0, &u1, &vt, &vb);
	float yplane = (ty + (2 - row)) * (float)TILE + 8.0f, x0 = tx * (float)TILE, x1 = x0 + TILE;
	float z1 = (2 - row + 1) * 16.0f + 4.0f, z0 = z1 - 16.0f;
	float sh = 1.0f;
	uint32_t c = pack(sh, sh, sh, 1, x0 + 8, yplane);
	V(G_OPAQUE, x0, yplane, z0, u0, vb, c); V(G_OPAQUE, x1, yplane, z0, u1, vb, c); V(G_OPAQUE, x1, yplane, z1, u1, vt, c);
	V(G_OPAQUE, x0, yplane, z0, u0, vb, c); V(G_OPAQUE, x1, yplane, z1, u1, vt, c); V(G_OPAQUE, x0, yplane, z1, u0, vt, c);
}

// ---- people ----------------------------------------------------------------------------------------------------------
typedef struct { float len, w, h; uint32_t rgb; } GunDef;
static const GunDef guns[W_COUNT] = {
	{0, 0, 0, 0}, {5.0f, 1.5f, 2.0f, 0x34363E}, {7.0f, 1.8f, 2.4f, 0x2C2E34}, {11.0f, 1.6f, 1.8f, 0x4A4C54},
	{12.0f, 1.7f, 2.4f, 0x4A5238}, {11.0f, 2.4f, 3.0f, 0x2E3036}, {15.0f, 1.4f, 1.8f, 0x3C4430}, {12.0f, 3.0f, 3.0f, 0x58683C},
};
static void emit_gun(const Fr *f, int weapon, float lr, float z, bool flash)
{
	if (weapon <= 0 || weapon >= W_COUNT) return;
	const GunDef *g = &guns[weapon];
	fbox(f, lr, g->len * 0.5f + 1.5f, z, g->w, g->len, g->h, g->rgb, 0);
	if (weapon == W_SHOTGUN) fbox(f, lr, 3.0f, z - 0.3f, 1.8f, 4.0f, 2.2f, 0x8A5A32, 0);
	if (weapon == W_RIFLE) { fbox(f, lr, 4.0f, z - 1.8f, 1.2f, 2.0f, 2.0f, 0x222428, 0); fbox(f, lr, 2.0f, z - 0.2f, 1.9f, 4.0f, 2.2f, 0x8A5A32, 0); }
	if (weapon == W_MG) { fbox(f, lr + 2.2f, 5.0f, z - 0.4f, 2.0f, 3.2f, 2.6f, 0x58683C, 0); }
	if (weapon == W_SNIPER) { fbox(f, lr, 6.0f, z + g->h, 1.2f, 4.0f, 1.2f, 0x181A1E, 0); fbox(f, lr, 2.5f, z - 0.2f, 1.6f, 5.0f, 2.2f, 0x6E4A2C, 0); }
	if (weapon == W_RPG) { fbox(f, lr, 1.5f + g->len + 1.0f, z + 0.1f, 3.4f, 2.4f, 2.8f, 0xD23C32, 0); fbox(f, lr, 1.0f, z - 0.2f, 3.8f, 1.5f, 3.4f, 0x2A2C30, 0); }
	if (flash) {
		float tx, ty; fpt(f, lr * f->s, (g->len + 4.0f) * f->s, &tx, &ty);
		billboard(SPR_MUZZLE, tx, ty, (z + 1.0f) * f->s, 16.0f, 16.0f, 1.0f, 1.0f);
	}
}

static void emit_shadow(float x, float y, float heading, float w, float l, float lift, float alpha)
{
	decal(SPR_SHADOW, x + 0.6f * lift, y + 0.45f * lift, 0.18f, heading, w, l, 0.0f, alpha);
}

static void emit_person(float x, float y, float heading, float walkT, const uint32_t col[4], bool moving, int weapon, bool flash, bool hurt, bool bag)
{
	Fr f = frame(x, y, heading); f.s = 1.3f;
	float sw = moving ? sinf(walkT * 3.1416f) * 2.4f : 0.0f;
	uint32_t shirt = col[0], hair = col[1], skin = col[2], shoes = col[3], pants = 0x39405A;
	if (hurt) { shirt = 0xE04030; }
	emit_shadow(x, y, heading, 11.5f, 10.0f, 0, 0.5f);
	fbox(&f, -1.5f, sw, 0, 2.2f, 2.8f, 5.0f, pants, 0); fbox(&f, 1.5f, -sw, 0, 2.2f, 2.8f, 5.0f, pants, 0);
	fbox(&f, -1.5f, sw + 0.4f, 0, 2.3f, 3.4f, 1.3f, shoes, 0); fbox(&f, 1.5f, -sw + 0.4f, 0, 2.3f, 3.4f, 1.3f, shoes, 0);
	fbox(&f, 0, 0, 5.0f, 6.0f, 3.6f, 5.2f, shirt, 0);
	if (bag) fbox(&f, 0, -3.0f, 5.4f, 4.4f, 2.6f, 4.0f, 0xCEAA6E, 0);
	if (weapon > 0) {                                           // arms out in front, holding the weapon
		fbox(&f, -3.5f, 3.6f, 6.4f, 1.7f, 5.5f, 1.8f, shirt, 0); fbox(&f, 3.5f, 3.6f, 6.4f, 1.7f, 5.5f, 1.8f, shirt, 0);
		emit_gun(&f, weapon, 0, 6.6f, flash);
	} else {
		fbox(&f, -3.9f, -sw, 5.2f, 1.8f, 2.0f, 4.6f, shirt, 0); fbox(&f, 3.9f, sw, 5.2f, 1.8f, 2.0f, 4.6f, shirt, 0);
	}
	fbox(&f, 0, 0.2f, 10.2f, 3.8f, 3.8f, 3.6f, skin, 0);
	fbox(&f, 0, -0.1f, 13.4f, 4.2f, 4.2f, 1.6f, hair, 0);
}

static void emit_corpse(float x, float y, float heading, const uint32_t col[4])
{
	Fr f = frame(x, y, heading); f.s = 1.3f;
	fbox(&f, 0, 0, 0, 6.0f, 5.2f, 2.4f, col[0], 0);
	fbox(&f, -1.5f, -5.0f, 0, 2.2f, 4.8f, 2.0f, 0x39405A, 0); fbox(&f, 1.5f, -5.4f, 0, 2.2f, 4.8f, 2.0f, 0x39405A, 0);
	fbox(&f, 0, 4.2f, 0, 3.8f, 3.8f, 3.0f, col[2], 0);
}

// ---- vehicles --------------------------------------------------------------------------------------------------------
static void emit_car(const Car *c, float time)
{
	const CarDef *d = &car_defs[c->model];
	bool wreck = c->state == CS_WRECK;
	float L = d->len, Wd = d->wid;
	uint32_t body = wreck ? 0x242220 : carvar_rgb[c->variant], glass = wreck ? 0x1A1816 : 0x26385A;
	float bodyH = 4.6f, cabH = 3.6f;
	switch (c->model) { case M_COMPACT: bodyH = 4.4f; cabH = 3.8f; break; case M_SEDAN: case M_TAXI: case M_POLICE: bodyH = 4.8f; cabH = 3.6f; break;
	                    case M_SPORTS: bodyH = 3.8f; cabH = 3.0f; break; case M_VAN: bodyH = 10.0f; cabH = 0; break; case M_SWAT: bodyH = 9.0f; cabH = 0; break; }
	if (wreck) { bodyH *= 0.8f; cabH *= 0.7f; }
	Fr f = frame(c->x, c->y, c->heading);
	float z0 = 2.0f;
	emit_shadow(c->x, c->y, c->heading, Wd + 5.0f, L + 6.0f, 0, 0.55f);
	for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) fbox(&f, sx * (Wd * 0.5f - 0.4f), sy * L * 0.30f, 0, 2.2f, 4.6f, 4.6f, 0x16161A, 0);
	fbox(&f, 0, 0, z0, Wd, L * 0.97f, bodyH, body, 0);
	if (c->model == M_VAN || c->model == M_SWAT) {
		fbox(&f, 0, L * 0.5f - 1.0f, z0 + bodyH * 0.45f, Wd - 1.6f, 2.2f, bodyH * 0.4f, glass, 0);        // windscreen
		fbox(&f, 0, 0, z0 + bodyH, Wd - 1.0f, L * 0.7f, 0.5f, wreck ? 0x242220 : (c->model == M_SWAT ? 0x30343C : 0xC8C8CC), 0);
	} else {
		fbox(&f, 0, -L * 0.07f, z0 + bodyH, Wd - 2.2f, L * 0.52f, cabH, glass, 0);
		fbox(&f, 0, -L * 0.07f, z0 + bodyH + cabH, Wd - 2.8f, L * 0.47f, 0.8f, body, 0);
		if (c->model == M_SPORTS && !wreck) { fbox(&f, -1.4f, 0, z0 + bodyH + 0.02f, 1.2f, L * 0.9f, 0.05f, 0xF0F0F4, 0); fbox(&f, 1.4f, 0, z0 + bodyH + 0.02f, 1.2f, L * 0.9f, 0.05f, 0xF0F0F4, 0); }
	}
	if (!wreck && NIGHT > 0.05f) {                              // headlight beams on the road
		float hx, hy; fpt(&f, 0, L * 0.5f + 17.0f, &hx, &hy);
		decal_c(SPR_GLOW_WHITE, hx, hy, 0.2f, c->heading, Wd + 6.0f, 44.0f, 1.0f, 0.96f, 0.78f, 0.5f * NIGHT, true);
	}
	if (!wreck) {
		for (int s = -1; s <= 1; s += 2) {
			fbox(&f, s * (Wd * 0.5f - 2.0f), L * 0.5f - 0.2f, z0 + 1.2f, 2.4f, 0.8f, 1.5f, 0xFFF4B4, 1.0f);        // headlights
			fbox(&f, s * (Wd * 0.5f - 2.0f), -L * 0.5f + 0.2f, z0 + 1.2f, 2.4f, 0.8f, 1.5f, 0xE02A2A, 0.8f);       // tail lights
		}
	}
	float roof = z0 + bodyH + cabH + 0.8f;
	if (c->model == M_TAXI && !wreck) fbox(&f, 0, -L * 0.07f, roof, 4.4f, 2.0f, 1.4f, 0xFFF6C0, 0.7f);
	if (c->model == M_POLICE && !wreck) {
		fbox(&f, -Wd * 0.5f - 0.05f, 0, z0 + 0.4f, 0.3f, L * 0.4f, bodyH - 0.8f, 0xEEEEF2, 0); fbox(&f, Wd * 0.5f + 0.05f, 0, z0 + 0.4f, 0.3f, L * 0.4f, bodyH - 0.8f, 0xEEEEF2, 0);
	}
	if ((c->model == M_POLICE || c->model == M_SWAT) && !wreck) {
		bool on = c->state == CS_CHASE || c->sirenT > 0;
		bool phase = ((int)(time * 7.0f + c->x * 0.01f) & 1) != 0;
		float lz = c->model == M_SWAT ? z0 + bodyH + 0.5f : roof;
		fbox(&f, -2.3f, -L * 0.07f, lz, 3.6f, 2.4f, 1.3f, on && phase ? 0xFF3030 : 0x6A1A1A, on && phase ? 1.0f : 0.2f);
		fbox(&f, 2.3f, -L * 0.07f, lz, 3.6f, 2.4f, 1.3f, on && !phase ? 0x3C6CFF : 0x1A2A6A, on && !phase ? 1.0f : 0.2f);
		if (on) {
			float gx, gy; fpt(&f, phase ? -2.3f : 2.3f, -L * 0.07f, &gx, &gy);
			billboard(phase ? SPR_GLOW_RED : SPR_GLOW_BLUE, gx, gy, lz + 2.5f, 22.0f, 22.0f, 1.0f, 0.9f);
		}
	}
	if (wreck && c->burnT > 0) { /* flames come from the particle system */ }
}

static void emit_heli(const Car *c, float time)
{
	bool wreck = c->state == CS_WRECK;
	float alt = c->alt;
	float z = 2.0f + alt * 56.0f;
	uint32_t body = wreck ? 0x242220 : 0xD6DAE2, glass = wreck ? 0x1A1816 : 0x2E5A8C;
	Fr f = frame(c->x, c->y, c->heading);
	emit_shadow(c->x, c->y, c->heading, 17.0f + alt * 7.0f, 22.0f + alt * 6.0f, alt * 56.0f, 0.5f - alt * 0.22f);
	float zs = z - 1.6f;
	for (int s = -1; s <= 1; s += 2) {
		fbox(&f, s * 5.0f, 0.5f, zs, 0.9f, 15.0f, 0.8f, 0x1A1A1E, 0);
		fbox(&f, s * 4.4f, 4.0f, zs + 0.8f, 0.7f, 0.7f, 1.8f, 0x1A1A1E, 0); fbox(&f, s * 4.4f, -3.0f, zs + 0.8f, 0.7f, 0.7f, 1.8f, 0x1A1A1E, 0);
	}
	fbox(&f, 0, 0, z, 6.8f, 14.0f, 6.0f, body, 0);
	fbox(&f, 0, 5.4f, z + 1.2f, 6.2f, 5.4f, 4.2f, glass, 0);
	fbox(&f, 0, 5.4f, z + 5.4f, 5.6f, 4.0f, 0.5f, body, 0);
	if (!wreck) fbox(&f, 0, -1.0f, z + 1.6f, 7.0f, 1.6f, 1.2f, 0xD23838, 0);
	fbox(&f, 0, -11.0f, z + 3.2f, 2.0f, 12.0f, 2.2f, wreck ? 0x242220 : 0xC0C4CC, 0);
	fbox(&f, 0, -16.0f, z + 3.4f, 0.6f, 3.6f, 5.0f, wreck ? 0x242220 : 0xD23838, 0);
	fbox(&f, 0, -0.5f, z + 6.0f, 1.6f, 1.6f, 1.4f, 0x303036, 0);
	float ang = c->state == CS_PLAYER ? time * 38.0f : c->heading + 0.6f;
	Fr r1 = frame(c->x, c->y, ang), r2 = frame(c->x, c->y, ang + 1.5708f);
	float zr = z + 7.2f;
	if (!wreck) {
		fbox(&r1, 0, 0, zr, 1.8f, 36.0f, 0.35f, 0x24262C, 0); fbox(&r2, 0, 0, zr, 1.8f, 36.0f, 0.35f, 0x24262C, 0);
		if (c->state == CS_PLAYER) {                             // the blur of the spinning disc
			int n = 12; uint32_t cc = pack(0.85f, 0.88f, 0.92f, 0.16f, c->x, c->y);
			float u, v; white_uv(&u, &v);
			for (int i = 0; i < n; i++) {
				float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
				V(G_ALPHA, c->x, c->y, zr + 0.2f, u, v, cc); V(G_ALPHA, c->x + cosf(a0) * 18.5f, c->y + sinf(a0) * 18.5f, zr + 0.2f, u, v, cc); V(G_ALPHA, c->x + cosf(a1) * 18.5f, c->y + sinf(a1) * 18.5f, zr + 0.2f, u, v, cc);
			}
		}
	}
}

// ---- street furniture, the Ferris wheel, rooftop clutter --------------------------------------------------------------
static void emit_lamp(float x, float y, float tx, float ty)       // (tx, ty): unit vector pointing at the road
{
	Fr f = frame(x, y, atan2f(tx, -ty));
	fbox(&f, 0, 0, 0, 1.5f, 1.5f, 23.0f, 0x3C3E46, 0);
	fbox(&f, 0, 2.6f, 22.0f, 1.1f, 5.6f, 1.1f, 0x3C3E46, 0);
	float hx, hy; fpt(&f, 0, 5.4f, &hx, &hy);
	fbox(&f, 0, 5.4f, 21.0f, 2.6f, 3.6f, 1.3f, NIGHT > 0.4f ? 0xFFEBB0 : 0xCFCBB8, NIGHT > 0.4f ? 1.0f : 0.0f);
	if (NIGHT > 0.05f) {
		decal_c(SPR_GLOW_WHITE, hx, hy, 0.25f, 0, 56.0f, 56.0f, 1.0f, 0.88f, 0.55f, 0.6f * NIGHT, true);
		billboard(SPR_GLOW_WHITE, hx, hy, 19.0f, 15.0f, 15.0f, 1.0f, 0.85f * NIGHT);
	}
}

static void emit_ferris(float cx, float cy, float time)
{
	const float R = 27.0f, hub = 34.0f, ang = time * 0.3f;
	uint32_t rim = rgbc(0xD8DCE6, 1.0f, 1, cx, cy), spoke = rgbc(0xA8AEBC, 0.95f, 1, cx, cy), leg = rgbc(0x8A90A0, 0.9f, 1, cx, cy);
	for (int s = -1; s <= 1; s += 2) {
		float p[4][3] = {{cx - s * 1.2f, cy, hub}, {cx + s * 1.2f, cy, hub}, {cx + s * 17.0f + 1.4f, cy, 0}, {cx + s * 17.0f - 1.4f, cy, 0}};
		cquad(G_OPAQUE, p, leg, leg, leg, leg);
	}
	for (int ring = 0; ring < 2; ring++) {
		float ro = ring ? R * 0.5f : R, ri = ro - (ring ? 1.0f : 1.6f);
		for (int i = 0; i < 24; i++) {
			float a0 = i * 0.2618f, a1 = (i + 1) * 0.2618f;
			float p[4][3] = {{cx + cosf(a0) * ro, cy, hub + sinf(a0) * ro}, {cx + cosf(a1) * ro, cy, hub + sinf(a1) * ro}, {cx + cosf(a1) * ri, cy, hub + sinf(a1) * ri}, {cx + cosf(a0) * ri, cy, hub + sinf(a0) * ri}};
			cquad(G_OPAQUE, p, rim, rim, rim, rim);
		}
	}
	for (int i = 0; i < 12; i++) {
		float a = ang + i * 0.5236f, ca = cosf(a), sa = sinf(a);
		float p[4][3] = {{cx - sa * 0.5f, cy, hub + ca * 0.5f}, {cx + sa * 0.5f, cy, hub - ca * 0.5f}, {cx + ca * R + sa * 0.5f, cy, hub + sa * R - ca * 0.5f}, {cx + ca * R - sa * 0.5f, cy, hub + sa * R + ca * 0.5f}};
		cquad(G_OPAQUE, p, spoke, spoke, spoke, spoke);
		static const uint32_t gc[4] = {0xEC4646, 0xFAC83C, 0x46AAF0, 0x6ED278};
		Fr f = frame(cx + ca * R, cy, 0);
		fbox(&f, 0, 0, hub + sa * R - 6.0f, 4.4f, 3.4f, 4.6f, gc[i & 3], NIGHT > 0.4f ? 0.5f : 0.0f);
	}
	Fr f = frame(cx, cy, 0);
	fbox(&f, 0, 0, hub - 2.0f, 3.4f, 3.4f, 4.0f, 0x6E7482, 0);
}

static void emit_roof_props(int tx, int ty, int kind, float H, float time)
{
	unsigned h = hash2(tx * 3 + 11, ty * 7 + 5);
	float x = tx * (float)TILE + 8, y = ty * (float)TILE + 8;
	if (kind == K_BUILDING && h % 5 == 0) {
		Fr f = frame(x + ((int)(h >> 8) % 5 - 2), y + ((int)(h >> 12) % 5 - 2), (h >> 4) * 0.01f);
		fbox(&f, 0, 0, H, 6.0f, 5.0f, 2.6f, 0xB4B8C0, 0);
		fbox(&f, 0, 0, H + 2.6f, 4.4f, 3.6f, 0.4f, 0x44464E, 0);
	} else if (kind == K_TOWER && h % 6 == 0) {
		Fr f = frame(x, y, 0);
		fbox(&f, 0, 0, H, 5.0f, 5.0f, 4.0f, 0x6E7480, 0);
		fbox(&f, 0, 0, H + 4.0f, 0.9f, 0.9f, 20.0f, 0x8C2020, 0);
		float blink = 0.5f + 0.5f * sinf(time * 4.0f + h);
		billboard(SPR_GLOW_RED, x, y, H + 25.0f, 9.0f, 9.0f, 1.0f, (0.25f + 0.75f * NIGHT) * blink);
	}
}

// ---- the whole frame ---------------------------------------------------------------------------------------------------
static void norm3(float *v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; } }

static void camera(const Game *g, R3DScene *s)
{
	float yaw = g->camyaw;
	{   // time of day: a full day and night every eight minutes, starting in the morning
		float tod = fmodf(0.33f + g->time / 480.0f, 1.0f);
		float e = sinf(6.2831853f * (tod - 0.25f));
		float t = (e + 0.12f) / 0.42f; t = t < 0 ? 0 : t > 1 ? 1 : t;
		DAYF = t * t * (3.0f - 2.0f * t); NIGHT = 1.0f - DAYF;
		float warm = 1.0f - fabsf(e) * 3.2f; warm = warm < 0 ? 0 : warm;
		static const float nt[3] = {0.34f, 0.40f, 0.64f}, dt_[3] = {1.0f, 0.99f, 0.95f}, ns[3] = {0.03f, 0.05f, 0.12f}, ds[3] = {0.62f, 0.72f, 0.86f}, dusk[3] = {0.93f, 0.52f, 0.36f};
		for (int k = 0; k < 3; k++) { TINT[k] = nt[k] + (dt_[k] - nt[k]) * DAYF; SKY[k] = ns[k] + (ds[k] - ns[k]) * DAYF; SKY[k] += (dusk[k] - SKY[k]) * warm * 0.65f; }
		TINT[0] *= 1.0f + 0.10f * warm; TINT[1] *= 1.0f - 0.12f * warm; TINT[2] *= 1.0f - 0.32f * warm;
	}
	FOCX = g->camx + SCREEN_W * 0.5f; FOCY = g->camy + SCREEN_H * 0.5f;
	float back = 112.0f, height = 158.0f;
	if (g->p.car >= 0 && g->cars[g->p.car].model == M_HELI) { height += 70.0f * g->cars[g->p.car].alt; back += 30.0f * g->cars[g->p.car].alt; }
	EX = FOCX - sinf(yaw) * back; EZ = FOCY + cosf(yaw) * back; EH = height;
	float t[3] = {FOCX, 0.0f, FOCY};
	CF[0] = t[0] - EX; CF[1] = t[1] - EH; CF[2] = t[2] - EZ; norm3(CF);
	float up[3] = {0, 1, 0};
	CR[0] = CF[1] * up[2] - CF[2] * up[1]; CR[1] = CF[2] * up[0] - CF[0] * up[2]; CR[2] = CF[0] * up[1] - CF[1] * up[0]; norm3(CR);
	CU[0] = CR[1] * CF[2] - CR[2] * CF[1]; CU[1] = CR[2] * CF[0] - CR[0] * CF[2]; CU[2] = CR[0] * CF[1] - CR[1] * CF[0];
	s->eye[0] = EX; s->eye[1] = EH; s->eye[2] = EZ; s->target[0] = t[0]; s->target[1] = t[1]; s->target[2] = t[2];
	s->fov_deg = 48.0f; s->focus = sqrtf(back * back + height * height);
	float *v = s->view;
	float e[3] = {EX, EH, EZ};
	v[0] = CR[0]; v[1] = CR[1]; v[2] = CR[2]; v[3] = -(CR[0] * e[0] + CR[1] * e[1] + CR[2] * e[2]);
	v[4] = CU[0]; v[5] = CU[1]; v[6] = CU[2]; v[7] = -(CU[0] * e[0] + CU[1] * e[1] + CU[2] * e[2]);
	v[8] = -CF[0]; v[9] = -CF[1]; v[10] = -CF[2]; v[11] = (CF[0] * e[0] + CF[1] * e[1] + CF[2] * e[2]);
	v[12] = 0; v[13] = 0; v[14] = 0; v[15] = 1;
	TY = tanf(s->fov_deg * 0.5f * 3.14159265f / 180.0f) * 1.08f; TX = TY * (SCREEN_W / SCREEN_H);
	s->sky[0] = SKY[0]; s->sky[1] = SKY[1]; s->sky[2] = SKY[2];
}

void r3d_build(const Game *g, R3DScene *s)
{
	S = s; GG = g; W = &g->world;
	gcap[G_OPAQUE] = CAP_OPAQUE; gcap[G_ALPHA] = CAP_ALPHA; gcap[G_SPRITE] = CAP_SPRITE;
	s->first[G_OPAQUE] = 0; s->first[G_ALPHA] = CAP_OPAQUE; s->first[G_SPRITE] = CAP_OPAQUE + CAP_ALPHA;
	for (int i = 0; i < G_COUNT; i++) { gp[i] = s->buf + s->first[i]; gn[i] = 0; }
	camera(g, s);

	// terrain and buildings
	int cx = (int)floorf(FOCX / TILE), cy = (int)floorf(FOCY / TILE);
	const int R = 17;
	for (int ty = cy - R; ty <= cy + R; ty++) {
		for (int tx = cx - R; tx <= cx + R; tx++) {
			int id = tile_at(tx, ty);
			if (id < 0) continue;
			float px = tx * (float)TILE + 8, py = ty * (float)TILE + 8;
			int kind = tile_kind[id];
			if (kind == K_BUILDING || kind == K_TOWER) {
				float H = (float)bh_at(tx, ty);
				if (!visible(px, py, H * 0.5f, 12.0f + H * 0.5f)) continue;
				emit_building(tx, ty, id);
				emit_roof_props(tx, ty, kind, H < 1 ? 32.0f : H, g->time);
				continue;
			}
			if (!visible(px, py, 6.0f, 22.0f)) continue;
			emit_ground_tile(tx, ty, id);
			if (kind == K_TREE || kind == K_PALM || kind == K_BUSH) emit_tree(tx, ty, kind);
			else if (kind == K_SIGN) emit_sign(tx, ty, id);
			else if (id == TILE_FERRIS_5) emit_ferris(tx * (float)TILE + 16.0f, ty * (float)TILE + 16.0f, g->time);
			else if (id == TILE_SIDEWALK0 || id == TILE_SIDEWALK1) {
				static const int ldx[4] = {0, 1, 0, -1}, ldy[4] = {-1, 0, 1, 0};
				int road[4];
				for (int d = 0; d < 4; d++) { int nid = tile_at(tx + ldx[d], ty + ldy[d]); road[d] = nid >= 0 && (tile_flags[nid] & F_ROAD); }
				bool ns = road[0] || road[2], ew = road[1] || road[3];
				if (ns != ew && ((ns && tx % 6 == 2) || (ew && ty % 6 == 2))) {
					int d = road[0] ? 0 : road[2] ? 2 : road[1] ? 1 : 3;
					emit_lamp(px + ldx[d] * 5.0f, py + ldy[d] * 5.0f, (float)ldx[d], (float)ldy[d]);
				}
			}
		}
	}

	// blood and scorch marks on the ground
	int ns = g->stain_n < MAX_STAINS ? g->stain_n : MAX_STAINS;
	for (int i = 0; i < ns; i++) {
		const Stain *st = &g->stains[i];
		if (!visible(st->x, st->y, 0, 14)) continue;
		const SprRect *r = &spr_rects[SPR_STAIN0 + st->img];
		decal(SPR_STAIN0 + st->img, st->x, st->y, 0.1f + 0.01f * (i % 7), 0.0f, r->w, r->h, 1.0f, 0.95f);
	}

	// pickups: spinning crates with the item floating above
	for (int i = 0; i < MAX_PICKUPS; i++) {
		const Pickup *k = &g->pickups[i];
		if (!k->active || !visible(k->x, k->y, 6, 14)) continue;
		float bob = 5.5f + sinf(g->time * 3.0f + i) * 1.4f;
		static const uint32_t pc[11] = {0xE8F0E8, 0x4C82F0, 0xF2DC78, 0xF2DC78, 0xF2DC78, 0x60C060, 0xF2DC78, 0xF2DC78, 0xF2DC78, 0xF2DC78, 0x6E7E4A};
		Fr f = frame(k->x, k->y, g->time * 2.0f + i);
		fbox(&f, 0, 0, bob, 5.5f, 5.5f, 5.5f, pc[k->kind > 10 ? 5 : k->kind], 0.25f);
		static const int sp[11] = {SPR_PK_HEALTH, SPR_PK_ARMOR, SPR_PK_PISTOL, SPR_PK_SMG, SPR_PK_SHOTGUN, SPR_PK_CASH, SPR_PK_RIFLE, SPR_PK_MG, SPR_PK_SNIPER, SPR_PK_RPG, SPR_PK_AMMO};
		billboard(sp[k->kind > 10 ? 5 : k->kind], k->x, k->y, bob + 12.0f, 14.0f, 14.0f, 1.0f, 1.0f);
		emit_shadow(k->x, k->y, 0, 8.0f, 8.0f, bob, 0.4f);
	}

	// people
	for (int i = 0; i < MAX_PEDS; i++) {
		const Ped *p = &g->peds[i];
		if (!p->active || !visible(p->x, p->y, 6, 14)) continue;
		if (p->state == PS_DEAD) { emit_corpse(p->x, p->y, p->heading + (i & 3) * 1.1f, ped_rgb[p->type]); continue; }
		bool armed = p->kind != PK_CIV && p->state == PS_ATTACK;
		bool flash = armed && p->fireT > 0.0f && p->fireT < 0.08f;
		float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
		emit_person(p->x, p->y, p->heading, p->walkT, ped_rgb[p->type], speed > 8.0f, armed ? (p->kind == PK_SWAT ? W_RIFLE : W_PISTOL) : 0, flash, false, false);
	}
	// Otto
	if (g->p.car < 0) {
		const Player *p = &g->p;
		static const uint32_t otto[4] = {0xF07A20, 0x1E1E26, 0xEEBE96, 0x1A1A1E};
		if (p->status == PL_DEAD) emit_corpse(p->x, p->y, p->heading, otto);
		else emit_person(p->x, p->y, p->heading, p->walkT, otto, p->moving, p->weapon, p->fireT > weapon_defs[p->weapon].rate * 0.55f, p->hurtT > 0, true);
	}

	// vehicles
	for (int i = 0; i < MAX_CARS; i++) {
		const Car *c = &g->cars[i];
		if (!c->active || !visible(c->x, c->y, 8, 40)) continue;
		if (c->model == M_HELI) emit_heli(c, g->time); else emit_car(c, g->time);
	}

	// bullets, rockets
	for (int i = 0; i < MAX_BULLETS; i++) {
		const Bullet *b = &g->bullets[i];
		if (!b->active) continue;
		float ang = atan2f(b->vx, -b->vy);
		Fr f = frame(b->x, b->y, ang);
		if (b->rocket) {
			fbox(&f, 0, 0, 5.5f, 2.0f, 8.0f, 2.0f, 0x484E40, 0); fbox(&f, 0, 4.5f, 5.5f, 2.0f, 1.8f, 2.0f, 0xD23C32, 0);
			float tx, ty; fpt(&f, 0, -6.0f, &tx, &ty);
			billboard(SPR_FIRE0 + ((int)(g->time * 20.0f) % 3), tx, ty, 6.5f, 9.0f, 11.0f, 1.0f, 1.0f);
		} else fbox(&f, 0, 0, 5.5f, 0.9f, 7.0f, 0.9f, 0xFFEA9A, 1.0f);
	}

	// particles
	for (int i = 0; i < MAX_PARTS; i++) {
		const Particle *p = &g->parts[i];
		if (!p->active || !visible(p->x, p->y, 8, 30)) continue;
		float lr = p->life / (p->maxlife > 0.001f ? p->maxlife : 1.0f), age = 1.0f - lr;
		switch (p->type) {
		case P_BLOOD: billboard(SPR_BLOOD_DOT, p->x, p->y, 1.5f + 5.0f * sinf(3.1416f * age), 3.2f, 3.2f, 1.0f, 1.0f); break;
		case P_SMOKE: billboard(SPR_SMOKE0 + (p->frame % 3), p->x, p->y, 5.0f + age * 20.0f, 8.0f + age * 12.0f, 8.0f + age * 12.0f, 1.0f, 0.85f * lr + 0.1f); break;
		case P_FIRE:  billboard(SPR_FIRE0 + (p->frame % 3), p->x, p->y, 4.0f + age * 6.0f, 10.0f, 12.0f, 1.0f, 1.0f); break;
		case P_SPARK: billboard(SPR_SPARK, p->x, p->y, 6.0f, 6.0f, 6.0f, 1.0f, 1.0f); break;
		case P_MUZZLE: billboard(SPR_MUZZLE, p->x, p->y, 7.0f, 16.0f, 16.0f, 1.0f, 1.0f); break;
		case P_BOOM: { int fr = p->frame > 5 ? 5 : p->frame; const SprRect *r = &spr_rects[SPR_BOOM0 + fr]; billboard(SPR_BOOM0 + fr, p->x, p->y, 12.0f + age * 6.0f, r->w * 1.25f, r->h * 1.25f, 1.0f, 1.0f); break; }
		}
	}
	s->count[G_OPAQUE] = gn[G_OPAQUE]; s->count[G_ALPHA] = gn[G_ALPHA]; s->count[G_SPRITE] = gn[G_SPRITE];
}
