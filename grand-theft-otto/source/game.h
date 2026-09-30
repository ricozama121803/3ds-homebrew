// Grand Theft Otto - game state and rules (no 3DS dependencies, so it can be tested on a PC)
#pragma once
#include "world.h"
#include "nav.h"

#define SCREEN_W      400.0f
#define SCREEN_H      240.0f
#define PLAYER_HALF   5.0f      // half-size of a person's collision square (a person is smaller than a tile)
#define WALK_SPEED    62.0f     // pixels per second
#define RUN_SPEED     106.0f
#define PI_F          3.14159265f

#define MAX_CARS      30
#define MAX_PEDS      40
#define MAX_BULLETS   64
#define MAX_PARTS     160
#define MAX_STAINS    80
#define MAX_PICKUPS   80
#define MAX_POPUPS    5
#define MAX_PARK      160

// ---------------------------------------------------------------- weapons
enum { W_FISTS, W_PISTOL, W_SMG, W_SHOTGUN, W_COUNT };
typedef struct { const char *name; float dmg, rate, spread, speed, life; int pellets, maxammo; } WeaponDef;
extern const WeaponDef weapon_defs[W_COUNT];

// ---------------------------------------------------------------- cars
enum { M_COMPACT, M_SEDAN, M_SPORTS, M_VAN, M_TAXI, M_POLICE, M_SWAT, M_COUNT };
typedef struct { float len, wid, maxspeed, accel, steer, mass, hp; } CarDef;
extern const CarDef car_defs[M_COUNT];

typedef enum { DRV_NONE, DRV_CIV, DRV_COP, DRV_SWAT, DRV_PLAYER } Driver;
typedef enum { CS_PARKED, CS_TRAFFIC, CS_PLAYER, CS_CHASE, CS_WRECK } CarState;

typedef struct {
	bool active;
	int variant, model;
	CarState state;
	Driver driver;
	float x, y, heading, vx, vy;              // heading: radians clockwise from "up"
	float hp;
	float steer, throttle, brake, hand;       // controls: steer -1..1, others 0..1
	// traffic route: a polyline of waypoints the car follows
	float px[4], py[4];
	int np, pi;
	int road, half, lane;                     // which street / side / lane we are on
	float cruise;
	float stuckT, reverseT, lostT, waitT;
	float fireT, sirenT, burnT, wreckT, hitT;       // hitT > 0: the player attacked this car recently
	int spot;                                 // parking spot this car came from (-1 if none)
	bool smoking;
} Car;

// ---------------------------------------------------------------- people
typedef enum { PK_CIV, PK_COP, PK_SWAT } PedKind;
typedef enum { PS_WALK, PS_FLEE, PS_ATTACK, PS_DEAD } PedState;
typedef struct {
	bool active;
	PedKind kind;
	int type;                                  // sprite type: 0-5 civilians, 6 cop, 7 SWAT
	PedState state;
	float x, y, heading, vx, vy;
	float hp;
	float walkT, turnT, stateT, fireT, deadT;
	float threatX, threatY;
} Ped;

// ---------------------------------------------------------------- effects
typedef struct { bool active; float x, y, vx, vy, life; int owner; float dmg; } Bullet;      // owner: 0 player, 1 police
enum { P_BLOOD, P_SMOKE, P_FIRE, P_SPARK, P_MUZZLE, P_BOOM };
typedef struct { bool active; int type, frame; float x, y, vx, vy, life, maxlife; } Particle;
typedef struct { float x, y; int img; } Stain;
typedef struct { bool active; float x, y; int kind, amount; float respawnT, lifeT; bool temp; } Pickup;    // kind: 0 health 1 armor 2 pistol 3 smg 4 shotgun 5 cash
typedef struct { char text[40]; unsigned color; float t; } Popup;

typedef enum { PL_ALIVE, PL_DEAD, PL_BUSTED } PlayerStatus;
typedef struct {
	float x, y, vx, vy, heading, walkT;
	bool moving;
	float hp, armor;
	int cash;
	int weapon;
	int ammo[W_COUNT];
	bool has[W_COUNT];
	float fireT, hurtT, bustT, sprayT, punchT;
	int car;                                   // index of the car we're driving, or -1
	PlayerStatus status;
	float statusT;
} Player;

// ---------------------------------------------------------------- input (filled in by main.c or by tests)
typedef struct {
	float mx, my;                              // stick, -1..1 (my > 0 = down)
	bool run;                                  // R
	bool a, b, l;                              // held
	bool x_p, y_p, l_p;                        // pressed this frame
	bool tap; float tx, ty;                    // touch pressed this frame (bottom-screen pixels)
	bool start_p, select_p;
} Input;

typedef struct {
	World world;
	Player p;
	Car cars[MAX_CARS];
	Ped peds[MAX_PEDS];
	Bullet bullets[MAX_BULLETS];
	Particle parts[MAX_PARTS];
	Stain stains[MAX_STAINS]; int stain_n;
	Pickup pickups[MAX_PICKUPS];
	Popup popups[MAX_POPUPS];
	int park_car[MAX_PARK];                    // car index occupying each parking spot, or -1

	float camx, camy, time;
	int zone; float zoneT;

	// wanted level
	float heat; int stars;
	float unseenT, seenT;
	bool cops_see;

	bool paused, quit;
	unsigned rng;
	Field foot_field, car_field;
	float fieldT;
	float spawnCarT, spawnPedT, spawnCopT, saveT;
	bool saveNeeded;
	float flashT;                              // damage flash on screen
	float slow;
} Game;

typedef struct { int cash; bool has[W_COUNT]; int ammo[W_COUNT]; } SaveData;

bool game_init(Game *g, const uint8_t *map, size_t size, const SaveData *save);
void game_update(Game *g, const Input *in, float dt);
int  game_zone_at(int tx, int ty);
void game_save_data(const Game *g, SaveData *out);

// ---------------------------------------------------------------- shared helpers (game.c)
unsigned g_rnd(Game *g);
float g_rndf(Game *g, float a, float b);
int g_rndi(Game *g, int n);
void popup(Game *g, const char *text, unsigned color);
void add_heat(Game *g, float amount);
bool los_clear(const Game *g, float x0, float y0, float x1, float y1);
bool ped_tile_ok(const World *w, int tx, int ty);
int  find_car_near(const Game *g, float x, float y, float radius);
void player_wasted(Game *g);
void player_busted(Game *g);
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float wrap_pi(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

// ---------------------------------------------------------------- combat.c
void fx_blood(Game *g, float x, float y, float dx, float dy, int amount);
void fx_stain(Game *g, float x, float y, int size);
void fx_boom(Game *g, float x, float y);
void fx_part(Game *g, int type, float x, float y, float vx, float vy, float life, int frame);
void particles_update(Game *g, float dt);
void bullets_update(Game *g, float dt);
void spawn_bullet(Game *g, float x, float y, float ang, const WeaponDef *w, int owner, float dmgmul);
void player_fire(Game *g, const Input *in, float dt);
void damage_ped(Game *g, int i, float dmg, float dx, float dy, int source);   // source: 0 player, 1 police, 2 car/explosion
void damage_player(Game *g, float dmg, float dx, float dy);
void damage_car(Game *g, int i, float dmg, int source);
void explode_car(Game *g, int i);
void drop_cash(Game *g, float x, float y, int amount);

// ---------------------------------------------------------------- car.c
void cars_update(Game *g, float dt);
int  car_spawn(Game *g, int variant, float x, float y, float heading, CarState st, Driver drv);
bool car_area_free(const Game *g, float x, float y, float heading, int model, int ignore);
void car_circles(const Car *c, float cx[3], float cy[3], float *r);
void player_use(Game *g);                       // enter / exit a car
void spawn_traffic_car(Game *g);
void spawn_cop_car(Game *g);
void car_remove(Game *g, int i);

// ---------------------------------------------------------------- ped.c
void peds_update(Game *g, float dt);
int  ped_spawn(Game *g, PedKind kind, int type, float x, float y);
void spawn_civilian(Game *g);
void spawn_cop_foot(Game *g, bool swat);
void ped_remove(Game *g, int i);
