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
#define MAX_CAR   40
#define MAX_PED   56
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

enum { BK_BLDG, BK_TOWER, BK_DOME };
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
	int downtown0, downtown1;                // downtown blocks are i >= downtown0 and j < downtown1 (north-east corner)
} World;

typedef struct { bool hitx, hitz, head, landed; float nx, nz; } Contact;

void  city_generate(World *w, unsigned seed);
float city_ray(const World *w, const float o[3], const float d[3], float tmax, float n[3]);   // distance to the first box hit, or -1
void  city_move(const World *w, float p[3], const float d[3], Contact *c);                     // move the on-foot body with collision (axis by axis)
bool  city_push_circle(const World *w, float *x, float *z, float r, float n[2]);               // push a ground circle out of the buildings; true (and the push direction) if it hit
static inline bool world_walkable(float x, float z)       // on foot: the city, the beach and the pier
{
	if (z >= 1.0f && z <= CITY - 1.0f && x >= BEACH_X && x <= CITY - 1.0f) return true;
	return x >= PIER_X && x <= BEACH_X && z >= PIER_Z0 && z <= PIER_Z1;
}
unsigned rnd_next(unsigned *s);
float rndf(unsigned *s);

#define PLAYER_MAX_HP 150.0f
#define BODY_HW 0.4f
#define BODY_H  1.8f

// ---- game -----------------------------------------------------------------------------------------------------------------
typedef struct {
	float lx, ly;                            // circle pad: move / steer
	float cx, cy;                            // C-stick / d-pad: camera
	float tdx, tdy;                          // touch drag on the bottom screen (pixels this frame)
	bool run, gas, brake, hand, cover;       // held: (unused), R = gas / shoot, B = brake, A or L = handbrake, L = take cover (on foot)
	bool enter_p, a_p, b_p, x_p, start_p, select_p, up_p, down_p, touch_p;
	int weap;                                // touch: weapon slot tapped this frame + 1 (0 = none)
	float tx, ty;
} Input;

enum { W_FISTS, W_PISTOL, W_SMG, W_SHOTGUN, W_COUNT };
enum { PS_ALIVE, PS_WASTED, PS_BUSTED };
typedef struct {
	float p[3], v[3];                        // feet position, velocity
	float hp, armor;                         // 100 each at most; armour soaks damage first
	int weapon, ammo[W_COUNT]; bool has[W_COUNT];
	int cash;
	float fireCd, aimT, flashT, punchT, hurtT, lastShotT;     // aimT 0..1: arm raised; flashT: muzzle flash left; punchT: punch left; hurtT: red flash left
	int target;                              // ped being auto-aimed at, or -1
	int ps; float psT;                       // alive / wasted / busted, and for how long
	float arrestT;                           // how long a cop has had hold of Otto
	float aimYaw;
	int punchSide;                           // alternates: right jab, left cross
	bool punchHit;                           // the punch has not landed yet (it does when the fist is fully out)
	bool cover, sprint;                      // crouched behind a wall or car; running flat out (two quick taps of B)
	float coverT, coverNx, coverNz, bTapT, sprintT;   // cover blend 0..1, the direction away from the cover, time since the last B tap, how long the stick has been let go
	float face;                              // body heading (yaw)
	float phase;                             // gait cycles travelled (the fractional part picks the animation frame)
	float speed;                             // smoothed ground speed
	int car;                                 // index of the car being driven, or -1
	float enterT;                            // seat transition time left
} Player;

enum { CAR_SEDAN, CAR_SPORTS, CAR_VAN, CAR_TAXI, CAR_POLICE, CAR_SWAT, CAR_KINDS };    // same order as LINEUP in assets/models/build_cars.py
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
	float hp;
	Ai ai;
	bool bot;                                // steered by chase input below (police)
	float botLx; bool botGas, botBrake, botHand;
	float botStuck, botReplan, botTx, botTz, botStillT; bool unloaded;
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

typedef struct { bool on; float p[3], v[3], life; int owner, dmg; } Bullet;          // owner 0 = Otto, 1 = police
enum { PT_BLOOD, PT_SMOKE, PT_FIRE, PT_SPARK };
typedef struct { bool on; int kind; float p[3], v[3], life, maxLife, size; } Particle;
typedef struct { bool on; float x, z, age, size; } Decal;                          // a blood stain on the ground
enum { PK_CASH, PK_HEALTH, PK_ARMOR, PK_PISTOL, PK_SMG, PK_SHOTGUN, PK_COUNT };
typedef struct { bool on; int type, amount; float x, z, t, respawn; bool fixed; } Pickup;
typedef struct { bool on; float x, y, z, t; } Blast;

typedef struct { float eye[3], yaw, pitch, dist, fov, manualT; } Cam;

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
	unsigned rng;
	float sens; bool invertY, showFps, lowq; float fps;
	bool quit, saveNeeded;
} Game;

#define CAR_LEN 4.5f
#define CAR_R   1.05f                        // collision circles: three along the car

void  game_init(Game *g, bool lowq);
void  game_start(Game *g);                                  // new game: Otto on the street, a few cars parked around
void  game_update(Game *g, const Input *in, float dt);
int   game_nearest_car(const Game *g, float x, float z, float maxd);   // closest unoccupied car within maxd of the point, or -1
int   game_add_car(Game *g, int kind, float x, float z, float yaw);
void  car_circles(const Car *c, float cx[3], float cz[3]); // world positions of the car's 3 collision circles

// ---- traffic.c ------------------------------------------------------------------------------------------------------------
void  traffic_init(Game *g);                                // spawn the computer-driven cars and the pedestrians
void  traffic_update(Game *g, float dt);
void  traffic_eject_driver(Game *g, int car);               // the driver of car steps out and runs
int   signal_state(const Game *g, int jx, int jz, int axis); // 0 red, 1 yellow, 2 green for traffic travelling along axis at junction (jx, jz)
bool  car_is_moving_traffic(const Car *c);

typedef struct { const char *name; float dmg; int pellets; float rate, spread, range, speed; int buy; } WeaponDef;
extern const WeaponDef WPN[W_COUNT];
#define HOSPITAL_X (2 * PITCH)
#define HOSPITAL_Z (6 * PITCH - 22.0f)
#define POLICE_X   (6 * PITCH)
#define POLICE_Z   (5 * PITCH + 22.0f)
#define SPRAY_X    (5 * PITCH)
#define SPRAY_Z    (3 * PITCH + 26.0f)
#define SPRAY_R    5.5f

// ---- combat.c: weapons, bullets, blood, damage, cash and pickups, explosions, WASTED / BUSTED ----------------------------------------------------------
void  combat_init(Game *g);
void  combat_update(Game *g, const Input *in, float dt);
void  damage_player(Game *g, float amount);
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
