#include <cstring>

#include "escapeMenu.h"
#include "delt.h"
#include "uiDraw.h"
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/config.h>
#include <TFE_DarkForces/cheats.h>
#include <TFE_DarkForces/player.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Game/reticle.h>
#include <TFE_Archive/archive.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/roffscreenBuffer.h>
#include <TFE_System/system.h>
#ifdef _XBOX
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Input/input.h>
#include <TFE_Input/input_xbox.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#endif

using namespace TFE_Jedi;
using namespace TFE_Input;

namespace TFE_DarkForces
{
	enum ConfirmDlg
	{
		CONFIRM_NEXT_BG = 0,
		CONFIRM_ABORT_BG,
		CONFIRM_QUIT_BG,
		// Next / Abort
		CONFIRM_NEXT_NOBTN_DOWN,
		CONFIRM_NEXT_NOBTN_UP,
		CONFIRM_NEXT_YESBTN_DOWN,
		CONFIRM_NEXT_YESBTN_UP,
		// Quit
		CONFIRM_QUIT_NOBTN_DOWN,
		CONFIRM_QUIT_NOBTN_UP,
		CONFIRM_QUIT_YESBTN_DOWN,
		CONFIRM_QUIT_YESBTN_UP,
		CONFIRM_COUNT
	};

	enum ConfirmState
	{
		CONFIRM_STATE_NONE = 0,
		CONFIRM_STATE_ABORT,
		CONFIRM_STATE_NEXT,
		CONFIRM_STATE_QUIT,
		CONFIRM_STATE_CONT
	};

	enum EscapeButtons
	{
#ifdef _XBOX
		ESC_BTN_RESUME,
		ESC_BTN_PDA,
		ESC_BTN_ABORT,
		ESC_BTN_QUICKSAVE,
		ESC_BTN_OPTIONS,
		ESC_BTN_CHEAT,
#else
		ESC_BTN_ABORT,
		ESC_BTN_CONFIG,
		ESC_BTN_QUIT,
		ESC_BTN_RETURN,
#endif
		ESC_BTN_COUNT
	};
	enum ConfirmButtons
	{
		CONFIRM_NO = 0,
		CONFIRM_YES,
		CONFIRM_BTN_COUNT
	};
	enum
	{
		XBOX_CHEAT_COUNT = 10,
		XBOX_CHEAT_GIVE_ALL = 9
	};
	enum XboxPauseOptionsPage
	{
		XPOPAGE_ROOT = 0,
		XPOPAGE_CONTROLS,
		XPOPAGE_VIDEO,
		XPOPAGE_AUDIO
	};
	static Vec2i c_escButtons[ESC_BTN_COUNT] =
	{
#ifdef _XBOX
		{64, 35},	// ESC_RESUME
		{64, 55},	// ESC_PDA
		{64, 75},	// ESC_ABORT
		{64, 95},	// ESC_QUICKSAVE
		{64, 115},	// ESC_OPTIONS
		{64, 135},	// ESC_CHEAT
#else
		{64, 35},	// ESC_ABORT
		{64, 55},	// ESC_CONFIG
		{64, 75},	// ESC_QUIT
		{64, 99},	// ESC_RETURN
#endif
	};
	static const Vec2i c_escButtonDim = { 96, 16 };
	static Vec4i s_confirmButtonRange[4];
	static u32 s_escMenuPalette[256];

	struct EscapeMenuState
	{
		JBool escMenuOpen;

		u32 escMenuFrameCount;
		DeltFrame* escMenuFrames;

		u32 confirmMenuFrameCount;
		DeltFrame* confirmMenuFrames;

		OffScreenBuffer* framebufferCopy;
		u8* framebuffer;

		Vec2i cursorPosAccum;
		Vec2i cursorPos;
		s32   buttonPressed;
		bool  buttonHover;
		ConfirmState confirmState;
#ifdef _XBOX
		s32   quickSaveStatus;
		bool  quickSaveWaitRelease;
		bool  quickSaveClosePending;
		bool  optionsOpen;
		s32   optionsSelection;
		s32   optionsScroll;
		s32   optionsItemCount;
		s32   optionsPage;
		u32   optionsFrame;
		bool  optionsStickUpHeld;
		bool  optionsStickDownHeld;
		bool  optionsStickLeftHeld;
		bool  optionsStickRightHeld;
		TFE_RenderBackend::XboxOptionsItem optionsItems[32];
		bool  cheatsOpen;
		s32   cheatsSelection;
		s32   cheatsScroll;
		bool  cheatsStickUpHeld;
		bool  cheatsStickDownHeld;
		TFE_RenderBackend::XboxCheatItem cheatsItems[XBOX_CHEAT_COUNT];
#endif

		RenderTargetHandle renderTarget;
		LangHotkeys* langKeys;

		EscapeMenuState()
			: escMenuOpen(JFALSE), escMenuFrameCount(0), escMenuFrames(NULL)
			, confirmMenuFrameCount(0), confirmMenuFrames(NULL)
			, framebufferCopy(NULL), framebuffer(NULL)
			, buttonPressed(-1), buttonHover(false)
			, confirmState(CONFIRM_STATE_NONE)
#ifdef _XBOX
			, quickSaveStatus(0), quickSaveWaitRelease(false), quickSaveClosePending(false)
			, optionsOpen(false), optionsSelection(0), optionsScroll(0), optionsItemCount(0)
			, optionsPage(XPOPAGE_ROOT), optionsFrame(0)
			, optionsStickUpHeld(false), optionsStickDownHeld(false)
			, optionsStickLeftHeld(false), optionsStickRightHeld(false)
			, cheatsOpen(false), cheatsSelection(0), cheatsScroll(0)
			, cheatsStickUpHeld(false), cheatsStickDownHeld(false)
#endif
			, renderTarget(NULL), langKeys(NULL)
		{
			cursorPosAccum.x = 0; cursorPosAccum.z = 0;
			cursorPos.x = 0;      cursorPos.z = 0;
		}
	};
	static EscapeMenuState s_emState;

	enum XboxPauseRootOptionIndex
	{
		XPROOT_CONTROLS = 0,
		XPROOT_VIDEO,
		XPROOT_AUDIO,
		XPROOT_COUNT
	};

	enum XboxPauseControlsOptionIndex
	{
		XPCTRL_LOOK_SENS_X = 0,
		XPCTRL_LOOK_SENS_Y,
		XPCTRL_RIGHT_STICK_DEADZONE,
		XPCTRL_BIND_JUMP,
		XPCTRL_BIND_CROUCH,
		XPCTRL_BIND_USE,
		XPCTRL_BIND_PRIMARY,
		XPCTRL_BIND_SECONDARY,
		XPCTRL_BIND_AUTOMAP,
		XPCTRL_BIND_PREV_WEAPON,
		XPCTRL_BIND_NEXT_WEAPON,
		XPCTRL_BIND_HEADLAMP,
		XPCTRL_BIND_CLEATS,
		XPCTRL_BIND_NIGHTVISION,
		XPCTRL_BIND_GASMASK,
		XPCTRL_BIND_MENU,
		XPCTRL_COUNT
	};

	enum XboxPauseVideoOptionIndex
	{
		XPVID_SAFE_ZONE = 0,
		XPVID_SCREEN_X,
		XPVID_SCREEN_Y,
		XPVID_COUNT
	};

	enum XboxPauseAudioOptionIndex
	{
		XPAUD_MASTER_VOLUME = 0,
		XPAUD_SFX_VOLUME,
		XPAUD_MUSIC_VOLUME,
		XPAUD_CUTSCENE_SFX,
		XPAUD_CUTSCENE_MUSIC,
		XPAUD_COUNT
	};

	enum XboxPauseOptionIconId
	{
		XPICON_A = 0,
		XPICON_B,
		XPICON_X,
		XPICON_Y,
		XPICON_WHITE,
		XPICON_BLACK,
		XPICON_START,
		XPICON_LSTICK,
		XPICON_LSTICK_DPAD,
		XPICON_LSTICK_SMALL,
		XPICON_RSTICK,
		XPICON_RSTICK_DPAD,
		XPICON_RSTICK_SMALL,
		XPICON_BACK,
		XPICON_DPAD,
		XPICON_DPAD_UP,
		XPICON_DPAD_DOWN,
		XPICON_DPAD_LEFT,
		XPICON_DPAD_RIGHT,
		XPICON_LT,
		XPICON_RT
	};

	struct XboxPauseBindingOption
	{
		s32 option;
		TFE_Input::InputAction action;
		const char* label;
	};

	static const XboxPauseBindingOption c_xboxPauseBindingOptions[] =
	{
		{ XPCTRL_BIND_JUMP,        TFE_Input::IADF_JUMP,             "JUMP" },
		{ XPCTRL_BIND_CROUCH,      TFE_Input::IADF_CROUCH,           "CROUCH" },
		{ XPCTRL_BIND_USE,         TFE_Input::IADF_USE,              "USE" },
		{ XPCTRL_BIND_PRIMARY,     TFE_Input::IADF_PRIMARY_FIRE,     "PRIMARY FIRE" },
		{ XPCTRL_BIND_SECONDARY,   TFE_Input::IADF_SECONDARY_FIRE,   "SECONDARY FIRE" },
		{ XPCTRL_BIND_AUTOMAP,     TFE_Input::IADF_AUTOMAP,          "AUTOMAP" },
		{ XPCTRL_BIND_PREV_WEAPON, TFE_Input::IADF_CYCLEWPN_PREV,    "PREV WEAPON" },
		{ XPCTRL_BIND_NEXT_WEAPON, TFE_Input::IADF_CYCLEWPN_NEXT,    "NEXT WEAPON" },
		{ XPCTRL_BIND_HEADLAMP,    TFE_Input::IADF_HEAD_LAMP_TOGGLE, "HEADLAMP" },
		{ XPCTRL_BIND_CLEATS,      TFE_Input::IADF_CLEATS_TOGGLE,    "CLEATS" },
		{ XPCTRL_BIND_NIGHTVISION, TFE_Input::IADF_NIGHT_VISION_TOG, "NIGHT VISION" },
		{ XPCTRL_BIND_GASMASK,     TFE_Input::IADF_GAS_MASK_TOGGLE,  "GAS MASK" },
		{ XPCTRL_BIND_MENU,        TFE_Input::IADF_MENU_TOGGLE,      "PAUSE MENU" },
	};
	static bool s_xboxPauseOptionsCapture = false;

	void escMenu_resetCursor();
	void escMenu_handleMousePosition();
	bool escapeMenu_getTextures(TextureInfoList& texList, AssetPool pool);
	void escapeMenu_draw(JBool drawMouse, JBool drawBackground);
	EscapeMenuAction escapeMenu_updateUI();
#ifdef _XBOX
	void escapeMenu_drawXboxOverlay();
	EscapeMenuAction escapeMenu_updateXboxUI();
	const char* escapeMenu_xboxButtonName(s32 button);
	static void xboxOpenOptions();
	static void xboxOpenCheats();
#endif

	extern void pauseLevelSound();
	extern void resumeLevelSound();
	extern void clearBufferedSound();

	void escapeMenu_resetState()
	{
#ifdef _XBOX
		TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false);
		TFE_RenderBackend::xboxSetOptionsScreen(false, true, 0, 0, 0, NULL, 0);
		TFE_RenderBackend::xboxSetCheatScreen(false, 0, 0, NULL, 0);
#endif
		// TFE: GPU Support.
		if (s_emState.renderTarget)
		{
			TFE_RenderBackend::freeRenderTarget(s_emState.renderTarget);
		}

		// Free memory
		freeOffScreenBuffer(s_emState.framebufferCopy);

		// Clear State.
		memset(&(s_emState), 0, sizeof(s_emState));
	}

	Vec4i getButtonRange(DeltFrame* frames, s32 index)
	{
		Vec4i range;
		ScreenRect rect;
		getDeltaFrameRect(&frames[index], &rect);
		range.x = rect.left;
		range.y = rect.top;
		range.z = rect.right;
		range.w = rect.bot;
		return range;
	}
			
	void escapeMenu_load(LangHotkeys* langKeys)
	{
		s_emState.langKeys = langKeys;
		if (!s_emState.escMenuFrames)
		{
			u8 paletteBuffer[768] = { 0 };

			FilePath filePath;
			if (!TFE_Paths::getFilePath("MENU.LFD", &filePath)) { return; }
			Archive* archive = Archive::getArchive(ARCHIVE_LFD, "MENU", filePath.path);
			TFE_Paths::addLocalArchive(archive);
				s_emState.escMenuFrameCount = getFramesFromAnim("escmenu.anim", &s_emState.escMenuFrames);
				s_emState.confirmMenuFrameCount = getFramesFromAnim("yesno.anim", &s_emState.confirmMenuFrames);
				loadPaletteFromPltt("menu.pltt", paletteBuffer);
			TFE_Paths::removeLastArchive();

			// Adjust button ranges since different languages seem to move the menu around for some reason...
			Vec4i range = getButtonRange(s_emState.escMenuFrames, 0);
			s32 dx = range.x - 36;
			s32 dy = range.y - 25;
			for (s32 i = 0; i < ESC_BTN_COUNT; i++)
			{
				c_escButtons[i].x += dx;
				c_escButtons[i].z += dy;
			}

			// Get confirmation button positions.
			s_confirmButtonRange[0] = getButtonRange(s_emState.confirmMenuFrames, CONFIRM_NEXT_NOBTN_DOWN);
			s_confirmButtonRange[1] = getButtonRange(s_emState.confirmMenuFrames, CONFIRM_NEXT_YESBTN_DOWN);

			s_confirmButtonRange[2] = getButtonRange(s_emState.confirmMenuFrames, CONFIRM_QUIT_NOBTN_DOWN);
			s_confirmButtonRange[3] = getButtonRange(s_emState.confirmMenuFrames, CONFIRM_QUIT_YESBTN_DOWN);
			
			// TFE
			TFE_Jedi::renderer_addHudTextureCallback(escapeMenu_getTextures);

			// convert palette to argb entries now since we don't need the raw format anywhere.
			u8* pal = paletteBuffer;
			for (u32 i = 0; i < 256; i++, pal += 3)
			{
				s_escMenuPalette[i] = 0xffu << 24 | ((u32)pal[0]) | ((u32)(pal[1]) << 8) | ((u32)pal[2] << 16);
			}
			texturepacker_setConversionPalette(0, 8, paletteBuffer);
		}
	}

	void escapeMenu_copyBackground(u8* framebuffer, u8* palette)
	{
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);

		if (TFE_Jedi::getSubRenderer() == TSR_CLASSIC_GPU)
		{
			// 1. Create a render target to hold the frame.
			u32 prevWidth = 0, prevHeight = 0;
			if (s_emState.renderTarget)
			{
				TFE_RenderBackend::getRenderTargetDim(s_emState.renderTarget, &prevWidth, &prevHeight);
			}

			if (!s_emState.renderTarget || prevWidth != dispWidth || prevHeight != dispHeight)
			{
				TFE_RenderBackend::freeRenderTarget(s_emState.renderTarget);
				s_emState.renderTarget = TFE_RenderBackend::createRenderTarget(dispWidth, dispHeight);
			}

			// 2. Blit current frame to the new render target.
			TFE_Jedi::endRender();
#ifdef _XBOX
			TFE_RenderBackend::copyBackbufferToRenderTarget(s_emState.renderTarget);
#else
			TFE_RenderBackend::swap(true);

			TFE_RenderBackend::copyBackbufferToRenderTarget(s_emState.renderTarget);
#endif
			TFE_RenderBackend::unbindRenderTarget();
		}
		else // Software renderer code.
		{
			if (s_emState.framebufferCopy && (s_emState.framebufferCopy->width != dispWidth || s_emState.framebufferCopy->height != dispHeight))
			{
				freeOffScreenBuffer(s_emState.framebufferCopy);
				s_emState.framebufferCopy = nullptr;
			}

			if (!s_emState.framebufferCopy)
			{
				s_emState.framebufferCopy = createOffScreenBuffer(dispWidth, dispHeight, OBF_NONE);
			}
			memcpy(s_emState.framebufferCopy->image, framebuffer, s_emState.framebufferCopy->size);
			s_emState.framebuffer = framebuffer;

			// Post process to convert sceen capture to grayscale.
			for (s32 i = 0; i < s_emState.framebufferCopy->size; i++)
			{
				u8 color = s_emState.framebufferCopy->image[i];
				u8* rgb = &palette[color * 3];
				u8 luminance = ((rgb[1] >> 1) + (rgb[0] >> 2) + (rgb[2] >> 2)) >> 1;
				s_emState.framebufferCopy->image[i] = 63 - luminance;
			}
		}
	}

	void escapeMenu_open(u8* framebuffer, u8* palette)
	{
		// TFE
		reticle_enable(false);
		TFE_RenderBackend::bloomPostEnable(false);

		pauseLevelSound();
		s_emState.escMenuOpen = JTRUE;

		escapeMenu_copyBackground(framebuffer, palette);

		escMenu_resetCursor();
#ifdef _XBOX
		s_emState.buttonPressed = ESC_BTN_RESUME;
		s_emState.buttonHover = true;
#else
		s_emState.buttonPressed = -1;
		s_emState.buttonHover = false;
#endif
		s_emState.confirmState = CONFIRM_STATE_NONE;
	}

	void escapeMenu_resetLevel()
	{
		s_emState.escMenuOpen = JFALSE;
#ifdef _XBOX
		TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false);
#endif
	}

	void escapeMenu_close()
	{
		s_emState.escMenuOpen = JFALSE;
#ifdef _XBOX
		TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false);
#endif
		resumeLevelSound();

		// TFE
		reticle_enable(true);
		TFE_RenderBackend::bloomPostEnable(true);
	}

	JBool escapeMenu_isOpen()
	{
		return s_emState.escMenuOpen;
	}

	void escapeMenu_addDeltFrame(TextureInfoList& texList, DeltFrame* frame)
	{
		TextureInfo texInfo = {};
		texInfo.type = TEXINFO_DF_DELT_TEX;
		texInfo.texData = &frame->texture;
		texList.push_back(texInfo);
	}

	bool escapeMenu_getTextures(TextureInfoList& texList, AssetPool pool)
	{
		for (u32 i = 0; i < s_emState.escMenuFrameCount; i++)
		{
			s_emState.escMenuFrames[i].texture.palIndex = 0;
			escapeMenu_addDeltFrame(texList, &s_emState.escMenuFrames[i]);
		}
		for (u32 i = 0; i < s_emState.confirmMenuFrameCount; i++)
		{
			s_emState.confirmMenuFrames[i].texture.palIndex = 0;
			escapeMenu_addDeltFrame(texList, &s_emState.confirmMenuFrames[i]);
		}
		s_cursor.texture.palIndex = 0;
		escapeMenu_addDeltFrame(texList, &s_cursor);
		return true;
	}

	void escapeMenu_drawGpu(JBool drawMouse, JBool drawBackground)
	{
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);

		const fixed16_16 xScale = vfb_getXScale();
		const fixed16_16 yScale = vfb_getYScale();
		const s32 xOffset = vfb_getWidescreenOffset();

		// Draw the background.
		if (drawBackground)
		{
			if (xOffset)
			{
				screenGPU_addImageQuad(0, 0, dispWidth, dispHeight, (TextureGpu*)TFE_RenderBackend::getRenderTargetTexture(s_emState.renderTarget));
			}
			else
			{
				// Adjust the UV coordinates to stretch 4:3 to the full size.
				DisplayInfo displayInfo;
				TFE_RenderBackend::getDisplayInfo(&displayInfo);

				s32 offsetWidth = displayInfo.height * 4 / 3;
				s32 imgOffset = (displayInfo.width - offsetWidth) / 2;

				f32 offset = f32(imgOffset) / f32(displayInfo.width);
				f32 u0 = offset;
				f32 u1 = 1.0f - offset;
				screenGPU_addImageQuad(0, 0, dispWidth, dispHeight, u0, u1, (TextureGpu*)TFE_RenderBackend::getRenderTargetTexture(s_emState.renderTarget));
			}
		}

		// Draw the menu.
		if (s_emState.confirmState == CONFIRM_STATE_NONE)
		{
			screenGPU_blitTextureScaled(&s_emState.escMenuFrames[0].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);

			if (s_levelComplete)
			{
				// Attempt to clean up the button positions, note this is only a problem at non-vanilla resolutions.
				fixed16_16 yOffset = (dispHeight == 200 || dispHeight == 400) ? 0 : round16(yScale / 2);

				if (s_emState.buttonPressed == ESC_BTN_ABORT && s_emState.buttonHover)
				{
					screenGPU_blitTextureScaled(&s_emState.escMenuFrames[3].texture, nullptr, intToFixed16(xOffset), yOffset, xScale, yScale, 31);
				}
				else
				{
					screenGPU_blitTextureScaled(&s_emState.escMenuFrames[4].texture, nullptr, intToFixed16(xOffset), yOffset, xScale, yScale, 31);
				}
			}
			if ((s_emState.buttonPressed > ESC_BTN_ABORT || (s_emState.buttonPressed == ESC_BTN_ABORT && !s_levelComplete)) && s_emState.buttonHover)
			{
				// Attempt to clean up the button positions, note this is only a problem at non-vanilla resolutions.
				fixed16_16 yOffset = (dispHeight == 200 || dispHeight == 400) ? 0 : round16(yScale / 2);
				yOffset = min(yOffset, 3 - s_emState.buttonPressed);

				// Draw the highlight button
				const s32 highlightIndices[] = { 1, 7, 9, 5 };
				screenGPU_blitTextureScaled(&s_emState.escMenuFrames[highlightIndices[s_emState.buttonPressed]].texture, nullptr, intToFixed16(xOffset), yOffset, xScale, yScale, 31);
			}
		}
		// Confirmation.
		else if (s_emState.confirmState == CONFIRM_STATE_ABORT)
		{
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[CONFIRM_ABORT_BG].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_NEXT_YESBTN_DOWN : CONFIRM_NEXT_YESBTN_UP].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_NEXT_NOBTN_DOWN : CONFIRM_NEXT_NOBTN_UP].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
		}
		else if (s_emState.confirmState == CONFIRM_STATE_NEXT)
		{
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[CONFIRM_NEXT_BG].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_NEXT_YESBTN_DOWN : CONFIRM_NEXT_YESBTN_UP].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_NEXT_NOBTN_DOWN : CONFIRM_NEXT_NOBTN_UP].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
		}
		else if (s_emState.confirmState == CONFIRM_STATE_QUIT)
		{
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[CONFIRM_QUIT_BG].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_QUIT_YESBTN_DOWN : CONFIRM_QUIT_YESBTN_UP].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
			screenGPU_blitTextureScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_QUIT_NOBTN_DOWN : CONFIRM_QUIT_NOBTN_UP].texture, nullptr, intToFixed16(xOffset), 0, xScale, yScale, 31);
		}

		// Draw the mouse.
		if (drawMouse)
		{
			screenGPU_blitTextureScaled(&s_cursor.texture, nullptr, intToFixed16(s_emState.cursorPos.x), intToFixed16(s_emState.cursorPos.z), xScale, yScale, 31);
		}
	}

	void escapeMenu_draw(JBool drawMouse, JBool drawBackground)
	{
		// TFE Note: handle GPU drawing differently, though the UI update is exactly the same.
		if (TFE_Jedi::getSubRenderer() == TSR_CLASSIC_GPU)
		{
			escapeMenu_drawGpu(drawMouse, drawBackground);
			return;
		}

		// Draw the screen capture.
		ScreenRect* drawRect = vfb_getScreenRect(VFB_RECT_UI);
		u32 dispWidth, dispHeight;
		vfb_getResolution(&dispWidth, &dispHeight);

		if (dispWidth == 320 && dispHeight == 200)
		{
			if (drawBackground)
			{
				hud_drawElementToScreen(s_emState.framebufferCopy, drawRect, 0, 0, s_emState.framebuffer);
			}

			if (s_emState.confirmState == CONFIRM_STATE_NONE)
			{
				// Draw the menu background.
				blitDeltaFrame(&s_emState.escMenuFrames[0], 0, 0, s_emState.framebuffer);

				if (s_levelComplete)
				{
					if (s_emState.buttonPressed == ESC_BTN_ABORT && s_emState.buttonHover)
					{
						blitDeltaFrame(&s_emState.escMenuFrames[3], 0, 0, s_emState.framebuffer);
					}
					else
					{
						blitDeltaFrame(&s_emState.escMenuFrames[4], 0, 0, s_emState.framebuffer);
					}
				}
				if ((s_emState.buttonPressed > ESC_BTN_ABORT || (s_emState.buttonPressed == ESC_BTN_ABORT && !s_levelComplete)) && s_emState.buttonHover)
				{
					// Draw the highlight button
					const s32 highlightIndices[] = { 1, 7, 9, 5 };
					blitDeltaFrame(&s_emState.escMenuFrames[highlightIndices[s_emState.buttonPressed]], 0, 0, s_emState.framebuffer);
				}
			}
			// Confirmation.
			else if (s_emState.confirmState == CONFIRM_STATE_ABORT)
			{
				blitDeltaFrame(&s_emState.confirmMenuFrames[CONFIRM_ABORT_BG], 0, 0, s_emState.framebuffer);
				blitDeltaFrame(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_NEXT_YESBTN_DOWN : CONFIRM_NEXT_YESBTN_UP], 0, 0, s_emState.framebuffer);
				blitDeltaFrame(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_NEXT_NOBTN_DOWN : CONFIRM_NEXT_NOBTN_UP], 0, 0, s_emState.framebuffer);
			}
			else if (s_emState.confirmState == CONFIRM_STATE_NEXT)
			{
				blitDeltaFrame(&s_emState.confirmMenuFrames[CONFIRM_NEXT_BG], 0, 0, s_emState.framebuffer);
				blitDeltaFrame(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_NEXT_YESBTN_DOWN : CONFIRM_NEXT_YESBTN_UP], 0, 0, s_emState.framebuffer);
				blitDeltaFrame(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_NEXT_NOBTN_DOWN : CONFIRM_NEXT_NOBTN_UP], 0, 0, s_emState.framebuffer);
			}
			else if (s_emState.confirmState == CONFIRM_STATE_QUIT)
			{
				blitDeltaFrame(&s_emState.confirmMenuFrames[CONFIRM_QUIT_BG], 0, 0, s_emState.framebuffer);
				blitDeltaFrame(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_QUIT_YESBTN_DOWN : CONFIRM_QUIT_YESBTN_UP], 0, 0, s_emState.framebuffer);
				blitDeltaFrame(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_QUIT_NOBTN_DOWN : CONFIRM_QUIT_NOBTN_UP], 0, 0, s_emState.framebuffer);
			}

			// Draw the mouse.
			blitDeltaFrame(&s_cursor, s_emState.cursorPos.x, s_emState.cursorPos.z, s_emState.framebuffer);
		}
		else
		{
			const fixed16_16 xScale = vfb_getXScale();
			const fixed16_16 yScale = vfb_getYScale();
			const s32 xOffset = vfb_getWidescreenOffset();

			if (drawBackground)
			{
				hud_drawElementToScreen(s_emState.framebufferCopy, drawRect, 0, 0, s_emState.framebuffer);
			}

			if (s_emState.confirmState == CONFIRM_STATE_NONE)
			{
				// Draw the menu background.
				blitDeltaFrameScaled(&s_emState.escMenuFrames[0], xOffset, 0, xScale, yScale, s_emState.framebuffer);

				if (s_levelComplete)
				{
					// Attempt to clean up the button positions, note this is only a problem at non-vanilla resolutions.
					fixed16_16 yOffset = (dispHeight == 200 || dispHeight == 400) ? 0 : round16(yScale / 2);

					if (s_emState.buttonPressed == ESC_BTN_ABORT && s_emState.buttonHover)
					{
						blitDeltaFrameScaled(&s_emState.escMenuFrames[3], xOffset, yOffset, xScale, yScale, s_emState.framebuffer);
					}
					else
					{
						blitDeltaFrameScaled(&s_emState.escMenuFrames[4], xOffset, yOffset, xScale, yScale, s_emState.framebuffer);
					}
				}
				if ((s_emState.buttonPressed > ESC_BTN_ABORT || (s_emState.buttonPressed == ESC_BTN_ABORT && !s_levelComplete)) && s_emState.buttonHover)
				{
					// Attempt to clean up the button positions, note this is only a problem at non-vanilla resolutions.
					fixed16_16 yOffset = (dispHeight == 200 || dispHeight == 400) ? 0 : round16(yScale / 2);
					yOffset = min(yOffset, 3 - s_emState.buttonPressed);

					// Draw the highlight button
					const s32 highlightIndices[] = { 1, 7, 9, 5 };
					blitDeltaFrameScaled(&s_emState.escMenuFrames[highlightIndices[s_emState.buttonPressed]], xOffset, yOffset, xScale, yScale, s_emState.framebuffer);
				}
			}
			// Confirmation.
			else if (s_emState.confirmState == CONFIRM_STATE_ABORT)
			{
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[CONFIRM_ABORT_BG], xOffset, 0, xScale, yScale, s_emState.framebuffer);
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_NEXT_YESBTN_DOWN : CONFIRM_NEXT_YESBTN_UP], xOffset, 0, xScale, yScale, s_emState.framebuffer);
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_NEXT_NOBTN_DOWN : CONFIRM_NEXT_NOBTN_UP], xOffset, 0, xScale, yScale, s_emState.framebuffer);
			}
			else if (s_emState.confirmState == CONFIRM_STATE_NEXT)
			{
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[CONFIRM_NEXT_BG], xOffset, 0, xScale, yScale, s_emState.framebuffer);
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_NEXT_YESBTN_DOWN : CONFIRM_NEXT_YESBTN_UP], xOffset, 0, xScale, yScale, s_emState.framebuffer);
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_NEXT_NOBTN_DOWN : CONFIRM_NEXT_NOBTN_UP], xOffset, 0, xScale, yScale, s_emState.framebuffer);
			}
			else if (s_emState.confirmState == CONFIRM_STATE_QUIT)
			{
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[CONFIRM_QUIT_BG], xOffset, 0, xScale, yScale, s_emState.framebuffer);
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_YES ? CONFIRM_QUIT_YESBTN_DOWN : CONFIRM_QUIT_YESBTN_UP], xOffset, 0, xScale, yScale, s_emState.framebuffer);
				blitDeltaFrameScaled(&s_emState.confirmMenuFrames[s_emState.buttonPressed == CONFIRM_NO ? CONFIRM_QUIT_NOBTN_DOWN : CONFIRM_QUIT_NOBTN_UP], xOffset, 0, xScale, yScale, s_emState.framebuffer);
			}

			// Draw the mouse.
			if (drawMouse)
			{
				blitDeltaFrameScaled(&s_cursor, s_emState.cursorPos.x, s_emState.cursorPos.z, xScale, yScale, s_emState.framebuffer);
			}
		}
	}

	EscapeMenuAction escapeMenu_update()
	{
#ifndef _XBOX
		vfb_setPalette(s_escMenuPalette);
#endif

		EscapeMenuAction action = escapeMenu_updateUI();
		if (action != ESC_CONTINUE)
		{
			s_emState.escMenuOpen = JFALSE;
#ifdef _XBOX
			TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false);
			TFE_RenderBackend::xboxSetOptionsScreen(false, true, 0, 0, 0, NULL, 0);
			s_emState.optionsOpen = false;
#endif
			// Avoid sound pops due to buffered sound when returning to the Agent or Main menu.
			if (!s_levelComplete || action != ESC_ABORT_OR_NEXT)
			{
				clearBufferedSound();
			}
			resumeLevelSound();

			// TFE
			reticle_enable(true);
			TFE_RenderBackend::bloomPostEnable(true);
		}

		if (action == ESC_CONTINUE)
		{
#ifdef _XBOX
			escapeMenu_drawXboxOverlay();
#else
			escapeMenu_draw(JTRUE, JTRUE);
#endif
		}
		return action;
	}

	///////////////////////////////////////
	// Internal
	///////////////////////////////////////
	EscapeMenuAction escapeMenu_handleAction(EscapeMenuAction action, s32 actionPressed)
	{
		if (s_emState.confirmState == CONFIRM_STATE_NONE)
		{
			if (actionPressed < 0)
			{
				// Handle keyboard shortcuts.
				if ((TFE_Input::keyPressed(KEY_A) && !s_levelComplete) || (TFE_Input::keyPressed(KEY_N) && s_levelComplete))
				{
					actionPressed = ESC_BTN_ABORT;
				}
				if (TFE_Input::keyPressed(s_emState.langKeys->k_conf))
				{
#ifdef _XBOX
					actionPressed = ESC_BTN_OPTIONS;
#else
					actionPressed = ESC_BTN_CONFIG;
#endif
				}
				if (TFE_Input::keyPressed(s_emState.langKeys->k_quit))
				{
#ifndef _XBOX
					actionPressed = ESC_BTN_QUIT;
#endif
				}
				if (TFE_Input::keyPressed(s_emState.langKeys->k_cont))
				{
#ifdef _XBOX
					actionPressed = ESC_BTN_RESUME;
#else
					actionPressed = ESC_BTN_RETURN;
#endif
				}
			}

			switch (actionPressed)
			{
#ifdef _XBOX
			case ESC_BTN_RESUME:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "action Resume");
				action = ESC_RETURN;
				break;
			case ESC_BTN_PDA:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "action Datapad");
				action = ESC_PDA;
				break;
			case ESC_BTN_QUICKSAVE:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "action Quick Save");
				{
					TFE_System::logWrite(LOG_MSG, "PauseMenu", "quick save begin");
					char quickSaveName[TFE_MAX_PATH];
					TFE_SaveSystem::getQuickSaveFilename(quickSaveName, TFE_MAX_PATH);
					const bool saved = TFE_SaveSystem::saveGame(quickSaveName, "Quicksave");
					s_emState.quickSaveStatus = saved ? 1 : 2;
					s_emState.quickSaveWaitRelease = true;
					s_emState.quickSaveClosePending = false;
					s_emState.buttonPressed = ESC_BTN_QUICKSAVE;
					s_emState.buttonHover = true;
					TFE_System::logWrite(saved ? LOG_MSG : LOG_ERROR, "PauseMenu", "quick save %s; waiting for acknowledgement", saved ? "complete" : "failed");
					action = ESC_CONTINUE;
				}
				break;
			case ESC_BTN_CHEAT:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "Cheats selected");
				xboxOpenCheats();
				break;
			case ESC_BTN_ABORT:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "open Abort Mission confirm");
				s_emState.confirmState = s_levelComplete ? CONFIRM_STATE_NEXT : CONFIRM_STATE_ABORT;
				s_emState.buttonPressed = CONFIRM_NO;
				s_emState.buttonHover = true;
				break;
			case ESC_BTN_OPTIONS:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "Options selected");
				xboxOpenOptions();
				break;
#else
			case ESC_BTN_CONFIG:
				action = ESC_CONFIG;
				break;
			case ESC_BTN_QUIT:
				s_emState.confirmState = CONFIRM_STATE_QUIT;
				break;
			case ESC_BTN_RETURN:
				action = ESC_RETURN;
				break;
#endif
			};
		}
		else
		{
			if (actionPressed < 0)
			{
				if (TFE_Input::keyPressed(s_emState.langKeys->k_yes))
				{
					actionPressed = CONFIRM_YES;
				}
				else if (TFE_Input::keyPressed(KEY_N))
				{
					actionPressed = CONFIRM_NO;
				}
				else if (TFE_Input::keyPressed(KEY_RETURN))
				{
					actionPressed = s_emState.confirmState == CONFIRM_STATE_NEXT ? CONFIRM_YES : CONFIRM_NO;
				}
				else if (TFE_Input::keyPressed(KEY_ESCAPE))
				{
					actionPressed = CONFIRM_NO;
				}
			}
			switch (actionPressed)
			{
				case CONFIRM_YES:
					if (s_emState.confirmState == CONFIRM_STATE_ABORT || s_emState.confirmState == CONFIRM_STATE_NEXT)
					{
						TFE_System::logWrite(LOG_MSG, "PauseMenu", "confirm Yes -> abort/next");
						action = ESC_ABORT_OR_NEXT;
					}
					else
					{
						TFE_System::logWrite(LOG_MSG, "PauseMenu", "confirm Yes -> quit");
						action = ESC_QUIT;
					}
					break;
				case CONFIRM_NO:
					TFE_System::logWrite(LOG_MSG, "PauseMenu", "confirm No -> return to pause menu");
					s_emState.confirmState = CONFIRM_STATE_NONE;
#ifdef _XBOX
					s_emState.buttonPressed = ESC_BTN_RESUME;
					s_emState.buttonHover = true;
#endif
					break;
			};
#ifndef _XBOX
			s_emState.buttonHover = false;
			s_emState.buttonPressed = -1;
#endif
		}
		return action;
	}

	EscapeMenuAction escapeMenu_updateUI()
	{
#ifdef _XBOX
		return escapeMenu_updateXboxUI();
#endif
		EscapeMenuAction action = ESC_CONTINUE;
		escMenu_handleMousePosition();
		if (inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED || TFE_Input::keyPressed(KEY_ESCAPE))
		{
			action = ESC_RETURN;
			s_emState.escMenuOpen = JFALSE;
		}

		s32 x = s_emState.cursorPos.x;
		s32 z = s_emState.cursorPos.z;

		// Move into "UI space"
		fixed16_16 xScale = vfb_getXScale();
		fixed16_16 yScale = vfb_getYScale();
		x = floor16(div16(intToFixed16(x - vfb_getWidescreenOffset()), xScale));
		z = floor16(div16(intToFixed16(z), yScale));

		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_emState.buttonPressed = -1;
			if (s_emState.confirmState == CONFIRM_STATE_NONE)
			{
				for (u32 i = 0; i < ESC_BTN_COUNT; i++)
				{
					if (x >= c_escButtons[i].x && x < c_escButtons[i].x + c_escButtonDim.x &&
						z >= c_escButtons[i].z && z < c_escButtons[i].z + c_escButtonDim.z)
					{
						s_emState.buttonPressed = s32(i);
						s_emState.buttonHover = true;
						break;
					}
				}
			}
			else if (s_emState.confirmState == CONFIRM_STATE_ABORT || s_emState.confirmState == CONFIRM_STATE_NEXT)
			{
				for (u32 i = 0; i < 2; i++)
				{
					if (x >= s_confirmButtonRange[i].x && x < s_confirmButtonRange[i].z &&
						z >= s_confirmButtonRange[i].y && z < s_confirmButtonRange[i].w)
					{
						s_emState.buttonPressed = s32(i);
						s_emState.buttonHover = true;
						break;
					}
				}
			}
			else
			{
				for (u32 i = 0; i < 2; i++)
				{
					if (x >= s_confirmButtonRange[i+2].x && x < s_confirmButtonRange[i+2].z &&
						z >= s_confirmButtonRange[i+2].y && z < s_confirmButtonRange[i+2].w)
					{
						s_emState.buttonPressed = s32(i);
						s_emState.buttonHover = true;
						break;
					}
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_emState.confirmState == CONFIRM_STATE_NONE && s_emState.buttonPressed >= 0)
		{
			s_emState.buttonHover = false;
			// Verify that the mouse is still over the button.
			if (x >= c_escButtons[s_emState.buttonPressed].x && x < c_escButtons[s_emState.buttonPressed].x + c_escButtonDim.x &&
				z >= c_escButtons[s_emState.buttonPressed].z && z < c_escButtons[s_emState.buttonPressed].z + c_escButtonDim.z)
			{
				s_emState.buttonHover = true;
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && (s_emState.confirmState == CONFIRM_STATE_ABORT || s_emState.confirmState == CONFIRM_STATE_NEXT) && s_emState.buttonPressed >= 0)
		{
			s_emState.buttonHover = false;
			if (x >= s_confirmButtonRange[s_emState.buttonPressed].x && x < s_confirmButtonRange[s_emState.buttonPressed].z &&
				z >= s_confirmButtonRange[s_emState.buttonPressed].y && z < s_confirmButtonRange[s_emState.buttonPressed].w)
			{
				s_emState.buttonHover = true;
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_emState.confirmState == CONFIRM_STATE_QUIT && s_emState.buttonPressed >= 0)
		{
			s_emState.buttonHover = false;
			if (x >= s_confirmButtonRange[s_emState.buttonPressed+2].x && x < s_confirmButtonRange[s_emState.buttonPressed+2].z &&
				z >= s_confirmButtonRange[s_emState.buttonPressed+2].y && z < s_confirmButtonRange[s_emState.buttonPressed+2].w)
			{
				s_emState.buttonHover = true;
			}
		}
		else
		{
			action = escapeMenu_handleAction(action, (s_emState.buttonPressed >= 0 && s_emState.buttonHover) ? s_emState.buttonPressed : -1);
			// Reset.
			s_emState.buttonPressed = -1;
			s_emState.buttonHover = false;
		}

		return action;
	}

#ifdef _XBOX
	static const u8 c_xboxPausePanel = 13;
	static const u8 c_xboxPauseGreen = 12;
	static const u8 c_xboxPauseWhite = 47;

#include "xboxPauseFont.inc"

	const char* escapeMenu_xboxButtonName(s32 button)
	{
		switch (button)
		{
			case ESC_BTN_RESUME:  return "Resume";
			case ESC_BTN_PDA:     return "Datapad";
			case ESC_BTN_ABORT:   return "Abort Mission";
			case ESC_BTN_QUICKSAVE: return "Quick Save";
			case ESC_BTN_OPTIONS: return "Options";
			case ESC_BTN_CHEAT:   return "Cheats";
		}
		return "Unknown";
	}

	static s32 xboxScaleX(s32 x, s32 width)
	{
		return (x * width + 320) / 640;
	}

	static s32 xboxScaleY(s32 y, s32 height)
	{
		return (y * height + 240) / 480;
	}

	static void xboxDrawRect(u8* fb, s32 width, s32 height, s32 x, s32 y, s32 w, s32 h, u8 color)
	{
		if (w <= 0 || h <= 0 || x >= width || y >= height || x + w <= 0 || y + h <= 0) return;
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > width)  w = width - x;
		if (y + h > height) h = height - y;

		for (s32 iy = 0; iy < h; iy++)
		{
			memset(&fb[(y + iy) * width + x], color, w);
		}
	}

	static void xboxDrawRectBase(u8* fb, s32 width, s32 height, s32 x, s32 y, s32 w, s32 h, u8 color)
	{
		const s32 x0 = xboxScaleX(x, width);
		const s32 y0 = xboxScaleY(y, height);
		const s32 x1 = xboxScaleX(x + w, width);
		const s32 y1 = xboxScaleY(y + h, height);
		xboxDrawRect(fb, width, height, x0, y0, x1 - x0, y1 - y0, color);
	}

	static void xboxDrawHLineBase(u8* fb, s32 width, s32 height, s32 x, s32 y, s32 w, s32 thickness, u8 color)
	{
		xboxDrawRectBase(fb, width, height, x, y, w, thickness, color);
	}

	static void xboxDrawVLineBase(u8* fb, s32 width, s32 height, s32 x, s32 y, s32 h, s32 thickness, u8 color)
	{
		xboxDrawRectBase(fb, width, height, x, y, thickness, h, color);
	}

	static void xboxDrawTextMask(u8* fb, s32 width, s32 height, XboxPauseTextId id, s32 x, s32 y, bool selected, bool shadow)
	{
		const XboxPauseTextSprite* sprite = &c_xboxPauseText[id];
		const u8* data = sprite->data;
		const s32 dstX0 = xboxScaleX(x * 2, width);
		const s32 dstY0 = xboxScaleY(y * 12 / 5, height);
		const s32 dstW = max(1, xboxScaleX((x + sprite->width) * 2, width) - dstX0);
		const s32 dstH = max(1, xboxScaleY((y * 12 / 5) + sprite->height * 2, height) - dstY0);

		const s32 shadowOffsetX = max(1, width / 640);
		const s32 shadowOffsetY = max(1, height / 480);
		for (s32 py = 0; py < sprite->height; py++)
		{
			const s32 rowY0 = dstY0 + (py * dstH) / sprite->height + (shadow ? shadowOffsetY : 0);
			const s32 rowY1 = dstY0 + ((py + 1) * dstH) / sprite->height + (shadow ? shadowOffsetY : 0);
			for (s32 px = 0; px < sprite->width; px++)
			{
				const u8 coverage = data[py * sprite->width + px];
				if (coverage)
				{
					const s32 colX0 = dstX0 + (px * dstW) / sprite->width + (shadow ? shadowOffsetX : 0);
					const s32 colX1 = dstX0 + ((px + 1) * dstW) / sprite->width + (shadow ? shadowOffsetX : 0);
					u8 color = 0;
					if (!shadow)
					{
						if (selected)
						{
							color = coverage >= 2 ? c_xboxPauseWhite : 36;
						}
						else
						{
							color = coverage >= 2 ? 36 : 0;
						}
					}
					xboxDrawRect(fb, width, height, colX0, rowY0, max(1, colX1 - colX0), max(1, rowY1 - rowY0), color);
				}
			}
		}
	}

	static void xboxDrawText(u8* fb, s32 width, s32 height, XboxPauseTextId id, s32 x, s32 y, bool selected)
	{
		xboxDrawTextMask(fb, width, height, id, x, y, selected, true);
		xboxDrawTextMask(fb, width, height, id, x, y, selected, false);
	}

	static bool xboxStickPressed(f32 value, bool* latch)
	{
		const bool down = value > 0.55f || value < -0.55f;
		const bool pressed = down && !*latch;
		*latch = down;
		return pressed;
	}

	static s32 xboxOptionPercent(float value)
	{
		s32 pct = (s32)(value * 100.0f + 0.5f);
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		return pct;
	}

	static s32 xboxClampS32(s32 value, s32 minValue, s32 maxValue)
	{
		if (value < minValue) return minValue;
		if (value > maxValue) return maxValue;
		return value;
	}

	static float xboxClampF32(float value, float minValue, float maxValue)
	{
		if (value < minValue) return minValue;
		if (value > maxValue) return maxValue;
		return value;
	}

	static void xboxPauseRuntimeSettingsPath(char* path, size_t pathSize)
	{
		snprintf(path, pathSize, "%sSaves\\xbox_settings.cfg", TFE_Paths::getPath(PATH_PROGRAM));
	}

	static void xboxApplyPauseVideoSettings()
	{
		TFE_Settings_System* system = TFE_Settings::getSystemSettings();
		system->xboxSafeZonePercent = xboxClampS32(system->xboxSafeZonePercent, 80, 100);
		system->xboxSafeZoneOffsetX = xboxClampS32(system->xboxSafeZoneOffsetX, -40, 40);
		system->xboxSafeZoneOffsetY = xboxClampS32(system->xboxSafeZoneOffsetY, -30, 30);
		TFE_RenderBackend::xboxSetSafeZone(system->xboxSafeZonePercent,
			system->xboxSafeZoneOffsetX, system->xboxSafeZoneOffsetY);
	}

	static void xboxApplyPauseControlSettings()
	{
		TFE_Settings_System* system = TFE_Settings::getSystemSettings();
		system->xboxLookSensitivityX = xboxClampF32(system->xboxLookSensitivityX, 0.25f, 2.5f);
		system->xboxLookSensitivityY = xboxClampF32(system->xboxLookSensitivityY, 0.25f, 2.5f);
		system->xboxRightStickDeadzone = xboxClampF32(system->xboxRightStickDeadzone, 0.0f, 0.30f);
		system->xboxLookSensitivity = (system->xboxLookSensitivityX + system->xboxLookSensitivityY) * 0.5f;
		system->xboxStickDeadzone = system->xboxRightStickDeadzone;
		TFE_InputXbox::setLookSensitivityX(system->xboxLookSensitivityX);
		TFE_InputXbox::setLookSensitivityY(system->xboxLookSensitivityY);
		TFE_InputXbox::setRightStickDeadzone(system->xboxRightStickDeadzone);
	}

	static bool xboxSaveRuntimeSettings()
	{
		char savesDir[TFE_MAX_PATH];
		snprintf(savesDir, TFE_MAX_PATH, "%sSaves\\", TFE_Paths::getPath(PATH_PROGRAM));
		FileUtil::makeDirectory(savesDir);

		char path[TFE_MAX_PATH];
		xboxPauseRuntimeSettingsPath(path, sizeof(path));
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
		file.writeString("xboxSafeZonePercent=%d\r\n", system->xboxSafeZonePercent);
		file.writeString("xboxSafeZoneOffsetX=%d\r\n", system->xboxSafeZoneOffsetX);
		file.writeString("xboxSafeZoneOffsetY=%d\r\n", system->xboxSafeZoneOffsetY);
		file.writeString("masterVolumePct=%d\r\n", xboxOptionPercent(sound->masterVolume));
		file.writeString("soundFxVolumePct=%d\r\n", xboxOptionPercent(sound->soundFxVolume));
		file.writeString("musicVolumePct=%d\r\n", xboxOptionPercent(sound->musicVolume));
		file.writeString("cutsceneSoundFxVolumePct=%d\r\n", xboxOptionPercent(sound->cutsceneSoundFxVolume));
		file.writeString("cutsceneMusicVolumePct=%d\r\n", xboxOptionPercent(sound->cutsceneMusicVolume));
		file.close();
		TFE_System::logWrite(LOG_MSG, "Settings", "Xbox runtime settings saved from pause menu: '%s'", path);
		return true;
	}

	static const XboxPauseBindingOption* xboxFindPauseBindingOption(s32 option)
	{
		if (s_emState.optionsPage != XPOPAGE_CONTROLS) return NULL;
		for (s32 i = 0; i < (s32)TFE_ARRAYSIZE(c_xboxPauseBindingOptions); i++)
		{
			if (c_xboxPauseBindingOptions[i].option == option) return &c_xboxPauseBindingOptions[i];
		}
		return NULL;
	}

	static const char* xboxPauseButtonName(Button button)
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

	static s32 xboxPauseButtonIcon(Button button)
	{
		switch (button)
		{
			case CONTROLLER_BUTTON_A: return XPICON_A;
			case CONTROLLER_BUTTON_B: return XPICON_B;
			case CONTROLLER_BUTTON_X: return XPICON_X;
			case CONTROLLER_BUTTON_Y: return XPICON_Y;
			case CONTROLLER_BUTTON_BACK: return XPICON_BACK;
			case CONTROLLER_BUTTON_START: return XPICON_START;
			case CONTROLLER_BUTTON_LEFTSTICK: return XPICON_LSTICK;
			case CONTROLLER_BUTTON_RIGHTSTICK: return XPICON_RSTICK;
			case CONTROLLER_BUTTON_LEFTSHOULDER: return XPICON_WHITE;
			case CONTROLLER_BUTTON_RIGHTSHOULDER: return XPICON_BLACK;
			case CONTROLLER_BUTTON_DPAD_UP: return XPICON_DPAD_UP;
			case CONTROLLER_BUTTON_DPAD_DOWN: return XPICON_DPAD_DOWN;
			case CONTROLLER_BUTTON_DPAD_LEFT: return XPICON_DPAD_LEFT;
			case CONTROLLER_BUTTON_DPAD_RIGHT: return XPICON_DPAD_RIGHT;
			default: return -1;
		}
	}

	static const char* xboxPauseAxisName(Axis axis)
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

	static s32 xboxPauseAxisIcon(Axis axis)
	{
		switch (axis)
		{
			case AXIS_LEFT_TRIGGER: return XPICON_LT;
			case AXIS_RIGHT_TRIGGER: return XPICON_RT;
			case AXIS_LEFT_X:
			case AXIS_LEFT_Y: return XPICON_LSTICK;
			case AXIS_RIGHT_X:
			case AXIS_RIGHT_Y: return XPICON_RSTICK;
			default: return -1;
		}
	}

	static const char* xboxPauseBindingName(TFE_Input::InputAction action)
	{
		u32 indices[16];
		const u32 count = TFE_Input::inputMapping_getBindingsForAction(action, indices, TFE_ARRAYSIZE(indices));
		for (u32 i = 0; i < count; i++)
		{
			TFE_Input::InputBinding* bind = TFE_Input::inputMapping_getBindingByIndex(indices[i]);
			if (bind->type == TFE_Input::ITYPE_CONTROLLER)
			{
				return xboxPauseButtonName(bind->ctrlBtn);
			}
			if (bind->type == TFE_Input::ITYPE_CONTROLLER_AXIS)
			{
				return xboxPauseAxisName(bind->axis);
			}
		}
		return "UNMAPPED";
	}

	static s32 xboxPauseBindingIcon(TFE_Input::InputAction action)
	{
		u32 indices[16];
		const u32 count = TFE_Input::inputMapping_getBindingsForAction(action, indices, TFE_ARRAYSIZE(indices));
		for (u32 i = 0; i < count; i++)
		{
			TFE_Input::InputBinding* bind = TFE_Input::inputMapping_getBindingByIndex(indices[i]);
			if (bind->type == TFE_Input::ITYPE_CONTROLLER)
			{
				return xboxPauseButtonIcon(bind->ctrlBtn);
			}
			if (bind->type == TFE_Input::ITYPE_CONTROLLER_AXIS)
			{
				return xboxPauseAxisIcon(bind->axis);
			}
		}
		return -1;
	}

	static const char* xboxPauseOptionsTitle()
	{
		switch (s_emState.optionsPage)
		{
			case XPOPAGE_CONTROLS: return "CONTROLS";
			case XPOPAGE_VIDEO: return "VIDEO";
			case XPOPAGE_AUDIO: return "AUDIO";
			default: return "OPTIONS";
		}
	}

	static s32 xboxPauseOptionsVisibleCount()
	{
		switch (s_emState.optionsPage)
		{
			case XPOPAGE_CONTROLS: return XPCTRL_COUNT;
			case XPOPAGE_VIDEO: return XPVID_COUNT;
			case XPOPAGE_AUDIO: return XPAUD_COUNT;
			default: return XPROOT_COUNT;
		}
	}

	static void xboxSetPauseOptionText(s32 index, const char* label, const char* valueText)
	{
		s_emState.optionsItems[index].label = label;
		s_emState.optionsItems[index].valueText = valueText;
	}

	static void xboxSetPauseOptionSlider(s32 index, const char* label, s32 value, s32 minValue, s32 maxValue)
	{
		s_emState.optionsItems[index].label = label;
		s_emState.optionsItems[index].value = value;
		s_emState.optionsItems[index].minValue = minValue;
		s_emState.optionsItems[index].maxValue = maxValue;
	}

	static void xboxSetPauseOptionIcon(s32 index, const char* label, s32 icon)
	{
		s_emState.optionsItems[index].label = label;
		if (icon >= 0)
		{
			s_emState.optionsItems[index].hasIcon = true;
			s_emState.optionsItems[index].valueIcon = icon;
		}
		else
		{
			s_emState.optionsItems[index].valueText = "UNMAPPED";
		}
	}

	static void xboxRefreshOptionsItems()
	{
		TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
		TFE_Settings_System* system = TFE_Settings::getSystemSettings();
		memset(s_emState.optionsItems, 0, sizeof(s_emState.optionsItems));
		s_emState.optionsItemCount = xboxPauseOptionsVisibleCount();

		if (s_emState.optionsPage == XPOPAGE_ROOT)
		{
			xboxSetPauseOptionText(XPROOT_CONTROLS, "CONTROLS", ">");
			xboxSetPauseOptionText(XPROOT_VIDEO, "VIDEO", ">");
			xboxSetPauseOptionText(XPROOT_AUDIO, "AUDIO", ">");
		}
		else if (s_emState.optionsPage == XPOPAGE_CONTROLS)
		{
			xboxSetPauseOptionSlider(XPCTRL_LOOK_SENS_X, "LOOK X SENS", (s32)(TFE_InputXbox::getLookSensitivityX() * 100.0f + 0.5f), 25, 250);
			xboxSetPauseOptionSlider(XPCTRL_LOOK_SENS_Y, "LOOK Y SENS", (s32)(TFE_InputXbox::getLookSensitivityY() * 100.0f + 0.5f), 25, 250);
			xboxSetPauseOptionSlider(XPCTRL_RIGHT_STICK_DEADZONE, "RIGHT DEADZONE", (s32)(TFE_InputXbox::getRightStickDeadzone() * 100.0f + 0.5f), 0, 30);

			for (s32 i = 0; i < (s32)TFE_ARRAYSIZE(c_xboxPauseBindingOptions); i++)
			{
				const XboxPauseBindingOption* option = &c_xboxPauseBindingOptions[i];
				if (s_xboxPauseOptionsCapture && s_emState.optionsSelection == option->option)
				{
					xboxSetPauseOptionText(option->option, option->label, "PRESS BUTTON");
					s_emState.optionsItems[option->option].capture = true;
				}
				else
				{
					xboxSetPauseOptionIcon(option->option, option->label, xboxPauseBindingIcon(option->action));
				}
			}
		}
		else if (s_emState.optionsPage == XPOPAGE_VIDEO)
		{
			xboxSetPauseOptionSlider(XPVID_SAFE_ZONE, "SCREEN SIZE", system->xboxSafeZonePercent, 80, 100);
			xboxSetPauseOptionSlider(XPVID_SCREEN_X, "H POSITION", system->xboxSafeZoneOffsetX, -40, 40);
			xboxSetPauseOptionSlider(XPVID_SCREEN_Y, "V POSITION", system->xboxSafeZoneOffsetY, -30, 30);
		}
		else if (s_emState.optionsPage == XPOPAGE_AUDIO)
		{
			xboxSetPauseOptionSlider(XPAUD_MASTER_VOLUME, "MASTER VOLUME", xboxOptionPercent(sound->masterVolume), 0, 100);
			xboxSetPauseOptionSlider(XPAUD_SFX_VOLUME, "SFX VOLUME", xboxOptionPercent(sound->soundFxVolume), 0, 100);
			xboxSetPauseOptionSlider(XPAUD_MUSIC_VOLUME, "MUSIC VOLUME", xboxOptionPercent(sound->musicVolume), 0, 100);
			xboxSetPauseOptionSlider(XPAUD_CUTSCENE_SFX, "CUTSCENE SFX", xboxOptionPercent(sound->cutsceneSoundFxVolume), 0, 100);
			xboxSetPauseOptionSlider(XPAUD_CUTSCENE_MUSIC, "CUTSCENE MUSIC", xboxOptionPercent(sound->cutsceneMusicVolume), 0, 100);
		}

		if (s_emState.optionsSelection >= s_emState.optionsItemCount) s_emState.optionsSelection = s_emState.optionsItemCount > 0 ? s_emState.optionsItemCount - 1 : 0;
		if (s_emState.optionsSelection < 0) s_emState.optionsSelection = 0;
		if (s_emState.optionsScroll > s_emState.optionsItemCount - 7) s_emState.optionsScroll = s_emState.optionsItemCount > 7 ? s_emState.optionsItemCount - 7 : 0;
		if (s_emState.optionsScroll < 0) s_emState.optionsScroll = 0;
	}

	static void xboxApplyOptionValue(s32 index, s32 value)
	{
		if (index < 0 || index >= s_emState.optionsItemCount) return;
		if (xboxFindPauseBindingOption(index)) return;
		if (value < s_emState.optionsItems[index].minValue) value = s_emState.optionsItems[index].minValue;
		if (value > s_emState.optionsItems[index].maxValue) value = s_emState.optionsItems[index].maxValue;

		TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
		TFE_Settings_System* system = TFE_Settings::getSystemSettings();
		if (s_emState.optionsPage == XPOPAGE_CONTROLS)
		{
			switch (index)
			{
				case XPCTRL_LOOK_SENS_X: system->xboxLookSensitivityX = (float)value / 100.0f; break;
				case XPCTRL_LOOK_SENS_Y: system->xboxLookSensitivityY = (float)value / 100.0f; break;
				case XPCTRL_RIGHT_STICK_DEADZONE: system->xboxRightStickDeadzone = (float)value / 100.0f; break;
			}
			xboxApplyPauseControlSettings();
		}
		else if (s_emState.optionsPage == XPOPAGE_VIDEO)
		{
			switch (index)
			{
				case XPVID_SAFE_ZONE: system->xboxSafeZonePercent = value; break;
				case XPVID_SCREEN_X: system->xboxSafeZoneOffsetX = value; break;
				case XPVID_SCREEN_Y: system->xboxSafeZoneOffsetY = value; break;
			}
			xboxApplyPauseVideoSettings();
		}
		else if (s_emState.optionsPage == XPOPAGE_AUDIO)
		{
			switch (index)
			{
				case XPAUD_MASTER_VOLUME: sound->masterVolume = (float)value / 100.0f; break;
				case XPAUD_SFX_VOLUME: sound->soundFxVolume = (float)value / 100.0f; break;
				case XPAUD_MUSIC_VOLUME: sound->musicVolume = (float)value / 100.0f; break;
				case XPAUD_CUTSCENE_SFX: sound->cutsceneSoundFxVolume = (float)value / 100.0f; break;
				case XPAUD_CUTSCENE_MUSIC: sound->cutsceneMusicVolume = (float)value / 100.0f; break;
			}
			sound = TFE_Settings::getSoundSettings();
			TFE_MidiPlayer::setVolume(sound->musicVolume * sound->masterVolume);
		}
		xboxRefreshOptionsItems();
	}

	static void xboxOpenOptions()
	{
		s_emState.optionsPage = XPOPAGE_ROOT;
		xboxRefreshOptionsItems();
		s_emState.optionsOpen = true;
		s_emState.optionsSelection = 0;
		s_emState.optionsScroll = 0;
		s_emState.optionsStickUpHeld = s_emState.optionsStickDownHeld = false;
		s_emState.optionsStickLeftHeld = s_emState.optionsStickRightHeld = false;
		s_xboxPauseOptionsCapture = false;
		TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false);
		TFE_RenderBackend::xboxSetOptionsScreen(true, true, xboxPauseOptionsTitle(),
			s_emState.optionsSelection, s_emState.optionsScroll, s_emState.optionsFrame,
			s_emState.optionsItems, s_emState.optionsItemCount);
	}

	static void xboxCloseOptions()
	{
		TFE_Settings::writeToDisk();
		xboxSaveRuntimeSettings();
		s_emState.optionsOpen = false;
		s_xboxPauseOptionsCapture = false;
		TFE_RenderBackend::xboxSetOptionsScreen(false, true, 0, 0, 0, NULL, 0);
	}

	static void xboxMoveOptions(s32 delta)
	{
		s_emState.optionsSelection += delta;
		if (s_emState.optionsSelection < 0) s_emState.optionsSelection = s_emState.optionsItemCount - 1;
		if (s_emState.optionsSelection >= s_emState.optionsItemCount) s_emState.optionsSelection = 0;
		if (s_emState.optionsSelection < s_emState.optionsScroll) s_emState.optionsScroll = s_emState.optionsSelection;
		if (s_emState.optionsSelection >= s_emState.optionsScroll + 7) s_emState.optionsScroll = s_emState.optionsSelection - 6;
		s_xboxPauseOptionsCapture = false;
	}

	static void xboxShowOptionsPage(s32 page)
	{
		s_emState.optionsPage = page;
		s_emState.optionsSelection = 0;
		s_emState.optionsScroll = 0;
		s_xboxPauseOptionsCapture = false;
		xboxRefreshOptionsItems();
		TFE_RenderBackend::xboxSetOptionsScreen(s_emState.optionsOpen, true, xboxPauseOptionsTitle(),
			s_emState.optionsSelection, s_emState.optionsScroll, s_emState.optionsFrame++,
			s_emState.optionsItems, s_emState.optionsItemCount);
	}

	static void xboxRefreshOptionsScreen()
	{
		TFE_RenderBackend::xboxSetOptionsScreen(s_emState.optionsOpen, true, xboxPauseOptionsTitle(),
			s_emState.optionsSelection, s_emState.optionsScroll, s_emState.optionsFrame++,
			s_emState.optionsItems, s_emState.optionsItemCount);
	}

	static EscapeMenuAction xboxUpdateOptions()
	{
		if (s_xboxPauseOptionsCapture)
		{
			const XboxPauseBindingOption* option = xboxFindPauseBindingOption(s_emState.optionsSelection);
			Button button = TFE_Input::getControllerButtonPressed();
			Axis axis = TFE_Input::getControllerAnalogDown();
			if (option && button != CONTROLLER_BUTTON_UNKNOWN)
			{
				TFE_Input::inputMapping_setControllerBinding(option->action, TFE_Input::ITYPE_CONTROLLER, (u32)button);
				TFE_Input::inputMapping_serialize();
				s_xboxPauseOptionsCapture = false;
				xboxRefreshOptionsItems();
			}
			else if (option && axis != AXIS_UNKNOWN)
			{
				TFE_Input::inputMapping_setControllerBinding(option->action, TFE_Input::ITYPE_CONTROLLER_AXIS, (u32)axis);
				TFE_Input::inputMapping_serialize();
				s_xboxPauseOptionsCapture = false;
				xboxRefreshOptionsItems();
			}
			else if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) ||
				inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED)
			{
				s_xboxPauseOptionsCapture = false;
				xboxRefreshOptionsItems();
			}

			xboxRefreshOptionsScreen();
			return ESC_CONTINUE;
		}

		const f32 lx = TFE_Input::getAxis(AXIS_LEFT_X);
		const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
		const bool stickUp = ly > 0.55f;
		const bool stickDown = ly < -0.55f;
		const bool stickLeft = lx < -0.55f;
		const bool stickRight = lx > 0.55f;

		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) || (stickUp && !s_emState.optionsStickUpHeld)) xboxMoveOptions(-1);
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) || (stickDown && !s_emState.optionsStickDownHeld)) xboxMoveOptions(1);
		s_emState.optionsStickUpHeld = stickUp;
		s_emState.optionsStickDownHeld = stickDown;

		s32 delta = 0;
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_LEFT) || (stickLeft && !s_emState.optionsStickLeftHeld)) delta = -5;
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_RIGHT) || (stickRight && !s_emState.optionsStickRightHeld)) delta = 5;
		s_emState.optionsStickLeftHeld = stickLeft;
		s_emState.optionsStickRightHeld = stickRight;
		if (delta && s_emState.optionsPage != XPOPAGE_ROOT)
		{
			xboxApplyOptionValue(s_emState.optionsSelection, s_emState.optionsItems[s_emState.optionsSelection].value + delta);
		}

		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A))
		{
			if (s_emState.optionsPage == XPOPAGE_ROOT)
			{
				if (s_emState.optionsSelection == XPROOT_CONTROLS) xboxShowOptionsPage(XPOPAGE_CONTROLS);
				else if (s_emState.optionsSelection == XPROOT_VIDEO) xboxShowOptionsPage(XPOPAGE_VIDEO);
				else if (s_emState.optionsSelection == XPROOT_AUDIO) xboxShowOptionsPage(XPOPAGE_AUDIO);
				return ESC_CONTINUE;
			}
			else if (xboxFindPauseBindingOption(s_emState.optionsSelection))
			{
				s_xboxPauseOptionsCapture = true;
				xboxRefreshOptionsItems();
			}
			else
			{
				TFE_Settings::writeToDisk();
				xboxSaveRuntimeSettings();
			}
		}
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) ||
			inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED)
		{
			if (s_emState.optionsPage != XPOPAGE_ROOT)
			{
				xboxShowOptionsPage(XPOPAGE_ROOT);
				return ESC_CONTINUE;
			}
			xboxCloseOptions();
		}

		xboxRefreshOptionsScreen();
		return ESC_CONTINUE;
	}

	static void xboxRefreshCheatItems()
	{
		s_emState.cheatsItems[0].label = "FULL INVINCIBILITY";
		s_emState.cheatsItems[0].enabled = s_invincibility != 0;
		s_emState.cheatsItems[1].label = "FLY MODE";
		s_emState.cheatsItems[1].enabled = s_flyMode ? true : false;
		s_emState.cheatsItems[2].label = "NO CLIP";
		s_emState.cheatsItems[2].enabled = s_noclip ? true : false;
		s_emState.cheatsItems[3].label = "FULL-BRIGHT";
		s_emState.cheatsItems[3].enabled = TFE_Jedi::s_fullBright ? true : false;
		s_emState.cheatsItems[4].label = "PONDERING";
		s_emState.cheatsItems[4].enabled = !s_aiActive;
		s_emState.cheatsItems[5].label = "ONE-HIT KILL";
		s_emState.cheatsItems[5].enabled = s_oneHitKillEnabled ? true : false;
		s_emState.cheatsItems[6].label = "HARDCORE MODE";
		s_emState.cheatsItems[6].enabled = s_instaDeathEnabled ? true : false;
		s_emState.cheatsItems[7].label = "INSECT MODE";
		s_emState.cheatsItems[7].enabled = s_smallModeEnabled ? true : false;
		s_emState.cheatsItems[8].label = "HEIGHT CHECK";
		s_emState.cheatsItems[8].enabled = !s_limitStepHeight;
		s_emState.cheatsItems[XBOX_CHEAT_GIVE_ALL].label = "GIVE ALL";
		s_emState.cheatsItems[XBOX_CHEAT_GIVE_ALL].enabled = false;
	}

	static CheatID xboxCheatIdForIndex(s32 index)
	{
		static const CheatID ids[XBOX_CHEAT_COUNT] =
		{
			CHEAT_LAIMLAME,
			CHEAT_LAFLY,
			CHEAT_LANOCLIP,
			CHEAT_LABRIGHT,
			CHEAT_LAREDLITE,
			CHEAT_LAIMDEATH,
			CHEAT_LAHARDCORE,
			CHEAT_LABUG,
			CHEAT_LAPOGO,
			CHEAT_NONE
		};
		return (index >= 0 && index < XBOX_CHEAT_COUNT) ? ids[index] : CHEAT_NONE;
	}

	static void xboxOpenCheats()
	{
		xboxRefreshCheatItems();
		s_emState.cheatsOpen = true;
		s_emState.cheatsSelection = 0;
		s_emState.cheatsScroll = 0;
		s_emState.cheatsStickUpHeld = s_emState.cheatsStickDownHeld = false;
		TFE_RenderBackend::xboxSetPauseOverlay(true, s_emState.buttonPressed, s_emState.buttonPressed, false);
		TFE_RenderBackend::xboxSetCheatScreen(true, s_emState.cheatsSelection, s_emState.cheatsScroll, s_emState.cheatsItems, XBOX_CHEAT_COUNT);
	}

	static void xboxCloseCheats()
	{
		s_emState.cheatsOpen = false;
		TFE_RenderBackend::xboxSetCheatScreen(false, 0, 0, NULL, 0);
	}

	static void xboxMoveCheats(s32 delta)
	{
		s_emState.cheatsSelection += delta;
		if (s_emState.cheatsSelection < 0) s_emState.cheatsSelection = XBOX_CHEAT_COUNT - 1;
		if (s_emState.cheatsSelection >= XBOX_CHEAT_COUNT) s_emState.cheatsSelection = 0;
		if (s_emState.cheatsSelection < s_emState.cheatsScroll) s_emState.cheatsScroll = s_emState.cheatsSelection;
		if (s_emState.cheatsSelection >= s_emState.cheatsScroll + 6) s_emState.cheatsScroll = s_emState.cheatsSelection - 5;
	}

	static EscapeMenuAction xboxUpdateCheats()
	{
		const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
		const bool stickUp = ly > 0.55f;
		const bool stickDown = ly < -0.55f;
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) || (stickUp && !s_emState.cheatsStickUpHeld)) xboxMoveCheats(-1);
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) || (stickDown && !s_emState.cheatsStickDownHeld)) xboxMoveCheats(1);
		s_emState.cheatsStickUpHeld = stickUp;
		s_emState.cheatsStickDownHeld = stickDown;

		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A))
		{
			if (s_emState.cheatsSelection == XBOX_CHEAT_GIVE_ALL)
			{
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "cheat give all");
				cheat_giveAll();
			}
			else
			{
				CheatID id = xboxCheatIdForIndex(s_emState.cheatsSelection);
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "cheat toggle selection=%d id=%d", s_emState.cheatsSelection, (s32)id);
				executeCheat(id);
			}
			xboxRefreshCheatItems();
		}

		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) ||
			inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED)
		{
			xboxCloseCheats();
		}

		TFE_RenderBackend::xboxSetCheatScreen(s_emState.cheatsOpen, s_emState.cheatsSelection, s_emState.cheatsScroll, s_emState.cheatsItems, XBOX_CHEAT_COUNT);
		return ESC_CONTINUE;
	}

	static void xboxMoveSelection(s32 delta)
	{
		if (s_emState.confirmState != CONFIRM_STATE_NONE)
		{
			if (s_emState.buttonPressed < 0) s_emState.buttonPressed = CONFIRM_NO;
			else s_emState.buttonPressed = (s_emState.buttonPressed == CONFIRM_NO) ? CONFIRM_YES : CONFIRM_NO;
			s_emState.buttonHover = true;
			return;
		}

		if (s_emState.buttonPressed < 0) s_emState.buttonPressed = ESC_BTN_RESUME;
		s_emState.buttonPressed += delta;
		if (s_emState.buttonPressed < 0) s_emState.buttonPressed = ESC_BTN_COUNT - 1;
		if (s_emState.buttonPressed >= ESC_BTN_COUNT) s_emState.buttonPressed = 0;
		s_emState.buttonHover = true;
		TFE_System::logWrite(LOG_MSG, "PauseMenu", "selection=%d (%s)",
			s_emState.buttonPressed, escapeMenu_xboxButtonName(s_emState.buttonPressed));
	}

	EscapeMenuAction escapeMenu_updateXboxUI()
	{
		EscapeMenuAction action = ESC_CONTINUE;
		static bool s_stickYLatched = false;
		static bool s_stickXLatched = false;

		if (s_emState.optionsOpen)
		{
			return xboxUpdateOptions();
		}
		if (s_emState.cheatsOpen)
		{
			return xboxUpdateCheats();
		}

		if (s_emState.quickSaveStatus != 0)
		{
			const bool dismissDown =
				TFE_Input::buttonDown(CONTROLLER_BUTTON_A) ||
				TFE_Input::buttonDown(CONTROLLER_BUTTON_B) ||
				TFE_Input::buttonDown(CONTROLLER_BUTTON_START) ||
				inputMapping_getActionState(IADF_MENU_TOGGLE) != STATE_UP;

			if (s_emState.quickSaveWaitRelease)
			{
				if (!dismissDown)
				{
					TFE_System::logWrite(LOG_MSG, "PauseMenu", "quick save acknowledgement armed");
					s_emState.quickSaveWaitRelease = false;
				}
				return ESC_CONTINUE;
			}

			if (s_emState.quickSaveClosePending)
			{
				if (dismissDown)
				{
					return ESC_CONTINUE;
				}

				TFE_System::logWrite(LOG_MSG, "PauseMenu", "quick save closed after release");
				s_emState.quickSaveStatus = 0;
				s_emState.quickSaveClosePending = false;
				s_emState.quickSaveWaitRelease = true;
				return ESC_RETURN;
			}

			if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) ||
				TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) ||
				TFE_Input::buttonPressed(CONTROLLER_BUTTON_START) ||
				inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED)
			{
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "quick save dismissed; waiting for release before gameplay resumes");
				s_emState.quickSaveClosePending = true;
				s_emState.quickSaveWaitRelease = true;
			}
			return action;
		}

		const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
		const f32 lx = TFE_Input::getAxis(AXIS_LEFT_X);
		const bool stickYPressed = xboxStickPressed(ly, &s_stickYLatched);
		const bool stickXPressed = xboxStickPressed(lx, &s_stickXLatched);
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) ||
			(stickYPressed && ly > 0.0f))
		{
			xboxMoveSelection(-1);
		}
		else if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) ||
			(stickYPressed && ly < 0.0f))
		{
			xboxMoveSelection(1);
		}

		if (s_emState.confirmState != CONFIRM_STATE_NONE)
		{
			if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_LEFT) ||
				TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_RIGHT) ||
				stickXPressed)
			{
				xboxMoveSelection(1);
			}
		}

		if (inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED ||
			TFE_Input::buttonPressed(CONTROLLER_BUTTON_B))
		{
			if (s_emState.confirmState != CONFIRM_STATE_NONE)
			{
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "cancel confirm");
				s_emState.confirmState = CONFIRM_STATE_NONE;
				s_emState.buttonPressed = ESC_BTN_RESUME;
				s_emState.buttonHover = true;
			}
			else
			{
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "close via B/Start");
				action = ESC_RETURN;
				s_emState.escMenuOpen = JFALSE;
			}
			return action;
		}

		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A))
		{
			TFE_System::logWrite(LOG_MSG, "PauseMenu", "activate selection=%d (%s) confirm=%d",
				s_emState.buttonPressed,
				s_emState.confirmState == CONFIRM_STATE_NONE ? escapeMenu_xboxButtonName(s_emState.buttonPressed) :
					(s_emState.buttonPressed == CONFIRM_YES ? "Confirm Yes" : "Confirm No"),
				(int)s_emState.confirmState);
			action = escapeMenu_handleAction(action, s_emState.buttonPressed);
			if (s_emState.confirmState != CONFIRM_STATE_NONE && s_emState.buttonPressed < 0)
			{
				s_emState.buttonPressed = CONFIRM_NO;
				s_emState.buttonHover = true;
			}
		}

		return action;
	}

	static void xboxDimFramebuffer(u8* fb, s32 width, s32 height)
	{
		for (s32 y = 0; y < height; y++)
		{
			u8* row = &fb[y * width];
			for (s32 x = (y & 1); x < width; x += 2)
			{
				row[x] = 0;
			}
		}
	}

	static void xboxDrawFrame(u8* fb, s32 width, s32 height, s32 x, s32 y, s32 w, s32 h)
	{
		xboxDrawRectBase(fb, width, height, x, y, w, h, c_xboxPausePanel);
		xboxDrawHLineBase(fb, width, height, x, y, w, 2, c_xboxPauseWhite);
		xboxDrawHLineBase(fb, width, height, x, y + h - 2, w, 2, 36);
		xboxDrawVLineBase(fb, width, height, x, y, h, 2, c_xboxPauseWhite);
		xboxDrawVLineBase(fb, width, height, x + w - 2, y, h, 2, 36);
		xboxDrawHLineBase(fb, width, height, x + 5, y + 5, w - 10, 2, 36);
		xboxDrawHLineBase(fb, width, height, x + 5, y + h - 7, w - 10, 2, c_xboxPauseWhite);
		xboxDrawVLineBase(fb, width, height, x + 5, y + 5, h - 10, 2, 36);
		xboxDrawVLineBase(fb, width, height, x + w - 7, y + 5, h - 10, 2, c_xboxPauseWhite);
	}

	static void xboxDrawMenuText(u8* fb, s32 width, s32 height, XboxPauseTextId textId, s32 x, s32 y, bool selected)
	{
		if (selected)
		{
			const XboxPauseTextSprite* sprite = &c_xboxPauseText[textId];
			xboxDrawRectBase(fb, width, height, x * 2 - 26, y * 12 / 5 - 5, 300, sprite->height * 2 + 10, c_xboxPauseGreen);
			xboxDrawText(fb, width, height, XPT_ARROW, x - 23, y + ((sprite->height - c_xboxPauseText[XPT_ARROW].height) >> 1), true);
		}
		xboxDrawText(fb, width, height, textId, x, y, selected);
	}

	void escapeMenu_drawXboxOverlay()
	{
		if (s_emState.optionsOpen)
		{
			return;
		}
		TFE_RenderBackend::xboxSetPauseOverlay(true,
			s_emState.buttonPressed,
			s_emState.buttonPressed,
			s_emState.confirmState != CONFIRM_STATE_NONE,
			s_emState.quickSaveStatus);
	}
#endif

	// The cursor is handled independently for the Escape Menu for now so it can later handle
	// widescreen. However, it may be better to merge these functions anyway.
	void escMenu_resetCursor()
	{
		// Reset the cursor.
		u32 width, height;
		vfb_getResolution(&width, &height);

		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		s_emState.cursorPosAccum.x = (s32)displayInfo.width >> 1; s_emState.cursorPosAccum.z = (s32)displayInfo.height >> 1;
		s_emState.cursorPos.x = clamp(s_emState.cursorPosAccum.x * (s32)height / (s32)displayInfo.height, 0, (s32)width - 3);
		s_emState.cursorPos.z = clamp(s_emState.cursorPosAccum.z * (s32)height / (s32)displayInfo.height, 0, (s32)height - 3);
	}

	void escMenu_handleMousePosition()
	{
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		u32 width, height;
		vfb_getResolution(&width, &height);

		s32 dx, dy;
		TFE_Input::getAccumulatedMouseMove(&dx, &dy);

		MonitorInfo monitorInfo;
		TFE_RenderBackend::getCurrentMonitorInfo(&monitorInfo);

		s32 mx, my;
		TFE_Input::getMousePos(&mx, &my);
		s_emState.cursorPosAccum.x = mx; s_emState.cursorPosAccum.z = my;

		if (displayInfo.width >= displayInfo.height)
		{
			s_emState.cursorPos.x = clamp(s_emState.cursorPosAccum.x * (s32)height / (s32)displayInfo.height, 0, (s32)width - 3);
			s_emState.cursorPos.z = clamp(s_emState.cursorPosAccum.z * (s32)height / (s32)displayInfo.height, 0, (s32)height - 3);
		}
		else
		{
			s_emState.cursorPos.x = clamp(s_emState.cursorPosAccum.x * (s32)width / (s32)displayInfo.width, 0, (s32)width - 3);
			s_emState.cursorPos.z = clamp(s_emState.cursorPosAccum.z * (s32)width / (s32)displayInfo.width, 0, (s32)height - 3);
		}
	}
}
