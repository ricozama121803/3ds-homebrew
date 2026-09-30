#include "render.h"
#include "gen_map.h"

#ifdef HOST_PREVIEW
#include "../host/c2d_shim.h"
#define IMG_W(i) ((i).width)
#define IMG_H(i) ((i).height)
#else
#include <3ds.h>
#include <citro2d.h>
#include "atlas_t3x.h"                     // texture atlas built from assets/gfx (see Makefile)
#define IMG_W(i) ((int)(i).subtex->width)
#define IMG_H(i) ((int)(i).subtex->height)
#endif

#include <math.h>
#include <stdio.h>

#define RGBA(r, g, b, a) C2D_Color32((r), (g), (b), (a))
#define RGB(r, g, b)     C2D_Color32((r), (g), (b), 255)
#define ORANGE RGB(246, 128, 30)
#define NAVY   RGB(22, 64, 124)
#define BLOOD  RGB(210, 20, 30)

static C2D_Image im[IMG_COUNT];
#ifndef HOST_PREVIEW
static C2D_SpriteSheet s_sheet;
#endif

static C2D_TextBuf s_static, s_dyn;
static C2D_Text t_zone[NZONES], t_land[NLANDMARKS];
static C2D_Text t_wname[W_COUNT], t_cashv, t_ammo[W_COUNT], t_pop[MAX_POPUPS];
static C2D_Text t_cash, t_wanted, t_paused, t_resume, t_quit, t_wasted, t_busted, t_car;
static C2D_Text t_foot[4], t_drive[4];

void render_init(void)
{
	s_static = C2D_TextBufNew(2048);
	s_dyn    = C2D_TextBufNew(1024);
#define STATIC_TEXT(t, s) do { C2D_TextParse(&(t), s_static, (s)); C2D_TextOptimize(&(t)); } while (0)
	for (int i = 0; i < NZONES; i++)     { C2D_TextParse(&t_zone[i], s_static, zones[i].name); C2D_TextOptimize(&t_zone[i]); }
	for (int i = 0; i < NLANDMARKS; i++) { C2D_TextParse(&t_land[i], s_static, landmarks[i].name); C2D_TextOptimize(&t_land[i]); }
	for (int i = 0; i < W_COUNT; i++)    STATIC_TEXT(t_wname[i], weapon_defs[i].name);
	STATIC_TEXT(t_cash, "CASH");  STATIC_TEXT(t_wanted, "WANTED"); STATIC_TEXT(t_car, "CAR");
	STATIC_TEXT(t_paused, "PAUSED"); STATIC_TEXT(t_resume, "START: resume"); STATIC_TEXT(t_quit, "SELECT: quit game");
	STATIC_TEXT(t_wasted, "WASTED"); STATIC_TEXT(t_busted, "BUSTED");
	STATIC_TEXT(t_foot[0], "A: fire");  STATIC_TEXT(t_foot[1], "Y: enter car"); STATIC_TEXT(t_foot[2], "X / L: weapon"); STATIC_TEXT(t_foot[3], "R: run");
	STATIC_TEXT(t_drive[0], "A: gas"); STATIC_TEXT(t_drive[1], "B: brake"); STATIC_TEXT(t_drive[2], "L: handbrake"); STATIC_TEXT(t_drive[3], "Y: get out");

#ifdef HOST_PREVIEW
	for (int i = 0; i < IMG_COUNT; i++) {
		char path[512];
		snprintf(path, sizeof path, "%s/%s.rgba", shim_gfxdir, atlas_names[i]);
		im[i] = shim_load_image(path);
	}
#else
	s_sheet = C2D_SpriteSheetLoadFromMem(atlas_t3x, atlas_t3x_size);
	for (int i = 0; i < IMG_COUNT; i++) im[i] = C2D_SpriteSheetGetImage(s_sheet, i);
#endif
}

void render_exit(void)
{
	C2D_TextBufDelete(s_static);
	C2D_TextBufDelete(s_dyn);
#ifndef HOST_PREVIEW
	C2D_SpriteSheetFree(s_sheet);
#endif
}

void render_prepare(const Game *g)
{
	char buf[48];
	C2D_TextBufClear(s_dyn);
	snprintf(buf, sizeof buf, "$%d", g->p.cash); C2D_TextParse(&t_cashv, s_dyn, buf); C2D_TextOptimize(&t_cashv);
	for (int i = 0; i < W_COUNT; i++) {
		if (i == W_FISTS) snprintf(buf, sizeof buf, "-"); else snprintf(buf, sizeof buf, "%d", g->p.ammo[i]);
		C2D_TextParse(&t_ammo[i], s_dyn, buf); C2D_TextOptimize(&t_ammo[i]);
	}
	for (int i = 0; i < MAX_POPUPS; i++) {
		if (g->popups[i].t <= 0) continue;
		C2D_TextParse(&t_pop[i], s_dyn, g->popups[i].text); C2D_TextOptimize(&t_pop[i]);
	}
}

// ---- helpers -----------------------------------------------------------
static void draw(int id, float x, float y) { C2D_DrawImageAt(im[id], x, y, 0, NULL, 1.0f, 1.0f); }
static void draw_c(int id, float cx, float cy) { C2D_DrawImageAt(im[id], floorf(cx - IMG_W(im[id]) * 0.5f), floorf(cy - IMG_H(im[id]) * 0.5f), 0, NULL, 1.0f, 1.0f); }
static void draw_tint(int id, float x, float y, u32 color, float blend)
{
	C2D_ImageTint t;
	C2D_PlainImageTint(&t, color, blend);
	C2D_DrawImageAt(im[id], x, y, 0, &t, 1.0f, 1.0f);
}

static void text(const C2D_Text *t, float x, float y, float sc, u32 col, bool center)
{
	u32 flags = C2D_WithColor | (center ? C2D_AlignCenter : 0);
	C2D_DrawText(t, flags, x + 1.0f, y + 1.0f, 0, sc, sc, RGBA(0, 0, 0, 200));
	C2D_DrawText(t, flags, x, y, 0, sc, sc, col);
}

// which "building" a roof tile belongs to, so an outline is drawn wherever two different buildings touch
static int roof_group(int id)
{
	if (id >= TILE_ROOF_GRAY0 && id <= TILE_ROOF_ORANGE3) return TILE_ROOF_GRAY0 + ((id - TILE_ROOF_GRAY0) >> 2) * 4;
	if (id >= TILE_HOSP_0 && id <= TILE_HOSP_8) return TILE_ROOF_WHITE0;
	if (id >= TILE_TOWER_GLASS0 && id <= TILE_TOWER_GLASS1) return TILE_TOWER_GLASS0;
	if (id >= TILE_TOWER_DARK0 && id <= TILE_TOWER_DARK1) return TILE_TOWER_DARK0;
	if (id >= TILE_TOWER_STONE0 && id <= TILE_TOWER_STONE1) return TILE_TOWER_STONE0;
	if (id >= TILE_OBS_0 && id <= TILE_OBS_8) return TILE_OBS_0;
	if (id >= TILE_FERRIS_0 && id <= TILE_FERRIS_15) return TILE_FERRIS_0;
	return id;
}
static bool same_building(const World *w, int gx, int gy, int group)
{
	int id = world_tile(w, gx, gy);
	return id >= 0 && (tile_flags[id] & F_BUILDING) && roof_group(id) == group;
}

static int canopy_for(int tile)
{
	if (tile == TILE_PALM) return IMG_PALMTOP;
	return tile == TILE_TREE1 ? IMG_CANOPY1 : IMG_CANOPY0;
}

static int dir_index(float heading, int n)
{
	int d = (int)lroundf(heading / (2.0f * PI_F / n));
	return ((d % n) + n) % n;
}

// ---- entities ----------------------------------------------------------
static void draw_car(const Game *g, const Car *c, int camx, int camy)
{
	float sx = floorf(c->x) - camx, sy = floorf(c->y) - camy;
	if (sx < -40 || sx > SCREEN_W + 40 || sy < -40 || sy > SCREEN_H + 40) return;
	int id = IMG_CAR(c->variant, dir_index(c->heading, DIRS));
	float x = floorf(sx - IMG_W(im[id]) * 0.5f), y = floorf(sy - IMG_H(im[id]) * 0.5f);
	if (c->state == CS_WRECK) draw_tint(id, x, y, RGB(22, 20, 18), 0.72f);
	else draw(id, x, y);

	// police light bars flash while they're working
	if ((c->model == M_POLICE || c->model == M_SWAT) && (c->state == CS_CHASE || c->sirenT > 0) && c->state != CS_WRECK) {
		bool phase = ((int)(g->time * 7.0f + c->x * 0.01f) & 1) != 0;
		float rx = cosf(c->heading) * 3.6f, ry = sinf(c->heading) * 3.6f;
		draw_c(phase ? IMG_GLOW_RED : IMG_GLOW_BLUE, sx - rx, sy - ry);
		draw_c(phase ? IMG_GLOW_BLUE : IMG_GLOW_RED, sx + rx, sy + ry);
	}
}

static void draw_ped(const Game *g, const Ped *p, int camx, int camy)
{
	float sx = floorf(p->x) - camx, sy = floorf(p->y) - camy;
	if (sx < -20 || sx > SCREEN_W + 20 || sy < -20 || sy > SCREEN_H + 20) return;
	if (p->state == PS_DEAD) {
		int k = ((int)(p->x * 7.0f + p->y * 3.0f)) & 3;
		draw_c(IMG_CORPSE(p->type, k), sx, sy);
		return;
	}
	draw_c(IMG_SHADOW, sx, sy + 3);
	int id = IMG_PED(p->type, ((int)p->walkT) & 1, dir_index(p->heading, PED_DIRS));
	draw_c(id, sx, sy);
	(void)g;
}

static void draw_player(const Game *g, int camx, int camy)
{
	const Player *p = &g->p;
	float sx = floorf(p->x) - camx, sy = floorf(p->y) - camy;
	if (p->car >= 0) return;                                        // he's inside the car
	if (p->status == PL_DEAD) { draw_c(IMG_CORPSE(0, 1), sx, sy); return; }
	draw_c(IMG_SHADOW, sx, sy + 3);
	int frame = p->moving ? ((int)p->walkT & 3) : 1;
	int id = IMG_PLAYER(frame, dir_index(p->heading, DIRS));
	float x = floorf(sx - IMG_W(im[id]) * 0.5f), y = floorf(sy - IMG_H(im[id]) * 0.5f);
	if (p->hurtT > 0) draw_tint(id, x, y, RGB(255, 40, 40), 0.65f); else draw(id, x, y);
}

static int pickup_img(int kind)
{
	switch (kind) { case 0: return IMG_PK_HEALTH; case 1: return IMG_PK_ARMOR; case 2: return IMG_PK_PISTOL; case 3: return IMG_PK_SMG; case 4: return IMG_PK_SHOTGUN; default: return IMG_PK_CASH; }
}

static void draw_entities(const Game *g, int camx, int camy)
{
	// blood on the ground
	int ns = g->stain_n < MAX_STAINS ? g->stain_n : MAX_STAINS;
	for (int i = 0; i < ns; i++) {
		const Stain *s = &g->stains[i];
		float sx = s->x - camx, sy = s->y - camy;
		if (sx < -20 || sx > SCREEN_W + 20 || sy < -20 || sy > SCREEN_H + 20) continue;
		draw_c(IMG_STAIN0 + s->img, sx, sy);
	}
	for (int i = 0; i < MAX_PICKUPS; i++) {
		const Pickup *k = &g->pickups[i];
		if (!k->active) continue;
		float sx = k->x - camx, sy = k->y - camy + sinf(g->time * 3.0f + i) * 1.5f;
		if (sx < -12 || sx > SCREEN_W + 12 || sy < -12 || sy > SCREEN_H + 12) continue;
		draw_c(pickup_img(k->kind), sx, sy);
	}
	for (int i = 0; i < MAX_PEDS; i++) if (g->peds[i].active && g->peds[i].state == PS_DEAD) draw_ped(g, &g->peds[i], camx, camy);
	for (int i = 0; i < MAX_CARS; i++) if (g->cars[i].active && g->cars[i].state == CS_WRECK) draw_car(g, &g->cars[i], camx, camy);
	for (int i = 0; i < MAX_CARS; i++) if (g->cars[i].active && g->cars[i].state != CS_WRECK && g->cars[i].state != CS_PLAYER) draw_car(g, &g->cars[i], camx, camy);
	for (int i = 0; i < MAX_PEDS; i++) if (g->peds[i].active && g->peds[i].state != PS_DEAD) draw_ped(g, &g->peds[i], camx, camy);
	if (g->p.car >= 0) draw_car(g, &g->cars[g->p.car], camx, camy);
	draw_player(g, camx, camy);
}

static void draw_effects(const Game *g, int camx, int camy)
{
	for (int i = 0; i < MAX_BULLETS; i++) {
		const Bullet *b = &g->bullets[i];
		if (b->active) draw_c(IMG_BULLET, b->x - camx, b->y - camy);
	}
	for (int i = 0; i < MAX_PARTS; i++) {
		const Particle *p = &g->parts[i];
		if (!p->active) continue;
		float sx = p->x - camx, sy = p->y - camy;
		if (sx < -30 || sx > SCREEN_W + 30 || sy < -30 || sy > SCREEN_H + 30) continue;
		switch (p->type) {
		case P_BLOOD:  draw_c(IMG_BLOOD_DOT, sx, sy); break;
		case P_SMOKE:  draw_c(IMG_SMOKE0 + (p->frame % 3), sx, sy); break;
		case P_FIRE:   draw_c(IMG_FIRE0 + (p->frame % 3), sx, sy); break;
		case P_SPARK:  draw_c(IMG_SPARK, sx, sy); break;
		case P_MUZZLE: draw_c(IMG_MUZZLE, sx, sy); break;
		case P_BOOM:   draw_c(IMG_BOOM0 + (p->frame > 5 ? 5 : p->frame), sx, sy); break;
		}
	}
}

// ---- top screen: the city ----------------------------------------------
static void star(float cx, float cy, float r, u32 col)
{
	C2D_DrawTriangle(cx, cy - r, col, cx - r * 0.9f, cy + r * 0.6f, col, cx + r * 0.9f, cy + r * 0.6f, col, 0);
	C2D_DrawTriangle(cx, cy + r, col, cx - r * 0.9f, cy - r * 0.6f, col, cx + r * 0.9f, cy - r * 0.6f, col, 0);
}

void render_top(const Game *g, float eye, float slider)
{
	const World *w = &g->world;
	int camx = (int)floorf(g->camx), camy = (int)floorf(g->camy);       // whole pixels: crisp tiles, no seams
	int tx0 = camx / TILE, ty0 = camy / TILE;
	int ox = -(camx % TILE), oy = -(camy % TILE);
	enum { NX = 26, NY = 16 };
	float sep = slider * 3.0f;                                          // pixels of parallax per unit of height
	int canopy_dx = (int)lroundf(eye * sep * 0.8f);

	// pass 1: ground. Building tiles are drawn dark here: the part that peeks out beside the shifted roof is the wall.
	int bx[NX * NY], by[NY * NX], bid[NX * NY], nb = 0;
	C2D_ImageTint wall;
	C2D_PlainImageTint(&wall, RGB(10, 10, 20), 0.55f);
	for (int j = 0; j < NY; j++) {
		for (int i = 0; i < NX; i++) {
			int id = world_tile(w, tx0 + i, ty0 + j);
			if (id < 0) continue;
			float x = (float)(ox + i * TILE), y = (float)(oy + j * TILE);
			if (tile_flags[id] & F_BUILDING) {
				bx[nb] = i; by[nb] = j; bid[nb] = id; nb++;
				if (slider > 0) C2D_DrawImageAt(im[id], x, y, 0, &wall, 1.0f, 1.0f);
			} else {
				draw(id, x, y);
			}
		}
	}

	// ground-level things: blood, pickups, bodies, cars, people
	draw_entities(g, camx, camy);

	// pass 2: roofs (shifted by height) with dark outlines where a building ends. Low roofs first, then skyscrapers on top.
	for (int round = 0; round < 2; round++) {
		float mult = round == 0 ? 1.0f : 3.2f;
		int dx_roof = (int)lroundf(eye * sep * mult);
		for (int k = 0; k < nb; k++) {
			bool tall = (tile_flags[bid[k]] & F_TALL) != 0;
			if (tall != (round == 1)) continue;
			int i = bx[k], j = by[k];
			float x = (float)(ox + i * TILE + dx_roof), y = (float)(oy + j * TILE);
			draw(bid[k], x, y);
			int gx = tx0 + i, gy = ty0 + j;
			int grp = roof_group(bid[k]);
			if (!same_building(w, gx, gy - 1, grp)) draw(IMG_EDGE_N, x, y);
			if (!same_building(w, gx, gy + 1, grp)) draw(IMG_EDGE_S, x, y);
			if (!same_building(w, gx + 1, gy, grp)) draw(IMG_EDGE_E, x, y);
			if (!same_building(w, gx - 1, gy, grp)) draw(IMG_EDGE_W, x, y);
		}
	}

	// pass 3: tree canopies overhead
	for (int j = -1; j <= NY; j++) {
		for (int i = -1; i <= NX; i++) {
			int id = world_tile(w, tx0 + i, ty0 + j);
			if (id < 0 || !(tile_flags[id] & F_TREE)) continue;
			int ci = canopy_for(id);
			float cw = (float)IMG_W(im[ci]), ch = (float)IMG_H(im[ci]);
			draw(ci, (float)(ox + i * TILE + TILE / 2) - cw * 0.5f + canopy_dx, (float)(oy + j * TILE + TILE / 2) - ch * 0.5f - 2.0f);
		}
	}

	// pass 4: bullets, smoke, fire, explosions on top of everything
	draw_effects(g, camx, camy);

	// landmark labels (only when you're close and on foot)
	if (g->p.car < 0) {
		for (int k = 0; k < NLANDMARKS; k++) {
			float lx = landmarks[k].tx * TILE + TILE * 0.5f - camx, ly = landmarks[k].ty * TILE - camy;
			float dx = lx + camx - g->p.x, dy = ly + camy - g->p.y;
			if (dx * dx + dy * dy > 170.0f * 170.0f || lx < -80 || lx > SCREEN_W + 80 || ly < -10 || ly > SCREEN_H) continue;
			text(&t_land[k], lx + eye * sep * -0.6f, ly - 6, 0.42f, RGB(255, 255, 255), true);
		}
	}

	// ---- HUD (untextured shapes + text) ----
	if (g->flashT > 0) C2D_DrawRectSolid(0, 0, 0, SCREEN_W, SCREEN_H, RGBA(230, 0, 0, (int)fminf(110.0f, g->flashT * 260.0f)));
	if (g->zoneT > 0) {
		float a = g->zoneT > 0.5f ? 1.0f : g->zoneT / 0.5f;
		C2D_DrawRectSolid(0, 26, 0, SCREEN_W, 20, RGBA(0, 0, 0, (int)(130 * a)));
		C2D_DrawText(&t_zone[g->zone], C2D_WithColor | C2D_AlignCenter, SCREEN_W * 0.5f, 27, 0, 0.55f, 0.55f, RGBA(255, 255, 255, (int)(255 * a)));
	}
	float hx = 8.0f - eye * sep * 0.5f;                                  // small health/armor bars pop out a little
	C2D_DrawRectSolid(hx, 8, 0, 74, 8, RGB(12, 8, 10));
	C2D_DrawRectSolid(hx + 1, 9, 0, 72.0f * clampf(g->p.hp / 100.0f, 0, 1), 6, BLOOD);
	if (g->p.armor > 0) { C2D_DrawRectSolid(hx, 18, 0, 74, 6, RGB(8, 10, 20)); C2D_DrawRectSolid(hx + 1, 19, 0, 72.0f * clampf(g->p.armor / 100.0f, 0, 1), 4, RGB(90, 150, 240)); }
	bool blink = g->cops_see && ((int)(g->time * 4.0f) & 1);
	for (int i = 0; i < 5; i++) {
		u32 col = i < g->stars ? (blink ? RGB(255, 60, 60) : RGB(255, 220, 60)) : RGBA(255, 255, 255, 60);
		star(SCREEN_W - 12 - (4 - i) * 15 + eye * sep * -0.5f, 14, 6, col);
	}
	for (int i = 0; i < MAX_POPUPS; i++) {
		const Popup *pp = &g->popups[i];
		if (pp->t <= 0) continue;
		float a = pp->t > 0.5f ? 1.0f : pp->t / 0.5f;
		u32 c = pp->color;
		u32 col = RGBA(c & 255, (c >> 8) & 255, (c >> 16) & 255, (int)(255 * a));
		C2D_DrawText(&t_pop[i], C2D_WithColor | C2D_AlignCenter, SCREEN_W * 0.5f + 1, 52 + i * 15 + 1, 0, 0.6f, 0.6f, RGBA(0, 0, 0, (int)(200 * a)));
		C2D_DrawText(&t_pop[i], C2D_WithColor | C2D_AlignCenter, SCREEN_W * 0.5f, 52 + i * 15, 0, 0.6f, 0.6f, col);
	}
	if (g->p.status != PL_ALIVE) {
		float a = fminf(1.0f, g->p.statusT / 0.8f);
		C2D_DrawRectSolid(0, 0, 0, SCREEN_W, SCREEN_H, RGBA(g->p.status == PL_DEAD ? 60 : 0, 0, g->p.status == PL_BUSTED ? 60 : 0, (int)(150 * a)));
		const C2D_Text *t = g->p.status == PL_DEAD ? &t_wasted : &t_busted;
		u32 col = g->p.status == PL_DEAD ? RGBA(230, 30, 40, (int)(255 * a)) : RGBA(80, 130, 255, (int)(255 * a));
		C2D_DrawText(t, C2D_WithColor | C2D_AlignCenter, SCREEN_W * 0.5f + 2, 84 + 2, 0, 1.9f, 1.9f, RGBA(0, 0, 0, (int)(220 * a)));
		C2D_DrawText(t, C2D_WithColor | C2D_AlignCenter, SCREEN_W * 0.5f - eye * sep * 0.6f, 84, 0, 1.9f, 1.9f, col);
	}
	if (g->paused) {
		C2D_DrawRectSolid(0, 0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
		C2D_DrawText(&t_paused, C2D_WithColor | C2D_AlignCenter, SCREEN_W * 0.5f, 90, 0, 1.6f, 1.6f, RGB(255, 255, 255));
	}
}

// ---- bottom screen: map and HUD ---------------------------------------
static bool weapon_button(int i, float *x, float *y)
{
	static const float bx[4] = {238, 278, 238, 278}, by[4] = {96, 96, 126, 126};
	*x = bx[i]; *y = by[i];
	return true;
}

static void blip(float x, float y, u32 col)
{
	C2D_DrawRectSolid(x - 2, y - 2, 0, 4, 4, RGB(0, 0, 0));
	C2D_DrawRectSolid(x - 1, y - 1, 0, 2, 2, col);
}

void render_bottom(const Game *g)
{
	u32 navy2 = RGB(10, 26, 60);
	C2D_DrawRectangle(0, 0, 0, 320, 240, NAVY, NAVY, navy2, navy2);
	C2D_DrawRectSolid(0, 0, 0, 320, 3, ORANGE);

	// whole-city map, north up (1.375 map pixels per tile -> 220 px)
	const float MS = 1.375f, MX = 6, MY = 6;
	C2D_DrawRectSolid(MX - 2, MY - 2, 0, 160 * MS + 4, 160 * MS + 4, RGB(8, 10, 20));
	C2D_DrawImageAt(im[IMG_MINIMAP], MX, MY, 0, NULL, MS, MS);
	float vx = MX + g->camx / TILE * MS, vy = MY + g->camy / TILE * MS, vw = SCREEN_W / TILE * MS, vh = SCREEN_H / TILE * MS;
	u32 vc = RGBA(255, 255, 255, 150);
	C2D_DrawRectSolid(vx, vy, 0, vw, 1, vc); C2D_DrawRectSolid(vx, vy + vh, 0, vw, 1, vc);
	C2D_DrawRectSolid(vx, vy, 0, 1, vh, vc); C2D_DrawRectSolid(vx + vw, vy, 0, 1, vh + 1, vc);

	// places that matter: hospital, police station, Pay 'n' Spray
	blip(MX + (HOSPITAL_X + 0.5f) * MS, MY + (HOSPITAL_Y + 0.5f) * MS, RGB(255, 255, 255));
	blip(MX + (POLICE_X + 0.5f) * MS, MY + (POLICE_Y + 0.5f) * MS, RGB(70, 120, 255));
	for (int i = 0; i < NSPRAY; i++) blip(MX + (spray_pads[i].x0 + spray_pads[i].x1 + 1) * 0.5f * MS, MY + (spray_pads[i].y0 + spray_pads[i].y1 + 1) * 0.5f * MS, RGB(220, 120, 255));
	// police on the map: flashing red and blue
	int ph = (int)(g->time * 4.0f);
	for (int i = 0; i < MAX_PEDS; i++) {
		const Ped *p = &g->peds[i];
		if (p->active && p->kind != PK_CIV && p->state != PS_DEAD) blip(MX + p->x / TILE * MS, MY + p->y / TILE * MS, ((ph + i) & 1) ? RGB(255, 50, 50) : RGB(70, 120, 255));
	}
	for (int i = 0; i < MAX_CARS; i++) {
		const Car *c = &g->cars[i];
		if (c->active && c->state == CS_CHASE) blip(MX + c->x / TILE * MS, MY + c->y / TILE * MS, ((ph + i) & 1) ? RGB(255, 50, 50) : RGB(70, 120, 255));
	}
	// the player: an arrow pointing where he's heading
	float px = MX + g->p.x / TILE * MS, py = MY + g->p.y / TILE * MS, h = g->p.heading;
	float tx = px + sinf(h) * 6, ty = py - cosf(h) * 6;
	float lx = px + sinf(h + 2.5f) * 5, ly = py - cosf(h + 2.5f) * 5, rx = px + sinf(h - 2.5f) * 5, ry = py - cosf(h - 2.5f) * 5;
	C2D_DrawTriangle(tx, ty, RGB(0, 0, 0), lx, ly, RGB(0, 0, 0), rx, ry, RGB(0, 0, 0), 0);
	u32 me = ((int)(g->time * 3.0f) & 1) ? ORANGE : RGB(255, 255, 255);
	C2D_DrawTriangle(px + sinf(h) * 4.5f, py - cosf(h) * 4.5f, me, px + sinf(h + 2.5f) * 3.2f, py - cosf(h + 2.5f) * 3.2f, me, px + sinf(h - 2.5f) * 3.2f, py - cosf(h - 2.5f) * 3.2f, me, 0);

	// district name under the map
	text(&t_zone[g->zone], 6 + 160 * MS * 0.5f, 6 + 160 * MS + 3, 0.5f, RGB(255, 255, 255), true);

	// ---- HUD column ----
	const float X = 236;
	text(&t_cash, X, 6, 0.36f, RGB(255, 190, 120), false);
	text(&t_cashv, X, 15, 0.7f, RGB(120, 255, 140), false);
	text(&t_wanted, X, 42, 0.36f, RGB(255, 190, 120), false);
	bool blink = g->cops_see && ((int)(g->time * 4.0f) & 1);
	for (int i = 0; i < 5; i++) {
		u32 col = i < g->stars ? (blink ? RGB(255, 60, 60) : RGB(255, 220, 60)) : RGB(60, 74, 104);
		star(X + 7 + i * 15, 60, 6, col);
	}
	C2D_DrawRectSolid(X, 72, 0, 76, 9, RGB(10, 14, 30));
	C2D_DrawRectSolid(X + 1, 73, 0, 74.0f * clampf(g->p.hp / 100.0f, 0, 1), 7, BLOOD);
	C2D_DrawRectSolid(X, 84, 0, 76, 9, RGB(10, 14, 30));
	C2D_DrawRectSolid(X + 1, 85, 0, 74.0f * clampf(g->p.armor / 100.0f, 0, 1), 7, RGB(90, 150, 240));

	// weapon buttons (tap to switch)
	for (int i = 0; i < W_COUNT; i++) {
		float bx, by;
		weapon_button(i, &bx, &by);
		bool sel = g->p.weapon == i, own = g->p.has[i];
		C2D_DrawRectSolid(bx - 1, by - 1, 0, 40, 28, sel ? ORANGE : RGB(50, 70, 110));
		C2D_DrawRectSolid(bx, by, 0, 38, 26, sel ? RGB(60, 40, 20) : RGB(10, 18, 40));
		int ic = IMG_IC_FISTS + i;
		float ix = bx + (38 - IMG_W(im[ic])) * 0.5f, iy = by + 1;
		if (own) draw(ic, ix, iy); else draw_tint(ic, ix, iy, RGB(40, 48, 70), 0.85f);
		if (own && i != W_FISTS) text(&t_ammo[i], bx + 3, by + 14, 0.34f, RGB(255, 230, 160), false);
	}
	// what's being held / the car
	if (g->p.car >= 0) {
		const Car *c = &g->cars[g->p.car];
		text(&t_car, X, 160, 0.36f, RGB(255, 190, 120), false);
		float f = clampf(c->hp / car_defs[c->model].hp, 0, 1);
		C2D_DrawRectSolid(X, 172, 0, 76, 9, RGB(10, 14, 30));
		C2D_DrawRectSolid(X + 1, 173, 0, 74.0f * f, 7, f > 0.5f ? RGB(80, 200, 90) : f > 0.25f ? RGB(240, 180, 40) : RGB(220, 50, 40));
		for (int i = 0; i < 4; i++) text(&t_drive[i], X, 186 + i * 12, 0.36f, RGB(200, 220, 255), false);
	} else {
		text(&t_wname[g->p.weapon], X, 160, 0.5f, RGB(255, 255, 255), false);
		for (int i = 0; i < 4; i++) text(&t_foot[i], X, 186 + i * 12, 0.36f, RGB(200, 220, 255), false);
	}
	if (g->paused) {
		C2D_DrawRectSolid(0, 0, 0, 320, 240, RGBA(0, 0, 0, 170));
		text(&t_paused, 160, 70, 1.2f, RGB(255, 255, 255), true);
		text(&t_resume, 160, 130, 0.7f, RGB(255, 255, 255), true);
		text(&t_quit, 160, 160, 0.7f, RGB(255, 190, 120), true);
	}
}
