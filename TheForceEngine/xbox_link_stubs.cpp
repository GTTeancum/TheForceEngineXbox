//////////////////////////////////////////////////////////////////////
// xbox_link_stubs.cpp
// No-op stubs for symbols referenced by included game code but whose
// implementations live in modules excluded from the Xbox build
// (GPU renderer, mod/external data, scripting, registry, etc.).
//
// Compiled only when _XBOX is defined.
//////////////////////////////////////////////////////////////////////
#ifdef _XBOX

#include <TFE_System/types.h>
#include <TFE_System/math.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Renderer/textureInfo.h>
#include <TFE_Jedi/Renderer/rsectorRender.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rclassicGPU.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rsectorGPU.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_RenderShared/quadDraw2d.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/robjData.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#include <TFE_ForceScript/forceScript.h>
#include <TFE_DarkForces/Landru/lsystem.h>
#include <TFE_Settings/windows/registry.h>

#include <math.h>
#include <string.h>

// =====================================================================
// TFE_ExternalData is now compiled in for Xbox (see build_xbox.bat).
// JSON files must be deployed under D:\ExternalData\DarkForces\.
// =====================================================================

// =====================================================================
// GPU renderer stubs (screenDrawGPU.cpp and related files excluded)
// NOTE: screenDraw.cpp IS in the Xbox build and already provides:
//   screen_clear, screen_enableGPU, screenDraw_begin/endLines,
//   screenDraw_begin/endQuads, screenDraw_setTransColor,
//   screen_drawPoint, screen_drawLine, screen_drawCircle,
//   screen_clipLineToRect, and all blitTextureToScreen* overloads.
// NOTE: jediRenderer.cpp IS in the Xbox build and already provides:
//   blitTextureToScreen(TextureData*,s32,s32), clear3DView,
//   renderer_addHudTextureCallback.
// Only screenGPU_* functions and RClassic_GPU stubs are needed here.
// =====================================================================
namespace TFE_Jedi
{
	// screenDraw.cpp provides real software-blit implementations of all
	// the functions below. Earlier the duplicate stubs here were winning
	// at link time and blit calls silently did nothing - that's exactly
	// why the HUD/weapon were invisible in Phase 9. Removed; the real
	// software path now runs. screen_enableGPU is patched in
	// screenDraw.cpp itself to keep s_gpuEnabled=false on Xbox so the
	// software branches are taken even though the renderer is set to
	// RENDERER_HARDWARE.

	// =====================================================================
	// screenDrawGPU.h - 2D screen-space draw port.
	//
	// Upstream RClassic_GPU/screenDrawGPU.cpp uses a quad-batching system
	// with shaders (gpu_render_quad.vert/frag) and a texture-packer atlas
	// to draw the escape menu, agent menu, PDA, and any other UI element
	// when RENDERER_HARDWARE is selected.
	//
	// The Xbox port replaces the shader+atlas pipeline with immediate
	// D3D8 fixed-function draws via TFE_RenderBackend::gpuDrawScreenQuad
	// (renderBackend_xbox.cpp). Each TextureData* (DELT/BM frame) is
	// uploaded once to a P8 D3D8 texture via gpuGetOrUploadIndexedTexture
	// (keyed by the TextureData pointer, so re-blits hit the cache), then
	// drawn as a textured screen-space quad with alpha-test for
	// palette-index-0 transparency.
	//
	// The render-target capture (escapeMenu_copyBackground GPU branch)
	// goes through the real createRenderTarget / copyBackbufferToRenderTarget
	// implementations in renderBackend_xbox.cpp; getRenderTargetTexture
	// returns the same handle reinterpreted as TextureGpu*, which we pass
	// straight back to gpuDrawScreenQuad, which detects the sentinel and
	// samples it as a plain A8R8G8B8 texture.
	// =====================================================================
	// Tracks the virtual-display size set by the most recent
	// screenGPU_beginQuads / screenGPU_beginImageQuads call. The screenGPU_
	// blit* functions inherit it (upstream's shader does the same via the
	// ScaleOffset uniform). Default to 320x200 in case a blit fires before
	// any explicit begin (defensive; upstream always pairs them).
	static u32 s_scrGpuW = 320;
	static u32 s_scrGpuH = 200;

	void screenGPU_init()    { TFE_RenderShared::quadInit(); }
	void screenGPU_destroy() { TFE_RenderShared::quadDestroy(); }

	void screenGPU_beginLines(u32, u32)      {}
	void screenGPU_endLines()                {}

	// Image quads route through quadDraw2d (upstream RClassic_GPU/
	// screenDrawGPU.cpp:268-276 does the same). The captured render
	// target for the pause menu background goes through this path.
	void screenGPU_beginImageQuads(u32 w, u32 h)
	{
		s_scrGpuW = w ? w : 320;
		s_scrGpuH = h ? h : 200;
		TFE_RenderShared::quadDraw2d_begin(s_scrGpuW, s_scrGpuH);
	}
	void screenGPU_endImageQuads()
	{
		TFE_RenderShared::quadDraw2d_draw();
	}

	// ----- Sprite-quad batch (upstream screenGPU_beginQuads / blit* /
	// endQuads, lines 185-256 + 416-450 of screenDrawGPU.cpp).
	//
	// Upstream stores per-vertex data in s_scrQuads[SCR_MAX_QUAD_COUNT*4],
	// packs textureId / color / lightLevel into a 32-bit per-vertex
	// attribute, and flushes everything in one DrawIndexedTriangles in
	// endQuads after a single shader bind. On Xbox there's no atlas (each
	// TextureData* uploads to its own D3D8 texture via the existing P8
	// cache) so we hold the resolved GpuTextureHandle per quad and flush
	// per-quad in endQuads.
	//
	// lightLevel is dropped on the Xbox path - the upstream shader uses it
	// as a colormap row index for DF-style lit blitting; the only call
	// site that passes anything other than 31 (fullbright) is the weapon
	// flash effect which doesn't go through this code path.
	enum { XBOX_SCR_QUAD_MAX = 1024 };
	struct XboxScrSpriteQuad
	{
		f32 x0, y0, x1, y1;
		f32 u0, v0, u1, v1;
		TFE_RenderBackend::GpuTextureHandle tex;
	};
	static XboxScrSpriteQuad s_scrSpriteQuads[XBOX_SCR_QUAD_MAX];
	static u32 s_scrSpriteCount = 0;

	void screenGPU_beginQuads(u32 w, u32 h)
	{
		s_scrGpuW = w ? w : 320;
		s_scrGpuH = h ? h : 200;
		s_scrSpriteCount = 0;
	}
	void screenGPU_endQuads()
	{
		// Single flush at end of frame (mirrors upstream endQuads at
		// lines 202-256, which uploads the batch then issues one
		// drawIndexedTriangles per draw group).
		static u32 s_lastReportedCount = 0xFFFFFFFFu;
		if (s_scrSpriteCount != s_lastReportedCount)
		{
			TFE_System::logWrite(LOG_MSG, "GPU", "screenGPU_endQuads flushing %u sprite quads (vdisp %ux%u)",
				s_scrSpriteCount, s_scrGpuW, s_scrGpuH);
			s_lastReportedCount = s_scrSpriteCount;
		}
		for (u32 i = 0; i < s_scrSpriteCount; i++)
		{
			const XboxScrSpriteQuad& q = s_scrSpriteQuads[i];
			TFE_RenderBackend::gpuDrawScreenQuad(
				q.x0, q.y0, q.x1, q.y1,
				q.u0, q.v0, q.u1, q.v1,
				s_scrGpuW, s_scrGpuH, q.tex, /*alphaTest*/true);
		}
		s_scrSpriteCount = 0;
	}

	void screenGPU_setIndexedColors(u32, const Vec4f*) {}
	void screenGPU_drawColoredQuad(fixed16_16, fixed16_16, fixed16_16, fixed16_16, u8) {}

	void screenGPU_setHudTextureCallbacks(s32, TextureListCallback*, bool) {}

	void screenGPU_drawPoint(ScreenRect*, s32, s32, u8) {}
	void screenGPU_drawLine(ScreenRect*, s32, s32, s32, s32, u8) {}

	// Compute UV crop ratio for a non-pow2 source uploaded into a pow2
	// D3D texture by gpuGetOrUploadIndexedTexture's pad path.
	static inline void scrGpu_uvMax(s32 srcW, s32 srcH, f32* uMax, f32* vMax)
	{
		(void)srcW; (void)srcH;
		// gpuGetOrUploadIndexedTexture scales non-pow2 sources so the
		// entire DELT/BM fills the pow2 texture. Sampling src/pow2 here
		// zooms into the top-left of menu art and stretches it.
		*uMax = 1.0f;
		*vMax = 1.0f;
	}

	static inline void scrGpu_queueBlit(TFE_RenderBackend::GpuTextureHandle tex,
	                                     f32 x0, f32 y0, f32 x1, f32 y1,
	                                     f32 uMax, f32 vMax)
	{
		if (s_scrSpriteCount >= XBOX_SCR_QUAD_MAX) return;
		XboxScrSpriteQuad& q = s_scrSpriteQuads[s_scrSpriteCount++];
		q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
		q.u0 = 0.0f; q.v0 = 0.0f; q.u1 = uMax; q.v1 = vMax;
		q.tex = tex;
	}

	// TextureData* version (DELT / BM frames). DELT pixel layout is
	// ROW-major after delt_loadDeltIntoFrame; pass columnMajor=false.
	// Matches upstream screenDrawGPU.cpp:327-355: queue-only, no draw.
	void screenGPU_blitTexture(TextureData* texture, DrawRect* /*rect*/, s32 x0, s32 y0,
	                           JBool /*forceTransparency*/, JBool /*forceOpaque*/)
	{
		if (!texture || !texture->image || texture->width <= 0 || texture->height <= 0) return;
		TFE_RenderBackend::GpuTextureHandle gpuTex =
			TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
				texture, texture->image, texture->width, texture->height, /*columnMajor*/false);
		if (!gpuTex) return;
		f32 uMax, vMax; scrGpu_uvMax(texture->width, texture->height, &uMax, &vMax);
		scrGpu_queueBlit(gpuTex,
			(f32)x0, (f32)y0,
			(f32)(x0 + texture->width), (f32)(y0 + texture->height),
			uMax, vMax);
	}

	void screenGPU_blitTextureLit(TextureData* texture, DrawRect* rect, s32 x0, s32 y0,
	                              u8 /*lightLevel*/, JBool ft)
	{
		// Xbox path doesn't have the lit colourmap shader; same as unlit.
		screenGPU_blitTexture(texture, rect, x0, y0, ft, JFALSE);
	}

	// Matches upstream screenDrawGPU.cpp:416-450.
	void screenGPU_blitTextureScaled(TextureData* texture, DrawRect* /*rect*/,
	                                  fixed16_16 x0, fixed16_16 y0,
	                                  fixed16_16 xScale, fixed16_16 yScale,
	                                  u8 /*lightLevel*/, JBool /*forceTransparency*/)
	{
		if (!texture || !texture->image || texture->width <= 0 || texture->height <= 0) return;
		TFE_RenderBackend::GpuTextureHandle gpuTex =
			TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
				texture, texture->image, texture->width, texture->height, /*columnMajor*/false);
		if (!gpuTex) return;
		f32 uMax, vMax; scrGpu_uvMax(texture->width, texture->height, &uMax, &vMax);
		const f32 fx0 = fixed16ToFloat(x0);
		const f32 fy0 = fixed16ToFloat(y0);
		const f32 fx1 = fx0 + (f32)texture->width  * fixed16ToFloat(xScale);
		const f32 fy1 = fy0 + (f32)texture->height * fixed16ToFloat(yScale);
		scrGpu_queueBlit(gpuTex, fx0, fy0, fx1, fy1, uMax, vMax);
	}

	// addImageQuad - verbatim ports of upstream screenDrawGPU.cpp:278-289.
	// quadDraw2d batches the requests; flush happens in
	// screenGPU_endImageQuads above.
	void screenGPU_addImageQuad(s32 x0, s32 z0, s32 x1, s32 z1, TextureGpu* texture)
	{
		u32 colors[] = { 0xffffffff, 0xffffffff };
		Vec2f vtx[]  = { { (f32)x0, (f32)z0 }, { (f32)x1, (f32)z1 } };
		TFE_RenderShared::quadDraw2d_add(vtx, colors, texture);
	}
	void screenGPU_addImageQuad(s32 x0, s32 z0, s32 x1, s32 z1,
	                            f32 u0, f32 u1, TextureGpu* texture)
	{
		u32 colors[] = { 0xffffffff, 0xffffffff };
		Vec2f vtx[]  = { { (f32)x0, (f32)z0 }, { (f32)x1, (f32)z1 } };
		TFE_RenderShared::quadDraw2d_add(vtx, colors, u0, u1, texture);
	}

	// ScreenImage* versions - unused by escape menu / agent menu / PDA.
	// Stubbed for now; revisit if anything triggers them.
	void screenGPU_blitTexture(ScreenImage*, DrawRect*, s32, s32) {}
	void screenGPU_blitTextureScaled(ScreenImage*, DrawRect*, s32, s32, fixed16_16, fixed16_16) {}
	void screenGPU_blitTextureLitScaled(ScreenImage*, DrawRect*, s32, s32, fixed16_16, fixed16_16, u8) {}
	void screenGPU_blitTextureIScale(TextureData*, DrawRect*, s32, s32, s32) {}

	// =====================================================================
	// Phase 2 of the RClassic_GPU/D3D8 port.
	//
	// Camera globals are populated by RClassic_GPU::computeCameraTransform
	// once per frame (called from jediRenderer at the end of the camera
	// update). The Xbox renderer keeps its own D3D-LH view + projection
	// matrices alongside, derived from the same yaw/pitch/position. We do
	// NOT reuse RClassic_GPU's s_cameraProj because that matrix is built
	// for OpenGL conventions (clip-z in [-1,1], w_clip = -view.z); D3D8
	// needs clip-z in [0,1] and w_clip = +view.z, which is cleaner to
	// build fresh from focal length + viewport.
	// =====================================================================
	Vec3f s_cameraPos   = { 0.0f, 0.0f, 0.0f };
	Vec3f s_cameraDir   = { 0.0f, 0.0f, 0.0f };
	Vec3f s_cameraDirXZ = { 0.0f, 0.0f, 0.0f };

	// D3D-ready matrices, row-major (D3DMATRIX layout). Updated each frame.
	static f32 s_xboxViewMtx[16];
	static f32 s_xboxProjMtx[16];
	static s32 s_xboxViewW = 320;
	static s32 s_xboxViewH = 200;
	static f32 s_xboxLastYaw = 0.0f, s_xboxLastPitch = 0.0f;

	// =====================================================================
	// Phase 12.A - Frustum helpers, ported 1:1 from upstream
	// TFE_Jedi/Renderer/RClassic_GPU/frustum.cpp (TheForceEngine-ORIGINAL).
	//
	// Used for CPU-side Sutherland-Hodgman clipping. NV2A's D3D8 FF pipeline
	// has no user clip planes (the Xbox D3D8 only exposes depth clipping),
	// so we do the equivalent of upstream's gl_ClipDistance on the CPU.
	//
	// Nothing in this block is called yet - this commit just lands the
	// helpers so 12.B/12.C can wire them up incrementally.
	// =====================================================================
	enum { XBOX_FRUSTUM_STACK_SIZE = 64, XBOX_FRUSTUM_PLANE_MAX = 32 };
	static const f32 c_xboxPlaneEps = 0.0001f;

	struct XboxFrustum
	{
		u32   planeCount;
		Vec4f planes[XBOX_FRUSTUM_PLANE_MAX];
	};
	struct XboxPolygon
	{
		s32   vertexCount;
		Vec3f vtx[XBOX_FRUSTUM_PLANE_MAX];
	};

	static XboxFrustum s_xboxFrustumStack[XBOX_FRUSTUM_STACK_SIZE];
	static u32         s_xboxFrustumStackPtr = 0;

	static inline f32 xboxFrustum_planeDist(const Vec4f* plane, const Vec3f* pos)
	{
		return plane->x*pos->x + plane->y*pos->y + plane->z*pos->z + plane->w;
	}

	static inline void xboxFrustum_copy(const XboxFrustum* src, XboxFrustum* dst)
	{
		dst->planeCount = src->planeCount;
		for (u32 i = 0; i < src->planeCount; i++) dst->planes[i] = src->planes[i];
	}

	static inline void xboxFrustum_clearStack() { s_xboxFrustumStackPtr = 0; }

	static inline void xboxFrustum_push(const XboxFrustum* f)
	{
		if (s_xboxFrustumStackPtr >= XBOX_FRUSTUM_STACK_SIZE) return;
		xboxFrustum_copy(f, &s_xboxFrustumStack[s_xboxFrustumStackPtr++]);
	}

	static inline void xboxFrustum_pop()
	{
		if (s_xboxFrustumStackPtr > 0) s_xboxFrustumStackPtr--;
	}

	static inline XboxFrustum* xboxFrustum_getBack()
	{
		return (s_xboxFrustumStackPtr > 0) ? &s_xboxFrustumStack[s_xboxFrustumStackPtr - 1] : NULL;
	}

	// Build side planes through camera + polygon edges, plus a near plane
	// from the polygon's own plane. Matches upstream frustum_buildFromPolygon.
	static void xboxFrustum_buildFromPolygon(const XboxPolygon* polygon, XboxFrustum* out)
	{
		const s32 count = polygon->vertexCount;
		out->planeCount = 0;

		// Sides: each edge becomes a plane through the camera.
		for (s32 i = 0; i < count && out->planeCount < XBOX_FRUSTUM_PLANE_MAX - 1; i++)
		{
			const s32 e0 = i, e1 = (i + 1) % count;
			Vec3f S = { polygon->vtx[e0].x - s_cameraPos.x,
			            polygon->vtx[e0].y - s_cameraPos.y,
			            polygon->vtx[e0].z - s_cameraPos.z };
			Vec3f T = { polygon->vtx[e1].x - s_cameraPos.x,
			            polygon->vtx[e1].y - s_cameraPos.y,
			            polygon->vtx[e1].z - s_cameraPos.z };
			Vec3f N = TFE_Math::cross(&S, &T);
			if (TFE_Math::dot(&N, &N) <= 0.0f) continue;
			N = TFE_Math::normalize(&N);
			const f32 d = -TFE_Math::dot(&N, &s_cameraPos);
			Vec4f p = { N.x, N.y, N.z, d };
			out->planes[out->planeCount++] = p;
		}

		// Near plane: the polygon's own plane.
		if (count >= 3 && out->planeCount < XBOX_FRUSTUM_PLANE_MAX)
		{
			const Vec3f& O = polygon->vtx[0];
			Vec3f S = { polygon->vtx[1].x - O.x, polygon->vtx[1].y - O.y, polygon->vtx[1].z - O.z };
			Vec3f T = { polygon->vtx[2].x - O.x, polygon->vtx[2].y - O.y, polygon->vtx[2].z - O.z };
			Vec3f N = TFE_Math::cross(&S, &T);
			if (TFE_Math::dot(&N, &N) > 0.0f)
			{
				N = TFE_Math::normalize(&N);
				const f32 d = -TFE_Math::dot(&N, &O);
				Vec4f p = { N.x, N.y, N.z, d };
				out->planes[out->planeCount++] = p;
			}
		}
	}

	// Generalised Sutherland-Hodgman: clip an arbitrary input polygon
	// against a list of planes. Keeps verts with planeDist >= -eps.
	// Returns true if the clipped polygon has >= 3 vertices.
	static bool xboxFrustum_clipPolyToPlanes(const Vec3f* inVtx, s32 inN,
	                                          const Vec4f* planes, s32 planeCount,
	                                          XboxPolygon* out)
	{
		if (inN < 3 || inN > XBOX_FRUSTUM_PLANE_MAX) return false;

		XboxPolygon scratch[2];
		XboxPolygon* cur  = &scratch[0];
		XboxPolygon* next = &scratch[1];

		cur->vertexCount = inN;
		for (s32 i = 0; i < inN; i++) cur->vtx[i] = inVtx[i];

		f32 vtxDist[XBOX_FRUSTUM_PLANE_MAX];
		const Vec4f* plane = planes;
		for (s32 p = 0; p < planeCount; p++, plane++)
		{
			s32 positive = 0, negative = 0;
			next->vertexCount = 0;

			for (s32 v = 0; v < cur->vertexCount; v++)
			{
				vtxDist[v] = xboxFrustum_planeDist(plane, &cur->vtx[v]);
				if (vtxDist[v] >=  c_xboxPlaneEps) positive++;
				else if (vtxDist[v] <= -c_xboxPlaneEps) negative++;
				else vtxDist[v] = 0.0f;
			}

			if (positive == cur->vertexCount) continue;     // wholly inside
			if (negative == cur->vertexCount) return false; // wholly outside

			for (s32 v = 0; v < cur->vertexCount; v++)
			{
				const s32 a = v;
				const s32 b = (v + 1) % cur->vertexCount;
				const f32 d0 = vtxDist[a], d1 = vtxDist[b];

				if (d0 < 0.0f && d1 < 0.0f) continue;
				if (d0 >= 0.0f && d1 >= 0.0f)
				{
					if (next->vertexCount < XBOX_FRUSTUM_PLANE_MAX)
						next->vtx[next->vertexCount++] = cur->vtx[a];
					continue;
				}

				const f32 t = -d0 / (d1 - d0);
				Vec3f isect = { (1.0f - t)*cur->vtx[a].x + t*cur->vtx[b].x,
				                (1.0f - t)*cur->vtx[a].y + t*cur->vtx[b].y,
				                (1.0f - t)*cur->vtx[a].z + t*cur->vtx[b].z };
				if (d0 > 0.0f && next->vertexCount < XBOX_FRUSTUM_PLANE_MAX)
					next->vtx[next->vertexCount++] = cur->vtx[a];
				if (t < 1.0f && next->vertexCount < XBOX_FRUSTUM_PLANE_MAX)
					next->vtx[next->vertexCount++] = isect;
			}

			XboxPolygon* tmp = cur; cur = next; next = tmp;
			if (cur->vertexCount < 3) return false;
		}

		out->vertexCount = cur->vertexCount;
		for (s32 i = 0; i < out->vertexCount; i++) out->vtx[i] = cur->vtx[i];
		return true;
	}

	// =====================================================================
	// Phase 14 step 1 - scaffolding only.
	//
	// Per-object portal-frustum snapshot pool, plus empty sprite + model
	// display lists. Nothing reads from or writes to these yet. Step 2
	// will route the sprite path through s_xboxSpriteList; step 3 will
	// route the 3DO model path through s_xboxModelList; each step is its
	// own commit so a regression can be bisected to the step that
	// introduced it.
	// =====================================================================
	enum { XBOX_PORTAL_PLANE_POOL = 4096 };
	static Vec4f s_xboxPortalPlanePool[XBOX_PORTAL_PLANE_POOL];
	static u32   s_xboxPortalPlaneCount = 0;

	struct XboxSpriteListEntry
	{
		Vec3f anchor;            // obj->posWS - for the all-or-nothing center-in-frustum test
		Vec3f cornerBL, cornerTL, cornerTR, cornerBR;
		u32   color;
		TFE_RenderBackend::GpuTextureHandle tex;
		u32   portalInfo;        // (offset << 8) | count into s_xboxPortalPlanePool
		bool  flipU;
	};
	enum { XBOX_SPRITE_LIST_CAP = 512 };
	static XboxSpriteListEntry s_xboxSpriteList[XBOX_SPRITE_LIST_CAP];
	static u32 s_xboxSpriteListCount = 0;

	struct XboxModelListEntry
	{
		SecObject* obj;
		u32        color;
		bool       fullBright;
		u32        portalInfo;
	};
	enum { XBOX_MODEL_LIST_CAP = 256 };
	static XboxModelListEntry s_xboxModelList[XBOX_MODEL_LIST_CAP];
	static u32 s_xboxModelListCount = 0;

	// Phase 14 step 2 helpers.
	static inline void xboxPortalPlanes_clear()
	{
		s_xboxPortalPlaneCount = 0;
	}

	static inline void xboxObjectLists_clear()
	{
		s_xboxSpriteListCount = 0;
		s_xboxModelListCount  = 0;
	}

	// =====================================================================
	// Phase 15 - wall display list.
	//
	// Upstream sdisplayList_addSegment (sectorDisplayList.cpp) appends
	// each wall the recursion encounters to a vertex buffer, then flushes
	// the whole list in a few batched draw calls at end of frame. Walls
	// re-emitted from multi-portal-path visits cost only a memcpy +
	// shader-side per-pixel clip; draw-call count stays small.
	//
	// On Xbox NV2A FF we can't shader-clip per pixel - but the wall
	// quads themselves render identical pixels regardless of which portal
	// reached the sector (z-test handles overdraw). So the equivalent is:
	// append per visit, batch by texture, submit at end of frame. That
	// turns ~500 per-visit draw calls in dense areas into ~10-20 per-
	// texture batches. The vertex buffer carries the duplicates; the GPU
	// eats them with the depth test.
	//
	// Untextured fallback (debug-coloured walls when tex is unusable)
	// uses NULL tex - the backend routes stage-0 COLORARG1=DIFFUSE so
	// the per-vertex color drives the pixel directly.
	// =====================================================================
	enum {
		XBOX_WALL_VERT_CAP  = 32768,   // ~ 768 KB at 24 bytes/vert
		XBOX_WALL_CHUNK_CAP = 2048
	};
	struct XboxWallChunk
	{
		TFE_RenderBackend::GpuTextureHandle tex;
		u32 vertStart;
		u32 vertCount;
	};
	static TFE_RenderBackend::GpuTexVert s_xboxWallVerts[XBOX_WALL_VERT_CAP];
	static u32                           s_xboxWallVertCount = 0;
	static XboxWallChunk                 s_xboxWallChunks[XBOX_WALL_CHUNK_CAP];
	static u32                           s_xboxWallChunkCount = 0;

	static inline void xboxWallList_clear()
	{
		s_xboxWallVertCount  = 0;
		s_xboxWallChunkCount = 0;
	}

	// Append 6 verts (a 2-tri quad). Extends the last chunk if the same
	// texture is contiguous, otherwise starts a new chunk. The contiguous-
	// merge case is the common one (xboxEmitWallQuad calls for one wall's
	// mid+top+bot often share the wall texture).
	static void xboxWallList_appendQuad(TFE_RenderBackend::GpuTextureHandle tex,
	                                     const TFE_RenderBackend::GpuTexVert* verts)
	{
		if (s_xboxWallVertCount + 6 > XBOX_WALL_VERT_CAP) return;

		if (s_xboxWallChunkCount > 0)
		{
			XboxWallChunk& last = s_xboxWallChunks[s_xboxWallChunkCount - 1];
			if (last.tex == tex && last.vertStart + last.vertCount == s_xboxWallVertCount)
			{
				memcpy(&s_xboxWallVerts[s_xboxWallVertCount], verts,
				       sizeof(TFE_RenderBackend::GpuTexVert) * 6);
				last.vertCount      += 6;
				s_xboxWallVertCount += 6;
				return;
			}
		}

		if (s_xboxWallChunkCount >= XBOX_WALL_CHUNK_CAP) return;
		XboxWallChunk& c = s_xboxWallChunks[s_xboxWallChunkCount++];
		c.tex       = tex;
		c.vertStart = s_xboxWallVertCount;
		c.vertCount = 6;
		memcpy(&s_xboxWallVerts[s_xboxWallVertCount], verts,
		       sizeof(TFE_RenderBackend::GpuTexVert) * 6);
		s_xboxWallVertCount += 6;
	}

	static void xboxFlushWallList()
	{
		for (u32 i = 0; i < s_xboxWallChunkCount; i++)
		{
			const XboxWallChunk& c = s_xboxWallChunks[i];
			const u32 tris = c.vertCount / 3;
			if (tris == 0) continue;
			TFE_RenderBackend::gpuDrawTexturedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, c.tex, &s_xboxWallVerts[c.vertStart], tris);
		}
	}

	// Snapshot the current back frustum into the plane pool. Returns a
	// packed (offset<<8) | (count) handle. count 0 means "no clip"
	// (start sector, where the frustum stack is empty).
	static u32 xboxPortalPlanes_snapshotBack()
	{
		const XboxFrustum* f = xboxFrustum_getBack();
		if (!f || f->planeCount == 0) return 0;

		const u32 n = (f->planeCount < 255) ? f->planeCount : 255;
		if (s_xboxPortalPlaneCount + n > XBOX_PORTAL_PLANE_POOL) return 0;

		const u32 offset = s_xboxPortalPlaneCount;
		for (u32 i = 0; i < n; i++) s_xboxPortalPlanePool[offset + i] = f->planes[i];
		s_xboxPortalPlaneCount += n;
		return (offset << 8) | (n & 0xFF);
	}

	// Test whether a single world-space point is inside every plane in
	// the given list. Used for the all-or-nothing sprite visibility
	// decision at flush time.
	static bool xboxFrustum_pointInside(const Vec3f* p, const Vec4f* planes,
	                                     s32 planeCount, f32 eps)
	{
		for (s32 i = 0; i < planeCount; i++)
		{
			const f32 d = xboxFrustum_planeDist(&planes[i], p);
			if (d < -eps) return false;
		}
		return true;
	}

	static inline void xboxIdentity4(f32 m[16])
	{
		memset(m, 0, sizeof(f32) * 16);
		m[0] = m[5] = m[10] = m[15] = 1.0f;
	}

	static void xboxBuildProj()
	{
		// JEDI engine convention - at 320x200 with horiz FOV ~90 the
		// focalLength = halfWidth (160), focalLenAspect = 160. The proj
		// scales are 2*focal/dim, which for 320x200 gives xScale=1,
		// yScale=1.6. Rectangular-pixel correction (200p) keeps yScale=1.6
		// since aspectScaleY only kicks in for square-pixel resolutions.
		const f32 halfW = (f32)(s_xboxViewW >> 1);
		const f32 focal = halfW;            // 90 deg horiz FOV
		const f32 focalAspect = halfW;      // 200p case, no scale
		const f32 xScale = 2.0f * focal       / (f32)s_xboxViewW;
		const f32 yScale = 2.0f * focalAspect / (f32)s_xboxViewH;
		const f32 zn = 0.01f, zf = 4096.0f;

		f32* p = s_xboxProjMtx;
		memset(p, 0, sizeof(f32) * 16);
		p[ 0] = xScale;
		// NEGATIVE yScale flips screen-Y vs view-Y. JEDI uses -Y up in
		// world space (floorHeight > ceilingHeight numerically); with a
		// standard +yScale, view-space "up" lands on screen-space "down"
		// and the world renders upside down (or, with the camera between
		// floor and ceiling, simply off-screen). Flipping yScale lets us
		// keep every vertex + the camera in TFE conventions and have the
		// projection step do the handedness fix in one place.
		p[ 5] = -yScale;
		p[10] = zf / (zf - zn);
		p[11] = 1.0f;                       // w_clip = view.z (D3D LH)
		p[14] = -zn * zf / (zf - zn);
	}

	namespace RClassic_GPU
	{
		void resetState() {}
		void setupInitCameraAndLights(s32 w, s32 h)
		{
			s_xboxViewW = w; s_xboxViewH = h;
			xboxBuildProj();
			xboxIdentity4(s_xboxViewMtx);
		}
		void changeResolution(s32 w, s32 h)
		{
			s_xboxViewW = w; s_xboxViewH = h;
			xboxBuildProj();
		}
		void computeCameraTransform(RSector*, f32 pitch, f32 yaw,
		                            f32 camX, f32 camY, f32 camZ)
		{
			s_cameraPos.x = camX; s_cameraPos.y = camY; s_cameraPos.z = camZ;
			s_xboxLastYaw = yaw; s_xboxLastPitch = pitch;

			// TFE camera basis (matches rclassicGPU.cpp):
			//   right   = ( cosYaw,                0,                 sinYaw)
			//   up      = ( sinYaw*sinPitch,       cosPitch,         -cosYaw*sinPitch)
			//   forward = (-sinYaw*cosPitch,       sinPitch,          cosYaw*cosPitch)
			// (TFE stores forward = -m2 since m2 is "back"; we materialise
			// forward directly here so the D3D view matrix is straightforward.)
			//
			// Angles arrive in JEDI angle14 units (16384 = 2pi). sinCosFlt
			// does the unit conversion - calling raw sinf/cosf treats the
			// 14-bit angle as radians and produces a garbage basis (the
			// Phase 2 walls drew at hr=S_OK but landed off-screen).
			f32 sy, cy, sp, cp;
			TFE_Jedi::sinCosFlt(-yaw,   &sy, &cy);
			TFE_Jedi::sinCosFlt(-pitch, &sp, &cp);
			const Vec3f r = {  cy,             0.0f,           sy           };
			const Vec3f u = {  sy * sp,        cp,            -cy * sp      };
			const Vec3f f = { -sy * cp,        sp,             cy * cp      };
			s_cameraDir   = f;
			s_cameraDirXZ.x = f.x; s_cameraDirXZ.y = 0.0f; s_cameraDirXZ.z = f.z;

			// D3D LH view matrix, row-vector convention:
			//   [ r.x  u.x  f.x  0 ]
			//   [ r.y  u.y  f.y  0 ]
			//   [ r.z  u.z  f.z  0 ]
			//   [-P.r -P.u -P.f  1 ]
			const Vec3f P = s_cameraPos;
			f32* m = s_xboxViewMtx;
			m[ 0] = r.x; m[ 1] = u.x; m[ 2] = f.x; m[ 3] = 0.0f;
			m[ 4] = r.y; m[ 5] = u.y; m[ 6] = f.y; m[ 7] = 0.0f;
			m[ 8] = r.z; m[ 9] = u.z; m[10] = f.z; m[11] = 0.0f;
			m[12] = -(P.x*r.x + P.y*r.y + P.z*r.z);
			m[13] = -(P.x*u.x + P.y*u.y + P.z*u.z);
			m[14] = -(P.x*f.x + P.y*f.y + P.z*f.z);
			m[15] = 1.0f;
		}
		void transformPointByCamera(vec3_float*, vec3_float*) {}
		void computeSkyOffsets() {}
	}

	// =====================================================================
	// TFE_Sectors_GPU - Phase 2 implementation.
	//
	// draw(): walk the current sector's wall list and submit two triangles
	// per wall (a vertical quad spanning ceilingHeight to floorHeight in
	// world space). One flat color per wall, derived from wall index, so
	// adjacent walls don't blend visually. No textures, no lighting, no
	// portal traversal yet - just the room you're standing in as a
	// flat-shaded outline. Adjoin walls (open portals to next sectors)
	// render as solid in Phase 2 so the room reads as a closed volume.
	//
	// Per-frame allocation: a static vertex buffer sized for any plausible
	// sector wall count. SECBASE's largest sectors hover around 30-40
	// walls; 256 walls (1536 verts, 24 KB) is comfortable headroom.
	// =====================================================================
	static inline f32 fixedToF(fixed16_16 x) { return (f32)x * (1.0f / 65536.0f); }

	// Cheap deterministic color from wall id so adjacent walls contrast.
	// Phase 3 fallback when a wall has no usable mid texture (NULL,
	// compressed RLE we don't decode yet, or non-power-of-2 dims).
	static inline u32 wallColor(s32 wid, s32 secId)
	{
		const u32 seed = (u32)(wid * 2654435761u) ^ (u32)(secId * 374761393u);
		const u8 r = (u8)(64 + (seed         & 0x7F));
		const u8 g = (u8)(64 + ((seed >> 8)  & 0x7F));
		const u8 b = (u8)(64 + ((seed >> 16) & 0x7F));
		return (0xFFu << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
	}

	static inline bool isPow2(u32 v) { return v != 0 && (v & (v - 1)) == 0; }

	// Phase 6: convert sector->ambient (fixed16_16 in [0, MAX_LIGHT_LEVEL=31])
	// into a 0xFFRRGGBB grayscale diffuse colour that MODULATEs the texel
	// at stage 0. Pure linear mapping for the first cut - DF's actual
	// light ramp is non-linear (colormap-based) but per-sector flat is
	// good enough to restore the moody dim/bright contrast.
	//
	// Headlamp / weapon flash / level ambient feed in via s_worldAmbient
	// (set by renderer_setWorldAmbient: stored as MAX_LIGHT_LEVEL - boost,
	// so subtracting gets the boost value back). DF's lighting takes the
	// max of per-sector ambient and this global, which is why a sector
	// at light=4 reads as fully-lit when the headlamp is on at boost=31.
	// Without this lift the headlamp / blaster muzzle flashes do nothing.
	static inline u32 ambientToColor(fixed16_16 ambientFx)
	{
		s32 level = ambientFx >> 16;       // per-sector light level
		const s32 worldLift = 31 - TFE_Jedi::s_worldAmbient;  // headlamp / weapon flash / level lift
		if (worldLift > level) level = worldLift;
		if (level < 0)  level = 0;
		if (level > 31) level = 31;
		// 31 * 8 = 248, close enough to 255 to read as fullbright at max.
		const u32 g = (u32)(level * 8);
		return 0xFF000000u | (g << 16) | (g << 8) | g;
	}

	// Phase 5.5 proper: multi-loop polygon triangulation for sector flats.
	//
	// DF sectors store walls as one OR MORE closed loops in
	// sector->walls. Loop boundaries are implicit: walls[i].w1 ==
	// walls[i+1].w0 inside a loop, and != at loop boundaries (and at
	// the wrap from the last wall back to the first wall of its loop).
	// Single-loop sectors are plain polygons. Multi-loop sectors have
	// one outer boundary plus one or more interior "hole" boundaries
	// (interior pillars / columns).
	//
	// Algorithm:
	//   1. Walk walls; emit each w0 into v[], split into loops by
	//      checking consecutive w1 == next w0.
	//   2. Signed-area each loop. The loop with the largest |area| is
	//      the outer boundary; all others are holes.
	//   3. For each hole, pick its rightmost vertex H, find the
	//      Euclidean-closest outer vertex O, splice the hole into the
	//      outer at O via a degenerate bridge: ... O, H, hole-from-H-
	//      around-back-to-H, H, O, ... . If the hole's winding doesn't
	//      match the outer's, walk it in reverse so the unified polygon
	//      stays simple.
	//   4. Ear-clip the unified polygon.
	//   5. If ear-clip gets stuck (degenerate input), fall back to the
	//      old fan-from-v[0] for whatever wasn't emitted.
	static const u32 XBOX_MAX_FLAT_VERTS = 2048;
	static const u32 XBOX_MAX_FLAT_POLY  = 384;     // verts inc. bridge dupes
	static const u32 XBOX_MAX_FLAT_LOOPS = 16;
	static TFE_RenderBackend::GpuTexVert s_flatVerts[XBOX_MAX_FLAT_VERTS];

	struct XboxFlatVert { f32 x, z; };

	static inline bool xboxPointInTri(f32 px, f32 pz,
	                                  f32 ax, f32 az,
	                                  f32 bx, f32 bz,
	                                  f32 cx, f32 cz)
	{
		const f32 d1 = (px - bx) * (az - bz) - (ax - bx) * (pz - bz);
		const f32 d2 = (px - cx) * (bz - cz) - (bx - cx) * (pz - cz);
		const f32 d3 = (px - ax) * (cz - az) - (cx - ax) * (pz - az);
		const bool neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
		const bool pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
		return !(neg && pos);
	}

	static inline f32 xboxSignedArea2(const XboxFlatVert* v, u32 start, u32 count)
	{
		f32 a = 0.0f;
		for (u32 i = 0; i < count; i++)
		{
			const XboxFlatVert& p = v[start + i];
			const XboxFlatVert& q = v[start + (i + 1) % count];
			a += p.x * q.z - q.x * p.z;
		}
		return a;
	}

	// Phase 11 diagnostic counters - definition hoisted ahead of
	// xboxDrawSectorFlat so the flat path can write its fields. The
	// reset/dump helpers stay further down with the object code.
	struct XboxObjStats
	{
		u32 sprites, frames, models, others;
		u32 nullObj, behindCam, noFrame, noCell;
		u32 zeroSize, uploadFail, drawn;
		// Phase 13 corpse-debug split.
		u32 corpses;         // sprites with ETFLAG_CORPSE seen
		u32 corpsesDrawn;    // " " that made it through the pipeline
		u32 noWax, noAnim, noView;
	};
	static XboxObjStats s_objStats;
	static u32 s_objStatsFrame = 0;

	static void xboxDrawSectorFlat(RSector* sector, fixed16_16 heightFx,
	                               TextureData** texPtr, vec2_fixed offset)
	{
		if (!texPtr || !*texPtr) return;
		TextureData* tex = *texPtr;
		if (!tex->image || tex->compressed != 0) return;
		// pow2 not required - backend upscales as needed.
		if (sector->wallCount < 3) return;

		// ---- Step 1: build vertex array + identify loop boundaries.
		XboxFlatVert loopVerts[XBOX_MAX_FLAT_POLY];
		u32 loopStart[XBOX_MAX_FLAT_LOOPS];
		u32 loopLen  [XBOX_MAX_FLAT_LOOPS];
		u32 numLoops = 0;
		u32 vN = 0;
		u32 curStart = 0;

		for (s32 i = 0; i < sector->wallCount; i++)
		{
			RWall* w = &sector->walls[i];
			if (!w->w0) continue;
			if (vN >= XBOX_MAX_FLAT_POLY) return;
			loopVerts[vN].x = fixedToF(w->w0->x);
			loopVerts[vN].z = fixedToF(w->w0->z);
			vN++;

			// Loop boundary: this wall's w1 doesn't match the next
			// wall's w0 (or this is the last wall in the sector).
			vec2_fixed* nextW0 = (i + 1 < sector->wallCount)
			                   ? sector->walls[i + 1].w0 : NULL;
			if (w->w1 != nextW0)
			{
				if (numLoops >= XBOX_MAX_FLAT_LOOPS) return;
				loopStart[numLoops] = curStart;
				loopLen  [numLoops] = vN - curStart;
				numLoops++;
				curStart = vN;
			}
		}
		if (numLoops == 0) return;

		// ---- Step 2: identify the outer loop (largest |signed area|).
		f32 loopArea[XBOX_MAX_FLAT_LOOPS];
		u32 outerIdx = 0;
		f32 maxAbs = 0.0f;
		for (u32 i = 0; i < numLoops; i++)
		{
			loopArea[i] = xboxSignedArea2(loopVerts, loopStart[i], loopLen[i]);
			const f32 a = (loopArea[i] < 0.0f) ? -loopArea[i] : loopArea[i];
			if (a > maxAbs) { maxAbs = a; outerIdx = i; }
		}
		const f32 outerSign = (loopArea[outerIdx] >= 0.0f) ? 1.0f : -1.0f;

		// ---- Step 3: build the unified polygon. Start with outer.
		XboxFlatVert poly[XBOX_MAX_FLAT_POLY];
		u32 pN = 0;
		for (u32 i = 0; i < loopLen[outerIdx]; i++)
		{
			poly[pN++] = loopVerts[loopStart[outerIdx] + i];
		}

		// Splice each hole.
		for (u32 h = 0; h < numLoops; h++)
		{
			if (h == outerIdx) continue;
			const u32 hs = loopStart[h];
			const u32 hL = loopLen[h];
			if (hL < 3) continue;

			// Reverse the hole's traversal direction if its signed area
			// has the SAME sign as the outer (holes should be wound the
			// opposite way).
			const bool reverseHole =
				((loopArea[h] >= 0.0f) ? 1.0f : -1.0f) == outerSign;

			// Rightmost hole vertex - canonical bridge starting point.
			u32 hStart = 0;
			for (u32 i = 1; i < hL; i++)
				if (loopVerts[hs + i].x > loopVerts[hs + hStart].x) hStart = i;
			const XboxFlatVert hv = loopVerts[hs + hStart];

			// Closest current-polygon vertex (Euclidean).
			u32 bestO = 0;
			f32 bestD = 1e30f;
			for (u32 i = 0; i < pN; i++)
			{
				const f32 dx = poly[i].x - hv.x;
				const f32 dz = poly[i].z - hv.z;
				const f32 d2 = dx * dx + dz * dz;
				if (d2 < bestD) { bestD = d2; bestO = i; }
			}

			// Splice in:  ... poly[0..bestO], O, H, hole_walk, H, O, poly[bestO+1..] ...
			// We insert (hL + 2) new vertices right after position bestO.
			const u32 insertN = hL + 2;
			if (pN + insertN > XBOX_MAX_FLAT_POLY) continue;

			// Shift the tail.
			for (s32 k = (s32)pN - 1; k > (s32)bestO; k--)
				poly[k + insertN] = poly[k];
			pN += insertN;

			// Write the spliced region.
			u32 w = bestO + 1;
			poly[w++] = hv;                                  // H (entering)
			if (reverseHole)
			{
				for (u32 i = 1; i < hL; i++)
				{
					const u32 src = (hStart + hL - i) % hL;
					poly[w++] = loopVerts[hs + src];
				}
			}
			else
			{
				for (u32 i = 1; i < hL; i++)
				{
					const u32 src = (hStart + i) % hL;
					poly[w++] = loopVerts[hs + src];
				}
			}
			poly[w++] = hv;                                  // H (exiting)
			poly[w++] = poly[bestO];                         // O (exiting)
		}

		// ---- Step 4: ear-clip the unified polygon.
		const f32 y    = fixedToF(heightFx);
		const f32 ox   = fixedToF(offset.x);
		const f32 oz   = fixedToF(offset.z);
		const f32 uMul = 8.0f / (f32)tex->width;
		const f32 vMul = 8.0f / (f32)tex->height;
		const u32 col  = ambientToColor(sector->ambient);

		TFE_RenderBackend::GpuTexVert* out    = s_flatVerts;
		TFE_RenderBackend::GpuTexVert* outEnd = s_flatVerts + XBOX_MAX_FLAT_VERTS;

		u32 idx[XBOX_MAX_FLAT_POLY];
		for (u32 i = 0; i < pN; i++) idx[i] = i;

		u32 remaining = pN;
		u32 guard = pN * pN + 16;
		while (remaining > 3 && guard--)
		{
			bool found = false;
			for (u32 i = 0; i < remaining; i++)
			{
				const u32 ia = idx[(i == 0) ? remaining - 1 : i - 1];
				const u32 ib = idx[i];
				const u32 ic = idx[(i + 1) % remaining];

				const f32 cross = (poly[ib].x - poly[ia].x) * (poly[ic].z - poly[ib].z)
				                - (poly[ib].z - poly[ia].z) * (poly[ic].x - poly[ib].x);
				if (cross * outerSign <= 0.0f) continue;

				bool ok = true;
				for (u32 j = 0; j < remaining; j++)
				{
					const u32 ij = idx[j];
					if (ij == ia || ij == ib || ij == ic) continue;
					if (xboxPointInTri(poly[ij].x, poly[ij].z,
					                    poly[ia].x, poly[ia].z,
					                    poly[ib].x, poly[ib].z,
					                    poly[ic].x, poly[ic].z))
					{ ok = false; break; }
				}
				if (!ok) continue;

				if (out + 3 > outEnd) break;
				#define EMIT(ix) do {                                 \
					out->x = poly[ix].x; out->y = y; out->z = poly[ix].z; \
					out->color = col;                                  \
					out->u = (poly[ix].x + ox) * uMul;                 \
					out->v = (poly[ix].z + oz) * vMul;                 \
					out++;                                             \
				} while(0)
				EMIT(ia); EMIT(ib); EMIT(ic);

				for (u32 k = i; k + 1 < remaining; k++) idx[k] = idx[k + 1];
				remaining--;
				found = true;
				break;
			}
			if (!found) break;
		}

		if (remaining == 3 && out + 3 <= outEnd)
		{
			EMIT(idx[0]); EMIT(idx[1]); EMIT(idx[2]);
		}
		else if (remaining > 3)
		{
			// Ear-clip stuck on a degenerate region - fan-cover what's
			// left so we don't leave a giant gap.
			for (u32 i = 1; i + 1 < remaining && out + 3 <= outEnd; i++)
			{
				EMIT(idx[0]); EMIT(idx[i]); EMIT(idx[i + 1]);
			}
		}
		#undef EMIT

		const u32 tris = (u32)(out - s_flatVerts) / 3;
		if (tris == 0) return;

		TFE_RenderBackend::GpuTextureHandle gpuTex =
			TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
				tex, tex->image, tex->width, tex->height, /*columnMajor*/true);

		// Phase 12.C - Per-flat frustum clip (CPU Sutherland-Hodgman).
		// The traversal in TFE_Sectors_GPU::draw stamps a portal frustum
		// onto the frustum stack before each recursion; here we clip every
		// flat triangle against the current back frustum so co-planar
		// flats from neighbouring sectors only paint the screen area
		// visible through the chain of portals we walked. That eliminates
		// the corridor z-fight without changing the wall draw path.
		//
		// Flats are constant-Y with a linear (x+ox)*uMul / (z+oz)*vMul UV
		// mapping, so clip-generated verts get correct UVs by recomputing
		// from XZ rather than barycentric interpolation. Colour is a
		// per-sector ambient constant.
		const XboxFrustum* curF = xboxFrustum_getBack();
		if (!curF || curF->planeCount == 0)
		{
			// Start sector (no portal cone) - submit unchanged.
			TFE_RenderBackend::gpuDrawTexturedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, gpuTex, s_flatVerts, tris);
			return;
		}

		static TFE_RenderBackend::GpuTexVert s_flatClipBuf[XBOX_MAX_FLAT_VERTS * 2];
		u32 clipOutCount = 0;
		const u32 clipOutCap = sizeof(s_flatClipBuf) / sizeof(s_flatClipBuf[0]);

		for (u32 t = 0; t < tris && clipOutCount + 3 <= clipOutCap; t++)
		{
			const TFE_RenderBackend::GpuTexVert& va = s_flatVerts[t * 3 + 0];
			const TFE_RenderBackend::GpuTexVert& vb = s_flatVerts[t * 3 + 1];
			const TFE_RenderBackend::GpuTexVert& vc = s_flatVerts[t * 3 + 2];
			Vec3f triPos[3] = {
				{ va.x, va.y, va.z },
				{ vb.x, vb.y, vb.z },
				{ vc.x, vc.y, vc.z }
			};
			XboxPolygon clipped;
			if (!xboxFrustum_clipPolyToPlanes(triPos, 3, curF->planes,
			                                  (s32)curF->planeCount, &clipped)) continue;
			if (clipped.vertexCount < 3) continue;

			// Fan-triangulate the (convex) clip output from vertex 0.
			for (s32 j = 1; j + 1 < clipped.vertexCount; j++)
			{
				if (clipOutCount + 3 > clipOutCap) break;
				const s32 ix[3] = { 0, j, j + 1 };
				for (s32 k = 0; k < 3; k++)
				{
					const Vec3f& p = clipped.vtx[ix[k]];
					TFE_RenderBackend::GpuTexVert& o = s_flatClipBuf[clipOutCount++];
					o.x = p.x; o.y = y; o.z = p.z;
					o.color = col;
					o.u = (p.x + ox) * uMul;
					o.v = (p.z + oz) * vMul;
				}
			}
		}

		const u32 clipTris = clipOutCount / 3;
		if (clipTris == 0) return;
		TFE_RenderBackend::gpuDrawTexturedTrisWorld(
			s_xboxViewMtx, s_xboxProjMtx, gpuTex, s_flatClipBuf, clipTris);
	}

	// Draw every solid wall (no nextSector) of `sector`. Adjoin walls
	// (nextSector != NULL) are skipped so the player can see through
	// them into the next sector. The traversal in draw() pushes those
	// next sectors onto the visit queue.
	// Emit one wall quad (two textured tris) spanning [yTop, yBot] in
	// world-Y between (x0,z0) and (x1,z1). texHeightFx is the wall
	// portion's height in fixed16_16 texels (DF's per-portion height
	// values: mid/top/bot TexelHeight). Falls back to a hashed-colour
	// quad if the texture can't be uploaded.
	static void xboxEmitWallQuad(s32 secId, s32 wallIdx,
	                              f32 x0, f32 z0, f32 x1, f32 z1,
	                              f32 yTop, f32 yBot,
	                              TextureData* tex,
	                              fixed16_16 texelLengthFx,
	                              fixed16_16 texHeightFx,
	                              fixed16_16 vOffsetFx,
	                              u32 color)
	{
		// Phase 11: pow2 check dropped. The backend now upscales non-pow2
		// source textures to next-pow2 internally during palette expand.
		const bool texUsable =
			tex && tex->image && tex->compressed == 0;

		// Phase 15 - route to the wall display list instead of an
		// immediate draw. Untextured fallback shares the textured-vertex
		// path with tex=NULL + per-vertex debug color (the backend's
		// stage-0 routes COLORARG1=DIFFUSE when tex is NULL).
		TFE_RenderBackend::GpuTextureHandle gpuTex = NULL;
		u32 vcolor = color;
		f32 uMax = 0.0f, vMax = 0.0f;
		(void)vOffsetFx;
		if (texUsable)
		{
			const f32 texelLen = fixedToF(texelLengthFx);
			const f32 texH     = fixedToF(texHeightFx);
			uMax = texelLen / (f32)tex->width;
			vMax = texH     / (f32)tex->height;
			// Door / elevator scrolling note:
			// We deliberately do NOT add the wall's matching offset.z
			// (midOffset.z / topOffset.z / botOffset.z) here. On the GPU
			// mesh path the texture is already world-anchored: D3D
			// linearly interpolates V across the quad, and texHeightFx
			// here is the wall's *current* midTexelHeight / topTexelHeight
			// / botTexelHeight, recomputed every frame by DF's
			// wall_computeTexelHeights as the sector moves. The V value
			// at any fixed world Y on the wall therefore reduces to
			// (Y - yTop)*8/texH - constant across the door's motion -
			// so the texture follows the mesh naturally.
			//
			// Upstream's software renderer DOES apply the offset because
			// its per-pixel vCoordStep stretches midTexelHeight across
			// the on-screen wall height; the offset is the compensation
			// term that path needs. Notably upstream's
			// sector_adjustTextureWallOffsets_Floor only ever touches
			// bot/mid offsets - it never touches topOffset.z - confirming
			// that top slivers (the typical DF door) are already
			// mesh-anchored even in upstream.
			//
			// TODO: scrolling-texture walls (conveyor belts, etc., flag
			// WF1_SCROLL_*_TEX) need their own offset application; those
			// are rare in DF and will be revisited separately.
			gpuTex = TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
				tex, tex->image, tex->width, tex->height, /*columnMajor*/true);
		}
		else
		{
			vcolor = wallColor(wallIdx, secId);
		}

		// V convention matches upstream rwallFloat.cpp:856 / 1505:
		// V=0 at yBot (the wall portion's BOTTOM edge in world space),
		// V=vMax at yTop. For a door's top sliver yBot is the door's
		// rising ceiling, so V=0 is anchored to the moving edge - the
		// texture follows the door upward just like upstream's column
		// drawer. Previously had these swapped; tileable wall textures
		// looked fine, but doors stayed glued to the fixed top edge
		// instead of sliding with the door.
		TFE_RenderBackend::GpuTexVert tv[6];
		tv[0].x = x0; tv[0].y = yBot; tv[0].z = z0; tv[0].color = vcolor; tv[0].u = 0.0f; tv[0].v = 0.0f;
		tv[1].x = x0; tv[1].y = yTop; tv[1].z = z0; tv[1].color = vcolor; tv[1].u = 0.0f; tv[1].v = vMax;
		tv[2].x = x1; tv[2].y = yTop; tv[2].z = z1; tv[2].color = vcolor; tv[2].u = uMax; tv[2].v = vMax;
		tv[3].x = x0; tv[3].y = yBot; tv[3].z = z0; tv[3].color = vcolor; tv[3].u = 0.0f; tv[3].v = 0.0f;
		tv[4].x = x1; tv[4].y = yTop; tv[4].z = z1; tv[4].color = vcolor; tv[4].u = uMax; tv[4].v = vMax;
		tv[5].x = x1; tv[5].y = yBot; tv[5].z = z1; tv[5].color = vcolor; tv[5].u = uMax; tv[5].v = 0.0f;

		xboxWallList_appendQuad(gpuTex, tv);
	}

	// Round up to the next power of two. Sprite cells aren't generally
	// pow2 so we pad to the nearest size and CLAMP the UVs to [0, cellW/W].
	static inline u32 nextPow2(u32 v)
	{
		u32 r = 1;
		while (r < v) r <<= 1;
		return r;
	}

	// Phase 8: upload a WAX/FME cell into an alpha-tested GPU texture.
	// Cells are column-major 8-bit indexed (RLE-compressed when
	// cell->compressed != 0). Palette index 0 is the DF transparent
	// colour - we put alpha = 0 there, every other index becomes the
	// fully-opaque palette colour.
	//
	// Cells aren't generally power-of-two; we pad to the next pow2 with
	// alpha = 0 padding and the billboard UV math uses (cellW/texW,
	// cellH/texH) as the right/bottom UV with CLAMP addressing so the
	// padding never gets sampled.
	enum { XBOX_MAX_CELL_PIXELS = 256 * 256 };
	static u32 s_cellStage[XBOX_MAX_CELL_PIXELS];

	// Phase 11: produce raw 8-bit indices in a tight row-major buffer
	// of the cell's actual size and hand off to the shared P8 upload
	// path. The hardware palette (set by ensureP8PaletteSynced) does
	// the index->RGBA expansion at sample time. Index 0 (DF transparent
	// colour) becomes alpha=0 via the palette, so the alpha-tested
	// sprite draw discards transparent pixels.
	static u8 s_cellIdxStage[XBOX_MAX_CELL_PIXELS];

	static TFE_RenderBackend::GpuTextureHandle
	xboxUploadWaxCell(WaxCell* cell, const void* waxBase, u32* outTexW, u32* outTexH)
	{
		if (!cell || cell->sizeX <= 0 || cell->sizeY <= 0) return NULL;

		const u32 cellW = (u32)cell->sizeX;
		const u32 cellH = (u32)cell->sizeY;
		if (cellW * cellH > XBOX_MAX_CELL_PIXELS) return NULL;

		const u8*  imageData = (const u8*)cell + sizeof(WaxCell);
		const u32* columnOffset = (const u32*)((const u8*)waxBase + cell->columnOffset);
		const u8*  imageStart = (cell->compressed == 1)
		                      ? imageData + (cell->sizeX * sizeof(u32))
		                      : imageData;

		// s_cellIdxStage is row-major [cellH][cellW] of indices. Default
		// to 0 (transparent) so unused regions discard via alpha test
		// even if the pow2-pad upscale picks them up.
		memset(s_cellIdxStage, 0, cellW * cellH);

		u8 colBuf[1024];
		for (u32 x = 0; x < cellW; x++)
		{
			const u8* column;
			if (cell->compressed == 1)
			{
				const u8* colPtr = (const u8*)cell + columnOffset[x];
				if (cellH > sizeof(colBuf)) continue;
				TFE_Jedi::sprite_decompressColumn(colPtr, colBuf, (s32)cellH);
				column = colBuf;
			}
			else
			{
				column = imageStart + columnOffset[x];
			}
			for (u32 y = 0; y < cellH; y++)
				s_cellIdxStage[y * cellW + x] = column[y];
		}

		// The upload function pads to pow2 internally; report the
		// rounded-up dimensions back to the caller for UV math.
		u32 texW = 1, texH = 1;
		while (texW < cellW) texW <<= 1;
		while (texH < cellH) texH <<= 1;
		if (outTexW) *outTexW = texW;
		if (outTexH) *outTexH = texH;
		return TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
			cell, s_cellIdxStage, cellW, cellH, /*columnMajor*/false);
	}

	static inline void xboxObjStatsReset() { memset(&s_objStats, 0, sizeof(s_objStats)); }

	// Perf-window stats: accumulate per-frame counters across the
	// dump window so we can report avg/peak instead of one snapshot.
	static u32 s_xboxPerfFrames        = 0;
	static u32 s_xboxPerfPortalsSum    = 0;
	static u32 s_xboxPerfPortalsPeak   = 0;
	static u32 s_xboxPerfUniqueSum     = 0;
	static u32 s_xboxPerfUniquePeak    = 0;
	static u32 s_xboxPerfSecDrawsSum   = 0;
	static u32 s_xboxPerfSecDrawsPeak  = 0;
	static f64 s_xboxPerfWindowStartT  = 0.0;

	static void xboxPerfTick(u32 portals, u32 uniqueSec, u32 secDraws)
	{
		s_xboxPerfFrames++;
		s_xboxPerfPortalsSum  += portals;
		s_xboxPerfUniqueSum   += uniqueSec;
		s_xboxPerfSecDrawsSum += secDraws;
		if (portals   > s_xboxPerfPortalsPeak)   s_xboxPerfPortalsPeak   = portals;
		if (uniqueSec > s_xboxPerfUniquePeak)    s_xboxPerfUniquePeak    = uniqueSec;
		if (secDraws  > s_xboxPerfSecDrawsPeak)  s_xboxPerfSecDrawsPeak  = secDraws;
	}

	static void xboxObjStatsDump()
	{
		TFE_System::logWrite(LOG_MSG, "GPU",
			"objects: spr=%u fme=%u mdl=%u oth=%u | drawn=%u skip{behindCam=%u noWax=%u noAnim=%u noView=%u noFrame=%u noCell=%u zero=%u up=%u null=%u} corpses{seen=%u drawn=%u}",
			s_objStats.sprites, s_objStats.frames, s_objStats.models, s_objStats.others,
			s_objStats.drawn, s_objStats.behindCam, s_objStats.noWax, s_objStats.noAnim, s_objStats.noView,
			s_objStats.noFrame, s_objStats.noCell, s_objStats.zeroSize, s_objStats.uploadFail, s_objStats.nullObj,
			s_objStats.corpses, s_objStats.corpsesDrawn);

		// Perf window: avg / peak portals + sectors, plus FPS over the
		// elapsed window time. Avoid %f - MSVC 2005 vsprintf on Xbox
		// hangs - use integer scaling.
		const f64 now = TFE_System::getTime();
		const f64 dt  = (s_xboxPerfWindowStartT > 0.0) ? (now - s_xboxPerfWindowStartT) : 0.0;
		const u32 fpsX100 = (dt > 0.0001 && s_xboxPerfFrames > 0)
		                  ? (u32)((f64)s_xboxPerfFrames * 100.0 / dt) : 0u;
		const u32 framesSafe = s_xboxPerfFrames ? s_xboxPerfFrames : 1u;

		TFE_System::logWrite(LOG_MSG, "GPU",
			"perf: fps=%u.%02u frames=%u | sectors: avgUnique=%u peakUnique=%u avgDraws=%u peakDraws=%u | portals: avg=%u peak=%u (cap=%u)",
			fpsX100 / 100u, fpsX100 % 100u, s_xboxPerfFrames,
			s_xboxPerfUniqueSum  / framesSafe, s_xboxPerfUniquePeak,
			s_xboxPerfSecDrawsSum / framesSafe, s_xboxPerfSecDrawsPeak,
			s_xboxPerfPortalsSum / framesSafe, s_xboxPerfPortalsPeak,
			512u);   // matches XBOX_MAX_PORTAL_TRAVERSALS (defined later in file)

		s_xboxPerfFrames = 0;
		s_xboxPerfPortalsSum = s_xboxPerfPortalsPeak = 0;
		s_xboxPerfUniqueSum = s_xboxPerfUniquePeak = 0;
		s_xboxPerfSecDrawsSum = s_xboxPerfSecDrawsPeak = 0;
		s_xboxPerfWindowStartT = now;
	}

	// Phase 13 - JEDI 3DO model render.
	//
	// Each OBJ_TYPE_3D has a JediModel (obj->model). Verts are model-
	// local fixed16_16; obj->transform is a 3x3 rotation, obj->posWS is
	// translation. We transform on CPU to world space, then submit one
	// draw call per polygon (per-poly texture varies, so batching is
	// awkward; SECBASE shows ~7 models per frame so per-poly is fine).
	//
	// UV format per modelAsset_jedi.cpp:870: stored as mul16(uv_norm,
	// texDim), i.e. fixed16_16 in pixel units. Normalise to [0..1] by
	// dividing by texDim AFTER fixedToF.
	//
	// Shading modes covered: PSHADE_TEXTURE, PSHADE_GOURAUD_TEXTURE
	// (treated as plain textured - no per-vertex lighting yet). FLAT /
	// GOURAUD without texture are skipped (rare in DF; mostly debug
	// shapes). Per-sector ambient is applied via diffuse modulate, same
	// as walls/flats.
	enum {
		XBOX_MODEL_MAX_VERTS    = 512,                       // 3DO cap is 500
		XBOX_MODEL_MAX_POLY_TRI = 8,                         // worst-case fan from one poly
		// Sized for the vertex-mode batch case (hologram models with
		// MFLAG_DRAW_VERTICES emit 2 tris per vertex into this same
		// buffer, batched into a single draw call). 1024 tris * 3 verts
		// * 24 bytes/vert = 72 KB - fine on Xbox 64 MB. The poly-fill
		// loop resets outCount per polygon so it never uses more than
		// XBOX_MODEL_MAX_POLY_TRI * 3 at a time.
		XBOX_MODEL_MAX_OUT_TRIS = 1024
	};
	static Vec3f s_modelVertsWS[XBOX_MODEL_MAX_VERTS];
	static TFE_RenderBackend::GpuTexVert s_modelTriBuf[XBOX_MODEL_MAX_OUT_TRIS * 3];

	static void xboxDrawModel(SecObject* obj, u32 sectorColor, bool fullBright)
	{
		JediModel* model = obj->model;
		if (!model || model->polygonCount <= 0 || model->vertexCount <= 0) return;
		if (model->vertexCount > XBOX_MODEL_MAX_VERTS) return;

		// Build world-space verts: ws = R * local + pos, where R is the
		// row-major 3x3 obj->transform (rotateVectorM3x3 convention).
		const f32 R0 = fixedToF(obj->transform[0]);
		const f32 R1 = fixedToF(obj->transform[1]);
		const f32 R2 = fixedToF(obj->transform[2]);
		const f32 R3 = fixedToF(obj->transform[3]);
		const f32 R4 = fixedToF(obj->transform[4]);
		const f32 R5 = fixedToF(obj->transform[5]);
		const f32 R6 = fixedToF(obj->transform[6]);
		const f32 R7 = fixedToF(obj->transform[7]);
		const f32 R8 = fixedToF(obj->transform[8]);
		const f32 tx = fixedToF(obj->posWS.x);
		const f32 ty = fixedToF(obj->posWS.y);
		const f32 tz = fixedToF(obj->posWS.z);

		for (s32 v = 0; v < model->vertexCount; v++)
		{
			const vec3& mv = model->vertices[v];
			const f32 lx = fixedToF(mv.x);
			const f32 ly = fixedToF(mv.y);
			const f32 lz = fixedToF(mv.z);
			s_modelVertsWS[v].x = R0*lx + R1*ly + R2*lz + tx;
			s_modelVertsWS[v].y = R3*lx + R4*ly + R5*lz + ty;
			s_modelVertsWS[v].z = R6*lx + R7*ly + R8*lz + tz;
		}

		// Color + fullBright are precomputed at insertion time so a
		// projectile that crosses a sector boundary between recursion
		// and flush doesn't lose its captured ambient.
		const u32 color = sectorColor;

		// MFLAG_DRAW_VERTICES (used by hologram models like the
		// LEC-Imperial Death Star at the end of LEVEL 1): upstream
		// plots each vertex as a single screen point in the palette
		// colour from polygons[0].color. Xbox FF equivalent: a tiny
		// camera-facing quad per vertex. We early-return; no polygon
		// fill happens for these models.
		if (model->flags & MFLAG_DRAW_VERTICES)
		{
			const u8  vtxPalIdx = (model->polygonCount > 0)
			                    ? (u8)(model->polygons[0].color & 0xFF) : 0u;
			const u32 vtxColor  = TFE_RenderBackend::gpuPaletteEntryRGBA(vtxPalIdx);

			const f32 rx = s_xboxViewMtx[0];
			const f32 rz = s_xboxViewMtx[2];
			const f32 dotHalfWS = 0.04f;

			// Batch every vertex's quad into one draw call. A naive
			// per-vertex submit hammers the GPU with hundreds of calls
			// for a holographic model and tanks frame rate. Reuse the
			// poly-fill triangle buffer (s_modelTriBuf) - cap at
			// XBOX_MODEL_MAX_OUT_TRIS triangles total.
			u32 outCount = 0;
			const u32 outCap = XBOX_MODEL_MAX_OUT_TRIS * 3;
			for (s32 v = 0; v < model->vertexCount; v++)
			{
				if (outCount + 6 > outCap) break;
				const Vec3f& P = s_modelVertsWS[v];
				const f32 hx = rx * dotHalfWS;
				const f32 hz = rz * dotHalfWS;
				const f32 hy = dotHalfWS;
				TFE_RenderBackend::GpuTexVert* o = &s_modelTriBuf[outCount];
				o[0].x = P.x - hx; o[0].y = P.y - hy; o[0].z = P.z - hz;
				o[1].x = P.x - hx; o[1].y = P.y + hy; o[1].z = P.z - hz;
				o[2].x = P.x + hx; o[2].y = P.y + hy; o[2].z = P.z + hz;
				o[3] = o[0];
				o[4] = o[2];
				o[5].x = P.x + hx; o[5].y = P.y - hy; o[5].z = P.z + hz;
				for (s32 k = 0; k < 6; k++)
				{
					o[k].color = vtxColor;
					o[k].u = 0.0f; o[k].v = 0.0f;
				}
				outCount += 6;
			}
			const u32 tris = outCount / 3;
			if (tris > 0)
			{
				TFE_RenderBackend::gpuDrawTexturedTrisWorld(
					s_xboxViewMtx, s_xboxProjMtx, NULL, s_modelTriBuf, tris);
			}
			return;
		}

		for (s32 p = 0; p < model->polygonCount; p++)
		{
			const JmPolygon& poly = model->polygons[p];
			if (poly.vertexCount < 3 || !poly.indices) continue;

			const bool textured = (poly.shading & PSHADE_TEXTURE) && poly.texture && poly.uv;

			// Resolve texture + per-vertex color depending on shading mode.
			TFE_RenderBackend::GpuTextureHandle gpuTex = NULL;
			u32 polyColor = color;
			f32 invTexW = 0.0f, invTexH = 0.0f;

			if (textured)
			{
				TextureData* tex = poly.texture;
				if (!tex->image || tex->compressed != 0) continue;
				gpuTex = TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
					tex, tex->image, tex->width, tex->height, /*columnMajor*/true);
				if (!gpuTex) continue;
				invTexW = 1.0f / (f32)tex->width;
				invTexH = 1.0f / (f32)tex->height;
				if (fullBright) polyColor = 0xFFFFFFFFu;
			}
			else
			{
				// PSHADE_FLAT (or GOURAUD without texture). The polygon
				// carries a palette index in poly.color - look it up in
				// the current DF palette. NULL tex routes the backend's
				// stage 0 to COLORARG1=DIFFUSE, so the per-vert color
				// drives the pixel directly. This is the bolt path
				// (wrbolt.3do has no textures - colored geometry only).
				const u8 palIdx = (u8)(poly.color & 0xFF);
				polyColor = TFE_RenderBackend::gpuPaletteEntryRGBA(palIdx);
			}

			// Fan-triangulate (poly vertices are CW or CCW per source).
			u32 outCount = 0;
			const s32 maxFan = (poly.vertexCount - 2);
			for (s32 j = 0; j < maxFan; j++)
			{
				if (outCount + 3 > XBOX_MODEL_MAX_OUT_TRIS * 3) break;
				const s32 ix[3] = { 0, j + 1, j + 2 };
				for (s32 k = 0; k < 3; k++)
				{
					const s32 vi = poly.indices[ix[k]];
					if (vi < 0 || vi >= model->vertexCount) { outCount = 0; break; }
					TFE_RenderBackend::GpuTexVert& o = s_modelTriBuf[outCount++];
					o.x = s_modelVertsWS[vi].x;
					o.y = s_modelVertsWS[vi].y;
					o.z = s_modelVertsWS[vi].z;
					o.color = polyColor;
					if (textured)
					{
						o.u = fixedToF(poly.uv[ix[k]].x) * invTexW;
						o.v = fixedToF(poly.uv[ix[k]].y) * invTexH;
					}
					else
					{
						o.u = 0.0f; o.v = 0.0f;
					}
				}
			}

			const u32 tris = outCount / 3;
			if (tris == 0) continue;
			TFE_RenderBackend::gpuDrawTexturedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, gpuTex, s_modelTriBuf, tris);
		}

		s_objStats.drawn++;
	}

	// Phase 8: draw every visible sprite/frame object in `sector` as a
	// camera-facing billboard.
	static void xboxDrawSectorObjects(RSector* sector)
	{
		if (!sector->objectList || sector->objectCount <= 0) return;

		// Camera right vector matches the view-matrix's right basis row.
		// computeCameraTransform built it as (cos(-yaw), 0, sin(-yaw))
		// so the billboard expands ALONG that vector either side of the
		// object's posWS.
		f32 sy, cy;
		TFE_Jedi::sinCosFlt(-s_xboxLastYaw, &sy, &cy);
		const f32 rx = cy, rz = sy;
		const u32 sectorColor = ambientToColor(sector->ambient);

		SecObject** objIter = sector->objectList;
		for (s32 i = 0; i < sector->objectCount; objIter++)
		{
			SecObject* obj = *objIter;
			if (!obj) { s_objStats.nullObj++; continue; }
			i++;

			// Categorise.
			switch (obj->type)
			{
				case OBJ_TYPE_SPRITE: s_objStats.sprites++; break;
				case OBJ_TYPE_FRAME:  s_objStats.frames++;  break;
				case OBJ_TYPE_3D:
					s_objStats.models++;
					// Phase 14 step 3 - enqueue 3DO model for deferred
					// draw. Same shape as the sprite path: snapshot
					// portal frustum at insertion (unused for clipping,
					// kept for symmetry / Phase 15 use), draw at flush.
					// Rely on the wall z-test for occlusion - same
					// reasoning as sprites.
					if (s_xboxModelListCount < XBOX_MODEL_LIST_CAP)
					{
						XboxModelListEntry& me = s_xboxModelList[s_xboxModelListCount++];
						me.obj        = obj;
						me.color      = ambientToColor(sector->ambient);
						me.fullBright = (obj->flags & OBJ_FLAG_FULLBRIGHT) != 0;
						me.portalInfo = xboxPortalPlanes_snapshotBack();
					}
					continue;
				default:              s_objStats.others++;  continue;
			}

			// Resolve the WaxFrame for this object's current anim/view/frame.
			WaxFrame* frame = NULL;
			void*     waxBase = NULL;
			const bool isCorpse = (obj->entityFlags & ETFLAG_CORPSE) != 0;
			if (isCorpse) s_objStats.corpses++;

			if (obj->type == OBJ_TYPE_SPRITE)
			{
				if (!obj->wax) { s_objStats.noWax++; continue; }
				Wax* wax = obj->wax;

				// Anim slot. Try the configured anim, then fall back to
				// 0 (idle) for corpses/dead actors whose dead-anim slot
				// is unpopulated on some wax variants.
				WaxAnim* anim = WAX_AnimPtr(wax, obj->anim & 0x1f);
				if (!anim) anim = WAX_AnimPtr(wax, 0);
				if (!anim) { s_objStats.noAnim++; continue; }

				// 32-bucket view selection (matches RClassic_Float).
				const f32 dx = s_cameraPos.x - fixedToF(obj->posWS.x);
				const f32 dz = s_cameraPos.z - fixedToF(obj->posWS.z);
				const s32 ang = TFE_Jedi::vec2ToAngle(dx, dz);
				s32 angleDiff = ((ang - (s32)obj->yaw) >> 9) & 31;
				s32 viewIdx = 31 - angleDiff;

				WaxView* view = WAX_ViewPtr(wax, anim, viewIdx);
				if (!view) view = WAX_ViewPtr(wax, anim, 0);
				if (!view) { s_objStats.noView++; continue; }

				// Frame index can legally be 0..(anim->frameCount-1).
				// 0x1f mask is fine since DF caps anims at 32 frames.
				frame = WAX_FramePtr(wax, view, obj->frame & 0x1f);
				waxBase = wax;
			}
			else if (obj->type == OBJ_TYPE_FRAME && obj->fme)
			{
				frame = obj->fme;
				// FME cell is sized off the frame itself; cell->columnOffset
				// is stored as an absolute offset into the frame's buffer.
				waxBase = obj->fme;
			}
			if (!frame) { s_objStats.noFrame++; continue; }
			WaxCell* cell = WAX_CellPtr(waxBase, frame);
			if (!cell) { s_objStats.noCell++; continue; }

			u32 texW = 0, texH = 0;
			TFE_RenderBackend::GpuTextureHandle tex =
				xboxUploadWaxCell(cell, waxBase, &texW, &texH);
			if (!tex) { s_objStats.uploadFail++; continue; }

			const f32 px = fixedToF(obj->posWS.x);
			const f32 py = fixedToF(obj->posWS.y);
			const f32 pz = fixedToF(obj->posWS.z);

			// Quad sizing + anchor come from the FRAME, not the object.
			// obj->worldWidth/Height are collision sizes (often 0 for
			// pickups/projectiles); render size lives on the WaxFrame.
			// Per upstream sprite_drawFrame (rwallFloat.cpp:2225):
			//   x0_view = viewX - offsetX,   x1 = x0 + widthWS
			//   y0_view = viewY - (heightWS - offsetY)
			//   y1_view = y0 + heightWS = viewY + offsetY
			// In TFE -Y up world: posY - hWS + ofY is the upper Y
			// (head), posY + ofY is the lower Y (feet).
			const f32 wWS = fixedToF(frame->widthWS);
			const f32 hWS = fixedToF(frame->heightWS);
			const f32 ofX = fixedToF(frame->offsetX);
			const f32 ofY = fixedToF(frame->offsetY);
			if (wWS <= 0.0f || hWS <= 0.0f) { s_objStats.zeroSize++; continue; }

			// Cheap behind-camera cull (XZ-plane forward dot with the
			// view direction). Avoids submitting sprites the GPU would
			// reject after transform anyway, and stops draws against
			// extremely-close objects from blowing the near plane.
			const f32 dxC = px - s_cameraPos.x;
			const f32 dzC = pz - s_cameraPos.z;
			f32 fyaw_s, fyaw_c;
			TFE_Jedi::sinCosFlt(-s_xboxLastYaw, &fyaw_s, &fyaw_c);
			const f32 forwardDot = dxC * (-fyaw_s) + dzC * fyaw_c;
			if (forwardDot < 0.05f) { s_objStats.behindCam++; continue; }

			// World corners. Sprite extends along camera-right by wWS,
			// shifted by -ofX along right.
			const f32 sxL = px - ofX * rx;
			const f32 szL = pz - ofX * rz;
			const f32 sxR = sxL + wWS * rx;
			const f32 szR = szL + wWS * rz;
			const f32 yt  = py - hWS + ofY;
			const f32 yb  = py + ofY;

			// Phase 11: the P8 upload nearest-neighbour-scales the cell
			// up to a pow2 texture (the whole pow2 surface ends up
			// containing the full sprite, not a sub-rect with padding
			// as the earlier Phase 8 code assumed). UVs span the full
			// [0,1] range. Sampling cell->sizeX/texW etc. here was
			// clipping off the top portion of every sprite ("torso
			// down only" symptom).
			f32 uL = 0.0f, uR = 1.0f;
			const f32 vB = 1.0f;
			if (frame->flip) { const f32 t = uL; uL = uR; uR = t; }
			(void)texW; (void)texH;

			// Phase 14 step 2 - enqueue sprite for deferred draw.
			// Anchor = obj->posWS. Snapshot the current back frustum.
			// Visibility is decided by xboxFlushSpriteList using a
			// center-vs-frustum test (all-or-nothing - either the
			// whole sprite quad emits, or it doesn't). UV is fixed at
			// [0,1] - no per-vertex reconstruction needed since we
			// never clip the quad geometry.
			(void)uL; (void)uR; (void)vB;
			if (s_xboxSpriteListCount < XBOX_SPRITE_LIST_CAP)
			{
				XboxSpriteListEntry& e = s_xboxSpriteList[s_xboxSpriteListCount++];
				e.anchor.x   = px;  e.anchor.y   = py;  e.anchor.z   = pz;
				e.cornerBL.x = sxL; e.cornerBL.y = yb; e.cornerBL.z = szL;
				e.cornerTL.x = sxL; e.cornerTL.y = yt; e.cornerTL.z = szL;
				e.cornerTR.x = sxR; e.cornerTR.y = yt; e.cornerTR.z = szR;
				e.cornerBR.x = sxR; e.cornerBR.y = yb; e.cornerBR.z = szR;
				e.color      = sectorColor;
				e.tex        = tex;
				e.portalInfo = xboxPortalPlanes_snapshotBack();
				e.flipU      = (frame->flip != 0);
				if (isCorpse) s_objStats.corpsesDrawn++;
			}
		}
	}

	// Phase 10: wall signs (buttons, switches, door panels, level
	// number plates...). Each wall optionally has a signTex (TextureData**)
	// positioned at signOffset (in texels from the wall's w0 corner
	// horizontally, from the wall's top vertically). Drawn as a small
	// alpha-tested quad nudged a hair toward the room interior so it
	// sits cleanly on top of the wall texture instead of Z-fighting.
	static void xboxDrawWallSign(RWall* w, f32 x0, f32 z0, f32 x1, f32 z1,
	                              f32 ceilY, f32 floorY, u32 sectorColor)
	{
		if (!w->signTex || !*w->signTex) return;
		TextureData* tex = *w->signTex;
		if (!tex->image || tex->compressed != 0) return;
		// pow2 not required - backend upscales.
		if (w->midTexelHeight <= 0 || w->texelLength <= 0) return;

		// Sign U range along the wall (in texels along its length).
		// signOffset.x lives in the wall TEXTURE'S U coordinate space,
		// which is shifted by midOffset.x. Upstream's column check is
		// "uCoord >= signU0" where uCoord = uCoord0 + midOffset.x +
		// perspective(screenX). For a simple wall (uCoord0 = 0) the
		// screen position of the sign's left edge satisfies
		// perspective(X) = signOffset.x - midOffset.x, so the world
		// fraction along the wall is (signOffset.x - midOffset.x) /
		// texelLength. Without the midOffset.x subtraction the sign
		// drifts off the wall texture by midOffset.x texels.
		const f32 wallTexelLen = fixedToF(w->texelLength);
		const f32 midOfsTx     = fixedToF(w->midOffset.x);
		const f32 sigU0Tx      = fixedToF(w->signOffset.x) - midOfsTx;
		const f32 sigU1Tx      = sigU0Tx + (f32)tex->width;
		const f32 fracL = sigU0Tx / wallTexelLen;
		const f32 fracR = sigU1Tx / wallTexelLen;

		// Sign Y. Upstream rwallFloat.cpp:877 anchors the sign with its
		// BOTTOM at (floor_screen + signOffset.z / vCoordStep) and the
		// sign extends UPWARD by texHeight. signOffset.z is measured
		// from the FLOOR (positive going down on screen / down the wall
		// in texel space; typical values are negative so the bottom of
		// the sign sits above the floor in world space).
		//
		// World-space equivalent: texelsToWorld = (floorY - ceilY) /
		// midTexelHeight (positive because TFE -Y up means floorY >
		// ceilY). Sign bottom world Y = floorY + signOffset.z * scale
		// (so a negative offset moves it UP toward the ceiling). Sign
		// top world Y = sign bottom - texHeight * scale.
		const f32 wallTexelH    = fixedToF(w->midTexelHeight);
		const f32 wallWorldH    = floorY - ceilY;
		const f32 texelsToWorld = (wallTexelH > 0.001f) ? (wallWorldH / wallTexelH) : 0.0f;
		const f32 sigOffsetW    = fixedToF(w->signOffset.z) * texelsToWorld;
		const f32 sigTexHW      = (f32)tex->height * texelsToWorld;

		const f32 dx = x1 - x0, dz = z1 - z0;
		const f32 sxL = x0 + fracL * dx;
		const f32 szL = z0 + fracL * dz;
		const f32 sxR = x0 + fracR * dx;
		const f32 szR = z0 + fracR * dz;
		const f32 yB = floorY + sigOffsetW;   // sign bottom
		const f32 yT = yB - sigTexHW;         // sign top

		// Bias the sign 0.05 units along the wall normal toward the
		// room interior. Wall normal in XZ for CCW-wound DF sectors:
		// rotate the wall direction 90 degrees clockwise.
		const f32 wallLen = fixedToF(w->length);
		const f32 nx = (wallLen > 0.001f) ? ( dz / wallLen) : 0.0f;
		const f32 nz = (wallLen > 0.001f) ? (-dx / wallLen) : 0.0f;
		const f32 bias = 0.05f;
		const f32 bx = nx * bias, bz = nz * bias;

		TFE_RenderBackend::GpuTextureHandle gpuTex =
			TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
				tex, tex->image, tex->width, tex->height, /*columnMajor*/true);
		if (!gpuTex) return;

		TFE_RenderBackend::GpuTexVert v[6];
		// BL
		v[0].x = sxL + bx; v[0].y = yB; v[0].z = szL + bz;
		v[0].color = sectorColor; v[0].u = 0.0f; v[0].v = 1.0f;
		// TL
		v[1].x = sxL + bx; v[1].y = yT; v[1].z = szL + bz;
		v[1].color = sectorColor; v[1].u = 0.0f; v[1].v = 0.0f;
		// TR
		v[2].x = sxR + bx; v[2].y = yT; v[2].z = szR + bz;
		v[2].color = sectorColor; v[2].u = 1.0f; v[2].v = 0.0f;
		// BL
		v[3] = v[0];
		// TR
		v[4] = v[2];
		// BR
		v[5].x = sxR + bx; v[5].y = yB; v[5].z = szR + bz;
		v[5].color = sectorColor; v[5].u = 1.0f; v[5].v = 1.0f;

		TFE_RenderBackend::gpuDrawAlphaTestedTrisWorld(
			s_xboxViewMtx, s_xboxProjMtx, gpuTex, v, 2);
	}

	static void xboxDrawSectorWalls(RSector* sector)
	{
		const f32 floorY = fixedToF(sector->floorHeight);
		const f32 ceilY  = fixedToF(sector->ceilingHeight);
		const u32 col    = ambientToColor(sector->ambient);

		for (s32 i = 0; i < sector->wallCount; i++)
		{
			RWall* w = &sector->walls[i];
			if (!w->w0 || !w->w1) continue;

			const f32 x0 = fixedToF(w->w0->x);
			const f32 z0 = fixedToF(w->w0->z);
			const f32 x1 = fixedToF(w->w1->x);
			const f32 z1 = fixedToF(w->w1->z);

			if (!w->nextSector)
			{
				// Solid wall - one mid quad full floor-to-ceiling.
				xboxEmitWallQuad(sector->id, i, x0, z0, x1, z1,
					ceilY, floorY,
					w->midTex, w->texelLength, w->midTexelHeight,
					w->midOffset.z, col);
				xboxDrawWallSign(w, x0, z0, x1, z1, ceilY, floorY, col);
				continue;
			}

			// Phase 7: portal wall. Skip the middle (we look through),
			// but draw the top sliver if our ceiling sits higher than
			// the next sector's, and the bot sliver if our floor sits
			// lower. JEDI's WDF_TOP/WDF_BOT flags already encode this.
			RSector* next = w->nextSector;
			const f32 nextCeil  = fixedToF(next->ceilingHeight);
			const f32 nextFloor = fixedToF(next->floorHeight);

			if (w->drawFlags & WDF_TOP)
			{
				// Sliver from our ceiling (yTop=ceilY) down to next
				// sector's ceiling (yBot=nextCeil). In TFE -Y up:
				// nextCeil is numerically greater (lower) than ceilY.
				xboxEmitWallQuad(sector->id, i, x0, z0, x1, z1,
					ceilY, nextCeil,
					w->topTex, w->texelLength, w->topTexelHeight,
					w->topOffset.z, col);
			}
			if (w->drawFlags & WDF_BOT)
			{
				// Sliver from next sector's floor down to ours.
				xboxEmitWallQuad(sector->id, i, x0, z0, x1, z1,
					nextFloor, floorY,
					w->botTex, w->texelLength, w->botTexelHeight,
					w->botOffset.z, col);
			}

			// Sign overlay (door panels, switches, etc.) - draws on
			// adjoin walls too. signOffset.z is measured down from the
			// current sector's ceiling.
			xboxDrawWallSign(w, x0, z0, x1, z1, ceilY, floorY, col);
		}
	}

	void TFE_Sectors_GPU::destroy()             {}
	void TFE_Sectors_GPU::reset()               {}
	void TFE_Sectors_GPU::prepare()             {}
	// Phase 12.B/C - upstream-grounded recursive traversal.
	// Mirrors traverseSector / traverseScene in
	// TheForceEngine-ORIGINAL/.../RClassic_GPU/rsectorGPU.cpp.
	//
	// Walls + sprites still draw immediately (unclipped, same as Phase 11).
	// Flats are CPU-clipped against the current frustum at draw time (see
	// xboxDrawSectorFlat) - that's where the corridor z-fight gets fixed.
	//
	// Per-sector OBJECT draw is gated by sec->prevDrawFrame so sprites
	// don't render twice when a sector is visited through two portals.
	// (Walls/flats are still re-drawn per visit; their geometry is what
	// changes between visits because the frustum stack differs.)
	enum { XBOX_TRAVERSE_MAX_DEPTH = 32, XBOX_MAX_PORTAL_TRAVERSALS = 512 };
	static u32 s_xboxPortalsTraversed = 0;
	static u32 s_xboxVisitedThisFrame = 0;   // unique sectors (firstVisit count)
	static u32 s_xboxSectorDrawsThisFrame = 0; // total entries (incl. duplicates via multi-portal)

	static void xboxTraverseSector(RSector* sec, s32 depth)
	{
		if (!sec || sec->wallCount <= 0 || depth > XBOX_TRAVERSE_MAX_DEPTH) return;

		const bool firstVisit = (sec->prevDrawFrame != TFE_Jedi::s_drawFrame);
		sec->prevDrawFrame = TFE_Jedi::s_drawFrame;
		if (firstVisit) s_xboxVisitedThisFrame++;
		s_xboxSectorDrawsThisFrame++;

		xboxDrawSectorWalls(sec);
		if (!(sec->flags1 & SEC_FLAGS1_PIT))
			xboxDrawSectorFlat(sec, sec->floorHeight,   sec->floorTex, sec->floorOffset);
		if (!(sec->flags1 & SEC_FLAGS1_EXTERIOR))
			xboxDrawSectorFlat(sec, sec->ceilingHeight, sec->ceilTex,  sec->ceilOffset);
		// Objects draw per-visit (each visit enqueues with the portal-
		// frustum snapshot for its specific path; the display list
		// flushes per-object) - that's what Phase 14 needs to keep
		// flicker fixed.
		xboxDrawSectorObjects(sec);

		for (s32 i = 0; i < sec->wallCount; i++)
		{
			if (s_xboxPortalsTraversed >= XBOX_MAX_PORTAL_TRAVERSALS) break;
			RWall* w = &sec->walls[i];
			RSector* next = w->nextSector;
			if (!next || !w->w0 || !w->w1) continue;
			// Skip the portal we entered THIS sector through. Parent
			// stamps its wall before recursing; we skip when we see
			// the matching stamp.
			if (w->drawFrame == TFE_Jedi::s_drawFrame) continue;

			// Portal quad in world space - the OPEN vertical span shared
			// by both sectors. TFE -Y up: ceiling has the smaller Y, so
			// the more restrictive (lower) ceiling is MAX(ceilA,ceilB)
			// and the more restrictive floor is MIN(floorA,floorB).
			const fixed16_16 topFx = (sec->ceilingHeight > next->ceilingHeight)
			                       ? sec->ceilingHeight : next->ceilingHeight;
			const fixed16_16 botFx = (sec->floorHeight   < next->floorHeight)
			                       ? sec->floorHeight   : next->floorHeight;
			if (topFx >= botFx) continue;  // closed (degenerate)

			const f32 px0 = fixedToF(w->w0->x), pz0 = fixedToF(w->w0->z);
			const f32 px1 = fixedToF(w->w1->x), pz1 = fixedToF(w->w1->z);
			const f32 pyt = fixedToF(topFx);
			const f32 pyb = fixedToF(botFx);

			// CCW from camera looking through the portal from cur into next.
			Vec3f portalVerts[4];
			portalVerts[0].x = px0; portalVerts[0].y = pyb; portalVerts[0].z = pz0;
			portalVerts[1].x = px0; portalVerts[1].y = pyt; portalVerts[1].z = pz0;
			portalVerts[2].x = px1; portalVerts[2].y = pyt; portalVerts[2].z = pz1;
			portalVerts[3].x = px1; portalVerts[3].y = pyb; portalVerts[3].z = pz1;

			// Clip the portal against the current frustum so the child
			// frustum is the intersection of cur + raw-portal cone.
			XboxPolygon clippedPortal;
			const XboxFrustum* curF = xboxFrustum_getBack();
			if (curF && curF->planeCount > 0)
			{
				if (!xboxFrustum_clipPolyToPlanes(portalVerts, 4, curF->planes,
				                                  (s32)curF->planeCount, &clippedPortal)) continue;
			}
			else
			{
				clippedPortal.vertexCount = 4;
				for (s32 k = 0; k < 4; k++) clippedPortal.vtx[k] = portalVerts[k];
			}
			if (clippedPortal.vertexCount < 3) continue;

			XboxFrustum childF;
			xboxFrustum_buildFromPolygon(&clippedPortal, &childF);
			if (childF.planeCount == 0) continue;

			s_xboxPortalsTraversed++;
			const s32 savedDrawFrame = w->drawFrame;
			w->drawFrame = TFE_Jedi::s_drawFrame;
			xboxFrustum_push(&childF);
			xboxTraverseSector(next, depth + 1);
			xboxFrustum_pop();
			w->drawFrame = savedDrawFrame;
		}
	}

	// Phase 14 step 2b - sprite list flush.
	// Every enqueued sprite emits its full quad - no per-sprite portal
	// visibility test. The wall z-buffer already occludes parts of the
	// sprite behind level geometry, so the only reason to skip a sprite
	// would be "its sector isn't visible." But if it got enqueued, its
	// sector WAS visited (recursion reached it and ran objects). The
	// earlier center-vs-frustum test had FP variance for sprites sitting
	// near portal edges - same flicker root cause we're trying to fix.
	static void xboxFlushSpriteList()
	{
		for (u32 i = 0; i < s_xboxSpriteListCount; i++)
		{
			const XboxSpriteListEntry& e = s_xboxSpriteList[i];

			const f32 uL = e.flipU ? 1.0f : 0.0f;
			const f32 uR = e.flipU ? 0.0f : 1.0f;

			TFE_RenderBackend::GpuTexVert v[6];
			v[0].x = e.cornerBL.x; v[0].y = e.cornerBL.y; v[0].z = e.cornerBL.z;
			v[0].color = e.color; v[0].u = uL; v[0].v = 0.0f;
			v[1].x = e.cornerTL.x; v[1].y = e.cornerTL.y; v[1].z = e.cornerTL.z;
			v[1].color = e.color; v[1].u = uL; v[1].v = 1.0f;
			v[2].x = e.cornerTR.x; v[2].y = e.cornerTR.y; v[2].z = e.cornerTR.z;
			v[2].color = e.color; v[2].u = uR; v[2].v = 1.0f;
			v[3] = v[0];
			v[4] = v[2];
			v[5].x = e.cornerBR.x; v[5].y = e.cornerBR.y; v[5].z = e.cornerBR.z;
			v[5].color = e.color; v[5].u = uR; v[5].v = 0.0f;

			TFE_RenderBackend::gpuDrawAlphaTestedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, e.tex, v, 2);
			s_objStats.drawn++;
		}
	}

	// Phase 14 step 3 - 3DO model list flush.
	// Same shape as the sprite flush. The per-portal frustum snapshot
	// is captured but not used for clipping (wall z-test handles
	// occlusion). Color + fullBright were resolved at insertion time
	// from the sector the model was in then.
	static void xboxFlushModelList()
	{
		for (u32 i = 0; i < s_xboxModelListCount; i++)
		{
			const XboxModelListEntry& e = s_xboxModelList[i];
			xboxDrawModel(e.obj, e.color, e.fullBright);
		}
	}

	void TFE_Sectors_GPU::draw(RSector* startSector)
	{
		if (!startSector || startSector->wallCount <= 0) return;

		xboxObjStatsReset();
		s_objStatsFrame++;
		s_xboxPortalsTraversed = 0;
		s_xboxVisitedThisFrame = 0;
		s_xboxSectorDrawsThisFrame = 0;
		xboxFrustum_clearStack();
		xboxPortalPlanes_clear();
		xboxObjectLists_clear();
		xboxWallList_clear();

		xboxTraverseSector(startSector, 0);
		const u32 visited = s_xboxVisitedThisFrame;

		// Phase 15 - flush walls first (opaque world geometry), then
		// objects on top. Flats are still drawn immediately inside the
		// recursion because they need per-visit portal-frustum clipping
		// to fix the corridor z-fight (will move to a list with per-
		// entry clip in a follow-up).
		xboxFlushWallList();
		// Phase 14 step 2 + 3 - deferred sprite and model lists.
		xboxFlushSpriteList();
		xboxFlushModelList();

		// Accumulate per-frame perf counters for the dump window.
		xboxPerfTick(s_xboxPortalsTraversed, s_xboxVisitedThisFrame, s_xboxSectorDrawsThisFrame);

		// Per-frame object/cache stats. Dump every 120th draw frame
		// (~2s at 60Hz mission tick) so we have steady signal.
		if ((s_objStatsFrame % 120) == 1) xboxObjStatsDump();

		// One log line each time the player walks into a new starting
		// sector, plus how many sectors the traversal reached.
		static s32 s_lastStartSectorId = -1;
		if (startSector->id != s_lastStartSectorId)
		{
			s_lastStartSectorId = startSector->id;
			TFE_System::logWrite(LOG_MSG, "GPU",
				"entered sec=%d walls=%d visited=%u cam=(%d/100,%d/100,%d/100) yaw=%d",
				startSector->id, startSector->wallCount, visited,
				(s32)(s_cameraPos.x * 100.0f), (s32)(s_cameraPos.y * 100.0f), (s32)(s_cameraPos.z * 100.0f),
				(s32)s_xboxLastYaw);
		}
	}
	void TFE_Sectors_GPU::subrendererChanged()  {}
	void TFE_Sectors_GPU::flushCache()          {}
	void TFE_Sectors_GPU::flushTextureCache()   {}
	TextureGpu* TFE_Sectors_GPU::getColormap()  { return NULL; }

	// =====================================================================
	// getLevelScript / getLevelScriptFunc
	// (missing from the #else _XBOX block in level.cpp)
	// =====================================================================
	TFE_ForceScript::ModuleHandle getLevelScript()
	{
		return NULL;
	}

	TFE_ForceScript::FunctionHandle getLevelScriptFunc(const char*)
	{
		return NULL;
	}


}  // namespace TFE_Jedi

// =====================================================================
// Compiler runtime shims for MSVC 2005 targeting Xbox
// =====================================================================
extern "C"
{
	// __CxxFrameHandler3 is generated by MSVC 2005 but XDK CRT only has __CxxFrameHandler.
	// Redirect to __CxxFrameHandler which has the same signature.
	extern int __cdecl __CxxFrameHandler(void*, void*, void*, void*);
	int __cdecl __CxxFrameHandler3(void* a, void* b, void* c, void* d)
	{
		return __CxxFrameHandler(a, b, c, d);
	}

	// __ftol2_sse: MSVC 2005 float-to-long. XDK CRT has __ftol.
	// Provide a shim using x87 fistp.
	long __declspec(naked) _ftol2_sse(void)
	{
		__asm
		{
			push    ebp
			mov     ebp, esp
			sub     esp, 8
			fnstcw  word ptr [ebp-2]
			mov     ax, word ptr [ebp-2]
			or      ax, 0C00h
			mov     word ptr [ebp-4], ax
			fldcw   word ptr [ebp-4]
			fistp   qword ptr [ebp-12]
			fldcw   word ptr [ebp-2]
			mov     eax, dword ptr [ebp-12]
			leave
			ret
		}
	}

}

// Debug heap operator stubs for clipper.cpp debug STL usage
namespace std {
	struct _DebugHeapTag_t { int _Type; };
	const _DebugHeapTag_t _DebugHeapTag = { 0 };
}

void* __cdecl operator new(unsigned int sz, const std::_DebugHeapTag_t&, char*, int)
{ return operator new(sz); }

void __cdecl operator delete(void* p, const std::_DebugHeapTag_t&, char*, int)
{ operator delete(p); }

void* __cdecl operator new[](unsigned int sz, const std::_DebugHeapTag_t&, char*, int)
{ return operator new[](sz); }

// =====================================================================
// TFE_Settings stubs (loadCustomModSettings is inside #ifndef _XBOX)
// =====================================================================
namespace TFE_Settings
{
	void loadCustomModSettings() {}
}

// =====================================================================
// WindowsRegistry stubs
// registry.cpp has Xbox stubs under the wrong namespace (TFE_Registry
// instead of WindowsRegistry), so we provide correct ones here.
// =====================================================================
namespace WindowsRegistry
{
	bool getGogPathFromRegistry(const char*, const char*, char*) { return false; }
	bool getSteamPathFromRegistry(u32, const char*, const char*, const char*, char*) { return false; }
}

#endif // _XBOX
