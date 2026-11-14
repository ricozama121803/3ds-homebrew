// BeatPad - shared game state (pure logic: no 3DS calls, so the PC harness can run it)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NUM_LANES   6
#define MAX_SONGS   64
#define NUM_DIFFS   3
#define MAX_SPARKS  128
#define MAX_RINGS   12

// lanes, left to right: D-pad Left, D-pad Up, D-pad Right | Y, X, A
enum { LANE_DLEFT, LANE_DUP, LANE_DRIGHT, LANE_Y, LANE_X, LANE_A };
enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD };
enum { J_PERFECT, J_GREAT, J_GOOD, J_MISS, J_NONE };
enum { NS_PENDING, NS_HIT, NS_HOLDING, NS_DONE, NS_DROPPED, NS_MISSED };

// timing windows (ms either side of the note)
#define WIN_PERFECT 45
#define WIN_GREAT   85
#define WIN_GOOD    125
#define HOLD_GRACE  110    // a hold released this close to its end still counts as complete

typedef struct {
	int t, len;            // ms from the start of the audio; hold length (0 = tap)
	uint8_t lane, state, judge;
	int16_t offset;        // hit time - note time (ms), for the timing stats
	float tailScore;       // points earned so far on a hold
} Note;

typedef struct {
	Note *notes;
	int n;
	uint8_t laneMask;      // which lanes this chart uses
	int lastEnd;           // end time of the last note (ms)
} Chart;

typedef struct {
	char root[32];         // "romfs:" (built in) or "sdmc:/3ds/beatpad" (downloaded)
	char dir[48];          // folder name under <root>/songs/
	char version[24];      // from version.txt (downloaded songs): lets Get Songs spot updates
	char title[64], artist[64];
	float bpm;
	int length_ms, preview_ms;
	int noteCount[NUM_DIFFS];
	int nbeats;
	int *beats;            // beat times (ms)
	uint8_t *downbeat;
} Song;

typedef struct { float x, y, vx, vy, life, max; uint32_t color; uint8_t lane; } Spark;   // x, y: px from the lane's target
typedef struct { float t; uint8_t lane, perfect; } HitRing;                             // t: 0 -> 1 as it expands

typedef struct {
	const Song *song;
	Chart chart;
	int diff;
	float t;               // current song time (ms); negative during the lead-in
	int score, combo, maxCombo, counts[4];
	float health;          // rock meter 0..1
	bool noFail, failed, fullCombo;
	int first;             // first note that may still need attention
	int hitSum, hitN;      // timing stats (sum of offsets of hits)
	// presentation
	float laneFlash[NUM_LANES], lanePress[NUM_LANES];
	uint8_t heldMask;
	int popJudge; float popT;          // judgement popup
	float comboPopT, multPopT, missT;
	int lastMult;
	int missEvents;         // incremented per miss (main plays the miss sound)
	Spark sparks[MAX_SPARKS];
	HitRing rings[MAX_RINGS];
	float beam[NUM_LANES];  // light shooting up the lane after a hit (1 -> 0)
	float pump;             // highway kick on each hit (1 -> 0)
	float fever;            // eases to 1 while the multiplier is x4
	float feverPopT;        // "FEVER!" banner when it starts
	float flashT;           // white flash on streak milestones
	float scorePop;         // bottom-screen score bump
	unsigned rng;
} Play;

extern const uint32_t LANE_RGB[NUM_LANES];   // 0xRRGGBB per lane

// ---- song data (song.c) ----
int  songs_scan(const char *root, Song *out, int max);          // appends songs found under <root>/songs, skipping ids in out[0..n); returns count
bool chart_load(const Song *s, int diff, Chart *c);
void chart_free(Chart *c);
void song_free(Song *s);
bool song_delete(const Song *s);          // removes a downloaded song from the SD card

// ---- play logic (game.c) ----
void play_start(Play *p, const Song *s, Chart *c, int diff, bool noFail);
// pressed/held: lane bitmasks for this frame; t: song time in ms; dt: seconds
void play_update(Play *p, float t, uint8_t pressed, uint8_t held, float dt);
int  play_multiplier(const Play *p);
float play_accuracy(const Play *p);       // 0..100
int  play_resolved(const Play *p);        // notes judged so far
const char *play_grade(const Play *p);
bool play_done(const Play *p);            // every note judged and holds finished
// autoplay bot: the lanes a perfect player would press / hold at time t
void play_autoplay(const Play *p, float t, uint8_t *pressed, uint8_t *held);
const char *diff_name(int d);
uint8_t diff_lanes(int d);
