// PC stand-in for the few citro2d calls the renderer uses: a tiny software rasteriser that lets us
// look at frames without a 3DS. Only used for development previews (-DHOST_PREVIEW).
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef uint32_t u32;
typedef uint8_t  u8;

#define C2D_AtBaseline 1
#define C2D_WithColor  2
#define C2D_AlignCenter (2 << 2)

static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) { return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24); }

// ---- framebuffer -------------------------------------------------------
static float shim_fb[240][400][3];
static int   shim_w = 400, shim_h = 240;
static FILE *shim_textlog;
static long shim_tris;          // triangles submitted since the last shim_begin (draw-cost estimate)

static inline void shim_begin(int w, int h, u32 clr)
{
	shim_w = w; shim_h = h; shim_tris = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			shim_fb[y][x][0] = clr & 255; shim_fb[y][x][1] = (clr >> 8) & 255; shim_fb[y][x][2] = (clr >> 16) & 255;
		}
}

static inline void shim_write_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", shim_w, shim_h);
	for (int y = 0; y < shim_h; y++)
		for (int x = 0; x < shim_w; x++)
			for (int c = 0; c < 3; c++) fputc((int)(shim_fb[y][x][c] + 0.5f), f);
	fclose(f);
}

static inline void shim_px(int x, int y, float r, float g, float b, float a)
{
	if (x < 0 || y < 0 || x >= shim_w || y >= shim_h) return;
	float *p = shim_fb[y][x];
	p[0] += (r - p[0]) * a; p[1] += (g - p[1]) * a; p[2] += (b - p[2]) * a;
}

static inline float shim_min3(float a, float b, float c) { return fminf(a, fminf(b, c)); }
static inline float shim_max3(float a, float b, float c) { return fmaxf(a, fmaxf(b, c)); }

static inline bool C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1,
                                    float x2, float y2, u32 c2, float depth)
{
	(void)depth;
	shim_tris++;
	float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	if (fabsf(area) < 1e-6f) return true;
	int minx = (int)floorf(shim_min3(x0, x1, x2)), maxx = (int)ceilf(shim_max3(x0, x1, x2));
	int miny = (int)floorf(shim_min3(y0, y1, y2)), maxy = (int)ceilf(shim_max3(y0, y1, y2));
	if (minx < 0) minx = 0; if (miny < 0) miny = 0;
	if (maxx > shim_w - 1) maxx = shim_w - 1; if (maxy > shim_h - 1) maxy = shim_h - 1;
	for (int y = miny; y <= maxy; y++)
		for (int x = minx; x <= maxx; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
			float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			float r = w0 * (c0 & 255) + w1 * (c1 & 255) + w2 * (c2 & 255);
			float g = w0 * ((c0 >> 8) & 255) + w1 * ((c1 >> 8) & 255) + w2 * ((c2 >> 8) & 255);
			float b = w0 * ((c0 >> 16) & 255) + w1 * ((c1 >> 16) & 255) + w2 * ((c2 >> 16) & 255);
			float a = (w0 * (c0 >> 24) + w1 * (c1 >> 24) + w2 * (c2 >> 24)) / 255.0f;
			shim_px(x, y, r, g, b, a);
		}
	return true;
}

static inline bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 c0, u32 c1, u32 c2, u32 c3)
{
	(void)z;   // corner order: top-left, top-right, bottom-left, bottom-right
	C2D_DrawTriangle(x, y, c0, x + w, y, c1, x, y + h, c2, 0);
	C2D_DrawTriangle(x + w, y, c1, x + w, y + h, c3, x, y + h, c2, 0);
	return true;
}

static inline bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c)
{
	return C2D_DrawRectangle(x, y, z, w, h, c, c, c, c);
}

static inline bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float t, float depth)
{
	(void)c1; (void)depth;
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-4f) return true;
	float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
	C2D_DrawTriangle(x0 + nx, y0 + ny, c0, x0 - nx, y0 - ny, c0, x1 + nx, y1 + ny, c0, 0);
	C2D_DrawTriangle(x0 - nx, y0 - ny, c0, x1 - nx, y1 - ny, c0, x1 + nx, y1 + ny, c0, 0);
	return true;
}

// ---- text: logged, then drawn with a real font by preview.py ----------
typedef struct { char str[96]; } C2D_Text;
typedef void *C2D_TextBuf;
static inline C2D_TextBuf C2D_TextBufNew(size_t n) { (void)n; return (void *)1; }
static inline void C2D_TextBufDelete(C2D_TextBuf b) { (void)b; }
static inline void C2D_TextBufClear(C2D_TextBuf b) { (void)b; }
static inline const char *C2D_TextParse(C2D_Text *t, C2D_TextBuf b, const char *s)
{
	(void)b; snprintf(t->str, sizeof t->str, "%s", s); return s + strlen(s);
}
static inline void C2D_TextOptimize(const C2D_Text *t) { (void)t; }
static inline void C2D_TextGetDimensions(const C2D_Text *t, float sx, float sy, float *w, float *h)
{
	*w = strlen(t->str) * 15.5f * sx; *h = 30.0f * sy;
}
static inline void C2D_DrawText(const C2D_Text *t, u32 flags, float x, float y, float z, float sx, float sy, ...)
{
	(void)z;
	u32 col = 0xffffffffu;
	if (flags & C2D_WithColor) { va_list ap; va_start(ap, sy); col = va_arg(ap, u32); va_end(ap); }
	if (shim_textlog)
		fprintf(shim_textlog, "%d\t%.2f\t%.2f\t%.3f\t%.3f\t%u\t%s\n", (flags & C2D_AlignCenter) ? 1 : 0, x, y, sx, sy, col, t->str);
}

// ---- images (textured quads); loaded from raw RGBA files written by preview.py ----
static const char *shim_gfxdir = ".";          // where the raw sprite files live
typedef struct { int width, height; unsigned char *rgba; } C2D_Image;
typedef struct { u32 color; float blend; } C2D_ImageTint;
static inline void C2D_PlainImageTint(C2D_ImageTint *t, u32 color, float blend) { t->color = color; t->blend = blend; }

static inline C2D_Image shim_load_image(const char *path)
{
	C2D_Image img = {0, 0, NULL};
	FILE *f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "cannot open %s\n", path); return img; }
	int wh[2];
	if (fread(wh, sizeof wh, 1, f) != 1) { fclose(f); return img; }
	img.width = wh[0]; img.height = wh[1];
	img.rgba = malloc((size_t)wh[0] * wh[1] * 4);
	if (fread(img.rgba, 4, (size_t)wh[0] * wh[1], f) != (size_t)wh[0] * wh[1]) { free(img.rgba); img.rgba = NULL; }
	fclose(f);
	return img;
}

static inline bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint *tint, float sx, float sy)
{
	(void)depth;
	if (!img.rgba) return false;
	shim_tris += 2;
	float dw = img.width * sx, dh = img.height * sy;
	int x0 = (int)floorf(x), x1 = (int)ceilf(x + dw), y0 = (int)floorf(y), y1 = (int)ceilf(y + dh);
	for (int py = y0; py < y1; py++)
		for (int px = x0; px < x1; px++) {
			float u = (px + 0.5f - x) / dw * img.width - 0.5f, v = (py + 0.5f - y) / dh * img.height - 0.5f;
			int iu = (int)floorf(u), iv = (int)floorf(v);
			float fu = u - iu, fv = v - iv, c[4] = {0, 0, 0, 0}, wsum = 0;
			for (int dy = 0; dy < 2; dy++)                 // bilinear filter, like the GPU's linear filtering
				for (int dx = 0; dx < 2; dx++) {
					int tx = iu + dx, ty = iv + dy;
					if (tx < 0 || ty < 0 || tx >= img.width || ty >= img.height) continue;
					float wgt = (dx ? fu : 1 - fu) * (dy ? fv : 1 - fv);
					const unsigned char *p = img.rgba + ((size_t)ty * img.width + tx) * 4;
					for (int k = 0; k < 4; k++) c[k] += p[k] * wgt * (k < 3 ? p[3] / 255.0f : 1);   // premultiplied
					wsum += wgt;
				}
			if (wsum <= 0) continue;
			float a = c[3] / wsum / 255.0f;
			if (a <= 0.001f) continue;
			float r = c[0] / wsum / a, g = c[1] / wsum / a, b = c[2] / wsum / a;
			if (tint && tint->blend > 0) {
				float t = tint->blend;
				r += ((tint->color & 255) - r) * t; g += (((tint->color >> 8) & 255) - g) * t; b += (((tint->color >> 16) & 255) - b) * t;
			}
			shim_px(px, py, r, g, b, a);
		}
	return true;
}
