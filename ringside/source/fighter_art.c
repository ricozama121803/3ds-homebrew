// Fighter meshes, painting, costumes and drawing. Two body sets: 0 = the player (lean, headband, fighting pants)
// and 1 = the rival (heavier, bald and bearded, gold-trimmed trunks). Colour lives in painted vertex tints
// so each part is a single draw call.
#include "fighter.h"
#include <string.h>

typedef struct {
	float kc, ka, kl, kh;                       // build: chest/shoulder width, arm, leg and head thickness
	int   pants, bald;
	float skin[3], cloth[3], stripe[3], sash[3], hair[3], tape[3];
} BodySpec;

static const BodySpec SPEC[2] = {
	{ 1.02f, 1.05f, 1.14f, 1.00f, 1, 0,
	  { 0.90f, 0.66f, 0.50f }, { 0.11f, 0.14f, 0.27f }, { 0.92f, 0.92f, 0.96f }, { 0.86f, 0.10f, 0.12f }, { 0.09f, 0.07f, 0.06f }, { 0.95f, 0.95f, 0.90f } },
	{ 1.12f, 1.24f, 1.10f, 0.96f, 0, 1,
	  { 0.54f, 0.34f, 0.24f }, { 0.05f, 0.05f, 0.07f }, { 0.96f, 0.78f, 0.20f }, { 0.96f, 0.78f, 0.20f }, { 0.06f, 0.05f, 0.05f }, { 0.90f, 0.85f, 0.70f } },
};

typedef struct {
	Mesh pelvis, belt, torso, pec, delt, trap, ab;
	Mesh neck, skull, jaw, brow, nose, ear, eye, cheek, spikes, band, beard, must;
	Mesh upper_arm, bicep, fore_arm, flexor, glove, cuff, thumb;
	Mesh thigh, quad, cloth_thigh, shin, calf, cloth_shin, hem, foot;
} Parts;

static Parts P[2];
static Mesh SHADOW;
static int g_detail = 1;
#define Q(hi, lo) (g_detail ? (hi) : (lo))               // mesh density: New 3DS / Old 3DS

void fighter_set_detail(int high) { g_detail = high; }

// ---- painters ------------------------------------------------------------------------------
// All coordinates are in the mesh's own local space.
static void paint_torso(const float p[3], const float n[3], float c[3], void *ctx)
{
	(void)n; (void)ctx;
	float x = p[0], y = p[1], z = p[2];
	float v = 1.0f, front = clampf(x * 9.0f, 0.0f, 1.0f);
	if (fabsf(z) < 0.024f && y > -0.02f && y < 0.30f) v *= 1.0f - 0.26f * front;                       // linea alba
	static const float rows[3] = { 0.045f, 0.118f, 0.190f };
	for (int i = 0; i < 3; i++)
		if (fabsf(y - rows[i]) < 0.017f && fabsf(z) < 0.105f) v *= 1.0f - 0.22f * front;                // ab grooves
	if (fabsf(y - 0.272f) < 0.022f && fabsf(z) > 0.012f && fabsf(z) < 0.17f) v *= 1.0f - 0.24f * front; // under the pecs
	if (fabsf(z) < 0.020f && y > 0.28f && y < 0.42f) v *= 1.0f - 0.20f * front;                        // sternum
	if (fabsf(z) > 0.19f && y > 0.20f && y < 0.38f) v *= 0.88f;                                        // lats / armpits
	v *= 0.90f + 0.10f * clampf((y + 0.05f) / 0.5f, 0.0f, 1.0f);                                        // slightly darker at the belly
	c[0] = v; c[1] = v * 0.97f; c[2] = v * 0.96f;                                                       // warm shadows
}

static void paint_skull(const float p[3], const float n[3], float c[3], void *ctx)
{
	(void)n;
	const BodySpec *sp = ctx;
	float x = p[0], y = p[1], z = p[2];
	if (!sp->bald) {                                                     // hair covers the top and back
		float line = 0.062f - 0.14f * clampf(-x / 0.10f, 0.0f, 1.0f);
		if (y > line && !(x > 0.05f && fabsf(z) > 0.055f && y < 0.09f)) {
			for (int i = 0; i < 3; i++) c[i] = sp->hair[i] / sp->skin[i];
			return;
		}
	}
	float v = 1.0f;
	if (x > 0.06f && fabsf(z) < 0.07f && y > 0.005f && y < 0.055f) v = 0.84f;   // eye sockets
	c[0] = v; c[1] = v * 0.96f; c[2] = v * 0.95f;
}

static void paint_foot(const float p[3], const float n[3], float c[3], void *ctx)
{
	(void)n;
	const BodySpec *sp = ctx;
	float y = p[1];
	if (y > 0.015f && y < 0.105f) { for (int i = 0; i < 3; i++) c[i] = sp->tape[i] / sp->skin[i]; }   // wrapped instep
}

static void paint_cloth(const float p[3], const float n[3], float c[3], void *ctx)
{
	(void)n;
	const BodySpec *sp = ctx;
	float v = 1.0f;
	if (p[0] > 0.0f && fabsf(p[2]) < 0.026f) { c[0] = sp->stripe[0]; c[1] = sp->stripe[1]; c[2] = sp->stripe[2]; return; }   // side stripe
	c[0] = sp->cloth[0] * v; c[1] = sp->cloth[1] * v; c[2] = sp->cloth[2] * v;
}

static void paint_flat(const float p[3], const float n[3], float c[3], void *ctx)
{
	(void)p; (void)n;
	const float *k = ctx;
	c[0] = k[0]; c[1] = k[1]; c[2] = k[2];
}

// ---- mesh helpers ---------------------------------------------------------------------------
static Mesh loft_keys(const float (*keys)[3], int nk, int sections, float sx, float sz, int seg)
{
	Section s[MESH_MAX_SECTIONS];
	int n = 0;
	for (int i = 0; i < sections; i++) {
		float t = (float)i / (float)(sections - 1) * (float)(nk - 1);
		int k = (int)t; if (k >= nk - 1) k = nk - 2;
		float f = t - (float)k;
		f = f * f * (3.0f - 2.0f * f) * 0.5f + f * 0.5f;                    // ease between keys so the silhouette stays smooth
		Section sec = { lerpf(keys[k][0], keys[k + 1][0], f), lerpf(keys[k][1], keys[k + 1][1], f) * sx, lerpf(keys[k][2], keys[k + 1][2], f) * sz, 0, 0 };
		s[n++] = sec;
	}
	return mesh_loft(s, n, seg);
}

static Mesh spikes_mesh(void)
{
	// anime-style swept-back hair spikes, in the head frame (origin at the base of the neck)
	static const float sp[][6] = {          // base x,y,z and direction x,y,z
		{  0.050f, 0.300f,  0.000f,  0.35f, 1.00f,  0.00f }, { -0.020f, 0.312f,  0.000f, -0.10f, 1.00f,  0.00f },
		{ -0.090f, 0.290f,  0.000f, -0.65f, 0.80f,  0.00f }, {  0.020f, 0.298f,  0.055f,  0.10f, 1.00f,  0.45f },
		{  0.020f, 0.298f, -0.055f,  0.10f, 1.00f, -0.45f }, { -0.060f, 0.280f,  0.070f, -0.45f, 0.75f,  0.55f },
		{ -0.060f, 0.280f, -0.070f, -0.45f, 0.75f, -0.55f }, { -0.115f, 0.240f,  0.000f, -1.00f, 0.35f,  0.00f },
	};
	mesh_group_begin();
	for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) {
		Mesh m = mesh_ellipsoid(0.034f, 0.085f, 0.034f, 6, 4);
		Vec3 base = v3(sp[i][0], sp[i][1], sp[i][2]), dir = v3norm(v3(sp[i][3], sp[i][4], sp[i][5]));
		Mat4 xf = mat4_bone(v3madd(base, dir, 0.045f), v3madd(base, dir, 1.0f), v3(0, 0, 1));
		mesh_place(m, &xf, 1, 1, 1);
	}
	return mesh_group_end();
}

// ---- building ---------------------------------------------------------------------------------
static void build_body(int id)
{
	const BodySpec *sp = &SPEC[id];
	Parts *p = &P[id];
	Section s[MESH_MAX_SECTIONS];
	int n;
	const float kc = sp->kc, ka = sp->ka, kl = sp->kl, kh = sp->kh;

	// torso, waist pivot at the origin: a real V-taper with wide shoulders
	static const float T[12][3] = {
		{ -0.060f, 0.000f, 0.000f }, { -0.045f, 0.100f, 0.148f }, { 0.020f, 0.104f, 0.150f }, { 0.100f, 0.114f, 0.168f },
		{ 0.180f, 0.126f, 0.205f }, { 0.260f, 0.140f, 0.236f }, { 0.340f, 0.150f, 0.252f }, { 0.410f, 0.138f, 0.246f },
		{ 0.455f, 0.108f, 0.216f }, { 0.490f, 0.078f, 0.140f }, { 0.522f, 0.062f, 0.088f }, { 0.540f, 0.000f, 0.000f } };
	p->torso = loft_keys(T, 12, Q(18, 13), kc, kc * 1.02f, Q(22, 16));
	mesh_paint(p->torso, paint_torso, NULL);
	p->pec  = mesh_ellipsoid(0.052f * kc, 0.070f * kc, 0.105f * kc, Q(10, 8), Q(6, 5));
	p->delt = mesh_ellipsoid(0.078f * ka, 0.090f * ka, 0.078f * ka, Q(10, 8), Q(6, 5));
	p->trap = mesh_ellipsoid(0.070f * kc, 0.045f * kc, 0.115f * kc, Q(8, 6), Q(5, 4));
	p->ab   = mesh_ellipsoid(0.032f * kc, 0.030f, 0.048f * kc, 7, 5);

	// pelvis: pants / trunks, plus the belt or waistband
	{
		const Section h[] = { { -0.13f, 0, 0, 0, 0 }, { -0.10f, 0.11f * kl, 0.16f * kl, 0, 0 }, { -0.03f, 0.145f * kl, 0.192f * kl, 0, 0 },
		                      { 0.05f, 0.135f * kl, 0.178f * kl, 0, 0 }, { 0.10f, 0.122f * kl, 0.168f * kl, 0, 0 }, { 0.118f, 0, 0, 0, 0 } };
		p->pelvis = mesh_loft(h, 6, Q(16, 12));
		mesh_paint(p->pelvis, paint_flat, (void *)sp->cloth);
		const Section b[] = { { 0.070f, 0, 0, 0, 0 }, { 0.074f, 0.132f * kl, 0.178f * kl, 0, 0 }, { 0.120f, 0.127f * kl, 0.172f * kl, 0, 0 }, { 0.124f, 0, 0, 0, 0 } };
		p->belt = mesh_loft(b, 4, Q(16, 12));
		mesh_paint(p->belt, paint_flat, (void *)sp->sash);
	}

	// head: skull + jaw + brow + face, origin at the base of the neck
	n = sections_capsule(s, 3, 0.075f * kh, 0.068f * kh, 0.11f); p->neck = mesh_loft(s, n, Q(10, 8));
	mesh_paint(p->neck, paint_torso, NULL);
	p->skull = mesh_ellipsoid(0.106f * kh, 0.118f * kh, 0.092f * kh, Q(16, 12), Q(12, 8));
	mesh_paint(p->skull, paint_skull, (void *)sp);
	p->jaw   = mesh_ellipsoid(0.078f * kh, 0.064f * kh, 0.080f * kh, Q(12, 9), Q(6, 5));
	p->brow  = mesh_ellipsoid(0.030f, 0.020f, 0.090f * kh, 8, 5);
	p->nose  = mesh_ellipsoid(0.030f, 0.034f, 0.018f, 6, 5);
	p->ear   = mesh_ellipsoid(0.016f, 0.030f, 0.011f, 6, 4);
	p->eye   = mesh_ellipsoid(0.010f, 0.008f, 0.021f, 5, 3);
	p->cheek = mesh_ellipsoid(0.040f, 0.030f, 0.032f, 7, 5);
	if (!sp->bald) {
		p->spikes = spikes_mesh();
		const Section b[] = { { 0.0f, 0, 0, 0, 0 }, { 0.002f, 0.113f * kh, 0.099f * kh, 0, 0 }, { 0.026f, 0.111f * kh, 0.097f * kh, 0, 0 }, { 0.028f, 0, 0, 0, 0 } };
		p->band = mesh_loft(b, 4, Q(16, 12));
		mesh_paint(p->band, paint_flat, (void *)sp->sash);
	} else {
		p->beard = mesh_ellipsoid(0.072f * kh, 0.060f * kh, 0.088f * kh, Q(12, 9), Q(6, 5));
		p->must  = mesh_ellipsoid(0.022f, 0.011f, 0.052f, 6, 3);
	}

	// arms (bone frame: +Y from joint to joint)
	n = sections_capsule(s, Q(4, 2), 0.064f * ka, 0.054f * ka, UPPER_ARM); p->upper_arm = mesh_loft(s, n, Q(12, 9));
	p->bicep = mesh_ellipsoid(0.058f * ka, 0.115f, 0.058f * ka, Q(9, 7), Q(5, 4));
	n = sections_capsule(s, Q(4, 2), 0.054f * ka, 0.041f * ka, FORE_ARM);  p->fore_arm = mesh_loft(s, n, Q(12, 9));
	p->flexor = mesh_ellipsoid(0.056f * ka, 0.095f, 0.054f * ka, Q(9, 7), Q(5, 4));
	{   // glove: origin at the wrist, points along the forearm; big and round
		const float k = 1.16f;
		const Section g[] = { { -0.025f, 0, 0, 0, 0 }, { -0.02f, 0.050f * k, 0.050f * k, 0, 0 }, { 0.035f, 0.056f * k, 0.056f * k, 0, 0 },
		                      { 0.075f, 0.084f * k, 0.088f * k, 0, 0 }, { 0.120f, 0.092f * k, 0.096f * k, 0, 0 }, { 0.170f, 0.084f * k, 0.088f * k, 0, 0 },
		                      { 0.208f, 0.056f * k, 0.058f * k, 0, 0 }, { 0.222f, 0, 0, 0, 0 } };
		p->glove = mesh_loft(g, 8, Q(14, 10));
		const Section c[] = { { 0.0f, 0, 0, 0, 0 }, { 0.004f, 0.062f, 0.062f, 0, 0 }, { 0.056f, 0.064f, 0.064f, 0, 0 }, { 0.060f, 0, 0, 0, 0 } };
		p->cuff = mesh_loft(c, 4, Q(12, 9));
		p->thumb = mesh_ellipsoid(0.030f, 0.058f, 0.032f, 7, 5);
	}

	// legs
	n = sections_capsule(s, Q(4, 2), 0.100f * kl, 0.070f * kl, THIGH); p->thigh = mesh_loft(s, n, Q(14, 10));
	p->quad = mesh_ellipsoid(0.090f * kl, 0.17f, 0.090f * kl, Q(10, 8), Q(6, 5));
	n = sections_capsule(s, Q(4, 2), 0.068f * kl, 0.046f * kl, SHIN);  p->shin = mesh_loft(s, n, Q(12, 9));
	p->calf = mesh_ellipsoid(0.066f * kl, 0.105f, 0.066f * kl, Q(9, 7), Q(5, 4));
	if (sp->pants) {
		n = sections_capsule(s, Q(3, 2), 0.122f * kl, 0.094f * kl, 0.42f); p->cloth_thigh = mesh_loft(s, n, Q(18, 14));
		mesh_paint(p->cloth_thigh, paint_cloth, (void *)sp);
		const Section sh[] = { { -0.02f, 0, 0, 0, 0 }, { -0.01f, 0.086f * kl, 0.086f * kl, 0, 0 }, { 0.14f, 0.080f * kl, 0.080f * kl, 0, 0 },
		                       { 0.30f, 0.080f * kl, 0.080f * kl, 0, 0 }, { 0.38f, 0.098f * kl, 0.098f * kl, 0, 0 }, { 0.415f, 0.100f * kl, 0.100f * kl, 0, 0 }, { 0.42f, 0, 0, 0, 0 } };
		p->cloth_shin = mesh_loft(sh, 7, Q(18, 14));
		mesh_paint(p->cloth_shin, paint_cloth, (void *)sp);
	} else {
		n = sections_capsule(s, Q(3, 2), 0.128f * kl, 0.112f * kl, 0.20f); p->cloth_thigh = mesh_loft(s, n, Q(16, 12));
		mesh_paint(p->cloth_thigh, paint_flat, (void *)sp->cloth);
		const Section hm[] = { { 0.0f, 0, 0, 0, 0 }, { 0.003f, 0.121f * kl, 0.121f * kl, 0, 0 }, { 0.036f, 0.121f * kl, 0.121f * kl, 0, 0 }, { 0.040f, 0, 0, 0, 0 } };
		p->hem = mesh_loft(hm, 4, Q(16, 12));
		mesh_paint(p->hem, paint_flat, (void *)sp->stripe);
	}
	{   // foot: bone frame X = vertical, Z = horizontal, +Y towards the toes
		const Section f[] = { { -0.055f, 0, 0, 0, 0 }, { -0.045f, 0.042f, 0.038f, 0, 0 }, { 0.0f, 0.052f, 0.050f, 0, 0 },
		                      { 0.10f, 0.044f, 0.058f, 0, 0 }, { 0.18f, 0.030f, 0.052f, 0, 0 }, { 0.22f, 0, 0, 0, 0 } };
		p->foot = mesh_loft(f, 6, Q(12, 8));
		mesh_paint(p->foot, paint_foot, (void *)sp);
	}
}

void fighter_meshes_build(void)
{
	build_body(0);
	build_body(1);
	SHADOW = mesh_disc(1.0f, 24);
}

// ---- costumes -------------------------------------------------------------------------------
Look look_player(void)
{
	const BodySpec *sp = &SPEC[0];
	Look l = {
		.skin  = { sp->skin[0], sp->skin[1], sp->skin[2], 0.28f, 0.50f, 0.60f, 1 },
		.cloth = { 1, 1, 1, 0.06f, 0.30f, 0.30f, 1 },
		.glove = { 0.88f, 0.06f, 0.08f, 0.95f, 0.10f, 0.70f, 1 },
		.hair  = { sp->hair[0], sp->hair[1], sp->hair[2], 0.55f, 0.10f, 0.60f, 1 },
		.trim  = { 0.95f, 0.94f, 0.88f, 0.30f, 0.10f, 0.35f, 1 },
		.dark  = { 0.03f, 0.03f, 0.04f, 0.60f, 0.0f, 0.10f, 1 },
		.body  = 0,
	};
	return l;
}

Look look_opponent(void)
{
	const BodySpec *sp = &SPEC[1];
	Look l = {
		.skin  = { sp->skin[0], sp->skin[1], sp->skin[2], 0.30f, 0.50f, 0.60f, 1 },
		.cloth = { 1, 1, 1, 0.10f, 0.30f, 0.35f, 1 },
		.glove = { 0.10f, 0.26f, 0.90f, 0.95f, 0.10f, 0.70f, 1 },
		.hair  = { sp->hair[0], sp->hair[1], sp->hair[2], 0.55f, 0.10f, 0.60f, 1 },
		.trim  = { 0.96f, 0.80f, 0.22f, 0.55f, 0.10f, 0.45f, 1 },
		.dark  = { 0.03f, 0.03f, 0.04f, 0.60f, 0.0f, 0.10f, 1 },
		.body  = 1,
	};
	return l;
}

// ---- drawing ----------------------------------------------------------------------------------
#define OUTLINE_WIDTH 0.011f

static int g_pass;                            // 0 = shaded, 1 = outline hull

static void part(const Mesh *m, const Mat4 *frame, const Mat4 *local, const Material *mat, int outlined)
{
	if (g_pass && !outlined) return;
	static const Material ink = { 0, 0, 0, 0, 0, 0, 1 };
	Mat4 w = local ? mat4_mul(frame, local) : *frame;
	gfx_draw(m, &w, g_pass ? &ink : mat);
}

static void draw_all(const Fighter *f)
{
	const Rig *r = &f->rig;
	const Look *lk = &f->look;
	const Parts *p = &P[lk->body];
	const BodySpec *sp = &SPEC[lk->body];
	const int hi = g_detail;
	Mat4 L;

	part(&p->pelvis, &r->pelvis, NULL, &lk->cloth, 1);
	part(&p->belt, &r->pelvis, NULL, &lk->cloth, 0);
	part(&p->torso, &r->torso, NULL, &lk->skin, 1);
	L = mat4_translate(v3(0, -0.05f, 0));                                      part(&p->neck, &r->head, &L, &lk->skin, 1);
	for (int sd = -1; sd <= 1; sd += 2) {                                     // chest, shoulders, traps, abs
		L = mat4_translate(v3(0.120f * sp->kc, 0.335f, sd * 0.108f * sp->kc)); part(&p->pec, &r->torso, &L, &lk->skin, 0);
		L = mat4_translate(v3(0.0f, 0.425f, sd * 0.235f));                    part(&p->delt, &r->torso, &L, &lk->skin, 0);
		L = mat4_translate(v3(-0.015f, 0.490f, sd * 0.078f * sp->kc));        part(&p->trap, &r->torso, &L, &lk->skin, 0);
		if (hi)
			for (int row = 0; row < 3; row++) {
				L = mat4_translate(v3(0.104f * sp->kc + 0.004f * (2 - row), 0.050f + 0.072f * row, sd * 0.034f * sp->kc));
				part(&p->ab, &r->torso, &L, &lk->skin, 0);
			}
	}

	// head (frame origin at the base of the neck; skull centre 0.20 above)
	L = mat4_translate(v3(0.005f, 0.20f, 0));                                 part(&p->skull, &r->head, &L, &lk->skin, 1);
	L = mat4_translate(v3(0.040f, 0.112f, 0));                                part(&p->jaw, &r->head, &L, &lk->skin, 1);
	if (p->spikes.nidx) {
		part(&p->spikes, &r->head, NULL, &lk->hair, 1);
		L = mat4_translate(v3(0.0f, 0.245f, 0));                              part(&p->band, &r->head, &L, &lk->cloth, 1);
	} else {
		L = mat4_translate(v3(0.052f, 0.100f, 0));                            part(&p->beard, &r->head, &L, &lk->hair, 1);
		L = mat4_translate(v3(0.104f, 0.152f, 0));                            part(&p->must, &r->head, &L, &lk->hair, 0);
	}
	if (hi) {
		L = mat4_translate(v3(0.088f, 0.240f, 0));                            part(&p->brow, &r->head, &L, &lk->skin, 0);
		L = mat4_translate(v3(0.114f, 0.190f, 0));                            part(&p->nose, &r->head, &L, &lk->skin, 0);
		for (int sd = -1; sd <= 1; sd += 2) {
			L = mat4_translate(v3(0.005f, 0.195f, sd * 0.096f));              part(&p->ear, &r->head, &L, &lk->skin, 0);
			L = mat4_translate(v3(0.094f, 0.215f, sd * 0.041f));              part(&p->eye, &r->head, &L, &lk->dark, 0);
			L = mat4_translate(v3(0.088f, 0.168f, sd * 0.058f));              part(&p->cheek, &r->head, &L, &lk->skin, 0);
		}
	}

	for (int h = 0; h < 2; h++) {
		part(&p->upper_arm, &r->upper_arm[h], NULL, &lk->skin, 1);
		L = mat4_translate(v3(0, 0.14f, 0));                                  part(&p->bicep, &r->upper_arm[h], &L, &lk->skin, 0);
		part(&p->fore_arm, &r->fore_arm[h], NULL, &lk->skin, 1);
		L = mat4_translate(v3(0, 0.07f, 0));                                  part(&p->flexor, &r->fore_arm[h], &L, &lk->skin, 0);
		part(&p->glove, &r->glove[h], NULL, &lk->glove, 1);
		L = mat4_translate(v3(0, -0.045f, 0));                                part(&p->cuff, &r->glove[h], &L, &lk->trim, 0);
		if (hi) {
			float side = (h == 0 ? f->lead : -f->lead);
			L = mat4_translate(v3(0.0f, 0.105f, -side * 0.088f));             part(&p->thumb, &r->glove[h], &L, &lk->glove, 0);
		}
	}
	for (int h = 0; h < 2; h++) {
		part(&p->thigh, &r->thigh[h], NULL, &lk->skin, 1);
		if (hi) { L = mat4_translate(v3(0, 0.20f, 0));                        part(&p->quad, &r->thigh[h], &L, &lk->skin, 0); }
		part(&p->shin, &r->shin[h], NULL, &lk->skin, 1);
		if (hi) { L = mat4_translate(v3(0, 0.12f, 0));                        part(&p->calf, &r->shin[h], &L, &lk->skin, 0); }
		if (sp->pants) {
			L = mat4_translate(v3(0, 0.02f, 0));                              part(&p->cloth_thigh, &r->thigh[h], &L, &lk->cloth, 1);
			L = mat4_translate(v3(0, 0.0f, 0));                               part(&p->cloth_shin, &r->shin[h], &L, &lk->cloth, 1);
		} else {
			L = mat4_translate(v3(0, 0.0f, 0));                               part(&p->cloth_thigh, &r->thigh[h], &L, &lk->cloth, 1);
			L = mat4_translate(v3(0, 0.20f, 0));                              part(&p->hem, &r->thigh[h], &L, &lk->trim, 0);
		}
		part(&p->foot, &r->foot[h], NULL, &lk->skin, 1);
	}
}

void fighter_draw(const Fighter *f)
{
	g_pass = 0;
	draw_all(f);
	if (g_detail) {                                                            // inverted-hull outline pass
		g_pass = 1;
		gfx_outline(OUTLINE_WIDTH);
		draw_all(f);
		gfx_outline(0);
		g_pass = 0;
	}
}

void fighter_draw_trail(const Fighter *f)
{
	if (f->st != ST_ATTACK || f->atk == ATK_NONE || f->trail_n < 2) return;
	const AttackDef *d = &ATTACKS[f->atk];
	if (f->atk_t < d->startup / 2 || f->atk_t > d->startup + d->active + 3) return;          // only while the limb is moving fast
	const Parts *p = &P[f->look.body];
	const Mesh *m = d->limb < 2 ? &p->glove : &p->foot;
	for (int i = 1; i < f->trail_n; i++) {
		Material ghost = { 2.4f, 2.6f, 3.4f, 0, 0, 0, 0.34f - 0.10f * (float)i };
		gfx_draw(m, &f->trail[i], &ghost);
	}
}

void fighter_draw_shadow(const Fighter *f)
{
	float k = f->fall > 0 ? 1.0f + 0.7f * f->fall : 1.0f;
	for (int i = 0; i < 3; i++) {
		float rad = (0.66f - 0.14f * i) * k;
		Mat4 m = mat4_basis(v3(rad, 0, 0), v3(0, 1, 0), v3(0, 0, rad * 0.75f), v3(f->x - 0.10f * f->fall, 0.004f + 0.001f * i, f->z));
		Material sh = { 0, 0, 0, 0, 0, 0, 0.20f };
		gfx_draw(&SHADOW, &m, &sh);
	}
}
