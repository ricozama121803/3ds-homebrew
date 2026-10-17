// Grand Theft Otto II - HUD: health, armour, cash, wanted stars, weapon, speed, prompts, WASTED / BUSTED; bottom screen: city map, weapon buttons, controls.
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
static const u32 GOLD = 0xFF40D8FFu;                 // 0xAABBGGRR: warm yellow
static const u32 ORANGE = 0xFF1C7CF2u;

void hud_init(void) { s_buf = C2D_TextBufNew(4096); }
void hud_exit(void) { C2D_TextBufDelete(s_buf); }
void hud_frame_begin(void) { C2D_TextBufClear(s_buf); }

enum { AL_L, AL_C, AL_R };
static void T(const char *s, float x, float y, float sc, u32 col, int al)
{
	C2D_Text t; C2D_TextParse(&t, s_buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sc, sc, &w, &h);
	if (al == AL_C) x -= w * 0.5f; else if (al == AL_R) x -= w;
	C2D_DrawText(&t, C2D_WithColor, x + 1, y + 1, 0, sc, sc, RGBA(0, 0, 0, 170));
	C2D_DrawText(&t, C2D_WithColor, x, y, 0, sc, sc, col);
}
static void ammo_text(char *buf, size_t n, int ammo) { if (TEST_LOADOUT) snprintf(buf, n, "inf"); else snprintf(buf, n, "%d", ammo); }
static u32 with_alpha(u32 c, float a) { return (c & 0x00FFFFFFu) | ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24); }
static void panel(float x, float y, float w, float h, u32 col) { C2D_DrawRectSolid(x, y, 0, w, h, col); }
static void tri_dir(float cx, float cy, float r, float ang, u32 col)         // arrow triangle pointing along ang (0 = up)
{
	float s = sinf(ang), c = cosf(ang);
	float px[3] = { 0, -0.7f, 0.7f }, py[3] = { -1.0f, 0.8f, 0.8f }, ox[3], oy[3];
	for (int i = 0; i < 3; i++) { ox[i] = cx + (px[i] * c - py[i] * s) * r; oy[i] = cy + (px[i] * s + py[i] * c) * r; }
	C2D_DrawTriangle(ox[0], oy[0], col, ox[1], oy[1], col, ox[2], oy[2], col, 0);
}
static void star(float cx, float cy, float r, u32 col)                        // a five-pointed star as ten triangles
{
	float px[10], py[10];
	for (int i = 0; i < 10; i++) { float a = -PI_F / 2 + i * PI_F / 5, rr = (i & 1) ? r * 0.42f : r; px[i] = cx + cosf(a) * rr; py[i] = cy + sinf(a) * rr; }
	for (int i = 0; i < 10; i++) { int j = (i + 1) % 10; C2D_DrawTriangle(cx, cy, col, px[i], py[i], col, px[j], py[j], col, 0); }
}
static void bar(float x, float y, float w, float h, float frac, u32 fill, u32 back)
{
	panel(x - 1, y - 1, w + 2, h + 2, RGBA(0, 0, 0, 200)); panel(x, y, w, h, back);
	if (frac > 0) panel(x, y, w * (frac > 1 ? 1 : frac), h, fill);
}

static void help_page(void)
{
	T("CONTROLS", 160, 2, 0.5f, ORANGE, AL_C);
	static const char *rows[][2] = {
		{ "On foot", "" }, { "Circle Pad", "walk / run" }, { "B B B ...", "sprint; keep tapping for faster" }, { "R", "shoot (auto-aims) / punch" }, { "L  (hold)", "aim; lock on. Circle Pad: switch target" },
		{ "Y", "next weapon" }, { "SELECT", "weapon wheel (Circle Pad / D-pad)" }, { "X", "get into the nearest vehicle" },
		{ "Car / motorcycle", "" }, { "Circle Pad", "steer" }, { "R  /  B", "gas  /  brake, reverse" }, { "A  or  L", "handbrake (drift)" }, { "X  (when slow)", "get out" },
		{ "Helicopter", "" }, { "Circle Pad", "forward / back, turn" }, { "R", "climb" }, { "B  or  A  or  L", "descend (X to get out on the ground)" },
		{ "Camera", "" }, { "C-stick / D-pad / drag", "turn the camera" }, { "START", "pause; SELECT there = back to start" } };
	for (int i = 0; i < 20; i++) {
		bool head = rows[i][1][0] == 0;
		float y = 17 + i * 10.7f;
		if (head) T(rows[i][0], 6, y, 0.3f, GOLD, AL_L);
		else { T(rows[i][0], 12, y, 0.28f, WHITE, AL_L); T(rows[i][1], 112, y, 0.28f, RGBA(255, 255, 255, 200), AL_L); }
	}
}

static void speed_lines(const Game *g)
{
	if (g->p.car < 0) return;
	const Car *c = &g->car[g->p.car];
	float spd = sqrtf(c->vx * c->vx + c->vz * c->vz), k = clampf((spd - 26.0f) / 26.0f, 0, 1);
	if (k <= 0) return;
	for (int i = 0; i < 26; i++) {
		float a = i * 2 * PI_F / 26 + (i * 7 % 5) * 0.05f;
		float jit = ((i * 13 + (int)(g->time * 30) * 7) % 10) / 10.0f;
		float r0 = 92 + jit * 46, r1 = r0 + 26 + k * 54 + jit * 22;
		float ca = cosf(a), sa = sinf(a), px = -sa * (0.8f + k * 0.8f), py = ca * (0.8f + k * 0.8f);
		float x0 = 200 + ca * r0 * 1.7f, y0 = 120 + sa * r0, x1 = 200 + ca * r1 * 1.7f, y1 = 120 + sa * r1;
		u32 c0 = RGBA(255, 255, 255, (int)(k * 85)), c1 = RGBA(255, 255, 255, 0);
		C2D_DrawTriangle(x0 - px, y0 - py, c0, x0 + px, y0 + py, c0, x1, y1, c1, 0);
	}
	if (k > 0.5f) { int a = (int)((k - 0.5f) * 60); panel(0, 0, 400, 8, RGBA(0, 0, 0, a)); panel(0, 232, 400, 8, RGBA(0, 0, 0, a)); }
}

// a thick line between two points
static void seg(float x0, float y0, float x1, float y1, float th, u32 col) { C2D_DrawLine(x0, y0, col, x1, y1, col, th, 0); }

// the crosshair: four ticks around a gap; locked on, it closes in and turns red. Hit marker: white diagonal ticks that flick outward. Kill marker: a big red cross.
static void crosshair(const Game *g, float cx, float cy, bool locked)
{
	float gap = locked ? 5.0f : 8.0f, len = locked ? 6.0f : 5.0f;
	u32 c = locked ? RGBA(255, 70, 50, 240) : RGBA(255, 255, 255, 220);
	seg(cx - gap - len, cy, cx - gap, cy, 2, c); seg(cx + gap, cy, cx + gap + len, cy, 2, c);
	seg(cx, cy - gap - len, cx, cy - gap, 2, c); seg(cx, cy + gap, cx, cy + gap + len, 2, c);
	panel(cx - 1, cy - 1, 2, 2, c);
	if (g->hitT > 0) {                                                                    // a hit landed
		float k = g->hitT / 0.22f, a = clampf(k * 1.6f, 0, 1) * 255, o = 9.0f + (1 - k) * 5.0f, l = 6.0f;
		u32 w = RGBA(255, 255, 255, (int)a);
		for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) seg(cx + sx * o, cy + sy * o, cx + sx * (o + l), cy + sy * (o + l), 2.2f, w);
	}
	if (g->killT > 0) {                                                                   // confirmed kill
		float k = g->killT / 0.9f, grow = 1.0f + 0.6f * clampf((k - 0.8f) * 5.0f, 0, 1), a = clampf(k * 2.5f, 0, 1) * 255, o = 6.0f * grow, l = 12.0f * grow;
		u32 r = RGBA(255, 40, 30, (int)a), d = RGBA(0, 0, 0, (int)(a * 0.7f));
		for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
			seg(cx + sx * o + 1, cy + sy * o + 1, cx + sx * (o + l) + 1, cy + sy * (o + l) + 1, 4.0f, d);
			seg(cx + sx * o, cy + sy * o, cx + sx * (o + l), cy + sy * (o + l), 3.0f, r);
		}
	}
}

// the weapon wheel: one slice per weapon (up, right, down, left), the chosen one lit
static void weapon_wheel(const Game *g)
{
	panel(0, 0, 400, 240, RGBA(0, 0, 0, 120));
	static const float ox[4] = { 0, 96, 0, -96 }, oy[4] = { -70, 0, 70, 0 };
	for (int i = 0; i < W_COUNT; i++) {
		float x = 200 + ox[i % 4] - 42, y = 120 + oy[i % 4] - 22;
		bool has = g->p.has[i] && (i == W_FISTS || g->p.ammo[i] > 0), sel = g->p.weapon == i, hot = g->wheelSel == i;
		panel(x - 2, y - 2, 88, 48, hot ? RGBA(255, 220, 120, 255) : RGBA(0, 0, 0, 160));
		panel(x, y, 84, 44, sel ? RGBA(210, 100, 24, 255) : has ? RGBA(40, 48, 72, 255) : RGBA(26, 30, 40, 255));
		T(WPN[i].name, x + 42, y + 5, 0.55f, has ? WHITE : RGBA(255, 255, 255, 90), AL_C);
		if (i != W_FISTS) T(TEST_LOADOUT ? "inf" : "", x + 42, y + 25, 0.45f, GOLD, AL_C);
	}
	T("Circle Pad / D-pad: choose    SELECT: close", 200, 214, 0.38f, RGBA(255, 255, 255, 200), AL_C);
}

// ---- top screen ----------------------------------------------------------------------------------------------------------------------
void hud_top(const Game *g, const Scene *s)
{
	char buf[64];
	const Player *p = &g->p;
	speed_lines(g);
	// red flash when hurt
	if (p->hurtT > 0 && p->ps == PS_ALIVE) {
		float a = fminf(1.0f, p->hurtT / 0.45f) * 120;
		panel(0, 0, 400, 14, RGBA(200, 0, 0, (int)a)); panel(0, 226, 400, 14, RGBA(200, 0, 0, (int)a)); panel(0, 14, 14, 212, RGBA(200, 0, 0, (int)a)); panel(386, 14, 14, 212, RGBA(200, 0, 0, (int)a));
	}
	// crosshair: on the person Otto is aiming at, else the middle of the screen while a gun is up
	if (p->ps == PS_ALIVE && p->car < 0) {
		bool locked = p->lock >= 0;
		float sx, sy;
		bool shown = false;
		if (p->target >= 0) {
			const Ped *q = &g->ped[p->target];
			float pos[3] = { q->x, 1.35f, q->z };
			if (q->on && (!q->dead || g->hitT > 0 || g->killT > 0) && scene_project(s, pos, &sx, &sy)) {            // a person who has just died keeps the markers for a moment
				if (locked && !q->dead) { u32 c = RGBA(255, 60, 40, 235); panel(sx - 13, sy - 13, 6, 2, c); panel(sx - 13, sy - 13, 2, 6, c); panel(sx + 7, sy - 13, 6, 2, c); panel(sx + 11, sy - 13, 2, 6, c); panel(sx - 13, sy + 11, 6, 2, c); panel(sx - 13, sy + 7, 2, 6, c); panel(sx + 7, sy + 11, 6, 2, c); panel(sx + 11, sy + 7, 2, 6, c); }
				crosshair(g, sx, sy, locked && !q->dead);
				shown = true;
			}
		}
		if (!shown && p->weapon != W_FISTS && (p->aimT > 0.35f || g->hitT > 0 || g->killT > 0)) crosshair(g, 200, 120, false);
	}
	// health, armour
	panel(0, 0, 150, 38, RGBA(0, 0, 0, 90));
	bar(8, 6, 120, 9, p->hp / PLAYER_MAX_HP, p->hp < 45 ? RGBA(235, 50, 40, 255) : RGBA(210, 60, 60, 255), RGBA(70, 20, 20, 255));
	T("HP", 132, 2, 0.32f, RGBA(255, 255, 255, 200), AL_L);
	bar(8, 20, 120, 7, p->armor / 100.0f, RGBA(70, 130, 235, 255), RGBA(20, 30, 60, 255));
	T("AR", 132, 14, 0.32f, RGBA(255, 255, 255, 200), AL_L);
	// cash
	snprintf(buf, sizeof buf, "$%d", p->cash);
	T(buf, 394, 3, 0.75f, GOLD, AL_R);
	// wanted stars
	bool blink = g->starFlash > 0 || (g->stars > 0 && g->unseenT < 0.5f && ((int)(g->time * 3.0f) & 1));
	for (int i = 0; i < 5; i++) {
		bool on = i < g->stars;
		u32 col = on ? (blink ? RGBA(255, 255, 255, 255) : RGBA(255, 210, 40, 255)) : RGBA(255, 255, 255, 60);
		star(200 + (i - 2) * 22, 14, on ? 9.0f : 7.0f, col);
	}
		// weapon and ammo
	{
		const WeaponDef *w = &WPN[p->weapon];
		panel(0, 206, 120, 34, RGBA(0, 0, 0, 100));
		T(w->name, 8, 208, 0.5f, WHITE, AL_L);
		if (p->weapon != W_FISTS) { ammo_text(buf, sizeof buf, p->ammo[p->weapon]); T(buf, 112, 208, 0.5f, p->ammo[p->weapon] < 6 ? RGBA(255, 90, 70, 255) : GOLD, AL_R); }
		else T("SELECT: weapons", 8, 224, 0.3f, RGBA(255, 255, 255, 150), AL_L);
	}
	// driving: speed and car health
	if (p->car >= 0) {
		const Car *c = &g->car[p->car];
		float spd = sqrtf(c->vx * c->vx + c->vz * c->vz);
		panel(300, c->kind == CAR_HELI ? 172 : 190, 100, c->kind == CAR_HELI ? 68 : 50, RGBA(0, 0, 0, 100));
		snprintf(buf, sizeof buf, "%d", (int)(spd * 3.6f));
		T(buf, 394, 190, 0.9f, spd > 30 ? GOLD : WHITE, AL_R);
		T("km/h", 394, 216, 0.32f, RGBA(255, 255, 255, 180), AL_R);
		if (c->kind == CAR_HELI) { snprintf(buf, sizeof buf, "ALT %d m", (int)(c->y + 0.5f)); T(buf, 394, 174, 0.45f, RGBA(150, 220, 255, 255), AL_R); }
		bar(306, 232, 88, 4, c->hp / 100.0f, c->hp < 30 ? RGBA(255, 120, 40, 255) : RGBA(120, 220, 120, 255), RGBA(40, 40, 40, 255));
	} else if (game_nearest_car(g, p->p[0], p->p[2], 3.4f) >= 0 && !g->paused && p->ps == PS_ALIVE) {
		T("X   get in", 250, 190, 0.55f, GOLD, AL_C);
	}
	// arrest meter
	if (p->arrestT > 0.05f && p->ps == PS_ALIVE) {
		float lim = p->car >= 0 ? 2.2f : 1.4f;
		bar(150, 168, 100, 8, p->arrestT / lim, RGBA(80, 140, 255, 255), RGBA(20, 30, 60, 200));
		T("being arrested!", 200, 150, 0.4f, RGBA(120, 170, 255, 255), AL_C);
	}
	// small messages
	if (g->toastT > 0) T(g->toast, 200, 120, 0.55f, with_alpha(WHITE, fminf(1.0f, g->toastT) * 255), AL_C);
	if (g->hintT > 0 && !g->paused) {
		float a = g->hintT > 1 ? 255 : g->hintT * 255;
		float t = 9.0f - g->hintT;
		if (t < 4.0f) T("GRAND THEFT OTTO II", 200, 40, 0.9f, with_alpha(ORANGE, a), AL_C);
		T("R shoot   L aim / lock on   SELECT weapons   X get in   B B B sprint   START pause", 200, 70, 0.36f, with_alpha(WHITE, a * 0.85f), AL_C);
	}
	// WASTED / BUSTED
	if (p->ps != PS_ALIVE) {
		float k = fminf(1.0f, p->psT / 0.8f);
		panel(0, 0, 400, 240, RGBA(0, 0, 0, (int)(110 * k)));
		bool w = p->ps == PS_WASTED;
		T(w ? "WASTED" : "BUSTED", 200, 78, 1.8f * (1.3f - 0.3f * k), w ? RGBA(220, 30, 30, (int)(255 * k)) : RGBA(80, 130, 255, (int)(255 * k)), AL_C);
	}
	if (g->wheel) weapon_wheel(g);
	if (g->paused) {
		panel(0, 0, 400, 240, RGBA(0, 0, 0, 140));
		T("PAUSED", 200, 30, 1.0f, WHITE, AL_C);
		T("START  resume", 200, 100, 0.55f, WHITE, AL_C);
		snprintf(buf, sizeof buf, "D-pad down: invert camera Y  (%s)", g->invertY ? "on" : "off"); T(buf, 200, 160, 0.4f, RGBA(255, 255, 255, 200), AL_C);
		snprintf(buf, sizeof buf, "D-pad up: show FPS  (%s)", g->showFps ? "on" : "off"); T(buf, 200, 178, 0.4f, RGBA(255, 255, 255, 200), AL_C);
	}
	if (g->showFps) { snprintf(buf, sizeof buf, "%d fps", (int)(g->fps + 0.5f)); T(buf, 4, 192, 0.35f, RGBA(180, 255, 180, 255), AL_L); }
}

// ---- bottom screen: the city map and weapon buttons -------------------------------------------------------------------------------------------
#define MAP_X 6.0f
#define MAP_Y 6.0f
#define MAP_X0 BEACH_X                                    // the map shows the beach strip too
#define MAP_S (232.0f / (CITY - MAP_X0))

static void map_pt(float wx, float wz, float *x, float *y) { *x = MAP_X + clampf(wx - MAP_X0, 0, CITY - MAP_X0) * MAP_S; *y = MAP_Y + wz * MAP_S; }

static void city_map(const Game *g)
{
	float side = CITY * MAP_S, wide = (CITY - MAP_X0) * MAP_S;
	panel(MAP_X - 3, MAP_Y - 3, wide + 6, side + 6, RGBA(8, 20, 40, 255));
	panel(MAP_X, MAP_Y, wide, side, RGBA(70, 72, 80, 255));
	{ float sx0, sy0; map_pt(MAP_X0, 0, &sx0, &sy0); float ex, ey; map_pt(-14.0f, 0, &ex, &ey); panel(sx0, sy0, ex - sx0, side, RGBA(214, 192, 142, 255)); }                // sand
	{ float ex, ey, px, py; map_pt(-14.0f, 0, &ex, &ey); map_pt(0, 0, &px, &py); panel(ex, ey, px - ex, side, RGBA(150, 146, 140, 255)); }                                        // boardwalk
	{ float px, py; map_pt(MAP_X0, PIER_Z0, &px, &py); panel(MAP_X, py, 6, (PIER_Z1 - PIER_Z0) * MAP_S, RGBA(150, 110, 70, 255)); }                                          // the pier heads off to the west
	const World *w = &g->w;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		float x0 = (i + 0.15f) * PITCH, z0 = (j + 0.15f) * PITCH, sz = 0.7f * PITCH;
		float mx, my; map_pt(x0, z0, &mx, &my);
		if (w->park[j][i]) { panel(mx, my, sz * MAP_S, sz * MAP_S, RGBA(52, 120, 76, 255)); continue; }
		float top = 0;
		for (int k = w->bfirst[j][i]; k < w->bfirst[j][i] + w->bcount[j][i]; k++) if (w->box[k].y1 > top) top = w->box[k].y1;
		float t = clampf(top / 100.0f, 0, 1);
		panel(mx, my, sz * MAP_S, sz * MAP_S, RGBA((int)(150 + 40 * t), (int)(140 + 60 * t), (int)(125 + 100 * t), 255));
	}
	// places: hospital (red cross), police station (blue), Pay 'n' Spray (purple)
	{
		float x, y;
		map_pt(HOSPITAL_X, HOSPITAL_Z, &x, &y); panel(x - 4, y - 4, 8, 8, RGBA(250, 250, 250, 255)); panel(x - 3, y - 1, 6, 2, RGBA(220, 30, 30, 255)); panel(x - 1, y - 3, 2, 6, RGBA(220, 30, 30, 255));
		map_pt(POLICE_X, POLICE_Z, &x, &y); panel(x - 4, y - 4, 8, 8, RGBA(40, 80, 220, 255)); star(x, y, 3.2f, RGBA(255, 255, 255, 255));
		map_pt(SPRAY_X, SPRAY_Z, &x, &y); panel(x - 3, y - 3, 6, 6, RGBA(190, 70, 240, 255));
		for (int i = 0; i < N_PADS; i++) { map_pt(PADS[i][0], PADS[i][1], &x, &y); panel(x - 3, y - 3, 6, 6, RGBA(60, 70, 90, 255)); panel(x - 2, y - 0.5f, 4, 1, RGBA(255, 255, 255, 255)); panel(x - 2, y - 2, 1, 4, RGBA(255, 255, 255, 255)); panel(x + 1, y - 2, 1, 4, RGBA(255, 255, 255, 255)); }
	}
	for (int i = 0; i < MAX_PICKUP; i++) {
		const Pickup *k = &g->pick[i];
		if (!k->on) continue;
		static const unsigned char col[PK_COUNT][3] = { { 90, 230, 110 }, { 240, 70, 70 }, { 90, 130, 240 }, { 255, 215, 90 }, { 255, 215, 90 }, { 255, 215, 90 } };
		float x, y; map_pt(k->x, k->z, &x, &y);
		panel(x - 1, y - 1, 2, 2, RGBA(col[k->type][0], col[k->type][1], col[k->type][2], 255));
	}
	for (int i = 0; i < MAX_CAR; i++) {
		if (!g->car[i].on || g->car[i].driven || g->car[i].wreck) continue;
		float cx, cy; map_pt(g->car[i].x, g->car[i].z, &cx, &cy);
		bool cop = g->car[i].bot;
		if (g->car[i].kind == CAR_HELI) { panel(cx - 2.5f, cy - 2.5f, 5, 5, RGBA(120, 220, 255, 255)); continue; }
		panel(cx - 1.5f, cy - 1.5f, 3, 3, cop ? (((int)(g->time * 4) & 1) ? RGBA(255, 60, 60, 255) : RGBA(70, 110, 255, 255)) : g->car[i].ai.on ? RGBA(230, 240, 255, 255) : RGBA(255, 220, 90, 255));
	}
	for (int i = 0; i < MAX_PED; i++) {
		const Ped *q = &g->ped[i];
		if (!q->on || !q->cop || q->dead || g->stars == 0) continue;
		float x, y; map_pt(q->x, q->z, &x, &y);
		panel(x - 1.5f, y - 1.5f, 3, 3, ((int)(g->time * 4) & 1) ? RGBA(255, 60, 60, 255) : RGBA(70, 110, 255, 255));
	}
	float px, py; map_pt(g->p.p[0], g->p.p[2], &px, &py);
	tri_dir(px, py, 6.5f, g->cam.yaw, RGBA(240, 100, 20, 255));
	tri_dir(px, py, 3.5f, g->cam.yaw, RGBA(255, 255, 255, 255));
	T("N", MAP_X + wide * 0.5f, MAP_Y + 1, 0.4f, RGBA(255, 255, 255, 200), AL_C);
}

static void weapon_buttons(const Game *g)
{
	static const char *short_[W_COUNT] = { "Fists", "Pistol", "SMG", "Shotgun" };
	char buf[24];
	for (int i = 0; i < W_COUNT; i++) {
		float x = 6.0f + i * 58.5f, y = 216.0f;
		bool has = g->p.has[i] && (i == W_FISTS || g->p.ammo[i] > 0), sel = g->p.weapon == i;
		panel(x, y, 55, 22, sel ? RGBA(200, 90, 20, 255) : has ? RGBA(46, 54, 78, 255) : RGBA(26, 30, 40, 255));
		T(short_[i], x + 27.5f, y, 0.32f, has ? WHITE : RGBA(255, 255, 255, 80), AL_C);
		if (i != W_FISTS) { ammo_text(buf, sizeof buf, g->p.ammo[i]); T(buf, x + 27.5f, y + 10, 0.32f, has ? GOLD : RGBA(255, 255, 255, 70), AL_C); }
	}
}

void hud_bottom(const Game *g)
{
	panel(0, 0, 320, 240, RGBA(14, 16, 22, 255));
	if (g->paused) { help_page(); return; }
	char buf[48];
	city_map(g);
	weapon_buttons(g);
	float x = 246;
	T("OTTO", x, 6, 0.6f, ORANGE, AL_L);
	snprintf(buf, sizeof buf, "$%d", g->p.cash); T(buf, x, 26, 0.55f, GOLD, AL_L);
	T(g->stars ? "WANTED" : "clean", x, 48, 0.4f, g->stars ? RGBA(255, 120, 100, 255) : RGBA(150, 220, 150, 255), AL_L);
	T("blue / red", x, 90, 0.3f, RGBA(255, 255, 255, 140), AL_L); T("= police", x, 101, 0.3f, RGBA(255, 255, 255, 140), AL_L);
	T("purple =", x, 118, 0.3f, RGBA(255, 255, 255, 140), AL_L); T("paint shop", x, 129, 0.3f, RGBA(255, 255, 255, 140), AL_L);
	T("START:", x, 190, 0.3f, RGBA(255, 255, 255, 140), AL_L); T("controls", x, 201, 0.3f, RGBA(255, 255, 255, 140), AL_L);
}
