// Grand Theft Otto II - Nintendo 3DS homebrew: a small 3D crime sandbox
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
#define SAVE_FILE "sdmc:/3ds/grandtheftotto2.sav"
#define LOCK_FILE "sdmc:/3ds/grandtheftotto2.lock"      // exists while a launch is young: if the game dies in that window, the next start uses Low graphics
#define SAVE_MAGIC 0x47543203u
typedef struct { unsigned magic; float sens; int invert, fps, quality, cash; } SaveData;
static SaveData load_save(void)
{
	SaveData sd = { SAVE_MAGIC, 1.0f, 0, 0, 1, 0 };
	FILE *f = fopen(SAVE_FILE, "rb");
	if (!f) return sd;
	SaveData in = sd;
	size_t n = fread(&in, 1, sizeof in, f);
	fclose(f);
	if (n == sizeof in && in.magic == SAVE_MAGIC && in.sens >= 0.4f && in.sens <= 2.0f) { sd = in; if (sd.cash < 0 || sd.cash > 99999999) sd.cash = 0; }
	return sd;
}
static void write_save(const Game *gm)
{
	SaveData sd = { SAVE_MAGIC, gm->sens, gm->invertY, gm->showFps, gm->lowq ? 0 : 1, gm->p.cash };
	mkdir(SAVE_DIR, 0777);
	FILE *f = fopen(SAVE_FILE, "wb");
	if (f) { fwrite(&sd, sizeof sd, 1, f); fclose(f); }
}
static bool file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false; }

static void fail(const char *msg)
{
	consoleInit(GFX_BOTTOM, NULL);
	printf("\x1b[2;1HGrand Theft Otto II could not start:\n\n%s\n\nPress START to exit.", msg);
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
	g.sens = sv.sens; g.invertY = sv.invert != 0; g.showFps = sv.fps != 0; g.p.cash = sv.cash;
	g.rng ^= (unsigned)osGetTime();
	if (crashed) { g.saveNeeded = true; }
	int framesRun = 0; float fpsAvg = 30.0f;
	if (!gfx_init(&g)) {
		gfx_exit(); hud_exit(); C2D_Fini(); C3D_Fini();
		remove(LOCK_FILE);
		fail("The 3D renderer could not be set up (out of memory or bad data).");
		cfguExit(); gfxExit();
		return 1;
	}

	g.regionDirty = false;
	const u32 clearBot = C2D_Color32(14, 16, 22, 255);
	u32 clearTop = C2D_Color32(204, 224, 247, 255);
	u64 last = svcGetSystemTick(), lastSave = last;
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
		stick(cp.dx / 150.0f, cp.dy / 150.0f, &in.lx, &in.ly);                  // circle pad: move / steer
		stick(cs.dx / 150.0f, cs.dy / 150.0f, &in.cx, &in.cy);                  // C-stick: camera
		// the D-pad does not move the camera: left / right / down glance and turn, up (held) opens the weapon wheel, up twice switches weapon (game.c)
		in.cx = in.cx > 1 ? 1 : in.cx < -1 ? -1 : in.cx; in.cy = in.cy > 1 ? 1 : in.cy < -1 ? -1 : in.cy;
		in.gas = (held & (KEY_R | KEY_ZR)) != 0;                               // R: shoot on foot, gas in a car
		in.hand = (held & (KEY_A | KEY_L | KEY_ZL)) != 0;                      // A or L: handbrake in a car
		in.aim = (held & (KEY_L | KEY_ZL)) != 0;                               // L: aim / lock onto a target on foot (flick the circle pad to switch)
		in.dpx = (held & KEY_DRIGHT) ? 1.0f : (held & KEY_DLEFT) ? -1.0f : 0.0f;       // D-pad: also picks a weapon on the weapon wheel
		in.dpy = (held & KEY_DUP) ? 1.0f : (held & KEY_DDOWN) ? -1.0f : 0.0f;
		in.brake = (held & KEY_B) != 0;                                        // B: brake / reverse; two quick taps on foot = sprint
		in.a_p = (down & KEY_A) != 0;
		in.b_p = (down & KEY_B) != 0;
		in.x_p = (down & KEY_Y) != 0;                                          // Y: take / leave cover (next to a wall or car), otherwise roll
		in.enter_p = (down & KEY_X) != 0;                                      // X: get in / out of a car
		in.start_p = (down & KEY_START) != 0;
		in.select_p = (down & KEY_SELECT) != 0;                                // SELECT: closes the weapon wheel (D-pad up opens it); restart is on the pause screen
		in.up_p = (down & KEY_DUP) != 0;
		in.down_p = (down & KEY_DDOWN) != 0;
		if (held & KEY_TOUCH) {
			touchPosition tp; hidTouchRead(&tp);
			if (down & KEY_TOUCH) {
				in.touch_p = true; in.tx = tp.px; in.ty = tp.py;
				if (tp.py >= 214 && tp.px >= 6 && tp.px < 6 + W_COUNT * 38.5f) in.weap = 1 + (int)((tp.px - 6) / 38.5f);        // the weapon buttons under the map
			}
			else { in.tdx = (float)tp.px - lastTouch.px; in.tdy = (float)tp.py - lastTouch.py; }
			lastTouch = tp;
		}

		u64 t0 = svcGetSystemTick();
		game_update(&g, &in, dt);
		u64 t1 = svcGetSystemTick();
		fpsAvg += (1.0f / dt - fpsAvg) * fminf(1.0f, dt * 1.5f); g.fps = fpsAvg;
		{                                                                   // automatic quality: tighten the drawing limits after a few poor seconds, relax them after a long smooth stretch
			static float lowT = 0, hiT = 0;
			if (fpsAvg < 24.0f) { lowT += dt; hiT = 0; } else if (fpsAvg > 50.0f) { hiT += dt; lowT = 0; } else { lowT = fmaxf(0, lowT - dt); hiT = 0; }
			if (!g.slow && lowT > 3.0f) { g.slow = true; lowT = 0; }
			else if (g.slow && hiT > 12.0f) { g.slow = false; hiT = 0; }
		}
		if (framesRun < 100000 && ++framesRun == 900) remove(LOCK_FILE);          // 30 s of play: this launch is fine
		if (g.saveNeeded && now - lastSave > 5 * (u64)SYSCLOCK_ARM11) { write_save(&g); g.saveNeeded = false; lastSave = now; }                  // cash and settings, at most every 5 s

		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		hud_frame_begin();

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		if (g.regionDirty) {                                                // a new region: rebuild the scene (the screen is black at this moment)
			gfx_reload(&g); g.regionDirty = false;
			const float *hz = REGIONS[g.region].haze; clearTop = C2D_Color32((u8)(hz[0] * 255), (u8)(hz[1] * 255), (u8)(hz[2] * 255), 255);
		}
		u64 t2 = svcGetSystemTick();
		Scene *scene = gfx_begin(&g);
		u64 t3 = svcGetSystemTick();
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
		u64 t4 = svcGetSystemTick();
		C2D_TargetClear(bot, clearBot);
		C2D_SceneBegin(bot);
		hud_bottom(&g);
		u64 t5 = svcGetSystemTick();
		C3D_FrameEnd(0);
		u64 t6 = svcGetSystemTick();
		{ const float k = 0.05f, ms = 1000.0f / (float)SYSCLOCK_ARM11; float v[5] = { (t1 - t0) * ms, (t3 - t2) * ms, (t4 - t3) * ms, (t5 - t4) * ms, (t6 - t5) * ms }; for (int i = 0; i < 5; i++) g.tm[i] += (v[i] - g.tm[i]) * k; }
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
