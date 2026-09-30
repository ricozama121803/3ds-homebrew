// Tuffy Run - game logic (no 3DS dependencies, so it can be tested on a PC)
#pragma once
#include <stdbool.h>

#define PLAYER_Z   5.0f    // distance of Tuffy from the camera
#define SPAWN_Z    70.0f   // where new obstacles appear
#define LANE_W     1.7f    // width of one lane in world units
#define MAX_OBJS   240
#define STAND_H    1.7f    // Tuffy's height when running
#define SLIDE_H    0.75f   // ...and when sliding

#define SCOOTER_SPEED 13.0f  // how fast a scooter rider overtakes Tuffy (relative to the camera)
#define SCOOTER_WARN  1.7f   // seconds of warning before one reaches Tuffy

// Events: now and then the world does something special. Only one is ever scheduled at a time, and
// everything about it is a function of distance run (s = dist + z), so logic and drawing agree.
//  EV_TURN:   the whole road gradually bends through 90 degrees (left or right, `evSide`). Once it has turned,
//             two more lanes are added (5 lanes: -2..2) for a stretch, then it narrows back to 3.
//  EV_DISNEY: a fake theme-park zone (same 3 lanes, different scenery).
//  EV_STADIUM: a parody of Angel Stadium. You run under a big red A-frame, through the stadium gate onto a
//             single lane, round the bases (1st, 2nd and 3rd are 90 degree left turns), and at home plate you are
//             launched up and out of the stadium, landing back on the normal 3 lane road.
#define TURN_LEN       170.0f   // distance over which the road swings through TURN_ANGLE
#define TURN_ANGLE     1.5708f  // 90 degrees
#define TURN_WIDE_A    200.0f   // the two extra lanes are usable from here (they widen into view just before)...
#define TURN_WIDE_B    330.0f   // ...until here, where they taper away and Tuffy is nudged into the road
#define TURN_CALM_A    225.0f   // obstacles use the extra lanes only between CALM_A and CALM_B; before that they just hold
#define TURN_CALM_B    300.0f   // coins (time to move across), and after CALM_B nobody gets nudged into a rock
#define TURN_TOTAL     350.0f   // length of the whole event
#define TURN_VIS_GAIN    1.5f   // how much the drawn bend is exaggerated (looks only, no effect on play)
#define DIZZY_LEN      420.0f
#define DIZZY_FADE      40.0f   // scenery blends in and out over this distance

// positions inside a stadium event, in distance from its start
#define STAD_BIGA        70.0f   // the red A-frame stands over the road here
#define STAD_NARROW_A   150.0f   // the side lanes stop here and the road narrows to one lane...
#define STAD_GATE       170.0f   // ...by the stadium gate
#define STAD_BASE1      260.0f   // 1st base: the road turns left through 90 degrees around each base
#define STAD_BASE_GAP    90.0f   // distance from base to base (2nd is BASE1 + GAP, 3rd + 2*GAP)
#define STAD_TURN_LEN    45.0f
#define STAD_HOME       530.0f   // home plate: the launch
#define STAD_FLIGHT      70.0f   // distance flown
#define STAD_FLIGHT_H     6.0f   // how high Tuffy goes
#define STAD_LAND       600.0f   // = HOME + FLIGHT
#define STAD_WIDE       610.0f   // the side lanes are back from here
#define STAD_TOTAL      640.0f

typedef enum { EV_NONE, EV_TURN, EV_DISNEY, EV_STADIUM } EvKind;

// OBJ_SCOOTER is a rider coming up from BEHIND Tuffy: he can't be jumped or slid under, only dodged.
// His z starts far behind the camera (negative) and increases; everything else's z decreases.
// OBJ_PETITION is someone holding out a giant clipboard: he stands still, and you have to jump him.
typedef enum { OBJ_COIN, OBJ_ROCK, OBJ_HURDLE, OBJ_BEAM, OBJ_SCOOTER, OBJ_PETITION } ObjType;

typedef struct {
	ObjType type;
	int lane;      // -1, 0, 1
	float z;       // distance ahead of the camera
	float y;       // height above ground (used by coins)
} Obj;

typedef enum { ST_TITLE, ST_PLAY, ST_PAUSED, ST_DEAD } GState;
typedef enum { ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_SLIDE } Action;

typedef struct {
	GState state;

	// player
	int lane;
	float x, y, vy;
	float slideT;          // seconds of slide remaining
	bool pendingSlide;     // slide requested while airborne
	float runPhase;        // drives the leg/arm animation
	bool flying;           // being launched out of the stadium

	// world
	float speed, dist, time;
	float nextRow;         // distance until the next obstacle row spawns
	float nextScooterAt;   // game time when the next scooter rider may be scheduled
	EvKind evKind;         // the current or upcoming event (distances are in dist units)
	float evStart, evEnd;
	int evSide;            // turns: -1 left / +1 right
	float baseHeading;     // radians: heading accumulated by the turns already finished (positive = right)
	Obj objs[MAX_OBJS];
	int nobjs;

	// score
	int coins, best;
	bool newBest, saveNeeded;
	float deadT;
	ObjType killer;

	unsigned rng;
} Game;

void game_init(Game *g, int best, unsigned seed);
void game_start(Game *g);             // begin a run (from title or after crashing)
void game_update(Game *g, float dt);
void game_action(Game *g, Action a);
int  game_score(const Game *g);

// Is a scooter about to overtake Tuffy? Gives the lane it will use and the seconds until it arrives.
bool game_scooter_warning(const Game *g, int *lane, float *eta);

// ---- track layout ----
// The road bends by shifting everything sideways by an amount that depends on how far ahead it is.
float track_curve(const Game *g, float z);          // sideways offset (world units) of the road's centre z ahead of the camera
float track_heading(const Game *g);                 // radians the camera has turned so far (positive = right)
float track_wide(const Game *g, float s);           // 0..1: how much of the extra lane on each side is visible at distance s
bool  lane_exists(const Game *g, int lane, float s);   // lanes are -1..1, or -2..2 while the road is wide
float zone_k(const Game *g, float s);               // 0..1: how much of the fake theme park is here
float stadium_k(const Game *g, float s);            // 0..1: how much of the baseball stadium is here
float track_narrow(const Game *g, float s);         // 0..1: how far the road has narrowed to a single lane
bool  track_bent(const Game *g);                    // is any of the road in view bending?
float flight_y(const Game *g, float s);             // Tuffy's height if he is being launched at distance s, else -1
