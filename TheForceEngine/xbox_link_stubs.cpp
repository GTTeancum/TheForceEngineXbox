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
#include <TFE_ForceScript/forceScript.h>
#include <TFE_DarkForces/Landru/lsystem.h>
#include <TFE_Settings/windows/registry.h>

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
	// RClassic_GPU stubs
	// =====================================================================
	namespace RClassic_GPU
	{
		void resetState() {}
		void setupInitCameraAndLights(s32, s32) {}
		void changeResolution(s32, s32) {}
		void computeCameraTransform(RSector*, f32, f32, f32, f32, f32) {}
		void transformPointByCamera(vec3_float*, vec3_float*) {}
		void computeSkyOffsets() {}
	}

	// =====================================================================
	// TFE_Sectors_GPU class stubs
	// The constructor is already inline in rsectorGPU.h, so we only
	// need the virtual methods and non-virtual helpers.
	// =====================================================================
	void TFE_Sectors_GPU::destroy()             {}
	void TFE_Sectors_GPU::reset()               {}
	void TFE_Sectors_GPU::prepare()             {}
	void TFE_Sectors_GPU::draw(RSector*)        {}
	void TFE_Sectors_GPU::subrendererChanged()  {}
	void TFE_Sectors_GPU::flushCache()          {}
	void TFE_Sectors_GPU::flushTextureCache()   {}
	TextureGpu* TFE_Sectors_GPU::getColormap()  { return NULL; }

	// =====================================================================
	// Camera globals (defined in rclassicGPU.cpp which is excluded)
	// Referenced by other GPU code that is also excluded, but the linker
	// may still need them if jediRenderer.cpp touches the GPU path.
	// =====================================================================
	Vec3f s_cameraPos   = { 0.0f, 0.0f, 0.0f };
	Vec3f s_cameraDir   = { 0.0f, 0.0f, 0.0f };
	Vec3f s_cameraDirXZ = { 0.0f, 0.0f, 0.0f };

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
