// Web Slinger - citro3d backend: static world buffer + two dynamic buffers, a pass-through vertex shader with distance haze, one texture per material.
#include "gfx3ds.h"
#include <citro2d.h>
#include <string.h>
#include "web_shbin.h"
#include "textures_bin.h"

static DVLB_s *s_dvlb;
static shaderProgram_s s_prog;
static int u_proj, u_mv, u_fogp, u_fogsel;
static C3D_Tex s_tex[MAT_COUNT];
static Vtx *s_stat, *s_dyn[2];
static int s_frame, s_statCount;
static Scene s_scene;
static bool s_ok;

static const float IOD = 0.18f, FOCUS = 9.0f;
#define FOG_START 60.0f
#define FOG_END   900.0f
#define HAZE_RGBA (0xFF000000u | ((u32)(0.80f * 255) << 0) | ((u32)(0.88f * 255) << 8) | ((u32)(0.97f * 255) << 16))     // 0xAABBGGRR

bool gfx_init(const Game *g)
{
	s_dvlb = DVLB_ParseFile((u32 *)web_shbin, web_shbin_size);
	if (!s_dvlb) return false;
	shaderProgramInit(&s_prog);
	shaderProgramSetVsh(&s_prog, &s_dvlb->DVLE[0]);
	u_proj = shaderInstanceGetUniformLocation(s_prog.vertexShader, "projection");
	u_mv = shaderInstanceGetUniformLocation(s_prog.vertexShader, "modelView");
	u_fogp = shaderInstanceGetUniformLocation(s_prog.vertexShader, "fogp");
	u_fogsel = shaderInstanceGetUniformLocation(s_prog.vertexShader, "fogsel");

	// textures: [count][offset,size]*count then the .t3x blobs (packed by assets/pack_textures.py)
	const u32 *hdr = (const u32 *)textures_bin;
	if (hdr[0] != MAT_COUNT) return false;
	for (int i = 0; i < MAT_COUNT; i++) {
		Tex3DS_Texture t = Tex3DS_TextureImport(textures_bin + hdr[1 + i * 2], hdr[2 + i * 2], &s_tex[i], NULL, false);
		if (!t) return false;
		Tex3DS_TextureFree(t);
		bool tiled = i != MAT_WHITE && i != MAT_GLOW && i != MAT_CLOUD && i != MAT_HERO;
		C3D_TexSetFilter(&s_tex[i], GPU_LINEAR, GPU_LINEAR);
		if (s_tex[i].maxLevel > 0) C3D_TexSetFilterMipmap(&s_tex[i], GPU_LINEAR);
		C3D_TexSetWrap(&s_tex[i], tiled ? GPU_REPEAT : GPU_CLAMP_TO_EDGE, tiled ? GPU_REPEAT : GPU_CLAMP_TO_EDGE);
	}

	s_statCount = scene_build_static(g, &s_scene, NULL);
	s_stat = (Vtx *)linearAlloc(sizeof(Vtx) * (size_t)s_statCount);
	for (int i = 0; i < 2; i++) s_dyn[i] = (Vtx *)linearAlloc(sizeof(Vtx) * CAP_DYN);
	if (!s_stat || !s_dyn[0] || !s_dyn[1]) return false;
	scene_build_static(g, &s_scene, s_stat);
	GSPGPU_FlushDataCache(s_stat, sizeof(Vtx) * (size_t)s_statCount);
	s_ok = true;
	return true;
}

void gfx_exit(void)
{
	if (!s_ok) return;
	for (int i = 0; i < MAT_COUNT; i++) C3D_TexDelete(&s_tex[i]);
	linearFree(s_stat); linearFree(s_dyn[0]); linearFree(s_dyn[1]);
	shaderProgramFree(&s_prog);
	DVLB_Free(s_dvlb);
	s_ok = false;
}

Scene *gfx_begin(const Game *g)
{
	s_frame ^= 1;
	s_scene.dyn = s_dyn[s_frame];
	scene_build(g, &s_scene);
	for (int i = 0; i < D_COUNT; i++)
		if (s_scene.count[i] > 0) GSPGPU_FlushDataCache(s_scene.dyn + s_scene.first[i], (u32)(s_scene.count[i] * sizeof(Vtx)));
	return &s_scene;
}

static void set_mv(const float m[16])
{
	for (int i = 0; i < 4; i++) C3D_FVUnifSet(GPU_VERTEX_SHADER, u_mv + i, m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]);
}
static void use_buf(const Vtx *p)
{
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, p, sizeof(Vtx), 3, 0x210);
}
// Haze: the vertex shader writes the haze amount into the colour alpha; texture environment stage 1 then blends the haze colour over the textured, lit result.
static void fog(bool on)
{
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_fogsel, on ? 1.0f : 0.0f, 0, 0, 0);
	C3D_TexEnv *e1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(e1);
	if (on) {
		C3D_TexEnvSrc(e1, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
		C3D_TexEnvOpRgb(e1, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
		C3D_TexEnvFunc(e1, C3D_RGB, GPU_INTERPOLATE);
		C3D_TexEnvColor(e1, HAZE_RGBA);
	}
}
static void blend_opaque(void) { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO); }
static void blend_alpha(void)  { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA); }
static void blend_add(void)    { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE); }
static void draw_group(const Scene *s, bool dynamic, int idx, int mat)
{
	int first = dynamic ? s->first[idx] : s->sfirst[idx], count = dynamic ? s->count[idx] : s->scount[idx];
	if (count <= 0) return;
	C3D_TexBind(0, &s_tex[mat]);
	C3D_DrawArrays(GPU_TRIANGLES, first, count);
}

static void draw_chunks(const Scene *s, int grp, int mat)                        // one draw per visible chunk
{
	bool bound = false;
	for (int c = 0; c <= NCH; c++) {
		int count = s->ccount[grp][c];
		if (count <= 0 || !s->vis[c]) continue;
		if (!bound) { C3D_TexBind(0, &s_tex[mat]); bound = true; }
		C3D_DrawArrays(GPU_TRIANGLES, s->cfirst[grp][c], count);
	}
}

static void draw_chunks_near(const Scene *s, int grp, int mat)
{
	bool bound = false;
	for (int c = 0; c < NCH; c++) {
		int count = s->ccount[grp][c];
		if (count <= 0 || !s->near[c]) continue;
		if (!bound) { C3D_TexBind(0, &s_tex[mat]); bound = true; }
		C3D_DrawArrays(GPU_TRIANGLES, s->cfirst[grp][c], count);
	}
}

void gfx_draw(const Scene *s, float eye, float slider)
{
	C3D_BindProgram(&s_prog);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);               // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);               // v1 = texture coordinate
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4);               // v2 = colour
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_fogp, -1.0f / (FOG_END - FOG_START), -FOG_START / (FOG_END - FOG_START), 0, 0);
	fog(false);

	C3D_Mtx proj;
	Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(s->fov), C3D_AspectRatioTop, 0.25f, 1500.0f, slider * IOD * eye, FOCUS, false);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_DepthMap(true, -1.0f, 0.0f);

	// 1. sky: no depth, drawn first (rotation-only view matrix keeps it infinitely far away)
	use_buf(s->stat);
	set_mv(s->skyview);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	blend_opaque(); draw_group(s, false, GRP_SKY, MAT_WHITE);
	blend_alpha();  draw_group(s, false, GRP_CLOUD, MAT_CLOUD);
	blend_add();    draw_group(s, false, GRP_SUN, MAT_GLOW);

	// 2. static world, one draw per texture, with haze
	set_mv(s->view);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	blend_opaque();
	fog(true);
	for (int m = 0; m < MAT_COUNT; m++) if (m != MAT_GLOW && m != MAT_CLOUD && m != MAT_HERO) draw_chunks(s, m, m);
	draw_chunks_near(s, GRP_DETAIL, MAT_WHITE);

	// 3. dynamic opaque (hero, web, cars), then the street shadows and soft blobs (blended), then additive glows
	use_buf(s->dyn);
	draw_group(s, true, D_OPAQUE, MAT_WHITE);
	draw_group(s, true, D_HERO, MAT_HERO);
	fog(false);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
	blend_alpha(); draw_group(s, true, D_ALPHA, MAT_GLOW);
	blend_add();   draw_group(s, true, D_ADD, MAT_GLOW);
	use_buf(s->stat);
	blend_alpha(); draw_chunks(s, GRP_SHADOW, MAT_WHITE);
}

void gfx_end_3d(void)
{
	// give citro2d its state back: its own program and buffers, no depth test (the HUD is drawn flat over the scene)
	C2D_Prepare();
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
}
