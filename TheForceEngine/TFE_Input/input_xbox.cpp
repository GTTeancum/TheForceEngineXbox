// input_xbox.cpp
// Xbox input polling via XInput (XDK).
// Replaces the SDL handleEvent() / SDL_PollEvent() loop from main.cpp.
//
// Call TFE_InputXbox::pollInput() once per frame from the Xbox main loop,
// before TFE_Input::inputMapping_updateInput().
//
// Controller layout mapped to TFE actions:
//   Left stick Y       -> IADF_FORWARD / IADF_BACKWARD   (also: menu cursor Y)
//   Left stick X       -> IADF_STRAFE_LT / IADF_STRAFE_RT (also: menu cursor X)
//   Right stick X      -> IADF_TURN_LT / IADF_TURN_RT  (look horizontal)
//   Right stick Y      -> IADF_LOOK_UP / IADF_LOOK_DN   (look vertical)
//   Right trigger      -> IADF_PRIMARY_FIRE
//   Left trigger       -> IADF_SECONDARY_FIRE
//   A                  -> IADF_JUMP   (also: MBUTTON_LEFT for menu clicks)
//   B                  -> IADF_CROUCH
//   X                  -> IADF_USE
//   Start              -> IADF_PAUSE / IADF_MENU_TOGGLE
//   Back               -> IADF_AUTOMAP
//   Left shoulder      -> IADF_CYCLEWPN_PREV
//   Right shoulder     -> IADF_CYCLEWPN_NEXT
//   D-pad right        -> IADF_HEAD_LAMP_TOGGLE
//   D-pad up           -> IADF_CLEATS_TOGGLE
//   D-pad left         -> IADF_NIGHT_VISION_TOG
//   D-pad down         -> IADF_GAS_MASK_TOGGLE
//   Left stick click   -> IADF_RUN toggle
//
// Menu cursor synthesis: the Landru / agent / escape / mission-briefing
// menus are mouse-driven. They call TFE_Input::getMousePos() and read
// MBUTTON_LEFT presses. We integrate the left stick into a cursor
// position state each frame and push it via setMousePos(), and we mirror
// the A button onto MBUTTON_LEFT for "click." This is harmless in-game:
// the menu mouse path isn't read during gameplay, so the same stick can
// drive both walking and menu navigation depending on context.
//
// The existing TFE_Input state arrays are populated so all higher-level
// inputMapping logic works without modification.

#include <TFE_Input/input.h>
#include <TFE_Input/input_xbox.h>
#include <TFE_Input/inputEnum.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/system.h>
#include <xtl.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Deadzone threshold (0-32767)
// ---------------------------------------------------------------------------
#define XINPUT_DEADZONE_STICK    8000

// ---------------------------------------------------------------------
// Right-stick look tuning.
//
//   XBOX_LOOK_ANALOG     1 = smaller deadzone + quadratic curve so
//                            small stick pushes give small look speeds
//                            and the full range is usable.
//                        0 = original 8000 deadzone + linear ramp,
//                            which feels stepped because crossing the
//                            deadzone snaps to a non-trivial speed.
// ---------------------------------------------------------------------
#define XBOX_LOOK_ANALOG         1
#define XINPUT_DEADZONE_TRIGGER  30      // out of 255

// ---------------------------------------------------------------------------
// Mouse cursor synthesis (for menus)
// ---------------------------------------------------------------------------
// Output screen size in pixels. Must match renderBackend_xbox.cpp's
// XBOX_OUTPUT_WIDTH / XBOX_OUTPUT_HEIGHT. The menu code reads mouse
// position in screen-space (it rescales internally using displayInfo).
#define XBOX_CURSOR_MAX_X        639
#define XBOX_CURSOR_MAX_Y        479

// Pixels-per-frame of cursor travel at full stick deflection. At 60 fps
// this is ~360 px/s, which crosses the full screen in <2 seconds - quick
// enough to feel responsive, slow enough to land on small buttons.
#define XBOX_CURSOR_SPEED        6.0f

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

namespace TFE_InputXbox
{
    static XINPUT_STATE s_prevState;
    static bool         s_prevStateValid     = false;
    static bool         s_loggedConnected    = false;
    static bool         s_loggedDisconnected = false;

    // XInput on Xbox needs a HANDLE from XInputOpen as the first arg to
    // XInputGetState - NOT a port index (that's the PC XInput signature).
    // Calling XInputGetState(0, ...) on Xbox passes NULL as the device
    // handle; CXBX-R's HLE then dereferences it and crashes.
    //
    // Lazy-open the controller on first poll: USB enumeration may not be
    // complete at init() time, so deferring until the first game-loop tick
    // is the established Xbox idiom (matches xquake / OpenJKDF2's
    // stdControl_xbox.c).
    static HANDLE s_hController       = NULL;
    static int    s_openRetryCounter  = 0;   // throttle retries to ~1/sec @ 60fps
    static int    s_pollCounter       = 0;   // total polls since startup
    static int    s_edgeLogBudget     = 0;   // edge-event logs stall hardware menus; enable only while bringing input up.
    static f32    s_lookSensitivityX  = 1.0f;
    static f32    s_lookSensitivityY  = 1.0f;
    static f32    s_leftStickDeadzone = 0.09f;
    static f32    s_rightStickDeadzone = 0.09f;
    static bool   s_invertLookY = false;

    // Synthesized mouse cursor state (screen-space, accumulated across frames).
    // Initialized to screen center on init().
    static f32    s_cursorX         = 320.0f;
    static f32    s_cursorY         = 240.0f;

    // Longplay trace controls. Drop tfe_input_record.txt next to the XBE to
    // write UDATA\xbox_input_trace.bin. Drop tfe_input_playback.txt next to
    // the XBE to feed that trace back through this same input mapper.
    #define XBOX_INPUT_TRACE_FILE      "xbox_input_trace.bin"
    #define XBOX_INPUT_RECORD_MARKER   "tfe_input_record.txt"
    #define XBOX_INPUT_PLAYBACK_MARKER "tfe_input_playback.txt"
    #define XBOX_INPUT_TRACE_VERSION   1
    #define XBOX_INPUT_TRACE_HEARTBEAT 1200

    enum XboxInputTraceMode
    {
        TRACE_OFF = 0,
        TRACE_RECORD,
        TRACE_PLAYBACK
    };

#pragma pack(push, 1)
    struct XboxInputTraceHeader
    {
        char  magic[8];
        u32   version;
        u32   frameCount;
        u32   reserved;
    };

    struct XboxInputTraceFrame
    {
        DWORD packetNumber;
        WORD  buttons;
        BYTE  analogButtons[8];
        SHORT thumbLX;
        SHORT thumbLY;
        SHORT thumbRX;
        SHORT thumbRY;
    };
#pragma pack(pop)

    static const char       c_traceMagic[8] = { 'T', 'F', 'E', 'X', 'I', 'N', 'P', '1' };
    static XboxInputTraceMode s_traceMode = TRACE_OFF;
    static HANDLE          s_traceFile = INVALID_HANDLE_VALUE;
    static u32             s_traceFramesWritten = 0;
    static u32             s_traceFramesRead = 0;
    static u32             s_traceFrameLimit = 0;
    static bool            s_traceEofLogged = false;
    static char            s_tracePath[TFE_MAX_PATH];

    // On the original Xbox, digital buttons (D-pad, Start, Back, thumbsticks)
    // are bitmask flags in wButtons.  Face buttons (A/B/X/Y) and Black/White
    // are analog values in bAnalogButtons[index].
    // We map White -> LeftShoulder and Black -> RightShoulder since the
    // original Xbox has no shoulder bumpers.

    // Digital buttons: bitmask in wButtons.
    struct DigitalButtonMap { WORD xinputBit; Button tfeButton; };
    static const DigitalButtonMap c_digitalMap[] =
    {
        { XINPUT_GAMEPAD_BACK,           CONTROLLER_BUTTON_BACK          },
        { XINPUT_GAMEPAD_START,          CONTROLLER_BUTTON_START         },
        { XINPUT_GAMEPAD_LEFT_THUMB,     CONTROLLER_BUTTON_LEFTSTICK     },
        { XINPUT_GAMEPAD_RIGHT_THUMB,    CONTROLLER_BUTTON_RIGHTSTICK    },
        { XINPUT_GAMEPAD_DPAD_UP,        CONTROLLER_BUTTON_DPAD_UP       },
        { XINPUT_GAMEPAD_DPAD_DOWN,      CONTROLLER_BUTTON_DPAD_DOWN     },
        { XINPUT_GAMEPAD_DPAD_LEFT,      CONTROLLER_BUTTON_DPAD_LEFT     },
        { XINPUT_GAMEPAD_DPAD_RIGHT,     CONTROLLER_BUTTON_DPAD_RIGHT    },
#ifdef XINPUT_GAMEPAD_LEFT_SHOULDER
        { XINPUT_GAMEPAD_LEFT_SHOULDER,  CONTROLLER_BUTTON_LEFTSHOULDER  },
#endif
#ifdef XINPUT_GAMEPAD_RIGHT_SHOULDER
        { XINPUT_GAMEPAD_RIGHT_SHOULDER, CONTROLLER_BUTTON_RIGHTSHOULDER },
#endif
    };
    static const int c_digitalMapCount = sizeof(c_digitalMap) / sizeof(c_digitalMap[0]);

    // Analog buttons: index into bAnalogButtons[]. Pressed if value > threshold.
    #define ANALOG_BUTTON_THRESHOLD 30
    struct AnalogButtonMap { int analogIndex; Button tfeButton; };
    static const AnalogButtonMap c_analogMap[] =
    {
        { XINPUT_GAMEPAD_A,              CONTROLLER_BUTTON_A             },
        { XINPUT_GAMEPAD_B,              CONTROLLER_BUTTON_B             },
        { XINPUT_GAMEPAD_X,              CONTROLLER_BUTTON_X             },
        { XINPUT_GAMEPAD_Y,              CONTROLLER_BUTTON_Y             },
        { XINPUT_GAMEPAD_WHITE,          CONTROLLER_BUTTON_LEFTSHOULDER  },  // White -> LB
        { XINPUT_GAMEPAD_BLACK,          CONTROLLER_BUTTON_RIGHTSHOULDER },  // Black -> RB
    };
    static const int c_analogMapCount = sizeof(c_analogMap) / sizeof(c_analogMap[0]);

    static inline f32 applyDeadzone(SHORT raw, SHORT deadzone)
    {
        if (raw > deadzone)
            return f32(raw - deadzone) / f32(32767 - deadzone);
        if (raw < -deadzone)
            return f32(raw + deadzone) / f32(32767 - deadzone);
        return 0.0f;
    }

    static void buildTracePath(TFE_PathType pathType, const char* fileName, char* outPath)
    {
        const char* base = TFE_Paths::getPath(pathType);
        if (!base) base = "";
        snprintf(outPath, TFE_MAX_PATH, "%s%s", base, fileName);
        outPath[TFE_MAX_PATH - 1] = 0;
    }

    static bool traceFileExists(const char* path)
    {
        if (!path || !path[0]) return false;
        DWORD attr = GetFileAttributesA(path);
        return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    static void closeTraceFile()
    {
        if (s_traceFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(s_traceFile);
            s_traceFile = INVALID_HANDLE_VALUE;
        }
    }

    static void shutdownTrace()
    {
        if (s_traceMode == TRACE_RECORD && s_traceFile != INVALID_HANDLE_VALUE)
        {
            DWORD bytesWritten = 0;
            SetFilePointer(s_traceFile, 12, NULL, FILE_BEGIN);
            WriteFile(s_traceFile, &s_traceFramesWritten, sizeof(s_traceFramesWritten), &bytesWritten, NULL);
            TFE_XboxLogf("InputTrace", "record stop frames=%lu path=%s",
                (unsigned long)s_traceFramesWritten, s_tracePath);
        }
        else if (s_traceMode == TRACE_PLAYBACK)
        {
            TFE_XboxLogf("InputTrace", "playback stop framesRead=%lu frameLimit=%lu path=%s",
                (unsigned long)s_traceFramesRead, (unsigned long)s_traceFrameLimit, s_tracePath);
        }

        closeTraceFile();
        s_traceMode = TRACE_OFF;
        s_traceFramesWritten = 0;
        s_traceFramesRead = 0;
        s_traceFrameLimit = 0;
        s_traceEofLogged = false;
        s_tracePath[0] = 0;
    }

    static bool openTracePlaybackFile(const char* path)
    {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        XboxInputTraceHeader header;
        DWORD bytesRead = 0;
        memset(&header, 0, sizeof(header));
        if (!ReadFile(file, &header, sizeof(header), &bytesRead, NULL) ||
            bytesRead != sizeof(header) ||
            memcmp(header.magic, c_traceMagic, sizeof(c_traceMagic)) != 0 ||
            header.version != XBOX_INPUT_TRACE_VERSION)
        {
            TFE_XboxLogf("InputTrace", "invalid playback file path=%s bytes=%lu version=%lu",
                path, (unsigned long)bytesRead, (unsigned long)header.version);
            CloseHandle(file);
            return false;
        }

        s_traceFile = file;
        s_traceFrameLimit = header.frameCount;
        s_traceFramesRead = 0;
        s_traceMode = TRACE_PLAYBACK;
        strncpy(s_tracePath, path, TFE_MAX_PATH - 1);
        s_tracePath[TFE_MAX_PATH - 1] = 0;
        TFE_XboxLogf("InputTrace", "playback start frames=%lu mode=raw-xinput-per-frame path=%s",
            (unsigned long)s_traceFrameLimit, s_tracePath);
        return true;
    }

    static void initTrace()
    {
        char recordMarker[TFE_MAX_PATH];
        char playbackMarker[TFE_MAX_PATH];
        char path[TFE_MAX_PATH];
        buildTracePath(PATH_PROGRAM, XBOX_INPUT_RECORD_MARKER, recordMarker);
        buildTracePath(PATH_PROGRAM, XBOX_INPUT_PLAYBACK_MARKER, playbackMarker);

        const bool recordRequested = traceFileExists(recordMarker);
        const bool playbackRequested = traceFileExists(playbackMarker);
        if (recordRequested && playbackRequested)
        {
            TFE_XboxLogf("InputTrace", "both markers present; playback wins");
        }

        if (playbackRequested)
        {
            buildTracePath(PATH_PROGRAM, XBOX_INPUT_TRACE_FILE, path);
            if (openTracePlaybackFile(path))
            {
                return;
            }

            buildTracePath(PATH_USER_DOCUMENTS, XBOX_INPUT_TRACE_FILE, path);
            if (openTracePlaybackFile(path))
            {
                return;
            }

            TFE_XboxLogf("InputTrace", "playback requested but %s not found beside XBE or in UDATA",
                XBOX_INPUT_TRACE_FILE);
            return;
        }

        if (!recordRequested)
        {
            return;
        }

        buildTracePath(PATH_USER_DOCUMENTS, XBOX_INPUT_TRACE_FILE, path);
        HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
        {
            TFE_XboxLogf("InputTrace", "record open failed path=%s err=%lu", path, GetLastError());
            return;
        }

        XboxInputTraceHeader header;
        DWORD bytesWritten = 0;
        memset(&header, 0, sizeof(header));
        memcpy(header.magic, c_traceMagic, sizeof(c_traceMagic));
        header.version = XBOX_INPUT_TRACE_VERSION;
        header.frameCount = 0;
        header.reserved = 0;
        if (!WriteFile(file, &header, sizeof(header), &bytesWritten, NULL) || bytesWritten != sizeof(header))
        {
            TFE_XboxLogf("InputTrace", "record header write failed path=%s err=%lu", path, GetLastError());
            CloseHandle(file);
            return;
        }

        s_traceFile = file;
        s_traceMode = TRACE_RECORD;
        s_traceFramesWritten = 0;
        strncpy(s_tracePath, path, TFE_MAX_PATH - 1);
        s_tracePath[TFE_MAX_PATH - 1] = 0;
        TFE_XboxLogf("InputTrace", "record start mode=raw-xinput-per-frame path=%s", s_tracePath);
    }

    static void writeTraceFrame(const XINPUT_STATE& state)
    {
        if (s_traceMode != TRACE_RECORD || s_traceFile == INVALID_HANDLE_VALUE)
        {
            return;
        }

        XboxInputTraceFrame frame;
        DWORD bytesWritten = 0;
        memset(&frame, 0, sizeof(frame));
        frame.packetNumber = state.dwPacketNumber;
        frame.buttons = state.Gamepad.wButtons;
        memcpy(frame.analogButtons, state.Gamepad.bAnalogButtons, sizeof(frame.analogButtons));
        frame.thumbLX = state.Gamepad.sThumbLX;
        frame.thumbLY = state.Gamepad.sThumbLY;
        frame.thumbRX = state.Gamepad.sThumbRX;
        frame.thumbRY = state.Gamepad.sThumbRY;

        if (!WriteFile(s_traceFile, &frame, sizeof(frame), &bytesWritten, NULL) || bytesWritten != sizeof(frame))
        {
            TFE_XboxLogf("InputTrace", "record frame write failed frame=%lu err=%lu",
                (unsigned long)s_traceFramesWritten, GetLastError());
            closeTraceFile();
            s_traceMode = TRACE_OFF;
            return;
        }

        s_traceFramesWritten++;
        if ((s_traceFramesWritten % XBOX_INPUT_TRACE_HEARTBEAT) == 0)
        {
            TFE_XboxLogf("InputTrace", "record status all-input-frames-captured=%lu seconds=%lu path=%s",
                (unsigned long)s_traceFramesWritten,
                (unsigned long)(s_traceFramesWritten / 60),
                s_tracePath);
        }
    }

    static bool readTraceFrame(XINPUT_STATE* state)
    {
        if (s_traceMode != TRACE_PLAYBACK || s_traceFile == INVALID_HANDLE_VALUE || !state)
        {
            return false;
        }

        if (s_traceFrameLimit > 0 && s_traceFramesRead >= s_traceFrameLimit)
        {
            if (!s_traceEofLogged)
            {
                TFE_XboxLogf("InputTrace", "playback reached frameLimit=%lu",
                    (unsigned long)s_traceFrameLimit);
                s_traceEofLogged = true;
            }
            return false;
        }

        XboxInputTraceFrame frame;
        DWORD bytesRead = 0;
        memset(&frame, 0, sizeof(frame));
        if (!ReadFile(s_traceFile, &frame, sizeof(frame), &bytesRead, NULL) || bytesRead != sizeof(frame))
        {
            if (!s_traceEofLogged)
            {
                TFE_XboxLogf("InputTrace", "playback eof/short read framesRead=%lu bytes=%lu err=%lu",
                    (unsigned long)s_traceFramesRead, (unsigned long)bytesRead, GetLastError());
                s_traceEofLogged = true;
            }
            return false;
        }

        memset(state, 0, sizeof(*state));
        state->dwPacketNumber = frame.packetNumber;
        state->Gamepad.wButtons = frame.buttons;
        memcpy(state->Gamepad.bAnalogButtons, frame.analogButtons, sizeof(frame.analogButtons));
        state->Gamepad.sThumbLX = frame.thumbLX;
        state->Gamepad.sThumbLY = frame.thumbLY;
        state->Gamepad.sThumbRX = frame.thumbRX;
        state->Gamepad.sThumbRY = frame.thumbRY;

        s_traceFramesRead++;
        if ((s_traceFramesRead % XBOX_INPUT_TRACE_HEARTBEAT) == 0)
        {
            TFE_XboxLogf("InputTrace", "playback status all-input-frames-fed=%lu seconds=%lu path=%s",
                (unsigned long)s_traceFramesRead,
                (unsigned long)(s_traceFramesRead / 60),
                s_tracePath);
        }
        return true;
    }

    void init()
    {
        memset(&s_prevState, 0, sizeof(s_prevState));
        s_prevStateValid = false;
        s_loggedConnected = false;
        s_loggedDisconnected = false;
        s_cursorX = 320.0f;
        s_cursorY = 240.0f;
        TFE_Input::setMousePos((s32)s_cursorX, (s32)s_cursorY);
        TFE_System::logWrite(LOG_MSG, "InputXbox", "XInput polling initialised");
        TFE_XboxLogf("InputXbox", "init sensitivityX=%d sensitivityY=%d rightDeadzonePct=%d invertY=%d trigger=%d",
            (int)(s_lookSensitivityX * 100.0f), (int)(s_lookSensitivityY * 100.0f),
            (int)(s_rightStickDeadzone * 100.0f), s_invertLookY ? 1 : 0, XINPUT_DEADZONE_TRIGGER);
        initTrace();
    }

    // Helper: clear all input state when no controller is connected.
    static void clearAllInput()
    {
        TFE_Input::setAxis(AXIS_LEFT_X,        0.0f);
        TFE_Input::setAxis(AXIS_LEFT_Y,        0.0f);
        TFE_Input::setAxis(AXIS_RIGHT_X,       0.0f);
        TFE_Input::setAxis(AXIS_RIGHT_Y,       0.0f);
        TFE_Input::setAxis(AXIS_LEFT_TRIGGER,  0.0f);
        TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, 0.0f);
        for (int i = 0; i < c_digitalMapCount; i++)
            TFE_Input::setButtonUp(c_digitalMap[i].tfeButton);
        for (int i = 0; i < c_analogMapCount; i++)
            TFE_Input::setButtonUp(c_analogMap[i].tfeButton);
        TFE_Input::setMouseButtonUp(MBUTTON_LEFT);
        s_prevStateValid = false;
    }

    void pollInput()
    {
        s_pollCounter++;

        XINPUT_STATE state;
        memset(&state, 0, sizeof(state));

        if (s_traceMode == TRACE_PLAYBACK)
        {
            if (!readTraceFrame(&state))
            {
                if (s_prevStateValid) clearAllInput();
                return;
            }
        }
        else
        {
            // Lazy XInputOpen. XInitDevices kicks off USB enumeration
            // asynchronously, so the very first poll can race the host stack and
            // see no devices. Re-poll XGetDevices each frame (cheap), and
            // XInputOpen once a device appears on PORT0.
            if (s_hController == NULL)
            {
                DWORD mask = XGetDevices(XDEVICE_TYPE_GAMEPAD);
                if (mask & XDEVICE_PORT0_MASK)
                {
                    TFE_XboxLogf("InputXbox", "XGetDevices mask=0x%lx - opening port 0", mask);
                    s_hController = XInputOpen(XDEVICE_TYPE_GAMEPAD,
                                               XDEVICE_PORT0,
                                               XDEVICE_NO_SLOT,
                                               NULL);
                    TFE_XboxLogf("InputXbox", "XInputOpen returned handle=%p", s_hController);
                }
                else if ((s_openRetryCounter++ % 60) == 0)
                {
                    // Log once per second while waiting.
                    TFE_XboxLogf("InputXbox", "no gamepad on PORT0 (mask=0x%lx)", mask);
                }
            }

            if (s_hController == NULL)
            {
                // No controller. Clear state once, then run silent.
                if (s_prevStateValid) clearAllInput();
                return;
            }

            const DWORD result = XInputGetState(s_hController, &state);
            if (result != ERROR_SUCCESS)
            {
                if (!s_loggedDisconnected)
                {
                    TFE_XboxLogf("InputXbox", "controller disconnected result=%lu", result);
                    s_loggedDisconnected = true;
                    s_loggedConnected    = false;
                }
                // Close and forget the handle so a re-plug can be picked up if
                // the user wiggles the cord. We don't retry XInputOpen here
                // because it can block.
                XInputClose(s_hController);
                s_hController      = NULL;
                s_openRetryCounter = 0;
                if (s_prevStateValid) clearAllInput();
                return;
            }
            if (!s_loggedConnected)
            {
                TFE_XboxLogf("InputXbox", "controller connected packet=%lu", state.dwPacketNumber);
                s_loggedConnected    = true;
                s_loggedDisconnected = false;
            }

            writeTraceFrame(state);
        }

        const XINPUT_GAMEPAD& pad  = state.Gamepad;
        const XINPUT_GAMEPAD& prev = s_prevState.Gamepad;

        // ---------------------------------------------------------------
        // Analog sticks
        // ---------------------------------------------------------------
        SHORT leftStickDeadzone = (SHORT)(s_leftStickDeadzone * 32767.0f);
        if (leftStickDeadzone < 0) leftStickDeadzone = 0;
        if (leftStickDeadzone > 30000) leftStickDeadzone = 30000;
        SHORT rightStickDeadzone = (SHORT)(s_rightStickDeadzone * 32767.0f);
        if (rightStickDeadzone < 0) rightStickDeadzone = 0;
        if (rightStickDeadzone > 30000) rightStickDeadzone = 30000;

        const f32 lx = applyDeadzone(pad.sThumbLX,  leftStickDeadzone);
        const f32 ly = applyDeadzone(pad.sThumbLY,  leftStickDeadzone);
        TFE_Input::setAxis(AXIS_LEFT_X,  lx);
        TFE_Input::setAxis(AXIS_LEFT_Y,  ly);
        // Right stick - look. Deadzone + curve controlled by the
        // XBOX_LOOK_* toggles above. Old behavior is kept available
        // by flipping the toggles to 0.
#if XBOX_LOOK_ANALOG
        // Smaller deadzone + quadratic curve. sign(x)*x*x keeps full
        // range [-1,1] but compresses small inputs so a gentle nudge
        // gives a gentle look-speed.
        f32 rx = applyDeadzone(pad.sThumbRX, rightStickDeadzone);
        f32 ry = applyDeadzone(pad.sThumbRY, rightStickDeadzone);
        rx = (rx < 0.0f ? -1.0f : 1.0f) * rx * rx;
        ry = (ry < 0.0f ? -1.0f : 1.0f) * ry * ry;
#else
        // Original Phase 11 behavior: big deadzone, linear ramp.
        f32 rx = applyDeadzone(pad.sThumbRX, XINPUT_DEADZONE_STICK);
        f32 ry = applyDeadzone(pad.sThumbRY, XINPUT_DEADZONE_STICK);
#endif
        TFE_Input::setAxis(AXIS_RIGHT_X, rx * s_lookSensitivityX);
        // Default: stick UP -> look UP. Invert Y flips only the vertical
        // look direction, leaving menu cursor and movement behavior alone.
        if (s_invertLookY) ry = -ry;
        TFE_Input::setAxis(AXIS_RIGHT_Y, ry * s_lookSensitivityY);

        // ---------------------------------------------------------------
        // Synthesized mouse cursor from left stick.
        // Menu code reads mouse position via TFE_Input::getMousePos() and
        // rescales screen-space -> 320x200 internally; we feed screen
        // pixels. Stick Y is positive-up on Xbox, screen Y is positive-
        // down, so subtract ly. Clamp to screen extent.
        // ---------------------------------------------------------------
        s_cursorX += lx * XBOX_CURSOR_SPEED;
        s_cursorY -= ly * XBOX_CURSOR_SPEED;
        if (s_cursorX < 0.0f)                       s_cursorX = 0.0f;
        if (s_cursorX > (f32)XBOX_CURSOR_MAX_X)     s_cursorX = (f32)XBOX_CURSOR_MAX_X;
        if (s_cursorY < 0.0f)                       s_cursorY = 0.0f;
        if (s_cursorY > (f32)XBOX_CURSOR_MAX_Y)     s_cursorY = (f32)XBOX_CURSOR_MAX_Y;
        TFE_Input::setMousePos((s32)s_cursorX, (s32)s_cursorY);

        // ---------------------------------------------------------------
        // Triggers (analog buttons on original Xbox)
        // ---------------------------------------------------------------
        BYTE leftTrigger  = pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER];
        BYTE rightTrigger = pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER];

        if (leftTrigger > XINPUT_DEADZONE_TRIGGER)
            TFE_Input::setAxis(AXIS_LEFT_TRIGGER,  f32(leftTrigger)  / 255.0f);
        else
            TFE_Input::setAxis(AXIS_LEFT_TRIGGER,  0.0f);

        if (rightTrigger > XINPUT_DEADZONE_TRIGGER)
            TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, f32(rightTrigger) / 255.0f);
        else
            TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, 0.0f);

        // ---------------------------------------------------------------
        // Digital buttons (wButtons bitmask) - compare against previous state
        // ---------------------------------------------------------------
        for (int i = 0; i < c_digitalMapCount; i++)
        {
            const WORD bit = c_digitalMap[i].xinputBit;
            const bool nowDown  = (pad.wButtons  & bit) != 0;
            const bool prevDown = s_prevStateValid && ((prev.wButtons & bit) != 0);

            if (nowDown && !prevDown)
            {
                TFE_Input::setButtonDown(c_digitalMap[i].tfeButton);
                if (s_edgeLogBudget > 0) {
                    TFE_XboxLogf("InputXbox", "digital DOWN bit=0x%04x tfeBtn=%d",
                        (unsigned)bit, (int)c_digitalMap[i].tfeButton);
                    s_edgeLogBudget--;
                }
            }
            else if (!nowDown && prevDown)
            {
                TFE_Input::setButtonUp(c_digitalMap[i].tfeButton);
                if (s_edgeLogBudget > 0) {
                    TFE_XboxLogf("InputXbox", "digital UP   bit=0x%04x tfeBtn=%d",
                        (unsigned)bit, (int)c_digitalMap[i].tfeButton);
                    s_edgeLogBudget--;
                }
            }
        }

        // ---------------------------------------------------------------
        // Analog buttons (bAnalogButtons[]) - compare against threshold
        // ---------------------------------------------------------------
        for (int i = 0; i < c_analogMapCount; i++)
        {
            const int idx = c_analogMap[i].analogIndex;
            const bool nowDown  = pad.bAnalogButtons[idx] > ANALOG_BUTTON_THRESHOLD;
            const bool prevDown = s_prevStateValid &&
                                  (prev.bAnalogButtons[idx] > ANALOG_BUTTON_THRESHOLD);

            if (nowDown && !prevDown)
            {
                TFE_Input::setButtonDown(c_analogMap[i].tfeButton);
                if (s_edgeLogBudget > 0) {
                    TFE_XboxLogf("InputXbox", "analog DOWN idx=%d tfeBtn=%d val=%d",
                        idx, (int)c_analogMap[i].tfeButton, (int)pad.bAnalogButtons[idx]);
                    s_edgeLogBudget--;
                }
            }
            else if (!nowDown && prevDown)
            {
                TFE_Input::setButtonUp(c_analogMap[i].tfeButton);
                if (s_edgeLogBudget > 0) {
                    TFE_XboxLogf("InputXbox", "analog UP   idx=%d tfeBtn=%d",
                        idx, (int)c_analogMap[i].tfeButton);
                    s_edgeLogBudget--;
                }
            }
        }

        // ---------------------------------------------------------------
        // Mirror A onto MBUTTON_LEFT for menu clicks. The Landru menu
        // checks TFE_Input::mousePressed(MBUTTON_LEFT) every frame, so
        // we need clean edge detection on the analog-A value.
        // ---------------------------------------------------------------
        {
            const bool aNow  = pad.bAnalogButtons[XINPUT_GAMEPAD_A] > ANALOG_BUTTON_THRESHOLD;
            const bool aPrev = s_prevStateValid &&
                               (prev.bAnalogButtons[XINPUT_GAMEPAD_A] > ANALOG_BUTTON_THRESHOLD);
            if (aNow && !aPrev)       TFE_Input::setMouseButtonDown(MBUTTON_LEFT);
            else if (!aNow && aPrev)  TFE_Input::setMouseButtonUp(MBUTTON_LEFT);
        }

        s_prevState      = state;
        s_prevStateValid = true;
    }

    // Call when the game loop exits.
    void shutdown()
    {
        TFE_XboxLogf("InputXbox", "shutdown handle=%p", s_hController);
        shutdownTrace();
        if (s_hController != NULL)
        {
            XInputClose(s_hController);
            s_hController = NULL;
        }
        s_openRetryCounter = 0;
        s_prevStateValid   = false;
    }

    void setLookSensitivity(float value)
    {
        setLookSensitivityX(value);
        setLookSensitivityY(value);
    }

    float getLookSensitivity()
    {
        return (s_lookSensitivityX + s_lookSensitivityY) * 0.5f;
    }

    void setLookSensitivityX(float value)
    {
        if (value < 0.25f) value = 0.25f;
        if (value > 2.5f) value = 2.5f;
        s_lookSensitivityX = value;
    }

    void setLookSensitivityY(float value)
    {
        if (value < 0.25f) value = 0.25f;
        if (value > 2.5f) value = 2.5f;
        s_lookSensitivityY = value;
    }

    float getLookSensitivityX()
    {
        return s_lookSensitivityX;
    }

    float getLookSensitivityY()
    {
        return s_lookSensitivityY;
    }

    void setStickDeadzone(float value)
    {
        setRightStickDeadzone(value);
    }

    float getStickDeadzone()
    {
        return getRightStickDeadzone();
    }

    void setRightStickDeadzone(float value)
    {
        if (value < 0.0f) value = 0.0f;
        if (value > 0.30f) value = 0.30f;
        s_rightStickDeadzone = value;
    }

    float getRightStickDeadzone()
    {
        return s_rightStickDeadzone;
    }

    void setInvertLookY(bool enabled)
    {
        s_invertLookY = enabled;
    }

    bool getInvertLookY()
    {
        return s_invertLookY;
    }

} // namespace TFE_InputXbox
