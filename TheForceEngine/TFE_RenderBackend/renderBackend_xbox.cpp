// renderBackend_xbox.cpp
// Xbox render backend using Direct3D 8.
//
// Architecture:
//   The Xbox hardware renderer draws the JEDI world through D3D8 fixed-
//   function helpers exposed by this backend. The legacy 8-bit virtual
//   framebuffer is still updated each frame for HUD, weapon, messages, and
//   menu overlays; swap() alpha-tests that overlay on top of the D3D8 scene.
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
#include <TFE_Game/saveSystem.h>

#include <xtl.h>
#include <d3d8.h>
#include <xgraphics.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>

#include <TFE_DarkForces/GameUI/xboxPauseFont.inc>
#include <TFE_RenderBackend/xboxStartLogo.inc>
#include <TFE_RenderBackend/xboxStartFont.inc>
#include <TFE_RenderBackend/xboxFooterFont.inc>
#include <TFE_RenderBackend/xboxDukeButtons.inc>
#include <TFE_RenderBackend/xboxBriefingPromptFont.inc>
#include <TFE_RenderBackend/xboxWheelFont.inc>
#include <TFE_RenderBackend/xboxPdaFrame.inc>

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
    static IDirect3DTexture8* s_startTex   = NULL;   // 640x480 start screen texture
    static bool               s_vdispHasAlpha = false;
    static u32                s_vdispTexWidth = 0;
    static u32                s_vdispTexHeight = 0;

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
    static s32  s_safeZoneWidthPercent = 100;
    static s32  s_safeZoneHeightPercent = 100;
    static s32  s_safeZoneOffsetX = 0;
    static s32  s_safeZoneOffsetY = 0;

    // Scratch expand buffer (palette -> XRGB).
    // Xbox virtual displays are capped to the fixed 640x480 output. Keeping
    // these at the old "safe" 1280x960 size reserved almost 10 MB across the
    // expand/capture buffers, which starves retail hardware before Landru can
    // allocate a display texture for the boot intro.
    #define MAX_VDISP_PIXELS (XBOX_OUTPUT_WIDTH * XBOX_OUTPUT_HEIGHT)
    static u32 s_expandBuf[MAX_VDISP_PIXELS];
    static u32 s_captureBuf[MAX_VDISP_PIXELS];
    static bool s_captureBufValid = false;

    static bool s_pauseOverlayEnabled = false;
    static s32  s_pauseSelection = 0;
    static s32  s_pauseConfirmSelection = 0;
    static bool s_pauseConfirmOpen = false;
    static s32  s_pauseNotice = 0;
    static bool s_briefingFooterEnabled = false;
    static bool s_briefingFooterObjectivesPrompt = true;
    static s32  s_briefingFooterDifficulty = 1;
    static bool s_startScreenEnabled = false;
    static s32  s_startSelection = 0;
    static u32  s_startFrame = 0;
    static bool s_loadScreenEnabled = false;
    static s32  s_loadSelection = 0;
    static u32  s_loadFrame = 0;
    static const XboxLoadSlotInfo* s_loadSlots = NULL;
    static s32  s_loadSlotCount = 0;
    static bool s_modScreenEnabled = false;
    static s32  s_modSelection = 0;
    static u32  s_modFrame = 0;
    static XboxModInfo s_mods[12];
    static s32  s_modCount = 0;
    static bool s_optionsScreenEnabled = false;
    static bool s_optionsPauseStyle = false;
    static s32  s_optionsSelection = 0;
    static s32  s_optionsScroll = 0;
    static u32  s_optionsFrame = 0;
    static char s_optionsTitle[32] = "OPTIONS";
    static XboxOptionsItem s_optionsItems[32];
    static s32  s_optionsItemCount = 0;
    static bool s_cheatScreenEnabled = false;
    static s32  s_cheatSelection = 0;
    static s32  s_cheatScroll = 0;
    static XboxCheatItem s_cheatItems[12];
    static s32  s_cheatItemCount = 0;
    static bool s_pdaOverlayEnabled = false;
    static s32  s_pdaOverlayMode = 0;
    static s32  s_pdaOverlayLayer = 0;
    static bool s_missionCompleteScreenEnabled = false;
    static s32  s_missionCompleteSelection = 0;
    static u32  s_missionCompleteFrame = 0;
    static XboxMissionCompleteInfo s_missionCompleteInfo = { 0, 0, 0, 0 };
    static bool s_weaponWheelEnabled = false;
    static XboxWeaponWheelInfo s_weaponWheelInfo;

    static const u32 XPAUSE_GREEN_DARK  = 0xFF003800u;
    static const u32 XPAUSE_GREEN_MID   = 0xFF00A000u;
    static const u32 XPAUSE_GREEN_EDGE  = 0xFF16D016u;
    static const u32 XPAUSE_WHITE       = 0xFFE8E8E8u;
    static const u32 XPAUSE_GREY        = 0xFF9A9A9Au;
    static const u32 XPAUSE_GREY_DARK   = 0xFF4C4C4Cu;
    static const u32 XPAUSE_BLACK       = 0xFF000000u;

    static const s32 XPAUSE_DESIGN_WIDTH  = 640;
    static const s32 XPAUSE_DESIGN_HEIGHT = 480;
    static const s32 XPAUSE_PANEL_WIDTH   = 460;
    static const s32 XPAUSE_PANEL_HEIGHT  = 300;
    static const s32 XPAUSE_ROW_WIDTH     = 330;
    static const s32 XPAUSE_ROW_STEP      = 38;
    static const s32 XPDA_SCREEN_SRC_X    = 20;
    static const s32 XPDA_SCREEN_SRC_Y    = 24;
    static const s32 XPDA_SCREEN_SRC_W    = 592;
    static const s32 XPDA_SCREEN_SRC_H    = 314;
    static const s32 XPAUSE_SCREEN_SRC_X  = 46;
    static const s32 XPAUSE_SCREEN_SRC_Y  = 60;
    static const s32 XPAUSE_SCREEN_SRC_W  = 540;
    static const s32 XPAUSE_SCREEN_SRC_H  = 251;

    static inline s32 pauseClamp(s32 v, s32 lo, s32 hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static u32 pauseBlend(u32 dst, u32 src, u32 a)
    {
        const u32 inv = 255u - a;
        const u32 rb = (((dst & 0x00FF00FFu) * inv + (src & 0x00FF00FFu) * a) >> 8) & 0x00FF00FFu;
        const u32 g  = (((dst & 0x0000FF00u) * inv + (src & 0x0000FF00u) * a) >> 8) & 0x0000FF00u;
        return 0xFF000000u | rb | g;
    }

    static void pauseFillRect(u32* dst, s32 width, s32 height, s32 x, s32 y, s32 w, s32 h, u32 color)
    {
        if (!dst || w <= 0 || h <= 0) return;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > width)  w = width - x;
        if (y + h > height) h = height - y;
        if (w <= 0 || h <= 0) return;

        for (s32 yy = 0; yy < h; yy++)
        {
            u32* row = dst + (y + yy) * width + x;
            for (s32 xx = 0; xx < w; xx++) row[xx] = color;
        }
    }

    static void pauseDim(u32* dst, s32 width, s32 height)
    {
        const u32 pixels = (u32)(width * height);
        for (u32 i = 0; i < pixels; i++)
        {
            dst[i] = pauseBlend(dst[i] | 0xFF000000u, XPAUSE_BLACK, 96);
        }
    }

    static void pauseFillPdaSourceRect(u32* dst, s32 width, s32 height, s32 frameX, s32 frameY, s32 frameW, s32 frameH, s32 sx, s32 sy, s32 sw, s32 sh, u32 color)
    {
        const s32 x0 = frameX + (sx * frameW) / XBOX_PDA_FRAME_WIDTH;
        const s32 y0 = frameY + (sy * frameH) / XBOX_PDA_FRAME_HEIGHT;
        const s32 x1 = frameX + ((sx + sw) * frameW) / XBOX_PDA_FRAME_WIDTH;
        const s32 y1 = frameY + ((sy + sh) * frameH) / XBOX_PDA_FRAME_HEIGHT;
        pauseFillRect(dst, width, height, x0, y0, x1 - x0, y1 - y0, color);
    }

    static void pauseFillPdaScreen(u32* dst, s32 width, s32 height, s32 frameX, s32 frameY, s32 frameW, s32 frameH, u32 color)
    {
        // The measured pause rect is in 640x400 art/screen coordinates. The
        // Xbox output is 640x480, so only Y needs conversion.
        const s32 y0 = (XPAUSE_SCREEN_SRC_Y * height) / XBOX_PDA_FRAME_HEIGHT;
        const s32 y1 = ((XPAUSE_SCREEN_SRC_Y + XPAUSE_SCREEN_SRC_H) * height) / XBOX_PDA_FRAME_HEIGHT;
        pauseFillRect(dst, width, height, XPAUSE_SCREEN_SRC_X, y0,
            XPAUSE_SCREEN_SRC_W, y1 - y0, color);
    }

    static void pauseDrawFrame(u32* dst, s32 width, s32 height, s32 x, s32 y, s32 w, s32 h)
    {
        const s32 frameW = w + 120;
        const s32 frameH = h + 60;
        const s32 frameX = x - 60;
        const s32 frameY = y - 30;

        pauseFillRect(dst, width, height, x + 8, y + 8, w, h, XPAUSE_GREY_DARK);
        pauseFillPdaScreen(dst, width, height, frameX, frameY, frameW, frameH, XPAUSE_GREEN_DARK);

        for (s32 dy = 0; dy < frameH; dy++)
        {
            const s32 py = frameY + dy;
            if (py < 0 || py >= height) continue;
            const s32 sy = (dy * XBOX_PDA_FRAME_HEIGHT) / frameH;
            for (s32 dx = 0; dx < frameW; dx++)
            {
                const s32 px = frameX + dx;
                if (px < 0 || px >= width) continue;
                const s32 sx = (dx * XBOX_PDA_FRAME_WIDTH) / frameW;
                const u32 src = c_xboxPdaFrame[sy * XBOX_PDA_FRAME_WIDTH + sx];
                const u32 a = (src >> 24) & 0xFFu;
                if (!a) continue;
                const s32 greenY0 = (XPAUSE_SCREEN_SRC_Y * height) / XBOX_PDA_FRAME_HEIGHT;
                const s32 greenY1 = ((XPAUSE_SCREEN_SRC_Y + XPAUSE_SCREEN_SRC_H) * height) / XBOX_PDA_FRAME_HEIGHT;
                if (px >= XPAUSE_SCREEN_SRC_X && px < XPAUSE_SCREEN_SRC_X + XPAUSE_SCREEN_SRC_W &&
                    py >= greenY0 && py < greenY1 &&
                    ((src & 0x00FFFFFFu) == 0))
                {
                    continue;
                }
                u32* pixel = dst + py * width + px;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, a);
            }
        }
    }

    static void pauseDrawTextRaw(u32* dst, s32 width, s32 height, XboxPauseTextId id, s32 x, s32 y, u32 primary, bool shadow)
    {
        const XboxPauseTextSprite* s = &c_xboxPauseText[id];
        const s32 ox = shadow ? 2 : 0;
        const s32 oy = shadow ? 2 : 0;
        for (s32 py = 0; py < s->height; py++)
        {
            const s32 dy = y + oy + py;
            if (dy < 0 || dy >= height) continue;
            for (s32 px = 0; px < s->width; px++)
            {
                const u8 cov = s->data[py * s->width + px];
                if (!cov) continue;
                const s32 dx = x + ox + px;
                if (dx < 0 || dx >= width) continue;
                const u32 src = shadow ? XPAUSE_BLACK : (cov >= 2 ? primary : (primary == XPAUSE_WHITE ? XPAUSE_GREY : XPAUSE_GREY_DARK));
                const u32 a = shadow ? (u32)(cov * 46) : (u32)(cov * 64);
                u32* pixel = dst + dy * width + dx;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, (u32)pauseClamp((s32)a, 0, 255));
            }
        }
    }

    static void pauseDrawText(u32* dst, s32 width, s32 height, XboxPauseTextId id, s32 x, s32 y, bool selected)
    {
        const u32 color = selected ? XPAUSE_WHITE : XPAUSE_GREY;
        pauseDrawTextRaw(dst, width, height, id, x, y, color, true);
        pauseDrawTextRaw(dst, width, height, id, x, y, color, false);
    }

    static void pauseDrawMenuRow(u32* dst, s32 width, s32 height, XboxPauseTextId id, s32 x, s32 y, bool selected)
    {
        if (selected)
        {
            const XboxPauseTextSprite* s = &c_xboxPauseText[id];
            const s32 rowW = (id == XPT_NO || id == XPT_YES) ? 115 : XPAUSE_ROW_WIDTH;
            pauseFillRect(dst, width, height, x - 24, y - 4, rowW, s->height + 8, XPAUSE_GREEN_MID);
            pauseDrawText(dst, width, height, XPT_ARROW, x - 18, y + ((s->height - c_xboxPauseText[XPT_ARROW].height) >> 1), true);
        }
        pauseDrawText(dst, width, height, id, x, y, selected);
    }

    static void cheatCompositeOverlay();

    static void pauseCompositeOverlay()
    {
        if (!s_pauseOverlayEnabled || !s_vdispWidth || !s_vdispHeight) return;
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        pauseDim(s_expandBuf, width, height);

        if (s_cheatScreenEnabled)
        {
            cheatCompositeOverlay();
            return;
        }

        const s32 boxW = XPAUSE_PANEL_WIDTH;
        const s32 boxH = XPAUSE_PANEL_HEIGHT;
        const s32 originX = (width - XPAUSE_DESIGN_WIDTH) / 2;
        const s32 originY = (height - XPAUSE_DESIGN_HEIGHT) / 2;
        const s32 boxX = originX + (XPAUSE_DESIGN_WIDTH - boxW) / 2;
        const s32 boxY = originY + (XPAUSE_DESIGN_HEIGHT - boxH) / 2;
        pauseDrawFrame(s_expandBuf, width, height, boxX, boxY, boxW, boxH);

        const s32 rowX = boxX + 105;
        const s32 firstY = boxY + 30;
        const s32 step = XPAUSE_ROW_STEP;
        if (s_pauseNotice != 0)
        {
            const bool saved = s_pauseNotice == 1;
            pauseDrawText(s_expandBuf, width, height, XPT_QUICK_SAVE,
                boxX + boxW / 2 - c_xboxPauseText[XPT_QUICK_SAVE].width / 2, boxY + 92, true);
            pauseDrawText(s_expandBuf, width, height, saved ? XPT_GAME_SAVED : XPT_SAVE_FAILED,
                boxX + boxW / 2 - c_xboxPauseText[saved ? XPT_GAME_SAVED : XPT_SAVE_FAILED].width / 2, boxY + 138, saved);
            pauseDrawText(s_expandBuf, width, height, XPT_PRESS_A,
                boxX + boxW / 2 - c_xboxPauseText[XPT_PRESS_A].width / 2, boxY + 195, false);
        }
        else if (!s_pauseConfirmOpen)
        {
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_RESUME,  rowX, firstY + step * 0, s_pauseSelection == 0);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_DATAPAD, rowX, firstY + step * 1, s_pauseSelection == 1);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_ABORT,   rowX, firstY + step * 2, s_pauseSelection == 2);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_QUICK_SAVE, rowX, firstY + step * 3, s_pauseSelection == 3);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_OPTIONS, rowX, firstY + step * 4, s_pauseSelection == 4);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_CHEAT,   rowX, firstY + step * 5, s_pauseSelection == 5);
        }
        else
        {
            pauseDrawText(s_expandBuf, width, height, XPT_ARE_YOU_SURE, boxX + boxW / 2 - c_xboxPauseText[XPT_ARE_YOU_SURE].width / 2, boxY + 88, true);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_NO,  boxX + boxW / 2 - 90, boxY + 145, s_pauseConfirmSelection == 0);
            pauseDrawMenuRow(s_expandBuf, width, height, XPT_YES, boxX + boxW / 2 + 35, boxY + 145, s_pauseConfirmSelection == 1);
        }
    }

    static void loadDrawTextCenter(u32* dst, s32 width, s32 height, const char* text, s32 centerX, s32 y, s32 scale, u32 color);

    static f32 wrapAngleDeg(f32 a)
    {
        while (a < -180.0f) a += 360.0f;
        while (a >  180.0f) a -= 360.0f;
        return a;
    }

    static bool angleInRange(f32 a, f32 start, f32 end)
    {
        a = wrapAngleDeg(a);
        start = wrapAngleDeg(start);
        end = wrapAngleDeg(end);
        if (start <= end) return a >= start && a <= end;
        return a >= start || a <= end;
    }

    static u32 weaponWheelDimColor(u32 color, u32 dim)
    {
        const u32 a = color & 0xFF000000u;
        const u32 r = (((color >> 16) & 0xFFu) * dim) / 255u;
        const u32 g = (((color >> 8) & 0xFFu) * dim) / 255u;
        const u32 b = ((color & 0xFFu) * dim) / 255u;
        return a | (r << 16) | (g << 8) | b;
    }

    static void weaponWheelDrawIcon(const u32* icon, s32 srcW, s32 srcH, s32 centerX, s32 centerY, s32 dstW, s32 dstH, bool shadow, bool available)
    {
        if (!icon || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        const s32 x0 = centerX - dstW / 2;
        const s32 y0 = centerY - dstH / 2;
        for (s32 y = 0; y < dstH; y++)
        {
            const s32 dy = y0 + y;
            if (dy < 0 || dy >= height) continue;
            const s32 sy = (y * srcH) / dstH;
            for (s32 x = 0; x < dstW; x++)
            {
                const s32 dx = x0 + x;
                if (dx < 0 || dx >= width) continue;
                const s32 sx = (x * srcW) / dstW;
                const u32 src = icon[sy * srcW + sx];
                const u32 srcAlpha = (src >> 24) & 0xFFu;
                if (!srcAlpha) continue;
                u32* dst = s_expandBuf + dy * width + dx;
                if (shadow)
                {
                    *dst = pauseBlend(*dst | 0xFF000000u, 0xFF151515u, (srcAlpha * 150u) / 255u);
                }
                else
                {
                    const u32 color = available ? src : weaponWheelDimColor(src, 78u);
                    *dst = pauseBlend(*dst | 0xFF000000u, color | 0xFF000000u, 255u);
                }
            }
        }
    }

    static const XboxWheelGlyph* wheelFindGlyph(char c)
    {
        for (s32 i = 0; i < XBOX_WHEEL_GLYPH_COUNT; i++)
        {
            if (c_xboxWheelGlyphs[i].ch == c)
            {
                return &c_xboxWheelGlyphs[i];
            }
        }
        return NULL;
    }

    static s32 wheelTextWidth(const char* text)
    {
        if (!text) return 0;
        s32 width = 0;
        while (*text)
        {
            const XboxWheelGlyph* g = wheelFindGlyph(*text++);
            width += g ? g->xAdvance : 10;
        }
        return width;
    }

    static void wheelDrawTextRaw(const char* text, s32 x, s32 baselineY, u32 primary, bool shadow)
    {
        if (!text) return;
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        const s32 ox = shadow ? 2 : 0;
        const s32 oy = shadow ? 2 : 0;
        s32 penX = x + ox;
        while (*text)
        {
            const XboxWheelGlyph* g = wheelFindGlyph(*text++);
            if (!g)
            {
                penX += 10;
                continue;
            }
            const s32 gx = penX + g->xOffset;
            const s32 gy = baselineY + oy + g->yOffset;
            for (s32 py = 0; py < g->height; py++)
            {
                const s32 dy = gy + py;
                if (dy < 0 || dy >= height) continue;
                for (s32 px = 0; px < g->width; px++)
                {
                    const u8 cov = g->data[py * g->width + px];
                    if (!cov) continue;
                    const s32 dx = gx + px;
                    if (dx < 0 || dx >= width) continue;
                    const u32 src = shadow ? XPAUSE_BLACK : primary;
                    const u32 a = shadow ? (u32)(cov * 52) : (u32)(cov * 72);
                    u32* pixel = s_expandBuf + dy * width + dx;
                    *pixel = pauseBlend(*pixel | 0xFF000000u, src, (u32)pauseClamp((s32)a, 0, 255));
                }
            }
            penX += g->xAdvance;
        }
    }

    static void wheelDrawTextCenter(const char* text, s32 centerX, s32 baselineY, u32 color)
    {
        const s32 x = centerX - wheelTextWidth(text) / 2;
        wheelDrawTextRaw(text, x, baselineY, color, true);
        wheelDrawTextRaw(text, x, baselineY, color, false);
    }

    static void wheelDrawTextRight(const char* text, s32 rightX, s32 baselineY, u32 color)
    {
        const s32 x = rightX - wheelTextWidth(text);
        wheelDrawTextRaw(text, x, baselineY, color, true);
        wheelDrawTextRaw(text, x, baselineY, color, false);
    }

    static s32 wheelTextWidthScaled(const char* text, s32 num, s32 den)
    {
        return (wheelTextWidth(text) * num) / den;
    }

    static void wheelTextBoundsScaled(const char* text, s32 num, s32 den, s32* top, s32* bottom)
    {
        s32 minY = 0;
        s32 maxY = 0;
        bool found = false;
        while (text && *text)
        {
            const XboxWheelGlyph* g = wheelFindGlyph(*text++);
            if (!g) continue;
            const s32 gy = (g->yOffset * num) / den;
            const s32 dh = pauseClamp((g->height * num) / den, 1, 128);
            if (!found)
            {
                minY = gy;
                maxY = gy + dh;
                found = true;
            }
            else
            {
                if (gy < minY) minY = gy;
                if (gy + dh > maxY) maxY = gy + dh;
            }
        }
        if (!found)
        {
            minY = 0;
            maxY = pauseClamp((18 * num) / den, 1, 128);
        }
        if (top) *top = minY;
        if (bottom) *bottom = maxY;
    }

    static s32 wheelTextBaselineForCenter(const char* text, s32 centerY, s32 num, s32 den)
    {
        s32 top = 0;
        s32 bottom = 0;
        wheelTextBoundsScaled(text, num, den, &top, &bottom);
        return centerY - ((top + bottom) / 2);
    }

    static s32 loadTextYForCenter(s32 centerY, s32 scale)
    {
        return centerY - ((7 * scale) / 2);
    }

    static void wheelDrawTextRawScaledTo(u32* dst, s32 width, s32 height, const char* text, s32 x, s32 baselineY, u32 primary, bool shadow, s32 num, s32 den)
    {
        if (!dst || !text || num <= 0 || den <= 0) return;
        const s32 ox = shadow ? 1 : 0;
        const s32 oy = shadow ? 1 : 0;
        s32 penX = x + ox;
        while (*text)
        {
            const XboxWheelGlyph* g = wheelFindGlyph(*text++);
            if (!g)
            {
                penX += (10 * num) / den;
                continue;
            }
            const s32 gx = penX + (g->xOffset * num) / den;
            const s32 gy = baselineY + oy + (g->yOffset * num) / den;
            const s32 dw = pauseClamp((g->width * num) / den, 1, 128);
            const s32 dh = pauseClamp((g->height * num) / den, 1, 128);
            for (s32 py = 0; py < dh; py++)
            {
                const s32 dy = gy + py;
                if (dy < 0 || dy >= height) continue;
                const s32 sy = pauseClamp((py * den) / num, 0, g->height - 1);
                for (s32 px = 0; px < dw; px++)
                {
                    const s32 dx = gx + px;
                    if (dx < 0 || dx >= width) continue;
                    const s32 sx = pauseClamp((px * den) / num, 0, g->width - 1);
                    const u8 cov = g->data[sy * g->width + sx];
                    if (!cov) continue;
                    const u32 src = shadow ? XPAUSE_BLACK : primary;
                    const u32 a = shadow ? (u32)(cov * 46) : (u32)(cov * 72);
                    u32* pixel = dst + dy * width + dx;
                    *pixel = pauseBlend(*pixel | 0xFF000000u, src, (u32)pauseClamp((s32)a, 0, 255));
                }
            }
            penX += (g->xAdvance * num) / den;
        }
    }

    static void wheelDrawTextRawScaled(const char* text, s32 x, s32 baselineY, u32 primary, bool shadow, s32 num, s32 den)
    {
        wheelDrawTextRawScaledTo(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight,
            text, x, baselineY, primary, shadow, num, den);
    }

    static void wheelDrawTextScaled(const char* text, s32 x, s32 baselineY, u32 color, s32 num, s32 den)
    {
        wheelDrawTextRawScaled(text, x, baselineY, color, true, num, den);
        wheelDrawTextRawScaled(text, x, baselineY, color, false, num, den);
    }

    static void wheelDrawTextScaledTo(u32* dst, s32 width, s32 height, const char* text, s32 x, s32 baselineY, u32 color, s32 num, s32 den)
    {
        wheelDrawTextRawScaledTo(dst, width, height, text, x, baselineY, color, true, num, den);
        wheelDrawTextRawScaledTo(dst, width, height, text, x, baselineY, color, false, num, den);
    }

    static void wheelDrawTextCenterScaled(const char* text, s32 centerX, s32 baselineY, u32 color, s32 num, s32 den)
    {
        const s32 x = centerX - wheelTextWidthScaled(text, num, den) / 2;
        wheelDrawTextScaled(text, x, baselineY, color, num, den);
    }

    static void wheelDrawTextCenterScaledTo(u32* dst, s32 width, s32 height, const char* text, s32 centerX, s32 baselineY, u32 color, s32 num, s32 den)
    {
        const s32 x = centerX - wheelTextWidthScaled(text, num, den) / 2;
        wheelDrawTextScaledTo(dst, width, height, text, x, baselineY, color, num, den);
    }

    static void wheelDrawTextRightScaled(const char* text, s32 rightX, s32 baselineY, u32 color, s32 num, s32 den)
    {
        const s32 x = rightX - wheelTextWidthScaled(text, num, den);
        wheelDrawTextScaled(text, x, baselineY, color, num, den);
    }

    static void wheelDrawTextRightScaledTo(u32* dst, s32 width, s32 height, const char* text, s32 rightX, s32 baselineY, u32 color, s32 num, s32 den)
    {
        const s32 x = rightX - wheelTextWidthScaled(text, num, den);
        wheelDrawTextScaledTo(dst, width, height, text, x, baselineY, color, num, den);
    }

    static void weaponWheelDrawSegment(s32 index, f32 centerDeg)
    {
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        if (width <= 0 || height <= 0) return;

        const f32 scale = (f32)height / 480.0f;
        const f32 cx = (f32)width * 0.5f;
        const f32 cy = (f32)height * 0.5f;
        const f32 inner = 56.0f * scale;
        const f32 outer = 116.0f * scale;
        const f32 start = centerDeg - 12.0f;
        const f32 end = centerDeg + 12.0f;
        const bool selected = index == s_weaponWheelInfo.selected;
        const bool current = index == s_weaponWheelInfo.current;
        const bool available = s_weaponWheelInfo.available[index];
        const u32 fill = !available ? (selected ? 0x77424242u : 0x55343434u) : (selected ? 0xCCBFC8C8u : (current ? 0xAA8DA6B8u : 0x99959B9Bu));
        const u32 edge = !available ? (selected ? 0xFF777777u : 0xFF303030u) : (selected ? 0xFFE8F0F0u : 0xFF656B6Bu);

        const s32 x0 = (s32)(cx - outer - 4);
        const s32 x1 = (s32)(cx + outer + 4);
        const s32 y0 = (s32)(cy - outer - 4);
        const s32 y1 = (s32)(cy + outer + 4);
        for (s32 y = y0; y <= y1; y++)
        {
            if (y < 0 || y >= height) continue;
            for (s32 x = x0; x <= x1; x++)
            {
                if (x < 0 || x >= width) continue;
                const f32 dx = (f32)x - cx;
                const f32 dy = cy - (f32)y;
                const f32 r2 = dx * dx + dy * dy;
                if (r2 < inner * inner || r2 > outer * outer) continue;
                const f32 a = atan2f(dy, dx) * 57.2957795f;
                if (!angleInRange(a, start, end)) continue;

                const f32 r = sqrtf(r2);
                const f32 startDelta = fabsf(wrapAngleDeg(a - start));
                const f32 endDelta = fabsf(wrapAngleDeg(a - end));
                const bool border = r < inner + 3.0f * scale || r > outer - 3.0f * scale ||
                    startDelta < 1.5f || endDelta < 1.5f;
                u32* pixel = s_expandBuf + y * width + x;
                *pixel = pauseBlend(*pixel | 0xFF000000u, border ? edge : fill, border ? 210u : 145u);
            }
        }

        if (s_weaponWheelInfo.icons[index])
        {
            const f32 labelR = (inner + outer) * 0.5f;
            const f32 rad = centerDeg * 0.0174532925f;
            const s32 tx = (s32)(cx + cosf(rad) * labelR);
            const s32 ty = (s32)(cy - sinf(rad) * labelR);
            s32 iconSize = (s32)(44.0f * scale);
            if (index == 3 || index == 7)
            {
                iconSize = (iconSize * 7) / 10;
            }
            weaponWheelDrawIcon(s_weaponWheelInfo.icons[index], s_weaponWheelInfo.iconWidth, s_weaponWheelInfo.iconHeight, tx + 1, ty + 2, iconSize, iconSize, true, available);
            weaponWheelDrawIcon(s_weaponWheelInfo.icons[index], s_weaponWheelInfo.iconWidth, s_weaponWheelInfo.iconHeight, tx, ty, iconSize, iconSize, false, available);
        }
    }

    static void weaponWheelComposite()
    {
        if (!s_weaponWheelEnabled || !s_vdispWidth || !s_vdispHeight) return;
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        const u32 pixels = (u32)(width * height);
        for (u32 i = 0; i < pixels; i++)
        {
            s_expandBuf[i] = pauseBlend(s_expandBuf[i] | 0xFF000000u, 0xFF10356Bu, 54u);
        }

        static const f32 centers[10] = { 120.0f, 150.0f, 180.0f, -150.0f, -120.0f, 60.0f, 30.0f, 0.0f, -30.0f, -60.0f };
        for (s32 i = 0; i < 10; i++)
        {
            weaponWheelDrawSegment(i, centers[i]);
        }
        const s32 cx = width / 2;
        const s32 cy = height / 2;
        pauseFillRect(s_expandBuf, width, height, cx - 1, cy - 10, 2, 20, 0xFFE6E6E6u);
        pauseFillRect(s_expandBuf, width, height, cx - 10, cy - 1, 20, 2, 0xFFE6E6E6u);

        if (s_weaponWheelInfo.selectedName && s_weaponWheelInfo.selectedName[0])
        {
            char label[96];
            if (s_weaponWheelInfo.selectedAmmo && s_weaponWheelInfo.selectedAmmo[0])
            {
                sprintf(label, "%s (%s)", s_weaponWheelInfo.selectedName, s_weaponWheelInfo.selectedAmmo);
            }
            else
            {
                sprintf(label, "%s", s_weaponWheelInfo.selectedName);
            }
            const bool selectedAvailable =
                s_weaponWheelInfo.selected >= 0 &&
                s_weaponWheelInfo.selected < 10 &&
                s_weaponWheelInfo.available[s_weaponWheelInfo.selected];
            wheelDrawTextCenter(label, cx, cy + (s32)(151.0f * ((f32)height / 480.0f)), selectedAvailable ? 0xFFE8E8E8u : 0xFF777777u);
        }
    }

    static u32 startHash(u32 v)
    {
        v ^= v >> 16;
        v *= 0x7feb352du;
        v ^= v >> 15;
        v *= 0x846ca68bu;
        v ^= v >> 16;
        return v;
    }

    static void startPutPixel(u32* dst, s32 width, s32 height, s32 x, s32 y, u32 color)
    {
        if (!dst || x < 0 || y < 0 || x >= width || y >= height) return;
        dst[y * width + x] = color;
    }

    static void startDrawStarfield(u32* dst, s32 width, s32 height, u32 frame)
    {
        pauseFillRect(dst, width, height, 0, 0, width, height, 0xFF000000u);
        const s32 cx = width / 2;
        const s32 cy = height / 2;
        for (u32 i = 0; i < 360; i++)
        {
            const u32 h = startHash(i * 97u + 13u);
            const u32 h2 = startHash(h ^ (frame * 1103515245u));
            const s32 sx = (s32)((h & 2047u) - 1024);
            const s32 sy = (s32)(((h >> 11) & 2047u) - 1024);
            const u32 speed = 2u + ((h >> 24) & 5u);
            const s32 phase = (s32)((((h >> 18) & 255u) + (frame * speed * 7u) / 10u) & 255u);
            const s32 wobbleX = (s32)((h2 & 7u) - 3);
            const s32 wobbleY = (s32)(((h2 >> 3) & 7u) - 3);
            const s32 x = cx + (sx * phase) / 132 + wobbleX;
            const s32 y = cy + (sy * phase) / 132 + wobbleY;
            if (x < 0 || y < 0 || x >= width || y >= height) continue;

            const u32 b = 72u + (u32)((phase * 183) / 255);
            const u32 color = 0xFF000000u | (b << 16) | (b << 8) | b;
            const s32 size = phase > 218 ? 3 : (phase > 150 ? 2 : 1);
            pauseFillRect(dst, width, height, x, y, size, size, color);
            if (phase > 235)
            {
                startPutPixel(dst, width, height, x - 1, y, color);
                startPutPixel(dst, width, height, x + size, y, color);
                startPutPixel(dst, width, height, x, y - 1, color);
                startPutPixel(dst, width, height, x, y + size, color);
            }
        }
    }

    static void startDrawLogo(u32* dst, s32 width, s32 height)
    {
        const s32 x0 = (width - XBOX_START_LOGO_WIDTH) / 2;
        const s32 y0 = 34;
        for (s32 y = 0; y < XBOX_START_LOGO_HEIGHT; y++)
        {
            const s32 dy = y0 + y;
            if (dy < 0 || dy >= height) continue;
            for (s32 x = 0; x < XBOX_START_LOGO_WIDTH; x++)
            {
                const u32 src = c_xboxStartLogo[y * XBOX_START_LOGO_WIDTH + x];
                const u32 a = src >> 24;
                if (!a) continue;
                const s32 dx = x0 + x;
                if (dx < 0 || dx >= width) continue;
                u32* pixel = dst + dy * width + dx;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src | 0xFF000000u, a);
            }
        }
    }

    static void startDrawTextSpriteRaw(u32* dst, s32 width, s32 height, XboxStartTextId id, s32 x, s32 y, u32 primary, bool shadow)
    {
        const XboxStartTextSprite* s = &c_xboxStartText[id];
        const s32 ox = shadow ? 2 : 0;
        const s32 oy = shadow ? 2 : 0;
        for (s32 py = 0; py < s->height; py++)
        {
            const s32 dy = y + oy + py;
            if (dy < 0 || dy >= height) continue;
            for (s32 px = 0; px < s->width; px++)
            {
                const u8 cov = s->data[py * s->width + px];
                if (!cov) continue;
                const s32 dx = x + ox + px;
                if (dx < 0 || dx >= width) continue;
                const u32 src = shadow ? XPAUSE_BLACK : primary;
                const u32 a = shadow ? (u32)(cov * 44) : (u32)(cov * 64);
                u32* pixel = dst + dy * width + dx;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, (u32)pauseClamp((s32)a, 0, 255));
            }
        }
    }

    static void startDrawTextSprite(u32* dst, s32 width, s32 height, XboxStartTextId id, s32 x, s32 y, u32 color, bool glow)
    {
        if (glow)
        {
            startDrawTextSpriteRaw(dst, width, height, id, x - 2, y, 0xFF3A0000u, false);
            startDrawTextSpriteRaw(dst, width, height, id, x + 2, y, 0xFF3A0000u, false);
            startDrawTextSpriteRaw(dst, width, height, id, x, y - 2, 0xFF3A0000u, false);
            startDrawTextSpriteRaw(dst, width, height, id, x, y + 2, 0xFF3A0000u, false);
        }
        startDrawTextSpriteRaw(dst, width, height, id, x, y, color, true);
        startDrawTextSpriteRaw(dst, width, height, id, x, y, color, false);
    }

    static void footerDrawTextRaw(XboxFooterTextId id, s32 x, s32 y, u32 primary, bool shadow)
    {
        const XboxFooterTextSprite* s = &c_xboxFooterText[id];
        const s32 ox = shadow ? 2 : 0;
        const s32 oy = shadow ? 2 : 0;
        for (s32 py = 0; py < s->height; py++)
        {
            const s32 dy = y + oy + py;
            if (dy < 0 || dy >= XBOX_OUTPUT_HEIGHT) continue;
            for (s32 px = 0; px < s->width; px++)
            {
                const u8 cov = s->data[py * s->width + px];
                if (!cov) continue;
                const s32 dx = x + ox + px;
                if (dx < 0 || dx >= XBOX_OUTPUT_WIDTH) continue;
                const u32 src = shadow ? XPAUSE_BLACK : primary;
                const u32 a = shadow ? (u32)(cov * 44) : (u32)(cov * 64);
                u32* pixel = s_expandBuf + dy * XBOX_OUTPUT_WIDTH + dx;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, (u32)pauseClamp((s32)a, 0, 255));
            }
        }
    }

    static void footerDrawText(XboxFooterTextId id, s32 x, s32 y, u32 color)
    {
        footerDrawTextRaw(id, x, y, color, true);
        footerDrawTextRaw(id, x, y, color, false);
    }

    static s32 dukeIconWidthForHeight(XboxDukeButtonIconId id, s32 targetH)
    {
        if (id < 0 || id >= XDB_COUNT || targetH <= 0) return 0;
        const XboxDukeButtonIcon* icon = &c_xboxDukeButtons[id];
        return (icon->width * targetH + icon->height / 2) / icon->height;
    }

    static void dukeDrawIconTo(u32* dst, s32 dstW, s32 dstH, XboxDukeButtonIconId id, s32 x, s32 y, s32 targetH, u32 tint)
    {
        if (!dst || id < 0 || id >= XDB_COUNT || targetH <= 0) return;
        const XboxDukeButtonIcon* icon = &c_xboxDukeButtons[id];
        const s32 targetW = dukeIconWidthForHeight(id, targetH);
        if (targetW <= 0) return;

        const u32 tintA = (tint >> 24) & 0xFFu;
        const u32 tintR = (tint >> 16) & 0xFFu;
        const u32 tintG = (tint >> 8) & 0xFFu;
        const u32 tintB = tint & 0xFFu;
        const bool useTint = tint != 0xFFFFFFFFu;

        for (s32 dy = 0; dy < targetH; dy++)
        {
            const s32 py = y + dy;
            if (py < 0 || py >= dstH) continue;
            const s32 sy = (dy * icon->height) / targetH;
            for (s32 dx = 0; dx < targetW; dx++)
            {
                const s32 px = x + dx;
                if (px < 0 || px >= dstW) continue;
                const s32 sx = (dx * icon->width) / targetW;
                u32 src = icon->data[sy * icon->width + sx];
                u32 a = (src >> 24) & 0xFFu;
                if (!a) continue;
                if (useTint)
                {
                    const u32 r = (((src >> 16) & 0xFFu) * tintR) / 255u;
                    const u32 g = (((src >> 8) & 0xFFu) * tintG) / 255u;
                    const u32 b = ((src & 0xFFu) * tintB) / 255u;
                    a = (a * tintA) / 255u;
                    src = (a << 24) | (r << 16) | (g << 8) | b;
                }
                u32* pixel = dst + py * dstW + px;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, a);
            }
        }
    }

    static void dukeDrawIcon(XboxDukeButtonIconId id, s32 x, s32 y, s32 targetH)
    {
        dukeDrawIconTo(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, id, x, y, targetH, 0xFFFFFFFFu);
    }

    static XboxDukeButtonIconId footerIconForItem(XboxFooterTextId id)
    {
        switch (id)
        {
            case XFT_A_SELECT:
            case XFT_A_LOAD:
            case XFT_A_START:
            case XFT_A_APPLY:
            case XFT_A_CONFIRM:
                return XDB_A;
            case XFT_B_BACK:
            case XFT_B_ABORT:
                return XDB_B;
            case XFT_X_RESUME:
            case XFT_X_DIFF_EASY:
            case XFT_X_DIFF_MEDIUM:
            case XFT_X_DIFF_HARD:
                return XDB_X;
            case XFT_DPAD_ADJUST:
                return XDB_DPAD;
            default:
                return XDB_A;
        }
    }

    static void footerDrawBar(u32 ruleColor)
    {
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 0, 424, XBOX_OUTPUT_WIDTH, 56, XPAUSE_BLACK);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 0, 422, XBOX_OUTPUT_WIDTH, 1, ruleColor);
    }

    static void footerDrawItem(XboxFooterTextId id, s32 x, u32 color)
    {
        const s32 iconH = (id == XFT_DPAD_ADJUST) ? 19 : 18;
        const XboxDukeButtonIconId icon = footerIconForItem(id);
        dukeDrawIcon(icon, x, 441, iconH);
        footerDrawText(id, x + dukeIconWidthForHeight(icon, iconH) + 8, 443, color);
    }

    static const char* loadGlyphRows(char c, s32 row)
    {
        static const char* sp[7] = { "00000","00000","00000","00000","00000","00000","00000" };
        static const char* dash[7]={ "00000","00000","00000","11111","00000","00000","00000" };
        static const char* dot[7] = { "00000","00000","00000","00000","00000","01100","01100" };
        static const char* apos[7]= { "01100","01100","00100","00000","00000","00000","00000" };
        static const char* slash[7]={"00001","00010","00010","00100","01000","01000","10000" };
        static const char* colon[7]={"00000","01100","01100","00000","01100","01100","00000" };
        static const char* zero[7]={ "01110","10001","10011","10101","11001","10001","01110" };
        static const char* one[7] = { "00100","01100","00100","00100","00100","00100","01110" };
        static const char* two[7] = { "01110","10001","00001","00010","00100","01000","11111" };
        static const char* three[7]={"11110","00001","00001","01110","00001","00001","11110" };
        static const char* four[7]= { "00010","00110","01010","10010","11111","00010","00010" };
        static const char* five[7]= { "11111","10000","10000","11110","00001","00001","11110" };
        static const char* six[7] = { "01110","10000","10000","11110","10001","10001","01110" };
        static const char* seven[7]={"11111","00001","00010","00100","01000","01000","01000" };
        static const char* eight[7]={"01110","10001","10001","01110","10001","10001","01110" };
        static const char* nine[7]= { "01110","10001","10001","01111","00001","00001","01110" };
        static const char* a[7]   = { "01110","10001","10001","11111","10001","10001","10001" };
        static const char* b[7]   = { "11110","10001","10001","11110","10001","10001","11110" };
        static const char* c_[7]  = { "01111","10000","10000","10000","10000","10000","01111" };
        static const char* d[7]   = { "11110","10001","10001","10001","10001","10001","11110" };
        static const char* e[7]   = { "11111","10000","10000","11110","10000","10000","11111" };
        static const char* f[7]   = { "11111","10000","10000","11110","10000","10000","10000" };
        static const char* g[7]   = { "01111","10000","10000","10111","10001","10001","01111" };
        static const char* h[7]   = { "10001","10001","10001","11111","10001","10001","10001" };
        static const char* i_[7]  = { "11111","00100","00100","00100","00100","00100","11111" };
        static const char* j[7]   = { "00111","00010","00010","00010","10010","10010","01100" };
        static const char* k[7]   = { "10001","10010","10100","11000","10100","10010","10001" };
        static const char* l[7]   = { "10000","10000","10000","10000","10000","10000","11111" };
        static const char* m[7]   = { "10001","11011","10101","10101","10001","10001","10001" };
        static const char* n[7]   = { "10001","11001","10101","10011","10001","10001","10001" };
        static const char* o[7]   = { "01110","10001","10001","10001","10001","10001","01110" };
        static const char* p[7]   = { "11110","10001","10001","11110","10000","10000","10000" };
        static const char* q[7]   = { "01110","10001","10001","10001","10101","10010","01101" };
        static const char* r[7]   = { "11110","10001","10001","11110","10100","10010","10001" };
        static const char* s[7]   = { "01111","10000","10000","01110","00001","00001","11110" };
        static const char* t[7]   = { "11111","00100","00100","00100","00100","00100","00100" };
        static const char* u[7]   = { "10001","10001","10001","10001","10001","10001","01110" };
        static const char* v[7]   = { "10001","10001","10001","10001","01010","01010","00100" };
        static const char* w[7]   = { "10001","10001","10001","10101","10101","10101","01010" };
        static const char* x[7]   = { "10001","01010","00100","00100","00100","01010","10001" };
        static const char* y[7]   = { "10001","01010","00100","00100","00100","00100","00100" };
        static const char* z[7]   = { "11111","00001","00010","00100","01000","10000","11111" };
        const char* const* glyph = sp;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        switch (c)
        {
            case '-': glyph = dash; break; case '.': glyph = dot; break; case '\'': glyph = apos; break; case '/': glyph = slash; break; case ':': glyph = colon; break;
            case '0': glyph = zero; break; case '1': glyph = one; break; case '2': glyph = two; break; case '3': glyph = three; break; case '4': glyph = four; break;
            case '5': glyph = five; break; case '6': glyph = six; break; case '7': glyph = seven; break; case '8': glyph = eight; break; case '9': glyph = nine; break;
            case 'A': glyph = a; break; case 'B': glyph = b; break; case 'C': glyph = c_; break; case 'D': glyph = d; break; case 'E': glyph = e; break; case 'F': glyph = f; break;
            case 'G': glyph = g; break; case 'H': glyph = h; break; case 'I': glyph = i_; break; case 'J': glyph = j; break; case 'K': glyph = k; break; case 'L': glyph = l; break;
            case 'M': glyph = m; break; case 'N': glyph = n; break; case 'O': glyph = o; break; case 'P': glyph = p; break; case 'Q': glyph = q; break; case 'R': glyph = r; break;
            case 'S': glyph = s; break; case 'T': glyph = t; break; case 'U': glyph = u; break; case 'V': glyph = v; break; case 'W': glyph = w; break; case 'X': glyph = x; break;
            case 'Y': glyph = y; break; case 'Z': glyph = z; break; default: glyph = sp; break;
        }
        return glyph[row];
    }

    static s32 loadTextWidth(const char* text, s32 scale)
    {
        s32 count = 0;
        while (text && text[count]) count++;
        return count * 6 * scale;
    }

    static void loadDrawText(u32* dst, s32 width, s32 height, const char* text, s32 x, s32 y, s32 scale, u32 color)
    {
        if (!text) return;
        s32 cx = x;
        for (const char* p = text; *p; p++, cx += 6 * scale)
        {
            for (s32 row = 0; row < 7; row++)
            {
                const char* bits = loadGlyphRows(*p, row);
                for (s32 col = 0; col < 5; col++)
                {
                    if (bits[col] != '1') continue;
                    pauseFillRect(dst, width, height, cx + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }

    static void loadDrawTextRight(u32* dst, s32 width, s32 height, const char* text, s32 rightX, s32 y, s32 scale, u32 color)
    {
        if (!text) return;
        loadDrawText(dst, width, height, text, rightX - loadTextWidth(text, scale), y, scale, color);
    }

    static void loadDrawTextCenter(u32* dst, s32 width, s32 height, const char* text, s32 centerX, s32 y, s32 scale, u32 color)
    {
        if (!text) return;
        loadDrawText(dst, width, height, text, centerX - loadTextWidth(text, scale) / 2, y, scale, color);
    }

    static const char* pdaModeName(s32 mode)
    {
        switch (mode)
        {
            case 0: return "MAP";
            case 1: return "WEAPONS";
            case 2: return "INVENTORY";
            case 3: return "OBJECTIVES";
            case 4: return "MISSION";
            default: return "DATAPAD";
        }
    }

    static void pdaDrawSmallText(const char* text, s32 x, s32 y, u32 color)
    {
        wheelDrawTextRaw(text, x, y - 8, color, true);
        wheelDrawTextRaw(text, x, y - 8, color, false);
    }

    static void pdaDrawMiniText(const char* text, s32 x, s32 y, u32 color)
    {
        loadDrawText(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, text, x + 1, y + 1, 2, 0xFF050505u);
        loadDrawText(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, text, x, y, 2, color);
    }

    static void pdaDrawControlLine(const char* key, const char* text, s32 x, s32 y, u32 keyColor)
    {
        const s32 keyW = wheelTextWidth(key) + 10;
        pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y - 14, keyW, 18, 0xFF050A06u);
        pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y - 14, keyW, 1, keyColor);
        pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y + 3, keyW, 1, keyColor);
        pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y - 14, 1, 18, keyColor);
        pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x + keyW - 1, y - 14, 1, 18, keyColor);
        pdaDrawSmallText(key, x + 5, y, keyColor);
        pdaDrawSmallText(text, x + keyW + 8, y, 0xFFE0D8B8u);
    }

    static void pdaDrawFrameImage(s32 dstX, s32 dstY, s32 dstW, s32 dstH)
    {
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        if (dstW <= 0 || dstH <= 0) return;
        for (s32 dy0 = 0; dy0 < dstH; dy0++)
        {
            const s32 dy = dstY + dy0;
            if (dy < 0 || dy >= height) continue;
            const s32 sy = (dy0 * XBOX_PDA_FRAME_HEIGHT) / dstH;
            for (s32 dx0 = 0; dx0 < dstW; dx0++)
            {
                const s32 sx = (dx0 * XBOX_PDA_FRAME_WIDTH) / dstW;
                if (sx >= XPDA_SCREEN_SRC_X && sx < XPDA_SCREEN_SRC_X + XPDA_SCREEN_SRC_W &&
                    sy >= XPDA_SCREEN_SRC_Y && sy < XPDA_SCREEN_SRC_Y + XPDA_SCREEN_SRC_H)
                {
                    continue;
                }
                const s32 dx = dstX + dx0;
                if (dx < 0 || dx >= width) continue;
                const u32 src = c_xboxPdaFrame[sy * XBOX_PDA_FRAME_WIDTH + sx];
                const u32 a = (src >> 24) & 0xFFu;
                if (!a) continue;
                u32* pixel = s_expandBuf + dy * width + dx;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, a);
            }
        }
    }

    static s32 pdaScaleFrameY(s32 sourceY)
    {
        return (sourceY * (s32)s_vdispHeight) / XBOX_PDA_FRAME_HEIGHT;
    }

    static void pdaDrawNativeTab(const char* label, s32 x, s32 y, s32 w, s32 h, bool selected)
    {
        if (selected)
        {
            pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y, w, h, XPAUSE_GREEN_MID);
            pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y, w, 2, 0xFF26E026u);
            pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y + h - 2, w, 2, 0xFF0D4E0Du);
            pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x, y, 2, h, 0xFF26E026u);
            pauseFillRect(s_expandBuf, (s32)s_vdispWidth, (s32)s_vdispHeight, x + w - 2, y, 2, h, 0xFF0D4E0Du);
        }
        wheelDrawTextCenterScaled(label, x + w / 2, pdaScaleFrameY(358), selected ? XPAUSE_WHITE : 0xFF8FB28Fu, 7, 10);
    }

    static void pdaDrawLayerStackKey(s32 x, s32 y)
    {
        const u32 edge = 0xFFFF9A22u;
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;

        {
            const s32 iconH = 20;
            const s32 iconW = dukeIconWidthForHeight(XDB_WHITE, iconH);
            dukeDrawIconTo(s_expandBuf, width, height, XDB_WHITE, x + 32 - iconW / 2, y - 2, iconH, 0xFFFFFFFFu);
        }
        for (s32 r = 0; r < 14; r++)
        {
            pauseFillRect(s_expandBuf, width, height, x + 31 - r, y + 28 + r, r * 2 + 3, 2, edge);
        }
        pauseFillRect(s_expandBuf, width, height, x + 26, y + 36, 12, 22, edge);

        pauseFillRect(s_expandBuf, width, height, x + 12, y + 76, 40, 24, edge);
        pauseFillRect(s_expandBuf, width, height, x + 16, y + 80, 32, 16, XPAUSE_BLACK);
        char layerNumber[8];
        sprintf(layerNumber, "%d", s_pdaOverlayLayer);
        wheelDrawTextCenterScaled(layerNumber, x + 32, y + 78, XPAUSE_WHITE, 7, 10);

        for (s32 r = 0; r < 14; r++)
        {
            pauseFillRect(s_expandBuf, width, height, x + 31 - r, y + 136 - r, r * 2 + 3, 2, edge);
        }
        pauseFillRect(s_expandBuf, width, height, x + 26, y + 106, 12, 22, edge);
        {
            const s32 iconH = 20;
            const s32 iconW = dukeIconWidthForHeight(XDB_BLACK, iconH);
            dukeDrawIconTo(s_expandBuf, width, height, XDB_BLACK, x + 32 - iconW / 2, y + 150, iconH, 0xFFFFFFFFu);
        }
    }

    static void pdaCompositeOverlay()
    {
        if (!s_pdaOverlayEnabled || !s_vdispWidth || !s_vdispHeight) return;

        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        const s32 frameX = 0;
        const s32 frameY = 0;
        const s32 frameW = width;
        const s32 frameH = height;
        pdaDrawFrameImage(frameX, frameY, frameW, frameH);

        const s32 tabY0 = pdaScaleFrameY(353);
        const s32 tabY1 = pdaScaleFrameY(379);
        const s32 tabH = tabY1 - tabY0;
        pdaDrawNativeTab("MAP", 153, pdaScaleFrameY(355), 48, pdaScaleFrameY(378) - pdaScaleFrameY(355), s_pdaOverlayMode == 0);
        pdaDrawNativeTab("WEAP", 233, tabY0, 58, tabH, s_pdaOverlayMode == 1);
        pdaDrawNativeTab("INV", 294, tabY0, 49, tabH, s_pdaOverlayMode == 2);
        pdaDrawNativeTab("OBJ", 347, tabY0, 56, tabH, s_pdaOverlayMode == 3);
        pdaDrawNativeTab("MIS", 437, tabY0, 48, tabH, s_pdaOverlayMode == 4);

        if (s_pdaOverlayMode == 0)
        {
            const s32 keyX = 448;
            const s32 textX = 480;
            const s32 y0 = 62;
            const s32 step = 24;
            dukeDrawIconTo(s_expandBuf, width, height, XDB_LSTICK_SMALL, keyX - 1, y0 - 4, 21, 0xFFFFFFFFu);
            wheelDrawTextScaled("PAN", textX, y0, 0xFFE0D8B8u, 13, 20);
            dukeDrawIconTo(s_expandBuf, width, height, XDB_A, keyX, y0 + step - 3, 18, 0xFFFFFFFFu);
            wheelDrawTextScaled("ZOOM IN", textX, y0 + step, 0xFF33D033u, 13, 20);
            dukeDrawIconTo(s_expandBuf, width, height, XDB_X, keyX, y0 + step * 2 - 3, 18, 0xFFFFFFFFu);
            wheelDrawTextScaled("ZOOM OUT", textX, y0 + step * 2, 0xFF64A8FFu, 13, 20);
            pdaDrawLayerStackKey(520, 150);
        }
    }

    static void cheatCompositeOverlay()
    {
        const s32 width = (s32)s_vdispWidth;
        const s32 height = (s32)s_vdispHeight;
        const s32 boxW = XPAUSE_PANEL_WIDTH;
        const s32 boxH = XPAUSE_PANEL_HEIGHT;
        const s32 originX = (width - XPAUSE_DESIGN_WIDTH) / 2;
        const s32 originY = (height - XPAUSE_DESIGN_HEIGHT) / 2;
        const s32 boxX = originX + (XPAUSE_DESIGN_WIDTH - boxW) / 2;
        const s32 boxY = originY + (XPAUSE_DESIGN_HEIGHT - boxH) / 2 - 34;
        pauseDrawFrame(s_expandBuf, width, height, boxX, boxY, boxW, boxH);

        const s32 visible = 7;
        const s32 rowX = boxX + 62;
        const s32 rowY = boxY + 24;
        const s32 rowH = 32;
        const s32 rowW = boxW - 124;
        for (s32 i = 0; i < visible; i++)
        {
            const s32 index = s_cheatScroll + i;
            if (index < 0 || index >= s_cheatItemCount) continue;
            const bool selected = index == s_cheatSelection;
            const bool enabled = s_cheatItems[index].enabled;
            const s32 y = rowY + i * rowH;
            if (selected)
            {
                pauseFillRect(s_expandBuf, width, height, rowX - 18, y + 2, rowW, 26, XPAUSE_GREEN_MID);
                pauseDrawText(s_expandBuf, width, height, XPT_ARROW, rowX - 4, y + 2, true);
            }
            wheelDrawTextRaw(s_cheatItems[index].label, rowX + 22, y, enabled ? 0xFFFF3030u : (selected ? XPAUSE_WHITE : XPAUSE_GREY), true);
            wheelDrawTextRaw(s_cheatItems[index].label, rowX + 22, y, enabled ? 0xFFFF3030u : (selected ? XPAUSE_WHITE : XPAUSE_GREY), false);
            wheelDrawTextRight(enabled ? "ON" : "OFF", boxX + boxW - 82, y, enabled ? 0xFFFF3030u : 0xFF8A8A8Au);
        }

        if (s_cheatScroll > 0)
        {
            const s32 ax = boxX + boxW - 34;
            const s32 ay = rowY - 8;
            for (s32 r = 0; r < 8; r++)
            {
                pauseFillRect(s_expandBuf, width, height, ax - r, ay + r, r * 2 + 1, 1, XPAUSE_GREY);
            }
        }
        if (s_cheatScroll + visible < s_cheatItemCount)
        {
            const s32 ax = boxX + boxW - 34;
            const s32 ay = rowY + visible * rowH + 4;
            for (s32 r = 0; r < 8; r++)
            {
                pauseFillRect(s_expandBuf, width, height, ax - r, ay - r, r * 2 + 1, 1, XPAUSE_GREY);
            }
        }
    }

    static void loadStrokeRect(u32* dst, s32 width, s32 height, s32 x, s32 y, s32 w, s32 h, u32 color)
    {
        pauseFillRect(dst, width, height, x, y, w, 1, color);
        pauseFillRect(dst, width, height, x, y + h - 1, w, 1, color);
        pauseFillRect(dst, width, height, x, y, 1, h, color);
        pauseFillRect(dst, width, height, x + w - 1, y, 1, h, color);
    }

    static void briefingDrawPromptTextRaw(u32* dst, s32 width, s32 height, XboxBriefingPromptTextId id, s32 x, s32 y, u32 primary, bool shadow)
    {
        const XboxBriefingPromptTextSprite* s = &c_xboxBriefingPromptText[id];
        const s32 ox = shadow ? 2 : 0;
        const s32 oy = shadow ? 2 : 0;
        for (s32 py = 0; py < s->height; py++)
        {
            const s32 dy = y + oy + py;
            if (dy < 0 || dy >= height) continue;
            for (s32 px = 0; px < s->width; px++)
            {
                const u8 cov = s->data[py * s->width + px];
                if (!cov) continue;
                const s32 dx = x + ox + px;
                if (dx < 0 || dx >= width) continue;
                const u32 src = shadow ? XPAUSE_BLACK : primary;
                const u32 a = shadow ? (u32)(cov * 44) : (u32)(cov * 64);
                u32* pixel = dst + dy * width + dx;
                *pixel = pauseBlend(*pixel | 0xFF000000u, src, (u32)pauseClamp((s32)a, 0, 255));
            }
        }
    }

    static void briefingDrawPromptText(XboxBriefingPromptTextId id, s32 x, s32 y, u32 color)
    {
        briefingDrawPromptTextRaw(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, id, x, y, color, true);
        briefingDrawPromptTextRaw(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, id, x, y, color, false);
    }

    static void briefingDrawButtonBox(const char* button, s32 x, s32 y, u32 buttonColor)
    {
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, x, y, 18, 18, 0xFF101010u);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, x, y, 18, 18, buttonColor);
        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, button, x + 9, y + 4, 1, buttonColor);
    }

    static void briefingCompositeFooter()
    {
        if (!s_briefingFooterEnabled) return;

        XboxFooterTextId diffText = XFT_X_DIFF_MEDIUM;
        if (s_briefingFooterDifficulty <= 0) diffText = XFT_X_DIFF_EASY;
        else if (s_briefingFooterDifficulty >= 2) diffText = XFT_X_DIFF_HARD;

        footerDrawBar(0xFF3C2E10u);
        footerDrawItem(XFT_A_START, 28, 0xFF33D033u);
        footerDrawItem(XFT_B_ABORT, 138, 0xFFFF3030u);
        footerDrawItem(diffText, 255, 0xFF64A8FFu);
    }

    static void loadDrawThumb(u32* dst, s32 width, s32 height, const u32* image, s32 x, s32 y, s32 w, s32 h)
    {
        if (!image)
        {
            pauseFillRect(dst, width, height, x, y, w, h, 0xFF16120Au);
            for (s32 yy = 0; yy < h; yy += 8)
            {
                for (s32 xx = ((yy / 8) & 1) ? 0 : 8; xx < w; xx += 16)
                    pauseFillRect(dst, width, height, x + xx, y + yy, 8, 8, 0xFF211A0Du);
            }
            loadDrawText(dst, width, height, "SAVE THUMBNAIL", x + 35, y + 39, 1, 0xFF8E8B72u);
            return;
        }
        for (s32 yy = 0; yy < h; yy++)
        {
            const s32 sy = (yy * TFE_SaveSystem::SAVE_IMAGE_HEIGHT) / h;
            for (s32 xx = 0; xx < w; xx++)
            {
                const s32 sx = (xx * TFE_SaveSystem::SAVE_IMAGE_WIDTH) / w;
                u32 src = image[sy * TFE_SaveSystem::SAVE_IMAGE_WIDTH + sx] | 0xFF000000u;
                dst[(y + yy) * width + x + xx] = src;
            }
        }
    }

    static void loadBuildFrame()
    {
        startDrawStarfield(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, s_loadFrame);
        startDrawTextSprite(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, XST_LOAD_GAME,
                            (XBOX_OUTPUT_WIDTH - c_xboxStartText[XST_LOAD_GAME].width) / 2, 38,
                            0xFFFF3030u, true);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "- SELECT A SAVE FILE -",
                     (XBOX_OUTPUT_WIDTH - loadTextWidth("- SELECT A SAVE FILE -", 1)) / 2, 75, 1, 0xFF8E8B72u);

        const s32 listX = 70;
        const s32 listY = 112;
        const s32 rowW = 350;
        const s32 rowH = 34;
        const s32 rowStep = 41;
        for (s32 i = 0; i < 6; i++)
        {
            const bool selected = (i == s_loadSelection);
            const XboxLoadSlotInfo* slot = (s_loadSlots && i < s_loadSlotCount) ? &s_loadSlots[i] : NULL;
            const bool valid = slot && slot->valid;
            const s32 y = listY + rowStep * i;
            const u32 edge = selected ? 0xFF4E4428u : 0xFF2F2817u;
            const u32 fill = selected ? 0xFF19150Au : 0xFF0C0A05u;
            pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, listX, y, rowW, rowH, fill);
            loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, listX, y, rowW, rowH, edge);
            char idx[8]; sprintf(idx, "%02d", i + 1);
            loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, idx, listX + 10, y + 10, 2, selected ? 0xFFFF3030u : 0xFF8E8B72u);
            if (valid)
            {
                loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, slot->saveName, listX + 56, y + 7, 1, selected ? 0xFFFF3030u : 0xFFE0D8B8u);
                loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, slot->levelName, listX + 206, y + 7, 1, 0xFF8E8B72u);
                loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, slot->dateTime, listX + 206, y + 20, 1, selected ? 0xFFFF3030u : 0xFFE0D8B8u);
                if (slot->autosave)
                {
                    loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, listX + 56, y + 20, 54, 12, 0xFF00A000u);
                    loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "AUTOSAVE", listX + 59, y + 22, 1, 0xFF33FF33u);
                }
            }
            else
            {
                loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "- EMPTY SLOT -", listX + 56, y + 11, 1, 0xFF4F4A34u);
            }
        }

        const XboxLoadSlotInfo* selectedSlot = (s_loadSlots && s_loadSelection < s_loadSlotCount) ? &s_loadSlots[s_loadSelection] : NULL;
        const bool selectedValid = selectedSlot && selectedSlot->valid;
        const s32 panelX = 435;
        const s32 panelY = 112;
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX, panelY, 170, 200, 0xFF0C0A05u);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX, panelY, 170, 200, 0xFF4E4428u);
        loadDrawThumb(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? selectedSlot->imageData : NULL, panelX + 8, panelY + 8, 154, 87);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX + 8, panelY + 8, 154, 87, 0xFF2F2817u);
        const s32 valueRightX = panelX + 162;
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "MISSION", panelX + 8, panelY + 112, 1, 0xFF8E8B72u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? selectedSlot->levelName : "[ EMPTY ]", valueRightX, panelY + 112, 1, 0xFFFF3030u);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "FILE", panelX + 8, panelY + 137, 1, 0xFF8E8B72u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? selectedSlot->fileName : "-", valueRightX, panelY + 137, 1, 0xFFFF3030u);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "SAVED", panelX + 8, panelY + 162, 1, 0xFF8E8B72u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? selectedSlot->dateTime : "-", valueRightX, panelY + 162, 1, 0xFFFF3030u);

        footerDrawBar(0xFF3C2E10u);
        footerDrawItem(XFT_A_LOAD, 28, selectedValid ? 0xFF33D033u : 0xFF4F4A34u);
        footerDrawItem(XFT_B_BACK, 138, 0xFFFF3030u);
    }

    static void modDrawWrappedText(const char* text, s32 x, s32 y, s32 maxChars, s32 maxLines, u32 color)
    {
        if (!text || !text[0]) return;
        char line[80];
        const char* p = text;
        for (s32 lineIndex = 0; lineIndex < maxLines && *p; lineIndex++)
        {
            while (*p == ' ') p++;
            s32 len = 0;
            s32 lastSpace = -1;
            while (p[len] && len < maxChars)
            {
                if (p[len] == ' ') lastSpace = len;
                len++;
            }
            if (p[len] && lastSpace > 0) len = lastSpace;
            if (len > 70) len = 70;
            memcpy(line, p, len);
            line[len] = 0;
            const char* next = p + len;
            while (*next == ' ') next++;
            if (lineIndex == maxLines - 1 && *next)
            {
                s32 end = len;
                while (end > 0 && line[end - 1] == ' ') end--;
                const s32 maxEnd = maxChars - 3;
                if (end > maxEnd)
                {
                    s32 wordEnd = maxEnd;
                    while (wordEnd > 0 && line[wordEnd] != ' ') wordEnd--;
                    end = wordEnd > 0 ? wordEnd : maxEnd;
                }
                if (end < 0) end = 0;
                line[end++] = '.';
                line[end++] = '.';
                line[end++] = '.';
                line[end] = 0;
            }
            loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, line, x, y + lineIndex * 15, 1, color);
            p += len;
        }
    }

    static void modBuildFrame()
    {
        startDrawStarfield(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, s_modFrame);

        startDrawTextSprite(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, XST_START_MOD,
                            (XBOX_OUTPUT_WIDTH - c_xboxStartText[XST_START_MOD].width) / 2, 38,
                            0xFFFF3030u, true);
        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "- SELECT AN INSTALLED MOD -", XBOX_OUTPUT_WIDTH / 2, 76, 1, 0xFF8E8B72u);

        const s32 listX = 42;
        const s32 listY = 112;
        const s32 listW = 385;
        const s32 rowH = 38;
        const s32 visibleRows = 6;
        s32 firstMod = s_modSelection - visibleRows + 1;
        if (firstMod < 0) firstMod = 0;
        if (firstMod > s_modCount - visibleRows) firstMod = s_modCount - visibleRows;
        if (firstMod < 0) firstMod = 0;
        for (s32 i = 0; i < visibleRows; i++)
        {
            const s32 modIndex = firstMod + i;
            const s32 y = listY + i * rowH;
            const bool valid = modIndex < s_modCount && s_mods[modIndex].valid;
            const bool selected = modIndex == s_modSelection;
            pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, listX, y, listW, 28, selected ? 0xFF24180Eu : 0xFF100D07u);
            loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, listX, y, listW, 28, selected ? 0xFFFF3030u : 0xFF4F4A34u);

            char idx[8];
            sprintf(idx, "%02d", modIndex + 1);
            loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, idx, listX + 12, y + 8, 1, selected ? 0xFFFF3030u : 0xFF8E8B72u);
            loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, valid ? s_mods[modIndex].title : "- EMPTY SLOT -",
                listX + 58, y + 8, 1, selected ? 0xFFFF3030u : (valid ? 0xFFE0D8B8u : 0xFF4F4A34u));
        }

        const bool selectedValid = s_modSelection >= 0 && s_modSelection < s_modCount && s_mods[s_modSelection].valid;
        const XboxModInfo* selectedMod = selectedValid ? &s_mods[s_modSelection] : NULL;
        const s32 panelX = 446;
        const s32 panelY = 112;
        const s32 panelW = 168;
        const s32 panelH = 292;
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX, panelY, panelW, panelH, 0xFF080604u);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX, panelY, panelW, panelH, 0xFF4F4A34u);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX + 10, panelY + 10, panelW - 20, 84, 0xFF160F08u);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, panelX + 10, panelY + 10, panelW - 20, 84, 0xFF3F3420u);
        loadDrawThumb(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
            selectedValid ? selectedMod->imageData : NULL, panelX + 10, panelY + 10, panelW - 20, 84);
        modDrawWrappedText(selectedValid ? selectedMod->description : "Drop ZIP mods into the Mods folder. Use a matching _metadata.txt file for title, author, version, missions, and description.", panelX + 10, panelY + 112, 25, 5, selectedValid ? 0xFFFF3030u : 0xFF8E8B72u);

        const s32 valueRightX = panelX + panelW - 12;
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "AUTHOR", panelX + 10, panelY + 208, 1, 0xFF8E8B72u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? selectedMod->author : "-", valueRightX, panelY + 208, 1, 0xFFFF3030u);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "VERSION", panelX + 10, panelY + 232, 1, 0xFF8E8B72u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? selectedMod->version : "-", valueRightX, panelY + 232, 1, 0xFFFF3030u);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "MISSIONS", panelX + 10, panelY + 256, 1, 0xFF8E8B72u);
        char missionText[16];
        sprintf(missionText, "%d", selectedValid ? selectedMod->missionCount : 0);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, selectedValid ? missionText : "-", valueRightX, panelY + 256, 1, 0xFFFF3030u);

        footerDrawBar(0xFF3C2E10u);
        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "Visit https://df-21.net/downloads/levels/ for more mods!", XBOX_OUTPUT_WIDTH / 2, 430, 1, 0xFF8E8B72u);
        footerDrawItem(XFT_A_START, 28, selectedValid ? 0xFF33D033u : 0xFF4F4A34u);
        footerDrawItem(XFT_B_BACK, 138, 0xFFFF3030u);
        if (selectedValid && selectedMod->hasQuickSave)
        {
            footerDrawItem(XFT_X_RESUME, 255, 0xFF64A8FFu);
        }
    }

    static void optionsDrawTriangle(s32 cx, s32 y, s32 halfW, s32 h, bool up, u32 color)
    {
        for (s32 row = 0; row < h; row++)
        {
            const s32 span = up ? row : (h - 1 - row);
            const s32 w = (span * halfW) / (h - 1);
            for (s32 x = cx - w; x <= cx + w; x++)
            {
                const s32 py = y + row;
                if (x < 0 || x >= XBOX_OUTPUT_WIDTH || py < 0 || py >= XBOX_OUTPUT_HEIGHT) continue;
                s_expandBuf[py * XBOX_OUTPUT_WIDTH + x] = color;
            }
        }
    }

    static void optionsDrawCornerMarker(s32 x, s32 y, bool right, bool bottom, u32 edge, u32 highlight)
    {
        const s32 len = 44;
        const s32 thick = 5;
        const s32 hX = right ? x - len + 1 : x;
        const s32 hY = bottom ? y - thick + 1 : y;
        const s32 vX = right ? x - thick + 1 : x;
        const s32 vY = bottom ? y - len + 1 : y;

        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, hX, hY, len, thick, edge);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, vX, vY, thick, len, edge);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
            hX + (right ? 3 : 0), hY + (bottom ? 0 : 3), len - 3, 2, highlight);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
            vX + (right ? 0 : 3), vY + (bottom ? 3 : 0), 2, len - 3, highlight);
    }

    static void optionsDrawSafeAreaMarkers(bool pauseStyle)
    {
        if (strcmp(s_optionsTitle, "VIDEO") != 0) return;

        const u32 edge = pauseStyle ? XPAUSE_GREEN_EDGE : 0xFFFF3030u;
        const u32 highlight = pauseStyle ? XPAUSE_WHITE : 0xFFFFFFFFu;
        optionsDrawCornerMarker(0, 0, false, false, edge, highlight);
        optionsDrawCornerMarker(XBOX_OUTPUT_WIDTH - 1, 0, true, false, edge, highlight);
        optionsDrawCornerMarker(0, XBOX_OUTPUT_HEIGHT - 1, false, true, edge, highlight);
        optionsDrawCornerMarker(XBOX_OUTPUT_WIDTH - 1, XBOX_OUTPUT_HEIGHT - 1, true, true, edge, highlight);
    }

    static void optionsDrawSlider(s32 x, s32 y, s32 w, const XboxOptionsItem* item, bool selected, bool pauseStyle)
    {
        const u32 dim = pauseStyle ? 0xFF5F775Fu : 0xFF4F4A34u;
        const u32 fill = pauseStyle ? XPAUSE_GREEN_EDGE : 0xFFFF3030u;
        const u32 knob = selected ? 0xFFE8E8E8u : 0xFF8E8B72u;
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, x, y + 7, w, 4, dim);

        s32 range = item->maxValue - item->minValue;
        if (range <= 0) range = 1;
        s32 pos = ((item->value - item->minValue) * w) / range;
        if (pos < 0) pos = 0;
        if (pos > w) pos = w;
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, x, y + 7, pos, 4, fill);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, x + pos - 3, y + 2, 6, 14, knob);
    }

    struct OptionsLayout
    {
        bool pauseStyle;
        s32 panelX;
        s32 panelY;
        s32 panelW;
        s32 panelH;
        s32 screenX;
        s32 screenY;
        s32 screenW;
        s32 screenH;
        s32 titleCenterX;
        s32 titleY;
        s32 rowsX;
        s32 rowsW;
        s32 firstRowCenterY;
        s32 rowH;
        s32 selectedH;
        s32 labelX;
        s32 sliderX;
        s32 sliderW;
        s32 valueRightX;
        s32 arrowX;
        s32 arrowUpY;
        s32 arrowDownY;
        s32 visibleRows;
    };

    static const s32 OPTIONS_SCROLL_ARROW_H = 10;

    static s32 optionsEven(s32 value)
    {
        return value & ~1;
    }

    static void optionsBuildLayout(bool pauseStyle, OptionsLayout* layout)
    {
        memset(layout, 0, sizeof(*layout));
        layout->pauseStyle = pauseStyle;
        layout->visibleRows = 7;

        if (pauseStyle)
        {
            layout->panelW = XPAUSE_PANEL_WIDTH;
            layout->panelH = XPAUSE_PANEL_HEIGHT;
            layout->panelX = (XBOX_OUTPUT_WIDTH - layout->panelW) / 2;
            layout->panelY = (XBOX_OUTPUT_HEIGHT - layout->panelH) / 2;

            layout->screenX = XPAUSE_SCREEN_SRC_X;
            layout->screenY = (XPAUSE_SCREEN_SRC_Y * XBOX_OUTPUT_HEIGHT) / XBOX_PDA_FRAME_HEIGHT;
            layout->screenW = XPAUSE_SCREEN_SRC_W;
            layout->screenH = ((XPAUSE_SCREEN_SRC_Y + XPAUSE_SCREEN_SRC_H) * XBOX_OUTPUT_HEIGHT) / XBOX_PDA_FRAME_HEIGHT - layout->screenY;

            const s32 sideInset = layout->screenW / 12;
            layout->rowH = pauseClamp(layout->screenH / 10, 28, XPAUSE_ROW_STEP);
            layout->selectedH = layout->rowH - pauseClamp(layout->rowH / 6, 4, 8);
            layout->rowsX = layout->screenX + sideInset;
            layout->rowsW = layout->screenW - sideInset * 2;
            layout->labelX = layout->rowsX + layout->rowH / 2;
            layout->valueRightX = layout->screenX + layout->screenW - sideInset;
            layout->sliderW = layout->screenW / 4;
            layout->sliderX = layout->valueRightX - layout->sliderW - layout->screenW / 6;
            layout->titleCenterX = layout->screenX + layout->screenW / 2;
            layout->titleY = wheelTextBaselineForCenter(s_optionsTitle, layout->screenY + layout->screenH / 5, 1, 1);
            layout->firstRowCenterY = layout->screenY + layout->screenH / 4 + layout->rowH / 2;
            layout->arrowX = layout->screenX + layout->screenW - sideInset / 2;
            layout->arrowUpY = layout->firstRowCenterY - layout->rowH;
            layout->arrowDownY = layout->firstRowCenterY + (layout->visibleRows - 1) * layout->rowH - OPTIONS_SCROLL_ARROW_H / 2;
        }
        else
        {
            layout->panelW = optionsEven((XBOX_OUTPUT_WIDTH * 73) / 100);
            layout->panelH = (XBOX_OUTPUT_HEIGHT * 27) / 40;
            layout->panelX = (XBOX_OUTPUT_WIDTH - layout->panelW) / 2;
            layout->panelY = XBOX_OUTPUT_HEIGHT / 6 - 4;

            layout->screenX = layout->panelX;
            layout->screenY = layout->panelY;
            layout->screenW = layout->panelW;
            layout->screenH = layout->panelH;

            const s32 bandInset = layout->panelW / 40;
            layout->rowH = (XBOX_OUTPUT_HEIGHT * 3) / 40;
            layout->selectedH = layout->rowH - 6;
            layout->rowsX = layout->panelX + bandInset;
            layout->rowsW = layout->panelW - bandInset * 2;
            layout->labelX = layout->rowsX + layout->rowH / 2;
            layout->valueRightX = layout->panelX + layout->panelW - layout->panelW / 20;
            layout->sliderW = (layout->panelW * 29) / 100;
            layout->sliderX = layout->valueRightX - layout->sliderW - layout->panelW / 8;
            layout->titleCenterX = XBOX_OUTPUT_WIDTH / 2;
            layout->titleY = layout->panelY / 2;
            layout->firstRowCenterY = layout->panelY + layout->panelH / 5 - 2;
            layout->arrowX = layout->panelX + layout->panelW - bandInset;
            layout->arrowUpY = layout->firstRowCenterY - layout->rowH / 2;
            layout->arrowDownY = layout->panelY + layout->panelH - layout->rowH / 2;
        }
    }

    static s32 optionsSliderYForCenter(s32 centerY)
    {
        return centerY - 9;
    }

    static void optionsDrawTextLabel(const char* text, s32 x, s32 centerY, u32 color, bool pauseStyle)
    {
        if (!text || !text[0]) return;
        if (pauseStyle)
        {
            const s32 baseline = wheelTextBaselineForCenter(text, centerY, 2, 3);
            wheelDrawTextScaledTo(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                text, x, baseline, color, 2, 3);
        }
        else
        {
            loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                text, x, loadTextYForCenter(centerY, 1), 1, color);
        }
    }

    static void optionsDrawTextRight(const char* text, s32 rightX, s32 centerY, u32 color, bool pauseStyle)
    {
        if (!text || !text[0]) return;
        if (pauseStyle)
        {
            const s32 baseline = wheelTextBaselineForCenter(text, centerY, 2, 3);
            wheelDrawTextRightScaledTo(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                text, rightX, baseline, color, 2, 3);
        }
        else
        {
            loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                text, rightX, loadTextYForCenter(centerY, 1), 1, color);
        }
    }

    static void optionsDrawRows(const OptionsLayout* layout)
    {
        const bool pauseStyle = layout->pauseStyle;
        const u32 normalText = pauseStyle ? 0xFFC8C8C8u : 0xFF8E8B72u;
        const u32 selectedText = pauseStyle ? XPAUSE_WHITE : 0xFFFF3030u;

        for (s32 row = 0; row < layout->visibleRows; row++)
        {
            const s32 index = s_optionsScroll + row;
            if (index < 0 || index >= s_optionsItemCount) continue;

            const bool selected = index == s_optionsSelection;
            const s32 rowCenterY = layout->firstRowCenterY + row * layout->rowH;
            if (selected)
            {
                const u32 bar = pauseStyle ? XPAUSE_GREEN_MID : 0xFF24180Eu;
                pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                    layout->rowsX, rowCenterY - layout->selectedH / 2,
                    layout->rowsW, layout->selectedH, bar);
            }

            optionsDrawTextLabel(s_optionsItems[index].label, layout->labelX, rowCenterY,
                selected ? selectedText : normalText, pauseStyle);
            if (s_optionsItems[index].valueText)
            {
                const u32 valueColor = s_optionsItems[index].capture ? 0xFF33D033u : (selected ? selectedText : normalText);
                optionsDrawTextRight(s_optionsItems[index].valueText, layout->valueRightX, rowCenterY, valueColor, pauseStyle);
            }
            else if (s_optionsItems[index].hasIcon &&
                     s_optionsItems[index].valueIcon >= 0 &&
                     s_optionsItems[index].valueIcon < XDB_COUNT)
            {
                const XboxDukeButtonIconId icon = (XboxDukeButtonIconId)s_optionsItems[index].valueIcon;
                const s32 iconH = pauseStyle ? 20 : 24;
                const s32 iconW = dukeIconWidthForHeight(icon, iconH);
                dukeDrawIconTo(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, icon,
                    layout->valueRightX - iconW, rowCenterY - iconH / 2, iconH, 0xFFFFFFFFu);
            }
            else
            {
                optionsDrawSlider(layout->sliderX, optionsSliderYForCenter(rowCenterY),
                    layout->sliderW, &s_optionsItems[index], selected, pauseStyle);

                char valueText[16];
                sprintf(valueText, "%d", s_optionsItems[index].value);
                optionsDrawTextRight(valueText, layout->valueRightX, rowCenterY, selected ? selectedText : normalText, pauseStyle);
            }
        }
    }

    static void optionsBuildFrame()
    {
        if (s_optionsPauseStyle)
        {
            memset(s_expandBuf, 0, XBOX_OUTPUT_WIDTH * XBOX_OUTPUT_HEIGHT * sizeof(u32));
        }
        else
        {
            startDrawStarfield(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, s_optionsFrame);
        }

        OptionsLayout layout;
        optionsBuildLayout(s_optionsPauseStyle, &layout);
        const bool pauseStyle = layout.pauseStyle;

        if (pauseStyle)
        {
            pauseDrawFrame(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                layout.panelX, layout.panelY, layout.panelW, layout.panelH);
        }
        else
        {
            pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                layout.panelX, layout.panelY, layout.panelW, layout.panelH, 0xCC080604u);
            loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                layout.panelX, layout.panelY, layout.panelW, layout.panelH, 0xFF4F4A34u);
        }

        if (pauseStyle)
        {
            wheelDrawTextCenterScaledTo(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                s_optionsTitle, layout.titleCenterX, layout.titleY, XPAUSE_WHITE, 1, 1);
        }
        else
        {
            loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT,
                s_optionsTitle, layout.titleCenterX, layout.titleY, 3, 0xFFFF3030u);
        }

        optionsDrawRows(&layout);

        const u32 arrowColor = pauseStyle ? XPAUSE_GREEN_EDGE : 0xFFFF3030u;
        if (s_optionsScroll > 0)
        {
            optionsDrawTriangle(layout.arrowX, layout.arrowUpY, 7, OPTIONS_SCROLL_ARROW_H, true, arrowColor);
        }
        if (s_optionsScroll + 7 < s_optionsItemCount)
        {
            optionsDrawTriangle(layout.arrowX, layout.arrowDownY, 7, OPTIONS_SCROLL_ARROW_H, false, arrowColor);
        }

        if (!pauseStyle)
        {
            footerDrawBar(0xFF3C2E10u);
            footerDrawItem(XFT_A_APPLY, 28, 0xFF33D033u);
            footerDrawItem(XFT_B_BACK, 138, 0xFFFF3030u);
            footerDrawItem(XFT_DPAD_ADJUST, 255, 0xFF8E8B72u);
        }

        optionsDrawSafeAreaMarkers(pauseStyle);
    }

    static void missionCompleteBuildFrame()
    {
        startDrawStarfield(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, s_missionCompleteFrame);

        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "MISSION ACCOMPLISHED", XBOX_OUTPUT_WIDTH / 2, 120, 4, 0xFFFF3030u);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 94, 156, 452, 2, 0xFFFF3030u);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 94, 160, 452, 1, 0xFF661010u);

        char timeText[16];
        char secretText[16];
        const u32 seconds = s_missionCompleteInfo.seconds;
        const u32 minutes = seconds / 60;
        sprintf(timeText, "%02u:%02u", (unsigned)minutes, (unsigned)(seconds % 60));
        sprintf(secretText, "%d/%d", s_missionCompleteInfo.secretsFound, s_missionCompleteInfo.secretsTotal);

        const char* diffText = "MEDIUM";
        if (s_missionCompleteInfo.difficulty <= 0) diffText = "EASY";
        else if (s_missionCompleteInfo.difficulty >= 2) diffText = "HARD";

        const s32 statsY = 205;
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "TIME", 150, statsY, 1, 0xFF33D033u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, timeText, 250, statsY, 1, 0xFF33FF33u);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "SECRETS", 284, statsY, 1, 0xFF33D033u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, secretText, 390, statsY, 1, 0xFF33FF33u);
        loadDrawText(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "DIFFICULTY", 424, statsY, 1, 0xFF33D033u);
        loadDrawTextRight(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, diffText, 526, statsY, 1, 0xFF33FF33u);

        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 150, statsY + 18, 100, 1, 0xFF143814u);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 284, statsY + 18, 106, 1, 0xFF143814u);
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 424, statsY + 18, 102, 1, 0xFF143814u);

        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "SAVE GAME?", XBOX_OUTPUT_WIDTH / 2, 280, 3, 0xFFFF3030u);

        const s32 yesX = 244;
        const s32 noX = 360;
        const s32 buttonY = 340;
        const bool yesSelected = s_missionCompleteSelection == 0;
        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, yesX, buttonY, 84, 32, yesSelected ? 0xFF381010u : 0xFF201C12u);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, yesX, buttonY, 84, 32, yesSelected ? 0xFFFF3030u : 0xFF8E8B72u);
        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "YES", yesX + 42, buttonY + 10, 2, yesSelected ? 0xFFFF3030u : 0xFFE0D8B8u);

        pauseFillRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, noX, buttonY, 72, 32, !yesSelected ? 0xFF381010u : 0xFF201C12u);
        loadStrokeRect(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, noX, buttonY, 72, 32, !yesSelected ? 0xFFFF3030u : 0xFF8E8B72u);
        loadDrawTextCenter(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, "NO", noX + 36, buttonY + 10, 2, !yesSelected ? 0xFFFF3030u : 0xFFE0D8B8u);

        footerDrawBar(0xFF3C2E10u);
        footerDrawItem(XFT_A_CONFIRM, 28, 0xFF33D033u);
    }

    static bool startUploadTexture()
    {
        if (!s_deviceReady) return false;
        if (!s_startTex)
        {
            HRESULT hr = s_device->CreateTexture(
                XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, 1, 0,
                D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED, &s_startTex);
            if (FAILED(hr))
            {
                RB_LOG_ERROR("Create start texture failed hr=0x%08x", hr);
                return false;
            }
        }

        D3DLOCKED_RECT lr;
        HRESULT hr = s_startTex->LockRect(0, &lr, NULL, 0);
        if (FAILED(hr))
        {
            RB_LOG_ERROR("Lock start texture failed hr=0x%08x", hr);
            return false;
        }
        const u8* srcRow = (const u8*)s_expandBuf;
        u8* dstRow = (u8*)lr.pBits;
        const u32 pitch = XBOX_OUTPUT_WIDTH * 4;
        for (u32 y = 0; y < XBOX_OUTPUT_HEIGHT; y++)
        {
            memcpy(dstRow, srcRow, pitch);
            srcRow += pitch;
            dstRow += lr.Pitch;
        }
        s_startTex->UnlockRect(0);
        return true;
    }

    static void startBuildFrame()
    {
        startDrawStarfield(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, s_startFrame);
        startDrawLogo(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT);

        static const XboxStartTextId labels[4] = { XST_START_GAME, XST_LOAD_GAME, XST_START_MOD, XST_OPTIONS };
        const s32 menuY = 274;
        const s32 rowStep = 34;
        for (s32 i = 0; i < 4; i++)
        {
            const bool selected = (i == s_startSelection);
            const u32 color = selected ? 0xFFFF3030u : 0xFF8E8B72u;
            const s32 textW = c_xboxStartText[labels[i]].width;
            const s32 x = (XBOX_OUTPUT_WIDTH - textW) / 2;
            const s32 y = menuY + i * rowStep;
            startDrawTextSprite(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, labels[i], x, y, color, selected);
        }

        footerDrawItem(XFT_A_SELECT, 28, 0xFF33D033u);
        startDrawTextSprite(s_expandBuf, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, XST_VERSION,
                            XBOX_OUTPUT_WIDTH - c_xboxStartText[XST_VERSION].width - 14, 450, 0xFF8E8B72u, false);
    }

    // -----------------------------------------------------------------------
    // Compute destination rect.
    // At 640x480 the back buffer is already 4:3 - no letterbox needed.
    // Dark Forces native is 320x200 with 4:3 display aspect, so it stretches
    // edge-to-edge onto the back buffer.
    // -----------------------------------------------------------------------
    static void computeDestRect()
    {
        if (s_safeZoneWidthPercent < 80) s_safeZoneWidthPercent = 80;
        if (s_safeZoneWidthPercent > 100) s_safeZoneWidthPercent = 100;
        if (s_safeZoneHeightPercent < 80) s_safeZoneHeightPercent = 80;
        if (s_safeZoneHeightPercent > 100) s_safeZoneHeightPercent = 100;

        s32 w = (XBOX_OUTPUT_WIDTH * s_safeZoneWidthPercent) / 100;
        s32 h = (XBOX_OUTPUT_HEIGHT * s_safeZoneHeightPercent) / 100;
        if (w < 1) w = 1;
        if (h < 1) h = 1;

        const s32 marginX = XBOX_OUTPUT_WIDTH - w;
        const s32 marginY = XBOX_OUTPUT_HEIGHT - h;
        s32 x = (marginX / 2) + s_safeZoneOffsetX;
        s32 y = (marginY / 2) + s_safeZoneOffsetY;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > marginX) x = marginX;
        if (y > marginY) y = marginY;

        s_destRect.left   = x;
        s_destRect.top    = y;
        s_destRect.right  = x + w;
        s_destRect.bottom = y + h;
    }

    static void setViewportRect(const RECT& rect)
    {
        if (!s_deviceReady || !s_device) return;
        D3DVIEWPORT8 vp;
        vp.X = rect.left;
        vp.Y = rect.top;
        vp.Width = rect.right - rect.left;
        vp.Height = rect.bottom - rect.top;
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        s_device->SetViewport(&vp);
    }

    static void setFullViewport()
    {
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = XBOX_OUTPUT_WIDTH;
        rect.bottom = XBOX_OUTPUT_HEIGHT;
        setViewportRect(rect);
    }

    static void setSafeViewport()
    {
        setViewportRect(s_destRect);
    }

    // -----------------------------------------------------------------------
    // Create / recreate the virtual display texture.
    // -----------------------------------------------------------------------
    static bool createVdispTexture(u32 width, u32 height)
    {
        const bool needsAlpha = s_vdispGpuMode;
        const u32 texWidth = XBOX_OUTPUT_WIDTH;
        const u32 texHeight = XBOX_OUTPUT_HEIGHT;

        if (width > texWidth || height > texHeight)
        {
            RB_LOG_ERROR("Virtual display request too large: logical=%ux%u backing=%ux%u",
                width, height, texWidth, texHeight);
            return false;
        }

        if (s_vdispTex && s_vdispSurf &&
            s_vdispTexWidth >= width && s_vdispTexHeight >= height &&
            (!needsAlpha || s_vdispHasAlpha))
        {
            RB_LOG_MSG("Virtual display texture reused logical=%ux%u backing=%ux%u alpha=%d",
                width, height, s_vdispTexWidth, s_vdispTexHeight, s_vdispHasAlpha ? 1 : 0);
            return true;
        }

        if (s_vdispSurf)  { s_vdispSurf->Release();  s_vdispSurf  = NULL; }
        if (s_vdispTex)   { s_vdispTex->Release();   s_vdispTex   = NULL; }
        s_vdispTexWidth = 0;
        s_vdispTexHeight = 0;

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
        // Allocate a full 640x480 backing texture once and reuse top-left
        // logical subrects for 320x200 Landru and 640x480 menus/gameplay.
        // This avoids late texture allocation after Landru/game arenas have
        // fragmented memory. Prefer alpha so the later GPU HUD overlay path
        // can also reuse the same surface.
        s_vdispHasAlpha = true;
        D3DFORMAT fmt = s_vdispHasAlpha ? D3DFMT_LIN_A8R8G8B8 : D3DFMT_LIN_X8R8G8B8;
        HRESULT hr = s_device->CreateTexture(
            texWidth, texHeight, 1,
            0,                      // no render target
            fmt,
            D3DPOOL_DEFAULT,
            &s_vdispTex);

        if (FAILED(hr))
        {
            RB_LOG_ERROR("CreateTexture backing=%ux%u logical=%ux%u fmt=0x%08x pool=DEFAULT failed hr=0x%08x",
                texWidth, texHeight, width, height, fmt, hr);
            if (s_vdispHasAlpha)
            {
                s_vdispHasAlpha = false;
                fmt = D3DFMT_LIN_X8R8G8B8;
                hr = s_device->CreateTexture(
                    texWidth, texHeight, 1,
                    0,
                    fmt,
                    D3DPOOL_DEFAULT,
                    &s_vdispTex);
                if (FAILED(hr))
                {
                    RB_LOG_ERROR("CreateTexture fallback backing=%ux%u logical=%ux%u fmt=0x%08x pool=DEFAULT failed hr=0x%08x",
                        texWidth, texHeight, width, height, fmt, hr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        hr = s_vdispTex->GetSurfaceLevel(0, &s_vdispSurf);
        if (FAILED(hr))
        {
            RB_LOG_ERROR("GetSurfaceLevel failed hr=0x%08x", hr);
            s_vdispTex->Release();
            s_vdispTex = NULL;
            s_vdispTexWidth = 0;
            s_vdispTexHeight = 0;
            return false;
        }

        s_vdispTexWidth = texWidth;
        s_vdispTexHeight = texHeight;
        RB_LOG_MSG("Virtual display texture created logical=%ux%u backing=%ux%u fmt=0x%08x alpha=%d",
            width, height, texWidth, texHeight, fmt, s_vdispHasAlpha ? 1 : 0);
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
        // D3DSWAPEFFECT_COPY (not DISCARD) so the back buffer retains
        // the just-presented frame after Present(). Upstream's escape-
        // menu capture path is:
        //   TFE_RenderBackend::swap(true);   // present current world
        //   TFE_RenderBackend::copyBackbufferToRenderTarget(rt);
        // The copy-after-swap only works if Present preserves back
        // buffer contents. With DISCARD the back buffer is undefined
        // after Present and the captured RT comes out black.
        // COPY costs an extra back-to-front blit per frame; on NV2A
        // at 640x480 that's ~1.2MB of fast video memory bandwidth -
        // negligible compared to the world render cost.
        pp.SwapEffect                   = D3DSWAPEFFECT_COPY;
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
        if (!createVdispTexture(XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT))
        {
            RB_LOG_ERROR("Virtual display prewarm failed");
        }
        RB_LOG_MSG("D3D8 device created. Output: %dx%d", XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT);
        TFE_XboxLogf("RenderBackend", "device ready dest=%ld,%ld,%ld,%ld",
            s_destRect.left, s_destRect.top, s_destRect.right, s_destRect.bottom);
        return true;
    }

    void destroy()
    {
        TFE_XboxLogf("RenderBackend", "destroy begin ready=%d", s_deviceReady ? 1 : 0);
        s_deviceReady = false;
        gpuInvalidateTextureCache();
        if (s_p8Palette) { s_p8Palette->Release(); s_p8Palette = NULL; }
        s_p8PaletteDirty = true;
        if (s_startTex)   { s_startTex->Release();   s_startTex   = NULL; }
        if (s_vdispSurf)  { s_vdispSurf->Release();  s_vdispSurf  = NULL; }
        if (s_vdispTex)   { s_vdispTex->Release();   s_vdispTex   = NULL; }
        s_vdispTexWidth = 0;
        s_vdispTexHeight = 0;
        s_vdispHasAlpha = false;
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

        memcpy(s_captureBuf, s_expandBuf, pixels * sizeof(u32));
        s_captureBufValid = true;

        pauseCompositeOverlay();
        briefingCompositeFooter();
        pdaCompositeOverlay();
        weaponWheelComposite();

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

    void xboxSetPauseOverlay(bool enabled, s32 selection, s32 confirmSelection, bool confirmOpen, s32 notice)
    {
        s_pauseOverlayEnabled = enabled;
        s_pauseSelection = pauseClamp(selection, 0, 5);
        s_pauseConfirmSelection = pauseClamp(confirmSelection, 0, 1);
        s_pauseConfirmOpen = confirmOpen;
        s_pauseNotice = notice;
    }

    void xboxSetBriefingFooter(bool enabled, bool objectivesPrompt, s32 difficulty)
    {
        s_briefingFooterEnabled = enabled;
        s_briefingFooterObjectivesPrompt = objectivesPrompt;
        s_briefingFooterDifficulty = pauseClamp(difficulty, 0, 2);
    }

    void xboxSetStartScreen(bool enabled, s32 selection, u32 frame)
    {
        s_startScreenEnabled = enabled;
        s_startSelection = pauseClamp(selection, 0, 3);
        s_startFrame = frame;
    }

    void xboxSetLoadScreen(bool enabled, s32 selection, u32 frame, const XboxLoadSlotInfo* slots, s32 slotCount)
    {
        s_loadScreenEnabled = enabled;
        s_loadSelection = pauseClamp(selection, 0, 5);
        s_loadFrame = frame;
        s_loadSlots = slots;
        s_loadSlotCount = pauseClamp(slotCount, 0, 6);
    }

    void xboxSetModScreen(bool enabled, s32 selection, u32 frame, const XboxModInfo* mods, s32 modCount)
    {
        s_modScreenEnabled = enabled;
        s_modCount = pauseClamp(modCount, 0, 12);
        s_modSelection = pauseClamp(selection, 0, s_modCount > 0 ? s_modCount - 1 : 0);
        s_modFrame = frame;
        for (s32 i = 0; i < s_modCount; i++)
        {
            s_mods[i] = mods[i];
        }
    }

    void xboxSetOptionsScreen(bool enabled, bool pauseStyle, const char* title, s32 selection, s32 scroll, u32 frame, const XboxOptionsItem* items, s32 itemCount)
    {
        s_optionsScreenEnabled = enabled;
        s_optionsPauseStyle = pauseStyle;
        if (title && title[0])
        {
            strncpy(s_optionsTitle, title, sizeof(s_optionsTitle) - 1);
            s_optionsTitle[sizeof(s_optionsTitle) - 1] = 0;
        }
        else
        {
            strcpy(s_optionsTitle, "OPTIONS");
        }
        s_optionsItemCount = pauseClamp(itemCount, 0, 32);
        s_optionsSelection = pauseClamp(selection, 0, s_optionsItemCount > 0 ? s_optionsItemCount - 1 : 0);
        s_optionsScroll = pauseClamp(scroll, 0, s_optionsItemCount > 7 ? s_optionsItemCount - 7 : 0);
        s_optionsFrame = frame;
        for (s32 i = 0; i < s_optionsItemCount; i++)
        {
            s_optionsItems[i] = items[i];
        }
    }

    void xboxSetSafeZone(s32 widthPercent, s32 heightPercent, s32 offsetX, s32 offsetY)
    {
        if (widthPercent < 80) widthPercent = 80;
        if (widthPercent > 100) widthPercent = 100;
        if (heightPercent < 80) heightPercent = 80;
        if (heightPercent > 100) heightPercent = 100;
        if (offsetX < -40) offsetX = -40;
        if (offsetX > 40) offsetX = 40;
        if (offsetY < -30) offsetY = -30;
        if (offsetY > 30) offsetY = 30;

        s_safeZoneWidthPercent = widthPercent;
        s_safeZoneHeightPercent = heightPercent;
        s_safeZoneOffsetX = offsetX;
        s_safeZoneOffsetY = offsetY;
        computeDestRect();
        if (s_deviceReady)
        {
            setFullViewport();
        }
        TFE_XboxLogf("RenderBackend", "safe zone width=%d height=%d offset=%d,%d dest=%ld,%ld,%ld,%ld",
            s_safeZoneWidthPercent, s_safeZoneHeightPercent,
            s_safeZoneOffsetX, s_safeZoneOffsetY,
            s_destRect.left, s_destRect.top, s_destRect.right, s_destRect.bottom);
    }

    void xboxSetCheatScreen(bool enabled, s32 selection, s32 scroll, const XboxCheatItem* items, s32 itemCount)
    {
        s_cheatScreenEnabled = enabled;
        s_cheatItemCount = pauseClamp(itemCount, 0, 12);
        s_cheatSelection = pauseClamp(selection, 0, s_cheatItemCount > 0 ? s_cheatItemCount - 1 : 0);
        s_cheatScroll = pauseClamp(scroll, 0, s_cheatItemCount > 7 ? s_cheatItemCount - 7 : 0);
        for (s32 i = 0; i < s_cheatItemCount; i++)
        {
            s_cheatItems[i] = items[i];
        }
    }

    void xboxSetPdaOverlay(bool enabled, s32 mode, s32 layer)
    {
        s_pdaOverlayEnabled = enabled;
        s_pdaOverlayMode = pauseClamp(mode, 0, 4);
        s_pdaOverlayLayer = layer;
    }

    void xboxSetMissionCompleteScreen(bool enabled, s32 selection, u32 frame, const XboxMissionCompleteInfo* info)
    {
        s_missionCompleteScreenEnabled = enabled;
        s_missionCompleteSelection = pauseClamp(selection, 0, 1);
        s_missionCompleteFrame = frame;
        if (info) s_missionCompleteInfo = *info;
    }

    void xboxSetWeaponWheel(bool enabled, const XboxWeaponWheelInfo* info)
    {
        s_weaponWheelEnabled = enabled;
        if (info)
        {
            s_weaponWheelInfo = *info;
        }
        else
        {
            memset(&s_weaponWheelInfo, 0, sizeof(s_weaponWheelInfo));
            s_weaponWheelInfo.selected = -1;
            s_weaponWheelInfo.current = -1;
        }
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

        static const bool s_verbosePaletteLog = false;
        if (s_verbosePaletteLog && (s_paletteCpu[0] != s_lastFirst || s_paletteCpu[1] != s_lastSecond))
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
    const TextureGpu* getPaletteTexture(){ return NULL; } // Desktop GPU API is unused by the Xbox D3D8 bridge.

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
    static void blitTextureQuad(IDirect3DTexture8* tex, u32 texW, u32 texH, bool alphaTest)
    {
        if (!tex) return;
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

        s_device->SetTexture(0, tex);
        s_device->SetVertexShader(PRESENT_QUAD_FVF);

        const f32 l = (f32)s_destRect.left   - 0.5f;
        const f32 t = (f32)s_destRect.top    - 0.5f;
        const f32 r = (f32)s_destRect.right  - 0.5f;
        const f32 b = (f32)s_destRect.bottom - 0.5f;
        const f32 uMax = (f32)texW;
        const f32 vMax = (f32)texH;

        PresentQuadVert q[4];
        q[0].x = l; q[0].y = t; q[0].z = 0.0f; q[0].rhw = 1.0f; q[0].u = 0.0f; q[0].v = 0.0f;
        q[1].x = r; q[1].y = t; q[1].z = 0.0f; q[1].rhw = 1.0f; q[1].u = uMax; q[1].v = 0.0f;
        q[2].x = l; q[2].y = b; q[2].z = 0.0f; q[2].rhw = 1.0f; q[2].u = 0.0f; q[2].v = vMax;
        q[3].x = r; q[3].y = b; q[3].z = 0.0f; q[3].rhw = 1.0f; q[3].u = uMax; q[3].v = vMax;

        s_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(PresentQuadVert));
        s_device->SetTexture(0, NULL);

        if (alphaTest) s_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    }

    static void blitVdispQuad(bool alphaTest)
    {
        blitTextureQuad(s_vdispTex, s_vdispWidth, s_vdispHeight, alphaTest && s_vdispHasAlpha);
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
        setFullViewport();

        // Always clear TARGET|ZBUFFER|STENCIL together (NV20 quirk).
        // OpenJKDF2 fakeglx.cpp:1829-1844. xquake same. Mercs same.
        // We allocated D3DFMT_D24S8 in the present params; partial-clear
        // leaves it undefined which the HLE Present path crashes on.
        s_device->Clear(0, NULL,
                        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        0, 1.0f, 0);

        if (s_optionsScreenEnabled)
        {
            if (s_optionsPauseStyle && blitVirtualDisplay && s_vdispTex)
            {
                blitVdispQuad(/*alphaTest*/false);
            }
            optionsBuildFrame();
            if (startUploadTexture())
            {
                blitTextureQuad(s_startTex, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, s_optionsPauseStyle);
            }
        }
        else if (s_missionCompleteScreenEnabled)
        {
            missionCompleteBuildFrame();
            if (startUploadTexture())
            {
                blitTextureQuad(s_startTex, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, /*alphaTest*/false);
            }
        }
        else if (s_loadScreenEnabled)
        {
            loadBuildFrame();
            if (startUploadTexture())
            {
                blitTextureQuad(s_startTex, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, /*alphaTest*/false);
            }
        }
        else if (s_modScreenEnabled)
        {
            modBuildFrame();
            if (startUploadTexture())
            {
                blitTextureQuad(s_startTex, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, /*alphaTest*/false);
            }
        }
        else if (s_startScreenEnabled)
        {
            startBuildFrame();
            if (startUploadTexture())
            {
                blitTextureQuad(s_startTex, XBOX_OUTPUT_WIDTH, XBOX_OUTPUT_HEIGHT, /*alphaTest*/false);
            }
        }
        else if (blitVirtualDisplay && s_vdispTex)
        {
            blitVdispQuad(/*alphaTest*/false);
        }

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
    // Hardware world path. bindVirtualDisplay opens a D3D8 scene for
    // RClassic_GPU's Xbox fixed-function bridge; swap() closes and presents it.
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
        setFullViewport();
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
        setSafeViewport();
    }
    void copyToVirtualDisplay(RenderTargetHandle /*src*/)  {}

    // ---- Render target (real impl) -------------------------------------
    //
    // Upstream uses createRenderTarget + copyBackbufferToRenderTarget to
    // snapshot the world frame when the escape menu opens, then the menu's
    // screenGPU_addImageQuad blits that captured texture as the backdrop
    // (with darken + greyscale post applied to the original world frame).
    //
    // D3D8 implementation: each handle is an XboxRenderTarget* (typedef
    // RenderTargetHandle = void*). It owns a D3DUSAGE_RENDERTARGET texture
    // + its level-0 surface. copyBackbufferToRenderTarget pulls the front-
    // most back buffer via GetBackBuffer and CopyRects into the RT surface.
    //
    // getRenderTargetTexture returns the same handle reinterpreted as
    // TextureGpu* so screenGPU_addImageQuad can hand it back to us; we
    // recognise it by a membership check against s_xboxRTRegistry rather
    // than a struct-internal sentinel (which would race with a stray P8
    // texture pointer that happens to alias the sentinel field).
    struct XboxRenderTarget
    {
        IDirect3DTexture8* tex;
        IDirect3DSurface8* surf;
        u32 width, height;
    };

    // Fixed-size registry of live render targets. We only ever allocate
    // one (the escape menu's), but reserve a small array so PDA / agent
    // menu can co-exist if they ever capture too.
    enum { XBOX_RT_REGISTRY_MAX = 8 };
    static XboxRenderTarget* s_xboxRTRegistry[XBOX_RT_REGISTRY_MAX] = { 0 };

    static void xboxRT_registryAdd(XboxRenderTarget* rt)
    {
        for (u32 i = 0; i < XBOX_RT_REGISTRY_MAX; i++)
        {
            if (!s_xboxRTRegistry[i]) { s_xboxRTRegistry[i] = rt; return; }
        }
    }
    static void xboxRT_registryRemove(XboxRenderTarget* rt)
    {
        for (u32 i = 0; i < XBOX_RT_REGISTRY_MAX; i++)
        {
            if (s_xboxRTRegistry[i] == rt) { s_xboxRTRegistry[i] = NULL; return; }
        }
    }
    static bool xboxRT_registryContains(const void* p)
    {
        for (u32 i = 0; i < XBOX_RT_REGISTRY_MAX; i++)
        {
            if ((const void*)s_xboxRTRegistry[i] == p) return true;
        }
        return false;
    }

    void copyBackbufferToRenderTarget(RenderTargetHandle dst)
    {
        if (!s_deviceReady || !dst) return;
        if (!xboxRT_registryContains(dst)) return;
        XboxRenderTarget* rt = (XboxRenderTarget*)dst;
        if (!rt->surf) return;

        if (s_gpuSceneOpen)
        {
            if (s_vdispTex) blitVdispQuad(/*alphaTest*/true);
            s_device->EndScene();
            s_gpuSceneOpen = false;
        }

        IDirect3DSurface8* back = NULL;
        HRESULT hr = s_device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back);
        if (FAILED(hr) || !back) return;

        // CopyRects requires identical surface formats on Xbox D3D8.
        // The RT below is created with D3DFMT_LIN_X8R8G8B8 to match the
        // back buffer's pp.BackBufferFormat (D3DFMT_X8R8G8B8). On Xbox
        // back buffers are mandated to be linear (render targets cannot
        // be swizzled), so the driver treats D3DFMT_X8R8G8B8 as the
        // linear variant internally - meaning LIN_X8R8G8B8 on the RT is
        // the correct match. Previous A8R8G8B8 (with alpha channel)
        // failed silently and left the captured surface black.
        HRESULT cr = s_device->CopyRects(back, NULL, 0, rt->surf, NULL);
        RB_LOG_MSG("copyBackbufferToRenderTarget hr=0x%08x rt=%p back=%p", cr, rt, back);
        back->Release();
    }
    void captureScreenToMemory(u32* mem)
    {
        if (!mem) return;
        const u32 outW = XBOX_OUTPUT_WIDTH;
        const u32 outH = XBOX_OUTPUT_HEIGHT;
        const u32* srcBuf = s_captureBufValid ? s_captureBuf : s_expandBuf;
        if (s_vdispWidth == outW && s_vdispHeight == outH)
        {
            memcpy(mem, srcBuf, outW * outH * sizeof(u32));
            return;
        }
        for (u32 y = 0; y < outH; y++)
        {
            const u32 sy = s_vdispHeight ? (y * s_vdispHeight) / outH : 0;
            for (u32 x = 0; x < outW; x++)
            {
                const u32 sx = s_vdispWidth ? (x * s_vdispWidth) / outW : 0;
                mem[y * outW + x] = srcBuf[sy * s_vdispWidth + sx] | 0xFF000000u;
            }
        }
    }
    void queueScreenshot(const char* /*path*/)             {}
    void startGifRecording(const char* /*path*/, bool)     {}
    void stopGifRecording()                                {}
    void bindGlobalVAO()                                   {}
    void setViewport(s32, s32, s32, s32)                   {}
    void setScissorRect(bool, s32, s32, s32, s32)          {}

    // -----------------------------------------------------------------------
    // Render target
    // -----------------------------------------------------------------------
    // See note next to copyBackbufferToRenderTarget above.
    RenderTargetHandle createRenderTarget(u32 w, u32 h, bool /*depth*/)
    {
        if (!s_deviceReady || w == 0 || h == 0) return NULL;
        XboxRenderTarget* rt = (XboxRenderTarget*)malloc(sizeof(XboxRenderTarget));
        if (!rt) return NULL;
        memset(rt, 0, sizeof(*rt));
        rt->width  = w;
        rt->height = h;

        // D3DUSAGE_RENDERTARGET + LIN_X8R8G8B8 to match the back buffer's
        // effective format. CopyRects requires identical formats; the back
        // buffer is D3DFMT_X8R8G8B8 (linear on Xbox - render targets cannot
        // be swizzled) so the RT must be LIN_X8R8G8B8. Previously used
        // LIN_A8R8G8B8 which was a silent format mismatch and left the
        // captured surface black.
        HRESULT hr = s_device->CreateTexture(
            w, h, 1,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_LIN_X8R8G8B8,
            D3DPOOL_DEFAULT,
            &rt->tex);
        if (FAILED(hr) || !rt->tex)
        {
            RB_LOG_ERROR("createRenderTarget CreateTexture hr=0x%08x", hr);
            free(rt);
            return NULL;
        }
        hr = rt->tex->GetSurfaceLevel(0, &rt->surf);
        if (FAILED(hr) || !rt->surf)
        {
            RB_LOG_ERROR("createRenderTarget GetSurfaceLevel hr=0x%08x", hr);
            rt->tex->Release();
            free(rt);
            return NULL;
        }
        xboxRT_registryAdd(rt);
        RB_LOG_MSG("createRenderTarget %ux%u handle=%p", w, h, rt);
        return (RenderTargetHandle)rt;
    }
    void freeRenderTarget(RenderTargetHandle handle)
    {
        if (!handle) return;
        if (!xboxRT_registryContains(handle)) return;
        XboxRenderTarget* rt = (XboxRenderTarget*)handle;
        xboxRT_registryRemove(rt);
        if (rt->surf) { rt->surf->Release(); rt->surf = NULL; }
        if (rt->tex)  { rt->tex->Release();  rt->tex  = NULL; }
        free(rt);
    }
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

    // Phase 13: palette lookup for the 3DO model path (flat-shaded
    // polygons). s_paletteCpu is XRGB in memory; convert to D3DCOLOR
    // (0xAARRGGBB) with alpha forced opaque.
    u32 gpuPaletteEntryRGBA(u8 index)
    {
        const u32 xrgb = s_paletteCpu[index];
        return 0xFF000000u | (xrgb & 0x00FFFFFFu);
    }
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
        s_texCacheRejectLogged = 0;
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

    // -----------------------------------------------------------------------
    // gpuDrawScreenQuad - 2D screen-space draw (port of upstream's
    // screenDrawGPU.cpp screenGPU_blitTextureScaled / addImageQuad).
    //
    // Vertices use D3DFVF_XYZRHW (pre-transformed: x/y are raw back-buffer
    // pixel coords, z/rhw bypass projection). The virtual display rect
    // (0,0)-(vdispW,vdispH) is mapped linearly to s_destRect on the back
    // buffer, matching the same mapping blitVdispQuad uses to present the
    // software framebuffer.
    //
    // Two texture cases, distinguished by registry membership:
    //   - XboxRenderTarget* (from getRenderTargetTexture) -> sample as
    //     X8R8G8B8 directly, no palette, no alpha test.
    //   - GpuTextureHandle (P8 cached, from gpuGetOrUploadIndexedTexture)
    //     -> palette path + alpha test for DELT transparency.
    void gpuDrawScreenQuad(f32 x0, f32 y0, f32 x1, f32 y1,
                           f32 u0, f32 v0, f32 u1, f32 v1,
                           u32 vdispW, u32 vdispH,
                           GpuTextureHandle tex, bool alphaTest,
                           u32 topColor, u32 botColor)
    {
        if (!s_deviceReady || !s_gpuSceneOpen || vdispW == 0 || vdispH == 0) return;

        // Safe membership check vs the RT registry (no dereferencing of
        // the unknown pointer until after we know it's one we made).
        IDirect3DTexture8* d3dTex = NULL;
        bool isRenderTarget = false;
        if (tex)
        {
            if (xboxRT_registryContains(tex))
            {
                XboxRenderTarget* rt = (XboxRenderTarget*)tex;
                d3dTex = rt->tex;
                isRenderTarget = (d3dTex != NULL);
            }
            else
            {
                d3dTex = (IDirect3DTexture8*)tex;
            }
        }

        ensureP8PaletteSynced();
        if (s_p8Palette && !isRenderTarget) s_device->SetPalette(0, s_p8Palette);

        // Map virtual-display coords to back-buffer pixels via s_destRect.
        // 0.5 sub-pixel offset matches the D3D8 RHW convention (texel
        // centres land on pixel centres without the half-pixel shift).
        const f32 dx0 = (f32)s_destRect.left;
        const f32 dy0 = (f32)s_destRect.top;
        const f32 dw  = (f32)(s_destRect.right  - s_destRect.left);
        const f32 dh  = (f32)(s_destRect.bottom - s_destRect.top);
        const f32 sx  = dw / (f32)vdispW;
        const f32 sy  = dh / (f32)vdispH;
        const f32 fx0 = dx0 + x0 * sx - 0.5f;
        const f32 fy0 = dy0 + y0 * sy - 0.5f;
        const f32 fx1 = dx0 + x1 * sx - 0.5f;
        const f32 fy1 = dy0 + y1 * sy - 0.5f;

        // State block. Mirrors blitVdispQuad / the alpha-tested path.
        s_device->SetRenderState(D3DRS_LIGHTING,         FALSE);
        s_device->SetRenderState(D3DRS_ZENABLE,          FALSE);
        s_device->SetRenderState(D3DRS_ZWRITEENABLE,     FALSE);
        s_device->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
        s_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        s_device->SetRenderState(D3DRS_FOGENABLE,        FALSE);
        s_device->SetRenderState(D3DRS_ALPHATESTENABLE,  alphaTest ? TRUE : FALSE);
        if (alphaTest)
        {
            s_device->SetRenderState(D3DRS_ALPHAREF,  0x80);
            s_device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        }

        s_device->SetTexture(0, d3dTex);
        // MODULATE so per-vertex DIFFUSE colour tints the texel. With
        // colour = 0xFFFFFFFF (default) the result is identical to
        // SELECTARG1; non-white colours produce the per-edge gradient
        // upstream quadDraw2d_add supports.
        s_device->SetTextureStageState(0, D3DTSS_COLOROP,   d3dTex ? D3DTOP_MODULATE  : D3DTOP_SELECTARG2);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        s_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        // Alpha source: TEXTURE for the alpha-test sprite path (palette
        // index 0 uploads with alpha=0 so it's discarded), DIFFUSE for
        // the opaque RT path (the RT is captured from a D3DFMT_X8R8G8B8
        // back buffer whose X channel is undefined - reading alpha from
        // texture there would be garbage, often 0, which makes the quad
        // invisible). DIFFUSE alpha is 0xFF from the vertex colour.
        s_device->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        s_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, alphaTest ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        s_device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        s_device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        s_device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP);
        s_device->SetTextureStageState(0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP);
        s_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        s_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        // Alpha blending OFF on both paths. Upstream quadDraw2d_draw
        // enables BLEND_ONE / BLEND_INVSRCALPHA for premultiplied alpha,
        // but every call site that reaches us is either fully opaque
        // (RT captured from X8R8G8B8 back buffer) or uses alpha test
        // (DELT sprite with discard transparency). Blending against an
        // undefined-alpha RGBA texture produced black on the previous
        // attempt; turning it off is the simpler correct path.
        s_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

        struct ScreenVert { f32 x, y, z, rhw; u32 color; f32 u, v; };
        const u32 SCREEN_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

        // Quad layout TL, TR, BR, BL — top edge takes topColor, bottom
        // edge takes botColor (matches upstream quadDraw2d_add lines
        // 145-148: vert[0/1] get colors[0], vert[2/3] get colors[1]).
        // Triangle strip order is TL, TR, BL, BR so the diagonal runs
        // TL -> BR.
        ScreenVert q[4];
        q[0].x = fx0; q[0].y = fy0; q[0].z = 0.0f; q[0].rhw = 1.0f; q[0].color = topColor; q[0].u = u0; q[0].v = v0;
        q[1].x = fx1; q[1].y = fy0; q[1].z = 0.0f; q[1].rhw = 1.0f; q[1].color = topColor; q[1].u = u1; q[1].v = v0;
        q[2].x = fx0; q[2].y = fy1; q[2].z = 0.0f; q[2].rhw = 1.0f; q[2].color = botColor; q[2].u = u0; q[2].v = v1;
        q[3].x = fx1; q[3].y = fy1; q[3].z = 0.0f; q[3].rhw = 1.0f; q[3].color = botColor; q[3].u = u1; q[3].v = v1;

        s_device->SetVertexShader(SCREEN_FVF);
        s_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(ScreenVert));

        s_device->SetTexture(0, NULL);
        if (alphaTest) s_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
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

    const TextureGpu* getRenderTargetTexture(RenderTargetHandle h)
    {
        // Return the handle itself reinterpreted as TextureGpu* so the
        // shared GameUI code can pass it back to us through screenGPU_
        // addImageQuad. gpuDrawScreenQuad detects the XBRT_SENTINEL field
        // to unwrap back to an IDirect3DTexture8*.
        return (const TextureGpu*)h;
    }
    void getRenderTargetDim(RenderTargetHandle h, u32* w, u32* hOut)
    {
        if (!h || !xboxRT_registryContains(h))
        {
            if (w)    *w    = 0;
            if (hOut) *hOut = 0;
            return;
        }
        XboxRenderTarget* rt = (XboxRenderTarget*)h;
        if (w)    *w    = rt->width;
        if (hOut) *hOut = rt->height;
    }

    // -----------------------------------------------------------------------
    // Desktop TextureGpu stubs. The Xbox hardware path uses the explicit
    // GpuTextureHandle helpers above instead of the OpenGL-style TextureGpu API.
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
