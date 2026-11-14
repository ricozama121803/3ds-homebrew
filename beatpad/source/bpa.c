// Song audio reader: IMA-ADPCM blocks (.bpa) decoded on the fly, or raw PCM passed through.
#include "bpa.h"
#include <string.h>

static const int16_t STEPS[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
	107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
	4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
	22385, 24623, 27086, 29794, 32767 };
static const int8_t IDX_ADJ[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

#define HEADER 16

bool bpa_open(BpaReader *r, FILE *fp)
{
	memset(r, 0, sizeof *r);
	r->fp = fp;
	uint8_t h[HEADER];
	if (fread(h, 1, HEADER, fp) == HEADER && !memcmp(h, "BPA1", 4)) {
		uint32_t frames = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t)h[11] << 24;
		uint32_t block = h[12] | h[13] << 8 | h[14] << 16 | (uint32_t)h[15] << 24;
		if (block != BPA_BLOCK) return false;
		r->adpcm = true; r->frames = (long)frames;
	} else {
		fseek(fp, 0, SEEK_END);
		r->frames = ftell(fp) / 4;
	}
	r->bufFirst = -1;
	bpa_seek(r, 0);
	return true;
}

void bpa_seek(BpaReader *r, long frame)
{
	if (frame < 0) frame = 0;
	if (frame > r->frames) frame = r->frames;
	r->pos = frame;
	if (!r->adpcm) fseek(r->fp, frame * 4, SEEK_SET);
}

static bool decode_block(BpaReader *r, long block)
{
	const long bsz = 8 + BPA_BLOCK;
	if (fseek(r->fp, HEADER + block * bsz, SEEK_SET) || fread(r->raw, 1, bsz, r->fp) != (size_t)bsz) return false;
	for (int ch = 0; ch < 2; ch++) {
		const uint8_t *h = r->raw + ch * 4;
		int pred = (int16_t)(h[0] | h[1] << 8), idx = h[2];
		if (idx > 88) idx = 88;
		const uint8_t *nib = r->raw + 8 + ch * (BPA_BLOCK / 2);
		int16_t *out = r->buf + ch;
		for (int i = 0; i < BPA_BLOCK; i++) {
			int code = (i & 1) ? nib[i >> 1] >> 4 : nib[i >> 1] & 15;
			int step = STEPS[idx], diff = step >> 3;
			if (code & 4) diff += step;
			if (code & 2) diff += step >> 1;
			if (code & 1) diff += step >> 2;
			pred += (code & 8) ? -diff : diff;
			if (pred > 32767) pred = 32767;
			if (pred < -32768) pred = -32768;
			idx += IDX_ADJ[code & 7];
			if (idx < 0) idx = 0;
			if (idx > 88) idx = 88;
			out[i * 2] = (int16_t)pred;
		}
	}
	r->bufFirst = block * BPA_BLOCK;
	r->bufLen = BPA_BLOCK;
	return true;
}

size_t bpa_read(BpaReader *r, int16_t *dst, size_t frames)
{
	if ((long)frames > r->frames - r->pos) frames = (size_t)(r->frames - r->pos);
	if (!r->adpcm) {
		size_t got = fread(dst, 4, frames, r->fp);
		r->pos += (long)got;
		return got;
	}
	size_t done = 0;
	while (done < frames) {
		if (r->bufFirst < 0 || r->pos < r->bufFirst || r->pos >= r->bufFirst + r->bufLen)
			if (!decode_block(r, r->pos / BPA_BLOCK)) break;
		long off = r->pos - r->bufFirst;
		size_t n = (size_t)(r->bufLen - off);
		if (n > frames - done) n = frames - done;
		memcpy(dst + done * 2, r->buf + off * 2, n * 4);
		done += n; r->pos += (long)n;
	}
	return done;
}
