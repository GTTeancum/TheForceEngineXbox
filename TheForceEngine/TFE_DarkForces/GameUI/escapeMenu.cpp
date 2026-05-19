#include <cstring>

#include "escapeMenu.h"
#include "delt.h"
#include "uiDraw.h"
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/config.h>
#include <TFE_Game/reticle.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/roffscreenBuffer.h>
#include <TFE_System/system.h>
#ifdef _XBOX
#include <TFE_Input/input.h>
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
		ESC_BTN_RESPAWN,
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
	static Vec2i c_escButtons[ESC_BTN_COUNT] =
	{
#ifdef _XBOX
		{64, 35},	// ESC_RESUME
		{64, 55},	// ESC_PDA
		{64, 75},	// ESC_ABORT
		{64, 95},	// ESC_RESPAWN
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

		RenderTargetHandle renderTarget;
		LangHotkeys* langKeys;

		EscapeMenuState()
			: escMenuOpen(JFALSE), escMenuFrameCount(0), escMenuFrames(NULL)
			, confirmMenuFrameCount(0), confirmMenuFrames(NULL)
			, framebufferCopy(NULL), framebuffer(NULL)
			, buttonPressed(-1), buttonHover(false)
			, confirmState(CONFIRM_STATE_NONE)
			, renderTarget(NULL), langKeys(NULL)
		{
			cursorPosAccum.x = 0; cursorPosAccum.z = 0;
			cursorPos.x = 0;      cursorPos.z = 0;
		}
	};
	static EscapeMenuState s_emState;

	void escMenu_resetCursor();
	void escMenu_handleMousePosition();
	bool escapeMenu_getTextures(TextureInfoList& texList, AssetPool pool);
	void escapeMenu_draw(JBool drawMouse, JBool drawBackground);
	EscapeMenuAction escapeMenu_updateUI();
#ifdef _XBOX
	void escapeMenu_drawXboxOverlay();
	EscapeMenuAction escapeMenu_updateXboxUI();
	const char* escapeMenu_xboxButtonName(s32 button);
#endif

	extern void pauseLevelSound();
	extern void resumeLevelSound();
	extern void clearBufferedSound();

	void escapeMenu_resetState()
	{
#ifdef _XBOX
		TFE_RenderBackend::xboxSetPauseOverlay(false, 0, 0, false);
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
			case ESC_BTN_RESPAWN:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "Respawn selected: no checkpoint respawn handler wired yet");
				break;
			case ESC_BTN_CHEAT:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "Enter Cheat Code selected: no cheat-entry UI wired yet");
				break;
			case ESC_BTN_ABORT:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "open Abort Mission confirm");
				s_emState.confirmState = s_levelComplete ? CONFIRM_STATE_NEXT : CONFIRM_STATE_ABORT;
				s_emState.buttonPressed = CONFIRM_NO;
				s_emState.buttonHover = true;
				break;
			case ESC_BTN_OPTIONS:
				TFE_System::logWrite(LOG_MSG, "PauseMenu", "Options selected: no options UI wired yet");
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
			case ESC_BTN_RESPAWN: return "Respawn";
			case ESC_BTN_OPTIONS: return "Options";
			case ESC_BTN_CHEAT:   return "Enter Cheat Code";
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
		TFE_RenderBackend::xboxSetPauseOverlay(true,
			s_emState.buttonPressed,
			s_emState.buttonPressed,
			s_emState.confirmState != CONFIRM_STATE_NONE);
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
