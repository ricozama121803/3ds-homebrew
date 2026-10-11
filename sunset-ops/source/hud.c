// Sunset Ops - HUD: radar, kill feed, score, ammo, crosshair, hit markers, damage indicators, name tags, menus.
#ifdef HOST_PREVIEW
#include "../host/c2d_shim.h"
#else
#include <citro2d.h>
#endif
#include "hud.h"
#include <stdio.h>
#include <string.h>

#define RGBA(r, g, b, a) C2D_Color32(r, g, b, a)
static C2D_TextBuf s_buf;
static const u32 WHITE = 0xFFFFFFFFu;

void hud_init(void) { s_buf = C2D_TextBufNew(4096); }
void hud_exit(void) { C2D_TextBufDelete(s_buf); }
void hud_frame_begin(void) { C2D_TextBufClear(s_buf); }

enum { AL_L, AL_C, AL_R };
static float tw(const char *s, float sc)
{
	C2D_Text t; C2D_TextParse(&t, s_buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sc, sc, &w, &h);
	return w;
}
static void T(const char *s, float x, float y, float sc, u32 col, int al)
{
	C2D_Text t; C2D_TextParse(&t, s_buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sc, sc, &w, &h);
	if (al == AL_C) x -= w * 0.5f; else if (al == AL_R) x -= w;
	C2D_DrawText(&t, C2D_WithColor, x + 1, y + 1, 0, sc, sc, RGBA(0, 0, 0, 170));
	C2D_DrawText(&t, C2D_WithColor, x, y, 0, sc, sc, col);
}
static u32 rgba_a(u32 c, float a) { return (c & 0x00FFFFFFu) | ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24); }

static void tri_up(float cx, float cy, float r, float ang, u32 col)         // arrow triangle pointing along ang (0 = up/north)
{
	float s = sinf(ang), c = cosf(ang);
	float px[3] = { 0, -0.7f, 0.7f }, py[3] = { -1.0f, 0.8f, 0.8f }, ox[3], oy[3];
	for (int i = 0; i < 3; i++) { ox[i] = cx + (px[i] * c - py[i] * s) * r; oy[i] = cy + (px[i] * s + py[i] * c) * r; }
	C2D_DrawTriangle(ox[0], oy[0], col, ox[1], oy[1], col, ox[2], oy[2], col, 0);
}

// ---- radar -----------------------------------------------------------------------------------------------------------
static void radar(const Game *g, float x0, float y0, float sc, bool big)
{
	float w = GRID_W * sc, h = GRID_H * sc;
	C2D_DrawRectSolid(x0 - 1, y0 - 1, 0, w + 2, h + 2, RGBA(10, 14, 18, big ? 255 : 210));
	C2D_DrawRectSolid(x0, y0, 0, w, h, big ? RGBA(36, 44, 52, 255) : RGBA(30, 38, 46, 200));
	// the compound (grass), the street, then every solid below head height (house walls show as a floor plan)
	for (int i = 0; i < MAP_NRECT; i++) {
		const float *r = MAP_RECTS[i];
		C2D_DrawRectSolid(x0 + (r[0] + ARENA_X) * sc, y0 + (r[1] + ARENA_Z) * sc, 0, (r[2] - r[0]) * sc, (r[3] - r[1]) * sc, big ? RGBA(54, 66, 44, 255) : RGBA(48, 60, 40, 200));
	}
	{
		static const float st[3][4] = { { -5.5f, -4, 7.5f, 18 }, { -4.5f, -9, 6.5f, -4 }, { -3, -10.5f, 5, -9 } };
		for (int i = 0; i < 3; i++) C2D_DrawRectSolid(x0 + (st[i][0] + ARENA_X) * sc, y0 + (st[i][1] + ARENA_Z) * sc, 0, (st[i][2] - st[i][0]) * sc, (st[i][3] - st[i][1]) * sc, big ? RGBA(66, 68, 72, 255) : RGBA(60, 62, 66, 210));
	}
	for (int i = 0; i < g->w.nsolid; i++) {
		const Box *b = &g->w.box[g->w.solid_idx[i]];
		if (b->y1 < 1.1f || b->y0 > 1.8f) continue;
		float bx0 = x0 + (b->x0 + ARENA_X) * sc, bz0 = y0 + (b->z0 + ARENA_Z) * sc, bw = (b->x1 - b->x0) * sc, bh = (b->z1 - b->z0) * sc;
		if (bx0 < x0) { bw -= x0 - bx0; bx0 = x0; } if (bz0 < y0) { bh -= y0 - bz0; bz0 = y0; }
		if (bx0 + bw > x0 + w) bw = x0 + w - bx0; if (bz0 + bh > y0 + h) bh = y0 + h - bz0;
		if (bw <= 0 || bh <= 0) continue;
		u32 c = b->y1 > 3.0f ? RGBA(150, 154, 160, 255) : RGBA(96, 100, 106, 255);
		C2D_DrawRectSolid(bx0, bz0, 0, bw, bh, c);
	}
	for (int i = 0; i < g->w.npick; i++) {
		const Pickup *p = &g->w.pick[i];
		if (p->respawn > 0 || !big) continue;
		u32 c = p->type == 0 ? RGBA(240, 200, 60, 255) : p->type == 1 ? RGBA(90, 230, 110, 255) : RGBA(230, 230, 230, 255);
		C2D_DrawRectSolid(x0 + (p->x + ARENA_X) * sc - 2, y0 + (p->z + ARENA_Z) * sc - 2, 0, 4, 4, c);
	}
	for (int i = 1; i < MAX_ACT; i++) {
		const Actor *a = &g->a[i];
		if (!a->alive) continue;
		float ax = x0 + (a->x + ARENA_X) * sc, az = y0 + (a->z + ARENA_Z) * sc, r = big ? 5.0f : 3.2f;
		if (a->team == TEAM_ALLY) tri_up(ax, az, r, a->yaw, RGBA(70, 255, 90, 255));
		else if (a->spottedT > 0) tri_up(ax, az, r, a->yaw, RGBA(255, 70, 60, 255));
	}
	const Actor *p = &g->a[0];
	if (p->alive) tri_up(x0 + (p->x + ARENA_X) * sc, y0 + (p->z + ARENA_Z) * sc, big ? 6.5f : 4.0f, p->yaw, RGBA(255, 215, 60, 255));
	// grenades
	for (int i = 0; i < MAX_GREN; i++) if (g->gren[i].live && big) C2D_DrawRectSolid(x0 + (g->gren[i].p[0] + ARENA_X) * sc - 1.5f, y0 + (g->gren[i].p[2] + ARENA_Z) * sc - 1.5f, 0, 3, 3, RGBA(255, 120, 40, 255));
}

// ---- top screen --------------------------------------------------------------------------------------------------------
static void crosshair(const Game *g)
{
	const Player *p = &g->p; const WeaponDef *wd = &weapons[p->weapon];
	float cx = 200, cy = 120;
	if (p->ads > 0.6f && p->weapon != W_SHOTGUN) { C2D_DrawRectSolid(cx - 1, cy - 1, 0, 2, 2, RGBA(255, 60, 50, 255)); return; }
	float spread = lerpf(wd->spread_hip, wd->spread_ads, p->ads) * (1.0f + 0.5f * p->moveFrac * (1 - p->ads)) + p->kick * 40.0f;
	float gap = 3.0f + spread * 1.7f, len = 5.0f;
	u32 c = RGBA(255, 255, 255, (u32)(235 - 130 * p->ads));
	for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2)
		C2D_DrawLine(cx + sx * gap, cy + sy * gap, c, cx + sx * (gap + len), cy + sy * (gap + len), c, 1.6f, 0);
	if (p->hitMarkT > 0) {
		u32 hc = p->killMarkT > 0 ? RGBA(255, 60, 50, 255) : WHITE;
		for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2)
			C2D_DrawLine(cx + sx * 4, cy + sy * 4, hc, cx + sx * 10, cy + sy * 10, hc, 2.0f, 0);
	}
}

static void damage_fx(const Game *g)
{
	const Player *p = &g->p; const Actor *a = &g->a[0];
	float a1 = 0;
	if (a->alive) {
		float recent = p->lastDmgT < 0.6f ? (0.6f - p->lastDmgT) / 0.6f : 0;
		float low = a->hp < 45 ? (45 - a->hp) / 45.0f : 0;
		float pulse = low * (0.5f + 0.5f * sinf(g->time * 6.0f));
		a1 = fmaxf(recent * 0.6f, pulse * 0.55f + low * 0.2f);
	} else a1 = 0.65f;
	if (a1 > 0.01f) {
		u32 edge = RGBA(190, 0, 0, (u32)(a1 * 255)), clear = RGBA(190, 0, 0, 0);
		C2D_DrawRectangle(0, 0, 0, 400, 50, edge, edge, clear, clear);
		C2D_DrawRectangle(0, 190, 0, 400, 50, clear, clear, edge, edge);
		C2D_DrawRectangle(0, 0, 0, 60, 240, edge, clear, edge, clear);
		C2D_DrawRectangle(340, 0, 0, 60, 240, clear, edge, clear, edge);
	}
	for (int i = 0; i < MAX_HITS; i++) {
		if (p->dmgT[i] <= 0 || !a->alive) continue;
		float ang = p->dmgDir[i], al = p->dmgT[i] > 1.0f ? 1.0f : p->dmgT[i];
		float s = sinf(ang), c = cosf(ang), r = 78;
		float cx = 200 + s * r, cy = 120 - c * r;
		u32 col = RGBA(230, 20, 20, (u32)(al * 230));
		float px[3] = { 0, -9, 9 }, py[3] = { -8, 8, 8 }, ox[3], oy[3];
		for (int k = 0; k < 3; k++) { ox[k] = cx + px[k] * c - py[k] * s * -1; oy[k] = cy + px[k] * s + py[k] * c * -1; }
		// triangle pointing outwards from the centre
		float dx = s, dy = -c, nx = -dy, ny = dx;
		C2D_DrawTriangle(cx + dx * 9, cy + dy * 9, col, cx - dx * 6 + nx * 11, cy - dy * 6 + ny * 11, col, cx - dx * 6 - nx * 11, cy - dy * 6 - ny * 11, col, 0);
		(void)ox; (void)oy;
	}
}

static void nametags(const Game *g, const Scene *s)
{
	for (int i = 1; i < MAX_ACT; i++) {
		const Actor *a = &g->a[i];
		if (!a->alive || a->team != TEAM_ALLY) continue;
		float dx = a->x - g->cam.eye[0], dz = a->z - g->cam.eye[2];
		float d = sqrtf(dx * dx + dz * dz);
		if (d > 60 || d < 1.5f) continue;
		float p[3] = { a->x, 2.05f, a->z }, sx, sy;
		if (!scene_project(s, p, &sx, &sy)) continue;
		if (sx < 20 || sx > 380 || sy < 10 || sy > 220) continue;
		char buf[48]; snprintf(buf, sizeof buf, "[SQD] %s", callsign(a->callsign));
		float sc = d < 20 ? 0.36f : 0.3f;
		T(buf, sx, sy - 12, sc, RGBA(120, 255, 130, 255), AL_C);
		tri_up(sx, sy + 2, 3.5f, PI_F, RGBA(80, 255, 100, 230));
	}
}

static void score_block(const Game *g)
{
	int mins = (int)(g->timeLeft > 0 ? g->timeLeft : 0) / 60, secs = (int)(g->timeLeft > 0 ? g->timeLeft : 0) % 60;
	char buf[48];
	float me = g->score[0], them = g->score[1];
	const char *st = me > them ? "WINNING" : me < them ? "LOSING" : "TIED";
	snprintf(buf, sizeof buf, "%d:%02d", mins, secs);
	u32 sc = me > them ? RGBA(120, 255, 130, 255) : me < them ? RGBA(255, 110, 100, 255) : WHITE;
	float x = 10, y = 188;
	C2D_DrawRectangle(x - 6, y - 4, 0, 130, 50, RGBA(0, 0, 0, 100), RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 100), RGBA(0, 0, 0, 0));
	T(buf, x, y - 3, 0.5f, WHITE, AL_L);
	T(st, x + 46, y - 1, 0.4f, sc, AL_L);
	// emblem: stacked chevrons
	tri_up(x + 16, y + 28, 12, 0, RGBA(230, 235, 245, 255));
	tri_up(x + 16, y + 33, 7, 0, RGBA(60, 90, 140, 255));
	snprintf(buf, sizeof buf, "%d", (int)me); T(buf, x + 40, y + 10, 0.85f, WHITE, AL_L);
	snprintf(buf, sizeof buf, "%d", (int)them); T(buf, x + 40, y + 29, 0.55f, RGBA(255, 150, 140, 255), AL_L);
}

static void ammo_block(const Game *g)
{
	const Player *p = &g->p; const WeaponDef *wd = &weapons[p->weapon];
	char buf[48];
	float xr = 392, y = 184;
	C2D_DrawRectangle(xr - 190, y - 6, 0, 198, 56, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 100), RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 100));
	T(wd->name, xr, y - 3, 0.42f, RGBA(230, 230, 235, 255), AL_R);
	T(wd->mode, xr, y + 9, 0.3f, RGBA(180, 190, 205, 255), AL_R);
	snprintf(buf, sizeof buf, "/%d", p->reserve[p->weapon]); float rw = tw(buf, 0.55f);
	T(buf, xr, y + 24, 0.55f, RGBA(190, 195, 205, 255), AL_R);
	snprintf(buf, sizeof buf, "%d", p->mag[p->weapon]);
	u32 mc = p->mag[p->weapon] <= wd->mag / 4 ? RGBA(255, 120, 100, 255) : WHITE;
	T(buf, xr - rw - 2, y + 14, 0.95f, mc, AL_R);
	// grenades + medkits
	float gx = xr - 176, gy = y + 36;
	C2D_DrawRectSolid(gx, gy - 6, 0, 8, 10, RGBA(70, 100, 60, 255)); C2D_DrawRectSolid(gx + 2, gy - 9, 0, 4, 3, RGBA(160, 160, 160, 255));
	snprintf(buf, sizeof buf, "x%d", p->grenades); T(buf, gx + 11, gy - 9, 0.4f, p->grenades ? WHITE : RGBA(255, 120, 100, 255), AL_L);
	float mx = gx + 42;
	C2D_DrawRectSolid(mx, gy - 6, 0, 11, 10, RGBA(235, 235, 235, 255));
	C2D_DrawRectSolid(mx + 4, gy - 5, 0, 3, 8, RGBA(200, 30, 30, 255)); C2D_DrawRectSolid(mx + 1, gy - 2, 0, 9, 3, RGBA(200, 30, 30, 255));
	snprintf(buf, sizeof buf, "x%d", p->medkits); T(buf, mx + 14, gy - 9, 0.4f, p->medkits ? WHITE : RGBA(255, 120, 100, 255), AL_L);
}

static void sun_veil(const Scene *s)                // looking into the low sun washes the picture out with warm light (a cheap bloom)
{
	float sd[3]; scene_sun_dir(sd);
	float d = s->fwd[0] * sd[0] + s->fwd[1] * sd[1] + s->fwd[2] * sd[2];
	float t = (d - 0.5f) / 0.5f; if (t <= 0) return;
	t = t * t * (3 - 2 * t);
	u32 c = RGBA(255, 206, 150, (u32)(110 * t)), c2 = RGBA(255, 190, 130, (u32)(60 * t));
	C2D_DrawRectangle(0, 0, 0, 400, 240, c, c, c2, c2);
}

void hud_top(const Game *g, const Scene *s)
{
	if (g->state == GS_TITLE) {
		C2D_DrawRectangle(0, 0, 0, 400, 240, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 170), RGBA(0, 0, 0, 170));
		T("SUNSET OPS", 200, 62, 1.25f, RGBA(255, 214, 140, 255), AL_C);
		T("TEAM DEATHMATCH  -  4 vs 4", 200, 112, 0.5f, WHITE, AL_C);
		T("Press A to deploy", 200, 166, 0.62f, (((int)(g->time * 2)) & 1) ? WHITE : RGBA(255, 214, 140, 255), AL_C);
		return;
	}
	const Actor *a = &g->a[0];
	sun_veil(s);
	radar(g, 6, 6, 0.72f, false);
	// kill feed under the radar
	{
		float y = 58;
		for (int i = 0; i < MAX_FEED; i++) {
			const Feed *f = &g->feed[i];
			if (f->t <= 0) continue;
			float al = f->t > 1 ? 255 : f->t * 255;
			const Actor *k = &g->a[f->killer], *v = &g->a[f->victim];
			u32 kc = k->team == TEAM_ALLY ? RGBA(120, 255, 130, (u32)al) : RGBA(255, 120, 110, (u32)al), vc = v->team == TEAM_ALLY ? RGBA(120, 255, 130, (u32)al) : RGBA(255, 120, 110, (u32)al);
			char b[48]; snprintf(b, sizeof b, "%s", callsign(f->killer));
			T(b, 8, y, 0.33f, kc, AL_L);
			float w = tw(b, 0.33f);
			C2D_DrawRectSolid(8 + w + 5, y + 5, 0, 12, 3, RGBA(210, 210, 210, (u32)al)); C2D_DrawRectSolid(8 + w + 5, y + 5, 0, 3, 6, RGBA(210, 210, 210, (u32)al));
			if (f->killer == f->victim) snprintf(b, sizeof b, "%s", callsign(f->victim)); else snprintf(b, sizeof b, "%s", callsign(f->victim));
			T(b, 8 + w + 22, y, 0.33f, vc, AL_L);
			y += 12;
		}
	}
	if (g->showFps) { char fb[24]; snprintf(fb, sizeof fb, "%d FPS", (int)(g->fps + 0.5f)); T(fb, 394, 4, 0.4f, g->fps >= 27 ? RGBA(120, 255, 130, 255) : g->fps >= 20 ? RGBA(255, 220, 100, 255) : RGBA(255, 110, 100, 255), AL_R); }
	nametags(g, s);
	crosshair(g);
	damage_fx(g);
	score_block(g);
	ammo_block(g);
	// centre messages
	{
		const Player *p = &g->p; char b[48];
		if (!a->alive) {
			T("YOU WERE KILLED", 200, 78, 0.7f, RGBA(255, 110, 100, 255), AL_C);
			snprintf(b, sizeof b, "by %s", callsign(a->lastAttacker)); T(b, 200, 102, 0.45f, WHITE, AL_C);
			snprintf(b, sizeof b, "Respawning in %d", (int)(a->respawnT + 0.99f)); T(b, 200, 126, 0.4f, RGBA(200, 200, 210, 255), AL_C);
		} else {
			if (p->reloading) { T("RELOADING", 200, 138, 0.42f, RGBA(255, 230, 150, 255), AL_C); float f = 1 - p->reloadT / weapons[p->weapon].reload; C2D_DrawRectSolid(170, 156, 0, 60, 3, RGBA(0, 0, 0, 150)); C2D_DrawRectSolid(170, 156, 0, 60 * f, 3, WHITE); }
			else if (p->mag[p->weapon] == 0 && p->reserve[p->weapon] == 0) T("NO AMMO", 200, 138, 0.42f, RGBA(255, 110, 100, 255), AL_C);
			else if (p->mag[p->weapon] <= weapons[p->weapon].mag / 4) T("LOW AMMO - RELOAD (SELECT)", 200, 138, 0.36f, RGBA(255, 200, 120, 255), AL_C);
			if (p->healT > 0) { float f = 1 - p->healT / 1.5f; C2D_DrawRectSolid(165, 170, 0, 70, 4, RGBA(0, 0, 0, 150)); C2D_DrawRectSolid(165, 170, 0, 70 * f, 4, RGBA(90, 230, 110, 255)); }
			if (a->invulnT > 0) T("SPAWN PROTECTION", 200, 172, 0.3f, RGBA(180, 200, 255, 200), AL_C);
		}
		float y = 150;
		for (int i = 0; i < 3; i++) if (g->pop[i].t > 0 && a->alive) { u32 c = g->pop[i].col; float al = g->pop[i].t > 0.5f ? 255 : g->pop[i].t * 510; T(g->pop[i].text, 200, y, 0.4f, rgba_a(c, al), AL_C); y += 13; }
		if (a->alive && a->hp < 100) {                                       // health bar above the score (thin)
			C2D_DrawRectSolid(10, 180, 0, 102, 5, RGBA(0, 0, 0, 140));
			u32 hc = a->hp > 50 ? RGBA(120, 230, 120, 255) : a->hp > 25 ? RGBA(240, 190, 70, 255) : RGBA(240, 70, 60, 255);
			C2D_DrawRectSolid(11, 181, 0, a->hp, 3, hc);
		}
	}
	if (g->state == GS_OVER) {
		C2D_DrawRectSolid(0, 0, 0, 400, 240, RGBA(0, 0, 0, 150));
		const char *t = g->winner == 0 ? "VICTORY" : g->winner == 1 ? "DEFEAT" : "DRAW";
		u32 c = g->winner == 0 ? RGBA(130, 255, 140, 255) : g->winner == 1 ? RGBA(255, 110, 100, 255) : WHITE;
		T(t, 200, 40, 1.4f, c, AL_C);
		char b[64]; snprintf(b, sizeof b, "Your team %d  -  %d  Enemy team", (int)g->score[0], (int)g->score[1]); T(b, 200, 108, 0.55f, WHITE, AL_C);
		snprintf(b, sizeof b, "Your kills %d   deaths %d   best %d", a->kills, a->deaths, g->bestKills); T(b, 200, 134, 0.45f, RGBA(220, 220, 230, 255), AL_C);
		if (g->overT > 1.5f) T("Press A for a rematch", 200, 176, 0.5f, (((int)(g->time * 2)) & 1) ? WHITE : RGBA(255, 214, 140, 255), AL_C);
	}
	if (g->paused) {
		C2D_DrawRectSolid(0, 0, 0, 400, 240, RGBA(0, 0, 0, 160));
		T("PAUSED", 200, 28, 0.9f, WHITE, AL_C);
		char b[64];
		snprintf(b, sizeof b, "LOOK SPEED   < %d%% >   (D-pad left/right)", (int)(g->sens * 100 + 0.5f)); T(b, 200, 92, 0.42f, WHITE, AL_C);
		snprintf(b, sizeof b, "INVERT Y: %s   (X)", g->invertY ? "ON" : "OFF"); T(b, 200, 110, 0.42f, WHITE, AL_C);
		snprintf(b, sizeof b, "AIM ASSIST: %s   (Y)", g->aimAssist ? "ON" : "OFF"); T(b, 200, 128, 0.42f, WHITE, AL_C);
		snprintf(b, sizeof b, "GRAPHICS: %s   (R)%s", g->quality ? "HIGH" : "LOW", g->quality != g->qualityActive ? "  - restart to apply" : ""); T(b, 200, 146, 0.42f, WHITE, AL_C);
		snprintf(b, sizeof b, "FPS COUNTER: %s   (L)", g->showFps ? "ON" : "OFF"); T(b, 200, 164, 0.42f, WHITE, AL_C);
		snprintf(b, sizeof b, "DISTANCE HAZE: %s   (D-pad up)", g->noHaze ? "OFF" : "ON"); T(b, 200, 182, 0.42f, WHITE, AL_C);
		T("A  resume     B  quit game", 200, 210, 0.45f, RGBA(255, 214, 140, 255), AL_C);
	}
}

// ---- bottom screen --------------------------------------------------------------------------------------------------------
void hud_bottom(const Game *g)
{
	if (g->state == GS_TITLE) {
		T("CONTROLS", 160, 8, 0.6f, RGBA(255, 214, 140, 255), AL_C);
		static const char *rows[][2] = {
			{ "X / B", "forward (double-tap X: sprint) / back" }, { "Y / A", "strafe left / right" }, { "Circle Pad", "look around" },
			{ "R", "shoot" }, { "L", "aim down sights (slows the look speed)" },
			{ "D-Pad left/right", "change weapon" }, { "D-Pad up", "throw grenade" }, { "D-Pad down", "heal (medkit)" },
			{ "SELECT / touch", "reload" }, { "START", "pause" },
		};
		float y = 40;
		for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
			T(rows[i][0], 12, y, 0.4f, RGBA(255, 214, 140, 255), AL_L);
			T(rows[i][1], 128, y, 0.4f, WHITE, AL_L);
			y += 17;
		}
		T("Capture the most kills before time runs out", 160, 214, 0.34f, RGBA(190, 195, 205, 255), AL_C);
		return;
	}
	radar(g, 10, 4, 3.4f, true);
	char b[96];
	snprintf(b, sizeof b, "K %d   D %d", g->a[0].kills, g->a[0].deaths); T(b, 14, 8, 0.4f, WHITE, AL_L);
	T("X fwd  B back  Y left  A right   L aim  R fire   SELECT reload", 160, 224, 0.28f, RGBA(200, 205, 215, 255), AL_C);
}
