#pragma once

#include <stdint.h>
#include <math.h>
#include <float.h>

typedef uint64_t u64;
typedef int64_t  s64;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint8_t  u8;
typedef int8_t   s8;
typedef intptr_t iptr;
typedef float    f32;
typedef double   f64;

// ---------------------------------------------------------------------------
// C++11 strongly-typed-enum compatibility shim.
//
// VC71 (XDK 5558 toolchain) doesn't support `enum NAME : TYPE { ... }`. On
// Xbox the macro strips the underlying-type spec; default int sizing matches
// u32/s32 on x86. On PC the macro expands to the proper spec, preserving
// the explicit type for size-sensitive code.
//
// Usage:
//   enum FooBits TFE_ENUM_BASE(u32) { Bit0 = 1, Bit1 = 2 };
// ---------------------------------------------------------------------------
#ifndef TFE_ENUM_BASE
  #ifdef _XBOX
    #define TFE_ENUM_BASE(x)
  #else
    #define TFE_ENUM_BASE(x) : x
  #endif
#endif

// ---------------------------------------------------------------------------
// Atomic types
// On PC builds: std::atomic from <atomic>
// On Xbox / MSVC 2005: use LONG with Interlocked* intrinsics.
//   The Xbox kernel exposes InterlockedExchange, InterlockedCompareExchange,
//   InterlockedIncrement, InterlockedDecrement in <xtl.h>.
//   We wrap them in a minimal struct that covers the usage in this codebase
//   (load, store, compare_exchange, operator=, implicit cast).
// ---------------------------------------------------------------------------
#ifdef _XBOX

#include <xtl.h>

struct atomic_u32
{
    volatile LONG val;
    atomic_u32()             : val(0)         {}
    atomic_u32(u32 v)        : val((LONG)v)   {}
    void  store(u32 v)       { InterlockedExchange((LPLONG)&val, (LONG)v); }
    u32   load()       const { return (u32)val; }
    operator u32()     const { return load(); }
    void  operator=(u32 v)   { store(v); }
    bool  compare_exchange_strong(u32& expected, u32 desired)
    {
        LONG old = InterlockedCompareExchange((LPLONG)&val, (LONG)desired, (LONG)expected);
        if ((u32)old == expected) return true;
        expected = (u32)old;
        return false;
    }
};

struct atomic_s32
{
    volatile LONG val;
    atomic_s32()             : val(0)         {}
    atomic_s32(s32 v)        : val((LONG)v)   {}
    void  store(s32 v)       { InterlockedExchange((LPLONG)&val, (LONG)v); }
    s32   load()       const { return (s32)val; }
    operator s32()     const { return load(); }
    void  operator=(s32 v)   { store(v); }
    s32 operator++()    { return (s32)InterlockedIncrement((LPLONG)&val); }
    s32 operator++(int) { s32 old = load(); store(old+1); return old; }
    s32 operator--()    { return (s32)InterlockedDecrement((LPLONG)&val); }
    s32 operator--(int) { s32 old = load(); store(old-1); return old; }
};

struct atomic_f32
{
    // Float atomics are used only for volume; full Interlocked precision not needed.
    // Using a volatile float with a LONG exchange via type-pun.
    volatile LONG val;
    atomic_f32() : val(0) {}
    atomic_f32(f32 v) { store(v); }
    void store(f32 v)
    {
        LONG bits;
        memcpy(&bits, &v, sizeof(LONG));
        InterlockedExchange((LPLONG)&val, bits);
    }
    f32 load() const
    {
        LONG bits = val;
        f32 f;
        memcpy(&f, &bits, sizeof(f32));
        return f;
    }
    operator f32() const { return load(); }
    void operator=(f32 v) { store(v); }
};

struct atomic_bool
{
    volatile LONG val;
    atomic_bool()           : val(0)      {}
    atomic_bool(bool v)     : val(v?1:0)  {}
    void  store(bool v)     { InterlockedExchange((LPLONG)&val, v ? 1 : 0); }
    bool  load()      const { return val != 0; }
    operator bool()   const { return load(); }
    void  operator=(bool v) { store(v); }
};

#else // PC build

#include <atomic>
typedef std::atomic<uint32_t> atomic_u32;
typedef std::atomic<int32_t>  atomic_s32;
typedef std::atomic<float>    atomic_f32;
typedef std::atomic<bool>     atomic_bool;

#endif // _XBOX

// ---------------------------------------------------------------------------
// Math structs
// ---------------------------------------------------------------------------
struct Vec2f
{
    union
    {
        struct { f32 x, z; };
        f32 m[2];
    };
};

struct Vec3f
{
    union
    {
        struct { f32 x, y, z; };
        f32 m[3];
    };
};

struct Vec4f
{
    union
    {
        struct { f32 x, y, z, w; };
        f32 m[4];
    };
};

struct Vec2i
{
    union
    {
        struct { s32 x, z; };
        s32 m[2];
    };
};

struct Vec3i
{
    union
    {
        struct { s32 x, y, z; };
        s32 m[3];
    };
};

struct Vec4i
{
    union
    {
        struct { s32 x, y, z, w; };
        s32 m[4];
    };
};

struct Vec4ui
{
    union
    {
        struct { u32 x, y, z, w; };
        u32 m[4];
    };
};

struct Mat3
{
    union
    {
        struct { Vec3f m0, m1, m2; };
        struct { Vec3f m[3]; };
        f32 data[9];
    };
};

struct Mat4
{
    union
    {
        struct { Vec4f m0, m1, m2, m3; };
        struct { Vec4f m[4]; };
        f32 data[16];
    };
};

// ---------------------------------------------------------------------------
// JEDI bool type
// ---------------------------------------------------------------------------
typedef u32 JBool;
enum JediBool
{
    JTRUE  = 0xffffffffu,
    JFALSE = 0u,
};

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------
#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp  _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#endif

#ifdef _WIN32
#define TFE_STDCALL __stdcall
#else
#define TFE_STDCALL
#endif

#define TFE_ARRAYSIZE(arr)      (sizeof(arr)/sizeof(*arr))
#define TFE_ARRAYPOS(ptr, arr)  s32(((u8*)(ptr) - (u8*)(arr)) / sizeof(*(ptr)))

#define FLAG_BIT(bit)   (1u  << u32(bit))
#define FLAG_BIT64(bit) (1ull << u64(bit))
#define SIGN_BIT(x)     ((x)<0?1:0)

#define PI     3.14159265358979323846f
#define TWO_PI 6.28318530717958647693f

// ---------------------------------------------------------------------------
// Asset pool IDs
// ---------------------------------------------------------------------------
enum AssetPool
{
    POOL_GAME = 0,
    POOL_LEVEL,
    POOL_COUNT
};
