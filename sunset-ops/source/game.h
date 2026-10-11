// Sunset Ops - game state and simulation (no 3DS dependencies: the PC preview/tests compile this file as is).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define PI_F 3.14159265f
#define DEG(x) ((x) * PI_F / 180.0f)
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// world: x east, y up, z south. The arena: x in [-ARENA_X, ARENA_X], z in [-ARENA_Z, ARENA_Z]; the walled compound is a union of rectangles (MAP_RECTS), desert all around it.
#define ARENA_X 44
#define ARENA_Z 24
#define GRID_W (ARENA_X * 2)
#define GRID_H (ARENA_Z * 2)

enum { MAT_WHITE, MAT_ASPHALT, MAT_DECK, MAT_CONCRETE, MAT_PANEL, MAT_WINDOW, MAT_STUCCO, MAT_CONTAINER, MAT_WOOD, MAT_ROOF, MAT_GRASS, MAT_SAND, MAT_GLOW, MAT_CLOUD, MAT_CHARS, MAT_COUNT };

#define MAX_BOX 2000
#define MAX_ACT 8
#define MAX_NAV 44
#define MAX_PICK 12
#define MAX_GREN 8
#define MAX_FX 220
#define MAX_TRACER 40
#define MAX_DECAL 48
#define MAX_SHELL 8
#define MAX_FEED 5
#define MAX_SPAWN 12
#define MAX_HITS 4

typedef struct {
	float x0, y0, z0, x1, y1, z1;
	uint8_t side, top;            // materials of the four walls and of the top face
	bool solid;                   // blocks movement and bullets (false: decoration such as window bands)
	float r, g, b;                // tint
	float tile;                   // metres per texture repeat
	uint8_t shape;                // 0 box, 1 rounded box (bev = corner radius), 2 cylinder along y, 3 along x, 4 along z, 5 ellipsoid (all fill the box's extents)
	float bev;
} Box;

typedef struct { float x, z; } Pt;

enum { W_AR, W_SMG, W_PISTOL, W_SHOTGUN, W_COUNT };
typedef struct {
	const char *name, *mode;
	bool automatic;
	int mag, reserve, pellets;
	float dmg, rate, reload, spread_hip, spread_ads, kick, ads_fov, range0, range1, zoomsens;
} WeaponDef;
extern const WeaponDef weapons[W_COUNT];

enum { TEAM_ALLY, TEAM_ENEMY };
enum { AS_PATROL, AS_FIGHT };

typedef struct {
	float x, z, yaw, pitch;
	float vx, vz;
	float hp; int team; bool alive;
	float deadT, respawnT, invulnT;
	int weapon;
	bool frozen; float stillT, fireT, burstT, reactT, thinkT, stuckT, strafeT, strafeDir, grenT, hesitate;
	int burstLeft, target, wp, callsign;
	float walk, flashT, hitT, spottedT;
	int kills, deaths, lastAttacker;
	float lastX, lastZ;
} Actor;

typedef struct {                      // everything only the human player has
	float eyeh;                       // eye height (drops when dead)
	int weapon, nextWeapon;
	int mag[W_COUNT], reserve[W_COUNT];
	int grenades, medkits;
	float ads, kick, kickYaw, reloadT, swapT, healT, healLeft, throwT, shake, bob, sway[2], flashT, hitMarkT, killMarkT, lastDmgT, moveFrac, lookVel[2];
	bool fireHeld, fwdPrev, sprinting; float fireBuf, tapT, sprint;
	float dmgDir[MAX_HITS]; float dmgT[MAX_HITS];
	bool reloading;
	int streak;
} Player;

typedef struct { float p[3], v[3], t; int owner; bool live; } Grenade;
typedef struct { float p[3], v[3], life, max, size, r, g, b, grow; uint8_t kind; } Fx;       // kind: 0 additive glow, 1 smoke (alpha), 2 debris
typedef struct { float a[3], b[3], life; } Tracer;
typedef struct { float p[3], n[3], life; } Decal;
typedef struct { float p[3], v[3], life, rot; } Shell;
typedef struct { float x, z; int type; float respawn; } Pickup;      // type 0 ammo, 1 medkit, 2 grenades
typedef struct { char text[40]; float t; unsigned col; } Popup;
typedef struct { int killer, victim, weapon; float t; } Feed;

enum { GS_TITLE, GS_PLAY, GS_OVER };

typedef struct {
	bool fwd, back, left, right;          // X, B, Y, A
	bool fire, aim;                       // R, L
	float lx, ly;                         // circle pad, -1..1 (ly: up is positive)
	bool next_p, prev_p, grenade_p, heal_p, reload_p, start_p, a_p, b_p, x_p, y_p, r_p, l_p;
} Input;

typedef struct {
	Box box[MAX_BOX]; int nbox;
	int nsolid; int solid_idx[MAX_BOX];
	Pt nav[MAX_NAV]; int nnav;
	Pt spawn[MAX_SPAWN]; int nspawn;
	Pickup pick[MAX_PICK]; int npick;
} World;

typedef struct {
	World w;
	uint16_t navd[MAX_NAV][GRID_H][GRID_W];          // steps to each nav point from every free cell (0xFFFF: unreachable)
	uint8_t free_cell[GRID_H][GRID_W];
	int state; bool paused;
	Actor a[MAX_ACT];                                // 0 = the human player (ally team); 1..3 allies; 4..7 enemies
	Player p;
	Grenade gren[MAX_GREN]; Fx fx[MAX_FX]; Tracer tr[MAX_TRACER]; Decal dec[MAX_DECAL]; Shell shell[MAX_SHELL];
	int fx_n, dec_n, tr_n, shell_n;
	float score[2], timeLeft, time, overT;
	int winner;                                      // -1 draw, 0 allies (you), 1 enemies
	Popup pop[3]; Feed feed[MAX_FEED];
	unsigned rng;
	struct { float eye[3], yaw, pitch, roll, fov; } cam;
	float fovHip;
	bool quit, autoplay, saveNeeded, invertY, aimAssist;
	int quality, qualityActive;                      // 0 low, 1 high: the chosen setting, and the one the level was built with (changes apply at launch)
	bool showFps, noHaze; float fps;
	float sens;                                      // look speed multiplier (pause menu)
	int bestKills;
} Game;

#define KILLS_TO_WIN 30
#define MATCH_TIME 480.0f
#define PLAYER_R 0.42f
#define EYE_H 1.62f

void game_init(Game *g, int quality);                             // builds the map + navigation (slow: once at start)
void game_new_match(Game *g);
void game_update(Game *g, const Input *in, float dt);
float frand(Game *g);                                // 0..1
const char *callsign(int id);
void map_build(World *w, int high);                            // map.c
extern const float MAP_RECTS[][4];                              // the compound: union of x0,z0,x1,z1 rectangles
extern const int MAP_NRECT;
int map_ground(float x, float z);                               // ground material at a point (MAT_ASPHALT, MAT_CONCRETE, MAT_GRASS, MAT_DECK, MAT_SAND)
bool world_blocked(const World *w, float x, float z, float r);
float ray_world(const World *w, const float o[3], const float d[3], float maxd, float n[3]);   // distance to first solid box (maxd if none); fills the surface normal
