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
#ifdef _XBOX
#include <TFE_RenderBackend/renderBackend_xbox.h>
#endif
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
	static bool s_xboxPrevA = false;
	static bool s_xboxPrevB = false;
	static bool s_xboxPrevX = false;
	static bool s_xboxPrevY = false;
	static bool s_xboxPrevBack = false;
	static bool s_xboxPrevLB = false;
	static bool s_xboxPrevRB = false;
	static u8 s_xboxBriefingFrame[640 * 480];
	static u8 s_xboxPortraitSource[320 * 200];
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
#ifdef _XBOX
		TFE_RenderBackend::xboxSetBriefingFooter(false, true, 1);
#endif
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
			s_skill = (s_skill + 1) % 3;
			TFE_System::logWrite(LOG_MSG, "MissionBriefing", "difficulty=%d", s_skill);
		}
		else if (yPressed)
		{
		}

		s_xboxNavY = axisY;
		return exitBriefing;
	}

	void missionBriefing_drawXboxFooter()
	{
		TFE_RenderBackend::xboxSetBriefingFooter(true, true, s_skill);
	}

	static void missionBriefing_xboxFillRect(u8* dst, s32 x, s32 y, s32 w, s32 h, u8 color)
	{
		if (!dst || w <= 0 || h <= 0) return;
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > 640) w = 640 - x;
		if (y + h > 480) h = 480 - y;
		if (w <= 0 || h <= 0) return;

		for (s32 yy = 0; yy < h; yy++)
		{
			memset(dst + (y + yy) * 640 + x, color, w);
		}
	}

	static void missionBriefing_xboxStrokeRect(u8* dst, s32 x, s32 y, s32 w, s32 h, u8 color)
	{
		missionBriefing_xboxFillRect(dst, x, y, w, 2, color);
		missionBriefing_xboxFillRect(dst, x, y + h - 2, w, 2, color);
		missionBriefing_xboxFillRect(dst, x, y, 2, h, color);
		missionBriefing_xboxFillRect(dst, x + w - 2, y, 2, h, color);
	}

	static void missionBriefing_xboxTriangle(u8* dst, s32 cx, s32 y, s32 halfW, s32 h, JBool up, u8 color)
	{
		if (!dst) return;
		for (s32 row = 0; row < h; row++)
		{
			const s32 span = up ? row : (h - 1 - row);
			const s32 w = (span * halfW) / (h - 1);
			for (s32 x = cx - w; x <= cx + w; x++)
			{
				const s32 py = y + row;
				if (x < 0 || x >= 640 || py < 0 || py >= 480) continue;
				dst[py * 640 + x] = color;
			}
		}
	}

	static void missionBriefing_xboxBlitScaled(u8* dst, const u8* src, s32 sx, s32 sy, s32 sw, s32 sh,
		s32 dx, s32 dy, s32 scale, u8 transparent)
	{
		if (!dst || !src || scale <= 0) return;
		for (s32 y = 0; y < sh; y++)
		{
			const s32 srcY = sy + y;
			if (srcY < 0 || srcY >= 200) continue;
			for (s32 x = 0; x < sw; x++)
			{
				const s32 srcX = sx + x;
				if (srcX < 0 || srcX >= 320) continue;
				const u8 c = src[srcY * 320 + srcX];
				if (c == transparent) continue;

				const s32 outX = dx + x * scale;
				const s32 outY = dy + y * scale;
				for (s32 yy = 0; yy < scale; yy++)
				{
					const s32 py = outY + yy;
					if (py < 0 || py >= 480) continue;
					for (s32 xx = 0; xx < scale; xx++)
					{
						const s32 px = outX + xx;
						if (px < 0 || px >= 640) continue;
						dst[py * 640 + px] = c;
					}
				}
			}
		}
	}

	static void missionBriefing_xboxBlitBriefingScaledClipped(u8* dst, const u8* src, s32 sx, s32 sy, s32 sw, s32 sh,
		s32 dx, s32 dy, s32 scale, u8 black, u8 bg0, u8 bg1, u8 bg2,
		s32 clipX, s32 clipY, s32 clipW, s32 clipH)
	{
		if (!dst || !src || scale <= 0) return;
		const s32 clipR = clipX + clipW;
		const s32 clipB = clipY + clipH;
		for (s32 y = 0; y < sh; y++)
		{
			const s32 srcY = sy + y;
			if (srcY < 0 || srcY >= 200) continue;
			for (s32 x = 0; x < sw; x++)
			{
				const s32 srcX = sx + x;
				if (srcX < 0 || srcX >= 320) continue;
				const u8 c = src[srcY * 320 + srcX];
				if (c == black) continue;
				if (c == bg0 || c == bg1 || c == bg2) continue;

				const s32 outX = dx + x * scale;
				const s32 outY = dy + y * scale;
				for (s32 yy = 0; yy < scale; yy++)
				{
					const s32 py = outY + yy;
					if (py < clipY || py >= clipB || py < 0 || py >= 480) continue;
					for (s32 xx = 0; xx < scale; xx++)
					{
						const s32 px = outX + xx;
						if (px < clipX || px >= clipR || px < 0 || px >= 640) continue;
						dst[py * 640 + px] = c;
					}
				}
			}
		}
	}

	static void missionBriefing_blitXboxNative(const u8* portraitSrc, const u8* briefingSrc)
	{
		if (!portraitSrc || !briefingSrc) return;

		const u8 black = portraitSrc[5 * 320 + 5];
		const u8 brown = portraitSrc[80 * 320 + 150];
		const u8 green = portraitSrc[24 * 320 + 155];
		const u8 green2 = portraitSrc[165 * 320 + 155];
		const u8 border = portraitSrc[16 * 320 + 141];
		const s32 redSampleX = s_missionTextRect.left + 80;
		const s32 redSampleY = s_missionTextRect.top + 28;
		const u8 red = briefingSrc[(redSampleY >= 0 && redSampleY < 200 && redSampleX >= 0 && redSampleX < 320) ?
			redSampleY * 320 + redSampleX : 0];

		memset(s_xboxBriefingFrame, black, sizeof(s_xboxBriefingFrame));

		const s32 panelX = 230;
		const s32 panelY = 50;
		const s32 panelW = 405;
		const s32 panelH = 356;
		const s32 innerX = panelX + 10;
		const s32 innerW = panelW - 25;
		const s32 headerY = panelY + 10;
		const s32 headerH = 80;
		const s32 bodyY = headerY + headerH;
		const s32 bodyH = 230;
		const s32 sectionY = bodyY + bodyH;
		const s32 sectionH = 36;
		const s32 textClipW = innerW - 20;
		const s32 textClipH = sectionY + sectionH - headerY;
		const s32 arrowX = panelX + panelW - 15;

		missionBriefing_xboxFillRect(s_xboxBriefingFrame, panelX, panelY, panelW, panelH, brown);
		missionBriefing_xboxFillRect(s_xboxBriefingFrame, innerX, headerY, innerW, headerH, green);
		missionBriefing_xboxFillRect(s_xboxBriefingFrame, innerX, bodyY, innerW, bodyH, brown);
		missionBriefing_xboxFillRect(s_xboxBriefingFrame, innerX, sectionY, innerW, sectionH, green2);
		missionBriefing_xboxStrokeRect(s_xboxBriefingFrame, panelX, panelY, panelW, panelH, border);
		missionBriefing_xboxStrokeRect(s_xboxBriefingFrame, innerX - 1, headerY - 1, innerW + 2, sectionY + sectionH - headerY + 2, border);
		missionBriefing_xboxTriangle(s_xboxBriefingFrame, arrowX, bodyY + 18, 7, 10, JTRUE, red);
		missionBriefing_xboxTriangle(s_xboxBriefingFrame, arrowX, bodyY + bodyH - 28, 7, 10, JFALSE, red);

		missionBriefing_xboxBlitScaled(s_xboxBriefingFrame, portraitSrc, 0, 10, 108, 150, 16, 54, 2, black);
		missionBriefing_xboxBlitBriefingScaledClipped(s_xboxBriefingFrame, briefingSrc,
			s_missionTextRect.left, s_missionTextRect.top,
			s_missionTextRect.right - s_missionTextRect.left,
			s_missionTextRect.bottom - s_missionTextRect.top,
			innerX, headerY, 2, black, brown, green, green2, innerX, headerY, textClipW, textClipH);

		vfb_setResolution(640, 480);
		memcpy(vfb_getCpuBuffer(), s_xboxBriefingFrame, sizeof(s_xboxBriefingFrame));
		vfb_swap();
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

#ifndef _XBOX
		// Background
		lcanvas_eraseRect(&s_viewBounds);
		lactor_setState(s_menuActor, 0, 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);

		// Buttons
		for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
		{
			drawButton(BriefingButton(i));
		}
#else
		// Portrait source pass only. Do not present this buffer; it contains
		// legacy briefing chrome and buttons that the Xbox UI replaces.
		lcanvas_eraseRect(&s_viewBounds);
		lactor_setState(s_menuActor, 0, 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);
		memcpy(s_xboxPortraitSource, ldraw_getBitmap(), sizeof(s_xboxPortraitSource));

		// Clean briefing-content pass. This isolates the DELT text and inline
		// art from the old Easy/Med/Hard/Cancel/OK controls.
		lcanvas_clear();
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
		menu_blitToScreen();
#else
		missionBriefing_drawXboxFooter();
		missionBriefing_blitXboxNative(s_xboxPortraitSource, ldraw_getBitmap());
#endif
		return JTRUE;
	}
	
	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////
}
