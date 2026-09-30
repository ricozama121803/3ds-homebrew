// The stage ("Lantern Alley"): a stone plaza at night with a red fence, pillars, tiered buildings with lit windows,
// paper lanterns and a moon. All of it is static geometry merged into a handful of painted meshes.
#include "scene.h"

static struct {
	Mesh plaza, curb, fence, pillars, buildings, roofs, windows, lanterns, moon, crowd, glow;
	Mesh spark_a, spark_b;
} S;

static unsigned hash2(int a, int b)
{
	unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}
static float rnd01(int a, int b) { return (float)(hash2(a, b) & 0xffff) / 65535.0f; }

// place a freshly built mesh: translate + optional rotation, then tint
static void put(Mesh m, Vec3 pos, float r, float g, float b)
{
	Mat4 t = mat4_translate(pos);
	mesh_place(m, &t, r, g, b);
}
static void put_rot(Mesh m, Vec3 pos, const Mat4 *rot, float r, float g, float b)
{
	Mat4 t = mat4_translate(pos);
	Mat4 x = mat4_mul(&t, rot);
	mesh_place(m, &x, r, g, b);
}

static void build_plaza(void)
{
	mesh_group_begin();
	Mesh base = mesh_grid(22.0f, 14.0f, 4, 4);
	put(base, v3(0, -0.012f, 0.6f), 0.02f, 0.018f, 0.025f);
	for (int iz = -5; iz <= 6; iz++)                                   // stone slabs with mortar gaps between them
		for (int ix = -9; ix <= 9; ix++) {
			Mesh t = mesh_grid(0.74f, 0.74f, 1, 1);
			float n = rnd01(ix, iz), warm = rnd01(ix + 40, iz + 9);
			float v = 0.20f + 0.16f * n;
			put(t, v3(ix * 0.8f, 0.0f, iz * 0.8f), v * (0.92f + 0.10f * warm), v * 0.90f, v * (1.02f - 0.12f * warm));
		}
	S.plaza = mesh_group_end();
}

static void build_curb_fence(void)
{
	mesh_group_begin();
	put(mesh_box(15.0f, 0.22f, 0.34f), v3(0, 0.11f, -3.35f), 0.30f, 0.28f, 0.30f);                   // stone kerb
	S.curb = mesh_group_end();

	mesh_group_begin();
	for (int i = -10; i <= 10; i++) {                                                                  // red lacquered fence
		put(mesh_box(0.09f, 1.05f, 0.09f), v3(i * 0.72f, 0.75f, -3.35f), 0.75f, 0.08f, 0.07f);
		if (i % 2 == 0) put(mesh_ellipsoid(0.07f, 0.07f, 0.07f, 6, 4), v3(i * 0.72f, 1.32f, -3.35f), 0.95f, 0.76f, 0.22f);   // gold finials
	}
	put(mesh_box(15.0f, 0.10f, 0.11f), v3(0, 1.22f, -3.35f), 0.80f, 0.08f, 0.07f);
	put(mesh_box(15.0f, 0.08f, 0.10f), v3(0, 0.62f, -3.35f), 0.70f, 0.07f, 0.06f);
	put(mesh_box(15.0f, 0.06f, 0.08f), v3(0, 0.30f, -3.35f), 0.95f, 0.76f, 0.22f);
	S.fence = mesh_group_end();
}

static void build_pillars(void)
{
	mesh_group_begin();
	static const float px[] = { -3.6f, 4.1f, -10.0f };
	static const float pz[] = { -4.2f, -4.6f, -5.0f };
	for (int i = 0; i < 3; i++) {
		Vec3 p = v3(px[i], 0, pz[i]);
		Section s[10] = {
			{ 0.00f, 0, 0, 0, 0 }, { 0.01f, 0.46f, 0.46f, 0, 0 }, { 0.30f, 0.44f, 0.44f, 0, 0 }, { 0.36f, 0.34f, 0.34f, 0, 0 },
			{ 0.60f, 0.32f, 0.32f, 0, 0 }, { 4.50f, 0.30f, 0.30f, 0, 0 }, { 4.56f, 0.36f, 0.36f, 0, 0 }, { 5.00f, 0.36f, 0.36f, 0, 0 },
			{ 5.06f, 0.30f, 0.30f, 0, 0 }, { 5.10f, 0, 0, 0, 0 } };
		Mesh shaft = mesh_loft(s, 10, 10);
		put(shaft, p, 0.62f, 0.07f, 0.06f);
		Mesh band = mesh_capsule(0.335f, 0.335f, 0.10f, 10, 1);
		put(band, v3(p.x, 3.4f, p.z), 0.95f, 0.76f, 0.22f);
		Mesh band2 = mesh_capsule(0.335f, 0.335f, 0.10f, 10, 1);
		put(band2, v3(p.x, 0.95f, p.z), 0.95f, 0.76f, 0.22f);
		put(mesh_box(1.0f, 0.34f, 1.0f), v3(p.x, 0.17f, p.z), 0.30f, 0.27f, 0.28f);
		put(mesh_box(2.4f, 0.32f, 0.36f), v3(p.x, 4.85f, p.z), 0.55f, 0.06f, 0.05f);                 // bracket beams at the capital
		put(mesh_box(0.36f, 0.30f, 2.0f), v3(p.x, 5.15f, p.z), 0.95f, 0.76f, 0.22f);
	}
	S.pillars = mesh_group_end();
}

static void build_buildings(void)
{
	mesh_group_begin();
	for (int i = 0; i < 16; i++) {                                                                     // near row
		float w = 2.4f + 1.4f * rnd01(i, 1), h = 3.2f + 4.4f * rnd01(i, 2), z = -9.0f - 1.6f * rnd01(i, 3);
		float x = -17.0f + i * 2.25f;
		float c = 0.9f + 0.4f * rnd01(i, 4);
		put(mesh_box(w, h, 2.6f), v3(x, h * 0.5f, z), 0.13f * c, 0.10f * c, 0.19f * c);
	}
	for (int i = 0; i < 12; i++) {                                                                     // far row: silhouettes only
		float w = 3.4f + 2.0f * rnd01(i, 11), h = 5.0f + 6.0f * rnd01(i, 12), x = -22.0f + i * 4.0f;
		put(mesh_box(w, h, 3.0f), v3(x, h * 0.5f, -19.0f - 2.0f * rnd01(i, 13)), 0.07f, 0.06f, 0.12f);
	}
	S.buildings = mesh_group_end();

	mesh_group_begin();                                                                                // swept pagoda roofs
	for (int i = 0; i < 16; i += 2) {
		float w = 2.4f + 1.4f * rnd01(i, 1), h = 3.2f + 4.4f * rnd01(i, 2), z = -9.0f - 1.6f * rnd01(i, 3);
		float x = -17.0f + i * 2.25f;
		put(mesh_box(w + 0.9f, 0.16f, 3.5f), v3(x, h + 0.08f, z + 0.3f), 0.20f, 0.06f, 0.07f);
		put(mesh_box(w + 0.2f, 0.20f, 3.0f), v3(x, h + 0.30f, z + 0.3f), 0.16f, 0.05f, 0.06f);
		put(mesh_box(w * 0.55f, 0.24f, 2.4f), v3(x, h + 0.52f, z + 0.3f), 0.13f, 0.04f, 0.05f);
	}
	S.roofs = mesh_group_end();

	mesh_group_begin();                                                                                // lit windows (glowing)
	Mat4 face = mat4_rotx(PI_F * 0.5f);                                                                // flat quad facing +Z
	for (int i = 0; i < 16; i++) {
		float w = 2.4f + 1.4f * rnd01(i, 1), h = 3.2f + 4.4f * rnd01(i, 2), z = -9.0f - 1.6f * rnd01(i, 3);
		float x = -17.0f + i * 2.25f;
		int cols = (int)(w / 0.7f), rows = (int)((h - 1.0f) / 1.1f);
		for (int r = 0; r < rows; r++)
			for (int c = 0; c < cols; c++) {
				if (rnd01(i * 31 + c, r + 7) < 0.45f) continue;                                        // some windows are dark
				float px = x - w * 0.5f + (c + 0.5f) * (w / cols), py = 0.9f + r * 1.1f;
				float warm = rnd01(c + 5, r + i);
				Mesh q = mesh_grid(0.30f, 0.44f, 1, 1);
				float dim = 0.45f + 0.40f * rnd01(c * 7 + i, r + 3);
				put_rot(q, v3(px, py, z + 1.32f), &face, dim, dim * (0.50f + 0.25f * warm), dim * (0.16f + 0.12f * warm));
			}
	}
	S.windows = mesh_group_end();
}

static void build_lanterns(void)
{
	mesh_group_begin();
	for (int row = 0; row < 3; row++) {                                                                // strings of lanterns across the alley
		int count = 7 - row * 2;
		float z = -3.0f - row * 2.8f, y0 = 3.05f + row * 0.55f, span = 7.5f + row * 2.0f;
		for (int i = 0; i < count; i++) {
			float t = (float)i / (float)(count - 1), x = -span + 2.0f * span * t;
			float y = y0 - 0.50f * sinf(PI_F * t);
			put(mesh_ellipsoid(0.20f, 0.27f, 0.20f, 6, 4), v3(x, y, z), 1.0f, 0.34f + 0.12f * rnd01(i, row), 0.10f);
					}
	}
	S.lanterns = mesh_group_end();

	mesh_group_begin();                                                                                // soft glow halos (drawn blended)
	Mat4 face = mat4_rotx(PI_F * 0.5f);
	for (int row = 0; row < 3; row++) {
		int count = 7 - row * 2;
		float z = -3.0f - row * 2.8f, y0 = 3.05f + row * 0.55f, span = 7.5f + row * 2.0f;
		for (int i = 0; i < count; i++) {
			float t = (float)i / (float)(count - 1), x = -span + 2.0f * span * t;
			float y = y0 - 0.50f * sinf(PI_F * t);
			put_rot(mesh_disc(0.62f, 12), v3(x, y, z + 0.05f), &face, 1.0f, 0.42f, 0.12f);
		}
	}
	S.glow = mesh_group_end();

	mesh_group_begin();
	put_rot(mesh_disc(3.4f, 28), v3(-11.0f, 14.0f, -42.0f), &face, 0.95f, 0.95f, 0.85f);
	S.moon = mesh_group_end();
}

static void build_crowd(void)
{
	mesh_group_begin();                                                                                // bystanders behind the fence
	static const float xs[] = { -5.6f, -4.9f, 2.4f, 5.4f, 6.4f, 0.4f };
	for (int i = 0; i < 6; i++) {
		float x = xs[i], z = -4.6f - 0.4f * (i % 3);
		float r = 0.10f + 0.3f * rnd01(i, 20), g = 0.10f + 0.25f * rnd01(i, 21), b = 0.12f + 0.3f * rnd01(i, 22);
		put(mesh_capsule(0.24f, 0.20f, 0.75f, 7, 2), v3(x, 0.30f, z), r, g, b);
		put(mesh_ellipsoid(0.13f, 0.15f, 0.13f, 6, 4), v3(x, 1.52f + 0.02f * (i % 2), z), 0.62f + 0.12f * rnd01(i, 23), 0.44f, 0.34f);
			}
	S.crowd = mesh_group_end();
}

void scene_build(void)
{
	S.spark_a = mesh_star(9, 0.62f, 0.17f);
	S.spark_b = mesh_star(6, 0.36f, 0.11f);
	build_plaza();
	build_curb_fence();
	build_pillars();
	build_buildings();
	build_lanterns();
	build_crowd();
}

Lights scene_lights(void)
{
	Lights l;
	l.dir[0] = v3norm(v3(-0.45f, 0.70f, 0.55f)); l.col[0] = v3(1.00f, 0.72f, 0.46f);       // warm lantern key from the front-left
	l.dir[1] = v3norm(v3(0.60f, 0.30f, -0.75f)); l.col[1] = v3(0.34f, 0.52f, 1.00f);      // cold moonlight rim from behind
	l.sky = v3(0.20f, 0.22f, 0.36f); l.ground = v3(0.26f, 0.15f, 0.09f);
	l.ppos = v3(0.0f, 4.4f, 0.8f); l.pcol = v3(0.95f, 0.50f, 0.22f); l.pinv = 1.0f / (10.0f * 10.0f);
	return l;
}

Vec3 scene_clear_top(void)    { return v3(0.03f, 0.03f, 0.10f); }
Vec3 scene_clear_bottom(void) { return v3(0.10f, 0.05f, 0.10f); }

// ---- camera -------------------------------------------------------------------------------
void camera_update(CamState *cs, const Fighter *a, const Fighter *b, float dt, float time, Camera *out)
{
	float ax = a->x - cosf(a->yaw) * 0.9f * a->fall, az = a->z + sinf(a->yaw) * 0.9f * a->fall;   // a fallen fighter lies behind their feet
	float bx = b->x - cosf(b->yaw) * 0.9f * b->fall, bz = b->z + sinf(b->yaw) * 0.9f * b->fall;
	Vec3 mid = v3((ax + bx) * 0.5f, 0.98f, (az + bz) * 0.5f);
	float spread = sqrtf(sqrf(ax - bx) + sqrf(az - bz));
	float want = clampf(4.3f + spread * 1.05f, 5.3f, 8.5f) - cs->zoom_pulse * 0.18f;
	if (!cs->have_state) { cs->focus = mid; cs->dist = want; cs->have_state = 1; }
	float k = 1.0f - expf(-dt * 6.0f);
	cs->focus = v3lerp(cs->focus, mid, k);
	cs->dist = lerpf(cs->dist, want, k);
	cs->shake *= expf(-dt * 9.0f);
	cs->zoom_pulse *= expf(-dt * 7.0f);

	float ang = CAM_ANGLE;                                    // a slight 3/4 view so both fighters read in 3D
	float sx = cs->shake * sinf(time * 91.0f), sy = cs->shake * sinf(time * 77.0f + 1.0f);
	out->target = v3(cs->focus.x + sx * 0.05f, cs->focus.y + 0.10f + sy * 0.05f, cs->focus.z);
	out->eye = v3(cs->focus.x + sinf(ang) * cs->dist, 1.35f, cs->focus.z + cosf(ang) * cs->dist);
	out->fov_deg = 30.0f;
	out->iod = 0.0f;
}

// ---- drawing ------------------------------------------------------------------------------
void scene_draw(const Fighter *a, const Fighter *b, float time, const Fx *fx, int nfx, float cam_yaw)
{
	static const Material stone = { 1, 1, 1, 0.20f, 0.10f, 0.30f, 1 };                     // wet-ish plaza: a little specular
	static const Material matte = { 1, 1, 1, 0.04f, 0.20f, 0.35f, 1 };
	static const Material lacquer = { 1, 1, 1, 0.55f, 0.15f, 0.55f, 1 };
	static const Material emit = { 4.0f, 4.0f, 4.0f, 0, 0, 0, 1 };                          // self-lit (saturates to the painted colour)
	static const Material halo = { 1, 1, 1, 0, 0, 0, 0.13f };
	Mat4 id = mat4_identity();

	gfx_draw(&S.moon, &id, &emit);
	gfx_draw(&S.buildings, &id, &matte);
	gfx_draw(&S.roofs, &id, &matte);
	gfx_draw(&S.windows, &id, &emit);
	gfx_draw(&S.plaza, &id, &stone);
	gfx_draw(&S.curb, &id, &matte);
	gfx_draw(&S.fence, &id, &lacquer);
	gfx_draw(&S.pillars, &id, &lacquer);
	gfx_draw(&S.lanterns, &id, &emit);

	Mat4 sway = mat4_translate(v3(0, 0.03f * sinf(time * 2.0f), 0));                         // the crowd bobs a little
	gfx_draw(&S.crowd, &sway, &matte);

	// fighters: opaque first, then blended shadows and lantern halos
	fighter_draw(a);
	fighter_draw(b);
	fighter_draw_shadow(a);
	fighter_draw_shadow(b);
	fighter_draw_trail(a);
	fighter_draw_trail(b);
	gfx_draw(&S.glow, &id, &halo);

	// impact sparks: camera-facing bursts that flare and fade
	for (int i = 0; i < nfx; i++) {
		static const float life[3] = { 0.24f, 0.26f, 0.60f };
		static const float col[3][2][3] = { { { 1.0f, 0.50f, 0.08f }, { 1.0f, 0.95f, 0.65f } },
		                                    { { 0.35f, 0.65f, 1.00f }, { 0.85f, 0.95f, 1.00f } },
		                                    { { 1.0f, 0.15f, 0.08f }, { 1.0f, 0.90f, 0.75f } } };
		const Fx *e = &fx[i];
		int k = e->kind < 3 ? e->kind : 0;
		float u = e->t / life[k];
		if (u >= 1.0f) continue;
		float grow = 0.45f + 1.15f * (1.0f - (1.0f - u) * (1.0f - u));
		Vec3 toward = v3(sinf(cam_yaw), 0.0f, cosf(cam_yaw));
		Vec3 p = v3madd(e->pos, toward, 0.30f);
		for (int layer = 0; layer < 2; layer++) {
			float sc = e->size * grow * (layer ? 0.55f : 1.0f);
			float spin = e->t * (layer ? -7.0f : 4.0f) + (float)i;
			Mat4 r1 = mat4_roty(cam_yaw), r2 = mat4_rotx(PI_F * 0.5f), r3 = mat4_roty(spin);
			Mat4 m = mat4_mul(&r1, &r2); m = mat4_mul(&m, &r3);
			Mat4 sc_m = mat4_basis(v3(sc, 0, 0), v3(0, 1, 0), v3(0, 0, sc), v3(0, 0, 0));
			m = mat4_mul(&m, &sc_m);
			m.m[3] = p.x; m.m[7] = p.y + 0.02f * layer; m.m[11] = p.z;
			Material mat = { 4.0f * col[k][layer][0], 4.0f * col[k][layer][1], 4.0f * col[k][layer][2], 0, 0, 0, (layer ? 0.95f : 0.80f) * (1.0f - u) };
			gfx_draw(layer ? &S.spark_b : &S.spark_a, &m, &mat);
		}
	}
}
