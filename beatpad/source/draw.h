// BeatPad - small citro2d drawing helpers shared by the renderer
#pragma once
#ifndef HOST_PREVIEW
#include <citro2d.h>
#endif
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "game.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

extern C2D_TextBuf g_tbuf;     // cleared once per frame by render_begin

// ---- small helpers -------------------------------------------------------
static inline uint32_t rgba(uint32_t rgb, int a)
{
	if (a < 0) a = 0;
	if (a > 255) a = 255;
	return C2D_Color32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (uint8_t)a);
}
static inline uint32_t mixrgb(uint32_t a, uint32_t b, float t)
{
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	int r = (int)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
	int g = (int)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
	int bl = (int)((a & 255) * (1 - t) + (b & 255) * t);
	return (uint32_t)(r << 16 | g << 8 | bl);
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c)
{
	C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0);
}
static inline void quad4(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
                  float x2, float y2, uint32_t c2, float x3, float y3, uint32_t c3)   // 0-1 top edge, 3-2 bottom edge
{
	C2D_DrawTriangle(x0, y0, c0, x1, y1, c1, x2, y2, c2, 0);
	C2D_DrawTriangle(x0, y0, c0, x2, y2, c2, x3, y3, c3, 0);
}
static inline void rect(float x, float y, float w, float h, uint32_t c) { C2D_DrawRectSolid(x, y, 0, w, h, c); }
static inline void vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bot)
{
	C2D_DrawRectangle(x, y, 0, w, h, top, top, bot, bot);
}
static inline void ellipse(float cx, float cy, float rx, float ry, uint32_t c, int seg)
{
	float px = cx + rx, py = cy;
	for (int i = 1; i <= seg; i++) {
		float a = 6.2831853f * i / seg, x = cx + cosf(a) * rx, y = cy + sinf(a) * ry;
		tri(cx, cy, px, py, x, y, c);
		px = x; py = y;
	}
}
static inline void ring(float cx, float cy, float rx, float ry, float th, uint32_t c, int seg)
{
	for (int i = 0; i < seg; i++) {
		float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
		float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		quad4(cx + c0 * rx, cy + s0 * ry, c, cx + c1 * rx, cy + s1 * ry, c,
		      cx + c1 * (rx - th), cy + s1 * (ry - th), c, cx + c0 * (rx - th), cy + s0 * (ry - th), c);
	}
}

enum { AL_LEFT, AL_CENTER, AL_RIGHT };
static inline void text(float x, float y, float size, uint32_t color, int align, const char *fmt, ...)
{
	char buf[128];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
	C2D_Text t;
	C2D_TextParse(&t, g_tbuf, buf);
	C2D_TextOptimize(&t);
	u32 flags = C2D_WithColor | (align == AL_CENTER ? C2D_AlignCenter : align == AL_RIGHT ? C2D_AlignRight : 0);
	C2D_DrawText(&t, flags, x, y, 0, size, size, color);
}
// text with a 1px dark shadow, for readability over the highway
static inline void stext(float x, float y, float size, uint32_t color, int align, const char *s)
{
	uint8_t a = (uint8_t)(color >> 24);
	text(x + 1.5f, y + 1.5f, size, C2D_Color32(0, 0, 0, a * 3 / 4), align, "%s", s);
	text(x, y, size, color, align, "%s", s);
}


static inline void draw_button_label(int lane, float x, float y, uint32_t c)
{
	float r = 6.5f;
	switch (lane) {
	case LANE_DLEFT:  tri(x - r, y, x + r * 0.7f, y - r, x + r * 0.7f, y + r, c); break;
	case LANE_DUP:    tri(x, y - r, x - r, y + r * 0.7f, x + r, y + r * 0.7f, c); break;
	case LANE_DRIGHT: tri(x + r, y, x - r * 0.7f, y - r, x - r * 0.7f, y + r, c); break;
	default: {
		static const char *L[] = { "", "", "", "Y", "X", "A" };
		ring(x, y, 9, 9, 1.5f, c, 16);
		text(x, y - 8.5f, 0.5f, c, AL_CENTER, "%s", L[lane]);
	}
	}
}

