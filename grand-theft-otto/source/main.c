// Grand Theft Otto - Nintendo 3DS homebrew: top-down LA crime sandbox
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#include "game.h"
#include "render.h"
#include "r3d_3ds.h"
#include "map_bin.h"                        // the city, embedded from assets/map.bin (see Makefile)

// ---- high scores / cash on the SD card --------------------------------
#define SAVE_DIR  "sdmc:/3ds"
#define SAVE_FILE "sdmc:/3ds/grandtheftotto.sav"
static bool load_save(SaveData *sd)
{
	FILE *f = fopen(SAVE_FILE, "rb");
	if (!f) return false;
	memset(sd, 0, sizeof *sd);
	size_t n = fread(sd, 1, sizeof *sd, f);                       // older saves are shorter (fewer weapons): the rest stays zero
	bool ok = n >= SAVE_V1_SIZE && sd->cash >= 0 && sd->cash < 100000000;
	fclose(f);
	return ok;
}
static void write_save(const Game *g)
{
	SaveData sd;
	game_save_data(g, &sd);
	mkdir(SAVE_DIR, 0777);
	FILE *f = fopen(SAVE_FILE, "wb");
	if (f) { fwrite(&sd, sizeof sd, 1, f); fclose(f); }
}

// ---- crash guard for the 3D view: a lock file exists while a 3D session is young; if the game dies in that window the next start uses 2D
#define LOCK_FILE "sdmc:/3ds/grandtheftotto.3d.lock"
static bool file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false; }
static void lock_set(void) { mkdir(SAVE_DIR, 0777); FILE *f = fopen(LOCK_FILE, "wb"); if (f) { fputc('1', f); fclose(f); } }
static void lock_clear(void) { remove(LOCK_FILE); }

static Game g;                                    // big: lives in static memory, not on the stack

int main(void)
{
	gfxInitDefault();
	gfxSet3D(true);                          // stereoscopic 3D on the top screen
	cfguInit();                              // lets citro2d load the system font
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(16384);
	C2D_Prepare();
	osSetSpeedupEnable(true);                // New 3DS: full speed (ignored on old models)

	// the top screen targets carry a depth buffer, so the 3D view can use the same ones as the 2D view
	const u32 xfer = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
	C3D_RenderTarget *topL = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(topL, GFX_TOP, GFX_LEFT, xfer);
	C3D_RenderTarget *topR = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(topR, GFX_TOP, GFX_RIGHT, xfer);
	C3D_RenderTarget *bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	render_init();

	SaveData sd;
	bool have_save = load_save(&sd);
	if (!game_init(&g, map_bin, map_bin_size, have_save ? &sd : NULL)) {
		render_exit(); C2D_Fini(); C3D_Fini(); cfguExit(); gfxExit();
		return 1;
	}
	g.rng ^= (unsigned)osGetTime();          // a different city every time you launch

	bool use3d = false, r3d_ready = false;
	int frames3d = 0;
	if (file_exists(LOCK_FILE)) {
		popup(&g, "3D view crashed last time: using 2D (pause, X to retry)", 0xFF60C0FF);
	} else {
		lock_set();
		r3d_ready = r3d3ds_init();
		use3d = r3d_ready;
		if (use3d) r3d_init(&g.world); else lock_clear();
	}
	g.view3d = use3d;

	const u32 clear = C2D_Color32(20, 24, 40, 255);
	u64 last = svcGetSystemTick();

	while (aptMainLoop() && !g.quit) {
		u64 now = svcGetSystemTick();
		float dt = (float)(now - last) / (float)SYSCLOCK_ARM11;
		last = now;
		if (dt > 0.05f) dt = 0.05f;
		if (dt < 0.0005f) dt = 0.0005f;

		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();

		Input in;
		memset(&in, 0, sizeof in);
		circlePosition cp;
		hidCircleRead(&cp);
		in.mx = cp.dx / 150.0f;
		in.my = -cp.dy / 150.0f;                                     // circle pad up is positive; screen down is positive
		if (fabsf(in.mx) < 0.12f) in.mx = 0;                         // dead zone
		if (fabsf(in.my) < 0.12f) in.my = 0;
		if (held & KEY_DLEFT)  in.mx = -1;
		if (held & KEY_DRIGHT) in.mx = 1;
		if (held & KEY_DUP)    in.my = -1;
		if (held & KEY_DDOWN)  in.my = 1;
		in.run = (held & KEY_R) != 0;
		in.a = (held & KEY_A) != 0;
		in.b = (held & KEY_B) != 0;
		in.l = (held & KEY_L) != 0;
		in.x_p = (down & KEY_X) != 0;
		in.y_p = (down & KEY_Y) != 0;
		in.l_p = (down & KEY_L) != 0;
		in.start_p = (down & KEY_START) != 0;
		in.select_p = (down & KEY_SELECT) != 0;
		if (down & KEY_TOUCH) { touchPosition tp; hidTouchRead(&tp); in.tap = true; in.tx = tp.px; in.ty = tp.py; }

		if (g.paused && (down & KEY_X)) {                            // pause menu: X switches between the 3D and the 2D view
			if (use3d) { use3d = false; lock_clear(); g.camyaw = 0; }
			else {
				lock_set();
				if (!r3d_ready) { r3d_ready = r3d3ds_init(); if (r3d_ready) r3d_init(&g.world); }
				use3d = r3d_ready; frames3d = 0;
				if (!use3d) lock_clear();
			}
			g.view3d = use3d;
		}
		in.x_p = in.x_p && !g.paused;
		game_update(&g, &in, dt);
		if (g.saveNeeded && g.saveT > 3.0f) { write_save(&g); g.saveNeeded = false; g.saveT = 0; }

		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		render_prepare(&g);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		R3DScene *scene = use3d ? r3d3ds_begin(&g) : NULL;
		const u32 sky = scene ? C2D_Color32f(scene->sky[0], scene->sky[1], scene->sky[2], 1.0f) : clear;
		C2D_TargetClear(topL, sky);
		C2D_SceneBegin(topL);
		if (scene) { r3d3ds_draw(scene, -1.0f, slider); r3d3ds_end_3d(); } else render_top(&g, -1.0f, slider);
		render_top_hud(&g, -1.0f, slider);
		if (slider > 0) {
			C2D_TargetClear(topR, sky);
			C2D_SceneBegin(topR);
			if (scene) { r3d3ds_draw(scene, +1.0f, slider); r3d3ds_end_3d(); } else render_top(&g, +1.0f, slider);
			render_top_hud(&g, +1.0f, slider);
		}
		if (use3d && frames3d < 100000 && ++frames3d == 900) lock_clear();                   // survived 30 seconds of 3D: not a crash-on-start problem
		C2D_TargetClear(bot, clear);
		C2D_SceneBegin(bot);
		render_bottom(&g);
		C3D_FrameEnd(0);
	}

	write_save(&g);
	lock_clear();
	if (r3d_ready) r3d3ds_exit();
	render_exit();
	C2D_Fini();
	C3D_Fini();
	cfguExit();
	gfxExit();
	return 0;
}
