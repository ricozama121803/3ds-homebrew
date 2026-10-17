// Which gait clips Otto plays at a given ground speed, and how far he travels per cycle. Shared by the game logic (it advances the phase) and the scene builder (it poses him).
#pragma once
#include "otto_mesh.h"
#include "otto_anim.h"

static inline float gait_ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }
#define GAIT_JOG_FROM  1.9f                   // walk -> jog blend starts / ends (m/s)
#define GAIT_JOG_TO    2.9f
#define GAIT_RUN_FROM  3.5f                   // jog -> sprint
#define GAIT_RUN_TO    4.6f
static inline float gait_blend(float speed, int *a, int *b)
{
	if (speed < 3.2f) { *a = 0; *b = 1; return gait_ease((speed - GAIT_JOG_FROM) / (GAIT_JOG_TO - GAIT_JOG_FROM)); }
	*a = 1; *b = 2; return gait_ease((speed - GAIT_RUN_FROM) / (GAIT_RUN_TO - GAIT_RUN_FROM));
}
static inline float gait_stride(float speed)       // metres per full cycle at this speed (clip blend)
{
	int a, b; float t = gait_blend(speed, &a, &b);
	return gait_clips[a].stride + (gait_clips[b].stride - gait_clips[a].stride) * t;
}
