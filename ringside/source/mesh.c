#include "mesh.h"
#include <stdlib.h>
#include <string.h>

#define MAX_VERTS 30000
#define MAX_IDX   110000

static Vertex   bank_v[MAX_VERTS];
static uint16_t bank_i[MAX_IDX];
static int      nv, ni;
static int      group_v, group_i, in_group;

const Vertex   *mesh_bank_verts(void)        { return bank_v; }
const uint16_t *mesh_bank_indices(void)      { return bank_i; }
int             mesh_bank_vertex_count(void) { return nv; }
int             mesh_bank_index_count(void)  { return ni; }

static void bank_check(int addv, int addi)
{
	if (!in_group && (ni & 1)) bank_i[ni++] = 0;       // keep every mesh's index range 4-byte aligned for the GPU
	if (nv + addv > MAX_VERTS || ni + addi > MAX_IDX) abort();   // grow the bank if this ever trips
}

static void put_vertex(Vec3 p, Vec3 n)
{
	Vertex *v = &bank_v[nv++];
	v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
	v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
	v->col[0] = v->col[1] = v->col[2] = 1.0f;
}

static Vec3 loft_point(const Section *s, int i, int j, int seg)
{
	float a = 2.0f * PI_F * (float)(j % seg) / (float)seg;
	return v3(s[i].ox + s[i].rx * cosf(a), s[i].y, s[i].oz + s[i].rz * sinf(a));
}

Mesh mesh_loft(const Section *s, int n, int seg)
{
	int cols = seg + 1;                                   // duplicated seam column so normals stay smooth
	bank_check(n * cols, (n - 1) * seg * 6);
	Mesh m = { ni, 0, nv, n * cols };
	int base = nv;
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < cols; j++) {
			Vec3 p = loft_point(s, i, j, seg);
			Vec3 tu = v3sub(loft_point(s, i + 1 < n ? i + 1 : i, j, seg), loft_point(s, i > 0 ? i - 1 : i, j, seg));
			Vec3 tv = v3sub(loft_point(s, i, j + 1, seg), loft_point(s, i, j + seg - 1, seg));
			Vec3 nrm = v3cross(tu, tv);
			if (v3len(nrm) < 1e-9f) nrm = v3(0, i == 0 ? -1.0f : 1.0f, 0);   // pole: no ring tangent
			put_vertex(p, v3norm(nrm));
		}
	}
	for (int i = 0; i + 1 < n; i++)
		for (int j = 0; j < seg; j++) {
			uint16_t a = base + i * cols + j, b = base + (i + 1) * cols + j;
			uint16_t c = base + (i + 1) * cols + j + 1, d = base + i * cols + j + 1;
			bank_i[ni++] = a; bank_i[ni++] = b; bank_i[ni++] = c;
			bank_i[ni++] = a; bank_i[ni++] = c; bank_i[ni++] = d;
		}
	m.nidx = ni - m.ioff;
	return m;
}

int sections_ellipsoid(Section *out, int rings, float rx, float ry, float rz)
{
	int n = 0;
	for (int i = 0; i <= rings; i++) {
		float th = PI_F * (float)i / (float)rings;
		float r = sinf(th);
		Section s = { -ry * cosf(th), rx * r, rz * r, 0, 0 };
		out[n++] = s;
	}
	return n;
}

int sections_capsule(Section *out, int cap_rings, float r0, float r1, float len)
{
	int n = 0;
	for (int i = 0; i <= cap_rings; i++) {                // bottom hemisphere, y = -r0 .. 0
		float th = 0.5f * PI_F * (float)i / (float)cap_rings;
		Section s = { -r0 * cosf(th), r0 * sinf(th), r0 * sinf(th), 0, 0 };
		out[n++] = s;
	}
	for (int i = 0; i <= cap_rings; i++) {                // top hemisphere, y = len .. len + r1
		float th = 0.5f * PI_F + 0.5f * PI_F * (float)i / (float)cap_rings;
		Section s = { len - r1 * cosf(th), r1 * sinf(th), r1 * sinf(th), 0, 0 };
		out[n++] = s;
	}
	return n;
}

Mesh mesh_ellipsoid(float rx, float ry, float rz, int seg, int rings)
{
	Section s[MESH_MAX_SECTIONS];
	int n = sections_ellipsoid(s, rings, rx, ry, rz);
	return mesh_loft(s, n, seg);
}

Mesh mesh_capsule(float r0, float r1, float len, int seg, int cap_rings)
{
	Section s[MESH_MAX_SECTIONS];
	int n = sections_capsule(s, cap_rings, r0, r1, len);
	return mesh_loft(s, n, seg);
}

Mesh mesh_box(float sx, float sy, float sz)
{
	bank_check(24, 36);
	static const float N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	Mesh m = { ni, 36, nv, 24 };
	for (int f = 0; f < 6; f++) {
		Vec3 nrm = v3(N[f][0], N[f][1], N[f][2]);
		Vec3 t = fabsf(nrm.y) > 0.5f ? v3(1, 0, 0) : v3(0, 1, 0);
		Vec3 u = v3norm(v3cross(t, nrm)), w = v3cross(nrm, u);
		Vec3 half = v3(sx * 0.5f, sy * 0.5f, sz * 0.5f);
		Vec3 c = v3(nrm.x * half.x, nrm.y * half.y, nrm.z * half.z);
		float ue = fabsf(u.x) * half.x + fabsf(u.y) * half.y + fabsf(u.z) * half.z;
		float we = fabsf(w.x) * half.x + fabsf(w.y) * half.y + fabsf(w.z) * half.z;
		int base = nv;
		for (int k = 0; k < 4; k++) {
			float su = (k == 0 || k == 3) ? -1.0f : 1.0f, sw = (k < 2) ? -1.0f : 1.0f;
			put_vertex(v3add(c, v3add(v3mul(u, su * ue), v3mul(w, sw * we))), nrm);
		}
		// choose winding so the geometric normal matches nrm
		Vec3 p0 = v3(bank_v[base].pos[0], bank_v[base].pos[1], bank_v[base].pos[2]);
		Vec3 p1 = v3(bank_v[base + 1].pos[0], bank_v[base + 1].pos[1], bank_v[base + 1].pos[2]);
		Vec3 p2 = v3(bank_v[base + 2].pos[0], bank_v[base + 2].pos[1], bank_v[base + 2].pos[2]);
		int flip = v3dot(v3cross(v3sub(p1, p0), v3sub(p2, p0)), nrm) < 0;
		static const uint8_t A[6] = { 0, 1, 2, 0, 2, 3 }, B[6] = { 0, 2, 1, 0, 3, 2 };
		for (int k = 0; k < 6; k++) bank_i[ni++] = base + (flip ? B[k] : A[k]);
	}
	return m;
}

Mesh mesh_grid(float w, float d, int nx, int nz)
{
	bank_check((nx + 1) * (nz + 1), nx * nz * 6);
	Mesh m = { ni, 0, nv, (nx + 1) * (nz + 1) };
	int base = nv;
	for (int k = 0; k <= nz; k++)
		for (int i = 0; i <= nx; i++)
			put_vertex(v3((float)i / (float)nx * w - 0.5f * w, 0, (float)k / (float)nz * d - 0.5f * d), v3(0, 1, 0));
	for (int k = 0; k < nz; k++)
		for (int i = 0; i < nx; i++) {
			uint16_t a = base + k * (nx + 1) + i, b = base + (k + 1) * (nx + 1) + i;
			uint16_t c = base + (k + 1) * (nx + 1) + i + 1, dd = base + k * (nx + 1) + i + 1;
			bank_i[ni++] = a; bank_i[ni++] = b; bank_i[ni++] = c;      // +Z is "down" the grid, so (a,b,c) faces +Y
			bank_i[ni++] = a; bank_i[ni++] = c; bank_i[ni++] = dd;
		}
	m.nidx = ni - m.ioff;
	return m;
}

Mesh mesh_disc(float radius, int seg)
{
	bank_check(seg + 2, seg * 3);
	Mesh m = { ni, 0, nv, seg + 2 };
	int base = nv;
	put_vertex(v3(0, 0, 0), v3(0, 1, 0));
	for (int j = 0; j <= seg; j++) {
		float a = 2.0f * PI_F * (float)j / (float)seg;
		put_vertex(v3(radius * cosf(a), 0, radius * sinf(a)), v3(0, 1, 0));
	}
	for (int j = 0; j < seg; j++) {
		bank_i[ni++] = base; bank_i[ni++] = base + 2 + j; bank_i[ni++] = base + 1 + j;
	}
	m.nidx = ni - m.ioff;
	return m;
}

Mesh mesh_star(int points, float outer, float inner)
{
	int n = points * 2;
	bank_check(n + 1, n * 3);
	Mesh m = { ni, 0, nv, n + 1 };
	int base = nv;
	put_vertex(v3(0, 0, 0), v3(0, 1, 0));
	for (int j = 0; j < n; j++) {
		float a = 2.0f * PI_F * (float)j / (float)n, r = (j & 1) ? inner : outer;
		put_vertex(v3(r * cosf(a), 0, r * sinf(a)), v3(0, 1, 0));
	}
	for (int j = 0; j < n; j++) {
		bank_i[ni++] = base; bank_i[ni++] = base + 1 + (j + 1) % n; bank_i[ni++] = base + 1 + j;
	}
	m.nidx = ni - m.ioff;
	return m;
}

void mesh_paint(Mesh m, PaintFn fn, void *ctx)
{
	for (int i = 0; i < m.nv; i++) {
		Vertex *v = &bank_v[m.voff + i];
		fn(v->pos, v->nrm, v->col, ctx);
	}
}

void mesh_place(Mesh m, const Mat4 *xf, float r, float g, float b)
{
	for (int i = 0; i < m.nv; i++) {
		Vertex *v = &bank_v[m.voff + i];
		Vec3 p = mat4_point(xf, v3(v->pos[0], v->pos[1], v->pos[2]));
		Vec3 n = v3norm(mat4_dir(xf, v3(v->nrm[0], v->nrm[1], v->nrm[2])));
		v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
		v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
		v->col[0] *= r; v->col[1] *= g; v->col[2] *= b;
	}
}

void mesh_group_begin(void)
{
	if (ni & 1) bank_i[ni++] = 0;
	group_v = nv; group_i = ni; in_group = 1;
}

Mesh mesh_group_end(void)
{
	Mesh m = { group_i, ni - group_i, group_v, nv - group_v };
	in_group = 0;
	return m;
}
