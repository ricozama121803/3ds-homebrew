// BeatPad - app state shared by main.c and the renderer, and the draw entry points
#pragma once
#include "game.h"

enum { SCR_SELECT, SCR_SETTINGS, SCR_PLAY, SCR_PAUSE, SCR_RESULTS, SCR_NOSONGS, SCR_GETSONGS };
enum { GS_NEW, GS_UPDATE, GS_INSTALLED, GS_LOCAL_ONLY };

// one row of the Get Songs list: a song on the PC, on the 3DS, or both
typedef struct { int remote, local, status; } GsItem;
enum { SET_SPEED, SET_OFFSET, SET_NOFAIL, SET_BACK, SET_COUNT };

typedef struct { int score; float acc; char grade[4]; bool fc, played; } Best;

typedef struct {
	int screen;
	float time;                    // seconds since boot (animation)
	float screenT;                 // seconds spent on the current screen
	Song songs[MAX_SONGS];
	int nsongs, sel, diff;
	Best best[MAX_SONGS][NUM_DIFFS];
	float speed;                   // note speed (1.0 - 4.0)
	int offset;                    // audio offset (ms): positive = notes later
	bool noFail;
	int menuSel;                   // cursor on pause / settings
	Play play;                     // the song being played (or the menu's autoplay demo)
	Chart chart;
	bool demo;                     // play is the menu's autoplay preview
	bool newBest;
	bool audioOk;
	float displayScore;            // rolling score counter
	float leadIn;                  // ms of lead-in before the audio starts
	// Get Songs
	GsItem gs[MAX_SONGS + 64];
	int ngs, gsScroll;
	bool gsGetAll;                 // X: keep downloading everything new
	int gsConfirmDelete;           // row waiting for a second SELECT (-1 none)
	uint32_t lastIp;               // last PC that served songs (network byte order)
} App;

void render_init(void);
void render_exit(void);
void render_begin(const App *a);                       // once per frame, before the screens
void render_top(const App *a, float eye, float slider); // eye: -1 left, +1 right
void render_bottom(const App *a);
float render_lookahead_ms(const App *a);
