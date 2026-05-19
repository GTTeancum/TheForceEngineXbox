#include <cstring>

#include "missionBriefing.h"
#include "menu.h"
#include "uiDraw.h"
#include <TFE_DarkForces/Landru/lactorDelt.h>
#include <TFE_DarkForces/Landru/lactorAnim.h>
#include <TFE_DarkForces/Landru/lpalette.h>
#include <TFE_DarkForces/Landru/lcanvas.h>
#include <TFE_DarkForces/Landru/ldraw.h>
#include <TFE_Archive/lfdArchive.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_System/system.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum BriefingButton
	{
		BRIEF_BTN_OK = 0,
		BRIEF_BTN_UP,
		BRIEF_BTN_DOWN,
		BRIEF_BTN_CANCEL,
		BRIEF_BTN_EASY,
		BRIEF_BTN_MEDIUM,
		BRIEF_BTN_HARD,
		BRIEF_BTN_COUNT,
	};
	static s32 s_keyPressed = -1;
	static JBool s_briefingOpen = JFALSE;
	static s32 s_skill = 0;
	static LRect s_briefRect = { 25, 15, 155, 305 };
	static LRect s_missionTextRect;
	static LRect s_viewBounds;
	static LActor* s_briefActor = nullptr;
	static LActor* s_menuActor = nullptr;
	static LPalette* s_palette = nullptr;
	static u8* s_framebuffer = nullptr;
	static LangHotkeys* s_langKeys;
#ifdef _XBOX
	static BriefingButton s_xboxSelectedButton = BRIEF_BTN_OK;
	static s32 s_xboxNavX = 0;
	static s32 s_xboxNavY = 0;
	static JBool s_xboxShowObjectives = JFALSE;
	static bool s_xboxPrevA = false;
	static bool s_xboxPrevB = false;
	static bool s_xboxPrevX = false;
	static bool s_xboxPrevY = false;
	static bool s_xboxPrevBack = false;
	static bool s_xboxPrevLB = false;
	static bool s_xboxPrevRB = false;
#endif

	s16 s_briefY;
	s32 s_briefingMaxY;
	LRect s_overlayRect;

	enum
	{
		MENU_TYPE_BACK = 200,
		MENU_TYPE_OVERLAY = 201,
	};

	static const s16 c_frameTypes[] =
	{
		MENU_TYPE_BACK,
		MENU_TYPE_OVERLAY,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_REPEAT_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_REPEAT_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
	};

	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void missionBriefing_start(const char* archive, const char* bgAnim, const char* mission, const char* palette, s32 skill, LangHotkeys* langKeys)
	{
		s_langKeys = langKeys;

		menu_init();
		menu_startupDisplay();

		s_briefingOpen = JFALSE;
		s_skill = skill;
#ifdef _XBOX
		s_xboxSelectedButton = BRIEF_BTN_OK;
		s_xboxNavX = 0;
		s_xboxNavY = 0;
		s_xboxShowObjectives = JFALSE;
		s_xboxPrevA = TFE_Input::buttonDown(CONTROLLER_BUTTON_A);
		s_xboxPrevB = TFE_Input::buttonDown(CONTROLLER_BUTTON_B);
		s_xboxPrevX = TFE_Input::buttonDown(CONTROLLER_BUTTON_X);
		s_xboxPrevY = TFE_Input::buttonDown(CONTROLLER_BUTTON_Y);
		s_xboxPrevBack = TFE_Input::buttonDown(CONTROLLER_BUTTON_BACK);
		s_xboxPrevLB = TFE_Input::buttonDown(CONTROLLER_BUTTON_LEFTSHOULDER);
		s_xboxPrevRB = TFE_Input::buttonDown(CONTROLLER_BUTTON_RIGHTSHOULDER);
		s_buttonPressed = -1;
		s_buttonHover = JFALSE;
		s_keyPressed = -1;
#endif

		if (!menu_openResourceArchive(archive))
		{
			// Try the dfbrief.lfd file.
			if (!menu_openResourceArchive("dfbrief.lfd"))
			{
				return;
			}
		}

		// Mission specific text and images.
		s_briefActor = lactorDelt_load(mission, &s_briefRect, 0, 0, 0);
		if (!s_briefActor)
		{
			menu_closeResourceArchive();
			return;
		}

		// Menu Items
		LRect bounds;
		lcanvas_getBounds(&bounds);
		s_menuActor = lactorAnim_load(bgAnim, &bounds, 0, 0, 0);
		lactor_setTime(s_menuActor, -1, -1);
		s_palette = lpalette_load(palette);
		lpalette_setScreenPal(s_palette);

		if (!s_menuActor)
		{
			menu_closeResourceArchive();
			s_briefingOpen = JFALSE;
			s_skill = 1;
			vfb_forceToBlack();
			lcanvas_clear();
			return;
		}

		s16 state_btn_index = 600;
		s16 button_index = 0;
		s16 slider_index = 0;
		s16 back_state = -1;
		s16 overlay_state = -1;
			   
		// Buttons
		const s32 count = s_menuActor ? s_menuActor->arraySize : 0;
		for (s32 i = 0; i < count; i++)
		{
			LRect rect;
			lactor_setState(s_menuActor, i, 0);
			lactor_getFrame(s_menuActor, &rect);

			if (c_frameTypes[i] == MENU_TYPE_BACK)
			{
				back_state = i;
			}
			else if (c_frameTypes[i] == MENU_TYPE_OVERLAY)
			{
				overlay_state = i;
				lactorAnim_getFrame(s_menuActor, &s_missionTextRect);
			}
		}
		menu_closeResourceArchive();
		
		LRect rect;
		lactor_setTime(s_briefActor, -1, -1);
		lactorDelt_getFrame(s_briefActor, &rect);

		s_briefingMaxY = (rect.bottom - rect.top) - (s_briefRect.bottom - s_briefRect.top) + BRIEF_VERT_MARGIN;
		// Round to a factor of the line scrolling value.
		s_briefingMaxY = s_briefingMaxY + BRIEF_LINE_SCROLL - (s_briefingMaxY % BRIEF_LINE_SCROLL);

		if (s_briefingMaxY < -BRIEF_VERT_MARGIN)
		{
			s_briefingMaxY = -BRIEF_VERT_MARGIN;
		}
		s_overlayRect = s_briefRect;
		s_briefY = -BRIEF_VERT_MARGIN;

		s_briefingOpen = JTRUE;

		s_framebuffer = ldraw_getBitmap();
		lcanvas_getBounds(&s_viewBounds);

		ltime_setFrameDelay(20);
	}

	void missionBriefing_cleanup()
	{
		lactor_removeActor(s_briefActor);
		lactor_removeActor(s_menuActor);

		lactor_free(s_briefActor);
		lactor_free(s_menuActor);
		lpalette_free(s_palette);

		s_briefActor = nullptr;
		s_menuActor = nullptr;
		s_palette = nullptr;
	}
		
	void drawButton(BriefingButton id)
	{
		s32 pressed = 0;
		if ((s_buttonHover && id == s_buttonPressed) || (id == s_keyPressed))
		{
			pressed = 1;
		}
		else if (id >= BRIEF_BTN_EASY && s_skill == id - BRIEF_BTN_EASY)
		{
			pressed = 1;
		}

		lactor_setState(s_menuActor, 2*(1+id) + (pressed ? 0 : 1), 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);
	}

	void missionBriefing_scroll(s32 amt)
	{
		if (amt < 0 && s_briefY > -BRIEF_VERT_MARGIN)
		{
			s_briefY += amt;
			if (s_briefY < -BRIEF_VERT_MARGIN) { s_briefY = -BRIEF_VERT_MARGIN; }
		}
		else if (amt > 0 && s_briefY != s_briefingMaxY)
		{
			s_briefY += amt;
			if (s_briefY > s_briefingMaxY) { s_briefY = s_briefingMaxY; }
		}
	}

#ifdef _XBOX
	JBool missionBriefing_xboxPressed(Button button, bool* prevDown)
	{
		const bool down = TFE_Input::buttonDown(button);
		const JBool pressed = (down && !*prevDown) ? JTRUE : JFALSE;
		*prevDown = down;
		return pressed;
	}

	void missionBriefing_activateButton(BriefingButton button, JBool* abort, JBool* exitBriefing)
	{
		s_keyPressed = button;
		TFE_System::logWrite(LOG_MSG, "MissionBriefing", "activate button=%d", button);

		switch (button)
		{
			case BRIEF_BTN_OK:
			{
				*abort = JFALSE;
				*exitBriefing = JTRUE;
			} break;
			case BRIEF_BTN_UP:
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL);
			} break;
			case BRIEF_BTN_DOWN:
			{
				missionBriefing_scroll(BRIEF_LINE_SCROLL);
			} break;
			case BRIEF_BTN_CANCEL:
			{
				*abort = JTRUE;
				*exitBriefing = JTRUE;
			} break;
			case BRIEF_BTN_EASY:
			{
				s_skill = 0;
			} break;
			case BRIEF_BTN_MEDIUM:
			{
				s_skill = 1;
			} break;
			case BRIEF_BTN_HARD:
			{
				s_skill = 2;
			} break;
			default:
			{
			} break;
		}
	}

	BriefingButton missionBriefing_buttonFromVisualIndex(s32 index)
	{
		static const BriefingButton c_order[] =
		{
			BRIEF_BTN_EASY,
			BRIEF_BTN_MEDIUM,
			BRIEF_BTN_HARD,
			BRIEF_BTN_CANCEL,
			BRIEF_BTN_UP,
			BRIEF_BTN_DOWN,
			BRIEF_BTN_OK,
		};
		if (index < 0) { index = 0; }
		if (index >= (s32)TFE_ARRAYSIZE(c_order)) { index = (s32)TFE_ARRAYSIZE(c_order) - 1; }
		return c_order[index];
	}

	s32 missionBriefing_visualIndexFromButton(BriefingButton button)
	{
		static const BriefingButton c_order[] =
		{
			BRIEF_BTN_EASY,
			BRIEF_BTN_MEDIUM,
			BRIEF_BTN_HARD,
			BRIEF_BTN_CANCEL,
			BRIEF_BTN_UP,
			BRIEF_BTN_DOWN,
			BRIEF_BTN_OK,
		};
		for (s32 i = 0; i < (s32)TFE_ARRAYSIZE(c_order); i++)
		{
			if (c_order[i] == button) { return i; }
		}
		return (s32)TFE_ARRAYSIZE(c_order) - 1;
	}

	JBool missionBriefing_handleXboxInput(JBool* abort)
	{
		JBool exitBriefing = JFALSE;
		s_buttonPressed = -1;
		s_buttonHover = JFALSE;
		s_keyPressed = -1;

		const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
		const s32 axisY = ly > 0.45f ? 1 : (ly < -0.45f ? -1 : 0);
		const JBool aPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_A, &s_xboxPrevA);
		const JBool bPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_B, &s_xboxPrevB);
		const JBool xPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_X, &s_xboxPrevX);
		const JBool yPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_Y, &s_xboxPrevY);
		const JBool backPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_BACK, &s_xboxPrevBack);
		const JBool lbPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_LEFTSHOULDER, &s_xboxPrevLB);
		const JBool rbPressed = missionBriefing_xboxPressed(CONTROLLER_BUTTON_RIGHTSHOULDER, &s_xboxPrevRB);

		s32 moveY = 0;
		if (TFE_Input::buttonDown(CONTROLLER_BUTTON_DPAD_UP))   { moveY = -1; }
		if (TFE_Input::buttonDown(CONTROLLER_BUTTON_DPAD_DOWN)) { moveY =  1; }
		if (axisY) { moveY = -axisY; }

		if (moveY && ltime_isFrameReady())
		{
			s_keyPressed = moveY < 0 ? BRIEF_BTN_UP : BRIEF_BTN_DOWN;
			missionBriefing_scroll(moveY < 0 ? -BRIEF_LINE_SCROLL : BRIEF_LINE_SCROLL);
		}

		if (lbPressed)
		{
			s_keyPressed = BRIEF_BTN_UP;
			missionBriefing_scroll(-BRIEF_PAGE_SCROLL);
		}
		else if (rbPressed)
		{
			s_keyPressed = BRIEF_BTN_DOWN;
			missionBriefing_scroll(BRIEF_PAGE_SCROLL);
		}

		if (aPressed)
		{
			TFE_System::logWrite(LOG_MSG, "MissionBriefing", "A START");
			missionBriefing_activateButton(BRIEF_BTN_OK, abort, &exitBriefing);
		}
		else if (bPressed || backPressed)
		{
			TFE_System::logWrite(LOG_MSG, "MissionBriefing", "B/BACK ABORT");
			s_keyPressed = BRIEF_BTN_CANCEL;
			*abort = JTRUE;
			exitBriefing = JTRUE;
		}
		else if (xPressed)
		{
			s_xboxShowObjectives = !s_xboxShowObjectives;
			TFE_System::logWrite(LOG_MSG, "MissionBriefing", "toggle view=%s", s_xboxShowObjectives ? "objectives" : "briefing");
		}
		else if (yPressed)
		{
			s_skill = (s_skill + 1) % 3;
			TFE_System::logWrite(LOG_MSG, "MissionBriefing", "difficulty=%d", s_skill);
		}

		s_xboxNavY = axisY;
		return exitBriefing;
	}

	void missionBriefing_drawXboxFooter()
	{
		LRect strip = { 164, 0, 200, 320 };
		const char* viewPrompt = s_xboxShowObjectives ? "BRIEFING" : "OBJECTIVES";
		const char* skillPrompt = s_skill == 0 ? "EASY" : (s_skill == 1 ? "MEDIUM" : "HARD");
		drawClippedColorRect(&strip, 0);
		print("A START", 8, 190, 12, s_framebuffer);
		print("B ABORT", 61, 190, 39, s_framebuffer);
		print("X", 118, 190, 34, s_framebuffer);
		print(viewPrompt, 130, 190, 47, s_framebuffer);
		print("Y", 218, 190, 43, s_framebuffer);
		print(skillPrompt, 230, 190, 47, s_framebuffer);
	}
#endif
		
	JBool missionBriefing_handleInput(JBool* abort)
	{
		JBool exitBriefing = JFALSE;

#ifdef _XBOX
		return missionBriefing_handleXboxInput(abort);
#else
		// Add support for mouse wheel scrolling.
		s32 wdx, wdy;
		TFE_Input::getMouseWheel(&wdx, &wdy);
		
		// Mouse interactions.
		menu_handleMousePosition();
		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
			{
				lactor_setState(s_menuActor, 2 * (1 + i), 0);
				LRect buttonRect;
				lactorAnim_getFrame(s_menuActor, &buttonRect);

				if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
					s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = JTRUE;
					break;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			lactor_setState(s_menuActor, 2 * (1 + s_buttonPressed), 0);
			LRect buttonRect;
			lactorAnim_getFrame(s_menuActor, &buttonRect);

			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
				s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
			{
				s_buttonHover = JTRUE;
				if (ltime_isFrameReady())
				{
					if (s_buttonPressed == BRIEF_BTN_UP)
					{
						missionBriefing_scroll(-BRIEF_LINE_SCROLL);
					}
					else if (s_buttonPressed == BRIEF_BTN_DOWN)
					{
						missionBriefing_scroll(BRIEF_LINE_SCROLL);
					}
				}
			}
			else
			{
				s_buttonHover = JFALSE;
			}
		}
		else
		{
			// Mouse wheel support.
			if (wdy > 0) // up
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL * wdy);
			}
			else if (wdy < 0) // down
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL * wdy);
			}

			if (s_buttonPressed >= 0 && s_buttonHover)
			{
				switch (s_buttonPressed)
				{
					case BRIEF_BTN_OK:
					{
						*abort = JFALSE;
						exitBriefing = JTRUE;
					} break;
					case BRIEF_BTN_UP:
					{
						missionBriefing_scroll(-BRIEF_LINE_SCROLL);
					} break;
					case BRIEF_BTN_DOWN:
					{
						missionBriefing_scroll(BRIEF_LINE_SCROLL);
					} break;
					case BRIEF_BTN_CANCEL:
					{
						*abort = JTRUE;
						exitBriefing = JTRUE;
					} break;
					case BRIEF_BTN_EASY:
					{
						s_skill = 0;
					} break;
					case BRIEF_BTN_MEDIUM:
					{
						s_skill = 1;
					} break;
					case BRIEF_BTN_HARD:
					{
						s_skill = 2;
					} break;
				}
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = JFALSE;
		}

		// Keyboard shortcuts.

		// These need to be timer limited so that the scrolling works correctly.
		if (ltime_isFrameReady())
		{
			s_keyPressed = -1;
			if (TFE_Input::keyDown(KEY_UP))
			{
				s_keyPressed = BRIEF_BTN_UP;
				missionBriefing_scroll(-BRIEF_LINE_SCROLL);
			}
			else if (TFE_Input::keyDown(KEY_DOWN))
			{
				s_keyPressed = BRIEF_BTN_DOWN;
				missionBriefing_scroll(BRIEF_LINE_SCROLL);
			}
			
			if (TFE_Input::keyDown(KEY_PAGEUP))
			{
				missionBriefing_scroll(-BRIEF_PAGE_SCROLL);
			}
			else if (TFE_Input::keyDown(KEY_PAGEDOWN))
			{
				missionBriefing_scroll(BRIEF_PAGE_SCROLL);
			}
		}

		if (TFE_Input::keyPressed(s_langKeys->k_easy))
		{
			s_keyPressed = BRIEF_BTN_EASY;
			s_skill = 0;
		}
		else if (TFE_Input::keyPressed(s_langKeys->k_med))
		{
			s_keyPressed = BRIEF_BTN_MEDIUM;
			s_skill = 1;
		}
		else if (TFE_Input::keyPressed(s_langKeys->k_hard))
		{
			s_keyPressed = BRIEF_BTN_HARD;
			s_skill = 2;
		}

		if (TFE_Input::keyPressed(s_langKeys->k_canc) || TFE_Input::keyPressed(KEY_ESCAPE))
		{
			*abort = JTRUE;
			exitBriefing = JTRUE;
		}
		else if (TFE_Input::keyPressed(KEY_O) || TFE_Input::keyPressed(KEY_RETURN))
		{
			*abort = JFALSE;
			exitBriefing = JTRUE;
		}

		return exitBriefing;
#endif
	}

	JBool missionBriefing_update(s32* skill, JBool* abort)
	{
		if (!s_briefingOpen)
		{
			*skill = s_skill;
			*abort = JFALSE;
			return JFALSE;
		}
		tfe_updateLTime();

		// Input
		if (missionBriefing_handleInput(abort))
		{
			s_briefingOpen = JFALSE;
			*skill = s_skill;
			vfb_forceToBlack();
			lcanvas_clear();
			return JFALSE;
		}

		// Background
		lcanvas_eraseRect(&s_viewBounds);
		lactor_setState(s_menuActor, 0, 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);

#ifndef _XBOX
		// Buttons
		for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
		{
			drawButton(BriefingButton(i));
		}
#endif

		// Briefing Text.
		LRect rect = s_missionTextRect;
		s16 diff = ((rect.right - rect.left) - s_briefActor->w) >> 1;
		s16 x = diff + rect.left - s_briefActor->x;
		s16 y = (rect.top - s_briefActor->y) - s_briefY;

		lcanvas_setClip(&s_missionTextRect);
		lactorDelt_draw(s_briefActor, &rect, &s_missionTextRect, x, y, JTRUE);
		lcanvas_clearClipRect();

#ifndef _XBOX
		menu_blitCursor(s_cursorPos.x, s_cursorPos.z, s_framebuffer);
#else
		missionBriefing_drawXboxFooter();
#endif
		menu_blitToScreen();
		return JTRUE;
	}
	
	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////
}
