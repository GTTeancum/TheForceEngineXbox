#include <cstring>

#include "darkForcesMain.h"
#include "agent.h"
#include "automap.h"
#include "config.h"
#include "briefingList.h"
#include "gameMessage.h"
#include "gameMusic.h"
#include "hud.h"
#include "item.h"
#include "mission.h"
#include "player.h"
#include "pickup.h"
#include "projectile.h"
#include "random.h"
#include "time.h"
#include "weapon.h"
#include "vueLogic.h"
#include "GameUI/menu.h"
#include "GameUI/agentMenu.h"
#include "GameUI/escapeMenu.h"
#include "GameUI/missionBriefing.h"
#include "GameUI/pda.h"
#include "Landru/lsystem.h"
#include "Landru/lmusic.h"
#include "Landru/cutscene_film.h"
#include <TFE_DarkForces/Landru/cutscene.h>
#include <TFE_DarkForces/Landru/cutsceneList.h>
#include <TFE_DarkForces/Actor/actor.h>
#include <TFE_Game/reticle.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_System/tfeMessage.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_ForceScript/scriptInterface.h>
#include <TFE_A11y/accessibility.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Archive/archive.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Jedi/Level/levelData.h>
#include <TFE_Jedi/Level/rfont.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_ExternalData/weaponExternal.h>
#include <TFE_ExternalData/pickupExternal.h>
#include <assert.h>

// Add texture callbacks.
#include <TFE_Jedi/Level/levelTextures.h>

#ifdef _XBOX
extern "C" void TFE_XboxReturnToStartMenu();
#endif

using namespace TFE_Memory;
using namespace TFE_Input;

namespace TFE_DarkForces
{
#ifdef _XBOX
	static bool s_xboxIntroOnly = false;
	static bool s_xboxStartAtBriefing = false;
	static bool s_xboxMissionCompleteOpen = false;
	static s32 s_xboxMissionCompleteSelection = 0;
	static u32 s_xboxMissionCompleteFrame = 0;
	static bool s_xboxMissionCompleteLeftHeld = false;
	static bool s_xboxMissionCompleteRightHeld = false;
	static bool s_xboxPendingMissionCompleteAutosave = false;
	static s32 s_xboxPendingMissionCompleteAutosaveFrames = 0;
	static TFE_RenderBackend::XboxMissionCompleteInfo s_xboxMissionCompleteInfo = { 0, 0, 0, 0 };

	void xboxMissionCompleteOpen()
	{
		s_xboxMissionCompleteOpen = true;
		s_xboxMissionCompleteSelection = 0;
		s_xboxMissionCompleteFrame = 0;
		s_xboxMissionCompleteLeftHeld = false;
		s_xboxMissionCompleteRightHeld = false;
		s_xboxMissionCompleteInfo.seconds = s_curTick / TICKS_PER_SECOND;
		s_xboxMissionCompleteInfo.secretsFound = s_secretsFound;
		s_xboxMissionCompleteInfo.secretsTotal = TFE_Jedi::s_levelState.secretCount;
		s_xboxMissionCompleteInfo.difficulty = s_agentData[s_agentId].difficulty;
		TFE_RenderBackend::xboxSetMissionCompleteScreen(true, s_xboxMissionCompleteSelection, s_xboxMissionCompleteFrame, &s_xboxMissionCompleteInfo);
		TFE_System::logWrite(LOG_MSG, "MissionComplete", "open time=%u secrets=%d/%d difficulty=%d",
			s_xboxMissionCompleteInfo.seconds, s_xboxMissionCompleteInfo.secretsFound,
			s_xboxMissionCompleteInfo.secretsTotal, s_xboxMissionCompleteInfo.difficulty);
	}

	bool xboxMissionCompleteUpdate(bool* saveSelected)
	{
		const f32 lx = TFE_Input::getAxis(AXIS_LEFT_X);
		const bool stickLeft = lx < -0.55f;
		const bool stickRight = lx > 0.55f;
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_LEFT) || (stickLeft && !s_xboxMissionCompleteLeftHeld))
		{
			s_xboxMissionCompleteSelection = 0;
		}
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_RIGHT) || (stickRight && !s_xboxMissionCompleteRightHeld))
		{
			s_xboxMissionCompleteSelection = 1;
		}
		s_xboxMissionCompleteLeftHeld = stickLeft;
		s_xboxMissionCompleteRightHeld = stickRight;

		TFE_RenderBackend::xboxSetMissionCompleteScreen(true, s_xboxMissionCompleteSelection, s_xboxMissionCompleteFrame++, &s_xboxMissionCompleteInfo);

		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
		{
			*saveSelected = false;
			TFE_System::logWrite(LOG_MSG, "MissionComplete", "confirm save=0");
			return true;
		}
		if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
		{
			*saveSelected = (s_xboxMissionCompleteSelection == 0);
			TFE_System::logWrite(LOG_MSG, "MissionComplete", "confirm save=%d", *saveSelected ? 1 : 0);
			return true;
		}
		return false;
	}

	void xboxMaybeRunPendingMissionCompleteAutosave()
	{
		if (!s_xboxPendingMissionCompleteAutosave) { return; }
		if (s_xboxPendingMissionCompleteAutosaveFrames++ < 2) { return; }

		char autosaveName[TFE_MAX_PATH];
		char saveLabel[TFE_SaveSystem::SAVE_MAX_NAME_LEN];
		const char* levelName = agent_getLevelDisplayName();
		sprintf(autosaveName, "save000.tfe");
		if (levelName && levelName[0])
		{
			strncpy(saveLabel, levelName, sizeof(saveLabel) - 1);
			saveLabel[sizeof(saveLabel) - 1] = 0;
		}
		else
		{
			strcpy(saveLabel, "Autosave");
		}

		const bool saved = TFE_SaveSystem::saveGameQuiet(autosaveName, saveLabel);
		TFE_System::logWrite(saved ? LOG_MSG : LOG_ERROR, "MissionComplete",
			"next-level autosave %s filename='%s' label='%s'", saved ? "complete" : "failed", autosaveName, saveLabel);
		s_xboxPendingMissionCompleteAutosave = false;
		s_xboxPendingMissionCompleteAutosaveFrames = 0;
	}
#endif

	/////////////////////////////////////////////
	// Constants
	/////////////////////////////////////////////
	static const char* c_gobFileNames[] =
	{
		"DARK.GOB",
		"SOUNDS.GOB",
		"TEXTURES.GOB",
		"SPRITES.GOB",
	};

	static const char* c_optionalGobFileNames[] =
	{
		"enhanced.gob",
	};

	enum GameConstants
	{
		MAX_MOD_LFD = 16,
	};

	enum GameState
	{
		GSTATE_STARTUP_CUTSCENES = 0,
		GSTATE_AGENT_MENU,
		GSTATE_CUTSCENE,
		GSTATE_BRIEFING,
		GSTATE_MISSION,
		GSTATE_COUNT
	};

	enum GameMode
	{
		GMODE_END = -1,
		GMODE_CUTSCENE = 0,
		GMODE_BRIEFING = 1,
		GMODE_MISSION = 2,
	};

	struct CutsceneData
	{
		s32 levelIndex;
		GameMode nextGameMode;
		s32 cutscene;
	};

	static CutsceneData s_cutsceneData[] =
	{
		// Startup intro is handled separately through scene 10. This table is
		// the campaign flow after Start Game / Agent Menu selection. Only list
		// scene IDs that actually exist in CUTSCENE.LST; missing placeholder
		// IDs make progression look broken even when the fallback skip works.
		{ 1,  GMODE_BRIEFING,   0 },
		{ 1,  GMODE_MISSION,	0 },

		{ 2,  GMODE_CUTSCENE, 200 },
		{ 2,  GMODE_BRIEFING,   0 },
		{ 2,  GMODE_MISSION,    0 },

		{ 3,  GMODE_BRIEFING,   0 },
		{ 3,  GMODE_MISSION,	0 },

		{ 4,  GMODE_BRIEFING,   0 },
		{ 4,  GMODE_MISSION,    0 },

		{ 5,  GMODE_CUTSCENE, 500 },
		{ 5,  GMODE_BRIEFING,	0 },
		{ 5,  GMODE_MISSION,	0 },
		{ 5,  GMODE_CUTSCENE, 550 },

		{ 6,  GMODE_CUTSCENE, 600 },
		{ 6,  GMODE_BRIEFING,	0 },
		{ 6,  GMODE_MISSION,	0 },

		{ 7,  GMODE_BRIEFING,	0 },
		{ 7,  GMODE_MISSION,	0 },

		{ 8,  GMODE_CUTSCENE, 800 },
		{ 8,  GMODE_BRIEFING,	0 },
		{ 8,  GMODE_MISSION, 	0 },
		{ 8,  GMODE_CUTSCENE, 850 },

		{ 9,  GMODE_BRIEFING,	0 },
		{ 9,  GMODE_MISSION,	0 },

		{ 10, GMODE_CUTSCENE,1000 },
		{ 10, GMODE_BRIEFING,   0 },
		{ 10, GMODE_MISSION,	0 },
		{ 10, GMODE_CUTSCENE,1050 },

		{ 11, GMODE_BRIEFING,	0 },
		{ 11, GMODE_MISSION,	0 },

		{ 12, GMODE_BRIEFING,	0 },
		{ 12, GMODE_MISSION,	0 },

		{ 13, GMODE_BRIEFING,	0 },
		{ 13, GMODE_MISSION,	0 },

		{ 14, GMODE_CUTSCENE,1400 },
		{ 14, GMODE_BRIEFING,   0 },
		{ 14, GMODE_MISSION,	0 },
		{ 14, GMODE_CUTSCENE,1450 },
		{ 14, GMODE_CUTSCENE,1500 },	//	game ending.
		// Game flow end (restart).
		{ -1, GMODE_END, -1 }
	};

	/////////////////////////////////////////////
	// Internal State
	/////////////////////////////////////////////
	struct RunGameState
	{
		s32 argCount;
		char* args[64];

		JBool cutscenesEnabled;
		JBool localMsgLoaded;
		s32   startLevel;
		GameState state;
		s32   levelIndex;
		s32   cutsceneIndex;
		JBool abortLevel;

		RunGameState()
			: argCount(0), cutscenesEnabled(JTRUE), localMsgLoaded(JFALSE)
			, startLevel(0), state(GSTATE_STARTUP_CUTSCENES)
			, levelIndex(0), cutsceneIndex(0), abortLevel(JFALSE)
		{
			memset(args, 0, sizeof(args));
		}
	};
	struct SharedGameState
	{
		GameMessages localMessages;
		GameMessages hotKeyMessages;
		TextureData* diskErrorImg;
		Font* swFont1;
		Font* mapNumFont;
		SoundSourceId screenShotSndSrc;
		BriefingList  briefingList;
		JBool gameStarted;

		Task* loadMissionTask;
		CutsceneState* cutsceneList;
		char customGobName[256];
		LangHotkeys langKeys;

		SharedGameState()
			: diskErrorImg(NULL), swFont1(NULL), mapNumFont(NULL)
			, screenShotSndSrc(NULL_SOUND), gameStarted(JFALSE)
			, loadMissionTask(NULL), cutsceneList(NULL)
		{
			memset(&localMessages,  0, sizeof(localMessages));
			memset(&hotKeyMessages, 0, sizeof(hotKeyMessages));
			memset(&briefingList,   0, sizeof(briefingList));
			customGobName[0] = 0;
		}
	};
	static RunGameState   s_runGameState;
	static SharedGameState s_sharedState;

	/////////////////////////////////////////////
	// Forward Declarations
	/////////////////////////////////////////////
	void printGameInfo();
	void processCommandLineArgs(s32 argCount, const char* argv[], char* startLevel);
	void enableCutscenes(JBool enable);
	void loadCustomGob(const char* gobName);
	void setInitialLevel(const char* levelName);
	s32  loadLocalMessages();
	void buildSearchPaths();
	bool openGobFiles();
	void gameStartup();
	void loadAgentAndLevelData();
	void startNextMode();
	void freeAllMidi();
	void pauseLevelSound();
	void resumeLevelSound();

	/////////////////////////////////////////////
	// API
	/////////////////////////////////////////////

	// This is the equivalent of the initial part of main() in Dark Forces DOS.
	// This part loads and sets up the game.
	bool DarkForces::runGame(s32 argCount, const char* argv[], Stream* stream)
	{
		s_runGameState = RunGameState();
		s_sharedState = SharedGameState();

		// TFE: Initially disable the reticle.
		reticle_enable(false);

		if (!stream)
		{
			// Normal start.
			s_runGameState.argCount = min(64, argCount);
			for (s32 i = 0; i < s_runGameState.argCount; i++)
			{
				if (i == 0)
				{
					// No need to store the executable path, just put in a dummy value so everything else works as-is.
					s_runGameState.args[i] = (char*)game_alloc(strlen("ExeName") + 1);
					strcpy(s_runGameState.args[i], "ExeName");
				}
				else
				{
					s_runGameState.args[i] = (char*)game_alloc(strlen(argv[i]) + 1);
					strcpy(s_runGameState.args[i], argv[i]);
				}
			}
		}
		else
		{
			// Start from save game.
			SERIALIZE(SaveVersionInit, s_runGameState.argCount, 0);
			for (s32 i = 0; i < s_runGameState.argCount; i++)
			{
				u32 length;
				SERIALIZE(SaveVersionInit, length, 0);

				s_runGameState.args[i] = (char*)game_alloc(length + 1);
				SERIALIZE_BUF(SaveVersionInit, s_runGameState.args[i], length);
				s_runGameState.args[i][length] = 0;
			}

			argCount = s_runGameState.argCount;
			argv = (const char**)s_runGameState.args;
		}

		char startLevel[TFE_MAX_PATH] = "";
		bitmap_setAllocator(s_gameRegion);

		bitmap_setCoreArchives(c_gobFileNames, TFE_ARRAYSIZE(c_gobFileNames));

		printGameInfo();
		buildSearchPaths();
		processCommandLineArgs(argCount, argv, startLevel);
		loadLocalMessages();
		if (!openGobFiles())
			return false;

		// Sound is initialized before the task system.
		sound_open(s_gameRegion);

		TFE_Jedi::task_setDefaults();
		TFE_Jedi::task_setMinStepInterval(1.0f / f32(TICKS_PER_SECOND));
		TFE_Jedi::setupInitCameraAndLights();
		config_startup();
		gameStartup();
		loadAgentAndLevelData();
		lsystem_init();

		renderer_init();

		// Handle start level
		setInitialLevel(startLevel);

		// TFE Specific
		agentMenu_load(&s_sharedState.langKeys);
		escapeMenu_load(&s_sharedState.langKeys);
		// Add texture callbacks.
		renderer_addHudTextureCallback(TFE_Jedi::level_getLevelTextures);
		renderer_addHudTextureCallback(TFE_Jedi::level_getObjectTextures);

		// Deserialize.
		if (stream)
		{
			SERIALIZE(SaveVersionInit, s_runGameState.cutscenesEnabled, JTRUE);
			SERIALIZE(SaveVersionInit, s_runGameState.localMsgLoaded, JFALSE);
			SERIALIZE(SaveVersionInit, s_runGameState.startLevel, 0);
			SERIALIZE(SaveVersionInit, s_runGameState.state, GSTATE_STARTUP_CUTSCENES);
			SERIALIZE(SaveVersionInit, s_runGameState.levelIndex, 0);
			SERIALIZE(SaveVersionInit, s_runGameState.cutsceneIndex, 0);
			SERIALIZE(SaveVersionInit, s_runGameState.abortLevel, 0);
		}

		s_sharedState.gameStarted = JTRUE;
		sound_setLevelStart();

		// TFE
#ifndef _XBOX
		TFE_ScriptInterface::registerScriptInterface(API_GAME);
		TFE_ScriptInterface::setAPI(API_GAME, nullptr);
#endif

		return true;
	}

	void DarkForces::exitGame()
	{
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame begin state=%d introOnly=%d gameStarted=%d",
			s_runGameState.state, s_xboxIntroOnly ? 1 : 0, s_sharedState.gameStarted ? 1 : 0);
	#endif
		if (s_sharedState.gameStarted && !s_xboxIntroOnly && s_runGameState.state == GSTATE_MISSION)
		{
			saveLevelStatus();
		}
	#ifdef _XBOX
		else if (s_sharedState.gameStarted)
		{
			TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame skipping save status state=%d introOnly=%d",
				s_runGameState.state, s_xboxIntroOnly ? 1 : 0);
		}
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame cutscene shutdown");
	#endif
		cutscene_shutdown();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame free midi/messages");
	#endif
		freeAllMidi();

		gameMessage_freeBuffer();
		briefingList_freeBuffer();
		cutsceneList_freeBuffer();
		cutsceneFilm_reset();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame destroy landru");
	#endif
		lsystem_destroy();
		bitmap_clearAll();

		// Clear task/path search state. Archive objects are cache-owned; the path
		// stack is non-owning because Landru also mounts static archives there.
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame task shutdown begin");
	#endif
		task_shutdown();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame task shutdown end");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame clear search paths begin");
	#endif
		TFE_Paths::clearSearchPaths();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame clear search paths end");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame clear local archives begin");
	#endif
		TFE_Paths::clearLocalArchives();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame clear local archives end");
	#endif

		// Sound is destroyed after the task system.
		sound_close();
		config_shutdown();

		// TFE Specific
		// Reset state
		actor_exitState();
		weapon_resetState();
		renderer_resetState();
		agentMenu_resetState();
		menu_resetState();
		pda_resetState();
		escapeMenu_resetState();
		vue_resetState();
		lsystem_destroy();
		hud_reset();

		// TFE
		TFE_Sprite_Jedi::freeAll();
		TFE_Model_Jedi::freeAll();
		reticle_enable(false);
		texturepacker_reset();
		freeLevelScript();

		TFE_MidiPlayer::resume();
		TFE_Audio::resume();

		TFE_Jedi::renderer_destroy();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame renderer destroyed");
	#endif

		// Reset state.
		memset(&s_sharedState, 0, sizeof(s_sharedState));
		memset(&(s_runGameState), 0, sizeof(s_runGameState));
	#ifdef _XBOX
		s_xboxIntroOnly = false;
	#endif

		// TFE - Script system.
		TFE_ScriptInterface::reset();
	#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "exitGame complete");
	#endif
	}

	void DarkForces::pauseGame(bool pause)
	{
		mission_pause(pause ? JTRUE : JFALSE);
	}

	bool DarkForces::isPaused()
	{
		return s_gamePaused;
	}

	void DarkForces::pauseSound(bool pause)
	{
		if (pause) { pauseLevelSound(); }
		else { resumeLevelSound(); }
	}

	void DarkForces::restartMusic()
	{
		gameMusic_setState(MUS_STATE_NULLSTATE);
		ImReintializeMidi();
		gameMusic_setState(MUS_STATE_STALK);
	}

	void handleLevelComplete()
	{
		s32 completedLevelIndex = agent_getLevelIndex();
		u8 diff = s_agentData[s_agentId].difficulty;

		// Save the level completion, inventory and other stats into the agent data and then save to disk.
		agent_saveLevelCompletion(diff, completedLevelIndex);
		agent_updateAgentSavedData();
		agent_saveInventory(s_agentId, completedLevelIndex + 1);
		agent_setNextLevelByIndex(completedLevelIndex + 1);
	}

	void saveLevelStatus()
	{
		if (s_levelComplete)
		{
			handleLevelComplete();
		}
		else
		{
			agent_updateAgentSavedData();
		}
	}

	bool DarkForces::canSave()
	{
		return s_runGameState.state == GSTATE_MISSION;
	}

	void DarkForces::getLevelName(char* name)
	{
		const char* levelName = agent_getLevelDisplayName();
		if (levelName)
		{
			strcpy(name, levelName);
		}
		else
		{
			name[0] = 0;
		}
	}

	void DarkForces::getLevelId(char* name)
	{
		const char* levelName = agent_getLevelName();
		if (levelName)
		{
			strcpy(name, levelName);
		}
		else
		{
			name[0] = 0;
		}
	}

	void skipToLevelNextScene(s32 index)
	{
		if (!index) { return; }

		s_invalidLevelIndex = JTRUE;
		for (s32 i = 0; i < TFE_ARRAYSIZE(s_cutsceneData); i++)
		{
			if (s_cutsceneData[i].levelIndex >= 0 && s_cutsceneData[i].levelIndex == index && s_cutsceneData[i].nextGameMode == GMODE_BRIEFING)
			{
				s_runGameState.cutsceneIndex = i - 1;
				s_invalidLevelIndex = JFALSE;
				break;
			}
		}
	}

	void DarkForces::getModList(char* modList)
	{
		strcpy(modList, s_sharedState.customGobName);
	}

	/**********The basic structure of the Dark Forces main loop is as follows:***************
	while (1)  // <- This will be replaced by the function call from the main TFE loop.
	{
		// TFE: This becomes a game state in TFE - where runAgentMenu() gets an update function - it can't loop forever.
		s32 levelIndex = runAgentMenu();  <- this loops internally until complete and returns the level to load.
		updateAgentSavedData();	// handle any agent changes.

		// Invalid level index just maps back to the runAgentMenu().
		if (levelIndex <= 0) { continue; }  // <- This becomes a return in TFE, back off and try again.

		// Then we go through the list of cutscenes, looking for the first instance that matches our desired level.
		s32 levelDataIndex;	// <- this is the index into the cutscene list.
		for (s32 i = 0; ; i++)
		{
			if (cutsceneData[i].levelIndex >= 0 && s_agentLevelData[n].levelIndex == levelIndex)
			{
				levelDataIndex = i;
				break;
			}
		}
		// Then do some init setup for the next level ahead of time; the actual loading will happen after the cutscenes and mission briefing.
		setLevelByIndex(levelIndex);

		// The inner most loop - this cycles through the cutscene entries, each of which lists the game mode.
		// TFE: Again this becomes a game state, where each iteration through this loop is from a single function call into loopGame().
		while (!s_invalidLevelIndex && !s_abortLevel)
		{
			GameMode mode = s_agentLevelData[levelDataIndex].nextGameMode;
			switch (mode)
			{
				case GMODE_ERROR:
					// Error out.
				break;
				case GMODE_CUTSCENE:
					// TFE: Cutscene playback becomes a state.
					// Play the cutscene
					levelDataIndex++;	// <- next loop, this goes to the next cutscene or instruction.
				break;
				case GMODE_BRIEFING:
					// TFE: Briefing becomes a state.
					// Handle the mission briefing.
					levelDataIndex++;	// <- next loop, this goes to the next cutscene or instruction.
				break;
				case GMODE_MISSION:
					// TFE: In Mission becomes a state, but here the Task System will take up the slack (which will act very similarly to the original game).
					// Create the loadMission task, which will then create the main task, etc.
					// Start the level music.
					// Read saved game data for this agent and this level.
					// Launch the level load task.
					// After returning from the level load task, stop the music.
					// If not complete set abortLevel to true (which breaks out of this inner loop) otherwise

					// Save the game state and level completion info to disk.
					levelDataIndex++;	// <- next loop, this goes to the next cutscene or instruction.
					levelIndex++;
					setNextLevelByIndex(levelIndex);	// <- prepares the next level for when we get to it after the cutscenes and briefing.
				break;
			}
		}  // Inner Loop
	}  // Outer Loop
	****************************************************/
	void DarkForces::loopGame()
	{
		updateTime();

		switch (s_runGameState.state)
		{
		case GSTATE_STARTUP_CUTSCENES:
		{
			s_runGameState.state = GSTATE_CUTSCENE;
			s_invalidLevelIndex = JTRUE;

			// Always force cutscenes off for demo playbac for cutscenes. 
			if (isDemoPlayback())
			{
				s_runGameState.cutscenesEnabled = JFALSE;
			}

			if (s_runGameState.startLevel)
			{
				s_runGameState.abortLevel = JFALSE;
				s_runGameState.levelIndex = s_runGameState.startLevel;
				s_runGameState.startLevel = 0;
#ifdef _XBOX
				if (s_xboxStartAtBriefing)
				{
					s_xboxStartAtBriefing = false;
					s_invalidLevelIndex = JTRUE;
					for (s32 i = 0; i < TFE_ARRAYSIZE(s_cutsceneData); i++)
					{
						if (s_cutsceneData[i].levelIndex >= 0 &&
							s_cutsceneData[i].levelIndex == s_runGameState.levelIndex &&
							s_cutsceneData[i].nextGameMode == GMODE_BRIEFING)
						{
							s_runGameState.cutsceneIndex = i;
							s_invalidLevelIndex = JFALSE;
							break;
						}
					}
				}
				else
#endif
				{
					skipToLevelNextScene(s_runGameState.levelIndex);
				}
				lmusic_reset();
				agent_setNextLevelByIndex(s_runGameState.levelIndex);
				startNextMode();
				break;
			}

			if (s_runGameState.cutscenesEnabled && !s_runGameState.startLevel)
			{
				s_invalidLevelIndex = JFALSE;
				s_runGameState.levelIndex = 1;
				s_runGameState.cutsceneIndex = -1;
				agent_setNextLevelByIndex(1);
				if (!cutscene_play(10))
				{
#ifdef _XBOX
					if (s_xboxIntroOnly)
					{
						TFE_System::logWrite(LOG_ERROR, "Main", "Xbox startup intro scene 10 failed; returning to start menu");
						s_xboxIntroOnly = false;
						TFE_XboxReturnToStartMenu();
						break;
					}
#endif
					s_runGameState.cutsceneIndex = 0;
					startNextMode();
				}
			}
			else
			{
				startNextMode();
			}
		} break;
		case GSTATE_AGENT_MENU:
		{
			bool levelSelected = false;
			bool startLevelSelected = false;
			if (s_runGameState.startLevel)
			{
				s_runGameState.abortLevel = JFALSE;
				s_runGameState.levelIndex = s_runGameState.startLevel;
				s_runGameState.startLevel = 0;
				levelSelected = true;
				startLevelSelected = true;
			}
			else if (!agentMenu_update(&s_runGameState.levelIndex))
			{
				agent_updateAgentSavedData();
				levelSelected = true;
			}

			if (levelSelected)
			{
				if (startLevelSelected)
				{
					skipToLevelNextScene(s_runGameState.levelIndex);
				}
				else
				{
					s_invalidLevelIndex = JTRUE;
					for (s32 i = 0; i < TFE_ARRAYSIZE(s_cutsceneData); i++)
					{
						if (s_cutsceneData[i].levelIndex >= 0 && s_cutsceneData[i].levelIndex == s_runGameState.levelIndex)
						{
							s_runGameState.cutsceneIndex = i;
							s_invalidLevelIndex = JFALSE;
							break;
						}
					}
				}

				lmusic_reset();
				s_runGameState.abortLevel = JFALSE;
				agent_setNextLevelByIndex(s_runGameState.levelIndex);
				startNextMode();
			}
		} break;
		case GSTATE_CUTSCENE:
		{
			if (cutscene_update())
			{
				if (TFE_A11Y::cutsceneCaptionsEnabled()) { TFE_A11Y::drawCaptions(); }
			}
			else
			{
#ifdef _XBOX
				if (s_xboxIntroOnly)
				{
					TFE_System::logWrite(LOG_MSG, "Main", "Xbox startup intro complete; returning to start menu");
					s_xboxIntroOnly = false;
					TFE_XboxReturnToStartMenu();
					break;
				}
#endif
				s_runGameState.cutsceneIndex++;
				if (s_cutsceneData[s_runGameState.cutsceneIndex].nextGameMode == GMODE_END)
				{
					s_runGameState.state = GSTATE_AGENT_MENU;
					s_invalidLevelIndex = JTRUE;
				}
				else
				{
					startNextMode();
				}
				TFE_A11Y::clearActiveCaptions();
			}
		} break;
		case GSTATE_BRIEFING:
		{
			s32 skill;
			JBool abort;
			lmusic_reset();	// Fix a Dark Forces bug where music won't play when entering a cutscene again without restarting.
			if (!missionBriefing_update(&skill, &abort))
			{
				missionBriefing_cleanup();
				TFE_Input::clearAccumulatedMouseMove();

				if (abort)
				{
#ifdef _XBOX
					TFE_XboxReturnToStartMenu();
#else
					s_invalidLevelIndex = JTRUE;
					s_runGameState.cutsceneIndex--;
#endif
				}
				else
				{
					s_agentData[s_agentId].difficulty = skill;
					s_runGameState.cutsceneIndex++;
				}
#ifdef _XBOX
				if (!abort)
				{
					startNextMode();
				}
#else
				startNextMode();
#endif
			}
		} break;
		case GSTATE_MISSION:
		{
			// At this point the mission has already been launched.
			// The task system will take over. Basically every frame we just check to see if there are any tasks running.
			if (task_getCount())
			{
				if (!s_gamePaused && TFE_A11Y::gameplayCaptionsEnabled()) { TFE_A11Y::drawCaptions(); }
			}
			else
			{
#ifdef _XBOX
				bool xboxAbortToStart = false;
				if (s_levelComplete)
				{
					if (!s_xboxMissionCompleteOpen)
					{
						xboxMissionCompleteOpen();
						return;
					}

					bool saveSelected = false;
					if (!xboxMissionCompleteUpdate(&saveSelected))
					{
						return;
					}

					if (saveSelected)
					{
						s_xboxPendingMissionCompleteAutosave = true;
						s_xboxPendingMissionCompleteAutosaveFrames = 0;
						TFE_System::logWrite(LOG_MSG, "MissionComplete", "queued next-level autosave");
					}
					TFE_RenderBackend::xboxSetMissionCompleteScreen(false, 0, 0, NULL);
					s_xboxMissionCompleteOpen = false;
				}
#endif

				// We have returned from the mission tasks.
				renderer_reset();
				gameMusic_stop();
				sound_levelStop();
				agent_levelEndTask();
				lmusic_reset();	// Fix a Dark Forces bug where music won't play when entering a cutscene again without restarting.
				pda_cleanup();

				// Reset
				TFE_Jedi::renderer_setType(RENDERER_SOFTWARE);
				TFE_Jedi::render_setResolution();
				TFE_Jedi::renderer_setLimits();

				// TFE
				reticle_enable(false);
				// TFE - Script system.
				TFE_ScriptInterface::reset();

				if (!s_levelComplete)
				{
					s_runGameState.abortLevel = JTRUE;
#ifdef _XBOX
					xboxAbortToStart = true;
					TFE_System::logWrite(LOG_MSG, "Main", "Mission aborted; returning to Xbox start menu");
#else
					s_runGameState.cutsceneIndex--;
#endif
				}
				else
				{
					s_runGameState.cutsceneIndex++;
					handleLevelComplete();
				}

#ifdef _XBOX
				bitmap_clearLevelData();
				level_freeAllAssets();
				bitmap_setAllocator(s_gameRegion);
				region_clear(s_levelRegion);
				TFE_A11Y::clearActiveCaptions();

				if (xboxAbortToStart)
				{
					TFE_XboxReturnToStartMenu();
				}
				else
#endif
				{
					startNextMode();
				}
#ifndef _XBOX
				bitmap_clearLevelData();
				level_freeAllAssets();
				bitmap_setAllocator(s_gameRegion);
				region_clear(s_levelRegion);
				TFE_A11Y::clearActiveCaptions();
#endif
			}
		} break;
		}
	}

	void loadCutsceneList()
	{
		s_sharedState.cutsceneList = cutsceneList_load("cutscene.lst");
		cutscene_init(s_sharedState.cutsceneList);
	}

	void freeAllMidi()
	{
		gameMusic_stop();
	}

	void pauseLevelSound()
	{
		TFE_MidiPlayer::pause();
		TFE_Audio::pause();
	}

	void resumeLevelSound()
	{
		TFE_MidiPlayer::resume();
		TFE_Audio::resume();
	}

	void clearBufferedSound()
	{
		TFE_Audio::bufferedAudioClear();
	}

	void startNextMode()
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode begin invalid=%d abort=%d cutsceneIndex=%d levelIndex=%d state=%d",
			s_invalidLevelIndex ? 1 : 0, s_runGameState.abortLevel ? 1 : 0,
			s_runGameState.cutsceneIndex, s_runGameState.levelIndex, s_runGameState.state);
#endif
		if (s_invalidLevelIndex || s_runGameState.abortLevel)
		{
			s_runGameState.state = GSTATE_AGENT_MENU;
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode -> agent menu");
#endif
			return;
		}

		if (s_runGameState.cutsceneIndex < 0)
		{
			s_runGameState.cutsceneIndex = 0;
		}
		else if (s_runGameState.cutsceneIndex >= TFE_ARRAYSIZE(s_cutsceneData))
		{
			s_runGameState.cutsceneIndex = 0;
			s_invalidLevelIndex = JTRUE;
			startNextMode();
			return;
		}

		GameMode mode = s_cutsceneData[s_runGameState.cutsceneIndex].nextGameMode;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode mode=%d cutscene=%d",
			(s32)mode, s_cutsceneData[s_runGameState.cutsceneIndex].cutscene);
#endif
		switch (mode)
		{
		case GMODE_END:
		{
			s_runGameState.cutsceneIndex = 0;
			s_invalidLevelIndex = JTRUE;
			startNextMode();
		} break;
		case GMODE_CUTSCENE:
		{
			if (s_runGameState.cutscenesEnabled && cutscene_play(s_cutsceneData[s_runGameState.cutsceneIndex].cutscene))
			{
				s_runGameState.state = GSTATE_CUTSCENE;
			}
			else
			{
				s_runGameState.cutsceneIndex++;
				startNextMode();
			}
		} break;
		case GMODE_BRIEFING:
		{
			BriefingInfo* brief = nullptr;
			if (s_runGameState.cutscenesEnabled)
			{
				const char* levelName = agent_getLevelName();
				s32 briefingIndex = 0;
				for (s32 i = 0; i < s_sharedState.briefingList.count; i++)
				{
					if (strcasecmp(levelName, s_sharedState.briefingList.briefing[i].mission) == 0)
					{
						briefingIndex = i;
						break;
					}
				}

				s32 skill = (s32)s_agentData[s_agentId].difficulty;
				brief = &s_sharedState.briefingList.briefing[briefingIndex];
				if (brief)
				{
					missionBriefing_start(brief->archive, brief->bgAnim, levelName, brief->palette, skill, &s_sharedState.langKeys);
					s_runGameState.state = GSTATE_BRIEFING;
				}
			}

			if (!brief)
			{
				s_runGameState.cutsceneIndex++;
				startNextMode();
			}
		}  break;
		case GMODE_MISSION:
		{
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode mission begin");
#endif
			sound_levelStart();
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode sound_levelStart done");
#endif

			bitmap_setAllocator(s_levelRegion);
			actor_clearState();
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode actor/task clear begin");
#endif

			task_reset();
			inf_clearState();

			TFE_Settings_Game* gameSettings = TFE_Settings::getGameSettings();

			// Entry point to replay a demo
			if (gameSettings->df_enableReplay && !isDemoPlayback())
			{
				loadReplay();
			}

			// Entry point to recording a demo
			if (gameSettings->df_enableRecording && !isRecording())
			{
				startRecording();
			}

			s_sharedState.loadMissionTask = createTask("start mission", mission_startTaskFunc, JTRUE);
			mission_setLoadMissionTask(s_sharedState.loadMissionTask);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode loadMissionTask=%p", s_sharedState.loadMissionTask);
#endif

			s32 levelIndex = agent_getLevelIndex();
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode gameMusic_start begin levelIndex=%d", levelIndex);
#endif
			gameMusic_start(levelIndex);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode gameMusic_start done");
#endif

			agent_setLevelComplete(JFALSE);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode agent_readSavedDataForLevel begin agent=%d level=%d", s_agentId, levelIndex);
#endif
			agent_readSavedDataForLevel(s_agentId, levelIndex);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode agent_readSavedDataForLevel done");
#endif

			// The load mission task should begin immediately once the Task System updates,
			// so launchCurrentTask() is not required here.
			// In the original, the task system would simply loop here.
			s_runGameState.state = GSTATE_MISSION;
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "DarkForces", "startNextMode mission end state=%d", s_runGameState.state);
#endif
		}
		}
	}

	/////////////////////////////////////////////
	// Internal Implementation
	/////////////////////////////////////////////
	void printGameInfo()
	{
		TFE_System::logWrite(LOG_MSG, "Game", "Dark Forces Version: %d.%d (Build %d)", 1, 0, 1);
	}

	// Note: not all command line arguments have been brought over from the DOS version.
	// Many no longer make sense and in some cases will always be available (such as screenshots).
	void processCommandLineArgs(s32 argCount, const char* argv[], char* startLevel)
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "processCommandLineArgs begin argc=%d", argCount);
		for (s32 ai = 0; ai < argCount; ai++)
		{
			TFE_System::logWrite(LOG_MSG, "DarkForces", "argv[%d]='%s'", ai, argv && argv[ai] ? argv[ai] : "");
		}
#endif
		s_sharedState.customGobName[0] = 0;
#ifdef _XBOX
		s_xboxIntroOnly = false;
		s_xboxStartAtBriefing = false;
		agent_clearXboxCustomLevelName();
#endif
		TFE_Settings::clearModSettings();

		for (s32 i = 0; i < argCount; i++)
		{
			const char* arg = argv[i];
			char c = arg[0];

			if (c == '-' || c == '/' || c == '+')
			{
				c = arg[1];
				if (c == 'c' || c == 'C')
				{
					enableCutscenes(arg[2] == '1' ? JTRUE : JFALSE);
				}
				else if ((c == 'l' || c == 'L') && arg[2])
				{
					strncpy(startLevel, arg + 2, TFE_MAX_PATH - 1);
					startLevel[TFE_MAX_PATH - 1] = 0;
				}
				else if (c == 'u' || c == 'U')
				{
					loadCustomGob(arg + 2);
				}
#ifdef _XBOX
				else if ((c == 'x' || c == 'X') && strcasecmp(arg + 2, "intro") == 0)
				{
					s_xboxIntroOnly = true;
				}
				else if ((c == 'x' || c == 'X') && strcasecmp(arg + 2, "briefing") == 0)
				{
					s_xboxStartAtBriefing = true;
				}
#endif
			}
		}

		// TFE: Support drag and drop.
		if (argCount == 2)
		{
			const char* arg = argv[1];
			if (arg && arg[0] && arg[0] != '-')
			{
				const size_t len = strlen(arg);
				const char* ext = len > 3 ? &arg[len - 3] : nullptr;
				if (ext && strcasecmp(ext, "zip") == 0)
				{
					// Next check to see if the path is already in the local paths.
					char path[TFE_MAX_PATH];
					char fileName[TFE_MAX_PATH];
					size_t len = strlen(arg);
					size_t lastSlash = 0;
					for (size_t i = 0; i < len; i++)
					{
						if (arg[i] == '\\' || arg[i] == '/')
						{
							lastSlash = i;
						}
					}
					memcpy(path, arg, lastSlash + 1);
					memcpy(fileName, &arg[lastSlash + 1], len - lastSlash - 1);

					TFE_Paths::fixupPathAsDirectory(path);
					TFE_Paths::addAbsoluteSearchPath(path);
					TFE_System::logWrite(LOG_MSG, "DarkForces", "Drag and Drop Mod File: '%s'; Path: '%s'", fileName, path);

					loadCustomGob(fileName);
				}
			}
		}
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces",
			"processCommandLineArgs end startLevel='%s' customGob='%s' introOnly=%d startAtBriefing=%d cutscenes=%d",
			startLevel ? startLevel : "", s_sharedState.customGobName,
			s_xboxIntroOnly ? 1 : 0, s_xboxStartAtBriefing ? 1 : 0,
			s_runGameState.cutscenesEnabled ? 1 : 0);
		TFE_Paths::debugLogState("df-after-args");
#endif
	}

	void enableCutscenes(JBool enable)
	{
		s_runGameState.cutscenesEnabled = enable;
	}

	bool getCutscenesEnabled()
	{
		return s_runGameState.cutscenesEnabled;
	}

	void setInitialLevel(const char* levelName)
	{
		s_runGameState.startLevel = 0;

		if (!levelName || levelName[0] == 0) { return; }
		s_runGameState.startLevel = agent_getLevelIndexFromName(levelName);
#ifdef _XBOX
		if (!s_runGameState.startLevel && s_sharedState.customGobName[0])
		{
			agent_setXboxCustomLevelName(levelName);
			s_runGameState.startLevel = agent_getLevelIndexFromName(levelName);
			TFE_System::logWrite(LOG_MSG, "DarkForces", "Using Xbox custom mod level '%s' startIndex=%d", levelName, s_runGameState.startLevel);
		}
#endif
	}

	char* extractTextFileFromZip(ZipArchive& zip, u32 fileIndex)
	{
		u32 bufferLen = (u32)zip.getFileLength(fileIndex);
		char* buffer = (char*)malloc(bufferLen + 1);
		if (!buffer) { return nullptr; }
		zip.openFile(fileIndex);
		zip.readFile(buffer, bufferLen);
		zip.closeFile();
		buffer[bufferLen] = 0;

		return buffer;
	}

	typedef void (*ExternalJsonParser)(char* data, bool fromMod);

	static void loadLooseExternalJson(const char* jsonPath, const char* label, ExternalJsonParser parser)
	{
		if (!jsonPath || !jsonPath[0] || !parser) return;
		if (!FileUtil::exists(jsonPath)) return;

		FileStream file;
		if (!file.open(jsonPath, FileStream::MODE_READ))
		{
			TFE_System::logWrite(LOG_WARNING, "Mod",
				"skipping optional external data '%s': open failed path='%s'",
				label ? label : "", jsonPath);
			return;
		}

		const size_t size = file.getSize();
		if (size == 0)
		{
			file.close();
			TFE_System::logWrite(LOG_WARNING, "Mod",
				"skipping optional external data '%s': empty file path='%s'",
				label ? label : "", jsonPath);
			return;
		}

		char* data = (char*)malloc(size + 1);
		if (!data)
		{
			file.close();
			TFE_System::logWrite(LOG_ERROR, "Mod",
				"skipping optional external data '%s': alloc failed size=%u path='%s'",
				label ? label : "", (u32)size, jsonPath);
			return;
		}

		file.readBuffer(data, (u32)size);
		data[size] = 0;
		file.close();
		parser(data, true);
		free(data);
	}

	void loadCustomGob(const char* gobName)
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "Mod", "loadCustomGob begin name='%s'", gobName ? gobName : "");
		TFE_Paths::debugLogState("mod-loadCustomGob-before");
#endif
		FilePath archivePath;
		s32 lfdIndex[MAX_MOD_LFD];
		char lfdName[MAX_MOD_LFD][TFE_MAX_PATH];
		char briefingName[TFE_MAX_PATH];
		s32 lfdCount = 0;
		s32 briefingIndex = -1;

		if (!gobName || !gobName[0])
		{
			s_sharedState.customGobName[0] = 0;
			TFE_System::logWrite(LOG_ERROR, "Mod", "custom archive name is empty");
			return;
		}
		if (strlen(gobName) >= sizeof(s_sharedState.customGobName))
		{
			s_sharedState.customGobName[0] = 0;
			TFE_System::logWrite(LOG_ERROR, "Mod", "custom archive name too long '%s'", gobName);
			return;
		}
		strncpy(s_sharedState.customGobName, gobName, sizeof(s_sharedState.customGobName) - 1);
		s_sharedState.customGobName[sizeof(s_sharedState.customGobName) - 1] = 0;

		if (TFE_Paths::getFilePath(gobName, &archivePath))
		{
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "Mod", "custom archive resolved name='%s' path='%s' archive=%p index=%u",
				gobName, archivePath.path, archivePath.archive, archivePath.index);
#endif
			// Is this really a gob?
			const size_t len = strlen(gobName);
			if (len < 3)
			{
				TFE_System::logWrite(LOG_ERROR, "Mod", "custom archive name too short '%s'", gobName);
				return;
			}
			const char* ext = &gobName[len - 3];
			const char* ext4 = len >= 4 ? &gobName[len - 4] : "";
			if (strcasecmp(ext, "zip") == 0 || strcasecmp(ext, "pk3") == 0 || strcasecmp(ext4, "gobx") == 0)
			{
#ifdef _XBOX
				// Hardware rule: mods must be extracted to Mods\<modname>\ and
				// mounted by their loose GOB. The ZIP path keeps the compressed
				// container resident, may inflate a nested GOB into heap memory,
				// and extracts LFDs through Temp\. That was acceptable on desktop
				// and CXBX-R, but it fragments the 64 MB retail Xbox heap across
				// repeated mod swaps.
				TFE_System::logWrite(LOG_ERROR, "Mod",
					"zip/pk3/gobx mods are not mounted on Xbox; extract to Mods\\<modname>\\ name='%s'",
					gobName);
				return;
#else
				// In the case of a zip file, we want to extract the GOB into an in-memory format and use that directly.
				// Note that the archive will be deleted on exit, so we can safely allocate here and pass it along.
				ZipArchive* zipArchive = new ZipArchive();
				if (zipArchive->open(archivePath.path))
				{
					s32 gobIndex = -1;
					const u32 count = zipArchive->getFileCount();
#ifdef _XBOX
					TFE_System::logWrite(LOG_MSG, "Mod", "opened custom zip '%s' fileCount=%u", archivePath.path, count);
#endif
					for (u32 i = 0; i < count; i++)
					{
						const char* name = zipArchive->getFileName(i);
						if (!name) { continue; }
						const size_t nameLen = strlen(name);
						if (nameLen < 3) { continue; }
						const char* zext = &name[nameLen - 3];
						const char* zext4 = nameLen >= 4 ? &name[nameLen - 4] : "";
						if (strcasecmp(zext, "gob") == 0)
						{
							// Avoid MacOS references, they aren't real files.
							char gobFileName[TFE_MAX_PATH];
							FileUtil::getFileNameFromPath(name, gobFileName, true);
							if (gobFileName[0] != '.' || gobFileName[1] != '_')
							{
								gobIndex = i;
							}
						}
						else if (strcasecmp(zext, "lfd") == 0 && lfdCount < MAX_MOD_LFD)
						{
							if (strstr(name, "brief") || strstr(name, "BRIEF"))
							{
								briefingIndex = i;
							}
							else
							{
								lfdIndex[lfdCount++] = i;
							}
						}
						else if (strcasecmp(zext4, "json") == 0)
						{
							// Load external data overrides
							char fname[TFE_MAX_PATH];
							FileUtil::getFileNameFromPath(name, fname, true);

							if (strcasecmp(fname, "projectiles.json") == 0)
							{
								char* buffer = extractTextFileFromZip(*zipArchive, i);
								if (buffer) { TFE_ExternalData::parseExternalProjectiles(buffer, true); }
								free(buffer);
							}
							else if (strcasecmp(fname, "effects.json") == 0)
							{
								char* buffer = extractTextFileFromZip(*zipArchive, i);
								if (buffer) { TFE_ExternalData::parseExternalEffects(buffer, true); }
								free(buffer);
							}
							else if (strcasecmp(fname, "pickups.json") == 0)
							{
								char* buffer = extractTextFileFromZip(*zipArchive, i);
								if (buffer) { TFE_ExternalData::parseExternalPickups(buffer, true); }
								free(buffer);
							}
							else if (strcasecmp(fname, "weapons.json") == 0)
							{
								char* buffer = extractTextFileFromZip(*zipArchive, i);
								if (buffer) { TFE_ExternalData::parseExternalWeapons(buffer, true); }
								free(buffer);
							}
							else
							{
								char name2[TFE_MAX_PATH];
								strcpy(name2, name);
								const char* subdir = strtok(name2, "/");

								// If in logics subdirectory, attempt to load logics from JSON
#ifndef _XBOX
								if (strcasecmp(subdir, "logics") == 0)
								{
									char* buffer = extractTextFileFromZip(*zipArchive, i);
									TFE_ExternalData::ExternalLogics* logics = TFE_ExternalData::getExternalLogics();
									TFE_ExternalData::parseLogicData(buffer, name, logics->actorLogics);
									free(buffer);
								}
#endif
							}
						}

						else if (strcasecmp(zext, "txt") == 0)
						{
							char fname[TFE_MAX_PATH];
							FileUtil::getFileNameFromPath(name, fname, true);

							if (strcasecmp(fname, "tfemessages.txt") == 0)
							{
								char* buffer = extractTextFileFromZip(*zipArchive, i);
								int bufferLen = zipArchive->getFileLength(i);

								// Load Mod TFE Messages
								if (!buffer || !TFE_System::loadMessagesBuffer(buffer, bufferLen, true))
								{
									TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load mod TFE messages.");
								}

								free(buffer);
							}
						}
					}

					// If there is only 1 LFD, assume it is mission briefings.
					if (lfdCount == 1 && briefingIndex < 0)
					{
						briefingIndex = lfdIndex[0];
						lfdCount = 0;
					}

					if (gobIndex >= 0)
					{
						u32 bufferLen = (u32)zipArchive->getFileLength(gobIndex);
						u8* buffer = (u8*)malloc(bufferLen);
						if (buffer)
						{
							zipArchive->openFile(gobIndex);
							zipArchive->readFile(buffer, bufferLen);
							zipArchive->closeFile();

							GobMemoryArchive* gobArchive = new GobMemoryArchive();
							gobArchive->setName(zipArchive->getFileName(gobIndex));
							gobArchive->open(buffer, bufferLen);
							TFE_Paths::addLocalArchiveToFront(gobArchive);
							TFE_System::logWrite(LOG_MSG, "Mod", "mounted GOB from ZIP at archive front '%s'", zipArchive->getFileName(gobIndex));
						}
						else
						{
							TFE_System::logWrite(LOG_ERROR, "Mod", "failed to allocate ZIP GOB buffer bytes=%u", bufferLen);
						}
					}

					char tempPath[TFE_MAX_PATH];
					sprintf(tempPath, "%sTemp/", TFE_Paths::getPath(PATH_PROGRAM_DATA));
					FileUtil::makeDirectory(tempPath);
					// Extract and copy the briefing.
					if (briefingIndex >= 0)
					{
						u32 bufferLen = (u32)zipArchive->getFileLength(briefingIndex);
						u8* buffer = (u8*)malloc(bufferLen);
						if (buffer)
						{
							zipArchive->openFile(briefingIndex);
							zipArchive->readFile(buffer, bufferLen);
							zipArchive->closeFile();

							char lfdPath[TFE_MAX_PATH];
							sprintf(lfdPath, "%sdfbrief.lfd", tempPath);
							FileStream file;
							if (file.open(lfdPath, Stream::MODE_WRITE))
							{
								file.writeBuffer(buffer, bufferLen);
								file.close();
							}
							free(buffer);

							TFE_Paths::addSingleFilePath("dfbrief.lfd", lfdPath);
						}
					}
					// Extract and copy the LFD.
					for (s32 i = 0; i < lfdCount; i++)
					{
						u32 bufferLen = (u32)zipArchive->getFileLength(lfdIndex[i]);
						u8* buffer = (u8*)malloc(bufferLen);
						if (buffer)
						{
							zipArchive->openFile(lfdIndex[i]);
							zipArchive->readFile(buffer, bufferLen);
							zipArchive->closeFile();

							char lfdPath[TFE_MAX_PATH];
							sprintf(lfdPath, "%scutscenes%d.lfd", tempPath, i);
							FileStream file;
							if (file.open(lfdPath, Stream::MODE_WRITE))
							{
								file.writeBuffer(buffer, bufferLen);
								file.close();
							}
							free(buffer);

							TFE_Paths::addSingleFilePath(zipArchive->getFileName(lfdIndex[i]), lfdPath);
						}
					}

					// Add the ZIP archive itself only when it is the primary payload.
					// On Xbox ZipArchive keeps the compressed file resident; for the
					// common DF mod shape (ZIP containing a GOB plus optional LFDs),
					// keeping both the ZIP and extracted GOB mounted wastes several MB
					// and fragments memory across repeated mod swaps.
					if (gobIndex < 0)
					{
						TFE_Paths::addLocalArchive(zipArchive);
#ifdef _XBOX
						TFE_System::logWrite(LOG_MSG, "Mod", "mounted ZIP archive itself '%s'", archivePath.path);
#endif
					}
					else
					{
#ifdef _XBOX
						TFE_System::logWrite(LOG_MSG, "Mod", "released ZIP container after extracting GOB '%s'", archivePath.path);
#endif
						delete zipArchive;
					}
				}
				else
				{
					// Delete on read failure since the allocation is not added to TFE_Paths in this case.
#ifdef _XBOX
					TFE_System::logWrite(LOG_ERROR, "Mod", "failed to open custom zip '%s'", archivePath.path);
#endif
					delete zipArchive;
				}
#endif
			}
			else
			{
				Archive* archive = Archive::getArchive(ARCHIVE_GOB, gobName, archivePath.path);
				if (archive)
				{
					TFE_Paths::addLocalArchiveToFront(archive);
					TFE_System::logWrite(LOG_MSG, "Mod", "mounted GOB at archive front '%s'", gobName);

					char modPath[TFE_MAX_PATH];
					FileUtil::getFilePath(archivePath.path, modPath);

					// Add the Mod directory to head of search paths - so that assets here will be loaded preferentially
					TFE_Paths::addAbsoluteSearchPathToHead(modPath);

					// Handle LFD files.
					// Look for LFD files.
					lfdCount = 0;
					briefingIndex = -1;

					FileList fileList;
					FileUtil::readDirectory(modPath, "lfd", fileList);
					const size_t count = fileList.size();
					const std::string* file = count ? &fileList[0] : NULL;

					for (size_t i = 0; i < count; i++, file++)
					{
						const size_t len = file->length();
						const char* name = file->c_str();

						if (lfdCount < 16)
						{
							if (strstr(name, "brief") || strstr(name, "BRIEF"))
							{
								briefingIndex = s32(i);
								strncpy(briefingName, name, TFE_MAX_PATH - 1);
								briefingName[TFE_MAX_PATH - 1] = 0;
							}
							else if (lfdCount < MAX_MOD_LFD)
							{
								strncpy(lfdName[lfdCount], name, TFE_MAX_PATH - 1);
								lfdName[lfdCount][TFE_MAX_PATH - 1] = 0;
								lfdCount++;
							}
						}
					}

					// If there is only 1 LFD, assume it is mission briefings.
					if (lfdCount == 1 && briefingIndex < 0)
					{
						strncpy(briefingName, lfdName[0], TFE_MAX_PATH - 1);
						briefingName[TFE_MAX_PATH - 1] = 0;
						briefingIndex = 0;
						lfdCount = 0;
					}

					// Extract and copy the briefing.
					char lfdPath[TFE_MAX_PATH];
					if (briefingIndex >= 0)
					{
						snprintf(lfdPath, TFE_MAX_PATH, "%s%s", modPath, briefingName);
						TFE_Paths::addSingleFilePath("dfbrief.lfd", lfdPath);
					}

					// Extract and copy the LFD.
					for (s32 i = 0; i < lfdCount; i++)
					{
						snprintf(lfdPath, TFE_MAX_PATH, "%s%s", modPath, lfdName[i]);
						TFE_Paths::addSingleFilePath(lfdName[i], lfdPath);
					}

					// Load external data overrides
					char jsonPath[TFE_MAX_PATH];

					snprintf(jsonPath, TFE_MAX_PATH, "%s%s", modPath, "projectiles.json");
					loadLooseExternalJson(jsonPath, "projectiles.json", TFE_ExternalData::parseExternalProjectiles);

					snprintf(jsonPath, TFE_MAX_PATH, "%s%s", modPath, "effects.json");
					loadLooseExternalJson(jsonPath, "effects.json", TFE_ExternalData::parseExternalEffects);

					snprintf(jsonPath, TFE_MAX_PATH, "%s%s", modPath, "pickups.json");
					loadLooseExternalJson(jsonPath, "pickups.json", TFE_ExternalData::parseExternalPickups);

					snprintf(jsonPath, TFE_MAX_PATH, "%s%s", modPath, "weapons.json");
					loadLooseExternalJson(jsonPath, "weapons.json", TFE_ExternalData::parseExternalWeapons);
				}
			}
		}
#ifdef _XBOX
		else
		{
			TFE_System::logWrite(LOG_ERROR, "Mod", "custom archive not found name='%s'", gobName ? gobName : "");
		}
#endif

		TFE_Settings::loadCustomModSettings();
#ifdef _XBOX
		TFE_Paths::debugLogState("mod-loadCustomGob-after");
		TFE_System::logWrite(LOG_MSG, "Mod", "loadCustomGob end name='%s'", gobName ? gobName : "");
#endif
	}

	s32 loadLocalMessages()
	{
		if (s_runGameState.localMsgLoaded)
		{
			return 1;
		}

		FilePath path;
		TFE_Paths::getFilePath("local.msg", &path);
		return parseMessageFile(&s_sharedState.localMessages, &path, 0);
	}

	void buildSearchPaths()
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "buildSearchPaths begin");
		TFE_Paths::debugLogState("df-buildSearchPaths-before");
#endif
		TFE_Paths::addLocalSearchPath("");
		TFE_Paths::addLocalSearchPath("LFD/");
		// Dark Forces also adds C:/ and C:/LFD but TFE won't be doing that for obvious reasons...

		// Add some extra directories, if they exist.
		// Obviously these were not in the original code.
		TFE_Paths::addLocalSearchPath("Mods/");

		// Add Mods/ paths to the program data directory and local executable directory.
		// Note only directories that exist are actually added.
		const char* programData = TFE_Paths::getPath(PATH_PROGRAM_DATA);
		const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);
		char path[TFE_MAX_PATH];

		sprintf(path, "%sMods/", programData);
		TFE_Paths::addAbsoluteSearchPath(path);

		sprintf(path, "%s", "Mods/");
		if (!TFE_Paths::mapSystemPath(path))
			sprintf(path, "%sMods/", programDir);
		TFE_Paths::addAbsoluteSearchPath(path);

		// Add the adjustable HUD.
		sprintf(path, "%s", "Mods/TFE/AdjustableHud");
		if (!TFE_Paths::mapSystemPath(path))
			sprintf(path, "%sMods/TFE/AdjustableHud", programDir);
		TFE_Paths::addAbsoluteSearchPath(path);
#ifdef _XBOX
		TFE_Paths::debugLogState("df-buildSearchPaths-after");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "buildSearchPaths end");
#endif
	}

	bool openGobFiles()
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "openGobFiles begin");
		TFE_Paths::debugLogState("df-openGobFiles-before");
#endif
		for (s32 i = 0; i < TFE_ARRAYSIZE(c_gobFileNames); i++)
		{
			FilePath archivePath;
			if (TFE_Paths::getFilePath(c_gobFileNames[i], &archivePath))
			{
#ifdef _XBOX
				TFE_System::logWrite(LOG_MSG, "DarkForces", "required gob resolved '%s' path='%s'", c_gobFileNames[i], archivePath.path);
#endif
				assert(archivePath.path[0] != 0);
				Archive* archive = Archive::getArchive(ARCHIVE_GOB, c_gobFileNames[i], archivePath.path);
				if (archive)
				{
					TFE_Paths::addLocalArchive(archive);
				}
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "Dark Forces Main", "Cannot find required game data - '%s'.", c_gobFileNames[i]);
				return false;
			}
		}
		// Optional gobs
		for (s32 i = 0; i < TFE_ARRAYSIZE(c_optionalGobFileNames); i++)
		{
			FilePath archivePath;
			if (TFE_Paths::getFilePath(c_optionalGobFileNames[i], &archivePath))
			{
#ifdef _XBOX
				TFE_System::logWrite(LOG_MSG, "DarkForces", "optional gob resolved '%s' path='%s'", c_optionalGobFileNames[i], archivePath.path);
#endif
				assert(archivePath.path[0] != 0);
				Archive* archive = Archive::getArchive(ARCHIVE_GOB, c_optionalGobFileNames[i], archivePath.path);
				if (archive)
				{
					TFE_Paths::addLocalArchive(archive);
				}
			}
		}
#ifdef _XBOX
		TFE_Paths::debugLogState("df-openGobFiles-after");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "openGobFiles end");
#endif
		return true;
	}

	void loadMapNumFont()
	{
		FilePath filePath;
		s_sharedState.mapNumFont = nullptr;
		if (TFE_Paths::getFilePath("map-nums.fnt", &filePath))
		{
			s_sharedState.mapNumFont = font_load(&filePath);
		}
	}

	Font* getMapNumFont()
	{
		return s_sharedState.mapNumFont;
	}

	static void parseKey(GameMessages* msgs, u32 keyId, KeyboardCode* dest, KeyboardCode dflt)
	{
		GameMessage* m = getGameMessage(msgs, keyId);
		unsigned char c;

		*dest = dflt;
		if (m)
		{
			c = toupper(m->text[0]);
			if ((c >= 'A') && (c <= 'Z'))
			{
				*dest = (KeyboardCode)((u32)(KEY_A) + (c - 'A'));
			}
		}
	}

	static void loadLangHotkeys(void)
	{
		GameMessages msgs;
		FilePath fp;

		TFE_Paths::getFilePath("HOTKEYS.MSG", &fp);
		parseMessageFile(&msgs, &fp, 1);
		parseKey(&msgs, 160, &s_sharedState.langKeys.k_yes,   KEY_Y);
		parseKey(&msgs, 350, &s_sharedState.langKeys.k_quit,  KEY_Q);
		parseKey(&msgs, 330, &s_sharedState.langKeys.k_cont,  KEY_R);
		parseKey(&msgs, 340, &s_sharedState.langKeys.k_conf,  KEY_C);
		parseKey(&msgs, 110, &s_sharedState.langKeys.k_agdel, KEY_R);
		parseKey(&msgs, 130, &s_sharedState.langKeys.k_begin, KEY_B);
		parseKey(&msgs, 240, &s_sharedState.langKeys.k_easy,  KEY_E);
		parseKey(&msgs, 250, &s_sharedState.langKeys.k_med,   KEY_M);
		parseKey(&msgs, 260, &s_sharedState.langKeys.k_hard,  KEY_H);
		parseKey(&msgs, 230, &s_sharedState.langKeys.k_canc,  KEY_C);
	}

	void gameStartup()
	{
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup begin");
#endif
		hud_loadGraphics();
		hud_loadGameMessages();
		loadMapNumFont();
		inf_loadSounds();
		actor_loadSounds();
		actor_allocatePhysicsActorList();
		loadCutsceneList();
		loadLangHotkeys();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup core assets loaded");
#endif

		TFE_ExternalData::loadCustomLogics();

		TFE_ExternalData::loadExternalPickups();
		if (!TFE_ExternalData::validateExternalPickups())
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Warning: Pickup data is incomplete. PICKUPS.JSON may have been altered. Pickups may not behave as expected.");
		}

		TFE_ExternalData::loadExternalProjectiles();
		if (!TFE_ExternalData::validateExternalProjectiles())
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Warning: Projectile data is incomplete. PROJECTILES.JSON may have been altered. Projectiles may not behave as expected.");
		}

		TFE_ExternalData::loadExternalEffects();
		if (!TFE_ExternalData::validateExternalEffects())
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Warning: Effect data is incomplete. EFFECTS.JSON may have been altered. Effects may not behave as expected.");
		}

		TFE_ExternalData::loadExternalWeapons();
		if (!TFE_ExternalData::validateExternalWeapons())
		{
			TFE_System::logWrite(LOG_ERROR, "EXTERNAL_DATA", "Warning: Weapon data is incomplete. WEAPONS.JSON may have been altered. Weapons may not behave as expected.");
		}
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup external data loaded");
#endif

#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup projectile_startup begin");
#endif
		projectile_startup();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup projectile_startup end");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup hitEffect_startup begin");
#endif
		hitEffect_startup();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup hitEffect_startup end");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup weapon_startup begin");
#endif
		weapon_startup();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup weapon_startup end");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup item_loadData begin");
#endif
		item_loadData();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup item_loadData end");
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup player_init begin");
#endif
		player_init();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup player_init end");
#endif
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup gameplay systems loaded");
#endif

		FilePath filePath;
		TFE_Paths::getFilePath("swfont1.fnt", &filePath);
		s_sharedState.swFont1 = font_load(&filePath);

		renderer_setVisionEffect(0);
		renderer_setupCameraLight(JFALSE, JFALSE);

		s_defaultLoadScreen = bitmap_load("wait.bm", 1, POOL_GAME);  // allow load screen to be moddable; the default will remain wait.bm
		s_loadScreen = s_defaultLoadScreen;
		if (TFE_Paths::getFilePath("wait.pal", &filePath))
		{
			FileStream::readContents(&filePath, s_loadingScreenPal, 768);
		}

		weapon_enableAutomount(s_config.wpnAutoMount);
		s_sharedState.screenShotSndSrc = sound_load("scrshot.voc", SOUND_PRIORITY_HIGH0);
		sound_setBaseVolume(s_sharedState.screenShotSndSrc, 127);
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "DarkForces", "gameStartup end");
#endif
	}

	void loadAgentAndLevelData()
	{
		s32 agentCount = agent_loadData();
#ifdef _XBOX
		// Virtual agent defaults. Note that agent_loadData
		// returns the number of successful *reads*, not the number of valid
		// agents - a freshly-created DARKPILO.CFG (14 zeroed records) makes
		// it return 14 even though every slot is blank. Detect the blank-
		// slot-0 case (empty name AND zero nextMission, both impossible for
		// a real agent) and seed sane defaults so level_load gets a non-zero
		// difficulty (curDiff=1+difficulty filters enemies in level.cpp:902).
		if (s_agentData[0].name[0] == 0 || s_agentData[0].nextMission == 0)
		{
			memset(&s_agentData[0], 0, sizeof(AgentData));
			strcpy(s_agentData[0].name, "XBOX");
			s_agentData[0].difficulty       = 1;   // medium (curDiff=2 in level loader)
			s_agentData[0].nextMission      = 1;
			s_agentData[0].selectedMission  = 1;
			TFE_System::logWrite(LOG_MSG, "DarkForcesMain",
				"Xbox: virtual agent populated in-memory (agentCount from disk=%d, slot 0 was blank)",
				(int)agentCount);
		}
#endif
		if (!agent_loadLevelList("jedi.lvl"))
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "Failed to load level list.");
		}
		if (!parseBriefingList(&s_sharedState.briefingList, "briefing.lst"))
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "Failed to load briefing list.");
		}

		FilePath filePath;
		if (TFE_Paths::getFilePath("hotkeys.msg", &filePath))
		{
			parseMessageFile(&s_sharedState.hotKeyMessages, &filePath, 1);
		}
		s_sharedState.diskErrorImg = bitmap_load("diskerr.bm", 0, POOL_GAME);
		if (!s_sharedState.diskErrorImg)
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "Failed to load diskerr image.");
		}
	}

	void startMissionFromSave(s32 levelIndex)
	{
		// We have returned from the mission tasks.
		renderer_reset();
		gameMusic_stop();
		sound_levelStop();
		agent_levelEndTask();
		lmusic_reset();	// Fix a Dark Forces bug where music won't play when entering a cutscene again without restarting.
		pda_cleanup();
		reticle_enable(true);

		bitmap_clearLevelData();
		level_freeAllAssets();
		region_clear(s_levelRegion);

		// Next
		sound_levelStart();
		bitmap_setAllocator(s_levelRegion);
		actor_clearState();

		task_reset();
		inf_clearState();
		mission_setLoadingFromSave();	// This tells the mission system that this is loading from a save.
		s_sharedState.loadMissionTask = createTask("start mission", mission_startTaskFunc, JTRUE);
		mission_setLoadMissionTask(s_sharedState.loadMissionTask);
		gameMusic_start(levelIndex);

		s_runGameState.state = GSTATE_MISSION;
		mission_setupTasks();
	}

	bool serializeLoopState(Stream* stream, DarkForces* game)
	{
		if (s_sharedState.gameStarted)
		{
			SERIALIZE(SaveVersionInit, s_runGameState.argCount, 0);
			for (s32 i = 0; i < s_runGameState.argCount; i++)
			{
				SERIALIZE_CSTRING_GAME_ALLOC(SaveVersionInit, s_runGameState.args[i]);
			}

			SERIALIZE(SaveVersionInit, s_runGameState.cutscenesEnabled, JTRUE);
			SERIALIZE(SaveVersionInit, s_runGameState.localMsgLoaded, JFALSE);
			SERIALIZE(SaveVersionInit, s_runGameState.startLevel, 0);
			SERIALIZE(SaveVersionInit, s_runGameState.state, GSTATE_STARTUP_CUTSCENES);
			SERIALIZE(SaveVersionInit, s_runGameState.levelIndex, 0);
			SERIALIZE(SaveVersionInit, s_runGameState.cutsceneIndex, 0);
			SERIALIZE(SaveVersionInit, s_runGameState.abortLevel, 0);
		}
		else if (serialization_getMode() == SMODE_READ)  // We need to start the game.
		{
			if (!game || !game->runGame(0, nullptr, stream))
			{
#ifdef _XBOX
				TFE_System::logWrite(LOG_ERROR, "SaveSystem", "serializeLoopState failed to start game while loading save");
#endif
				return false;
			}
		}
		return true;
	}

	void serializeVersion(Stream* stream)
	{
		SERIALIZE_VERSION(SaveVersionCur);
	}

	bool DarkForces::serializeGameState(Stream* stream, const char* filename, bool writeState)
	{
		if (!stream) { return false; }
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState begin write=%d file='%s' loc=%u state=%d",
			writeState ? 1 : 0, filename ? filename : "", (u32)stream->getLoc(), s_runGameState.state);
#endif
		if (writeState && filename)
		{
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState hud message begin");
#endif
			// Write the save message.
			const char* msg = TFE_System::getMessage(TFE_MSG_SAVE);
			if (msg)
			{
				char fullMsg[TFE_MAX_PATH];
				sprintf(fullMsg, "%s [%s]", msg, filename);
				hud_sendTextMessage(fullMsg, 0);
			}
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState hud message end");
#endif
		}

#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState time_pause begin");
#endif
		time_pause(JTRUE);
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState time_pause end");
#endif
		if (writeState)
		{
			serialization_setMode(SMODE_WRITE);
		}
		else
		{
			serialization_setMode(SMODE_READ);
		}

#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState version begin loc=%u", (u32)stream->getLoc());
#endif
		serializeVersion(stream);
		const u32 curVersion = serialization_getVersion();
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState version end loc=%u version=%u", (u32)stream->getLoc(), curVersion);
#define XBOX_SAVE_STEP_BEGIN(name) TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState " name " begin loc=%u", (u32)stream->getLoc())
#define XBOX_SAVE_STEP_END(name) TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState " name " end loc=%u", (u32)stream->getLoc())
#else
#define XBOX_SAVE_STEP_BEGIN(name)
#define XBOX_SAVE_STEP_END(name)
#endif

		XBOX_SAVE_STEP_BEGIN("loop");
		if (!serializeLoopState(stream, this))
		{
			XBOX_SAVE_STEP_END("loop");
			time_pause(JFALSE);
#ifdef _XBOX
			TFE_System::logWrite(LOG_ERROR, "SaveSystem", "serializeGameState aborting after loop startup failure");
#endif
			return false;
		}
		XBOX_SAVE_STEP_END("loop");
		XBOX_SAVE_STEP_BEGIN("agent");
		agent_serialize(stream);
		XBOX_SAVE_STEP_END("agent");
		XBOX_SAVE_STEP_BEGIN("time");
		time_serialize(stream);
		XBOX_SAVE_STEP_END("time");
		if (!writeState)
		{
			XBOX_SAVE_STEP_BEGIN("startMissionFromSave");
			startMissionFromSave(agent_getLevelIndex());
			XBOX_SAVE_STEP_END("startMissionFromSave");
		}
		XBOX_SAVE_STEP_BEGIN("sound");
		sound_serializeLevelSounds(stream);
		XBOX_SAVE_STEP_END("sound");
		XBOX_SAVE_STEP_BEGIN("random");
		random_serialize(stream);
		XBOX_SAVE_STEP_END("random");
		XBOX_SAVE_STEP_BEGIN("automap");
		automap_serialize(stream);
		XBOX_SAVE_STEP_END("automap");
		XBOX_SAVE_STEP_BEGIN("hitEffect");
		hitEffect_serializeTasks(stream);
		XBOX_SAVE_STEP_END("hitEffect");
		XBOX_SAVE_STEP_BEGIN("weapon");
		weapon_serialize(stream);
		XBOX_SAVE_STEP_END("weapon");
		XBOX_SAVE_STEP_BEGIN("colorMap");
		mission_serializeColorMap(stream);
		XBOX_SAVE_STEP_END("colorMap");
		XBOX_SAVE_STEP_BEGIN("level");
		level_serialize(stream);
		XBOX_SAVE_STEP_END("level");
		XBOX_SAVE_STEP_BEGIN("inf");
		inf_serialize(stream);
		XBOX_SAVE_STEP_END("inf");
		XBOX_SAVE_STEP_BEGIN("pickup");
		pickupLogic_serializeTasks(stream);
		XBOX_SAVE_STEP_END("pickup");
		XBOX_SAVE_STEP_BEGIN("mission");
		mission_serialize(stream);
		XBOX_SAVE_STEP_END("mission");

		// TFE - Scripting.
		serialization_setVersion(curVersion);
		XBOX_SAVE_STEP_BEGIN("forceScript");
		TFE_ForceScript::serialize(stream);
		XBOX_SAVE_STEP_END("forceScript");
		if (!writeState)
		{
			// Setup the level script after script serialization and fixup ScriptCall function pointers.
			loadLevelScript();
			inf_fixupScriptCalls();
			logic_fixupScriptCalls();
		}

		XBOX_SAVE_STEP_BEGIN("messages");
		TFE_System::messages_serialize(stream);
		XBOX_SAVE_STEP_END("messages");

		if (!writeState)
		{
			agent_restartEndLevelTask();
		}

		time_pause(JFALSE);
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "serializeGameState end loc=%u", (u32)stream->getLoc());
#undef XBOX_SAVE_STEP_BEGIN
#undef XBOX_SAVE_STEP_END
#endif
		if (!writeState)
		{
			task_updateTime();
			mission_pause(JFALSE);
		}
		return true;
	}
}
