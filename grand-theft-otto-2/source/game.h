// Grand Theft Otto II - game state, shared by the game logic, the scene builder, the HUD and the host tools (no 3DS dependencies here).
#pragma once
#include <stdbool.h>
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define DEG(x) ((x) * PI_F / 180.0f)

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float wrapang(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

enum { MAT_WHITE, MAT_ROAD, MAT_GLASS, MAT_CREAM, MAT_RED, MAT_TAN, MAT_COPPER, MAT_ROOF, MAT_GRASS, MAT_WATER, MAT_GLOW, MAT_CLOUD, MAT_STORE, MAT_SAND, MAT_PLANK, MAT_COUNT };

// ---- the city: NB x NB blocks, one road texture tile (PITCH metres) per block --------------------------------------------
#define NB        8
#define PITCH     64.0f
#define CITY      (NB * PITCH)
#define MAX_BOX   900
#define MAX_CAR   56
#define MAX_PED   56
#define MAX_PAD   4
#define MAX_BULLET 48
#define MAX_PART  120
#define MAX_DECAL 28
#define MAX_PICKUP 90
#define MAX_BLAST 6
#define BLOCK_LO  0.2f                       // buildings stand between 20% and 80% of the tile (the rest is street and pavement)
#define BLOCK_HI  0.8f
#define ROAD_HALF 11.0f                      // the street (kerb to kerb) is this far either side of a block-grid line
#define WATER_Y   (-3.0f)
#define BEACH_X   (-66.0f)                   // west edge of the sand (the sea starts just beyond)
#define PIER_X    (-338.0f)                  // far end of the pier
#define PIER_Z0   250.0f                     // the pier is 12 m wide, centred on the middle of the city
#define PIER_Z1   262.0f
#define FLOOR_H   3.4f                       // metres per storey (the facade textures repeat every two storeys)

enum { BK_BLDG, BK_TOWER, BK_DOME, BK_MAST };                           // BK_MAST: a lattice radio mast
enum { RF_FLAT, RF_PYRAMID, RF_SPIRE, RF_TILE };                         // RF_TILE: a hip roof of red tiles
typedef struct {
	float x0, y0, z0, x1, y1, z1;
	float yb;                                // where the visible wall starts (top of the tier below; the collision box always starts lower)
	float tint[3];
	unsigned char mat, kind, roof;
	short bld;                               // boxes of one building are consecutive and share this id
} Box;

// ---- regions: separate towns of the same size (8 x 8 blocks), one resident at a time; you cross between them on a gate road (or by air) with a fade ----
enum { REG_CITY, REG_AIRPORT, REG_HILLS, REG_COUNT };
typedef struct { const char *name; unsigned seed; float haze[3], skyHor[3], skyMid[3], skyTop[3], road[3], dry[3]; int ai, peds, parked; } RegionDef;
static const RegionDef REGIONS[REG_COUNT] = {
	{ "LOS OTTO",     0x20C0FFEEu, { 0.80f, 0.88f, 0.97f }, { 0.80f, 0.88f, 0.97f }, { 0.46f, 0.67f, 0.94f }, { 0.18f, 0.40f, 0.82f }, { 0.92f, 0.92f, 0.92f }, { 1.00f, 0.80f, 0.44f }, 14, 32, 17 },
	{ "OTTO AIRPORT", 0x41495250u, { 0.80f, 0.86f, 0.93f }, { 0.82f, 0.87f, 0.93f }, { 0.52f, 0.68f, 0.90f }, { 0.22f, 0.42f, 0.78f }, { 0.80f, 0.83f, 0.88f }, { 0.52f, 0.62f, 0.42f }, 9, 20, 12 },
	{ "SUNSET HILLS", 0x48494C4Cu, { 0.98f, 0.83f, 0.64f }, { 0.99f, 0.80f, 0.58f }, { 0.86f, 0.66f, 0.66f }, { 0.34f, 0.40f, 0.74f }, { 1.00f, 0.86f, 0.70f }, { 1.00f, 0.74f, 0.52f }, 7, 16, 10 } };
// the world map on the touch screen: where each town's tile is, and the two buttons (pixels on the 320 x 240 bottom screen)
static inline void world_tile(int r, float *x, float *y, float *w, float *h) { static const float T[REG_COUNT][4] = { { 84, 24, 70, 70 }, { 84, 118, 70, 70 }, { 176, 24, 56, 70 } }; *x = T[r][0]; *y = T[r][1]; *w = T[r][2]; *h = T[r][3]; }
static inline bool in_box(float px, float py, float x, float y, float w, float h) { return px >= x && px < x + w && py >= y && py < y + h; }
#define MAP_BTN_X 246.0f
#define MAP_BTN_W 68.0f
#define MAP_BTN_H 22.0f
#define MAP_TOGGLE_Y 138.0f
#define MAP_TRAVEL_Y 163.0f
typedef struct { float x0, z0, x1, z1; int dir, dest; } Gate;             // a road out of the town: dir = the way out (0 +x, 1 -x, 2 +z, 3 -z); at its far end the next region loads

// street furniture on the pavements: trash bins, newspaper boxes and dumpsters. Low cover (they do not stop a standing shot, a crouched Otto hides behind them); only people and bullets see them.
#define MAX_PROP 320
typedef struct { float x, z, hx, hz, h; unsigned char type, face; } Prop;      // type 0 bin, 1 newspaper box, 2 dumpster; face 0 +x, 1 -x, 2 +z, 3 -z: the way it faces (the street)

typedef struct {
	Box box[MAX_BOX]; int nbox, nbld;
	Prop prop[MAX_PROP]; int nprop; short pfirst[NB][NB], pcount[NB][NB];
	int region;
	bool field[NB][NB], lot[NB][NB];         // desert ground / tarmac car park instead of a built-up block
	Gate gate[2]; int ngate;
	int nbr[4];                              // the region you reach by flying out of the +x, -x, +z, -z side (or -1)
	float rectF[6][4], rectC[6][4]; int nrectF, nrectC;     // where Otto may walk / a car may drive: x0, z0, x1, z1
	int bfirst[NB][NB], bcount[NB][NB];      // boxes per block (all boxes of a block are consecutive)
	bool park[NB][NB];
	int downtown0, downtown1;                // downtown blocks are i >= downtown0 and j < downtown1 (north-east corner)
} World;

typedef struct { bool hitx, hitz, head, landed; float nx, nz; } Contact;

void  city_generate(World *w, int region);
float city_ray(const World *w, const float o[3], const float d[3], float tmax, float n[3]);   // distance to the first box hit, or -1
void  city_move(const World *w, float p[3], const float d[3], Contact *c);                     // move the on-foot body with collision (axis by axis)
bool  city_push_circle(const World *w, float *x, float *z, float r, float n[2]);               // push a ground circle out of the buildings; true (and the push direction) if it hit
bool  city_push_circle_y(const World *w, float *x, float *z, float r, float ylo, float yhi, float n[2]);   // same, only against buildings that reach into the height range
bool  city_prop_push(const World *w, float *x, float *z, float r, float n[2]);                 // push a ground circle out of the street furniture; true (and the direction) if it hit
float city_prop_seg(const World *w, const float a[3], const float b[3]);                      // 0..1 where a segment first goes through street furniture, or -1
float city_roof_h(const World *w, float x, float z, float r);                                  // top of the highest building under a circle (0 over a street)
// the airport south of the city: Otto can walk or drive onto it from the city's southern edge. Its solid parts (terminal, tower, hangars, parked planes) are AIRBOX boxes.
#define AIR_X0 40.0f
#define AIR_X1 (CITY - 12.0f)
#define AIR_Z0 CITY
#define AIR_Z1 (CITY + 440.0f)
#define RWY1_Z (CITY + 297.0f)                   // the centre lines of the two runways, running east - west
#define RWY2_Z (CITY + 385.0f)
typedef struct { float x0, z0, x1, z1, h; } AirBox;
#define N_AIRBOX 10
static const AirBox AIRBOX[N_AIRBOX] = {
	{ 262, CITY + 88, 436, CITY + 128, 14 },            // terminal
	{ 440, CITY + 92, 452, CITY + 104, 34 },            // control tower
	{ 52, CITY + 90, 132, CITY + 135, 15 },             // hangars
	{ 52, CITY + 150, 132, CITY + 195, 15 },
	{ 456, CITY + 140, 498, CITY + 178, 9 },            // cargo shed
	{ 287.6f, CITY + 149, 292.4f, CITY + 187, 6.5f },   // the four parked airliners (fuselages)
	{ 337.6f, CITY + 149, 342.4f, CITY + 187, 6.5f },
	{ 387.6f, CITY + 149, 392.4f, CITY + 187, 6.5f },
	{ 437.6f, CITY + 149, 442.4f, CITY + 187, 6.5f },
	{ 188.6f, CITY + 62.6f, 189.4f, CITY + 63.4f, 8 } };   // a lamp tower on the plaza
static const float AIR_PLANE_X[4] = { 290.0f, 340.0f, 390.0f, 440.0f };
#define AIR_PLANE_Z (CITY + 168.0f)
bool  world_walkable(const World *w, float x, float z);                    // on foot: the town, its gate roads, and the beach and pier in the city / the airfield at the airport
void  world_keep_car(const World *w, float *x, float *z);                // pull a car back inside where cars may drive
unsigned rnd_next(unsigned *s);
float rndf(unsigned *s);

#define PLAYER_MAX_HP 280.0f
#define BODY_HW 0.4f
#define BODY_H  1.8f

// ---- game -----------------------------------------------------------------------------------------------------------------
typedef struct {
	float lx, ly;                            // circle pad: move / steer
	float cx, cy;                            // C-stick / d-pad: camera
	float tdx, tdy;                          // touch drag on the bottom screen (pixels this frame)
	float dpx, dpy;                          // D-pad as held (-1 / 0 / 1): picks a weapon on the weapon wheel
	bool run, gas, brake, hand, aim;         // held: (unused), R = gas / shoot, B = brake, A or L = handbrake, L = aim / lock on (on foot)
	bool enter_p, a_p, b_p, x_p, start_p, select_p, up_p, down_p, touch_p;
	int weap;                                // touch: weapon slot tapped this frame + 1 (0 = none)
	float tx, ty;
} Input;

enum { W_FISTS, W_PISTOL, W_SMG, W_SHOTGUN, W_BAT, W_RIFLE, W_SNIPER, W_RPG, W_COUNT };        // the weapon wheel has one slice for each, clockwise from the top
static inline float melee_time(int w) { return w == W_BAT ? 0.5f : 0.34f; }                // how long a punch or a bat swing lasts
static inline bool weapon_melee(int w) { return w == W_FISTS || w == W_BAT; }                   // no ammo, hits what is right in front of Otto
#ifndef TEST_LOADOUT
#define TEST_LOADOUT 1                       // testing: Otto starts (and restarts) with every weapon and never runs out of ammo; set to 0 for the real game
#endif
#define TEST_AMMO    999
enum { PS_ALIVE, PS_WASTED, PS_BUSTED };
typedef struct {
	float p[3], v[3];                        // feet position, velocity
	float hp, armor;                         // 100 each at most; armour soaks damage first
	int weapon, ammo[W_COUNT]; bool has[W_COUNT];
	int cash;
	float fireCd, aimT, flashT, punchT, hurtT, lastShotT;     // aimT 0..1: arm raised; flashT: muzzle flash left; punchT: punch left; hurtT: red flash left
	int target;                              // ped being aimed at (auto-aim or the lock), or -1
	int lock;                                // ped locked onto while L is held, or -1
	float lockCd, lockLosT, lockMag;         // time until the lock may switch again, how long the locked ped has been out of sight, the stick's length last frame
	int ps; float psT;                       // alive / wasted / busted, and for how long
	float arrestT;                           // how long a cop has had hold of Otto
	float aimYaw;
	int punchSide;                           // alternates: right jab, left cross
	bool punchHit;                           // the punch has not landed yet (it does when the fist is fully out)
	int cover;                               // 0 in the open, 1 hugging a wall, 2 crouched behind a car (Y)
	int coverCar;                            // the car that is the cover, or -1 for a wall
	float coverX, coverZ, coverNx, coverNz;  // where Otto hides, and the direction from the cover toward him (he faces that way, his back against the cover)
	float coverCarX, coverCarZ;              // where that car was when he took cover (if it moves, the cover is gone)
	int peek;                                // 0 hidden, -1 / +1: leaning out round the left / right end of the cover to aim
	float peekT, peekDx, peekDz, coverRef;   // 0..1 how far out he is, how far he goes at 1, and the heading he aims at when nobody is in sight
	float chute, airT;                       // parachute 0..1 (open), seconds since he left the ground (v[1] is his vertical speed)
	float crouch, lean;                      // 0..1: how crouched he is, how far he leans against the cover
	float wrapT;                             // time left of the move round a corner onto the next wall
	int coverView;                           // which way along the cover the camera looks (-1 / +1)
	bool sprint;                             // running flat out (two quick taps of B)
	float sprintBoost, bTapT, sprintT;       // 0..1: more taps of B, a faster sprint; time since the last B tap; how long the stick has been let go
	float face;                              // body heading (yaw)
	float phase;                             // gait cycles travelled (the fractional part picks the animation frame)
	float speed;                             // smoothed ground speed
	int car;                                 // index of the car being driven, or -1
	float enterT;                            // seat transition time left
} Player;
static inline bool weapon_usable(const Player *p, int w) { return p->has[w] && (weapon_melee(w) || p->ammo[w] > 0); }

enum { CAR_SEDAN, CAR_SPORTS, CAR_VAN, CAR_TAXI, CAR_POLICE, CAR_SWAT, CAR_KINDS,    // CAR_KINDS: the Blender-modelled cars, same order as LINEUP in assets/models/build_cars.py
       CAR_BIKE = CAR_KINDS, CAR_HELI, CAR_PLANE, CAR_ALL };                                    // a motorcycle, a helicopter and an airliner are built from prisms in scene.c
// A car driven by the computer: it follows a lane (one per direction, right-hand traffic) along a grid line, obeys the traffic lights and turns at junctions on a curve.
typedef struct {
	bool on;
	int axis, dir, line;                     // axis 0 = travels along x, 1 = along z; dir +1 / -1; line = grid line index (1 .. NB-1)
	float s;                                 // position along the axis
	float v, cruise;                         // speed along the path, preferred speed
	int mode;                                // 0 = in the lane, 1 = turning at a junction
	int plan;                                // manoeuvre for the junction at cj: 0 not chosen, 1 straight, 2 right, 3 left
	float cj;                                // coordinate of the junction the plan is for
	float t, len, e[2], q[2], x[2], es;      // the turn: Bezier from e through q to x, progress, length, where along the lane it starts
	int naxis, ndir, nline; float ns;        // the lane the turn joins
	float stuck, ignore, hold;
} Ai;

typedef struct {
	bool on;
	int kind;
	float x, z, yaw;                         // centre on the ground, heading (forward = sin yaw, -cos yaw)
	float vx, vz;                            // world velocity
	float steer;                             // current front wheel angle (radians, + = right)
	float wheel;                             // wheel roll phase
	float lean, pitch;                       // body roll / pitch from the suspension (radians)
	float col[3];
	bool braking, driven;
	float y, vy, rpm, rotor;                 // helicopter: height of the skids above the ground, climb rate, rotor speed 0..1, rotor angle
	float hp;
	Ai ai;
	bool bot;                                // steered by chase input below (police)
	float botLx; bool botGas, botBrake, botHand;
	float botStuck, botReplan, botTx, botTz, botStillT; bool unloaded, botEnd;
	bool wreck; float burn, wreckT;
	int owner;                               // spawned for the police (1) or traffic (0)
} Car;

enum { PED_WALK, PED_IDLE, PED_FLEE, PED_DOWN };
typedef struct {
	bool on;
	int state;
	float x, z, yaw, speed, phase, vx, vz, timer, scale;
	int node, prev; bool cw;                 // pavement graph: current target node, the one before, walking direction round the block
	float jx, jz;                            // personal offset so people do not walk in single file
	unsigned char shirt, pants, skin, hair, shoe;
	float hp;
	unsigned char cop;                       // 0 civilian, 1 police officer, 2 SWAT
	bool dead;
	float shootCd, aimT, losT, replanT, hurtT, flashT;
	int wpNode;                              // pavement node a chasing officer is heading for
	float cash;                              // what the body drops
} Ped;

typedef struct { bool on; float p[3], v[3], life; int owner, dmg; int skip; bool rocket; } Bullet;          // owner 0 = Otto, 1 = police; skip = the car it was fired from inside (index + 1); rockets explode
enum { PT_BLOOD, PT_SMOKE, PT_FIRE, PT_SPARK, PT_TIRE };
typedef struct { bool on; int kind; float p[3], v[3], life, maxLife, size; } Particle;
typedef struct { bool on; float x, z, age, size; } Decal;                          // a blood stain on the ground
enum { PK_CASH, PK_HEALTH, PK_ARMOR, PK_PISTOL, PK_SMG, PK_SHOTGUN, PK_BAT, PK_RIFLE, PK_SNIPER, PK_RPG, PK_COUNT };       // the weapon pickups follow the order of the W_ enum from W_PISTOL
typedef struct { bool on; int type, amount; float x, z, t, respawn; bool fixed; } Pickup;
typedef struct { bool on; float x, y, z, t; } Blast;

typedef struct { float eye[3], yaw, pitch, dist, fov, manualT, look, shoulder, aimFrame, framePitch, zoomK, ctrlYaw; } Cam;      // shoulder: how far the camera has slid to Otto's right while he aims (0..1)      // look: extra yaw while the D-pad glances left / right / back

typedef struct {
	World w;
	Player p; Cam cam;
	Car car[MAX_CAR];
	Ped ped[MAX_PED];
	unsigned trng;                           // traffic / pedestrian random numbers
	short jlock[NB + 1][NB + 1];             // which car (index + 1) holds the left-turn slot at each junction (one at a time)
	Bullet bullet[MAX_BULLET]; Particle part[MAX_PART]; Decal decal[MAX_DECAL]; Pickup pick[MAX_PICKUP]; Blast blast[MAX_BLAST];
	float heat; int stars;                   // the wanted level: heat builds with crimes, stars are its steps
	float unseenT, starFlash, copsSeeT;      // seconds since a police officer last saw Otto; the stars blink while they do
	float hudT; char hudMsg[24];             // a big message (WASTED, BUSTED, ...) and its time left
	float timeScale;                         // 1, slower during WASTED / BUSTED
	char toast[28]; float toastT;            // small pickup messages (+$25, Pistol ...)
	float noiseX, noiseZ, noiseT;            // where the last gunshot was and how long ago: pedestrians run from it
	float respawnT, pnsT, pickT, stillT;
	bool paused;
	float time, hintT;
	float starCd;                            // seconds before the wanted level may rise another star
	int region, regionGo; float fade, fadeDir; bool regionDirty; float bannerT;      // the town Otto is in, the one he is leaving for (-1 none), the black fade (1 = black) and its direction, the scene needs rebuilding, name banner time
	float goX, goZ, goYaw; int goFrom;     // where he arrives in the new region (goYaw < -50: keep his heading)
	int npad; float pad[MAX_PAD][3];        // the helipads of this region
	bool wheel, wheelHold; int wheelSel; float wheelT;     // wheelHold: D-pad up opened it and is still held (do not read it as 'fists' yet)
	int mapMode, mapSel;                     // bottom screen: 0 = this town, 1 = the world map; the town picked on it (-1 none)  // the weapon wheel (SELECT): open, the weapon under the stick, how long it has been open
	float hitT, killT;                       // crosshair hit marker and kill marker time left
	float dpT[3]; bool dpPrev[3], dpSup[3];  // D-pad right / left / down: time since the last press, held last frame, glance suppressed (a double tap turns Otto instead)
	float turnT, turnAmt;                    // a double tap turns Otto and the camera by turnAmt over turnT seconds
	float lookOff, yawSwing;                 // D-pad down / left / right held: how far the camera has swung away from behind Otto; a double-tap's turn still to do
	float dTapAt[3]; float dPrev[3];         // D-pad down, left, right: when each was last pressed, and whether it was down last frame
	unsigned rng;
	float sens; bool invertY, showFps, lowq; float fps;
	bool slow; float tm[5];                  // slow: the frame rate has been poor, so the drawing limits are tightened (not saved); tm: smoothed milliseconds per frame for the logic, the scene build, the 3D draw, the HUD and the wait for the screen
#define LOWQ(g) ((g)->lowq || (g)->slow)
	bool quit, saveNeeded;
} Game;

#define CAR_LEN 4.5f
#define CAR_R   1.05f                        // collision circles: three along the car
static inline float car_radius(int kind) { return kind == CAR_BIKE ? 0.62f : kind == CAR_HELI ? 1.5f : kind == CAR_PLANE ? 1.9f : CAR_R; }
static inline float car_spacing(int kind) { return kind == CAR_BIKE ? 0.55f : kind == CAR_HELI ? 1.0f : kind == CAR_PLANE ? 3.2f : 1.45f; }

void  game_init(Game *g, bool lowq);
void  game_start(Game *g);
void  region_enter(Game *g, int region);                    // build a region's world, traffic and pickups (Otto and the wanted level are kept by the caller)
void  region_go(Game *g, int dest, float x, float z, float yaw);       // start the fade to another region, arriving at x, z                                  // new game: Otto on the street, a few cars parked around
void  game_update(Game *g, const Input *in, float dt);
int   game_nearest_car(const Game *g, float x, float z, float maxd);   // closest unoccupied car within maxd of the point, or -1
int   game_add_car(Game *g, int kind, float x, float z, float yaw);
bool  car_flying(const Car *c);                             // a helicopter well above the ground
bool  cover_find(const Game *g, float x, float z, float refYaw, float out[6]);   // cover within reach of the point: out = Otto's spot x, z, direction from the cover to him x, z, the car index or -1, 1 if the cover is low (a car or street furniture)
bool  cover_peek_offset(const Game *g, int side, float out[2]);                  // how far Otto must step to clear the end of his cover on one side (-1 left, +1 right); false if it is not close or not free
void  car_circles(const Car *c, float cx[3], float cz[3]); // world positions of the car's 3 collision circles

// ---- traffic.c ------------------------------------------------------------------------------------------------------------
void  traffic_init(Game *g);                                // spawn the computer-driven cars and the pedestrians
void  traffic_update(Game *g, float dt);
void  traffic_eject_driver(Game *g, int car);               // the driver of car steps out and runs
int   signal_state(const Game *g, int jx, int jz, int axis); // 0 red, 1 yellow, 2 green for traffic travelling along axis at junction (jx, jz)
bool  car_is_moving_traffic(const Car *c);

typedef struct { const char *name; float dmg; int pellets; float rate, spread, range, speed; int buy; bool rocket; } WeaponDef;
extern const WeaponDef WPN[W_COUNT];
#define HOSPITAL_X (2 * PITCH)
#define HOSPITAL_Z (6 * PITCH - 22.0f)
#define POLICE_X   (6 * PITCH)
#define POLICE_Z   (5 * PITCH + 22.0f)
#define SPRAY_X    (5 * PITCH)
#define SPRAY_Z    (3 * PITCH + 26.0f)
#define SPRAY_R    5.5f
static const float PADS_CITY[3][3] = { { 5.5f * PITCH, 6.5f * PITCH, 0.0f }, { -38.0f, 140.0f, 1.57f }, { -38.0f, 390.0f, 1.57f } };
static const float PADS_AIRPORT[1][3] = { { 5.5f * PITCH, CITY + 62.0f, 0.0f } }, PADS_HILLS[1][3] = { { 1.5f * PITCH, 5.5f * PITCH, 0.0f } };    // x, z, heading of the parked helicopter

// ---- combat.c: weapons, bullets, blood, damage, cash and pickups, explosions, WASTED / BUSTED ----------------------------------------------------------
void  combat_init(Game *g);
void  combat_update(Game *g, const Input *in, float dt);
void  combat_world_init(Game *g);                           // pickups and effects for a newly built region
void  damage_player(Game *g, float amount);
void  player_respawn(Game *g);                              // the pause menu's respawn: back on his feet at the hospital, cash kept
void  damage_ped(Game *g, int idx, float amount, int byPlayer);
void  damage_car(Game *g, int idx, float amount, int byPlayer);
void  spawn_particles(Game *g, int kind, float x, float y, float z, int n, float speed, float size, float life);
void  spawn_blast(Game *g, float x, float y, float z, int byPlayer);
void  add_heat(Game *g, float amount);
void  fire_bullet(Game *g, int owner, const float from[3], float yaw, float pitch, const WeaponDef *w, float dmgScale);
void  drop_cash(Game *g, float x, float z, int amount);
void  player_muzzle(const Game *g, float out[3]);
void  toast(Game *g, const char *text);

// ---- police.c: the wanted level, officers on foot, police cars -------------------------------------------------------------------------------------------
void  police_init(Game *g);
void  police_update(Game *g, float dt);
void  police_reset(Game *g);                                // wanted level gone: officers go back to their beat, chase cars stand down
bool  los_clear(const Game *g, float ax, float ay, float az, float bx, float by, float bz);       // nothing solid between two points

// ---- traffic.c helpers used by the police
void  traffic_node_xy(int id, float *x, float *z);
int   traffic_nearest_node(const Game *g, float x, float z);   // nearest pavement node that can be walked to in a straight line
int   traffic_path_next(int from, int to);                  // next pavement node on the shortest way from -> to
int   traffic_spawn_car(Game *g, int kind, float minFromPlayer, float maxFromPlayer, bool ai);       // on a lane; returns the car index or -1
