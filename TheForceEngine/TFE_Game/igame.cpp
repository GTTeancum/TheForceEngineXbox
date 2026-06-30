#include "igame.h"
#include <TFE_FrontEndUI/console.h>
#include <TFE_DarkForces/darkForcesMain.h>
#ifdef _XBOX
#include <TFE_System/system.h>
#endif
#ifndef _XBOX
#include <TFE_Outlaws/outlawsMain.h>
#endif

enum GameConstants
{
#ifdef _XBOX
	GAME_MEMORY_BASE  = 1024 * 1024, // 1 MB chunks; OG Xbox cannot afford 8 MB growth slabs.
	LEVEL_MEMORY_BASE = 1024 * 1024, // Large enough for known level/HUD allocations, small enough to grow safely.
	RES_MEMORY_BASE   = 1024 * 1024,
#else
	GAME_MEMORY_BASE  = 8 * 1024 * 1024, // 8 MB
	LEVEL_MEMORY_BASE = 8 * 1024 * 1024, // 8 MB
	RES_MEMORY_BASE   = 8 * 1024 * 1024, // 8 MB
#endif
};

using namespace TFE_Memory;
MemoryRegion* s_gameRegion = nullptr;
MemoryRegion* s_levelRegion = nullptr;

#ifdef _XBOX
static void logRegionState(const char* label)
{
	u64 gameUsed = s_gameRegion ? region_getMemoryUsed(s_gameRegion) : 0;
	u64 gameCap = s_gameRegion ? region_getMemoryCapacity(s_gameRegion) : 0;
	u64 levelUsed = s_levelRegion ? region_getMemoryUsed(s_levelRegion) : 0;
	u64 levelCap = s_levelRegion ? region_getMemoryCapacity(s_levelRegion) : 0;
	TFE_System::logWrite(LOG_MSG, "Game",
		"%s game=%u/%u level=%u/%u",
		label, (u32)gameUsed, (u32)gameCap, (u32)levelUsed, (u32)levelCap);
}
#endif

static void createMemoryRegions()
{
	s_gameRegion  = region_create("game",  GAME_MEMORY_BASE);	// Region for "permanent" game allocations.
	s_levelRegion = region_create("level", LEVEL_MEMORY_BASE);	// Region for "per-level" game allocations.
#ifdef _XBOX
	TFE_System::logWrite(LOG_MSG, "Game", "memory regions initialised gameBlock=%u levelBlock=%u game=%p level=%p",
		(u32)GAME_MEMORY_BASE, (u32)LEVEL_MEMORY_BASE, s_gameRegion, s_levelRegion);
#endif
}

static void destroyMemoryRegions()
{
	if (s_gameRegion)
	{
		region_destroy(s_gameRegion);
		s_gameRegion = nullptr;
	}
	if (s_levelRegion)
	{
		region_destroy(s_levelRegion);
		s_levelRegion = nullptr;
	}
}

void displayMemoryUsage(const ConsoleArgList& args)
{
	char res[256];
	u64 blockCount, blockSize;
	region_getBlockInfo(s_gameRegion, &blockCount, &blockSize);
	TFE_Console::addToHistory("-------------------------------------------------------------------");
	TFE_Console::addToHistory("Region   | Memory Used | Current Capacity | Block Count | BlockSize");
	TFE_Console::addToHistory("-------------------------------------------------------------------");
	sprintf(res, "Game     | %11zu | %16zu | %11zu | %9zu", region_getMemoryUsed(s_gameRegion), region_getMemoryCapacity(s_gameRegion), blockCount, blockSize);
	TFE_Console::addToHistory(res);

	region_getBlockInfo(s_levelRegion, &blockCount, &blockSize);
	sprintf(res, "Level    | %11zu | %16zu | %11zu | %9zu", region_getMemoryUsed(s_levelRegion), region_getMemoryCapacity(s_levelRegion), blockCount, blockSize);
	TFE_Console::addToHistory(res);
	TFE_Console::addToHistory("-------------------------------------------------------------------");
}

void game_init()
{
	createMemoryRegions();

	CCMD("displayMemoryUsage", displayMemoryUsage, 0, "Display memory usage.");
}

void game_destroy()
{
	destroyMemoryRegions();
}

void game_clearLevelData()
{
#ifdef _XBOX
	game_resetLevelRegion("game_clearLevelData");
#else
	region_clear(s_levelRegion);
#endif
}

void game_resetLevelRegion(const char* context)
{
#ifdef _XBOX
	TFE_System::logWrite(LOG_MSG, "Game", "resetLevelRegion begin context='%s'", context ? context : "");
	logRegionState("resetLevelRegion before");

	MemoryRegion* oldRegion = s_levelRegion;
	MemoryRegion* newRegion = region_create("level", LEVEL_MEMORY_BASE);
	if (newRegion)
	{
		if (oldRegion)
		{
			region_destroy(oldRegion);
		}
		s_levelRegion = newRegion;
	}
	else
	{
		s_levelRegion = oldRegion;
		if (s_levelRegion)
		{
			TFE_System::logWrite(LOG_WARNING, "Game", "resetLevelRegion could not preallocate replacement; keeping old region");
			region_clear(s_levelRegion);
		}
		else
		{
			TFE_System::logWrite(LOG_ERROR, "Game", "resetLevelRegion failed with no existing level region");
		}
	}

	logRegionState("resetLevelRegion after");
#else
	region_clear(s_levelRegion);
#endif
}

IGame* createGame(GameID id)
{
	IGame* game = nullptr;
	switch (id)
	{
		case Game_Dark_Forces:
		{
			game = new TFE_DarkForces::DarkForces();
		} break;
#ifndef _XBOX
		case Game_Outlaws:
		{
			game = new TFE_Outlaws::Outlaws();
		} break;
#endif
	}
	if (game)
	{
		game->id = id;
	}

	return game;
}

void freeGame(IGame* game)
{
	if (game)
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "Game", "freeGame exitGame begin game=%p", game);
#endif
		game->exitGame();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "Game", "freeGame delete begin game=%p", game);
#endif
		delete game;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "Game", "freeGame delete complete");
#endif
	}
#ifdef _XBOX
	logRegionState("freeGame before region reset");
	// A hardware transition should give memory back to the system, not keep the
	// highest previous mod/level allocation resident until shutdown.
	destroyMemoryRegions();
	createMemoryRegions();
#else
	region_clear(s_gameRegion);
	region_clear(s_levelRegion);
#endif
}
