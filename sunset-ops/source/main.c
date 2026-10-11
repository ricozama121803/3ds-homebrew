// Sunset Ops - Nintendo 3DS homebrew: first-person team deathmatch
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
#define SAVE_FILE "sdmc:/3ds/sunsetops.sav"
#define LOCK_FILE "sdmc:/3ds/sunsetops.lock"       // exists while a launch is young: if the game dies in that window, the next start uses Low graphics
typedef struct { unsigned magic; float sens; int invert, best, assist, quality, fps, nohaze; } SaveData;
static SaveData load_save(void)
{
	SaveData sd = { 0x534F5001u, 1.0f, 0, 0, 1, 1, 0, 0 };
	FILE *f = fopen(SAVE_FILE, "rb");
	if (!f) return sd;
	SaveData in = sd;
	size_t n = fread(&in, 1, sizeof in, f);            // older saves are shorter: the missing fields keep their defaults
	fclose(f);
	if (n >= 16 && in.magic == 0x534F5001u && in.sens >= 0.4f && in.sens <= 2.0f) {
		sd = in;
		if (n < 20) sd.assist = 1;
		if (n < 24) sd.quality = 1;
		if (n < 28) sd.fps = 0;
		if (n < 32) sd.nohaze = 0;
		if (sd.best < 0 || sd.best > 10000) sd.best = 0;
	}
	return sd;
}
static void write_save(const Game *gm)
{
	SaveData sd = { 0x534F5001u, gm->sens, gm->invertY, gm->bestKills, gm->aimAssist, gm->quality, gm->showFps, gm->noHaze };
	mkdir(SAVE_DIR, 0777);
	FILE *f = fopen(SAVE_FILE, "wb");
	if (f) { fwrite(&sd, sizeof sd, 1, f); fclose(f); }
}
static bool file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false; }

static void fail(const char *msg)
{
	consoleInit(GFX_BOTTOM, NULL);
	printf("\x1b[2;1HSunset Ops could not start:\n\n%s\n\nPress START to exit.", msg);
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

int main(void)
{
	gfxInitDefault();
	gfxSet3D(true);                          // stereoscopic 3D on the top screen
	cfguInit();                              // lets citro2d load the system font
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(8192);
	C2D_Prepare();
	osSetSpeedupEnable(true);                // New 3DS: full speed (ignored on old models)
	bool cstick = R_SUCCEEDED(irrstInit());  // New 3DS C-stick (optional extra look stick)

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
	game_init(&g, sv.quality);
	g.sens = sv.sens; g.invertY = sv.invert != 0; g.bestKills = sv.best; g.aimAssist = sv.assist != 0; g.showFps = sv.fps != 0; g.noHaze = sv.nohaze != 0;
	if (crashed) { snprintf(g.pop[0].text, sizeof g.pop[0].text, "Last launch crashed: Low graphics"); g.pop[0].t = 8.0f; g.pop[0].col = 0xFF60C0FF; g.saveNeeded = true; }
	int framesRun = 0; float fpsAvg = 30.0f;
	g.rng ^= (unsigned)osGetTime();
	if (!gfx_init(&g)) {
		gfx_exit(); hud_exit(); C2D_Fini(); C3D_Fini();
		remove(LOCK_FILE);
		fail("The 3D renderer could not be set up (out of memory or bad data).");
		cfguExit(); gfxExit();
		return 1;
	}

	const u32 clearBot = C2D_Color32(14, 18, 24, 255), clearTop = C2D_Color32(230, 180, 140, 255);
	u64 last = svcGetSystemTick();

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
		// circle pad = look (left stick); radial dead zone
		float lx = cp.dx / 150.0f + cs.dx / 150.0f, ly = cp.dy / 150.0f + cs.dy / 150.0f;
		float mag = sqrtf(lx * lx + ly * ly);
		if (mag > 1.0f) { lx /= mag; ly /= mag; mag = 1.0f; }
		float m2 = deadzone(mag);
		if (mag > 0.0001f) { lx *= m2 / mag; ly *= m2 / mag; } else lx = ly = 0;
		in.lx = lx; in.ly = ly;
		in.fwd = (held & KEY_X) != 0;                    // X forward, B back, Y strafe left, A strafe right
		in.back = (held & KEY_B) != 0;
		in.left = (held & KEY_Y) != 0;
		in.right = (held & KEY_A) != 0;
		in.fire = (held & (KEY_R | KEY_ZR)) != 0;        // R shoot (ZR on New 3DS too)
		in.aim = (held & (KEY_L | KEY_ZL)) != 0;         // L aim down sights
		in.next_p = (down & KEY_DRIGHT) != 0;            // d-pad: right/left change weapon, up grenade, down heal
		in.prev_p = (down & KEY_DLEFT) != 0;
		in.grenade_p = (down & KEY_DUP) != 0;
		in.heal_p = (down & KEY_DDOWN) != 0;
		in.reload_p = (down & (KEY_SELECT | KEY_TOUCH)) != 0;
		in.start_p = (down & KEY_START) != 0;
		in.a_p = (down & KEY_A) != 0;
		in.b_p = (down & KEY_B) != 0;
		in.x_p = (down & KEY_X) != 0;
		in.r_p = (down & KEY_R) != 0;
		in.l_p = (down & KEY_L) != 0;
		in.y_p = (down & KEY_Y) != 0;

		game_update(&g, &in, dt);
		fpsAvg += (1.0f / dt - fpsAvg) * fminf(1.0f, dt * 1.5f); g.fps = fpsAvg;
		if (framesRun < 100000 && ++framesRun == 900) remove(LOCK_FILE);          // 30 s of play: this launch is fine
		if (g.saveNeeded) { write_save(&g); g.saveNeeded = false; }

		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		hud_frame_begin();

		gfx_set_haze(!g.noHaze);
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
