// Extra entry points only the PC preview uses.
#pragma once
#include "../source/vec.h"

void gfx_host_setup(int w, int h, int ss);
void gfx_host_clear(Vec3 top, Vec3 bottom);
void gfx_host_rect(float x0, float y0, float x1, float y1, Vec3 c, float a);
void gfx_host_write_ppm(const char *path);
long gfx_host_triangles(void);
long gfx_host_submitted(void);                       // triangles sent to the GPU (what the 3DS pays for)
