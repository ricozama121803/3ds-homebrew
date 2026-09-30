#include "render.h"

#ifdef HOST_PREVIEW
#include "../host/c2d_shim.h"
#define IMG_W(i) ((i).width)
#define IMG_H(i) ((i).height)
#else
#include <3ds.h>
#include <citro2d.h>
#include "landmarks_t3x.h"                 // texture atlas built from assets/gfx (see Makefile)
#define IMG_W(i) ((int)(i).subtex->width)
#define IMG_H(i) ((int)(i).subtex->height)
#endif

#include <math.h>
#include <stdio.h>

#define RGBA(r, g, b, a) C2D_Color32((r), (g), (b), (a))
#define RGB(r, g, b)     C2D_Color32((r), (g), (b), 255)

#define SCR_W   400.0f
#define SCR_H   240.0f
#define HORIZON 66.0f      // y of the horizon on the top screen
#define FOCAL   200.0f     // perspective strength
static float g_camh = 3.0f;     // camera height above the road (it rises while Tuffy is launched)
#define CAMH    g_camh
#define ROAD_HW 2.7f       // half width of the road

#define FOG_CAMPUS RGB(246, 200, 150)      // sunset haze over the campus...
#define FOG_DIZZY  RGB(255, 190, 216)      // ...and pink haze over the fake theme park
#define FOG_STAD   RGB(196, 218, 244)      // ...and a bright daytime haze over the ball park

// ---- state shared by the drawing helpers -------------------------------
static float g_eye, g_sep;        // stereoscopic eye (-1/+1) and separation in pixels
static float g_camx;              // camera follows Tuffy sideways a little
static float g_shx, g_shy;        // screen shake
static float g_time;              // game clock, for things that wiggle
static float g_zk, g_sk;          // 0..1: how much theme park / ball park is in the distance (drives sky and haze)
static u32   g_fog;               // current haze colour (set every frame)
static float g_lmoff;             // how far the road's edges have moved out (extra lanes), for landmarks
static float g_head;              // how far the camera has turned (radians), pans the sky
static const Game *g_game;        // the game being drawn (the bend of the road depends on it)
static bool  g_curve_on;          // is any of the road currently bent?

static C2D_TextBuf s_static, s_dyn;
static C2D_Text t_title, t_titans, t_paused, t_crashed, t_newbest;
static C2D_Text t_score, t_best, t_coins, t_scoreBig, t_bestBig, t_coinsBig;
static C2D_Text t_start, t_retry, t_help1, t_help2, t_help3, t_resume, t_quit, t_hdr;
static C2D_Text t_lScore, t_lBest, t_lCoins, t_cause, t_exit, t_scooter, t_help4;
static C2D_Text t_turnL, t_turnR, t_welcome, t_bye, t_csuf, t_cal, t_fuller, t_sign;
static C2D_Text t_angle, t_anahim, t_farce, t_playball, t_launch, t_base[4];
enum { LMT_CASTLE, LMT_MATTER, LMT_DUMPO, LMT_WHEEL, LMT_MOOKEY, LMT_SPACE, LMT_TEACUP, LMT_STREET, LMT_COUNT };
static C2D_Text t_lm[LMT_COUNT];                                  // names on the fake theme-park landmarks

// campus landmark sprites (cut out of photos by assets/make_landmarks.py); the atlas order matches assets/landmarks.t3s
enum { IMG_POLLAK, IMG_TSU, IMG_STATUE, IMG_COUNT };
#define IMG_MARGIN 2                       // transparent margin around each sprite in the atlas
static C2D_Image s_img[IMG_COUNT];
#ifndef HOST_PREVIEW
static C2D_SpriteSheet s_sheet;
#endif
static void draw_scooter(const Game *g, const Obj *o);

// ---- small helpers -----------------------------------------------------
static u32 mixc(u32 a, u32 b, float t)
{
	int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
	int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
	return C2D_Color32((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t), (int)(ab + (bb - ab) * t), 255);
}

// far things fade into the sunset haze
static u32 fogc(u32 c, float z)
{
	float t = (z - 12.0f) / 58.0f;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return mixc(c, g_fog, t * 0.85f);
}

// blends a campus colour into its theme-park twin
static u32 themed(u32 campus, u32 dizzy, float k) { return k <= 0.002f ? campus : k >= 0.998f ? dizzy : mixc(campus, dizzy, k); }
static u32 withalpha(u32 c, int a) { return (c & 0x00FFFFFFu) | ((u32)a << 24); }

static u32 theme3(u32 campus, u32 dizzy, u32 stad, float kd, float ks) { return themed(themed(campus, dizzy, kd), stad, ks); }
static u32 haze_for(float kd, float ks) { return theme3(FOG_CAMPUS, FOG_DIZZY, FOG_STAD, kd, ks); }
u32 render_haze(const Game *g) { return haze_for(zone_k(g, g->dist + 40.0f), stadium_k(g, g->dist + 40.0f)); }

static float S(float z)  { return FOCAL / (z < 1.0f ? 1.0f : z); }
static float PX(float wx, float z)                  // the road bends by sliding everything sideways, more the farther it is
{
	float cv = g_curve_on ? track_curve(g_game, z) : 0.0f;
	return SCR_W * 0.5f + (wx + cv - g_camx) * S(z) + g_eye * g_sep * (1.0f - PLAYER_Z / z) + g_shx;
}
static float PY(float wy, float z) { return HORIZON + (CAMH - wy) * S(z) + g_shy; }

// Ellipses are drawn as triangle fans. citro2d's own ellipse call needs an expensive GPU state
// switch whenever it alternates with rectangles/triangles, and this game alternates constantly.
static void fill_ellipse(float x, float y, float z, float w, float h, u32 c)
{
	(void)z;
	if (w <= 0.5f || h <= 0.5f) return;
	float rx = w * 0.5f, ry = h * 0.5f, cx = x + rx, cy = y + ry;
	float r = fmaxf(rx, ry);
	int n = r < 8 ? 8 : r < 20 ? 12 : r < 40 ? 16 : 22;
	static float cs[23], sn[23];
	static int tabN = 0;
	if (tabN != n) {                                    // (re)build the unit-circle table
		for (int i = 0; i <= n; i++) { cs[i] = cosf(i * 6.2831853f / n); sn[i] = sinf(i * 6.2831853f / n); }
		tabN = n;
	}
	float px = cx + rx, py = cy;
	for (int i = 1; i <= n; i++) {
		float nx = cx + cs[i] * rx, ny = cy + sn[i] * ry;
		C2D_DrawTriangle(cx, cy, c, px, py, c, nx, ny, c, 0);
		px = nx; py = ny;
	}
}

static void fill_circle(float cx, float cy, float z, float r, u32 c)
{
	fill_ellipse(cx - r, cy - r, z, r * 2, r * 2, c);
}

static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c)
{
	C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0);
	C2D_DrawTriangle(x0, y0, c, x2, y2, c, x3, y3, c, 0);
}

// A 3D box in world space: front face, top face and the side face that faces the camera.
static void box(float x0, float x1, float y0, float y1, float z0, float z1,
                u32 front, u32 top, u32 side, u32 edge)
{
	if (z0 < 1.3f) return;
	float fx = z0;
	front = fogc(front, fx); top = fogc(top, fx); side = fogc(side, fx); edge = fogc(edge, fx);

	float ax = PX(x0, z0), bx = PX(x1, z0), ty = PY(y1, z0), by = PY(y0, z0);

	if (g_camx < x0)       // left face is visible
		quad(PX(x0, z0), PY(y0, z0), PX(x0, z1), PY(y0, z1), PX(x0, z1), PY(y1, z1), PX(x0, z0), PY(y1, z0), side);
	else if (g_camx > x1)  // right face is visible
		quad(PX(x1, z0), PY(y0, z0), PX(x1, z1), PY(y0, z1), PX(x1, z1), PY(y1, z1), PX(x1, z0), PY(y1, z0), side);

	if (CAMH > y1)
		quad(PX(x0, z0), PY(y1, z0), PX(x1, z0), PY(y1, z0), PX(x1, z1), PY(y1, z1), PX(x0, z1), PY(y1, z1), top);

	float o = fmaxf(1.0f, 0.045f * S(z0));
	C2D_DrawRectSolid(ax - o, ty - o, 0, (bx - ax) + 2 * o, (by - ty) + 2 * o, edge);
	C2D_DrawRectSolid(ax, ty, 0, bx - ax, by - ty, front);
}

static void ell3d(float wx, float wy, float z, float w, float h, u32 c)   // ellipse in world units
{
	float s = S(z);
	fill_ellipse(PX(wx, z) - w * s * 0.5f, PY(wy, z) - h * s * 0.5f, 0, w * s, h * s, fogc(c, z));
}

// ---- text --------------------------------------------------------------
static void text(const C2D_Text *t, float x, float y, float sc, u32 col, bool center, float pop)
{
	u32 flags = C2D_WithColor | (center ? C2D_AlignCenter : 0);
	x += -g_eye * g_sep * pop;
	C2D_DrawText(t, flags, x + 1.5f, y + 1.5f, 0, sc, sc, RGBA(0, 0, 0, 200));
	C2D_DrawText(t, flags, x, y, 0, sc, sc, col);
}

static void parse(C2D_Text *t, C2D_TextBuf b, const char *s)
{
	C2D_TextParse(t, b, s);
	C2D_TextOptimize(t);
}

void render_init(void)
{
	g_fog = FOG_CAMPUS;
	s_static = C2D_TextBufNew(2048);
	s_dyn    = C2D_TextBufNew(512);
	parse(&t_title,   s_static, "TUFFY RUN");
	parse(&t_titans,  s_static, "TITANS");
	parse(&t_paused,  s_static, "PAUSED");
	parse(&t_crashed, s_static, "CRASHED!");
	parse(&t_newbest, s_static, "NEW BEST!");
	parse(&t_start,   s_static, "Press A or tap to start");
	parse(&t_retry,   s_static, "Press A or tap to retry");
	parse(&t_help1,   s_static, "Swipe or D-Pad: change lane");
	parse(&t_help2,   s_static, "Swipe up / A: jump");
	parse(&t_help3,   s_static, "Swipe down / B: slide");
	parse(&t_resume,  s_static, "START: resume");
	parse(&t_quit,    s_static, "SELECT: quit");
	parse(&t_exit,    s_static, "START: quit");
	parse(&t_hdr,     s_static, "TUFFY RUN");
	parse(&t_lScore,  s_static, "SCORE");
	parse(&t_lBest,   s_static, "BEST");
	parse(&t_lCoins,  s_static, "COINS");
	parse(&t_scooter, s_static, "SCOOTER!");
	parse(&t_help4,   s_static, "Dodge scooters coming from behind!");
	parse(&t_turnL,   s_static, "<< TURN AHEAD");
	parse(&t_turnR,   s_static, "TURN AHEAD >>");
	parse(&t_welcome, s_static, "WELCOME TO DIZZYLAND");
	parse(&t_bye,     s_static, "COME BACK SOON!");
	parse(&t_csuf,    s_static, "CSUF");
	parse(&t_cal,     s_static, "CALIFORNIA STATE UNIVERSITY");
	parse(&t_fuller,  s_static, "FULLERTON");
	parse(&t_sign,    s_static, "SIGN HERE!");
	parse(&t_angle,   s_static, "ANGLE STADIUM");
	parse(&t_anahim,  s_static, "ANAHIM");
	parse(&t_farce,   s_static, "STATE FARCE");
	parse(&t_playball, s_static, "PLAY BALL!");
	parse(&t_launch,  s_static, "LAUNCH!");
	parse(&t_base[0], s_static, "1ST BASE");
	parse(&t_base[1], s_static, "2ND BASE");
	parse(&t_base[2], s_static, "3RD BASE");
	parse(&t_base[3], s_static, "HOME PLATE");
	static const char *lmnames[LMT_COUNT] = { "SLEEPY BOOTY CASTLE", "MATTERCORN", "DUMPO", "BIG WHEELIE", "MOOKEY MOUSE",
	                                          "SPACE MOUNTIN", "MAD TEA CUPPS", "LAME STREET" };
	for (int i = 0; i < LMT_COUNT; i++) parse(&t_lm[i], s_static, lmnames[i]);

#ifdef HOST_PREVIEW
	static const char *files[IMG_COUNT] = { "pollak.rgba", "tsu.rgba", "statue.rgba" };
	for (int i = 0; i < IMG_COUNT; i++) {
		char path[512];
		snprintf(path, sizeof path, "%s/%s", shim_gfxdir, files[i]);
		s_img[i] = shim_load_image(path);
	}
#else
	s_sheet = C2D_SpriteSheetLoadFromMem(landmarks_t3x, landmarks_t3x_size);
	for (int i = 0; i < IMG_COUNT; i++) s_img[i] = C2D_SpriteSheetGetImage(s_sheet, i);
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

	snprintf(buf, sizeof buf, "%d", game_score(g));  parse(&t_score, s_dyn, buf);
	snprintf(buf, sizeof buf, "Best %d", g->best);   parse(&t_best, s_dyn, buf);
	snprintf(buf, sizeof buf, "%d", g->coins);       parse(&t_coins, s_dyn, buf);
	snprintf(buf, sizeof buf, "%d", game_score(g));  parse(&t_scoreBig, s_dyn, buf);
	snprintf(buf, sizeof buf, "%d", g->best);        parse(&t_bestBig, s_dyn, buf);
	snprintf(buf, sizeof buf, "%d", g->coins);       parse(&t_coinsBig, s_dyn, buf);

	const char *what = g->killer == OBJ_ROCK ? "You ran into a stone block!"
	                 : g->killer == OBJ_HURDLE ? "You tripped on a hurdle!"
	                 : g->killer == OBJ_SCOOTER ? "A scooter rider ran you over!"
	                 : g->killer == OBJ_PETITION ? "You got stuck signing a petition!"
	                 : "You hit your head on a beam!";
	parse(&t_cause, s_dyn, what);
}

// ---- scenery -----------------------------------------------------------
static void draw_sky(void)
{
	float k = g_zk;
	u32 top = theme3(RGB(64, 120, 196), RGB(124, 76, 206), RGB(52, 130, 232), k, g_sk);
	C2D_DrawRectangle(0, 0, 0, SCR_W, HORIZON + 3, top, top, g_fog, g_fog);

	float sh = g_eye * g_sep * 0.9f;                 // sun and hills are very far away
	float pan = -g_head * 260.0f;                    // turning the road turns the whole sky

	float sunx = fmodf(300.0f + pan, 900.0f);
	if (sunx < 0) sunx += 900.0f;
	if (sunx < 460.0f) {
		fill_circle(sunx + sh, 50, 0, 34, withalpha(themed(RGB(255, 236, 170), RGB(255, 150, 220), k), 70));
		fill_circle(sunx + sh, 50, 0, 22, themed(RGB(255, 244, 200), RGB(255, 214, 240), k));
	}

	u32 hill1 = mixc(themed(RGB(70, 110, 90), RGB(232, 146, 216), k), g_fog, 0.55f);
	u32 hill2 = mixc(themed(RGB(52, 92, 70), RGB(206, 120, 224), k), g_fog, 0.35f);
	float q = fmodf(-g_camx * 3.0f + sh + pan, 560.0f);      // the hills repeat every 560 px
	if (q < 0) q += 560.0f;
	for (int c = 0; c < 2; c++) {
		float p = c ? q - 560.0f : q;
		if (p + 500.0f < 0 || p - 40.0f > SCR_W) continue;
		fill_ellipse(-40 + p, HORIZON - 30, 0, 250, 90, hill1);
		fill_ellipse(190 + p, HORIZON - 42, 0, 300, 110, hill1);
		fill_ellipse(60 + p,  HORIZON - 18, 0, 200, 60, hill2);
		fill_ellipse(280 + p, HORIZON - 22, 0, 220, 70, hill2);
	}

	if (k > 0.25f) {                                 // fireworks over the theme park
		const u32 cols[3] = { RGB(255, 120, 190), RGB(120, 220, 255), RGB(255, 230, 110) };
		for (int i = 0; i < 3; i++) {
			float ph = fmodf(g_time * 0.55f + i * 0.34f, 1.0f);
			float cx = 70 + i * 130 + sh * 0.6f + sinf(i * 5.0f + g_time * 0.1f) * 10, cy = 22 + (i & 1) * 18;
			float r = 4 + ph * 26;
			u32 c = withalpha(cols[i], (int)((1 - ph) * (1 - ph) * 230 * k));
			for (int j = 0; j < 10; j++) {
				float a = j * 0.6283f + i;
				C2D_DrawLine(cx + cosf(a) * r * 0.55f, cy + sinf(a) * r * 0.55f, c, cx + cosf(a) * r, cy + sinf(a) * r, c, 2, 0);
			}
			if (ph < 0.2f) fill_circle(cx, cy, 0, 5 * (1 - ph * 5), withalpha(RGB(255, 255, 255), (int)(200 * k)));
		}
	}
}

// One strip of one road: its kerbs, surface and lane lines. Edges are world x; kl/kr are kerb widths.
static void road_strip(float z, float yy, float hh, float xl, float xr, float kl, float kr,
                       const float *dx, int nd, u32 kerb, u32 road, u32 dashc)
{
	float s = S(z);
	float a = PX(xl, z), b = PX(xr, z);
	if (kl > 0) C2D_DrawRectSolid(a - kl * s, yy, 0, kl * s, hh, kerb);
	if (kr > 0) C2D_DrawRectSolid(b, yy, 0, kr * s, hh, kerb);
	C2D_DrawRectSolid(a, yy, 0, b - a, hh, road);
	if (nd) {
		float lw = 0.07f * s;
		for (int i = 0; i < nd; i++) C2D_DrawRectSolid(PX(dx[i], z) - lw * 0.5f, yy, 0, lw, hh, dashc);
	}
}

static void draw_road(const Game *g)
{
	const float STEP = 3.0f;

	for (float y = HORIZON; y < SCR_H; y += STEP) {
		float yc = y + STEP * 0.5f - g_shy;
		if (yc - HORIZON < 0.5f) continue;
		float z  = FOCAL * CAMH / (yc - HORIZON);
		float zw = z + g->dist;
		int band = z < 50.0f ? ((int)floorf(zw / 2.5f) & 1) : 0;
		int dash = z < 40.0f ? ((int)floorf(zw / 1.6f) & 1) : 0;
		float k  = zone_k(g, zw), ks = stadium_k(g, zw);
		float yy = y + g_shy, hh = STEP + 0.6f;

		u32 grass = fogc(theme3(band ? RGB(72, 140, 52) : RGB(62, 124, 46), band ? RGB(104, 224, 190) : RGB(86, 204, 176), band ? RGB(70, 178, 78) : RGB(56, 158, 66), k, ks), z);
		u32 kerb  = fogc(theme3(band ? RGB(200, 60, 40) : RGB(240, 232, 214), band ? RGB(70, 120, 236) : RGB(255, 255, 255), band ? RGB(255, 255, 255) : RGB(226, 226, 226), k, ks), z);
		u32 road  = fogc(theme3(band ? RGB(206, 170, 112) : RGB(192, 154, 98), band ? RGB(250, 180, 214) : RGB(238, 156, 200), band ? RGB(198, 144, 98) : RGB(184, 132, 88), k, ks), z);
		u32 dashc = fogc(theme3(RGB(240, 226, 190), RGB(255, 246, 130), RGB(198, 144, 98), k, ks), z);
		C2D_DrawRectSolid(0, yy, 0, SCR_W, hh, grass);

		// one road; while it is wide there is an extra lane on each side, widening in and out smoothly
		float wide = track_wide(g, zw);
		const float KW = 0.32f, HALF = LANE_W * 0.5f;
		float narrow = track_narrow(g, zw);                              // inside the stadium: one lane
		float edge = ROAD_HW + LANE_W * wide - (ROAD_HW - 1.0f) * narrow;
		float dx[4];
		int nd = 0;
		if (dash && narrow < 0.02f) {
			dx[nd++] = -HALF; dx[nd++] = HALF;
			if (wide > 0.5f) { dx[nd++] = -1.5f * LANE_W; dx[nd++] = 1.5f * LANE_W; }     // the lines next to the extra lanes
		}
		road_strip(z, yy, hh, -edge, edge, KW, KW, dx, nd, kerb, road, dashc);
	}
}

static unsigned hash(unsigned n)
{
	n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16;
	return n;
}

// ---- landmarks ---------------------------------------------------------
// Campus: the three photo sprites, plus procedural buildings. Dizzyland: a fake theme park's greatest hits.
enum { LM_POLLAK, LM_TSU, LM_STATUE, LM_MIHAYLO, LM_GYM, LM_PARKING, LM_PAC, LM_ARBORETUM, LM_STADIUM, LM_CSUF, LM_CAMPUS_COUNT,
       LM_D_CASTLE = LM_CAMPUS_COUNT, LM_D_MATTER, LM_D_DUMPO, LM_D_WHEEL, LM_D_MOOKEY, LM_D_SPACE, LM_D_TEACUP, LM_D_STREET,
       LM_COUNT };

// Roughly every 8 seconds a landmark stands beside the road: one candidate per stretch of 12 decor slots.
static bool landmark_for_slot(int n, int dizzy, int *kind, int *side)
{
	unsigned h = hash((unsigned)(n / 12) * 977u + 13u);
	if (h % 3 == 0) return false;                        // some stretches have none
	if ((int)((h >> 4) % 12) != n % 12) return false;
	int r = (int)((h >> 12) % 16);
	if (dizzy) {
		*kind = LM_CAMPUS_COUNT + r % (LM_COUNT - LM_CAMPUS_COUNT);
		*side = ((h >> 16) & 1) ? 1 : -1;
		return true;
	}
	*kind = r < 6 ? r % 3 : LM_MIHAYLO + (r - 6) % (LM_CAMPUS_COUNT - LM_MIHAYLO);   // the photo landmarks stay common
	*side = (*kind == LM_TSU || *kind == LM_STATUE) ? 1 : (((h >> 16) & 1) ? 1 : -1); // TSU and the statue face the road from the right
	return true;
}

// draws the visible part of a landmark sprite (without its atlas margin) into a screen rectangle
static void draw_img(int which, float xl, float yt, float xr, float yb, float z)
{
	C2D_Image im = s_img[which];
	float cw = (float)(IMG_W(im) - 2 * IMG_MARGIN), ch = (float)(IMG_H(im) - 2 * IMG_MARGIN);
	if (cw <= 0 || ch <= 0 || xr - xl < 1.0f) return;
	float sx = (xr - xl) / cw, sy = (yb - yt) / ch;
	float t = (z - 12.0f) / 58.0f;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, g_fog, t * 0.85f);
	C2D_DrawImageAt(im, xl - IMG_MARGIN * sx, yt - IMG_MARGIN * sy, 0, &tint, sx, sy);
}

// Helpers for building landmarks. `d` is the distance from the road's centre line (mirrored by `side`), so a landmark
// is written once and works on either side; g_lmoff pushes it outwards when the road gets extra lanes.
static float lx(int side, float d) { return side * (d + g_lmoff); }

static void sbox(int side, float da, float db, float y0, float y1, float z0, float z1, u32 front, u32 top, u32 sd, u32 edge)
{
	float xa = lx(side, da), xb = lx(side, db);
	box(fminf(xa, xb), fmaxf(xa, xb), y0, y1, z0, z1, front, top, sd, edge);
}

static void sell(int side, float d, float y, float z, float w, float h, u32 c) { ell3d(lx(side, d), y, z, w, h, c); }

static void rect3d(int side, float da, float db, float y0, float y1, float z, u32 c)   // flat rectangle facing the camera
{
	float xa = PX(lx(side, da), z), xb = PX(lx(side, db), z);
	if (xa > xb) { float t = xa; xa = xb; xb = t; }
	float ya = PY(y1, z), yb = PY(y0, z);
	C2D_DrawRectSolid(xa, ya, 0, xb - xa, yb - ya, fogc(c, z));
}

static void tri3d(int side, float d0, float y0, float d1, float y1, float d2, float y2, float z, u32 c)
{
	c = fogc(c, z);
	C2D_DrawTriangle(PX(lx(side, d0), z), PY(y0, z), c, PX(lx(side, d1), z), PY(y1, z), c, PX(lx(side, d2), z), PY(y2, z), c, 0);
}

static void line3d(int side, float d0, float y0, float d1, float y1, float z, float th, u32 c)
{
	c = fogc(c, z);
	C2D_DrawLine(PX(lx(side, d0), z), PY(y0, z), c, PX(lx(side, d1), z), PY(y1, z), c, fmaxf(1.0f, th * S(z)), 0);
}

static void windows(int side, float da, float db, float y0, float y1, float z, int cols, int rows, u32 c)
{
	float cw = (db - da) / cols, rh = (y1 - y0) / rows;
	for (int r = 0; r < rows; r++)
		for (int i = 0; i < cols; i++)
			rect3d(side, da + i * cw + cw * 0.12f, da + (i + 1) * cw - cw * 0.12f, y0 + r * rh + rh * 0.14f, y0 + (r + 1) * rh - rh * 0.14f, z, c);
}

static void text_at(const C2D_Text *t, float wx, float wy, float ww, float z, u32 col)   // centred, `ww` world units wide
{
	if (z < 2.0f || z > 60.0f) return;
	float w, h;
	C2D_TextGetDimensions(t, 1.0f, 1.0f, &w, &h);
	float sc = ww * S(z) / w;
	C2D_DrawText(t, C2D_WithColor | C2D_AlignCenter, PX(wx, z), PY(wy, z) - h * sc * 0.5f, 0, sc, sc, fogc(col, z));
}

static void text3d(const C2D_Text *t, int side, float d, float wy, float ww, float z, u32 col)
{
	text_at(t, lx(side, d), wy, ww, z, col);
}

static void sign3d(const C2D_Text *t, int side, float d, float y, float ww, float z, u32 board, u32 ink)   // a text board on the ground floor
{
	rect3d(side, d - ww * 0.55f, d + ww * 0.55f, y - 0.4f, y + 0.4f, z, board);
	text3d(t, side, d, y, ww, z, ink);
}

// --- campus ---
static void lm_palm(int side, float d, float z)
{
	sbox(side, d - 0.16f, d + 0.16f, 0, 4.8f, z, z + 0.32f, RGB(150, 116, 78), RGB(170, 136, 96), RGB(112, 84, 54), RGB(50, 36, 24));
	sell(side, d, 5.2f, z + 0.1f, 3.6f, 0.75f, RGB(40, 122, 52));
	sell(side, d - 0.8f, 4.9f, z + 0.1f, 1.7f, 0.9f, RGB(52, 142, 62));
	sell(side, d + 0.8f, 4.9f, z + 0.1f, 1.7f, 0.9f, RGB(52, 142, 62));
}

static void lm_orange_tree(int side, float d, float z)
{
	sbox(side, d - 0.2f, d + 0.2f, 0, 1.8f, z, z + 0.4f, RGB(120, 84, 52), RGB(140, 100, 64), RGB(90, 62, 38), RGB(46, 30, 18));
	sell(side, d, 2.9f, z + 0.2f, 3.0f, 2.3f, RGB(36, 112, 46));
	sell(side, d - 0.5f, 3.2f, z + 0.2f, 0.32f, 0.32f, RGB(255, 150, 30));
	sell(side, d + 0.6f, 2.7f, z + 0.2f, 0.32f, 0.32f, RGB(255, 150, 30));
	sell(side, d + 0.1f, 3.6f, z + 0.2f, 0.32f, 0.32f, RGB(255, 150, 30));
	sell(side, d - 0.7f, 2.5f, z + 0.2f, 0.32f, 0.32f, RGB(255, 150, 30));
}

static void draw_campus_landmark(int kind, int side, float z)
{
	switch (kind) {
	case LM_POLLAK: {                                    // Pollak Library: a big white block with the photo on its front
		const float W = 8.0f, H = 8.0f;
		sbox(side, 7.4f, 7.4f + W, 0, H, z, z + 6.0f, RGB(214, 210, 200), RGB(232, 228, 218), RGB(190, 186, 176), RGB(60, 56, 48));
		float xa = lx(side, 7.4f), xb = lx(side, 7.4f + W);
		draw_img(IMG_POLLAK, PX(fminf(xa, xb), z), PY(H, z), PX(fmaxf(xa, xb), z), PY(0, z), z);
		break;
	}
	case LM_TSU:                                         // Titan Student Union with the TITANS letters
		draw_img(IMG_TSU, PX(lx(side, 5.6f), z), PY(4.6f, z), PX(lx(side, 18.6f), z), PY(0, z), z);
		break;
	case LM_STATUE: {                                    // Tuffy statue on a concrete plinth
		sbox(side, 5.9f, 10.1f, 0, 0.5f, z, z + 3.2f, RGB(168, 166, 160), RGB(196, 194, 186), RGB(138, 136, 130), RGB(50, 48, 44));
		float zs = z + 1.6f;
		draw_img(IMG_STATUE, PX(lx(side, 6.3f), zs), PY(0.5f + 4.6f, zs), PX(lx(side, 9.75f), zs), PY(0.5f, zs), zs);
		break;
	}
	case LM_MIHAYLO: {                                   // Mihaylo Hall: glass-and-stone business school with a clock tower
		sbox(side, 7.6f, 14.6f, 0, 6.4f, z, z + 6.0f, RGB(208, 190, 160), RGB(228, 212, 186), RGB(178, 162, 134), RGB(56, 48, 40));
		windows(side, 8.0f, 14.2f, 0.7f, 6.0f, z, 5, 3, RGB(66, 118, 162));
		rect3d(side, 7.6f, 14.6f, 6.4f, 6.65f, z, RGB(150, 70, 44));
		float zt = z + 0.5f;
		sbox(side, 7.6f, 9.6f, 6.4f, 10.6f, zt, zt + 1.8f, RGB(196, 176, 146), RGB(220, 204, 176), RGB(168, 150, 122), RGB(56, 48, 40));
		if (zt > 2.0f) {                                 // clock
			float s = S(zt), cx = PX(lx(side, 8.6f), zt), cy = PY(9.3f, zt);
			u32 dk = fogc(RGB(40, 34, 30), zt), wh = fogc(RGB(248, 246, 236), zt);
			fill_circle(cx, cy, 0, 0.66f * s, dk);
			fill_circle(cx, cy, 0, 0.54f * s, wh);
			float a = g_time * 0.4f;
			C2D_DrawLine(cx, cy, dk, cx + sinf(a) * 0.42f * s, cy - cosf(a) * 0.42f * s, dk, fmaxf(1, 0.07f * s), 0);
			C2D_DrawLine(cx, cy, dk, cx + sinf(a * 12) * 0.28f * s, cy - cosf(a * 12) * 0.28f * s, dk, fmaxf(1, 0.07f * s), 0);
		}
		tri3d(side, 7.5f, 10.6f, 9.7f, 10.6f, 8.6f, 12.0f, zt, RGB(150, 70, 44));
		break;
	}
	case LM_GYM:                                         // Titan Gym: barrel roof, navy and orange trim
		sell(side, 11.5f, 4.0f, z + 0.1f, 8.2f, 3.4f, RGB(150, 170, 192));
		sbox(side, 7.4f, 15.6f, 0, 4.0f, z, z + 6.0f, RGB(224, 216, 200), RGB(236, 230, 216), RGB(190, 182, 166), RGB(56, 50, 42));
		rect3d(side, 7.4f, 15.6f, 2.5f, 3.1f, z, RGB(20, 62, 122));
		rect3d(side, 7.4f, 15.6f, 3.1f, 3.35f, z, RGB(245, 126, 28));
		rect3d(side, 10.4f, 12.6f, 0, 2.1f, z, RGB(52, 60, 80));
		rect3d(side, 11.42f, 11.58f, 0, 2.1f, z, RGB(150, 160, 176));
		break;
	case LM_PARKING: {                                   // Nutwood Parking Structure: bare decks, pillars, a few parked cars
		sbox(side, 7.6f, 14.6f, 0, 7.6f, z, z + 6.0f, RGB(70, 70, 78), RGB(96, 96, 104), RGB(56, 56, 64), RGB(30, 30, 34));
		const u32 car[4] = { RGB(200, 50, 44), RGB(60, 110, 200), RGB(230, 230, 232), RGB(240, 190, 40) };
		for (int j = 0; j < 4; j++) {
			float y = j * 1.9f;
			rect3d(side, 7.6f, 14.6f, y, y + 0.3f, z, RGB(196, 196, 190));
			rect3d(side, 8.4f + (j & 1) * 1.1f, 9.3f + (j & 1) * 1.1f, y + 0.3f, y + 0.85f, z, car[j]);
			rect3d(side, 11.5f - (j & 1) * 1.3f, 12.4f - (j & 1) * 1.3f, y + 0.3f, y + 0.85f, z, car[(j + 2) & 3]);
		}
		rect3d(side, 7.6f, 14.6f, 7.3f, 7.6f, z, RGB(196, 196, 190));
		for (int c = 0; c < 5; c++) rect3d(side, 7.6f + c * 1.65f, 7.85f + c * 1.65f, 0, 7.3f, z, RGB(176, 176, 170));
		break;
	}
	case LM_PAC:                                         // Performing Arts Center: glass lobby under a slanted canopy
		sbox(side, 8.0f, 14.6f, 0, 3.8f, z, z + 6.0f, RGB(232, 222, 206), RGB(242, 234, 220), RGB(198, 188, 172), RGB(56, 50, 42));
		rect3d(side, 8.5f, 14.1f, 0.3f, 3.2f, z, RGB(60, 112, 152));
		for (int i = 1; i < 5; i++) rect3d(side, 8.5f + i * 1.12f - 0.04f, 8.5f + i * 1.12f + 0.04f, 0.3f, 3.2f, z, RGB(36, 50, 66));
		tri3d(side, 7.4f, 3.8f, 15.2f, 3.8f, 15.2f, 5.4f, z, RGB(214, 90, 60));
		tri3d(side, 7.4f, 3.8f, 15.2f, 5.4f, 7.4f, 7.4f, z, RGB(226, 112, 74));
		sell(side, 6.6f, 1.8f, z + 0.4f, 1.5f, 1.5f, RGB(245, 126, 28));
		break;
	case LM_ARBORETUM: {                                 // the Arboretum: palms and orange trees
		lm_palm(side, 14.4f, z + 4.0f);
		lm_orange_tree(side, 11.2f, z + 2.4f);
		lm_palm(side, 8.0f, z + 1.0f);
		sbox(side, 6.5f, 6.7f, 0, 1.0f, z, z + 0.2f, RGB(120, 84, 52), RGB(140, 100, 64), RGB(90, 62, 38), RGB(46, 30, 18));
		rect3d(side, 5.9f, 7.3f, 0.7f, 1.4f, z, RGB(58, 120, 62));
		break;
	}
	case LM_STADIUM:                                     // Titan Stadium: floodlight towers over the bleachers
		for (int i = 0; i < 2; i++) {
			float zt = z + (1 - i) * 3.6f, d = i ? 8.0f : 14.4f;   // far tower first
			if (i == 0) {
				sbox(side, 8.6f, 15.6f, 0, 1.6f, z + 1.0f, z + 6.5f, RGB(150, 152, 160), RGB(180, 182, 190), RGB(120, 122, 130), RGB(40, 40, 46));
				rect3d(side, 8.6f, 15.6f, 0.5f, 0.85f, z + 1.0f, RGB(20, 62, 122));
				rect3d(side, 8.6f, 15.6f, 0.85f, 1.05f, z + 1.0f, RGB(245, 126, 28));
			}
			sbox(side, d - 0.16f, d + 0.16f, 0, 9.4f, zt, zt + 0.32f, RGB(140, 140, 148), RGB(170, 170, 178), RGB(110, 110, 118), RGB(40, 40, 46));
			sbox(side, d - 1.1f, d + 1.1f, 9.4f, 10.8f, zt - 0.05f, zt + 0.3f, RGB(255, 250, 214), RGB(255, 255, 230), RGB(210, 206, 176), RGB(60, 56, 44));
			for (int c = 0; c < 4; c++) rect3d(side, d - 0.95f + c * 0.5f, d - 0.85f + c * 0.5f, 9.5f, 10.7f, zt - 0.05f, RGB(150, 146, 120));
		}
		break;
	case LM_CSUF: {                                      // the entrance monument
		sbox(side, 6.6f, 13.6f, 0, 2.6f, z, z + 0.9f, RGB(172, 92, 62), RGB(196, 116, 84), RGB(136, 70, 46), RGB(50, 26, 18));
		rect3d(side, 7.1f, 13.1f, 0.55f, 2.15f, z, RGB(20, 62, 122));
		text3d(&t_csuf, side, 10.1f, 1.35f, 3.5f, z, RGB(245, 126, 28));
		sell(side, 6.3f, 0.6f, z, 1.8f, 1.3f, RGB(52, 128, 50));
		sell(side, 13.9f, 0.6f, z, 1.8f, 1.3f, RGB(52, 128, 50));
		break;
	}
	}
}

// --- Dizzyland (a fake theme park; the names are jokes and all the art is our own) ---
static void lm_elephant(int side, float d, float y, float z, float face)     // "Dumpo", a flying elephant with big pink ears
{
	sell(side, d - face * 0.5f, y + 0.25f, z, 1.2f, 1.4f, RGB(255, 160, 200));
	sell(side, d, y, z, 2.2f, 1.5f, RGB(170, 190, 232));
	sell(side, d + face * 0.95f, y + 0.4f, z, 1.15f, 1.05f, RGB(170, 190, 232));
	line3d(side, d + face * 1.4f, y + 0.25f, d + face * 1.85f, y - 0.35f, z, 0.22f, RGB(150, 172, 220));
	sell(side, d + face * 1.05f, y + 0.6f, z, 0.2f, 0.26f, RGB(20, 20, 30));
	sell(side, d + face * 0.9f, y + 1.05f, z, 0.9f, 0.4f, RGB(70, 110, 220));      // little hat
}

static void draw_dizzy_landmark(int kind, int side, float z)
{
	switch (kind) {
	case LM_D_CASTLE: {                                  // Sleepy Booty Castle
		const u32 roof[3] = { RGB(255, 110, 180), RGB(90, 140, 240), RGB(255, 110, 180) };
		sbox(side, 7.0f, 15.0f, 0, 3.2f, z, z + 5.0f, RGB(250, 232, 246), RGB(255, 246, 252), RGB(214, 190, 214), RGB(90, 50, 90));
		static const float td[3] = { 7.3f, 10.4f, 13.2f }, tw[3] = { 1.6f, 2.2f, 1.6f }, th[3] = { 6.2f, 8.4f, 5.6f };
		for (int i = 2; i >= 0; i--) {
			float zt = z + 0.4f + (i == 1 ? 0.6f : 0.0f);
			sbox(side, td[i], td[i] + tw[i], 0, th[i], zt, zt + tw[i] * 0.8f, RGB(255, 240, 250), RGB(255, 250, 255), RGB(220, 196, 222), RGB(90, 50, 90));
			rect3d(side, td[i] + tw[i] * 0.4f, td[i] + tw[i] * 0.6f, th[i] * 0.55f, th[i] * 0.55f + 0.9f, zt, RGB(70, 60, 150));
			tri3d(side, td[i] - 0.2f, th[i], td[i] + tw[i] + 0.2f, th[i], td[i] + tw[i] * 0.5f, th[i] + 2.4f, zt, roof[i]);
			line3d(side, td[i] + tw[i] * 0.5f, th[i] + 2.4f, td[i] + tw[i] * 0.5f, th[i] + 3.1f, zt, 0.06f, RGB(60, 40, 40));
			tri3d(side, td[i] + tw[i] * 0.5f, th[i] + 3.1f, td[i] + tw[i] * 0.5f, th[i] + 2.6f, td[i] + tw[i] * 0.5f + 0.6f, th[i] + 2.85f, zt, RGB(255, 220, 60));
		}
		rect3d(side, 11.2f, 12.4f, 0, 1.6f, z, RGB(96, 70, 160));
		sign3d(&t_lm[LMT_CASTLE], side, 9.0f, 1.2f, 4.2f, z - 0.05f, RGB(70, 60, 150), RGB(255, 240, 150));
		break;
	}
	case LM_D_MATTER: {                                  // Mattercorn: a snowy mountain with a bobsled track
		float zm = z + 3.0f;
		tri3d(side, 5.5f, 0, 12.0f, 0, 12.0f, 11.0f, zm, RGB(120, 146, 206));
		tri3d(side, 12.0f, 0, 19.0f, 0, 12.0f, 11.0f, zm, RGB(84, 106, 170));
		tri3d(side, 9.9f, 7.6f, 14.1f, 7.6f, 12.0f, 11.0f, zm, RGB(250, 250, 255));
		tri3d(side, 9.9f, 7.6f, 11.2f, 7.6f, 10.6f, 6.6f, zm, RGB(250, 250, 255));
		tri3d(side, 11.2f, 7.6f, 12.8f, 7.6f, 12.0f, 6.4f, zm, RGB(250, 250, 255));
		tri3d(side, 12.8f, 7.6f, 14.1f, 7.6f, 13.4f, 6.7f, zm, RGB(250, 250, 255));
		line3d(side, 11.6f, 6.0f, 9.0f, 3.6f, zm, 0.14f, RGB(230, 60, 60));
		line3d(side, 9.0f, 3.6f, 10.6f, 1.6f, zm, 0.14f, RGB(230, 60, 60));
		sign3d(&t_lm[LMT_MATTER], side, 9.0f, 0.5f, 3.6f, z, RGB(60, 70, 130), RGB(255, 255, 255));
		break;
	}
	case LM_D_DUMPO: {                                   // Dumpo the flying elephant ride
		float zr = z + 1.5f, bob = sinf(g_time * 2.0f) * 0.35f;
		sbox(side, 11.7f, 12.3f, 0, 6.4f, zr, zr + 0.6f, RGB(255, 240, 240), RGB(255, 255, 255), RGB(220, 180, 200), RGB(120, 60, 100));
		for (int i = 0; i < 4; i++) rect3d(side, 11.7f, 12.3f, i * 1.6f, i * 1.6f + 0.8f, zr, RGB(255, 110, 170));
		tri3d(side, 11.4f, 6.4f, 12.6f, 6.4f, 12.0f, 7.4f, zr, RGB(255, 220, 60));
		line3d(side, 12.0f, 6.2f, 8.4f, 5.6f + bob, zr, 0.12f, RGB(120, 60, 100));
		line3d(side, 12.0f, 6.2f, 15.6f, 5.6f - bob, zr, 0.12f, RGB(120, 60, 100));
		lm_elephant(side, 15.6f, 4.4f - bob, zr, -1.0f);
		lm_elephant(side, 8.4f, 4.4f + bob, zr, 1.0f);
		sign3d(&t_lm[LMT_DUMPO], side, 12.0f, 0.5f, 2.6f, z, RGB(255, 110, 170), RGB(255, 255, 255));
		break;
	}
	case LM_D_WHEEL: {                                   // Big Wheelie: a ferris wheel
		float zw = z + 2.0f, cx = 12.0f, cy = 6.2f, R = 4.6f, rot = g_time * 0.35f;
		line3d(side, cx, cy, cx - 2.8f, 0, zw, 0.24f, RGB(150, 150, 170));
		line3d(side, cx, cy, cx + 2.8f, 0, zw, 0.24f, RGB(150, 150, 170));
		const u32 pod[4] = { RGB(255, 110, 170), RGB(90, 170, 250), RGB(255, 220, 70), RGB(120, 220, 140) };
		for (int i = 0; i < 8; i++) {
			float a0 = rot + i * 0.7854f, a1 = a0 + 0.7854f;
			line3d(side, cx + cosf(a0) * R, cy + sinf(a0) * R, cx + cosf(a1) * R, cy + sinf(a1) * R, zw, 0.16f, RGB(235, 235, 245));
			line3d(side, cx, cy, cx + cosf(a0) * R, cy + sinf(a0) * R, zw, 0.08f, RGB(200, 200, 216));
			sell(side, cx + cosf(a0) * R, cy + sinf(a0) * R - 0.5f, zw, 0.9f, 0.9f, pod[i & 3]);
		}
		sell(side, cx, cy, zw, 0.8f, 0.8f, RGB(255, 220, 70));
		sign3d(&t_lm[LMT_WHEEL], side, 12.0f, 0.5f, 3.2f, z, RGB(60, 110, 200), RGB(255, 255, 255));
		break;
	}
	case LM_D_MOOKEY: {                                  // a giant statue of Mookey Mouse (three circles and an attitude)
		const u32 K = RGB(96, 64, 158);                                                 // purple, not the famous black
		float zs = z + 1.4f, wave = sinf(g_time * 4.0f) * 0.35f;
		sbox(side, 6.0f, 10.0f, 0, 0.7f, z, z + 3.0f, RGB(150, 160, 200), RGB(180, 190, 226), RGB(116, 126, 168), RGB(40, 44, 70));
		sell(side, 7.1f, 0.95f, zs, 1.5f, 0.55f, RGB(255, 140, 40));                  // shoes
		sell(side, 8.9f, 0.95f, zs, 1.5f, 0.55f, RGB(255, 140, 40));
		rect3d(side, 7.0f, 7.5f, 1.1f, 2.4f, zs, K);                                  // legs
		rect3d(side, 8.5f, 9.0f, 1.1f, 2.4f, zs, K);
		sell(side, 8.0f, 2.7f, zs, 2.4f, 1.3f, RGB(30, 190, 160));                    // shorts
		sell(side, 8.0f, 4.0f, zs, 2.0f, 2.3f, K);                                    // body
		line3d(side, 6.9f, 4.5f, 5.7f, 5.6f + wave, zs, 0.3f, K);                     // waving arm
		sell(side, 5.7f, 5.7f + wave, zs, 0.7f, 0.7f, RGB(255, 226, 80));
		line3d(side, 9.1f, 4.5f, 10.1f, 3.3f, zs, 0.3f, K);
		sell(side, 10.1f, 3.2f, zs, 0.7f, 0.7f, RGB(255, 226, 80));
		sell(side, 6.7f, 7.3f, zs, 1.5f, 1.5f, K);                                    // ears
		sell(side, 9.3f, 7.3f, zs, 1.5f, 1.5f, K);
		sell(side, 8.0f, 6.0f, zs, 2.3f, 2.1f, K);                                    // head
		sell(side, 8.0f, 5.75f, zs, 1.6f, 1.4f, RGB(226, 210, 250));                  // face
		sell(side, 7.65f, 6.1f, zs, 0.4f, 0.6f, RGB(255, 255, 255));
		sell(side, 8.35f, 6.1f, zs, 0.4f, 0.6f, RGB(255, 255, 255));
		sell(side, 7.65f, 6.05f, zs, 0.16f, 0.3f, K);
		sell(side, 8.35f, 6.05f, zs, 0.16f, 0.3f, K);
		sell(side, 8.0f, 5.75f, zs, 0.36f, 0.26f, K);                                 // nose
		sell(side, 8.0f, 5.35f, zs, 0.9f, 0.3f, RGB(190, 50, 60));                    // grin
		sign3d(&t_lm[LMT_MOOKEY], side, 8.0f, 0.35f, 3.2f, z - 0.05f, RGB(70, 70, 120), RGB(255, 255, 255));
		break;
	}
	case LM_D_SPACE: {                                   // Space Mountin: a white cone with glowing rings
		float zc = z + 2.0f;
		sbox(side, 7.0f, 17.0f, 0, 2.0f, z, z + 5.0f, RGB(236, 238, 250), RGB(250, 250, 255), RGB(190, 192, 214), RGB(60, 60, 100));
		tri3d(side, 7.6f, 2.0f, 16.4f, 2.0f, 12.0f, 9.0f, zc, RGB(226, 228, 246));
		tri3d(side, 12.0f, 2.0f, 16.4f, 2.0f, 12.0f, 9.0f, zc, RGB(190, 194, 224));
		for (int i = 1; i <= 3; i++) {
			float y = 2.0f + i * 1.5f, hw = 4.4f * (1.0f - (y - 2.0f) / 7.0f);
			bool on = ((int)(g_time * 3.0f) + i) & 1;
			rect3d(side, 12.0f - hw, 12.0f + hw, y, y + 0.22f, zc, on ? RGB(90, 200, 255) : RGB(160, 120, 255));
		}
		line3d(side, 12.0f, 9.0f, 12.0f, 10.4f, zc, 0.1f, RGB(90, 90, 120));
		sell(side, 12.0f, 10.5f, zc, 0.6f, 0.6f, ((int)(g_time * 4.0f) & 1) ? RGB(255, 80, 80) : RGB(255, 200, 200));
		sign3d(&t_lm[LMT_SPACE], side, 12.0f, 1.0f, 4.4f, z, RGB(40, 40, 110), RGB(150, 230, 255));
		break;
	}
	case LM_D_TEACUP: {                                  // Mad Tea Cupps: three huge teacups
		const u32 cup[3] = { RGB(255, 140, 190), RGB(120, 220, 190), RGB(190, 150, 250) };
		for (int i = 2; i >= 0; i--) {
			float d = 8.0f + i * 3.6f, zc = z + i * 1.3f, bob = sinf(g_time * 2.5f + i * 2.0f) * 0.08f;
			sell(side, d, 0.4f, zc, 3.5f, 0.8f, RGB(250, 246, 250));                    // saucer
			sell(side, d + 1.5f, 1.6f + bob, zc, 1.0f, 1.0f, cup[i]);                    // handle
			sell(side, d, 1.6f + bob, zc, 2.7f, 2.0f, cup[i]);                           // cup
			sell(side, d, 2.45f + bob, zc, 2.7f, 0.55f, RGB(255, 246, 250));             // rim
			sell(side, d, 2.45f + bob, zc, 2.2f, 0.32f, RGB(150, 90, 60));               // tea
		}
		sign3d(&t_lm[LMT_TEACUP], side, 11.6f, 0.4f, 4.4f, z - 0.05f, RGB(120, 70, 170), RGB(255, 255, 255));
		break;
	}
	case LM_D_STREET: {                                  // Lame Street: three pastel shops with awnings and bunting
		const u32 wall[3] = { RGB(255, 200, 220), RGB(200, 230, 255), RGB(255, 240, 180) };
		const u32 roofc[3] = { RGB(200, 70, 110), RGB(70, 110, 190), RGB(220, 150, 40) };
		static const float sd0[3] = { 7.0f, 9.8f, 12.6f }, sh[3] = { 3.4f, 4.2f, 3.6f };
		for (int i = 2; i >= 0; i--) {
			float d = sd0[i], zt = z + (i == 1 ? 0.0f : 0.5f);
			sbox(side, d, d + 2.8f, 0, sh[i], zt, zt + 4.0f, wall[i], RGB(255, 255, 255), wall[i] - 0x00202020u, RGB(80, 50, 70));
			tri3d(side, d - 0.1f, sh[i], d + 2.9f, sh[i], d + 1.4f, sh[i] + 1.1f, zt, roofc[i]);
			rect3d(side, d + 0.3f, d + 1.3f, 1.2f, 2.6f, zt, RGB(70, 90, 150));
			rect3d(side, d + 1.6f, d + 2.4f, 0, 1.9f, zt, RGB(120, 70, 90));
			for (int a = 0; a < 5; a++)                                                  // striped awning
				rect3d(side, d + a * 0.56f, d + (a + 1) * 0.56f, 2.65f, 3.0f, zt, (a & 1) ? RGB(255, 255, 255) : roofc[i]);
		}
		const u32 flag[4] = { RGB(255, 90, 90), RGB(255, 220, 70), RGB(90, 200, 255), RGB(140, 230, 140) };
		for (int f = 0; f < 6; f++) {
			float d = 7.4f + f * 1.4f;
			tri3d(side, d, 5.2f, d + 0.5f, 5.2f, d + 0.25f, 4.6f, z - 0.05f, flag[f & 3]);
		}
		line3d(side, 7.0f, 5.25f, 15.4f, 5.25f, z - 0.05f, 0.05f, RGB(70, 50, 60));
		sign3d(&t_lm[LMT_STREET], side, 11.2f, 4.5f, 3.4f, z + 0.0f, RGB(90, 60, 130), RGB(255, 240, 150));
		break;
	}
	}
}

static void draw_landmark(int kind, int side, float z)
{
	if (z < 1.8f) return;
	if (kind < LM_CAMPUS_COUNT) draw_campus_landmark(kind, side, z);
	else                        draw_dizzy_landmark(kind, side, z);
}

// ---- roadside decor ------------------------------------------------------
static void draw_duck(float x, float z)             // "Donut Duck": a yellow baker in a chef hat, waving a donut
{
	float wave = sinf(g_time * 5.0f + x) * 0.22f;
	ell3d(x - 0.28f, 0.1f, z, 0.5f, 0.22f, RGB(255, 130, 30));
	ell3d(x + 0.28f, 0.1f, z, 0.5f, 0.22f, RGB(255, 130, 30));
	ell3d(x, 0.95f, z, 1.15f, 1.15f, RGB(255, 214, 60));                             // body
	ell3d(x, 0.85f, z, 0.85f, 0.85f, RGB(255, 140, 190));                            // pink apron
	ell3d(x, 2.0f, z + 0.05f, 0.95f, 0.9f, RGB(255, 214, 60));                       // head
	ell3d(x, 1.82f, z, 0.72f, 0.28f, RGB(255, 130, 30));                             // bill
	ell3d(x - 0.2f, 2.12f, z, 0.2f, 0.26f, RGB(20, 20, 30));
	ell3d(x + 0.2f, 2.12f, z, 0.2f, 0.26f, RGB(20, 20, 30));
	ell3d(x, 2.55f, z, 0.85f, 0.5f, RGB(252, 252, 255));                             // chef hat
	ell3d(x, 2.85f, z, 0.6f, 0.45f, RGB(252, 252, 255));
	float s = S(z), th = fmaxf(1.0f, 0.16f * s);
	u32 w = fogc(RGB(255, 214, 60), z);
	C2D_DrawLine(PX(x + 0.5f, z), PY(1.3f, z), w, PX(x + 0.9f, z), PY(1.85f + wave, z), w, th, 0);
	ell3d(x + 0.95f, 1.95f + wave, z, 0.55f, 0.55f, RGB(240, 150, 190));             // the donut
	ell3d(x + 0.95f, 1.95f + wave, z, 0.18f, 0.18f, RGB(120, 70, 50));
}

static void draw_dizzy_decor(int kind, float x, float z, unsigned h)
{
	const u32 pal[6] = { RGB(255, 90, 130), RGB(255, 220, 70), RGB(90, 190, 255), RGB(150, 230, 120), RGB(200, 130, 255), RGB(255, 160, 60) };
	if (kind == 0) {                                     // balloon bunch
		u32 st = fogc(RGB(250, 250, 250), z);
		C2D_DrawLine(PX(x, z), PY(0, z), st, PX(x, z), PY(2.8f, z), st, fmaxf(1.0f, 0.03f * S(z)), 0);
		ell3d(x - 0.4f, 3.5f, z, 0.9f, 1.1f, pal[h % 6]);
		ell3d(x + 0.4f, 3.7f, z, 0.9f, 1.1f, pal[(h >> 3) % 6]);
		ell3d(x, 4.2f, z, 0.9f, 1.1f, pal[(h >> 6) % 6]);
	} else if (kind == 1) {                              // topiary shaped like a mouse head
		box(x - 0.15f, x + 0.15f, 0, 1.1f, z, z + 0.3f, RGB(120, 84, 52), RGB(140, 100, 64), RGB(90, 62, 38), RGB(46, 30, 18));
		ell3d(x - 0.8f, 2.7f, z + 0.2f, 0.95f, 0.95f, RGB(36, 130, 80));
		ell3d(x + 0.8f, 2.7f, z + 0.2f, 0.95f, 0.95f, RGB(36, 130, 80));
		ell3d(x, 2.0f, z + 0.2f, 1.9f, 1.7f, RGB(46, 148, 92));
	} else if (kind == 2) {                              // candy-striped lamp post
		box(x - 0.1f, x + 0.1f, 0, 3.4f, z, z + 0.2f, RGB(255, 255, 255), RGB(255, 255, 255), RGB(220, 200, 210), RGB(120, 60, 90));
		if (z > 1.8f) {
			float s = S(z);
			for (int i = 0; i < 4; i++)
				C2D_DrawRectSolid(PX(x - 0.1f, z), PY(0.3f + i * 0.85f + 0.42f, z), 0, 0.2f * s, 0.42f * s, fogc(RGB(255, 100, 160), z));
		}
		ell3d(x, 3.7f, z, 0.85f, 0.85f, RGB(255, 236, 150));
		ell3d(x, 3.7f, z, 0.45f, 0.45f, RGB(255, 252, 220));
	} else {
		draw_duck(x, z);
	}
}

// ---- one-off props: the university gate at the start, the turn sign, the gates of Dizzyland ----
static void draw_turn_sign(const Game *g, float z)      // yellow warning board with an arrow the way the road turns
{
	if (z < 1.8f) return;
	float side = (float)g->evSide, x = side * 4.3f;
	box(x - 0.1f, x + 0.1f, 0, 2.4f, z, z + 0.2f, RGB(140, 120, 100), RGB(170, 150, 128), RGB(100, 84, 68), RGB(40, 32, 24));
	box(x - 1.1f, x + 1.1f, 1.6f, 3.2f, z - 0.05f, z + 0.25f, RGB(250, 210, 40), RGB(255, 226, 90), RGB(200, 160, 20), RGB(40, 32, 20));
	float s = S(z), cx = PX(x, z), cy = PY(2.4f, z);
	u32 ink = fogc(RGB(30, 26, 24), z);
	float tip = cx + side * 0.75f * s, neck = cx + side * 0.15f * s;
	C2D_DrawRectSolid(fminf(cx - side * 0.6f * s, neck), cy - 0.11f * s, 0, 0.75f * s, 0.22f * s, ink);
	C2D_DrawTriangle(neck, cy - 0.42f * s, ink, neck, cy + 0.42f * s, ink, tip, cy, ink, 0);
}

// The real "California State University Fullerton" gate: two big dark stone columns holding a navy banner,
// smaller piers with round seals on each side, and the TITANS plaque.
static void draw_start_gate(float z)
{
	if (z < 1.8f) return;
	const u32 stone = RGB(104, 88, 82), stoneT = RGB(132, 114, 106), stoneS = RGB(76, 62, 58), stoneE = RGB(36, 28, 26);
	const u32 navy = RGB(28, 42, 104), navyT = RGB(56, 74, 150), navyS = RGB(18, 28, 72), navyE = RGB(10, 14, 40);
	const u32 flute = RGB(128, 110, 102);
	float s = S(z);
	for (int side = -1; side <= 1; side += 2) {
		float xa = side * 3.5f, xb = side * 4.7f, x0 = fminf(xa, xb), x1 = fmaxf(xa, xb);
		box(x0, x1, 0, 7.4f, z, z + 1.0f, stone, stoneT, stoneS, stoneE);                       // big column
		box(x0 - 0.15f, x1 + 0.15f, 7.4f, 7.7f, z - 0.05f, z + 1.05f, stoneT, stoneT, stone, stoneE);
		for (int i = 0; i < 3; i++)                                                              // fluting
			C2D_DrawRectSolid(PX(x0 + 0.14f + i * 0.34f, z), PY(6.9f, z), 0, 0.14f * s, 6.5f * s, fogc(flute, z));
		float pa = side * 7.4f, pb = side * 8.4f, p0 = fminf(pa, pb), p1 = fmaxf(pa, pb);
		box(p0, p1, 0, 4.4f, z + 0.1f, z + 0.9f, stone, stoneT, stoneS, stoneE);                  // small pier
		box(p0 - 0.12f, p1 + 0.12f, 4.4f, 4.65f, z + 0.05f, z + 0.95f, stoneT, stoneT, stone, stoneE);
		float ba = side * 4.7f, bb = side * 7.4f;
		box(fminf(ba, bb), fmaxf(ba, bb), 3.5f, 4.3f, z + 0.15f, z + 0.85f, navy, navyT, navyS, navyE);   // navy beam between them
		fill_circle(PX(side * 7.9f, z + 0.1f), PY(3.3f, z + 0.1f), 0, 0.36f * S(z + 0.1f), fogc(RGB(200, 200, 208), z));   // the seal
		fill_circle(PX(side * 7.9f, z + 0.1f), PY(3.3f, z + 0.1f), 0, 0.27f * S(z + 0.1f), fogc(navy, z));
	}
	box(-3.5f, 3.5f, 5.0f, 6.9f, z, z + 1.0f, navy, navyT, navyS, navyE);                       // the banner across the top
	C2D_DrawRectSolid(PX(-3.35f, z), PY(6.75f, z), 0, 6.7f * s, 0.06f * s, fogc(RGB(150, 140, 130), z));
	C2D_DrawRectSolid(PX(-3.35f, z), PY(5.15f, z), 0, 6.7f * s, 0.06f * s, fogc(RGB(150, 140, 130), z));
	text_at(&t_cal, 0.0f, 6.4f, 5.6f, z, RGB(244, 246, 255));
	text_at(&t_fuller, 0.0f, 5.75f, 4.0f, z, RGB(255, 255, 255));
	box(8.9f, 12.4f, 0, 1.9f, z + 0.4f, z + 1.2f, navy, navyT, navyS, navyE);                    // TITANS plaque on the right...
	C2D_DrawRectSolid(PX(8.9f, z + 0.4f), PY(1.9f, z + 0.4f), 0, 3.5f * S(z + 0.4f), 0.1f * S(z + 0.4f), fogc(RGB(150, 140, 130), z));
	text_at(&t_titans, 10.65f, 0.95f, 2.4f, z + 0.4f, RGB(255, 255, 255));
	box(-12.4f, -8.9f, 0, 1.3f, z + 0.4f, z + 1.2f, navy, navyT, navyS, navyE);                 // ...and a plain wall on the left
}

// A squirrel scampering across the road (just for show: the campus is full of them)
static void draw_squirrel(float x, float z, int dir, float phase)
{
	if (z < 3.0f) return;
	float run = sinf(g_time * 13.0f + phase);
	float y = fabsf(run) * 0.14f;
	float sc = 1.7f;                                                                                // a bit cartoonishly big, so you can see them
	fill_ellipse(PX(x, z) - 0.4f * sc * S(z), PY(0, z) - 0.05f * sc * S(z), 0, 0.8f * sc * S(z), 0.1f * sc * S(z), RGBA(0, 0, 0, 55));
	ell3d(x - dir * 0.46f * sc, y + 0.62f * sc, z, 0.32f * sc, 0.82f * sc, RGB(122, 118, 112));    // fluffy tail
	ell3d(x - dir * 0.42f * sc, y + 1.0f * sc, z, 0.27f * sc, 0.34f * sc, RGB(176, 172, 164));
	ell3d(x, y + 0.34f * sc, z, 0.78f * sc, 0.46f * sc, RGB(112, 108, 104));                       // body
	ell3d(x + dir * 0.06f * sc, y + 0.28f * sc, z, 0.4f * sc, 0.26f * sc, RGB(226, 222, 212));     // pale belly
	ell3d(x - 0.2f * sc + run * 0.1f, y + 0.1f * sc, z, 0.22f * sc, 0.16f * sc, RGB(70, 66, 62));   // feet
	ell3d(x + 0.22f * sc - run * 0.1f, y + 0.1f * sc, z, 0.22f * sc, 0.16f * sc, RGB(70, 66, 62));
	ell3d(x + dir * 0.44f * sc, y + 0.54f * sc, z, 0.38f * sc, 0.36f * sc, RGB(124, 120, 114));    // head
	ell3d(x + dir * 0.4f * sc, y + 0.76f * sc, z, 0.1f * sc, 0.16f * sc, RGB(88, 84, 80));          // ear
	ell3d(x + dir * 0.55f * sc, y + 0.57f * sc, z, 0.08f * sc, 0.08f * sc, RGB(20, 20, 20));        // eye
	ell3d(x + dir * 0.63f * sc, y + 0.5f * sc, z, 0.07f * sc, 0.06f * sc, RGB(40, 30, 30));         // nose
}

// flat quad from world coordinates at one depth
static void wquad(float z, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c)
{
	c = fogc(c, z);
	quad(PX(x0, z), PY(y0, z), PX(x1, z), PY(y1, z), PX(x2, z), PY(y2, z), PX(x3, z), PY(y3, z), c);
}

// The "Big A": a tall red A-frame with a halo ring on top, and a big board you run under. (A parody: "ANGLE STADIUM, ANAHIM".)
static void draw_big_a(float z)
{
	if (z < 1.8f) return;
	const u32 red = RGB(200, 30, 42), redD = RGB(110, 12, 22), redL = RGB(232, 70, 74), white = RGB(252, 252, 252);
	for (int side = -1; side <= 1; side += 2) {
		float o = 0.16f;
		wquad(z, side * 6.1f, 0, side * 4.5f, 0, side * 0.3f, 12.0f, side * 1.0f, 12.0f, redD);                       // legs (dark outline first)
		wquad(z, side * (6.1f - o), o, side * (4.5f + o), o, side * (0.3f + o * 0.5f), 12.0f - o, side * (1.0f - o * 0.5f), 12.0f - o, red);
	}
	box(-3.0f, 3.0f, 3.6f, 6.6f, z + 0.3f, z + 0.9f, red, redL, redD, RGB(70, 8, 14));                           // the big board
	rect3d(1, -2.7f, 2.7f, 3.72f, 3.8f, z + 0.3f, white);
	if (z > 2.0f) {                                                                                                // logo: a big "A" wearing a halo
		float s = S(z + 0.3f), cx = PX(-2.0f, z + 0.3f);
		u32 wh = fogc(white, z), rd = fogc(red, z);
		C2D_DrawTriangle(cx, PY(6.2f, z + 0.3f), wh, cx - 0.65f * s, PY(4.0f, z + 0.3f), wh, cx + 0.65f * s, PY(4.0f, z + 0.3f), wh, 0);
		C2D_DrawTriangle(cx, PY(5.75f, z + 0.3f), rd, cx - 0.4f * s, PY(4.1f, z + 0.3f), rd, cx + 0.4f * s, PY(4.1f, z + 0.3f), rd, 0);
		C2D_DrawTriangle(cx, PY(5.0f, z + 0.3f), wh, cx - 0.16f * s, PY(4.1f, z + 0.3f), wh, cx + 0.16f * s, PY(4.1f, z + 0.3f), wh, 0);
		ell3d(-2.0f, 6.45f, z + 0.3f, 1.0f, 0.34f, white);
		ell3d(-2.0f, 6.45f, z + 0.3f, 0.66f, 0.16f, red);
	}
	text_at(&t_angle, 0.7f, 5.55f, 3.9f, z + 0.3f, white);
	text_at(&t_anahim, 0.7f, 4.5f, 2.0f, z + 0.3f, white);
	box(-1.5f, 1.5f, 7.3f, 8.7f, z + 0.3f, z + 0.9f, red, redL, redD, RGB(70, 8, 14));                            // the sponsor's sign
	text_at(&t_farce, 0.0f, 8.0f, 2.6f, z + 0.3f, white);
	for (int i = 0; i < 20; i++) {                                                                                 // the halo on top
		float a0 = i * 0.31416f, a1 = a0 + 0.31416f;
		line3d(1, cosf(a0) * 2.5f, 13.4f + sinf(a0) * 0.85f, cosf(a1) * 2.5f, 13.4f + sinf(a1) * 0.85f, z + 0.3f, 0.2f, red);
	}
	for (int i = 0; i < 6; i++)
		line3d(1, 0, 13.4f, cosf(i * 1.0472f) * 2.4f, 13.4f + sinf(i * 1.0472f) * 0.8f, z + 0.3f, 0.06f, RGB(230, 230, 236));
}

// The stadium's brick wall across the road, with a one-lane gate in it
static void draw_stadium_wall(float z)
{
	if (z < 1.8f) return;
	const u32 brick = RGB(176, 60, 50), brickT = RGB(206, 96, 82), brickS = RGB(126, 38, 32), edge = RGB(60, 18, 14), trim = RGB(244, 240, 230);
	box(-46.0f, -1.5f, 0, 9.0f, z, z + 2.0f, brick, brickT, brickS, edge);
	box(1.5f, 46.0f, 0, 9.0f, z, z + 2.0f, brick, brickT, brickS, edge);
	box(-1.6f, 1.6f, 4.3f, 9.0f, z, z + 2.0f, brick, brickT, brickS, edge);
	rect3d(1, -46.0f, 46.0f, 8.2f, 8.7f, z, trim);                                                                 // white trim along the top
	rect3d(1, -46.0f, 46.0f, 4.0f, 4.15f, z, trim);
	for (int side = -1; side <= 1; side += 2)                                                                     // rows of arched windows
		for (int i = 0; i < 4; i++) {
			float d = 3.8f + i * 2.4f;
			rect3d(side, d, d + 1.0f, 5.0f, 7.2f, z, RGB(58, 30, 30));
			rect3d(side, d + 0.2f, d + 0.8f, 5.2f, 6.6f, z, RGB(96, 130, 170));
		}
	rect3d(1, -1.5f, 1.5f, 0, 4.3f, z + 2.0f, RGB(20, 16, 24));                                                    // the tunnel is dark
	box(-6.5f, 6.5f, 9.0f, 11.0f, z + 0.4f, z + 1.2f, trim, RGB(255, 255, 255), RGB(196, 192, 184), edge);      // big name plate on top
	text_at(&t_angle, 0.0f, 10.0f, 10.0f, z + 0.4f, RGB(200, 30, 42));
}

// A base on the road, with a sign beside it. idx 0..2 are 1st..3rd, 3 is home plate (with the launch arch).
static void draw_base(int idx, float z)
{
	float z0 = z - 0.8f, z1 = z + 0.8f;
	if (z1 < 1.6f) return;
	if (z0 < 1.3f) z0 = 1.3f;
	u32 white = fogc(RGB(252, 252, 252), z), gray = fogc(RGB(200, 200, 200), z);
	float hw = idx == 3 ? 1.0f : 0.8f;
	quad(PX(-hw - 0.08f, z0), PY(0, z0), PX(hw + 0.08f, z0), PY(0, z0), PX(hw + 0.08f, z1), PY(0, z1), PX(-hw - 0.08f, z1), PY(0, z1), gray);
	quad(PX(-hw, z0), PY(0, z0), PX(hw, z0), PY(0, z0), PX(hw, z1), PY(0, z1), PX(-hw, z1), PY(0, z1), white);
	if (z < 2.0f) return;
	box(1.9f, 2.05f, 0, 2.0f, z, z + 0.15f, RGB(120, 110, 100), RGB(150, 140, 128), RGB(90, 80, 72), RGB(40, 34, 28));   // sign post
	box(1.15f, 2.85f, 1.4f, 2.2f, z - 0.05f, z + 0.2f, RGB(28, 42, 104), RGB(56, 74, 150), RGB(18, 28, 72), RGB(10, 14, 40));
	text_at(&t_base[idx], 2.0f, 1.8f, 1.5f, z - 0.05f, RGB(255, 255, 255));
	if (idx == 3) {                                                                                                // launch pad chevrons + arch
		for (int i = 0; i < 4; i++) {
			float zc = z - 1.6f - i * 1.7f;
			if (zc < 1.4f) continue;
			u32 c = ((int)(g_time * 6.0f) + i) & 1 ? RGB(255, 226, 40) : RGB(255, 120, 30);
			float zn = zc - 0.5f, zf = zc + 0.3f;
			if (zn < 1.3f) zn = 1.3f;
			c = fogc(c, zc);
			C2D_DrawTriangle(PX(0, zf), PY(0, zf), c, PX(-0.9f, zn), PY(0, zn), c, PX(0.9f, zn), PY(0, zn), c, 0);
		}
		box(-1.7f, -1.4f, 0, 5.4f, z + 0.5f, z + 0.9f, RGB(250, 200, 30), RGB(255, 226, 90), RGB(200, 150, 10), RGB(70, 50, 10));
		box(1.4f, 1.7f, 0, 5.4f, z + 0.5f, z + 0.9f, RGB(250, 200, 30), RGB(255, 226, 90), RGB(200, 150, 10), RGB(70, 50, 10));
		box(-1.8f, 1.8f, 4.6f, 5.9f, z + 0.5f, z + 0.9f, RGB(250, 200, 30), RGB(255, 226, 90), RGB(200, 150, 10), RGB(70, 50, 10));
		text_at(&t_launch, 0.0f, 5.25f, 3.0f, z + 0.5f, RGB(200, 30, 42));
	}
}

// crowd stands lining the stadium corridor: two steps of seats with people on them, and an ad wall behind
static void draw_stand(int side, float z, unsigned h)
{
	u32 a = ((h >> 4) & 1) ? RGB(190, 36, 44) : RGB(28, 60, 140);
	u32 b = ((h >> 4) & 1) ? RGB(28, 60, 140) : RGB(190, 36, 44);
	box(fminf(side * 2.3f, side * 3.7f), fmaxf(side * 2.3f, side * 3.7f), 0, 0.7f, z, z + 6.6f, a, a, a, RGB(30, 20, 30));
	box(fminf(side * 3.7f, side * 5.1f), fmaxf(side * 3.7f, side * 5.1f), 0, 1.5f, z, z + 6.6f, b, b, b, RGB(30, 20, 30));
	box(fminf(side * 5.1f, side * 5.5f), fmaxf(side * 5.1f, side * 5.5f), 0, 2.6f, z, z + 6.6f, RGB(232, 232, 238), RGB(244, 244, 250), RGB(190, 190, 200), RGB(30, 20, 30));
	if (z > 2.2f) {
		float s = S(z);
		for (int t = 0; t < 2; t++) {                                                    // one fan per step, in red or white
			unsigned hh = hash(h * 7u + t * 31u);
			static const float dd0[2] = { 2.3f, 3.7f };
			float dd = dd0[t] + 0.35f + (hh & 3) * 0.2f, ht = t ? 1.5f : 0.7f;
			float cx = PX(side * dd, z), cy = PY(ht, z);
			u32 sc = (hh & 4) ? RGB(240, 200, 160) : RGB(170, 120, 84);
			u32 shirt = (hh & 8) ? RGB(230, 40, 44) : RGB(252, 252, 252);
			C2D_DrawRectSolid(cx - 0.17f * s, cy - 0.5f * s, 0, 0.34f * s, 0.3f * s, fogc(sc, z));
			C2D_DrawRectSolid(cx - 0.24f * s, cy - 0.2f * s, 0, 0.48f * s, 0.24f * s, fogc(shirt, z));
		}
		rect3d(side, 5.1f, 5.5f, 2.0f, 2.2f, z, RGB(28, 60, 140));                        // stripe on the ad wall
	}
}

static void draw_arch(float z, const C2D_Text *txt)
{
	if (z < 1.8f) return;
	box(-4.2f, -3.4f, 0, 6.2f, z, z + 0.8f, RGB(255, 150, 205), RGB(255, 190, 225), RGB(220, 110, 170), RGB(90, 30, 70));
	box( 3.4f,  4.2f, 0, 6.2f, z, z + 0.8f, RGB(255, 150, 205), RGB(255, 190, 225), RGB(220, 110, 170), RGB(90, 30, 70));
	box(-4.2f,  4.2f, 5.0f, 6.9f, z, z + 0.8f, RGB(88, 60, 190), RGB(130, 100, 230), RGB(60, 40, 140), RGB(30, 20, 80));
	ell3d(0.0f, 7.9f, z + 0.3f, 2.0f, 2.0f, RGB(96, 64, 158));                       // three round purple shapes on top
	ell3d(-1.15f, 8.8f, z + 0.3f, 1.2f, 1.2f, RGB(96, 64, 158));
	ell3d( 1.15f, 8.8f, z + 0.3f, 1.2f, 1.2f, RGB(96, 64, 158));
	if (z > 2.0f && z < 60.0f) {
		float w, h;
		C2D_TextGetDimensions(txt, 1.0f, 1.0f, &w, &h);
		float sc = 7.4f * S(z) / w;
		C2D_DrawText(txt, C2D_WithColor | C2D_AlignCenter, PX(0, z), PY(5.95f, z) - h * sc * 0.5f, 0, sc, sc, fogc(RGB(255, 236, 130), z));
	}
}

#define START_GATE_S 30.0f     // where the university gate stands over the road at the start of a run

typedef struct { float z; int type; bool done; } Prop;    // type: 0 turn sign, 1 Dizzyland entrance, 2 Dizzyland exit, 3 the start gate,
                                                            // 4 the Big A, 5 the stadium wall, 6..9 the bases (9 = home)

static void draw_prop(const Game *g, const Prop *p)
{
	if (p->type == 0) draw_turn_sign(g, p->z);
	else if (p->type == 3) draw_start_gate(p->z);
	else if (p->type == 4) draw_big_a(p->z);
	else if (p->type == 5) draw_stadium_wall(p->z);
	else if (p->type >= 6) draw_base(p->type - 6, p->z);
	else draw_arch(p->z, p->type == 1 ? &t_welcome : &t_bye);
}

// temple pillars and jungle trees on campus, fake-theme-park stuff in Dizzyland, along both sides of the road
static void draw_decor(const Game *g)
{
	const float SP = 7.0f;
	Prop props[8];
	int np = 0;
	if (g->evKind == EV_TURN) props[np++] = (Prop){ g->evStart - 25.0f - g->dist, 0, false };
	props[np++] = (Prop){ START_GATE_S - g->dist, 3, false };
	if (g->evKind == EV_STADIUM) {
		props[np++] = (Prop){ g->evStart + STAD_BIGA - g->dist, 4, false };
		props[np++] = (Prop){ g->evStart + STAD_GATE - g->dist, 5, false };
		for (int i = 0; i < 3; i++) props[np++] = (Prop){ g->evStart + STAD_BASE1 + i * STAD_BASE_GAP - g->dist, 6 + i, false };
		props[np++] = (Prop){ g->evStart + STAD_HOME - g->dist, 9, false };
	}
	if (g->evKind == EV_DISNEY) {
		props[np++] = (Prop){ g->evStart + 20.0f - g->dist, 1, false };      // the entrance gate
		props[np++] = (Prop){ g->evEnd - 20.0f - g->dist, 2, false };        // ...and the exit
	}

	for (int i = 1; i < np; i++)                                            // far to near, so overlapping props stack correctly
		for (int j = i; j > 0 && props[j - 1].z < props[j].z; j--) { Prop t = props[j]; props[j] = props[j - 1]; props[j - 1] = t; }

	int n0 = (int)floorf(g->dist / SP);
	for (int n = n0 + 11; n >= n0; n--) {
		float s = n * SP;
		float z = s - g->dist;
		if (z < 1.6f || z > 74.0f) continue;

		for (int i = 0; i < np; i++)                                        // props farther than this slot go first
			if (!props[i].done && props[i].z >= z) {
				props[i].done = true;
				if (props[i].z > 76.0f) continue;
				draw_prop(g, &props[i]);
			}

		bool dizzy = zone_k(g, s) > 0.5f;
		int lkind = 0, lside = 1;
		bool stadEv = g->evKind == EV_STADIUM;
		float ps = s - g->evStart;                                              // distance into the event
		float ks = stadium_k(g, s);
		bool inStad = stadEv && ps >= STAD_GATE && ks > 0.5f;                   // inside the ball park: stands instead of scenery
		bool atGate = (s > START_GATE_S - 8.0f && s < START_GATE_S + 14.0f)     // keep the surroundings of big props clear
		           || (stadEv && ps > STAD_BIGA - 9.0f && ps < STAD_BIGA + 12.0f)
		           || (stadEv && ps > STAD_GATE - 10.0f && ps < STAD_GATE);
		float extra = LANE_W * track_wide(g, s);                                // the road has an extra lane on each side
		bool landmark = !atGate && ks < 0.05f && landmark_for_slot(n, dizzy, &lkind, &lside);
		if (landmark) {
			g_lmoff = extra;
			draw_landmark(lkind, lside, z);
		}

		if ((n & 3) == 0) {                                                     // squirrels: one candidate every 4 slots
			unsigned sh = hash((unsigned)n * 131u + 17u);
			if (sh % 100 < 12 && !atGate && ks < 0.05f) {                     // ...and only a few of them run
				const float Z0 = 42.0f, Z1 = 5.0f;                              // they run in from one side while we approach
				float u = (Z0 - z) / (Z0 - Z1);
				if (u > 0 && u < 1) {
					int dir = ((sh >> 8) & 1) ? 1 : -1;
					float sx = dir * (-10.0f + 20.0f * u);
					draw_squirrel(sx, z, dir, (float)(sh & 255) * 0.1f);
				}
			}
		}
		for (int side = -1; side <= 1; side += 2) {
			// keep that side clear around a landmark (including the slots in front of it, whose trees would hide it)
			bool clear = false;
			for (int d = 0; d <= 2; d++) {
				int k2, s2;
				if (landmark_for_slot(n + d, zone_k(g, (n + d) * SP) > 0.5f, &k2, &s2) && s2 == side) clear = true;
			}
			unsigned h = hash((unsigned)(n * 2 + (side > 0)));
			if (inStad) {
				draw_stand(side, z, h);
				if (h % 5 == 0) {                                                  // floodlight tower behind the stands
					float lxp = side * 8.4f;
					box(lxp - 0.15f, lxp + 0.15f, 0, 8.0f, z, z + 0.3f, RGB(140, 140, 148), RGB(170, 170, 178), RGB(110, 110, 118), RGB(40, 40, 46));
					box(lxp - 1.0f, lxp + 1.0f, 8.0f, 9.2f, z - 0.05f, z + 0.3f, RGB(255, 250, 214), RGB(255, 255, 230), RGB(210, 206, 176), RGB(60, 56, 44));
				}
				continue;
			}
			if (clear || atGate) continue;
			float x = side * (4.5f + (h >> 8) % 3 * 0.7f + extra);
			int kind = dizzy ? (int)((h >> 4) % 4) : (int)(h % 3);
			if (dizzy) {
				draw_dizzy_decor(kind, x, z, h);
			} else if (kind == 0) {                                   // stone pillar
				box(x - 0.45f, x + 0.45f, 0, 3.2f, z, z + 0.9f,
				    RGB(196, 184, 160), RGB(226, 216, 194), RGB(150, 140, 120), RGB(60, 52, 40));
				box(x - 0.62f, x + 0.62f, 3.2f, 3.55f, z - 0.1f, z + 1.0f,
				    RGB(176, 164, 140), RGB(216, 206, 184), RGB(130, 120, 100), RGB(60, 52, 40));
				ell3d(x - 0.15f, 3.55f, z + 0.4f, 0.5f, 0.3f, RGB(84, 150, 60));     // moss on top
			} else if (kind == 1) {                                   // jungle tree
				box(x - 0.22f, x + 0.22f, 0, 2.6f, z, z + 0.44f,
				    RGB(120, 84, 52), RGB(140, 100, 64), RGB(90, 62, 38), RGB(46, 30, 18));
				ell3d(x,          3.6f, z + 0.2f, 3.0f, 2.2f, RGB(34, 110, 44));
				ell3d(x - 0.8f,   3.1f, z + 0.2f, 2.0f, 1.6f, RGB(46, 132, 54));
				ell3d(x + 0.7f,   3.3f, z + 0.2f, 2.0f, 1.7f, RGB(56, 146, 62));
			} else {                                                  // ruined wall + bush
				box(x - 0.9f, x + 0.9f, 0, 1.1f, z, z + 0.7f,
				    RGB(170, 158, 134), RGB(206, 196, 172), RGB(126, 116, 96), RGB(60, 52, 40));
				ell3d(x + 0.3f, 1.2f, z + 0.3f, 1.6f, 0.9f, RGB(52, 128, 50));
			}
		}
	}
	for (int i = 0; i < np; i++)                                            // props nearer than every slot
		if (!props[i].done && props[i].z <= 76.0f) {
			props[i].done = true;
			draw_prop(g, &props[i]);
		}
}

// ---- obstacles and coins -----------------------------------------------
static void draw_obj(const Game *g, const Obj *o)
{
	float z = o->z, wx = o->lane * LANE_W;
	switch (o->type) {
	case OBJ_ROCK:
		box(wx - 0.72f, wx + 0.72f, 0, 2.0f, z - 0.4f, z + 0.4f,
		    RGB(152, 142, 126), RGB(196, 186, 164), RGB(108, 100, 88), RGB(40, 34, 28));
		if (z > 1.6f) {                                        // carved face + cracks + moss
			float s = S(z - 0.4f);
			float cx = PX(wx, z - 0.4f), cy = PY(1.1f, z - 0.4f);
			u32 dk = fogc(RGB(70, 62, 52), z);
			C2D_DrawRectSolid(cx - 0.34f * s, cy - 0.22f * s, 0, 0.22f * s, 0.14f * s, dk);
			C2D_DrawRectSolid(cx + 0.12f * s, cy - 0.22f * s, 0, 0.22f * s, 0.14f * s, dk);
			C2D_DrawRectSolid(cx - 0.3f * s, cy + 0.2f * s, 0, 0.6f * s, 0.1f * s, dk);
			C2D_DrawLine(cx - 0.5f * s, PY(1.9f, z - 0.4f), dk, cx - 0.3f * s, cy - 0.35f * s, dk, fmaxf(1, 0.03f * s), 0);
			ell3d(wx - 0.35f, 2.0f, z - 0.4f, 0.7f, 0.28f, RGB(84, 150, 60));
		}
		break;

	case OBJ_HURDLE: {
		box(wx - 0.78f, wx + 0.78f, 0, 0.7f, z - 0.35f, z + 0.35f,
		    RGB(160, 100, 60), RGB(200, 140, 92), RGB(112, 70, 42), RGB(46, 28, 16));
		if (z > 1.6f) {                                        // warning stripes
			float zf = z - 0.35f, s = S(zf);
			for (int i = 0; i < 5; i++) {
				float u0 = wx - 0.78f + i * 0.312f + 0.04f;
				u32 c = (i & 1) ? RGB(40, 34, 30) : RGB(250, 200, 40);
				C2D_DrawRectSolid(PX(u0, zf), PY(0.5f, zf), 0, 0.16f * s, 0.32f * s, fogc(c, zf));
			}
		}
		break;
	}

	case OBJ_BEAM:
		for (int side = -1; side <= 1; side += 2)              // two posts
			box(wx + side * 0.72f - 0.16f, wx + side * 0.72f + 0.16f, 0, 1.7f, z - 0.25f, z + 0.25f,
			    RGB(140, 120, 100), RGB(180, 160, 138), RGB(100, 84, 68), RGB(40, 32, 24));
		box(wx - 0.95f, wx + 0.95f, 1.0f, 1.65f, z - 0.4f, z + 0.4f,      // lintel: slide under it
		    RGB(52, 128, 130), RGB(92, 176, 176), RGB(34, 90, 92), RGB(20, 44, 46));
		if (z > 1.6f) {
			float zf = z - 0.4f, s = S(zf);
			float cx = PX(wx, zf), cy = PY(1.32f, zf);
			C2D_DrawRectSolid(cx - 0.8f * s, cy - 0.05f * s, 0, 1.6f * s, 0.1f * s, fogc(RGB(240, 196, 60), zf));
		}
		break;

	case OBJ_PETITION: {                                                 // a student holding out a giant petition clipboard
		int v = (o->lane + 5) % 3;
		u32 shirt = v == 0 ? RGB(250, 218, 40) : v == 1 ? RGB(240, 90, 90) : RGB(80, 200, 130);   // bright, so you notice them
		u32 jeans = RGB(52, 84, 156), skin = v == 1 ? RGB(150, 100, 72) : RGB(236, 186, 146), hair = v == 2 ? RGB(230, 190, 90) : RGB(60, 40, 30);
		float bob = sinf(g->time * 6.0f + o->lane * 2.0f) * 0.05f;
		box(wx - 0.34f, wx - 0.06f, 0, 0.85f, z - 0.12f, z + 0.12f, jeans, jeans, jeans, RGB(20, 30, 60));
		box(wx + 0.06f, wx + 0.34f, 0, 0.85f, z - 0.12f, z + 0.12f, jeans, jeans, jeans, RGB(20, 30, 60));
		ell3d(wx, 1.35f, z, 0.98f, 1.05f, shirt);                             // body
		ell3d(wx, 2.05f, z, 0.56f, 0.6f, skin);                               // head
		ell3d(wx, 2.24f, z, 0.6f, 0.34f, hair);
		if (z > 2.4f) {
			float s = S(z), cx = PX(wx, z), ey = PY(2.07f, z);
			u32 eye = fogc(RGB(20, 20, 30), z);
			C2D_DrawRectSolid(cx - 0.15f * s, ey, 0, 0.07f * s, 0.09f * s, eye);
			C2D_DrawRectSolid(cx + 0.08f * s, ey, 0, 0.07f * s, 0.09f * s, eye);
			C2D_DrawRectSolid(cx - 0.1f * s, PY(1.9f, z), 0, 0.2f * s, 0.06f * s, eye);   // a hopeful smile
			float zf = z - 0.45f, sf = S(zf), bx = PX(wx, zf), by = PY(1.0f + bob, zf);   // arms out to the clipboard
			u32 arm = fogc(skin, z);
			C2D_DrawLine(PX(wx - 0.5f, z), PY(1.7f, z), arm, bx - 0.5f * sf, by, arm, fmaxf(1.0f, 0.16f * s), 0);
			C2D_DrawLine(PX(wx + 0.5f, z), PY(1.7f, z), arm, bx + 0.5f * sf, by, arm, fmaxf(1.0f, 0.16f * s), 0);
		}
		box(wx - 0.62f, wx + 0.62f, 0.55f + bob, 1.3f + bob, z - 0.5f, z - 0.4f,     // the clipboard
		    RGB(250, 250, 246), RGB(255, 255, 255), RGB(196, 196, 190), RGB(60, 44, 30));
		if (z > 2.4f) {
			float zf = z - 0.5f, sf = S(zf), cx = PX(wx, zf);
			u32 ink = fogc(RGB(70, 76, 96), zf);
			for (int i = 0; i < 4; i++)                                             // lines to sign on
				C2D_DrawRectSolid(cx - 0.46f * sf, PY(1.12f - i * 0.17f + bob, zf), 0, 0.92f * sf, fmaxf(1.0f, 0.03f * sf), ink);
			C2D_DrawRectSolid(cx - 0.18f * sf, PY(1.36f + bob, zf), 0, 0.36f * sf, 0.1f * sf, fogc(RGB(150, 152, 160), zf));   // the clip
		}
		if (z > 3.0f && z < 34.0f) {                                                 // "SIGN HERE!"
			float s = S(z);
			fill_ellipse(PX(wx + 0.5f, z) - 0.95f * s, PY(2.95f, z) - 0.32f * s, 0, 1.9f * s, 0.64f * s, fogc(RGB(255, 255, 255), z));
			text_at(&t_sign, wx + 0.5f, 2.95f, 1.5f, z, RGB(200, 40, 40));
		}
		break;
	}

	case OBJ_SCOOTER:
		draw_scooter(g, o);
		break;

	case OBJ_COIN: {
		float s = S(z);
		float spin = fabsf(cosf(g->time * 6.0f + z * 0.5f));
		float w = fmaxf(0.06f, 0.62f * spin) * s, h = 0.62f * s;
		float cx = PX(wx, z), cy = PY(o->y, z);
		fill_ellipse(cx - w * 0.5f - 1, cy - h * 0.5f - 1, 0, w + 2, h + 2, fogc(RGB(120, 76, 8), z));
		fill_ellipse(cx - w * 0.5f, cy - h * 0.5f, 0, w, h, fogc(RGB(252, 204, 40), z));
		fill_ellipse(cx - w * 0.28f, cy - h * 0.28f, 0, w * 0.56f, h * 0.56f, fogc(RGB(255, 230, 120), z));
		// little ground shadow
		fill_ellipse(cx - 0.2f * s, PY(0, z) - 0.05f * s, 0, 0.4f * s, 0.1f * s, RGBA(0, 0, 0, 60));
		break;
	}
	}
}

// ---- Tuffy -------------------------------------------------------------
static float T_ox, T_oy, T_k, T_sx, T_sq;

#define T_GRAY   RGB(226, 226, 228)
#define T_GRAYD  RGB(168, 168, 176)
#define T_BLACK  RGB(14, 14, 16)
#define T_NAVY   RGB(20, 62, 122)
#define T_ORANGE RGB(245, 126, 28)

static void t_ell(float u, float v, float w, float h, u32 col, u32 edge)   // centred ellipse
{
	float o = 0.06f * T_k;
	float cx = T_ox + u * T_k * T_sx, cy = T_oy - v * T_k * T_sq;
	float W = w * T_k * T_sx, H = h * T_k * T_sq;
	if (edge) fill_ellipse(cx - W / 2 - o, cy - H / 2 - o, 0, W + 2 * o, H + 2 * o, edge);
	fill_ellipse(cx - W / 2, cy - H / 2, 0, W, H, col);
}

static void t_rect(float u0, float v0, float u1, float v1, u32 col, u32 edge)
{
	float o = 0.06f * T_k;
	float x0 = T_ox + u0 * T_k * T_sx, x1 = T_ox + u1 * T_k * T_sx;
	float y1 = T_oy - v0 * T_k * T_sq, y0 = T_oy - v1 * T_k * T_sq;
	if (edge) C2D_DrawRectSolid(x0 - o, y0 - o, 0, x1 - x0 + 2 * o, y1 - y0 + 2 * o, edge);
	C2D_DrawRectSolid(x0, y0, 0, x1 - x0, y1 - y0, col);
}

// Tuffy the Titan elephant, seen from behind in his navy TITANS shirt.
static void draw_tuffy(const Game *g)
{
	float z = PLAYER_Z;
	bool air = g->y > 0.001f, dead = g->state == ST_DEAD;
	bool sliding = g->slideT > 0 && !air;

	float wx = g->x;
	T_k = S(z) * 0.9f; T_ox = PX(wx, z); T_oy = PY(g->y, z);
	T_sx = 1; T_sq = 1;
	if (sliding)  { T_sq = 0.62f; T_sx = 1.12f; }
	else if (air) { T_sq = 1.04f; }

	float ph = g->runPhase, sw = sinf(ph);
	float bob = (air || dead) ? 0 : fabsf(sw) * 0.06f;
	float liftL = 0, liftR = 0;
	if (air) liftL = liftR = 0.24f;
	else if (!dead) { liftL = fmaxf(0, sw) * 0.32f; liftR = fmaxf(0, -sw) * 0.32f; }

	// shadow on the ground
	{
		float sh = 1.0f / (1.0f + g->y * 0.7f), gx = PX(wx, z), gy = PY(0, z);
		fill_ellipse(gx - 0.75f * T_k * sh, gy - 0.17f * T_k * sh, 0, 1.5f * T_k * sh, 0.34f * T_k * sh, RGBA(0, 0, 0, 90));
	}

	// legs and feet
	t_rect(-0.36f, liftL + 0.08f, -0.06f, 0.75f, T_GRAY, T_BLACK);
	t_rect( 0.06f, liftR + 0.08f,  0.36f, 0.75f, T_GRAY, T_BLACK);
	t_ell(-0.21f, liftL + 0.07f, 0.44f, 0.22f, T_GRAYD, T_BLACK);
	t_ell( 0.21f, liftR + 0.07f, 0.44f, 0.22f, T_GRAYD, T_BLACK);

	// arms swing opposite to the legs (hands up when jumping)
	float aL = air ? 0.3f : -sw * 0.14f, aR = air ? 0.3f : sw * 0.14f;
	t_ell(-0.66f, 0.98f + bob + aL * 0.6f, 0.36f, 0.56f, T_NAVY, T_BLACK);
	t_ell( 0.66f, 0.98f + bob + aR * 0.6f, 0.36f, 0.56f, T_NAVY, T_BLACK);
	t_ell(-0.66f, 0.98f + bob + aL * 0.6f - 0.16f, 0.36f, 0.09f, T_ORANGE, 0);
	t_ell( 0.66f, 0.98f + bob + aR * 0.6f - 0.16f, 0.36f, 0.09f, T_ORANGE, 0);
	t_ell(-0.7f, 0.66f + bob + aL, 0.32f, 0.3f, T_GRAY, T_BLACK);
	t_ell( 0.7f, 0.66f + bob + aR, 0.32f, 0.3f, T_GRAY, T_BLACK);

	// shirt: orange hem peeks out under the navy body
	float bv = 1.0f + bob;
	t_ell(0, bv - 0.09f, 1.06f, 0.92f, T_ORANGE, T_BLACK);
	t_ell(0, bv,          1.06f, 0.92f, T_NAVY,   T_BLACK);

	// "TITANS" on the back
	{
		float w, h;
		C2D_TextGetDimensions(&t_titans, 1.0f, 1.0f, &w, &h);
		float sc = (0.8f * T_k * T_sx) / w;
		float cx = T_ox, cy = T_oy - (bv + 0.02f) * T_k * T_sq - h * sc * 0.5f * T_sq;
		C2D_DrawText(&t_titans, C2D_WithColor | C2D_AlignCenter, cx + 1, cy + 1, 0, sc, sc * T_sq, RGB(8, 20, 50));
		C2D_DrawText(&t_titans, C2D_WithColor | C2D_AlignCenter, cx, cy, 0, sc, sc * T_sq, T_ORANGE);
	}

	// tail
	{
		float tw = sinf(ph * 2.0f) * 0.12f;
		float x0 = T_ox, y0 = T_oy - (0.62f + bob) * T_k * T_sq;
		float x1 = T_ox + tw * T_k, y1 = T_oy - 0.28f * T_k * T_sq;
		C2D_DrawLine(x0, y0, T_BLACK, x1, y1, T_BLACK, 0.16f * T_k, 0);
		C2D_DrawLine(x0, y0, T_GRAYD, x1, y1, T_GRAYD, 0.08f * T_k, 0);
		fill_circle(x1, y1, 0, 0.09f * T_k, T_BLACK);
	}

	// big flappy ears, then the head
	float flap = 1.0f + 0.07f * sinf(ph * 2.0f);
	float hv = 1.82f + bob;
	t_ell(-0.7f, hv + 0.1f, 0.78f * flap, 1.0f, T_GRAY,  T_BLACK);
	t_ell( 0.7f, hv + 0.1f, 0.78f * flap, 1.0f, T_GRAY,  T_BLACK);
	t_ell(-0.74f, hv + 0.08f, 0.44f, 0.64f, T_GRAYD, 0);
	t_ell( 0.74f, hv + 0.08f, 0.44f, 0.64f, T_GRAYD, 0);
	t_ell(0, hv, 0.92f, 0.88f, T_GRAY, T_BLACK);
	t_ell(-0.15f, hv + 0.22f, 0.3f, 0.16f, RGB(246, 246, 248), 0);              // highlight

	// stars circling his head when he crashes
	if (dead && g->deadT > 0.12f) {
		for (int i = 0; i < 3; i++) {
			float a = g->deadT * 7.0f + i * 2.094f;
			t_ell(cosf(a) * 0.75f, hv + 0.62f + sinf(a) * 0.12f, 0.2f, 0.2f, RGB(255, 226, 60), T_BLACK);
		}
	}
}

// ---- scooter rider (comes up from behind, seen from behind) --------------
#define J_HOOD  RGB(226, 62, 48)
#define J_JEANS RGB(52, 84, 156)

static void draw_scooter(const Game *g, const Obj *o)
{
	float z = o->z;
	if (z < 1.4f) return;                                // still behind the camera
	float sway = sinf(g->time * 9.0f + o->lane) * 0.04f;
	T_k = S(z); T_ox = PX(o->lane * LANE_W + sway, z); T_oy = PY(0, z); T_sx = 1; T_sq = 1;
	float kick = fmaxf(0, sinf(g->time * 8.0f)) * 0.28f;

	fill_ellipse(T_ox - 0.7f * T_k, T_oy - 0.16f * T_k, 0, 1.4f * T_k, 0.32f * T_k, RGBA(0, 0, 0, 90));

	t_ell(0, 0.26f, 0.2f, 0.52f, RGB(44, 44, 50), T_BLACK);                        // rear wheel
	t_rect(-0.28f, 0.24f, 0.28f, 0.36f, RGB(96, 98, 110), T_BLACK);                // deck
	t_rect(-0.62f, 1.22f, 0.62f, 1.32f, RGB(72, 74, 84), T_BLACK);                 // handlebar (beyond the rider)

	t_rect(-0.32f, 0.34f, -0.07f, 1.05f, J_JEANS, T_BLACK);                        // one foot on the deck...
	t_rect( 0.07f, 0.34f + kick, 0.32f, 1.05f, J_JEANS, T_BLACK);                  // ...one pushing
	t_ell(-0.2f, 0.36f, 0.36f, 0.17f, RGB(245, 245, 245), T_BLACK);
	t_ell( 0.2f, 0.36f + kick, 0.36f, 0.17f, RGB(245, 245, 245), T_BLACK);

	t_ell(0, 1.52f, 0.96f, 1.02f, J_HOOD, T_BLACK);                                // hoodie
	t_ell(0, 1.48f, 0.62f, 0.72f, RGB(250, 204, 44), T_BLACK);                     // backpack
	t_rect(-0.16f, 1.34f, 0.16f, 1.42f, RGB(214, 166, 20), 0);

	for (int side = -1; side <= 1; side += 2) {                                    // arms to the handlebar
		float sx0 = T_ox + side * 0.46f * T_k, sy0 = T_oy - 1.78f * T_k;
		float gx = T_ox + side * 0.6f * T_k,   gy = T_oy - 1.28f * T_k;
		C2D_DrawLine(sx0, sy0, T_BLACK, gx, gy, T_BLACK, 0.3f * T_k, 0);
		C2D_DrawLine(sx0, sy0, J_HOOD, gx, gy, J_HOOD, 0.2f * T_k, 0);
		fill_circle(gx, gy, 0, 0.12f * T_k, RGB(30, 30, 34));
	}

	t_ell(0, 2.12f, 0.5f, 0.52f, RGB(232, 180, 140), T_BLACK);                     // head
	t_ell(0, 2.24f, 0.62f, 0.46f, RGB(245, 245, 250), T_BLACK);                    // helmet
	t_rect(-0.05f, 2.3f, 0.05f, 2.46f, J_HOOD, 0);
}

// Big flashing warning in the lane a scooter will use, so you can get out of the way in time.
static void draw_scooter_warning(const Game *g, int lane, float eta)
{
	float wx = lane * LANE_W;
	float rate = 3.0f + 6.0f * (1.0f - eta / SCOOTER_WARN);          // flashes faster as he closes in
	bool on = fmodf(g->time * rate, 1.0f) < 0.5f;

	quad(PX(wx - 0.8f, 3.4f), PY(0, 3.4f), PX(wx + 0.8f, 3.4f), PY(0, 3.4f),
	     PX(wx + 0.8f, 13.0f), PY(0, 13.0f), PX(wx - 0.8f, 13.0f), PY(0, 13.0f), RGBA(255, 40, 30, on ? 140 : 50));

	float ph = fmodf(g->time * 2.5f, 1.0f);                           // chevrons slide up the lane: he comes from behind
	for (int i = 0; i < 3; i++) {
		float z = 4.0f + (i + ph) * 2.8f, s = S(z), cx = PX(wx, z), cy = PY(0, z);
		float th = fmaxf(2.0f, 0.13f * s);
		C2D_DrawLine(cx - 0.55f * s, cy, RGB(255, 240, 200), cx, cy - 0.38f * s, RGB(255, 240, 200), th, 0);
		C2D_DrawLine(cx + 0.55f * s, cy, RGB(255, 240, 200), cx, cy - 0.38f * s, RGB(255, 240, 200), th, 0);
	}

	// warning sign at the bottom of the lane (pops out of the screen in 3D)
	float cx = PX(wx, 4.2f) - g_eye * g_sep * 0.8f, cy = 208.0f;
	u32 sign = on ? RGB(255, 208, 0) : RGB(255, 236, 120);
	C2D_DrawTriangle(cx - 22, cy + 17, T_BLACK, cx + 22, cy + 17, T_BLACK, cx, cy - 22, T_BLACK, 0);
	C2D_DrawTriangle(cx - 17, cy + 13, sign, cx + 17, cy + 13, sign, cx, cy - 15, sign, 0);
	C2D_DrawRectSolid(cx - 2.5f, cy - 7, 0, 5, 12, T_BLACK);                     // the "!"
	fill_circle(cx, cy + 8.5f, 0, 2.6f, T_BLACK);
	text(&t_scooter, cx, cy - 46, 0.6f, on ? RGB(255, 236, 90) : RGB(255, 255, 255), true, 0.8f);
}

// ---- screens -----------------------------------------------------------
static void draw_hud(const Game *g)
{
	// pops out of the screen a little in 3D
	text(&t_score, 12, 4, 0.9f, RGB(255, 255, 255), false, 0.7f);
	fill_circle(SCR_W - 90 - g_eye * g_sep * 0.7f, 20, 0, 9, RGB(120, 76, 8));
	fill_circle(SCR_W - 90 - g_eye * g_sep * 0.7f, 20, 0, 7, RGB(252, 204, 40));
	text(&t_coins, SCR_W - 74, 6, 0.75f, RGB(255, 226, 90), false, 0.7f);
	if (g->best > 0) text(&t_best, 12, 26, 0.5f, RGB(255, 220, 170), false, 0.7f);

	float sp = g->dist + PLAYER_Z;
	if (g->evKind == EV_STADIUM) {                                 // ball park messages
		float p = sp - g->evStart;
		const C2D_Text *msg = NULL;
		if (g->flying) msg = &t_launch;
		else if (p > STAD_GATE - 4.0f && p < STAD_GATE + 40.0f) msg = &t_playball;
		else for (int i = 0; i < 3; i++)
			if (p > STAD_BASE1 + i * STAD_BASE_GAP - 30.0f && p < STAD_BASE1 + i * STAD_BASE_GAP + 8.0f) msg = &t_base[i];
		if (!msg && p > STAD_HOME - 40.0f && p < STAD_HOME) msg = &t_base[3];
		if (msg && fmodf(g->time * 3.0f, 1.0f) < 0.75f) text(msg, SCR_W * 0.5f, g->flying ? 150 : 44, 0.9f, RGB(255, 236, 90), true, 0.8f);
	}
	// a turn is coming up
	if (g->evKind == EV_TURN && sp > g->evStart - 50.0f && sp < g->evStart + 15.0f && fmodf(g->time * 3.0f, 1.0f) < 0.7f)
		text(g->evSide < 0 ? &t_turnL : &t_turnR, SCR_W * 0.5f, 44, 0.75f, RGB(255, 226, 90), true, 0.8f);
}

void render_top(const Game *g, float eye, float slider)
{
	g_eye = eye; g_sep = slider * 7.0f;
	g_camx = g->x * 0.55f;
	g_shx = g_shy = 0;
	g_time = g->time;
	g_game = g;
	g_head = track_heading(g);
	g_curve_on = track_bent(g);
	g_zk = zone_k(g, g->dist + 40.0f);
	g_sk = stadium_k(g, g->dist + 40.0f);
	g_fog = haze_for(g_zk, g_sk);
	g_camh = 3.0f + (g->flying ? 0.6f * g->y : 0.0f);               // the camera climbs after Tuffy when he is launched
	if (g->state == ST_DEAD && g->deadT < 0.4f) {
		float k = (0.4f - g->deadT) * 30.0f;
		g_shx = sinf(g->deadT * 95.0f) * k;
		g_shy = cosf(g->deadT * 80.0f) * k * 0.6f;
	}

	draw_sky();
	draw_road(g);
	draw_decor(g);

	// objects sorted far -> near, with Tuffy drawn in between
	int order[MAX_OBJS];
	for (int i = 0; i < g->nobjs; i++) {
		int j = i;
		while (j > 0 && g->objs[order[j - 1]].z < g->objs[i].z) { order[j] = order[j - 1]; j--; }
		order[j] = i;
	}
	int i = 0;
	for (; i < g->nobjs && g->objs[order[i]].z >= PLAYER_Z; i++) draw_obj(g, &g->objs[order[i]]);
	bool hidden = g->state == ST_DEAD && g->deadT < 1.0f && fmodf(g->deadT, 0.16f) < 0.07f;
	if (!hidden) draw_tuffy(g);
	for (; i < g->nobjs; i++) draw_obj(g, &g->objs[order[i]]);

	g_shx = g_shy = 0;
	if (g->flying) {                                                          // speed lines while launched
		for (int i = 0; i < 14; i++) {
			float a = i * 0.4488f + g->time * 0.3f, r0 = 90.0f + 30.0f * fmodf(g->time * 3.0f + i * 0.37f, 1.0f), r1 = r0 + 40.0f;
			float cx = SCR_W * 0.5f, cy = 100.0f;
			C2D_DrawLine(cx + cosf(a) * r0 * 1.5f, cy + sinf(a) * r0, RGBA(255, 255, 255, 110), cx + cosf(a) * r1 * 1.5f, cy + sinf(a) * r1, RGBA(255, 255, 255, 110), 2, 0);
		}
	}
	int wlane; float weta;
	if (g->state == ST_PLAY && game_scooter_warning(g, &wlane, &weta)) draw_scooter_warning(g, wlane, weta);

	if (g->state == ST_DEAD && g->deadT < 0.25f)
		C2D_DrawRectSolid(0, 0, 0, SCR_W, SCR_H, RGBA(255, 40, 20, (int)(140 * (1.0f - g->deadT / 0.25f))));

	if (g->state == ST_TITLE) {
		text(&t_title, SCR_W * 0.5f, 18, 1.5f, T_ORANGE, true, 1.0f);
	} else {
		draw_hud(g);
		if (g->state == ST_DEAD) {
			C2D_DrawRectSolid(0, 0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, (int)fminf(120, g->deadT * 200)));
			if (g->deadT > 0.3f) text(&t_crashed, SCR_W * 0.5f, 70, 1.4f, RGB(255, 90, 60), true, 1.0f);
		} else if (g->state == ST_PAUSED) {
			C2D_DrawRectSolid(0, 0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 130));
			text(&t_paused, SCR_W * 0.5f, 90, 1.4f, RGB(255, 255, 255), true, 1.0f);
		}
	}
}

static void radar_obj(float cx, float y, const Obj *o, float k)      // k scales for narrower lanes
{
	switch (o->type) {
	case OBJ_COIN:   fill_circle(cx, y, 0, 3.5f, RGB(252, 204, 40)); break;
	case OBJ_ROCK:   C2D_DrawRectSolid(cx - 15 * k, y - 5, 0, 30 * k, 10, RGB(160, 150, 134)); break;
	case OBJ_HURDLE: C2D_DrawRectSolid(cx - 16 * k, y - 3, 0, 32 * k, 6, RGB(232, 130, 40));   break;
	case OBJ_BEAM:   C2D_DrawRectSolid(cx - 16 * k, y - 3, 0, 32 * k, 6, RGB(52, 170, 170));   break;
	case OBJ_PETITION:                                             // a little person with a clipboard
		fill_circle(cx, y - 6, 0, 3.2f, RGB(236, 186, 146));
		C2D_DrawRectSolid(cx - 6, y - 3, 0, 12, 8, RGB(250, 218, 40));
		C2D_DrawRectSolid(cx - 14 * k, y + 1, 0, 28 * k, 4, RGB(250, 250, 246));
		break;
	case OBJ_SCOOTER:                                              // red arrow: coming up from behind
		C2D_DrawTriangle(cx - 15, y + 6, T_BLACK, cx + 15, y + 6, T_BLACK, cx, y - 14, T_BLACK, 0);
		C2D_DrawTriangle(cx - 11, y + 3, RGB(255, 60, 40), cx + 11, y + 3, RGB(255, 60, 40), cx, y - 9, RGB(255, 60, 40), 0);
		C2D_DrawRectSolid(cx - 1.5f, y - 3, 0, 3, 6, RGB(255, 255, 255));
		break;
	}
}

void render_bottom(const Game *g)
{
	g_eye = 0; g_sep = 0; g_shx = g_shy = 0;
	u32 navy = RGB(16, 44, 92), navy2 = RGB(10, 26, 60);
	C2D_DrawRectangle(0, 0, 0, 320, 240, navy, navy, navy2, navy2);
	C2D_DrawRectSolid(0, 0, 0, 320, 4, T_ORANGE);

	if (g->state == ST_PLAY) {
		text(&t_lScore, 14, 20, 0.5f, RGB(255, 190, 120), false, 0);
		text(&t_scoreBig, 14, 36, 1.1f, RGB(255, 255, 255), false, 0);
		text(&t_lBest, 14, 90, 0.5f, RGB(255, 190, 120), false, 0);
		text(&t_bestBig, 14, 106, 0.8f, RGB(255, 255, 255), false, 0);
		text(&t_lCoins, 14, 146, 0.5f, RGB(255, 190, 120), false, 0);
		text(&t_coinsBig, 14, 162, 0.8f, RGB(255, 226, 90), false, 0);

		// radar: what's coming, seen from above. Ahead is up; a scooter rider arrives from below.
		// Normally 3 lane columns; while the road is wide (after a turn) there are 5.
		const float LX = 235, y0 = 22, y1 = 216, ys = 168;
		float sp = g->dist + PLAYER_Z;
		int cmin = -1, cmax = 1;
		bool fork = g->evKind == EV_TURN && sp + (SPAWN_Z - PLAYER_Z) > g->evStart + TURN_WIDE_A - 12.0f && sp < g->evStart + TURN_WIDE_B + 12.0f;
		if (fork) { cmin = -2; cmax = 2; }                       // the road has 5 lanes for a while
		if (g->evKind == EV_STADIUM && sp >= g->evStart + STAD_NARROW_A && sp < g->evStart + STAD_WIDE) { cmin = cmax = 0; }   // ...or one lane in the stadium
		int nc = cmax - cmin + 1;
		float LW = nc == 3 ? 46.0f : 32.0f, X0 = LX - LW * nc * 0.5f;
		#define COLX(l) (X0 + ((l) - cmin + 0.5f) * LW)

		int wl; float weta;
		bool warn = game_scooter_warning(g, &wl, &weta);
		bool on = warn && fmodf(g->time * (3.0f + 6.0f * (1.0f - weta / SCOOTER_WARN)), 1.0f) < 0.5f;

		if (!fork) {
			C2D_DrawRectSolid(X0, y0, 0, LW * nc, y1 - y0, RGBA(0, 0, 0, 90));
		} else {                                                   // draw each lane only where it exists at that distance
			for (int l = cmin; l <= cmax; l++) {
				for (float y = y0; y < y1; y += 6.0f) {
					float f = y < ys ? (ys - (y + 3.0f)) / (ys - y0 - 6.0f) : 0.0f;
					float s = sp + f * (SPAWN_Z - PLAYER_Z);
					if (!lane_exists(g, l, s)) continue;
					C2D_DrawRectSolid(X0 + (l - cmin) * LW + 1, y, 0, LW - 2, 6.0f, (l < -1 || l > 1) ? RGBA(255, 160, 60, 70) : RGBA(0, 0, 0, 90));
				}
			}
		}
		if (warn) C2D_DrawRectSolid(COLX(wl) - LW * 0.5f, y0, 0, LW, y1 - y0, on ? RGBA(255, 40, 30, 110) : RGBA(255, 40, 30, 40));
		if (!fork) {
			C2D_DrawRectSolid(LX - LW * 0.5f - 1, y0, 0, 2, y1 - y0, RGBA(255, 255, 255, 60));
			C2D_DrawRectSolid(LX + LW * 0.5f - 1, y0, 0, 2, y1 - y0, RGBA(255, 255, 255, 60));
		}
		C2D_DrawRectSolid(X0, ys, 0, LW * nc, 1, RGBA(255, 255, 255, 40));   // Tuffy's line
		for (int i = 0; i < g->nobjs; i++) {
			const Obj *o = &g->objs[i];
			if (o->lane < cmin || o->lane > cmax) continue;
			float y;
			if (o->type == OBJ_SCOOTER) {
				float eta = (PLAYER_Z - o->z) / SCOOTER_SPEED;
				if (eta > SCOOTER_WARN || eta < -0.4f) continue;
				y = ys + (eta / SCOOTER_WARN) * (y1 - ys - 12);
			} else {
				float f = (o->z - PLAYER_Z) / (SPAWN_Z - PLAYER_Z);
				if (f < -0.05f || f > 1.0f) continue;
				y = ys - f * (ys - y0 - 6);
			}
			radar_obj(COLX(o->lane), y, o, LW / 46.0f);
		}
		float px = X0 + (g->x / LANE_W - cmin + 0.5f) * LW;
		C2D_DrawTriangle(px - 10, ys + 12, T_BLACK, px + 10, ys + 12, T_BLACK, px, ys - 10, T_BLACK, 0);
		C2D_DrawTriangle(px - 7, ys + 9, T_ORANGE, px + 7, ys + 9, T_ORANGE, px, ys - 5, T_ORANGE, 0);
		if (warn) text(&t_scooter, 70, 200, 0.6f, on ? RGB(255, 236, 90) : RGB(255, 255, 255), true, 0);
		#undef COLX
		return;
	}

	text(&t_hdr, 160, 16, 1.0f, T_ORANGE, true, 0);
	if (g->state == ST_TITLE) {
		text(&t_start, 160, 62, 0.8f, RGB(255, 255, 255), true, 0);
		text(&t_help1, 160, 118, 0.6f, RGB(200, 220, 255), true, 0);
		text(&t_help2, 160, 142, 0.6f, RGB(200, 220, 255), true, 0);
		text(&t_help3, 160, 166, 0.6f, RGB(200, 220, 255), true, 0);
		text(&t_help4, 160, 192, 0.5f, RGB(255, 140, 120), true, 0);
		text(&t_exit,  160, 216, 0.5f, RGB(255, 190, 120), true, 0);
	} else if (g->state == ST_DEAD) {
		text(&t_cause,    160, 50, 0.6f, RGB(255, 170, 150), true, 0);
		text(&t_lScore,   160, 82, 0.5f, RGB(255, 190, 120), true, 0);
		text(&t_scoreBig, 160, 96, 1.2f, RGB(255, 255, 255), true, 0);
		if (g->newBest && fmodf(g->deadT, 0.6f) < 0.4f)
			text(&t_newbest, 160, 140, 0.8f, RGB(255, 226, 90), true, 0);
		else if (!g->newBest)
			text(&t_bestBig, 160, 142, 0.7f, RGB(200, 220, 255), true, 0);
		text(&t_retry, 160, 176, 0.7f, RGB(255, 255, 255), true, 0);
		text(&t_exit,  160, 208, 0.5f, RGB(255, 190, 120), true, 0);
	}
	if (g->state == ST_PAUSED) {
		text(&t_resume, 160, 100, 0.7f, RGB(255, 255, 255), true, 0);
		text(&t_quit,   160, 130, 0.7f, RGB(255, 255, 255), true, 0);
	}
}
