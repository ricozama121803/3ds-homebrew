// citro3d backend: one vertex buffer + one index buffer for every mesh, a per-vertex lighting shader
// (shader.v.pica), and citro2d for the HUD overlay.
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "shader_shbin.h"

static DVLB_s *s_dvlb;
static shaderProgram_s s_prog;
static int u_proj, u_mv, u_L0, u_L1, u_C0, u_C1, u_gnd, u_hemi, u_up, u_ppos, u_pcol, u_pinv, u_base, u_parm, u_outl;

static void *s_vbo, *s_ibo;
static Mat4 s_view;
static int s_blend;                       // currently drawing blended (shadows)?

static C2D_TextBuf s_tbuf;

void gfx_init(void)
{
	s_dvlb = DVLB_ParseFile((u32 *)shader_shbin, shader_shbin_size);
	shaderProgramInit(&s_prog);
	shaderProgramSetVsh(&s_prog, &s_dvlb->DVLE[0]);
#define U(var, name) var = shaderInstanceGetUniformLocation(s_prog.vertexShader, name)
	U(u_proj, "projection"); U(u_mv, "modelView");
	U(u_L0, "L0"); U(u_L1, "L1"); U(u_C0, "C0"); U(u_C1, "C1");
	U(u_gnd, "gndC"); U(u_hemi, "hemiD"); U(u_up, "upV");
	U(u_ppos, "pPos"); U(u_pcol, "pCol"); U(u_pinv, "pInv");
	U(u_base, "matBase"); U(u_parm, "matParm"); U(u_outl, "outl");
#undef U
	s_tbuf = C2D_TextBufNew(4096);
}

void gfx_exit(void)
{
	C2D_TextBufDelete(s_tbuf);
	if (s_vbo) linearFree(s_vbo);
	if (s_ibo) linearFree(s_ibo);
	shaderProgramFree(&s_prog);
	DVLB_Free(s_dvlb);
}

void gfx_upload_meshes(void)
{
	size_t vb = (size_t)mesh_bank_vertex_count() * sizeof(Vertex);
	size_t ib = (size_t)mesh_bank_index_count() * sizeof(uint16_t);
	s_vbo = linearAlloc(vb);
	s_ibo = linearAlloc(ib);
	memcpy(s_vbo, mesh_bank_verts(), vb);
	memcpy(s_ibo, mesh_bank_indices(), ib);
	GSPGPU_FlushDataCache(s_vbo, vb);
	GSPGPU_FlushDataCache(s_ibo, ib);
}

static void set_vec(int loc, Vec3 v, float w) { C3D_FVUnifSet(GPU_VERTEX_SHADER, loc, v.x, v.y, v.z, w); }

static void set_blend(int on)
{
	if (on == s_blend) return;
	s_blend = on;
	if (on) {
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);            // shadows test depth but do not write it
	} else {
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	}
}

void gfx_scene_begin(const Camera *cam, const Lights *lt, int eye)
{
	// citro2d (the HUD) leaves its own program and buffers bound, so rebind everything each time
	C3D_BindProgram(&s_prog);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);          // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3);          // v1 = normal
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 3);          // v2 = painted tint
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, s_vbo, sizeof(Vertex), 3, 0x210);

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	s_blend = -1;
	set_blend(0);
	gfx_outline(0);

	s_view = mat4_lookat(cam->eye, cam->target, v3(0, 1, 0));
	float focal = v3dist(cam->eye, cam->target);
	C3D_Mtx proj;
	Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(cam->fov_deg), C3D_AspectRatioTop, 0.1f, 60.0f, cam->iod * (float)eye, focal, false);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);

	// lights go to the shader in view space
	set_vec(u_L0, v3norm(mat4_dir(&s_view, lt->dir[0])), 0);
	set_vec(u_L1, v3norm(mat4_dir(&s_view, lt->dir[1])), 0);
	set_vec(u_C0, lt->col[0], 0); set_vec(u_C1, lt->col[1], 0);
	set_vec(u_gnd, lt->ground, 0);
	set_vec(u_hemi, v3sub(lt->sky, lt->ground), 0);
	set_vec(u_up, v3norm(mat4_dir(&s_view, v3(0, 1, 0))), 0);
	set_vec(u_ppos, mat4_point(&s_view, lt->ppos), 0);
	set_vec(u_pcol, lt->pcol, 0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_pinv, lt->pinv, 0, 0, 0);
}

void gfx_draw(const Mesh *m, const Mat4 *model, const Material *mat)
{
	Mat4 mv = mat4_mul(&s_view, model);
	for (int i = 0; i < 4; i++)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, u_mv + i, mv.m[i * 4], mv.m[i * 4 + 1], mv.m[i * 4 + 2], mv.m[i * 4 + 3]);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_base, mat->r, mat->g, mat->b, mat->alpha);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_parm, mat->spec, mat->wrap, mat->rim, 0);
	set_blend(mat->alpha < 1.0f);
	C3D_DrawElements(GPU_TRIANGLES, m->nidx, C3D_UNSIGNED_SHORT, (const u16 *)s_ibo + m->ioff);
}

void gfx_outline(float width)
{
	C3D_CullFace(width > 0 ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_outl, width, 0, 0, 0);
}

void gfx_scene_end(void) { set_blend(0); gfx_outline(0); }

// ---- 2D overlay ---------------------------------------------------------------------------
void gfx2d_frame_begin(void) { C2D_TextBufClear(s_tbuf); }

static u32 col(unsigned rgba) { return C2D_Color32((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255); }

void gfx2d_rect(float x, float y, float w, float h, unsigned rgba)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, h, col(rgba));
}

void gfx2d_text(float x, float y, float scale, unsigned rgba, int align, const char *s)
{
	C2D_Text t;
	C2D_TextParse(&t, s_tbuf, s);
	C2D_TextOptimize(&t);
	float w, h;
	C2D_TextGetDimensions(&t, scale, scale, &w, &h);
	if (align == GFX_ALIGN_CENTER) x -= w * 0.5f;
	else if (align == GFX_ALIGN_RIGHT) x -= w;
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, col(rgba));
}
