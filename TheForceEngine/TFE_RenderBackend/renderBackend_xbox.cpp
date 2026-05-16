// renderBackend_xbox.cpp
// Xbox render backend using Direct3D 8.
//
// Architecture:
//   The software renderer writes an 8-bit paletted framebuffer.
//   vfb_swap() calls updateVirtualDisplay(buf, size) once per frame.
//   We CPU-expand the 8-bit data to XRGB8888 using the stored palette,
//   lock a D3D8 texture, copy in, then StretchRect to the 1280x720
//   back buffer with correct aspect-ratio letterboxing, then Present.
//
// GPU renderer path (RClassic_GPU) is NOT used on Xbox.
// All render-target / shader / indexed-draw functions are safe stubs.
//
// 720p is the fixed output resolution. The Xbox dashboard is expected
// to have widescreen enabled in system settings; we do not force AV pack
// settings here.

#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#include <TFE_RenderBackend/textureGpu.h>
#include <TFE_RenderBackend/dynamicTexture.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/paths.h>

#include <xtl.h>
#include <d3d8.h>
#include <xgraphics.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Output resolution
//
// xquake (xbox/private/test/games/xquake/gl_fakegl.cpp:91-92) ships with
// 640x480 on Xbox. This is the always-supported NTSC mode, works on every
// dashboard config, and is what we match 1:1 for the initial port. 720p is
// a future enhancement after baseline rendering is confirmed.
// ---------------------------------------------------------------------------
#define XBOX_OUTPUT_WIDTH   640
#define XBOX_OUTPUT_HEIGHT  480

// ---------------------------------------------------------------------------
// Log shim - VC71 doesn't support variadic macros. Inline variadic functions
// for each log level forward to TFE_System::logWrite with the "RenderBackend"
// tag prefilled. Replaces the old RB_LOG(type, ...) macro.
// ---------------------------------------------------------------------------
static inline void RB_LOG_MSG(const char* fmt, ...)
{
    char buf[1024];
    va_list arg; va_start(arg, fmt);
    vsprintf(buf, fmt, arg);
    va_end(arg);
    TFE_System::logWrite(LOG_MSG, "RenderBackend", "%s", buf);
}
static inline void RB_LOG_ERROR(const char* fmt, ...)
{
    char buf[1024];
    va_list arg; va_start(arg, fmt);
    vsprintf(buf, fmt, arg);
    va_end(arg);
    TFE_System::logWrite(LOG_ERROR, "RenderBackend", "%s", buf);
}

namespace TFE_RenderBackend
{
    // -----------------------------------------------------------------------
    // D3D8 state
    // -----------------------------------------------------------------------
    static IDirect3D8*        s_d3d        = NULL;
    static IDirect3DDevice8*  s_device     = NULL;
    static IDirect3DTexture8* s_vdispTex   = NULL;   // Virtual display texture (XRGB)
    static IDirect3DSurface8* s_vdispSurf  = NULL;   // Level-0 surface of s_vdispTex

    static u32 s_vdispWidth  = 320;
    static u32 s_vdispHeight = 200;
    static u32 s_vdispWidthUi  = 320;
    static u32 s_vdispWidth3d  = 320;
    static DisplayMode s_displayMode = DMODE_ASPECT_CORRECT;

    static u32  s_paletteCpu[256];
    static bool s_vsync        = false;
    static bool s_widescreen   = false;
    static bool s_deviceReady  = false;
    // GPU render-target mode: when set, the per-frame contract is
    //   bindVirtualDisplay   -> BeginScene
    //   clearVirtualDisplay  -> Clear(TARGET|ZBUFFER|STENCIL, color)
    //   ... sector / sprite / HUD draws happen here ...
    //   unbindRenderTarget   -> no-op (scene stays open for HUD)
    //   swap                 -> EndScene + Present (no clear, no blit)
    // s_gpuSceneOpen tracks the BeginScene/EndScene pairing so menu /
    // edge states that skip bind still get a valid frame.
    static bool s_vdispGpuMode = false;
    static bool s_gpuSceneOpen = false;

    // Phase 11 rework: hardware D3DFMT_P8 palette. Textures upload as
    // raw 8-bit indices ONCE, and the per-frame DF palette FX (damage
    // flash, dark-trooper screen tint, level-load fade etc.) updates
    // only this one palette object. Mirrors upstream TFE's approach
    // where the GPU shader does palette lookup with a per-frame uniform.
    //
    // Without P8 the texture cache was being invalidated every frame
    // during palette FX (50+ per-frame palette updates dropped 100+
    // cached cell textures each), so half the sprites flickered as the
    // re-upload couldn't keep up with mission_render's draw rate.
    static D3DPalette* s_p8Palette = NULL;
    static bool        s_p8PaletteDirty = true;
    // True once at least one GPU-mode frame has been Presented. While
    // false, no-bind frames issue a black clear+Present so the loading
    // screen left over from software mode doesn't sit on the front
    // buffer. After that, no-bind frames skip Present entirely and the
    // display engine continues scanning out the last wall frame from
    // the front buffer - prevents flashing between wall frames and
    // black clears at the mission_render vs main_loop rate mismatch.
    static bool s_gpuFirstFramePresented = false;

    // Forward decls: setPalette (defined early in the file) calls into
    // the Phase 3 texture cache which lives later.
    void gpuInvalidateTextureCache();
    u32  gpuTextureCacheCount();

    // Destination rect on back buffer (letterbox/pillarbox).
    static RECT s_destRect;

    // Scratch expand buffer (palette -> XRGB).
    // Max virtual display size: 1280x960 to be safe. ~5MB.
    #define MAX_VDISP_PIXELS (1280 * 960)
    static u32 s_expandBuf[MAX_VDISP_PIXELS];

    // -----------------------------------------------------------------------
    // Compute destination rect.
    // At 640x480 the back buffer is already 4:3 - no letterbox needed.
    // Dark Forces native is 320x200 with 4:3 display aspect, so it stretches
    // edge-to-edge onto the back buffer.
    // -----------------------------------------------------------------------
    static void computeDestRect()
    {
        s_destRect.left   = 0;
        s_destRect.top    = 0;
        s_destRect.right  = XBOX_OUTPUT_WIDTH;
        s_destRect.bottom = XBOX_OUTPUT_HEIGHT;
    }

    // -----------------------------------------------------------------------
    // Create / recreate the virtual display texture.
    // -----------------------------------------------------------------------
    static bool createVdispTexture(u32 width, u32 height)
    {
        if (s_vdispSurf)  { s_vdispSurf->Release();  s_vdispSurf  = NULL; }
        if (s_vdispTex)   { s_vdispTex->Release();   s_vdispTex   = NULL; }

        // D3DFMT_X8R8G8B8 (0x07) is the SWIZZLED 32-bit format on Xbox; the
        // linear (scanline-major) variant is D3DFMT_LIN_X8R8G8B8 (0x1E). We
        // write linear pixel data into this texture via LockRect every frame,
        // so we have to declare the layout as linear or the GPU samples it
        // through Morton/Z-order decoding and the on-screen output appears
        // as repeating vertical color bands instead of the source image.
        // (Reference: d3d8types-xbox.h pairs at 0x07/0x1E; Cxbx-Reloaded
        // XbConvert.cpp:1105-1132 confirms swizzled vs linear classification.
        // Non-power-of-2 dimensions like 320x200 force linear in any case;
        // making it explicit removes ambiguity.)
        // A8R8G8B8 (not X8R8G8B8) so Phase 9 HUD overlay can use the
        // alpha channel for palette-index-0 transparency. In software
        // mode the alpha is just 0xFF everywhere and looks identical
        // to the old format.
        HRESULT hr = s_device->CreateTexture(
            width, height, 1,
            0,                      // no render target
            D3DFMT_LIN_A8R8G8B8,
            D3DPOOL_MANAGED,
            &s_vdispTex);

        if (FAILED(hr))
        {
            RB_LOG_ERROR("CreateTexture failed hr=0x%08x", hr);
            return false;
        }

        hr = s_vdispTex->GetSurfaceLevel(0, &s_vdispSurf);
        if (FAILED(hr))
        {
            RB_LOG_ERROR("GetSurfaceLevel failed hr=0x%08x", hr);
            s_vdispTex->Release();
            s_vdispTex = NULL;
            return false;
        }

        RB_LOG_MSG("Virtual display texture created %ux%u", width, height);
        return true;
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    bool init(const WindowState& state)
    {
        RB_LOG_MSG("init (Xbox D3D8)");
        // %f avoided - MSVC 2005 vsprintf float formatting hangs on Xbox.
        TFE_XboxLogf("RenderBackend", "init state=%dx%d flags=0x%08x refresh=%d(x100)",
            state.width, state.height, state.flags, (int)(state.refreshRate * 100.0f));

        // Pass 0, NOT the D3D_SDK_VERSION macro. On this XDK install
        // <d3d8.h> resolves to the PC DirectX 8 header which defines
        // D3D_SDK_VERSION = 120; the Xbox runtime / CXBX-R HLE both
        // expect 0. The PC header's COM vtables happen to be ABI-
        // compatible with d3d8-xbox.lib so the rest of the code works
        // unchanged - only the SDK-version argument needs overriding.
        s_d3d = Direct3DCreate8(0);
        if (!s_d3d)
        {
            RB_LOG_ERROR("Direct3DCreate8 failed");
            return false;
        }

        // Match xquake gl_fakegl.cpp InitD3DX() 1:1 for the Xbox path.
        // Notably: EnableAutoDepthStencil = TRUE with D3DFMT_D24S8, and
        // CreateDevice flag set is HARDWARE_VERTEXPROCESSING | PUREDEVICE.
        // These are what xquake ships with on retail and what we adopt.
        D3DPRESENT_PARAMETERS pp;
        memset(&pp, 0, sizeof(pp));
        pp.BackBufferWidth              = XBOX_OUTPUT_WIDTH;
        pp.BackBufferHeight             = XBOX_OUTPUT_HEIGHT;
        pp.BackBufferFormat             = D3DFMT_X8R8G8B8;
        pp.BackBufferCount              = 1;
        pp.Windowed                     = FALSE;       // must be FALSE on Xbox
        pp.EnableAutoDepthStencil       = TRUE;
        pp.AutoDepthStencilFormat       = D3DFMT_D24S8;
        pp.SwapEffect                   = D3DSWAPEFFECT_DISCARD;
        pp.FullScreen_RefreshRateInHz   = 60;
        pp.hDeviceWindow                = NULL;
        pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        // xquake hardcodes IMMEDIATE on Xbox. Record the requested vsync
        // intent for reporting; we don't toggle the device's actual interval.
        s_vsync = (state.flags & WINFLAG_VSYNC) != 0;
        TFE_XboxLogf("RenderBackend", "present interval=immediate (xquake match)");

        HRESULT hr = s_d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            0,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
            &pp,
            &s_device);
        TFE_XboxLogf("RenderBackend", "CreateDevice hr=0x%08x dev=%p", hr, s_device);

        if (FAILED(hr))
        {
            RB_LOG_ERROR("CreateDevice failed hr=0x%08x", hr);
            s_d3d->Release();
            s_d3d = NULL;
            return false;
        }

        // xquake warm-up: query the back buffer's surface desc, then release.
        // This forces the device to fully realize the back buffer before any
        // subsequent rendering. Without it, the first GetBackBuffer in swap()
        // can return a not-yet-valid surface on Xbox.
        {
            IDirect3DSurface8* pBackBuffer = NULL;
            HRESULT hrBB = s_device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
            TFE_XboxLogf("RenderBackend", "warm-up GetBackBuffer hr=0x%08x surf=%p", hrBB, pBackBuffer);
            if (SUCCEEDED(hrBB) && pBackBuffer)
            {
                D3DSURFACE_DESC desc;
                pBackBuffer->GetDesc(&desc);
                TFE_XboxLogf("RenderBackend", "warm-up back-buffer %ux%u fmt=%d",
                    (unsigned)desc.Width, (unsigned)desc.Height, (int)desc.Format);
                pBackBuffer->Release();
            }
        }

        // Clear palette to grey so any unset entries are visible but not black.
        for (int i = 0; i < 256; i++)
            s_paletteCpu[i] = 0xFF808080u;

        computeDestRect();
        s_deviceReady = true;
        RB_LOG_MSG("D3D8 device created. Output: %dx%d", XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT);
        TFE_XboxLogf("RenderBackend", "device ready dest=%ld,%ld,%ld,%ld",
            s_destRect.left, s_destRect.top, s_destRect.right, s_destRect.bottom);
        return true;
    }

    void destroy()
    {
        TFE_XboxLogf("RenderBackend", "destroy begin ready=%d", s_deviceReady ? 1 : 0);
        s_deviceReady = false;
        if (s_vdispSurf)  { s_vdispSurf->Release();  s_vdispSurf  = NULL; }
        if (s_vdispTex)   { s_vdispTex->Release();   s_vdispTex   = NULL; }
        if (s_device)     { s_device->Release();      s_device     = NULL; }
        if (s_d3d)        { s_d3d->Release();         s_d3d        = NULL; }
        RB_LOG_MSG("destroy");
    }

    bool getVsyncEnabled()   { return s_vsync; }
    bool isWindowMinimized() { return false; }  // No minimize on Xbox.

    void enableVsync(bool enable)
    {
        // Changing present interval requires device reset on D3D8.
        // For simplicity, store intent; applied on next init.
        s_vsync = enable;
        TFE_XboxLogf("RenderBackend", "enableVsync requested=%d", enable ? 1 : 0);
    }

    void setClearColor(const f32* /*color*/) {}  // Letterbox bars are always black.

    void clearWindow()
    {
        if (!s_deviceReady) return;
        // Clear requires a scene bracket on Xbox D3D8, same as swap().
        // Always clear TARGET|ZBUFFER|STENCIL together - per OpenJKDF2
        // fakeglx.cpp:1829: "for NV20 it's better to always clear everything."
        // Partial clears leave the depth/stencil buffer in undefined state
        // and the NV20 HLE choke chain ends in a Present crash.
        s_device->BeginScene();
        s_device->Clear(0, NULL,
                        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        0, 1.0f, 0);
        s_device->EndScene();
    }

    // -----------------------------------------------------------------------
    // Virtual display
    // -----------------------------------------------------------------------
    bool createVirtualDisplay(const VirtualDisplayInfo& vdispInfo)
    {
        TFE_XboxLogf("VDISP", "createVirtualDisplay %ux%u ui=%u 3d=%u mode=%d flags=0x%08x",
            vdispInfo.width, vdispInfo.height, vdispInfo.widthUi, vdispInfo.width3d,
            (int)vdispInfo.mode, vdispInfo.flags);
        s_vdispWidth    = vdispInfo.width;
        s_vdispHeight   = vdispInfo.height;
        s_vdispWidthUi  = vdispInfo.widthUi;
        s_vdispWidth3d  = vdispInfo.width3d;
        s_displayMode   = vdispInfo.mode;
        s_widescreen    = (vdispInfo.flags & VDISP_WIDESCREEN) != 0;
        s_vdispGpuMode  = (vdispInfo.flags & VDISP_RENDER_TARGET) != 0;
        if (s_vdispGpuMode)
        {
            TFE_XboxLogf("VDISP", "GPU render-target mode active");
        }

        computeDestRect();

        if (!s_deviceReady)
        {
            TFE_XboxLogf("RenderBackend", "createVirtualDisplay failed: device not ready");
            return false;
        }
        return createVdispTexture(s_vdispWidth, s_vdispHeight);
    }

    // Called once per frame with the 8-bit paletted framebuffer.
    void updateVirtualDisplay(const void* buffer, size_t size)
    {
        // Heartbeat so we know the game is feeding pixels each frame.
        static int s_vdispCalls = 0;
        if (!s_deviceReady || !s_vdispTex || !buffer) { s_vdispCalls++; return; }

        const u8* src = (const u8*)buffer;
        const u32 pixels = s_vdispWidth * s_vdispHeight;

        if (pixels > MAX_VDISP_PIXELS)
        {
            RB_LOG_ERROR("Virtual display too large: %u pixels", pixels);
            return;
        }

        // CPU palette expand: 8-bit index -> ARGB8888.
        // Palette index 0 = transparent (DF convention). Force alpha=0
        // there so the GPU-mode overlay alpha-test discards untouched
        // pixels; force alpha=0xFF everywhere else so software-mode
        // present looks identical to before.
        for (u32 i = 0; i < pixels; i++)
        {
            const u8 idx = src[i];
            s_expandBuf[i] = (idx == 0) ? 0u
                : ((s_paletteCpu[idx] & 0x00FFFFFFu) | 0xFF000000u);
        }

        s_vdispCalls++;

        // Lock, copy, unlock.
        D3DLOCKED_RECT lr;
        HRESULT hr = s_vdispTex->LockRect(0, &lr, NULL, 0);
        if (FAILED(hr)) { RB_LOG_ERROR("LockRect failed 0x%08x", hr); return; }

        const u8* srcRow = (const u8*)s_expandBuf;
        u8*       dstRow = (u8*)lr.pBits;
        const u32 srcPitch = s_vdispWidth * 4;
        for (u32 y = 0; y < s_vdispHeight; y++)
        {
            memcpy(dstRow, srcRow, srcPitch);
            srcRow += srcPitch;
            dstRow += lr.Pitch;
        }
        s_vdispTex->UnlockRect(0);
    }

    void setPalette(const u32* palette)
    {
        if (!palette) return;
        // The game calls setPalette every frame; only log when entry 0 or
        // entry 1 (often used as the "primary" color in DF palettes)
        // changes, so we get meaningful signal in the trace without
        // drowning every other log line.
        static u32 s_lastFirst = 0xDEADBEEF;
        static u32 s_lastSecond = 0xDEADBEEF;

        // TFE stores its palette as RGBA in memory: byte0=R, byte1=G, byte2=B,
        // byte3=A. As a little-endian u32 that's 0xAABBGGRR. Our texture is
        // D3DFMT_LIN_X8R8G8B8 which expects XRGB in memory: byte0=B, byte1=G,
        // byte2=R, byte3=X. As a u32 that's 0xXXRRGGBB. A straight memcpy
        // swaps the R and B channels (verified visually: red title text
        // showed up blue, green-tinted panels showed up red-tinted).
        // Swap R<->B as we copy.
        for (int i = 0; i < 256; i++)
        {
            const u32 src = palette[i];
            s_paletteCpu[i] = (src & 0xFF00FF00u)         // A and G stay put
                            | ((src & 0x000000FFu) << 16) // src R -> dst R slot
                            | ((src & 0x00FF0000u) >> 16);// src B -> dst B slot
        }

        if (s_paletteCpu[0] != s_lastFirst || s_paletteCpu[1] != s_lastSecond)
        {
            TFE_XboxLogf("RenderBackend", "palette CHANGED first=0x%08x second=0x%08x",
                s_paletteCpu[0], s_paletteCpu[1]);
            s_lastFirst  = s_paletteCpu[0];
            s_lastSecond = s_paletteCpu[1];
        }

        // Phase 11: textures are D3DFMT_P8 (raw indices). On a palette
        // change we just mark the D3D palette object dirty - the next
        // frame's ensureP8PaletteSynced rewrites its 256 entries.
        // No cached pixel data needs to be discarded.
        static u32 s_paletteCpuLast[256] = {0};
        if (memcmp(s_paletteCpu, s_paletteCpuLast, sizeof(s_paletteCpu)) != 0)
        {
            s_p8PaletteDirty = true;
            memcpy(s_paletteCpuLast, s_paletteCpu, sizeof(s_paletteCpu));
        }
    }

    const u32* getPalette()             { return s_paletteCpu; }
    const TextureGpu* getPaletteTexture(){ return NULL; } // Not used on Xbox software path.

    // -----------------------------------------------------------------------
    // Swap - blit virtual display to back buffer and present.
    //
    // Render path: textured fullscreen quad via DrawPrimitiveUP with XYZRHW
    // (pre-transformed) vertices. We dropped the previous D3DXLoadSurfaceFrom-
    // Surface call: that helper is a D3DX bitmap-loader, not a runtime blit
    // primitive, and on Xbox D3D8 it does not reliably copy from a managed-
    // pool source surface into a tiled vidmem back buffer - the result was a
    // black back buffer every frame even though the vdisp texture had pixels.
    // xquake and OpenJKDF2 both render textured quads for their final present
    // path; this matches that pattern.
    // -----------------------------------------------------------------------
    struct PresentQuadVert
    {
        f32 x, y, z, rhw;   // pre-transformed (screen-space) position
        f32 u, v;           // texture coordinates
    };
    #define PRESENT_QUAD_FVF (D3DFVF_XYZRHW | D3DFVF_TEX1)

    // Blit s_vdispTex as a screen-aligned quad over the back buffer.
    // alphaTest=true enables the alpha test so palette-index-0 pixels
    // (uploaded with alpha=0 in updateVirtualDisplay) discard - used
    // for the Phase 9 HUD overlay in GPU mode.
    static void blitVdispQuad(bool alphaTest)
    {
        s_device->SetRenderState(D3DRS_LIGHTING,          FALSE);
        s_device->SetRenderState(D3DRS_ZENABLE,           FALSE);
        s_device->SetRenderState(D3DRS_ZWRITEENABLE,      FALSE);
        s_device->SetRenderState(D3DRS_CULLMODE,          D3DCULL_NONE);
        s_device->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
        s_device->SetRenderState(D3DRS_ALPHATESTENABLE,   alphaTest ? TRUE : FALSE);
        if (alphaTest)
        {
            s_device->SetRenderState(D3DRS_ALPHAREF,      0x80);
            s_device->SetRenderState(D3DRS_ALPHAFUNC,     D3DCMP_GREATEREQUAL);
        }
        s_device->SetRenderState(D3DRS_FOGENABLE,         FALSE);

        s_device->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        // POINT in GPU/alpha-test mode (chunky HUD pixels), LINEAR in
        // software mode (matches the prior look).
        const D3DTEXTUREFILTERTYPE filt = alphaTest ? D3DTEXF_POINT : D3DTEXF_LINEAR;
        s_device->SetTextureStageState(0, D3DTSS_MAGFILTER, filt);
        s_device->SetTextureStageState(0, D3DTSS_MINFILTER, filt);
        s_device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP);

        s_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        s_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        s_device->SetTexture(0, s_vdispTex);
        s_device->SetVertexShader(PRESENT_QUAD_FVF);

        const f32 l = (f32)s_destRect.left   - 0.5f;
        const f32 t = (f32)s_destRect.top    - 0.5f;
        const f32 r = (f32)s_destRect.right  - 0.5f;
        const f32 b = (f32)s_destRect.bottom - 0.5f;
        const f32 uMax = (f32)s_vdispWidth;
        const f32 vMax = (f32)s_vdispHeight;

        PresentQuadVert q[4];
        q[0].x = l; q[0].y = t; q[0].z = 0.0f; q[0].rhw = 1.0f; q[0].u = 0.0f; q[0].v = 0.0f;
        q[1].x = r; q[1].y = t; q[1].z = 0.0f; q[1].rhw = 1.0f; q[1].u = uMax; q[1].v = 0.0f;
        q[2].x = l; q[2].y = b; q[2].z = 0.0f; q[2].rhw = 1.0f; q[2].u = 0.0f; q[2].v = vMax;
        q[3].x = r; q[3].y = b; q[3].z = 0.0f; q[3].rhw = 1.0f; q[3].u = uMax; q[3].v = vMax;

        s_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(PresentQuadVert));
        s_device->SetTexture(0, NULL);

        if (alphaTest) s_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    }

    void swap(bool blitVirtualDisplay)
    {
        if (!s_deviceReady) return;

        // GPU mode:
        //   - scene opened (mission_render ran this frame): blit the
        //     8-bit framebuffer as an alpha-tested HUD overlay (Phase 9),
        //     then finish + present.
        //   - scene not opened, no GPU frame ever presented: clear +
        //     present to wipe the stale software-mode loading screen.
        //   - scene not opened, walls already presented earlier: skip
        //     Present so the display engine holds the last wall frame.
        if (s_vdispGpuMode)
        {
            if (s_gpuSceneOpen)
            {
                if (blitVirtualDisplay && s_vdispTex) blitVdispQuad(/*alphaTest*/true);
                s_device->EndScene();
                s_gpuSceneOpen = false;
                s_device->Present(NULL, NULL, NULL, NULL);
                s_gpuFirstFramePresented = true;
                return;
            }
            if (!s_gpuFirstFramePresented)
            {
                s_device->BeginScene();
                s_device->Clear(0, NULL,
                                D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                                D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
                s_device->EndScene();
                s_device->Present(NULL, NULL, NULL, NULL);
            }
            return;
        }

        // Software path - unchanged from Phase 0. Begin/Clear/blit/End/Present
        // all inside swap() because the 8-bit framebuffer is uploaded as a
        // texture and presented via a fullscreen quad here.
        s_device->BeginScene();

        // Always clear TARGET|ZBUFFER|STENCIL together (NV20 quirk).
        // OpenJKDF2 fakeglx.cpp:1829-1844. xquake same. Mercs same.
        // We allocated D3DFMT_D24S8 in the present params; partial-clear
        // leaves it undefined which the HLE Present path crashes on.
        s_device->Clear(0, NULL,
                        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        0, 1.0f, 0);

        if (blitVirtualDisplay && s_vdispTex) blitVdispQuad(/*alphaTest*/false);

        s_device->EndScene();
        s_device->Present(NULL, NULL, NULL, NULL);
    }

    // -----------------------------------------------------------------------
    // Display info
    // -----------------------------------------------------------------------
    void getDisplayInfo(DisplayInfo* displayInfo)
    {
        if (!displayInfo) return;
        displayInfo->width       = XBOX_OUTPUT_WIDTH;
        displayInfo->height      = XBOX_OUTPUT_HEIGHT;
        displayInfo->refreshRate = s_vsync ? 60.0f : 0.0f;
    }

    f32 getDisplayRefreshRate() { return 60.0f; }
    s32 getDisplayCount()       { return 1; }

    s32 getDisplayIndex(s32 /*x*/, s32 /*y*/) { return 0; }

    bool getDisplayMonitorInfo(s32 /*displayIndex*/, MonitorInfo* monitorInfo)
    {
        if (!monitorInfo) return false;
        monitorInfo->x = 0; monitorInfo->y = 0;
        monitorInfo->w = XBOX_OUTPUT_WIDTH;
        monitorInfo->h = XBOX_OUTPUT_HEIGHT;
        return true;
    }

    void getCurrentMonitorInfo(MonitorInfo* monitorInfo)
    {
        getDisplayMonitorInfo(0, monitorInfo);
    }

    void resize(s32 /*width*/, s32 /*height*/) {}  // Fixed output on Xbox.
    void enableFullscreen(bool /*enable*/)     {}  // Always fullscreen on Xbox.
    void updateSettings()                      {}
    void setColorCorrection(bool /*enabled*/, const ColorCorrection* /*color*/, bool /*bloomChanged*/) {}
    void bloomPostEnable(bool /*enable*/)      {}

    bool getWidescreen()       { return s_widescreen; }
    bool getFrameBufferAsync() { return false; }
    bool getGPUColorConvert()  { return false; } // CPU expand only.

    u32 getVirtualDisplayWidth2D()  { return s_vdispWidthUi; }
    u32 getVirtualDisplayWidth3D()  { return s_vdispWidth3d; }
    u32 getVirtualDisplayHeight()   { return s_vdispHeight; }

    u32 getVirtualDisplayOffset2D()
    {
        return (s_vdispWidth > s_vdispWidthUi) ? (s_vdispWidth - s_vdispWidthUi) >> 1 : 0;
    }
    u32 getVirtualDisplayOffset3D()
    {
        return (s_vdispWidth > s_vdispWidth3d) ? (s_vdispWidth - s_vdispWidth3d) >> 1 : 0;
    }

    void* getVirtualDisplayGpuPtr() { return NULL; }

    // -----------------------------------------------------------------------
    // Stubs - bindVirtualDisplay / clearVirtualDisplay / copyTo*
    // These are only used by the GPU renderer path which is not active.
    // -----------------------------------------------------------------------
    void bindVirtualDisplay()
    {
        if (!s_deviceReady || !s_vdispGpuMode) return;
        if (!s_gpuSceneOpen)
        {
            s_device->BeginScene();
            s_gpuSceneOpen = true;
        }
        // Back buffer is the only render target on Xbox right now (no
        // offscreen RT yet) so nothing to SetRenderTarget here.
    }

    void clearVirtualDisplay(f32* color, bool clearColor)
    {
        if (!s_deviceReady || !s_vdispGpuMode) return;
        if (!s_gpuSceneOpen)
        {
            s_device->BeginScene();
            s_gpuSceneOpen = true;
        }
        D3DCOLOR c = D3DCOLOR_XRGB(0, 0, 0);
        if (color && clearColor)
        {
            const u8 r = (u8)(color[0] * 255.0f);
            const u8 g = (u8)(color[1] * 255.0f);
            const u8 b = (u8)(color[2] * 255.0f);
            const u8 a = (u8)(color[3] * 255.0f);
            c = D3DCOLOR_ARGB(a, r, g, b);
        }
        // NV20 quirk - always clear all three together. D24S8 must not be
        // left undefined or Present can crash inside the HLE.
        s_device->Clear(0, NULL,
                        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        c, 1.0f, 0);
    }
    void copyToVirtualDisplay(RenderTargetHandle /*src*/)  {}
    void copyBackbufferToRenderTarget(RenderTargetHandle /*dst*/) {}
    void captureScreenToMemory(u32* /*mem*/)               {}
    void queueScreenshot(const char* /*path*/)             {}
    void startGifRecording(const char* /*path*/, bool)     {}
    void stopGifRecording()                                {}
    void bindGlobalVAO()                                   {}
    void setViewport(s32, s32, s32, s32)                   {}
    void setScissorRect(bool, s32, s32, s32, s32)          {}

    // -----------------------------------------------------------------------
    // Render target stubs
    // -----------------------------------------------------------------------
    RenderTargetHandle createRenderTarget(u32 /*w*/, u32 /*h*/, bool /*depth*/)
    {
        return NULL;
    }
    void freeRenderTarget(RenderTargetHandle /*handle*/)            {}
    void bindRenderTarget(RenderTargetHandle /*handle*/)            {}
    void clearRenderTarget(RenderTargetHandle, const f32*, f32)     {}
    void clearRenderTargetDepth(RenderTargetHandle, f32)            {}
    void copyRenderTarget(RenderTargetHandle, RenderTargetHandle)   {}
    void unbindRenderTarget()
    {
        // Scene stays open through HUD/swap. Phase 2+ may move HUD here.
    }

    // -----------------------------------------------------------------------
    // gpuDrawColoredTrisWorld - Phase 2 of the RClassic_GPU/D3D8 port.
    // Untextured colored geometry in world space. Used by the JEDI sector
    // renderer to visualise sector walls before texturing / lighting land.
    // Sets matrices + FF state, draws, restores nothing - subsequent calls
    // can change state freely.
    // -----------------------------------------------------------------------
    void gpuDrawColoredTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                 const GpuColorVert* verts, u32 triCount)
    {
        if (!s_deviceReady || !s_gpuSceneOpen || !verts || triCount == 0) return;

        D3DMATRIX view; memcpy(&view, viewMtx, sizeof(view));
        D3DMATRIX proj; memcpy(&proj, projMtx, sizeof(proj));
        D3DMATRIX world; memset(&world, 0, sizeof(world));
        world._11 = world._22 = world._33 = world._44 = 1.0f;

        s_device->SetTransform(D3DTS_PROJECTION, &proj);
        s_device->SetTransform(D3DTS_VIEW,       &view);
        s_device->SetTransform(D3DTS_WORLD,      &world);

        // Untextured colored geometry: depth on (so walls occlude each
        // other), cull off (Phase 2 - winding is unverified), no blending
        // or fog, lighting off (the diffuse vertex attribute IS the color).
        s_device->SetRenderState(D3DRS_LIGHTING,         FALSE);
        s_device->SetRenderState(D3DRS_ZENABLE,          TRUE);
        s_device->SetRenderState(D3DRS_ZWRITEENABLE,     TRUE);
        s_device->SetRenderState(D3DRS_ZFUNC,            D3DCMP_LESSEQUAL);
        s_device->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
        s_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        s_device->SetRenderState(D3DRS_ALPHATESTENABLE,  FALSE);
        s_device->SetRenderState(D3DRS_FOGENABLE,        FALSE);

        s_device->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        s_device->SetTexture(0, NULL);
        s_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        s_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        s_device->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        HRESULT hr = s_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount,
                                               verts, sizeof(GpuColorVert));

        static bool s_loggedFirstWorld = false;
        if (!s_loggedFirstWorld)
        {
            s_loggedFirstWorld = true;
            TFE_XboxLogf("GPU", "Phase 2 first world draw: tris=%u hr=0x%08x",
                         triCount, hr);
        }
    }

    // -----------------------------------------------------------------------
    // Phase 3: indexed-texture cache + textured world draw.
    //
    // Strategy: CPU-expand 8-bit indexed pixels to XRGB8888 using the
    // current palette, upload as D3DFMT_LIN_X8R8G8B8, cache keyed by the
    // source TextureData*. DF BM data is column-major - we transpose
    // during the expand. Cache is a linear array; lookup is O(N) but N
    // stays small (DF levels have ~30-150 unique wall textures and
    // SECBASE is on the low end).
    //
    // We do NOT use D3DFMT_P8 even though Xbox supports it. The present-
    // quad path already established 32-bit linear textures as the
    // working convention on this NV2A target, and per-frame palette
    // upload + texture palette state would add a state-change axis we
    // don't need yet. Palette FX (vision modifiers etc.) will require
    // re-uploading and are handled by gpuInvalidateTextureCache().
    // -----------------------------------------------------------------------
    // Cache cap sized for SECBASE: ~150 wall+flat textures + per-frame
    // sprite cells (enemies have ~5 anims x 32 views x ~5 frames ~= 800
    // cells per type, though most slots are NULL view fallbacks). 1024
    // is a comfortable headroom on Xbox's 64 MB; worst-case 1024 *
    // 64 KB = 64 MB would blow the budget but typical sprite cells
    // are 16-32 KB swizzled.
    enum { XBOX_TEX_CACHE_CAP = 1024 };
    struct GpuTexCacheEntry
    {
        const void* key;
        IDirect3DTexture8* tex;
    };
    static GpuTexCacheEntry s_texCache[XBOX_TEX_CACHE_CAP];
    static u32              s_texCacheCount = 0;
    static u32              s_texCacheRejectLogged = 0;

    u32 gpuTextureCacheCount() { return s_texCacheCount; }

    void gpuInvalidateTextureCache()
    {
        for (u32 i = 0; i < s_texCacheCount; i++)
        {
            if (s_texCache[i].tex) s_texCache[i].tex->Release();
            s_texCache[i].tex = NULL;
            s_texCache[i].key = NULL;
        }
        s_texCacheCount = 0;
    }

    static inline u32 nextPow2_u32(u32 v)
    {
        u32 r = 1;
        while (r < v) r <<= 1;
        return r;
    }

    // Create (lazily) + update the hardware palette from s_paletteCpu.
    // Index 0 is the DF transparent colour for sprites: we force its
    // alpha to 0 so the sprite-draw alpha test discards it. All other
    // indices get alpha 0xFF. Walls don't enable alpha test so the
    // alpha bits are simply ignored on those draws.
    static void ensureP8PaletteSynced()
    {
        if (!s_deviceReady) return;
        if (!s_p8Palette)
        {
            HRESULT hr = s_device->CreatePalette(D3DPALETTE_256, &s_p8Palette);
            if (FAILED(hr) || !s_p8Palette)
            {
                TFE_XboxLogf("GPU", "CreatePalette failed hr=0x%08x", hr);
                return;
            }
            TFE_XboxLogf("GPU", "Phase 11 hardware palette created");
            s_p8PaletteDirty = true;
        }
        if (!s_p8PaletteDirty) return;

        D3DCOLOR* entries = NULL;
        HRESULT hr = s_p8Palette->Lock(&entries, 0);
        if (FAILED(hr) || !entries) return;

        for (int i = 0; i < 256; i++)
        {
            // s_paletteCpu already in 0xAARRGGBB layout.
            entries[i] = (s_paletteCpu[i] & 0x00FFFFFFu) | 0xFF000000u;
        }
        entries[0] = 0x00000000u;  // transparent
        s_p8Palette->Unlock();
        s_p8PaletteDirty = false;
    }

    GpuTextureHandle gpuGetOrUploadIndexedTexture(const void* key,
                                                  const u8* indexed,
                                                  u32 width, u32 height,
                                                  bool columnMajor)
    {
        if (!s_deviceReady || !key || !indexed || !width || !height) return NULL;

        // O(N) lookup. N <= 384, branch-cheap; if this ever shows up in
        // profiles, swap for a sparse hash keyed by (key>>4) & MASK.
        for (u32 i = 0; i < s_texCacheCount; i++)
        {
            if (s_texCache[i].key == key) return (GpuTextureHandle)s_texCache[i].tex;
        }

        if (s_texCacheCount >= XBOX_TEX_CACHE_CAP)
        {
            if (!s_texCacheRejectLogged)
            {
                s_texCacheRejectLogged = 1;
                TFE_XboxLogf("GPU", "texture cache full (%u entries) - new uploads dropped, expect missing textures",
                             s_texCacheCount);
            }
            return NULL;
        }

        // NV2A swizzled D3DFMT_P8 needs pow2 dimensions. Non-pow2
        // sources scale up via nearest-neighbour as before; the caller's
        // [0,1] WRAP UV math is unchanged since one full GPU-texture tile
        // covers the same world distance regardless of resolution.
        const u32 texW = nextPow2_u32(width);
        const u32 texH = nextPow2_u32(height);
        const bool needScale = (texW != width) || (texH != height);

        // D3DFMT_P8 (swizzled): raw 8-bit indices, sampled with the
        // currently-bound D3D palette to produce RGBA. Static through
        // palette FX - the palette object changes, the texture data
        // does not.
        IDirect3DTexture8* tex = NULL;
        HRESULT hr = s_device->CreateTexture(texW, texH, 1, 0,
                                             D3DFMT_P8,
                                             0, &tex);
        if (FAILED(hr) || !tex)
        {
            TFE_XboxLogf("GPU", "CreateTexture P8 %ux%u (src %ux%u) failed hr=0x%08x",
                         texW, texH, width, height, hr);
            return NULL;
        }

        // 1 byte per pixel. 512x512 = 256 KB stage; we share a 1 MB
        // pool with the (now-removed) RGBA expand path so the existing
        // size is fine.
        enum { MAX_STAGE_BYTES = 512 * 512 };
        static u8 s_stage[MAX_STAGE_BYTES];
        if (texW * texH > MAX_STAGE_BYTES)
        {
            TFE_XboxLogf("GPU", "texture %ux%u exceeds P8 stage buffer, dropping", texW, texH);
            tex->Release();
            return NULL;
        }

        if (!needScale && columnMajor)
        {
            // DF BM column-major -> row-major copy.
            for (u32 y = 0; y < texH; y++)
            {
                u8* dstRow = s_stage + y * texW;
                for (u32 x = 0; x < texW; x++)
                    dstRow[x] = indexed[x * height + y];
            }
        }
        else if (!needScale)
        {
            for (u32 y = 0; y < texH; y++)
            {
                u8* dstRow = s_stage + y * texW;
                const u8* srcRow = indexed + y * width;
                memcpy(dstRow, srcRow, texW);
            }
        }
        else
        {
            for (u32 y = 0; y < texH; y++)
            {
                const u32 srcY = (y * height) / texH;
                u8* dstRow = s_stage + y * texW;
                if (columnMajor)
                {
                    for (u32 x = 0; x < texW; x++)
                    {
                        const u32 srcX = (x * width) / texW;
                        dstRow[x] = indexed[srcX * height + srcY];
                    }
                }
                else
                {
                    const u8* srcRow = indexed + srcY * width;
                    for (u32 x = 0; x < texW; x++)
                    {
                        const u32 srcX = (x * width) / texW;
                        dstRow[x] = srcRow[srcX];
                    }
                }
            }
        }

        D3DLOCKED_RECT lr;
        hr = tex->LockRect(0, &lr, NULL, 0);
        if (FAILED(hr))
        {
            TFE_XboxLogf("GPU", "tex LockRect failed hr=0x%08x", hr);
            tex->Release();
            return NULL;
        }
        // 1 byte per pixel; XGSwizzleRect handles the swizzle layout.
        XGSwizzleRect(s_stage, texW, NULL,
                      lr.pBits, texW, texH, NULL, 1);
        tex->UnlockRect(0);

        s_texCache[s_texCacheCount].key = key;
        s_texCache[s_texCacheCount].tex = tex;
        s_texCacheCount++;

        static bool s_loggedFirstTex = false;
        if (!s_loggedFirstTex)
        {
            s_loggedFirstTex = true;
            TFE_XboxLogf("GPU", "Phase 3 first texture upload: %ux%u (gpu %ux%u), cache=%u",
                         width, height, texW, texH, s_texCacheCount);
        }
        // Log every 64-entry milestone so we can see growth without spam.
        if ((s_texCacheCount & 63) == 0)
        {
            TFE_XboxLogf("GPU", "texture cache at %u / %u entries",
                         s_texCacheCount, (u32)XBOX_TEX_CACHE_CAP);
        }
        return (GpuTextureHandle)tex;
    }

    // gpuGetOrUploadRgbaTexture removed in Phase 11. Sprite uploads now
    // also use the P8 path via gpuGetOrUploadIndexedTexture - hardware
    // palette handles the index->RGBA expansion at sample time.

    void gpuDrawAlphaTestedTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                     GpuTextureHandle tex,
                                     const GpuTexVert* verts, u32 triCount)
    {
        if (!s_deviceReady || !s_gpuSceneOpen || !verts || triCount == 0) return;
        ensureP8PaletteSynced();
        if (s_p8Palette) s_device->SetPalette(0, s_p8Palette);

        D3DMATRIX view; memcpy(&view, viewMtx, sizeof(view));
        D3DMATRIX proj; memcpy(&proj, projMtx, sizeof(proj));
        D3DMATRIX world; memset(&world, 0, sizeof(world));
        world._11 = world._22 = world._33 = world._44 = 1.0f;

        s_device->SetTransform(D3DTS_PROJECTION, &proj);
        s_device->SetTransform(D3DTS_VIEW,       &view);
        s_device->SetTransform(D3DTS_WORLD,      &world);

        s_device->SetRenderState(D3DRS_LIGHTING,         FALSE);
        s_device->SetRenderState(D3DRS_ZENABLE,          TRUE);
        s_device->SetRenderState(D3DRS_ZWRITEENABLE,     TRUE);
        s_device->SetRenderState(D3DRS_ZFUNC,            D3DCMP_LESSEQUAL);
        s_device->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
        s_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        s_device->SetRenderState(D3DRS_FOGENABLE,        FALSE);
        // Alpha test: keep pixels whose alpha > 0. Sprite uploads put
        // alpha=0 on palette-index-0 pixels (the DF transparency colour).
        s_device->SetRenderState(D3DRS_ALPHATESTENABLE,  TRUE);
        s_device->SetRenderState(D3DRS_ALPHAREF,         0x80);
        s_device->SetRenderState(D3DRS_ALPHAFUNC,        D3DCMP_GREATEREQUAL);

        s_device->SetTexture(0, (IDirect3DTexture8*)tex);
        s_device->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        s_device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        s_device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        s_device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP);
        s_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        s_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        s_device->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        s_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, verts, sizeof(GpuTexVert));

        // Restore alpha test off so subsequent opaque draws aren't affected.
        s_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    }

    void gpuDrawTexturedTrisWorld(const f32 viewMtx[16], const f32 projMtx[16],
                                  GpuTextureHandle tex,
                                  const GpuTexVert* verts, u32 triCount)
    {
        ensureP8PaletteSynced();
        if (s_p8Palette) s_device->SetPalette(0, s_p8Palette);
        if (!s_deviceReady || !s_gpuSceneOpen || !verts || triCount == 0) return;

        D3DMATRIX view; memcpy(&view, viewMtx, sizeof(view));
        D3DMATRIX proj; memcpy(&proj, projMtx, sizeof(proj));
        D3DMATRIX world; memset(&world, 0, sizeof(world));
        world._11 = world._22 = world._33 = world._44 = 1.0f;

        s_device->SetTransform(D3DTS_PROJECTION, &proj);
        s_device->SetTransform(D3DTS_VIEW,       &view);
        s_device->SetTransform(D3DTS_WORLD,      &world);

        s_device->SetRenderState(D3DRS_LIGHTING,         FALSE);
        s_device->SetRenderState(D3DRS_ZENABLE,          TRUE);
        s_device->SetRenderState(D3DRS_ZWRITEENABLE,     TRUE);
        s_device->SetRenderState(D3DRS_ZFUNC,            D3DCMP_LESSEQUAL);
        s_device->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
        s_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        s_device->SetRenderState(D3DRS_ALPHATESTENABLE,  FALSE);
        s_device->SetRenderState(D3DRS_FOGENABLE,        FALSE);

        s_device->SetTexture(0, (IDirect3DTexture8*)tex);
        // MODULATE so per-vertex diffuse tints the texel (Phase 6
        // per-sector ambient). With diffuse = 0xFFFFFFFF this is
        // equivalent to the previous SELECTARG1 path.
        s_device->SetTextureStageState(0, D3DTSS_COLOROP,   tex ? D3DTOP_MODULATE  : D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG1, tex ? D3DTA_TEXTURE    : D3DTA_DIFFUSE);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        s_device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        s_device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        s_device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSU,  D3DTADDRESS_WRAP);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSV,  D3DTADDRESS_WRAP);
        s_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        s_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        s_device->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        s_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, verts, sizeof(GpuTexVert));
    }

    const TextureGpu* getRenderTargetTexture(RenderTargetHandle /*h*/) { return NULL; }
    void getRenderTargetDim(RenderTargetHandle /*h*/, u32* w, u32* h)
    {
        if (w) *w = 0;
        if (h) *h = 0;
    }

    // -----------------------------------------------------------------------
    // Texture stubs - software path never uses GPU textures.
    // -----------------------------------------------------------------------
    TextureGpu* createTexture(u32 /*w*/, u32 /*h*/, const u32* /*data*/, MagFilter /*f*/)
    {
        return NULL;
    }
    TextureGpu* createTexture(u32 /*w*/, u32 /*h*/, TexFormat /*fmt*/)
    {
        return NULL;
    }
    TextureGpu* createTextureArray(u32, u32, u32, u32, u32) { return NULL; }
    void freeTexture(TextureGpu* /*texture*/)               {}
    void getTextureDim(TextureGpu* /*t*/, u32* w, u32* h)
    {
        if (w) *w = 0;
        if (h) *h = 0;
    }
    void* getGpuPtr(const TextureGpu* /*texture*/)          { return NULL; }

    // -----------------------------------------------------------------------
    // Draw stubs
    // -----------------------------------------------------------------------
    void drawIndexedTriangles(u32, u32, u32) {}
    void drawLines(u32)                      {}

} // namespace TFE_RenderBackend


// ---------------------------------------------------------------------------
// TextureGpu stub implementation (Xbox)
// The software renderer never creates GPU textures directly.
// Minimal implementation satisfies the linker.
// ---------------------------------------------------------------------------
TextureGpu::~TextureGpu() {}

bool TextureGpu::create(u32 width, u32 height, TexFormat /*format*/, bool /*hasMipmaps*/, MagFilter /*magFilter*/)
{
    m_width  = width;
    m_height = height;
    return true;
}

bool TextureGpu::createArray(u32 width, u32 height, u32 layers, u32 /*channels*/, u32 /*mipCount*/)
{
    m_width  = width;
    m_height = height;
    m_layers = layers;
    return true;
}

bool TextureGpu::createWithData(u32 width, u32 height, const void* /*buffer*/, MagFilter /*magFilter*/)
{
    m_width  = width;
    m_height = height;
    return true;
}

bool TextureGpu::update(const void* /*buffer*/, size_t /*size*/, s32 /*layer*/, s32 /*mipLevel*/)
{
    return true;
}

void TextureGpu::setFilter(MagFilter /*magFilter*/, MinFilter /*minFilter*/, bool /*isArray*/) const {}
void TextureGpu::bind(u32 /*slot*/) const {}
void TextureGpu::clear(u32 /*slot*/) {}
void TextureGpu::clearSlots(u32 /*count*/, u32 /*start*/) {}
void TextureGpu::readCpu(u8* /*image*/) {}


// ---------------------------------------------------------------------------
// DynamicTexture stub implementation (Xbox)
// Used by the palette texture path which is bypassed (getGPUColorConvert=false).
// ---------------------------------------------------------------------------
#include <TFE_RenderBackend/dynamicTexture.h>

// Static members
u32 DynamicTexture::s_alignment = 4;

DynamicTexture::~DynamicTexture()
{
    freeBuffers();
}

bool DynamicTexture::create(u32 width, u32 height, u32 bufferCount, DynamicTexFormat format)
{
    m_width       = width;
    m_height      = height;
    m_bufferCount = bufferCount;
    m_format      = format;
    return true;
}

void DynamicTexture::resize(u32 newWidth, u32 newHeight)
{
    m_width  = newWidth;
    m_height = newHeight;
}

bool DynamicTexture::changeBufferCount(u32 newBufferCount, bool /*forceRealloc*/)
{
    m_bufferCount = newBufferCount;
    return true;
}

void DynamicTexture::update(const void* /*imageData*/, size_t /*size*/) {}
void DynamicTexture::bind(u32 /*slot*/) const {}

void DynamicTexture::freeBuffers()
{
    delete[] m_textures;
    delete[] m_stagingBuffers;
    m_textures       = NULL;
    m_stagingBuffers = NULL;
    m_bufferCount    = 0;
}
