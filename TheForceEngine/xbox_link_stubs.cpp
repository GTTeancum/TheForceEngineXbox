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
#include <TFE_Jedi/Level/robjData.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
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

	// Phase 6: convert sector->ambient (fixed16_16 in [0, MAX_LIGHT_LEVEL=31])
	// into a 0xFFRRGGBB grayscale diffuse colour that MODULATEs the texel
	// at stage 0. Pure linear mapping for the first cut - DF's actual
	// light ramp is non-linear (colormap-based) but per-sector flat is
	// good enough to restore the moody dim/bright contrast.
	static inline u32 ambientToColor(fixed16_16 ambientFx)
	{
		s32 level = ambientFx >> 16;       // integer light level
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

		TFE_RenderBackend::gpuDrawTexturedTrisWorld(
			s_xboxViewMtx, s_xboxProjMtx, gpuTex, s_flatVerts, tris);
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
	                              u32 color)
	{
		// Phase 11: pow2 check dropped. The backend now upscales non-pow2
		// source textures to next-pow2 internally during palette expand.
		const bool texUsable =
			tex && tex->image && tex->compressed == 0;

		if (texUsable)
		{
			const f32 texelLen = fixedToF(texelLengthFx);
			const f32 texH     = fixedToF(texHeightFx);
			const f32 uMax = texelLen / (f32)tex->width;
			const f32 vMax = texH     / (f32)tex->height;

			TFE_RenderBackend::GpuTextureHandle gpuTex =
				TFE_RenderBackend::gpuGetOrUploadIndexedTexture(
					tex, tex->image, tex->width, tex->height, /*columnMajor*/true);

			TFE_RenderBackend::GpuTexVert tv[6];
			tv[0].x = x0; tv[0].y = yBot; tv[0].z = z0; tv[0].color = color; tv[0].u = 0.0f; tv[0].v = vMax;
			tv[1].x = x0; tv[1].y = yTop; tv[1].z = z0; tv[1].color = color; tv[1].u = 0.0f; tv[1].v = 0.0f;
			tv[2].x = x1; tv[2].y = yTop; tv[2].z = z1; tv[2].color = color; tv[2].u = uMax; tv[2].v = 0.0f;
			tv[3].x = x0; tv[3].y = yBot; tv[3].z = z0; tv[3].color = color; tv[3].u = 0.0f; tv[3].v = vMax;
			tv[4].x = x1; tv[4].y = yTop; tv[4].z = z1; tv[4].color = color; tv[4].u = uMax; tv[4].v = 0.0f;
			tv[5].x = x1; tv[5].y = yBot; tv[5].z = z1; tv[5].color = color; tv[5].u = uMax; tv[5].v = vMax;

			TFE_RenderBackend::gpuDrawTexturedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, gpuTex, tv, 2);
		}
		else
		{
			const u32 c = wallColor(wallIdx, secId);
			TFE_RenderBackend::GpuColorVert cv[6];
			cv[0].x = x0; cv[0].y = yBot; cv[0].z = z0; cv[0].color = c;
			cv[1].x = x0; cv[1].y = yTop; cv[1].z = z0; cv[1].color = c;
			cv[2].x = x1; cv[2].y = yTop; cv[2].z = z1; cv[2].color = c;
			cv[3].x = x0; cv[3].y = yBot; cv[3].z = z0; cv[3].color = c;
			cv[4].x = x1; cv[4].y = yTop; cv[4].z = z1; cv[4].color = c;
			cv[5].x = x1; cv[5].y = yBot; cv[5].z = z1; cv[5].color = c;

			TFE_RenderBackend::gpuDrawColoredTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, cv, 2);
		}
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

	// Phase 11 diagnostic counters - accumulate per draw frame, dump
	// when the totals change a lot (sector switch / interesting event)
	// or every N frames so we have steady signal without log spam.
	struct XboxObjStats
	{
		u32 sprites, frames, models, others;     // by type
		u32 nullObj, behindCam, noFrame, noCell; // skip reasons
		u32 zeroSize, uploadFail, drawn;
	};
	static XboxObjStats s_objStats;
	static u32 s_objStatsFrame = 0;

	static inline void xboxObjStatsReset() { memset(&s_objStats, 0, sizeof(s_objStats)); }

	static void xboxObjStatsDump()
	{
		TFE_System::logWrite(LOG_MSG, "GPU",
			"objects: spr=%u fme=%u mdl=%u oth=%u | drawn=%u skip{behindCam=%u noFrame=%u noCell=%u zero=%u up=%u null=%u}",
			s_objStats.sprites, s_objStats.frames, s_objStats.models, s_objStats.others,
			s_objStats.drawn, s_objStats.behindCam, s_objStats.noFrame, s_objStats.noCell,
			s_objStats.zeroSize, s_objStats.uploadFail, s_objStats.nullObj);
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

		for (s32 i = 0; i < sector->objectCount; i++)
		{
			SecObject* obj = sector->objectList[i];
			if (!obj) { s_objStats.nullObj++; continue; }

			// Categorise.
			switch (obj->type)
			{
				case OBJ_TYPE_SPRITE: s_objStats.sprites++; break;
				case OBJ_TYPE_FRAME:  s_objStats.frames++;  break;
				case OBJ_TYPE_3D:     s_objStats.models++;  continue;  // Phase 13
				default:              s_objStats.others++;  continue;
			}

			// Resolve the WaxFrame for this object's current anim/view/frame.
			WaxFrame* frame = NULL;
			void*     waxBase = NULL;
			if (obj->type == OBJ_TYPE_SPRITE && obj->wax)
			{
				Wax* wax = obj->wax;
				WaxAnim* anim = WAX_AnimPtr(wax, obj->anim & 0x1f);
				if (!anim) continue;

				// 32-bucket view selection (matches RClassic_Float).
				// Angle from object to camera in angle14 units; subtract
				// the object's own yaw to get a relative angle; shift down
				// by 9 (16384 / 512 = 32) and mask to wrap.
				const f32 dx = s_cameraPos.x - fixedToF(obj->posWS.x);
				const f32 dz = s_cameraPos.z - fixedToF(obj->posWS.z);
				const s32 ang = TFE_Jedi::vec2ToAngle(dx, dz);
				s32 angleDiff = ((ang - (s32)obj->yaw) >> 9) & 31;
				s32 viewIdx = 31 - angleDiff;

				// Many waxes only have a subset of the 32 view slots
				// populated (8-view sprites are common). Fall back to
				// view 0 if the picked slot is empty.
				WaxView* view = WAX_ViewPtr(wax, anim, viewIdx);
				if (!view) view = WAX_ViewPtr(wax, anim, 0);
				if (!view) { s_objStats.noFrame++; continue; }

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

			// WAX cells stored column bottom-up: bottom vertex v=0,
			// top vertex v=vB.
			TFE_RenderBackend::GpuTexVert v[6];
			v[0].x = sxL; v[0].y = yb; v[0].z = szL;
			v[0].color = sectorColor; v[0].u = uL; v[0].v = 0.0f;
			v[1].x = sxL; v[1].y = yt; v[1].z = szL;
			v[1].color = sectorColor; v[1].u = uL; v[1].v = vB;
			v[2].x = sxR; v[2].y = yt; v[2].z = szR;
			v[2].color = sectorColor; v[2].u = uR; v[2].v = vB;
			v[3] = v[0];
			v[4] = v[2];
			v[5].x = sxR; v[5].y = yb; v[5].z = szR;
			v[5].color = sectorColor; v[5].u = uR; v[5].v = 0.0f;

			TFE_RenderBackend::gpuDrawAlphaTestedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, tex, v, 2);
			s_objStats.drawn++;
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
		const f32 wallTexelLen = fixedToF(w->texelLength);
		const f32 sigU0Tx = fixedToF(w->signOffset.x);
		const f32 sigU1Tx = sigU0Tx + (f32)tex->width;
		const f32 fracL = sigU0Tx / wallTexelLen;
		const f32 fracR = sigU1Tx / wallTexelLen;

		// Sign Y range (in normalised wall fraction from top down).
		const f32 wallTexelH = fixedToF(w->midTexelHeight);
		const f32 sigV0Tx = fixedToF(w->signOffset.z);
		const f32 sigV1Tx = sigV0Tx + (f32)tex->height;
		const f32 fracT = sigV0Tx / wallTexelH;
		const f32 fracB = sigV1Tx / wallTexelH;

		// World positions of the sign quad corners.
		const f32 dx = x1 - x0, dz = z1 - z0;
		const f32 sxL = x0 + fracL * dx;
		const f32 szL = z0 + fracL * dz;
		const f32 sxR = x0 + fracR * dx;
		const f32 szR = z0 + fracR * dz;
		const f32 yT = ceilY + fracT * (floorY - ceilY);
		const f32 yB = ceilY + fracB * (floorY - ceilY);

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
					w->midTex, w->texelLength, w->midTexelHeight, col);
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
					w->topTex, w->texelLength, w->topTexelHeight, col);
			}
			if (w->drawFlags & WDF_BOT)
			{
				// Sliver from next sector's floor down to ours.
				xboxEmitWallQuad(sector->id, i, x0, z0, x1, z1,
					nextFloor, floorY,
					w->botTex, w->texelLength, w->botTexelHeight, col);
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

		xboxObjStatsReset();
		s_objStatsFrame++;

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
			xboxDrawSectorObjects(sec);
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
