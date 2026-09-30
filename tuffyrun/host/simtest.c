// Headless test: a bot plays many runs to check the level generator is always survivable.
#include "../source/game.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

static void bot(Game *g)
{
	float sp = g->speed;
	int best = g->lane; float bestPen = 1e9f;
	float sp0 = g->dist + PLAYER_Z;
	for (int l = -2; l <= 2; l++) {
		if (!lane_exists(g, l, sp0)) continue;
		float pen = abs(l - g->lane) * 0.5f;
		for (int i = 0; i < g->nobjs; i++) {
			Obj *o = &g->objs[i];
			if (o->lane != l) continue;
			if (o->type == OBJ_SCOOTER) {                        // comes from behind: eta from the warning
				float eta = (PLAYER_Z - o->z) / SCOOTER_SPEED;
				if (eta > -0.12f && eta < 1.5f) pen += 1000.0f - eta * 100.0f;
				continue;
			}
			if (o->type != OBJ_ROCK) continue;
			float ttc = (o->z - PLAYER_Z) / sp;
			if (ttc > -0.08f && ttc < 1.3f) pen += 1000.0f - ttc * 100.0f;
		}
		if (pen < bestPen) { bestPen = pen; best = l; }
	}
	int step = (best > g->lane) - (best < g->lane);
	if (step) {
		int nl = g->lane + step; int ok = 1;             // don't step into a lane whose rock is about to arrive
		for (int i = 0; i < g->nobjs; i++) {
			Obj *o = &g->objs[i];
			float ttc = (o->z - PLAYER_Z) / sp;
			if (o->type == OBJ_SCOOTER && o->lane == nl) {
				float eta = (PLAYER_Z - o->z) / SCOOTER_SPEED;
				if (eta > -0.15f && eta < 0.40f) ok = 0;         // he is right behind us: don't step in front of him
			}
			if (o->type == OBJ_ROCK && o->lane == nl && ttc > -0.12f && ttc < 0.30f) ok = 0;
			if (o->type == OBJ_HURDLE && o->lane == nl && ttc > -0.12f && ttc < 0.12f) ok = 0;   // too late to jump it
			if (o->type == OBJ_PETITION && o->lane == nl && ttc > -0.12f && ttc < 0.30f) ok = 0;   // needs more air than a hurdle
		}
		if (ok) game_action(g, step < 0 ? ACT_LEFT : ACT_RIGHT);
	}

	for (int i = 0; i < g->nobjs; i++) {
		Obj *o = &g->objs[i];
		if (o->lane != g->lane) continue;
		float ttc = (o->z - PLAYER_Z) / sp;
		if ((o->type == OBJ_HURDLE || o->type == OBJ_PETITION) && ttc > 0.12f && ttc < 0.38f) game_action(g, ACT_JUMP);
		if (o->type == OBJ_BEAM   && ttc > -0.12f && ttc < 0.30f) game_action(g, ACT_SLIDE);
	}
}

int main(void)
{
	const int runs = 300;
	const float horizon = 400.0f;      // game-seconds per run (top speed reached at ~94 s)
	int survived = 0; float worst = 1e9f; unsigned worstSeed = 0;
	int kills[8] = {0}; long scooters = 0; int scooterDeaths = 0;
	long turns = 0, onWide = 0, zones = 0, stadiums = 0, flights = 0;
	for (int r = 0; r < runs; r++) {
		Game g; game_init(&g, 0, 1000u + r * 7919u); game_start(&g);
		float t = 0;
		int seen = 0; float lastEv = -1;
		while (g.state == ST_PLAY && t < horizon) {
			bot(&g); game_update(&g, 1.0f / 60.0f); t += 1.0f / 60.0f;
			float sp = g.dist + PLAYER_Z;
			if (g.evStart != lastEv && sp >= g.evStart + (g.evKind == EV_TURN ? 260.0f : 340.0f) && sp <= g.evEnd) {      // well into an event
				lastEv = g.evStart;
				if (g.evKind == EV_TURN) { turns++; if (abs(g.lane) == 2) onWide++; }
				else if (g.evKind == EV_STADIUM) stadiums++;
				else zones++;
			}
			static int wasFly; if (g.flying && !wasFly) flights++; wasFly = g.flying;
			int now = 0;
			for (int i = 0; i < g.nobjs; i++) if (g.objs[i].type == OBJ_SCOOTER) now++;
			if (now > seen) scooters += now - seen;
			seen = now;
		}
		if (g.state == ST_PLAY) survived++;
		else { kills[g.killer]++; if (t < worst) { worst = t; worstSeed = 1000u + r * 7919u; } }
	}
	printf("%ld turns (%ld with Tuffy in an extra lane), %ld theme-park zones, %ld stadiums (%ld launches)\n", turns, onWide, zones, stadiums, flights);
	printf("survived %d/%d runs of %.0fs (%ld scooter riders spawned, about one every %.1fs)\n", survived, runs, horizon, scooters, runs * horizon / (scooters ? scooters : 1));
	if (survived < runs) printf("earliest death %.1fs (seed %u); killers: rock=%d hurdle=%d beam=%d scooter=%d petition=%d\n",
	                            worst, worstSeed, kills[OBJ_ROCK], kills[OBJ_HURDLE], kills[OBJ_BEAM], kills[OBJ_SCOOTER], kills[OBJ_PETITION]);
	return survived == runs ? 0 : 1;
}
