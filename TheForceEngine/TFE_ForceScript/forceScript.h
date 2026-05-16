#pragma once
// forceScript.h - Xbox stub
// Keeps all enums, constants, and struct types so that logic.h,
// infTypesInternal.h, levelData.h, and serialization.h compile unchanged.
// All function implementations are no-ops in forceScript_xbox.cpp.
// The TFE_DarkForces/Scripting/ folder is excluded from the Xbox build entirely.

#include <TFE_System/types.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/stream.h>
#include <string.h>  // strcpy

namespace TFE_ForceScript
{
    enum ScriptConst
    {
        TFE_MAX_SCRIPT_ARG = 16,
    };

    enum ScriptCallConst
    {
        MAX_SCRIPT_CALL_NAME_LEN = 64,
        MAX_SCRIPT_CALL_ARG      = 5,
    };

    enum FS_BuiltInType
    {
        FSTYPE_STRING = 0,
        FSTYPE_ARRAY,
        FSTYPE_FLOAT2,
        FSTYPE_FLOAT3,
        FSTYPE_FLOAT4,
        FSTYPE_FLOAT2x2,
        FSTYPE_FLOAT3x3,
        FSTYPE_FLOAT4x4,
        FSTYPE_COUNT
    };

    enum ScriptArgType
    {
        ARG_S32 = 0,
        ARG_U32,
        ARG_F32,
        ARG_BOOL,
        ARG_OBJECT,
        ARG_STRING,
        ARG_FLOAT2,
        ARG_FLOAT3,
        ARG_FLOAT4,
    };

    struct ScriptArg
    {
        ScriptArgType type;
        union
        {
            s32   iValue;
            u32   uValue;
            f32   fValue;
            bool  bValue;
            void* objPtr;
            Vec2f float2Value;
            Vec3f float3Value;
            Vec4f float4Value;
            char  strValue[32];
        };
    };

    // Opaque handles - NULL on Xbox (scripting not active).
    typedef void* ModuleHandle;
    typedef void* FunctionHandle;
    typedef void(*ScriptMessageCallback)(LogWriteType type, const char* section, s32 row, s32 col, const char* msg);

    // All functions are no-ops or safe-default returns on Xbox.
    inline void init()                                                               {}
    inline void destroy()                                                            {}
    inline void stopAllFunc()                                                        {}
    inline void overrideCallback(ScriptMessageCallback /*cb*/ = NULL)               {}
    inline void update(f32 /*dt*/ = 0.0f)                                           {}
    inline void* getEngine()                                                         { return NULL; }

    inline ModuleHandle   getModule(const char*)                                     { return NULL; }
    inline ModuleHandle   createModule(const char*, const char*, const char*, u32)   { return NULL; }
    inline ModuleHandle   createModule(const char*, const char*, bool, u32)          { return NULL; }
    inline void           deleteModule(const char*)                                  {}
    inline FunctionHandle findScriptFuncByDecl(ModuleHandle, const char*)            { return NULL; }
    inline FunctionHandle findScriptFuncByName(ModuleHandle, const char*)            { return NULL; }
    inline FunctionHandle findScriptFuncByNameNoCase(ModuleHandle, const char*)      { return NULL; }
    inline s32            getObjectTypeId(FS_BuiltInType)                            { return -1; }
    inline s32            execFunc(FunctionHandle, s32 = 0, const ScriptArg* = NULL) { return -1; }
    inline void           resume(s32)                                                {}
    inline void           serialize(Stream*)                                         {}

    // scriptArg helpers - kept for any inline usage in headers.
    inline ScriptArg scriptArg(s32  v) { ScriptArg a; a.type = ARG_S32;    a.iValue = v;      return a; }
    inline ScriptArg scriptArg(u32  v) { ScriptArg a; a.type = ARG_U32;    a.uValue = v;      return a; }
    inline ScriptArg scriptArg(f32  v) { ScriptArg a; a.type = ARG_F32;    a.fValue = v;      return a; }
    inline ScriptArg scriptArg(bool v) { ScriptArg a; a.type = ARG_BOOL;   a.bValue = v;      return a; }
    inline ScriptArg scriptArg(const char* v)
    {
        ScriptArg a; a.type = ARG_STRING;
        strncpy(a.strValue, v ? v : "", 31); a.strValue[31] = 0;
        return a;
    }
    inline ScriptArg scriptArg(Vec2f v) { ScriptArg a; a.type = ARG_FLOAT2; a.float2Value = v; return a; }
    inline ScriptArg scriptArg(Vec3f v) { ScriptArg a; a.type = ARG_FLOAT3; a.float3Value = v; return a; }
    inline ScriptArg scriptArg(Vec4f v) { ScriptArg a; a.type = ARG_FLOAT4; a.float4Value = v; return a; }
    inline ScriptArg scriptArg(void* v) { ScriptArg a; a.type = ARG_OBJECT; a.objPtr = v;      return a; }

} // namespace TFE_ForceScript
