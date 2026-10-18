// Grand Theft Otto II - game logic: Otto on foot, cars with arcade drift physics, enter / exit, chase camera.
#include "game.h"
#include "otto_gait.h"
#include <stdio.h>

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

bool car_flying(const Car *c) { return (c->kind == CAR_HELI || c->kind == CAR_PLANE) && c->y > 2.5f; }

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
		c->on = true; c->kind = kind; c->x = x; c->z = z; c->yaw = yaw; c->hp = kind == CAR_BIKE ? 60.0f : kind == CAR_HELI ? 160.0f : kind == CAR_PLANE ? 260.0f : 100.0f;
		if (kind == CAR_TAXI) { c->col[0] = 0.95f; c->col[1] = 0.75f; c->col[2] = 0.08f; }
		else if (kind == CAR_HELI || kind == CAR_PLANE) { c->col[0] = 0.92f; c->col[1] = 0.92f; c->col[2] = 0.95f; }
		else { const float *p = PALETTE[rnd_next(&g->rng) % 8]; memcpy(c->col, p, sizeof c->col); }
		return i;
	}
	return -1;
}

// ---- regions ------------------------------------------------------------------------------------------------------------------------
bool world_walkable(const World *w, float x, float z)
{
	for (int k = 0; k < w->nrectF; k++) { const float *r = w->rectF[k]; if (x >= r[0] && x <= r[2] && z >= r[1] && z <= r[3]) return true; }
	return false;
}

void world_keep_car(const World *w, float *x, float *z)
{
	float bd = 1e18f, bx = *x, bz = *z;
	for (int k = 0; k < w->nrectC; k++) {
		const float *r = w->rectC[k];
		float cx = clampf(*x, r[0], r[2]), cz = clampf(*z, r[1], r[3]), d = (cx - *x) * (cx - *x) + (cz - *z) * (cz - *z);
		if (d < bd) { bd = d; bx = cx; bz = cz; }
	}
	*x = bx; *z = bz;
}

static void park_cars(Game *g, int count, int bikes)
{
	for (int tries = 0; tries < 500; tries++) {
		int line = 1 + (int)(rnd_next(&g->rng) % (NB - 1)), side = (rnd_next(&g->rng) & 1) ? 1 : -1;
		float u = 14.0f + rndf(&g->rng) * (CITY - 28.0f), off = side * 6.4f;
		float m = fmodf(u, PITCH);
		if (m < 15.0f || m > PITCH - 15.0f) continue;                       // keep the junctions clear
		bool alongX = (rnd_next(&g->rng) & 1) != 0;
		float x = alongX ? u : line * PITCH + off, z = alongX ? line * PITCH + off : u;
		float yaw = alongX ? (side > 0 ? DEG(90) : DEG(-90)) : (side > 0 ? 0.0f : DEG(180));
		if (!car_free(g, x, z, -1)) continue;
		int k = rnd_next(&g->rng) % 12, kind;
		if (g->region == REG_HILLS) kind = k < 4 ? CAR_SEDAN : k < 7 ? CAR_SPORTS : k < 10 ? CAR_VAN : CAR_TAXI;       // the suburbs: family cars
		else if (g->region == REG_AIRPORT) kind = k < 3 ? CAR_SEDAN : k < 5 ? CAR_SPORTS : k < 8 ? CAR_VAN : k < 11 ? CAR_TAXI : CAR_POLICE;      // taxis and vans
		else kind = k < 3 ? CAR_SEDAN : k < 8 ? CAR_SPORTS : k < 9 ? CAR_VAN : k < 11 ? CAR_TAXI : CAR_POLICE;
		game_add_car(g, kind, x, z, yaw);
		int n = 0; for (int i = 0; i < MAX_CAR; i++) n += g->car[i].on && g->car[i].kind < CAR_BIKE;
		if (n >= count) break;
	}
	for (int tries = 0, b = 0; tries < 300 && b < bikes; tries++) {
		int line = 1 + (int)(rnd_next(&g->rng) % (NB - 1)), side = (rnd_next(&g->rng) & 1) ? 1 : -1;
		float u = 14.0f + rndf(&g->rng) * (CITY - 28.0f), m = fmodf(u, PITCH);
		if (m < 15.0f || m > PITCH - 15.0f) continue;
		bool alongX = (rnd_next(&g->rng) & 1) != 0;
		float x = alongX ? u : line * PITCH + side * 8.2f, z = alongX ? line * PITCH + side * 8.2f : u;
		if (!car_free(g, x, z, -1)) continue;
		game_add_car(g, CAR_BIKE, x, z, alongX ? (side > 0 ? DEG(90) : DEG(-90)) : (side > 0 ? 0.0f : DEG(180)));
		b++;
	}
}

// Build a region: its blocks, its parked cars, traffic, pavement crowds, helipads and pickups. Otto and the wanted level are the caller's business.
void region_enter(Game *g, int region)
{
	g->region = region;
	city_generate(&g->w, region);
	memset(g->car, 0, sizeof g->car);
	memset(g->ped, 0, sizeof g->ped);
	g->rng ^= REGIONS[region].seed;
	g->trng = 0x7A11C0DEu ^ g->rng;
	g->npad = 0;
	if (region == REG_CITY) { for (int i = 0; i < 3; i++) memcpy(g->pad[g->npad++], PADS_CITY[i], sizeof g->pad[0]); }
	else if (region == REG_AIRPORT) memcpy(g->pad[g->npad++], PADS_AIRPORT[0], sizeof g->pad[0]);
	else memcpy(g->pad[g->npad++], PADS_HILLS[0], sizeof g->pad[0]);
	park_cars(g, REGIONS[region].parked, region == REG_CITY ? 5 : 3);
	for (int i = 0; i < g->npad; i++) game_add_car(g, CAR_HELI, g->pad[i][0], g->pad[i][1], g->pad[i][2]);
	if (region == REG_AIRPORT) {                                                                 // two airliners waiting at the west end of runway 2
		game_add_car(g, CAR_PLANE, 90.0f, RWY2_Z, PI_F / 2);
		game_add_car(g, CAR_PLANE, 190.0f, RWY2_Z, PI_F / 2);
	}
	traffic_init(g);
	combat_world_init(g);
	police_reset(g);
	g->regionDirty = true;
	g->bannerT = 3.2f;
}

void region_go(Game *g, int dest, float x, float z, float yaw)
{
	if (g->regionGo >= 0 || dest == g->region) return;
	g->regionGo = dest; g->fadeDir = 1.0f; g->goX = x; g->goZ = z; g->goYaw = yaw;
}

// the fade to black, the swap behind it, the fade back in
static void region_update(Game *g, float dt)
{
	if (g->bannerT > 0) g->bannerT -= dt;
	if (g->regionGo >= 0) {
		g->fade = fminf(1.0f, g->fade + dt / 0.45f);
		if (g->fade < 1.0f) return;
		Player *p = &g->p;
		int dest = g->regionGo; g->regionGo = -1;
		Car saved; bool inCar = p->car >= 0;
		if (inCar) saved = g->car[p->car];
		region_enter(g, dest);
		p->p[0] = g->goX; p->p[2] = g->goZ;
		if (g->goYaw > -50.0f) p->face = g->goYaw;
		for (int i = 0; i < MAX_CAR; i++) if (g->car[i].on && hypotf(g->car[i].x - g->goX, g->car[i].z - g->goZ) < 9.0f && g->car[i].kind < CAR_HELI) g->car[i].on = false;
		p->car = -1;
		if (inCar) {                                                       // the vehicle he is driving comes along
			int ni = game_add_car(g, saved.kind, g->goX, g->goZ, saved.yaw);
			if (ni >= 0) { g->car[ni] = saved; g->car[ni].x = g->goX; g->car[ni].z = g->goZ; g->car[ni].on = true; g->car[ni].ai.on = false; g->car[ni].bot = false; p->car = ni; }
		}
		p->lock = -1; p->cover = 0; p->peek = 0; p->peekT = 0; p->crouch = 0;
		g->cam.manualT = 0; g->fadeDir = -1.0f;
		return;
	}
	if (g->fadeDir < 0) { g->fade -= dt / 0.6f; if (g->fade <= 0) { g->fade = 0; g->fadeDir = 0; } }
}

// has Otto (or the vehicle he is in) reached the end of a gate road, or flown out of the region?
static void region_check(Game *g)
{
	Player *p = &g->p;
	if (g->regionGo >= 0 || g->fade > 0.05f || p->ps != PS_ALIVE) return;
	const World *w = &g->w;
	float x = p->p[0], z = p->p[2];
	bool air = (p->car >= 0 && car_flying(&g->car[p->car])) || (p->car < 0 && p->airT > 0.5f);
	for (int k = 0; k < w->ngate && !air; k++) {                           // gate roads are for people and vehicles on the ground; aircraft cross the border in the air
		const Gate *gt = &w->gate[k];
		if (x < gt->x0 || x > gt->x1 || z < gt->z0 || z > gt->z1) continue;
		bool out = gt->dir == 0 ? x > gt->x1 - 4.0f : gt->dir == 1 ? x < gt->x0 + 4.0f : gt->dir == 2 ? z > gt->z1 - 4.0f : z < gt->z0 + 4.0f;
		if (!out) continue;
		float lat = clampf(gt->dir < 2 ? z : x, 226.0f, 294.0f);
		static const float AX[4] = { -48.0f, CITY + 48.0f, 0, 0 }, AZ[4] = { 0, 0, -48.0f, CITY + 48.0f };
		region_go(g, gt->dest, gt->dir < 2 ? AX[gt->dir] : lat, gt->dir < 2 ? lat : AZ[gt->dir], -100.0f);
		return;
	}
	if (air) {                                                              // flying out over the border
		static const float OUT[4] = { CITY + 150.0f, -150.0f, 0, -150.0f };
		int dir = x > CITY + 150.0f ? 0 : x < -150.0f ? 1 : z > (w->region == REG_AIRPORT ? AIR_Z1 : CITY) + 150.0f ? 2 : z < -150.0f ? 3 : -1;
		(void)OUT;
		if (dir >= 0 && w->nbr[dir] >= 0) {
			float ax = dir == 0 ? -140.0f : dir == 1 ? CITY + 140.0f : clampf(x, 20.0f, CITY - 20.0f), az = dir == 2 ? -140.0f : dir == 3 ? CITY + 140.0f : clampf(z, 20.0f, CITY - 20.0f);
			float py = p->p[1];
			region_go(g, w->nbr[dir], ax, az, -100.0f);
			(void)py;
		}
	}
}

void game_start(Game *g)
{
	int keepCash = g->p.cash;
	g->wheel = false; g->hitT = g->killT = 0; g->starCd = 0;
	g->regionGo = -1; g->fade = 0; g->fadeDir = 0;
	Player *p = &g->p;
	memset(p, 0, sizeof *p);
	p->p[0] = START_X; p->p[2] = START_Z; p->face = DEG(180); p->car = -1; p->cash = keepCash; p->target = -1; p->bTapT = 9.0f;
	g->cam.yaw = DEG(180); g->cam.pitch = -0.2f; g->cam.dist = 5.0f; g->cam.fov = 62; g->cam.manualT = 0;
	region_enter(g, REG_CITY);
	// a few cars and two motorcycles right at the start
	game_add_car(g, CAR_SPORTS, 4 * PITCH + 6.4f, START_Z + 16.0f, 0.0f);
	game_add_car(g, CAR_SEDAN, 4 * PITCH - 6.4f, START_Z + 12.0f, DEG(180));
	game_add_car(g, CAR_TAXI, 4 * PITCH + 6.4f, START_Z + 30.0f, 0.0f);
	game_add_car(g, CAR_BIKE, 4 * PITCH - 6.6f, START_Z - 3.0f, DEG(180));
	game_add_car(g, CAR_BIKE, 4 * PITCH + 6.6f, START_Z + 44.0f, 0.0f);
	combat_init(g);
	police_init(g);
}

void game_init(Game *g, bool lowq)
{
	memset(g, 0, sizeof *g);
	g->rng = 0x6F770002u;
	for (int i = 0; i < 3; i++) g->dTapAt[i] = -10.0f;
	g->lowq = lowq; g->sens = 1.0f;
	g->regionGo = -1;
	game_start(g);
	g->hintT = 9.0f;
}

// White smoke off the rear tyres when the car brakes really hard from speed or slides sideways (handbrake turns)
static void tyre_smoke(Game *g, const Car *c, float fs, float slip, float fx, float fz, float rx, float rz, float dt)
{
	float sp = fabsf(fs);
	float hard = c->braking && sp > 16.0f ? clampf((sp - 16.0f) / 18.0f, 0.45f, 1.0f) : 0.0f;
	float slide = slip > 4.0f && sp > 6.0f ? clampf((slip - 4.0f) / 6.0f, 0.35f, 1.0f) : 0.0f;
	float k = fmaxf(hard, slide);
	if (k <= 0 || hypotf(c->x - g->p.p[0], c->z - g->p.p[2]) > 80.0f) return;
	bool bike = c->kind == CAR_BIKE;
	float back = bike ? 0.75f : 1.35f;
	for (int w = bike ? 0 : -1; w <= (bike ? 0 : 1); w += 2) {
		if (rndf(&g->trng) > k * 24.0f * dt) continue;
		spawn_particles(g, PT_TIRE, c->x - fx * back + rx * w * 0.8f, 0.12f, c->z - fz * back + rz * w * 0.8f, 1, 0.7f, 0.65f, 0.95f);
	}
}

// ---- cars ----------------------------------------------------------------------------------------------------------------------
static void car_step(Game *g, int idx, const Input *in, float dt)
{
	Car *c = &g->car[idx];
	float fx = sinf(c->yaw), fz = -cosf(c->yaw), rx = cosf(c->yaw), rz = sinf(c->yaw);
	float fs = c->vx * fx + c->vz * fz;
	// top speed (m/s) and acceleration (m/s^2) per kind: sedan, sports, van, taxi, police
	static const float TOPV[CAR_ALL] = { 46.0f, 58.0f, 38.0f, 46.0f, 54.0f, 40.0f, 46.0f, 0.0f, 0.0f }, ACC[CAR_ALL] = { 13.0f, 17.0f, 9.0f, 13.0f, 16.0f, 11.0f, 16.0f, 0.0f, 0.0f };
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
	float ls = c->vx * nrx + c->vz * nrz, slip = fabsf(ls);
	float grip = hand ? 2.4f : 12.5f;                                    // sideways grip: less slippery than before, still a drift with the handbrake
	ls *= expf(-grip * dt);
	c->vx = nfx * fs + nrx * ls; c->vz = nfz * fs + nrz * ls;
	tyre_smoke(g, c, fs, slip, nfx, nfz, nrx, nrz, dt);

	c->x += c->vx * dt; c->z += c->vz * dt;
	float spd = sqrtf(c->vx * c->vx + c->vz * c->vz);
	c->wheel += fs * dt / 0.33f;
	float latAcc = yawRate * fs, lonAcc = thr > 0 ? accel * 0.3f : c->braking ? -9.0f : 0;
	if (bike) c->lean += (-clampf(latAcc * 0.028f, -0.55f, 0.55f) - c->lean) * fminf(1.0f, dt * 6.0f);          // a motorcycle leans into the turn
	else c->lean += (clampf(latAcc * 0.012f, -0.09f, 0.09f) - c->lean) * fminf(1.0f, dt * 7.0f);
	{
		bool wheelie = bike && c->driven && in && in->gas && in->ly < -0.5f && fs > 4.0f && fs < 36.0f;       // pull the pad back with the throttle on: the front wheel comes up
		float pt = wheelie ? 0.62f : clampf(lonAcc * 0.004f, -0.05f, 0.05f);
		c->pitch += (pt - c->pitch) * fminf(1.0f, dt * (wheelie ? 4.5f : 6.0f));
	}
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
	world_keep_car(&g->w, &c->x, &c->z);                                       // the town, its gate roads, and (airport) the airfield
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
	c->x = clampf(c->x, -420.0f, CITY + 420.0f); c->z = clampf(c->z, -420.0f, (g->w.region == REG_AIRPORT ? AIR_Z1 : CITY) + 420.0f);
}

// An airliner: R = throttle, B / A / L = brake, the circle pad banks left / right and pitches (pull back = nose up). It needs about 150 km/h to lift off and stalls below that.
static void plane_step(Game *g, int idx, const Input *in, float dt)
{
	Car *c = &g->car[idx];
	bool pilot = c->driven && in;
	float fx = sinf(c->yaw), fz = -cosf(c->yaw);
	float fs = c->vx * fx + c->vz * fz;                                           // (the horizontal velocity is along the heading)
	fs = fs / fmaxf(cosf(c->pitch), 0.3f);
	bool ground = c->y <= 0.06f;
	float thr = pilot && in->gas ? 1.0f : 0.0f, brk = pilot && (in->brake || in->hand) ? 1.0f : 0.0f, stickX = pilot ? in->lx : 0, pitchIn = pilot ? -in->ly : 0;
	c->braking = false;
	c->rpm += ((pilot ? 1.0f : 0.0f) - c->rpm) * fminf(1.0f, dt * 1.2f);
	c->rotor += dt * 60.0f * c->rpm;
	fs += (thr * 18.0f - 0.0034f * fs * fs - (ground ? 1.1f : 0.35f)) * dt;
	if (brk) { fs -= (ground ? 20.0f : 7.0f) * dt; c->braking = true; }
	if (fs < 0) fs = 0;
	if (ground) {
		c->yaw += stickX * 0.9f * clampf(fs / 10.0f, 0, 1) * dt;                  // nose-wheel steering
		if (fs > 30.0f && pitchIn > 0.25f) { c->pitch += 0.55f * dt; if (c->pitch > 0.08f) c->y = 0.1f; }       // rotate: the nose comes up and she lifts off
		else c->pitch += (0 - c->pitch) * fminf(1.0f, dt * 4.0f);
		c->lean += (0 - c->lean) * fminf(1.0f, dt * 5.0f);
		c->vy = 0;
	} else {
		float want = clampf(pitchIn, -1, 1) * 0.5f;
		c->pitch += (pilot ? (want - c->pitch) * 2.6f : (-0.35f - c->pitch) * 0.5f) * dt;
		if (!pilot || fabsf(pitchIn) < 0.1f) c->pitch += (0 - c->pitch) * 0.15f * dt;
		c->pitch = clampf(c->pitch, -0.6f, 0.45f);
		c->lean += ((pilot ? stickX * 0.95f : 0.0f) - c->lean) * fminf(1.0f, dt * 5.0f);
		c->yaw += sinf(c->lean) * 1.45f * dt;                                      // a small plane: banks quickly and turns tight
		fs -= sinf(c->pitch) * 6.0f * dt;                                        // climbing costs speed, diving gains it
		float stall = clampf((26.0f - fs) / 26.0f, 0, 1);
		c->vy = fs * sinf(c->pitch) - stall * stall * 45.0f;
		if (stall > 0.2f) c->pitch -= stall * 0.5f * dt;                          // the nose drops
	}
	if (fs > 72.0f) fs = 72.0f;
	c->y += c->vy * dt;
	if (c->y < 0) {
		if (c->vy < -10.0f || c->pitch > 0.4f || fabsf(c->lean) > 0.35f) damage_car(g, idx, (-c->vy - 6.0f) * 9.0f + fabsf(c->lean) * 60.0f, c->driven);
		c->y = 0; c->vy = 0;
	}
	if (c->y > 700.0f) { c->y = 700.0f; if (c->vy > 0) c->vy = 0; }
	fx = sinf(c->yaw); fz = -cosf(c->yaw);
	float hs = fs * cosf(c->pitch);
	c->vx = fx * hs; c->vz = fz * hs;
	c->x += c->vx * dt; c->z += c->vz * dt;
	c->wheel += fs * dt / 0.6f;
	for (int it = 0; it < 2; it++) {                                              // buildings that reach up to the plane
		float cx[3], cz[3];
		car_circles(c, cx, cz);
		float px = 0, pz = 0, nx = 0, nz = 0; int hits = 0;
		for (int k = 0; k < 3; k++) {
			float x = cx[k], z = cz[k], n[2];
			if (city_push_circle_y(&g->w, &x, &z, car_radius(CAR_PLANE), c->y - 0.5f, c->y + 3.0f, n)) { px += x - cx[k]; pz += z - cz[k]; nx += n[0]; nz += n[1]; hits++; }
		}
		if (!hits) break;
		c->x += px / hits; c->z += pz / hits;
		float l = sqrtf(nx * nx + nz * nz);
		if (l > 1e-5f) {
			nx /= l; nz /= l;
			float vn = c->vx * nx + c->vz * nz;
			if (vn < 0) { c->vx -= 1.2f * vn * nx; c->vz -= 1.2f * vn * nz; if (-vn > 6) damage_car(g, idx, (-vn - 6.0f) * 6.0f, c->driven); }
		}
	}
	c->x = clampf(c->x, -1800.0f, 2400.0f); c->z = clampf(c->z, -1800.0f, 2400.0f);
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

// ---- cover (Y): hide behind a wall or a car, lean out round its end to aim ------------------------------------------------------------------------
// Otto stands with his back against the cover, leaning on it, the camera behind him looking along the cover toward the nearer end. L (or R) leans him out round that end and he aims from there.
#define COVER_WALL 0.45f                     // from a wall's face to Otto's centre
#define COVER_CAR  1.4f                      // from a car's centre line to Otto's centre

// 0 nothing, 1 a building, 2 a parked car (its index in *car), 3 street furniture, under a small circle
static int cover_probe(const Game *g, float x, float z, float r, int *car)
{
	float px = x, pz = z, n[2];
	if (city_push_circle(&g->w, &px, &pz, r, n)) return 1;
	if (city_prop_push(&g->w, &px, &pz, r, n)) return 3;
	for (int i = 0; i < MAX_CAR; i++) {
		const Car *c = &g->car[i];
		if (!c->on || c->kind >= CAR_KINDS || c->wreck || c->driven || car_is_moving_traffic(c)) continue;
		float cx[3], cz[3]; car_circles(c, cx, cz);
		for (int k = 0; k < 3; k++) if ((cx[k] - x) * (cx[k] - x) + (cz[k] - z) * (cz[k] - z) < (car_radius(c->kind) * 0.95f + r) * (car_radius(c->kind) * 0.95f + r)) { if (car) *car = i; return 2; }
	}
	return 0;
}

// is there still cover just behind this spot (n points from the cover to the spot)?
static bool cover_valid(const Game *g, float x, float z, float nx, float nz)
{
	return cover_probe(g, x - nx * 0.8f, z - nz * 0.8f, 0.15f, NULL) != 0;
}

bool cover_find(const Game *g, float x, float z, float refYaw, float out[6])
{
	float best = 1e9f; int bk = -1, kind = 0, bcar = -1;
	for (int k = 0; k < 16; k++) {
		float a = k * (PI_F / 8), dx = sinf(a), dz = -cosf(a);
		for (float d = 0.3f; d <= 1.6f; d += 0.1f) {
			int car = -1, kd = cover_probe(g, x + dx * d, z + dz * d, 0.05f, &car);
			if (!kd) continue;
			float score = d + 0.7f * (1.0f - cosf(wrapang(a - refYaw)));
			if (kd == 2) {                                              // only the long side of a car counts
				const Car *c = &g->car[car];
				if (fabsf(dx * cosf(c->yaw) + dz * sinf(c->yaw)) < 0.5f) break;
			}
			if (score < best) { best = score; bk = k; kind = kd; bcar = car; }
			break;
		}
	}
	if (bk < 0) return false;
	float a = bk * (PI_F / 8), dx = sinf(a), dz = -cosf(a), nx, nz;
	if (kind == 2) {
		const Car *c = &g->car[bcar];
		float rx = cosf(c->yaw), rz = sinf(c->yaw), fx = sinf(c->yaw), fz = -cosf(c->yaw);
		float side = dx * rx + dz * rz > 0 ? -1.0f : 1.0f;
		float u = clampf((x - c->x) * fx + (z - c->z) * fz, -1.8f, 1.8f);
		nx = rx * side; nz = rz * side;
		out[0] = c->x + fx * u + nx * COVER_CAR; out[1] = c->z + fz * u + nz * COVER_CAR;
	} else {
		if (fabsf(dx) > fabsf(dz)) { nx = dx > 0 ? -1.0f : 1.0f; nz = 0; } else { nx = 0; nz = dz > 0 ? -1.0f : 1.0f; }       // the buildings are boxes: the wall faces an axis
		float sd = 0.3f;
		for (; sd < 1.8f; sd += 0.02f) if (cover_probe(g, x - nx * sd, z - nz * sd, 0.05f, NULL) == kind) break;
		out[0] = x - nx * (sd - COVER_WALL); out[1] = z - nz * (sd - COVER_WALL);
	}
	float tx = out[0], tz = out[1], n[2];                                // the spot must be free
	if (city_push_circle(&g->w, &tx, &tz, 0.38f, n)) return false;
	out[2] = nx; out[3] = nz; out[4] = (float)(kind == 2 ? bcar : -1); out[5] = kind >= 2 ? 1.0f : 0.0f;
	return true;
}

// the cover's end on one side: how far along it Otto stands from the end, and where he steps to look round it
bool cover_peek_offset(const Game *g, int side, float out[2])
{
	const Player *p = &g->p;
	float tx = p->coverNz * side, tz = -p->coverNx * side, d = 0;
	while (d < 1.0f && cover_valid(g, p->coverX + tx * d, p->coverZ + tz * d, p->coverNx, p->coverNz)) d += 0.05f;
	if (d >= 1.0f) return false;                                         // the end is not close
	float o = d + 0.65f;
	if (cover_probe(g, p->coverX + tx * o, p->coverZ + tz * o, 0.38f, NULL)) return false;
	out[0] = tx * o; out[1] = tz * o;
	return true;
}

static void cover_leave(Player *p) { p->cover = 0; p->peek = 0; p->peekT = 0; p->coverCar = -1; p->lean = 0; p->wrapT = 0; }

static void cover_toggle(Game *g, Input *in)
{
	Player *p = &g->p;
	if (!in->x_p || p->car >= 0 || p->ps != PS_ALIVE) return;
	if (p->cover) { cover_leave(p); in->x_p = false; return; }
	float f[6];
	if (!cover_find(g, p->p[0], p->p[2], g->cam.yaw, f)) return;           // nothing near: Y keeps its other job (next weapon)
	in->x_p = false;
	p->cover = f[5] > 0 ? 2 : 1; p->coverCar = (int)f[4];
	p->coverX = f[0]; p->coverZ = f[1]; p->coverNx = f[2]; p->coverNz = f[3];
	if (p->coverCar >= 0) { p->coverCarX = g->car[p->coverCar].x; p->coverCarZ = g->car[p->coverCar].z; }
	{ float fx = sinf(g->cam.yaw), fz = -cosf(g->cam.yaw), d = fx * p->coverNz - fz * p->coverNx; p->coverView = d < -0.2f ? -1 : 1; }      // look the way the camera was already looking
	p->peek = 0; p->peekT = 0; p->sprint = false; p->v[0] = p->v[2] = 0; p->lean = 0;
	p->p[0] = p->coverX; p->p[2] = p->coverZ;
	g->cam.manualT = 0;
}

// Keep pushing the stick at the end of a wall and he goes round the corner onto the next wall (an outside corner), or onto the wall in front of him (an inside corner).
static bool cover_wrap(Game *g, int s)
{
	Player *p = &g->p;
	float nx = p->coverNx, nz = p->coverNz, tx = nz * s, tz = -nx * s;      // the way he is sliding
	float bx = p->coverX, bz = p->coverZ, sx, sz, nnx, nnz;
	if (cover_probe(g, bx + tx * 0.35f, bz + tz * 0.35f, 0.3f, NULL)) {      // a wall ahead of him: an inside corner, he turns to the new wall
		float e = 0;
		while (e < 1.5f && !cover_probe(g, bx + tx * e, bz + tz * e, 0.05f, NULL)) e += 0.05f;
		if (e >= 1.5f) return false;
		sx = bx + tx * (e - COVER_WALL); sz = bz + tz * (e - COVER_WALL); nnx = -tx; nnz = -tz;
	} else {                                                               // the wall ends: round the corner onto the side of the building
		float d = 0;
		while (d < 1.0f && cover_valid(g, bx + tx * d, bz + tz * d, nx, nz)) d += 0.05f;
		if (d >= 1.0f) return false;
		float c = fmaxf(d - 0.15f, 0);
		sx = bx + tx * (c + COVER_WALL) - nx * (COVER_WALL + 0.4f); sz = bz + tz * (c + COVER_WALL) - nz * (COVER_WALL + 0.4f); nnx = tx; nnz = tz;
	}
	if (!cover_valid(g, sx, sz, nnx, nnz) || cover_probe(g, sx, sz, 0.3f, NULL) || cover_probe(g, sx - nnx * 0.8f, sz - nnz * 0.8f, 0.15f, NULL) == 2) return false;
	p->coverX = sx; p->coverZ = sz; p->coverNx = nnx; p->coverNz = nnz; p->coverView = s; p->wrapT = 0.3f;
	return true;
}

static void cover_step(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	float nx = p->coverNx, nz = p->coverNz, tx = nz, tz = -nx;           // along the cover (the +tangent)
	bool gone = p->ps != PS_ALIVE || !cover_valid(g, p->coverX, p->coverZ, nx, nz);
	if (p->coverCar >= 0) {
		const Car *c = &g->car[p->coverCar];
		if (!c->on || c->wreck || c->driven || hypotf(c->x - p->coverCarX, c->z - p->coverCarZ) > 0.3f) gone = true;
	}
	if (gone) { cover_leave(p); return; }
	float m = in->lx;                                                      // the stick slides him along the cover: right = toward the screen's right, which is always the +tangent side (see the camera below)
	if (p->wrapT > 0) p->wrapT -= dt;
	if (p->peek == 0 && p->peekT < 0.05f && fabsf(m) > 0.15f && p->wrapT <= 0) {          // slide along the cover
		p->coverView = m > 0 ? 1 : -1;
		float step = m * 2.3f * dt, lead = m > 0 ? 0.35f : -0.35f;
		float x = p->coverX + tx * step, z = p->coverZ + tz * step, hx = tx * lead, hz = tz * lead;
		if (cover_valid(g, x + hx, z + hz, nx, nz) && !cover_probe(g, x, z, 0.3f, NULL)) { p->coverX = x; p->coverZ = z; }
		else if (fabsf(m) > 0.6f && p->coverCar < 0) cover_wrap(g, m > 0 ? 1 : -1);
	}
	p->peekT += ((p->peek ? 1.0f : 0.0f) - p->peekT) * fminf(1.0f, dt * (p->peek ? 9.0f : 7.0f));
	if (!p->peek && p->peekT < 0.02f) p->peekT = 0;
	float e = p->peekT * p->peekT * (3 - 2 * p->peekT);
	float gx = p->coverX + p->peekDx * e, gz = p->coverZ + p->peekDz * e;
	if (p->wrapT > 0) { float k = fminf(1.0f, dt * 12.0f); p->p[0] += (gx - p->p[0]) * k; p->p[2] += (gz - p->p[2]) * k; }       // glide round the corner
	else { p->p[0] = gx; p->p[2] = gz; }
	p->v[0] = p->v[2] = 0; p->speed = 0;
	float want = atan2f(nx, -nz);                                          // back to the cover, facing the street
	if (p->aimT < 0.3f) p->face = wrapang(p->face + wrapang(want - p->face) * fminf(1.0f, dt * 10.0f));
	float cr = p->peekT > 0.4f ? 0.0f : p->cover == 2 ? 1.0f : 0.0f;
	p->crouch += (cr - p->crouch) * fminf(1.0f, dt * 9.0f);
	p->lean += ((p->peekT > 0.4f ? 0.0f : 1.0f) - p->lean) * fminf(1.0f, dt * 8.0f);        // back flat against the cover while hidden
}

// ---- on foot -------------------------------------------------------------------------------------------------------------------------
static void foot_step(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	// the stick's frame of reference is the camera's heading, but it stays put while the camera swings round after Otto (below) and catches up when the stick is let go
	if (hypotf(in->lx, in->ly) < 0.12f || p->speed < 1.0f || g->cam.manualT > 0 || p->aimT > 0.3f || g->turnT > 0) g->cam.ctrlYaw = g->cam.yaw;
	float sy = sinf(g->cam.ctrlYaw), cy = cosf(g->cam.ctrlYaw);
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
	if (p->p[1] > city_roof_h(&g->w, p->p[0], p->p[2], 0.3f) + 0.03f) { top = mag > 0.05f ? mag * (p->chute > 0.5f ? 8.5f : 5.0f) : 0.0f; p->sprint = false; }       // in the air: steer the fall, or glide under the parachute
	float wl = sqrtf(wx * wx + wz * wz);
	float tx = 0, tz = 0;
	if (wl > 1e-4f) { tx = wx / wl * top; tz = wz / wl * top; }
	float k = fminf(1.0f, dt * 12.0f);
	p->v[0] += (tx - p->v[0]) * k; p->v[2] += (tz - p->v[2]) * k;
	float d[3] = { p->v[0] * dt, 0, p->v[2] * dt };
	float ox = p->p[0], oz = p->p[2];
	Contact ct; city_move(&g->w, p->p, d, &ct);
	{ float pn[2]; city_prop_push(&g->w, &p->p[0], &p->p[2], 0.4f, pn); city_push_circle_y(&g->w, &p->p[0], &p->p[2], 0.4f, p->p[1] + 0.1f, p->p[1] + 1.8f, pn); }       // bins and boxes on the pavement, and the airport's buildings, are solid
{                                                                      // gravity: roofs are floors, stepping off one (or jumping out of an aircraft) is a fall; a parachute opens after a second or so
	float fl = city_roof_h(&g->w, p->p[0], p->p[2], 0.3f);
	if (p->p[1] > fl + 0.03f) {
		p->airT += dt;
		p->v[1] = fmaxf(-42.0f, p->v[1] - 22.0f * dt);
		if (in->enter_p && p->chute <= 0 && p->airT > 0.2f && p->p[1] - fl > 6.0f) p->chute = 0.02f;      // X opens the parachute (it is never automatic)
		if (p->chute > 0) { p->chute = fminf(1.0f, p->chute + dt * 3.0f); p->v[1] += (-5.2f - p->v[1]) * fminf(1.0f, dt * 6.0f * p->chute); }
		p->p[1] += p->v[1] * dt;
		if (p->p[1] <= fl) { float imp = -p->v[1]; p->p[1] = fl; p->v[1] = 0; if (imp > (p->chute > 0.5f ? 18.0f : 11.0f)) damage_player(g, (imp - (p->chute > 0.5f ? 18.0f : 11.0f)) * 20.0f); p->chute = 0; p->airT = 0; }       // only an unopened (or too late) parachute hurts
	} else { p->v[1] = 0; p->chute = 0; p->airT = 0; }
}
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
	{ float pn[2]; city_push_circle_y(&g->w, &p->p[0], &p->p[2], 0.4f, p->p[1] + 0.1f, p->p[1] + 1.8f, pn); }          // being shoved by cars and people must not push him into a wall
	if (!world_walkable(&g->w, p->p[0], p->p[2])) {                         // the edge of the world (sea, pier rails): slide along it
		float nx = p->p[0], nz = p->p[2];
		if (world_walkable(&g->w, nx, oz)) p->p[2] = oz; else if (world_walkable(&g->w, ox, nz)) p->p[0] = ox; else { p->p[0] = ox; p->p[2] = oz; }
	}
	float sp = sqrtf(p->v[0] * p->v[0] + p->v[2] * p->v[2]);
	p->speed = sp;
	if (sp > 0.3f) {
		float want = atan2f(p->v[0], -p->v[2]);
		if (p->aimT < 0.3f) p->face = wrapang(p->face + wrapang(want - p->face) * fminf(1.0f, dt * 12.0f));
		p->phase += dt * sp / gait_stride(sp);                        // cycles: the feet keep up with the ground
	}
	// the camera drifts round to get behind Otto whichever way he runs (gently: the faster he goes the quicker)
	if (g->cam.manualT <= 0 && p->aimT < 0.3f && g->cam.look == 0 && g->turnT <= 0 && p->speed > 1.8f) {
		float heading = atan2f(p->v[0], -p->v[2]), rate = 1.25f * clampf((p->speed - 1.8f) / 3.0f, 0.0f, 1.0f);
		g->cam.yaw = wrapang(g->cam.yaw + wrapang(heading - g->cam.yaw) * fminf(1.0f, dt * rate));
	}
}

static void enter_exit(Game *g, const Input *in)
{
	Player *p = &g->p;
	if (!in->enter_p) return;
	if (p->car < 0) {
		if (p->p[1] > 1.0f) return;                                                  // in the air X is the parachute, not a door
		int i = game_nearest_car(g, p->p[0], p->p[2], ENTER_DIST);
		if (i >= 0) {
			if (g->car[i].ai.on) { traffic_eject_driver(g, i); g->car[i].ai.on = false; g->car[i].vx = g->car[i].vz = 0; add_heat(g, 14.0f); }
			if (g->car[i].kind == CAR_POLICE || g->car[i].kind == CAR_SWAT) { add_heat(g, 30.0f); g->car[i].bot = false; g->car[i].owner = 0; }
			p->car = i; g->car[i].driven = true; p->v[0] = p->v[2] = 0; g->hintT = 0;
		}
	} else {
		Car *c = &g->car[p->car];
		float sp = sqrtf(c->vx * c->vx + c->vz * c->vz), pos[2];
		bool air = (c->kind == CAR_HELI || c->kind == CAR_PLANE) && c->y > 1.5f;          // in the air Otto jumps and skydives; anything else he can leave at any speed
		if (c->kind == CAR_PLANE && !air && sp > 12.0f) return;
		if (exit_spot(g, c, -1, pos) || exit_spot(g, c, 1, pos)) {
			p->p[0] = pos[0]; p->p[2] = pos[1]; p->p[1] = air ? c->y : 0; p->face = c->yaw; p->v[0] = c->vx * 0.6f; p->v[2] = c->vz * 0.6f; p->v[1] = air ? c->vy : 0; p->chute = 0; p->airT = 0;      // he keeps some of the car's speed
			if (sp > 10.0f && !air) damage_player(g, (sp - 10.0f) * 1.5f);                  // and the landing hurts when it is fast
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
	if (p->car < 0 && p->cover && p->ps == PS_ALIVE && p->aimT < 0.5f && c->manualT <= 0) {      // in cover the camera sits behind Otto and looks along the cover, toward the end he leans out of
		float k = fminf(1.0f, dt * 4.0f);
		int v = p->peek == 1 || p->peek == -1 ? p->peek : p->coverView;
		float hx = -0.5f * p->coverNx + 0.87f * v * p->coverNz, hz = -0.5f * p->coverNz - 0.87f * v * p->coverNx;      // along the cover the way he last moved, turned a little toward it so the corner and the street are both in view
		c->yaw = wrapang(c->yaw + wrapang(atan2f(hx, -hz) - c->yaw) * k);
		c->pitch += (-0.3f - c->pitch) * k;
	}
	float tx, ty, tz, dist, fov;
	if (p->car >= 0) {
		const Car *car = &g->car[p->car];
		float spd = sqrtf(car->vx * car->vx + car->vz * car->vz);
		if (c->manualT <= 0) {
			float want = car->yaw;
			if (spd > 4.0f && car->kind != CAR_HELI && car->kind != CAR_PLANE) {                                          // lean toward the travel direction in a drift
				float vd = atan2f(car->vx, -car->vz), fw = car->vx * sinf(car->yaw) - car->vz * cosf(car->yaw);
				if (fw > 0) want = car->yaw + wrapang(vd - car->yaw) * 0.35f;
			}
			c->yaw = wrapang(c->yaw + wrapang(want - c->yaw) * fminf(1.0f, dt * 2.6f));
		}
		float sk = clampf((spd - 22.0f) / 30.0f, 0, 1);                      // 0..1 as the car goes from 80 to 190 km/h
		tx = car->x; ty = 1.6f - 0.2f * sk; tz = car->z; dist = 8.6f + fminf(spd, 55.0f) * 0.045f; fov = 62 + fminf(spd, 55.0f) * 0.4f;
		if (car->kind == CAR_HELI) { ty = car->y + 1.6f; dist = 15.0f + fminf(spd, 42.0f) * 0.1f; fov = 66 + fminf(spd, 42.0f) * 0.2f; }
		else if (car->kind == CAR_PLANE) {
			ty = car->y + 2.6f; dist = 15.0f + fminf(spd, 72.0f) * 0.1f; fov = 64 + fminf(spd, 72.0f) * 0.12f;
			if (c->manualT <= 0) c->pitch += (-0.16f - car->pitch * 0.5f - c->pitch) * fminf(1.0f, dt * 2.0f);
		}
		else if (car->kind == CAR_BIKE) dist = 7.2f + fminf(spd, 55.0f) * 0.045f;
	} else {
		tx = p->p[0]; ty = p->p[1] + 1.45f; tz = p->p[2]; dist = 4.6f + p->speed * 0.12f; fov = 60 + p->speed * 0.5f;
		if (p->airT > 0.05f) { dist = 7.5f; fov = 66; }                      // falling / under the parachute: a wider view
		if (p->cover) {                                                      // behind Otto and above, so a car does not hide the view; shifted so the street he leans out to is in the picture
			ty = p->p[1] + 1.6f - 0.5f * p->crouch; dist = 3.9f; fov = 58;
			tx += p->coverNx * 0.7f; tz += p->coverNz * 0.7f;                  // a little toward the street, so the cover is not in the way
		}
		float sh = p->ps == PS_ALIVE && !p->cover && !weapon_melee(p->weapon) && p->aimT > 0.35f ? 1.0f : 0.0f;       // aiming a gun: the camera comes in close and slides over Otto's right shoulder, so the weapon fills the view
		c->shoulder += (sh - c->shoulder) * fminf(1.0f, dt * 6.0f);
		if (c->shoulder > 0.01f) {
			float k = c->shoulder, off = 0.8f * k, rx = cosf(c->yaw), rz = sinf(c->yaw), o3[3] = { tx, ty, tz }, d3[3] = { rx, 0, rz }, n3[3];
			float hit = city_ray(&g->w, o3, d3, off + 0.4f, n3);              // a wall beside him: do not go through it
			if (hit >= 0 && hit - 0.4f < off) off = fmaxf(0, hit - 0.4f);
			tx += rx * off; tz += rz * off; ty += 0.08f * k;
			dist = lerpf(dist, 2.5f, k); fov = lerpf(fov, 52.0f, k);
		}
		if (p->weapon == W_SNIPER && in->aim && p->aimT > 0.5f) { fov = 30; dist = 2.2f; }          // looking through the scope
	}
	c->fov += (fov - c->fov) * fminf(1.0f, dt * 3.0f);
	c->dist += (dist - c->dist) * fminf(1.0f, dt * 4.0f);
	// aiming at somebody far off: the camera tips to frame them (it goes back to the player's own angle when he stops aiming)
	{
		float want = 0;
		if (p->car < 0 && p->ps == PS_ALIVE && p->aimT > 0.4f) {
			int tg = p->lock >= 0 ? p->lock : p->target;
			if (tg >= 0 && g->ped[tg].on) {
				const Ped *q = &g->ped[tg];
				float d = hypotf(q->x - p->p[0], q->z - p->p[2]);
				float fp = clampf(atan2f(1.4f - (p->p[1] + 2.2f), d + 3.0f), -0.45f, 0.1f);
				c->framePitch += (fp - c->framePitch) * fminf(1.0f, dt * 8.0f);
				c->zoomK += (clampf((d - 14.0f) / 40.0f, 0, 1) - c->zoomK) * fminf(1.0f, dt * 6.0f);       // the further away, the more the view closes in
				want = 1;
			}
		}
		c->aimFrame += (want - c->aimFrame) * fminf(1.0f, dt * (want > c->aimFrame ? 7.0f : 2.5f));
		if (want == 0 && c->aimFrame < 0.02f) { c->aimFrame = 0; c->framePitch = c->pitch; }
	}
	float vpitch = c->pitch + (c->framePitch - c->pitch) * c->aimFrame;
	float cp = cosf(vpitch), sp = sinf(vpitch);
	float vyaw = c->yaw + c->look;                                          // the view: the camera's heading plus the D-pad glance
	float f[3] = { sinf(vyaw) * cp, sp, -cosf(vyaw) * cp };
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

// The weapon wheel: D-pad up opens it, the circle pad or D-pad picks a slice (the weapons in order, clockwise from the top) and equips it at once;
// SELECT, A or B closes it. The world runs in slow motion while it is open.
// D-pad right / left / down: hold to glance that way (down = look back), double-tap to turn Otto and the camera that way. Up still tilts the camera.
static void dpad_look(Game *g, const Input *in, float dt)
{
	static const float OFF[3] = { PI_F / 2, -PI_F / 2, PI_F };
	Player *p = &g->p; Cam *c = &g->cam;
	bool held[3] = { in->dpx > 0, in->dpx < 0, in->dpy < 0 };
	bool can = !g->wheel && p->car < 0 && !p->cover && p->ps == PS_ALIVE;
	for (int i = 0; i < 3; i++) {
		g->dpT[i] += dt;
		if (held[i] && !g->dpPrev[i]) {
			if (g->dpT[i] < 0.32f && can) { g->turnT = 0.22f; g->turnAmt = OFF[i]; g->dpSup[i] = true; g->dpT[i] = 9.0f; }
			else g->dpT[i] = 0;
		}
		if (!held[i]) g->dpSup[i] = false;
		g->dpPrev[i] = held[i];
	}
	if (g->turnT > 0) {                                                   // the double-tap turn, eased
		float k = fminf(dt, g->turnT) / 0.22f, d = g->turnAmt * k;
		g->turnT -= dt; c->yaw = wrapang(c->yaw + d); c->ctrlYaw = c->yaw; p->face = wrapang(p->face + d);
	}
	float x = (held[0] && !g->dpSup[0] ? 1.0f : 0.0f) - (held[1] && !g->dpSup[1] ? 1.0f : 0.0f), y = held[2] && !g->dpSup[2] ? -1.0f : 0.0f, goal = 0;
	if (!g->wheel && (x != 0 || y != 0)) goal = atan2f(x, y);
	c->look = wrapang(c->look + wrapang(goal - c->look) * fminf(1.0f, dt * 9.0f));
	if (fabsf(c->look) < 0.003f && goal == 0) c->look = 0;
}

static void wheel_update(Game *g, const Input *in, float dt)
{
	Player *p = &g->p;
	g->wheelT += dt;
	float x = in->dpx, y = in->dpy;
	if (g->wheelHold) { if (in->dpy <= 0) g->wheelHold = false; else y = 0; }                  // the press that opened the wheel is not a choice
	if (x == 0 && y == 0 && hypotf(in->lx, in->ly) > 0.5f) { x = in->lx; y = in->ly; }
	if (x == 0 && y == 0) return;
	int sector = ((int)floorf((atan2f(x, y) + PI_F / W_COUNT) / (2 * PI_F / W_COUNT)) + W_COUNT) % W_COUNT;
	int w = sector;
	g->wheelSel = w;
	if (weapon_usable(p, w)) p->weapon = w;
}

// ---- the world map (bottom screen): tap the WORLD button, tap a town, tap TRAVEL --------------------------------------------------------------------------
static void fast_travel(Game *g, int dest)
{
	Player *p = &g->p;
	if (g->regionGo >= 0 || dest == g->region || p->ps != PS_ALIVE) return;
	if (g->stars > 0) { toast(g, "Lose the cops first"); return; }
	if (p->car >= 0 && (g->car[p->car].kind == CAR_HELI || g->car[p->car].kind == CAR_PLANE) && g->car[p->car].y > 0.6f) { toast(g, "Land first"); return; }
	if (p->car < 0 && p->airT > 0.05f) { toast(g, "Not while falling"); return; }
	char b[28]; snprintf(b, sizeof b, "To %s", REGIONS[dest].name); toast(g, b);
	region_go(g, dest, 4 * PITCH + 3.0f, 4 * PITCH + 24.0f, -100.0f);
	g->mapMode = 0; g->mapSel = -1;
}
static void map_touch(Game *g, const Input *in)
{
	if (!in->touch_p || g->wheel) return;
	float tx = in->tx, ty = in->ty;
	if (in_box(tx, ty, MAP_BTN_X, MAP_TOGGLE_Y, MAP_BTN_W, MAP_BTN_H)) { g->mapMode ^= 1; g->mapSel = -1; return; }
	if (g->mapMode != 1) return;
	for (int r = 0; r < REG_COUNT; r++) { float x, y, w, h; world_tile(r, &x, &y, &w, &h); if (in_box(tx, ty, x, y, w, h)) { g->mapSel = r == g->region ? -1 : r; return; } }
	if (g->mapSel >= 0 && in_box(tx, ty, MAP_BTN_X, MAP_TRAVEL_Y, MAP_BTN_W, MAP_BTN_H)) fast_travel(g, g->mapSel);
}

void game_update(Game *g, const Input *in, float dt)
{
	if (in->start_p) g->paused = !g->paused;
	if (g->paused) {
		if (in->up_p) g->showFps = !g->showFps, g->saveNeeded = true;
		if (in->down_p) g->invertY = !g->invertY, g->saveNeeded = true;
		if (in->a_p && g->p.ps == PS_ALIVE) { g->paused = false; player_respawn(g); }          // A on the pause screen: respawn at the hospital (stuck? lost?)
		if (in->select_p) { g->paused = false; g->wheel = false; game_start(g); }           // SELECT on the pause screen: back to the start
		return;
	}
	g->time += dt;
	if (g->hintT > 0) g->hintT -= dt;
	region_update(g, dt);
	if (in->up_p && !g->wheel && g->p.ps == PS_ALIVE) { g->wheel = true; g->wheelT = 0; g->wheelSel = g->p.weapon; g->wheelHold = in->dpy > 0; }      // D-pad up opens the weapon wheel 
	else if (g->wheel && (in->a_p || in->b_p || in->select_p)) g->wheel = false;
	map_touch(g, in);
	if (g->p.ps != PS_ALIVE) g->wheel = false;
	if (g->wheel) wheel_update(g, in, dt);
	dpad_look(g, in, dt);
	Input once = *in;                                   // button presses are used once, in the first physics step
	if (g->p.ps != PS_ALIVE) { once.gas = once.brake = once.hand = once.aim = once.enter_p = once.x_p = false; once.lx = once.ly = 0; }
	if (g->wheel) { once.gas = once.brake = once.hand = once.aim = once.enter_p = once.x_p = once.a_p = once.b_p = false; once.lx = once.ly = 0; }
	float sdt = dt * g->timeScale * (g->wheel ? 0.15f : 1.0f);   // the world runs slowly while Otto is being wasted or busted, or choosing a weapon
	float hs = sdt <= 0.036f ? sdt : sdt * 0.5f;           // one step up to 28 fps, two below: a slow frame must not queue up more physics (that is what makes slow frames slower)
	for (float t = 0; t < sdt - 1e-6f;) {
		float h = fminf(hs, sdt - t); t += h;
		Player *p = &g->p;
		if (p->car >= 0 || p->cover) g->cam.ctrlYaw = g->cam.yaw;
		cover_toggle(g, &once);
		if (p->car < 0) { if (p->cover) cover_step(g, &once, h); else foot_step(g, &once, h); }
		if (!p->cover) p->crouch = fmaxf(0.0f, p->crouch - h * 6.0f);
		for (int i = 0; i < MAX_CAR; i++) {
			Car *c = &g->car[i];
			if (!c->on || c->ai.on) continue;
			if (c->kind == CAR_HELI) heli_step(g, i, i == p->car ? &once : NULL, h);
			else if (c->kind == CAR_PLANE) plane_step(g, i, i == p->car ? &once : NULL, h);
			else if (c->bot) { Input bi; memset(&bi, 0, sizeof bi); bi.lx = c->botLx; bi.gas = c->botGas; bi.brake = c->botBrake; bi.hand = c->botHand; car_step(g, i, &bi, h); }
			else car_step(g, i, i == p->car ? &once : NULL, h);
		}
		traffic_update(g, h);
		cars_collide(g);
		if (p->car >= 0) {
			const Car *c = &g->car[p->car];
			p->p[0] = c->x; p->p[1] = c->kind == CAR_HELI || c->kind == CAR_PLANE ? c->y : 0; p->p[2] = c->z; p->v[0] = c->vx; p->v[2] = c->vz;
			p->speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
		}
		if (once.enter_p && p->cover) cover_leave(p);                              // X gets into the car (it may be the cover itself)
		enter_exit(g, &once);
		combat_update(g, &once, h);
		police_update(g, h);
		once.enter_p = once.select_p = once.start_p = once.up_p = once.down_p = once.x_p = false; once.weap = 0;
	}
	region_check(g);
	if (sdt < dt && g->p.ps != PS_ALIVE) { /* keep the respawn timers honest while time is slowed: combat_update scales its own dt */ }
	Input cin = *in;                                    // the D-pad picks weapons while the wheel is open, it does not turn the camera
	if (g->wheel) cin.cx = cin.cy = cin.tdx = cin.tdy = 0;
	camera_step(g, &cin, dt);
}
