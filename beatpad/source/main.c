// BeatPad - Nintendo 3DS rhythm game: notes scroll down a highway, hit the matching button on the beat.
// Lanes: D-pad Left / Up / Right and Y / X / A.
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "audio.h"
#include "game.h"
#include "render.h"
#include "net.h"

#define ROOT       "romfs:"               // songs built into the app
#define SD_ROOT    "sdmc:/3ds/beatpad"    // songs downloaded with Get Songs
#define SAVE_DIR   "sdmc:/3ds"
#define SAVE_FILE  "sdmc:/3ds/beatpad.txt"
#define LEAD_IN_MS 2400.0f
#define PREVIEW_MS 20000

static const u32 LANE_KEYS[NUM_LANES] = { KEY_DLEFT, KEY_DUP, KEY_DRIGHT, KEY_Y, KEY_X, KEY_A };

static App app;

// ---- save file: settings and best scores (plain text) -----------------------------
static void save_all(const App *a)
{
	mkdir(SAVE_DIR, 0777);
	FILE *f = fopen(SAVE_FILE, "w");
	if (!f) return;
	fprintf(f, "speed %.2f\noffset %d\nnofail %d\n", a->speed, a->offset, a->noFail ? 1 : 0);
	if (a->lastIp) { struct in_addr ia; ia.s_addr = a->lastIp; fprintf(f, "server %s\n", inet_ntoa(ia)); }
	for (int s = 0; s < a->nsongs; s++)
		for (int d = 0; d < NUM_DIFFS; d++) {
			const Best *b = &a->best[s][d];
			if (b->played) fprintf(f, "best %s %d %d %.2f %s %d\n", a->songs[s].dir, d, b->score, b->acc, b->grade, b->fc ? 1 : 0);
		}
	fclose(f);
}

static void load_all(App *a)
{
	a->speed = 1.75f; a->offset = 0; a->noFail = true;
	FILE *f = fopen(SAVE_FILE, "r");
	if (!f) return;
	char line[160], key[16], dir[48], grade[4];
	while (fgets(line, sizeof line, f)) {
		float v; int d, sc, fc; float acc;
		if (sscanf(line, "%15s", key) != 1) continue;
		if (!strcmp(key, "speed") && sscanf(line, "%*s %f", &v) == 1) a->speed = fminf(4, fmaxf(1, v));
		else if (!strcmp(key, "offset") && sscanf(line, "%*s %d", &d) == 1) a->offset = d;
		else if (!strcmp(key, "nofail") && sscanf(line, "%*s %d", &d) == 1) a->noFail = d != 0;
		else if (!strcmp(key, "server") && sscanf(line, "%*s %47s", dir) == 1) a->lastIp = inet_addr(dir);
		else if (!strcmp(key, "best") && sscanf(line, "%*s %47s %d %d %f %3s %d", dir, &d, &sc, &acc, grade, &fc) == 6 && d >= 0 && d < NUM_DIFFS)
			for (int s = 0; s < a->nsongs; s++)
				if (!strcmp(a->songs[s].dir, dir)) {
					Best *b = &a->best[s][d];
					b->played = true; b->score = sc; b->acc = acc; b->fc = fc != 0;
					snprintf(b->grade, sizeof b->grade, "%s", grade);
				}
	}
	fclose(f);
}

// ---- song clock: follows the DSP's play position, smoothed with the frame timer ------
static float clk;              // ms of audio time
static bool clkAudio;          // audio started (false during the lead-in)

static void clock_update(float dt_ms)
{
	clk += dt_ms;
	if (!clkAudio) {
		if (clk >= 0 && app.screen == SCR_PLAY) { audio_play((int)clk); clkAudio = true; }
		return;
	}
	if (!app.audioOk || audio_finished()) return;
	float err = audio_time_ms() - clk;
	if (fabsf(err) > 80) clk += err;       // big jump (seek / hiccup): snap
	else clk += err * 0.12f;               // otherwise ease toward the DSP position
}

// ---- screens -------------------------------------------------------------------------
static void load_chart(App *a)
{
	chart_free(&a->chart);
	chart_load(&a->songs[a->sel], a->diff, &a->chart);
}

static void open_song_audio(App *a)
{
	char path[160];
	snprintf(path, sizeof path, "%s/songs/%s", a->songs[a->sel].root, a->songs[a->sel].dir);
	audio_open(path);
}

// (re)reads both song folders, keeping the selected song selected
static void rescan(App *a)
{
	char keep[48];
	snprintf(keep, sizeof keep, "%s", a->nsongs ? a->songs[a->sel].dir : "");
	chart_free(&a->chart);
	memset(&a->play, 0, sizeof a->play);
	for (int i = 0; i < a->nsongs; i++) song_free(&a->songs[i]);
	memset(a->songs, 0, sizeof a->songs);
	memset(a->best, 0, sizeof a->best);
	a->nsongs = songs_scan(SD_ROOT, a->songs, MAX_SONGS);       // downloaded copies win over built-in ones
	a->nsongs = songs_scan(ROOT, a->songs, MAX_SONGS);
	a->sel = 0;
	for (int i = 0; i < a->nsongs; i++) if (!strcmp(a->songs[i].dir, keep)) a->sel = i;
	uint32_t ip = a->lastIp;
	load_all(a);
	if (!a->lastIp) a->lastIp = ip;
}

static void start_preview(App *a)
{
	a->demo = true;
	load_chart(a);
	play_start(&a->play, &a->songs[a->sel], &a->chart, a->diff, true);
	open_song_audio(a);
	int from = a->songs[a->sel].preview_ms;
	audio_set_volume(0.0f);
	audio_play(from);
	clk = (float)from; clkAudio = true;
	// mark notes before the preview as already done so the demo starts clean
	for (int i = 0; i < a->chart.n; i++) if (a->chart.notes[i].t < from + 300) a->play.chart.notes[i].state = NS_DONE;
}

static void restart_demo_chart(App *a)        // difficulty changed: keep the music going
{
	load_chart(a);
	play_start(&a->play, &a->songs[a->sel], &a->chart, a->diff, true);
	float now = clk - a->offset;
	for (int i = 0; i < a->chart.n; i++) if (a->chart.notes[i].t < now + 300) a->play.chart.notes[i].state = NS_DONE;
}

static void set_screen(App *a, int s) { a->screen = s; a->screenT = 0; a->menuSel = 0; }

static void start_song(App *a)
{
	a->demo = false;
	audio_stop();
	load_chart(a);
	play_start(&a->play, &a->songs[a->sel], &a->chart, a->diff, a->noFail);
	a->displayScore = 0;
	open_song_audio(a);
	audio_set_volume(1.0f);
	clk = -LEAD_IN_MS; clkAudio = false;
	a->play.t = clk - a->offset;
	set_screen(a, SCR_PLAY);
}

static void finish_song(App *a)
{
	Play *p = &a->play;
	Best *b = &a->best[a->sel][a->diff];
	a->newBest = !p->failed && (!b->played || p->score > b->score);
	if (a->newBest) {
		b->played = true; b->score = p->score; b->acc = play_accuracy(p); b->fc = p->fullCombo;
		snprintf(b->grade, sizeof b->grade, "%s", play_grade(p));
		save_all(a);
	}
	set_screen(a, SCR_RESULTS);
}

static void back_to_menu(App *a)
{
	if (a->nsongs == 0) { set_screen(a, SCR_NOSONGS); return; }
	set_screen(a, SCR_SELECT);
	start_preview(a);
}

// ---- Get Songs ----------------------------------------------------------------------
static void enter_getsongs(App *a)
{
	audio_stop();
	set_screen(a, SCR_GETSONGS);
	a->gsScroll = 0; a->gsGetAll = false; a->gsConfirmDelete = -1;
	net_start(a->lastIp);
}

static void leave_getsongs(App *a)
{
	net_stop();
	rescan(a);
	save_all(a);
	back_to_menu(a);
}

// the list: every song on the PC (new / update / installed), then downloaded songs the PC doesn't have
static void gs_build(App *a)
{
	int n = 0;
	bool used[MAX_SONGS] = { 0 };
	if (net_state() == NET_CONNECTED)
		for (int r = 0; r < net_song_count(); r++) {
			const RemoteSong *rs = net_song(r);
			int local = -1;
			for (int i = 0; i < a->nsongs; i++)
				if (!strcmp(a->songs[i].dir, rs->id) && !strncmp(a->songs[i].root, "sdmc:", 5)) local = i;
			if (local >= 0) used[local] = true;
			a->gs[n++] = (GsItem){ r, local, local < 0 ? GS_NEW : strcmp(a->songs[local].version, rs->version) ? GS_UPDATE : GS_INSTALLED };
		}
	for (int i = 0; i < a->nsongs; i++)
		if (!used[i] && !strncmp(a->songs[i].root, "sdmc:", 5)) a->gs[n++] = (GsItem){ -1, i, GS_LOCAL_ONLY };
	a->ngs = n;
	if (a->menuSel >= n) a->menuSel = n ? n - 1 : 0;
}

static void ask_address(App *a)
{
	SwkbdState kb;
	char buf[20] = "";
	swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 15);
	swkbdSetNumpadKeys(&kb, L'.', 0);
	swkbdSetHintText(&kb, "Your PC's address, e.g. 192.168.1.101");
	if (a->lastIp) { struct in_addr ia; ia.s_addr = a->lastIp; swkbdSetInitialText(&kb, inet_ntoa(ia)); }
	if (swkbdInputText(&kb, buf, sizeof buf) == SWKBD_BUTTON_CONFIRM) {
		uint32_t ip = inet_addr(buf);
		if (ip != INADDR_NONE && ip != 0) net_connect_ip(ip);
	}
}

static void update_getsongs(App *a, u32 down, u32 rep)
{
	static int seenFinished;
	net_poll();
	if (net_finished() != seenFinished) {              // a download landed: pick it up
		seenFinished = net_finished();
		a->lastIp = net_server_ip();
		rescan(a);
	}
	gs_build(a);
	if (rep & KEY_DDOWN) { a->menuSel = a->ngs ? (a->menuSel + 1) % a->ngs : 0; a->gsConfirmDelete = -1; }
	if (rep & KEY_DUP) { a->menuSel = a->ngs ? (a->menuSel + a->ngs - 1) % a->ngs : 0; a->gsConfirmDelete = -1; }
	if (a->menuSel < a->gsScroll) a->gsScroll = a->menuSel;
	if (a->menuSel >= a->gsScroll + 6) a->gsScroll = a->menuSel - 5;
	const GsItem *it = a->ngs ? &a->gs[a->menuSel] : NULL;

	if ((down & KEY_A) && it && it->remote >= 0 && !net_busy()) net_download(it->remote, SD_ROOT);
	if (down & KEY_X) a->gsGetAll = !a->gsGetAll;
	if (a->gsGetAll && !net_busy()) {                  // next song that is new or has an update
		int next = -1;
		for (int i = 0; i < a->ngs && next < 0; i++) if (a->gs[i].status == GS_NEW || a->gs[i].status == GS_UPDATE) next = i;
		if (next < 0) a->gsGetAll = false;
		else if (!net_download(a->gs[next].remote, SD_ROOT)) a->gsGetAll = false;
	}
	if ((down & KEY_SELECT) && it && it->local >= 0 && !net_busy()) {
		if (a->gsConfirmDelete == a->menuSel) {
			song_delete(&a->songs[it->local]);
			a->gsConfirmDelete = -1;
			rescan(a);
		} else a->gsConfirmDelete = a->menuSel;
	}
	if ((down & KEY_Y) && !net_busy()) ask_address(a);
	if (down & KEY_B) {
		if (net_busy()) { net_cancel(); a->gsGetAll = false; }
		else leave_getsongs(a);
	}
}

int main(void)
{
	gfxInitDefault();
	gfxSet3D(true);
	cfguInit();
	romfsInit();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(16384);
	C2D_Prepare();
	osSetSpeedupEnable(true);
	hidSetRepeatParameters(22, 4);         // held D-pad repeats in the settings menu

	C3D_RenderTarget *topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	C3D_RenderTarget *bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	render_init();

	App *a = &app;
	memset(a, 0, sizeof *a);
	a->audioOk = audio_init();
	a->leadIn = LEAD_IN_MS;
	rescan(a);
	a->diff = DIFF_NORMAL;
	back_to_menu(a);

	u64 last = svcGetSystemTick();
	bool quit = false;
	float previewFade = 0;

	while (aptMainLoop() && !quit) {
		u64 now = svcGetSystemTick();
		float dt = (float)(now - last) / (float)SYSCLOCK_ARM11;
		last = now;
		if (dt > 0.1f) dt = 0.1f;
		a->time += dt; a->screenT += dt;

		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld(), rep = hidKeysDownRepeat();
		uint8_t lanePressed = 0, laneHeld = 0;
		for (int l = 0; l < NUM_LANES; l++) {
			if (down & LANE_KEYS[l]) lanePressed |= 1 << l;
			if (held & LANE_KEYS[l]) laneHeld |= 1 << l;
		}

		audio_update();
		Play *p = &a->play;

		switch (a->screen) {
		case SCR_NOSONGS:
			if (down & KEY_Y) enter_getsongs(a);
			if (down & KEY_START) quit = true;
			break;

		case SCR_GETSONGS:
			update_getsongs(a, down, rep);
			break;

		case SCR_SELECT:
		case SCR_SETTINGS: {
			// autoplay demo of the chart over the song preview
			if (a->screen == SCR_SELECT) {
				int ds = (down & (KEY_DRIGHT | KEY_R)) ? 1 : (down & (KEY_DLEFT | KEY_L)) ? -1 : 0;
				if (ds) { a->diff = (a->diff + ds + NUM_DIFFS) % NUM_DIFFS; restart_demo_chart(a); }
				int ss = (rep & KEY_DDOWN) ? 1 : (rep & KEY_DUP) ? -1 : 0;
				if (ss && a->nsongs > 1) { a->sel = (a->sel + ss + a->nsongs) % a->nsongs; start_preview(a); previewFade = 0; }
				if (down & KEY_A) { start_song(a); break; }
				if (down & KEY_SELECT) set_screen(a, SCR_SETTINGS);
				if (down & KEY_Y) { enter_getsongs(a); break; }
				if (down & KEY_START) quit = true;
			} else {
				if (rep & KEY_DDOWN) a->menuSel = (a->menuSel + 1) % SET_COUNT;
				if (rep & KEY_DUP) a->menuSel = (a->menuSel + SET_COUNT - 1) % SET_COUNT;
				int d = (rep & KEY_DRIGHT) ? 1 : (rep & KEY_DLEFT) ? -1 : 0;
				if (d) {
					if (a->menuSel == SET_SPEED) a->speed = fminf(4.0f, fmaxf(1.0f, a->speed + 0.25f * d));
					if (a->menuSel == SET_OFFSET) a->offset = a->offset + 5 * d < -300 ? -300 : a->offset + 5 * d > 300 ? 300 : a->offset + 5 * d;
					if (a->menuSel == SET_NOFAIL) a->noFail = !a->noFail;
				}
				if ((down & (KEY_B | KEY_SELECT)) || ((down & KEY_A) && a->menuSel == SET_BACK)) {
					save_all(a);
					set_screen(a, SCR_SELECT);
				}
			}
			clock_update(dt * 1000);
			float t = clk - a->offset;
			uint8_t pr, hd;
			play_autoplay(p, t, &pr, &hd);
			play_update(p, t, pr, hd, dt);
			// fade the preview in, and loop it
			const Song *s = &a->songs[a->sel];
			float into = clk - s->preview_ms;
			previewFade = fminf(1, fmaxf(0, into / 1500.0f));
			if (into > PREVIEW_MS - 1500) previewFade = fmaxf(0, (PREVIEW_MS - into) / 1500.0f);
			audio_set_volume(previewFade * 0.8f);
			if (into > PREVIEW_MS || audio_finished()) start_preview(a);
			break;
		}

		case SCR_PLAY: {
			if (down & KEY_START) {
				audio_pause(true);
				set_screen(a, SCR_PAUSE);
				break;
			}
			clock_update(dt * 1000);
			int missesBefore = p->missEvents;
			play_update(p, clk - a->offset, lanePressed, laneHeld, dt);
			if (p->missEvents != missesBefore) audio_miss_sound();
			if (p->failed) { audio_stop(); finish_song(a); break; }
			bool ended = a->audioOk ? audio_finished() : clk > p->song->length_ms;
			if ((ended || clk > p->song->length_ms + 1000) && play_done(p)) finish_song(a);
			break;
		}

		case SCR_PAUSE:
			if (down & KEY_DDOWN) a->menuSel = (a->menuSel + 1) % 3;
			if (down & KEY_DUP) a->menuSel = (a->menuSel + 2) % 3;
			if ((down & KEY_START) || (down & KEY_B) || ((down & KEY_A) && a->menuSel == 0)) {
				set_screen(a, SCR_PLAY);
				if (clkAudio) audio_pause(false);
			} else if ((down & KEY_A) && a->menuSel == 1) start_song(a);
			else if ((down & KEY_A) && a->menuSel == 2) { audio_stop(); back_to_menu(a); }
			break;

		case SCR_RESULTS:
			if (a->screenT > 0.6f) {
				if (down & KEY_X) start_song(a);
				else if (down & (KEY_A | KEY_B)) { audio_stop(); back_to_menu(a); }
			}
			// let the outro play out under the results; keep the clock moving for the background
			clock_update(dt * 1000);
			p->t = clk - a->offset;
			break;
		}

		a->displayScore += (p->score - a->displayScore) * fminf(1, dt * 12);
		if (fabsf(p->score - a->displayScore) < 1) a->displayScore = (float)p->score;

		// ---- draw ----
		float slider = osGet3DSliderState();
		if (slider < 0.02f) slider = 0;
		const u32 black = C2D_Color32(0, 0, 0, 255);
		render_begin(a);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(topL, black);
		C2D_SceneBegin(topL);
		render_top(a, -1.0f, slider);
		if (slider > 0) {
			C2D_TargetClear(topR, black);
			C2D_SceneBegin(topR);
			render_top(a, +1.0f, slider);
		}
		C2D_TargetClear(bot, black);
		C2D_SceneBegin(bot);
		render_bottom(a);
		C3D_FrameEnd(0);
	}

	net_stop();
	audio_exit();
	chart_free(&a->chart);
	for (int i = 0; i < a->nsongs; i++) song_free(&a->songs[i]);
	render_exit();
	C2D_Fini();
	C3D_Fini();
	romfsExit();
	cfguExit();
	gfxExit();
	return 0;
}
