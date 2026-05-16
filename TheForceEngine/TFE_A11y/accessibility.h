#pragma once
// accessibility.h - Xbox stub
// All caption/accessibility functionality is disabled on Xbox.
// std::string kept for settings.cpp toLower() compatibility.

#include <TFE_System/types.h>
#include <string>
#include <ctype.h>
using std::string;

namespace TFE_A11Y
{
    enum CaptionEnv
    {
        CC_GAMEPLAY = 0,
        CC_CUTSCENE,
    };

    inline void  init()                                     {}
    inline void  shutdown()                                 {}
    inline bool  cutsceneCaptionsEnabled()                  { return false; }
    inline bool  gameplayCaptionsEnabled()                  { return false; }
    inline Vec2f drawCaptions()                             { Vec2f v; v.x = 0; v.z = 0; return v; }
    inline void  clearActiveCaptions()                      {}
    inline void  onSoundPlay(char*, CaptionEnv)             {}
    inline bool  hasPendingFont()                           { return false; }
    inline void  loadPendingFont()                          {}

    // toLower: used in settings.cpp mod-override parsing (unreachable on Xbox).
    // Provided so settings.cpp compiles without modification.
    inline string toLower(string input)
    {
        for (size_t i = 0; i < input.size(); i++)
            input[i] = (char)tolower((unsigned char)input[i]);
        return input;
    }
}
