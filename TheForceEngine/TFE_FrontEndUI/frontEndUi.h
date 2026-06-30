#pragma once
// frontEndUi.h - Xbox stub
// Satisfies all TFE_FrontEndUI:: call sites in game code.
// The actual ImGui-based front end is excluded from the Xbox build.
// AppState enum is redeclared here (matches main_xbox.cpp).

#include <TFE_System/types.h>

// AppState must match main_xbox.cpp declaration.
enum AppState
{
    APP_STATE_MENU = 0,
    APP_STATE_EDITOR,
    APP_STATE_LOAD,
    APP_STATE_MODS,
    APP_STATE_OPTIONS,
    APP_STATE_GAME,
    APP_STATE_QUIT,
    APP_STATE_NO_GAME_DATA,
    APP_STATE_CANNOT_RUN,
    APP_STATE_EXIT_TO_MENU,
    APP_STATE_SET_DEFAULTS,
    APP_STATE_COUNT,
    APP_STATE_UNINIT = APP_STATE_COUNT
};

struct ImFont;
struct IGame;

namespace TFE_FrontEndUI
{
    // All stubs - no ImGui, no UI on Xbox.
    inline void     setMenuReturnState(AppState) {}
    inline bool     toggleConsole()              { return false; }
    inline void     exitToMenu()                 {}
    inline bool     isConsoleOpen()              { return false; }
    inline bool     isConsoleAnimating()         { return false; }
    inline void     logToConsole(const char*)    {}
    inline bool     toggleEnhancements()         { return false; }
    inline void     setAppState(AppState)        {}
    inline AppState update()                     { return APP_STATE_GAME; }
    inline void     draw(bool, bool, bool, bool) {}
    inline void     init()                       {}
    inline void     initConsole()                {}
    inline void     shutdown()                   {}
    inline void     setCanSave(bool)             {}
    inline void     setCurrentGame(IGame*)       {}
    inline bool     uiControlsEnabled()          { return false; }
    inline bool     isConfigMenuOpen()           { return false; }
    inline void     enableConfigMenu()           {}
    inline AppState menuReturn()                 { return APP_STATE_GAME; }
    inline bool     isNoDataMessageSet()         { return false; }
    inline void     clearNoDataState()           {}
    inline bool     isGuiFrameActive()           { return false; }
    inline void     toggleProfilerView()         {}
    inline char*    getSelectedMod()             { return NULL; }
    inline void     modLoader_read()             {}
}
