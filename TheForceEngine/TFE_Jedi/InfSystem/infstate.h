#pragma once
//////////////////////////////////////////////////////////////////////
// INF System State
// Internal to the INF system.
// Note - this file should *only* be included by internal cpp files.
//////////////////////////////////////////////////////////////////////
#include "infPublicTypes.h"
#include <TFE_System/types.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_Jedi/Math/fixedPoint.h>

namespace TFE_Jedi
{
	struct Stop;

	// State that should be serialized for quick-saves.
	struct InfSerializableState
	{
#ifndef _XBOX
		s32 activeTriggerCount = 0;
		Allocator* infElevators = nullptr;
		Allocator* infTeleports = nullptr;
		Allocator* infTriggers = nullptr;
#else
		s32 activeTriggerCount;
		Allocator* infElevators;
		Allocator* infTeleports;
		Allocator* infTriggers;
#endif
	};
	extern InfSerializableState s_infSerState;

	// General state that should be cleared on reset but does not need to be serialized.
	struct InfState
	{
#ifndef _XBOX
		Task* infElevTask = nullptr;
		Task* infTriggerTask = nullptr;
		Task* teleportTask = nullptr;
		Stop* nextStop = nullptr;
#else
		Task* infElevTask;
		Task* infTriggerTask;
		Task* teleportTask;
		Stop* nextStop;
#endif
	};
	extern InfState s_infState;
}
