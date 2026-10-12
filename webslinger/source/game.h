// Web Slinger - game state, shared by the game logic, the scene builder, the HUD and the host tools (no 3DS dependencies here).
#pragma once
#include <stdbool.h>
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define DEG(x) ((x) * PI_F / 180.0f)

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

enum { MAT_WHITE, MAT_ROAD, MAT_GLASS, MAT_CREAM, MAT_RED, MAT_TAN, MAT_COPPER, MAT_ROOF, MAT_GRASS, MAT_WATER, MAT_HERO, MAT_GLOW, MAT_CLOUD, MAT_STORE, MAT_COUNT };

// ---- the city: NB x NB blocks, one road texture tile (PITCH metres) per block --------------------------------------------
#define NB        15
#define PITCH     64.0f
#define CITY      (NB * PITCH)
#define MAX_BOX   2000
#define MAX_CAR   56
#define MID       (NB / 2)                   // the centre block (landmark tower)
#define BLOCK_LO  0.2f                       // buildings stand between 20% and 80% of the tile (the rest is street and pavement)
#define BLOCK_HI  0.8f
#define WATER_Y   (-3.0f)
#define FLOOR_H   3.8f                       // metres per storey (the facade textures repeat every two storeys)

enum { BK_BLDG, BK_TOWER, BK_DOME };         // building tier, water tower, dome
enum { RF_FLAT, RF_PYRAMID, RF_SPIRE };
typedef struct {
	float x0, y0, z0, x1, y1, z1;
	float yb;                                // where the visible wall starts (top of the tier below; the collision box always starts lower)
	float tint[3];
	unsigned char mat, kind, roof;
	short bld;                               // boxes of one building are consecutive and share this id
} Box;

typedef struct {
	Box box[MAX_BOX]; int nbox, nbld;
	int bfirst[NB][NB], bcount[NB][NB];      // boxes per block (all boxes of a block are consecutive)
	bool park[NB][NB];
} World;

typedef struct { bool hitx, hitz, head, landed; float nx, nz; } Contact;

void  city_generate(World *w, unsigned seed);
float city_ray(const World *w, const float o[3], const float d[3], float tmax, float n[3]);   // distance to the first box hit, or -1
bool  city_overlap(const World *w, const float p[3]);                                          // is the player's body at feet position p inside a box?
void  city_move(const World *w, float p[3], const float d[3], Contact *c);                     // move the player body with collision (axis by axis)
float city_ground_below(const World *w, const float p[3]);                                     // height of the surface under p (0 street, roof top, WATER_Y when over water)
unsigned rnd_next(unsigned *s);
float rndf(unsigned *s);

#define BODY_HW 0.4f
#define BODY_H  1.8f
#define TRICK_DUR 0.8f
#define ROLL_DUR  0.65f

// ---- game -----------------------------------------------------------------------------------------------------------------
typedef struct {
	float lx, ly;                            // circle pad: move / steer
	float cx, cy;                            // C-stick / d-pad: camera
	float tdx, tdy;                          // touch drag on the bottom screen (pixels this frame)
	bool swing, sprint, dive, a_held;        // held: R, L, B, A
	bool a_rel;                              // A released this frame
	bool jump_p, zip_p, recenter_p, start_p, select_p, a_p, b_p, up_p, down_p, touch_p;
	float tx, ty;                            // touch position (valid when touch_p)
} Input;

enum { ST_GROUND, ST_AIR, ST_WALL };
typedef struct {
	float p[3], v[3];                        // feet position, velocity
	int state;
	bool rope, zip; float anchor[3], avis[3], ropeLen, webT, zipT; int hand;
	float zipN[2], zipOut;                   // zip target wall normal (x, z); time left to boost right after a zip ends
	float fireCd, retryT, wallCd;
	float charge, boostWin, aTapT, trickT;   // jump charge 0..1; time left to boost after a web lets go; double-tap window; trick time left
	int trickType, trickCount;               // 1 front flip, 2 back flip, 3 roll left, 4 roll right
	float wn[2];                             // wall normal (pointing out of the wall) while clinging
	float face;                              // body heading (yaw, same convention as the camera)
	float phase;                             // run / climb animation phase
	float speed;                             // smoothed total speed
	float rollT, rollDir[2];                 // landing roll: time left, heading
	float airT, swingT, landT, noAnchorT, lean;
	int stall, bumps; float wallPush;
	float aimDir[3];                         // last heading used to pick an anchor
} Player;

typedef struct { float eye[3], yaw, pitch, dist, fov, manualT; } Cam;
typedef struct { bool on; float p[3], t; } Beacon;
typedef struct { float along, speed, off; int axis, line; float col[3]; } Car;
typedef struct { char text[40]; float t; unsigned col; } Popup;

enum { GS_TITLE, GS_PLAY, GS_OVER };
enum { MODE_FREE, MODE_RUN };

typedef struct {
	World w;
	Player p; Cam cam; Beacon bc;
	Car car[MAX_CAR];
	Popup pop[3];
	int state, mode; bool paused, showHelp;
	float time, timeLeft, stateT;
	int score, best, chain, beacons, menuSel;
	float chainT, flown, topSpeed, hintT;
	unsigned rng;
	float sens; bool invertY, showFps, lowq; float fps;
	bool quit, saveNeeded;
} Game;

void game_init(Game *g, bool lowq);
void game_start(Game *g, int mode);                         // new run (spawns the player in the street)
void game_update(Game *g, const Input *in, float dt);
void game_title_camera(Game *g, float dt);
void game_popup(Game *g, const char *text, unsigned col);
void game_respawn(Game *g);
void game_bot(Game *g, Input *in, float dt, const float *target);   // autopilot: swings toward target (x, ., z) or between random waypoints
