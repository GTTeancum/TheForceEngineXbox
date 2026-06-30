#include <cstring>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

#include "inputMapping.h"
#include <TFE_Game/igame.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/replay.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/player.h>
#include <TFE_DarkForces/GameUI/pda.h>

// ---------------------------------------------------------------------------
// Xbox: mouse state is fed by input_xbox.cpp via TFE_Input::setRelativeMousePos
// and TFE_Input::setMousePos each frame, so we read it back from those values.
// SDL_GetRelativeMouseState / SDL_GetMouseState are not available.
// ---------------------------------------------------------------------------

namespace TFE_Input
{
	enum InputVersion
	{
		INPUT_INIT_VER      = 0x00010000,
		INPUT_ADD_QUICKSAVE = 0x00020001,
		INPUT_ADD_DEADZONE  = 0x00020002,
		INPUT_ADD_HIGH_DEF  = 0x00020003,
		INPUT_DEMO_CONFIG   = 0x00020004,
		INPUT_CUR_VERSION   = INPUT_DEMO_CONFIG
	};

	static const char* c_inputRemappingName    = "tfe_input_remapping.bin";
	static const char  c_inputRemappingHdr[4]  = { 'T', 'F', 'E', 0 };
	static const u32   c_inputRemappingVersion = INPUT_CUR_VERSION;

	// -----------------------------------------------------------------------
	// Default bindings
	// Note: MSVC 2005 (C++03) does not support = value in struct initialisers
	// inside aggregate initialisers.  All KeyMod fields that were defaulted to
	// KEYMOD_NONE are made explicit here.
	// -----------------------------------------------------------------------
	static InputBinding s_defaultKeyboardBinds[] =
	{
		// System
		{ IAS_CONSOLE,         ITYPE_KEYBOARD, { KEY_GRAVE },    KEYMOD_NONE },
		{ IAS_SYSTEM_MENU,     ITYPE_KEYBOARD, { KEY_F1 },       KEYMOD_ALT  },

		// General
		{ IADF_MENU_TOGGLE,      ITYPE_KEYBOARD, { KEY_ESCAPE },    KEYMOD_NONE },
		{ IADF_PDA_TOGGLE,       ITYPE_KEYBOARD, { KEY_F1 },        KEYMOD_NONE },
		{ IADF_NIGHT_VISION_TOG, ITYPE_KEYBOARD, { KEY_F2 },        KEYMOD_NONE },
		{ IADF_CLEATS_TOGGLE,    ITYPE_KEYBOARD, { KEY_F3 },        KEYMOD_NONE },
		{ IADF_GAS_MASK_TOGGLE,  ITYPE_KEYBOARD, { KEY_F4 },        KEYMOD_NONE },
		{ IADF_HEAD_LAMP_TOGGLE, ITYPE_KEYBOARD, { KEY_F5 },        KEYMOD_NONE },
		{ IADF_HEADWAVE_TOGGLE,  ITYPE_KEYBOARD, { KEY_F6 },        KEYMOD_NONE },
		{ IADF_HUD_TOGGLE,       ITYPE_KEYBOARD, { KEY_F7 },        KEYMOD_NONE },
		{ IADF_HOLSTER_WEAPON,   ITYPE_KEYBOARD, { KEY_F8 },        KEYMOD_NONE },
		{ IADF_AUTOMOUNT_TOGGLE, ITYPE_KEYBOARD, { KEY_F8 },        KEYMOD_ALT  },
		{ IADF_CYCLEWPN_PREV,    ITYPE_MOUSEWHEEL,{ MOUSEWHEEL_DOWN },KEYMOD_NONE },
		{ IADF_CYCLEWPN_NEXT,    ITYPE_MOUSEWHEEL,{ MOUSEWHEEL_UP }, KEYMOD_NONE },
		{ IADF_WPN_PREV,         ITYPE_KEYBOARD, { KEY_BACKSPACE },  KEYMOD_NONE },
		{ IADF_PAUSE,            ITYPE_KEYBOARD, { KEY_PAUSE },      KEYMOD_NONE },
		{ IADF_AUTOMAP,          ITYPE_KEYBOARD, { KEY_TAB },        KEYMOD_NONE },
		{ IADF_DEC_SCREENSIZE,   ITYPE_KEYBOARD, { KEY_MINUS },      KEYMOD_ALT  },
		{ IADF_INC_SCREENSIZE,   ITYPE_KEYBOARD, { KEY_EQUALS },     KEYMOD_ALT  },

		// Automap
		{ IADF_MAP_ZOOM_IN,       ITYPE_KEYBOARD, { KEY_EQUALS }, KEYMOD_NONE },
		{ IADF_MAP_ZOOM_OUT,      ITYPE_KEYBOARD, { KEY_MINUS  }, KEYMOD_NONE },
		{ IADF_MAP_ENABLE_SCROLL, ITYPE_KEYBOARD, { KEY_INSERT }, KEYMOD_NONE },
		{ IADF_MAP_SCROLL_UP,     ITYPE_KEYBOARD, { KEY_UP },     KEYMOD_NONE },
		{ IADF_MAP_SCROLL_DN,     ITYPE_KEYBOARD, { KEY_DOWN },   KEYMOD_NONE },
		{ IADF_MAP_SCROLL_LT,     ITYPE_KEYBOARD, { KEY_LEFT },   KEYMOD_NONE },
		{ IADF_MAP_SCROLL_RT,     ITYPE_KEYBOARD, { KEY_RIGHT },  KEYMOD_NONE },
		{ IADF_MAP_LAYER_UP,      ITYPE_KEYBOARD, { KEY_RIGHTBRACKET }, KEYMOD_NONE },
		{ IADF_MAP_LAYER_DN,      ITYPE_KEYBOARD, { KEY_LEFTBRACKET  }, KEYMOD_NONE },

		// Player Controls
		{ IADF_FORWARD,      ITYPE_KEYBOARD, { KEY_W },        KEYMOD_NONE },
		{ IADF_BACKWARD,     ITYPE_KEYBOARD, { KEY_S },        KEYMOD_NONE },
		{ IADF_STRAFE_LT,    ITYPE_KEYBOARD, { KEY_A },        KEYMOD_NONE },
		{ IADF_STRAFE_RT,    ITYPE_KEYBOARD, { KEY_D },        KEYMOD_NONE },
		{ IADF_TURN_LT,      ITYPE_KEYBOARD, { KEY_LEFT },     KEYMOD_NONE },
		{ IADF_TURN_RT,      ITYPE_KEYBOARD, { KEY_RIGHT },    KEYMOD_NONE },
		{ IADF_LOOK_UP,      ITYPE_KEYBOARD, { KEY_PAGEUP },   KEYMOD_NONE },
		{ IADF_LOOK_DN,      ITYPE_KEYBOARD, { KEY_PAGEDOWN }, KEYMOD_NONE },
		{ IADF_CENTER_VIEW,  ITYPE_KEYBOARD, { KEY_C },        KEYMOD_NONE },
		{ IADF_RUN,          ITYPE_KEYBOARD, { KEY_LSHIFT },   KEYMOD_NONE },
		{ IADF_SLOW,         ITYPE_KEYBOARD, { KEY_CAPSLOCK }, KEYMOD_NONE },
		{ IADF_CROUCH,       ITYPE_KEYBOARD, { KEY_LCTRL },    KEYMOD_NONE },
		{ IADF_JUMP,         ITYPE_KEYBOARD, { KEY_SPACE },    KEYMOD_NONE },
		{ IADF_USE,          ITYPE_KEYBOARD, { KEY_E },        KEYMOD_NONE },
		{ IADF_WEAPON_1,     ITYPE_KEYBOARD, { KEY_1 },        KEYMOD_NONE },
		{ IADF_WEAPON_2,     ITYPE_KEYBOARD, { KEY_2 },        KEYMOD_NONE },
		{ IADF_WEAPON_3,     ITYPE_KEYBOARD, { KEY_3 },        KEYMOD_NONE },
		{ IADF_WEAPON_4,     ITYPE_KEYBOARD, { KEY_4 },        KEYMOD_NONE },
		{ IADF_WEAPON_5,     ITYPE_KEYBOARD, { KEY_5 },        KEYMOD_NONE },
		{ IADF_WEAPON_6,     ITYPE_KEYBOARD, { KEY_6 },        KEYMOD_NONE },
		{ IADF_WEAPON_7,     ITYPE_KEYBOARD, { KEY_7 },        KEYMOD_NONE },
		{ IADF_WEAPON_8,     ITYPE_KEYBOARD, { KEY_8 },        KEYMOD_NONE },
		{ IADF_WEAPON_9,     ITYPE_KEYBOARD, { KEY_9 },        KEYMOD_NONE },
		{ IADF_WEAPON_10,    ITYPE_KEYBOARD, { KEY_0 },        KEYMOD_NONE },
		// PC binds primary/secondary fire to the mouse buttons. On Xbox
		// we mirror the A button to MBUTTON_LEFT so Landru menus can be
		// clicked; binding fire to MBUTTON_LEFT here would make A both
		// jump AND fire. Fire stays on the triggers (see lines below).
#ifndef _XBOX
		{ IADF_PRIMARY_FIRE,   ITYPE_MOUSE, { MBUTTON_LEFT  }, KEYMOD_NONE },
		{ IADF_SECONDARY_FIRE, ITYPE_MOUSE, { MBUTTON_RIGHT }, KEYMOD_NONE },
#endif

		// Saving
		{ IAS_QUICK_SAVE, ITYPE_KEYBOARD, { KEY_F5 }, KEYMOD_ALT },
		{ IAS_QUICK_LOAD, ITYPE_KEYBOARD, { KEY_F9 }, KEYMOD_ALT },

		// HD Asset
		{ IADF_HD_ASSET_TOGGLE,          ITYPE_KEYBOARD, { KEY_F3 },          KEYMOD_ALT  },
		{ IADF_SCREENSHOT,               ITYPE_KEYBOARD, { KEY_PRINTSCREEN },  KEYMOD_NONE },
		{ IADF_GIF_RECORD,               ITYPE_KEYBOARD, { KEY_F2 },           KEYMOD_ALT  },
		{ IADF_GIF_RECORD_NO_COUNTDOWN,  ITYPE_KEYBOARD, { KEY_F2 },           KEYMOD_CTRL },

		// DEMO handling
		{ IADF_DEMO_SPEEDUP,  ITYPE_KEYBOARD, { KEY_KP_PLUS  }, KEYMOD_NONE },
		{ IADF_DEMO_SLOWDOWN, ITYPE_KEYBOARD, { KEY_KP_MINUS }, KEYMOD_NONE },
	};

	static InputBinding s_defaultControllerBinds[] =
	{
		{ IAS_SYSTEM_MENU, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_RIGHTSTICK }, KEYMOD_NONE },

		{ IADF_JUMP,   ITYPE_CONTROLLER, { CONTROLLER_BUTTON_A }, KEYMOD_NONE },
		{ IADF_CROUCH, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_B }, KEYMOD_NONE },
		{ IADF_USE,    ITYPE_CONTROLLER, { CONTROLLER_BUTTON_X }, KEYMOD_NONE },

#ifdef _XBOX
		// Original Xbox controller has no GUIDE button; bind the pause/
		// escape menu to START. main_xbox.cpp still polls Start+Back as
		// the quit combo before inputMapping runs, so the combo wins and
		// pressing START alone falls through to here.
		{ IADF_MENU_TOGGLE, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_START }, KEYMOD_NONE },
#else
		{ IADF_MENU_TOGGLE, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_GUIDE }, KEYMOD_NONE },
#endif

		{ IADF_PRIMARY_FIRE,   ITYPE_CONTROLLER_AXIS, { AXIS_RIGHT_TRIGGER }, KEYMOD_NONE },
		{ IADF_SECONDARY_FIRE, ITYPE_CONTROLLER_AXIS, { AXIS_LEFT_TRIGGER  }, KEYMOD_NONE },

		{ IADF_AUTOMAP,          ITYPE_CONTROLLER, { CONTROLLER_BUTTON_BACK       }, KEYMOD_NONE },
		{ IADF_HEAD_LAMP_TOGGLE, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_DPAD_RIGHT }, KEYMOD_NONE },
		{ IADF_CLEATS_TOGGLE,    ITYPE_CONTROLLER, { CONTROLLER_BUTTON_DPAD_UP    }, KEYMOD_NONE },
		{ IADF_NIGHT_VISION_TOG, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_DPAD_LEFT  }, KEYMOD_NONE },
		{ IADF_GAS_MASK_TOGGLE,  ITYPE_CONTROLLER, { CONTROLLER_BUTTON_DPAD_DOWN  }, KEYMOD_NONE },

		{ IADF_CYCLEWPN_PREV, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_LEFTSHOULDER  }, KEYMOD_NONE },
		{ IADF_CYCLEWPN_NEXT, ITYPE_CONTROLLER, { CONTROLLER_BUTTON_RIGHTSHOULDER }, KEYMOD_NONE },
	};

	static InputConfig s_inputConfig = { 0 };
	static ActionState s_actions[IA_COUNT];
	static int replayCounter    = 0;
	static int maxReplayCounter = 0;

	// Xbox: fixed-capacity key tracking arrays replace std::vector.
	// Used only by clearKeys() which is only called from the (stubbed) replay path.
	#define CURRENT_KEYS_MAX 64
	static KeyboardCode s_currentKeys[CURRENT_KEYS_MAX];
	static int          s_currentKeyCount = 0;
	static KeyboardCode s_currentKeyPresses[CURRENT_KEYS_MAX];
	static int          s_currentKeyPressCount = 0;

	void addDefaultControlBinds();
	void inputMapping_removeBinding(u32 index);
#ifdef _XBOX
	void inputMapping_sanitizeXboxBindings();
#endif

	// -----------------------------------------------------------------------
	void inputMapping_startup()
	{
#ifdef _XBOX
		if (inputMapping_restore())
		{
			inputMapping_sanitizeXboxBindings();
			inputMapping_serialize();
			return;
		}
		inputMapping_resetToDefaults();
		inputMapping_sanitizeXboxBindings();
		inputMapping_serialize();
		return;
#else
		if (inputMapping_restore())
			return;

		inputMapping_resetToDefaults();
		inputMapping_serialize();
#endif
		// Note: s_gameSettings->df_enableRecording expression was a no-op in
		// original; preserved as comment for reference.
		// TFE_Settings::getGameSettings()->df_enableRecording;
	}

	void inputMapping_shutdown()
	{
		free(s_inputConfig.binds);
		s_inputConfig.bindCount    = 0;
		s_inputConfig.bindCapacity = 0;
		s_inputConfig.binds        = NULL;
	}

	void inputMapping_resetToDefaults()
	{
		s_inputConfig.bindCount    = 0;
		s_inputConfig.bindCapacity = IA_COUNT * 2;
		s_inputConfig.binds = (InputBinding*)realloc(s_inputConfig.binds,
			sizeof(InputBinding) * s_inputConfig.bindCapacity);

		s_inputConfig.controllerFlags      = CFLAG_ENABLE;
		s_inputConfig.axis[AA_LOOK_HORZ]   = AXIS_RIGHT_X;
		s_inputConfig.axis[AA_LOOK_VERT]   = AXIS_RIGHT_Y;
		s_inputConfig.axis[AA_STRAFE]      = AXIS_LEFT_X;
		s_inputConfig.axis[AA_MOVE]        = AXIS_LEFT_Y;
		s_inputConfig.ctrlSensitivity[0]   = 1.0f;
		s_inputConfig.ctrlSensitivity[1]   = 1.0f;
		s_inputConfig.ctrlDeadzone[0]      = 0.1f;
		s_inputConfig.ctrlDeadzone[1]      = 0.1f;

		s_inputConfig.mouseFlags           = 0;
		s_inputConfig.mouseMode            = MMODE_LOOK;
		s_inputConfig.mouseSensitivity[0]  = 1.0f;
		s_inputConfig.mouseSensitivity[1]  = 1.0f;

		memset(s_actions, 0, sizeof(ActionState) * IA_COUNT);
		addDefaultControlBinds();
	}

	bool inputMapping_serialize()
	{
		char fullPath[TFE_MAX_PATH];
		sprintf(fullPath, "%s%s", TFE_Paths::getPath(PATH_USER_DOCUMENTS), c_inputRemappingName);

		FileStream file;
		if (!file.open(fullPath, Stream::MODE_WRITE))
			return false;

		file.writeBuffer(c_inputRemappingHdr, 4);
		file.write(&c_inputRemappingVersion);
		file.write(&s_inputConfig.bindCount);
		file.write(&s_inputConfig.bindCapacity);
		file.writeBuffer(s_inputConfig.binds, sizeof(InputBinding), s_inputConfig.bindCount);
		file.write(&s_inputConfig.controllerFlags);
		file.writeBuffer(s_inputConfig.axis, sizeof(Axis), AA_COUNT);
		file.write(s_inputConfig.ctrlSensitivity, 2);
		file.write(s_inputConfig.ctrlDeadzone, 2);
		file.write(&s_inputConfig.mouseFlags);
		file.writeBuffer(&s_inputConfig.mouseMode, sizeof(MouseMode));
		file.write(s_inputConfig.mouseSensitivity, 2);
		file.close();
		return true;
	}

	bool inputMapping_restore()
	{
		char fullPath[TFE_MAX_PATH];
		sprintf(fullPath, "%s%s", TFE_Paths::getPath(PATH_USER_DOCUMENTS), c_inputRemappingName);

		FileStream file;
		if (!file.open(fullPath, Stream::MODE_READ))
			return false;

		char hdr[4];
		u32  version;
		file.readBuffer(hdr, 4);
		file.read(&version);
		if (memcmp(hdr, c_inputRemappingHdr, 4) != 0)
		{
			file.close();
			return false;
		}

		file.read(&s_inputConfig.bindCount);
		file.read(&s_inputConfig.bindCapacity);
		if (s_inputConfig.bindCount > s_inputConfig.bindCapacity)
		{
			s_inputConfig.bindCapacity =
				((s_inputConfig.bindCount + IA_COUNT - 1) / IA_COUNT) * IA_COUNT + IA_COUNT;
		}
		s_inputConfig.binds = (InputBinding*)realloc(s_inputConfig.binds,
			sizeof(InputBinding) * s_inputConfig.bindCapacity);
		file.readBuffer(s_inputConfig.binds, sizeof(InputBinding), s_inputConfig.bindCount);

		file.read(&s_inputConfig.controllerFlags);
		file.readBuffer(s_inputConfig.axis, sizeof(Axis), AA_COUNT);
		file.read(s_inputConfig.ctrlSensitivity, 2);

		if (version >= INPUT_ADD_DEADZONE)
			file.read(s_inputConfig.ctrlDeadzone, 2);
		else
		{
			s_inputConfig.ctrlDeadzone[0] = 0.1f;
			s_inputConfig.ctrlDeadzone[1] = 0.1f;
		}

		file.read(&s_inputConfig.mouseFlags);
		file.readBuffer(&s_inputConfig.mouseMode, sizeof(MouseMode));
		file.read(s_inputConfig.mouseSensitivity, 2);
		file.close();

		if (version < INPUT_ADD_QUICKSAVE)
		{
			inputMapping_addBinding(&s_defaultKeyboardBinds[IAS_QUICK_SAVE]);
			inputMapping_addBinding(&s_defaultKeyboardBinds[IAS_QUICK_LOAD]);
		}
		if (version < INPUT_ADD_HIGH_DEF)
		{
			inputMapping_addBinding(&s_defaultKeyboardBinds[IADF_HD_ASSET_TOGGLE]);
			inputMapping_addBinding(&s_defaultKeyboardBinds[IADF_SCREENSHOT]);
			inputMapping_addBinding(&s_defaultKeyboardBinds[IADF_GIF_RECORD]);
			inputMapping_addBinding(&s_defaultKeyboardBinds[IADF_GIF_RECORD_NO_COUNTDOWN]);
		}
		if (version < INPUT_DEMO_CONFIG)
		{
			inputMapping_addBinding(&s_defaultKeyboardBinds[IADF_DEMO_SPEEDUP]);
			inputMapping_addBinding(&s_defaultKeyboardBinds[IADF_DEMO_SLOWDOWN]);
		}
		return true;
	}

	void inputMapping_addBinding(InputBinding* binding)
	{
		u32 index = s_inputConfig.bindCount++;
		if (s_inputConfig.bindCount > s_inputConfig.bindCapacity)
		{
			s_inputConfig.bindCapacity += IA_COUNT;
			s_inputConfig.binds = (InputBinding*)realloc(s_inputConfig.binds,
				sizeof(InputBinding) * s_inputConfig.bindCapacity);
		}
		s_inputConfig.binds[index] = *binding;
	}

	bool inputMapping_setControllerBinding(InputAction action, InputType type, u32 code)
	{
		if (action < 0 || action >= IA_COUNT) return false;
		if (type != ITYPE_CONTROLLER && type != ITYPE_CONTROLLER_AXIS) return false;

		for (s32 i = (s32)s_inputConfig.bindCount - 1; i >= 0; i--)
		{
			InputBinding* bind = &s_inputConfig.binds[i];
			const bool controllerBinding = bind->type == ITYPE_CONTROLLER || bind->type == ITYPE_CONTROLLER_AXIS;
			const bool sameAction = bind->action == action;
			const bool sameControl = bind->type == type && bind->code == code;
			if (controllerBinding && (sameAction || sameControl))
			{
				inputMapping_removeBinding((u32)i);
			}
		}

		InputBinding binding;
		memset(&binding, 0, sizeof(binding));
		binding.action = action;
		binding.type = type;
		binding.code = code;
		binding.keyMod = KEYMOD_NONE;
		inputMapping_addBinding(&binding);
		return true;
	}

	void addDefaultControlBinds()
	{
		for (s32 i = 0; i < TFE_ARRAYSIZE(s_defaultKeyboardBinds); i++)
			inputMapping_addBinding(&s_defaultKeyboardBinds[i]);
		for (s32 i = 0; i < TFE_ARRAYSIZE(s_defaultControllerBinds); i++)
			inputMapping_addBinding(&s_defaultControllerBinds[i]);
	}

#ifdef _XBOX
	void inputMapping_sanitizeXboxBindings()
	{
		for (s32 i = (s32)s_inputConfig.bindCount - 1; i >= 0; i--)
		{
			InputBinding* bind = &s_inputConfig.binds[i];
			if (bind->type == ITYPE_MOUSE &&
				(bind->action == IADF_PRIMARY_FIRE || bind->action == IADF_SECONDARY_FIRE))
			{
				inputMapping_removeBinding((u32)i);
			}
		}
	}
#endif

	void inputMapping_endFrame()
	{
		for (u32 i = 0; i < IA_COUNT; i++)
			s_actions[i] = STATE_UP;
	}

	ActionState inputMapping_getAction(InputAction act)
	{
		return s_actions[act];
	}

	static bool inputMapping_isMovementAction(InputAction action)
	{
		return action >= IADF_FORWARD && action <= IADF_LOOK_DN;
	}

#ifndef _XBOX  // On Xbox, these are provided as inline stubs in replay.h
	void inputMapping_setReplayCounter(int counter) { replayCounter    = counter; }
	void inputMapping_resetCounter()                { replayCounter    = 0; }
	int  inputMapping_getCounter()                  { return replayCounter; }
	void inputMapping_setMaxCounter(int counter)    { maxReplayCounter = counter; }
#endif

	void inputMapping_updateInput()
	{
		for (u32 i = 0; i < s_inputConfig.bindCount; i++)
		{
			InputBinding* bind = &s_inputConfig.binds[i];
			switch (bind->type)
			{
				case ITYPE_KEYBOARD:
				{
					const bool keyIsMod = (s32)bind->keyMod == (s32)bind->keyCode
					                   || bind->keyMod == KEYMOD_NONE;
					const bool keyIsAlt = bind->keyCode == KEY_LALT
					                   || bind->keyCode == KEY_RALT;
					if (TFE_Input::keyModDown(bind->keyMod,
					        inputMapping_isMovementAction(bind->action))
					    || (keyIsMod && keyIsAlt))
					{
						if (TFE_Input::keyPressed(bind->keyCode))
						{
							s_actions[bind->action] = STATE_PRESSED;
							recordEvent(bind->action, bind->keyCode, true);
						}
						else if (TFE_Input::keyDown(bind->keyCode)
						         && s_actions[bind->action] != STATE_PRESSED)
						{
							s_actions[bind->action] = STATE_DOWN;
							recordEvent(bind->action, bind->keyCode, false);
						}
					}
				} break;

				case ITYPE_MOUSE:
				{
					if (TFE_Input::keyModDown(bind->keyMod, true))
					{
						if (TFE_Input::mousePressed(bind->mouseBtn))
						{
							s_actions[bind->action] = STATE_PRESSED;
							recordEvent(bind->action, bind->keyCode, true);
						}
						else if (TFE_Input::mouseDown(bind->mouseBtn)
						         && s_actions[bind->action] != STATE_PRESSED)
						{
							s_actions[bind->action] = STATE_DOWN;
							recordEvent(bind->action, bind->keyCode, false);
						}
					}
				} break;

				case ITYPE_MOUSEWHEEL:
				{
					s32 dx, dy;
					TFE_Input::getMouseWheel(&dx, &dy);
					if ((bind->mouseWheel == MOUSEWHEEL_LEFT  && dx < 0) ||
					    (bind->mouseWheel == MOUSEWHEEL_RIGHT && dx > 0) ||
					    (bind->mouseWheel == MOUSEWHEEL_UP    && dy > 0) ||
					    (bind->mouseWheel == MOUSEWHEEL_DOWN  && dy < 0))
					{
						s_actions[bind->action] = STATE_PRESSED;
						recordEvent(bind->action, bind->keyCode, true);
					}
				} break;

				case ITYPE_CONTROLLER:
				{
					if (!(s_inputConfig.controllerFlags & CFLAG_ENABLE)) break;
					if (TFE_Input::buttonPressed(bind->ctrlBtn))
					{
						s_actions[bind->action] = STATE_PRESSED;
						recordEvent(bind->action, bind->keyCode, true);
					}
					else if (TFE_Input::buttonDown(bind->ctrlBtn)
					         && s_actions[bind->action] != STATE_PRESSED)
					{
						s_actions[bind->action] = STATE_DOWN;
						recordEvent(bind->action, bind->keyCode, false);
					}
				} break;

				case ITYPE_CONTROLLER_AXIS:
				{
					if (!(s_inputConfig.controllerFlags & CFLAG_ENABLE)) break;
					if (TFE_Input::getAxis(bind->axis) > 0.5f)
					{
						s_actions[bind->action] = STATE_DOWN;
						recordEvent(bind->action, bind->keyCode, false);
					}
				} break;
			}
		}

#ifdef _XBOX
		static bool s_xboxRunToggle = false;
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_LEFTSTICK))
		{
			s_xboxRunToggle = !s_xboxRunToggle;
		}
		if (s_xboxRunToggle && s_actions[IADF_RUN] == STATE_UP)
		{
			s_actions[IADF_RUN] = STATE_DOWN;
		}
#endif
	}

	void inputMapping_removeState(InputAction action)  { s_actions[action] = STATE_UP; }
	void inputMapping_setStateDown(InputAction action)  { s_actions[action] = STATE_DOWN; }
	void inputMapping_setStatePress(InputAction action) { s_actions[action] = STATE_PRESSED; }

	ActionState inputMapping_getActionState(InputAction action)
	{
		return s_actions[action];
	}

	void inputMapping_clearKeyBinding(KeyboardCode key)
	{
		for (u32 i = 0; i < s_inputConfig.bindCount; i++)
		{
			InputBinding* bind = &s_inputConfig.binds[i];
			if (bind->type == ITYPE_KEYBOARD && bind->keyCode == key)
			{
				inputMapping_removeState(bind->action);
				return;
			}
		}
	}

	f32 inputMapping_getAnalogAxis(AnalogAxis axis)
	{
		if (!(s_inputConfig.controllerFlags & CFLAG_ENABLE))
			return 0.0f;

		Axis mappedAxis = s_inputConfig.axis[axis];
		f32  axisValue  = TFE_Input::getAxis(mappedAxis);
		if (s_inputConfig.controllerFlags & (1 << (mappedAxis + 1)))
			axisValue = -axisValue;

		const f32 sensitivity = s_inputConfig.ctrlSensitivity[mappedAxis < AXIS_RIGHT_X ? 0 : 1];
		f32 deadzone = s_inputConfig.ctrlDeadzone[mappedAxis < AXIS_RIGHT_X ? 0 : 1];
		if (fabsf(axisValue) <= deadzone)
		{
			axisValue = 0.0f;
		}
		else if (deadzone > FLT_EPSILON)
		{
			deadzone  *= (axisValue < 0.0f) ? -1.0f : 1.0f;
			axisValue  = (axisValue - deadzone) * sensitivity / (1.0f - fabsf(deadzone));
		}
		else
		{
			axisValue *= sensitivity;
		}
		return axisValue;
	}

	f32 inputMapping_getHorzMouseSensitivity()
	{
		return s_inputConfig.mouseSensitivity[0]
		     * ((s_inputConfig.mouseFlags & MFLAG_INVERT_HORZ) ? -1.0f : 1.0f);
	}

	f32 inputMapping_getVertMouseSensitivity()
	{
		return s_inputConfig.mouseSensitivity[1]
		     * ((s_inputConfig.mouseFlags & MFLAG_INVERT_VERT) ? -1.0f : 1.0f);
	}

	u32 inputMapping_getBindingsForAction(InputAction action, u32* indices, u32 maxIndices)
	{
		assert(indices);
		u32 count = 0;
		for (u32 i = 0; i < s_inputConfig.bindCount; i++)
		{
			if (s_inputConfig.binds[i].action == action)
			{
				indices[count++] = i;
				if (count >= maxIndices) return count;
			}
		}
		return count;
	}

	void inputMapping_removeBinding(u32 index)
	{
		if (index >= s_inputConfig.bindCount) return;
		for (u32 i = index; i + 1 < s_inputConfig.bindCount; i++)
			s_inputConfig.binds[i] = s_inputConfig.binds[i + 1];
		s_inputConfig.bindCount--;
	}

	InputBinding* inputMapping_getBindingByIndex(u32 index)
	{
		return &s_inputConfig.binds[index];
	}

	InputConfig* inputMapping_get()
	{
		return &s_inputConfig;
	}

	// clearKeys: only called from replay path which is stubbed on Xbox.
	// Implementation kept for link completeness.
	static void clearKeys()
	{
		for (int i = 0; i < s_currentKeyCount; i++)
			TFE_Input::setKeyUp(s_currentKeys[i]);
		s_currentKeyCount = 0;

		for (int i = 0; i < s_currentKeyPressCount; i++)
			TFE_Input::clearKeyPressed(s_currentKeyPresses[i]);
		s_currentKeyPressCount = 0;
	}

	bool isBindingPressed(InputAction action)
	{
		u32 indices[2];
		u32 count = inputMapping_getBindingsForAction(action, indices, 2);
		if (count > 0)
		{
			InputBinding* binding = inputMapping_getBindingByIndex(indices[0]);
			if (TFE_Input::keyPressed(binding->keyCode))
			{
				if (binding->keyMod && !TFE_Input::keyModDown(binding->keyMod, true))
					return false;
				return true;
			}
		}
		return false;
	}

	bool inputMapping_handleInputs()
	{
		// Escape during playback (replay always false on Xbox, kept for logic parity).
		if (keyPressed(KEY_ESCAPE) && !TFE_DarkForces::pda_isOpen())
		{
			if (isDemoPlayback())
			{
				sendEndPlaybackMsg();
				clearKeys();
				endReplay();
			}
			if (isRecording())
			{
				sendEndRecordingMsg();
				TFE_Input::endRecording();
			}
		}

		// On Xbox, mouse position is already set each frame by input_xbox.cpp
		// via TFE_Input::setRelativeMousePos / setMousePos.
		// No SDL_GetRelativeMouseState needed here.
		// isDemoPlayback() always returns false on Xbox, so replay block is
		// compiled but never entered.
		if (isDemoPlayback())
		{
			// (unreachable on Xbox)
			if (isBindingPressed(IADF_DEMO_SPEEDUP))   increaseReplayFrameRate();
			if (isBindingPressed(IADF_DEMO_SLOWDOWN))  decreaseReplayFrameRate();
			if (replayCounter >= maxReplayCounter)     endReplay();
			inputMapping_endFrame();
			replayEvent();
		}
		// isRecording() always returns false on Xbox.
		// Block compiled but never entered.

		// Process input bindings normally.
		if (!isDemoPlayback())
			inputMapping_updateInput();

		if (isReplaySystemLive())
			logReplayPosition(replayCounter);

		bool skipUpdateCounter = isDemoPlayback() && isReplayPaused()
		                       && TFE_DarkForces::s_playerEye;
		if (skipUpdateCounter)
			return false;

		replayCounter++;
		return true;
	}

}  // namespace TFE_Input
