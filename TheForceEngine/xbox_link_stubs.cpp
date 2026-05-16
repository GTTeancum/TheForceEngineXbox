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
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Renderer/textureInfo.h>
#include <TFE_Jedi/Renderer/rsectorRender.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rclassicGPU.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rsectorGPU.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Renderer/rcommon.h>
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
	// screenDraw wrapper stubs — screenDraw.cpp has these but they call
	// screenGPU_* which may cause link issues in Release. Provide explicit
	// stubs only for overloads that the linker can't find.
	// NOTE: screenDraw.cpp IS compiled but some callers use overloads with
	// different default-argument signatures that produce different manglings.
	void screen_clear() { }
	void screen_enableGPU(bool) { }
	void screenDraw_beginLines(u32, u32) { }
	void screenDraw_endLines() { }
	void screenDraw_beginQuads(u32, u32) { }
	void screenDraw_endQuads() { }
	void screenDraw_setTransColor(u8) { }
	void blitTextureToScreen(ScreenImage*, DrawRect*, s32, s32, u8*) {}
	void blitTextureToScreenScaled(ScreenImage*, DrawRect*, s32, s32, s32, s32, u8*) {}
	void blitTextureToScreen(TextureData*, DrawRect*, s32, s32, u8*, u32, u32) {}
	void blitTextureToScreenScaled(TextureData*, DrawRect*, s32, s32, s32, s32, u8*, u32) {}
	void blitTextureToScreenScaledText(TextureData*, DrawRect*, s32, s32, s32, s32, u8*, u32) {}
	void blitTextureToScreenLitScaled(TextureData*, DrawRect*, s32, s32, s32, s32, const u8*, u8*, u32) {}

	// screenDrawGPU.h functions
	void screenGPU_init()    {}
	void screenGPU_destroy() {}

	void screenGPU_beginLines(u32, u32)      {}
	void screenGPU_endLines()                {}
	void screenGPU_beginImageQuads(u32, u32) {}
	void screenGPU_endImageQuads()           {}
	void screenGPU_beginQuads(u32, u32)      {}
	void screenGPU_endQuads()                {}

	void screenGPU_setIndexedColors(u32, const Vec4f*) {}
	void screenGPU_drawColoredQuad(fixed16_16, fixed16_16, fixed16_16, fixed16_16, u8) {}

	void screenGPU_addImageQuad(s32, s32, s32, s32, TextureGpu*) {}
	void screenGPU_addImageQuad(s32, s32, s32, s32, f32, f32, TextureGpu*) {}

	void screenGPU_setHudTextureCallbacks(s32, TextureListCallback*, bool) {}

	void screenGPU_drawPoint(ScreenRect*, s32, s32, u8) {}
	void screenGPU_drawLine(ScreenRect*, s32, s32, s32, s32, u8) {}

	void screenGPU_blitTexture(TextureData*, DrawRect*, s32, s32, JBool, JBool) {}
	void screenGPU_blitTextureLit(TextureData*, DrawRect*, s32, s32, u8, JBool) {}
	void screenGPU_blitTextureScaled(TextureData*, DrawRect*, fixed16_16, fixed16_16, fixed16_16, fixed16_16, u8, JBool) {}

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

	// Phase 5: floor + ceiling polygons.
	//
	// Each sector is a closed 2D polygon in XZ (vertices defined by
	// walking the wall list, taking each wall's w0 in order). Floor and
	// ceiling are two horizontal triangle fans pinned to the sector's
	// floorHeight / ceilingHeight. UVs come from world XZ - DF tiles one
	// full texture every (texWidth/8) world units.
	//
	// Fan triangulation from vertex 0 is correct for convex sectors and
	// produces overlapping triangles for concave ones - with CULLNONE +
	// ZWRITE the visible result is still right (overlapping tris paint
	// over each other at the same Y so they Z-tie and the last one wins,
	// which is the same pixel value anyway). Ear-clip can replace this
	// later if specific sectors look bad.
	static const u32 XBOX_MAX_FLAT_VERTS = 512;
	static TFE_RenderBackend::GpuTexVert s_flatVerts[XBOX_MAX_FLAT_VERTS];

	static void xboxDrawSectorFlat(RSector* sector, fixed16_16 heightFx,
	                               TextureData** texPtr, vec2_fixed offset)
	{
		if (!texPtr || !*texPtr) return;
		TextureData* tex = *texPtr;
		if (!tex->image || tex->compressed != 0) return;
		if (!isPow2(tex->width) || !isPow2(tex->height)) return;
		if (sector->wallCount < 3) return;

		const s32 n = sector->wallCount;
		const u32 triCount = (u32)(n - 2);
		if (triCount * 3 > XBOX_MAX_FLAT_VERTS) return;

		const f32 y     = fixedToF(heightFx);
		const f32 ox    = fixedToF(offset.x);
		const f32 oz    = fixedToF(offset.z);
		const f32 uMul  = 8.0f / (f32)tex->width;
		const f32 vMul  = 8.0f / (f32)tex->height;

		// Pin vertex of the fan = walls[0].w0.
		vec2_fixed* v0 = sector->walls[0].w0;
		if (!v0) return;
		const f32 x0 = fixedToF(v0->x), z0 = fixedToF(v0->z);

		TFE_RenderBackend::GpuTexVert* out = s_flatVerts;
		for (s32 i = 1; i <= n - 2; i++)
		{
			vec2_fixed* vA = sector->walls[i    ].w0;
			vec2_fixed* vB = sector->walls[i + 1].w0;
			if (!vA || !vB) continue;
			const f32 xa = fixedToF(vA->x), za = fixedToF(vA->z);
			const f32 xb = fixedToF(vB->x), zb = fixedToF(vB->z);

			out[0].x = x0; out[0].y = y; out[0].z = z0;
			out[0].u = (x0 + ox) * uMul; out[0].v = (z0 + oz) * vMul;
			out[1].x = xa; out[1].y = y; out[1].z = za;
			out[1].u = (xa + ox) * uMul; out[1].v = (za + oz) * vMul;
			out[2].x = xb; out[2].y = y; out[2].z = zb;
			out[2].u = (xb + ox) * uMul; out[2].v = (zb + oz) * vMul;
			out += 3;
		}

		const u32 actualTris = (u32)(out - s_flatVerts) / 3;
		if (actualTris == 0) return;

		TFE_RenderBackend::GpuTextureHandle gpuTex =
			TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
				tex, tex->image, tex->width, tex->height, /*columnMajor*/true);

		TFE_RenderBackend::gpuDrawTexturedTrisWorld(
			s_xboxViewMtx, s_xboxProjMtx, gpuTex, s_flatVerts, actualTris);
	}

	// Draw every solid wall (no nextSector) of `sector`. Adjoin walls
	// (nextSector != NULL) are skipped so the player can see through
	// them into the next sector. The traversal in draw() pushes those
	// next sectors onto the visit queue.
	static void xboxDrawSectorWalls(RSector* sector)
	{
		const f32 floorY = fixedToF(sector->floorHeight);
		const f32 ceilY  = fixedToF(sector->ceilingHeight);
		const f32 yt = ceilY;
		const f32 yb = floorY;

		TFE_RenderBackend::GpuTexVert   tv[6];
		TFE_RenderBackend::GpuColorVert cv[6];

		for (s32 i = 0; i < sector->wallCount; i++)
		{
			RWall* w = &sector->walls[i];
			if (!w->w0 || !w->w1) continue;
			if (w->nextSector) continue;     // Phase 4: skip portal walls.

			const f32 x0 = fixedToF(w->w0->x);
			const f32 z0 = fixedToF(w->w0->z);
			const f32 x1 = fixedToF(w->w1->x);
			const f32 z1 = fixedToF(w->w1->z);

			TextureData* tex = w->midTex;
			const bool texUsable =
				tex && tex->image && tex->compressed == 0 &&
				isPow2(tex->width) && isPow2(tex->height);

			if (texUsable)
			{
				const f32 texelLen = fixedToF(w->texelLength);
				const f32 midHt    = fixedToF(w->midTexelHeight);
				const f32 uMax = texelLen / (f32)tex->width;
				const f32 vMax = midHt    / (f32)tex->height;

				TFE_RenderBackend::GpuTextureHandle gpuTex =
					TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
						tex, tex->image, tex->width, tex->height, /*columnMajor*/true);

				tv[0].x = x0; tv[0].y = yb; tv[0].z = z0; tv[0].u = 0.0f; tv[0].v = vMax;
				tv[1].x = x0; tv[1].y = yt; tv[1].z = z0; tv[1].u = 0.0f; tv[1].v = 0.0f;
				tv[2].x = x1; tv[2].y = yt; tv[2].z = z1; tv[2].u = uMax; tv[2].v = 0.0f;
				tv[3].x = x0; tv[3].y = yb; tv[3].z = z0; tv[3].u = 0.0f; tv[3].v = vMax;
				tv[4].x = x1; tv[4].y = yt; tv[4].z = z1; tv[4].u = uMax; tv[4].v = 0.0f;
				tv[5].x = x1; tv[5].y = yb; tv[5].z = z1; tv[5].u = uMax; tv[5].v = vMax;

				TFE_RenderBackend::gpuDrawTexturedTrisWorld(
					s_xboxViewMtx, s_xboxProjMtx, gpuTex, tv, 2);
			}
			else
			{
				const u32 c = wallColor(i, sector->id);
				cv[0].x = x0; cv[0].y = yb; cv[0].z = z0; cv[0].color = c;
				cv[1].x = x0; cv[1].y = yt; cv[1].z = z0; cv[1].color = c;
				cv[2].x = x1; cv[2].y = yt; cv[2].z = z1; cv[2].color = c;
				cv[3].x = x0; cv[3].y = yb; cv[3].z = z0; cv[3].color = c;
				cv[4].x = x1; cv[4].y = yt; cv[4].z = z1; cv[4].color = c;
				cv[5].x = x1; cv[5].y = yb; cv[5].z = z1; cv[5].color = c;

				TFE_RenderBackend::gpuDrawColoredTrisWorld(
					s_xboxViewMtx, s_xboxProjMtx, cv, 2);
			}
		}
	}

	void TFE_Sectors_GPU::destroy()             {}
	void TFE_Sectors_GPU::reset()               {}
	void TFE_Sectors_GPU::prepare()             {}
	void TFE_Sectors_GPU::draw(RSector* startSector)
	{
		if (!startSector || startSector->wallCount <= 0) return;

		// Phase 4: portal traversal.
		// BFS from the player's current sector. For each visited sector,
		// draw its solid walls (no nextSector); each adjoin wall queues
		// its nextSector for later visit. Sectors are visit-stamped with
		// the global s_drawFrame counter (incremented in jediRenderer
		// drawWorld right before us) so each sector is drawn at most
		// once per frame.
		//
		// No frustum culling, no portal clipping - the GPU's clipper +
		// Z-test handle visibility. DF levels have <500 sectors total
		// and we'll visit at most a connected subgraph reachable through
		// open adjoins. SECBASE traversal from spawn caps around 40-50
		// sectors; the 512-entry visit queue is comfortable headroom.
		enum { XBOX_VISIT_CAP = 512 };
		static RSector* s_visitQueue[XBOX_VISIT_CAP];
		u32 head = 0, tail = 0;

		s_visitQueue[tail++] = startSector;
		startSector->prevDrawFrame = TFE_Jedi::s_drawFrame;

		u32 visited = 0;
		while (head < tail)
		{
			RSector* sec = s_visitQueue[head++];
			if (!sec || sec->wallCount <= 0) continue;

			xboxDrawSectorWalls(sec);
			xboxDrawSectorFlat(sec, sec->floorHeight,   sec->floorTex, sec->floorOffset);
			xboxDrawSectorFlat(sec, sec->ceilingHeight, sec->ceilTex,  sec->ceilOffset);
			visited++;

			// Queue every neighbour reachable through an adjoin we
			// haven't already stamped this frame.
			for (s32 i = 0; i < sec->wallCount; i++)
			{
				RWall* w = &sec->walls[i];
				RSector* next = w->nextSector;
				if (!next) continue;
				if (next->prevDrawFrame == TFE_Jedi::s_drawFrame) continue;
				if (tail >= XBOX_VISIT_CAP) break;

				next->prevDrawFrame = TFE_Jedi::s_drawFrame;
				s_visitQueue[tail++] = next;
			}
		}

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
// Landru system stubs (lsystem.cpp not in Xbox build)
// =====================================================================
namespace TFE_DarkForces
{
	MemoryRegion* s_alloc = NULL;

	void lsystem_init()    {}
	void lsystem_destroy() {}
	void lsystem_setAllocator(LAllocator)   {}
	void lsystem_clearAllocator(LAllocator) {}
}

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
