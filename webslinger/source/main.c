// Web Slinger - Nintendo 3DS homebrew: swing through a Manhattan-style city
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "game.h"
#include "gfx3ds.h"
#include "hud.h"

static Game g;                                    // big: lives in static memory, not on the stack

// ---- settings and best score on the SD card -------------------------------------------------------------------------
#define SAVE_DIR  "sdmc:/3ds"
#define SAVE_FILE "sdmc:/3ds/webslinger.sav"
#define LOCK_FILE "sdmc:/3ds/webslinger.lock"      // exists while a launch is young: if the game dies in that window, the next start uses Low graphics
#define SAVE_MAGIC 0x57534C01u
typedef struct { unsigned magic; float sens; int invert, best, fps, quality; } SaveData;
static SaveData load_save(void)
{
	SaveData sd = { SAVE_MAGIC, 1.0f, 0, 0, 0, 1 };
	FILE *f = fopen(SAVE_FILE, "rb");
	if (!f) return sd;
	SaveData in = sd;
	size_t n = fread(&in, 1, sizeof in, f);
	fclose(f);
	if (n == sizeof in && in.magic == SAVE_MAGIC && in.sens >= 0.4f && in.sens <= 2.0f) {
		sd = in;
		if (sd.best < 0 || sd.best > 1000000) sd.best = 0;
	}
	return sd;
}
static void write_save(const Game *gm)
{
	SaveData sd = { SAVE_MAGIC, gm->sens, gm->invertY, gm->best, gm->showFps, gm->lowq ? 0 : 1 };
	mkdir(SAVE_DIR, 0777);
	FILE *f = fopen(SAVE_FILE, "wb");
	if (f) { fwrite(&sd, sizeof sd, 1, f); fclose(f); }
}
static bool file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false; }

static void fail(const char *msg)
{
	consoleInit(GFX_BOTTOM, NULL);
	printf("\x1b[2;1HWeb Slinger could not start:\n\n%s\n\nPress START to exit.", msg);
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;
		gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
	}
}

static float deadzone(float v)
{
	const float dz = 0.14f;
	float a = fabsf(v);
	if (a < dz) return 0;
	a = (a - dz) / (1.0f - dz);
	return v < 0 ? -a : a;
}
static void stick(float x, float y, float *ox, float *oy)       // radial dead zone
{
	float mag = sqrtf(x * x + y * y);
	if (mag > 1.0f) { x /= mag; y /= mag; mag = 1.0f; }
	float m2 = deadzone(mag);
	if (mag > 0.0001f) { x *= m2 / mag; y *= m2 / mag; } else x = y = 0;
	*ox = x; *oy = y;
}

int main(void)
{
	gfxInitDefault();
	gfxSet3D(true);                          // stereoscopic 3D on the top screen
	cfguInit();                              // lets citro2d load the system font
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(8192);
	C2D_Prepare();
	osSetSpeedupEnable(true);                // New 3DS: full speed (ignored on old models)
	bool cstick = R_SUCCEEDED(irrstInit());  // New 3DS C-stick (camera)

	const u32 xfer = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
	C3D_RenderTarget *topL = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(topL, GFX_TOP, GFX_LEFT, xfer);
	C3D_RenderTarget *topR = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(topR, GFX_TOP, GFX_RIGHT, xfer);
	C3D_RenderTarget *bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	hud_init();

	SaveData sv = load_save();
	bool crashed = file_exists(LOCK_FILE);
	if (crashed) sv.quality = 0;                                     // the last launch died early: play it safe
	else { mkdir(SAVE_DIR, 0777); FILE *lk = fopen(LOCK_FILE, "wb"); if (lk) { fputc('1', lk); fclose(lk); } }
	game_init(&g, sv.quality == 0);
	g.sens = sv.sens; g.invertY = sv.invert != 0; g.best = sv.best; g.showFps = sv.fps != 0;
	g.rng ^= (unsigned)osGetTime();
	if (crashed) { game_popup(&g, "Last launch crashed: Low graphics", 0xFF60C0FF); g.saveNeeded = true; }
	int framesRun = 0; float fpsAvg = 30.0f;
	if (!gfx_init(&g)) {
		gfx_exit(); hud_exit(); C2D_Fini(); C3D_Fini();
		remove(LOCK_FILE);
		fail("The 3D renderer could not be set up (out of memory or bad data).");
		cfguExit(); gfxExit();
		return 1;
	}

	const u32 clearBot = C2D_Color32(8, 14, 28, 255), clearTop = C2D_Color32(204, 224, 247, 255);
	u64 last = svcGetSystemTick();
	touchPosition lastTouch = { 0, 0 };

	while (aptMainLoop() && !g.quit) {
		u64 now = svcGetSystemTick();
		float dt = (float)(now - last) / (float)SYSCLOCK_ARM11;
		last = now;
		if (dt > 0.05f) dt = 0.05f;
		if (dt < 0.0005f) dt = 0.0005f;

		hidScanInput();
		if (cstick) irrstScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();

		Input in;
		memset(&in, 0, sizeof in);
		circlePosition cp, cs = { 0, 0 };
		hidCircleRead(&cp);
		if (cstick) irrstCstickRead(&cs);
		stick(cp.dx / 150.0f, cp.dy / 150.0f, &in.lx, &in.ly);                  // circle pad: run / steer
		stick(cs.dx / 150.0f, cs.dy / 150.0f, &in.cx, &in.cy);                  // C-stick: camera
		if (held & KEY_DRIGHT) in.cx += 1.0f;                                   // d-pad: camera too
		if (held & KEY_DLEFT) in.cx -= 1.0f;
		if (held & KEY_DUP) in.cy += 1.0f;
		if (held & KEY_DDOWN) in.cy -= 1.0f;
		in.cx = in.cx > 1 ? 1 : in.cx < -1 ? -1 : in.cx; in.cy = in.cy > 1 ? 1 : in.cy < -1 ? -1 : in.cy;
		in.swing = (held & (KEY_R | KEY_ZR)) != 0;
		in.sprint = (held & (KEY_L | KEY_ZL)) != 0;
		in.dive = (held & KEY_B) != 0;
		in.a_held = (held & KEY_A) != 0;
		in.a_rel = (hidKeysUp() & KEY_A) != 0;
		in.jump_p = in.a_p = (down & KEY_A) != 0;
		in.b_p = (down & KEY_B) != 0;
		in.zip_p = (down & KEY_X) != 0;
		in.recenter_p = (down & KEY_Y) != 0;
		in.start_p = (down & KEY_START) != 0;
		in.select_p = (down & KEY_SELECT) != 0;
		in.up_p = (down & KEY_DUP) != 0;
		in.down_p = (down & KEY_DDOWN) != 0;
		if (held & KEY_TOUCH) {
			touchPosition tp; hidTouchRead(&tp);
			if (down & KEY_TOUCH) { in.touch_p = true; in.tx = tp.px; in.ty = tp.py; }
			else { in.tdx = (float)tp.px - lastTouch.px; in.tdy = (float)tp.py - lastTouch.py; }
			lastTouch = tp;
		}

		game_update(&g, &in, dt);
		fpsAvg += (1.0f / dt - fpsAvg) * fminf(1.0f, dt * 1.5f); g.fps = fpsAvg;
		if (framesRun < 100000 && ++framesRun == 900) remove(LOCK_FILE);          // 30 s of play: this launch is fine
		if (g.saveNeeded) { write_save(&g); g.saveNeeded = false; }

		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		hud_frame_begin();

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		Scene *scene = gfx_begin(&g);
		C2D_TargetClear(topL, clearTop);
		C2D_SceneBegin(topL);
		gfx_draw(scene, -1.0f, slider); gfx_end_3d();
		hud_top(&g, scene);
		if (slider > 0) {
			C2D_TargetClear(topR, clearTop);
			C2D_SceneBegin(topR);
			gfx_draw(scene, +1.0f, slider); gfx_end_3d();
			hud_top(&g, scene);
		}
		C2D_TargetClear(bot, clearBot);
		C2D_SceneBegin(bot);
		hud_bottom(&g);
		C3D_FrameEnd(0);
	}

	write_save(&g);
	remove(LOCK_FILE);
	gfx_exit();
	hud_exit();
	if (cstick) irrstExit();
	C2D_Fini();
	C3D_Fini();
	cfguExit();
	gfxExit();
	return 0;
}
