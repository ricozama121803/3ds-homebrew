// Song audio reader: BeatPad ADPCM (.bpa, see tools/bpa.py) or raw s16le stereo PCM, with seeking.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BPA_BLOCK 2048

typedef struct {
	FILE *fp;
	bool adpcm;
	long frames;                 // total stereo frames
	long pos;                    // next frame to return
	long bufFirst;               // frame index of buf[0] (adpcm)
	int bufLen;
	int16_t buf[BPA_BLOCK * 2];  // one decoded block, interleaved L R
	uint8_t raw[8 + BPA_BLOCK];
} BpaReader;

bool   bpa_open(BpaReader *r, FILE *fp);       // takes ownership of fp's position; detects the format
void   bpa_seek(BpaReader *r, long frame);
size_t bpa_read(BpaReader *r, int16_t *dst, size_t frames);   // interleaved stereo; returns frames read
