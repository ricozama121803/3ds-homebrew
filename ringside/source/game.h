// Fight rules: movement, attacks, blocking, damage, round flow, camera. Runs at a fixed 60 Hz.
#pragma once
#include "fighter.h"
#include "scene.h"

#define TICK_HZ 60
#define TICK_DT (1.0f / TICK_HZ)
#define ROUND_SECONDS 99

typedef enum { PH_INTRO, PH_FIGHT, PH_KO, PH_TIMEUP } Phase;

typedef struct {
	unsigned rng;
	int      cooldown;        // frames until the next decision to attack
	int      react_t;         // frames left before the AI reacts to the opponent's attack
	int      defend_t;        // frames left holding a defensive input
	unsigned defend_mask;     // BTN_* / DP held while defending
	int      seen_atk_t;      // opponent attack timer last tick (to spot a new attack)
	int      strafe_t;
	float    strafe_dir;
	int      retreat_t;
	int      foe_was_atk;     // opponent was mid-attack last tick
	int      guard_t;         // frames left with the guard up
	int      combo_left;      // follow-up attacks queued
	float    skill;           // 0..1: reaction speed and how often it blocks / dodges
	float    aggression;      // 0..1
} Ai;

typedef struct {
	Fighter f[2];             // 0 = player, 1 = opponent
	Ai      ai;
	CamState cam;
	Phase   phase;
	int     phase_t;
	int     frame;
	float   time;
	int     hitstop;          // frames the action is frozen for impact
	float   round_time;
	int     winner;           // -1 while fighting, 0 player, 1 opponent, 2 draw
	float   flash;            // white flash on heavy hits (HUD)
	int     last_hit_side;    // fighter index that was hit last
	int     combo;            // consecutive hits landed by the player
	int     combo_t;
	float   hit_text_t;       // popup timer
	int     hit_text_kind;    // AttackId of the last landed hit, -1 none
	int     hit_text_block;
	Fx      fx[MAX_FX];         // impact sparks (age t < 0 slots are free)
	float   hp_trail[2];        // delayed health for the recent-damage flash on the HUD
} Game;

// Where attacker `f`'s striking limb should end up when attack `atk` lands on `foe` (a point on the target's surface).
Vec3 fight_aim_point(const Fighter *f, const Fighter *foe, int atk);

void game_init(Game *g);
void game_step(Game *g, const FighterInput *player);           // one 60 Hz tick
void game_restart(Game *g);

FighterInput ai_think(Ai *ai, const Fighter *me, const Fighter *foe);
void ai_init(Ai *ai, unsigned seed, float skill);
