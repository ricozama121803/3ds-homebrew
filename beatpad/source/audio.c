// NDSP streaming: a ring of wave buffers refilled from the song file as the DSP finishes them.
// The play position is read from the DSP (the buffer it is playing + its sample position), so the note
// highway follows what you actually hear rather than a separate timer.
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "bpa.h"

#define CH_MUSIC  0
#define CH_SFX    1
#define CHUNK     4096                 // stereo frames per buffer (128 ms)
#define NBUF      6                    // 768 ms queued

static bool ok, playing, paused, eof;
static FILE *fp;
static char *iobuf;
static BpaReader rd;
static long totalFrames, nextFrame;
static int ring;
static s16 *pcm;
static ndspWaveBuf wb[NBUF];
static long bufStart[NBUF];
static float lastPos;
static float volume = 1.0f;

static s16 *sfx;
static ndspWaveBuf sfxBuf;
#define SFX_FRAMES 2600

bool audio_ok(void) { return ok; }

bool audio_init(void)
{
	if (R_FAILED(ndspInit())) return false;
	ok = true;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(CH_MUSIC);
	ndspChnSetInterp(CH_MUSIC, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(CH_MUSIC, AUDIO_RATE);
	ndspChnSetFormat(CH_MUSIC, NDSP_FORMAT_STEREO_PCM16);
	pcm = linearAlloc(NBUF * CHUNK * 4);
	iobuf = malloc(64 * 1024);
	audio_set_volume(1.0f);

	// miss sound: a decaying 70 Hz thump with a little noise, mono
	ndspChnReset(CH_SFX);
	ndspChnSetInterp(CH_SFX, NDSP_INTERP_LINEAR);
	ndspChnSetRate(CH_SFX, AUDIO_RATE);
	ndspChnSetFormat(CH_SFX, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = { 0.55f, 0.55f };
	ndspChnSetMix(CH_SFX, mix);
	sfx = linearAlloc(SFX_FRAMES * 2);
	unsigned r = 1;
	for (int i = 0; i < SFX_FRAMES; i++) {
		float t = (float)i / AUDIO_RATE, env = expf(-t * 38.0f);
		r = r * 1103515245u + 12345u;
		float noise = ((r >> 16) & 0x7fff) / 16384.0f - 1.0f;
		float v = (sinf(2 * 3.14159f * 70 * t) * 0.8f + noise * 0.25f * expf(-t * 90.0f)) * env;
		sfx[i] = (s16)(v * 26000);
	}
	DSP_FlushDataCache(sfx, SFX_FRAMES * 2);
	memset(&sfxBuf, 0, sizeof sfxBuf);
	sfxBuf.status = NDSP_WBUF_DONE;
	return true;
}

void audio_exit(void)
{
	if (!ok) return;
	audio_stop();
	if (fp) fclose(fp);
	fp = NULL;
	ndspChnWaveBufClear(CH_SFX);
	linearFree(pcm); linearFree(sfx);
	free(iobuf);
	ndspExit();
	ok = false;
}

bool audio_open(const char *dir)
{
	if (!ok) return false;
	audio_stop();
	if (fp) fclose(fp);
	char path[256];
	snprintf(path, sizeof path, "%s/audio.bpa", dir);          // compressed (new songs)
	fp = fopen(path, "rb");
	if (!fp) { snprintf(path, sizeof path, "%s/audio.pcm", dir); fp = fopen(path, "rb"); }
	if (!fp) return false;
	setvbuf(fp, iobuf, _IOFBF, 64 * 1024);
	if (!bpa_open(&rd, fp)) { fclose(fp); fp = NULL; return false; }
	totalFrames = rd.frames;
	return true;
}

static bool fill(int i)
{
	long n = totalFrames - nextFrame;
	if (n > CHUNK) n = CHUNK;
	if (n <= 0) { eof = true; return false; }
	s16 *dst = pcm + i * CHUNK * 2;
	size_t got = bpa_read(&rd, dst, (size_t)n);
	if (got == 0) { eof = true; return false; }
	DSP_FlushDataCache(dst, got * 4);
	memset(&wb[i], 0, sizeof wb[i]);
	wb[i].data_vaddr = dst;
	wb[i].nsamples = (u32)got;
	bufStart[i] = nextFrame;
	nextFrame += (long)got;
	ndspChnWaveBufAdd(CH_MUSIC, &wb[i]);
	return true;
}

void audio_stop(void)
{
	if (!ok) return;
	bool was = playing;
	ndspChnWaveBufClear(CH_MUSIC);
	playing = false;
	if (was) svcSleepThread(12 * 1000 * 1000);    // let the DSP drop the buffers before they get rewritten
}

void audio_play(int from_ms)
{
	if (!ok || !fp) return;
	audio_stop();
	if (from_ms < 0) from_ms = 0;
	nextFrame = (long)((long long)from_ms * AUDIO_RATE / 1000);
	if (nextFrame > totalFrames) nextFrame = totalFrames;
	bpa_seek(&rd, nextFrame);
	eof = false; paused = false;
	lastPos = from_ms;
	ring = 0;
	for (int i = 0; i < NBUF; i++) wb[i].status = NDSP_WBUF_DONE;
	for (int i = 0; i < NBUF; i++) if (!fill(i)) break;
	ndspChnSetPaused(CH_MUSIC, false);
	playing = true;
}

void audio_pause(bool p)
{
	if (!ok) return;
	paused = p;
	ndspChnSetPaused(CH_MUSIC, p);
}

void audio_update(void)
{
	if (!ok || !playing) return;
	for (int k = 0; k < NBUF && wb[ring].status == NDSP_WBUF_DONE && !eof; k++) {
		if (!fill(ring)) break;
		ring = (ring + 1) % NBUF;
	}
}

float audio_time_ms(void)
{
	if (!ok || !playing) return lastPos;
	u16 seq = ndspChnGetWaveBufSeq(CH_MUSIC);
	for (int i = 0; i < NBUF; i++) {
		if (wb[i].sequence_id == seq && wb[i].status != NDSP_WBUF_DONE) {
			float pos = (bufStart[i] + (long)ndspChnGetSamplePos(CH_MUSIC)) * 1000.0f / AUDIO_RATE;
			if (pos > lastPos || pos < lastPos - 1000) lastPos = pos;   // monotonic, except after a seek
			return lastPos;
		}
	}
	if (audio_finished()) lastPos = totalFrames * 1000.0f / AUDIO_RATE;
	return lastPos;
}

bool audio_finished(void)
{
	if (!ok || !playing) return false;
	if (!eof) return false;
	for (int i = 0; i < NBUF; i++)
		if (wb[i].status == NDSP_WBUF_QUEUED || wb[i].status == NDSP_WBUF_PLAYING) return false;
	return true;
}

void audio_set_volume(float v)
{
	volume = v;
	if (!ok) return;
	float mix[12] = { v, v };
	ndspChnSetMix(CH_MUSIC, mix);
}

void audio_miss_sound(void)
{
	if (!ok) return;
	if (sfxBuf.status == NDSP_WBUF_QUEUED || sfxBuf.status == NDSP_WBUF_PLAYING) return;
	memset(&sfxBuf, 0, sizeof sfxBuf);
	sfxBuf.data_vaddr = sfx;
	sfxBuf.nsamples = SFX_FRAMES;
	ndspChnWaveBufAdd(CH_SFX, &sfxBuf);
}
