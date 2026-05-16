// texturePacker_xbox.cpp
// Xbox stub for TFE_RenderShared texture packer.
// Only implements the four functions called from game code on the software
// renderer path. All GPU-coupled functions (init, destroy, begin, commit,
// pack, etc.) are excluded - they are only reached via the GPU renderer
// which is not active on Xbox.
//
// Exclude texturePacker.cpp from the Xbox build configuration.
// Add this file to the Xbox configuration only.

#include <TFE_RenderShared/texturePacker.h>
#include <TFE_System/types.h>
#include <TFE_System/system.h>
#include <string.h>

namespace TFE_Jedi
{
    // ---------------------------------------------------------------------------
    // Internal palette state - matches texturePacker.cpp layout so that
    // getPalette() returns valid data to the software renderer.
    // ---------------------------------------------------------------------------
    enum
    {
        PALETTE_COUNT = 2,
        PALETTE_SIZE  = 256,
    };

    static u32 s_conversionPal[PALETTE_COUNT][PALETTE_SIZE];
    static s32 s_colorIndexStart = -1;

    #define CONV_6bitTo8bit_Tex(x) (((x)<<2) | ((x)>>4))

    // ---------------------------------------------------------------------------
    // texturepacker_setConversionPalette
    // Called by mission.cpp and escapeMenu.cpp to set up the palette tables
    // used by the software renderer for colour conversion.
    // ---------------------------------------------------------------------------
    void texturepacker_setConversionPalette(s32 index, s32 bpp, const u8* input)
    {
        TFE_XboxLogf("TexturePacker", "setConversionPalette index=%d bpp=%d input=%p", index, bpp, input);
        if (index < 0 || index >= PALETTE_COUNT)
        {
            TFE_System::logWrite(LOG_ERROR, "TexturePacker",
                "Invalid palette index %d.", index);
            return;
        }

        u32*       output   = s_conversionPal[index];
        const u8*  srcColor = input;

        if (bpp == 6)
        {
            for (s32 i = 0; i < PALETTE_SIZE; i++, output++, srcColor += 3)
            {
                *output = CONV_6bitTo8bit_Tex(srcColor[0])
                        | (CONV_6bitTo8bit_Tex(srcColor[1]) << 8u)
                        | (CONV_6bitTo8bit_Tex(srcColor[2]) << 16u)
                        | (0xffu << 24u);
            }
        }
        else if (bpp == 8)
        {
            for (s32 i = 0; i < PALETTE_SIZE; i++, output++, srcColor += 3)
            {
                *output = srcColor[0]
                        | (srcColor[1] << 8u)
                        | (srcColor[2] << 16u)
                        | (0xffu << 24u);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // texturepacker_setIndexStart
    // Called by hud.cpp.
    // ---------------------------------------------------------------------------
    void texturepacker_setIndexStart(s32 colorIndexStart)
    {
        s_colorIndexStart = colorIndexStart;
        TFE_XboxLogf("TexturePacker", "setIndexStart %d", colorIndexStart);
    }

    // ---------------------------------------------------------------------------
    // texturepacker_reset
    // Called by darkForcesMain.cpp on level transition.
    // No GPU resources to release on Xbox - safe no-op.
    // ---------------------------------------------------------------------------
    void texturepacker_reset()
    {
        TFE_XboxLogf("TexturePacker", "reset");
        // No global texture packer on Xbox - software renderer uses CPU palette.
    }

    // ---------------------------------------------------------------------------
    // texturepacker_freeGlobal
    // Called from main_xbox.cpp shutdown.
    // ---------------------------------------------------------------------------
    void texturepacker_freeGlobal()
    {
        TFE_XboxLogf("TexturePacker", "freeGlobal");
        // No GPU resources to free on Xbox.
    }

    // ---------------------------------------------------------------------------
    // getPalette - used internally by the software renderer path.
    // Expose so virtualFramebuffer.cpp can call it if needed.
    // ---------------------------------------------------------------------------
    const u32* getPalette(s32 index)
    {
        if (index < 0 || index >= PALETTE_COUNT)
            return NULL;
        return s_conversionPal[index];
    }

    // ---------------------------------------------------------------------------
    // Stub implementations of all other texturePacker functions that may be
    // referenced by object files in the Xbox build.
    // These are never called on the software renderer path.
    // ---------------------------------------------------------------------------
    TexturePacker* texturepacker_init(const char*, s32, s32)      { return NULL; }
    void texturepacker_destroy(TexturePacker*)                     {}
    bool texturepacker_begin(TexturePacker*)                       { return false; }
    void texturepacker_commit()                                    {}
    void texturepacker_reserveCommitedPages(TexturePacker*)        {}
    bool texturepacker_hasReservedPages(TexturePacker*)            { return false; }
    void texturepacker_discardUnreservedPages(TexturePacker*)      {}
    TexturePacker* texturepacker_getGlobal()                       { return NULL; }
    s32  texturepacker_pack(TextureListCallback, AssetPool)        { return 0; }

} // namespace TFE_Jedi
