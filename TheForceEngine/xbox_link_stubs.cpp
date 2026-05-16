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
		const u32 col   = ambientToColor(sector->ambient);

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

			out[0].x = x0; out[0].y = y; out[0].z = z0; out[0].color = col;
			out[0].u = (x0 + ox) * uMul; out[0].v = (z0 + oz) * vMul;
			out[1].x = xa; out[1].y = y; out[1].z = za; out[1].color = col;
			out[1].u = (xa + ox) * uMul; out[1].v = (za + oz) * vMul;
			out[2].x = xb; out[2].y = y; out[2].z = zb; out[2].color = col;
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
		const bool texUsable =
			tex && tex->image && tex->compressed == 0 &&
			isPow2(tex->width) && isPow2(tex->height);

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

	static TFE_RenderBackend::GpuTextureHandle
	xboxUploadWaxCell(WaxCell* cell, const void* waxBase, u32* outTexW, u32* outTexH)
	{
		if (!cell || cell->sizeX <= 0 || cell->sizeY <= 0) return NULL;

		const u32 cellW = (u32)cell->sizeX;
		const u32 cellH = (u32)cell->sizeY;
		const u32 texW  = nextPow2(cellW);
		const u32 texH  = nextPow2(cellH);
		if (texW * texH > XBOX_MAX_CELL_PIXELS) return NULL;

		// Build the linear RGBA buffer. Clear to zero (alpha=0) first so
		// the pow2 padding is invisible after the alpha test.
		memset(s_cellStage, 0, texW * texH * sizeof(u32));

		const u32* pal = TFE_RenderBackend::getPalette();
		const u8*  imageData = (const u8*)cell + sizeof(WaxCell);
		const u32* columnOffset = (const u32*)((const u8*)waxBase + cell->columnOffset);
		const u8*  imageStart = (cell->compressed == 1)
		                      ? imageData + (cell->sizeX * sizeof(u32))
		                      : imageData;

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
			{
				const u8 idx = column[y];
				if (idx == 0) continue;  // transparent
				// pal[] is s_paletteCpu, ALREADY in 0xAARRGGBB layout
				// (renderBackend::setPalette swaps R/B at receive time
				// for D3D's A8R8G8B8 format). Don't re-swap; just copy
				// the 24-bit colour and force alpha to 0xFF.
				s_cellStage[y * texW + x] = (pal[idx] & 0x00FFFFFFu) | 0xFF000000u;
			}
		}

		if (outTexW) *outTexW = texW;
		if (outTexH) *outTexH = texH;
		return TFE_RenderBackend::gpuGetOrUploadRgbaTexture(
			cell, s_cellStage, texW, texH);
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
			if (!obj) continue;
			if (obj->type != OBJ_TYPE_SPRITE && obj->type != OBJ_TYPE_FRAME) continue;

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
				if (!view) continue;

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
			if (!frame) continue;
			WaxCell* cell = WAX_CellPtr(waxBase, frame);
			if (!cell) continue;

			u32 texW = 0, texH = 0;
			TFE_RenderBackend::GpuTextureHandle tex =
				xboxUploadWaxCell(cell, waxBase, &texW, &texH);
			if (!tex) continue;

			const f32 px = fixedToF(obj->posWS.x);
			const f32 py = fixedToF(obj->posWS.y);
			const f32 pz = fixedToF(obj->posWS.z);
			const f32 wHalf = fixedToF(obj->worldWidth) * 0.5f;
			const f32 hFull = fixedToF(obj->worldHeight);

			// In TFE -Y up: sprite bottom sits at posWS.y, top is
			// posWS.y - hFull (smaller y = higher up).
			const f32 yb = py;
			const f32 yt = py - hFull;

			// CLAMP UVs - cell occupies the [0, cellW/texW]x[0, cellH/texH]
			// sub-rect of the pow2 texture; padding lies outside that.
			const f32 uMax = (f32)cell->sizeX / (f32)texW;
			const f32 vMax = (f32)cell->sizeY / (f32)texH;

			// WAX cells store column data bottom-up - column[0] is the
			// sprite's feet, column[cellH-1] is the head. So the bottom
			// vertex (feet) gets v=0 and the top vertex (head) gets
			// v=vMax. Reversed from BM wall textures which are top-down.
			TFE_RenderBackend::GpuTexVert v[6];
			// Bottom-left (feet):
			v[0].x = px - wHalf * rx; v[0].y = yb; v[0].z = pz - wHalf * rz;
			v[0].color = sectorColor; v[0].u = 0.0f; v[0].v = 0.0f;
			// Top-left (head):
			v[1].x = px - wHalf * rx; v[1].y = yt; v[1].z = pz - wHalf * rz;
			v[1].color = sectorColor; v[1].u = 0.0f; v[1].v = vMax;
			// Top-right (head):
			v[2].x = px + wHalf * rx; v[2].y = yt; v[2].z = pz + wHalf * rz;
			v[2].color = sectorColor; v[2].u = uMax; v[2].v = vMax;
			// Bottom-left (feet):
			v[3] = v[0];
			// Top-right (head):
			v[4] = v[2];
			// Bottom-right (feet):
			v[5].x = px + wHalf * rx; v[5].y = yb; v[5].z = pz + wHalf * rz;
			v[5].color = sectorColor; v[5].u = uMax; v[5].v = 0.0f;

			TFE_RenderBackend::gpuDrawAlphaTestedTrisWorld(
				s_xboxViewMtx, s_xboxProjMtx, tex, v, 2);
		}
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
