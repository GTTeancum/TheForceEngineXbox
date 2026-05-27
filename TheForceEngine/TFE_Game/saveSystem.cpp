#include "saveSystem.h"
#include <TFE_Asset/imageAsset.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_ExternalData/dfLogics.h>
#include <TFE_ExternalData/weaponExternal.h>
#include <TFE_ExternalData/pickupExternal.h>
#include <TFE_Settings/gameSourceData.h>
#include <TFE_System/system.h>
#include <cassert>
#include <cstring>

using namespace TFE_Input;

namespace TFE_SaveSystem
{
	static const bool s_verboseXboxSaveLog = false;
	enum SaveRequest
	{
		SF_REQ_NONE = 0,
		SF_REQ_SAVE,
		SF_REQ_LOAD,
	};

	enum SaveMasterVersion
	{
		SVER_INIT = 1,
		SVER_REPLAY = 7,
		SVER_CUR = SVER_REPLAY
	};

	const int TFE_MAX_SAVES = 1024; 

	static SaveRequest s_req = SF_REQ_NONE;
	static char s_reqFilename[TFE_MAX_PATH];
	static char s_reqSavename[TFE_MAX_PATH];
	static char s_gameSavePath[TFE_MAX_PATH];
	static IGame* s_game = nullptr;
	static s32 s_saveDelay = 0;
	static SaveHeader s_loadScratchHeader;

	static u32* s_imageBuffer[2] = { nullptr, nullptr };
	static size_t s_imageBufferSize[2] = { 0 };

#ifdef _XBOX
	static bool s_xboxSaveRootAttempted = false;
	static bool s_xboxSaveRootValid = false;
	static char s_xboxSaveRoot[TFE_MAX_PATH];

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

	static void xboxFatalSaveFailure(DWORD code)
	{
		assert(0 && "Xbox UDATA save failure");
		DebugBreak();

		volatile DWORD* crash = (volatile DWORD*)0;
		*crash = code ? code : 1;
		for (;;) {}
	}

	static bool xboxEnsureSaveRoot()
	{
		if (s_xboxSaveRootAttempted) return s_xboxSaveRootValid;
		s_xboxSaveRootAttempted = true;
		s_xboxSaveRootValid = false;
		s_xboxSaveRoot[0] = 0;

		WCHAR saveName[MAX_GAMENAME];
		xboxAsciiToWide("Dark Forces Saves", saveName, MAX_GAMENAME);

		char savePath[MAX_PATH];
		memset(savePath, 0, sizeof(savePath));
		DWORD result = XCreateSaveGame("U:\\", saveName, OPEN_ALWAYS, 0, savePath, MAX_PATH);
		if (result != ERROR_SUCCESS || !savePath[0])
		{
			TFE_System::logWrite(LOG_ERROR, "SaveSystem",
				"UDATA save root unavailable result=%lu path='%s'",
				result, savePath);
			xboxFatalSaveFailure(result);
			return false;
		}

		strncpy(s_xboxSaveRoot, savePath, TFE_MAX_PATH - 1);
		s_xboxSaveRoot[TFE_MAX_PATH - 1] = 0;
		xboxEnsureTrailingSlash(s_xboxSaveRoot);

		char srcImage[TFE_MAX_PATH];
		char dstImage[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_PROGRAM, "SaveImage.xbx", srcImage);
		snprintf(dstImage, TFE_MAX_PATH, "%sSaveImage.xbx", s_xboxSaveRoot);
		if (FileUtil::exists(srcImage))
		{
			FileUtil::copyFile(srcImage, dstImage);
		}

		s_xboxSaveRootValid = true;
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "UDATA save root ready '%s'", s_xboxSaveRoot);
		return true;
	}
#endif

	static u32 quickSaveHash(const char* text)
	{
		u32 hash = 2166136261u;
		if (!text) return hash;
		for (const char* p = text; *p; p++)
		{
			char c = *p;
			if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
			hash ^= (u8)c;
			hash *= 16777619u;
		}
		return hash ? hash : 1u;
	}

	void getQuickSaveFilenameForMod(const char* modName, char* filename, u32 size)
	{
		if (!filename || size == 0) return;
		filename[0] = 0;
		if (!modName || !modName[0])
		{
			strncpy(filename, c_quickSaveName, size - 1);
			filename[size - 1] = 0;
			return;
		}

		snprintf(filename, size, "quicksave_%08x.tfe", quickSaveHash(modName));
		filename[size - 1] = 0;
	}

	void getQuickSaveFilename(char* filename, u32 size)
	{
		char modList[256];
		modList[0] = 0;
		if (s_game)
		{
			s_game->getModList(modList);
		}
		getQuickSaveFilenameForMod(modList, filename, size);
	}

	bool versionValid(s32 version)
	{
		return version == SVER_CUR;
	}

	static void readHeaderString(Stream* stream, char* dst, size_t dstSize)
	{
		u8 len;
		stream->read(&len);
		const u32 rawLen = len;
		u32 readLen = dstSize > 0 ? (rawLen < (u32)dstSize - 1 ? rawLen : (u32)dstSize - 1) : 0;
		if (readLen)
		{
			stream->readBuffer(dst, readLen);
		}
		if (dstSize)
		{
			dst[readLen] = 0;
		}
		if (rawLen > readLen)
		{
			char scratch[64];
			u32 remaining = rawLen - readLen;
			while (remaining)
			{
				const u32 chunk = remaining < (u32)sizeof(scratch) ? remaining : (u32)sizeof(scratch);
				stream->readBuffer(scratch, chunk);
				remaining -= chunk;
			}
		}
	}

	static void consumeStreamBytes(Stream* stream, u32 byteCount)
	{
		char scratch[256];
		while (byteCount)
		{
			const u32 chunk = byteCount < (u32)sizeof(scratch) ? byteCount : (u32)sizeof(scratch);
			stream->readBuffer(scratch, chunk);
			byteCount -= chunk;
		}
	}

	void saveHeader(Stream* stream, const char* saveName)
	{
		// Generate a screenshot.
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);
		size_t size = displayInfo.width * displayInfo.height * 4;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveHeader begin saveName='%s' display=%ux%u captureBytes=%u",
			saveName ? saveName : "", (u32)displayInfo.width, (u32)displayInfo.height, (u32)size);
#endif
		if (size > s_imageBufferSize[0])
		{
			u32* newBuffer = (u32*)realloc(s_imageBuffer[0], size);
			if (newBuffer)
			{
				s_imageBuffer[0] = newBuffer;
				s_imageBufferSize[0] = size;
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "SaveSystem", "failed to allocate capture buffer (%u bytes)", (u32)size);
				s_imageBufferSize[0] = 0;
			}
		}
		if (s_imageBuffer[0])
		{
			TFE_RenderBackend::captureScreenToMemory(s_imageBuffer[0]);
		}
#ifdef _XBOX
		const u32 rawImageSize = SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT * sizeof(u32);
		if (rawImageSize > s_imageBufferSize[1])
		{
			u32* newBuffer = (u32*)realloc(s_imageBuffer[1], rawImageSize);
			if (newBuffer)
			{
				s_imageBuffer[1] = newBuffer;
				s_imageBufferSize[1] = rawImageSize;
			}
			else
			{
				TFE_System::logWrite(LOG_ERROR, "SaveSystem", "failed to allocate thumbnail buffer (%u bytes)", rawImageSize);
				s_imageBufferSize[1] = 0;
			}
		}
		if (s_imageBuffer[0] && s_imageBuffer[1])
		{
			for (u32 y = 0; y < SAVE_IMAGE_HEIGHT; y++)
			{
				const u32 sy = displayInfo.height ? (y * displayInfo.height) / SAVE_IMAGE_HEIGHT : 0;
				for (u32 x = 0; x < SAVE_IMAGE_WIDTH; x++)
				{
					const u32 sx = displayInfo.width ? (x * displayInfo.width) / SAVE_IMAGE_WIDTH : 0;
					s_imageBuffer[1][y * SAVE_IMAGE_WIDTH + x] =
						s_imageBuffer[0][sy * displayInfo.width + sx] | 0xff000000u;
				}
			}
		}
#else
		// Save to memory.
		u8* png = (u8*)malloc(SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT * 4);
		u32 pngSize = 0;
		if (png)
		{
			pngSize = (u32)TFE_Image::writeImageToMemory(png, displayInfo.width, displayInfo.height,
								 SAVE_IMAGE_WIDTH, SAVE_IMAGE_HEIGHT,
								 s_imageBuffer[0]);
		}
		else
		{
			pngSize = 0;
			png = (u8*)s_imageBuffer[0];
		}
#endif

		// Master version.
		u32 version = SVER_CUR;
		stream->write(&version);

		// Save Name.
		size_t saveNameLen = strlen(saveName);
		if (saveNameLen > SAVE_MAX_NAME_LEN - 1) { saveNameLen = SAVE_MAX_NAME_LEN - 1; }
		u8 len = (u8)saveNameLen;
		stream->write(&len);
		stream->writeBuffer(saveName, len);

		// Time and Date of Save.
		char timeDate[256];
		TFE_System::getDateTimeString(timeDate);
		len = (u8)strlen(timeDate);
		stream->write(&len);
		stream->writeBuffer(timeDate, len);

		// Level Name
		char levelName[256];
		s_game->getLevelName(levelName);
		len = (u8)strlen(levelName);
		stream->write(&len);
		stream->writeBuffer(levelName, len);

		//Level ID
		char levelId[256];
		s_game->getLevelId(levelId);
		len = (u8)strlen(levelId);
		stream->write(&len);
		stream->writeBuffer(levelId, len);

		// For Replays - Counter ID
		int counter = inputMapping_getCounter();		
		len = sizeof(counter);
		stream->write(&len);
		stream->writeBuffer(&counter, len);

		// Mod List
		char modList[256];
		s_game->getModList(modList);
		len = (u8)strlen(modList);
		stream->write(&len);
		stream->writeBuffer(modList, len);

		// Image.
#ifdef _XBOX
		u32 pngSize = s_imageBuffer[1] ? rawImageSize : 0;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem",
			"saveHeader fields save='%s' datetime='%s' levelName='%s' levelId='%s' modList='%s' thumbBytes=%u firstPixel=0x%08x",
			saveName ? saveName : "", timeDate, levelName, levelId, modList, pngSize,
			(s_imageBuffer[1] && pngSize) ? s_imageBuffer[1][0] : 0);
#endif
		stream->write(&pngSize);
		if (pngSize)
		{
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveHeader thumbnail write begin bytes=%u", pngSize);
#endif
			stream->writeBuffer(s_imageBuffer[1], pngSize);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveHeader thumbnail write end loc=%u", (u32)stream->getLoc());
#endif
		}
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveHeader end loc=%u", (u32)stream->getLoc());
#endif
#else
		stream->write(&pngSize);
		stream->writeBuffer(png, pngSize);
		free(png);
#endif
	}

	void loadHeaderInternal(Stream* stream, SaveHeader* header, const char* fileName, bool readImage)
	{
#ifdef _XBOX
		static const bool s_verboseSaveHeaderLog = false;
		if (s_verboseSaveHeaderLog)
		{
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "loadHeader begin file='%s' image=%d", fileName ? fileName : "", readImage ? 1 : 0);
		}
#endif
		memset(header, 0, sizeof(SaveHeader));
		if (fileName)
		{
			strncpy(header->fileName, fileName, sizeof(header->fileName) - 1);
			header->fileName[sizeof(header->fileName) - 1] = 0;
		}

		// Master version.
		u32 version;
		stream->read(&version);
		header->saveVersion = version;

		// Save Name.
		readHeaderString(stream, header->saveName, sizeof(header->saveName));
		// Fix existing invalid save names.
		header->saveName[SAVE_MAX_NAME_LEN - 1] = 0;

		// Handle the case when there is no save name.
		if (header->saveName[0] == 0 || header->saveName[0] == ' ')
		{
			FileUtil::getFileNameFromPath(fileName, header->saveName);
		}

		// Time and Date of Save.
		readHeaderString(stream, header->dateTime, sizeof(header->dateTime));

		// Level Name
		readHeaderString(stream, header->levelName, sizeof(header->levelName));

		if (version >= SVER_REPLAY)
		{
			// Level ID
			readHeaderString(stream, header->levelId, sizeof(header->levelId));

			// Counter
			u8 len;
			stream->read(&len);
			stream->readBuffer(&header->replayCounter, len);
		}

		// Mod List
		readHeaderString(stream, header->modNames, sizeof(header->modNames));

		// Image, re-use buffer 0 for the PNG.
		u32 pngSize;
		stream->read(&pngSize);
#ifdef _XBOX
		if (s_verboseSaveHeaderLog)
		{
			TFE_System::logWrite(LOG_MSG, "SaveSystem",
				"loadHeader fields file='%s' version=%u save='%s' datetime='%s' levelName='%s' levelId='%s' modList='%s' pngSize=%u",
				fileName ? fileName : "", version, header->saveName, header->dateTime,
				header->levelName, header->levelId, header->modNames, pngSize);
		}
		const u32 rawImageSize = SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT * sizeof(u32);
		if (!readImage)
		{
			memset(header->imageData, 0, rawImageSize);
			consumeStreamBytes(stream, pngSize);
			return;
		}
		if (pngSize == rawImageSize)
		{
			stream->readBuffer(header->imageData, pngSize);
			if (s_verboseSaveHeaderLog)
			{
				TFE_System::logWrite(LOG_MSG, "SaveSystem", "loaded raw save thumbnail '%s' bytes=%u firstPixel=0x%08x midPixel=0x%08x",
					fileName ? fileName : "", pngSize, header->imageData[0],
					header->imageData[(SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT) / 2]);
			}
		}
		else
		{
			memset(header->imageData, 0, rawImageSize);
			if (pngSize)
			{
				TFE_System::logWrite(LOG_WARNING, "SaveSystem", "save thumbnail '%s' is compressed/legacy bytes=%u; no Xbox decoder yet", fileName ? fileName : "", pngSize);
				if (pngSize > s_imageBufferSize[0])
				{
					u32* newBuffer = (u32*)realloc(s_imageBuffer[0], pngSize);
					if (newBuffer)
					{
						s_imageBuffer[0] = newBuffer;
						s_imageBufferSize[0] = pngSize;
					}
					else
					{
						TFE_System::logWrite(LOG_ERROR, "SaveSystem", "failed to allocate legacy thumbnail buffer '%s' bytes=%u",
							fileName ? fileName : "", pngSize);
						consumeStreamBytes(stream, pngSize);
						return;
					}
				}
				stream->readBuffer(s_imageBuffer[0], pngSize);
			}
		}
#else
		if (pngSize > s_imageBufferSize[0])
		{
			s_imageBuffer[0] = (u32*)realloc(s_imageBuffer[0], pngSize);
			s_imageBufferSize[0] = pngSize;
		}
		stream->readBuffer(s_imageBuffer[0], pngSize);

		SDL_Surface* image;
		TFE_Image::readImageFromMemory(&image, pngSize, s_imageBuffer[0]);
		if (image)
		{
			const u32 sz = SAVE_IMAGE_WIDTH * SAVE_IMAGE_HEIGHT * sizeof(u32);
			memcpy(header->imageData, image->pixels, sz);
			TFE_Image::free(image);
		}
#endif
	}

	void loadHeader(Stream* stream, SaveHeader* header, const char* fileName)
	{
		loadHeaderInternal(stream, header, fileName, true);
	}

	void populateSaveDirectory(std::vector<SaveHeader>& dir)
	{
		dir.clear();
		FileList fileList;
		FileUtil::readDirectory(s_gameSavePath, "tfe", fileList);
		size_t saveCount = fileList.size();
		dir.resize(saveCount);

		const std::string* filenames = &fileList[0];
		SaveHeader* headers = &dir[0];
		for (size_t i = 0; i < saveCount; i++)
		{
			loadGameHeader(filenames[i].c_str(), &headers[i]);
		}
	}

	void init()
	{
	}

	void destroy()
	{
		for (s32 i = 0; i < 2; i++)
		{
			free(s_imageBuffer[i]);
			s_imageBufferSize[i] = 0;
			s_imageBuffer[i] = nullptr;
		}
	}

	static bool saveGameInternal(const char* filename, const char* saveName, bool showMessage)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame begin file='%s' path='%s' saveName='%s' showMessage=%d game=%p",
			filename ? filename : "", filePath, saveName ? saveName : "", showMessage ? 1 : 0, s_game);
#endif
		if (stream.open(filePath, Stream::MODE_WRITE))
		{
			saveHeader(&stream, saveName);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame header complete loc=%u", (u32)stream.getLoc());
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame serialize begin");
#endif
			ret = s_game->serializeGameState(&stream, showMessage ? filename : NULL, true);
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame serialize end result=%d loc=%u", ret ? 1 : 0, (u32)stream.getLoc());
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame close begin");
#endif
			stream.close();
#ifdef _XBOX
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame close end");
#endif
		}
#ifdef _XBOX
		else
		{
			TFE_System::logWrite(LOG_ERROR, "SaveSystem", "saveGame open failed path='%s'", filePath);
		}
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "saveGame end file='%s' result=%d", filename ? filename : "", ret ? 1 : 0);
#endif
		return ret;
	}

	bool saveGame(const char* filename, const char* saveName)
	{
		return saveGameInternal(filename, saveName, true);
	}

	bool saveGameQuiet(const char* filename, const char* saveName)
	{
		return saveGameInternal(filename, saveName, false);
	}

	bool loadGame(const char* filename)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "loadGame begin file='%s' path='%s' game=%p",
			filename ? filename : "", filePath, s_game);
#endif
		if (stream.open(filePath, Stream::MODE_READ))
		{
			loadHeader(&stream, &s_loadScratchHeader, filename);
			
			// Clear out custom logics and external data before loading
			TFE_ExternalData::getExternalLogics()->actorLogics.clear();
			TFE_ExternalData::clearExternalWeapons();
			TFE_ExternalData::clearExternalProjectiles();
			TFE_ExternalData::clearExternalEffects();
			TFE_ExternalData::clearExternalPickups();

			ret = s_game->serializeGameState(&stream, filename, false);
			stream.close();
		}
#ifdef _XBOX
		else
		{
			TFE_System::logWrite(LOG_ERROR, "SaveSystem", "loadGame open failed path='%s'", filePath);
		}
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "loadGame end file='%s' result=%d", filename ? filename : "", ret ? 1 : 0);
#endif
		return ret;
	}

	bool loadGameHeader(const char* filename, SaveHeader* header)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
#ifdef _XBOX
		if (s_verboseXboxSaveLog)
		{
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "loadGameHeader begin file='%s' path='%s'", filename ? filename : "", filePath);
		}
#endif
		if (stream.open(filePath, Stream::MODE_READ))
		{
			loadHeader(&stream, header, filename);
			strcpy(header->fileName, filename);
			stream.close();
			ret = true;
		}
#ifdef _XBOX
		else
		{
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "loadGameHeader missing file='%s'", filename ? filename : "");
		}
		if (s_verboseXboxSaveLog)
		{
			TFE_System::logWrite(LOG_MSG, "SaveSystem", "loadGameHeader end file='%s' result=%d save='%s' level='%s' image=%p firstPixel=0x%08x",
				filename ? filename : "", ret ? 1 : 0, ret ? header->saveName : "", ret ? header->levelName : "",
				ret ? header->imageData : NULL, (ret && header->imageData) ? header->imageData[0] : 0);
		}
#endif
		return ret;
	}

#ifdef _XBOX
	bool loadGameHeaderLite(const char* filename, SaveHeader* header)
	{
		char filePath[TFE_MAX_PATH];
		sprintf(filePath, "%s%s", s_gameSavePath, filename);

		bool ret = false;
		FileStream stream;
		if (stream.open(filePath, Stream::MODE_READ))
		{
			loadHeaderInternal(&stream, header, filename, false);
			strcpy(header->fileName, filename);
			stream.close();
			ret = true;
		}
		return ret;
	}
#endif
		
	void postLoadRequest(const char* filename)
	{
		s_req = SF_REQ_LOAD;
		strcpy(s_reqFilename, filename);
	}

	void postSaveRequest(const char* filename, const char* saveName, s32 delay)
	{
		s_req = SF_REQ_SAVE;
		strcpy(s_reqFilename, filename);
		strcpy(s_reqSavename, saveName);
		s_saveDelay = delay;
	}

	const char* loadRequestFilename()
	{
		if (s_req == SF_REQ_LOAD)
		{
			s_req = SF_REQ_NONE;
			return s_reqFilename;
		}
		return nullptr;
	}

	const char* saveRequestFilename()
	{
		if (s_req == SF_REQ_SAVE && s_saveDelay <= 0)
		{
			s_req = SF_REQ_NONE;
			return s_reqFilename;
		}
		if (s_saveDelay > 0) { s_saveDelay--; }
		return nullptr;
	}

	void getSaveFilenameFromIndex(s32 index, char* name)
	{
		if (index == 0)
		{
			strcpy(name, c_quickSaveName);
		}
		else
		{
			sprintf(name, "save%03d.tfe", index - 1);
		}
	}

	void setCurrentGame(GameID id)
	{
#ifdef _XBOX
		if (!xboxEnsureSaveRoot())
		{
			s_gameSavePath[0] = 0;
			TFE_System::logWrite(LOG_ERROR, "SaveSystem", "setCurrentGame failed: UDATA unavailable id=%d", (s32)id);
			xboxFatalSaveFailure(1);
			return;
		}

		char relativeBasePath[TFE_MAX_PATH];
		snprintf(relativeBasePath, TFE_MAX_PATH, "%sSaves\\", s_xboxSaveRoot);
		if (!FileUtil::directoryExists(relativeBasePath))
		{
			FileUtil::makeDirectory(relativeBasePath);
		}

		snprintf(s_gameSavePath, TFE_MAX_PATH, "%sSaves\\%s\\", s_xboxSaveRoot, TFE_Settings::c_gameName[id]);
		if (!FileUtil::directoryExists(s_gameSavePath))
		{
			FileUtil::makeDirectory(s_gameSavePath);
		}
		TFE_System::logWrite(LOG_MSG, "SaveSystem", "setCurrentGame UDATA id=%d path='%s'", (s32)id, s_gameSavePath);
#else
		char relativeBasePath[TFE_MAX_PATH];
		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, "Saves/", relativeBasePath);
		if (!FileUtil::directoryExists(relativeBasePath))
		{
			FileUtil::makeDirectory(relativeBasePath);
		}

		char relativePath[TFE_MAX_PATH];
		sprintf(relativePath, "Saves/%s/", TFE_Settings::c_gameName[id]);

		TFE_Paths::appendPath(PATH_USER_DOCUMENTS, relativePath, s_gameSavePath);
		if (!FileUtil::directoryExists(s_gameSavePath))
		{
			FileUtil::makeDirectory(s_gameSavePath);
		}
#endif
	}
		
	void setCurrentGame(IGame* game)
	{
		s_game = game;
		setCurrentGame(game->id);
	}

	IGame* getCurrentGame()
	{
		return s_game;
	}

	void update()
	{
		if (!s_game) { return; }

		static s32 lastState = 0;
		const char* saveFilename = saveRequestFilename();

		bool canSave = !lastState && s_game->canSave();
		if (isReplaySystemLive())
		{
			// no saving or loading during replay system
			return;
		}
		else if (saveFilename && canSave)
		{
			saveGame(saveFilename, s_reqSavename);
			lastState = 1;
		}
		else if (inputMapping_getActionState(IAS_QUICK_SAVE) == STATE_PRESSED && canSave)
		{
			char quickSaveName[TFE_MAX_PATH];
			getQuickSaveFilename(quickSaveName, TFE_MAX_PATH);
			saveGame(quickSaveName, "Quicksave");
			lastState = 1;
		}
		else if (inputMapping_getActionState(IAS_QUICK_LOAD) == STATE_PRESSED && !lastState)
		{
			char quickSaveName[TFE_MAX_PATH];
			getQuickSaveFilename(quickSaveName, TFE_MAX_PATH);
			char filePath[TFE_MAX_PATH];
			sprintf(filePath, "%s%s", s_gameSavePath, quickSaveName);
			if (FileUtil::exists(filePath))
			{
				postLoadRequest(quickSaveName);
				lastState = 1;
			}
			else
			{
				TFE_DarkForces::hud_sendTextMessage("No Quicksave Found", 0, false);
				lastState = 0;
			}
		}
		else
		{
			lastState = 0;
		}
	}

	void getSaveFilename(char* filename, s32 index)
	{
		char saveFilePath[TFE_MAX_PATH];
		TFE_SaveSystem::getSaveFilenameFromIndex(index, filename);
		sprintf(saveFilePath, "%s%s", s_gameSavePath, filename);
		
		// If the file doesn't exist or we are overwriting, use the saveFilePath - ex: save015.tfe
		if (!FileUtil::exists(saveFilePath))
		{
			filename = saveFilePath;
			return;
		}
		else
		{
			// If the file already exists you must have deleted an older one so lets find the right index
			// Ex: save000.tfe save001.tfe save003.tfe (skipped 2) or you have custom save names. 
			for (int i = 0; i < TFE_MAX_SAVES; i++)
			{
				TFE_SaveSystem::getSaveFilenameFromIndex(i, filename);
				sprintf(saveFilePath, "%s%s", s_gameSavePath, filename);

				if (!FileUtil::exists(saveFilePath))
				{
					filename = saveFilePath;
					return;
				}
			}
		}

		TFE_System::logWrite(LOG_MSG, "SaveSystem", "Unable to create a save file after %d attempts", TFE_MAX_SAVES);
		assert(0);				
	}
}
