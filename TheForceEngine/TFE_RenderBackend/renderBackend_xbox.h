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

    // Drop every cached texture. Called on palette change so the next
    // draw re-uploads against the new palette. Phase 3 brute-force
    // invalidation - Phase 4+ may track palette versions per texture.
    void gpuInvalidateTextureCache();

    // Draw textured triangles. Same matrix conventions as the colored
    // variant. `tex` may be NULL - the draw then falls back to
    // untextured white (visible but obviously wrong, useful for
    // diagnosing missing textures).
    void gpuDrawTexturedTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                  GpuTextureHandle tex,
                                  const GpuTexVert* verts, u32 triCount);
}

#endif // _XBOX
