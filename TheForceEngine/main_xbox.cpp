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
#include <TFE_RenderBackend/renderBackend_xbox.h>
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
static s32      s_startMenuSelection = 0;
static u32      s_startMenuFrame = 0;
static bool     s_startStickUpHeld = false;
static bool     s_startStickDownHeld = false;
static s32      s_loadMenuSelection = 0;
static u32      s_loadMenuFrame = 0;
static bool     s_loadStickUpHeld = false;
static bool     s_loadStickDownHeld = false;
static TFE_SaveSystem::SaveHeader s_loadHeaders[6];
static TFE_RenderBackend::XboxLoadSlotInfo s_loadSlots[6];
static char s_loadDateDisplay[6][32];
static bool s_returnToStartRequested = false;

extern "C" void TFE_XboxReturnToStartMenu()
{
    s_returnToStartRequested = true;
    TFE_System::logWrite(LOG_MSG, "Main", "Return to start menu requested.");
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

static bool loadGameFromMenu(const char* filename)
{
    if (!filename || !filename[0]) return false;
    if (s_curGame)
    {
        freeGame(s_curGame);
        s_curGame = NULL;
    }

    TFE_Game* gameInfo = TFE_Settings::getGame();
    s_curGame = createGame(gameInfo->id);
    if (!s_curGame)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "Cannot create game for load.");
        s_curState = APP_STATE_CANNOT_RUN;
        return false;
    }
    TFE_SaveSystem::setCurrentGame(s_curGame);

    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    const bool loaded = TFE_SaveSystem::loadGame(filename);
    if (!loaded)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "load failed '%s'", filename);
        freeGame(s_curGame);
        s_curGame = NULL;
        s_curState = APP_STATE_MENU;
        TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
        return false;
    }

    TFE_Input::enableRelativeMode(true);
    s_curState = APP_STATE_GAME;
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "loaded '%s'", filename);
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
        s_loadSlots[i].autosave = (i == 0);
        s_loadSlots[i].fileName = valid ? s_loadHeaders[i].fileName : filename;
        s_loadSlots[i].saveName = valid ? s_loadHeaders[i].saveName : "";
        s_loadSlots[i].dateTime = valid ? s_loadDateDisplay[i] : "";
        s_loadSlots[i].levelName = valid ? s_loadHeaders[i].levelName : "";
        s_loadSlots[i].levelId = valid ? s_loadHeaders[i].levelId : "";
        s_loadSlots[i].imageData = valid ? s_loadHeaders[i].imageData : NULL;
    }
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "slots refreshed");
}

static void startMenuMove(s32 delta)
{
    s_startMenuSelection += delta;
    if (s_startMenuSelection < 0) s_startMenuSelection = 3;
    if (s_startMenuSelection > 3) s_startMenuSelection = 0;
    TFE_System::logWrite(LOG_MSG, "StartMenu", "selection=%d", s_startMenuSelection);
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
        TFE_System::logWrite(LOG_MSG, "StartMenu", "activate selection=%d", s_startMenuSelection);
        if (s_startMenuSelection == 0)
        {
            const char* gameArgv[] = { "tfe_xbox", "-lSECBASE" };
            TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
            TFE_System::logWrite(LOG_MSG, "Main", "Starting Dark Forces from start menu.");
            startGame(2, gameArgv);
        }
        else if (s_startMenuSelection == 1)
        {
            refreshLoadSlots();
            s_curState = APP_STATE_LOAD;
            s_loadMenuSelection = 0;
            TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
            TFE_RenderBackend::xboxSetLoadScreen(true, s_loadMenuSelection, s_loadMenuFrame, s_loadSlots, 6);
            TFE_System::logWrite(LOG_MSG, "StartMenu", "opened load screen");
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
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "selection=%d valid=%d",
        s_loadMenuSelection, s_loadSlots[s_loadMenuSelection].valid ? 1 : 0);
}

static void closeLoadMenu()
{
    s_curState = APP_STATE_MENU;
    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
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

    // Override settings for Xbox: always fullscreen with a native 640x480
    // game framebuffer presented through the D3D8 backend.
    TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
    windowSettings->fullscreen = true;
    windowSettings->width      = 1280;
    windowSettings->height     = 720;

    TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
    graphics->gameResolution.x = 640;
    graphics->gameResolution.z = 480;
    graphics->widescreen = false;
    graphics->rendererIndex = 0;  // RENDERER_SOFTWARE
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
    TFE_SaveSystem::setCurrentGame(TFE_Settings::getGame()->id);
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
    // Start at the Xbox-native menu. The game is created only when the
    // player chooses Start Game.
    // -----------------------------------------------------------------------
    s_curState = APP_STATE_MENU;
    TFE_Input::enableRelativeMode(false);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);

    TFE_System::logWrite(LOG_MSG, "Main", "Entering app loop at start menu.");

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

        if (s_curState == APP_STATE_MENU)
        {
            updateStartMenu();
        }
        else if (s_curState == APP_STATE_LOAD)
        {
            updateLoadMenu();
        }
        else if (!inputMapping_handleInputs())
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
                if (s_returnToStartRequested)
                {
                    freeGame(s_curGame);
                    s_curGame = NULL;
                    s_curState = APP_STATE_MENU;
                    s_returnToStartRequested = false;
                    TFE_Input::enableRelativeMode(false);
                    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
                    endInputFrame = true;
                }
                else
                {
                    endInputFrame = TFE_Jedi::task_run() != 0;
                }
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
