// xbox_compat.h
// Forced include for TheForceEngine Xbox build.
// Mirrors the pattern of CoreXboxCompat.h from the established XDK project.
//
// Set as forced include in vcproj:
//   ForcedIncludeFiles="xbox_compat.h"
//
// This file must sit in the same directory as the .vcproj (TheForceEngine\).

#ifndef XBOX_COMPAT_H
#define XBOX_COMPAT_H

// -----------------------------------------------------------------------
// Pull in the XDK headers first.
//
// C:\XDK\xbox\include contains a desktop Direct3D 8 header.  Do not let
// xtl.h select it: the desktop and Xbox enums and presentation structures
// are not ABI-compatible.  The clean 5558 headers below are byte-identical
// to the stock 5849 D3D8/D3D8Types headers and are already required by the
// direct Xbox build.
// -----------------------------------------------------------------------
#ifndef NOD3D
#define NOD3D
#define TFE_XBOX_COMPAT_OWNS_NOD3D
#endif
#ifndef NODSOUND
#define NODSOUND
#define TFE_XBOX_COMPAT_OWNS_NODSOUND
#endif
#include <xtl.h>
#include "C:/XDK_5558/XDK/xbox/include/D3D8.h"
#include "C:/XDK_5558/XDK/xbox/include/D3DX8.h"
#include "C:/XDK_5558/XDK/xbox/include/DSound.h"
#ifdef TFE_XBOX_COMPAT_OWNS_NODSOUND
#undef NODSOUND
#undef TFE_XBOX_COMPAT_OWNS_NODSOUND
#endif
#ifdef TFE_XBOX_COMPAT_OWNS_NOD3D
#undef NOD3D
#undef TFE_XBOX_COMPAT_OWNS_NOD3D
#endif
#include <xgmath.h>

// -----------------------------------------------------------------------
// TFE basic integer typedefs (u8/s8/u16/s16/u32/s32/u64/s64/f32/f64).
// Forced here because many TFE headers (robjData.h, logic.h, actorModule.h,
// collision.h, serialization.h, leveldata.h) use these types without
// explicitly including <TFE_System/types.h>. Under VC8 something else
// pulled them in transitively; VC71 needs the explicit forced include.
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
// Kill XDK macros that collide with TFE code
// -----------------------------------------------------------------------
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif
#ifdef Top
#undef Top
#endif

// -----------------------------------------------------------------------
// NULL
// -----------------------------------------------------------------------
#ifndef NULL
#define NULL 0
#endif

// -----------------------------------------------------------------------
// C++11 -> C++03 compatibility shims
// -----------------------------------------------------------------------
#define nullptr NULL
#define override
#define final
#define static_assert(expr, msg)
#define noexcept throw()
#define constexpr const

// -----------------------------------------------------------------------
// Disable editor on Xbox
// -----------------------------------------------------------------------
#ifdef ENABLE_EDITOR
#undef ENABLE_EDITOR
#endif
#define ENABLE_EDITOR 0

// -----------------------------------------------------------------------
// VC8 for-scope conformance
// -----------------------------------------------------------------------
#pragma conform(forScope, off)

// -----------------------------------------------------------------------
// _XBOX safety define
// -----------------------------------------------------------------------
#ifndef _XBOX
#define _XBOX
#endif

// -----------------------------------------------------------------------
// C runtime shims for MSVC 2005
// -----------------------------------------------------------------------
#define snprintf _snprintf
#define strtof(s,e) ((float)strtod((s),(e)))

// XDK's stdint.h in this environment is only a placeholder used by another
// port's forced include, so provide the fixed-width integer types here.
#ifndef _STDINT_H_TYPES_DEFINED_XBOX_TFE
#define _STDINT_H_TYPES_DEFINED_XBOX_TFE
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed __int64     int64_t;
typedef unsigned __int64   uint64_t;
typedef int                intptr_t;
typedef unsigned int       uintptr_t;
#endif

#ifndef INT16_MIN
#define INT16_MIN (-32768)
#endif
#ifndef INT16_MAX
#define INT16_MAX 32767
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 0xffffu
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffu
#endif

// Math constants
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// uint_fast32_t (C99 type not in MSVC 2005)
#ifndef _UINT_FAST32_T_DEFINED
#define _UINT_FAST32_T_DEFINED
typedef unsigned int uint_fast32_t;
#endif

// errno_t may not be defined in XDK headers
#ifndef _ERRCODE_DEFINED
#define _ERRCODE_DEFINED
typedef int errno_t;
#endif

// -----------------------------------------------------------------------
// Secure CRT shims (XDK lacks _s variants)
// -----------------------------------------------------------------------
#ifndef _XBOX_CRT_SHIMS
#define _XBOX_CRT_SHIMS

#include <stdio.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------
// Xbox-wide debug logging.
// These are implemented in TFE_System/system_xbox.cpp and intentionally
// avoid dependencies so any translation unit can emit boot diagnostics.
// -----------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
void TFE_XboxLog(const char* msg);
void TFE_XboxLogf(const char* tag, const char* fmt, ...);
#ifdef __cplusplus
}
#endif

static __inline errno_t fopen_s(FILE** pf, const char* fn, const char* mode)
{ *pf = fopen(fn, mode); return (*pf) ? 0 : -1; }

static __inline errno_t localtime_s(struct tm* t, const time_t* c)
{ struct tm* r = localtime(c); if(r){*t=*r; return 0;} return -1; }

static __inline errno_t strcpy_s(char* d, size_t n, const char* s)
{ strncpy(d, s, n); d[n-1]=0; return 0; }

static __inline errno_t strncpy_s(char* d, size_t dn, const char* s, size_t c)
{ size_t m = (c < dn-1) ? c : dn-1; strncpy(d, s, m); d[m]=0; return 0; }

// Wide-char file functions — not available on Xbox, stub out
static __inline errno_t _wfopen_s(FILE** pf, const wchar_t* fn, const wchar_t* m)
{ (void)fn; (void)m; *pf = NULL; return -1; }

static __inline errno_t _wfreopen_s(FILE** pf, const wchar_t* fn, const wchar_t* m, FILE* s)
{ (void)fn; (void)m; (void)s; *pf = NULL; return -1; }

// 64-bit file positioning — XDK has ftell/fseek only (32-bit)
#define _ftelli64(f)        ((long long)ftell(f))
#define _fseeki64(f, o, w)  fseek((f), (long)(o), (w))

// _chsize_s — truncate file (XDK has no 64-bit version)
#include <io.h>
static __inline errno_t _chsize_s(int fd, long long sz)
{ return _chsize(fd, (long)sz) == 0 ? 0 : -1; }

#endif // _XBOX_CRT_SHIMS

// -----------------------------------------------------------------------
// TFE_System/types.h - provides u8/s8/u16/s16/u32/s32/u64/s64/f32/f64
// that many TFE headers use without explicit include. Pulled in here so
// every TU sees them. Must come AFTER our stdint.h typedef fallbacks
// above so types.h compiles cleanly on XDKs that lack stdint.h.
// -----------------------------------------------------------------------
#ifdef __cplusplus
#include <TFE_System/types.h>
#endif

#endif // XBOX_COMPAT_H
