// Ringside - Nintendo 3DS homebrew: 3D kickboxing
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "hud.h"
#include "gfx.h"

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static Game g;                                    // big: lives in static memory, not on the stack

// Buttons -> the fighter controls:  Y lead punch, X rear punch, B lead kick, A rear kick, L hook modifier (L+R = body block),
// R block head, D-pad head movement, circle pad footwork.
static FighterInput read_input(u32 down, u32 held)
{
	FighterInput in;
	memset(&in, 0, sizeof in);
	static const struct { u32 key; unsigned bit; } map[] = {
		{ KEY_Y, BTN_LEAD_PUNCH }, { KEY_X, BTN_REAR_PUNCH }, { KEY_B, BTN_LEAD_KICK }, { KEY_A, BTN_REAR_KICK },
		{ KEY_L, BTN_MOD }, { KEY_R, BTN_GUARD },
		{ KEY_DLEFT, BTN_DP_LEFT }, { KEY_DRIGHT, BTN_DP_RIGHT }, { KEY_DUP, BTN_DP_UP }, { KEY_DDOWN, BTN_DP_DOWN },
	};
	for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
		if (held & map[i].key) in.held |= map[i].bit;
		if (down & map[i].key) in.pressed |= map[i].bit;
	}
	circlePosition cp;
	hidCircleRead(&cp);
	float x = cp.dx / 150.0f, y = cp.dy / 150.0f;
	float len = sqrtf(x * x + y * y);
	if (len < 0.14f) x = y = 0;                                   // dead zone
	else if (len > 1.0f) { x /= len; y /= len; }
	in.mx = x;
	in.mz = -y;                                                   // stick up = into the screen
	return in;
}

static void draw_view(C3D_RenderTarget *t, const Camera *cam, const Lights *lt, int eye, u32 clear)
{
	C3D_RenderTargetClear(t, C3D_CLEAR_ALL, clear, 0);
	C3D_FrameDrawOn(t);
	gfx_scene_begin(cam, lt, eye);
	scene_draw(&g.f[0], &g.f[1], g.time, g.fx, MAX_FX, CAM_ANGLE);
	gfx_scene_end();
	C2D_Prepare();                                                // the HUD needs citro2d's own state back
	C2D_SceneBegin(t);
	hud_top(&g);
}

int main(void)
{
	gfxInitDefault();
	gfxSet3D(true);                                               // stereoscopic 3D on the top screen
	cfguInit();                                                   // lets citro2d load the system font
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(4096);
	C2D_Prepare();
	osSetSpeedupEnable(true);                                     // New 3DS: full speed (ignored on old models)

	C3D_RenderTarget *topL = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTarget *topR = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(topL, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetSetOutput(topR, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	bool is_new = false;
	APT_CheckNew3DS(&is_new);
	fighter_set_detail(is_new);                                   // outlines and small details need the New 3DS's headroom
	gfx_init();
	fighter_meshes_build();
	scene_build();
	gfx_upload_meshes();
	game_init(&g);
	g.ai.rng ^= (unsigned)osGetTime();                            // a different fight every launch

	const Lights lights = scene_lights();
	const u32 clear = 0x06060CFF;
	u64 last = svcGetSystemTick();
	float acc = 0;
	int paused = 0;
	FighterInput in;
	memset(&in, 0, sizeof in);

	while (aptMainLoop()) {
		u64 now = svcGetSystemTick();
		float dt = (float)(now - last) / (float)SYSCLOCK_ARM11;
		last = now;
		if (dt > 0.1f) dt = 0.1f;

		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		in = read_input(down, held);

		if (down & KEY_START) {
			if (g.phase == PH_KO || g.phase == PH_TIMEUP) { if (g.phase_t > 60) game_restart(&g); }
			else paused = !paused;
		}
		if (paused && (down & KEY_SELECT)) break;

		if (!paused) {
			acc += dt;
			if (acc > TICK_DT * 4.0f) acc = TICK_DT * 4.0f;       // never spiral if a frame runs long
			while (acc >= TICK_DT) {
				game_step(&g, &in);
				in.pressed = 0;                                   // a press only counts for one tick
				acc -= TICK_DT;
			}
		}

		Camera cam;
		camera_update(&g.cam, &g.f[0], &g.f[1], paused ? 0.0f : dt, g.time, &cam);
		float slider = osGet3DSliderState();
		cam.iod = slider * 0.25f;

		gfx2d_frame_begin();
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		draw_view(topL, &cam, &lights, -1, clear);
		if (slider > 0.01f) draw_view(topR, &cam, &lights, +1, clear);
		C3D_RenderTargetClear(bot, C3D_CLEAR_ALL, 0x14161CFF, 0);
		C2D_SceneBegin(bot);
		hud_bottom(&g, &in, paused);
		C3D_FrameEnd(0);
	}

	gfx_exit();
	C2D_Fini();
	C3D_Fini();
	cfguExit();
	gfxExit();
	return 0;
}
