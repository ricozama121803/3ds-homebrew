// The arena: ring, ropes, lights, backdrop and the fight camera.
#pragma once
#include "fighter.h"

#define RING_HALF 2.9f            // playable half-size of the ring (metres); the ropes sit at RING_HALF + 0.1

typedef struct {
	float shake;                  // camera shake amount (decays)
	float zoom_pulse;             // brief punch-in on heavy hits
	Vec3  focus;                  // smoothed camera focus
	float dist;                   // smoothed camera distance
	int   have_state;
} CamState;

// A short-lived impact effect (spark burst) in world space.
#define MAX_FX 8
#define CAM_ANGLE 0.30f          // camera orbit angle about Y (radians), shared with the sparks so they face the lens
typedef struct { Vec3 pos; float t; int kind; float size; } Fx;   // kind: 0 hit, 1 block, 2 knockout; t counts up in seconds

void scene_build(void);
Lights scene_lights(void);
void camera_update(CamState *cs, const Fighter *a, const Fighter *b, float dt, float time, Camera *out);
void scene_draw(const Fighter *a, const Fighter *b, float time, const Fx *fx, int nfx, float cam_yaw);
Vec3 scene_clear_top(void);
Vec3 scene_clear_bottom(void);
