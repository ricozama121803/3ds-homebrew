// Renders sample frames of every screen on the PC (software rasteriser in c2d_shim.h) so they can be inspected.
//   host/preview.sh <out dir>
#ifndef HOST_PREVIEW
#define HOST_PREVIEW 1
#endif
#include "c2d_shim.h"
#include "../source/game.c"
#include "../source/song.c"
#include "../source/render.c"
#include "../source/net.c"

static const char *outdir = ".";
static App app;

static void snap(const char *name)
{
	char path[512];
	render_begin(&app);
	shim_begin(400, 240, C2D_Color32(0, 0, 0, 255));
	snprintf(path, sizeof path, "%s/%s_T.txt", outdir, name); shim_textlog = fopen(path, "w");
	render_top(&app, -1, 0);
	fclose(shim_textlog); shim_textlog = NULL;
	snprintf(path, sizeof path, "%s/%s_T.ppm", outdir, name); shim_write_ppm(path);
	printf("%-10s top %5ld tris", name, shim_tris);
	shim_begin(320, 240, C2D_Color32(0, 0, 0, 255));
	snprintf(path, sizeof path, "%s/%s_B.txt", outdir, name); shim_textlog = fopen(path, "w");
	render_bottom(&app);
	fclose(shim_textlog); shim_textlog = NULL;
	snprintf(path, sizeof path, "%s/%s_B.ppm", outdir, name); shim_write_ppm(path);
	printf("   bottom %5ld tris\n", shim_tris);
}

// play from `from` to `to` ms with the autoplay bot, missing every `missEvery`-th note (0 = none)
static void simulate(Play *p, float from, float to, int missEvery)
{
	for (float t = from; t <= to; t += 1000.0f / 60) {
		uint8_t pr, hd;
		play_autoplay(p, t, &pr, &hd);
		if (missEvery) for (int i = p->first; i < p->chart.n; i++) {
			const Note *n = &p->chart.notes[i];
			if (n->t > t) break;
			if (i % missEvery == 0) { pr &= ~(1 << n->lane); hd &= ~(1 << n->lane); }
		}
		play_update(p, t, pr, hd, 1.0f / 60);
		app.displayScore = (float)p->score;
		app.time += 1.0f / 60;
	}
}

int main(int argc, char **argv)
{
	if (argc > 1) outdir = argv[1];
	const char *root = argc > 2 ? argv[2] : "romfs";
	render_init();
	App *a = &app;
	a->nsongs = songs_scan(root, a->songs, MAX_SONGS);
	if (!a->nsongs) { fprintf(stderr, "no songs\n"); return 1; }
	a->speed = 1.75f; a->noFail = true; a->audioOk = true; a->time = 3.2f;

	// song select with the autoplay demo running over the preview
	a->diff = DIFF_NORMAL; a->screen = SCR_SELECT; a->demo = true;
	chart_load(&a->songs[0], a->diff, &a->chart);
	play_start(&a->play, &a->songs[0], &a->chart, a->diff, true);
	float pv = (float)a->songs[0].preview_ms;
	for (int i = 0; i < a->chart.n; i++) if (a->chart.notes[i].t < pv + 300) a->chart.notes[i].state = NS_DONE;
	simulate(&a->play, pv, pv + 4000, 0);
	snap("select");
	a->screen = SCR_SETTINGS; a->menuSel = SET_OFFSET; snap("settings");

	a->diff = DIFF_EASY; a->screen = SCR_SELECT;
	a->best[0][DIFF_EASY] = (Best){ 142310, 97.4f, "S", true, true };
	chart_free(&a->chart); chart_load(&a->songs[0], a->diff, &a->chart);
	play_start(&a->play, &a->songs[0], &a->chart, a->diff, true);
	simulate(&a->play, -2400, 30000, 0);
	snap("select_easy");

	// gameplay on Hard, mid-song with a long combo; then a frame just after a miss
	a->demo = false; a->diff = DIFF_HARD; a->screen = SCR_PLAY;
	chart_free(&a->chart); chart_load(&a->songs[0], a->diff, &a->chart);
	play_start(&a->play, &a->songs[0], &a->chart, a->diff, true);
	simulate(&a->play, -2400, 61360, 0);
	snap("play_hard");
	{                                                   // 40 ms after a hit that lands on a beat: beam, ring, beat flash
		const Chart *hc = &a->chart; int k = 0;
		const Song *so = &a->songs[0];
		for (int i = 0; i < hc->n && !k; i++)
			if (hc->notes[i].t > 61500 && hc->notes[i].len == 0)
				for (int b = 0; b < so->nbeats; b++) if (abs(so->beats[b] - hc->notes[i].t) < 3) { k = i; break; }
		Play saved = a->play;
		simulate(&a->play, 61360, hc->notes[k].t + 40, 0);
		snap("play_hit");
		a->play = saved;
	}
	simulate(&a->play, 61360, 70350, 7);
	snap("play_miss");

	// lead-in
	play_start(&a->play, &a->songs[0], &a->chart, a->diff, true);
	simulate(&a->play, -2400, -1200, 0);
	snap("leadin");

	// Normal with a hold being held
	a->diff = DIFF_NORMAL;
	chart_free(&a->chart); chart_load(&a->songs[0], a->diff, &a->chart);
	play_start(&a->play, &a->songs[0], &a->chart, a->diff, true);
	int holdT = 0;
	for (int i = 0; i < a->chart.n; i++) if (a->chart.notes[i].len > 600 && a->chart.notes[i].t > 30000) { holdT = a->chart.notes[i].t; break; }
	simulate(&a->play, -2400, holdT + 250, 0);
	snap("play_hold");

	a->screen = SCR_PAUSE; a->menuSel = 1; snap("pause");

	// results
	simulate(&a->play, holdT + 250, a->songs[0].length_ms + 1500, 23);
	a->screen = SCR_RESULTS; a->screenT = 2; a->newBest = true; a->time = 0.2f;
	snap("results");

	// ---- Get Songs, with a pretend PC library ----
	a->screen = SCR_GETSONGS; a->demo = false; a->gsConfirmDelete = -1; a->menuSel = 1;
	memset(&a->play, 0, sizeof a->play);
	state = NET_SEARCHING; startMs = now_ms() - 8000;
	snap("getsongs_search");
	static const char *T[][3] = { { "alv", "ALV", "" }, { "blinding-lights", "Blinding Lights", "The Weeknd" },
	                              { "levitating", "Levitating", "Dua Lipa" }, { "mr-brightside", "Mr. Brightside", "The Killers" },
	                              { "september", "September", "Earth, Wind & Fire" }, { "take-on-me", "Take On Me", "a-ha" },
	                              { "uptown-funk", "Uptown Funk (feat. Bruno Mars)", "Mark Ronson" } };
	nsongs = 7;
	for (int i = 0; i < 7; i++) {
		memset(&songs[i], 0, sizeof songs[i]);
		snprintf(songs[i].id, 48, "%s", T[i][0]); snprintf(songs[i].title, 64, "%s", T[i][1]); snprintf(songs[i].artist, 64, "%s", T[i][2]);
		songs[i].bytes = 6500000 + i * 400000; snprintf(songs[i].version, 24, "1");
	}
	state = NET_CONNECTED; snprintf(serverName, sizeof serverName, "fedora (192.168.1.101)");
	a->ngs = 0;
	a->gs[a->ngs++] = (GsItem){ 0, 0, GS_INSTALLED };
	for (int i = 1; i < 7; i++) a->gs[a->ngs++] = (GsItem){ i, -1, i == 3 ? GS_UPDATE : GS_NEW };
	job = JOB_DOWNLOAD; progress = 0.42f; a->gsGetAll = true;
	snprintf(status, sizeof status, "Blinding Lights  3.0 / 7.2 MB");
	snap("getsongs");
	job = JOB_NONE;

	return 0;
}
