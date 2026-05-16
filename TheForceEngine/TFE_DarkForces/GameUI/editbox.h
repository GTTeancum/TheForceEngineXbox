#pragma once
//////////////////////////////////////////////////////////////////////
// A simple edit box.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Level/rfont.h>

struct EditBox
{
#ifndef _XBOX
	char* inputField = nullptr;
	s32   cursor = 0;
	s32   maxLen = 0;
#else
	char* inputField;
	s32   cursor;
	s32   maxLen;
#endif
};

namespace TFE_DarkForces
{
	void updateEditBox(EditBox* editBox);
	void drawEditBox(EditBox* editBox, s32 x0, s32 y0, s32 x1, s32 y1, u8* framebuffer);
}
