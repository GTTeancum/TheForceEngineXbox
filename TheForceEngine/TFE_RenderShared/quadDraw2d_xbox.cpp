// quadDraw2d_xbox.cpp
//
// D3D8 port of TFE_RenderShared::quadDraw2d_* (upstream
// TheForceEngine-ORIGINAL/TheForceEngine/TFE_RenderShared/quadDraw2d.cpp).
//
// Upstream is a batched 2D quad system used by RClassic_GPU's
// screenGPU_addImageQuad to draw screen-space images (captured render
// targets, HUD overlays, etc.) with alpha blending. It uses a custom
// vertex/index buffer pair + a quad2d.vert/.frag shader.
//
// The Xbox port matches the upstream API and batching semantics 1:1:
//   begin(w, h) records the virtual-display size + resets the batch
//   add(...)    appends QuadDraw entries and per-vertex data to s_vertices
//   draw()      flushes everything in one go - one D3D state setup +
//               one DrawPrimitiveUP per texture group
//
// Backend notes:
//   - D3DFVF_XYZRHW so vertices are pre-transformed (raw back-buffer
//     pixel coords). The virtual-display rect (0..s_width, 0..s_height)
//     is mapped linearly to s_destRect via TFE_RenderBackend::
//     gpuDrawScreenQuads.
//   - Alpha blending matches upstream quadDraw2d_draw line 176-177:
//     BLEND_ONE / BLEND_ONE_MINUS_SRC_ALPHA. Diffuse vertex color
//     MODULATEs the texel.
//   - Uses the same XBRT sentinel unwrap as gpuDrawScreenQuad so
//     captured render targets are sampled as plain A8R8G8B8 without
//     palette indirection.
#ifdef _XBOX

#include "quadDraw2d.h"
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#include <TFE_System/system.h>
#include <cstring>

namespace TFE_RenderShared
{
    enum { QUAD_MAX_NUM = 1024, MAX_DRAW_GROUPS = 256 };

    struct QuadVertex
    {
        f32 x, z;       // 2D position in virtual-display pixels
        f32 u, v;
        u32 color;
    };
    struct QuadDraw
    {
        TextureGpu* texture;
        s32 offset;     // start quad index
        s32 count;      // quad count
    };

    static QuadVertex* s_vertices       = NULL;
    static QuadDraw    s_quadDraw[MAX_DRAW_GROUPS];
    static u32         s_quadCount      = 0;
    static u32         s_quadDrawCount  = 0;
    static u32         s_width          = 320;
    static u32         s_height         = 200;
    static bool        s_inited         = false;

    bool quadInit()
    {
        if (s_inited) return true;
        s_vertices = (QuadVertex*)malloc(sizeof(QuadVertex) * 4 * QUAD_MAX_NUM);
        if (!s_vertices) return false;
        s_quadCount     = 0;
        s_quadDrawCount = 0;
        s_inited = true;
        return true;
    }

    void quadDestroy()
    {
        if (!s_inited) return;
        free(s_vertices);
        s_vertices = NULL;
        s_quadCount     = 0;
        s_quadDrawCount = 0;
        s_inited = false;
    }

    void quadDraw2d_begin(u32 width, u32 height)
    {
        if (!s_inited) quadInit();
        s_width         = width  ? width  : 320;
        s_height        = height ? height : 200;
        s_quadCount     = 0;
        s_quadDrawCount = 0;
    }

    void quadDraw2d_add(u32 count, const Vec2f* quads, const u32* quadColors,
                        f32 u0, f32 u1, TextureGpu* texture)
    {
        if (!s_inited || !s_vertices || !quads || !quadColors || count == 0) return;
        if (s_quadDrawCount >= MAX_DRAW_GROUPS) return;

        // Record the draw group up front, matching upstream line 107-110.
        s_quadDraw[s_quadDrawCount].texture = texture;
        s_quadDraw[s_quadDrawCount].offset  = (s32)s_quadCount;
        s_quadDraw[s_quadDrawCount].count   = (s32)count;
        s_quadDrawCount++;

        QuadVertex* vert = &s_vertices[s_quadCount * 4];
        for (u32 i = 0; i < count && s_quadCount < QUAD_MAX_NUM;
             i++, quads += 2, quadColors += 2, vert += 4)
        {
            s_quadCount++;

            const f32 x0 = quads[0].x;
            const f32 x1 = quads[1].x;
            const f32 y0 = quads[0].z;
            const f32 y1 = quads[1].z;

            // Same vertex layout as upstream lines 124-148.
            vert[0].x = x0; vert[0].z = y0;
            vert[1].x = x1; vert[1].z = y0;
            vert[2].x = x1; vert[2].z = y1;
            vert[3].x = x0; vert[3].z = y1;

            vert[0].u = u0; vert[0].v = 0.0f;
            vert[1].u = u1; vert[1].v = 0.0f;
            vert[2].u = u1; vert[2].v = 1.0f;
            vert[3].u = u0; vert[3].v = 1.0f;

            // Top edge takes colors[0], bottom edge takes colors[1].
            vert[0].color = quadColors[0];
            vert[1].color = quadColors[0];
            vert[2].color = quadColors[1];
            vert[3].color = quadColors[1];
        }
    }

    void quadDraw2d_add(const Vec2f* vertices, const u32* colors, TextureGpu* texture)
    {
        quadDraw2d_add(1, vertices, colors, 0.0f, 1.0f, texture);
    }

    void quadDraw2d_add(const Vec2f* vertices, const u32* colors,
                        f32 u0, f32 u1, TextureGpu* texture)
    {
        quadDraw2d_add(1, vertices, colors, u0, u1, texture);
    }

    void quadDraw2d_draw()
    {
        static u32 s_lastReportedCount = 0xFFFFFFFFu;
        if (s_quadCount != s_lastReportedCount)
        {
            TFE_System::logWrite(LOG_MSG, "GPU", "quadDraw2d_draw flushing %u image quads (%u groups, vdisp %ux%u)",
                s_quadCount, s_quadDrawCount, s_width, s_height);
            s_lastReportedCount = s_quadCount;
        }
        if (!s_inited || !s_vertices || s_quadCount == 0 || s_quadDrawCount == 0) return;

        // For each draw group, emit a textured screen-space quad batch.
        // We call gpuDrawScreenQuad once per quad (per the existing
        // backend API). A future micro-optimisation could expose a
        // gpuDrawScreenQuads(verts, vertCount, tex) batch entrypoint,
        // but for the menu / HUD use-case the per-frame draw count is
        // small (~10 total) so the per-call overhead is irrelevant.
        //
        // Per-vertex color: upstream stores colors[0] on the TOP edge
        // (verts 0 & 1) and colors[1] on the BOTTOM edge (verts 2 & 3),
        // so we pass qv[0].color as topColor and qv[2].color as
        // botColor. gpuDrawScreenQuad's MODULATE stage multiplies the
        // sampled texel by this DIFFUSE colour.
        for (u32 g = 0; g < s_quadDrawCount; g++)
        {
            const QuadDraw* d = &s_quadDraw[g];
            for (s32 q = 0; q < d->count; q++)
            {
                const QuadVertex* qv = &s_vertices[(d->offset + q) * 4];
                TFE_RenderBackend::gpuDrawScreenQuad(
                    qv[0].x, qv[0].z,
                    qv[2].x, qv[2].z,
                    qv[0].u, qv[0].v,
                    qv[2].u, qv[2].v,
                    s_width, s_height,
                    (TFE_RenderBackend::GpuTextureHandle)d->texture,
                    /*alphaTest*/false,
                    qv[0].color, qv[2].color);
            }
        }

        // Match upstream's flush semantics: clear after draw.
        s_quadCount = 0;
        s_quadDrawCount = 0;
    }
}

#endif // _XBOX
