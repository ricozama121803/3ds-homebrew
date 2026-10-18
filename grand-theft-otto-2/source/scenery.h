// Grand Theft Otto II - the LA scenery around the grid, included into scene.c (header-style: it is not compiled on its own) (it uses that file's mesh helpers):
// the outskirts and the Pacific on the west, the beach with a boardwalk, palms and lifeguard towers, the pier, the hills to the north with the
// Hollywood sign and an observatory, and the elevated freeway along the east edge. Every piece goes into its own chunk so it is only drawn
// when it can be seen.

// ---- helpers ----------------------------------------------------------------------------------------------------------------------------
// a box with four sides and a top (no bottom), flat-shaded; uv scales the texture coordinates (units per metre) for textured materials
static void sbox(int mat, float x0, float y0, float z0, float x1, float y1, float z1, const float col[3], float uv)
{
	Mesh *m = &SM(mat);
	float sides[5][4][3] = {
		{ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 } },     // north face (-z)
		{ { x1, y0, z1 }, { x0, y0, z1 }, { x0, y1, z1 }, { x1, y1, z1 } },     // south (+z)
		{ { x0, y0, z1 }, { x0, y0, z0 }, { x0, y1, z0 }, { x0, y1, z1 } },     // west (-x)
		{ { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 } },     // east (+x)
		{ { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } } };   // top
	static const float nrm[5][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
	for (int f = 0; f < 5; f++) {
		float k = shade(nrm[f][0], nrm[f][1], nrm[f][2]), c[3] = { col[0] * k, col[1] * k, col[2] * k };
		SV v[4];
		for (int i = 0; i < 4; i++) {
			float *p = sides[f][i];
			float u = f < 2 ? p[0] * uv : f < 4 ? p[2] * uv : p[0] * uv, w = f == 4 ? p[2] * uv : p[1] * uv;
			v[i] = sv(p[0], p[1], p[2], mat == MAT_WHITE || mat >= MAT_COUNT ? 0.5f : u, mat == MAT_WHITE || mat >= MAT_COUNT ? 0.5f : w, c, 1);
		}
		quad(m, &v[0], &v[1], &v[2], &v[3]);
	}
}
// a horizontal quad with a texture scaled by uv (units per metre)
static void flat(int mat, float x0, float z0, float x1, float z1, float y, const float col[3], float uv)
{
	SV a = sv(x0, y, z0, x0 * uv, z0 * uv, col, 1), b = sv(x1, y, z0, x1 * uv, z0 * uv, col, 1), c = sv(x1, y, z1, x1 * uv, z1 * uv, col, 1), d = sv(x0, y, z1, x0 * uv, z1 * uv, col, 1);
	quad(&SM(mat), &a, &b, &c, &d);
}
static float vnoise(float x, float y)                                          // smooth value noise, 0..1
{
	int xi = (int)floorf(x), yi = (int)floorf(y);
	float fx = x - xi, fy = y - yi, sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
	float a = hf(hash2(xi, yi)), b = hf(hash2(xi + 1, yi)), c = hf(hash2(xi, yi + 1)), d = hf(hash2(xi + 1, yi + 1));
	return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy;
}

// ---- chunk table --------------------------------------------------------------------------------------------------------------------------
#define FWY_X0   (CITY + 62.0f)
#define FWY_X1   (CITY + 92.0f)
#define FWY_Y    9.0f                                                          // deck top
#define FWY_Z0   (-420.0f)
#define FWY_Z1   940.0f
static void chunk_info(int c, float *cx, float *cz, float *cy, float *rv, float *rh)
{
	if (c < NCH_GRID) {
		*cx = ((c % CH_N) + 0.5f) * CH_BLOCKS * PITCH; *cz = ((c / CH_N) + 0.5f) * CH_BLOCKS * PITCH; *cy = 120.0f;
		*rh = 0.5f * CH_BLOCKS * PITCH * 1.42f; *rv = *rh + 135.0f;
		return;
	}
	*cy = 20.0f;
	switch (c) {
	case EX_BEACH: *cx = -38.0f; *cz = CITY * 0.5f; *rh = 285.0f; break;
	case EX_PIER:  *cx = -205.0f; *cz = 256.0f; *rh = 175.0f; break;
	case EX_HILLS: *cx = CITY * 0.5f; *cz = -480.0f; *cy = 60.0f; *rh = 700.0f; break;
	case EX_NORTH: *cx = 270.0f; *cz = -85.0f; *rh = 290.0f; *cy = 30.0f; break;                      // Capitol Records and Dodger Stadium
	case EX_SOUTH: *cx = CITY * 0.38f; *cz = CITY + 95.0f; *rh = 45.0f; *cy = 20.0f; break;          // the Theme Building
	case EX_AIR: *cx = 290.0f; *cz = CITY + 235.0f; *rh = 330.0f; *cy = 30.0f; break;                 // the airport's runways, taxiway and paving
	case EX_AIR_T: *cx = 370.0f; *cz = CITY + 150.0f; *rh = 140.0f; *cy = 30.0f; break;               // the terminal, tower, gates and parked airliners
	case EX_AIR_W: *cx = 92.0f; *cz = CITY + 142.0f; *rh = 60.0f; *cy = 30.0f; break;                 // the hangars
	default: {
		int k = c - EX_FWY0;
		*cx = CITY + 77.0f; *cz = FWY_Z0 + (k + 0.5f) * (FWY_Z1 - FWY_Z0) / 4; *rh = 0.5f * (FWY_Z1 - FWY_Z0) / 4 + 20.0f; break; }
	}
	*rv = *rh + 20.0f;
}

// ---- outskirts and the sea ------------------------------------------------------------------------------------------------------------------
static void outskirts(void)
{
	curChunk = NCH;
	const RegionDef *rd = &REGIONS[SG->region];
	const bool city = SG->region == REG_CITY, desert = SG->region == REG_HILLS;
	const float sea[3] = { 0.85f, 0.92f, 1.0f };
	const float xe = 2600.0f, zn = -2600.0f, zs = CITY + 2600.0f, step = 500.0f, xw = city ? BEACH_X + 6 : -2600.0f;
	// dry land: everything around the grid (in the city: east of the beach)
	for (float z = zn; z < zs - 1; z += step) for (float x = xw; x < xe - 1; x += step) {
		float x1 = fminf(x + step, xe), z1 = fminf(z + step, zs);
		flat(desert ? MAT_SAND : MAT_GRASS, x, z, x1, z1, -0.12f, rd->dry, desert ? 1.0f / 14.0f : 1.0f / 22.0f);
	}
	if (!city) return;
	// the Pacific, west of the beach
	for (float z = zn; z < zs - 1; z += step) for (float x = -2700.0f; x < BEACH_X + 5.9f; x += step) {
		float x1 = fminf(x + step, BEACH_X + 6), z1 = fminf(z + step, zs);
		flat(MAT_WATER, x, z, x1, z1, WATER_Y, sea, 1.0f / 50.0f);
	}
}

// ---- beach, boardwalk, pier -------------------------------------------------------------------------------------------------------------------
static void beach(void)
{
	const float sand[3] = { 1.0f, 1.0f, 1.0f }, wet[3] = { 0.78f, 0.74f, 0.66f }, walk[3] = { 0.9f, 0.88f, 0.84f };
	// the strip along the city: boardwalk (pavement), sand, then a short slope into the water
	curChunk = EX_BEACH;
	flat(MAT_ROOF, -14.0f, 0, 0, CITY, 0.02f, walk, 1.0f / 8.0f);
	flat(MAT_SAND, -70.0f, 0, -14.0f, CITY, 0.0f, sand, 1.0f / 9.0f);
	{ SV a = sv(-70, 0, 0, -70 / 9.0f, 0, wet, 1), b = sv(-76, WATER_Y, 0, -76 / 9.0f, 0, wet, 1), c = sv(-76, WATER_Y, CITY, -76 / 9.0f, CITY / 9.0f, wet, 1), d = sv(-70, 0, CITY, -70 / 9.0f, CITY / 9.0f, wet, 1); quad(&SM(MAT_SAND), &a, &b, &c, &d); }
	// the coast carries on past the city to the north and south (always drawn: a few quads)
	curChunk = NCH;
	for (int s = 0; s < 2; s++) {
		float z0 = s ? CITY : -2600.0f, z1 = s ? CITY + 2600.0f : 0.0f;
		flat(MAT_SAND, -70.0f, z0, -14.0f, z1, 0.0f, sand, 1.0f / 9.0f);
		SV a = sv(-70, 0, z0, -70 / 9.0f, 0, wet, 1), b = sv(-76, WATER_Y, z0, -76 / 9.0f, 0, wet, 1), c = sv(-76, WATER_Y, z1, -76 / 9.0f, 1, wet, 1), d = sv(-70, 0, z1, -70 / 9.0f, 1, wet, 1);
		quad(&SM(MAT_SAND), &a, &b, &c, &d);
	}
	// lifeguard towers, beach umbrellas
	curChunk = EX_BEACH;
	static const float white[3] = { 0.92f, 0.92f, 0.9f }, red[3] = { 0.9f, 0.18f, 0.14f }, wood[3] = { 0.55f, 0.4f, 0.26f };
	for (int i = 0; i < 4; i++) {
		float z = 70 + i * 120.0f + 15.0f * (i & 1), x = -44.0f;
		sbox(MAT_WHITE, x - 1.2f, 0, z - 1.0f, x - 0.9f, 2.6f, z - 0.7f, wood, 1); sbox(MAT_WHITE, x + 0.9f, 0, z - 1.0f, x + 1.2f, 2.6f, z - 0.7f, wood, 1);
		sbox(MAT_WHITE, x - 1.2f, 0, z + 0.7f, x - 0.9f, 2.6f, z + 1.0f, wood, 1); sbox(MAT_WHITE, x + 0.9f, 0, z + 0.7f, x + 1.2f, 2.6f, z + 1.0f, wood, 1);
		sbox(MAT_WHITE, x - 1.5f, 2.6f, z - 1.3f, x + 1.5f, 3.0f, z + 1.3f, wood, 1);
		sbox(MAT_WHITE, x - 1.1f, 3.0f, z - 0.9f, x + 1.1f, 4.4f, z + 0.9f, white, 1);
		sbox(MAT_WHITE, x - 1.4f, 4.4f, z - 1.2f, x + 1.4f, 4.7f, z + 1.2f, red, 1);
		sbox(MAT_WHITE, x + 1.2f, 0, z - 0.3f, x + 3.2f, 0.5f, z + 0.3f, wood, 1);                // ramp-ish steps
	}
	unsigned st = 0xBEAC4u;
	for (int i = 0; i < (SG->lowq ? 5 : 14); i++) {
		float x = -62.0f + rndf(&st) * 38.0f, z = 20.0f + rndf(&st) * (CITY - 40.0f);
		if (near_pad(x, z, 10.0f)) continue;
		const float *c = i % 3 == 0 ? red : i % 3 == 1 ? white : (const float[3]){ 0.2f, 0.45f, 0.85f };
		sbox(MAT_WHITE, x - 0.04f, 0, z - 0.04f, x + 0.04f, 2.0f, z + 0.04f, wood, 1);
		pyramid(MAT_WHITE, x - 1.5f, z - 1.5f, x + 1.5f, z + 1.5f, 2.0f, 0.7f, c, false);
	}
	// the pier: planks on pillars, rails, an arcade shed or two, and the Ferris wheel (dynamic) at the end
	curChunk = EX_PIER;
	static const float plank[3] = { 1.05f, 1.0f, 0.95f }, post[3] = { 0.36f, 0.27f, 0.2f }, rail[3] = { 0.8f, 0.78f, 0.72f };
	sbox(MAT_PLANK, PIER_X, -0.45f, PIER_Z0, BEACH_X + 6.0f, 0.06f, PIER_Z1, plank, 0.25f);
	sbox(MAT_PLANK, PIER_X, -0.45f, PIER_Z0 - 11.0f, PIER_X + 40.0f, 0.06f, PIER_Z1 + 11.0f, plank, 0.25f);          // the wide platform at the end
	for (float x = BEACH_X - 6.0f; x > PIER_X; x -= 18.0f) {
		for (int s = 0; s < 2; s++) { float z = s ? PIER_Z1 - 1.0f : PIER_Z0 + 0.4f; sbox(MAT_WHITE, x - 0.3f, WATER_Y - 0.5f, z, x + 0.3f, -0.45f, z + 0.6f, post, 1); }
		for (int s = 0; s < 2; s++) { float z = s ? PIER_Z1 - 0.15f : PIER_Z0; sbox(MAT_WHITE, x - 0.07f, 0, z, x + 0.07f, 1.05f, z + 0.15f, rail, 1); }
	}
	for (int s = 0; s < 2; s++) {
		float z = s ? PIER_Z1 - 0.15f : PIER_Z0;
		sbox(MAT_WHITE, PIER_X + 40.0f, 0.95f, z, BEACH_X + 6.0f, 1.05f, z + 0.15f, rail, 1);
		sbox(MAT_WHITE, PIER_X + 40.0f, 0.5f, z, BEACH_X + 6.0f, 0.58f, z + 0.15f, rail, 1);
	}
	for (int s = 0; s < 2; s++) {                                                                                   // arcade sheds on the end platform
		Box b; memset(&b, 0, sizeof b);
		float zc = s ? PIER_Z1 + 5.0f : PIER_Z0 - 5.0f;
		b.x0 = PIER_X + 4.0f; b.x1 = PIER_X + 26.0f; b.z0 = zc - 3.5f; b.z1 = zc + 3.5f; b.y0 = b.yb = 0; b.y1 = 5.5f; b.mat = s ? MAT_CREAM : MAT_RED; b.bld = -1; b.tint[0] = b.tint[1] = b.tint[2] = 1.0f;
		for (int k = 0; k < 4; k++) wall(&b, k);
		roof_top(&b);
	}
}

// palms along the boardwalk
static void beach_palms(void)
{
	curChunk = EX_BEACH;
	unsigned st = 0x9A1Au;
	for (float z = 12.0f; z < CITY - 8.0f; z += 22.0f) {
		float tn[3] = { 0.9f + 0.2f * rndf(&st), 1.0f, 0.9f + 0.2f * rndf(&st) };
		prop_m(&PALMS[rnd_next(&st) % PALM_COUNT], -9.0f + (rndf(&st) - 0.5f) * 2.0f, 0, z + (rndf(&st) - 0.5f) * 6.0f, rndf(&st) * 6.28f, 0.95f + 0.4f * rndf(&st), tn);
	}
}

// ---- hills, the sign, the observatory ----------------------------------------------------------------------------------------------------------------
static struct { float x, z, y, r; } PATCH[2];                                 // flattened spots on the hills: the sign's terrace and the observatory's
static float hill_raw(float x, float z)
{
	float u = clampf((-z - 120.0f) / 900.0f, 0, 1);
	float ridge = u * u * (3 - 2 * u) * (0.35f + 0.65f * clampf(u * 2.5f, 0, 1));
	float n = 0.6f * vnoise(x * 0.0045f + 3, z * 0.0045f) + 0.3f * vnoise(x * 0.011f, z * 0.011f + 9) + 0.1f * vnoise(x * 0.03f, z * 0.03f);
	return ridge * (70.0f + 330.0f * powf(n, 1.25f));
}
static float hill_h(float x, float z)
{
	float h = hill_raw(x, z);
	for (int i = 0; i < 2; i++) {
		float d = sqrtf((x - PATCH[i].x) * (x - PATCH[i].x) + (z - PATCH[i].z) * (z - PATCH[i].z));
		float w = clampf((PATCH[i].r * 2.6f - d) / (PATCH[i].r * 1.6f), 0, 1);
		w = w * w * (3 - 2 * w);
		h = lerpf(h, PATCH[i].y, w);
	}
	return h < 0 ? 0 : h;
}

static const char *SIGN_GLYPH[8][7] = {                                           // 5 x 7 block letters: H O L Y W D (index in the order of SIGN_TEXT)
	{ "X...X", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X" },
	{ ".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX." },
	{ "X....", "X....", "X....", "X....", "X....", "X....", "XXXXX" },
	{ "X...X", "X...X", ".X.X.", "..X..", "..X..", "..X..", "..X.." },
	{ "X...X", "X...X", "X...X", "X.X.X", "X.X.X", "XX.XX", "X...X" },
	{ "XXXX.", "X...X", "X...X", "X...X", "X...X", "X...X", "XXXX." } };

static void hills(void)
{
	curChunk = EX_HILLS;
	// where the sign and the observatory go: the highest spot in the middle of the ridge, and a shoulder to the west
	float bx = CITY * 0.55f, bz = -380.0f, bh = -1;
	for (float z = -520; z <= -300; z += 20) for (float x = 120; x <= 420; x += 20) { float h = hill_raw(x, z); if (h > bh) { bh = h; bx = x; bz = z; } }
	PATCH[0].x = bx; PATCH[0].z = bz; PATCH[0].y = bh; PATCH[0].r = 95.0f;
	PATCH[1].x = 20.0f; PATCH[1].z = -300.0f; PATCH[1].y = hill_raw(20.0f, -300.0f); PATCH[1].r = 38.0f;
	enum { NX = 22, NZ = 16 };
	const float x0 = -640.0f, x1 = CITY + 640.0f, z0 = -1100.0f, z1 = -110.0f;
	static float H[NZ + 1][NX + 1];
	for (int j = 0; j <= NZ; j++) for (int i = 0; i <= NX; i++) H[j][i] = hill_h(x0 + (x1 - x0) * i / NX, z0 + (z1 - z0) * j / NZ);
	for (int j = 0; j < NZ; j++) for (int i = 0; i < NX; i++) {
		float xa = x0 + (x1 - x0) * i / NX, xb = x0 + (x1 - x0) * (i + 1) / NX, za = z0 + (z1 - z0) * j / NZ, zb = z0 + (z1 - z0) * (j + 1) / NZ;
		V3 p[4] = { v3(xa, H[j][i], za), v3(xb, H[j][i + 1], za), v3(xb, H[j + 1][i + 1], zb), v3(xa, H[j + 1][i], zb) };
		for (int t = 0; t < 2; t++) {
			V3 a = p[0], b = p[t ? 2 : 1], c = p[t ? 3 : 2];
			V3 n = norm(cross(sub(c, a), sub(b, a)));
			if (n.y < 0) n = mul(n, -1);
			float k = shadev(n) * 1.15f, hm = (a.y + b.y + c.y) / 3.0f, nz = vnoise((a.x + b.x + c.x) * 0.012f, (a.z + b.z + c.z) * 0.012f);
			float dry[3] = { 0.60f + 0.14f * nz, 0.54f + 0.10f * nz, 0.32f + 0.06f * nz }, hi[3] = { 0.52f, 0.43f, 0.33f }, tt = clampf(hm / 220.0f, 0, 1), col[3];
			for (int q = 0; q < 3; q++) col[q] = lerpf(dry[q], hi[q], tt) * k;
			SV A = sv(a.x, a.y, a.z, 0.5f, 0.5f, col, 1), B = sv(b.x, b.y, b.z, 0.5f, 0.5f, col, 1), C = sv(c.x, c.y, c.z, 0.5f, 0.5f, col, 1);
			tri(&SM(MAT_WHITE), &A, &B, &C);
		}
	}
	// the sign: white block letters standing on the terrace, facing south
	{
		const char *text = "HOLLYWOOD";
		static const int glyph[9] = { 0, 1, 2, 2, 3, 4, 1, 1, 5 };
		const float cell = 6.0f, gap = 1.5f * cell, width = 5 * cell;
		float total = 9 * width + 8 * gap, sx0 = PATCH[0].x - total * 0.5f, base = PATCH[0].y + 3.0f;
		static const float white[3] = { 1.55f, 1.55f, 1.5f }, post[3] = { 0.5f, 0.5f, 0.52f };
		(void)text;
		for (int L = 0; L < 9; L++) {
			float lx = sx0 + L * (width + gap);
			const char **rows = SIGN_GLYPH[glyph[L]];
			// merge runs: a vertical run of identical single cells becomes one rectangle
			bool used[7][5] = { { 0 } };
			for (int c = 0; c < 5; c++) for (int r = 0; r < 7; r++) {
				if (rows[r][c] != 'X' || used[r][c]) continue;
				int r1 = r; while (r1 + 1 < 7 && rows[r1 + 1][c] == 'X' && !used[r1 + 1][c]) r1++;
				int c1 = c; bool wide = r1 == r; if (wide) while (c1 + 1 < 5 && rows[r][c1 + 1] == 'X' && !used[r][c1 + 1]) c1++;
				for (int rr = r; rr <= r1; rr++) for (int cc = c; cc <= c1; cc++) used[rr][cc] = true;
				float xa = lx + c * cell, xb = lx + (c1 + 1) * cell, ya = base + (6 - r1) * cell, yb = base + (7 - r) * cell;
				float zc = PATCH[0].z;
				SV a = sv(xa, ya, zc, 0.5f, 0.5f, white, 1), b = sv(xb, ya, zc, 0.5f, 0.5f, white, 1), c2 = sv(xb, yb, zc, 0.5f, 0.5f, white, 1), d = sv(xa, yb, zc, 0.5f, 0.5f, white, 1);
				quad(&SM(MAT_WHITE), &a, &b, &c2, &d);
			}
			sbox(MAT_WHITE, lx + 2.0f, PATCH[0].y - 2.0f, PATCH[0].z - 0.4f, lx + 2.8f, base + 2.0f, PATCH[0].z + 0.4f, post, 1);
			sbox(MAT_WHITE, lx + width - 3.0f, PATCH[0].y - 2.0f, PATCH[0].z - 0.4f, lx + width - 2.2f, base + 2.0f, PATCH[0].z + 0.4f, post, 1);
		}
	}
	// the observatory: a white main block with two wings and copper domes
	{
		float ox = PATCH[1].x, oz = PATCH[1].z, oy = PATCH[1].y;
		static const float cream[3] = { 1.0f, 0.99f, 0.95f };
		sbox(MAT_WHITE, ox - 24.0f, oy - 6.0f, oz - 9.0f, ox + 24.0f, oy + 1.0f, oz + 9.0f, (const float[3]){ 0.7f, 0.68f, 0.62f }, 1);                // podium
		struct { float x0, x1, h; } part[3] = { { -9, 9, 13 }, { -23, -9, 8 }, { 9, 23, 8 } };
		for (int k = 0; k < 3; k++) {
			Box b; memset(&b, 0, sizeof b);
			b.x0 = ox + part[k].x0; b.x1 = ox + part[k].x1; b.z0 = oz - 6.0f; b.z1 = oz + 6.0f; b.y0 = b.yb = oy + 1.0f; b.y1 = oy + 1.0f + part[k].h; b.mat = MAT_CREAM; b.bld = -1;
			b.tint[0] = cream[0]; b.tint[1] = cream[1]; b.tint[2] = cream[2];
			for (int s = 0; s < 4; s++) wall(&b, s);
			roof_top(&b);
		}
		for (int k = 0; k < 3; k++) {
			Box d; memset(&d, 0, sizeof d);
			float r = k == 0 ? 8.0f : 3.8f, cx = ox + (k == 0 ? 0 : k == 1 ? -16.0f : 16.0f);
			d.x0 = cx - r; d.x1 = cx + r; d.z0 = oz - r; d.z1 = oz + r; d.y0 = oy + 1.0f + (k == 0 ? 13.0f : 8.0f); d.tint[0] = r;
			dome(&d);
		}
	}
}

// ---- the freeway along the east edge ---------------------------------------------------------------------------------------------------------------
static void freeway(void)
{
	static const float slab[3] = { 0.62f, 0.62f, 0.64f }, asphalt[3] = { 0.5f, 0.5f, 0.53f }, barrier[3] = { 0.8f, 0.8f, 0.78f }, pillar[3] = { 0.7f, 0.69f, 0.66f }, line[3] = { 1.7f, 1.7f, 1.6f }, yellow[3] = { 1.8f, 1.45f, 0.2f };
	const float seg = (FWY_Z1 - FWY_Z0) / 4;
	for (int k = 0; k < 4; k++) {
		curChunk = EX_FWY0 + k;
		float za = FWY_Z0 + k * seg, zb = za + seg;
		sbox(MAT_WHITE, FWY_X0, FWY_Y - 1.5f, za, FWY_X1, FWY_Y, zb, slab, 1);
		flat(MAT_WHITE, FWY_X0 + 0.5f, za, FWY_X1 - 0.5f, zb, FWY_Y + 0.01f, asphalt, 0.0f);
		sbox(MAT_WHITE, FWY_X0, FWY_Y, za, FWY_X0 + 0.5f, FWY_Y + 1.0f, zb, barrier, 1);
		sbox(MAT_WHITE, FWY_X1 - 0.5f, FWY_Y, za, FWY_X1, FWY_Y + 1.0f, zb, barrier, 1);
		sbox(MAT_WHITE, 0.5f * (FWY_X0 + FWY_X1) - 0.4f, FWY_Y, za, 0.5f * (FWY_X0 + FWY_X1) + 0.4f, FWY_Y + 0.9f, zb, barrier, 1);
		for (int l = 0; l < 4; l++) {                                                // lane lines: three lanes each way
			static const float lx[4] = { 4.0f, 8.0f, 17.0f, 21.0f };
			flat(MAT_WHITE, FWY_X0 + lx[l] - 0.08f, za, FWY_X0 + lx[l] + 0.08f, zb, FWY_Y + 0.02f, line, 0.0f);
		}
		flat(MAT_WHITE, FWY_X0 + 14.2f, za, FWY_X0 + 14.4f, zb, FWY_Y + 0.02f, yellow, 0.0f);
		for (float z = za + 15.0f; z < zb; z += 34.0f) for (int c = 0; c < 2; c++) {
			float x = c ? FWY_X1 - 6.0f : FWY_X0 + 6.0f;
			sbox(MAT_WHITE, x - 1.1f, 0, z - 1.1f, x + 1.1f, FWY_Y - 1.5f, z + 1.1f, pillar, 1);
		}
	}
}

// ---- LA landmarks: Capitol Records, Dodger Stadium, the LAX Theme Building, the Venice sign, Randy's donut, helipads (City Hall is a block of the city itself) ----------------
static void prism(Mesh *m, V3 a, V3 b, V3 s, V3 f, float w0, float d0, float w1, float d1, V3 col, bool capA);       // defined with the dynamic geometry below

// a vertical (or conical) drum: n flat-shaded sides from radius r0 at y0 to r1 at y1, with an optional lid
static void drum(float cx, float cz, float y0, float y1, float r0, float r1, int n, const float col[3], bool lid)
{
	Mesh *m = &SM(MAT_WHITE);
	float slope = (r0 - r1) / (y1 - y0);
	for (int i = 0; i < n; i++) {
		float a0 = i * 2 * PI_F / n, a1 = (i + 1) * 2 * PI_F / n, am = 0.5f * (a0 + a1);
		V3 nn = norm(v3(cosf(am), slope, sinf(am)));
		float k = shadev(nn), c[3] = { col[0] * k, col[1] * k, col[2] * k };
		SV p0 = sv(cx + r0 * cosf(a0), y0, cz + r0 * sinf(a0), 0.5f, 0.5f, c, 1), p1 = sv(cx + r0 * cosf(a1), y0, cz + r0 * sinf(a1), 0.5f, 0.5f, c, 1);
		SV p2 = sv(cx + r1 * cosf(a1), y1, cz + r1 * sinf(a1), 0.5f, 0.5f, c, 1), p3 = sv(cx + r1 * cosf(a0), y1, cz + r1 * sinf(a0), 0.5f, 0.5f, c, 1);
		quad(m, &p0, &p1, &p2, &p3);
		if (lid && r1 > 0.01f) {
			float kt = shade(0, 1, 0), ct[3] = { col[0] * kt, col[1] * kt, col[2] * kt };
			SV t0 = sv(cx, y1, cz, 0.5f, 0.5f, ct, 1), t1 = sv(cx + r1 * cosf(a1), y1, cz + r1 * sinf(a1), 0.5f, 0.5f, ct, 1), t2 = sv(cx + r1 * cosf(a0), y1, cz + r1 * sinf(a0), 0.5f, 0.5f, ct, 1);
			tri(m, &t0, &t1, &t2);
		}
	}
}
// a flat ring (annulus) between two radii at height y, facing up
static void ring_flat(float cx, float cz, float y, float r0, float r1, int n, const float col[3], int mat)
{
	float k = shade(0, 1, 0), c[3] = { col[0] * k, col[1] * k, col[2] * k };
	for (int i = 0; i < n; i++) {
		float a0 = i * 2 * PI_F / n, a1 = (i + 1) * 2 * PI_F / n;
		SV p0 = sv(cx + r0 * cosf(a0), y, cz + r0 * sinf(a0), 0.5f, 0.5f, c, 1), p1 = sv(cx + r1 * cosf(a0), y, cz + r1 * sinf(a0), 0.5f, 0.5f, c, 1);
		SV p2 = sv(cx + r1 * cosf(a1), y, cz + r1 * sinf(a1), 0.5f, 0.5f, c, 1), p3 = sv(cx + r0 * cosf(a1), y, cz + r0 * sinf(a1), 0.5f, 0.5f, c, 1);
		quad(&SM(mat), &p0, &p3, &p2, &p1);
	}
}
// a slanted band between two rings (the stadium's seating bowl)
static void bowl_band(float cx, float cz, float ya, float yb, float ra, float rb, int n, const float col[3])
{
	for (int i = 0; i < n; i++) {
		float a0 = i * 2 * PI_F / n, a1 = (i + 1) * 2 * PI_F / n, am = 0.5f * (a0 + a1);
		V3 nn = norm(v3(-cosf(am) * (yb - ya), fabsf(ra - rb), -sinf(am) * (yb - ya)));
		float k = shadev(nn), c[3] = { col[0] * k, col[1] * k, col[2] * k };
		SV p0 = sv(cx + ra * cosf(a0), ya, cz + ra * sinf(a0), 0.5f, 0.5f, c, 1), p1 = sv(cx + ra * cosf(a1), ya, cz + ra * sinf(a1), 0.5f, 0.5f, c, 1);
		SV p2 = sv(cx + rb * cosf(a1), yb, cz + rb * sinf(a1), 0.5f, 0.5f, c, 1), p3 = sv(cx + rb * cosf(a0), yb, cz + rb * sinf(a0), 0.5f, 0.5f, c, 1);
		quad(&SM(MAT_WHITE), &p0, &p1, &p2, &p3);
	}
}
static void sbeam(V3 a, V3 b, float w0, float d0, float w1, float d1, const float col[3])
{
	V3 d = norm(sub(b, a)), side = norm(cross(d, v3(0, 1, 0)));
	if (vlen(cross(d, v3(0, 1, 0))) < 0.1f) side = v3(1, 0, 0);
	prism(&SM(MAT_WHITE), a, b, side, norm(cross(side, d)), w0, d0, w1, d1, v3(col[0], col[1], col[2]), true);
}

static const char *EXTRA_GLYPH[5][7] = {                                           // V E N I C
	{ "X...X", "X...X", "X...X", "X...X", ".X.X.", ".X.X.", "..X.." },
	{ "XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX" },
	{ "X...X", "XX..X", "X.X.X", "X.X.X", "X..XX", "X...X", "X...X" },
	{ "XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX" },
	{ ".XXXX", "X....", "X....", "X....", "X....", "X....", ".XXXX" } };

// Capitol Records: a stack of round floors, slabs of white between bands of dark glass, a needle on top
static void capitol(float cx, float cz)
{
	curChunk = EX_NORTH;
	static const float slab[3] = { 1.35f, 1.33f, 1.28f }, glass[3] = { 0.30f, 0.42f, 0.58f }, base[3] = { 0.8f, 0.78f, 0.72f }, red[3] = { 1.6f, 0.25f, 0.2f };
	drum(cx, cz, -0.1f, 3.0f, 9.0f, 9.0f, 10, base, true);                                   // the low podium
	float y = 3.0f;
	for (int f = 0; f < 11; f++) {
		drum(cx, cz, y, y + 1.9f, 6.9f, 6.9f, 10, glass, false);
		drum(cx, cz, y + 1.9f, y + 2.5f, 7.7f, 7.7f, 10, slab, true);                       // each floor's slab sticks out past the glass
		y += 2.5f;
	}
	drum(cx, cz, y, y + 2.0f, 6.0f, 4.2f, 10, slab, true);                                   // the cap
	drum(cx, cz, y + 2.0f, y + 12.0f, 0.9f, 0.1f, 6, red, false);                           // the needle
}

// Dodger Stadium: four pastel tiers, the blue seating bowl, the green field, six light towers, the parking lot around it
static void dodger(float cx, float cz)
{
	curChunk = EX_NORTH;
	static const float cream[3] = { 1.25f, 1.15f, 0.95f }, sky[3] = { 0.55f, 0.85f, 1.2f }, orange[3] = { 1.5f, 0.85f, 0.35f }, yellow[3] = { 1.5f, 1.35f, 0.5f }, seat[3] = { 0.18f, 0.32f, 0.85f }, seat2[3] = { 0.5f, 0.55f, 0.75f };
	static const float lot[3] = { 0.5f, 0.5f, 0.53f }, grass[3] = { 0.9f, 1.0f, 0.9f }, dirt[3] = { 1.1f, 0.85f, 0.6f }, steel[3] = { 0.75f, 0.76f, 0.8f }, lamp[3] = { 2.2f, 2.2f, 1.8f };
	const int N = 20;
	ring_flat(cx, cz, 0.02f, 50.0f, 108.0f, 16, lot, MAT_WHITE);
	drum(cx, cz, 0.0f, 5.5f, 47.0f, 47.0f, N, cream, false);
	drum(cx, cz, 5.5f, 10.0f, 46.0f, 45.0f, N, sky, false);
	drum(cx, cz, 10.0f, 14.0f, 44.0f, 43.0f, N, orange, false);
	drum(cx, cz, 14.0f, 18.0f, 42.0f, 41.0f, N, yellow, false);
	ring_flat(cx, cz, 18.0f, 36.0f, 41.5f, N, cream, MAT_WHITE);
	bowl_band(cx, cz, 18.0f, 10.0f, 38.0f, 30.0f, N, seat);
	bowl_band(cx, cz, 10.0f, 4.0f, 30.0f, 22.0f, N, seat2);
	bowl_band(cx, cz, 4.0f, 1.2f, 22.0f, 17.0f, N, seat);
	{ Mesh *m = &SM(MAT_GRASS); float k = shade(0, 1, 0), c[3] = { grass[0] * k, grass[1] * k, grass[2] * k };
	  for (int i = 0; i < N; i++) { float a0 = i * 2 * PI_F / N, a1 = (i + 1) * 2 * PI_F / N;
		SV f0 = sv(cx, 1.2f, cz, cx / 12, cz / 12, c, 1), f1 = sv(cx + 17.0f * cosf(a1), 1.2f, cz + 17.0f * sinf(a1), (cx + 17.0f * cosf(a1)) / 12, (cz + 17.0f * sinf(a1)) / 12, c, 1), f2 = sv(cx + 17.0f * cosf(a0), 1.2f, cz + 17.0f * sinf(a0), (cx + 17.0f * cosf(a0)) / 12, (cz + 17.0f * sinf(a0)) / 12, c, 1);
		tri(m, &f0, &f1, &f2); } }
	{ float k = shade(0, 1, 0), c[3] = { dirt[0] * k, dirt[1] * k, dirt[2] * k };                      // the infield: a diamond of dirt
	  SV d0 = sv(cx, 1.25f, cz + 11.0f, 0.5f, 0.5f, c, 1), d1 = sv(cx + 9.0f, 1.25f, cz, 0.5f, 0.5f, c, 1), d2 = sv(cx, 1.25f, cz - 7.0f, 0.5f, 0.5f, c, 1), d3 = sv(cx - 9.0f, 1.25f, cz, 0.5f, 0.5f, c, 1);
	  quad(&SM(MAT_WHITE), &d0, &d1, &d2, &d3); }
	for (int i = 0; i < 6; i++) {                                                                       // light towers
		float a = i * 2 * PI_F / 6 + 0.5f, x = cx + 46.0f * cosf(a), z = cz + 46.0f * sinf(a);
		sbox(MAT_WHITE, x - 0.45f, 0, z - 0.45f, x + 0.45f, 36.0f, z + 0.45f, steel, 1);
		sbox(MAT_WHITE, x - 3.6f, 36.0f, z - 0.7f, x + 3.6f, 40.0f, z + 0.7f, steel, 1);
		sbox(MAT_WHITE, x - 3.4f, 36.4f, z - 0.75f, x + 3.4f, 39.6f, z + 0.75f, lamp, 1);
	}
	sbox(MAT_WHITE, cx - 7.0f, 18.0f, cz - 46.5f, cx + 7.0f, 25.0f, cz - 44.0f, steel, 1);          // the big scoreboard
	sbox(MAT_WHITE, cx - 6.4f, 18.5f, cz - 44.1f, cx + 6.4f, 24.4f, cz - 43.8f, seat, 1);
}

// the Theme Building at LAX: two crossing arches carrying a saucer-shaped restaurant
static void theme_building(float cx, float cz)
{
	curChunk = EX_SOUTH;
	static const float white[3] = { 1.4f, 1.4f, 1.38f }, glass[3] = { 0.28f, 0.42f, 0.58f }, roofc[3] = { 1.3f, 1.3f, 1.3f }, steel[3] = { 0.8f, 0.8f, 0.85f };
	const int SEG = 6; const float SPAN = 28.0f, TOP = 19.0f;
	for (int arch = 0; arch < 2; arch++) for (int i = 0; i < 2 * SEG; i++) {
		float t0 = -1.0f + i * (1.0f / SEG), t1 = t0 + 1.0f / SEG;
		V3 a = arch ? v3(cx, TOP * (1 - t0 * t0), cz + SPAN * t0) : v3(cx + SPAN * t0, TOP * (1 - t0 * t0), cz);
		V3 b = arch ? v3(cx, TOP * (1 - t1 * t1), cz + SPAN * t1) : v3(cx + SPAN * t1, TOP * (1 - t1 * t1), cz);
		float w0 = 1.1f + 1.3f * t0 * t0, w1 = 1.1f + 1.3f * t1 * t1;
		sbeam(a, b, arch ? 1.5f : w0, arch ? w0 : 1.5f, arch ? 1.5f : w1, arch ? w1 : 1.5f, white);
	}
	drum(cx, cz, TOP + 0.2f, TOP + 1.8f, 5.0f, 16.0f, 14, white, false);                       // underside of the saucer
	drum(cx, cz, TOP + 1.8f, TOP + 4.3f, 16.0f, 16.0f, 14, glass, false);                      // the dining room's windows
	drum(cx, cz, TOP + 4.3f, TOP + 6.0f, 16.0f, 6.0f, 14, roofc, true);                        // the dome of the roof
	drum(cx, cz, TOP + 6.0f, TOP + 14.0f, 1.3f, 0.5f, 8, steel, true);                         // the tower with its antenna
	drum(cx, cz, TOP + 14.0f, TOP + 20.0f, 0.2f, 0.05f, 4, steel, false);
}

// the Venice sign: an arch of letters over the street at the boardwalk, lettered on both sides
static void venice_sign(float x, float zc)
{
	curChunk = EX_BEACH;
	static const float post[3] = { 0.5f, 0.52f, 0.58f }, board[3] = { 0.12f, 0.2f, 0.45f }, neon[3] = { 2.4f, 2.2f, 1.5f };
	const float cell = 0.54f, span = 12.4f;
	sbox(MAT_WHITE, x - 0.4f, 0, zc - span - 0.4f, x + 0.4f, 9.4f, zc - span + 0.4f, post, 1);
	sbox(MAT_WHITE, x - 0.4f, 0, zc + span - 0.4f, x + 0.4f, 9.4f, zc + span + 0.4f, post, 1);
	sbox(MAT_WHITE, x - 0.3f, 5.0f, zc - span, x + 0.3f, 9.8f, zc + span, board, 1);
	static const int glyph[6] = { 6, 7, 8, 9, 10, 7 };                                                 // V E N I C E
	const float lw = 5 * cell, gap = 0.7f, total = 6 * lw + 5 * gap;
	for (int side = 0; side < 2; side++) {
		float px = side ? x - 0.34f : x + 0.34f;
		for (int L = 0; L < 6; L++) {
			const char **rows = EXTRA_GLYPH[glyph[L] - 6];
			float u0 = L * (lw + gap);                                                                  // distance along the reading direction
			for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) {
				if (rows[r][c] != 'X') continue;
				float zc0 = side ? zc - total * 0.5f + u0 + c * cell : zc + total * 0.5f - u0 - (c + 1) * cell, zc1 = zc0 + cell, ya = 9.2f - (r + 1) * cell * 0.9f - 0.15f, yb = ya + cell * 0.9f;
				SV a = sv(px, ya, zc0, 0.5f, 0.5f, neon, 1), b = sv(px, ya, zc1, 0.5f, 0.5f, neon, 1), cc = sv(px, yb, zc1, 0.5f, 0.5f, neon, 1), d = sv(px, yb, zc0, 0.5f, 0.5f, neon, 1);
				quad(&SM(MAT_WHITE), &a, &b, &cc, &d);
			}
		}
	}
}

// Randy's Donuts: a giant doughnut standing on posts on a rooftop
static void randys_donut(void)
{
	const World *w = &SG->w;
	int i0 = w->bfirst[5][3], n = w->bcount[5][3];
	if (n <= 0 || w->park[5][3]) return;
	const Box *b = &w->box[i0];
	for (int k = 0; k < n; k++) if (w->box[i0 + k].y1 > b->y1 && w->box[i0 + k].kind == BK_BLDG) b = &w->box[i0 + k];
	float cx = 0.5f * (b->x0 + b->x1), cz = 0.5f * (b->z0 + b->z1), y0 = b->y1;
	chunk_at(cx, cz);
	static const float dough[3] = { 1.3f, 0.92f, 0.5f }, icing[3] = { 1.75f, 0.7f, 0.95f }, post[3] = { 0.5f, 0.5f, 0.55f };
	const float R = 2.7f, r = 1.1f, yc = y0 + 3.0f + R + r;
	sbox(MAT_WHITE, cx - 0.2f, y0, cz - 1.4f, cx + 0.2f, yc - R, cz - 1.0f, post, 1);
	sbox(MAT_WHITE, cx - 0.2f, y0, cz + 1.0f, cx + 0.2f, yc - R, cz + 1.4f, post, 1);
	const int NT = 14, NP = 6;
	for (int i = 0; i < NT; i++) for (int j = 0; j < NP; j++) {
		float th[2] = { i * 2 * PI_F / NT, (i + 1) * 2 * PI_F / NT }, ph[2] = { j * 2 * PI_F / NP, (j + 1) * 2 * PI_F / NP };
		SV v[4]; V3 nsum = v3(0, 0, 0);
		for (int q = 0; q < 4; q++) {
			float t = th[(q == 1 || q == 2) ? 1 : 0], p = ph[(q >= 2) ? 1 : 0];
			V3 rad = v3(0, cosf(t), sinf(t)), nn = add(mul(rad, cosf(p)), v3(sinf(p), 0, 0));
			V3 pt = add(v3(cx, yc + R * rad.y, cz + R * rad.z), mul(nn, r));
			nsum = add(nsum, nn);
			v[q] = sv(pt.x, pt.y, pt.z, 0.5f, 0.5f, dough, 1);
		}
		V3 nm = norm(nsum);
		bool front = fabsf(nm.x) > 0.5f;                                                               // the faces that look along the axis carry the icing
		float k = shadev(nm); const float *base = front ? icing : dough;
		float c[3] = { base[0] * k, base[1] * k, base[2] * k };
		for (int q = 0; q < 4; q++) { v[q].c[0] = c[0]; v[q].c[1] = c[1]; v[q].c[2] = c[2]; }
		quad(&SM(MAT_WHITE), &v[0], &v[1], &v[2], &v[3]);
		quad(&SM(MAT_WHITE), &v[0], &v[3], &v[2], &v[1]);                                              // seen from both sides
	}
}

// a helipad: a dark square with a yellow ring and a white H
static void helipads(void)
{
	static const float dark[3] = { 0.42f, 0.42f, 0.46f }, ring[3] = { 1.7f, 1.4f, 0.25f }, white[3] = { 1.7f, 1.7f, 1.6f };
	for (int i = 0; i < SG->npad; i++) {
		float x = SG->pad[i][0], z = SG->pad[i][1];
		bool beach = SG->region == REG_CITY && x < 0;
		if (!beach) chunk_at(x, z); else curChunk = EX_BEACH;
		float y = !beach ? 0.14f : 0.05f;
		ring_flat(x, z, y, 0.0f, 7.4f, 16, ring, MAT_WHITE);
		ring_flat(x, z, y + 0.02f, 0.0f, 6.7f, 16, dark, MAT_WHITE);
		flat(MAT_WHITE, x - 2.2f, z - 2.6f, x - 1.4f, z + 2.6f, y + 0.05f, white, 0.0f);
		flat(MAT_WHITE, x + 1.4f, z - 2.6f, x + 2.2f, z + 2.6f, y + 0.05f, white, 0.0f);
		flat(MAT_WHITE, x - 1.4f, z - 0.4f, x + 1.4f, z + 0.4f, y + 0.05f, white, 0.0f);
	}
}

// the airport south of the city: plaza with the Theme Building, terminal and tower, hangars, an apron with four parked airliners, taxiway and two runways.
// (Its solid parts are the AIRBOX table in game.h; two more airliners take off and land in scene.c.)
static void airliner(Mesh *m, V3 pos, float yaw, float pitch, float roll, float sc, const V3 body, const V3 accent, bool gear);
static void airport(void)
{
	curChunk = EX_AIR;
	const float zc = CITY;
	static const float conc[3] = { 0.66f, 0.66f, 0.68f }, asph[3] = { 0.34f, 0.34f, 0.37f }, line[3] = { 1.6f, 1.6f, 1.5f }, yel[3] = { 1.7f, 1.4f, 0.25f };
	static const float cream[3] = { 1.05f, 1.02f, 0.95f }, glass[3] = { 0.95f, 1.0f, 1.05f }, white[3] = { 1.3f, 1.3f, 1.3f }, grey[3] = { 0.7f, 0.72f, 0.76f }, dark[3] = { 0.18f, 0.2f, 0.26f };
	// paving: the plaza and apron, the taxiway, the runways with their connectors
	flat(MAT_ROOF, 100.0f, zc + 40.0f, 520.0f, zc + 215.0f, 0.04f, conc, 1.0f / 8.0f);
	flat(MAT_ROOF, 30.0f, zc + 60.0f, 100.0f, zc + 215.0f, 0.04f, conc, 1.0f / 8.0f);
	flat(MAT_ROOF, 30.0f, zc + 215.0f, 560.0f, zc + 235.0f, 0.04f, asph, 1.0f / 8.0f);
	for (int k = 0; k < 2; k++) {
		float rz = k ? RWY2_Z : RWY1_Z;
		flat(MAT_ROOF, 10.0f, rz - 22.5f, 560.0f, rz + 22.5f, 0.04f, asph, 1.0f / 8.0f);
		for (float x = 30.0f; x < 540.0f; x += 50.0f) flat(MAT_WHITE, x, rz - 0.6f, x + 30.0f, rz + 0.6f, 0.07f, line, 0);
		flat(MAT_WHITE, 10.0f, rz - 22.2f, 560.0f, rz - 21.8f, 0.07f, line, 0);
		flat(MAT_WHITE, 10.0f, rz + 21.8f, 560.0f, rz + 22.2f, 0.07f, line, 0);
		for (int b = 0; b < 8; b++) for (int end = 0; end < 2; end++) { float x0 = end ? 541.0f : 14.0f; flat(MAT_WHITE, x0, rz - 19.0f + b * 5.0f, x0 + 12.0f, rz - 17.0f + b * 5.0f, 0.07f, line, 0); }
	}
	for (int k = 0; k < 3; k++) {                                          // connectors: taxiway - runway 1 - runway 2
		float x = 110.0f + k * 190.0f;
		flat(MAT_ROOF, x, zc + 235.0f, x + 16.0f, RWY1_Z - 22.5f, 0.04f, asph, 1.0f / 8.0f);
		flat(MAT_ROOF, x, RWY1_Z + 22.5f, x + 16.0f, RWY2_Z - 22.5f, 0.04f, asph, 1.0f / 8.0f);
	}
	flat(MAT_WHITE, 30.0f, zc + 224.6f, 560.0f, zc + 225.4f, 0.07f, yel, 0);
	for (int k = 0; k < 4; k++) flat(MAT_WHITE, AIR_PLANE_X[k] - 0.4f, zc + 150.0f, AIR_PLANE_X[k] + 0.4f, zc + 225.0f, 0.07f, yel, 0);     // the lead-in lines to the gates
	curChunk = EX_AIR_T;
	// terminal: glass front with a canopy over the gates, jet bridges to each plane
	sbox(MAT_GLASS, 262.0f, 0, zc + 88.0f, 436.0f, 14.0f, zc + 128.0f, glass, 1.0f / 6.8f);
	flat(MAT_ROOF, 262.0f, zc + 88.0f, 436.0f, zc + 128.0f, 14.03f, grey, 1.0f / 8.0f);
	sbox(MAT_WHITE, 258.0f, 9.0f, zc + 128.0f, 440.0f, 9.7f, zc + 140.0f, white, 1);
	for (int k = 0; k < 5; k++) sbox(MAT_WHITE, 258.0f + k * 45.5f, 0, zc + 138.0f, 259.0f + k * 45.5f, 9.0f, zc + 139.0f, grey, 1);
	for (int k = 0; k < 4; k++) sbox(MAT_WHITE, AIR_PLANE_X[k] - 1.1f, 3.2f, zc + 128.0f, AIR_PLANE_X[k] + 1.1f, 5.2f, zc + 148.0f, grey, 1);
	// control tower
	sbox(MAT_CREAM, 440.0f, 0, zc + 92.0f, 452.0f, 26.0f, zc + 104.0f, cream, 1.0f / 6.8f);
	sbox(MAT_GLASS, 437.0f, 26.0f, zc + 89.0f, 455.0f, 31.0f, zc + 107.0f, dark, 1.0f / 6.8f);
	sbox(MAT_WHITE, 436.0f, 31.0f, zc + 88.0f, 456.0f, 32.0f, zc + 108.0f, white, 1);
	sbox(MAT_WHITE, 445.6f, 32.0f, zc + 97.6f, 446.4f, 38.0f, zc + 98.4f, grey, 1);
	curChunk = EX_AIR_W;
	// hangars and the cargo shed
	for (int k = 0; k < 2; k++) {
		float z0 = zc + (k ? 150.0f : 90.0f), z1 = z0 + (k ? 45.0f : 45.0f);
		sbox(MAT_TAN, 52.0f, 0, z0, 132.0f, 12.0f, z1, cream, 1.0f / 6.8f);
		sbox(MAT_WHITE, 55.0f, 12.0f, z0 + 3.0f, 129.0f, 14.0f, z1 - 3.0f, grey, 1);
		sbox(MAT_WHITE, 62.0f, 0, z1, 122.0f, 9.0f, z1 + 0.15f, dark, 1);
	}
	curChunk = EX_AIR_T;
	sbox(MAT_CREAM, 456.0f, 0, zc + 140.0f, 498.0f, 9.0f, zc + 178.0f, cream, 1.0f / 6.8f);
	sbox(MAT_WHITE, 459.0f, 9.0f, zc + 143.0f, 495.0f, 10.0f, zc + 175.0f, grey, 1);
	curChunk = EX_AIR;
	sbox(MAT_WHITE, 188.6f, 0, zc + 62.6f, 189.4f, 8.0f, zc + 63.4f, grey, 1);
	sbox(MAT_WHITE, 187.0f, 8.0f, zc + 61.0f, 191.0f, 8.6f, zc + 65.0f, white, 1);
	curChunk = EX_AIR_T;                                                       // (the parked airliners are detail: drawn only when Otto is near)
	// four airliners at the gates, nose to the terminal
	static const V3 body = { 1.3f, 1.3f, 1.32f }, acc[4] = { { 0.2f, 0.36f, 1.1f }, { 1.4f, 0.25f, 0.2f }, { 0.2f, 1.1f, 0.45f }, { 1.5f, 0.8f, 0.15f } };
	for (int k = 0; k < 4; k++) airliner(&SM(GRP_DETAIL), v3(AIR_PLANE_X[k], 0.05f, AIR_PLANE_Z), 0.0f, 0.0f, 0.0f, 1.0f, body, acc[k], true);
}


// ---- the fence round the playable area (everything but the sea): the city's edges, the airport's edges. -------------------------------------------------
// chunk -1: pick the grid chunk the post stands in
static int air_chunk(float x, float z) { return x < 150.0f && z < CITY + 205.0f ? EX_AIR_W : x >= 150.0f && z < CITY + 220.0f ? EX_AIR_T : EX_AIR; }
static void fence_run(float x0, float z0, float x1, float z1, bool air)
{
	static const float post[3] = { 0.3f, 0.31f, 0.35f }, rail[3] = { 0.75f, 0.77f, 0.8f }, top[3] = { 1.7f, 1.25f, 0.25f };
	bool alongX = fabsf(x1 - x0) > fabsf(z1 - z0);
	float len = alongX ? fabsf(x1 - x0) : fabsf(z1 - z0);
	int n = (int)ceilf(len / 8.0f);
	float sx = (x1 - x0) / n, sz = (z1 - z0) / n;
	for (int i = 0; i <= n; i++) {
		float x = x0 + sx * i, z = z0 + sz * i;
		if (air) curChunk = air_chunk(x + sx * 0.5f, z + sz * 0.5f); else chunk_at(x + sx * 0.5f, z + sz * 0.5f);
		sbox(GRP_DETAIL, x - 0.07f, 0, z - 0.07f, x + 0.07f, 2.6f, z + 0.07f, post, 1);                // the fence is detail: it is only built into the near-only group
		if (i == n) break;
		float xn = x + sx, zn = z + sz;
		float ax = fminf(x, xn), bx = fmaxf(x, xn), az = fminf(z, zn), bz = fmaxf(z, zn);
		if (alongX) { sbox(GRP_DETAIL, ax, 1.0f, z - 0.03f, bx, 1.07f, z + 0.03f, rail, 1); sbox(GRP_DETAIL, ax, 2.4f, z - 0.05f, bx, 2.5f, z + 0.05f, top, 1); }
		else { sbox(GRP_DETAIL, x - 0.03f, 1.0f, az, x + 0.03f, 1.07f, bz, rail, 1); sbox(GRP_DETAIL, x - 0.05f, 2.4f, az, x + 0.05f, 2.5f, bz, top, 1); }
	}
}
// a stretch of fence with an opening between a0 and a1 along it (a0 == a1: none)
static void fence_gap(float x0, float z0, float x1, float z1, float g0, float g1, bool air)
{
	bool alongX = fabsf(x1 - x0) > fabsf(z1 - z0);
	float u0 = alongX ? x0 : z0, u1 = alongX ? x1 : z1, fixedc = alongX ? z0 : x0;
	if (g1 <= g0 || g1 < u0 || g0 > u1) { fence_run(x0, z0, x1, z1, air); return; }
	if (g0 > u0) { if (alongX) fence_run(u0, fixedc, g0, fixedc, air); else fence_run(fixedc, u0, fixedc, g0, air); }
	if (g1 < u1) { if (alongX) fence_run(g1, fixedc, u1, fixedc, air); else fence_run(fixedc, g1, fixedc, u1, air); }
}
static void fences(void)
{
	const World *w = &SG->w;
	const float zs = CITY - 0.6f, xe = CITY - 0.6f, G0 = 220.0f, G1 = 300.0f;
	float gN0 = 0, gN1 = 0, gE0 = 0, gE1 = 0, gS0 = 0, gS1 = 0, gW0 = 0, gW1 = 0;       // the openings of the gate roads on each side
	for (int k = 0; k < w->ngate; k++) { int d = w->gate[k].dir; if (d == 0) { gE0 = G0; gE1 = G1; } else if (d == 1) { gW0 = G0; gW1 = G1; } else if (d == 2) { gS0 = G0; gS1 = G1; } else { gN0 = G0; gN1 = G1; } }
	if (SG->region == REG_CITY) {
		fence_run(BEACH_X, 0.6f, xe, 0.6f, false);                              // north edge, from the water's edge
		fence_gap(xe, 0.6f, xe, zs, gE0, gE1, false);                           // east edge (the road to Sunset Hills goes through)
		fence_gap(BEACH_X, zs, xe, zs, gS0, gS1, false);                        // south edge (the road to the airport)
	} else if (SG->region == REG_AIRPORT) {
		const float xa = AIR_X0 - 20.0f - 0.4f, xb = AIR_X1 + 0.4f, ze = AIR_Z1 + 0.4f;
		fence_gap(0.6f, 0.6f, xe, 0.6f, gN0, gN1, false);                       // north edge, with the road in from the city
		fence_run(0.6f, 0.6f, 0.6f, zs, false); fence_run(xe, 0.6f, xe, zs, false);
		fence_run(0.6f, zs, xa, zs, false); fence_run(xb, zs, xe, zs, false);   // south edge of the district, either side of the airfield
		fence_run(xa, zs, xa, ze, true); fence_run(xb, zs, xb, ze, true); fence_run(xa, ze, xb, ze, true);
	} else {
		fence_run(0.6f, 0.6f, xe, 0.6f, false); fence_run(xe, 0.6f, xe, zs, false); fence_run(0.6f, zs, xe, zs, false);
		fence_gap(0.6f, 0.6f, 0.6f, zs, gW0, gW1, false);                       // west edge, with the road in from the city
	}
	(void)gS0; (void)gS1;
	// the sides of each gate road
	for (int k = 0; k < w->ngate; k++) {
		const Gate *gt = &w->gate[k];
		if (gt->dir < 2) { float xa = gt->dir == 0 ? CITY : gt->x0, xb = gt->dir == 0 ? gt->x1 : 0.0f; fence_run(xa, gt->z0, xb, gt->z0, false); fence_run(xa, gt->z1, xb, gt->z1, false); fence_run(gt->dir == 0 ? gt->x1 : gt->x0, gt->z0, gt->dir == 0 ? gt->x1 : gt->x0, gt->z1, false); }
		else { float za = gt->dir == 2 ? CITY : gt->z0, zb = gt->dir == 2 ? gt->z1 : 0.0f; fence_run(gt->x0, za, gt->x0, zb, false); fence_run(gt->x1, za, gt->x1, zb, false); fence_run(gt->x0, gt->dir == 2 ? gt->z1 : gt->z0, gt->x1, gt->dir == 2 ? gt->z1 : gt->z0, false); }
	}
}

// the road out of a town: an asphalt strip continuing the main street, a gantry sign with the destination's emblem
static void gate_roads(void)
{
	const World *w = &SG->w;
	static const float asph[3] = { 0.36f, 0.36f, 0.4f }, yel[3] = { 1.7f, 1.4f, 0.25f }, board[3] = { 0.12f, 0.5f, 0.28f }, white[3] = { 1.7f, 1.7f, 1.65f }, post[3] = { 0.5f, 0.5f, 0.55f };
	for (int k = 0; k < w->ngate; k++) {
		const Gate *gt = &w->gate[k];
		bool xdir = gt->dir < 2;
		float a0 = xdir ? gt->x0 : gt->z0, a1 = xdir ? gt->x1 : gt->z1;                 // along the road
		float s0 = 256.0f - 11.0f, s1 = 256.0f + 11.0f;                                  // across: the town's main street, 22 m wide
		chunk_at(xdir ? (gt->dir == 0 ? CITY - 1.0f : 1.0f) : s0, xdir ? s0 : (gt->dir == 2 ? CITY - 1.0f : 1.0f));
		curChunk = NCH;
		if (xdir) { flat(MAT_ROOF, a0, s0, a1, s1, 0.04f, asph, 1.0f / 8.0f); flat(MAT_WHITE, a0, 255.85f, a1, 256.15f, 0.07f, yel, 0); }
		else { flat(MAT_ROOF, s0, a0, s1, a1, 0.04f, asph, 1.0f / 8.0f); flat(MAT_WHITE, 255.85f, a0, 256.15f, a1, 0.07f, yel, 0); }
		float pos = gt->dir == 0 ? gt->x1 - 24.0f : gt->dir == 1 ? gt->x0 + 24.0f : gt->dir == 2 ? gt->z1 - 24.0f : gt->z0 + 24.0f;      // the gantry 24 m before the end
		float px0 = pos - 0.4f, px1 = pos + 0.4f;
		if (xdir) {
			sbox(MAT_WHITE, px0, 0, s0 - 1.2f, px1, 7.0f, s0 - 0.4f, post, 1); sbox(MAT_WHITE, px0, 0, s1 + 0.4f, px1, 7.0f, s1 + 1.2f, post, 1);
			sbox(MAT_WHITE, px0, 5.0f, s0 - 1.0f, px1, 7.0f, s1 + 1.0f, board, 1);
		} else {
			sbox(MAT_WHITE, s0 - 1.2f, 0, px0, s0 - 0.4f, 7.0f, px1, post, 1); sbox(MAT_WHITE, s1 + 0.4f, 0, px0, s1 + 1.2f, 7.0f, px1, post, 1);
			sbox(MAT_WHITE, s0 - 1.0f, 5.0f, px0, s1 + 1.0f, 7.0f, px1, board, 1);
		}
		// the emblem on the board (both faces): a plane for the airport, a sun for the hills, a skyline for the city
		float face[2] = { pos - 0.45f, pos + 0.45f };
		for (int f = 0; f < 2; f++) {
			float c = 256.0f, ya = 5.3f;
			#define EMB(u0, v0, u1, v1, col) do { if (xdir) sbox(MAT_WHITE, face[f] - 0.02f, ya + (v0), c + (u0), face[f] + 0.02f, ya + (v1), c + (u1), col, 1); else sbox(MAT_WHITE, c + (u0), ya + (v0), face[f] - 0.02f, c + (u1), ya + (v1), face[f] + 0.02f, col, 1); } while (0)
			if (gt->dest == REG_AIRPORT) { EMB(-0.4f, 0.1f, 0.4f, 1.6f, white); EMB(-3.0f, 0.7f, 3.0f, 1.1f, white); EMB(-1.2f, 0.1f, 1.2f, 0.4f, white); }
			else if (gt->dest == REG_HILLS) { static const float sun[3] = { 2.0f, 1.4f, 0.3f }; EMB(-1.0f, 0.3f, 1.0f, 1.5f, sun); EMB(-2.2f, 0.7f, -1.4f, 1.0f, sun); EMB(1.4f, 0.7f, 2.2f, 1.0f, sun); }
			else { EMB(-3.0f, 0.1f, -2.0f, 0.9f, white); EMB(-1.6f, 0.1f, -0.4f, 1.6f, white); EMB(0.0f, 0.1f, 1.2f, 1.2f, white); EMB(1.6f, 0.1f, 2.6f, 0.7f, white); }
			#undef EMB
		}
	}
}

// ---- Sunset Hills: the desert around it, red mesas on the horizon, cacti and boulders -------------------------------------------------------------------------
static void cactus(float x, float z, float sc, unsigned h)
{
	static const float green[3] = { 0.32f, 0.56f, 0.30f }, dark[3] = { 0.26f, 0.46f, 0.26f };
	float w = 0.3f * sc, H = (2.6f + 1.6f * hf(h)) * sc, ah = H * (0.4f + 0.2f * hf(h >> 6)), bh = H * (0.5f + 0.25f * hf(h >> 9)), a = 0.34f * sc;
	sbox(GRP_DETAIL, x - w, 0, z - w, x + w, H, z + w, green, 1);
	sbox(GRP_DETAIL, x - 1.0f * sc, ah, z - a * 0.5f, x - w, ah + a, z + a * 0.5f, dark, 1);
	sbox(GRP_DETAIL, x - 1.0f * sc, ah, z - a * 0.5f, x - 1.0f * sc + a, ah + 1.1f * sc, z + a * 0.5f, dark, 1);
	sbox(GRP_DETAIL, x + w, bh, z - a * 0.5f, x + 1.0f * sc, bh + a, z + a * 0.5f, dark, 1);
	sbox(GRP_DETAIL, x + 1.0f * sc - a, bh, z - a * 0.5f, x + 1.0f * sc, bh + 0.9f * sc, z + a * 0.5f, dark, 1);
}
static void boulder(float x, float z, float r, float h, unsigned seed)
{
	float t = 0.85f + 0.3f * hf(seed), tint[3] = { 0.66f * t, 0.46f * t, 0.36f * t };
	pyramid(GRP_DETAIL, x - r, z - r * 0.8f, x + r, z + r * 0.8f, 0, h, tint, false);
}
static void mesas(void)
{
	curChunk = NCH;
	unsigned st = 0x3E5A5u;
	static const float rock[3][3] = { { 0.88f, 0.46f, 0.28f }, { 0.78f, 0.38f, 0.24f }, { 0.94f, 0.58f, 0.36f } };
	for (int i = 0; i < 18; i++) {
		float a = i * 2 * PI_F / 18 + rndf(&st) * 0.2f, r = CITY * 0.5f + 360.0f + rndf(&st) * 520.0f, cx = CITY * 0.5f + cosf(a) * r, cz = CITY * 0.5f + sinf(a) * r;
		float w = 90.0f + rndf(&st) * 150.0f, d = 70.0f + rndf(&st) * 120.0f, h = 40.0f + rndf(&st) * 85.0f;
		for (int t = 0; t < 3; t++) {
			float k = t == 0 ? 1.0f : t == 1 ? 0.72f : 0.5f, y0 = t == 0 ? 0 : h * (t == 1 ? 0.45f : 0.8f), y1 = h * (t == 0 ? 0.45f : t == 1 ? 0.8f : 1.0f);
			sbox(MAT_WHITE, cx - w * 0.5f * k, y0, cz - d * 0.5f * k, cx + w * 0.5f * k, y1, cz + d * 0.5f * k, rock[t], 1);
		}
	}
}
// a lattice radio mast: four tapering legs, cross rings, a red light on top
static void mast(const Box *b)
{
	float cx = 0.5f * (b->x0 + b->x1), cz = 0.5f * (b->z0 + b->z1), H = b->y1;
	static const float steel[3] = { 1.25f, 1.25f, 1.3f }, red[3] = { 2.4f, 0.25f, 0.2f };
	for (int i = 0; i < 4; i++) {
		float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
		sbeam(v3(cx + sx * 1.5f, 0, cz + sz * 1.5f), v3(cx + sx * 0.25f, H, cz + sz * 0.25f), 0.12f, 0.12f, 0.06f, 0.06f, steel);
	}
	for (int k = 1; k <= 7; k++) {
		float y = H * k / 8.0f, hw = 1.5f - 1.25f * (y / H);
		sbox(MAT_WHITE, cx - hw, y, cz - hw - 0.04f, cx + hw, y + 0.12f, cz - hw + 0.04f, steel, 1); sbox(MAT_WHITE, cx - hw, y, cz + hw - 0.04f, cx + hw, y + 0.12f, cz + hw + 0.04f, steel, 1);
		sbox(MAT_WHITE, cx - hw - 0.04f, y, cz - hw, cx - hw + 0.04f, y + 0.12f, cz + hw, steel, 1); sbox(MAT_WHITE, cx + hw - 0.04f, y, cz - hw, cx + hw + 0.04f, y + 0.12f, cz + hw, steel, 1);
	}
	sbox(MAT_WHITE, cx - 0.35f, H, cz - 0.35f, cx + 0.35f, H + 1.2f, cz + 0.35f, red, 1);
}

static void landmarks(void)
{
	capitol(130.0f, -52.0f);
	dodger(CITY * 0.82f, -100.0f);
	venice_sign(-5.0f, 3 * PITCH);
	randys_donut();
	helipads();
}

static void scenery_static(void)
{
	outskirts();
	switch (SG->region) {
	case REG_CITY:
		beach(); beach_palms(); hills(); freeway(); landmarks();
		break;
	case REG_AIRPORT:
		theme_building(CITY * 0.38f, CITY + 95.0f); airport(); helipads();
		break;
	default:
		mesas(); helipads();
		break;
	}
	fences();
	gate_roads();
}
