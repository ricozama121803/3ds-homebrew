// Grand Theft Otto - citro3d backend: one vertex buffer per frame (built on the CPU by r3d.c), a pass-through shader, two textures.
#include "r3d_3ds.h"
#include <citro2d.h>
#include <string.h>
#include "r3d_shbin.h"
#include "tex_world_t3x.h"
#include "tex_sprites_t3x.h"

static DVLB_s *s_dvlb;
static shaderProgram_s s_prog;
static int u_proj, u_mv;
static C3D_Tex s_tex[2];
static Vtx *s_vbuf[2];
static int s_frame;
static R3DScene s_scene;
static bool s_ok;

static bool load_tex(C3D_Tex *tex, const void *data, size_t size)
{
	Tex3DS_Texture t = Tex3DS_TextureImport(data, size, tex, NULL, false);
	if (!t) return false;
	Tex3DS_TextureFree(t);
	C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	return true;
}

bool r3d3ds_init(void)
{
	s_dvlb = DVLB_ParseFile((u32 *)r3d_shbin, r3d_shbin_size);
	if (!s_dvlb) return false;
	shaderProgramInit(&s_prog);
	shaderProgramSetVsh(&s_prog, &s_dvlb->DVLE[0]);
	u_proj = shaderInstanceGetUniformLocation(s_prog.vertexShader, "projection");
	u_mv = shaderInstanceGetUniformLocation(s_prog.vertexShader, "modelView");
	if (!load_tex(&s_tex[0], tex_world_t3x, tex_world_t3x_size) || !load_tex(&s_tex[1], tex_sprites_t3x, tex_sprites_t3x_size)) return false;
	for (int i = 0; i < 2; i++) {
		s_vbuf[i] = (Vtx *)linearAlloc(sizeof(Vtx) * CAP_TOTAL);
		if (!s_vbuf[i]) return false;
	}
	s_ok = true;
	return true;
}

void r3d3ds_exit(void)
{
	if (!s_ok) return;
	for (int i = 0; i < 2; i++) { C3D_TexDelete(&s_tex[i]); linearFree(s_vbuf[i]); }
	shaderProgramFree(&s_prog);
	DVLB_Free(s_dvlb);
	s_ok = false;
}

R3DScene *r3d3ds_begin(const Game *g)
{
	s_frame ^= 1;
	s_scene.buf = s_vbuf[s_frame];
	r3d_build(g, &s_scene);
	for (int i = 0; i < G_COUNT; i++)
		if (s_scene.count[i] > 0) GSPGPU_FlushDataCache(s_scene.buf + s_scene.first[i], (u32)(s_scene.count[i] * sizeof(Vtx)));
	return &s_scene;
}

void r3d3ds_draw(const R3DScene *s, float eye, float slider)
{
	C3D_BindProgram(&s_prog);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);               // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);               // v1 = texture coordinate
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4);               // v2 = colour
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, s->buf, sizeof(Vtx), 3, 0x210);

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_CullFace(GPU_CULL_NONE);

	C3D_Mtx proj;
	Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(s->fov_deg), C3D_AspectRatioTop, 8.0f, 1400.0f, slider * 6.0f * eye, s->focus, false);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	for (int i = 0; i < 4; i++) C3D_FVUnifSet(GPU_VERTEX_SHADER, u_mv + i, s->view[i * 4], s->view[i * 4 + 1], s->view[i * 4 + 2], s->view[i * 4 + 3]);

	// 1. opaque world (alpha-tested: the Hollywood sign letters are cut out)
	C3D_TexBind(0, &s_tex[0]);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C3D_AlphaTest(true, GPU_GREATER, 127);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	if (s->count[G_OPAQUE]) C3D_DrawArrays(GPU_TRIANGLES, s->first[G_OPAQUE], s->count[G_OPAQUE]);

	// 2. blended world geometry (rotor blur), then 3. blended sprites (shadows, smoke, fire, pickups): they test depth but don't write it
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
	if (s->count[G_ALPHA]) C3D_DrawArrays(GPU_TRIANGLES, s->first[G_ALPHA], s->count[G_ALPHA]);
	C3D_TexBind(0, &s_tex[1]);
	if (s->count[G_SPRITE]) C3D_DrawArrays(GPU_TRIANGLES, s->first[G_SPRITE], s->count[G_SPRITE]);
}

void r3d3ds_end_3d(void)
{
	// give citro2d its state back: its own program and buffers, no depth test (the HUD is drawn flat over the scene)
	C2D_Prepare();
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
}
