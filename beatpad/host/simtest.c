// Logic tests on the PC: loads the real charts and plays them with a perfect bot, a sloppy bot and an idle player.
//   gcc -O2 -o /tmp/simtest host/simtest.c source/game.c source/song.c -lm && /tmp/simtest
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../source/game.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef enum { BOT_PERFECT, BOT_SLOPPY, BOT_IDLE, BOT_EARLY_RELEASE } Bot;

static void run(const Song *s, int diff, Bot bot, Play *p, Chart *c, bool noFail)
{
	chart_load(s, diff, c);
	play_start(p, s, c, diff, noFail);
	const float dt = 1000.0f / 60.0f;
	uint8_t prevHeld = 0;
	// per-lane hit times with jitter for the sloppy bot
	for (float t = -2000; t < s->length_ms + 2000 && !p->failed; t += dt) {
		uint8_t pr = 0, hd = 0;
		if (bot == BOT_PERFECT || bot == BOT_EARLY_RELEASE) {
			play_autoplay(p, t, &pr, &hd);
			if (bot == BOT_EARLY_RELEASE)        // let go of holds halfway
				for (int i = p->first; i < p->chart.n; i++) {
					const Note *n = &p->chart.notes[i];
					if (n->t > t) break;
					if (n->state == NS_HOLDING && t > n->t + n->len / 2) hd &= ~(1 << n->lane);
				}
		} else if (bot == BOT_SLOPPY) {
			// press each note at a random offset in +-100 ms, skip 5% of notes
			for (int i = p->first; i < p->chart.n; i++) {
				const Note *n = &p->chart.notes[i];
				if (n->t > t + 150) break;
				if (n->state != NS_PENDING) continue;
				unsigned h = (unsigned)i * 2654435761u ^ 0x5bd1e995u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
				int jitter = (int)(h % 201) - 100;              // fixed per note: +-100 ms
				if (((h >> 16) & 255) < 13) continue;           // ~5% of notes skipped
				if (t >= n->t + jitter && t - dt < n->t + jitter) pr |= 1 << n->lane;
			}
			play_autoplay(p, t, &(uint8_t){0}, &hd);
			hd |= pr;
		}
		pr &= ~prevHeld | pr;
		play_update(p, t, pr, hd, dt / 1000.0f);
		prevHeld = hd;
	}
}

int main(void)
{
	static Song songs[MAX_SONGS];             // zeroed: songs_scan appends after existing entries
	int n = songs_scan("romfs", songs, MAX_SONGS);
	CHECK(n >= 1, "no songs found");
	for (int si = 0; si < n; si++) {
		const Song *s = &songs[si];
		printf("%s (%s) %.2f BPM, %d ms, %d beats\n", s->title, s->dir, s->bpm, s->length_ms, s->nbeats);
		for (int d = 0; d < NUM_DIFFS; d++) {
			Play p; Chart c;
			run(s, d, BOT_PERFECT, &p, &c, false);
			int holds = 0; for (int i = 0; i < c.n; i++) holds += c.notes[i].len > 0;
			printf("  %-6s %4d notes %3d holds | perfect bot: %d/%d/%d/%d score %d acc %.2f grade %s maxCombo %d\n",
			       diff_name(d), c.n, holds, p.counts[0], p.counts[1], p.counts[2], p.counts[3], p.score, play_accuracy(&p), play_grade(&p), p.maxCombo);
			CHECK(p.counts[J_PERFECT] == c.n, "%s: perfect bot should hit every note perfect", diff_name(d));
			CHECK(p.maxCombo == c.n && p.fullCombo, "%s: full combo expected", diff_name(d));
			CHECK(play_done(&p), "%s: not done", diff_name(d));
			for (int i = 0; i < c.n; i++) CHECK(c.notes[i].state == NS_HIT || c.notes[i].state == NS_DONE, "%s note %d state %d", diff_name(d), i, c.notes[i].state);
			CHECK(c.laneMask == diff_lanes(d), "%s: chart uses lanes %02x, expected %02x", diff_name(d), c.laneMask, diff_lanes(d));
			// no two notes in one lane closer than 100 ms, no chord under one thumb, no note inside a hold on its lane
			for (int i = 0; i < c.n; i++)
				for (int j = i + 1; j < c.n && c.notes[j].t - c.notes[i].t < 2000 + c.notes[i].len; j++) {
					const Note *a = &c.notes[i], *b = &c.notes[j];
					if (a->lane == b->lane) CHECK(b->t - a->t >= 100 && b->t > a->t + a->len, "%s: lane clash at %d ms", diff_name(d), a->t);
					if (a->t == b->t) CHECK((a->lane < 3) != (b->lane < 3), "%s: chord under one thumb at %d ms", diff_name(d), a->t);
				}
			int perfectScore = p.score;
			chart_free(&c);

			run(s, d, BOT_SLOPPY, &p, &c, true);
			printf("         sloppy bot:  %d/%d/%d/%d score %d acc %.2f grade %s maxCombo %d health %.2f\n",
			       p.counts[0], p.counts[1], p.counts[2], p.counts[3], p.score, play_accuracy(&p), play_grade(&p), p.maxCombo, p.health);
			CHECK(play_resolved(&p) == c.n, "%s: sloppy: every note must be judged once (%d vs %d)", diff_name(d), play_resolved(&p), c.n);
			CHECK(p.score < perfectScore, "%s: sloppy should score less", diff_name(d));
			CHECK(p.counts[J_MISS] > 0 && p.counts[J_GREAT] + p.counts[J_GOOD] > 0, "%s: sloppy should get mixed judgements", diff_name(d));
			chart_free(&c);

			run(s, d, BOT_EARLY_RELEASE, &p, &c, true);
			int dropped = 0; for (int i = 0; i < c.n; i++) dropped += c.notes[i].state == NS_DROPPED;
			CHECK(dropped == holds, "%s: early release should drop every hold (%d of %d)", diff_name(d), dropped, holds);
			CHECK(holds == 0 || p.score < perfectScore, "%s: dropping holds should cost points", diff_name(d));
			chart_free(&c);

			run(s, d, BOT_IDLE, &p, &c, false);
			printf("         idle player: failed=%d after %.1f s, misses %d\n", p.failed, p.t / 1000, p.counts[J_MISS]);
			CHECK(p.failed, "%s: idle player should fail", diff_name(d));
			chart_free(&c);
			run(s, d, BOT_IDLE, &p, &c, true);
			CHECK(!p.failed && p.counts[J_MISS] == c.n && p.score == 0, "%s: idle no-fail should miss everything and survive", diff_name(d));
			chart_free(&c);
		}
		song_free(&songs[si]);
	}
	printf(fails ? "%d FAILURES\n" : "all checks passed\n", fails);
	return fails != 0;
}
