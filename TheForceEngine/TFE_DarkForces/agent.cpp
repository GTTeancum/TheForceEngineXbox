#include <cstring>

#include "agent.h"
#include "util.h"
#include "player.h"
#include "hud.h"
#include "weapon.h"
#include <TFE_Game/igame.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_System/parser.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <assert.h>

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Shared State
	///////////////////////////////////////////
	AgentData s_agentData[MAX_AGENT_COUNT];
	JBool s_levelComplete = JFALSE;
	JBool s_invalidLevelIndex;
	s32 s_maxLevelIndex = 0;
	s32 s_levelIndex = -1;	// This is actually s_levelIndex2 in the RE code.
	s32 s_agentId = 0;
	s32 s_headerSize = (s32)sizeof(PilotConfigHeader);
	char** s_levelDisplayNames;
	char** s_levelGamePaths;
	char** s_levelSrcPaths;
#ifdef _XBOX
	static char s_xboxCustomLevelName[32] = "";

	static void xboxAsciiToWide(const char* src, WCHAR* dst, size_t dstCount)
	{
		if (!dst || dstCount == 0) return;
		if (!src) src = "";
		size_t i = 0;
		for (; src[i] && i < dstCount - 1; i++)
		{
			dst[i] = (WCHAR)(u8)src[i];
		}
		dst[i] = 0;
	}

	static void xboxEnsureTrailingSlash(char* path)
	{
		if (!path || !path[0]) return;
		size_t len = strlen(path);
		if (len > 0 && path[len - 1] != '\\' && path[len - 1] != '/' && len < TFE_MAX_PATH - 1)
		{
			path[len] = '\\';
			path[len + 1] = 0;
		}
	}

	static void xboxFatalAgentSaveFailure(DWORD code)
	{
		assert(0 && "Xbox DARKPILO UDATA failure");
		DebugBreak();

		volatile DWORD* crash = (volatile DWORD*)0;
		*crash = code ? code : 1;
		for (;;) {}
	}
#endif

	static Task* s_levelEndTask = nullptr;

	void agent_writeSavedData(s32 agentId, LevelSaveData* saveData);
	void agent_readSavedData(s32 agentId, LevelSaveData* levelData);
		
	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void agent_checkNameLen(u8& nameLen)
	{
		if (nameLen >= 32)
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Agent load - name is too long: %d / 32", nameLen);
			assert(0);
			nameLen = 31;
		}
	}

	void agent_restartEndLevelTask()
	{
		// Add the level complete task.
		if (s_levelComplete)
		{
			agent_createLevelEndTask();
		}
	}

	void agent_serialize(Stream* stream)
	{
		bool write = serialization_getMode() == SMODE_WRITE;

		SERIALIZE(SaveVersionInit, s_levelComplete, JFALSE);
		SERIALIZE(SaveVersionInit, s_invalidLevelIndex, JFALSE);
		SERIALIZE(SaveVersionInit, s_maxLevelIndex, JFALSE);
		SERIALIZE(SaveVersionInit, s_levelIndex, JFALSE);
				
		// Agent.
		char name[64] = { 0 };
		u8 agentNameLen = 0;
		if (write)
		{
			strcpy(name, s_agentData[s_agentId].name);
			name[32] = 0;	// Just in case.

			agentNameLen = (u8)strlen(name);
			agent_checkNameLen(agentNameLen);
		}
		SERIALIZE(SaveVersionInit, agentNameLen, 0);
		if (!write)
		{
			agent_checkNameLen(agentNameLen);
		}
		SERIALIZE_BUF(SaveVersionInit, name, agentNameLen);
		name[agentNameLen] = 0;

		// Agent data.
		LevelSaveData data;
		if (write)
		{
			agent_readSavedData(s_agentId, &data);
		}
		{ LevelSaveData _def; memset(&_def, 0, sizeof(_def)); SERIALIZE(SaveVersionInit, data, _def); }

		// Find the agent.
		if (!write)
		{
			s_agentId = -1;
			// First get the count.
			s32 agentCount = 0;
			for (s32 i = 0; i < 14; i++)
			{
				if (!s_agentData[i].name[0]) { break; }
				agentCount++;
			}
			// Next find the agent, if it exists.
			for (s32 i = 0; i < agentCount; i++)
			{
				if (strcmp(s_agentData[i].name, name) == 0)
				{
					s_agentId = i;
					break;
				}
			}
			// If the agent doesn't exist, create it.
			if (s_agentId < 0)
			{
				if (agentCount < 14)
				{
					// Create a new agent.
					s_agentData[agentCount] = data.agentData;
					s_agentId = agentCount;
					agent_writeSavedData(s_agentId, &data);
				}
				else
				{
					TFE_System::logWrite(LOG_ERROR, "Agent", "Too many agents - cannot create a new agent from save.");
					assert(0);
					s_agentId = 0;
				}
			}
			else
			{
				// Check the agent and make sure it will work...
				if (s_agentData[s_agentId].nextMission < data.agentData.nextMission)
				{
					TFE_System::logWrite(LOG_WARNING, "Agent", "The agent in the save file has completed more levels than the local agent, updating.");
					s_agentData[s_agentId] = data.agentData;
					agent_writeSavedData(s_agentId, &data);
				}
			}
		}
	}

	s32 agent_loadData()
	{
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return 0;
		}

		s32 agentReadCount = 0;
		for (s32 i = 0; i < MAX_AGENT_COUNT; i++)
		{
			LevelSaveData saveData;
			if (agent_readConfigData(&file, i, &saveData))
			{
				memcpy(&s_agentData[i], &saveData.agentData, sizeof(AgentData));
				agentReadCount++;
			}
		}

		file.close();
		return agentReadCount;
	}

	void agent_levelComplete()
	{
		s_levelComplete = JTRUE;

		s32 curLevel = s_agentData[s_agentId].selectedMission + 1;
		if (curLevel > s_agentData[s_agentId].nextMission)
		{
			s_agentData[s_agentId].nextMission = curLevel;
		}
	}
		
	void levelEndTaskFunc(MessageType msg)
	{
		task_begin;

		// If you are using the auto end mission setting, then just wait a few seconds and end the level.
		if (TFE_Settings::getGameSettings()->df_autoEndMission)
		{
			task_yield(873); // ~ 6 seconds
			mission_setExitLevel(JTRUE);
		}
		else 
		{		
			// Otherwise loop the end level message until the player exits through the escape menu.
			while (1)
			{
				hud_sendTextMessage(461);
				task_yield(582);			// ~4 seconds
				hud_sendTextMessage(462);
				task_yield(4369);			// ~30 seconds		
			}	
		}
		task_end;
	}

	void agent_levelEndTask()
	{
		s_levelEndTask = nullptr;
	}

	void agent_createLevelEndTask()
	{
		if (!s_levelEndTask)
		{
			s_levelEndTask = createSubTask("LevelEnd", levelEndTaskFunc);
		}
	}

	void agent_saveLevelCompletion(u8 diff, s32 levelIndex)
	{
		s_agentData[s_agentId].completed[levelIndex - 1] = diff;
	}
	
	s32 agent_getLevelIndex()
	{
		return s_levelIndex;
	}

	s32 agent_getLevelIndexFromName(const char* name)
	{
		for (s32 i = 0; i < s_maxLevelIndex; i++)
		{
			if (!strcasecmp(s_levelGamePaths[i], name))
			{
				return i + 1;
			}
		}
#ifdef _XBOX
		if (s_xboxCustomLevelName[0] && name && !strcasecmp(s_xboxCustomLevelName, name))
		{
			return 1;
		}
#endif
		return 0;
	}
		
	const char* agent_getLevelName()
	{
#ifdef _XBOX
		if (s_xboxCustomLevelName[0]) { return s_xboxCustomLevelName; }
#endif
		if (!s_maxLevelIndex) { return nullptr; }
		const s32 index = clamp(s_levelIndex, 1, s_maxLevelIndex);
		return s_levelGamePaths[index - 1];
	}

	const char* agent_getLevelDisplayName()
	{
#ifdef _XBOX
		if (s_xboxCustomLevelName[0]) { return s_xboxCustomLevelName; }
#endif
		if (!s_maxLevelIndex) { return nullptr; }
		const s32 index = clamp(s_levelIndex, 1, s_maxLevelIndex);
		return s_levelDisplayNames[index - 1];
	}

#ifdef _XBOX
	void agent_setXboxCustomLevelName(const char* name)
	{
		if (!name)
		{
			s_xboxCustomLevelName[0] = 0;
			return;
		}
		strCopyAndZero(s_xboxCustomLevelName, name, sizeof(s_xboxCustomLevelName));
	}

	void agent_clearXboxCustomLevelName()
	{
		s_xboxCustomLevelName[0] = 0;
	}

	JBool agent_hasXboxCustomLevelName()
	{
		return s_xboxCustomLevelName[0] ? JTRUE : JFALSE;
	}
#endif

	void  agent_setLevelComplete(JBool complete)
	{
		s_levelComplete = complete;
	}

	JBool agent_getLevelComplete()
	{
		return s_levelComplete;
	}

	void agent_setNextLevelByIndex(s32 index)
	{
		if (index <= s_maxLevelIndex)
		{
			s_levelIndex = index;
			s_agentData[s_agentId].selectedMission = index;
		}
	}

	void agent_createNewAgent(s32 agentId, AgentData* data, const char* name)
	{
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return;
		}

		LevelSaveData saveData;
		memset(&saveData, 0, sizeof(LevelSaveData));
		memset(data, 0, sizeof(AgentData));

		saveData.inv[0]  = 0xff;			// bryar pistol
		saveData.inv[2]  = 0xff;			// s_itemUnknown1
		saveData.inv[6]  = 0xff;			// s_itemUnknown2
		saveData.inv[30] = WPN_PISTOL;		// current weapon
		saveData.inv[31] = 3;				// lives

		saveData.ammo[0] = min(s_ammoEnergyMax, 100);	// energy
		saveData.ammo[7] = 100;				// shields
		saveData.ammo[8] = 100;				// health
		saveData.ammo[9] = FIXED(2);		// battery

		strCopyAndZero(data->name, name, 32);
		data->difficulty = 1;
		data->nextMission = 1;
		data->selectedMission = 1;
		memcpy(&saveData.agentData, data, sizeof(AgentData));

		agent_writeAgentConfigData(&file, agentId, &saveData);
		file.close();
	}

	void agent_writeSavedData(s32 agentId, LevelSaveData* saveData)
	{
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return;
		}
		agent_writeAgentConfigData(&file, agentId, saveData);
		file.close();
	}

	void agent_updateAgentSavedData()
	{
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return;
		}

		for (s32 i = 0; i < MAX_AGENT_COUNT; i++)
		{
			LevelSaveData saveData;
			agent_readConfigData(&file, i, &saveData);

			// Copy Agent data into saved data.
			memcpy(&saveData, &s_agentData[i], sizeof(AgentData));
			agent_writeAgentConfigData(&file, i, &saveData);
		}

		file.close();
	}

	s32 agent_saveInventory(s32 agentId, s32 nextLevel)
	{
		if (nextLevel > 14) { return 0; }

		FileStream file;
		if (!openDarkPilotConfig(&file)) { return 0; }

		LevelSaveData saveData;
		if (!agent_readConfigData(&file, agentId, &saveData))
		{
			file.close();
			return 0;
		}

		s32 levelIndex = nextLevel - 1;
		s32* ammo = &saveData.ammo[levelIndex * 10];
		u8* inv = &saveData.inv[levelIndex * 32];
		player_writeInfo(inv, ammo);
		s32 written = agent_writeAgentConfigData(&file, agentId, &saveData);
		file.close();

		return written;
	}

	JBool agent_loadLevelList(const char* fileName)
	{
		FilePath filePath;
		if (!TFE_Paths::getFilePath(fileName, &filePath))
		{
			return JFALSE;
		}
		char* buffer = nullptr;
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			return JFALSE;
		}
		u32 len = (u32)file.getSize();
		buffer = (char*)game_alloc(len+1);
		file.readBuffer(buffer, len);
		file.close();
		buffer[len] = 0;

		TFE_Parser parser;
		parser.init(buffer, len);
		parser.addCommentString("#");

		size_t bufferPos = 0;
		const char* line = parser.readLine(bufferPos);
		s32 count;
		if (sscanf(line, "LEVELS %d", &count) < 1)
		{
			game_free(buffer);
			s_maxLevelIndex = 0;
			return JFALSE;
		}
		else
		{
			s_maxLevelIndex = min(count, MAX_LEVEL_COUNT);
			if (count)
			{
				s_levelDisplayNames = (char**)game_alloc(count * sizeof(char*));
				s_levelGamePaths    = (char**)game_alloc(count * sizeof(char*));
				s_levelSrcPaths     = (char**)game_alloc(count * sizeof(char*));
			}
		}

		for (s32 i = 0; i < count; i++)
		{
			line = parser.readLine(bufferPos);
			char* displayName = strtok((char*)line, ",");
			char* gamePath    = strtok(nullptr, ", \t\n\r");
			char* srcPath     = strtok(nullptr, ", \t\n\r");

			s_levelDisplayNames[i] = nullptr;
			s_levelGamePaths[i] = nullptr;
			s_levelSrcPaths[i] = nullptr;

			if (displayName && gamePath)
			{
				if (displayName[0])
				{
					s_levelDisplayNames[i] = copyAndAllocateString(displayName);
				}
				if (gamePath[0])
				{
					s_levelGamePaths[i] = copyAndAllocateString(gamePath);
				}
			}
		}

		game_free(buffer);
		return JTRUE;
	}

	JBool agent_readConfigData(FileStream* file, s32 agentId, LevelSaveData* saveData)
	{
		const s32 dataSize = (s32)sizeof(LevelSaveData);

		s32 offset = s_headerSize + agentId * dataSize;
		if (!file->seek(offset))
		{
			return JFALSE;
		}
		if (file->readBuffer(saveData, dataSize) != dataSize)
		{
			return JFALSE;
		}
		return JTRUE;
	}

	JBool agent_writeAgentConfigData(FileStream* file, s32 agentId, const LevelSaveData* saveData)
	{
		s32 fileOffset = agentId*sizeof(LevelSaveData) + s_headerSize;
		if (!file->seek(fileOffset))
		{
			return JFALSE;
		}
		file->writeBuffer(saveData, sizeof(LevelSaveData));
		return JTRUE;
	}

	void agent_readSavedData(s32 agentId, LevelSaveData* levelData)
	{
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return;
		}
		agent_readConfigData(&file, agentId, levelData);
		file.close();
	}

	void agent_readSavedDataForLevel(s32 agentId, s32 levelIndex)
	{
#ifdef _XBOX
		// Virtual agent: skip DARKPILO.CFG entirely. Going through
		// openDarkPilotConfig on Xbox is fragile (CreateFileA hangs on
		// missing FATX paths; even when the file exists the per-level
		// inv/ammo slot is zeroed for a fresh pilot, leaving the player
		// with no weapons). Hand player_readInfo the same default loadout
		// agent_createNewAgent uses for a brand-new pilot's level 1.
		// Constants mirror lines 287-296 of agent_createNewAgent above.
		(void)agentId; (void)levelIndex;   // unused on Xbox
		LevelSaveData defaults;
		memset(&defaults, 0, sizeof(LevelSaveData));
		defaults.inv[0]  = 0xff;            // bryar pistol
		defaults.inv[2]  = 0xff;            // s_itemUnknown1
		defaults.inv[6]  = 0xff;            // s_itemUnknown2
		defaults.inv[30] = WPN_PISTOL;      // current weapon
		defaults.inv[31] = 3;               // lives
		defaults.ammo[0] = min(s_ammoEnergyMax, 100);   // energy
		defaults.ammo[7] = 100;             // shields
		defaults.ammo[8] = 100;             // health
		defaults.ammo[9] = FIXED(2);        // battery
		player_readInfo(defaults.inv, defaults.ammo);
		TFE_System::logWrite(LOG_MSG, "Agent",
			"Xbox virtual agent: applied default level-1 loadout (bryar pistol, 100/100, 3 lives)");
#else
		FileStream file;
		if (!openDarkPilotConfig(&file))
		{
			TFE_System::logWrite(LOG_ERROR, "Agent", "Cannot open DarkPilo.cfg");
			return;
		}
		LevelSaveData levelData;
		agent_readConfigData(&file, agentId, &levelData);
		file.close();

		s32* ammo = &levelData.ammo[(levelIndex - 1) * 10];
		u8* inv = &levelData.inv[(levelIndex - 1) * 32];
		player_readInfo(inv, ammo);
#endif
	}

	// Creates a new Dark Pilot config file, which is used for saving.
	JBool createDarkPilotConfig(const char* path)
	{
		FileStream darkPilot;
		if (!darkPilot.open(path, Stream::MODE_WRITE))
		{
			return JFALSE;
		}

		PilotConfigHeader header =
		{
			{'P', 'C', 'F'},	// signature.
			0x12,				// version used by Dark Forces.
			14,					// maximum number of agents.
		};
		darkPilot.writeBuffer(&header, s_headerSize);
		LevelSaveData clearData = { 0 };
		for (s32 i = 0; i < MAX_AGENT_COUNT; i++)
		{
			darkPilot.writeBuffer(&clearData, sizeof(LevelSaveData));
		}
		darkPilot.close();
		return JTRUE;
	}
		
	// Xbox stores DARKPILO.CFG in the title's UDATA save container and fails
	// hard if that container cannot be created or opened.
	// Other platforms keep the original TFE migration path below.
	JBool openDarkPilotConfig(FileStream* file)
	{
#ifdef _XBOX
		assert(file);

		WCHAR saveName[MAX_GAMENAME];
		xboxAsciiToWide("Dark Forces Saves", saveName, MAX_GAMENAME);

		char saveRoot[MAX_PATH];
		memset(saveRoot, 0, sizeof(saveRoot));
		DWORD result = XCreateSaveGame("U:\\", saveName, OPEN_ALWAYS, 0, saveRoot, MAX_PATH);
		if (result != ERROR_SUCCESS || !saveRoot[0])
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain",
				"UDATA DARKPILO root unavailable result=%lu path='%s'",
				result, saveRoot);
			xboxFatalAgentSaveFailure(result);
			return JFALSE;
		}
		xboxEnsureTrailingSlash(saveRoot);

		char documentsPath[TFE_MAX_PATH];
		snprintf(documentsPath, TFE_MAX_PATH, "%sDARKPILO.CFG", saveRoot);
		if (!FileUtil::exists(documentsPath))
		{
			TFE_System::logWrite(LOG_MSG, "DarkForcesMain", "Creating UDATA DARKPILO.CFG at '%s'.", documentsPath);
			if (!createDarkPilotConfig(documentsPath))
			{
				TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "Cannot create UDATA DARKPILO.CFG at '%s'.", documentsPath);
				xboxFatalAgentSaveFailure(1);
				return JFALSE;
			}
		}

		if (!file->open(documentsPath, Stream::MODE_READWRITE))
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "cannot open UDATA DARKPILO.CFG at '%s'", documentsPath);
			xboxFatalAgentSaveFailure(1);
			return JFALSE;
		}

		PilotConfigHeader header;
		file->readBuffer(&header, sizeof(PilotConfigHeader));
		if (header.version == 0x12 && header.count == 14 && strncasecmp(header.signature, "PCF", 3) == 0)
		{
			return JTRUE;
		}

		file->close();
		TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "UDATA DARKPILO.CFG corrupted at '%s'.", documentsPath);
		xboxFatalAgentSaveFailure(1);
		return JFALSE;
#else
		bool triedonce = false;
		assert(file);

		// TFE uses its own local copy of the save game data to avoid corrupting existing data.
		// If this copy does not exist, then copy it.
		char documentsPath[TFE_MAX_PATH];
		char programDataPath[TFE_MAX_PATH];
		char sourcePath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, "DARKPILO.CFG", documentsPath);
		if (!FileUtil::exists(documentsPath))
		{
			// First check in /ProgramData since that is where the previous version stored it.
			TFE_Paths::appendPath(PATH_PROGRAM_DATA, "DARKPILO.CFG", programDataPath);
			if (FileUtil::exists(programDataPath))
			{
				TFE_System::logWrite(LOG_WARNING, "DarkForcesMain", "'DARKPILO.CFG' copied from '%s' to '%s', ProgramData/ will no longer be used and can be deleted.",
					programDataPath, documentsPath);
				FileUtil::copyFile(programDataPath, documentsPath);
				// Cleanup after TFE.
				FileUtil::deleteFile(programDataPath);
			}
			else
			{
				// Then try the source data path.
				TFE_Paths::appendPath(PATH_SOURCE_DATA, "DARKPILO.CFG", sourcePath);
				if (FileUtil::exists(sourcePath))
				{
					FileUtil::copyFile(sourcePath, documentsPath);
				}
				else
				{
					// Also check the remaster documents path.
					TFE_Paths::appendPath(PATH_REMASTER_DOCS, "DARKPILO.CFG", sourcePath);
					if (FileUtil::exists(sourcePath))
					{
						FileUtil::copyFile(sourcePath, documentsPath);
					}
					else
					{
						// Finally generate a new one.
						TFE_System::logWrite(LOG_WARNING, "DarkForcesMain", "Cannot find 'DARKPILO.CFG' at '%s'. Creating a new file for save data.", sourcePath);
					newpilo:
						createDarkPilotConfig(documentsPath);
					}
				}
			}
		}
		// Then try opening the file.
		if (!file->open(documentsPath, Stream::MODE_READWRITE))
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "cannot open DARKPILO.CFG");
			return JFALSE;
		}
		// Then verify the file.
		PilotConfigHeader header;
		file->readBuffer(&header, sizeof(PilotConfigHeader));
		if (header.version == 0x12 && header.count == 14 && strncasecmp(header.signature, "PCF", 3) == 0)
		{
			return JTRUE;
		}
		// If it is not correct, then close the file and return false.
		file->close();
		if (!triedonce)
		{
			TFE_System::logWrite(LOG_ERROR, "DarkForcesMain", "DARKPILO.CFG corrupted; creating new");
			triedonce = true;
			goto newpilo;
		}
		return JFALSE;
#endif
	}
}  // namespace TFE_DarkForces
