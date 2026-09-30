#include "hud.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define COL_TEXT   0xFFFFFFFF
#define COL_SHADOW 0x000000B0

static void text(float x, float y, float sc, unsigned col, int align, const char *s)
{
	gfx2d_text(x + 1.0f, y + 1.0f, sc, COL_SHADOW, align, s);
	gfx2d_text(x, y, sc, col, align, s);
}

static unsigned hp_color(float f)
{
	return f > 0.5f ? 0x3CD05AFF : f > 0.25f ? 0xF0C030FF : 0xE03C32FF;
}

// A slanted (parallelogram) bar built from 2px strips. `lean` is how far the top edge is shifted; the fill grows from
// the outer edge (from_right = 0 -> left screen edge). frac is the visible part 0..1.
static void slant_bar(float x, float y, float w, float h, float lean, float frac, unsigned col, int from_right)
{
	frac = clampf(frac, 0.0f, 1.0f);
	float fw = w * frac;
	int strips = (int)(h / 2.0f);
	for (int i = 0; i < strips; i++) {
		float t = strips > 1 ? (float)i / (float)(strips - 1) : 0.0f;
		float off = lean * (1.0f - t) * (from_right ? -1.0f : 1.0f);
		float sx = from_right ? x + w - fw + off : x + off;
		gfx2d_rect(sx, y + i * 2.0f, fw, 2.0f, col);
	}
}

static void filled_circle(float cx, float cy, float r, unsigned col)
{
	for (int i = -(int)r; i < (int)r; i += 2) {
		float dy = (float)i + 1.0f, half = sqrtf(fmaxf(r * r - dy * dy, 0.0f));
		gfx2d_rect(cx - half, cy + (float)i, half * 2.0f, 2.0f, col);
	}
}

// Plain rectangular bar (bottom screen).
static void bar(float x, float y, float w, float h, float frac, unsigned fill, int from_right)
{
	frac = clampf(frac, 0.0f, 1.0f);
	gfx2d_rect(x - 2, y - 2, w + 4, h + 4, 0x000000C8);
	gfx2d_rect(x, y, w, h, 0x2A2C34FF);
	float fw = w * frac;
	gfx2d_rect(from_right ? x + w - fw : x, y, fw, h, fill);
	gfx2d_rect(from_right ? x + w - fw : x, y, fw, h * 0.4f, 0xFFFFFF40);
}

static void health_bar(const Game *g, int side)
{
	const Fighter *f = &g->f[side];
	int right = side == 1;
	const float w = 152, h = 14, y = 12, lean = 6;
	float x = right ? 400 - 16 - w : 16;
	float frac = f->hp / 100.0f, trail = g->hp_trail[side] / 100.0f;
	slant_bar(x - 3, y - 3, w + 6, h + 6, lean, 1.0f, 0xF0C860FF, right);                 // gold frame
	slant_bar(x - 1, y - 1, w + 2, h + 2, lean, 1.0f, 0x08080CFF, right);
	slant_bar(x, y, w, h, lean, 1.0f, 0x2A1E30FF, right);
	if (trail > frac) slant_bar(x, y, w, h, lean, trail, 0xFF8A2AFF, right);              // the damage that was just taken
	unsigned col = hp_color(frac);
	if (frac < 0.25f && (int)(g->time * 6.0f) % 2 == 0) col = 0xFF7060FF;                  // low-health pulse
	slant_bar(x, y, w, h, lean, frac, col, right);
	slant_bar(x, y, w, h * 0.42f, lean, frac, 0xFFFFFF48, right);                          // gloss
	// stamina: ten small blocks
	for (int i = 0; i < 10; i++) {
		int lit = f->stamina >= (float)(i + 1) * 10.0f - 5.0f;
		float bx = right ? 400 - 20 - (i + 1) * 11.0f : 20 + i * 11.0f;
		gfx2d_rect(bx, 33, 9, 5, lit ? 0x52C8FFFF : 0x1C2430C0);
	}
}

void hud_top(const Game *g)
{
	char buf[48];

	if (g->flash > 0.01f) gfx2d_rect(0, 0, 400, 240, 0xFFFFFF00u | (unsigned)(g->flash * 90.0f));

	health_bar(g, 0);
	health_bar(g, 1);
	text(24, 40, 0.40f, COL_TEXT, GFX_ALIGN_LEFT, "YOU");
	text(376, 40, 0.40f, COL_TEXT, GFX_ALIGN_RIGHT, "RIVAL");

	// round clock in a badge
	filled_circle(200, 24, 24, 0xF0C860FF);
	filled_circle(200, 24, 21, 0x100818FF);
	filled_circle(200, 24, 19, 0x3A1A78FF);
	snprintf(buf, sizeof buf, "%d", (int)ceilf(g->round_time));
	text(200, 7, 0.78f, 0xFFF0B0FF, GFX_ALIGN_CENTER, buf);

	// what just landed
	if (g->hit_text_t > 0 && g->hit_text_kind >= 0) {
		char name[32];
		const char *src = g->hit_text_block ? "blocked" : ATTACKS[g->hit_text_kind].name;
		size_t i = 0;
		for (; src[i] && i < sizeof name - 1; i++) name[i] = (char)toupper((unsigned char)src[i]);
		name[i] = 0;
		float side = g->last_hit_side == 1 ? 300.0f : 100.0f;
		text(side, 200, 0.7f, g->hit_text_block ? 0x9CC8FFFF : 0xFFB030FF, GFX_ALIGN_CENTER, name);
	}
	if (g->combo >= 2) {
		snprintf(buf, sizeof buf, "%d HIT COMBO", g->combo);
		text(14, 52, 0.55f, 0xFFB030FF, GFX_ALIGN_LEFT, buf);
	}

	// banners
	if (g->phase == PH_INTRO) {
		text(200, 78, 1.1f, COL_TEXT, GFX_ALIGN_CENTER, g->phase_t < 50 ? "GET READY" : "FIGHT!");
	} else if (g->phase == PH_KO) {
		text(200, 66, 2.0f, 0xFF4030FF, GFX_ALIGN_CENTER, "K.O.!");
		if (g->phase_t > 90) text(200, 150, 0.7f, COL_TEXT, GFX_ALIGN_CENTER, g->winner == 0 ? "YOU WIN!" : "YOU LOSE");
		if (g->phase_t > 150) text(200, 190, 0.5f, 0xC8C8D0FF, GFX_ALIGN_CENTER, "Press START for a rematch");
	} else if (g->phase == PH_TIMEUP) {
		text(200, 66, 1.4f, 0xFFE070FF, GFX_ALIGN_CENTER, "TIME!");
		if (g->phase_t > 60) text(200, 130, 0.8f, COL_TEXT, GFX_ALIGN_CENTER, g->winner == 0 ? "YOU WIN ON POINTS" : g->winner == 1 ? "RIVAL WINS ON POINTS" : "DRAW");
		if (g->phase_t > 120) text(200, 190, 0.5f, 0xC8C8D0FF, GFX_ALIGN_CENTER, "Press START for a rematch");
	}
}

typedef struct { const char *key, *desc; unsigned mask, need_mod; } Row;   // need_mod: 1 = L must be held, 2 = L must not be held

void hud_bottom(const Game *g, const FighterInput *in, int paused)
{
	const Fighter *p = &g->f[0];
	unsigned h = in->held;
	int mod = (h & BTN_MOD) != 0;

	gfx2d_rect(0, 0, 320, 240, 0x14161CFF);
	gfx2d_rect(0, 0, 320, 26, 0x22242CFF);
	text(160, 3, 0.6f, 0xFFE070FF, GFX_ALIGN_CENTER, "CONTROLS");

	static const Row rows[] = {
		{ "Y",      "Lead punch", BTN_LEAD_PUNCH, 2 },
		{ "X",      "Rear punch", BTN_REAR_PUNCH, 2 },
		{ "B",      "Lead kick",  BTN_LEAD_KICK,  0 },
		{ "A",      "Rear kick",  BTN_REAR_KICK,  0 },
		{ "L+Y",    "Lead hook",  BTN_LEAD_PUNCH, 1 },
		{ "L+X",    "Rear hook",  BTN_REAR_PUNCH, 1 },
		{ "R",      "Block head", BTN_GUARD,      2 },
		{ "L+R",    "Block body", BTN_GUARD,      1 },
		{ "D-PAD",  "Move head",  BTN_DP_LEFT | BTN_DP_RIGHT | BTN_DP_UP | BTN_DP_DOWN, 0 },
		{ "CIRCLE", "Move",       0, 0 },
	};
	for (int i = 0; i < 10; i++) {
		const Row *r = &rows[i];
		float x = i < 5 ? 8.0f : 166.0f, y = 34.0f + (i % 5) * 27.0f;
		int on = (h & r->mask) && (r->need_mod == 0 || (r->need_mod == 1 ? mod : !mod));
		if (i == 9) on = fabsf(in->mx) + fabsf(in->mz) > 0.2f;
		gfx2d_rect(x, y, 146, 23, on ? 0x3A4A6EFF : 0x1E2028FF);
		gfx2d_rect(x, y, 52, 23, on ? 0xFFC030FF : 0x3A3E4CFF);
		gfx2d_text(x + 26, y + 3, 0.42f, on ? 0x101010FF : 0xE8E8F0FF, GFX_ALIGN_CENTER, r->key);
		gfx2d_text(x + 58, y + 3, 0.42f, on ? 0xFFFFFFFF : 0xB8BCC8FF, GFX_ALIGN_LEFT, r->desc);
	}

	// status of the player's fighter
	gfx2d_text(8, 176, 0.42f, 0xB8BCC8FF, GFX_ALIGN_LEFT, "HEALTH");
	bar(70, 180, 240, 9, p->hp / 100.0f, hp_color(p->hp / 100.0f), 0);
	gfx2d_text(8, 196, 0.42f, 0xB8BCC8FF, GFX_ALIGN_LEFT, "STAMINA");
	bar(70, 200, 240, 9, p->stamina / 100.0f, 0x4AA8FFFF, 0);
	const char *state = paused ? "PAUSED   START: resume   SELECT: quit" :
	                    p->guard == 2 ? "Guarding body" : p->guard == 1 ? "Guarding head" : "START: pause";
	gfx2d_text(160, 219, 0.42f, paused ? 0xFFE070FF : 0x8A8E9CFF, GFX_ALIGN_CENTER, state);
}
