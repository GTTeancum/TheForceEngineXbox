// main_xbox.cpp
// Xbox XBE entry point for The Force Engine.
// Replaces main.cpp for the Xbox build configuration.
//
// Stripped vs PC main.cpp:
//   - No SDL
//   - No command-line parsing (no args on Xbox)
//   - No TFE_FrontEndUI / ImGui (game draws its own UI into framebuffer)
//   - No TFE_Editor
//   - No TFE_ForceScript (commented out)
//   - No TFE_A11Y
//   - No screenshot / GIF recording
//   - No mod override STL path
//   - Game source data lives in DARK\ relative to XBE
//   - Goes straight to APP_STATE_GAME - no front-end menu
//
// Things kept:
//   - Full settings init/shutdown (saves/restores config from disk)
//   - TFE_Audio (PCM, OGG via STB Vorbis - see audio_xbox.cpp)
//   - inputMapping, TFE_Input state machine
//   - TFE_InputXbox XInput polling
//   - TFE_SaveSystem
//   - All JEDI game code paths

#include "version.h"
#include <TFE_System/types.h>
#include <TFE_System/system.h>
#include <TFE_System/frameLimiter.h>
#include <TFE_System/tfeMessage.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Archive/archive.h>
#include <TFE_Archive/gobArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Game/reticle.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/audioDevice.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Input/replay.h>
#include <TFE_Settings/settings.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Asset/paletteAsset.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Asset/colormapAsset.h>
#include <TFE_Asset/fontAsset.h>
#include <TFE_Asset/gmidAsset.h>
#include <TFE_Asset/spriteAsset.h>
#include <TFE_Asset/textureAsset.h>
#include <TFE_Asset/vocAsset.h>
#include <TFE_Asset/vueAsset.h>
#include <TFE_ExternalData/dfLogics.h>
#include <TFE_ExternalData/pickupExternal.h>
#include <TFE_ExternalData/weaponExternal.h>
#include <TFE_System/cJSON.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_DarkForces/cheats.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/player.h>

#include "xbox_avenger_thumb.inc"
#include "xbox_dashboard_assets.inc"

// AppState is defined in frontEndUi.h which pulls in STL and ImGui.
// Redeclare the enum directly here for Xbox to avoid those dependencies.
// Keep in sync with TFE_FrontEndUI/frontEndUi.h.
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

// Xbox-specific
#include <TFE_Input/input_xbox.h>

#include <xtl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

using namespace TFE_Input;

// ---------------------------------------------------------------------------
// Version string (from gitVersion.h / version.h)
// ---------------------------------------------------------------------------
#ifndef c_gitVersion
#define c_gitVersion "Xbox"
#endif

#define PROGRAM_ERROR   1
#define PROGRAM_SUCCESS 0

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool     s_loop      = true;
static IGame*   s_curGame   = NULL;
static AppState s_curState  = APP_STATE_UNINIT;
static bool     s_soundPaused = false;
static s32      s_startMenuSelection = 0;
static u32      s_startMenuFrame = 0;
static bool     s_startStickUpHeld = false;
static bool     s_startStickDownHeld = false;
static s32      s_loadMenuSelection = 0;
static u32      s_loadMenuFrame = 0;
static bool     s_loadStickUpHeld = false;
static bool     s_loadStickDownHeld = false;
static s32      s_modMenuSelection = 0;
static u32      s_modMenuFrame = 0;
static bool     s_modStickUpHeld = false;
static bool     s_modStickDownHeld = false;
static s32      s_optionsSelection = 0;
static s32      s_optionsScroll = 0;
static s32      s_optionsItemCount = 0;
static u32      s_optionsFrame = 0;
static bool     s_optionsStickUpHeld = false;
static bool     s_optionsStickDownHeld = false;
static bool     s_optionsStickLeftHeld = false;
static bool     s_optionsStickRightHeld = false;
static TFE_RenderBackend::XboxOptionsItem s_optionsItems[32];
static TFE_SaveSystem::SaveHeader s_loadHeaders[6];
static TFE_RenderBackend::XboxLoadSlotInfo s_loadSlots[6];
static char s_loadDateDisplay[6][32];
struct XboxModEntry
{
    TFE_RenderBackend::XboxModInfo ui;
    char path[TFE_MAX_PATH];
    char archiveName[96];
    char saveKey[96];
    char title[64];
    char author[64];
    char version[32];
    char description[256];
    char levelName[32];
    char quickSaveName[TFE_MAX_PATH];
    bool hasQuickSave;
};

struct XboxDf21CatalogEntry
{
    char slug[48];
    char installBase[96];
    char zipBase[96];
    char title[64];
    char author[64];
    char version[32];
    char description[256];
    char thumbPath[TFE_MAX_PATH];
};

static XboxModEntry s_modEntries[12];
static TFE_RenderBackend::XboxModInfo s_modUi[12];
static u32 s_modThumbs[12][TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT];
static bool s_modThumbValid[12];
static u32 s_modCatalogThumbs[12][TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT];
static bool s_modCatalogThumbValid[12];
static s32 s_modCount = 0;
static XboxDf21CatalogEntry s_df21Catalog[240];
static s32 s_df21CatalogCount = 0;
static bool s_df21CatalogLoaded = false;
static bool s_returnToStartRequested = false;
static char s_activeModSearchPath[TFE_MAX_PATH] = "";
static bool s_menuMusicReady = false;
static bool s_menuMusicPlaying = false;
static Archive* s_menuMusicArchive = NULL;
static ImSoundId s_menuMusicSound = 0;
static u32 s_menuMusicHeartbeatFrame = 0;
static s32 s_menuMusicLastChunk = -9999;
static s32 s_menuMusicLastMeasure = -9999;
static s32 s_menuMusicLastBeat = -9999;
static s32 s_menuMusicLastTick = -9999;
static u32 s_menuMusicSamePositionFrames = 0;
static const bool s_menuMusicVerbose = false;
static const bool s_frontendVerbose = false;
static u32 s_frontendPresentPhase = 0;
static AppState s_lastPresentedFrontendState = APP_STATE_UNINIT;
static bool s_soakMode = false;
static bool s_soakMissionReady = false;
static bool s_soakLastSaveOk = false;
static bool s_soakAwaitingLevelChange = false;
static u32  s_soakStartMs = 0;
static u32  s_soakFrame = 0;
static u32  s_soakNextSummaryMs = 0;
static u32  s_soakNextTransitionMs = 0;
static u32  s_soakLevelReadyMs = 0;
static s32  s_soakCycle = 0;
static s32  s_soakStep = 0;
static s32  s_soakTargetIndex = 0;
static s32  s_soakPendingTargetIndex = -1;
static char s_soakLevelName[32] = "";

enum XboxOptionsPage
{
    XOPAGE_ROOT = 0,
    XOPAGE_CONTROLS,
    XOPAGE_VIDEO,
    XOPAGE_AUDIO
};
static XboxOptionsPage s_optionsPage = XOPAGE_ROOT;

enum XboxRootOptionIndex
{
    XROOT_CONTROLS = 0,
    XROOT_VIDEO,
    XROOT_AUDIO,
    XROOT_COUNT
};

enum XboxControlsOptionIndex
{
    XCTRL_LOOK_SENS_X = 0,
    XCTRL_LOOK_SENS_Y,
    XCTRL_RIGHT_STICK_DEADZONE,
    XCTRL_BIND_JUMP,
    XCTRL_BIND_CROUCH,
    XCTRL_BIND_USE,
    XCTRL_BIND_PRIMARY,
    XCTRL_BIND_SECONDARY,
    XCTRL_BIND_AUTOMAP,
    XCTRL_BIND_PREV_WEAPON,
    XCTRL_BIND_NEXT_WEAPON,
    XCTRL_BIND_HEADLAMP,
    XCTRL_BIND_CLEATS,
    XCTRL_BIND_NIGHTVISION,
    XCTRL_BIND_GASMASK,
    XCTRL_BIND_MENU,
    XCTRL_COUNT
};

enum XboxVideoOptionIndex
{
    XVID_ASPECT_OUTPUT = 0,
    XVID_SAFE_ZONE_WIDTH,
    XVID_SAFE_ZONE_HEIGHT,
    XVID_SCREEN_X,
    XVID_SCREEN_Y,
    XVID_COUNT
};

enum XboxVideoMode
{
    XBOX_VIDEO_AUTO = 0,
    XBOX_VIDEO_4X3,
    XBOX_VIDEO_480P_WIDE,
    XBOX_VIDEO_720P_WIDE,
    XBOX_VIDEO_COUNT
};

enum XboxAudioOptionIndex
{
    XAUD_MASTER_VOLUME = 0,
    XAUD_SFX_VOLUME,
    XAUD_MUSIC_VOLUME,
    XAUD_CUTSCENE_SFX,
    XAUD_CUTSCENE_MUSIC,
    XAUD_COUNT
};

enum XboxOptionIconId
{
    XOPT_ICON_A = 0,
    XOPT_ICON_B,
    XOPT_ICON_X,
    XOPT_ICON_Y,
    XOPT_ICON_WHITE,
    XOPT_ICON_BLACK,
    XOPT_ICON_START,
    XOPT_ICON_LSTICK,
    XOPT_ICON_LSTICK_DPAD,
    XOPT_ICON_LSTICK_SMALL,
    XOPT_ICON_RSTICK,
    XOPT_ICON_RSTICK_DPAD,
    XOPT_ICON_RSTICK_SMALL,
    XOPT_ICON_BACK,
    XOPT_ICON_DPAD,
    XOPT_ICON_DPAD_UP,
    XOPT_ICON_DPAD_DOWN,
    XOPT_ICON_DPAD_LEFT,
    XOPT_ICON_DPAD_RIGHT,
    XOPT_ICON_LT,
    XOPT_ICON_RT
};

struct XboxBindingOption
{
    s32 option;
    TFE_Input::InputAction action;
    const char* label;
};

static const XboxBindingOption c_xboxBindingOptions[] =
{
    { XCTRL_BIND_JUMP,        TFE_Input::IADF_JUMP,             "JUMP" },
    { XCTRL_BIND_CROUCH,      TFE_Input::IADF_CROUCH,           "CROUCH" },
    { XCTRL_BIND_USE,         TFE_Input::IADF_USE,              "USE" },
    { XCTRL_BIND_PRIMARY,     TFE_Input::IADF_PRIMARY_FIRE,     "PRIMARY FIRE" },
    { XCTRL_BIND_SECONDARY,   TFE_Input::IADF_SECONDARY_FIRE,   "SECONDARY FIRE" },
    { XCTRL_BIND_AUTOMAP,     TFE_Input::IADF_AUTOMAP,          "AUTOMAP" },
    { XCTRL_BIND_PREV_WEAPON, TFE_Input::IADF_CYCLEWPN_PREV,    "PREV WEAPON" },
    { XCTRL_BIND_NEXT_WEAPON, TFE_Input::IADF_CYCLEWPN_NEXT,    "NEXT WEAPON" },
    { XCTRL_BIND_HEADLAMP,    TFE_Input::IADF_HEAD_LAMP_TOGGLE, "HEADLAMP" },
    { XCTRL_BIND_CLEATS,      TFE_Input::IADF_CLEATS_TOGGLE,    "CLEATS" },
    { XCTRL_BIND_NIGHTVISION, TFE_Input::IADF_NIGHT_VISION_TOG, "NIGHT VISION" },
    { XCTRL_BIND_GASMASK,     TFE_Input::IADF_GAS_MASK_TOGGLE,  "GAS MASK" },
    { XCTRL_BIND_MENU,        TFE_Input::IADF_MENU_TOGGLE,      "PAUSE MENU" },
};
static bool s_optionsCapture = false;

#ifdef _XBOX
static bool xboxAbsoluteFileExists(const char* path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

static bool xboxDetectSoakMode()
{
    return xboxAbsoluteFileExists("D:\\tfe_soak.txt") ||
           xboxAbsoluteFileExists("tfe_soak.txt");
}

static bool xboxReadFileEquals(const char* path, const unsigned char* data, u32 size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(h, NULL);
    if (fileSize != size)
    {
        CloseHandle(h);
        return false;
    }

    bool equal = true;
    unsigned char buffer[512];
    u32 offset = 0;
    while (offset < size)
    {
        DWORD want = size - offset;
        if (want > sizeof(buffer)) want = sizeof(buffer);
        DWORD got = 0;
        if (!ReadFile(h, buffer, want, &got, NULL) || got != want || memcmp(buffer, data + offset, want) != 0)
        {
            equal = false;
            break;
        }
        offset += want;
    }

    CloseHandle(h);
    return equal;
}

static void xboxWriteDashboardMetadataFile(const char* path, const unsigned char* data, u32 size)
{
    if (xboxReadFileEquals(path, data, size))
    {
        return;
    }

    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        TFE_XboxLogf("DashboardMeta", "open failed %s err=%lu", path, GetLastError());
        return;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(h, data, size, &written, NULL);
    CloseHandle(h);
    TFE_XboxLogf("DashboardMeta", "write %s ok=%d bytes=%lu/%u err=%lu",
        path, (ok && written == size) ? 1 : 0, written, size, (ok && written == size) ? 0 : GetLastError());
}

static void xboxEnsureDashboardMetadata()
{
    xboxWriteDashboardMetadataFile("U:\\TitleMeta.xbx", s_xboxDashboardTitleMetaXbx, s_xboxDashboardTitleMetaXbxSize);
    xboxWriteDashboardMetadataFile("U:\\TitleImage.xbx", s_xboxDashboardTitleImageXbx, s_xboxDashboardTitleImageXbxSize);
    xboxWriteDashboardMetadataFile("U:\\SaveImage.xbx", s_xboxDashboardSaveImageXbx, s_xboxDashboardSaveImageXbxSize);
}
#endif

#ifdef _XBOX
static void logXboxResourceSnapshot(const char* tag)
{
    u32 searchPaths = 0;
    u32 localArchives = 0;
    u32 fileMappings = 0;
    TFE_Paths::getResourceCounts(&searchPaths, &localArchives, &fileMappings);

    u64 gameUsed = s_gameRegion ? TFE_Memory::region_getMemoryUsed(s_gameRegion) : 0;
    u64 gameCap = s_gameRegion ? TFE_Memory::region_getMemoryCapacity(s_gameRegion) : 0;
    u64 levelUsed = s_levelRegion ? TFE_Memory::region_getMemoryUsed(s_levelRegion) : 0;
    u64 levelCap = s_levelRegion ? TFE_Memory::region_getMemoryCapacity(s_levelRegion) : 0;
    u64 gameBlocks = 0;
    u64 gameBlockSize = 0;
    u64 levelBlocks = 0;
    u64 levelBlockSize = 0;
    if (s_gameRegion) { TFE_Memory::region_getBlockInfo(s_gameRegion, &gameBlocks, &gameBlockSize); }
    if (s_levelRegion) { TFE_Memory::region_getBlockInfo(s_levelRegion, &levelBlocks, &levelBlockSize); }

    MEMORYSTATUS mem;
    memset(&mem, 0, sizeof(mem));
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatus(&mem);

    TFE_System::logWrite(LOG_MSG, "Resource",
        "%s memAvail=%uKB paths=%u mounted=%u mapped=%u cachedArchives=%u game=%u/%u blocks=%u level=%u/%u blocks=%u state=%d gamePtr=%p",
        tag ? tag : "",
        (u32)(mem.dwAvailPhys / 1024u),
        searchPaths,
        localArchives,
        fileMappings,
        Archive::getCachedArchiveCount(),
        (u32)gameUsed,
        (u32)gameCap,
        (u32)gameBlocks,
        (u32)levelUsed,
        (u32)levelCap,
        (u32)levelBlocks,
        (s32)s_curState,
        s_curGame);
}
#else
static void logXboxResourceSnapshot(const char*) {}
#endif

#ifdef _XBOX
static bool xboxTimeReached(u32 now, u32 deadline)
{
    return ((s32)(now - deadline)) >= 0;
}

static const TFE_DarkForces::CheatID c_soakTransitionCheats[] =
{
    TFE_DarkForces::CHEAT_LATALAY,
    TFE_DarkForces::CHEAT_LASEWERS,
    TFE_DarkForces::CHEAT_LATESTBASE,
    TFE_DarkForces::CHEAT_LAGROMAS,
    TFE_DarkForces::CHEAT_LADTENTION,
    TFE_DarkForces::CHEAT_LARAMSHED,
    TFE_DarkForces::CHEAT_LAROBOTICS,
    TFE_DarkForces::CHEAT_LANARSHADA,
    TFE_DarkForces::CHEAT_LAJABSHIP,
    TFE_DarkForces::CHEAT_LAIMPCITY,
    TFE_DarkForces::CHEAT_LAFUELSTAT,
    TFE_DarkForces::CHEAT_LAEXECUTOR,
    TFE_DarkForces::CHEAT_LAARC,
    TFE_DarkForces::CHEAT_LASECBASE
};

static const char* c_soakTransitionNames[] =
{
    "TALAY",
    "SEWERS",
    "TESTBASE",
    "GROMAS",
    "DTENTION",
    "RAMSHED",
    "ROBOTICS",
    "NARSHADA",
    "JABSHIP",
    "IMPCITY",
    "FUELSTAT",
    "EXECUTOR",
    "ARC",
    "SECBASE"
};

static void xboxSoakCopyLevelName(char* out, u32 outSize)
{
    if (!out || outSize == 0) return;
    const char* levelName = TFE_DarkForces::agent_getLevelName();
    if (!levelName) levelName = "";
    strncpy(out, levelName, outSize - 1);
    out[outSize - 1] = 0;
}

static const char* xboxSoakCurrentLevelName()
{
    const char* levelName = TFE_DarkForces::agent_getLevelName();
    return levelName ? levelName : "";
}

static bool xboxSoakInActiveMission()
{
    return s_curState == APP_STATE_GAME &&
           s_curGame &&
           TFE_DarkForces::s_missionMode == TFE_DarkForces::MISSION_MODE_MAIN &&
           TFE_Jedi::task_getCount() > 1 &&
           xboxSoakCurrentLevelName()[0] != 0;
}

static void xboxSoakHoldSurvivalFlags()
{
    // This is a harness, not a gameplay input path. Avoid LAIMLAME's toggle behavior.
    TFE_DarkForces::s_invincibility = -2;
}

static void logXboxSoakSummary(const char* phase, u32 now)
{
    u32 searchPaths = 0;
    u32 localArchives = 0;
    u32 fileMappings = 0;
    TFE_Paths::getResourceCounts(&searchPaths, &localArchives, &fileMappings);

    u64 gameUsed = s_gameRegion ? TFE_Memory::region_getMemoryUsed(s_gameRegion) : 0;
    u64 gameCap = s_gameRegion ? TFE_Memory::region_getMemoryCapacity(s_gameRegion) : 0;
    u64 levelUsed = s_levelRegion ? TFE_Memory::region_getMemoryUsed(s_levelRegion) : 0;
    u64 levelCap = s_levelRegion ? TFE_Memory::region_getMemoryCapacity(s_levelRegion) : 0;
    u64 gameBlocks = 0;
    u64 gameBlockSize = 0;
    u64 levelBlocks = 0;
    u64 levelBlockSize = 0;
    if (s_gameRegion) { TFE_Memory::region_getBlockInfo(s_gameRegion, &gameBlocks, &gameBlockSize); }
    if (s_levelRegion) { TFE_Memory::region_getBlockInfo(s_levelRegion, &levelBlocks, &levelBlockSize); }

    MEMORYSTATUS mem;
    memset(&mem, 0, sizeof(mem));
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatus(&mem);

    const u32 elapsedSec = s_soakStartMs ? (now - s_soakStartMs) / 1000u : 0u;
    TFE_System::logWrite(LOG_MSG, "SOAK",
        "summary t=%us phase=%s cycle=%d step=%d frame=%u state=%d missionMode=%d level='%s' levelIndex=%d tasks=%d mem=%uKB paths=%u mounted=%u mapped=%u archives=%u game=%u/%u blocks=%u level=%u/%u blocks=%u gamePtr=%p",
        elapsedSec,
        phase ? phase : "",
        s_soakCycle,
        s_soakStep,
        s_soakFrame,
        (s32)s_curState,
        (s32)TFE_DarkForces::s_missionMode,
        xboxSoakCurrentLevelName(),
        TFE_DarkForces::agent_getLevelIndex(),
        TFE_Jedi::task_getCount(),
        (u32)(mem.dwAvailPhys / 1024u),
        searchPaths,
        localArchives,
        fileMappings,
        Archive::getCachedArchiveCount(),
        (u32)gameUsed,
        (u32)gameCap,
        (u32)gameBlocks,
        (u32)levelUsed,
        (u32)levelCap,
        (u32)levelBlocks,
        s_curGame);
}

static void xboxSoakBeginLevel(u32 now, const char* levelName)
{
    if (s_soakAwaitingLevelChange)
    {
        TFE_System::logWrite(LOG_MSG, "SOAK",
            "level transition complete cycle=%d target='%s' arrived='%s' levelIndex=%d",
            s_soakCycle,
            (s_soakPendingTargetIndex >= 0) ? c_soakTransitionNames[s_soakPendingTargetIndex] : "",
            levelName ? levelName : "",
            TFE_DarkForces::agent_getLevelIndex());
        if (s_soakPendingTargetIndex >= 0)
        {
            s_soakTargetIndex = (s_soakPendingTargetIndex + 1) % (s32)TFE_ARRAYSIZE(c_soakTransitionCheats);
            s_soakPendingTargetIndex = -1;
        }
        s_soakCycle++;
    }

    xboxSoakCopyLevelName(s_soakLevelName, sizeof(s_soakLevelName));
    s_soakMissionReady = true;
    s_soakAwaitingLevelChange = false;
    s_soakLevelReadyMs = now;
    s_soakStep = 0;
    s_soakLastSaveOk = false;
    s_soakNextSummaryMs = now + 60000u;
    s_soakNextTransitionMs = now + 15000u;
    xboxSoakHoldSurvivalFlags();

    TFE_System::logWrite(LOG_MSG, "SOAK",
        "level ready cycle=%d level='%s' levelIndex=%d nextTarget='%s'",
        s_soakCycle,
        s_soakLevelName,
        TFE_DarkForces::agent_getLevelIndex(),
        c_soakTransitionNames[s_soakTargetIndex]);
    logXboxSoakSummary("level-ready", now);
}

static void updateXboxSoak()
{
    if (!s_soakMode) return;

    const u32 now = GetTickCount();
    s_soakFrame++;

    if (!s_soakStartMs)
    {
        s_soakStartMs = now ? now : 1u;
        s_soakNextSummaryMs = now + 60000u;
        s_soakNextTransitionMs = now + 15000u;
        TFE_System::logWrite(LOG_MSG, "SOAK", "begin campaign transition soak");
        logXboxSoakSummary("begin", now);
    }

    if (!xboxSoakInActiveMission())
    {
        if (xboxTimeReached(now, s_soakNextSummaryMs))
        {
            logXboxSoakSummary(s_soakAwaitingLevelChange ? "loading-next" : "waiting-mission", now);
            s_soakNextSummaryMs = now + 60000u;
        }
        return;
    }

    xboxSoakHoldSurvivalFlags();

    const char* currentLevel = xboxSoakCurrentLevelName();
    if (!s_soakMissionReady || strcmp(currentLevel, s_soakLevelName) != 0)
    {
        xboxSoakBeginLevel(now, currentLevel);
        return;
    }

    if (xboxTimeReached(now, s_soakNextSummaryMs))
    {
        logXboxSoakSummary("steady", now);
        s_soakNextSummaryMs = now + 60000u;
    }

    if (!xboxTimeReached(now, s_soakNextTransitionMs))
    {
        return;
    }

    if (s_soakStep == 0)
    {
        TFE_System::logWrite(LOG_MSG, "SOAK", "transition save begin cycle=%d", s_soakCycle);
        s_soakLastSaveOk = TFE_SaveSystem::saveGameQuiet("soak_cycle.tfe", "Soak");
        TFE_System::logWrite(s_soakLastSaveOk ? LOG_MSG : LOG_ERROR, "SOAK",
            "transition save end cycle=%d result=%d", s_soakCycle, s_soakLastSaveOk ? 1 : 0);
        logXboxSoakSummary("after-save", now);
        s_soakStep = s_soakLastSaveOk ? 1 : 0;
        s_soakNextTransitionMs = now + (s_soakLastSaveOk ? 10000u : 30000u);
    }
    else if (s_soakStep == 1)
    {
        TFE_System::logWrite(LOG_MSG, "SOAK", "transition load begin cycle=%d", s_soakCycle);
        const bool loaded = TFE_SaveSystem::loadGame("soak_cycle.tfe");
        TFE_System::logWrite(loaded ? LOG_MSG : LOG_ERROR, "SOAK",
            "transition load end cycle=%d result=%d", s_soakCycle, loaded ? 1 : 0);
        logXboxSoakSummary("after-load", now);
        s_soakStep = loaded ? 2 : 0;
        s_soakNextTransitionMs = now + (loaded ? 15000u : 30000u);
    }
    else
    {
        s32 targetIndex = s_soakPendingTargetIndex;
        if (targetIndex < 0)
        {
            targetIndex = s_soakTargetIndex % (s32)TFE_ARRAYSIZE(c_soakTransitionCheats);
            s_soakPendingTargetIndex = targetIndex;
        }

        TFE_System::logWrite(LOG_MSG, "SOAK",
            "level transition begin cycle=%d from='%s' target='%s' cheat=%d",
            s_soakCycle,
            s_soakLevelName,
            c_soakTransitionNames[targetIndex],
            (s32)c_soakTransitionCheats[targetIndex]);
        logXboxSoakSummary("before-level-change", now);
        TFE_DarkForces::executeCheat(c_soakTransitionCheats[targetIndex]);
        s_soakAwaitingLevelChange = true;
        s_soakStep = 3;
        s_soakNextTransitionMs = now + 120000u;
    }
}
#else
static void updateXboxSoak() {}
#endif

static bool isNativeFrontendState(AppState state)
{
    return state == APP_STATE_MENU ||
           state == APP_STATE_LOAD ||
           state == APP_STATE_MODS ||
           state == APP_STATE_OPTIONS;
}

static bool shouldPresentNativeFrontend(AppState state)
{
    if (!isNativeFrontendState(state))
    {
        s_lastPresentedFrontendState = state;
        s_frontendPresentPhase = 0;
        return true;
    }

    // Menu screens are CPU-composited into a 640x480 texture, then uploaded.
    // Present them at 30 Hz while the main loop remains at 60 Hz so audio and
    // input continue to get serviced between frontend uploads on Xbox hardware.
    if (state != s_lastPresentedFrontendState)
    {
        s_lastPresentedFrontendState = state;
        s_frontendPresentPhase = 0;
        return true;
    }

    s_frontendPresentPhase++;
    if (s_frontendPresentPhase >= 2)
    {
        s_frontendPresentPhase = 0;
        return true;
    }
    return false;
}

static void pumpXboxAudio()
{
    // The audio device owns a pump thread; this remains an opportunistic
    // catch-up path around frontend work.
    TFE_AudioDevice::pump();
}

extern "C" void TFE_XboxReturnToStartMenu()
{
    TFE_System::logWrite(LOG_MSG, "Main", "Return to start menu requested. state=%d game=%p",
        (s32)s_curState, s_curGame);
    s_returnToStartRequested = true;
}

extern "C" bool TFE_XboxSoakAutoAdvanceMissionComplete()
{
    return s_soakMode;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

// On Xbox the source data lives in <root>\DARK\ (mirrors original DF install).
static void setupSourceDataPath()
{
    char darkPath[TFE_MAX_PATH];
    snprintf(darkPath, TFE_MAX_PATH, "%sDARK\\", TFE_Paths::getPath(PATH_PROGRAM));
    TFE_Paths::setPath(PATH_SOURCE_DATA, darkPath);
    TFE_System::logWrite(LOG_MSG, "Paths", "Source Data: \"%s\"", darkPath);
}

static bool validatePath()
{
    if (!TFE_Paths::hasPath(PATH_SOURCE_DATA)) return false;

    char testFile[TFE_MAX_PATH];
    snprintf(testFile, TFE_MAX_PATH, "%sDARK.GOB", TFE_Paths::getPath(PATH_SOURCE_DATA));
    if (!FileUtil::exists(testFile))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "DARK.GOB not found at '%s'", testFile);
        return false;
    }
    if (!GobArchive::validate(testFile, 130))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "DARK.GOB is invalid");
        return false;
    }
    return true;
}

static const char* appStateName(AppState state)
{
    switch (state)
    {
        case APP_STATE_MENU: return "MENU";
        case APP_STATE_LOAD: return "LOAD";
        case APP_STATE_MODS: return "MODS";
        case APP_STATE_OPTIONS: return "OPTIONS";
        case APP_STATE_GAME: return "GAME";
        case APP_STATE_QUIT: return "QUIT";
        case APP_STATE_EXIT_TO_MENU: return "EXIT_TO_MENU";
        default: return "OTHER";
    }
}

static void logMenuMusicState(const char* tag)
{
    if (!s_menuMusicVerbose)
    {
        return;
    }
    ImSoundId foundLower = 0;
    ImSoundId foundUpper = 0;
    s32 marker = -1;
    s32 chunk = -1;
    s32 measure = -1;
    s32 beat = -1;
    s32 tick = -1;
    s32 hook = -1;
    s32 playCount = -1;
    s32 pendingCount = -1;
    s32 firstSoundType = -1;
    ImSoundId firstSound = 0;

    if (s_menuMusicReady)
    {
        foundLower = TFE_Jedi::ImFindMidi("crixmus");
        foundUpper = TFE_Jedi::ImFindMidi("CRIXMUS");
        firstSound = TFE_Jedi::ImGetNextSound(0);
        if (firstSound)
        {
            firstSoundType = TFE_Jedi::ImGetParam(firstSound, soundType);
        }
    }

    if (s_menuMusicReady && s_menuMusicSound)
    {
        marker = TFE_Jedi::ImGetParam(s_menuMusicSound, soundMarker);
        chunk = TFE_Jedi::ImGetParam(s_menuMusicSound, midiChunk);
        measure = TFE_Jedi::ImGetParam(s_menuMusicSound, midiMeasure);
        beat = TFE_Jedi::ImGetParam(s_menuMusicSound, midiBeat);
        tick = TFE_Jedi::ImGetParam(s_menuMusicSound, midiTick);
        hook = TFE_Jedi::ImGetHook(s_menuMusicSound);
        playCount = TFE_Jedi::ImGetParam(s_menuMusicSound, soundPlayCount);
        pendingCount = TFE_Jedi::ImGetParam(s_menuMusicSound, soundPendCount);
    }

    TFE_System::logWrite(LOG_MSG, "MenuMusic",
        "%s state=%s ready=%d playing=%d archive=%p sound=%I64d foundLower=%I64d foundUpper=%I64d first=%I64d firstType=%d marker=%d hook=%d chunk=%d measure=%d beat=%d tick=%d play=%d pending=%d sameFrames=%d",
        tag ? tag : "",
        appStateName(s_curState),
        s_menuMusicReady ? 1 : 0,
        s_menuMusicPlaying ? 1 : 0,
        s_menuMusicArchive,
        s_menuMusicSound,
        foundLower,
        foundUpper,
        firstSound,
        firstSoundType,
        marker,
        hook,
        chunk,
        measure,
        beat,
        tick,
        playCount,
        pendingCount,
        s_menuMusicSamePositionFrames);
}

static void updateMenuMusicHeartbeat()
{
    if (!s_menuMusicVerbose)
    {
        return;
    }
    if (!s_menuMusicPlaying || !s_menuMusicSound)
    {
        s_menuMusicHeartbeatFrame = 0;
        s_menuMusicSamePositionFrames = 0;
        return;
    }

    const s32 chunk = TFE_Jedi::ImGetParam(s_menuMusicSound, midiChunk);
    const s32 measure = TFE_Jedi::ImGetParam(s_menuMusicSound, midiMeasure);
    const s32 beat = TFE_Jedi::ImGetParam(s_menuMusicSound, midiBeat);
    const s32 tick = TFE_Jedi::ImGetParam(s_menuMusicSound, midiTick);
    if (chunk == s_menuMusicLastChunk &&
        measure == s_menuMusicLastMeasure &&
        beat == s_menuMusicLastBeat &&
        tick == s_menuMusicLastTick)
    {
        s_menuMusicSamePositionFrames++;
    }
    else
    {
        s_menuMusicSamePositionFrames = 0;
        s_menuMusicLastChunk = chunk;
        s_menuMusicLastMeasure = measure;
        s_menuMusicLastBeat = beat;
        s_menuMusicLastTick = tick;
    }

    s_menuMusicHeartbeatFrame++;
    if ((s_menuMusicHeartbeatFrame % 300) == 0)
    {
        logMenuMusicState("heartbeat");
        if (s_menuMusicSamePositionFrames >= 300)
        {
            TFE_System::logWrite(LOG_WARNING, "MenuMusic",
                "CRIXMUS MIDI position unchanged for %d frontend frames chunk=%d measure=%d beat=%d tick=%d",
                s_menuMusicSamePositionFrames, chunk, measure, beat, tick);
        }
    }
}

static void stopMenuMusic()
{
    if (!s_menuMusicReady && !s_menuMusicArchive && !s_menuMusicSound && !s_menuMusicPlaying)
    {
        if (s_menuMusicVerbose)
        {
            TFE_System::logWrite(LOG_MSG, "StartMenu", "stopMenuMusic skipped clean");
        }
        return;
    }

    TFE_System::logWrite(LOG_MSG, "StartMenu", "stopping menu music ready=%d playing=%d archive=%p",
        s_menuMusicReady ? 1 : 0, s_menuMusicPlaying ? 1 : 0, s_menuMusicArchive);
    logMenuMusicState("stop-before");
    if (s_menuMusicReady)
    {
        TFE_Jedi::ImStopAllSounds();
        TFE_Jedi::ImUnloadAll();
        TFE_Jedi::ImTerminate();
    }

    if (s_menuMusicArchive)
    {
        TFE_Paths::removeLastArchive();
        Archive::freeArchive(s_menuMusicArchive);
        s_menuMusicArchive = NULL;
    }

    s_menuMusicReady = false;
    s_menuMusicPlaying = false;
    s_menuMusicSound = 0;
    s_menuMusicHeartbeatFrame = 0;
    s_menuMusicSamePositionFrames = 0;
    s_menuMusicLastChunk = -9999;
    s_menuMusicLastMeasure = -9999;
    s_menuMusicLastBeat = -9999;
    s_menuMusicLastTick = -9999;
    logMenuMusicState("stop-after");
}

static void startMenuMusic()
{
    if (s_menuMusicPlaying)
    {
        if (s_menuMusicVerbose)
        {
            TFE_System::logWrite(LOG_MSG, "StartMenu", "startMenuMusic skipped already playing archive=%p", s_menuMusicArchive);
        }
        logMenuMusicState("start-skip-playing");
        return;
    }
    if (s_menuMusicReady || s_menuMusicArchive || s_menuMusicSound)
    {
        TFE_System::logWrite(LOG_WARNING, "StartMenu", "startMenuMusic found stale partial state; cleaning first ready=%d archive=%p sound=%I64d",
            s_menuMusicReady ? 1 : 0, s_menuMusicArchive, s_menuMusicSound);
        stopMenuMusic();
    }

    char gobPath[TFE_MAX_PATH];
    snprintf(gobPath, TFE_MAX_PATH, "%sSOUNDS.GOB", TFE_Paths::getPath(PATH_SOURCE_DATA));
    TFE_System::logWrite(LOG_MSG, "StartMenu", "startMenuMusic begin state=%s gob='%s'",
        appStateName(s_curState), gobPath);
    s_menuMusicArchive = Archive::getArchive(ARCHIVE_GOB, "SOUNDS.GOB", gobPath);
    if (!s_menuMusicArchive)
    {
        TFE_System::logWrite(LOG_ERROR, "StartMenu", "could not open SOUNDS.GOB for menu music");
        return;
    }
    TFE_Paths::addLocalArchive(s_menuMusicArchive);

    if (TFE_Jedi::ImInitialize(s_gameRegion) != imSuccess)
    {
        TFE_System::logWrite(LOG_ERROR, "StartMenu", "iMuse init failed for menu music");
        TFE_Paths::removeLastArchive();
        Archive::freeArchive(s_menuMusicArchive);
        s_menuMusicArchive = NULL;
        return;
    }
    s_menuMusicReady = true;

    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    TFE_MidiPlayer::setVolume(sound->musicVolume * sound->masterVolume);

    ImSoundId song = TFE_Jedi::ImLoadMidi("crixmus");
    if (!song)
    {
        song = TFE_Jedi::ImLoadMidi("CRIXMUS");
    }
    s_menuMusicSound = song;
    TFE_System::logWrite(LOG_MSG, "StartMenu", "CRIXMUS load result sound=%I64d", song);
    if (!song || TFE_Jedi::ImStartSound(song, 64) != imSuccess)
    {
        TFE_System::logWrite(LOG_ERROR, "StartMenu", "failed to start CRIXMUS menu music");
        stopMenuMusic();
        return;
    }

    s_menuMusicPlaying = true;
    TFE_System::logWrite(LOG_MSG, "StartMenu", "playing CRIXMUS");
    logMenuMusicState("start-after");
}

// ---------------------------------------------------------------------------
// Game lifecycle
// ---------------------------------------------------------------------------
static void hideAllFrontendScreens()
{
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "Main", "hiding all frontend screens"); }
    TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetOptionsScreen(false, false, "OPTIONS", 0, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false, 0);
    TFE_RenderBackend::xboxSetCheatScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetPdaOverlay(false, 0, 0);
    TFE_RenderBackend::xboxSetMissionCompleteScreen(false, 0, 0, NULL);
    TFE_RenderBackend::xboxSetWeaponWheel(false, NULL);
}

static void purgeRuntimeGameResources(const char* context)
{
    TFE_System::logWrite(LOG_MSG, "Main", "purgeRuntimeGameResources context='%s'", context ? context : "");

    TFE_Paths::clearSearchPaths();
    TFE_Paths::clearLocalArchives();
    Archive::freeAllArchives();
    TFE_Settings::clearModSettings();
    TFE_ExternalData::getExternalLogics()->actorLogics.clear();
    TFE_ExternalData::clearExternalWeapons();
    TFE_ExternalData::clearExternalProjectiles();
    TFE_ExternalData::clearExternalEffects();
    TFE_ExternalData::clearExternalPickups();
    TFE_Texture::freeAll();
    TFE_Sprite::freeAll();
    TFE_GmidAsset::freeAll();
    TFE_VocAsset::freeAll();
    TFE_VueAsset::freeAll();
    TFE_ColorMap::freeAll();
    TFE_Font::freeAll();
    TFE_Palette::freeAll();
    TFE_Input::enableRelativeMode(false);
    TFE_Input::clearAccumulatedMouseMove();
}

static void prepareGameLaunch(const char* context)
{
    TFE_System::logWrite(LOG_MSG, "Main", "prepareGameLaunch context='%s' state=%d game=%p",
        context ? context : "", (s32)s_curState, s_curGame);
    logXboxResourceSnapshot("prepare-before");
    TFE_Paths::debugLogState("prepare-before");
    stopMenuMusic();
    pumpXboxAudio();
    hideAllFrontendScreens();

    if (s_curGame)
    {
        TFE_System::logWrite(LOG_MSG, "Main", "freeing existing game before launch game=%p", s_curGame);
        freeGame(s_curGame);
        s_curGame = NULL;
        TFE_SaveSystem::setCurrentGame(NULL);
        s_activeModSearchPath[0] = 0;
        TFE_System::logWrite(LOG_MSG, "Main", "existing game freed");
        pumpXboxAudio();
    }

    // Be deliberately hostile to stale mod/menu state. The next runGame()
    // rebuilds search paths and archives from its argv/save payload.
    purgeRuntimeGameResources(context);
    pumpXboxAudio();
    TFE_Paths::debugLogState("prepare-after");
    logXboxResourceSnapshot("prepare-after");
    TFE_System::logWrite(LOG_MSG, "Main", "prepareGameLaunch complete context='%s'", context ? context : "");
}

static void startGame(int argc, const char** argv, const char* extraSearchPath = NULL)
{
    TFE_System::logWrite(LOG_MSG, "Main", "startGame argc=%d extraSearchPath='%s'", argc, extraSearchPath ? extraSearchPath : "");
    for (s32 i = 0; s_frontendVerbose && i < argc; i++)
    {
        TFE_System::logWrite(LOG_MSG, "Main", "startGame argv[%d]='%s'", i, argv && argv[i] ? argv[i] : "");
    }
    prepareGameLaunch("start");
    if (extraSearchPath && extraSearchPath[0])
    {
        TFE_Paths::addAbsoluteSearchPathToHead(extraSearchPath);
        TFE_Paths::debugLogState("start-after-extra-path");
    }

    TFE_Game* gameInfo = TFE_Settings::getGame();
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "Main", "creating game id=%d", gameInfo ? gameInfo->id : -1); }
    if (!gameInfo)
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot create game: no current game settings.");
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }
    s_curGame = createGame(gameInfo->id);

    if (!s_curGame)
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot create game.");
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }
    TFE_SaveSystem::setCurrentGame(s_curGame);
    if (extraSearchPath && extraSearchPath[0])
    {
        strncpy(s_activeModSearchPath, extraSearchPath, TFE_MAX_PATH - 1);
        s_activeModSearchPath[TFE_MAX_PATH - 1] = 0;
    }
    else
    {
        s_activeModSearchPath[0] = 0;
    }

    TFE_System::logWrite(LOG_MSG, "Main", "runGame begin game=%p", s_curGame);
    if (!s_curGame->runGame(argc, argv, NULL))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot run game.");
        freeGame(s_curGame);
        s_curGame = NULL;
        TFE_SaveSystem::setCurrentGame(NULL);
        s_activeModSearchPath[0] = 0;
        purgeRuntimeGameResources("start-failed");
        pumpXboxAudio();
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }

    TFE_Input::enableRelativeMode(true);
    s_curState = APP_STATE_GAME;
    TFE_Paths::debugLogState("start-after-runGame");
    logXboxResourceSnapshot("start-after-runGame");
    TFE_System::logWrite(LOG_MSG, "Main", "Game started. state=%d game=%p", (s32)s_curState, s_curGame);
}

static bool loadGameFromMenu(const char* filename, const char* extraSearchPath = NULL)
{
    if (!filename || !filename[0]) return false;
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "loadGameFromMenu requested file='%s' extraSearchPath='%s'",
        filename, extraSearchPath ? extraSearchPath : "");
    prepareGameLaunch("load");
    if (extraSearchPath && extraSearchPath[0])
    {
        TFE_Paths::addAbsoluteSearchPathToHead(extraSearchPath);
        TFE_Paths::debugLogState("load-after-extra-path");
    }

    TFE_Game* gameInfo = TFE_Settings::getGame();
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "creating game for load id=%d", gameInfo ? gameInfo->id : -1);
    if (!gameInfo)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "Cannot create game for load: no current game settings.");
        s_curState = APP_STATE_CANNOT_RUN;
        return false;
    }
    s_curGame = createGame(gameInfo->id);
    if (!s_curGame)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "Cannot create game for load.");
        s_curState = APP_STATE_CANNOT_RUN;
        return false;
    }
    TFE_SaveSystem::setCurrentGame(s_curGame);
    if (extraSearchPath && extraSearchPath[0])
    {
        strncpy(s_activeModSearchPath, extraSearchPath, TFE_MAX_PATH - 1);
        s_activeModSearchPath[TFE_MAX_PATH - 1] = 0;
    }
    else
    {
        s_activeModSearchPath[0] = 0;
    }

    TFE_System::logWrite(LOG_MSG, "LoadMenu", "loadGame begin game=%p file='%s'", s_curGame, filename);
    const bool loaded = TFE_SaveSystem::loadGame(filename);
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "loadGame returned %d file='%s'", loaded ? 1 : 0, filename);
    if (!loaded)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "load failed '%s'", filename);
        freeGame(s_curGame);
        s_curGame = NULL;
        TFE_SaveSystem::setCurrentGame(NULL);
        s_activeModSearchPath[0] = 0;
        purgeRuntimeGameResources("load-failed");
        pumpXboxAudio();
        s_curState = APP_STATE_MENU;
        TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
        startMenuMusic();
        return false;
    }

    TFE_Input::enableRelativeMode(true);
    s_curState = APP_STATE_GAME;
    TFE_Paths::debugLogState("load-after-loadGame");
    logXboxResourceSnapshot("load-after-loadGame");
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "loaded '%s' state=%d game=%p", filename, (s32)s_curState, s_curGame);
    return true;
}

static void refreshLoadSlots()
{
    memset(s_loadHeaders, 0, sizeof(s_loadHeaders));
    memset(s_loadSlots, 0, sizeof(s_loadSlots));
    memset(s_loadDateDisplay, 0, sizeof(s_loadDateDisplay));
    for (s32 i = 0; i < 6; i++)
    {
        char filename[TFE_MAX_PATH];
        if (i == 0)
            strcpy(filename, TFE_SaveSystem::c_quickSaveName);
        else
            sprintf(filename, "save%03d.tfe", i - 1);

        const bool valid = TFE_SaveSystem::loadGameHeader(filename, &s_loadHeaders[i]);
        if (valid)
        {
            char dow[8], mon[8];
            int day = 0, hour = 0, minute = 0, second = 0, year = 0;
            if (sscanf(s_loadHeaders[i].dateTime, "%7s %7s %d %d:%d:%d %d", dow, mon, &day, &hour, &minute, &second, &year) == 7)
            {
                sprintf(s_loadDateDisplay[i], "%s %02d, %04d %02d:%02d", mon, day, year, hour, minute);
            }
            else
            {
                strncpy(s_loadDateDisplay[i], s_loadHeaders[i].dateTime, 31);
                s_loadDateDisplay[i][31] = 0;
            }
        }
        s_loadSlots[i].valid = valid;
        s_loadSlots[i].autosave = valid && (!strcasecmp(filename, "save000.tfe") || !strcasecmp(s_loadHeaders[i].saveName, "Autosave"));
        s_loadSlots[i].fileName = valid ? s_loadHeaders[i].fileName : filename;
        s_loadSlots[i].saveName = valid ? s_loadHeaders[i].saveName : "";
        s_loadSlots[i].dateTime = valid ? s_loadDateDisplay[i] : "";
        s_loadSlots[i].levelName = valid ? s_loadHeaders[i].levelName : "";
        s_loadSlots[i].levelId = valid ? s_loadHeaders[i].levelId : "";
        s_loadSlots[i].imageData = valid ? s_loadHeaders[i].imageData : NULL;
        if (s_frontendVerbose)
        {
            TFE_System::logWrite(LOG_MSG, "LoadMenu",
                "slot=%d valid=%d file='%s' save='%s' level='%s' levelId='%s' date='%s'",
                i, valid ? 1 : 0, filename,
                valid ? s_loadHeaders[i].saveName : "",
                valid ? s_loadHeaders[i].levelName : "",
                valid ? s_loadHeaders[i].levelId : "",
                valid ? s_loadDateDisplay[i] : "");
        }
    }
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "LoadMenu", "slots refreshed"); }
}

static void copyString(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = 0;
}

static char* trimText(char* text)
{
    if (!text) return text;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
    return text;
}

static bool isAppleDoubleFile(const char* name)
{
    if (!name) return false;
    char fileName[TFE_MAX_PATH];
    FileUtil::getFileNameFromPath(name, fileName, true);
    return fileName[0] == '.' && fileName[1] == '_';
}

static void copyPathLeaf(char* dst, size_t dstSize, const char* path)
{
    if (!dst || dstSize == 0) return;
    dst[0] = 0;
    if (!path || !path[0]) return;

    char trimmed[TFE_MAX_PATH];
    copyString(trimmed, sizeof(trimmed), path);
    size_t len = strlen(trimmed);
    while (len > 0 && (trimmed[len - 1] == '\\' || trimmed[len - 1] == '/'))
    {
        trimmed[--len] = 0;
    }
    FileUtil::getFileNameFromPath(trimmed, dst, false);
    if (!dst[0])
    {
        copyString(dst, dstSize, trimmed);
    }
}

static const char* jsonString(cJSON* object, const char* key)
{
    cJSON* item = object ? cJSON_GetObjectItem(object, key) : NULL;
    return (item && cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

static void makeDf21ThumbPath(char* dst, size_t dstSize, const char* relativePath)
{
    if (!dst || dstSize == 0) return;
    dst[0] = 0;
    if (!relativePath || !relativePath[0]) return;

    char normalized[TFE_MAX_PATH];
    copyString(normalized, sizeof(normalized), relativePath);
    for (char* c = normalized; *c; c++)
    {
        if (*c == '/') *c = '\\';
    }

    snprintf(dst, dstSize, "%sExternalData\\DarkForces\\Mods\\DF21\\%s",
        TFE_Paths::getPath(PATH_PROGRAM), normalized);
}

static void df21SetFromCurrentVersion(XboxDf21CatalogEntry* entry, cJSON* currentVersion)
{
    if (!entry || !currentVersion) return;

    const char* filename = jsonString(currentVersion, "filename");
    if (filename && filename[0])
    {
        FileUtil::getFileNameFromPath(filename, entry->zipBase, false);
    }

    const char* installDir = jsonString(currentVersion, "installDir");
    if (installDir && installDir[0])
    {
        copyPathLeaf(entry->installBase, sizeof(entry->installBase), installDir);
    }

    if (!entry->installBase[0] && entry->zipBase[0])
    {
        copyString(entry->installBase, sizeof(entry->installBase), entry->zipBase);
    }

    const char* version = jsonString(currentVersion, "key");
    if (version && version[0])
    {
        copyString(entry->version, sizeof(entry->version), version);
    }
}

static bool loadDf21Catalog()
{
    if (s_df21CatalogLoaded) return s_df21CatalogCount > 0;
    s_df21CatalogLoaded = true;
    s_df21CatalogCount = 0;
    memset(s_df21Catalog, 0, sizeof(s_df21Catalog));

    char catalogPath[TFE_MAX_PATH];
    snprintf(catalogPath, TFE_MAX_PATH, "%sExternalData\\DarkForces\\Mods\\DF21\\df21_catalog_xbox.json",
        TFE_Paths::getPath(PATH_PROGRAM));

    FileStream file;
    if (!file.open(catalogPath, Stream::MODE_READ))
    {
        TFE_System::logWrite(LOG_WARNING, "DF21", "catalog not found '%s'", catalogPath);
        return false;
    }

    size_t size = file.getSize();
    if (size == 0 || size > 768u * 1024u)
    {
        file.close();
        TFE_System::logWrite(LOG_WARNING, "DF21", "catalog size rejected path='%s' size=%u", catalogPath, (u32)size);
        return false;
    }

    char* data = (char*)malloc(size + 1);
    if (!data)
    {
        file.close();
        TFE_System::logWrite(LOG_ERROR, "DF21", "catalog alloc failed size=%u", (u32)size);
        return false;
    }
    memset(data, 0, size + 1);
    file.readBuffer(data, 1, (u32)size);
    file.close();

    cJSON* root = cJSON_Parse(data);
    free(data);
    if (!root)
    {
        TFE_System::logWrite(LOG_WARNING, "DF21", "catalog parse failed '%s'", catalogPath);
        return false;
    }

    cJSON* levels = cJSON_GetObjectItem(root, "levels");
    if (!levels || !cJSON_IsArray(levels))
    {
        cJSON_Delete(root);
        TFE_System::logWrite(LOG_WARNING, "DF21", "catalog missing levels array");
        return false;
    }

    cJSON* level = levels->child;
    while (level && s_df21CatalogCount < (s32)(sizeof(s_df21Catalog) / sizeof(s_df21Catalog[0])))
    {
        if (cJSON_IsObject(level))
        {
            XboxDf21CatalogEntry* entry = &s_df21Catalog[s_df21CatalogCount];
            memset(entry, 0, sizeof(*entry));
            copyString(entry->slug, sizeof(entry->slug), jsonString(level, "slug"));
            copyString(entry->title, sizeof(entry->title), jsonString(level, "title"));
            copyString(entry->author, sizeof(entry->author), jsonString(level, "author"));
            copyString(entry->description, sizeof(entry->description), jsonString(level, "description"));
            makeDf21ThumbPath(entry->thumbPath, sizeof(entry->thumbPath), jsonString(level, "thumbnail"));
            cJSON* currentVersion = cJSON_GetObjectItem(level, "currentVersion");
            if (currentVersion && cJSON_IsObject(currentVersion))
            {
                df21SetFromCurrentVersion(entry, currentVersion);
            }
            if (!entry->version[0])
            {
                copyString(entry->version, sizeof(entry->version), "DF-21");
            }
            if (entry->slug[0] && entry->title[0] && entry->installBase[0])
            {
                s_df21CatalogCount++;
            }
        }
        level = level->next;
    }

    cJSON_Delete(root);
    TFE_System::logWrite(LOG_MSG, "DF21", "catalog loaded count=%d path='%s'", s_df21CatalogCount, catalogPath);
    return s_df21CatalogCount > 0;
}

static bool loadDf21XbtThumb(const char* path, u32* outImage)
{
    if (!path || !path[0] || !outImage) return false;

    FileStream file;
    if (!file.open(path, Stream::MODE_READ))
    {
        if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "DF21", "thumb missing '%s'", path); }
        return false;
    }

    const u32 expectedPixels = TFE_SaveSystem::SAVE_IMAGE_WIDTH * TFE_SaveSystem::SAVE_IMAGE_HEIGHT;
    const u32 expectedSize = 12 + expectedPixels * 2;
    size_t size = file.getSize();
    if (size != expectedSize)
    {
        file.close();
        TFE_System::logWrite(LOG_WARNING, "DF21", "thumb size rejected path='%s' size=%u expected=%u",
            path, (u32)size, expectedSize);
        return false;
    }

    u8 header[12];
    memset(header, 0, sizeof(header));
    file.readBuffer(header, 1, sizeof(header));
    const u16 version = (u16)(header[4] | (header[5] << 8));
    const u16 width = (u16)(header[6] | (header[7] << 8));
    const u16 height = (u16)(header[8] | (header[9] << 8));
    const u16 format = (u16)(header[10] | (header[11] << 8));
    if (memcmp(header, "XBT1", 4) != 0 || version != 1 ||
        width != TFE_SaveSystem::SAVE_IMAGE_WIDTH ||
        height != TFE_SaveSystem::SAVE_IMAGE_HEIGHT ||
        format != 1)
    {
        file.close();
        TFE_System::logWrite(LOG_WARNING, "DF21", "thumb header rejected path='%s' magic=%c%c%c%c v=%u w=%u h=%u f=%u",
            path, header[0], header[1], header[2], header[3], version, width, height, format);
        return false;
    }

    for (u32 i = 0; i < expectedPixels; i++)
    {
        u8 px[2];
        file.readBuffer(px, 1, 2);
        const u16 c = (u16)(px[0] | (px[1] << 8));
        const u32 r5 = (c >> 11) & 31;
        const u32 g6 = (c >> 5) & 63;
        const u32 b5 = c & 31;
        const u32 r = (r5 << 3) | (r5 >> 2);
        const u32 g = (g6 << 2) | (g6 >> 4);
        const u32 b = (b5 << 3) | (b5 >> 2);
        outImage[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    file.close();
    return true;
}

static const XboxDf21CatalogEntry* findDf21CatalogForMod(const XboxModEntry* mod)
{
    if (!mod || !loadDf21Catalog()) return NULL;

    char folderBase[96];
    copyPathLeaf(folderBase, sizeof(folderBase), mod->path);
    char archiveBase[96];
    FileUtil::getFileNameFromPath(mod->archiveName, archiveBase, false);

    for (s32 i = 0; i < s_df21CatalogCount; i++)
    {
        const XboxDf21CatalogEntry* entry = &s_df21Catalog[i];
        if ((folderBase[0] && !strcasecmp(folderBase, entry->installBase)) ||
            (folderBase[0] && !strcasecmp(folderBase, entry->zipBase)) ||
            (archiveBase[0] && !strcasecmp(archiveBase, entry->slug)) ||
            (archiveBase[0] && !strcasecmp(archiveBase, entry->zipBase)))
        {
            return entry;
        }
    }
    return NULL;
}

static const u32* applyDf21CatalogToMod(XboxModEntry* mod, s32 index)
{
    if (!mod || index < 0 || index >= 12) return NULL;

    const XboxDf21CatalogEntry* entry = findDf21CatalogForMod(mod);
    if (!entry) return NULL;

    copyString(mod->title, sizeof(mod->title), entry->title);
    copyString(mod->author, sizeof(mod->author), entry->author[0] ? entry->author : "-");
    copyString(mod->version, sizeof(mod->version), entry->version[0] ? entry->version : "DF-21");
    copyString(mod->description, sizeof(mod->description), entry->description[0] ? entry->description : "DF-21 custom mission.");
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    if (mod->ui.missionCount == 0) mod->ui.missionCount = 1;

    s_modCatalogThumbValid[index] = false;
    if (!s_modThumbValid[index] && entry->thumbPath[0] && loadDf21XbtThumb(entry->thumbPath, s_modCatalogThumbs[index]))
    {
        s_modCatalogThumbValid[index] = true;
    }

    TFE_System::logWrite(LOG_MSG, "DF21", "matched mod title='%s' slug='%s' install='%s' thumb=%d",
        mod->title, entry->slug, entry->installBase, s_modCatalogThumbValid[index] ? 1 : 0);
    return s_modCatalogThumbValid[index] ? s_modCatalogThumbs[index] : NULL;
}

static void assignModDefaults(XboxModEntry* mod, s32 index, const char* folderPath, const char* archiveName)
{
    memset(mod, 0, sizeof(XboxModEntry));
    mod->ui.valid = true;
    copyString(mod->path, TFE_MAX_PATH, folderPath);
    copyString(mod->archiveName, sizeof(mod->archiveName), archiveName);

    char folderTitle[96];
    copyPathLeaf(folderTitle, sizeof(folderTitle), folderPath);
    char archiveTitle[96];
    FileUtil::getFileNameFromPath(archiveName && archiveName[0] ? archiveName : folderPath, archiveTitle, false);

    char title[96];
    copyString(title, sizeof(title), folderTitle[0] ? folderTitle : archiveTitle);
    if (!title[0]) strcpy(title, "Installed Mod");
    copyString(mod->title, sizeof(mod->title), title);
    copyString(mod->saveKey, sizeof(mod->saveKey), folderTitle[0] ? folderTitle : archiveTitle);
    copyString(mod->author, sizeof(mod->author), "-");
    copyString(mod->version, sizeof(mod->version), "-");
    copyString(mod->description, sizeof(mod->description), "No description provided.");
    mod->levelName[0] = 0;
    TFE_SaveSystem::getQuickSaveFilenameForMod(mod->saveKey, mod->quickSaveName, TFE_MAX_PATH);
    mod->hasQuickSave = TFE_SaveSystem::loadGameHeaderLite(mod->quickSaveName, &s_loadHeaders[0]);
    if (index >= 0 && index < 12 && mod->hasQuickSave)
    {
        // Keep mod enumeration light on Xbox. Save thumbnails are small now,
        // but still load the image only when a panel actually needs it.
        s_modThumbValid[index] = false;
    }
    if (s_frontendVerbose)
    {
        TFE_System::logWrite(LOG_MSG, "ModMenu",
            "defaults index=%d title='%s' archive='%s' quickSave='%s' hasQuickSave=%d",
            index, mod->title, mod->archiveName, mod->quickSaveName, mod->hasQuickSave ? 1 : 0);
    }
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    mod->ui.missionCount = archiveName && archiveName[0] ? 1 : 0;
    mod->ui.hasQuickSave = mod->hasQuickSave;
    mod->ui.imageData = (index >= 0 && index < 12 && s_modThumbValid[index]) ? s_modThumbs[index] : NULL;
}

static void parseModManifestLine(XboxModEntry* mod, char* line)
{
    char* sep = strchr(line, '=');
    if (!sep) sep = strchr(line, ':');
    if (!sep) return;
    *sep = 0;
    char* key = trimText(line);
    char* value = trimText(sep + 1);
    if (!key[0] || !value[0]) return;

    if (!strcasecmp(key, "title") || !strcasecmp(key, "name"))
        copyString(mod->title, sizeof(mod->title), value);
    else if (!strcasecmp(key, "author"))
        copyString(mod->author, sizeof(mod->author), value);
    else if (!strcasecmp(key, "version"))
        copyString(mod->version, sizeof(mod->version), value);
    else if (!strcasecmp(key, "description") || !strcasecmp(key, "desc"))
        copyString(mod->description, sizeof(mod->description), value);
    else if (!strcasecmp(key, "missions"))
        mod->ui.missionCount = atoi(value);
    else if (!strcasecmp(key, "level") || !strcasecmp(key, "startlevel"))
        copyString(mod->levelName, sizeof(mod->levelName), value);
}

static void parseModManifestBuffer(XboxModEntry* mod, char* buffer)
{
    char* line = buffer;
    while (line && *line)
    {
        char* next = strchr(line, '\n');
        if (next) *next++ = 0;
        parseModManifestLine(mod, line);
        line = next;
    }

    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    mod->ui.hasQuickSave = mod->hasQuickSave;
}

static bool readModManifestFile(XboxModEntry* mod, const char* filename)
{
    FileStream file;
    if (!file.open(filename, Stream::MODE_READ))
    {
        if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "metadata not found '%s'", filename ? filename : ""); }
        return false;
    }

    size_t size = file.getSize();
    if (size > 2047) size = 2047;
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    file.readBuffer(buffer, 1, (u32)size);
    file.close();
    parseModManifestBuffer(mod, buffer);
    if (s_frontendVerbose)
    {
        TFE_System::logWrite(LOG_MSG, "ModMenu", "metadata loaded file='%s' bytes=%u title='%s' level='%s'",
            filename, (u32)size, mod->title, mod->levelName);
    }
    return true;
}

static bool readModManifestFromZip(XboxModEntry* mod)
{
    const size_t archiveLen = strlen(mod->archiveName);
    if (archiveLen < 4) return false;
    const char* ext3 = archiveLen >= 3 ? &mod->archiveName[archiveLen - 3] : "";
    if (strcasecmp(ext3, "zip") != 0)
    {
        return false;
    }

    char archivePath[TFE_MAX_PATH];
    snprintf(archivePath, TFE_MAX_PATH, "%s%s", mod->path, mod->archiveName);
    ZipArchive zip;
    if (!zip.open(archivePath))
    {
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "could not open zip for metadata '%s'", archivePath);
        return false;
    }

    const u32 count = zip.getFileCount();
    TFE_System::logWrite(LOG_MSG, "ModMenu", "scanning zip metadata archive='%s' fileCount=%u", archivePath, count);
    for (u32 i = 0; i < count; i++)
    {
        const char* zipName = zip.getFileName(i);
        if (!zipName) { continue; }
        char fileName[TFE_MAX_PATH];
        FileUtil::getFileNameFromPath(zipName, fileName, true);
        if (strcasecmp(fileName, "metadata.txt") == 0 ||
            strcasecmp(fileName, "mod.txt") == 0 ||
            strcasecmp(fileName, "manifest.txt") == 0 ||
            strcasecmp(fileName, "mod.ini") == 0)
        {
            u32 bufferLen = (u32)zip.getFileLength(i);
            if (bufferLen > 2047) bufferLen = 2047;
            char buffer[2048];
            memset(buffer, 0, sizeof(buffer));
            zip.openFile(i);
            zip.readFile(buffer, bufferLen);
            zip.closeFile();
            parseModManifestBuffer(mod, buffer);
            zip.close();
            TFE_System::logWrite(LOG_MSG, "ModMenu", "metadata loaded from zip file='%s' bytes=%u title='%s' level='%s'",
                fileName, bufferLen, mod->title, mod->levelName);
            return true;
        }
    }
    zip.close();
    TFE_System::logWrite(LOG_MSG, "ModMenu", "no metadata inside zip archive='%s'", archivePath);
    return false;
}

static bool readFirstLevelFromGobBuffer(const u8* data, u32 size, char* outLevel, size_t outSize)
{
    if (!data || !size || !outLevel || outSize == 0) return false;

    GobMemoryArchive gob;
    if (!gob.openView(data, size)) return false;

    const u32 count = gob.getFileCount();
    for (u32 i = 0; i < count; i++)
    {
        const char* name = gob.getFileName(i);
        if (!name) continue;
        const size_t len = strlen(name);
        if (len < 5) continue;
        if (strcasecmp(&name[len - 3], "lev") == 0)
        {
            char levelName[32];
            FileUtil::getFileNameFromPath(name, levelName, false);
            copyString(outLevel, outSize, levelName);
            gob.close();
            return outLevel[0] != 0;
        }
    }

    gob.close();
    return false;
}

static bool readFirstLevelFromZip(XboxModEntry* mod)
{
    if (mod->levelName[0]) return true;

    char archivePath[TFE_MAX_PATH];
    snprintf(archivePath, TFE_MAX_PATH, "%s%s", mod->path, mod->archiveName);
    ZipArchive zip;
    if (!zip.open(archivePath))
    {
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "could not open zip for level scan '%s'", archivePath);
        return false;
    }

    const u32 count = zip.getFileCount();
    TFE_System::logWrite(LOG_MSG, "ModMenu", "scanning zip levels archive='%s' fileCount=%u", archivePath, count);
    for (u32 i = 0; i < count; i++)
    {
        const char* name = zip.getFileName(i);
        if (!name) continue;
        const size_t len = strlen(name);
        if (len < 5 || strcasecmp(&name[len - 3], "gob") != 0) continue;

        char gobFileName[TFE_MAX_PATH];
        FileUtil::getFileNameFromPath(name, gobFileName, true);
        if (gobFileName[0] == '.' && gobFileName[1] == '_') continue;

        u32 bufferLen = (u32)zip.getFileLength(i);
        if (bufferLen == 0 || bufferLen > 16u * 1024u * 1024u)
        {
            TFE_System::logWrite(LOG_WARNING, "ModMenu", "skipping nested gob scan '%s' size=%u", name, bufferLen);
            continue;
        }
        u8* buffer = (u8*)malloc(bufferLen);
        if (!buffer)
        {
            TFE_System::logWrite(LOG_ERROR, "ModMenu", "could not allocate %u bytes for gob scan '%s'", bufferLen, name);
            zip.close();
            return false;
        }

        if (!zip.openFile(i))
        {
            free(buffer);
            TFE_System::logWrite(LOG_WARNING, "ModMenu", "could not open nested gob '%s' for scan", name);
            continue;
        }
        const size_t readLen = zip.readFile(buffer, bufferLen);
        zip.closeFile();
        if (readLen != bufferLen)
        {
            free(buffer);
            TFE_System::logWrite(LOG_WARNING, "ModMenu", "nested gob short read '%s' read=%u expected=%u", name, (u32)readLen, bufferLen);
            continue;
        }
        const bool found = readFirstLevelFromGobBuffer(buffer, bufferLen, mod->levelName, sizeof(mod->levelName));
        free(buffer);
        zip.close();
        TFE_System::logWrite(found ? LOG_MSG : LOG_WARNING, "ModMenu", "zip level scan archive='%s' level='%s'", mod->archiveName, mod->levelName);
        return found;
    }

    zip.close();
    return false;
}

static bool readFirstLevelFromGobFile(XboxModEntry* mod)
{
    if (mod->levelName[0]) return true;
    if (!mod->archiveName[0]) return false;

    char archivePath[TFE_MAX_PATH];
    snprintf(archivePath, TFE_MAX_PATH, "%s%s", mod->path, mod->archiveName);
    GobArchive archive;
    if (!archive.open(archivePath))
    {
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "could not open gob for level scan '%s'", archivePath);
        return false;
    }

    const u32 count = archive.getFileCount();
    for (u32 i = 0; i < count; i++)
    {
        const char* name = archive.getFileName(i);
        if (!name || isAppleDoubleFile(name)) continue;
        const size_t len = strlen(name);
        if (len < 5) continue;
        if (strcasecmp(&name[len - 3], "lev") == 0)
        {
            char levelName[32];
            FileUtil::getFileNameFromPath(name, levelName, false);
            copyString(mod->levelName, sizeof(mod->levelName), levelName);
            archive.close();
            TFE_System::logWrite(LOG_MSG, "ModMenu", "gob level scan archive='%s' level='%s'",
                mod->archiveName, mod->levelName);
            return mod->levelName[0] != 0;
        }
    }

    archive.close();
    TFE_System::logWrite(LOG_WARNING, "ModMenu", "no level found in gob '%s'", archivePath);
    return false;
}

static void readModManifest(XboxModEntry* mod)
{
    char baseName[96];
    FileUtil::getFileNameFromPath(mod->archiveName, baseName, false);

    bool metadataLoaded = false;
    char filename[TFE_MAX_PATH];
    if (mod->saveKey[0])
    {
        snprintf(filename, TFE_MAX_PATH, "%s%s_metadata.txt", mod->path, mod->saveKey);
        metadataLoaded = readModManifestFile(mod, filename);
    }
    if (baseName[0])
    {
        snprintf(filename, TFE_MAX_PATH, "%s%s_metadata.txt", mod->path, baseName);
        if (!metadataLoaded) metadataLoaded = readModManifestFile(mod, filename);
    }

    static const char* names[] = { "metadata.txt", "mod.txt", "manifest.txt", "mod.ini" };
    for (s32 i = 0; !metadataLoaded && i < 4; i++)
    {
        snprintf(filename, TFE_MAX_PATH, "%s%s", mod->path, names[i]);
        metadataLoaded = readModManifestFile(mod, filename);
    }

#ifdef _XBOX
    if (!metadataLoaded)
    {
        if (s_frontendVerbose)
        {
            TFE_System::logWrite(LOG_MSG, "ModMenu",
                "zip metadata scan skipped on Xbox menu path archive='%s'", mod->archiveName);
        }
    }
    if (mod->ui.missionCount == 0)
    {
        mod->ui.missionCount = 1;
    }
#else
    if (!metadataLoaded)
    {
        readModManifestFromZip(mod);
    }
    readFirstLevelFromZip(mod);
#endif

    FileList levels;
    FileUtil::readDirectory(mod->path, "lev", levels);
    if (mod->ui.missionCount == 0 && levels.size() > 0)
    {
        mod->ui.missionCount = (s32)levels.size();
    }
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
}

static bool findFirstModArchive(const char* dir, char* outName, size_t outSize)
{
    FileList files;
#ifdef _XBOX
    FileUtil::readDirectory(dir, "gob", files);
#else
    FileUtil::readDirectory(dir, "zip", files);
#endif
    for (size_t i = 0; i < files.size(); i++)
    {
        if (isAppleDoubleFile(files[i].c_str())) continue;
        copyString(outName, outSize, files[i].c_str());
        return true;
    }
    outName[0] = 0;
    return false;
}

static void updateModQuickSaveState(XboxModEntry* mod, s32 index, bool loadImage)
{
    if (!mod) return;

    mod->hasQuickSave = false;
    if (index >= 0 && index < 12)
    {
        s_modThumbValid[index] = false;
    }

    if (!mod->quickSaveName[0]) return;

    TFE_SaveSystem::SaveHeader header;
    memset(&header, 0, sizeof(header));
    const bool valid = loadImage
        ? TFE_SaveSystem::loadGameHeader(mod->quickSaveName, &header)
        : TFE_SaveSystem::loadGameHeaderLite(mod->quickSaveName, &header);

    mod->hasQuickSave = valid;
    if (valid && loadImage && index >= 0 && index < 12)
    {
        memcpy(s_modThumbs[index], header.imageData, sizeof(s_modThumbs[index]));
        s_modThumbValid[index] = true;
    }
}

static void setModUiImage(XboxModEntry* mod, s32 index, const u32* fallbackImage)
{
    if (!mod) return;
    mod->ui.hasQuickSave = mod->hasQuickSave;
    mod->ui.imageData = (index >= 0 && index < 12 && s_modThumbValid[index]) ? s_modThumbs[index] : fallbackImage;
}

static void addModEntry(const char* dir, const char* archiveName)
{
    if (s_modCount >= 12 || !archiveName || !archiveName[0]) return;
    XboxModEntry* mod = &s_modEntries[s_modCount];
    if (s_frontendVerbose)
    {
        TFE_System::logWrite(LOG_MSG, "ModMenu", "addModEntry index=%d dir='%s' archive='%s'",
            s_modCount, dir ? dir : "", archiveName);
    }
    assignModDefaults(mod, s_modCount, dir, archiveName);
    copyString(mod->saveKey, sizeof(mod->saveKey), archiveName);
    TFE_SaveSystem::getQuickSaveFilenameForMod(mod->saveKey, mod->quickSaveName, TFE_MAX_PATH);
    updateModQuickSaveState(mod, s_modCount, true);
    readModManifest(mod);
    const u32* catalogImage = applyDf21CatalogToMod(mod, s_modCount);
    setModUiImage(mod, s_modCount, catalogImage);
    s_modUi[s_modCount] = mod->ui;
    TFE_System::logWrite(LOG_MSG, "ModMenu",
        "mod[%d] title='%s' archive='%s' saveKey='%s' quickSave='%s' hasQuickSave=%d imageSource='%s'",
        s_modCount, mod->title, mod->archiveName, mod->saveKey, mod->quickSaveName,
        mod->hasQuickSave ? 1 : 0, s_modThumbValid[s_modCount] ? "quicksave" : (catalogImage ? "df21" : "none"));
    s_modCount++;
}

static void addRemasterExtrasEntry()
{
    if (s_modCount >= 12) return;

    char extrasPath[TFE_MAX_PATH];
    snprintf(extrasPath, TFE_MAX_PATH, "%sextras.gob", TFE_Paths::getPath(PATH_SOURCE_DATA));
    if (!FileUtil::exists(extrasPath)) return;

    XboxModEntry* mod = &s_modEntries[s_modCount];
    assignModDefaults(mod, s_modCount, TFE_Paths::getPath(PATH_SOURCE_DATA), "extras.gob");
    copyString(mod->saveKey, sizeof(mod->saveKey), "extras.gob");
    TFE_SaveSystem::getQuickSaveFilenameForMod(mod->saveKey, mod->quickSaveName, TFE_MAX_PATH);
    updateModQuickSaveState(mod, s_modCount, true);
    copyString(mod->title, sizeof(mod->title), "Avenger Prototype");
    copyString(mod->author, sizeof(mod->author), "LucasArts");
    copyString(mod->version, sizeof(mod->version), "Remaster Extras");
    copyString(mod->description, sizeof(mod->description),
        "Original Avenger prototype level from Dark Forces Remaster extras.gob.");
    copyString(mod->levelName, sizeof(mod->levelName), "AVENGER");
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    mod->ui.missionCount = 1;
    setModUiImage(mod, s_modCount, s_xboxAvengerModThumb);
    s_modUi[s_modCount] = mod->ui;
    s_modCount++;
    TFE_System::logWrite(LOG_MSG, "ModMenu", "added Remaster extras map '%s' hasQuickSave=%d imageSource='%s'",
        extrasPath, mod->hasQuickSave ? 1 : 0, s_modThumbValid[s_modCount - 1] ? "quicksave" : "baked");
}

static void refreshModSlots()
{
    TFE_System::logWrite(LOG_MSG, "ModMenu", "refresh begin previousCount=%d", s_modCount);
    memset(s_modEntries, 0, sizeof(s_modEntries));
    memset(s_modUi, 0, sizeof(s_modUi));
    memset(s_modThumbs, 0, sizeof(s_modThumbs));
    memset(s_modThumbValid, 0, sizeof(s_modThumbValid));
    memset(s_modCatalogThumbs, 0, sizeof(s_modCatalogThumbs));
    memset(s_modCatalogThumbValid, 0, sizeof(s_modCatalogThumbValid));
    s_modCount = 0;

    addRemasterExtrasEntry();

    char modsRoot[TFE_MAX_PATH];
    snprintf(modsRoot, TFE_MAX_PATH, "%sMods\\", TFE_Paths::getPath(PATH_PROGRAM));
    FileUtil::makeDirectory(modsRoot);

    FileList dirs;
    FileUtil::readSubdirectories(modsRoot, dirs);
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "modsRoot='%s' subdirCount=%u", modsRoot, (u32)dirs.size()); }
    for (size_t i = 0; i < dirs.size() && s_modCount < 12; i++)
    {
        char archiveName[96];
        if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "scan subdir[%u]='%s'", (u32)i, dirs[i].c_str()); }
        if (findFirstModArchive(dirs[i].c_str(), archiveName, sizeof(archiveName)))
        {
            addModEntry(dirs[i].c_str(), archiveName);
        }
        else
        {
            if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "no gob in subdir '%s'", dirs[i].c_str()); }
        }
    }

#ifndef _XBOX
    FileList files;
    FileUtil::readDirectory(modsRoot, "zip", files);
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "modsRoot zipCount=%u", (u32)files.size()); }
    for (size_t f = 0; f < files.size() && s_modCount < 12; f++)
    {
        if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "scan zip[%u]='%s'", (u32)f, files[f].c_str()); }
        addModEntry(modsRoot, files[f].c_str());
    }
#else
    FileList zipFiles;
    FileUtil::readDirectory(modsRoot, "zip", zipFiles);
    if (zipFiles.size() > 0)
    {
        TFE_System::logWrite(LOG_WARNING, "ModMenu",
            "ignored %u root zip mod(s); extract each zip to Mods\\<modname>\\ with its GOB inside",
            (u32)zipFiles.size());
    }
#endif

    if (s_modMenuSelection >= s_modCount) s_modMenuSelection = s_modCount > 0 ? s_modCount - 1 : 0;
    TFE_System::logWrite(LOG_MSG, "ModMenu", "refresh complete count=%d", s_modCount);
}

static void startMenuMove(s32 delta)
{
    s_startMenuSelection += delta;
    if (s_startMenuSelection < 0) s_startMenuSelection = 3;
    if (s_startMenuSelection > 3) s_startMenuSelection = 0;
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "StartMenu", "selection=%d", s_startMenuSelection); }
}

static s32 optionPercent(float value)
{
    s32 pct = (s32)(value * 100.0f + 0.5f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static s32 clampS32(s32 value, s32 minValue, s32 maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static float clampF32(float value, float minValue, float maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static const char* xboxVideoModeName(s32 mode)
{
    switch (mode)
    {
        case XBOX_VIDEO_4X3: return "4:3";
        case XBOX_VIDEO_480P_WIDE: return "480P WIDE";
        case XBOX_VIDEO_720P_WIDE: return "720P WIDE";
        default: return "AUTO";
    }
}

static s32 xboxParseVideoModeValue(const char* value)
{
    if (!value || !value[0]) return XBOX_VIDEO_AUTO;
    if (strcasecmp(value, "auto") == 0) return XBOX_VIDEO_AUTO;
    if (strcasecmp(value, "4:3") == 0 || strcasecmp(value, "4x3") == 0) return XBOX_VIDEO_4X3;
    if (strcasecmp(value, "480p-wide") == 0 || strcasecmp(value, "480p wide") == 0) return XBOX_VIDEO_480P_WIDE;
    if (strcasecmp(value, "720p-wide") == 0 || strcasecmp(value, "720p wide") == 0) return XBOX_VIDEO_720P_WIDE;
    return atoi(value);
}

static s32 xboxForcedVideoModeFromMarkers()
{
    if (xboxAbsoluteFileExists("D:\\tfe_force_720p.txt") ||
        xboxAbsoluteFileExists("tfe_force_720p.txt"))
    {
        return XBOX_VIDEO_720P_WIDE;
    }
    if (xboxAbsoluteFileExists("D:\\tfe_force_480p.txt") ||
        xboxAbsoluteFileExists("tfe_force_480p.txt"))
    {
        return XBOX_VIDEO_480P_WIDE;
    }
    return -1;
}

static void xboxRuntimeSettingsPath(char* path, size_t pathSize)
{
    if (path && pathSize) path[0] = 0;
    if (TFE_SaveSystem::xboxGetSaveDirectory(Game_Dark_Forces, path, (u32)pathSize))
    {
        size_t len = strlen(path);
        if (len < pathSize)
        {
            snprintf(path + len, pathSize - len, "xbox_settings.cfg");
            return;
        }
    }
}

static void applyXboxVideoSettings()
{
    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    system->xboxSafeZoneWidthPercent = clampS32(system->xboxSafeZoneWidthPercent, 80, 100);
    system->xboxSafeZoneHeightPercent = clampS32(system->xboxSafeZoneHeightPercent, 80, 100);
    system->xboxSafeZonePercent = (system->xboxSafeZoneWidthPercent + system->xboxSafeZoneHeightPercent) / 2;
    system->xboxSafeZoneOffsetX = clampS32(system->xboxSafeZoneOffsetX, -40, 40);
    system->xboxSafeZoneOffsetY = clampS32(system->xboxSafeZoneOffsetY, -30, 30);
    system->xboxVideoMode = clampS32(system->xboxVideoMode, XBOX_VIDEO_AUTO, XBOX_VIDEO_COUNT - 1);
    TFE_RenderBackend::xboxSetSafeZone(system->xboxSafeZoneWidthPercent,
        system->xboxSafeZoneHeightPercent,
        system->xboxSafeZoneOffsetX, system->xboxSafeZoneOffsetY);
}

static void applyXboxControlSettings()
{
    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    system->xboxLookSensitivityX = clampF32(system->xboxLookSensitivityX, 0.25f, 2.5f);
    system->xboxLookSensitivityY = clampF32(system->xboxLookSensitivityY, 0.25f, 2.5f);
    system->xboxRightStickDeadzone = clampF32(system->xboxRightStickDeadzone, 0.0f, 0.30f);
    system->xboxLookSensitivity = (system->xboxLookSensitivityX + system->xboxLookSensitivityY) * 0.5f;
    system->xboxStickDeadzone = system->xboxRightStickDeadzone;
    TFE_InputXbox::setLookSensitivityX(system->xboxLookSensitivityX);
    TFE_InputXbox::setLookSensitivityY(system->xboxLookSensitivityY);
    TFE_InputXbox::setRightStickDeadzone(system->xboxRightStickDeadzone);
}

static void xboxApplyRuntimeSettings()
{
    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    sound->masterVolume = clampF32(sound->masterVolume, 0.0f, 1.0f);
    sound->soundFxVolume = clampF32(sound->soundFxVolume, 0.0f, 1.0f);
    sound->musicVolume = clampF32(sound->musicVolume, 0.0f, 1.0f);
    sound->cutsceneSoundFxVolume = clampF32(sound->cutsceneSoundFxVolume, 0.0f, 1.0f);
    sound->cutsceneMusicVolume = clampF32(sound->cutsceneMusicVolume, 0.0f, 1.0f);
    applyXboxControlSettings();
    applyXboxVideoSettings();
}

static void xboxParseRuntimeSetting(char* line)
{
    if (!line) return;
    char* eq = strchr(line, '=');
    if (!eq) return;
    *eq++ = 0;
    char* key = line;
    char* value = eq;
    while (*key == ' ' || *key == '\t') key++;
    while (*value == ' ' || *value == '\t') value++;

    char* keyEnd = key + strlen(key);
    while (keyEnd > key && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) *--keyEnd = 0;
    char* valueEnd = value + strlen(value);
    while (valueEnd > value && (valueEnd[-1] == '\r' || valueEnd[-1] == '\n' || valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) *--valueEnd = 0;

    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    if (strcasecmp(key, "xboxLookSensitivity") == 0)
    {
        const float v = (float)atof(value);
        system->xboxLookSensitivityX = v;
        system->xboxLookSensitivityY = v;
    }
    else if (strcasecmp(key, "xboxLookSensitivityX") == 0) system->xboxLookSensitivityX = (float)atof(value);
    else if (strcasecmp(key, "xboxLookSensitivityY") == 0) system->xboxLookSensitivityY = (float)atof(value);
    else if (strcasecmp(key, "xboxLookSensitivityXPct") == 0) system->xboxLookSensitivityX = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "xboxLookSensitivityYPct") == 0) system->xboxLookSensitivityY = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "xboxStickDeadzone") == 0) system->xboxRightStickDeadzone = (float)atof(value);
    else if (strcasecmp(key, "xboxRightStickDeadzone") == 0) system->xboxRightStickDeadzone = (float)atof(value);
    else if (strcasecmp(key, "xboxRightStickDeadzonePct") == 0) system->xboxRightStickDeadzone = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "xboxSafeZonePercent") == 0)
    {
        system->xboxSafeZonePercent = atoi(value);
        system->xboxSafeZoneWidthPercent = system->xboxSafeZonePercent;
        system->xboxSafeZoneHeightPercent = system->xboxSafeZonePercent;
    }
    else if (strcasecmp(key, "xboxSafeZoneWidthPercent") == 0) system->xboxSafeZoneWidthPercent = atoi(value);
    else if (strcasecmp(key, "xboxSafeZoneHeightPercent") == 0) system->xboxSafeZoneHeightPercent = atoi(value);
    else if (strcasecmp(key, "xboxSafeZoneOffsetX") == 0) system->xboxSafeZoneOffsetX = atoi(value);
    else if (strcasecmp(key, "xboxSafeZoneOffsetY") == 0) system->xboxSafeZoneOffsetY = atoi(value);
    else if (strcasecmp(key, "xboxVideoMode") == 0) system->xboxVideoMode = xboxParseVideoModeValue(value);
    else if (strcasecmp(key, "masterVolume") == 0) sound->masterVolume = (float)atof(value);
    else if (strcasecmp(key, "soundFxVolume") == 0) sound->soundFxVolume = (float)atof(value);
    else if (strcasecmp(key, "musicVolume") == 0) sound->musicVolume = (float)atof(value);
    else if (strcasecmp(key, "cutsceneSoundFxVolume") == 0) sound->cutsceneSoundFxVolume = (float)atof(value);
    else if (strcasecmp(key, "cutsceneMusicVolume") == 0) sound->cutsceneMusicVolume = (float)atof(value);
    else if (strcasecmp(key, "masterVolumePct") == 0) sound->masterVolume = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "soundFxVolumePct") == 0) sound->soundFxVolume = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "musicVolumePct") == 0) sound->musicVolume = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "cutsceneSoundFxVolumePct") == 0) sound->cutsceneSoundFxVolume = (float)atoi(value) / 100.0f;
    else if (strcasecmp(key, "cutsceneMusicVolumePct") == 0) sound->cutsceneMusicVolume = (float)atoi(value) / 100.0f;
}

static bool xboxLoadRuntimeSettings()
{
    char path[TFE_MAX_PATH];
    xboxRuntimeSettingsPath(path, sizeof(path));

    FileStream file;
    if (!file.open(path, Stream::MODE_READ))
    {
        TFE_System::logWrite(LOG_MSG, "Settings", "Xbox runtime settings not found: '%s'", path);
        xboxApplyRuntimeSettings();
        return false;
    }

    size_t size = file.getSize();
    if (size > 4095) size = 4095;
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    file.readBuffer(buffer, 1, (u32)size);
    file.close();

    char* line = buffer;
    while (line && *line)
    {
        char* next = strchr(line, '\n');
        if (next) *next++ = 0;
        xboxParseRuntimeSetting(line);
        line = next;
    }

    xboxApplyRuntimeSettings();
    TFE_System::logWrite(LOG_MSG, "Settings", "Xbox runtime settings loaded: '%s'", path);
    return true;
}

static bool xboxSaveRuntimeSettings()
{
    char path[TFE_MAX_PATH];
    xboxRuntimeSettingsPath(path, sizeof(path));
    FileStream file;
    if (!file.open(path, Stream::MODE_WRITE))
    {
        TFE_System::logWrite(LOG_WARNING, "Settings", "Cannot write Xbox runtime settings: '%s'", path);
        return false;
    }

    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    file.writeString("# TheForceEngineXbox runtime settings\r\n");
    file.writeString("xboxLookSensitivityXPct=%d\r\n", (s32)(system->xboxLookSensitivityX * 100.0f + 0.5f));
    file.writeString("xboxLookSensitivityYPct=%d\r\n", (s32)(system->xboxLookSensitivityY * 100.0f + 0.5f));
    file.writeString("xboxRightStickDeadzonePct=%d\r\n", (s32)(system->xboxRightStickDeadzone * 100.0f + 0.5f));
    file.writeString("xboxSafeZoneWidthPercent=%d\r\n", system->xboxSafeZoneWidthPercent);
    file.writeString("xboxSafeZoneHeightPercent=%d\r\n", system->xboxSafeZoneHeightPercent);
    file.writeString("xboxSafeZoneOffsetX=%d\r\n", system->xboxSafeZoneOffsetX);
    file.writeString("xboxSafeZoneOffsetY=%d\r\n", system->xboxSafeZoneOffsetY);
    file.writeString("xboxVideoMode=%d\r\n", system->xboxVideoMode);
    file.writeString("masterVolumePct=%d\r\n", optionPercent(sound->masterVolume));
    file.writeString("soundFxVolumePct=%d\r\n", optionPercent(sound->soundFxVolume));
    file.writeString("musicVolumePct=%d\r\n", optionPercent(sound->musicVolume));
    file.writeString("cutsceneSoundFxVolumePct=%d\r\n", optionPercent(sound->cutsceneSoundFxVolume));
    file.writeString("cutsceneMusicVolumePct=%d\r\n", optionPercent(sound->cutsceneMusicVolume));
    file.close();
    TFE_System::logWrite(LOG_MSG, "Settings", "Xbox runtime settings saved: '%s'", path);
    return true;
}

static void xboxApplyVideoModeToSettings()
{
    TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
    TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
    TFE_Settings_System* system = TFE_Settings::getSystemSettings();

    DWORD avPack = XGetAVPack();
    DWORD videoFlags = XGetVideoFlags();
    const bool hasHdtv = (avPack == XC_AV_PACK_HDTV);
    const bool dashWidescreen = (videoFlags & XC_VIDEO_FLAGS_WIDESCREEN) != 0;
    const bool can480p = hasHdtv && ((videoFlags & XC_VIDEO_FLAGS_HDTV_480p) != 0);
    const bool can720p = hasHdtv && ((videoFlags & XC_VIDEO_FLAGS_HDTV_720p) != 0);

    s32 requestedMode = clampS32(system->xboxVideoMode, XBOX_VIDEO_AUTO, XBOX_VIDEO_COUNT - 1);
    const s32 markerMode = xboxForcedVideoModeFromMarkers();
    if (markerMode >= 0) requestedMode = markerMode;

    s32 selectedMode = XBOX_VIDEO_4X3;
    if (requestedMode == XBOX_VIDEO_AUTO)
    {
        if (can480p && dashWidescreen) selectedMode = XBOX_VIDEO_480P_WIDE;
        else selectedMode = XBOX_VIDEO_4X3;
    }
    else
    {
        selectedMode = requestedMode;
    }

    u32 outputW = 640u;
    u32 outputH = 480u;
    u32 displayW = 640u;
    u32 displayH = 480u;
    u32 renderW = 640u;
    u32 renderH = 480u;
    bool widescreen = false;
    bool progressive = false;

    if (selectedMode == XBOX_VIDEO_720P_WIDE)
    {
        outputW = 1280u;
        outputH = 720u;
        displayW = 1280u;
        displayH = 720u;
        renderW = 856u;
        renderH = 480u;
        widescreen = true;
        progressive = true;
    }
    else if (selectedMode == XBOX_VIDEO_480P_WIDE)
    {
        outputW = 640u;
        outputH = 480u;
        displayW = 856u;
        displayH = 480u;
        renderW = 856u;
        renderH = 480u;
        widescreen = true;
        progressive = true;
    }

    windowSettings->fullscreen = true;
    windowSettings->width = outputW;
    windowSettings->height = outputH;

    graphics->gameResolution.x = renderW;
    graphics->gameResolution.z = renderH;
    graphics->widescreen = widescreen;
    graphics->rendererIndex = 0;        // RENDERER_SOFTWARE
    graphics->colorMode = (ColorMode)0; // COLORMODE_8BIT
    graphics->useMipmapping = false;
    graphics->frameRateLimit = 60;

    TFE_RenderBackend::xboxSetVideoMode(outputW, outputH, displayW, displayH, widescreen, progressive);
    TFE_System::logWrite(LOG_MSG, "Video",
        "Xbox video selected: requested=%s marker=%d selected=%s avPack=%lu flags=0x%08lx dashWide=%d can480p=%d can720p=%d output=%ux%u display=%ux%u render=%ux%u progressive=%d",
        xboxVideoModeName(system->xboxVideoMode), markerMode, xboxVideoModeName(selectedMode),
        avPack, videoFlags, dashWidescreen ? 1 : 0, can480p ? 1 : 0, can720p ? 1 : 0,
        outputW, outputH, displayW, displayH, renderW, renderH, progressive ? 1 : 0);
}

static const XboxBindingOption* findXboxBindingOption(s32 option)
{
    if (s_optionsPage != XOPAGE_CONTROLS) return NULL;
    for (s32 i = 0; i < (s32)TFE_ARRAYSIZE(c_xboxBindingOptions); i++)
    {
        if (c_xboxBindingOptions[i].option == option) return &c_xboxBindingOptions[i];
    }
    return NULL;
}

static const char* xboxButtonName(Button button)
{
    switch (button)
    {
        case CONTROLLER_BUTTON_A: return "A";
        case CONTROLLER_BUTTON_B: return "B";
        case CONTROLLER_BUTTON_X: return "X";
        case CONTROLLER_BUTTON_Y: return "Y";
        case CONTROLLER_BUTTON_BACK: return "BACK";
        case CONTROLLER_BUTTON_START: return "START";
        case CONTROLLER_BUTTON_LEFTSTICK: return "LSTICK";
        case CONTROLLER_BUTTON_RIGHTSTICK: return "RSTICK";
        case CONTROLLER_BUTTON_LEFTSHOULDER: return "WHITE";
        case CONTROLLER_BUTTON_RIGHTSHOULDER: return "BLACK";
        case CONTROLLER_BUTTON_DPAD_UP: return "DPAD UP";
        case CONTROLLER_BUTTON_DPAD_DOWN: return "DPAD DOWN";
        case CONTROLLER_BUTTON_DPAD_LEFT: return "DPAD LEFT";
        case CONTROLLER_BUTTON_DPAD_RIGHT: return "DPAD RIGHT";
        default: return "BUTTON";
    }
}

static s32 xboxButtonIcon(Button button)
{
    switch (button)
    {
        case CONTROLLER_BUTTON_A: return XOPT_ICON_A;
        case CONTROLLER_BUTTON_B: return XOPT_ICON_B;
        case CONTROLLER_BUTTON_X: return XOPT_ICON_X;
        case CONTROLLER_BUTTON_Y: return XOPT_ICON_Y;
        case CONTROLLER_BUTTON_BACK: return XOPT_ICON_BACK;
        case CONTROLLER_BUTTON_START: return XOPT_ICON_START;
        case CONTROLLER_BUTTON_LEFTSTICK: return XOPT_ICON_LSTICK;
        case CONTROLLER_BUTTON_RIGHTSTICK: return XOPT_ICON_RSTICK;
        case CONTROLLER_BUTTON_LEFTSHOULDER: return XOPT_ICON_WHITE;
        case CONTROLLER_BUTTON_RIGHTSHOULDER: return XOPT_ICON_BLACK;
        case CONTROLLER_BUTTON_DPAD_UP: return XOPT_ICON_DPAD_UP;
        case CONTROLLER_BUTTON_DPAD_DOWN: return XOPT_ICON_DPAD_DOWN;
        case CONTROLLER_BUTTON_DPAD_LEFT: return XOPT_ICON_DPAD_LEFT;
        case CONTROLLER_BUTTON_DPAD_RIGHT: return XOPT_ICON_DPAD_RIGHT;
        default: return -1;
    }
}

static const char* xboxAxisName(Axis axis)
{
    switch (axis)
    {
        case AXIS_LEFT_TRIGGER: return "LEFT TRIGGER";
        case AXIS_RIGHT_TRIGGER: return "RIGHT TRIGGER";
        case AXIS_LEFT_X: return "LEFT X";
        case AXIS_LEFT_Y: return "LEFT Y";
        case AXIS_RIGHT_X: return "RIGHT X";
        case AXIS_RIGHT_Y: return "RIGHT Y";
        default: return "AXIS";
    }
}

static s32 xboxAxisIcon(Axis axis)
{
    switch (axis)
    {
        case AXIS_LEFT_TRIGGER: return XOPT_ICON_LT;
        case AXIS_RIGHT_TRIGGER: return XOPT_ICON_RT;
        case AXIS_LEFT_X:
        case AXIS_LEFT_Y: return XOPT_ICON_LSTICK;
        case AXIS_RIGHT_X:
        case AXIS_RIGHT_Y: return XOPT_ICON_RSTICK;
        default: return -1;
    }
}

static const char* xboxBindingName(TFE_Input::InputAction action)
{
    u32 indices[16];
    const u32 count = TFE_Input::inputMapping_getBindingsForAction(action, indices, TFE_ARRAYSIZE(indices));
    for (u32 i = 0; i < count; i++)
    {
        TFE_Input::InputBinding* bind = TFE_Input::inputMapping_getBindingByIndex(indices[i]);
        if (bind->type == TFE_Input::ITYPE_CONTROLLER)
        {
            return xboxButtonName(bind->ctrlBtn);
        }
        if (bind->type == TFE_Input::ITYPE_CONTROLLER_AXIS)
        {
            return xboxAxisName(bind->axis);
        }
    }
    return "UNMAPPED";
}

static s32 xboxBindingIcon(TFE_Input::InputAction action)
{
    u32 indices[16];
    const u32 count = TFE_Input::inputMapping_getBindingsForAction(action, indices, TFE_ARRAYSIZE(indices));
    for (u32 i = 0; i < count; i++)
    {
        TFE_Input::InputBinding* bind = TFE_Input::inputMapping_getBindingByIndex(indices[i]);
        if (bind->type == TFE_Input::ITYPE_CONTROLLER)
        {
            return xboxButtonIcon(bind->ctrlBtn);
        }
        if (bind->type == TFE_Input::ITYPE_CONTROLLER_AXIS)
        {
            return xboxAxisIcon(bind->axis);
        }
    }
    return -1;
}

static const char* optionsTitle()
{
    switch (s_optionsPage)
    {
        case XOPAGE_CONTROLS: return "CONTROLS";
        case XOPAGE_VIDEO: return "VIDEO";
        case XOPAGE_AUDIO: return "AUDIO";
        default: return "OPTIONS";
    }
}

static s32 optionsVisibleCount()
{
    switch (s_optionsPage)
    {
        case XOPAGE_CONTROLS: return XCTRL_COUNT;
        case XOPAGE_VIDEO: return XVID_COUNT;
        case XOPAGE_AUDIO: return XAUD_COUNT;
        default: return XROOT_COUNT;
    }
}

static void setOptionText(s32 index, const char* label, const char* valueText)
{
    s_optionsItems[index].label = label;
    s_optionsItems[index].valueText = valueText;
}

static void setOptionSlider(s32 index, const char* label, s32 value, s32 minValue, s32 maxValue)
{
    s_optionsItems[index].label = label;
    s_optionsItems[index].value = value;
    s_optionsItems[index].minValue = minValue;
    s_optionsItems[index].maxValue = maxValue;
}

static void setOptionChoice(s32 index, const char* label, s32 value, s32 minValue, s32 maxValue, const char* valueText)
{
    setOptionSlider(index, label, value, minValue, maxValue);
    s_optionsItems[index].valueText = valueText;
}

static void setOptionIcon(s32 index, const char* label, s32 icon)
{
    s_optionsItems[index].label = label;
    if (icon >= 0)
    {
        s_optionsItems[index].hasIcon = true;
        s_optionsItems[index].valueIcon = icon;
    }
    else
    {
        s_optionsItems[index].valueText = "UNMAPPED";
    }
}

static void refreshOptionsItems()
{
    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    memset(s_optionsItems, 0, sizeof(s_optionsItems));
    s_optionsItemCount = optionsVisibleCount();

    if (s_optionsPage == XOPAGE_ROOT)
    {
        setOptionText(XROOT_CONTROLS, "CONTROLS", ">");
        setOptionText(XROOT_VIDEO, "VIDEO", ">");
        setOptionText(XROOT_AUDIO, "AUDIO", ">");
    }
    else if (s_optionsPage == XOPAGE_CONTROLS)
    {
        setOptionSlider(XCTRL_LOOK_SENS_X, "AIM SPEED X", (s32)(TFE_InputXbox::getLookSensitivityX() * 100.0f + 0.5f), 25, 250);
        setOptionSlider(XCTRL_LOOK_SENS_Y, "AIM SPEED Y", (s32)(TFE_InputXbox::getLookSensitivityY() * 100.0f + 0.5f), 25, 250);
        setOptionSlider(XCTRL_RIGHT_STICK_DEADZONE, "AIM DEAD ZONE", (s32)(TFE_InputXbox::getRightStickDeadzone() * 100.0f + 0.5f), 0, 30);

        for (s32 i = 0; i < (s32)TFE_ARRAYSIZE(c_xboxBindingOptions); i++)
        {
            const XboxBindingOption* option = &c_xboxBindingOptions[i];
            if (s_optionsCapture && s_optionsSelection == option->option)
            {
                setOptionText(option->option, option->label, "PRESS BUTTON");
                s_optionsItems[option->option].capture = true;
            }
            else
            {
                setOptionIcon(option->option, option->label, xboxBindingIcon(option->action));
            }
        }
    }
    else if (s_optionsPage == XOPAGE_VIDEO)
    {
        setOptionChoice(XVID_ASPECT_OUTPUT, "ASPECT / OUTPUT", system->xboxVideoMode,
            XBOX_VIDEO_AUTO, XBOX_VIDEO_COUNT - 1, xboxVideoModeName(system->xboxVideoMode));
        setOptionSlider(XVID_SAFE_ZONE_WIDTH, "SAFE AREA WIDTH", system->xboxSafeZoneWidthPercent, 80, 100);
        setOptionSlider(XVID_SAFE_ZONE_HEIGHT, "SAFE AREA HEIGHT", system->xboxSafeZoneHeightPercent, 80, 100);
        setOptionSlider(XVID_SCREEN_X, "HORIZONTAL SHIFT", system->xboxSafeZoneOffsetX, -40, 40);
        setOptionSlider(XVID_SCREEN_Y, "VERTICAL SHIFT", system->xboxSafeZoneOffsetY, -30, 30);
    }
    else if (s_optionsPage == XOPAGE_AUDIO)
    {
        setOptionSlider(XAUD_MASTER_VOLUME, "MASTER VOLUME", optionPercent(sound->masterVolume), 0, 100);
        setOptionSlider(XAUD_SFX_VOLUME, "SFX VOLUME", optionPercent(sound->soundFxVolume), 0, 100);
        setOptionSlider(XAUD_MUSIC_VOLUME, "MUSIC VOLUME", optionPercent(sound->musicVolume), 0, 100);
        setOptionSlider(XAUD_CUTSCENE_SFX, "CUTSCENE SFX", optionPercent(sound->cutsceneSoundFxVolume), 0, 100);
        setOptionSlider(XAUD_CUTSCENE_MUSIC, "CUTSCENE MUSIC", optionPercent(sound->cutsceneMusicVolume), 0, 100);
    }

    if (s_optionsSelection >= s_optionsItemCount) s_optionsSelection = s_optionsItemCount > 0 ? s_optionsItemCount - 1 : 0;
    if (s_optionsSelection < 0) s_optionsSelection = 0;
    if (s_optionsScroll > s_optionsItemCount - 7) s_optionsScroll = s_optionsItemCount > 7 ? s_optionsItemCount - 7 : 0;
    if (s_optionsScroll < 0) s_optionsScroll = 0;
}

static void applyOptionValue(s32 index, s32 value)
{
    if (index < 0 || index >= s_optionsItemCount) return;
    if (findXboxBindingOption(index)) return;
    if (value < s_optionsItems[index].minValue) value = s_optionsItems[index].minValue;
    if (value > s_optionsItems[index].maxValue) value = s_optionsItems[index].maxValue;

    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    if (s_optionsPage == XOPAGE_CONTROLS)
    {
        switch (index)
        {
            case XCTRL_LOOK_SENS_X:
                system->xboxLookSensitivityX = (float)value / 100.0f;
                break;
            case XCTRL_LOOK_SENS_Y:
                system->xboxLookSensitivityY = (float)value / 100.0f;
                break;
            case XCTRL_RIGHT_STICK_DEADZONE:
                system->xboxRightStickDeadzone = (float)value / 100.0f;
                break;
        }
        applyXboxControlSettings();
    }
    else if (s_optionsPage == XOPAGE_VIDEO)
    {
        switch (index)
        {
            case XVID_ASPECT_OUTPUT: system->xboxVideoMode = value; break;
            case XVID_SAFE_ZONE_WIDTH: system->xboxSafeZoneWidthPercent = value; break;
            case XVID_SAFE_ZONE_HEIGHT: system->xboxSafeZoneHeightPercent = value; break;
            case XVID_SCREEN_X: system->xboxSafeZoneOffsetX = value; break;
            case XVID_SCREEN_Y: system->xboxSafeZoneOffsetY = value; break;
        }
        applyXboxVideoSettings();
    }
    else if (s_optionsPage == XOPAGE_AUDIO)
    {
        switch (index)
        {
            case XAUD_MASTER_VOLUME: sound->masterVolume = (float)value / 100.0f; break;
            case XAUD_SFX_VOLUME: sound->soundFxVolume = (float)value / 100.0f; break;
            case XAUD_MUSIC_VOLUME: sound->musicVolume = (float)value / 100.0f; break;
            case XAUD_CUTSCENE_SFX: sound->cutsceneSoundFxVolume = (float)value / 100.0f; break;
            case XAUD_CUTSCENE_MUSIC: sound->cutsceneMusicVolume = (float)value / 100.0f; break;
        }
        sound = TFE_Settings::getSoundSettings();
        TFE_MidiPlayer::setVolume(sound->musicVolume * sound->masterVolume);
    }

    refreshOptionsItems();
}

static s32 optionAdjustStep()
{
    return (s_optionsPage == XOPAGE_VIDEO) ? 1 : 5;
}

static void optionsMove(s32 delta)
{
    s_optionsSelection += delta;
    if (s_optionsSelection < 0) s_optionsSelection = s_optionsItemCount - 1;
    if (s_optionsSelection >= s_optionsItemCount) s_optionsSelection = 0;
    if (s_optionsSelection < s_optionsScroll) s_optionsScroll = s_optionsSelection;
    if (s_optionsSelection >= s_optionsScroll + 7) s_optionsScroll = s_optionsSelection - 6;
    s_optionsCapture = false;
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "Options", "selection=%d", s_optionsSelection); }
}

static void openModMenu();

static void openOptionsMenu(bool pauseStyle)
{
    (void)pauseStyle;
    logMenuMusicState("open-options");
    s_optionsPage = XOPAGE_ROOT;
    refreshOptionsItems();
    s_optionsSelection = 0;
    s_optionsScroll = 0;
    s_optionsCapture = false;
    s_optionsStickUpHeld = s_optionsStickDownHeld = false;
    s_optionsStickLeftHeld = s_optionsStickRightHeld = false;
    s_curState = APP_STATE_OPTIONS;
    TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetOptionsScreen(true, false, optionsTitle(), s_optionsSelection, s_optionsScroll, s_optionsFrame, s_optionsItems, s_optionsItemCount);
}

static void closeOptionsMenu()
{
    logMenuMusicState("close-options-before");
    TFE_Settings::writeToDisk();
    xboxSaveRuntimeSettings();
    s_curState = APP_STATE_MENU;
    s_optionsCapture = false;
    TFE_RenderBackend::xboxSetOptionsScreen(false, false, "OPTIONS", 0, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
    startMenuMusic();
    logMenuMusicState("close-options-after");
}

static void showOptionsPage(XboxOptionsPage page)
{
    s_optionsPage = page;
    s_optionsSelection = 0;
    s_optionsScroll = 0;
    s_optionsCapture = false;
    refreshOptionsItems();
    TFE_RenderBackend::xboxSetOptionsScreen(s_curState == APP_STATE_OPTIONS, false, optionsTitle(),
        s_optionsSelection, s_optionsScroll, s_optionsFrame++, s_optionsItems, s_optionsItemCount);
}

static void refreshOptionsScreen()
{
    TFE_RenderBackend::xboxSetOptionsScreen(s_curState == APP_STATE_OPTIONS, false, optionsTitle(),
        s_optionsSelection, s_optionsScroll, s_optionsFrame++, s_optionsItems, s_optionsItemCount);
}

static void updateOptionsMenu()
{
    if (s_optionsCapture)
    {
        const XboxBindingOption* option = findXboxBindingOption(s_optionsSelection);
        Button button = TFE_Input::getControllerButtonPressed();
        Axis axis = TFE_Input::getControllerAnalogDown();
        if (option && button != CONTROLLER_BUTTON_UNKNOWN)
        {
            TFE_Input::inputMapping_setControllerBinding(option->action, TFE_Input::ITYPE_CONTROLLER, (u32)button);
            TFE_Input::inputMapping_serialize();
            s_optionsCapture = false;
            refreshOptionsItems();
        }
        else if (option && axis != AXIS_UNKNOWN)
        {
            TFE_Input::inputMapping_setControllerBinding(option->action, TFE_Input::ITYPE_CONTROLLER_AXIS, (u32)axis);
            TFE_Input::inputMapping_serialize();
            s_optionsCapture = false;
            refreshOptionsItems();
        }
        else if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
        {
            s_optionsCapture = false;
            refreshOptionsItems();
        }

        refreshOptionsScreen();
        return;
    }

    const f32 lx = TFE_Input::getAxis(AXIS_LEFT_X);
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;
    const bool stickLeft = lx < -0.55f;
    const bool stickRight = lx > 0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) || (stickUp && !s_optionsStickUpHeld)) optionsMove(-1);
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) || (stickDown && !s_optionsStickDownHeld)) optionsMove(1);
    s_optionsStickUpHeld = stickUp;
    s_optionsStickDownHeld = stickDown;

    s32 delta = 0;
    const s32 adjustStep = optionAdjustStep();
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_LEFT) || (stickLeft && !s_optionsStickLeftHeld)) delta = -adjustStep;
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_RIGHT) || (stickRight && !s_optionsStickRightHeld)) delta = adjustStep;
    s_optionsStickLeftHeld = stickLeft;
    s_optionsStickRightHeld = stickRight;
    if (delta) applyOptionValue(s_optionsSelection, s_optionsItems[s_optionsSelection].value + delta);

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        if (s_optionsPage == XOPAGE_ROOT)
        {
            if (s_optionsSelection == XROOT_CONTROLS) showOptionsPage(XOPAGE_CONTROLS);
            else if (s_optionsSelection == XROOT_VIDEO) showOptionsPage(XOPAGE_VIDEO);
            else if (s_optionsSelection == XROOT_AUDIO) showOptionsPage(XOPAGE_AUDIO);
            return;
        }
        else if (findXboxBindingOption(s_optionsSelection))
        {
            s_optionsCapture = true;
            refreshOptionsItems();
        }
        else
        {
            TFE_Settings::writeToDisk();
            xboxSaveRuntimeSettings();
            TFE_System::logWrite(LOG_MSG, "Options", "applied");
        }
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
    {
        if (s_optionsPage != XOPAGE_ROOT)
        {
            showOptionsPage(XOPAGE_ROOT);
            return;
        }
        closeOptionsMenu();
        return;
    }

    refreshOptionsScreen();
}

static void updateStartMenu()
{
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) ||
        (stickUp && !s_startStickUpHeld))
    {
        startMenuMove(-1);
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) ||
        (stickDown && !s_startStickDownHeld))
    {
        startMenuMove(1);
    }
    s_startStickUpHeld = stickUp;
    s_startStickDownHeld = stickDown;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) ||
        TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "StartMenu", "activate selection=%d", s_startMenuSelection); }
        if (s_startMenuSelection == 0)
        {
            const char* gameArgv[] = { "tfe_xbox", "-xbriefing", "-lSECBASE" };
            TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
            TFE_System::logWrite(LOG_MSG, "Main", "Starting Dark Forces from start menu.");
            startGame(3, gameArgv);
        }
        else if (s_startMenuSelection == 1)
        {
            logMenuMusicState("open-load-before");
            refreshLoadSlots();
            s_curState = APP_STATE_LOAD;
            s_loadMenuSelection = 0;
            TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
            TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
            TFE_RenderBackend::xboxSetLoadScreen(true, s_loadMenuSelection, s_loadMenuFrame, s_loadSlots, 6);
            if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "StartMenu", "opened load screen"); }
            logMenuMusicState("open-load-after");
        }
        else if (s_startMenuSelection == 2)
        {
            openModMenu();
        }
        else if (s_startMenuSelection == 3)
        {
            if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "StartMenu", "opened options screen"); }
            openOptionsMenu(false);
        }
        else
        {
            TFE_System::logWrite(LOG_WARNING, "StartMenu", "selection %d is not wired yet", s_startMenuSelection);
        }
    }

    TFE_RenderBackend::xboxSetStartScreen(s_curState == APP_STATE_MENU, s_startMenuSelection, s_startMenuFrame++);
}

static void loadMenuMove(s32 delta)
{
    s_loadMenuSelection += delta;
    if (s_loadMenuSelection < 0) s_loadMenuSelection = 5;
    if (s_loadMenuSelection > 5) s_loadMenuSelection = 0;
    if (s_frontendVerbose)
    {
        TFE_System::logWrite(LOG_MSG, "LoadMenu", "selection=%d valid=%d",
            s_loadMenuSelection, s_loadSlots[s_loadMenuSelection].valid ? 1 : 0);
    }
}

static void closeLoadMenu()
{
    logMenuMusicState("close-load-before");
    s_curState = APP_STATE_MENU;
    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
    startMenuMusic();
    logMenuMusicState("close-load-after");
}

static void openModMenu()
{
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "openModMenu begin state=%d game=%p", (s32)s_curState, s_curGame); }
    logMenuMusicState("open-mod-before");
    logXboxResourceSnapshot("mod-menu-open-before");
    pumpXboxAudio();
    refreshModSlots();
    pumpXboxAudio();
    s_curState = APP_STATE_MODS;
    s_modMenuSelection = 0;
    s_modStickUpHeld = false;
    s_modStickDownHeld = false;
    TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
    TFE_RenderBackend::xboxSetModScreen(true, s_modMenuSelection, s_modMenuFrame, s_modUi, s_modCount);
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "StartMenu", "opened mod screen"); }
    logXboxResourceSnapshot("mod-menu-open-after");
    logMenuMusicState("open-mod-after");
}

static void closeModMenu()
{
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "closeModMenu state=%d selection=%d count=%d", (s32)s_curState, s_modMenuSelection, s_modCount); }
    logMenuMusicState("close-mod-before");
    logXboxResourceSnapshot("mod-menu-close-before");
    s_curState = APP_STATE_MENU;
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
    startMenuMusic();
    logXboxResourceSnapshot("mod-menu-close-after");
    logMenuMusicState("close-mod-after");
}

static void modMenuMove(s32 delta)
{
    if (s_modCount <= 0) return;
    s_modMenuSelection += delta;
    if (s_modMenuSelection < 0) s_modMenuSelection = s_modCount - 1;
    if (s_modMenuSelection >= s_modCount) s_modMenuSelection = 0;
    if (s_frontendVerbose) { TFE_System::logWrite(LOG_MSG, "ModMenu", "selection=%d title='%s'", s_modMenuSelection, s_modEntries[s_modMenuSelection].title); }
}

static void startSelectedMod()
{
    TFE_System::logWrite(LOG_MSG, "ModMenu", "start selection=%d count=%d", s_modMenuSelection, s_modCount);
    logXboxResourceSnapshot("mod-start-before");
    if (s_modMenuSelection < 0 || s_modMenuSelection >= s_modCount) return;
    XboxModEntry* mod = &s_modEntries[s_modMenuSelection];
    if (!mod->ui.valid || !mod->archiveName[0])
    {
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "startSelectedMod invalid valid=%d archive='%s'",
            mod->ui.valid ? 1 : 0, mod->archiveName);
        return;
    }

    if (!mod->levelName[0])
    {
        TFE_System::logWrite(LOG_MSG, "ModMenu", "selected mod needs level scan title='%s' archive='%s'",
            mod->title, mod->archiveName);
#ifdef _XBOX
        readFirstLevelFromGobFile(mod);
#else
        readFirstLevelFromZip(mod);
#endif
    }

    static char modArg[128];
    static char levelArg[64];
    snprintf(modArg, sizeof(modArg), "-u%s", mod->archiveName);
    if (mod->levelName[0])
    {
        snprintf(levelArg, sizeof(levelArg), "-l%s", mod->levelName);
        const char* gameArgv[] = { "tfe_xbox", "-c0", modArg, levelArg };
        TFE_System::logWrite(LOG_MSG, "ModMenu", "starting title='%s' archive='%s' level='%s'",
            mod->title, mod->archiveName, mod->levelName);
        startGame(4, gameArgv, mod->path);
    }
    else
    {
        TFE_System::logWrite(LOG_ERROR, "ModMenu", "cannot start mod without detected level title='%s' path='%s' archive='%s'",
            mod->title, mod->path, mod->archiveName);
    }
}

static void resumeSelectedMod()
{
    TFE_System::logWrite(LOG_MSG, "ModMenu", "resume selection=%d count=%d", s_modMenuSelection, s_modCount);
    logXboxResourceSnapshot("mod-resume-before");
    if (s_modMenuSelection < 0 || s_modMenuSelection >= s_modCount) return;
    XboxModEntry* mod = &s_modEntries[s_modMenuSelection];
    if (!mod->ui.valid || !mod->archiveName[0] || !mod->hasQuickSave || !mod->quickSaveName[0])
    {
        TFE_System::logWrite(LOG_WARNING, "ModMenu",
            "resumeSelectedMod invalid valid=%d archive='%s' hasQuickSave=%d quickSave='%s'",
            mod->ui.valid ? 1 : 0, mod->archiveName, mod->hasQuickSave ? 1 : 0, mod->quickSaveName);
        return;
    }

    TFE_System::logWrite(LOG_MSG, "ModMenu", "resuming title='%s' save='%s' archive='%s'",
        mod->title, mod->quickSaveName, mod->archiveName);
    loadGameFromMenu(mod->quickSaveName, mod->path);
}

static void updateModMenu()
{
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) || (stickUp && !s_modStickUpHeld))
    {
        modMenuMove(-1);
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) || (stickDown && !s_modStickDownHeld))
    {
        modMenuMove(1);
    }
    s_modStickUpHeld = stickUp;
    s_modStickDownHeld = stickDown;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
    {
        closeModMenu();
        return;
    }

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        if (s_modCount > 0)
        {
            startSelectedMod();
            return;
        }
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "start pressed with no mods installed");
    }

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_X))
    {
        if (s_modCount > 0 && s_modEntries[s_modMenuSelection].hasQuickSave)
        {
            resumeSelectedMod();
            return;
        }
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "resume pressed without mod quicksave selection=%d", s_modMenuSelection);
    }

    TFE_RenderBackend::xboxSetModScreen(s_curState == APP_STATE_MODS, s_modMenuSelection, s_modMenuFrame++, s_modUi, s_modCount);
}

static void updateLoadMenu()
{
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) ||
        (stickUp && !s_loadStickUpHeld))
    {
        loadMenuMove(-1);
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) ||
        (stickDown && !s_loadStickDownHeld))
    {
        loadMenuMove(1);
    }
    s_loadStickUpHeld = stickUp;
    s_loadStickDownHeld = stickDown;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) ||
        TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
    {
        closeLoadMenu();
        return;
    }

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) ||
        TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        if (s_loadSlots[s_loadMenuSelection].valid)
        {
            loadGameFromMenu(s_loadSlots[s_loadMenuSelection].fileName);
            return;
        }
        TFE_System::logWrite(LOG_WARNING, "LoadMenu", "empty slot selected=%d", s_loadMenuSelection);
    }

    TFE_RenderBackend::xboxSetLoadScreen(s_curState == APP_STATE_LOAD, s_loadMenuSelection, s_loadMenuFrame++, s_loadSlots, 6);
}

// ---------------------------------------------------------------------------
// XBE entry point
// The XDK expects void __cdecl main() with no arguments.
// ---------------------------------------------------------------------------
void __cdecl main()
{
    // Open the log file as the very first thing, before any other init,
    // so early-boot failures are captured. XEMU soak logs go to E:\ because
    // the staged ISO's D:\ root is read-only; normal Xbox logs stay on D:\.
    s_soakMode = xboxDetectSoakMode();
    TFE_System::openRotatingLog(s_soakMode ? "E:\\tfe_xbox_soak_log.txt" : "tfe_xbox_log.txt");
    TFE_XboxLogf("Main", "XBE entry");

    // -----------------------------------------------------------------------
    // XInput device enumeration - MUST come before D3D init.
    // XDK requirement: XInitDevices must be called before
    // Direct3D_CreateDevice (ordering required by the USB host controller
    // initialisation sequence on NV2A hardware). Without this call,
    // XInputOpen returns NULL and no controllers are detected.
    // Pattern lifted verbatim from OpenJKDF2's main_xbox.c.
    // -----------------------------------------------------------------------
    {
        XDEVICE_PREALLOC_TYPE xdpt[2];
        xdpt[0].DeviceType      = XDEVICE_TYPE_GAMEPAD;
        xdpt[0].dwPreallocCount = 4;
        xdpt[1].DeviceType      = XDEVICE_TYPE_MEMORY_UNIT;
        xdpt[1].dwPreallocCount = 8;
        TFE_XboxLogf("Main", "calling XInitDevices");
        XInitDevices(2, xdpt);
        TFE_XboxLogf("Main", "XInitDevices done");
    }

    // -----------------------------------------------------------------------
    // Paths
    // -----------------------------------------------------------------------
    TFE_XboxLogf("Main", "initialising paths");
    TFE_XboxLogf("Main", "calling setProgramPath");
    TFE_Paths::setProgramPath();
    TFE_XboxLogf("Main", "setProgramPath returned");
    TFE_XboxLogf("Main", "calling setProgramDataPath");
    TFE_Paths::setProgramDataPath("TFE");
    TFE_XboxLogf("Main", "setProgramDataPath returned");
    TFE_XboxLogf("Main", "calling setUserDocumentsPath");
    TFE_Paths::setUserDocumentsPath("TFE");
    TFE_XboxLogf("Main", "setUserDocumentsPath returned");
    TFE_XboxLogf("DashboardMeta", "ensuring title metadata");
    xboxEnsureDashboardMetadata();

    // ----- CP block: pre-logWrite probe. logWrite uses time()/localtime()/
    // strftime() which may hang if the Xbox CRT time-of-day isn't set up.
    // If the next CP fires but no [Main] line follows, logWrite is the hang.
    TFE_System::logWrite(LOG_MSG, "Main", "The Force Engine %s (Xbox)", c_gitVersion);

    TFE_System::logWrite(LOG_MSG, "Paths", "Program:   \"%s\"", TFE_Paths::getPath(PATH_PROGRAM));
    TFE_System::logWrite(LOG_MSG, "Paths", "SaveData:  \"%s\"", TFE_Paths::getPath(PATH_PROGRAM_DATA));

    // -----------------------------------------------------------------------
    // Messages (skipped on Xbox)
    // -----------------------------------------------------------------------
    // We deliberately do NOT probe for D:\UI_Text\TfeMessages.txt on Xbox.
    // The xquake gold-standard pattern (xbox/private/test/games/xquake) is:
    // only call file APIs on paths under registered, validated search dirs.
    // If we blind-probe a path whose parent dir doesn't exist, FATX's path
    // resolver hangs (confirmed empirically with fopen, CreateFileA,
    // GetFileAttributesA, and FindFirstFileA — all hang on missing
    // intermediate dirs). UI_Text\ isn't shipped with the XBE FTP, so we
    // skip the call entirely. Loadcaller is already designed to handle
    // missing messages.
    TFE_System::logWrite(LOG_WARNING, "Main", "TfeMessages.txt skipped on Xbox - continuing.");

    // -----------------------------------------------------------------------
    // Settings
    // -----------------------------------------------------------------------
    bool firstRun = false;
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising settings.");
    if (!TFE_Settings::init(firstRun))
    {
        TFE_System::logWrite(LOG_CRITICAL, "Main", "Cannot load settings.");
        TFE_System::logClose();
        return;
    }
    TFE_System::logWrite(LOG_MSG, "Main", "Settings initialised firstRun=%d", firstRun ? 1 : 0);

    // Set source data path from program directory.
    setupSourceDataPath();

    // Load Xbox runtime settings from the writable title save container.
    xboxLoadRuntimeSettings();

    // Override settings for Xbox before D3D device creation. Output/aspect
    // mode must be selected here; changing it live would require a device reset.
    TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
    TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
    xboxApplyVideoModeToSettings();
    TFE_System::logWrite(LOG_MSG, "Main",
        "Xbox settings applied: window=%dx%d game=%dx%d renderer=%d colorMode=%d vsync=%d fpsLimit=%d",
        windowSettings->width, windowSettings->height,
        graphics->gameResolution.x, graphics->gameResolution.z,
        graphics->rendererIndex, (int)graphics->colorMode, graphics->vsync ? 1 : 0,
        graphics->frameRateLimit);

    // -----------------------------------------------------------------------
    // System init (timing)
    // -----------------------------------------------------------------------
    const f32 refreshRate = 60.0f;
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising system timing.");
    TFE_System::init(refreshRate, graphics->vsync, c_gitVersion);

    // -----------------------------------------------------------------------
    // Render backend (D3D8)
    // -----------------------------------------------------------------------
    WindowState windowState;
    memset(&windowState, 0, sizeof(windowState));
    strcpy(windowState.name, "The Force Engine");
    windowState.width          = windowSettings->width;
    windowState.height         = windowSettings->height;
    windowState.baseWindowWidth  = windowSettings->width;
    windowState.baseWindowHeight = windowSettings->height;
    windowState.monitorWidth   = windowSettings->width;
    windowState.monitorHeight  = windowSettings->height;
    windowState.flags          = WINFLAG_FULLSCREEN | (graphics->vsync ? WINFLAG_VSYNC : 0);
    windowState.refreshRate    = refreshRate;

    bool rbOk = TFE_RenderBackend::init(windowState);
    if (!rbOk)
    {
        TFE_System::logWrite(LOG_CRITICAL, "GPU", "Cannot initialise D3D8 render backend.");
        TFE_System::logClose();
        return;
    }
    TFE_System::logWrite(LOG_MSG, "GPU", "Render backend initialised.");

    // -----------------------------------------------------------------------
    // Audio (PCM + OGG via STB Vorbis - MIDI is baked)
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising audio.");
    // The Xbox device owns a lightweight DirectSound pump thread so audio can
    // continue while menu and level transitions do synchronous work. The main
    // loop still calls pump() as catch-up.
    TFE_Audio::init(false, TFE_Settings::getSoundSettings()->audioDevice);
    TFE_System::logWrite(LOG_MSG, "Main", "Audio init returned.");

    // MidiPlayer: OPL3 software synth (Fm4Opl3Device). Default device type
    // matches iMuse expectation; rendering happens inside the audio mixer
    // callback chain via synthesizeMidi().
    TFE_MidiPlayer::init(-1, MIDI_TYPE_OPL3);
    TFE_System::logWrite(LOG_MSG, "Main", "MidiPlayer init returned.");

    // -----------------------------------------------------------------------
    // Asset systems
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising image and palette systems.");
    TFE_Image::init();
    TFE_Palette::createDefault256();

    // -----------------------------------------------------------------------
    // Game systems
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising game/input/save systems.");
    game_init();
    inputMapping_startup();
    TFE_SaveSystem::init();
    TFE_SaveSystem::setCurrentGame(TFE_Settings::getGame()->id);
    TFE_InputXbox::init();
    applyXboxControlSettings();

    // -----------------------------------------------------------------------
    // Reticle
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising reticle.");
    reticle_init();

    // -----------------------------------------------------------------------
    // Validate game data
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Validating game data.");
    bool pathOk = validatePath();
    if (!pathOk)
    {
        TFE_System::logWrite(LOG_CRITICAL, "Main",
            "Game data not found. Place DARK.GOB in the DARK\\ folder next to the XBE.");
        // Spin with a black screen and error in log - no recovery UI on Xbox.
        while (true)
        {
            TFE_RenderBackend::clearWindow();
            TFE_RenderBackend::swap(false);
            Sleep(1000);
        }
    }

    // -----------------------------------------------------------------------
    // Frame limiter
    // -----------------------------------------------------------------------
    TFE_System::frameLimiter_set(graphics->frameRateLimit);
    TFE_System::logWrite(LOG_MSG, "Main", "Frame limiter set to %d", graphics->frameRateLimit);

    // -----------------------------------------------------------------------
    // Start with the original Landru intro sequence, then return to the
    // Xbox-native menu. Start Game itself jumps to the first briefing so the
    // intro does not replay every time.
    // -----------------------------------------------------------------------
    TFE_Input::enableRelativeMode(false);
    if (s_soakMode)
    {
        const char* soakArgv[] = { "tfe_xbox", "-c0", "-lSECBASE" };
        TFE_System::logWrite(LOG_MSG, "SOAK", "marker present; starting campaign transition soak at SECBASE");
        startGame(3, soakArgv);
    }
    else
    {
        const char* introArgv[] = { "tfe_xbox", "-xintro" };
        TFE_System::logWrite(LOG_MSG, "Main", "Starting Xbox startup intro.");
        startGame(2, introArgv);
    }

    TFE_System::logWrite(LOG_MSG, "Main", "Entering app loop.");

    // -----------------------------------------------------------------------
    // Game loop
    // -----------------------------------------------------------------------
    while (s_loop && !TFE_System::quitMessagePosted())
    {
        TFE_System::frameLimiter_begin();

        TFE_InputXbox::pollInput();
        pumpXboxAudio();

        // Check Start+Back as quit combo (held together).
        if (TFE_Input::buttonDown(CONTROLLER_BUTTON_START) &&
            TFE_Input::buttonDown(CONTROLLER_BUTTON_BACK))
        {
            TFE_System::logWrite(LOG_MSG, "Main", "Start+Back held - quitting.");
            s_loop = false;
            break;
        }

        if (s_curState == APP_STATE_MENU)
        {
            updateStartMenu();
        }
        else if (s_curState == APP_STATE_LOAD)
        {
            updateLoadMenu();
        }
        else if (s_curState == APP_STATE_MODS)
        {
            updateModMenu();
        }
        else if (s_curState == APP_STATE_OPTIONS)
        {
            updateOptionsMenu();
        }
        else if (!inputMapping_handleInputs())
        {
            pumpXboxAudio();
            TFE_Input::endFrame();
            inputMapping_endFrame();
            continue;
        }
        if (s_curState == APP_STATE_MENU ||
            s_curState == APP_STATE_LOAD ||
            s_curState == APP_STATE_MODS ||
            s_curState == APP_STATE_OPTIONS)
        {
            updateMenuMusicHeartbeat();
        }
        pumpXboxAudio();

        TFE_System::update();

        bool endInputFrame = true;
        if (s_curState == APP_STATE_GAME)
        {
            if (!s_curGame)
            {
                // Game exited cleanly (e.g. credits ended).
                s_loop = false;
            }
            else
            {
                TFE_SaveSystem::update();
                const char* loadRequest = TFE_SaveSystem::loadRequestFilename();
                if (loadRequest && loadRequest[0])
                {
                    char loadFilename[TFE_MAX_PATH];
                    strncpy(loadFilename, loadRequest, TFE_MAX_PATH - 1);
                    loadFilename[TFE_MAX_PATH - 1] = 0;
                    TFE_System::logWrite(LOG_MSG, "LoadMenu",
                        "in-game load request file='%s' activeModPath='%s'",
                        loadFilename, s_activeModSearchPath);
                    loadGameFromMenu(loadFilename, s_activeModSearchPath[0] ? s_activeModSearchPath : NULL);
                    endInputFrame = true;
                }
                else
                {
                    s_curGame->loopGame();
                    if (s_returnToStartRequested)
                    {
                        TFE_System::logWrite(LOG_MSG, "Main", "processing return-to-start begin game=%p state=%d", s_curGame, (s32)s_curState);
                        TFE_Paths::debugLogState("return-before-free");
                        logXboxResourceSnapshot("return-before-free");
                        freeGame(s_curGame);
                        s_curGame = NULL;
                        s_activeModSearchPath[0] = 0;
                        purgeRuntimeGameResources("return-to-frontend");
                        pumpXboxAudio();
                        s_curState = APP_STATE_MENU;
                        s_returnToStartRequested = false;
                        TFE_Input::enableRelativeMode(false);
                        TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
                        TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
                        TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
                        TFE_RenderBackend::xboxSetOptionsScreen(false, false, "OPTIONS", 0, 0, 0, NULL, 0);
                        startMenuMusic();
                        logMenuMusicState("return-to-start-after-music");
                        TFE_Paths::debugLogState("return-after-menu");
                        logXboxResourceSnapshot("return-after-menu");
                        TFE_System::logWrite(LOG_MSG, "Main", "processing return-to-start complete state=%d game=%p", (s32)s_curState, s_curGame);
                        endInputFrame = true;
                    }
                    else
                    {
                        const s32 taskRan = TFE_Jedi::task_run();
                        updateXboxSoak();
                        endInputFrame = taskRan != 0;
                    }
                }
            }
        }

        if (s_curState == APP_STATE_GAME || shouldPresentNativeFrontend(s_curState))
        {
            TFE_RenderBackend::swap(s_curState == APP_STATE_GAME);
        }
        else
        {
            pumpXboxAudio();
        }

        // Catch-up pump; the Xbox audio device also has its own pump thread.
        pumpXboxAudio();

        TFE_System::frameLimiter_end();

        if (endInputFrame)
        {
            TFE_Input::endFrame();
            inputMapping_endFrame();
        }
    }

    TFE_System::logWrite(LOG_MSG, "Main", "Game loop ended. Shutting down.");
    stopMenuMusic();

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    TFE_InputXbox::shutdown();

    if (s_curGame)
    {
        freeGame(s_curGame);
        s_curGame = NULL;
    }

    game_destroy();
    reticle_destroy();
    inputMapping_shutdown();

    TFE_Audio::shutdown();
    TFE_MidiPlayer::destroy();
    TFE_Image::shutdown();
    TFE_Palette::freeAll();
    TFE_Settings::shutdown();
    TFE_Jedi::texturepacker_freeGlobal();
    TFE_RenderBackend::destroy();
    TFE_SaveSystem::destroy();
    TFE_System::freeMessages();

    TFE_System::logWrite(LOG_MSG, "Main", "Shutdown complete.");
    TFE_System::logClose();
}
