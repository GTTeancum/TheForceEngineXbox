#pragma once
//////////////////////////////////////////////////////////////////////
// Pack level textures into an atlas texture or array of textures.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/memoryPool.h>
#include <TFE_Jedi/Renderer/textureInfo.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_RenderBackend/textureGpu.h>
#include <TFE_RenderBackend/shaderBuffer.h>

struct TextureData;
struct AnimatedTexture;
struct WaxFrame;

namespace TFE_Jedi
{
    struct TextureNode;

    struct TexturePage
    {
        s32          textureCount;
        u8*          backingMemory;
        TextureNode* root;

#ifdef _XBOX
        TexturePage() : textureCount(0), backingMemory(NULL), root(NULL) {}
#endif
    };

    struct TexturePacker
    {
        // GPU resources
        ShaderBuffer textureTableGPU;
        TextureGpu*  texture;

        // CPU memory
        Vec4i*         textureTable;
        TexturePage**  pages;

        // General Data
        s32  width;
        s32  height;
        s32  texturesPacked;
        s32  pageCount;
        s32  reservedPages;
        s32  reservedTexturesPacked;
        u32  bytesPerTexel;
        u32  pageSize;
        bool trueColor;

        // Mip maps.
        u32 mipCount;
        u32 mipPadding;
        u32 mipOffset[8];

        // For debugging.
        char name[64];

#ifdef _XBOX
        TexturePacker()
            : texture(NULL), textureTable(NULL), pages(NULL)
            , width(0), height(0), texturesPacked(0), pageCount(0)
            , reservedPages(0), reservedTexturesPacked(0)
            , bytesPerTexel(1), pageSize(0), trueColor(false)
            , mipCount(1), mipPadding(0)
        {
            name[0] = 0;
            memset(mipOffset, 0, sizeof(mipOffset));
        }
#endif
    };

    // PC builds keep the original in-class initializers via the non-Xbox path.
    // On Xbox the constructors above handle initialization.

    TexturePacker* texturepacker_init(const char* name, s32 width, s32 height);
    void texturepacker_destroy(TexturePacker* texturePacker);

    bool texturepacker_begin(TexturePacker* texturePacker);
    void texturepacker_commit();

    void texturepacker_reset();

    void texturepacker_reserveCommitedPages(TexturePacker* texturePacker);
    bool texturepacker_hasReservedPages(TexturePacker* texturePacker);
    void texturepacker_discardUnreservedPages(TexturePacker* texturePacker);

    TexturePacker* texturepacker_getGlobal();
    void texturepacker_freeGlobal();

    s32 texturepacker_pack(TextureListCallback getList, AssetPool pool);

    void texturepacker_setIndexStart(s32 colorIndexStart = -1);
    void texturepacker_setConversionPalette(s32 index, s32 bpp, const u8* input);

} // namespace TFE_Jedi
