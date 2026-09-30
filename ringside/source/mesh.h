// Procedural meshes. Everything lives in one shared vertex/index bank so the 3DS build can upload it
// as a single vertex buffer and a single index buffer (see gfx_3ds.c).
#pragma once
#include <stdint.h>
#include "vec.h"

typedef struct { float pos[3], nrm[3], col[3]; } Vertex;      // col is a painted tint multiplied into the material

// Vertex + index range inside the shared bank (indices are already absolute).
typedef struct { int ioff, nidx, voff, nv; } Mesh;

// One cross-section of a loft: an ellipse (rx, rz) centred at (ox, oz) at height y.
typedef struct { float y, rx, rz, ox, oz; } Section;

#define MESH_MAX_SECTIONS 64

// Sweep elliptical sections along +Y; smooth normals. First/last section should normally have zero radius (closed ends).
Mesh mesh_loft(const Section *s, int n, int seg);

// Build section lists for common shapes; return the section count.
int sections_ellipsoid(Section *out, int rings, float rx, float ry, float rz);   // centred at the origin
int sections_capsule(Section *out, int cap_rings, float r0, float r1, float len); // from y=0 (radius r0) to y=len (radius r1)

Mesh mesh_ellipsoid(float rx, float ry, float rz, int seg, int rings);
Mesh mesh_capsule(float r0, float r1, float len, int seg, int cap_rings);
Mesh mesh_box(float sx, float sy, float sz);                                   // centred, flat shaded
Mesh mesh_grid(float w, float d, int nx, int nz);                              // y = 0 plane, centred, normal +Y
Mesh mesh_disc(float radius, int seg);                                         // y = 0, normal +Y
Mesh mesh_star(int points, float outer, float inner);                         // y = 0, normal +Y: a spiky burst for impact effects

// Painting: rewrite the vertex tint from the vertex's own position/normal (in the mesh's local space).
typedef void (*PaintFn)(const float pos[3], const float nrm[3], float col[3], void *ctx);
void mesh_paint(Mesh m, PaintFn fn, void *ctx);

// Static scene building: transform a freshly built mesh in place (and tint it), then merge many into one mesh.
void mesh_place(Mesh m, const Mat4 *xf, float r, float g, float b);
void mesh_group_begin(void);
Mesh mesh_group_end(void);

const Vertex   *mesh_bank_verts(void);
const uint16_t *mesh_bank_indices(void);
int             mesh_bank_vertex_count(void);
int             mesh_bank_index_count(void);
