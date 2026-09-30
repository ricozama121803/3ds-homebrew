// Renderer interface. Two backends implement it: gfx_3ds.c (citro3d + PICA200 vertex shader) and
// host/gfx_host.c (software rasteriser used for PC previews). Both run the same per-vertex lighting model.
#pragma once
#include "vec.h"
#include "mesh.h"

typedef struct {
	float r, g, b;       // base colour
	float spec;          // specular strength
	float wrap;          // diffuse wrap (0 = hard Lambert, ~0.4 = soft skin)
	float rim;           // rim-light strength
	float alpha;         // < 1 draws blended, without writing depth
} Material;

typedef struct {
	Vec3 eye, target;
	float fov_deg;       // vertical field of view
	float iod;           // stereo eye offset (0 = mono); focal plane is at the target distance
} Camera;

typedef struct {
	Vec3 dir[2];         // directional lights: direction *towards* the light, world space (0 = key, 1 = back/rim)
	Vec3 col[2];
	Vec3 sky, ground;    // hemisphere ambient
	Vec3 ppos, pcol;     // one point light (the ring's overhead lamp)
	float pinv;          // 1 / range^2
} Lights;

void gfx_init(void);
void gfx_exit(void);
void gfx_upload_meshes(void);                                  // call once after all meshes are built

// Begin drawing 3D for one eye (-1 left, +1 right; 0 for mono) on the current render target.
void gfx_scene_begin(const Camera *cam, const Lights *lt, int eye);
void gfx_draw(const Mesh *m, const Mat4 *model, const Material *mat);
void gfx_scene_end(void);
// Outline mode: width > 0 pushes vertices out along their normals and draws only back faces (an inverted-hull outline).
void gfx_outline(float width);

// ---- 2D overlay (HUD). Coordinates are in screen pixels: 400x240 for the top screen, 320x240 for the bottom.
enum { GFX_ALIGN_LEFT, GFX_ALIGN_CENTER, GFX_ALIGN_RIGHT };
void gfx2d_frame_begin(void);                                    // once per frame, before any overlay drawing
void gfx2d_rect(float x, float y, float w, float h, unsigned rgba);              // 0xRRGGBBAA
void gfx2d_text(float x, float y, float scale, unsigned rgba, int align, const char *s);
