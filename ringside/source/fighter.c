#include "fighter.h"
#include <string.h>

// ---- attack table -------------------------------------------------------------------------
const AttackDef ATTACKS[ATK_COUNT] = {
	//  name         start act rec  reach dmg  stam hstun bstun push step limb head tipr
	{ "lead punch",   4, 3,  9, 0.72f,  4, 3, 14, 10, 0.10f, 0.10f, 0, 1, 0.090f },
	{ "rear punch",   6, 3, 13, 0.74f,  8, 5, 20, 12, 0.18f, 0.14f, 1, 1, 0.090f },
	{ "lead hook",    9, 4, 16, 0.68f,  9, 6, 22, 14, 0.20f, 0.10f, 0, 1, 0.095f },
	{ "rear hook",   11, 4, 20, 0.70f, 13, 8, 26, 16, 0.28f, 0.14f, 1, 1, 0.095f },
	{ "lead kick",   10, 4, 17, 0.98f,  7, 6, 20, 14, 0.26f, 0.06f, 2, 0, 0.090f },
	{ "rear kick",   13, 4, 24, 1.05f, 15, 10, 30, 18, 0.34f, 0.10f, 3, 1, 0.095f },
};

void fighter_init(Fighter *f, float x, float z, float yaw, int lead, const Look *look)
{
	memset(f, 0, sizeof *f);
	f->x = x; f->z = z; f->yaw = yaw; f->lead = lead;
	f->st = ST_IDLE; f->atk = ATK_NONE;
	f->hp = 100; f->stamina = 100;
	f->look = *look;
	f->bob_t = x * 1.7f;
}

// ---- placement ----------------------------------------------------------------------------
Mat4 fighter_matrix(const Fighter *f)
{
	Mat4 t = mat4_translate(v3(f->x, 0, f->z)), r = mat4_roty(f->yaw);
	return mat4_mul(&t, &r);
}

Vec3 fighter_to_local(const Fighter *f, Vec3 w)
{
	Vec3 d = v3(w.x - f->x, w.y, w.z - f->z);
	float c = cosf(f->yaw), s = sinf(f->yaw);
	return v3(d.x * c - d.z * s, d.y, d.x * s + d.z * c);   // inverse of mat4_roty
}

// ---- IK -----------------------------------------------------------------------------------
// Two-bone IK. Returns the middle joint; *tip is where the end actually lands (clamped to reach).
static Vec3 ik2(Vec3 root, Vec3 target, float a, float b, Vec3 pole, Vec3 *tip)
{
	Vec3 d = v3sub(target, root);
	float dist = v3len(d);
	Vec3 dir = v3norm(d);
	float dd = clampf(dist, fabsf(a - b) + 0.002f, a + b - 0.002f);
	float x = (a * a - b * b + dd * dd) / (2.0f * dd);
	float h = sqrtf(fmaxf(a * a - x * x, 0.0f));
	Vec3 p = v3sub(pole, v3mul(dir, v3dot(pole, dir)));
	if (v3len(p) < 1e-4f) p = v3cross(dir, v3(0, 0, 1));
	p = v3norm(p);
	*tip = v3madd(root, dir, dd);
	return v3add(v3madd(root, dir, x), v3mul(p, h));
}

// ---- pose ---------------------------------------------------------------------------------

static Vec3 bezier(Vec3 a, Vec3 c, Vec3 b, float t)
{
	float u = 1.0f - t;
	return v3add(v3add(v3mul(a, u * u), v3mul(c, 2.0f * u * t)), v3mul(b, t * t));
}

// Where the guard glove sits, relative to the waist frame without yaw (so it follows leaning and ducking).
static const Vec3 GUARD_HEAD[2]  = { { 0.27f, 0.72f,  0.05f }, { 0.20f, 0.73f, -0.03f } };
static const Vec3 GUARD_BASE[2]  = { { 0.36f, 0.66f,  0.15f }, { 0.19f, 0.62f, -0.10f } };
static const Vec3 GUARD_BODY[2]  = { { 0.24f, 0.20f,  0.13f }, { 0.20f, 0.18f, -0.10f } };

static Mat4 waist_frame_noyaw(const Pose *p)
{
	Mat4 t = mat4_translate(v3(p->pelvis.x, p->pelvis.y + WAIST_UP, p->pelvis.z));
	Mat4 rp = mat4_rotz(-p->torso_pitch), rr = mat4_rotx(p->torso_roll);
	Mat4 a = mat4_mul(&t, &rp);
	return mat4_mul(&a, &rr);
}

static Vec3 guard_hand(const Fighter *f, const Pose *p, int hand, float head_amt, float body_amt)
{
	int ls = f->lead;
	Vec3 g[3] = { GUARD_BASE[hand], GUARD_HEAD[hand], GUARD_BODY[hand] };
	for (int i = 0; i < 3; i++) g[i].z *= (float)ls;
	Vec3 l = v3lerp(g[0], g[1], head_amt);
	l = v3lerp(l, g[2], body_amt);
	Mat4 wf = waist_frame_noyaw(p);
	return mat4_point(&wf, l);
}

static void compute_pose(const Fighter *f, float t, Pose *p)
{
	const int ls = f->lead;
	const float lean = f->lean, duck = f->duck, slip = f->slip;
	const float ph = t * 2.0f * PI_F * 1.55f + f->bob_t;
	const float bob = 0.5f + 0.5f * sinf(ph);

	memset(p, 0, sizeof *p);
	p->pelvis = v3(-0.03f, 0.940f - 0.022f * bob, 0);
	p->pelvis_yaw = ls * 0.30f;
	p->torso_yaw = ls * 0.36f + 0.03f * sinf(ph * 0.5f);
	p->torso_pitch = 0.10f;
	p->elbow_flare[0] = p->elbow_flare[1] = 0.05f;
	p->foot[0] = v3(0.27f, ANKLE_H, ls * 0.13f);
	p->foot[1] = v3(-0.27f, ANKLE_H, -ls * 0.13f);
	p->foot_yaw[0] = -ls * 0.15f; p->foot_yaw[1] = ls * 0.50f;

	// footwork
	float wa = f->walk_amt, wp = f->walk_phase;
	p->foot[0].x += sinf(wp) * 0.11f * wa;             p->foot[0].y += fmaxf(0.0f, cosf(wp)) * 0.045f * wa;
	p->foot[1].x += sinf(wp + PI_F) * 0.11f * wa;      p->foot[1].y += fmaxf(0.0f, -cosf(wp)) * 0.045f * wa;
	p->pelvis.y -= 0.012f * wa;

	// head movement
	p->pelvis.x += 0.10f * lean + 0.06f * duck;
	p->torso_pitch += 0.42f * lean + 0.55f * duck;
	p->pelvis.y -= 0.24f * duck;
	p->pelvis.z += 0.10f * slip;
	p->torso_roll += 0.30f * slip;

	// head keeps looking at the opponent
	p->head_yaw = -(p->pelvis_yaw + p->torso_yaw);
	p->head_pitch = -0.55f * (p->torso_pitch - 0.10f) - 0.05f;
	p->head_roll = -0.5f * p->torso_roll;

	float ga = f->guard_amt[0], gb = f->guard_amt[1];
	p->elbow_flare[0] = p->elbow_flare[1] = lerpf(0.05f, -0.30f, fmaxf(ga, gb));

	// blocking recoil
	float rec = f->st == ST_BLOCKSTUN ? sinf(PI_F * clampf((float)f->st_t / (float)f->st_len, 0, 1)) : 0.0f;

	Vec3 base_hand[2], base_foot[2] = { p->foot[0], p->foot[1] };
	for (int h = 0; h < 2; h++) {
		base_hand[h] = guard_hand(f, p, h, ga, gb);
		base_hand[h].x -= 0.05f * rec;
		base_hand[h].y += 0.012f * sinf(ph * 1.0f + (float)h);
	}
	p->hand[0] = base_hand[0]; p->hand[1] = base_hand[1];

	// ---- attacks: the striking limb follows a Bezier path guard -> strike -> guard ----
	if (f->st == ST_ATTACK && f->atk != ATK_NONE) {
		const AttackDef *d = &ATTACKS[f->atk];
		float u = (float)f->atk_t;
		float e;
		if (u < d->startup) e = smooth01(u / (float)d->startup);
		else if (u < d->startup + d->active) e = 1.0f;
		else e = 1.0f - smooth01((u - d->startup - d->active) / (float)d->recovery);

		Vec3 s = fighter_to_local(f, f->strike);
		int limb = d->limb;
		Vec3 p0 = limb < 2 ? base_hand[limb] : base_foot[limb - 2];
		Vec3 root = limb < 2 ? fighter_to_local(f, f->rig.shoulder[limb]) : v3(0, 0.86f, (limb == 2 ? ls : -ls) * HIP_W);
		Vec3 toS = v3sub(s, root);
		if (v3len(toS) > d->reach) s = v3madd(root, v3norm(toS), d->reach);
		float side = (limb == 0 || limb == 2) ? (float)ls : -(float)ls;      // outward direction of the striking limb
		Vec3 mid = v3lerp(p0, s, 0.5f), c;
		switch (f->atk) {
		case ATK_LEAD_HOOK: c = v3add(mid, v3(-0.05f, 0.06f, side * 0.34f)); break;
		case ATK_REAR_HOOK: c = v3add(mid, v3(-0.08f, 0.07f, side * 0.42f)); break;
		case ATK_LEAD_KICK: c = v3add(mid, v3(-0.10f, 0.30f, 0.0f)); break;
		case ATK_REAR_KICK: c = v3add(mid, v3(-0.18f, 0.22f, side * 0.46f)); break;
		case ATK_LEAD_PUNCH: c = v3add(mid, v3(0, 0.03f, 0)); break;
		default: c = v3add(mid, v3(0, -0.02f, 0)); break;
		}
		Vec3 pos = bezier(p0, c, s, e);
		if (limb < 2) p->hand[limb] = pos; else p->foot[limb - 2] = pos;

		switch (f->atk) {
		case ATK_LEAD_PUNCH:
			p->torso_yaw += ls * 0.22f * e; p->pelvis_yaw += ls * 0.10f * e; p->torso_pitch += 0.10f * e; break;
		case ATK_REAR_PUNCH:
			p->torso_yaw -= ls * 0.70f * e; p->pelvis_yaw -= ls * 0.45f * e; p->torso_pitch += 0.12f * e; p->pelvis.x += 0.05f * e; break;
		case ATK_LEAD_HOOK:
			p->torso_yaw -= ls * 0.50f * e; p->pelvis_yaw -= ls * 0.25f * e; p->elbow_flare[0] = lerpf(0.05f, 1.6f, smooth01(e * 1.6f)); break;
		case ATK_REAR_HOOK:
			p->torso_yaw -= ls * 0.85f * e; p->pelvis_yaw -= ls * 0.55f * e; p->elbow_flare[1] = lerpf(0.05f, 1.7f, smooth01(e * 1.6f)); p->pelvis.x += 0.05f * e; break;
		case ATK_LEAD_KICK:
			p->torso_pitch -= 0.34f * e; p->pelvis.x -= 0.06f * e; p->pelvis.y += 0.02f * e; break;
		case ATK_REAR_KICK:
			p->pelvis_yaw -= ls * 1.20f * e; p->torso_yaw -= ls * 0.55f * e; p->torso_roll += ls * 0.40f * e;
			p->torso_pitch -= 0.12f * e; p->foot_yaw[1] -= ls * 1.0f * e; break;
		}
		p->head_yaw = -(p->pelvis_yaw + p->torso_yaw);
		if (limb < 2) {  // the guard hand of the striking side follows the shoulder
			p->hand[1 - limb] = guard_hand(f, p, 1 - limb, ga, gb);
		}
	}

	// ---- hit reaction and falling ----
	float r = f->react;
	if (r > 0.001f) {
		float head = f->react_zone == 1 ? 1.0f : 0.35f;
		p->torso_pitch -= (0.30f + 0.25f * head) * r;
		p->head_pitch -= 0.55f * head * r;
		p->head_roll += 0.30f * head * r * (float)ls;
		p->pelvis.x -= 0.07f * r;
		p->hand[0].y -= 0.10f * r; p->hand[1].y -= 0.10f * r;
		p->hand[0].x -= 0.10f * r; p->hand[1].x -= 0.10f * r;
	}
	if (f->st == ST_GUARDBREAK) {
		float k = sinf(PI_F * clampf((float)f->st_t / (float)f->st_len, 0, 1));
		p->torso_pitch -= 0.35f * k; p->hand[0].x -= 0.2f * k; p->hand[1].x -= 0.2f * k;
		p->hand[0].y -= 0.12f * k; p->hand[1].y -= 0.12f * k;
	}
	if (f->fall > 0) {
		float c = smooth01(f->fall * 1.6f);
		p->pelvis.y = lerpf(p->pelvis.y, 0.99f, c); p->pelvis.x = lerpf(p->pelvis.x, 0.0f, c);
		p->torso_pitch = lerpf(p->torso_pitch, -0.10f, c); p->torso_yaw = lerpf(p->torso_yaw, 0.0f, c);
		p->pelvis_yaw = lerpf(p->pelvis_yaw, 0.0f, c); p->torso_roll = lerpf(p->torso_roll, 0.0f, c);
		p->head_pitch = lerpf(p->head_pitch, -0.35f, c); p->head_yaw = lerpf(p->head_yaw, 0.0f, c);
		p->foot[0] = v3lerp(p->foot[0], v3(0.05f, ANKLE_H, ls * 0.13f), c);
		p->foot[1] = v3lerp(p->foot[1], v3(-0.03f, ANKLE_H, -ls * 0.13f), c);
		p->foot_yaw[0] = p->foot_yaw[1] = 0;
		p->hand[0] = v3lerp(p->hand[0], v3(-0.05f, 0.90f, ls * 0.46f), c);
		p->hand[1] = v3lerp(p->hand[1], v3(-0.05f, 0.90f, -ls * 0.46f), c);
		p->elbow_flare[0] = p->elbow_flare[1] = 0.6f * c;
	}
}

// ---- rig ----------------------------------------------------------------------------------
void fighter_pose(Fighter *f, float t)
{
	Pose *p = &f->pose;
	compute_pose(f, t, p);
	Rig *r = &f->rig;
	const int ls = f->lead;

	// fall: rigid topple backwards about the feet
	Mat4 base = fighter_matrix(f);
	if (f->fall > 0) {
		float a = smooth01(f->fall) * 1.42f;
		Mat4 g = mat4_rotz(a), lift = mat4_translate(v3(-0.10f * f->fall, 0.09f * f->fall, 0));
		Mat4 gg = mat4_mul(&lift, &g);
		base = mat4_mul(&base, &gg);
	}

	Mat4 T = mat4_translate(v3(p->pelvis.x + p->lean_x, p->pelvis.y, p->pelvis.z));
	Mat4 ry = mat4_roty(p->pelvis_yaw);
	Mat4 pf = mat4_mul(&T, &ry);
	Mat4 wt = mat4_translate(v3(0, WAIST_UP, 0));
	Mat4 tf = mat4_mul(&pf, &wt);
	Mat4 ty = mat4_roty(p->torso_yaw), tp = mat4_rotz(-p->torso_pitch), tr = mat4_rotx(p->torso_roll);
	tf = mat4_mul(&tf, &ty); tf = mat4_mul(&tf, &tp); tf = mat4_mul(&tf, &tr);
	Mat4 nt = mat4_translate(v3(0, NECK_UP, 0));
	Mat4 hf = mat4_mul(&tf, &nt);
	Mat4 hy = mat4_roty(p->head_yaw), hp = mat4_rotz(-p->head_pitch), hr = mat4_rotx(p->head_roll);
	hf = mat4_mul(&hf, &hy); hf = mat4_mul(&hf, &hp); hf = mat4_mul(&hf, &hr);

	r->pelvis = mat4_mul(&base, &pf);
	r->torso  = mat4_mul(&base, &tf);
	r->head   = mat4_mul(&base, &hf);
	r->pelvis_c = mat4_point(&r->pelvis, v3(0, 0, 0));
	r->chest_c  = mat4_point(&r->torso, v3(0, 0.30f, 0));
	r->head_c   = mat4_point(&r->head, v3(0.01f, 0.18f, 0));

	// arms
	for (int h = 0; h < 2; h++) {
		float side = (h == 0 ? ls : -ls);
		Vec3 sh = mat4_point(&tf, v3(0, SHOULDER_UP, side * SHOULDER_W));
		Vec3 pole = mat4_dir(&tf, v3(0.15f, -1.0f, side * (0.30f + p->elbow_flare[h])));
		Vec3 hand = p->hand[h], tip, wrist;
		Vec3 el = ik2(sh, hand, UPPER_ARM, FORE_ARM + GLOVE_OFF, pole, &tip);
		Vec3 fdir = v3norm(v3sub(hand, el));
		wrist = v3madd(hand, fdir, -GLOVE_OFF);
		el = ik2(sh, wrist, UPPER_ARM, FORE_ARM, pole, &tip);
		wrist = tip;
		fdir = v3norm(v3sub(v3madd(wrist, fdir, GLOVE_OFF), wrist));
		Vec3 sideh = v3norm(mat4_dir(&tf, v3(0, 0, side)));
		Mat4 ua = mat4_bone(sh, el, sideh), fa = mat4_bone(el, wrist, sideh), gl = mat4_bone(wrist, v3madd(wrist, fdir, 1.0f), sideh);
		r->upper_arm[h] = mat4_mul(&base, &ua);
		r->fore_arm[h] = mat4_mul(&base, &fa);
		r->glove[h] = mat4_mul(&base, &gl);
		r->tip[h] = mat4_point(&base, v3madd(wrist, fdir, GLOVE_OFF + 0.02f));
		r->shoulder[h] = mat4_point(&base, sh);
	}
	// legs
	for (int h = 0; h < 2; h++) {
		float side = (h == 0 ? ls : -ls);
		Vec3 hip = mat4_point(&pf, v3(0, -0.05f, side * HIP_W));
		Vec3 ankle = p->foot[h], tip;
		Vec3 fwd = mat4_dir(&pf, v3(1, 0, 0));
		Vec3 pole = v3add(v3(fwd.x, 0, fwd.z), v3(0, 0, side * 0.15f));
		if (h == 1 && f->atk == ATK_REAR_KICK && f->st == ST_ATTACK) pole = v3(0.2f, 0.4f, -side * 0.6f);
		if (h == 0 && f->atk == ATK_LEAD_KICK && f->st == ST_ATTACK) pole = v3(0.7f, 0.3f, 0);
		Vec3 kn = ik2(hip, ankle, THIGH, SHIN, pole, &tip);
		Vec3 sd = v3(0, 0, side);
		Mat4 th = mat4_bone(hip, kn, sd), sn = mat4_bone(kn, tip, sd);
		r->thigh[h] = mat4_mul(&base, &th);
		r->shin[h] = mat4_mul(&base, &sn);
		// foot: heel behind the ankle, toes ahead, yawed by foot_yaw
		float fy = p->foot_yaw[h], c = cosf(fy), s = sinf(fy);
		Vec3 toe_dir = v3(c, -0.10f, -s);
		Vec3 heel = v3madd(v3add(tip, v3(0, -0.035f, 0)), toe_dir, -0.045f);
		Vec3 up = v3(0, 1, 0);
		Vec3 y = v3norm(toe_dir), z = v3norm(v3cross(up, y)), x = v3cross(y, z);
		Mat4 ft = mat4_basis(x, y, z, heel);
		r->foot[h] = mat4_mul(&base, &ft);
		r->tip[2 + h] = mat4_point(&base, v3madd(tip, toe_dir, 0.10f));
	}
}

// Called after fighter_pose: remembers where the striking limb has been, for the motion afterimage.
void fighter_record_trail(Fighter *f)
{
	if (f->st != ST_ATTACK || f->atk == ATK_NONE) { f->trail_n = 0; return; }
	int limb = ATTACKS[f->atk].limb;
	Mat4 m = limb < 2 ? f->rig.glove[limb] : f->rig.foot[limb - 2];
	for (int i = 3; i > 0; i--) f->trail[i] = f->trail[i - 1];
	f->trail[0] = m;
	if (f->trail_n < 4) f->trail_n++;
}
