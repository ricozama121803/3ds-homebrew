// Grand Theft Otto II - game logic: Otto on foot, cars with arcade drift physics, enter / exit, chase camera.
#include "game.h"
#include "otto_gait.h"

#define WALK_MIN   1.0f
#define WALK_MAX   3.3f
#define RUN_SPEED  5.5f
#define ENTER_DIST 3.4f
#define MAX_FWD    32.0f
#define MAX_REV    9.0f
#define WHEELBASE  2.7f
#define FAST_SPEED 8.6f
#define STEP       (1.0f / 60.0f)

static const float START_X = 4 * PITCH + 3.0f, START_Z = 4 * PITCH + 14.0f;

void car_circles(const Car *c, float cx[3], float cz[3])
{
	float fx = sinf(c->yaw), fz = -cosf(c->yaw);
	float sp = car_spacing(c->kind);
	for (int i = 0; i < 3; i++) { float o = (i - 1) * sp; cx[i] = c->x + fx * o; cz[i] = c->z + fz * o; }
}

bool car_flying(const Car *c) { return c->kind == CAR_HELI && c->y > 2.5f; }

static const float PALETTE[8][3] = {
	{ 0.80f, 0.12f, 0.10f }, { 0.12f, 0.30f, 0.70f }, { 0.92f, 0.92f, 0.90f }, { 0.10f, 0.10f, 0.12f },
	{ 0.85f, 0.65f, 0.12f }, { 0.20f, 0.55f, 0.28f }, { 0.55f, 0.56f, 0.60f }, { 0.55f, 0.20f, 0.60f } };

static bool car_free(const Game *g, float x, float z, int skip)
{
	for (int i = 0; i < MAX_CAR; i++) {
		if (i == skip || !g->car[i].on) continue;
		float dx = g->car[i].x - x, dz = g->car[i].z - z;
		if (dx * dx + dz * dz < 6.0f * 6.0f) return false;
	}
	return true;
}

int game_add_car(Game *g, int kind, float x, float z, float yaw)
{
	for (int i = 0; i < MAX_CAR; i++) if (!g->car[i].on) {
		Car *c = &g->car[i];
		memset(c, 0, sizeof *c);
		c->on = true; c->kind = kind; c->x = x; c->z = z; c->yaw = yaw; c->hp = kind == CAR_BIKE ? 60.0f : kind == CAR_HELI ? 160.0f : 100.0f;
		if (kind == CAR_TAXI) { c->col[0] = 0.95f; c->col[1] = 0.75f; c->col[2] = 0.08f; }
		else if (kind == CAR_HELI) { c->col[0] = 0.92f; c->col[1] = 0.92f; c->col[2] = 0.95f; }
		else { const float *p = PALETTE[rnd_next(&g->rng) % 8]; memcpy(c->col, p, sizeof c->col); }
		return i;
	}
	return -1;
}

void game_start(Game *g)
{
	int keepCash = g->p.cash;
	g->wheel = false; g->hitT = g->killT = 0;
	memset(g->car, 0, sizeof g->car);
	memset(g->ped, 0, sizeof g->ped);
	g->trng = 0x7A11C0DEu ^ g->rng;
	Player *p = &g->p;
	memset(p, 0, sizeof *p);
	p->p[0] = START_X; p->p[2] = START_Z; p->face = DEG(180); p->car = -1; p->cash = keepCash; p->target = -1; p->bTapT = 9.0f;
	g->cam.yaw = DEG(180); g->cam.pitch = -0.2f; g->cam.dist = 5.0f; g->cam.fov = 62; g->cam.manualT = 0;
	// a few cars right at the start, then parked cars scattered along the streets
	game_add_car(g, CAR_SPORTS, 4 * PITCH + 6.4f, START_Z + 16.0f, 0.0f);
	game_add_car(g, CAR_SEDAN, 4 * PITCH - 6.4f, START_Z + 12.0f, DEG(180));
	game_add_car(g, CAR_TAXI, 4 * PITCH + 6.4f, START_Z + 30.0f, 0.0f);
	for (int tries = 0; tries < 400; tries++) {
		int line = 1 + (int)(rnd_next(&g->rng) % (NB - 1)), side = (rnd_next(&g->rng) & 1) ? 1 : -1;
		float u = 14.0f + rndf(&g->rng) * (CITY - 28.0f), off = side * 6.4f;
		float m = fmodf(u, PITCH);
		if (m < 15.0f || m > PITCH - 15.0f) continue;                       // keep the junctions clear
		bool alongX = (rnd_next(&g->rng) & 1) != 0;
		float x = alongX ? u : line * PITCH + off, z = alongX ? line * PITCH + off : u;
		float yaw = alongX ? (side > 0 ? DEG(90) : DEG(-90)) : (side > 0 ? 0.0f : DEG(180));
		if (!car_free(g, x, z, -1)) continue;
		int k = rnd_next(&g->rng) % 12; int kind = k < 4 ? CAR_SEDAN : k < 6 ? CAR_SPORTS : k < 8 ? CAR_VAN : k < 11 ? CAR_TAXI : CAR_POLICE;
		game_add_car(g, kind, x, z, yaw);
		int n = 0; for (int i = 0; i < MAX_CAR; i++) n += g->car[i].on;
		if (n >= 17) break;
	}
	// two motorcycles by the start, more parked along the streets; the helicopters wait on their pads
	game_add_car(g, CAR_BIKE, 4 * PITCH - 6.6f, START_Z - 3.0f, DEG(180));
	game_add_car(g, CAR_BIKE, 4 * PITCH + 6.6f, START_Z + 44.0f, 0.0f);
	for (int tries = 0, bikes = 0; tries < 300 && bikes < 5; tries++) {
		int line = 1 + (int)(rnd_next(&g->rng) % (NB - 1)), side = (rnd_next(&g->rng) & 1) ? 1 : -1;
		float u = 14.0f + rndf(&g->rng) * (CITY - 28.0f), m = fmodf(u, PITCH);
		if (m < 15.0f || m > PITCH - 15.0f) continue;
		bool alongX = (rnd_next(&g->rng) & 1) != 0;
		float x = alongX ? u : line * PITCH + side * 8.2f, z = alongX ? line * PITCH + side * 8.2f : u;
		if (!car_free(g, x, z, -1)) continue;
		game_add_car(g, CAR_BIKE, x, z, alongX ? (side > 0 ? DEG(90) : DEG(-90)) : (side > 0 ? 0.0f : DEG(180)));
		bikes++;
	}
	for (int i = 0; i < N_PADS; i++) game_add_car(g, CAR_HELI, PADS[i][0], PADS[i][1], PADS[i][2]);
	traffic_init(g);
	combat_init(g);
	police_init(g);
}

void game_init(Game *g, bool lowq)
{
	memset(g, 0, sizeof *g);
	g->rng = 0x6F770002u;
	g->lowq = lowq; g->sens = 1.0f;
	city_generate(&g->w, 0x20C0FFEEu);
	game_start(g);
	g->hintT = 9.0f;
}

// ---- cars ----------------------------------------------------------------------------------------------------------------------
static void car_step(Game *g, int idx, const Input *in, float dt)
{
	Car *c = &g->car[idx];
	float fx = sinf(c->yaw), fz = -cosf(c->yaw), rx = cosf(c->yaw), rz = sinf(c->yaw);
	float fs = c->vx * fx + c->vz * fz;
	// top speed (m/s) and acceleration (m/s^2) per kind: sedan, sports, van, taxi, police
	static const float TOPV[CAR_ALL] = { 46.0f, 58.0f, 38.0f, 46.0f, 54.0f, 40.0f, 64.0f, 0.0f }, ACC[CAR_ALL] = { 13.0f, 17.0f, 9.0f, 13.0f, 16.0f, 11.0f, 22.0f, 0.0f };
	bool bike = c->kind == CAR_BIKE;
	float maxv = TOPV[c->kind], accel = ACC[c->kind];
	float steerT = 0, thr = 0;
	bool hand = false;
	c->braking = false;
	if ((c->driven || c->bot) && in) {
		float lsc = bike ? 15.0f : 12.0f, lim = (bike ? 0.7f : 0.58f) / (1.0f + (fs / lsc) * (fs / lsc));              // the faster, the less the wheels can turn
		if (lim < 0.045f) lim = 0.045f;
		steerT = in->lx * lim;
		hand = in->hand;
		if (in->gas) thr = 1;
		if (in->brake) { if (fs > 0.8f) { fs -= 24.0f * dt; c->braking = true; } else thr = -0.55f; }
	}
	c->steer += (steerT - c->steer) * fminf(1.0f, dt * 9.0f);
	if (thr > 0 && fs < maxv) fs += accel * (1.0f - clampf(fs / maxv, 0, 1) * 0.85f) * dt;
	else if (thr < 0 && fs > -MAX_REV) fs -= 6.5f * dt;
	if (thr == 0) { float d = (c->driven ? 2.2f : 7.0f) * dt; fs = fabsf(fs) < d ? 0 : fs - (fs > 0 ? d : -d); }       // coasting: engine braking
	fs -= fs * fabsf(fs) * 0.0011f * dt;                                                                                 // air drag
	if (hand) { fs -= (fs > 0 ? 1 : -1) * fminf(fabsf(fs), 7.0f * dt); c->braking = true; }

	// heading from the steering angle (bicycle model); the velocity keeps its old direction and the grip drags it round
	float yawRate = fs / (bike ? 1.5f : WHEELBASE) * tanf(c->steer) * (hand ? 1.35f : 1.0f);
	c->yaw += yawRate * dt;
	float nfx = sinf(c->yaw), nfz = -cosf(c->yaw), nrx = cosf(c->yaw), nrz = sinf(c->yaw);
	float ls = c->vx * nrx + c->vz * nrz;
	float grip = hand ? 2.4f : 12.5f;                                    // sideways grip: less slippery than before, still a drift with the handbrake
	ls *= expf(-grip * dt);
	c->vx = nfx * fs + nrx * ls; c->vz = nfz * fs + nrz * ls;

	c->x += c->vx * dt; c->z += c->vz * dt;
	float spd = sqrtf(c->vx * c->vx + c->vz * c->vz);
	c->wheel += fs * dt / 0.33f;
	float latAcc = yawRate * fs, lonAcc = thr > 0 ? accel * 0.3f : c->braking ? -9.0f : 0;
	if (bike) c->lean += (-clampf(latAcc * 0.028f, -0.55f, 0.55f) - c->lean) * fminf(1.0f, dt * 6.0f);          // a motorcycle leans into the turn
	else c->lean += (clampf(latAcc * 0.012f, -0.09f, 0.09f) - c->lean) * fminf(1.0f, dt * 7.0f);
	c->pitch += (clampf(lonAcc * 0.004f, -0.05f, 0.05f) - c->pitch) * fminf(1.0f, dt * 6.0f);
	(void)rx; (void)rz; (void)spd;

	// buildings: three circles along the car
	for (int it = 0; it < 2; it++) {
		float cx[3], cz[3];
		car_circles(c, cx, cz);
		float px = 0, pz = 0, nx = 0, nz = 0; int hits = 0;
		for (int k = 0; k < 3; k++) {
			float x = cx[k], z = cz[k], n[2];
			if (city_push_circle(&g->w, &x, &z, car_radius(c->kind), n)) { px += x - cx[k]; pz += z - cz[k]; nx += n[0]; nz += n[1]; hits++; }
		}
		if (!hits) break;
		c->x += px / hits; c->z += pz / hits;
		float l = sqrtf(nx * nx + nz * nz);
		if (l > 1e-5f) {
			nx /= l; nz /= l;
			float vn = c->vx * nx + c->vz * nz;
			if (vn < 0) { c->vx -= 1.3f * vn * nx; c->vz -= 1.3f * vn * nz; c->vx *= 0.93f; c->vz *= 0.93f; if (-vn > 6) damage_car(g, idx, (-vn - 6) * 1.3f, c->driven); }
		}
	}
	c->x = clampf(c->x, BEACH_X + 3.0f, CITY - 1.5f); c->z = clampf(c->z, 1.5f, CITY - 1.5f);
}

// A helicopter: the circle pad pushes it forward / back and turns it, R climbs, B or A / L descends, and it hovers when nothing is pressed.
static void heli_step(Game *g, int idx, const Input *in, float dt)
{
	Car *c = &g->car[idx];
	bool pilot = c->driven && in;
	float thrust = pilot ? in->ly : 0, turn = pilot ? in->lx : 0, climb = pilot ? (in->gas ? 1.0f : 0.0f) - ((in->brake || in->hand) ? 1.0f : 0.0f) : 0;
	c->braking = false;
	c->rpm += ((pilot ? 1.0f : 0.0f) - c->rpm) * fminf(1.0f, dt * 0.9f);              // the rotor spools up in a couple of seconds
	c->rotor += dt * (3.0f + 40.0f * c->rpm);
	float floorY = city_roof_h(&g->w, c->x, c->z, 1.6f);
	bool lifted = c->rpm > 0.7f;
	float vyT = lifted ? climb * 9.5f : 0.0f;
	if (!pilot && c->y > floorY + 0.2f) vyT = -12.0f;                                  // abandoned in the air: it drops
	c->vy += (vyT - c->vy) * fminf(1.0f, dt * 3.0f);
	c->y += c->vy * dt;
	if (c->y < floorY) { if (c->vy < -7.0f) damage_car(g, idx, (-c->vy - 7.0f) * 7.0f, c->driven); c->y = floorY; if (c->vy < 0) c->vy = 0; }
	if (c->y > 120.0f) { c->y = 120.0f; if (c->vy > 0) c->vy = 0; }
	bool air = c->y > floorY + 0.2f;
	float fx = sinf(c->yaw), fz = -cosf(c->yaw), rx = cosf(c->yaw), rz = sinf(c->yaw);
	float fs = c->vx * fx + c->vz * fz, ls = c->vx * rx + c->vz * rz;
	float tgt = !air ? 0 : thrust > 0 ? thrust * 42.0f : thrust * 15.0f;
	fs += (tgt - fs) * fminf(1.0f, dt * 0.8f);
	ls *= expf(-1.4f * dt);
	if (!air) { fs *= expf(-3.0f * dt); ls *= expf(-3.0f * dt); }
	c->yaw += turn * (air ? 1.5f : (lifted ? 0.6f : 0.0f)) * dt;
	float nfx = sinf(c->yaw), nfz = -cosf(c->yaw), nrx = cosf(c->yaw), nrz = sinf(c->yaw);
	c->vx = nfx * fs + nrx * ls; c->vz = nfz * fs + nrz * ls;
	c->x += c->vx * dt; c->z += c->vz * dt;
	c->pitch += ((air ? -thrust * 0.2f : 0.0f) - c->pitch) * fminf(1.0f, dt * 3.0f);
	c->lean += ((air ? -turn * 0.25f : 0.0f) - c->lean) * fminf(1.0f, dt * 3.0f);
	for (int it = 0; it < 2; it++) {                                                   // buildings that reach up to the skids
		float cx[3], cz[3];
		car_circles(c, cx, cz);
		float px = 0, pz = 0, nx = 0, nz = 0; int hits = 0;
		for (int k = 0; k < 3; k++) {
			float x = cx[k], z = cz[k], n[2];
			if (city_push_circle_y(&g->w, &x, &z, car_radius(CAR_HELI), c->y + 0.25f, c->y + 3.3f, n)) { px += x - cx[k]; pz += z - cz[k]; nx += n[0]; nz += n[1]; hits++; }
		}
		if (!hits) break;
		c->x += px / hits; c->z += pz / hits;
		float l = sqrtf(nx * nx + nz * nz);
		if (l > 1e-5f) {
			nx /= l; nz /= l;
			float vn = c->vx * nx + c->vz * nz;
			if (vn < 0) { c->vx -= 1.4f * vn * nx; c->vz -= 1.4f * vn * nz; if (-vn > 9) damage_car(g, idx, (-vn - 9) * 3.0f, c->driven); }
		}
	}
	c->x = clampf(c->x, BEACH_X - 260.0f, CITY + 110.0f); c->z = clampf(c->z, -120.0f, CITY + 120.0f);
}

static void cars_collide(Game *g)
{
	for (int a = 0; a < MAX_CAR; a++) {
		if (!g->car[a].on) continue;
		for (int b = a + 1; b < MAX_CAR; b++) {
			if (!g->car[b].on) continue;
			Car *A = &g->car[a], *B = &g->car[b];
			float dx = B->x - A->x, dz = B->z - A->z;
			if (dx * dx + dz * dz > 5.5f * 5.5f) continue;
			if (car_flying(A) != car_flying(B)) continue;                                  // a helicopter overhead
			float ax[3], az[3], bx[3], bz[3];
			car_circles(A, ax, az); car_circles(B, bx, bz);
			for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
				float ex = bx[j] - ax[i], ez = bz[j] - az[i], d2 = ex * ex + ez * ez, r = (car_radius(A->kind) + car_radius(B->kind)) * 0.92f;
				if (d2 >= r * r || d2 < 1e-8f) continue;
				float d = sqrtf(d2), nx = ex / d, nz = ez / d, pen = (r - d) * 0.5f;
				float wa = A->driven ? 0.35f : 0.5f, wb = B->driven ? 0.35f : 0.5f, s = wa + wb;           // the driven car shoves the parked one more
				A->x -= nx * pen * 2 * wb / s; A->z -= nz * pen * 2 * wb / s; B->x += nx * pen * 2 * wa / s; B->z += nz * pen * 2 * wa / s;
				float rv = (B->vx - A->vx) * nx + (B->vz - A->vz) * nz;
				if (rv < 0) {
					float j2 = -(1.0f + 0.3f) * rv * 0.5f;
					A->vx -= nx * j2; A->vz -= nz * j2; B->vx += nx * j2; B->vz += nz * j2;
					if (-rv > 6) { damage_car(g, a, (-rv - 6) * 1.2f, A->driven || B->driven); damage_car(g, b, (-rv - 6) * 1.2f, A->driven || B->driven); }
					if (-rv > 2.5f) { A->ai.on = false; B->ai.on = false; }                      // a crash ends the computer driver's route
				}
			}
		}
	}
}

int game_nearest_car(const Game *g, float x, float z, float maxd)
{
	int best = -1; float bd = maxd;
	for (int i = 0; i < MAX_CAR; i++) {
		if (!g->car[i].on || g->car[i].driven || car_is_moving_traffic(&g->car[i]) || g->car[i].wreck) continue;
		float cx[3], cz[3]; car_circles(&g->car[i], cx, cz);
		for (int k = 0; k < 3; k++) { float d = sqrtf((cx[k] - x) * (cx[k] - x) + (cz[k] - z) * (cz[k] - z)) - car_radius(g->car[i].kind); if (d < bd) { bd = d; best = i; } }
	}
	return best;
}

static bool exit_spot(const Game *g, const Car *c, int side, float out[2])
{
	float rx = cosf(c->yaw), rz = sinf(c->yaw);
	float off = car_radius(c->kind) + 1.05f;
	float x = c->x + rx * side * off, z = c->z + rz * side * off, n[2];
	float tx = x, tz = z;
	if (city_push_circle(&g->w, &tx, &tz, 0.5f, n)) return false;
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *o = &g->car[i];
		if (!o->on || o == c) continue;
		float cx[3], cz[3]; car_circles(o, cx, cz);
		for (int k = 0; k < 3; k++) if ((cx[k] - x) * (cx[k] - x) + (cz[k] - z) * (cz[k] - z) < (car_radius(o->kind) + 0.5f) * (car_radius(o->kind) + 0.5f)) return false;
	}
	out[0] = x; out[1] = z;
	return true;
}

// ---- on foot -------------------------------------------------------------------------------------------------------------------------
static void foot_step(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	float sy = sinf(g->cam.yaw), cy = cosf(g->cam.yaw);
	// sprint: two quick taps of B; keep tapping and it gets faster (let go of the stick or start aiming and it stops)
	p->bTapT += dt;
	p->sprintBoost = fmaxf(0.0f, p->sprintBoost - dt * 0.18f);
	if (in->b_p) {
		if (p->bTapT < 0.38f) { p->sprintBoost = p->sprint ? fminf(1.0f, p->sprintBoost + 0.2f) : 0.0f; p->sprint = true; }
		p->bTapT = 0;
	}
	float wx = sy * in->ly + cy * in->lx, wz = -cy * in->ly + sy * in->lx;
	float mag = fminf(1.0f, sqrtf(in->lx * in->lx + in->ly * in->ly));
	if (mag < 0.1f) p->sprintT += dt; else p->sprintT = 0;
	if (p->sprintT > 0.3f || p->aimT > 0.3f) p->sprint = false;
	if (!p->sprint) p->sprintBoost = 0;
	float top = mag > 0.05f ? (p->sprint ? lerpf(RUN_SPEED, FAST_SPEED, p->sprintBoost) : lerpf(WALK_MIN, WALK_MAX, mag)) : 0;
	if (p->aimT > 0.3f) top *= 0.55f;                                  // firing: Otto slows down and turns to what he shoots at (combat.c)
	float wl = sqrtf(wx * wx + wz * wz);
	float tx = 0, tz = 0;
	if (wl > 1e-4f) { tx = wx / wl * top; tz = wz / wl * top; }
	float k = fminf(1.0f, dt * 12.0f);
	p->v[0] += (tx - p->v[0]) * k; p->v[2] += (tz - p->v[2]) * k;
	float d[3] = { p->v[0] * dt, 0, p->v[2] * dt };
	float ox = p->p[0], oz = p->p[2];
	Contact ct; city_move(&g->w, p->p, d, &ct);
	// cars are solid
	for (int i = 0; i < MAX_CAR; i++) {
		if (!g->car[i].on || car_flying(&g->car[i])) continue;
		float cx[3], cz[3]; car_circles(&g->car[i], cx, cz);
		for (int q = 0; q < 3; q++) {
			float ex = p->p[0] - cx[q], ez = p->p[2] - cz[q], dd = ex * ex + ez * ez, r = car_radius(g->car[i].kind) + 0.35f;
			if (dd < r * r && dd > 1e-8f) { float l = sqrtf(dd); p->p[0] = cx[q] + ex / l * r; p->p[2] = cz[q] + ez / l * r; }
		}
	}
	for (int i = 0; i < MAX_PED; i++) {
		const Ped *q = &g->ped[i];
		if (!q->on || q->state == PED_DOWN) continue;
		float ex = p->p[0] - q->x, ez = p->p[2] - q->z, dd = ex * ex + ez * ez;
		if (dd < 0.7f * 0.7f && dd > 1e-8f) { float l = sqrtf(dd); p->p[0] = q->x + ex / l * 0.7f; p->p[2] = q->z + ez / l * 0.7f; }
	}
	if (!world_walkable(p->p[0], p->p[2])) {                         // the edge of the world (sea, pier rails): slide along it
		float nx = p->p[0], nz = p->p[2];
		if (world_walkable(nx, oz)) p->p[2] = oz; else if (world_walkable(ox, nz)) p->p[0] = ox; else { p->p[0] = ox; p->p[2] = oz; }
	}
	float sp = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
	p->speed = sp;
	if (sp > 0.3f) {
		float want = atan2f(p->v[0], -p->v[2]);
		if (p->aimT < 0.3f) p->face = wrapang(p->face + wrapang(want - p->face) * fminf(1.0f, dt * 12.0f));
		p->phase += dt * sp / gait_stride(sp);                        // cycles: the feet keep up with the ground
	}
	// the camera swings round behind Otto when he runs sideways
	if (g->cam.manualT <= 0 && in->ly > -0.3f && fabsf(in->lx) > 0.2f && mag > 0.1f) g->cam.yaw = wrapang(g->cam.yaw + in->lx * 1.1f * dt);
}

static void enter_exit(Game *g, const Input *in)
{
	Player *p = &g->p;
	if (!in->enter_p) return;
	if (p->car < 0) {
		int i = game_nearest_car(g, p->p[0], p->p[2], ENTER_DIST);
		if (i >= 0) {
			if (g->car[i].ai.on) { traffic_eject_driver(g, i); g->car[i].ai.on = false; g->car[i].vx = g->car[i].vz = 0; add_heat(g, 10.0f); }
			if (g->car[i].kind == CAR_POLICE || g->car[i].kind == CAR_SWAT) { add_heat(g, 45.0f); g->car[i].bot = false; g->car[i].owner = 0; }
			p->car = i; g->car[i].driven = true; p->v[0] = p->v[2] = 0; g->hintT = 0;
		}
	} else {
		Car *c = &g->car[p->car];
		float sp = sqrtf(c->vx * c->vx + c->vz * c->vz), pos[2];
		if (sp > 5.0f || (c->kind == CAR_HELI && c->y > 0.6f)) return;                 // a helicopter must be on the ground
		if (exit_spot(g, c, -1, pos) || exit_spot(g, c, 1, pos)) {
			p->p[0] = pos[0]; p->p[2] = pos[1]; p->p[1] = 0; p->face = c->yaw; p->v[0] = p->v[2] = 0;
			c->driven = false; c->braking = true; p->car = -1;
			g->cam.manualT = 0;
		}
	}
}

// ---- camera ----------------------------------------------------------------------------------------------------------------------------
static void camera_step(Game *g, const Input *in, float dt)
{
	Cam *c = &g->cam; Player *p = &g->p;
	float inv = g->invertY ? -1.0f : 1.0f;
	float cx = in->cx, cy2 = in->cy;
	if (fabsf(cx) > 0.1f || fabsf(in->tdx) > 0) { c->yaw = wrapang(c->yaw + cx * 2.4f * g->sens * dt + in->tdx * 0.010f * g->sens); c->manualT = 1.6f; }
	if (fabsf(cy2) > 0.1f || fabsf(in->tdy) > 0) { c->pitch = clampf(c->pitch + inv * (cy2 * 1.4f * g->sens * dt - in->tdy * 0.006f * g->sens), -0.95f, 0.4f); c->manualT = 1.6f; }
	if (c->manualT > 0) c->manualT -= dt;
	if (p->car < 0 && p->aimT > 0.5f && p->ps == PS_ALIVE && !(in->aim && p->lock < 0)) {      // shooting or locked on: the camera swings round to where Otto is aiming
		c->yaw = wrapang(c->yaw + wrapang(p->face - c->yaw) * fminf(1.0f, dt * 11.0f));
		c->manualT = 0;
	}
	float tx, ty, tz, dist, fov;
	if (p->car >= 0) {
		const Car *car = &g->car[p->car];
		float spd = sqrtf(car->vx * car->vx + car->vz * car->vz);
		if (c->manualT <= 0) {
			float want = car->yaw;
			if (spd > 4.0f && car->kind != CAR_HELI) {                                          // lean toward the travel direction in a drift
				float vd = atan2f(car->vx, -car->vz), fw = car->vx * sinf(car->yaw) - car->vz * cosf(car->yaw);
				if (fw > 0) want = car->yaw + wrapang(vd - car->yaw) * 0.35f;
			}
			c->yaw = wrapang(c->yaw + wrapang(want - c->yaw) * fminf(1.0f, dt * 2.6f));
		}
		float sk = clampf((spd - 22.0f) / 30.0f, 0, 1);                      // 0..1 as the car goes from 80 to 190 km/h
		tx = car->x; ty = 1.6f - 0.2f * sk; tz = car->z; dist = 8.6f + fminf(spd, 55.0f) * 0.045f; fov = 62 + fminf(spd, 55.0f) * 0.4f;
		if (car->kind == CAR_HELI) { ty = car->y + 1.6f; dist = 15.0f + fminf(spd, 42.0f) * 0.1f; fov = 66 + fminf(spd, 42.0f) * 0.2f; }
		else if (car->kind == CAR_BIKE) dist = 7.2f + fminf(spd, 55.0f) * 0.045f;
	} else {
		tx = p->p[0]; ty = p->p[1] + 1.45f; tz = p->p[2]; dist = 4.6f + p->speed * 0.12f; fov = 60 + p->speed * 0.5f;
	}
	c->fov += (fov - c->fov) * fminf(1.0f, dt * 3.0f);
	c->dist += (dist - c->dist) * fminf(1.0f, dt * 4.0f);
	float cp = cosf(c->pitch), sp = sinf(c->pitch);
	float f[3] = { sinf(c->yaw) * cp, sp, -cosf(c->yaw) * cp };
	float o[3] = { tx, ty, tz }, n[3];
	float want = c->dist, d = city_ray(&g->w, o, (float[3]){ -f[0], -f[1], -f[2] }, c->dist + 0.5f, n);
	if (d >= 0 && d < want + 0.4f) want = fmaxf(0.9f, d - 0.35f);
	c->eye[0] = tx - f[0] * want; c->eye[1] = ty - f[1] * want; c->eye[2] = tz - f[2] * want;
	if (p->car >= 0) {                                                      // a rumble that grows with speed
		const Car *car = &g->car[p->car];
		float spd = sqrtf(car->vx * car->vx + car->vz * car->vz), sk = clampf((spd - 26.0f) / 28.0f, 0, 1), t = g->time;
		float amp = sk * sk * 0.07f;
		c->eye[0] += (sinf(t * 47.0f) + 0.6f * sinf(t * 31.0f)) * amp * cosf(c->yaw);
		c->eye[1] += (sinf(t * 53.0f) + 0.5f * sinf(t * 37.0f)) * amp;
		c->eye[2] += (sinf(t * 47.0f) + 0.6f * sinf(t * 31.0f)) * amp * sinf(c->yaw);
	}
	if (c->eye[1] < 0.5f) c->eye[1] = 0.5f;
}

// The weapon wheel: SELECT opens it, the circle pad or D-pad picks a slice (up, right, down, left = the weapons in order) and equips it at once;
// SELECT, A or B closes it. The world runs in slow motion while it is open.
static void wheel_update(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	g->wheelT += dt;
	float x = in->dpx, y = in->dpy;
	if (x == 0 && y == 0 && hypotf(in->lx, in->ly) > 0.5f) { x = in->lx; y = in->ly; }
	if (x == 0 && y == 0) return;
	int sector = ((int)floorf((atan2f(x, y) + PI_F / 4) / (PI_F / 2)) + 4) % 4;
	int w = sector % W_COUNT;
	g->wheelSel = w;
	if (p->has[w] && (w == W_FISTS || p->ammo[w] > 0)) p->weapon = w;
}

void game_update(Game *g, const Input *in, float dt)
{
	if (in->start_p) g->paused = !g->paused;
	if (g->paused) {
		if (in->up_p) g->showFps = !g->showFps, g->saveNeeded = true;
		if (in->down_p) g->invertY = !g->invertY, g->saveNeeded = true;
		if (in->select_p) { g->paused = false; g->wheel = false; game_start(g); }           // SELECT on the pause screen: back to the start
		return;
	}
	g->time += dt;
	if (g->hintT > 0) g->hintT -= dt;
	if (in->select_p && g->p.ps == PS_ALIVE) { g->wheel = !g->wheel; g->wheelT = 0; g->wheelSel = g->p.weapon; }
	else if (g->wheel && (in->a_p || in->b_p)) g->wheel = false;
	if (g->p.ps != PS_ALIVE) g->wheel = false;
	if (g->wheel) wheel_update(g, in, dt);
	Input once = *in;                                   // button presses are used once, in the first physics step
	if (g->p.ps != PS_ALIVE) { once.gas = once.brake = once.hand = once.aim = once.enter_p = once.x_p = false; once.lx = once.ly = 0; }
	if (g->wheel) { once.gas = once.brake = once.hand = once.aim = once.enter_p = once.x_p = once.a_p = once.b_p = false; once.lx = once.ly = 0; }
	float sdt = dt * g->timeScale * (g->wheel ? 0.15f : 1.0f);   // the world runs slowly while Otto is being wasted or busted, or choosing a weapon
	for (float t = 0; t < sdt - 1e-6f;) {
		float h = fminf(STEP, sdt - t); t += h;
		Player *p = &g->p;
		if (p->car < 0) foot_step(g, &once, h);
		for (int i = 0; i < MAX_CAR; i++) {
			Car *c = &g->car[i];
			if (!c->on || c->ai.on) continue;
			if (c->kind == CAR_HELI) heli_step(g, i, i == p->car ? &once : NULL, h);
			else if (c->bot) { Input bi; memset(&bi, 0, sizeof bi); bi.lx = c->botLx; bi.gas = c->botGas; bi.brake = c->botBrake; bi.hand = c->botHand; car_step(g, i, &bi, h); }
			else car_step(g, i, i == p->car ? &once : NULL, h);
		}
		traffic_update(g, h);
		cars_collide(g);
		if (p->car >= 0) {
			const Car *c = &g->car[p->car];
			p->p[0] = c->x; p->p[1] = c->kind == CAR_HELI ? c->y : 0; p->p[2] = c->z; p->v[0] = c->vx; p->v[2] = c->vz;
			p->speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
		}
		enter_exit(g, &once);
		combat_update(g, &once, h);
		police_update(g, h);
		once.enter_p = once.select_p = once.start_p = once.up_p = once.down_p = once.x_p = false; once.weap = 0;
	}
	if (sdt < dt && g->p.ps != PS_ALIVE) { /* keep the respawn timers honest while time is slowed: combat_update scales its own dt */ }
	Input cin = *in;                                    // the D-pad picks weapons while the wheel is open, it does not turn the camera
	if (g->wheel) cin.cx = cin.cy = cin.tdx = cin.tdy = 0;
	camera_step(g, &cin, dt);
}
