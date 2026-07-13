// Xbox-only D3D8 helpers exposed to higher-level renderer code.
// Anything in this header is meaningful only on the Xbox build; PC builds
// reach this code through the proper TFE_RenderBackend abstraction.
//
// Phase 2 of the RClassic_GPU/D3D8 port adds gpuDrawColoredTrisWorld so
// the JEDI sector renderer can submit world-space geometry without pulling
// d3d8.h into a JEDI-namespace file.
#pragma once
#ifdef _XBOX

#include <TFE_System/types.h>

namespace TFE_RenderBackend
{
    struct XboxLoadSlotInfo
    {
        bool valid;
        bool autosave;
        const char* fileName;
        const char* saveName;
        const char* dateTime;
        const char* levelName;
        const char* levelId;
        const u32* imageData;
    };

    struct XboxMissionCompleteInfo
    {
        u32 seconds;
        s32 secretsFound;
        s32 secretsTotal;
        s32 difficulty;
    };

    struct XboxOptionsItem
    {
        const char* label;
        const char* valueText;
        s32 value;
        s32 minValue;
        s32 maxValue;
        bool capture;
        bool hasIcon;
        s32 valueIcon;
    };

    struct XboxCheatItem
    {
        const char* label;
        bool enabled;
    };

    struct XboxWeaponWheelInfo
    {
        s32 selected;
        s32 current;
        bool available[10];
        const u32* icons[10];
        s32 iconWidth;
        s32 iconHeight;
        const char* selectedName;
        const char* selectedAmmo;
    };

    struct XboxModInfo
    {
        bool valid;
        const char* title;
        const char* author;
        const char* version;
        const char* description;
        s32 missionCount;
        bool hasQuickSave;
        const u32* imageData;
    };

    // Single-vertex layout for untextured colored geometry. Matches
    // D3DFVF_XYZ | D3DFVF_DIFFUSE on the wire.
    struct GpuColorVert
    {
        f32 x, y, z;
        u32 color;   // D3DCOLOR (0xAARRGGBB)
    };

    // Single-vertex layout for textured + per-vertex-colored geometry.
    // Matches D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1. The diffuse
    // colour MODULATEs the sampled texel at stage 0 - Phase 6 uses this
    // for per-sector ambient lighting (every wall/floor/ceiling vert of
    // a sector gets the same grayscale diffuse derived from
    // sector->ambient). UVs are normalised [0,1] with WRAP addressing.
    struct GpuTexVert
    {
        f32 x, y, z;
        u32 color;   // D3DCOLOR, 0xFFFFFFFF for fullbright
        f32 u, v;
    };

    // Opaque texture handle. Internally an IDirect3DTexture8* in
    // renderBackend_xbox.cpp; callers in JEDI namespace files use the
    // typedef so they don't have to pull d3d8.h into their includes.
    typedef void* GpuTextureHandle;

    // Draw an array of colored triangles in world space.
    // viewMtx / projMtx are 16-float arrays in D3D row-major order (the
    // same layout as D3DMATRIX). World matrix is forced to identity.
    //
    // Must be called between bindVirtualDisplay (which opens the scene +
    // clears the back buffer) and swap (which closes the scene + presents).
    // Safe to call repeatedly per frame.
    void gpuDrawColoredTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                 const GpuColorVert* verts, u32 triCount);

    // Texture-cached upload of a JEDI 8-bit indexed bitmap. `key` is
    // the TextureData*; subsequent calls with the same key return the
    // cached handle without re-uploading. `indexed` is the source 8-bit
    // pixel array; if `columnMajor` is true (DF BM format) the data is
    // transposed during upload. Width and height MUST be powers of two
    // (NV2A linear-format requirement). Returns NULL on cache full /
    // CreateTexture failure - caller should fall back to a colored draw.
    GpuTextureHandle gpuGetOrUploadIndexedTexture(const void* key,
                                                  const u8* indexed,
                                                  u32 width, u32 height,
                                                  bool columnMajor);

    // Drop every cached texture. Called on palette/level/backend lifetime
    // changes so stale TextureData keys never retain D3D resources.
    void gpuInvalidateTextureCache();

    // Draw textured triangles. Same matrix conventions as the colored
    // variant. `tex` may be NULL - the draw then falls back to
    // untextured white (visible but obviously wrong, useful for
    // diagnosing missing textures).
    void gpuDrawTexturedTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                  GpuTextureHandle tex,
                                  const GpuTexVert* verts, u32 triCount);

    // Draw textured triangles with alpha test (alpha < 1 -> discard).
    // For sprite billboards where palette-index-0 pixels were uploaded
    // with alpha = 0. Z-test on, Z-write on (so sprites occlude each
    // other correctly; transparent pixels still don't write Z because
    // they're discarded before the depth stage).
    void gpuDrawAlphaTestedTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                     GpuTextureHandle tex,
                                     const GpuTexVert* verts, u32 triCount);

    // Look up the current palette entry as a D3DCOLOR (0xAARRGGBB).
    // Used by the 3DO model path for PSHADE_FLAT polygons which carry
    // only a palette index (no texture). Alpha is forced to 0xFF.
    u32 gpuPaletteEntryRGBA(u8 index);

    // -----------------------------------------------------------------------
    // 2D screen-space draw - used by the escape menu / agent menu / PDA /
    // any RClassic_GPU UI overlay (upstream screenDrawGPU.cpp). Coordinates
    // are in virtual-display pixels: (0,0) top-left, (vdispW,vdispH)
    // bottom-right. The backend scales to the actual back buffer.
    //
    // - For DELT/BM sprites uploaded via gpuGetOrUploadIndexedTexture,
    //   pass alphaTest=true so palette-index-0 (transparent) is discarded.
    // - For render-target textures captured via copyBackbufferToRenderTarget,
    //   pass alphaTest=false (every texel is opaque).
    // - The UV rect is normalised [0,1]; pad to the next power-of-two if
    //   the source isn't pow2 and pass uMax = origW/padW etc. to crop.
    // - Must be called between bindVirtualDisplay (BeginScene) and swap
    //   (EndScene + Present).
    // - topColor / botColor are D3DCOLOR per-vertex DIFFUSE values that
    //   MODULATE the texel (default white = identity). Matches upstream
    //   quadDraw2d_add's per-edge color (top edge takes colors[0], bottom
    //   edge takes colors[1]).
    void gpuDrawScreenQuad(f32 x0, f32 y0, f32 x1, f32 y1,
                           f32 u0, f32 v0, f32 u1, f32 v1,
                           u32 vdispW, u32 vdispH,
                           GpuTextureHandle tex, bool alphaTest,
                           u32 topColor = 0xFFFFFFFFu,
                           u32 botColor = 0xFFFFFFFFu);

    void xboxSetPauseOverlay(bool enabled, s32 selection, s32 confirmSelection, bool confirmOpen, s32 notice = 0);
    void xboxSetBriefingFooter(bool enabled, bool objectivesPrompt, s32 difficulty);
    void xboxSetStartScreen(bool enabled, s32 selection, u32 frame);
    void xboxSetLoadScreen(bool enabled, s32 selection, u32 frame, const XboxLoadSlotInfo* slots, s32 slotCount);
    void xboxSetModScreen(bool enabled, s32 selection, u32 frame, const XboxModInfo* mods, s32 modCount);
    void xboxSetOptionsScreen(bool enabled, bool pauseStyle, const char* title, s32 selection, s32 scroll, u32 frame, const XboxOptionsItem* items, s32 itemCount);
    inline void xboxSetOptionsScreen(bool enabled, bool pauseStyle, s32 selection, s32 scroll, u32 frame, const XboxOptionsItem* items, s32 itemCount)
    {
        xboxSetOptionsScreen(enabled, pauseStyle, "OPTIONS", selection, scroll, frame, items, itemCount);
    }
    void xboxSetSafeZone(s32 widthPercent, s32 heightPercent, s32 offsetX, s32 offsetY);
    void xboxSetCheatScreen(bool enabled, s32 selection, s32 scroll, const XboxCheatItem* items, s32 itemCount);
    void xboxSetPdaOverlay(bool enabled, s32 mode, s32 layer = 0);
    void xboxSetMissionCompleteScreen(bool enabled, s32 selection, u32 frame, const XboxMissionCompleteInfo* info);
    void xboxSetWeaponWheel(bool enabled, const XboxWeaponWheelInfo* info);
}

#endif // _XBOX
