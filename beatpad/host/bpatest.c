// Checks the C .bpa decoder against tools/bpa.py's reference decode, including reads after seeks.
//   gcc -O2 -o /tmp/bpatest host/bpatest.c source/bpa.c && /tmp/bpatest song.bpa reference.pcm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/bpa.h"

int main(int argc, char **argv)
{
	if (argc < 3) { printf("usage: bpatest song.bpa reference.pcm\n"); return 1; }
	FILE *f = fopen(argv[1], "rb"), *g = fopen(argv[2], "rb");
	if (!f || !g) { printf("cannot open inputs\n"); return 1; }
	fseek(g, 0, SEEK_END); long n = ftell(g) / 4; fseek(g, 0, SEEK_SET);
	int16_t *ref = malloc(n * 4);
	if (fread(ref, 4, n, g) != (size_t)n) return 1;
	BpaReader r;
	if (!bpa_open(&r, f) || !r.adpcm || r.frames != n) { printf("FAIL: header (%ld frames vs %ld)\n", r.frames, n); return 1; }
	int16_t *out = malloc(n * 4);
	// sequential reads in odd-sized chunks
	long pos = 0; size_t chunk = 4093;
	while (pos < n) { size_t got = bpa_read(&r, out + pos * 2, chunk); if (!got) break; pos += (long)got; }
	if (pos != n || memcmp(out, ref, n * 4)) { printf("FAIL: sequential decode differs\n"); return 1; }
	// random seeks
	srand(1);
	for (int k = 0; k < 200; k++) {
		long at = rand() % n; int16_t tmp[5000 * 2];
		bpa_seek(&r, at);
		size_t got = bpa_read(&r, tmp, 5000);
		size_t want = (size_t)(n - at) < 5000 ? (size_t)(n - at) : 5000;
		if (got != want || memcmp(tmp, ref + at * 2, got * 4)) { printf("FAIL: seek to %ld\n", at); return 1; }
	}
	printf("bpa decoder matches the reference (%ld frames, 200 seeks)\n", n);
	return 0;
}
