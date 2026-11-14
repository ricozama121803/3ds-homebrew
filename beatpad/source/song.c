// Loads songs from <root>/songs/<dir>/ : info.txt, beats.txt and <difficulty>.chart (see tools/make_song.py)
#include "game.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *DIFF_FILES[NUM_DIFFS] = { "easy", "normal", "hard" };

const char *diff_name(int d) { static const char *n[] = { "EASY", "NORMAL", "HARD" }; return n[d]; }
uint8_t diff_lanes(int d) { return d == DIFF_EASY ? (1 << LANE_DLEFT) | (1 << LANE_DRIGHT) | (1 << LANE_Y) | (1 << LANE_A) : 0x3f; }

static void trim(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

static bool load_info(const char *root, Song *s)
{
	char path[256], line[160];
	snprintf(path, sizeof path, "%s/songs/%s/version.txt", root, s->dir);
	FILE *vf = fopen(path, "r");
	if (vf) { if (fgets(s->version, sizeof s->version, vf)) trim(s->version); fclose(vf); }
	snprintf(path, sizeof path, "%s/songs/%s/info.txt", root, s->dir);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	snprintf(s->title, sizeof s->title, "%s", s->dir);
	s->artist[0] = 0;
	s->bpm = 120;
	while (fgets(line, sizeof line, f)) {
		trim(line);
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		const char *k = line, *v = eq + 1;
		if (!strcmp(k, "title")) snprintf(s->title, sizeof s->title, "%s", v);
		else if (!strcmp(k, "artist")) snprintf(s->artist, sizeof s->artist, "%s", v);
		else if (!strcmp(k, "bpm")) s->bpm = (float)atof(v);
		else if (!strcmp(k, "length_ms")) s->length_ms = atoi(v);
		else if (!strcmp(k, "preview_ms")) s->preview_ms = atoi(v);
		else for (int d = 0; d < NUM_DIFFS; d++) {
			char key[32];
			snprintf(key, sizeof key, "notes_%s", DIFF_FILES[d]);
			if (!strcmp(k, key)) s->noteCount[d] = atoi(v);
		}
	}
	fclose(f);

	snprintf(path, sizeof path, "%s/songs/%s/beats.txt", root, s->dir);
	f = fopen(path, "r");
	s->nbeats = 0; s->beats = NULL; s->downbeat = NULL;
	if (f) {
		int cap = 512, t, d;
		s->beats = malloc(cap * sizeof *s->beats);
		s->downbeat = malloc(cap);
		while (fscanf(f, "%d %d", &t, &d) == 2) {
			if (s->nbeats == cap) {
				cap *= 2;
				s->beats = realloc(s->beats, cap * sizeof *s->beats);
				s->downbeat = realloc(s->downbeat, cap);
			}
			s->beats[s->nbeats] = t; s->downbeat[s->nbeats] = (uint8_t)d; s->nbeats++;
		}
		fclose(f);
	}
	return true;
}

// songs already in the list (callers pass out[] zeroed past the end of the list)
static int n0(const Song *out, int max) { int n = 0; while (n < max && out[n].dir[0]) n++; return n; }

int songs_scan(const char *root, Song *out, int max)
{
	char path[256];
	snprintf(path, sizeof path, "%s/songs", root);
	DIR *dir = opendir(path);
	if (!dir) return n0(out, max);
	struct dirent *e;
	int start = n0(out, max), n = start;
	while (n < max && (e = readdir(dir))) {
		if (e->d_name[0] == '.' || strstr(e->d_name, ".part")) continue;     // .part = unfinished download
		Song *s = &out[n];
		if (strlen(e->d_name) >= sizeof s->dir) continue;
		bool dup = false;
		for (int i = 0; i < start; i++) if (!strcmp(out[i].dir, e->d_name)) dup = true;
		if (dup) continue;
		memset(s, 0, sizeof *s);
		snprintf(s->root, sizeof s->root, "%s", root);
		memcpy(s->dir, e->d_name, strlen(e->d_name) + 1);
		char ap[200];                                                    // a song needs its audio to be playable
		snprintf(ap, sizeof ap, "%s/songs/%s/audio.bpa", root, s->dir);
		FILE *af = fopen(ap, "rb");
		if (!af) { snprintf(ap, sizeof ap, "%s/songs/%s/audio.pcm", root, s->dir); af = fopen(ap, "rb"); }
		if (!af) continue;
		fclose(af);
		if (load_info(root, s)) n++;
	}
	closedir(dir);
	// alphabetical by title
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && strcmp(out[j - 1].title, out[j].title) > 0; j--) {
			Song tmp = out[j]; out[j] = out[j - 1]; out[j - 1] = tmp;
		}
	return n;
}

bool song_delete(const Song *s)
{
	if (strncmp(s->root, "sdmc:", 5)) return false;             // built-in songs stay
	char dir[160], p[512];
	snprintf(dir, sizeof dir, "%s/songs/%s", s->root, s->dir);
	DIR *d = opendir(dir);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
			remove(p);
		}
		closedir(d);
	}
	return rmdir(dir) == 0;
}

void song_free(Song *s)
{
	free(s->beats); free(s->downbeat);
	s->beats = NULL; s->downbeat = NULL; s->nbeats = 0;
}

bool chart_load(const Song *s, int diff, Chart *c)
{
	char path[256];
	snprintf(path, sizeof path, "%s/songs/%s/%s.chart", s->root, s->dir, DIFF_FILES[diff]);
	memset(c, 0, sizeof *c);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	int cap = 1024, t, lane, len;
	c->notes = malloc(cap * sizeof *c->notes);
	while (fscanf(f, "%d %d %d", &t, &lane, &len) == 3) {
		if (lane < 0 || lane >= NUM_LANES) continue;
		if (c->n == cap) { cap *= 2; c->notes = realloc(c->notes, cap * sizeof *c->notes); }
		Note *nt = &c->notes[c->n++];
		memset(nt, 0, sizeof *nt);
		nt->t = t; nt->len = len > 0 ? len : 0; nt->lane = (uint8_t)lane; nt->judge = J_NONE;
		c->laneMask |= 1 << lane;
		if (t + nt->len > c->lastEnd) c->lastEnd = t + nt->len;
	}
	fclose(f);
	c->laneMask |= diff_lanes(diff);
	return c->n > 0;
}

void chart_free(Chart *c)
{
	free(c->notes);
	c->notes = NULL; c->n = 0;
}
