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
#include "map_bin.h"                        // the city, embedded from assets/map.bin (see Makefile)

// ---- high scores / cash on the SD card --------------------------------
#define SAVE_DIR  "sdmc:/3ds"
#define SAVE_FILE "sdmc:/3ds/grandtheftotto.sav"
static bool load_save(SaveData *sd)
{
	FILE *f = fopen(SAVE_FILE, "rb");
	if (!f) return false;
	bool ok = fread(sd, sizeof *sd, 1, f) == 1 && sd->cash >= 0 && sd->cash < 100000000;
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

	C3D_RenderTarget *topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	C3D_RenderTarget *bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	render_init();

	SaveData sd;
	bool have_save = load_save(&sd);
	if (!game_init(&g, map_bin, map_bin_size, have_save ? &sd : NULL)) {
		render_exit(); C2D_Fini(); C3D_Fini(); cfguExit(); gfxExit();
		return 1;
	}
	g.rng ^= (unsigned)osGetTime();          // a different city every time you launch

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

		game_update(&g, &in, dt);
		if (g.saveNeeded && g.saveT > 3.0f) { write_save(&g); g.saveNeeded = false; g.saveT = 0; }

		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		render_prepare(&g);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(topL, clear);
		C2D_SceneBegin(topL);
		render_top(&g, -1.0f, slider);
		if (slider > 0) {
			C2D_TargetClear(topR, clear);
			C2D_SceneBegin(topR);
			render_top(&g, +1.0f, slider);
		}
		C2D_TargetClear(bot, clear);
		C2D_SceneBegin(bot);
		render_bottom(&g);
		C3D_FrameEnd(0);
	}

	write_save(&g);
	render_exit();
	C2D_Fini();
	C3D_Fini();
	cfguExit();
	gfxExit();
	return 0;
}
