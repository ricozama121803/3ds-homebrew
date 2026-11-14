// BeatPad - judging, scoring and the rock meter
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const int JUDGE_POINTS[3] = { 100, 70, 40 };
static const float JUDGE_HEALTH[4] = { 0.030f, 0.024f, 0.012f, -0.06f };
#define TAIL_POINTS_PER_MS 0.25f

const uint32_t LANE_RGB[NUM_LANES] = { 0x3fd96a, 0xf04848, 0xf5d03a, 0x3d8cf5, 0xff9a2e, 0xc160f0 };

static float frand(Play *p)
{
	p->rng = p->rng * 1664525u + 1013904223u;
	return (p->rng >> 8) / 16777216.0f;
}

void play_start(Play *p, const Song *s, Chart *c, int diff, bool noFail)
{
	memset(p, 0, sizeof *p);
	p->song = s; p->chart = *c; p->diff = diff; p->noFail = noFail;
	p->health = 0.5f;
	p->popJudge = J_NONE;
	p->lastMult = 1;
	p->rng = 12345;
	for (int i = 0; i < c->n; i++) {
		Note *n = &c->notes[i];
		n->state = NS_PENDING; n->judge = J_NONE; n->offset = 0; n->tailScore = 0;
	}
	p->fullCombo = true;
}

int play_multiplier(const Play *p)
{
	int m = 1 + p->combo / 10;
	return m > 4 ? 4 : m;
}

int play_resolved(const Play *p)
{
	return p->counts[0] + p->counts[1] + p->counts[2] + p->counts[3];
}

float play_accuracy(const Play *p)
{
	int n = play_resolved(p);
	if (!n) return 100.0f;
	return 100.0f * (p->counts[J_PERFECT] + 0.7f * p->counts[J_GREAT] + 0.4f * p->counts[J_GOOD]) / n;
}

const char *play_grade(const Play *p)
{
	if (p->failed) return "F";
	float a = play_accuracy(p);
	if (a >= 98 && p->fullCombo) return "S+";
	if (a >= 95) return "S";
	if (a >= 90) return "A";
	if (a >= 80) return "B";
	if (a >= 70) return "C";
	if (a >= 60) return "D";
	return "F";
}

bool play_done(const Play *p)
{
	for (int i = p->first; i < p->chart.n; i++) {
		int st = p->chart.notes[i].state;
		if (st == NS_PENDING || st == NS_HOLDING) return false;
	}
	return true;
}

static void burst(Play *p, int lane, int count, float speed)
{
	for (int k = 0; k < count; k++) {
		for (int i = 0; i < MAX_SPARKS; i++) {
			Spark *s = &p->sparks[i];
			if (s->life > 0) continue;
			float a = 3.14159f * (0.15f + 0.7f * frand(p));         // upward fan
			float v = speed * (0.4f + 0.6f * frand(p));
			s->lane = (uint8_t)lane; s->x = 0; s->y = 0;
			s->vx = cosf(a) * v; s->vy = sinf(a) * v;
			s->max = s->life = 0.25f + 0.3f * frand(p);
			s->color = frand(p) < 0.3f ? 0xffffff : LANE_RGB[lane];
			break;
		}
	}
}

static void judge(Play *p, Note *n, int j, int offset)
{
	n->judge = (uint8_t)j; n->offset = (int16_t)offset;
	p->counts[j]++;
	p->popJudge = j; p->popT = 0.55f;
	if (j == J_MISS) {
		n->state = NS_MISSED;
		p->missEvents++;
		if (p->combo >= 10) p->missT = 0.35f;
		p->combo = 0; p->fullCombo = false;
	} else {
		n->state = n->len > 0 ? NS_HOLDING : NS_HIT;
		p->combo++;
		if (p->combo > p->maxCombo) p->maxCombo = p->combo;
		if (p->combo % 50 == 0) p->comboPopT = 1.0f;
		p->score += JUDGE_POINTS[j] * play_multiplier(p);
		p->hitSum += offset; p->hitN++;
		p->laneFlash[n->lane] = 1.0f;
		burst(p, n->lane, j == J_PERFECT ? 14 : 8, j == J_PERFECT ? 260 : 180);
	}
	p->health += JUDGE_HEALTH[j];
	if (p->health > 1) p->health = 1;
	if (p->health <= 0) {
		p->health = 0;
		if (!p->noFail) p->failed = true;
	}
	int m = play_multiplier(p);
	if (m > p->lastMult) p->multPopT = 0.6f;
	p->lastMult = m;
}

void play_update(Play *p, float t, uint8_t pressed, uint8_t held, float dt)
{
	Chart *c = &p->chart;
	float prevT = p->t;
	p->t = t;
	p->heldMask = held;

	// presses: each lane takes its earliest unjudged note inside the Good window
	for (int lane = 0; lane < NUM_LANES; lane++) {
		if (!(pressed & (1 << lane))) continue;
		p->lanePress[lane] = 1.0f;
		for (int i = p->first; i < c->n; i++) {
			Note *n = &c->notes[i];
			if (n->t - t > WIN_GOOD) break;
			if (n->lane != lane || n->state != NS_PENDING) continue;
			int off = (int)lrintf(t - n->t);
			if (abs(off) > WIN_GOOD) continue;
			int a = abs(off);
			judge(p, n, a <= WIN_PERFECT ? J_PERFECT : a <= WIN_GREAT ? J_GREAT : J_GOOD, off);
			break;
		}
	}

	// holds and misses
	for (int i = p->first; i < c->n; i++) {
		Note *n = &c->notes[i];
		if (n->t - t > WIN_GOOD) break;
		if (n->state == NS_PENDING && t - n->t > WIN_GOOD) judge(p, n, J_MISS, WIN_GOOD + 1);
		if (n->state == NS_HOLDING) {
			int end = n->t + n->len;
			float from = prevT > n->t ? prevT : (float)n->t, to = t < end ? t : (float)end;
			if (!(held & (1 << n->lane)) && t < end - HOLD_GRACE) {
				n->state = NS_DROPPED;                       // let go early: lose the rest of the tail
			} else {
				if (to > from) {
					float pts = (to - from) * TAIL_POINTS_PER_MS * play_multiplier(p);
					n->tailScore += pts;
					p->score += (int)pts;      // integer part now; fraction is dropped (small)
				}
				if (p->rng % 4 == 0) burst(p, n->lane, 1, 120);
				p->laneFlash[n->lane] = 0.6f;
				if (t >= end || !(held & (1 << n->lane))) {
					n->state = NS_DONE;
					burst(p, n->lane, 10, 220);
				}
			}
		}
	}
	while (p->first < c->n) {
		int st = c->notes[p->first].state;
		if (st == NS_PENDING || st == NS_HOLDING) break;
		p->first++;
	}

	// effects
	for (int l = 0; l < NUM_LANES; l++) {
		p->laneFlash[l] -= dt * 4.0f; if (p->laneFlash[l] < 0) p->laneFlash[l] = 0;
		p->lanePress[l] -= dt * 8.0f; if (p->lanePress[l] < 0) p->lanePress[l] = 0;
	}
	p->popT -= dt; p->comboPopT -= dt; p->multPopT -= dt; p->missT -= dt;
	for (int i = 0; i < MAX_SPARKS; i++) {
		Spark *s = &p->sparks[i];
		if (s->life <= 0) continue;
		s->life -= dt;
		s->x += s->vx * dt * 0.3f;
		s->y += s->vy * dt;
		s->vy -= 600.0f * dt;
	}
}

void play_autoplay(const Play *p, float t, uint8_t *pressed, uint8_t *held)
{
	uint8_t pr = 0, hd = 0;
	for (int i = p->first; i < p->chart.n; i++) {
		const Note *n = &p->chart.notes[i];
		if (n->t > t) break;
		if (n->state == NS_PENDING && t - n->t <= WIN_GOOD) pr |= 1 << n->lane;
		if (n->state == NS_HOLDING && t < n->t + n->len) hd |= 1 << n->lane;
	}
	*pressed = pr; *held = hd | pr;
}
