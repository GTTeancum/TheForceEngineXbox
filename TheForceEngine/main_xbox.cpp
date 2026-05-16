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
#include <TFE_Archive/gobArchive.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Game/reticle.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/audioDevice.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Input/replay.h>
#include <TFE_Settings/settings.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Asset/paletteAsset.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/mission.h>
// AppState is defined in frontEndUi.h which pulls in STL and ImGui.
// Redeclare the enum directly here for Xbox to avoid those dependencies.
// Keep in sync with TFE_FrontEndUI/frontEndUi.h.
enum AppState
{
    APP_STATE_MENU = 0,
    APP_STATE_EDITOR,
    APP_STATE_LOAD,
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

// ---------------------------------------------------------------------------
// Game lifecycle
// ---------------------------------------------------------------------------
static void startGame(int argc, const char** argv)
{
    if (s_curGame)
    {
        freeGame(s_curGame);
        s_curGame = NULL;
    }

    TFE_Game* gameInfo = TFE_Settings::getGame();
    s_curGame = createGame(gameInfo->id);
    TFE_SaveSystem::setCurrentGame(s_curGame);

    if (!s_curGame)
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot create game.");
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }

    if (!s_curGame->runGame(argc, argv, NULL))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot run game.");
        freeGame(s_curGame);
        s_curGame = NULL;
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }

    TFE_Input::enableRelativeMode(true);
    s_curState = APP_STATE_GAME;
    TFE_System::logWrite(LOG_MSG, "Main", "Game started.");
}

// ---------------------------------------------------------------------------
// XBE entry point
// The XDK expects void __cdecl main() with no arguments.
// ---------------------------------------------------------------------------
void __cdecl main()
{
    // Open the log file as the very first thing, before any other init,
    // so early-boot failures are captured. Hardcoded to E:\tfe_xbox_log.txt;
    // overwritten each boot (no rotation).
    TFE_System::openRotatingLog("tfe_xbox_log.txt");
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

    // Override settings for Xbox: always fullscreen, fixed 720p.
    TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
    windowSettings->fullscreen = true;
    windowSettings->width      = 1280;
    windowSettings->height     = 720;

    TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
    graphics->gameResolution.x = 320;
    graphics->gameResolution.z = 200;
    graphics->widescreen = false;
    // Phase 0 of the RClassic_GPU/D3D8 port: flip to RENDERER_HARDWARE so
    // jediRenderer takes the TSR_CLASSIC_GPU branch. The Xbox GPU stubs in
    // xbox_link_stubs.cpp still draw no world geometry; renderBackend_xbox
    // detects VDISP_RENDER_TARGET and clears the back buffer to magenta as
    // the visible confirmation that the path is being taken end to end.
    // Flip back to 0 to fall through to the software path while debugging.
    graphics->rendererIndex = 1;  // RENDERER_HARDWARE (Phase 0: magenta)
    graphics->colorMode = (ColorMode)0;  // COLORMODE_8BIT
    graphics->useMipmapping = false;
    TFE_System::logWrite(LOG_MSG, "Main",
        "Xbox settings applied: window=%dx%d game=%dx%d renderer=%d colorMode=%d vsync=%d",
        windowSettings->width, windowSettings->height,
        graphics->gameResolution.x, graphics->gameResolution.z,
        graphics->rendererIndex, (int)graphics->colorMode, graphics->vsync ? 1 : 0);

    // Ensure saves directory exists.
    char savesDir[TFE_MAX_PATH];
    snprintf(savesDir, TFE_MAX_PATH, "%sSaves\\", TFE_Paths::getPath(PATH_PROGRAM));
    FileUtil::makeDirectory(savesDir);
    TFE_System::logWrite(LOG_MSG, "Main", "Save directory ensured: '%s'", savesDir);

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
    windowState.width          = 1280;
    windowState.height         = 720;
    windowState.baseWindowWidth  = 1280;
    windowState.baseWindowHeight = 720;
    windowState.monitorWidth   = 1280;
    windowState.monitorHeight  = 720;
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
    // audioDevice_xbox.cpp is now polled (matches xquake / OpenJKDF2 /
    // Mercs). The earlier notification-thread implementation crashed in
    // CXBX-R's HLE; the polled model has no audio thread and runs each
    // pump() from the main loop. We pass useNullDevice=false to enable it.
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
    TFE_InputXbox::init();

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
    // Start game immediately (no front-end menu on Xbox)
    // -----------------------------------------------------------------------
    const char* gameArgv[] = { "tfe_xbox" };
    TFE_System::logWrite(LOG_MSG, "Main", "Starting Dark Forces.");
    startGame(1, gameArgv);

    if (s_curState != APP_STATE_GAME)
    {
        TFE_System::logWrite(LOG_CRITICAL, "Main", "Failed to start game - halting.");
        while (true) { Sleep(1000); }
    }

    TFE_System::logWrite(LOG_MSG, "Main", "Entering game loop.");

    // -----------------------------------------------------------------------
    // Game loop
    // -----------------------------------------------------------------------
    while (s_loop && !TFE_System::quitMessagePosted())
    {
        TFE_System::frameLimiter_begin();

        TFE_InputXbox::pollInput();

        // Check Start+Back as quit combo (held together).
        if (TFE_Input::buttonDown(CONTROLLER_BUTTON_START) &&
            TFE_Input::buttonDown(CONTROLLER_BUTTON_BACK))
        {
            TFE_System::logWrite(LOG_MSG, "Main", "Start+Back held - quitting.");
            s_loop = false;
            break;
        }

        if (!inputMapping_handleInputs())
        {
            TFE_Input::endFrame();
            inputMapping_endFrame();
            continue;
        }

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
                s_curGame->loopGame();
                endInputFrame = TFE_Jedi::task_run() != 0;
            }
        }

        TFE_RenderBackend::swap(s_curState == APP_STATE_GAME);

        // Refill the audio ring buffer (polled streaming - no audio thread).
        TFE_AudioDevice::pump();

        TFE_System::frameLimiter_end();

        if (endInputFrame)
        {
            TFE_Input::endFrame();
            inputMapping_endFrame();
        }
    }

    TFE_System::logWrite(LOG_MSG, "Main", "Game loop ended. Shutting down.");

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
