// Web Slinger - HUD: score, timer, speed, beacon pointer, speed lines, menus; bottom screen: city map and menus.
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
static u32 with_alpha(u32 c, float a) { return (c & 0x00FFFFFFu) | ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24); }

static void tri_dir(float cx, float cy, float r, float ang, u32 col)         // arrow triangle pointing along ang (0 = up)
{
	float s = sinf(ang), c = cosf(ang);
	float px[3] = { 0, -0.7f, 0.7f }, py[3] = { -1.0f, 0.8f, 0.8f }, ox[3], oy[3];
	for (int i = 0; i < 3; i++) { ox[i] = cx + (px[i] * c - py[i] * s) * r; oy[i] = cy + (px[i] * s + py[i] * c) * r; }
	C2D_DrawTriangle(ox[0], oy[0], col, ox[1], oy[1], col, ox[2], oy[2], col, 0);
}

static void panel(float x, float y, float w, float h, u32 col) { C2D_DrawRectSolid(x, y, 0, w, h, col); }

// ---- speed lines ---------------------------------------------------------------------------------------------------------------
static void speed_lines(const Game *g)
{
	float k = clampf((g->p.speed - 26.0f) / 26.0f, 0, 1);
	if (k <= 0 || g->p.state == ST_GROUND) return;
	for (int i = 0; i < 22; i++) {
		float a = i * 2 * PI_F / 22 + (i * 7 % 5) * 0.05f + (float)((int)(g->time * 20)) * 0.0f;
		float jit = ((i * 13 + (int)(g->time * 30) * 7) % 10) / 10.0f;
		float r0 = 88 + jit * 40, r1 = r0 + 30 + k * 40 + jit * 20;
		float ca = cosf(a), sa = sinf(a), px = -sa * 0.8f, py = ca * 0.8f;
		float x0 = 200 + ca * r0 * 1.7f, y0 = 120 + sa * r0, x1 = 200 + ca * r1 * 1.7f, y1 = 120 + sa * r1;
		u32 c0 = RGBA(255, 255, 255, (int)(k * 70)), c1 = RGBA(255, 255, 255, 0);
		C2D_DrawTriangle(x0 - px, y0 - py, c0, x0 + px, y0 + py, c0, x1, y1, c1, 0);
	}
}

// ---- beacon pointer ----------------------------------------------------------------------------------------------------------------
static void beacon_marker(const Game *g, const Scene *s)
{
	if (!g->bc.on) return;
	float sx, sy;
	float dx = g->bc.p[0] - g->p.p[0], dy = g->bc.p[1] - g->p.p[1], dz = g->bc.p[2] - g->p.p[2];
	float d = sqrtf(dx * dx + dy * dy + dz * dz);
	char buf[24]; snprintf(buf, sizeof buf, "%dm", (int)d);
	bool vis = scene_project(s, g->bc.p, &sx, &sy);
	if (vis && sx > 14 && sx < 386 && sy > 20 && sy < 226) {
		float p = 6 + 2 * sinf(g->time * 6);
		C2D_DrawRectSolid(sx - 1, sy - p - 7, 0, 2, 5, GOLD);
		tri_dir(sx, sy - p - 2, 4.5f, PI_F, GOLD);
		T(buf, sx, sy + 6, 0.4f, GOLD, AL_C);
		return;
	}
	// off screen: an arrow on the screen border pointing toward it
	float vx, vy;
	if (vis) { vx = sx - 200; vy = sy - 120; }
	else {                                                  // direction in camera space
		const float *m = s->view;
		float rx = m[0] * dx + m[1] * dy + m[2] * dz, ry = m[4] * dx + m[5] * dy + m[6] * dz;
		vx = rx; vy = -ry;
		if (fabsf(vx) < 0.001f && fabsf(vy) < 0.001f) vy = 1;
	}
	float l = sqrtf(vx * vx + vy * vy); vx /= l; vy /= l;
	float t = fminf(fabsf(vx) > 0.001f ? 170.0f / fabsf(vx) : 1e9f, fabsf(vy) > 0.001f ? 92.0f / fabsf(vy) : 1e9f);
	float ax = 200 + vx * t, ay = 122 + vy * t;
	tri_dir(ax, ay, 9, atan2f(vx, -vy), GOLD);
	T(buf, ax - vx * 22, ay - vy * 18 - 6, 0.4f, GOLD, AL_C);
}

// ---- top screen ----------------------------------------------------------------------------------------------------------------------
void hud_top(const Game *g, const Scene *s)
{
	char buf[64];
	if (g->state == GS_TITLE) {
		C2D_DrawRectangle(0, 0, 0, 400, 70, RGBA(0, 20, 60, 120), RGBA(0, 20, 60, 120), RGBA(0, 20, 60, 0), RGBA(0, 20, 60, 0));
		T("WEB SLINGER", 200, 6, 1.15f, WHITE, AL_C);
		T("swing through the city", 200, 56, 0.45f, RGBA(255, 255, 255, 230), AL_C);
		return;
	}
	if (g->state == GS_PLAY || g->state == GS_OVER) {
		speed_lines(g);
		// score block (top-left)
		panel(0, 0, 150, 40, RGBA(0, 0, 0, 90));
		snprintf(buf, sizeof buf, "%d", g->score);
		T("SCORE", 8, 2, 0.34f, RGBA(255, 255, 255, 180), AL_L);
		T(buf, 8, 12, 0.7f, WHITE, AL_L);
		if (g->chain > 1 && g->chainT > 0) { snprintf(buf, sizeof buf, "x%d", g->chain); T(buf, 144, 12, 0.7f, GOLD, AL_R); }
		// timer / mode (top centre)
		if (g->mode == MODE_RUN) {
			int t = (int)ceilf(g->timeLeft);
			snprintf(buf, sizeof buf, "%d:%02d", t / 60, t % 60);
			T(buf, 200, 2, 0.9f, g->timeLeft < 10 ? ((((int)(g->time * 4)) & 1) ? RGBA(255, 90, 80, 255) : WHITE) : WHITE, AL_C);
		} else T("FREE ROAM", 200, 4, 0.5f, RGBA(255, 255, 255, 200), AL_C);
		// speed (top-right)
		panel(268, 0, 132, 40, RGBA(0, 0, 0, 90));
		snprintf(buf, sizeof buf, "%d", (int)(g->p.speed * 3.6f));
		T("SPEED  km/h", 392, 2, 0.34f, RGBA(255, 255, 255, 180), AL_R);
		T(buf, 392, 12, 0.7f, g->p.speed > 38 ? GOLD : WHITE, AL_R);
		// height
		snprintf(buf, sizeof buf, "%d m up", (int)(g->p.p[1] > 0 ? g->p.p[1] : 0));
		T(buf, 392, 42, 0.34f, RGBA(255, 255, 255, 190), AL_R);
		beacon_marker(g, s);
		// popups
		for (int i = 0; i < 3; i++) if (g->pop[i].t > 0) {
			float a = g->pop[i].t > 0.5f ? 255 : g->pop[i].t * 510;
			T(g->pop[i].text, 200, 62 + i * 15, 0.5f, with_alpha(g->pop[i].col, a), AL_C);
		}
		if (g->hintT > 0 && g->state == GS_PLAY) {
			float a = g->hintT > 1 ? 230 : g->hintT * 230;
			T("R swing   A jump (hold = higher)   A at swing end = boost   A A = trick", 200, 214, 0.36f, with_alpha(WHITE, a), AL_C);
		}
		if (g->state == GS_PLAY && !g->paused && g->p.state == ST_AIR && !g->p.rope && g->p.airT > 0.4f && g->p.airT < 3.0f) T("hold R to swing", 200, 188, 0.42f, with_alpha(WHITE, 200), AL_C);
		if (g->p.charge > 0.02f && g->state == GS_PLAY) {                                                       // jump charge bar
			panel(150, 196, 100, 8, RGBA(0, 0, 0, 140)); panel(151, 197, 98 * g->p.charge, 6, g->p.charge > 0.85f ? GOLD : WHITE);
			T("release to jump", 200, 180, 0.36f, with_alpha(WHITE, 220), AL_C);
		}
		if (g->p.state == ST_WALL) T("climbing  -  A to jump off", 200, 188, 0.4f, with_alpha(WHITE, 200), AL_C);
	}
	if (g->state == GS_OVER) {
		panel(0, 0, 400, 240, RGBA(0, 0, 0, 120));
		T("TIME UP", 200, 36, 1.2f, WHITE, AL_C);
		snprintf(buf, sizeof buf, "Score  %d", g->score); T(buf, 200, 100, 0.8f, GOLD, AL_C);
		snprintf(buf, sizeof buf, "Beacons %d    Best %d", g->beacons, g->best); T(buf, 200, 136, 0.5f, WHITE, AL_C);
		snprintf(buf, sizeof buf, "Top speed %d km/h    Flown %d m", (int)(g->topSpeed * 3.6f), (int)g->flown); T(buf, 200, 158, 0.4f, RGBA(255, 255, 255, 200), AL_C);
		if (g->stateT > 0.8f) T("A  swing again        B  menu", 200, 196, 0.5f, (((int)(g->time * 2)) & 1) ? WHITE : GOLD, AL_C);
	}
	if (g->paused) {
		panel(0, 0, 400, 240, RGBA(0, 0, 0, 140));
		T("PAUSED", 200, 30, 1.0f, WHITE, AL_C);
		T("START  resume", 200, 100, 0.55f, WHITE, AL_C);
		T("B  quit to menu", 200, 124, 0.55f, WHITE, AL_C);
		snprintf(buf, sizeof buf, "D-pad down: invert camera Y  (%s)", g->invertY ? "on" : "off"); T(buf, 200, 160, 0.4f, RGBA(255, 255, 255, 200), AL_C);
		snprintf(buf, sizeof buf, "D-pad up: show FPS  (%s)", g->showFps ? "on" : "off"); T(buf, 200, 178, 0.4f, RGBA(255, 255, 255, 200), AL_C);
	}
	if (g->showFps) { snprintf(buf, sizeof buf, "%d fps", (int)(g->fps + 0.5f)); T(buf, 4, 224, 0.35f, RGBA(180, 255, 180, 255), AL_L); }
}

// ---- bottom screen: the city map ---------------------------------------------------------------------------------------------------------
#define MAP_X 6.0f
#define MAP_Y 6.0f
#define MAP_S 0.4f

static void map_pt(float wx, float wz, float *x, float *y) { *x = MAP_X + wx * MAP_S; *y = MAP_Y + wz * MAP_S; }

static void city_map(const Game *g)
{
	float side = CITY * MAP_S;
	panel(MAP_X - 3, MAP_Y - 3, side + 6, side + 6, RGBA(8, 20, 40, 255));
	panel(MAP_X, MAP_Y, side, side, RGBA(18, 52, 92, 255));
	const World *w = &g->w;
	for (int j = 0; j < NB; j++) for (int i = 0; i < NB; i++) {
		float x0 = (i + 0.15f) * PITCH, z0 = (j + 0.15f) * PITCH, sz = 0.7f * PITCH;
		float mx, my; map_pt(x0, z0, &mx, &my);
		if (w->park[j][i]) { panel(mx, my, sz * MAP_S, sz * MAP_S, RGBA(52, 120, 76, 255)); continue; }
		float top = 0;
		for (int k = w->bfirst[j][i]; k < w->bfirst[j][i] + w->bcount[j][i]; k++) if (w->box[k].y1 > top) top = w->box[k].y1;
		float t = clampf(top / 200.0f, 0, 1);
		panel(mx, my, sz * MAP_S, sz * MAP_S, RGBA((int)(60 + 60 * t), (int)(130 + 70 * t), (int)(180 + 60 * t), 255));
	}
	{ float mx, my; map_pt(4.5f * PITCH - 6, 4.5f * PITCH - 6, &mx, &my); panel(mx, my, 12 * MAP_S * 1.4f, 12 * MAP_S * 1.4f, RGBA(255, 230, 150, 255)); }
	// beacon
	if (g->bc.on) {
		float bx, by; map_pt(g->bc.p[0], g->bc.p[2], &bx, &by);
		float p = 4.5f + 1.5f * sinf(g->time * 6);
		panel(bx - p, by - 1, p * 2, 2, GOLD); panel(bx - 1, by - p, 2, p * 2, GOLD);
		panel(bx - 3, by - 3, 6, 6, RGBA(255, 255, 255, 255));
	}
	// the player: an arrow pointing the way the camera looks
	float px, py; map_pt(g->p.p[0], g->p.p[2], &px, &py);
	tri_dir(px, py, 6.5f, g->cam.yaw, RGBA(255, 70, 60, 255));
	tri_dir(px, py, 3.5f, g->cam.yaw, RGBA(255, 255, 255, 255));
	T("N", MAP_X + side * 0.5f, MAP_Y + 1, 0.4f, RGBA(255, 255, 255, 200), AL_C);
}

static void side_panel(const Game *g)
{
	char buf[48];
	float x = 242, y = 8;
	T("SCORE", x, y, 0.4f, RGBA(255, 255, 255, 170), AL_L);
	snprintf(buf, sizeof buf, "%d", g->score); T(buf, x, y + 11, 0.7f, WHITE, AL_L);
	T("BEST", x, y + 40, 0.4f, RGBA(255, 255, 255, 170), AL_L);
	snprintf(buf, sizeof buf, "%d", g->best); T(buf, x, y + 51, 0.6f, GOLD, AL_L);
	T("BEACONS", x, y + 80, 0.4f, RGBA(255, 255, 255, 170), AL_L);
	snprintf(buf, sizeof buf, "%d", g->beacons); T(buf, x, y + 91, 0.6f, WHITE, AL_L);
	if (g->mode == MODE_RUN) { int t = (int)ceilf(g->timeLeft); T("TIME", x, y + 120, 0.4f, RGBA(255, 255, 255, 170), AL_L); snprintf(buf, sizeof buf, "%d:%02d", t / 60, t % 60); T(buf, x, y + 131, 0.6f, WHITE, AL_L); }
	T("SELECT", x, 190, 0.3f, RGBA(255, 255, 255, 140), AL_L);
	T("back to street", x, 200, 0.3f, RGBA(255, 255, 255, 140), AL_L);
	T("drag: camera", x, 214, 0.3f, RGBA(255, 255, 255, 140), AL_L);
}

static void help_page(void)
{
	T("CONTROLS", 160, 6, 0.6f, GOLD, AL_C);
	static const char *rows[][2] = {
		{ "Circle Pad", "run / steer" }, { "R  (hold)", "web swing" }, { "A  (tap)", "jump" },
		{ "A  (hold, release)", "charge a super jump" }, { "A  at end of swing", "launch boost" }, { "A A  in the air", "flip / roll (stick = direction)" },
		{ "X", "web zip to a high point" }, { "L  (hold)", "sprint / climb faster" }, { "B", "dive  (in the air)" },
		{ "D-pad / C-stick / touch", "move the camera" }, { "Y   SELECT   START", "camera behind / street / pause" } };
	for (int i = 0; i < 11; i++) { T(rows[i][0], 8, 30 + i * 16.5f, 0.38f, WHITE, AL_L); T(rows[i][1], 122, 30 + i * 16.5f, 0.38f, RGBA(255, 255, 255, 200), AL_L); }
	T("Touch a wall in the air to cling to it.", 160, 222, 0.34f, GOLD, AL_C);
}

void hud_bottom(const Game *g)
{
	char buf[48];
	if (g->state == GS_TITLE) {
		panel(0, 0, 320, 240, RGBA(14, 22, 40, 255));
		if (g->showHelp) { help_page(); return; }
		static const char *names[3] = { "FREE ROAM", "BEACON RUN", "CONTROLS" };
		for (int i = 0; i < 3; i++) {
			float y = 78 + i * 46;
			bool sel = i == g->menuSel;
			C2D_DrawRectSolid(50, y, 0, 220, 40, sel ? RGBA(200, 40, 40, 255) : RGBA(40, 52, 80, 255));
			C2D_DrawRectSolid(50, y + 36, 0, 220, 4, sel ? RGBA(255, 120, 90, 255) : RGBA(26, 34, 56, 255));
			T(names[i], 160, y + 8, 0.7f, WHITE, AL_C);
		}
		T("WEB SLINGER", 160, 12, 0.9f, WHITE, AL_C);
		snprintf(buf, sizeof buf, "Best beacon run: %d", g->best); T(buf, 160, 52, 0.42f, GOLD, AL_C);
		T("D-pad + A  or  touch        START  exit", 160, 224, 0.32f, RGBA(255, 255, 255, 140), AL_C);
		return;
	}
	panel(0, 0, 320, 240, RGBA(8, 14, 28, 255));
	city_map(g);
	side_panel(g);
}
