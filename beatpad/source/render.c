// BeatPad - drawing: the note highway (top screen) and the HUD / menus (bottom screen), all with citro2d shapes
#ifndef HOST_PREVIEW
#include <citro2d.h>
#endif
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "draw.h"
#include "net.h"

// highway projection: s = 1 / (1 + KZ * z), z = 0 at the strike line, 1 at the far end
#define YH   (-12.0f)          // vanishing point
#define YS   204.0f            // strike line
#define KZ   3.0f
#define ZNEAR (-0.12f)

C2D_TextBuf g_tbuf;
static float g_par;            // stereo: horizontal shift at the far end for this eye (px)
static float g_laneW;
static int   g_nl, g_vis[NUM_LANES];   // visible lane count, lane -> visual column (-1 hidden)
// excitement: set once per frame in render_top
static float g_pump;           // highway kick from the last hit (1 -> 0)
static float g_beat;           // flash on every beat of the song (1 -> 0 over 250 ms)
static float g_streak;         // 0 below a 10 combo, 1 from 30 up: how hard the beat flashes hit
static float g_fever;          // x4 multiplier, eased

// copies s, cut to n characters with "..." (the system font is proportional; n is a rough fit)
static const char *fit(char *out, size_t sz, const char *s, int n)
{
	if ((int)strlen(s) <= n) snprintf(out, sz, "%s", s);
	else snprintf(out, sz, "%.*s...", n - 3, s);
	return out;
}

static void fmt_time(char *out, size_t n, int ms)
{
	if (ms < 0) ms = 0;
	snprintf(out, n, "%d:%02d", ms / 60000, (ms / 1000) % 60);
}

void render_init(void) { g_tbuf = C2D_TextBufNew(6000); }
void render_exit(void) { C2D_TextBufDelete(g_tbuf); }
void render_begin(const App *a) { (void)a; C2D_TextBufClear(g_tbuf); }

float render_lookahead_ms(const App *a) { return 2600.0f / a->speed; }

// ---- highway geometry ------------------------------------------------------
static void setup_lanes(uint8_t mask)
{
	g_nl = 0;
	for (int l = 0; l < NUM_LANES; l++) g_vis[l] = (mask & (1 << l)) ? g_nl++ : -1;
	if (g_nl == 0) { for (int l = 0; l < NUM_LANES; l++) g_vis[l] = l; g_nl = NUM_LANES; }
	g_laneW = g_nl <= 4 ? 64.0f : 52.0f;
}
static inline float proj_s(float z) { return 1.0f / (1.0f + KZ * z); }
static inline float proj_y(float s) { return YH + (YS - YH) * s * (1.0f + 0.012f * g_pump); }       // hits kick the highway at you
static inline float proj_x(float off, float s) { return TOP_W / 2 + off * s * (1.0f + 0.03f * g_pump) + g_par * (1.0f - s); }
static inline float col_off(float col) { return (col - (g_nl - 1) * 0.5f) * g_laneW; }   // column centre at the strike line
static inline float half_w(void) { return g_nl * g_laneW * 0.5f + 6; }

// time (ms) -> z
static float g_now, g_look;
static inline float zof(float t) { return (t - g_now) / g_look; }

static uint32_t mult_color(int m)
{
	static const uint32_t c[5] = { 0, 0x7fa8ff, 0x50e07a, 0xffd23a, 0xd070ff };
	return c[m < 1 ? 1 : m > 4 ? 4 : m];
}

static void draw_background(const App *a, float beatPulse, float downPulse)
{
	static const uint32_t FEVER_BG[3] = { 0x3a0d52, 0x0d2a55, 0x521035 };
	int fb = a->play.song ? (int)(a->play.t / (60000.0f / a->play.song->bpm)) % 3 : 0;   // changes every beat
	if (fb < 0) fb = 0;
	uint32_t top = mixrgb(0x120a2e, FEVER_BG[fb], g_fever * (0.45f + 0.35f * g_beat));
	vgrad(0, 0, TOP_W, SCR_H, rgba(top, 255), rgba(mixrgb(0x05030f, top, 0.3f * g_fever), 255));
	// glow at the vanishing point, pulsing on the beat
	float gy = proj_y(proj_s(1.0f));
	for (int i = 0; i < 4; i++) {
		float r = 60 + i * 40 + beatPulse * 18;
		ellipse(TOP_W / 2 + g_par, gy, r * 1.8f, r * 0.55f, rgba(0x6a3cff, (int)(26 + beatPulse * 18) - i * 5), 24);
	}
	// stage light beams swinging slowly, flashing on downbeats
	int beams = g_fever > 0.3f ? 8 : 4;
	for (int i = 0; i < beams; i++) {
		float bx = beams == 8 ? 20 + i * 51 : 40 + i * 107, sw = sinf(a->time * (0.6f + g_fever) + i * 1.7f) * 70;
		uint32_t c = beams == 8 ? LANE_RGB[i % NUM_LANES] : i & 1 ? 0xff4fd8 : 0x47c8ff;
		int al = (int)(16 + downPulse * 40 + g_beat * g_streak * 30 + g_fever * g_beat * 50);
		C2D_DrawTriangle(bx, -5, rgba(c, al), bx + sw - 40, 250, rgba(c, 0), bx + sw + 40, 250, rgba(c, 0), 0);
	}
}

static void draw_highway(const App *a, const Play *p)
{
	float hw = half_w();
	int m = p ? play_multiplier(p) : 1;
	// body in strips so it fades out into the distance
	const int STR = 10;
	for (int i = 0; i < STR; i++) {
		float z0 = ZNEAR + (1.0f - ZNEAR) * i / STR, z1 = ZNEAR + (1.0f - ZNEAR) * (i + 1) / STR;
		float s0 = proj_s(z0), s1 = proj_s(z1);
		int a0 = (int)(235 * clampf((1.0f - z0) * 3.0f, 0, 1)), a1 = (int)(235 * clampf((1.0f - z1) * 3.0f, 0, 1));
		uint32_t body = mixrgb(0x161427, 0x24164a, g_fever * (0.5f + 0.5f * g_beat));
		uint32_t c0 = rgba(body, a0), c1 = rgba(body, a1);
		quad4(proj_x(-hw, s1), proj_y(s1), c1, proj_x(hw, s1), proj_y(s1), c1,
		      proj_x(hw, s0), proj_y(s0), c0, proj_x(-hw, s0), proj_y(s0), c0);
	}
	// lane tint while a lane button is held
	if (p) for (int l = 0; l < NUM_LANES; l++) {
		if (g_vis[l] < 0 || !(p->heldMask & (1 << l))) continue;
		float c = col_off((float)g_vis[l]), w = g_laneW * 0.5f;
		float sN = proj_s(ZNEAR), sF = proj_s(0.55f);
		uint32_t cn = rgba(LANE_RGB[l], 38), cf = rgba(LANE_RGB[l], 0);
		quad4(proj_x(c - w, sF), proj_y(sF), cf, proj_x(c + w, sF), proj_y(sF), cf,
		      proj_x(c + w, sN), proj_y(sN), cn, proj_x(c - w, sN), proj_y(sN), cn);
	}
	// lane separators and the side rails (rails take the multiplier colour)
	float sN = proj_s(ZNEAR), sF = proj_s(1.0f);
	for (int i = 1; i < g_nl; i++) {
		float o = col_off(i - 0.5f);
		C2D_DrawTriangle(proj_x(o - 0.6f, sN), proj_y(sN), rgba(0x5a5a8c, 150), proj_x(o + 0.6f, sN), proj_y(sN), rgba(0x5a5a8c, 150),
		                 proj_x(o, sF), proj_y(sF), rgba(0x5a5a8c, 0), 0);
	}
	uint32_t rc = mult_color(m);
	float glow = (m == 4 ? 0.6f + 0.4f * sinf(a->time * 8) : 0.5f) + (p ? clampf(p->multPopT, 0, 1) : 0) + g_beat * g_streak;
	for (int side = -1; side <= 1; side += 2) {
		float o = side * hw, w = 3.5f + 2.0f * g_beat * g_streak;
		const int SEG = 6;                                      // in fever the rails run through the lane colours
		for (int k = 0; k < SEG; k++) {
			float z0 = ZNEAR + (1 - ZNEAR) * k / SEG, z1 = ZNEAR + (1 - ZNEAR) * (k + 1) / SEG;
			float s0 = proj_s(z0), s1 = proj_s(z1);
			uint32_t c = mixrgb(rc, LANE_RGB[(k + (int)(a->time * 8)) % NUM_LANES], g_fever);
			int a0 = (int)((160 + 95 * glow) * (1 - (float)k / SEG)), a1 = (int)((160 + 95 * glow) * (1 - (float)(k + 1) / SEG));
			quad4(proj_x(o - w, s1), proj_y(s1), rgba(c, a1), proj_x(o + w, s1), proj_y(s1), rgba(c, a1),
			      proj_x(o + w, s0), proj_y(s0), rgba(c, a0), proj_x(o - w, s0), proj_y(s0), rgba(c, a0));
		}
	}
	// fever: sparks streaming down the rails toward you
	if (g_fever > 0.05f)
		for (int k = 0; k < 20; k++) {
			float ph = fmodf(a->time * 0.9f + k * 0.618f, 1.0f), z = 1 - ph * (1 - ZNEAR);
			float sc = proj_s(z), side = (k & 1) ? 1 : -1;
			float x = proj_x(side * (hw + 7), sc), y = proj_y(sc), r = 1.2f + 2.2f * sc;
			rect(x - r, y - r, r * 2, r * 2, rgba(LANE_RGB[k % NUM_LANES], (int)(230 * g_fever * clampf((1 - z) * 3, 0, 1))));
		}
	// beat lines
	const Song *s = a->play.song ? a->play.song : (a->nsongs ? &a->songs[a->sel] : NULL);
	if (s) for (int i = 0; i < s->nbeats; i++) {
		float z = zof((float)s->beats[i]);
		if (z < ZNEAR) continue;
		if (z > 1.0f) break;
		float sc = proj_s(z), y = proj_y(sc);
		int al = (int)((s->downbeat[i] ? 150 : 60) * (1 + 0.6f * g_streak) * clampf((1.0f - z) * 3.0f, 0, 1));
		float th = (s->downbeat[i] ? 2.2f : 1.2f) * sc;
		quad4(proj_x(-hw, sc), y - th, rgba(0x9a8cff, al), proj_x(hw, sc), y - th, rgba(0x9a8cff, al),
		      proj_x(hw, sc), y + th, rgba(0x9a8cff, al), proj_x(-hw, sc), y + th, rgba(0x9a8cff, al));
	}
}

static void gem(float x, float y, float rx, uint32_t rgb, int alpha, bool star)
{
	float ry = rx * 0.45f;
	ellipse(x, y + ry * 0.35f, rx + 1.5f, ry + 1.5f, rgba(0x000000, alpha * 2 / 3), 14);      // shadow / rim
	ellipse(x, y, rx, ry, rgba(mixrgb(rgb, 0x000000, 0.25f), alpha), 14);
	ellipse(x, y - ry * 0.18f, rx * 0.84f, ry * 0.72f, rgba(rgb, alpha), 14);
	ellipse(x - rx * 0.12f, y - ry * 0.38f, rx * 0.45f, ry * 0.25f, rgba(mixrgb(rgb, 0xffffff, 0.65f), alpha), 10);
	ellipse(x, y - ry * 0.1f, rx * 0.2f, ry * 0.2f, rgba(0xffffff, star ? alpha : alpha / 2), 8);
}

static void draw_targets(const Play *p)
{
	float s = proj_s(0), y = proj_y(s);
	float hw = half_w();
	// the strike line flashes on every beat once you're on a streak
	float bf = g_beat * g_streak;
	uint32_t lc = mixrgb(0xffffff, LANE_RGB[(int)(g_now / 250) % NUM_LANES], g_fever * 0.6f);
	float th = 2 + 2.5f * bf;
	if (bf > 0.02f) quad4(proj_x(-hw, s), y - th * 3, rgba(lc, 0), proj_x(hw, s), y - th * 3, rgba(lc, 0),
	                      proj_x(hw, s), y, rgba(lc, (int)(110 * bf)), proj_x(-hw, s), y, rgba(lc, (int)(110 * bf)));
	quad4(proj_x(-hw, s), y - th, rgba(lc, (int)(90 + 165 * bf)), proj_x(hw, s), y - th, rgba(lc, (int)(90 + 165 * bf)),
	      proj_x(hw, s), y + th, rgba(lc, (int)(90 + 165 * bf)), proj_x(-hw, s), y + th, rgba(lc, (int)(90 + 165 * bf)));
	for (int l = 0; l < NUM_LANES; l++) {
		if (g_vis[l] < 0) continue;
		float x = proj_x(col_off((float)g_vis[l]), s), rx = g_laneW * 0.40f * (1 + 0.06f * bf);
		bool held = p && (p->heldMask & (1 << l));
		float flash = p ? p->laneFlash[l] : 0;
		if (flash > 0) ellipse(x, y, rx * (1.2f + 0.5f * flash), rx * 0.55f * (1.2f + 0.5f * flash), rgba(LANE_RGB[l], (int)(120 * flash)), 18);
		ellipse(x, y, rx, rx * 0.45f, rgba(0x0b0a16, 230), 16);
		ring(x, y, rx, rx * 0.45f, 3.0f, rgba(LANE_RGB[l], held ? 255 : 170), 18);
		if (held) ellipse(x, y, rx * 0.72f, rx * 0.3f, rgba(mixrgb(LANE_RGB[l], 0xffffff, 0.2f), 200), 14);
		draw_button_label(l, x, 224, held ? rgba(0xffffff, 255) : rgba(mixrgb(LANE_RGB[l], 0xffffff, 0.35f), 220));
	}
}

static void draw_notes(const Play *p)
{
	const Chart *c = &p->chart;
	float tmax = g_now + g_look, tmin = g_now - g_look * 0.15f;
	// first visible note: notes are time-sorted; holds may start long before now
	int lo = 0, hi = c->n;
	while (lo < hi) { int mid = (lo + hi) / 2; if (c->notes[mid].t < tmin - 8000) lo = mid + 1; else hi = mid; }
	int last = lo;
	while (last < c->n && c->notes[last].t <= tmax) last++;

	// tails first, so heads draw on top
	for (int i = lo; i < last; i++) {
		const Note *n = &c->notes[i];
		if (n->len <= 0 || g_vis[n->lane] < 0) continue;
		float t0 = (float)n->t, t1 = (float)(n->t + n->len);
		if (n->state == NS_HOLDING || n->state == NS_DONE) t0 = fmaxf(t0, g_now);
		if (n->state == NS_DONE || t1 < tmin) continue;
		float z0 = clampf(zof(t0), ZNEAR, 1), z1 = clampf(zof(t1), ZNEAR, 1);
		if (z1 <= z0) continue;
		float s0 = proj_s(z0), s1 = proj_s(z1), o = col_off((float)g_vis[n->lane]);
		bool dead = n->state == NS_MISSED || n->state == NS_DROPPED;
		bool live = n->state == NS_HOLDING;
		uint32_t rgb = dead ? 0x55556a : live ? mixrgb(LANE_RGB[n->lane], 0xffffff, 0.3f + 0.2f * sinf(g_now * 0.03f)) : LANE_RGB[n->lane];
		float w0 = g_laneW * (live ? 0.20f : 0.16f) * s0, w1 = g_laneW * 0.16f * s1;
		int a0 = 230, a1 = (int)(230 * clampf((1 - z1) * 3, 0, 1));
		float wob = live ? sinf(g_now * 0.05f) * 1.5f : 0;
		quad4(proj_x(o, s1) - w1, proj_y(s1), rgba(rgb, a1), proj_x(o, s1) + w1, proj_y(s1), rgba(rgb, a1),
		      proj_x(o, s0) + w0 + wob, proj_y(s0), rgba(rgb, a0), proj_x(o, s0) - w0 + wob, proj_y(s0), rgba(rgb, a0));
	}
	// heads, far to near
	for (int i = last - 1; i >= lo; i--) {
		const Note *n = &c->notes[i];
		if (g_vis[n->lane] < 0) continue;
		if (n->state == NS_HIT || n->state == NS_DONE || n->state == NS_DROPPED) continue;
		float z = zof((float)n->t);
		if (n->state == NS_HOLDING) z = 0;                      // head sits on the strike line while held
		if (z < ZNEAR || z > 1) continue;
		float s = proj_s(z);
		int al = (int)(255 * clampf((1 - z) * 4, 0, 1));
		bool missed = n->state == NS_MISSED;
		gem(proj_x(col_off((float)g_vis[n->lane]), s), proj_y(s), g_laneW * 0.38f * s,
		    missed ? 0x4a4a5a : LANE_RGB[n->lane], missed ? al / 2 : al, n->len > 0);
	}
}

// what a good hit leaves behind: a beam of light up the lane and a shockwave ring from the target
static void draw_hits(const Play *p)
{
	float s0 = proj_s(0), y0 = proj_y(s0);
	for (int l = 0; l < NUM_LANES; l++) {
		float b = p->beam[l];
		if (b <= 0 || g_vis[l] < 0) continue;
		float o = col_off((float)g_vis[l]), w = g_laneW * 0.42f * (0.6f + 0.4f * b);
		float sF = proj_s(0.75f * b + 0.1f);
		uint32_t c = mixrgb(LANE_RGB[l], 0xffffff, 0.35f);
		quad4(proj_x(o - w * 0.3f, sF), proj_y(sF), rgba(c, 0), proj_x(o + w * 0.3f, sF), proj_y(sF), rgba(c, 0),
		      proj_x(o + w, s0), y0, rgba(c, (int)(170 * b)), proj_x(o - w, s0), y0, rgba(c, (int)(170 * b)));
		quad4(proj_x(o - w * 0.1f, sF), proj_y(sF), rgba(0xffffff, 0), proj_x(o + w * 0.1f, sF), proj_y(sF), rgba(0xffffff, 0),
		      proj_x(o + w * 0.3f, s0), y0, rgba(0xffffff, (int)(200 * b)), proj_x(o - w * 0.3f, s0), y0, rgba(0xffffff, (int)(200 * b)));
	}
	for (int i = 0; i < MAX_RINGS; i++) {
		const HitRing *r = &p->rings[i];
		if (r->t <= 0 || r->t >= 1 || g_vis[r->lane] < 0) continue;
		float k = r->t, e = 1 - (1 - k) * (1 - k);              // fast out, slow finish
		float x = proj_x(col_off((float)g_vis[r->lane]), s0), rx = g_laneW * 0.4f * (1 + 1.7f * e);
		uint32_t c = r->perfect ? mixrgb(LANE_RGB[r->lane], 0xffffff, 0.5f) : LANE_RGB[r->lane];
		ring(x, y0, rx, rx * 0.45f, 3.5f * (1 - k) + 1, rgba(c, (int)(255 * (1 - k))), 22);
		if (r->perfect) ring(x, y0, rx * 0.7f, rx * 0.31f, 2, rgba(0xffffff, (int)(200 * (1 - k))), 18);
	}
}

static void draw_sparks(const Play *p)
{
	float s = proj_s(0), y = proj_y(s);
	for (int i = 0; i < MAX_SPARKS; i++) {
		const Spark *sp = &p->sparks[i];
		if (sp->life <= 0 || g_vis[sp->lane] < 0) continue;
		float x = proj_x(col_off((float)g_vis[sp->lane]) + sp->x, s);
		float yy = y - sp->y * 0.25f, k = sp->life / sp->max, r = 1.0f + 1.6f * k;
		rect(x - r, yy - r, r * 2, r * 2, rgba(sp->color, (int)(255 * k)));
	}
}

static void draw_play_hud(const App *a, const Play *p)
{
	char buf[48];
	// judgement popup
	if (p->popT > 0 && p->popJudge != J_NONE) {
		static const char *J[] = { "PERFECT", "GREAT", "GOOD", "MISS" };
		static const uint32_t JC[] = { 0x8ff7ff, 0x6cf08a, 0xffd84a, 0xff5050 };
		float k = p->popT / 0.55f, sc = 0.85f + 0.25f * fmaxf(0, k - 0.75f) * 4;
		stext(TOP_W / 2, 112 - (1 - k) * 6, sc, rgba(JC[p->popJudge], (int)(255 * clampf(k * 2.5f, 0, 1))), AL_CENTER, J[p->popJudge]);
	}
	if (p->combo >= 5) {
		snprintf(buf, sizeof buf, "%d", p->combo);
		float pop = clampf(p->comboPopT, 0, 1);
		stext(TOP_W / 2, 140, 0.7f + pop * 0.3f, rgba(mixrgb(0xffffff, 0xffd23a, pop), 230), AL_CENTER, buf);
	}
	if (p->feverPopT > 0) {
		float k = clampf(p->feverPopT / 1.4f, 0, 1), sc = 1.3f + 0.6f * fmaxf(0, k - 0.8f) * 5;
		const char *F = "FEVER!";
		for (int i = 0; F[i]; i++) {                              // each letter in a lane colour, bouncing
			char c[2] = { F[i], 0 };
			float x = TOP_W / 2 - 70 + i * 28, y = 40 + sinf(a->time * 14 + i) * 3;
			stext(x, y, sc, rgba(mixrgb(LANE_RGB[(i + (int)(a->time * 10)) % NUM_LANES], 0xffffff, 0.2f), (int)(255 * clampf(k * 3, 0, 1))), AL_CENTER, c);
		}
	} else if (p->fever > 0.5f)
		stext(36, 172, 0.42f, rgba(mixrgb(0xd070ff, 0xffffff, g_beat), 255), AL_CENTER, "FEVER");
	if (p->comboPopT > 0) {
		snprintf(buf, sizeof buf, "%d NOTE STREAK!", p->combo - p->combo % 50);
		stext(TOP_W / 2, 64, 0.75f, rgba(0xffd23a, (int)(255 * clampf(p->comboPopT * 2, 0, 1))), AL_CENTER, buf);
	}
	// multiplier badge
	int m = play_multiplier(p);
	const float bx = 36, by = 140;
	float mp = clampf(p->multPopT / 0.6f, 0, 1), r = 19 + mp * 5;
	ellipse(bx, by, r, r, rgba(0x0b0a16, 220), 20);
	ring(bx, by, r, r, 3, rgba(mult_color(m), 255), 20);
	int prog = m >= 4 ? 10 : p->combo % 10;            // ticks toward the next multiplier
	for (int i = 0; i < 10; i++) {
		float an = -1.5708f + 6.2831853f * i / 10;
		uint32_t c = i < prog ? rgba(mult_color(m), 255) : rgba(0x3a3a55, 255);
		rect(bx + cosf(an) * (r + 6) - 1.5f, by + sinf(an) * (r + 6) - 1.5f, 3, 3, c);
	}
	snprintf(buf, sizeof buf, "x%d", m);
	text(bx, by - 14, 0.75f, rgba(0xffffff, 255), AL_CENTER, "%s", buf);
	// rock meter: vertical bar on the right
	float hx = 372, hy = 70, hh = 108;
	rect(hx - 2, hy - 2, 14, hh + 4, rgba(0x000000, 200));
	vgrad(hx, hy, 10, hh * 0.33f, rgba(0x50e07a, 255), rgba(0xffd23a, 255));
	vgrad(hx, hy + hh * 0.33f, 10, hh * 0.34f, rgba(0xffd23a, 255), rgba(0xff8a2a, 255));
	vgrad(hx, hy + hh * 0.67f, 10, hh * 0.33f, rgba(0xff8a2a, 255), rgba(0xff3c3c, 255));
	float ny = hy + hh * (1 - p->health);
	tri(hx - 7, ny - 5, hx - 7, ny + 5, hx - 1, ny, rgba(0xffffff, 255));
	rect(hx - 1, ny - 1, 12, 2, rgba(0xffffff, 255));
	// score and progress
	snprintf(buf, sizeof buf, "%d", (int)a->displayScore);
	stext(394, 4, 0.6f, rgba(0xffffff, 240), AL_RIGHT, buf);
	const Song *s = p->song;
	if (s && s->length_ms > 0) {
		float f = clampf(p->t / s->length_ms, 0, 1);
		rect(0, 0, TOP_W, 2, rgba(0x2a2a44, 255));
		rect(0, 0, TOP_W * f, 2, rgba(0x9a8cff, 255));
	}
	if (p->missT > 0) {
		int al = (int)(110 * p->missT / 0.35f);
		C2D_DrawRectangle(0, 0, 0, 40, SCR_H, rgba(0xff2020, al), rgba(0xff2020, 0), rgba(0xff2020, al), rgba(0xff2020, 0));
		C2D_DrawRectangle(TOP_W - 40, 0, 0, 40, SCR_H, rgba(0xff2020, 0), rgba(0xff2020, al), rgba(0xff2020, 0), rgba(0xff2020, al));
	}
	// lead-in count
	if (p->t < 0 && !a->demo) {
		int n = (int)ceilf(-p->t / 1000.0f * 1.5f);
		if (n > 0 && n <= 3) { snprintf(buf, sizeof buf, "%d", n); stext(TOP_W / 2, 80, 1.4f, rgba(0xffffff, 230), AL_CENTER, buf); }
		else stext(TOP_W / 2, 90, 0.9f, rgba(0xffffff, 230), AL_CENTER, "GET READY");
	}
}

static void dim(int al) { rect(0, 0, TOP_W, SCR_H, rgba(0x05030f, al)); }

static void panel(float x, float y, float w, float h, uint32_t edge)
{
	rect(x, y, w, h, rgba(0x0e0b22, 225));
	rect(x, y, w, 2, rgba(edge, 255));
	rect(x, y + h - 1, w, 1, rgba(edge, 120));
}

static void draw_logo(float x, float y, float t)
{
	const char *L = "BEATPAD";
	for (int i = 0; L[i]; i++) {
		char c[2] = { L[i], 0 };
		float bob = sinf(t * 4 + i * 0.7f) * 2;
		uint32_t col = LANE_RGB[i % NUM_LANES];
		text(x - 84 + i * 24 + 2, y + bob + 2, 1.05f, rgba(0x000000, 200), AL_CENTER, "%s", c);
		text(x - 84 + i * 24, y + bob, 1.05f, rgba(mixrgb(col, 0xffffff, 0.25f), 255), AL_CENTER, "%s", c);
	}
}

void render_top(const App *a, float eye, float slider)
{
	g_par = eye * slider * 7.0f;
	const Play *p = &a->play;
	bool showPlay = a->screen == SCR_PLAY || a->screen == SCR_PAUSE || a->screen == SCR_RESULTS || a->demo;
	setup_lanes(showPlay && p->chart.n ? p->chart.laneMask : diff_lanes(a->diff));
	g_now = p->t; g_look = render_lookahead_ms(a);

	// beat pulse from the song's beat grid
	float beatPulse = 0, downPulse = 0;
	const Song *s = p->song;
	if (s && s->nbeats) {
		for (int i = 0; i < s->nbeats; i++) {
			float d = g_now - s->beats[i];
			if (d < 0) break;
			if (d < 400) { beatPulse = fmaxf(beatPulse, 1 - d / 400); if (s->downbeat[i]) downPulse = 1 - d / 400; }
		}
	}
	float bp = 0;                                               // sharper beat flash (250 ms)
	if (s) for (int i = 0; i < s->nbeats; i++) {
		float d = g_now - s->beats[i];
		if (d < 0) break;
		if (d < 250) bp = (1 - d / 250) * (1 - d / 250);
	}
	bool live = showPlay && s;
	g_beat = live ? bp : 0;
	g_streak = live ? clampf((p->combo - 10) / 20.0f, 0, 1) : 0;
	g_fever = live ? p->fever : 0;
	g_pump = live ? p->pump : 0;

	draw_background(a, beatPulse, downPulse);
	if (a->screen == SCR_NOSONGS) {
		draw_logo(TOP_W / 2, 60, a->time);
		stext(TOP_W / 2, 130, 0.6f, rgba(0xffffff, 255), AL_CENTER, "No songs yet: press Y to get some");
		return;
	}
	if (a->screen == SCR_GETSONGS) {
		draw_highway(a, NULL);
		draw_targets(NULL);
		dim(150);
		draw_logo(TOP_W / 2, 6, a->time);
		panel(30, 44, 340, 150, 0x3fd96a);
		text(TOP_W / 2, 48, 0.8f, rgba(0xffffff, 255), AL_CENTER, "GET SONGS");
		int st = net_state();
		char buf[96];
		if (st == NET_FAILED) {
			text(TOP_W / 2, 92, 0.5f, rgba(0xff8a6a, 255), AL_CENTER, "Wi-Fi isn't available.");
			text(TOP_W / 2, 112, 0.45f, rgba(0xd0c8f0, 255), AL_CENTER, "Turn wireless on, then come back here.");
		} else if (st == NET_SEARCHING) {
			int dots = (int)(a->time * 3) % 4;
			text(TOP_W / 2, 84, 0.55f, rgba(0xffffff, 255), AL_CENTER, "Looking for your PC%.*s", dots, "...");
			text(TOP_W / 2, 112, 0.45f, rgba(0xd0c8f0, 255), AL_CENTER, "On your PC, in the beatpad folder, run:");
			text(TOP_W / 2, 132, 0.5f, rgba(0x8ff7ff, 255), AL_CENTER, "./serve-songs.sh");
			if (net_searching_for() > 6)
				text(TOP_W / 2, 162, 0.42f, rgba(0xffd23a, 255), AL_CENTER, "Not finding it? Press Y and type the address it prints.");
		} else if (st == NET_CONNECTED) {
			text(TOP_W / 2, 80, 0.5f, rgba(0x6cf08a, 255), AL_CENTER, "Connected to %s", fit(buf, sizeof buf, net_server_name(), 34));
			int nn = 0, nu = 0;
			for (int i = 0; i < a->ngs; i++) { nn += a->gs[i].status == GS_NEW; nu += a->gs[i].status == GS_UPDATE; }
			text(TOP_W / 2, 102, 0.45f, rgba(0xd0c8f0, 255), AL_CENTER, "%d on your PC  -  %d new  -  %d to update", net_song_count(), nn, nu);
			if (net_busy()) {
				rect(60, 136, 280, 12, rgba(0x2a2448, 255));
				rect(60, 136, 280 * clampf(net_progress(), 0, 1), 12, rgba(0x3fd96a, 255));
				text(TOP_W / 2, 152, 0.42f, rgba(0xffffff, 255), AL_CENTER, "%s", fit(buf, sizeof buf, net_status(), 50));
				if (a->gsGetAll) text(TOP_W / 2, 170, 0.4f, rgba(0xffd23a, 255), AL_CENTER, "Getting everything new  (B: stop)");
			} else if (net_status()[0]) text(TOP_W / 2, 140, 0.45f, rgba(0xffffff, 255), AL_CENTER, "%s", fit(buf, sizeof buf, net_status(), 50));
		}
		return;
	}
	draw_highway(a, showPlay ? p : NULL);
	draw_targets(showPlay ? p : NULL);
	if (showPlay && p->chart.n) { draw_notes(p); draw_hits(p); draw_sparks(p); }

	if (a->screen == SCR_PLAY) {
		draw_play_hud(a, p);
		if (p->flashT > 0) rect(0, 0, TOP_W, SCR_H, rgba(0xffffff, (int)(140 * p->flashT)));
	}
	else if (a->screen == SCR_PAUSE) {
		draw_play_hud(a, p);
		dim(170);
		stext(TOP_W / 2, 50, 1.2f, rgba(0xffffff, 255), AL_CENTER, "PAUSED");
		static const char *items[] = { "Resume", "Restart", "Quit to menu" };
		for (int i = 0; i < 3; i++) {
			bool on = a->menuSel == i;
			if (on) rect(TOP_W / 2 - 90, 104 + i * 32, 180, 28, rgba(0x6a3cff, 200));
			text(TOP_W / 2, 106 + i * 32, 0.7f, rgba(on ? 0xffffff : 0xb0a8d8, 255), AL_CENTER, "%s", items[i]);
		}
	} else if (a->screen == SCR_RESULTS) {
		dim(190);
		const char *g = play_grade(p);
		uint32_t gc = p->failed ? 0xff5050 : g[0] == 'S' ? 0xffd23a : g[0] == 'A' ? 0x6cf08a : g[0] == 'B' ? 0x7fa8ff : 0xd0c8f0;
		float k = clampf(a->screenT * 3, 0, 1);
		for (int i = 3; i > 0; i--) ellipse(TOP_W / 2, 92, 50 + i * 10 * k, 50 + i * 10 * k, rgba(gc, 18), 28);
		stext(TOP_W / 2, 50 - (1 - k) * 20, 2.6f, rgba(gc, (int)(255 * k)), AL_CENTER, g);
		const char *banner = p->failed ? "SONG FAILED" : p->fullCombo ? "FULL COMBO!" : "SONG CLEAR";
		stext(TOP_W / 2, 14, 0.8f, rgba(p->failed ? 0xff5050 : 0xffffff, 255), AL_CENTER, banner);
		char buf[64];
		snprintf(buf, sizeof buf, "%d", (int)a->displayScore);
		stext(TOP_W / 2, 150, 1.0f, rgba(0xffffff, 255), AL_CENTER, buf);
		snprintf(buf, sizeof buf, "%.2f%%  accuracy", play_accuracy(p));
		stext(TOP_W / 2, 184, 0.6f, rgba(0xd0c8f0, 255), AL_CENTER, buf);
		if (a->newBest && fmodf(a->time, 0.8f) < 0.55f) stext(TOP_W / 2, 206, 0.65f, rgba(0xffd23a, 255), AL_CENTER, "NEW BEST!");
	} else {
		// song select / settings: logo + song card over the demo highway
		dim(70);
		draw_logo(TOP_W / 2, 6, a->time);
		const Song *cs = &a->songs[a->sel];
		panel(70, 42, 260, 70, LANE_RGB[(a->sel * 2) % NUM_LANES]);
		text(TOP_W / 2, 46, 0.85f, rgba(0xffffff, 255), AL_CENTER, "%s", cs->title);
		char len[16]; fmt_time(len, sizeof len, cs->length_ms);
		if (cs->artist[0]) text(TOP_W / 2, 74, 0.5f, rgba(0xb0a8d8, 255), AL_CENTER, "%s  -  %.0f BPM  -  %s", cs->artist, cs->bpm, len);
		else text(TOP_W / 2, 74, 0.5f, rgba(0xb0a8d8, 255), AL_CENTER, "%.0f BPM  -  %s", cs->bpm, len);
		for (int d = 0; d < NUM_DIFFS; d++) {
			bool on = a->diff == d;
			float x = 108 + d * 92;
			static const uint32_t DC[] = { 0x50e07a, 0xffd23a, 0xff5050 };
			rect(x - 40, 92, 80, 16, rgba(on ? DC[d] : 0x2a2448, on ? 255 : 230));
			text(x, 92, 0.45f, rgba(on ? 0x0e0b22 : 0xb0a8d8, 255), AL_CENTER, "%s", diff_name(d));
		}
	}
}

// ---- bottom screen ------------------------------------------------------------
static void bottom_bg(const App *a)
{
	vgrad(0, 0, BOT_W, SCR_H, rgba(0x15102e, 255), rgba(0x07051a, 255));
	for (int i = 0; i < 8; i++) {
		float x = fmodf(i * 53 + a->time * 9, BOT_W + 40) - 20;
		rect(x, 0, 1, SCR_H, rgba(0x2a2050, 120));
	}
}

static void lane_legend(const App *a, float y)
{
	uint8_t mask = diff_lanes(a->diff);
	int n = 0; for (int l = 0; l < NUM_LANES; l++) if (mask & (1 << l)) n++;
	const float w = 44, gap = 20;                       // gap between the D-pad hand and the face-button hand
	float x0 = BOT_W / 2 - ((n - 1) * w + gap) / 2;
	int k = 0;
	for (int l = 0; l < NUM_LANES; l++) {
		if (!(mask & (1 << l))) continue;
		float x = x0 + k * w + (l >= LANE_Y ? gap : 0);
		gem(x, y, 15, LANE_RGB[l], 255, false);
		draw_button_label(l, x, y + 22, rgba(0xffffff, 255));
		k++;
	}
}

static void draw_stat_row(float y, const char *label, const char *val, uint32_t c)
{
	text(24, y, 0.5f, rgba(0xb0a8d8, 255), AL_LEFT, "%s", label);
	text(296, y, 0.5f, rgba(c, 255), AL_RIGHT, "%s", val);
}

void render_bottom(const App *a)
{
	bottom_bg(a);
	const Play *p = &a->play;
	char buf[64];

	if (a->screen == SCR_NOSONGS) {
		text(BOT_W / 2, 90, 0.5f, rgba(0xffffff, 255), AL_CENTER, "Y: Get Songs from your PC over Wi-Fi");
		text(BOT_W / 2, 200, 0.5f, rgba(0xb0a8d8, 255), AL_CENTER, "START: quit");
		return;
	}

	if (a->screen == SCR_GETSONGS) {
		static const char *TAG[] = { "NEW", "UPDATE", "ON 3DS", "ON 3DS" };
		static const uint32_t TAGC[] = { 0x3fd96a, 0xffd23a, 0x8a82b0, 0x8a82b0 };
		char t1[64];
		if (!a->ngs) {
			text(BOT_W / 2, 90, 0.45f, rgba(0xb0a8d8, 255), AL_CENTER, net_state() == NET_CONNECTED ?
			     "Your PC has no songs yet:\n./add-song.sh song.mp3" : "Waiting for your PC...");
		}
		for (int r = 0; r < 6 && a->gsScroll + r < a->ngs; r++) {
			int i = a->gsScroll + r;
			const GsItem *it = &a->gs[i];
			float y = 6 + r * 30;
			bool on = i == a->menuSel;
			rect(8, y, 304, 27, rgba(on ? 0x6a3cff : 0x15102e, on ? 200 : 220));
			const char *title = it->remote >= 0 ? net_song(it->remote)->title : a->songs[it->local].title;
			const char *artist = it->remote >= 0 ? net_song(it->remote)->artist : a->songs[it->local].artist;
			text(16, y + 1, 0.5f, rgba(0xffffff, 255), AL_LEFT, "%s", fit(t1, sizeof t1, title, 26));
			if (artist[0]) text(16, y + 15, 0.36f, rgba(0xb0a8d8, 255), AL_LEFT, "%s", fit(t1, sizeof t1, artist, 34));
			if (it->remote >= 0) text(236, y + 15, 0.36f, rgba(0x8a82b0, 255), AL_RIGHT, "%.1f MB", net_song(it->remote)->bytes / 1048576.0);
			bool confirm = a->gsConfirmDelete == i;
			rect(242, y + 6, 64, 15, rgba(confirm ? 0xff5050 : TAGC[it->status], 255));
			text(274, y + 6, 0.38f, rgba(0x0e0b22, 255), AL_CENTER, "%s", confirm ? "DELETE?" : TAG[it->status]);
		}
		if (a->ngs > 6) {                                  // scrollbar
			float h = 180.0f * 6 / a->ngs, y = 6 + 180.0f * a->gsScroll / a->ngs;
			rect(314, y, 3, h, rgba(0x9a8cff, 200));
		}
		const GsItem *it = a->ngs ? &a->gs[a->menuSel] : NULL;
		const char *aLabel = it && it->status == GS_NEW ? "A: download" : it && it->status == GS_UPDATE ? "A: update" :
		                     it && it->status == GS_INSTALLED ? "A: download again" : "";
		text(BOT_W / 2, 192, 0.42f, rgba(0xffffff, 255), AL_CENTER, "%s%sX: get all new", aLabel, aLabel[0] ? "    " : "");
		text(BOT_W / 2, 208, 0.42f, rgba(0xffffff, 255), AL_CENTER, it && it->local >= 0 ? "SELECT twice: delete from 3DS    Y: PC address" : "Y: type PC address");
		text(BOT_W / 2, 224, 0.42f, rgba(0xb0a8d8, 255), AL_CENTER, net_busy() ? "B: cancel download" : "B: back");
		return;
	}

	if (a->screen == SCR_SELECT || a->screen == SCR_SETTINGS) {
		const Song *s = &a->songs[a->sel];
		const Best *b = &a->best[a->sel][a->diff];
		if (a->screen == SCR_SELECT) {
			// song list
			char t1[64];
			const int rows = 5;
			int first = a->sel - rows / 2;
			if (first > a->nsongs - rows) first = a->nsongs - rows;
			if (first < 0) first = 0;
			for (int r = 0; r < rows && first + r < a->nsongs; r++) {
				int i = first + r;
				const Song *si = &a->songs[i];
				float y = 4 + r * 23;
				bool on = i == a->sel;
				rect(8, y, 304, 21, rgba(on ? 0x6a3cff : 0x15102e, on ? 210 : 200));
				text(16, y + 2, 0.48f, rgba(on ? 0xffffff : 0xd0c8f0, 255), AL_LEFT, "%s", fit(t1, sizeof t1, si->title, 24));
				const Best *bi = &a->best[i][a->diff];
				if (bi->played) text(304, y + 3, 0.42f, rgba(bi->grade[0] == 'S' ? 0xffd23a : 0xffffff, 255), AL_RIGHT, "%s", bi->grade);
				else if (si->artist[0]) text(304, y + 5, 0.36f, rgba(0x8a82b0, 255), AL_RIGHT, "%s", fit(t1, sizeof t1, si->artist, 18));
			}
			if (a->nsongs > rows) {
				float h = 113.0f * rows / a->nsongs, y = 4 + 113.0f * first / a->nsongs;
				rect(314, y, 3, h, rgba(0x9a8cff, 200));
			}
			if (b->played)
				text(BOT_W / 2, 122, 0.45f, rgba(0xffffff, 255), AL_CENTER, "%s - %d notes - best %d (%s, %.1f%%)%s", diff_name(a->diff),
				     s->noteCount[a->diff], b->score, b->grade, b->acc, b->fc ? " FC" : "");
			else text(BOT_W / 2, 122, 0.45f, rgba(0xb0a8d8, 255), AL_CENTER, "%s - %d notes - not played yet", diff_name(a->diff), s->noteCount[a->diff]);
			lane_legend(a, 150);
			text(BOT_W / 2, 190, 0.42f, rgba(0xffffff, 255), AL_CENTER, "Up/Down: song    Left/Right: difficulty    A: play");
			text(BOT_W / 2, 206, 0.42f, rgba(0xffffff, 255), AL_CENTER, "Y: get songs    SELECT: settings    START: quit");
			if (!a->audioOk) text(BOT_W / 2, 224, 0.38f, rgba(0xff8a6a, 255), AL_CENTER, "No sound: dump the DSP firmware (sdmc:/3ds/dspfirm.cdc)");
		} else {
			panel(10, 8, 300, 26, 0x6a3cff);
			text(BOT_W / 2, 11, 0.55f, rgba(0xffffff, 255), AL_CENTER, "SETTINGS");
			const char *names[SET_COUNT] = { "Note speed", "Audio offset", "No-fail", "Back" };
			for (int i = 0; i < SET_COUNT; i++) {
				float y = 46 + i * 30;
				bool on = a->menuSel == i;
				if (on) rect(14, y - 2, 292, 26, rgba(0x6a3cff, 170));
				text(24, y, 0.55f, rgba(on ? 0xffffff : 0xd0c8f0, 255), AL_LEFT, "%s", names[i]);
				if (i == SET_SPEED) snprintf(buf, sizeof buf, "< %.2f >", a->speed);
				else if (i == SET_OFFSET) snprintf(buf, sizeof buf, "< %+d ms >", a->offset);
				else if (i == SET_NOFAIL) snprintf(buf, sizeof buf, "< %s >", a->noFail ? "On" : "Off");
				else buf[0] = 0;
				text(296, y, 0.55f, rgba(0xffffff, 255), AL_RIGHT, "%s", buf);
			}
			const char *help[SET_COUNT] = {
				"How fast notes scroll (the preview above follows it)",
				"Raise it if you hit late, lower it if you hit early.\nResults show your average timing.",
				"On: the rock meter can't fail you",
				"" };
			text(BOT_W / 2, 172, 0.42f, rgba(0xb0a8d8, 255), AL_CENTER, "%s", help[a->menuSel]);
			text(BOT_W / 2, 216, 0.45f, rgba(0xffffff, 255), AL_CENTER, "Left/Right: change    B: back");
		}
		return;
	}

	// play / pause / results: stats panel
	const Song *s = p->song;
	panel(8, 6, 304, 46, LANE_RGB[p->diff * 2]);
	text(16, 9, 0.55f, rgba(0xffffff, 255), AL_LEFT, "%s", s ? s->title : "");
	if (s && s->artist[0]) text(16, 30, 0.42f, rgba(0xb0a8d8, 255), AL_LEFT, "%s  -  %s", s->artist, diff_name(p->diff));
	else text(16, 30, 0.42f, rgba(0xb0a8d8, 255), AL_LEFT, "%s", diff_name(p->diff));
	char tm[16], tl[16];
	fmt_time(tm, sizeof tm, s ? (int)fminf(p->t, (float)s->length_ms) : 0); fmt_time(tl, sizeof tl, s ? s->length_ms : 0);
	text(304, 30, 0.42f, rgba(0xb0a8d8, 255), AL_RIGHT, "%s / %s", tm, tl);

	snprintf(buf, sizeof buf, "%d", (int)a->displayScore);
	text(BOT_W / 2, 58 - 4 * p->scorePop, 1.2f + 0.14f * p->scorePop, rgba(mixrgb(0xffffff, mult_color(play_multiplier(p)), p->scorePop * 0.6f), 255), AL_CENTER, "%s", buf);
	int m = play_multiplier(p);
	if (a->screen == SCR_RESULTS) {
		const char *g = play_grade(p);
		text(BOT_W / 2, 94, 0.5f, rgba(p->failed ? 0xff5050 : 0xffd23a, 255), AL_CENTER, p->failed ? "Failed" : p->fullCombo ? "Grade %s  -  Full combo!" : "Grade %s", g);
	} else text(BOT_W / 2, 94, 0.5f, rgba(mult_color(m), 255), AL_CENTER, "x%d multiplier   -   combo %d", m, p->combo);

	float y = 112;
	snprintf(buf, sizeof buf, "%d", p->counts[J_PERFECT]); draw_stat_row(y, "Perfect", buf, 0x8ff7ff); y += 15;
	snprintf(buf, sizeof buf, "%d", p->counts[J_GREAT]);   draw_stat_row(y, "Great", buf, 0x6cf08a); y += 15;
	snprintf(buf, sizeof buf, "%d", p->counts[J_GOOD]);    draw_stat_row(y, "Good", buf, 0xffd84a); y += 15;
	snprintf(buf, sizeof buf, "%d", p->counts[J_MISS]);    draw_stat_row(y, "Miss", buf, 0xff5050); y += 15;
	snprintf(buf, sizeof buf, "%.2f%%", play_accuracy(p)); draw_stat_row(y, "Accuracy", buf, 0xffffff); y += 15;
	snprintf(buf, sizeof buf, "%d", p->maxCombo);          draw_stat_row(y, "Best combo", buf, 0xffffff); y += 15;

	if (a->screen == SCR_RESULTS) {
		if (p->hitN > 0) {
			int avg = p->hitSum / p->hitN;
			snprintf(buf, sizeof buf, avg > 3 ? "%d ms late" : avg < -3 ? "%d ms early" : "on time", avg < 0 ? -avg : avg);
		} else snprintf(buf, sizeof buf, "-");
		draw_stat_row(y, "Avg. timing", buf, 0xffffff);
		text(BOT_W / 2, 220, 0.45f, rgba(0xffffff, 255), AL_CENTER, "A: continue      X: retry");
	} else {
		// progress bar
		float f = s && s->length_ms ? clampf(p->t / s->length_ms, 0, 1) : 0;
		rect(16, 228, 288, 5, rgba(0x2a2448, 255));
		rect(16, 228, 288 * f, 5, rgba(0x9a8cff, 255));
		text(BOT_W / 2, 206, 0.4f, rgba(0x8a82b0, 255), AL_CENTER, "START: pause");
	}
}
