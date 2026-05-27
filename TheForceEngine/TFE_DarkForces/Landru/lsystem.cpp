#include "lsystem.h"
#include "lactor.h"
#include "lactorAnim.h"
#include "lactorCust.h"
#include "lactorDelt.h"
#include "lcanvas.h"
#include "lfade.h"
#include "lfont.h"
#include "lmusic.h"
#include "ltimer.h"
#include "lpalette.h"
#include "lsound.h"
#include "lview.h"
#include "ldraw.h"
#include <TFE_Archive/lfdArchive.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <assert.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	static JBool s_lsystemInit = JFALSE;
	static LfdArchive s_archive;
	static LfdArchive s_soundFx;
	static MemoryRegion* s_lmem = nullptr;
	static MemoryRegion* s_lscene = nullptr;
	MemoryRegion* s_alloc = nullptr;

	enum LandruConstants
	{
		LANDRU_MEMORY_BASE   = 4 * 1024 * 1024, // 4 MB
		// Original Xbox hardware can fail one large contiguous 8 MB allocation
		// after game startup. The region allocator grows with more blocks, so
		// use smaller blocks while preserving the cutscene arena behavior.
		CUTSCENE_MEMORY_BASE = 2 * 1024 * 1024, // 2 MB
	};

	static MemoryRegion* createLandruRegion(const char* name, u32 preferredSize)
	{
		MemoryRegion* region = TFE_Memory::region_create(name, preferredSize);
		if (region) { return region; }

#ifdef _XBOX
		u32 fallbackSize = preferredSize >> 1;
		while (!region && fallbackSize >= 256 * 1024)
		{
			TFE_System::logWrite(LOG_WARNING, "Landru", "region '%s' retry with %u byte blocks", name, fallbackSize);
			region = TFE_Memory::region_create(name, fallbackSize);
			fallbackSize >>= 1;
		}
#endif
		return region;
	}
	
	void lsystem_init()
	{
		if (s_lsystemInit) { return; }
		s_lmem = createLandruRegion("Landru", LANDRU_MEMORY_BASE);
		s_lscene = createLandruRegion("Cutscene", CUTSCENE_MEMORY_BASE);
		TFE_System::logWrite(LOG_MSG, "Landru", "regions persistent=%p cutscene=%p", s_lmem, s_lscene);
		lsystem_setAllocator(LALLOC_PERSISTENT);

		s_lsystemInit = JTRUE;
		lcanvas_init(320, 200);
		ltime_init();
		lview_init();
		lpalette_init();
		lfade_init();
		lfont_init();
		lmusic_init();
		lSoundInit();

		lactor_init();
		lactorDelt_init();
		lactorAnim_init();
		lactorCust_init();

		FilePath lfdPath;
		if (TFE_Paths::getFilePath("menu.lfd", &lfdPath))
		{
			s_archive.open(lfdPath.path);
			TFE_Paths::addLocalArchive(&s_archive);

			// Default font used by in-game UI.
			lfont_load("font8", 0);
			lfont_set(0);

			s_archive.close();
			TFE_Paths::removeLastArchive();
		}

		FilePath sfxPath;
		if (TFE_Paths::getFilePath("jedisfx.lfd", &sfxPath))
		{
			s_soundFx.open(sfxPath.path);
			TFE_Paths::addLocalArchive(&s_soundFx);
		}
	}

	void lsystem_destroy()
	{
		if (!s_lsystemInit) { return; }
		vfb_forceToBlack();

		s_lsystemInit = JFALSE;
		lcanvas_destroy();
		lview_destroy();
		lpalette_destroy();
		lfade_destroy();
		lfont_destroy();
		lmusic_destroy();
		lSoundDestroy();

		lactorCust_destroy();
		lactorAnim_destroy();
		lactorDelt_destroy();
		lactor_destroy();

		if (s_lmem) { TFE_Memory::region_destroy(s_lmem); }
		if (s_lscene) { TFE_Memory::region_destroy(s_lscene); }
		s_archive.close();
		s_soundFx.close();
		s_lmem = nullptr;
		s_lscene = nullptr;
		s_alloc = nullptr;
	}

	void lsystem_setAllocator(LAllocator alloc)
	{
		MemoryRegion* region = (alloc == LALLOC_PERSISTENT) ? s_lmem : s_lscene;
		if (!region)
		{
			TFE_System::logWrite(LOG_ERROR, "Landru", "allocator %d unavailable; using persistent=%p cutscene=%p", alloc, s_lmem, s_lscene);
			region = s_lmem ? s_lmem : s_lscene;
		}
		s_alloc = region;
	}

	void lsystem_clearAllocator(LAllocator alloc)
	{
		MemoryRegion* region = (alloc == LALLOC_PERSISTENT) ? s_lmem : s_lscene;
		if (!region)
		{
			TFE_System::logWrite(LOG_ERROR, "Landru", "clear allocator %d skipped; persistent=%p cutscene=%p", alloc, s_lmem, s_lscene);
			return;
		}
		TFE_Memory::region_clear(region);
	}
}  // namespace TFE_DarkForces
