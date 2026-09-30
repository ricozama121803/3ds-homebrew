// Tuffy Run - Nintendo 3DS homebrew endless runner
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "game.h"
#include "render.h"

#define SAVE_DIR  "sdmc:/3ds"
#define SAVE_FILE "sdmc:/3ds/tuffyrun.sav"

// ---- high score on the SD card ----------------------------------------
static int load_best(void)
{
	int best = 0;
	FILE *f = fopen(SAVE_FILE, "rb");
	if (f) {
		if (fread(&best, sizeof best, 1, f) != 1 || best < 0 || best > 99999999) best = 0;
		fclose(f);
	}
	return best;
}

static void save_best(int best)
{
	mkdir(SAVE_DIR, 0777);                       // harmless if it already exists
	FILE *f = fopen(SAVE_FILE, "wb");
	if (f) {
		fwrite(&best, sizeof best, 1, f);
		fclose(f);
	}
}

// ---- touch swipes ------------------------------------------------------
typedef struct { bool active, fired; int x0, y0; } Swipe;

// returns true on a tap; fires game actions for swipes
static bool handle_touch(Game *g, Swipe *s, u32 down, u32 held, u32 up)
{
	touchPosition tp;
	if (down & KEY_TOUCH) {
		hidTouchRead(&tp);
		s->active = true; s->fired = false; s->x0 = tp.px; s->y0 = tp.py;
	}
	if (s->active && (held & KEY_TOUCH) && !s->fired) {
		hidTouchRead(&tp);
		int dx = tp.px - s->x0, dy = tp.py - s->y0;
		if (abs(dx) > 22 || abs(dy) > 22) {
			s->fired = true;
			if (abs(dx) > abs(dy)) game_action(g, dx < 0 ? ACT_LEFT : ACT_RIGHT);
			else                   game_action(g, dy < 0 ? ACT_JUMP : ACT_SLIDE);
		}
	}
	bool tap = false;
	if (up & KEY_TOUCH) {
		tap = s->active && !s->fired;
		s->active = false;
	}
	return tap;
}

int main(void)
{
	gfxInitDefault();
	gfxSet3D(true);                              // stereoscopic 3D on the top screen
	cfguInit();                                  // lets citro2d load the system font
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(16384);
	C2D_Prepare();
	osSetSpeedupEnable(true);                    // New 3DS: run at full speed (ignored on old models)

	C3D_RenderTarget *topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	C3D_RenderTarget *bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	render_init();

	Game g;
	game_init(&g, load_best(), (unsigned)osGetTime() | 1u);
	Swipe swipe = {0};

	const u32 black = C2D_Color32(0, 0, 0, 255);
	u64 last = svcGetSystemTick();
	bool quit = false;

	while (aptMainLoop() && !quit) {
		u64 now = svcGetSystemTick();
		float dt = (float)(now - last) / (float)SYSCLOCK_ARM11;
		last = now;
		if (dt > 0.05f) dt = 0.05f;              // don't let a hiccup teleport us through obstacles
		if (dt < 0.0005f) dt = 0.0005f;

		// ---- input ----
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
		bool tap = handle_touch(&g, &swipe, down, held, up);

		if (down & (KEY_LEFT  | KEY_L)) game_action(&g, ACT_LEFT);
		if (down & (KEY_RIGHT | KEY_R)) game_action(&g, ACT_RIGHT);
		if (down & (KEY_A | KEY_UP   | KEY_X)) game_action(&g, ACT_JUMP);
		if (down & (KEY_B | KEY_DOWN | KEY_Y)) game_action(&g, ACT_SLIDE);

		bool confirm = (down & KEY_A) || tap;
		switch (g.state) {
		case ST_TITLE:
			if (confirm) game_start(&g);
			if (down & KEY_START) quit = true;
			break;
		case ST_PLAY:
			if (down & KEY_START) g.state = ST_PAUSED;
			break;
		case ST_PAUSED:
			if (down & KEY_START) g.state = ST_PLAY;
			if (down & KEY_SELECT) quit = true;
			break;
		case ST_DEAD:
			if (g.deadT > 0.6f && confirm) game_start(&g);
			if (down & KEY_START) quit = true;
			break;
		}

		// ---- update ----
		game_update(&g, dt);
		if (g.saveNeeded) { save_best(g.best); g.saveNeeded = false; }

		// ---- draw ----
		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		render_prepare(&g);
		u32 haze = render_haze(&g);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(topL, haze);
		C2D_SceneBegin(topL);
		render_top(&g, -1.0f, slider);
		if (slider > 0) {
			C2D_TargetClear(topR, haze);
			C2D_SceneBegin(topR);
			render_top(&g, +1.0f, slider);
		}
		C2D_TargetClear(bot, black);
		C2D_SceneBegin(bot);
		render_bottom(&g);
		C3D_FrameEnd(0);
	}

	render_exit();
	C2D_Fini();
	C3D_Fini();
	cfguExit();
	gfxExit();
	return 0;
}
