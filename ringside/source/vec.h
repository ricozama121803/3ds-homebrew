// Small vector / matrix helpers shared by the 3DS build and the PC preview.
// Mat4 is row-major and used with column vectors: p' = M * p.
#pragma once
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

typedef struct { float x, y, z; } Vec3;
typedef struct { float m[16]; } Mat4;

static inline Vec3  v3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
static inline Vec3  v3add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3  v3sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3  v3mul(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline Vec3  v3madd(Vec3 a, Vec3 b, float s) { return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline float v3dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3  v3cross(Vec3 a, Vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3len(Vec3 a) { return sqrtf(v3dot(a, a)); }
static inline float v3dist(Vec3 a, Vec3 b) { return v3len(v3sub(a, b)); }
static inline Vec3  v3norm(Vec3 a)
{
	float l = v3len(a);
	return l > 1e-8f ? v3mul(a, 1.0f / l) : v3(0, 1, 0);
}
static inline Vec3  v3lerp(Vec3 a, Vec3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smooth01(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
static inline float sqrf(float v) { return v * v; }

static inline Mat4 mat4_identity(void)
{
	Mat4 r;
	memset(&r, 0, sizeof r);
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
	return r;
}

static inline Mat4 mat4_mul(const Mat4 *a, const Mat4 *b)
{
	Mat4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			float s = 0;
			for (int k = 0; k < 4; k++) s += a->m[i * 4 + k] * b->m[k * 4 + j];
			r.m[i * 4 + j] = s;
		}
	return r;
}

// Frame with the given axes (columns) and origin.
static inline Mat4 mat4_basis(Vec3 x, Vec3 y, Vec3 z, Vec3 o)
{
	Mat4 r = { { x.x, y.x, z.x, o.x,
	             x.y, y.y, z.y, o.y,
	             x.z, y.z, z.z, o.z,
	             0, 0, 0, 1 } };
	return r;
}

static inline Mat4 mat4_translate(Vec3 t) { return mat4_basis(v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1), t); }
static inline Mat4 mat4_roty(float a)
{
	float c = cosf(a), s = sinf(a);
	return mat4_basis(v3(c, 0, -s), v3(0, 1, 0), v3(s, 0, c), v3(0, 0, 0));
}
static inline Mat4 mat4_rotx(float a)
{
	float c = cosf(a), s = sinf(a);
	return mat4_basis(v3(1, 0, 0), v3(0, c, s), v3(0, -s, c), v3(0, 0, 0));
}
static inline Mat4 mat4_rotz(float a)
{
	float c = cosf(a), s = sinf(a);
	return mat4_basis(v3(c, s, 0), v3(-s, c, 0), v3(0, 0, 1), v3(0, 0, 0));
}

static inline Vec3 mat4_point(const Mat4 *m, Vec3 p)
{
	return v3(m->m[0] * p.x + m->m[1] * p.y + m->m[2] * p.z + m->m[3],
	          m->m[4] * p.x + m->m[5] * p.y + m->m[6] * p.z + m->m[7],
	          m->m[8] * p.x + m->m[9] * p.y + m->m[10] * p.z + m->m[11]);
}
static inline Vec3 mat4_dir(const Mat4 *m, Vec3 p)
{
	return v3(m->m[0] * p.x + m->m[1] * p.y + m->m[2] * p.z,
	          m->m[4] * p.x + m->m[5] * p.y + m->m[6] * p.z,
	          m->m[8] * p.x + m->m[9] * p.y + m->m[10] * p.z);
}

// Rigid frame whose +Y axis runs from a to b (used to lay a limb mesh between two joints).
// `side` is a hint for the twist about the bone so the limb does not roll randomly.
static inline Mat4 mat4_bone(Vec3 a, Vec3 b, Vec3 side)
{
	Vec3 y = v3norm(v3sub(b, a));
	Vec3 z = v3cross(side, y);
	if (v3len(z) < 1e-4f) z = v3cross(v3(1, 0, 0), y);
	z = v3norm(z);
	Vec3 x = v3cross(y, z);
	return mat4_basis(x, y, z, a);
}

// Right-handed look-at view matrix (camera looks down -Z).
static inline Mat4 mat4_lookat(Vec3 eye, Vec3 target, Vec3 up)
{
	Vec3 f = v3norm(v3sub(target, eye));
	Vec3 s = v3norm(v3cross(f, up));
	Vec3 u = v3cross(s, f);
	Mat4 r = { { s.x, s.y, s.z, -v3dot(s, eye),
	             u.x, u.y, u.z, -v3dot(u, eye),
	             -f.x, -f.y, -f.z, v3dot(f, eye),
	             0, 0, 0, 1 } };
	return r;
}
