#pragma once
// console.h - Xbox stub
// Satisfies TFE_Console:: calls in settings.cpp (writeCVars/parseCVars).
// No ImGui console on Xbox. CVars are parsed from settings.ini but not
// registered or executed. CVar count is always 0 so writeCVars writes nothing.

#include <TFE_System/types.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdlib.h>

enum CVarFlag
{
    CVFLAG_NONE             = 0,
    CVFLAG_READ_ONLY        = (1 << 0),
    CVFLAG_DO_NOT_SERIALIZE = (1 << 1),
};

// Macro stubs - do nothing on Xbox
#define CVAR_INT(var, name, flags, help)
#define CVAR_FLOAT(var, name, flags, help)
#define CVAR_BOOL(var, name, flags, help)
#define CVAR_STRING(var, name, flags, help)
#define CCMD(name, func, argCount, help)
#define CCMD_NOREPEAT(name, func, argCount, help)

// ConsoleArgList — needed by mission.cpp, audioSystem.cpp, igame.cpp, player.cpp
typedef std::vector<std::string> ConsoleArgList;

namespace TFE_Console
{
    // ConsoleFunc typedef — used by mission.cpp as TFE_Console::ConsoleFunc
    // Must be declared before registerCommand which uses it.
    typedef void (*ConsoleFunc)(const ConsoleArgList&);

    enum CVarType
    {
        CVAR_INT = 0,
        CVAR_FLOAT,
        CVAR_BOOL,
        CVAR_STRING,
    };

    // Minimal CVar stub — fields accessed by writeCVars in settings.cpp.
    struct CVar
    {
        char   name[64];
        char   helpString[128];
        CVarType type;
        u32    flags;
        u32    maxLen;
        union
        {
            s32*  valueInt;
            f32*  valueFloat;
            bool* valueBool;
            char* valueString;
            void* valuePtr;
        };
        union { s32 defaultInt; f32 defaultFlt; bool defaultBool; };
        char   defaultString[256];
        union { s32 serializedInt; f32 serializedFlt; bool serializedBool; };
        char   serializedString[256];

        CVar()
        {
            name[0] = 0; helpString[0] = 0;
            type = CVAR_INT; flags = 0; maxLen = 0;
            valuePtr = NULL; defaultInt = 0; defaultString[0] = 0;
            serializedInt = 0; serializedString[0] = 0;
        }
        // c_str() shim so settings.cpp's cvar->serializedString.c_str() compiles.
        // Not a real std::string — this is accessed only inside writeCVars which
        // returns immediately because getCVarCount() == 0.
        const char* c_str() const { return serializedString; }
    };

    inline void registerCVarInt(const char*, u32, s32*, const char*)      {}
    inline void registerCVarFloat(const char*, u32, f32*, const char*)    {}
    inline void registerCVarBool(const char*, u32, bool*, const char*)    {}
    inline void registerCVarString(const char*, u32, char*, u32, const char*) {}
    inline void registerCommand(const char*, ConsoleFunc, u32, const char*, bool = true) {}

    inline void addSerializedCVarInt(const char*, s32)    {}
    inline void addSerializedCVarFloat(const char*, f32)  {}
    inline void addSerializedCVarBool(const char*, bool)  {}
    inline void addSerializedCVarString(const char*, const char*) {}

    inline bool init()       { return true; }
    inline void destroy()    {}
    inline void update()     {}
    inline bool isOpen()     { return false; }
    inline bool isAnimating(){ return false; }
    inline void startOpen()  {}
    inline void startClose() {}
    inline void addToHistory(const char*) {}

    // Always 0 — writeCVars loop body never executes.
    inline u32        getCVarCount()              { return 0; }
    inline const CVar* getCVarByIndex(u32)        { return NULL; }

    // getFloatArg — used by audioSystem.cpp, midiPlayer.cpp, player.cpp
    inline f32 getFloatArg(const std::string& arg) { return (f32)atof(arg.c_str()); }
}
