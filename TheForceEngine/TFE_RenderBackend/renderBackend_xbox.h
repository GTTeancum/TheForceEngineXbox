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

    // Draw an array of colored triangles in world space.
    // viewMtx / projMtx are 16-float arrays in D3D row-major order (the
    // same layout as D3DMATRIX). World matrix is forced to identity.
    //
    // Must be called between bindVirtualDisplay (which opens the scene +
    // clears the back buffer) and swap (which closes the scene + presents).
    // Safe to call repeatedly per frame.
    void gpuDrawColoredTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                 const GpuColorVert* verts, u32 triCount);
}

#endif // _XBOX
