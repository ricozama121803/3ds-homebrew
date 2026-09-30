// Opponent AI. It produces the same FighterInput a player would, so it plays by the same rules.
#include "game.h"
#include <string.h>

static unsigned rnd(Ai *a)
{
	unsigned x = a->rng ? a->rng : 0x1234567u;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	a->rng = x;
	return x;
}
static float rndf(Ai *a) { return (float)(rnd(a) & 0xffffff) / (float)0x1000000; }
static int rndi(Ai *a, int n) { return (int)(rnd(a) % (unsigned)n); }

void ai_init(Ai *ai, unsigned seed, float skill)
{
	memset(ai, 0, sizeof *ai);
	ai->rng = seed; ai->skill = skill; ai->aggression = 0.55f;
	ai->cooldown = 70;
}

static unsigned atk_button(int atk)
{
	switch (atk) {
	case ATK_LEAD_PUNCH: return BTN_LEAD_PUNCH;
	case ATK_REAR_PUNCH: return BTN_REAR_PUNCH;
	case ATK_LEAD_HOOK:  return BTN_LEAD_PUNCH | BTN_MOD;
	case ATK_REAR_HOOK:  return BTN_REAR_PUNCH | BTN_MOD;
	case ATK_LEAD_KICK:  return BTN_LEAD_KICK;
	default:             return BTN_REAR_KICK;
	}
}

static int pick_attack(Ai *ai, float dist)
{
	float r = rndf(ai);
	if (dist < 1.00f) {
		if (r < 0.32f) return ATK_LEAD_PUNCH;
		if (r < 0.55f) return ATK_REAR_PUNCH;
		if (r < 0.70f) return ATK_LEAD_HOOK;
		if (r < 0.85f) return ATK_REAR_HOOK;
		return ATK_LEAD_KICK;
	}
	if (r < 0.28f) return ATK_LEAD_KICK;
	if (r < 0.46f) return ATK_REAR_KICK;
	if (r < 0.74f) return ATK_LEAD_PUNCH;
	return ATK_REAR_PUNCH;
}

// How to defend against an incoming attack. Returns the held mask (BTN_* including D-pad, already mapped to screen sides).
static unsigned pick_defence(Ai *ai, const Fighter *me, const Fighter *foe)
{
	int atk = foe->atk;
	int side_right = foe->x >= me->x;                       // opponent is to our right on screen
	unsigned back = side_right ? BTN_DP_LEFT : BTN_DP_RIGHT;
	float r = rndf(ai);
	if (atk == ATK_LEAD_KICK) return r < 0.6f ? (BTN_GUARD | BTN_MOD) : back;             // body kick: body block or lean away
	if (atk == ATK_REAR_KICK) return r < 0.45f ? BTN_GUARD : r < 0.75f ? BTN_DP_DOWN : back;
	if (atk == ATK_LEAD_HOOK || atk == ATK_REAR_HOOK) return r < 0.55f ? BTN_GUARD : r < 0.85f ? BTN_DP_DOWN : back;
	return r < 0.70f ? BTN_GUARD : back;                                                   // straights: block or lean
}

FighterInput ai_think(Ai *ai, const Fighter *me, const Fighter *foe)
{
	FighterInput in;
	memset(&in, 0, sizeof in);
	float dx = foe->x - me->x, dz = foe->z - me->z;
	float dist = sqrtf(dx * dx + dz * dz);
	float tx = dx / fmaxf(dist, 0.01f), tz = dz / fmaxf(dist, 0.01f);
	unsigned held = 0;

	if (ai->cooldown > 0) ai->cooldown--;
	if (ai->guard_t > 0) ai->guard_t--;

	int foe_down = foe->st == ST_KO || foe->st == ST_KNOCKDOWN;
	int foe_atk = foe->st == ST_ATTACK;

	// spot the start of the opponent's attack and decide whether to react to it
	if (foe_atk && !ai->foe_was_atk) {
		const AttackDef *d = &ATTACKS[foe->atk];
		float in_range = d->reach + 0.65f;
		if (dist < in_range && rndf(ai) < 0.30f + 0.65f * ai->skill) {
			ai->react_t = (int)(15.0f - 9.0f * ai->skill) + rndi(ai, 5);
			ai->defend_t = 0;
		}
	}
	ai->foe_was_atk = foe_atk;
	if (ai->react_t > 0 && --ai->react_t == 0 && foe_atk) {
		const AttackDef *d = &ATTACKS[foe->atk];
		int left = d->startup + d->active - foe->atk_t;
		if (left > 0) {
			ai->defend_mask = pick_defence(ai, me, foe);
			ai->defend_t = left + 6;
		}
	}
	if (!foe_atk && ai->react_t > 0) ai->react_t = 0;

	if (me->st == ST_ATTACK || me->st == ST_HITSTUN || me->st == ST_KNOCKDOWN || me->st == ST_KO || me->st == ST_GUARDBREAK)
		return in;                                                       // nothing to decide while busy

	if (ai->defend_t > 0) {
		ai->defend_t--;
		held |= ai->defend_mask;
	} else if (foe_down) {
		in.mx = -tx * 0.2f; in.mz = -tz * 0.2f;                          // give the fallen fighter space
	} else {
		// spacing
		float want_dist = 1.05f;
		if (ai->retreat_t > 0) { ai->retreat_t--; in.mx = -tx; in.mz = -tz; }
		else if (dist > want_dist + 0.30f) { in.mx = tx; in.mz = tz; }
		else if (dist < 0.80f) { in.mx = -tx * 0.7f; in.mz = -tz * 0.7f; }
		else {
			if (ai->strafe_t <= 0) { ai->strafe_t = 20 + rndi(ai, 40); ai->strafe_dir = rndi(ai, 3) - 1.0f; }
			ai->strafe_t--;
			in.mz = ai->strafe_dir * 0.6f;
			in.mx = (dist > want_dist ? 0.35f : -0.2f) * tx;
		}

		// keep the guard up now and then when the opponent is close
		if (ai->guard_t == 0 && dist < 1.25f && rndf(ai) < 0.010f * (0.4f + ai->skill)) ai->guard_t = 25 + rndi(ai, 40);
		if (ai->guard_t > 0) { held |= (rndf(ai) < 0.03f && ai->guard_t > 30) ? BTN_GUARD | BTN_MOD : BTN_GUARD; in.mx *= 0.3f; in.mz *= 0.3f; }

		// attack
		if (ai->guard_t == 0 && ai->cooldown == 0 && me->st == ST_IDLE) {
			float p = (ai->combo_left > 0 ? 0.6f : 0.03f + 0.06f * ai->aggression);
			int reach_ok = dist < 1.35f && dist > 0.65f;
			if (reach_ok && rndf(ai) < p) {
				int atk = pick_attack(ai, dist);
				if (dist > ATTACKS[atk].reach + 0.55f) atk = dist < 1.05f ? ATK_LEAD_PUNCH : ATK_LEAD_KICK;
				unsigned b = atk_button(atk);
				held |= b;
				in.pressed |= b & ~BTN_MOD;
				if (ai->combo_left > 0) ai->combo_left--;
				else if (rndf(ai) < 0.45f) ai->combo_left = 1 + rndi(ai, 2);
				const AttackDef *d = &ATTACKS[atk];
				ai->cooldown = ai->combo_left > 0 ? 2 : 24 + rndi(ai, 45);
				if (ai->combo_left == 0 && rndf(ai) < 0.5f) ai->retreat_t = d->startup + d->active + 8 + rndi(ai, 12);
			}
		}
	}
	in.held = held;
	return in;
}
