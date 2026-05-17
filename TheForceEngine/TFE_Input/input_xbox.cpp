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
//   B                  -> IADF_RUN (sprint)
//   X                  -> IADF_USE
//   Y                  -> IADF_CROUCH
//   Start              -> IADF_PAUSE / IADF_MENU_TOGGLE
//   Back               -> IADF_AUTOMAP
//   Left shoulder      -> IADF_CYCLEWPN_PREV
//   Right shoulder     -> IADF_CYCLEWPN_NEXT
//   D-pad up/down      -> IADF_INC_SCREENSIZE / IADF_DEC_SCREENSIZE
//   Left stick click   -> IADF_CENTER_VIEW
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
#include <TFE_Input/inputEnum.h>
#include <TFE_System/system.h>
#include <xtl.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Deadzone threshold (0-32767)
// ---------------------------------------------------------------------------
#define XINPUT_DEADZONE_STICK    8000

// ---------------------------------------------------------------------
// Right-stick look tuning (Phase 13 staging - rollback toggles).
// Flip either to 0 to fall back to the pre-Phase-12 behaviour.
//
//   XBOX_LOOK_Y_INVERT   1 = pushing the stick UP makes the camera
//                            LOOK UP (typical console FPS, also matches
//                            mouse convention on Xbox).
//                        0 = pushing UP makes the camera LOOK DOWN
//                            (the original Phase 11 behaviour - it was
//                            flipping the raw stick Y to compensate
//                            for a different convention elsewhere).
//
//   XBOX_LOOK_ANALOG     1 = smaller deadzone + quadratic curve so
//                            small stick pushes give small look speeds
//                            and the full range is usable.
//                        0 = original 8000 deadzone + linear ramp,
//                            which feels stepped because crossing the
//                            deadzone snaps to a non-trivial speed.
// ---------------------------------------------------------------------
#define XBOX_LOOK_Y_INVERT       1
#define XBOX_LOOK_ANALOG         1
#define XBOX_LOOK_DEADZONE       3000  // used only when XBOX_LOOK_ANALOG=1
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
    static int    s_edgeLogBudget     = 64;  // limit edge-event log spam

    // Synthesized mouse cursor state (screen-space, accumulated across frames).
    // Initialized to screen center on init().
    static f32    s_cursorX         = 320.0f;
    static f32    s_cursorY         = 240.0f;

    // On the original Xbox, digital buttons (D-pad, Start, Back, thumbsticks)
    // are bitmask flags in wButtons.  Face buttons (A/B/X/Y) and Black/White
    // are analog values in bAnalogButtons[index].
    // We map Black -> LeftShoulder and White -> RightShoulder since the
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
        { XINPUT_GAMEPAD_BLACK,          CONTROLLER_BUTTON_LEFTSHOULDER  },  // Black -> LB
        { XINPUT_GAMEPAD_WHITE,          CONTROLLER_BUTTON_RIGHTSHOULDER },  // White -> RB
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
        TFE_XboxLogf("InputXbox", "init deadzoneStick=%d deadzoneTrigger=%d",
            XINPUT_DEADZONE_STICK, XINPUT_DEADZONE_TRIGGER);
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

        XINPUT_STATE state;
        memset(&state, 0, sizeof(state));

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

        const XINPUT_GAMEPAD& pad  = state.Gamepad;
        const XINPUT_GAMEPAD& prev = s_prevState.Gamepad;

        // ---------------------------------------------------------------
        // Analog sticks
        // ---------------------------------------------------------------
        const f32 lx = applyDeadzone(pad.sThumbLX,  XINPUT_DEADZONE_STICK);
        const f32 ly = applyDeadzone(pad.sThumbLY,  XINPUT_DEADZONE_STICK);
        TFE_Input::setAxis(AXIS_LEFT_X,  lx);
        TFE_Input::setAxis(AXIS_LEFT_Y,  ly);
        // Right stick - look. Deadzone + curve controlled by the
        // XBOX_LOOK_* toggles above. Old behavior is kept available
        // by flipping the toggles to 0.
#if XBOX_LOOK_ANALOG
        // Smaller deadzone + quadratic curve. sign(x)*x*x keeps full
        // range [-1,1] but compresses small inputs so a gentle nudge
        // gives a gentle look-speed.
        f32 rx = applyDeadzone(pad.sThumbRX, XBOX_LOOK_DEADZONE);
        f32 ry = applyDeadzone(pad.sThumbRY, XBOX_LOOK_DEADZONE);
        rx = (rx < 0.0f ? -1.0f : 1.0f) * rx * rx;
        ry = (ry < 0.0f ? -1.0f : 1.0f) * ry * ry;
#else
        // Original Phase 11 behavior: big deadzone, linear ramp.
        f32 rx = applyDeadzone(pad.sThumbRX, XINPUT_DEADZONE_STICK);
        f32 ry = applyDeadzone(pad.sThumbRY, XINPUT_DEADZONE_STICK);
#endif
        TFE_Input::setAxis(AXIS_RIGHT_X, rx);
#if XBOX_LOOK_Y_INVERT
        // Stick UP -> look UP. Raw XInput RY is +ve when pushed up,
        // and player.cpp adds AA_LOOK_VERT to s_playerPitch where +ve
        // pitch = look up - so DON'T negate.
        TFE_Input::setAxis(AXIS_RIGHT_Y, ry);
#else
        // Pre-Phase-12 behavior: negate.
        TFE_Input::setAxis(AXIS_RIGHT_Y, -ry);
#endif

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
        if (s_hController != NULL)
        {
            XInputClose(s_hController);
            s_hController = NULL;
        }
        s_openRetryCounter = 0;
        s_prevStateValid   = false;
    }

} // namespace TFE_InputXbox
