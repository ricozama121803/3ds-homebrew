// Streams a song's raw PCM (s16le stereo, 32 kHz) from romfs through the DSP, and reports the playback position.
#pragma once
#include <stdbool.h>

#define AUDIO_RATE 32000

bool  audio_init(void);                  // false when the DSP can't start (no dspfirm.cdc dumped)
void  audio_exit(void);
bool  audio_open(const char *dir);       // opens <dir>/audio.bpa or audio.pcm (stops whatever was playing)
void  audio_play(int from_ms);           // start (or restart) playback at a position
void  audio_stop(void);
void  audio_pause(bool paused);
void  audio_update(void);                // call once per frame: refills finished buffers
float audio_time_ms(void);               // current playback position (ms)
bool  audio_finished(void);              // reached the end of the file
void  audio_set_volume(float v);         // 0..1
void  audio_miss_sound(void);            // short low "clunk" for a missed note
bool  audio_ok(void);
