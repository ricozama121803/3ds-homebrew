// PS3 XMB-style launcher for 3DS homebrew and installed titles.
// Lists installed titles (via AM) and .3dsx files in sdmc:/3ds. Titles are started by
// chainloading, .3dsx files through Luma's hb:ldr service.
#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define TOP_W 400.0f
#define TOP_H 240.0f
#define BOT_W 320.0f
#define BOT_H 240.0f

#define MAX_ITEMS 128
#define ARGBUF_SIZE 0x400
#define SMDH_SIZE 0x36C0

typedef enum { CAT_SETTINGS, CAT_GAMES, CAT_APPS, CAT_COUNT } Category;
typedef enum { ACT_NONE, ACT_LAUNCH, ACT_TITLE, ACT_ABOUT, ACT_RESCAN, ACT_EXIT } Action;

static const char *CAT_NAMES[CAT_COUNT] = {"Settings", "Game", "Apps"};

typedef struct {
	char name[64];
	char desc[96];
	char path[256];
	Action act;
	u64 tid;
	u8 media;
	bool hasIcon;
	C3D_Tex tex;
	Tex3DS_SubTexture sub;
} Item;

static Item g_items[CAT_COUNT][MAX_ITEMS];
static int g_count[CAT_COUNT];
static int g_sel[CAT_COUNT];
static float g_itemPos[CAT_COUNT];

static C2D_TextBuf g_buf;
static char g_status[96];
static bool g_quit;
static bool g_haveAm;

// ---------------------------------------------------------------------------
// Colours

static u32 gold(float a)
{
	if (a < 0.0f) a = 0.0f;
	if (a > 1.0f) a = 1.0f;
	return C2D_Color32(255, 236, 196, (u8)(a * 255.0f));
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------------
// Text (with a soft dark shadow so it stays readable over the ribbons)

static void text(const char *s, float x, float y, float scale, float alpha, u32 flags)
{
	C2D_Text t;
	C2D_TextParse(&t, g_buf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | flags, x + 1.0f, y + 1.0f, 0.45f, scale, scale,
	             C2D_Color32(20, 10, 2, (u8)(clampf(alpha, 0.0f, 1.0f) * 150.0f)));
	C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, scale, scale, gold(alpha));
}

// ---------------------------------------------------------------------------
// Background: brown gradient with silky twisting ribbons

#define MAX_COLS 64

// A ribbon is a bundle of strands that share a centre line and twist around it,
// so they cross and bunch up like the PS3's wave.
static void draw_ribbon(float w, float t, float baseY, float amp, float wid, float alpha,
                        int strands, float seed, int step)
{
	static float cy[MAX_COLS], cw[MAX_COLS], ce[MAX_COLS], cp[MAX_COLS];
	int cols = (int)(w / step) + 1;
	if (cols > MAX_COLS) cols = MAX_COLS;

	for (int i = 0; i < cols; i++) {
		float x = (float)i * step;
		cy[i] = baseY + amp * sinf(x * 0.011f + t * 0.5f + seed) +
		        amp * 0.45f * sinf(x * 0.024f - t * 0.35f + seed * 2.0f);
		cw[i] = wid * (0.35f + 0.65f * (0.5f + 0.5f * sinf(x * 0.007f + t * 0.25f + seed)));
		ce[i] = powf(sinf(3.14159f * clampf(x / w, 0.0f, 1.0f)), 0.5f);
		cp[i] = x * 0.015f + t * 0.4f + seed;
	}

	for (int s = 0; s < strands; s++) {
		float u = ((float)s / (float)(strands - 1)) * 2.0f - 1.0f;
		float px = 0, py = 0;
		u32 pc = 0;
		for (int i = 0; i < cols; i++) {
			float x = (float)i * step;
			float y = cy[i] + cw[i] * sinf(cp[i] + u * 0.9f);
			u32 c = C2D_Color32(255, 232, 185, (u8)(alpha * ce[i]));
			if (i > 0) {
				const float ht = 1.1f;
				C2D_DrawTriangle(px, py - ht, pc, x, y - ht, c, px, py + ht, pc, 0.1f);
				C2D_DrawTriangle(x, y - ht, c, x, y + ht, c, px, py + ht, pc, 0.1f);
			}
			px = x;
			py = y;
			pc = c;
		}
	}
}

static void draw_sparkles(float w, float h, float t)
{
	for (int i = 0; i < 22; i++) {
		float x = fmodf(i * 47.3f + t * (3.0f + (i % 5)), w);
		float y = fmodf(i * 31.7f + h - t * (2.0f + (i % 3)) * 1.5f, h);
		float a = 0.35f + 0.35f * sinf(t * 1.7f + i * 1.3f);
		float s = (i % 3 == 0) ? 2.0f : 1.0f;
		C2D_DrawRectSolid(x, y, 0.15f, s, s, C2D_Color32(255, 236, 196, (u8)(a * 255.0f)));
	}
}

static void draw_background(float w, float h, float t, bool full, float dim)
{
	u32 top = C2D_Color32(122, 76, 24, 255);
	u32 mid = C2D_Color32(74, 44, 14, 255);
	u32 bot = C2D_Color32(26, 14, 5, 255);
	C2D_DrawRectangle(0, 0, 0.0f, w, h * 0.5f, top, top, mid, mid);
	C2D_DrawRectangle(0, h * 0.5f, 0.0f, w, h * 0.5f, mid, mid, bot, bot);

	if (full) {
		draw_ribbon(w, t, h * 0.575f, 16.0f, 26.0f, 44.0f, 14, 0.0f, 10);
		draw_ribbon(w, t, h * 0.63f, 10.0f, 14.0f, 36.0f, 8, 2.1f, 10);
	} else {
		draw_ribbon(w, t, h * 0.58f, 12.0f, 20.0f, 40.0f, 8, 0.7f, 10);
	}
	draw_sparkles(w, h, t);

	// Soft vignette.
	u32 dark = C2D_Color32(0, 0, 0, 110), clear = C2D_Color32(0, 0, 0, 0);
	C2D_DrawRectangle(0, 0, 0.2f, w, 36, dark, dark, clear, clear);
	C2D_DrawRectangle(0, h - 36, 0.2f, w, 36, clear, clear, dark, dark);
	if (dim > 0.0f) C2D_DrawRectSolid(0, 0, 0.2f, w, h, C2D_Color32(0, 0, 0, (u8)(dim * 255.0f)));
}

// ---------------------------------------------------------------------------
// Icons drawn from primitives

static void icon_settings(float cx, float cy, float s, u32 c)
{
	u32 dark = C2D_Color32(40, 24, 8, 160);
	C2D_DrawRectSolid(cx - 6 * s, cy - 12 * s, 0.4f, 12 * s, 6 * s, c);
	C2D_DrawRectSolid(cx - 4 * s, cy - 10 * s, 0.45f, 8 * s, 3 * s, dark);
	C2D_DrawRectSolid(cx - 14 * s, cy - 7 * s, 0.4f, 28 * s, 18 * s, c);
	C2D_DrawRectSolid(cx - 14 * s, cy - 1 * s, 0.45f, 28 * s, 2 * s, dark);
	C2D_DrawRectSolid(cx - 2 * s, cy - 3 * s, 0.45f, 4 * s, 6 * s, dark);
}

static void icon_games(float cx, float cy, float s, u32 c)
{
	u32 dark = C2D_Color32(40, 24, 8, 180);
	C2D_DrawRectSolid(cx - 9 * s, cy - 7 * s, 0.4f, 18 * s, 14 * s, c);
	C2D_DrawCircleSolid(cx - 10 * s, cy + 2 * s, 0.4f, 8 * s, c);
	C2D_DrawCircleSolid(cx + 10 * s, cy + 2 * s, 0.4f, 8 * s, c);
	C2D_DrawRectSolid(cx - 11 * s, cy - 1 * s, 0.45f, 6 * s, 2 * s, dark);
	C2D_DrawRectSolid(cx - 9 * s, cy - 3 * s, 0.45f, 2 * s, 6 * s, dark);
	C2D_DrawCircleSolid(cx + 8 * s, cy - 1 * s, 0.45f, 1.6f * s, dark);
	C2D_DrawCircleSolid(cx + 12 * s, cy + 3 * s, 0.45f, 1.6f * s, dark);
}

static void icon_apps(float cx, float cy, float s, u32 c)
{
	for (int i = 0; i < 4; i++) {
		float ox = (i & 1) ? 1.5f : -12.5f;
		float oy = (i & 2) ? 1.5f : -12.5f;
		C2D_DrawRectSolid(cx + ox * s, cy + oy * s, 0.4f, 11 * s, 11 * s, c);
	}
}

static void draw_category_icon(int cat, float cx, float cy, float s, float a)
{
	u32 c = gold(a);
	switch (cat) {
	case CAT_SETTINGS: icon_settings(cx, cy, s, c); break;
	case CAT_GAMES: icon_games(cx, cy, s, c); break;
	default: icon_apps(cx, cy, s, c); break;
	}
}

static void draw_item_icon(const Item *it, float cx, float cy, float size, float a)
{
	if (it->hasIcon) {
		C2D_Image img = {(C3D_Tex *)&it->tex, (const Tex3DS_SubTexture *)&it->sub};
		C2D_ImageTint tint;
		C2D_AlphaImageTint(&tint, clampf(a, 0.0f, 1.0f));
		float sc = size / 48.0f;
		C2D_DrawImageAt(img, cx - size / 2, cy - size / 2, 0.4f, &tint, sc, sc);
	} else {
		float h = size / 2;
		C2D_DrawRectSolid(cx - h, cy - h, 0.4f, size, size, gold(a * 0.9f));
		C2D_DrawRectSolid(cx - h + 3, cy - h + 3, 0.45f, size - 6, size - 6,
		                  C2D_Color32(60, 36, 10, (u8)(clampf(a, 0.0f, 1.0f) * 255.0f)));
	}
}

// ---------------------------------------------------------------------------
// SMDH (title + icon), shared by .3dsx files and installed titles

static u32 rd32(const u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static void utf16_to_ascii(const u8 *src, char *dst, size_t max)
{
	size_t i = 0;
	for (; i + 1 < max; i++) {
		u8 lo = src[i * 2], hi = src[i * 2 + 1];
		if (lo == 0 && hi == 0) break;
		dst[i] = hi ? '?' : (char)lo;
	}
	dst[i] = '\0';
}

static void apply_smdh(const u8 *smdh, Item *it, char *title, size_t tsz)
{
	// English title if present, otherwise the first language that has one.
	for (int i = -1; i < 16; i++) {
		int idx = i < 0 ? 1 : i;
		const u8 *entry = smdh + 8 + idx * 0x200;
		utf16_to_ascii(entry, title, tsz);
		if (title[0]) {
			utf16_to_ascii(entry + 0x80, it->desc, sizeof(it->desc));
			break;
		}
	}

	// The large icon is already in GPU tile order; drop its tiles into a 64x64 texture.
	if (C3D_TexInit(&it->tex, 64, 64, GPU_RGB565)) {
		memset(it->tex.data, 0, it->tex.size);
		const u8 *icon = smdh + 0x24C0;
		for (int ty = 0; ty < 6; ty++)
			memcpy((u8 *)it->tex.data + ty * 8 * 64 * 2, icon + ty * 6 * 128, 6 * 128);
		GSPGPU_FlushDataCache(it->tex.data, it->tex.size);
		C3D_TexSetFilter(&it->tex, GPU_LINEAR, GPU_LINEAR);
		it->sub.width = 48;
		it->sub.height = 48;
		it->sub.left = 0.0f;
		it->sub.top = 1.0f;
		it->sub.right = 48.0f / 64.0f;
		it->sub.bottom = 1.0f - 48.0f / 64.0f;
		it->hasIcon = true;
	}
}

static void load_smdh_3dsx(const char *path, Item *it, char *title, size_t tsz)
{
	title[0] = '\0';
	FILE *f = fopen(path, "rb");
	if (!f) return;

	u8 hdr[0x2C];
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || memcmp(hdr, "3DSX", 4) != 0 ||
	    (hdr[4] | (hdr[5] << 8)) < 0x2C) {
		fclose(f);
		return;
	}
	u32 off = rd32(hdr + 0x20), size = rd32(hdr + 0x24);
	if (off != 0 && size >= SMDH_SIZE) {
		u8 *smdh = malloc(SMDH_SIZE);
		if (smdh && fseek(f, off, SEEK_SET) == 0 && fread(smdh, 1, SMDH_SIZE, f) == SMDH_SIZE &&
		    memcmp(smdh, "SMDH", 4) == 0)
			apply_smdh(smdh, it, title, tsz);
		free(smdh);
	}
	fclose(f);
}

static bool load_smdh_title(u64 tid, u8 media, Item *it, char *title, size_t tsz)
{
	static const u32 filePath[] = {0, 0, 2, 0x6E6F6369, 0}; // ExeFS "icon"
	u32 archivePath[] = {(u32)(tid & 0xFFFFFFFF), (u32)(tid >> 32), media, 0};
	FS_Path apath = {PATH_BINARY, sizeof(archivePath), archivePath};
	FS_Path fpath = {PATH_BINARY, sizeof(filePath), filePath};

	title[0] = '\0';
	Handle file = 0;
	if (R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, apath, fpath,
	                                     FS_OPEN_READ, 0)))
		return false;

	bool ok = false;
	u8 *smdh = malloc(SMDH_SIZE);
	u32 got = 0;
	if (smdh && R_SUCCEEDED(FSFILE_Read(file, &got, 0, smdh, SMDH_SIZE)) && got == SMDH_SIZE &&
	    memcmp(smdh, "SMDH", 4) == 0) {
		apply_smdh(smdh, it, title, tsz);
		ok = true;
	}
	free(smdh);
	FSFILE_Close(file);
	return ok;
}

// ---------------------------------------------------------------------------
// Library scan

static bool is_3dsx(const char *n)
{
	size_t l = strlen(n);
	return l > 5 && strcasecmp(n + l - 5, ".3dsx") == 0;
}

static bool contains_ci(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	for (; *hay; hay++)
		if (strncasecmp(hay, needle, n) == 0) return true;
	return false;
}

static bool is_tool(const char *name)
{
	static const char *tools[] = {"fbi", "anemone", "universal", "3hs", "godmode", "checkpoint",
	                              "ftpd", "luma", "updater", "hbmenu", "boot", "manager", "tool"};
	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++)
		if (contains_ci(name, tools[i])) return true;
	return false;
}

static void store_item(Item *tmp, const char *title)
{
	int cat = is_tool(title) || is_tool(tmp->path) ? CAT_APPS : CAT_GAMES;
	if (g_count[cat] >= MAX_ITEMS) {
		if (tmp->hasIcon) C3D_TexDelete(&tmp->tex);
		return;
	}
	snprintf(tmp->name, sizeof(tmp->name), "%s", title);
	g_items[cat][g_count[cat]++] = *tmp;
}

static void add_3dsx(const char *path, const char *fallback)
{
	if (contains_ci(path, "xmb-launcher")) return;

	Item tmp;
	memset(&tmp, 0, sizeof(tmp));
	char title[64];
	load_smdh_3dsx(path, &tmp, title, sizeof(title));

	if (!title[0]) {
		snprintf(title, sizeof(title), "%s", fallback);
		char *dot = strrchr(title, '.');
		if (dot) *dot = '\0';
	}
	snprintf(tmp.path, sizeof(tmp.path), "%s", path);
	tmp.act = ACT_LAUNCH;
	store_item(&tmp, title);
}

static void scan_titles(void)
{
	for (u8 media = 0; media < 3; media++) {
		u32 n = 0, got = 0;
		if (R_FAILED(AM_GetTitleCount(media, &n)) || n == 0) continue;
		u64 *ids = malloc(n * sizeof(u64));
		if (!ids) continue;
		if (R_SUCCEEDED(AM_GetTitleList(&got, media, n, ids))) {
			for (u32 i = 0; i < got; i++) {
				if ((ids[i] >> 32) != 0x00040000) continue; // applications only
				Item tmp;
				memset(&tmp, 0, sizeof(tmp));
				char title[64];
				if (!load_smdh_title(ids[i], media, &tmp, title, sizeof(title))) continue;
				if (!title[0]) snprintf(title, sizeof(title), "%016llX", (unsigned long long)ids[i]);
				tmp.act = ACT_TITLE;
				tmp.tid = ids[i];
				tmp.media = media;
				store_item(&tmp, title);
			}
		}
		free(ids);
	}
}

static int cmp_items(const void *a, const void *b)
{
	return strcasecmp(((const Item *)a)->name, ((const Item *)b)->name);
}

static void add_static(int cat, const char *name, const char *desc, Action act)
{
	Item *it = &g_items[cat][g_count[cat]++];
	memset(it, 0, sizeof(*it));
	snprintf(it->name, sizeof(it->name), "%s", name);
	snprintf(it->desc, sizeof(it->desc), "%s", desc);
	it->act = act;
}

static void free_items(void)
{
	for (int c = 0; c < CAT_COUNT; c++) {
		for (int i = 0; i < g_count[c]; i++)
			if (g_items[c][i].hasIcon) C3D_TexDelete(&g_items[c][i].tex);
		g_count[c] = 0;
	}
}

static void scan_library(void)
{
	free_items();

	if (g_haveAm) scan_titles();

	DIR *d = opendir("sdmc:/3ds");
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			char path[320];
			snprintf(path, sizeof(path), "sdmc:/3ds/%s", e->d_name);
			if (e->d_type == DT_DIR) {
				DIR *sub = opendir(path);
				if (!sub) continue;
				struct dirent *se;
				while ((se = readdir(sub))) {
					if (se->d_name[0] == '.' || !is_3dsx(se->d_name)) continue;
					char sp[600];
					snprintf(sp, sizeof(sp), "%s/%s", path, se->d_name);
					if (strlen(sp) < 256) add_3dsx(sp, e->d_name);
				}
				closedir(sub);
			} else if (is_3dsx(e->d_name) && strlen(path) < 256) {
				add_3dsx(path, e->d_name);
			}
		}
		closedir(d);
	}

	for (int c = CAT_GAMES; c < CAT_COUNT; c++) {
		qsort(g_items[c], g_count[c], sizeof(Item), cmp_items);
		if (g_count[c] == 0)
			add_static(c, "Nothing here yet", "No titles or .3dsx files found", ACT_NONE);
	}

	add_static(CAT_SETTINGS, "Rescan", "Look for new titles and homebrew", ACT_RESCAN);
	add_static(CAT_SETTINGS, "About", "XMB Launcher by ricozama", ACT_ABOUT);
	add_static(CAT_SETTINGS, "Exit", "Return to the Home Menu", ACT_EXIT);

	for (int c = 0; c < CAT_COUNT; c++)
		if (g_sel[c] >= g_count[c]) g_sel[c] = g_count[c] - 1;
}

// ---------------------------------------------------------------------------
// Launching

// .3dsx: same calls the Homebrew Launcher makes to Luma's hb:ldr.
static bool launch_3dsx(const char *path)
{
	Handle hbldr;
	if (R_FAILED(svcConnectToPort(&hbldr, "hb:ldr"))) return false;

	const char *target = strncmp(path, "sdmc:/", 6) == 0 ? path + 5 : path;
	u32 argbuf[ARGBUF_SIZE / sizeof(u32)];
	memset(argbuf, 0, sizeof(argbuf));
	argbuf[0] = 1;
	snprintf((char *)&argbuf[1], sizeof(argbuf) - 4, "%s", path);

	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(2, 0, 2);
	cmdbuf[1] = IPC_Desc_StaticBuffer(strlen(target) + 1, 0);
	cmdbuf[2] = (u32)target;
	Result rc = svcSendSyncRequest(hbldr);
	if (R_SUCCEEDED(rc)) rc = cmdbuf[1];

	if (R_SUCCEEDED(rc)) {
		cmdbuf = getThreadCommandBuffer();
		cmdbuf[0] = IPC_MakeHeader(3, 0, 2);
		cmdbuf[1] = IPC_Desc_StaticBuffer(sizeof(argbuf), 1);
		cmdbuf[2] = (u32)argbuf;
		rc = svcSendSyncRequest(hbldr);
		if (R_SUCCEEDED(rc)) rc = cmdbuf[1];
	}
	svcCloseHandle(hbldr);
	return R_SUCCEEDED(rc);
}

static void activate(const Item *it)
{
	switch (it->act) {
	case ACT_LAUNCH:
		if (launch_3dsx(it->path))
			g_quit = true;
		else
			snprintf(g_status, sizeof(g_status), "Could not start it (hb:ldr unavailable)");
		break;
	case ACT_TITLE:
		aptSetChainloader(it->tid, it->media);
		g_quit = true;
		break;
	case ACT_RESCAN:
		scan_library();
		snprintf(g_status, sizeof(g_status), "%d games, %d apps", g_count[CAT_GAMES],
		         g_count[CAT_APPS]);
		break;
	case ACT_ABOUT:
		snprintf(g_status, sizeof(g_status), "XMB Launcher v0.2");
		break;
	case ACT_EXIT:
		g_quit = true;
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Drawing

static void draw_top(float t, int cat, float catPos, float fade)
{
	draw_background(TOP_W, TOP_H, t, true, 0.0f);

	// Clock / ticker box in the top-right corner.
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char clock[32];
	strftime(clock, sizeof(clock), "%m/%d %I:%M %p", tm);
	u32 line = gold(0.55f);
	C2D_DrawRectSolid(236, 6, 0.3f, 158, 1, line);
	C2D_DrawRectSolid(236, 28, 0.3f, 158, 1, line);
	C2D_DrawRectSolid(236, 6, 0.3f, 1, 23, line);
	text(clock, 388, 9, 0.55f, 1.0f, C2D_AlignRight);
	if (g_status[0]) text(g_status, 242, 31, 0.36f, 0.85f, 0);

	// Category row: selected category sits at x = 96, neighbours are 58 px apart.
	const float catY = 62.0f;
	for (int i = 0; i < CAT_COUNT; i++) {
		float x = 96.0f + (i - catPos) * 58.0f;
		float d = fabsf(i - catPos);
		float a = clampf(1.0f - d * 0.45f, 0.35f, 1.0f);
		draw_category_icon(i, x, catY, 1.0f + (d < 1.0f ? (1.0f - d) * 0.25f : 0.0f), a);
		if (d < 0.5f) text(CAT_NAMES[i], x, catY + 22, 0.45f, (0.5f - d) * 2.0f, C2D_AlignCenter);
	}

	// Item list under the selected category.
	float pos = g_itemPos[cat];
	for (int i = 0; i < g_count[cat]; i++) {
		float d = i - pos;
		float y = 132.0f + d * 30.0f + (d < -0.5f ? -10.0f : (d > 0.5f ? 10.0f : d * 20.0f));
		if (y < 80.0f || y > 236.0f) continue;
		float a = clampf(1.0f - fabsf(d) * 0.16f, 0.28f, 1.0f);
		a *= clampf((y - 88.0f) / 20.0f, 0.0f, 1.0f) * fade;
		float size = fabsf(d) < 0.5f ? 34.0f : 24.0f;
		draw_item_icon(&g_items[cat][i], 96.0f, y, size, a);
		text(g_items[cat][i].name, 124.0f, y - 9.0f, fabsf(d) < 0.5f ? 0.6f : 0.5f, a, 0);
	}
}

static void draw_bottom(float t, int cat)
{
	draw_background(BOT_W, BOT_H, t * 0.8f + 2.0f, false, 0.2f);

	const Item *it = &g_items[cat][g_sel[cat]];
	draw_item_icon(it, 160.0f, 64.0f, 48.0f, 1.0f);
	text(it->name, 160.0f, 96.0f, 0.65f, 1.0f, C2D_AlignCenter);
	if (it->desc[0]) text(it->desc, 160.0f, 122.0f, 0.42f, 0.8f, C2D_AlignCenter);
	if (it->path[0]) text(it->path + 5, 160.0f, 142.0f, 0.36f, 0.55f, C2D_AlignCenter);

	text("Left/Right: category    Up/Down: choose", 160.0f, 196.0f, 0.4f, 0.7f, C2D_AlignCenter);
	text("A: start    START: exit", 160.0f, 212.0f, 0.4f, 0.7f, C2D_AlignCenter);
}

// ---------------------------------------------------------------------------

int main(void)
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(8192);
	C2D_Prepare();
	C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	g_buf = C2D_TextBufNew(8192);

	g_haveAm = R_SUCCEEDED(amInit());
	if (!g_haveAm) snprintf(g_status, sizeof(g_status), "Installed titles unavailable");

	hidSetRepeatParameters(15, 5);
	scan_library();

	int cat = CAT_GAMES;
	float catPos = (float)cat;
	float fade = 1.0f;
	for (int c = 0; c < CAT_COUNT; c++) g_itemPos[c] = (float)g_sel[c];

	u32 clear = C2D_Color32(0, 0, 0, 255);
	while (aptMainLoop() && !g_quit) {
		hidScanInput();
		u32 down = hidKeysDown();
		u32 rep = hidKeysDownRepeat();

		if (down & KEY_START) break;
		if ((down & (KEY_DRIGHT | KEY_R)) && cat < CAT_COUNT - 1) {
			cat++;
			fade = 0.0f;
		}
		if ((down & (KEY_DLEFT | KEY_L)) && cat > 0) {
			cat--;
			fade = 0.0f;
		}
		if ((rep & KEY_DDOWN) && g_sel[cat] < g_count[cat] - 1) g_sel[cat]++;
		if ((rep & KEY_DUP) && g_sel[cat] > 0) g_sel[cat]--;
		if (down & KEY_A) activate(&g_items[cat][g_sel[cat]]);

		// Ease towards the selection.
		catPos += ((float)cat - catPos) * 0.25f;
		fade += (1.0f - fade) * 0.2f;
		for (int c = 0; c < CAT_COUNT; c++) g_itemPos[c] += ((float)g_sel[c] - g_itemPos[c]) * 0.3f;

		float t = (float)osGetTime() / 1000.0f;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TextBufClear(g_buf);

		C2D_TargetClear(top, clear);
		C2D_SceneBegin(top);
		draw_top(t, cat, catPos, fade);

		C2D_TargetClear(bot, clear);
		C2D_SceneBegin(bot);
		draw_bottom(t, cat);

		C3D_FrameEnd(0);
	}

	free_items();
	if (g_haveAm) amExit();
	C2D_TextBufDelete(g_buf);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
