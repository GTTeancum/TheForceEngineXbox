// main_xbox.cpp
// Xbox XBE entry point for The Force Engine.
// Replaces main.cpp for the Xbox build configuration.
//
// Stripped vs PC main.cpp:
//   - No SDL
//   - No command-line parsing (no args on Xbox)
//   - No TFE_FrontEndUI / ImGui (game draws its own UI into framebuffer)
//   - No TFE_Editor
//   - No TFE_ForceScript (commented out)
//   - No TFE_A11Y
//   - No screenshot / GIF recording
//   - No mod override STL path
//   - Game source data lives in DARK\ relative to XBE
//   - Goes straight to APP_STATE_GAME - no front-end menu
//
// Things kept:
//   - Full settings init/shutdown (saves/restores config from disk)
//   - TFE_Audio (PCM, OGG via STB Vorbis - see audio_xbox.cpp)
//   - inputMapping, TFE_Input state machine
//   - TFE_InputXbox XInput polling
//   - TFE_SaveSystem
//   - All JEDI game code paths

#include "version.h"
#include <TFE_System/types.h>
#include <TFE_System/system.h>
#include <TFE_System/frameLimiter.h>
#include <TFE_System/tfeMessage.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Archive/archive.h>
#include <TFE_Archive/gobArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_Game/reticle.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/audioDevice.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/renderBackend_xbox.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Input/replay.h>
#include <TFE_Settings/settings.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_RenderShared/texturePacker.h>
#include <TFE_Asset/paletteAsset.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_DarkForces/hud.h>
#include <TFE_DarkForces/mission.h>

// AppState is defined in frontEndUi.h which pulls in STL and ImGui.
// Redeclare the enum directly here for Xbox to avoid those dependencies.
// Keep in sync with TFE_FrontEndUI/frontEndUi.h.
enum AppState
{
    APP_STATE_MENU = 0,
    APP_STATE_EDITOR,
    APP_STATE_LOAD,
    APP_STATE_MODS,
    APP_STATE_OPTIONS,
    APP_STATE_GAME,
    APP_STATE_QUIT,
    APP_STATE_NO_GAME_DATA,
    APP_STATE_CANNOT_RUN,
    APP_STATE_EXIT_TO_MENU,
    APP_STATE_SET_DEFAULTS,
    APP_STATE_COUNT,
    APP_STATE_UNINIT = APP_STATE_COUNT
};

// Xbox-specific
#include <TFE_Input/input_xbox.h>

#include <xtl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

using namespace TFE_Input;

// ---------------------------------------------------------------------------
// Version string (from gitVersion.h / version.h)
// ---------------------------------------------------------------------------
#ifndef c_gitVersion
#define c_gitVersion "Xbox"
#endif

#define PROGRAM_ERROR   1
#define PROGRAM_SUCCESS 0

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool     s_loop      = true;
static IGame*   s_curGame   = NULL;
static AppState s_curState  = APP_STATE_UNINIT;
static bool     s_soundPaused = false;
static s32      s_startMenuSelection = 0;
static u32      s_startMenuFrame = 0;
static bool     s_startStickUpHeld = false;
static bool     s_startStickDownHeld = false;
static s32      s_loadMenuSelection = 0;
static u32      s_loadMenuFrame = 0;
static bool     s_loadStickUpHeld = false;
static bool     s_loadStickDownHeld = false;
static s32      s_modMenuSelection = 0;
static u32      s_modMenuFrame = 0;
static bool     s_modStickUpHeld = false;
static bool     s_modStickDownHeld = false;
static s32      s_optionsSelection = 0;
static s32      s_optionsScroll = 0;
static u32      s_optionsFrame = 0;
static bool     s_optionsStickUpHeld = false;
static bool     s_optionsStickDownHeld = false;
static bool     s_optionsStickLeftHeld = false;
static bool     s_optionsStickRightHeld = false;
static TFE_RenderBackend::XboxOptionsItem s_optionsItems[7];
static TFE_SaveSystem::SaveHeader s_loadHeaders[6];
static TFE_RenderBackend::XboxLoadSlotInfo s_loadSlots[6];
static char s_loadDateDisplay[6][32];
struct XboxModEntry
{
    TFE_RenderBackend::XboxModInfo ui;
    char path[TFE_MAX_PATH];
    char archiveName[96];
    char title[64];
    char author[64];
    char version[32];
    char description[256];
    char levelName[32];
    char quickSaveName[TFE_MAX_PATH];
    bool hasQuickSave;
};
static XboxModEntry s_modEntries[12];
static TFE_RenderBackend::XboxModInfo s_modUi[12];
static s32 s_modCount = 0;
static bool s_returnToStartRequested = false;
static bool s_menuMusicReady = false;
static bool s_menuMusicPlaying = false;
static Archive* s_menuMusicArchive = NULL;

extern "C" void TFE_XboxReturnToStartMenu()
{
    s_returnToStartRequested = true;
    TFE_System::logWrite(LOG_MSG, "Main", "Return to start menu requested.");
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

// On Xbox the source data lives in <root>\DARK\ (mirrors original DF install).
static void setupSourceDataPath()
{
    char darkPath[TFE_MAX_PATH];
    snprintf(darkPath, TFE_MAX_PATH, "%sDARK\\", TFE_Paths::getPath(PATH_PROGRAM));
    TFE_Paths::setPath(PATH_SOURCE_DATA, darkPath);
    TFE_System::logWrite(LOG_MSG, "Paths", "Source Data: \"%s\"", darkPath);
}

static bool validatePath()
{
    if (!TFE_Paths::hasPath(PATH_SOURCE_DATA)) return false;

    char testFile[TFE_MAX_PATH];
    snprintf(testFile, TFE_MAX_PATH, "%sDARK.GOB", TFE_Paths::getPath(PATH_SOURCE_DATA));
    if (!FileUtil::exists(testFile))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "DARK.GOB not found at '%s'", testFile);
        return false;
    }
    if (!GobArchive::validate(testFile, 130))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "DARK.GOB is invalid");
        return false;
    }
    return true;
}

static void stopMenuMusic()
{
    if (!s_menuMusicReady)
    {
        return;
    }

    TFE_System::logWrite(LOG_MSG, "StartMenu", "stopping menu music");
    TFE_Jedi::ImStopAllSounds();
    TFE_Jedi::ImUnloadAll();
    TFE_Jedi::ImTerminate();

    if (s_menuMusicArchive)
    {
        TFE_Paths::removeLastArchive();
        Archive::freeArchive(s_menuMusicArchive);
        s_menuMusicArchive = NULL;
    }

    s_menuMusicReady = false;
    s_menuMusicPlaying = false;
}

static void startMenuMusic()
{
    if (s_menuMusicPlaying)
    {
        return;
    }

    char gobPath[TFE_MAX_PATH];
    snprintf(gobPath, TFE_MAX_PATH, "%sSOUNDS.GOB", TFE_Paths::getPath(PATH_SOURCE_DATA));
    s_menuMusicArchive = Archive::getArchive(ARCHIVE_GOB, "SOUNDS.GOB", gobPath);
    if (!s_menuMusicArchive)
    {
        TFE_System::logWrite(LOG_ERROR, "StartMenu", "could not open SOUNDS.GOB for menu music");
        return;
    }
    TFE_Paths::addLocalArchive(s_menuMusicArchive);

    if (TFE_Jedi::ImInitialize(s_gameRegion) != imSuccess)
    {
        TFE_System::logWrite(LOG_ERROR, "StartMenu", "iMuse init failed for menu music");
        TFE_Paths::removeLastArchive();
        Archive::freeArchive(s_menuMusicArchive);
        s_menuMusicArchive = NULL;
        return;
    }
    s_menuMusicReady = true;

    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    TFE_MidiPlayer::setVolume(sound->musicVolume * sound->masterVolume);

    ImSoundId song = TFE_Jedi::ImLoadMidi("crixmus");
    if (!song)
    {
        song = TFE_Jedi::ImLoadMidi("CRIXMUS");
    }
    if (!song || TFE_Jedi::ImStartSound(song, 64) != imSuccess)
    {
        TFE_System::logWrite(LOG_ERROR, "StartMenu", "failed to start CRIXMUS menu music");
        stopMenuMusic();
        return;
    }

    s_menuMusicPlaying = true;
    TFE_System::logWrite(LOG_MSG, "StartMenu", "playing CRIXMUS");
}

// ---------------------------------------------------------------------------
// Game lifecycle
// ---------------------------------------------------------------------------
static void startGame(int argc, const char** argv)
{
    stopMenuMusic();
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);

    if (s_curGame)
    {
        freeGame(s_curGame);
        s_curGame = NULL;
    }

    TFE_Game* gameInfo = TFE_Settings::getGame();
    s_curGame = createGame(gameInfo->id);
    TFE_SaveSystem::setCurrentGame(s_curGame);

    if (!s_curGame)
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot create game.");
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }

    if (!s_curGame->runGame(argc, argv, NULL))
    {
        TFE_System::logWrite(LOG_ERROR, "Main", "Cannot run game.");
        freeGame(s_curGame);
        s_curGame = NULL;
        s_curState = APP_STATE_CANNOT_RUN;
        return;
    }

    TFE_Input::enableRelativeMode(true);
    s_curState = APP_STATE_GAME;
    TFE_System::logWrite(LOG_MSG, "Main", "Game started.");
}

static bool loadGameFromMenu(const char* filename)
{
    if (!filename || !filename[0]) return false;
    stopMenuMusic();

    if (s_curGame)
    {
        freeGame(s_curGame);
        s_curGame = NULL;
    }

    TFE_Game* gameInfo = TFE_Settings::getGame();
    s_curGame = createGame(gameInfo->id);
    if (!s_curGame)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "Cannot create game for load.");
        s_curState = APP_STATE_CANNOT_RUN;
        return false;
    }
    TFE_SaveSystem::setCurrentGame(s_curGame);

    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    const bool loaded = TFE_SaveSystem::loadGame(filename);
    if (!loaded)
    {
        TFE_System::logWrite(LOG_ERROR, "LoadMenu", "load failed '%s'", filename);
        freeGame(s_curGame);
        s_curGame = NULL;
        s_curState = APP_STATE_MENU;
        TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
        startMenuMusic();
        return false;
    }

    TFE_Input::enableRelativeMode(true);
    s_curState = APP_STATE_GAME;
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "loaded '%s'", filename);
    return true;
}

static void refreshLoadSlots()
{
    memset(s_loadHeaders, 0, sizeof(s_loadHeaders));
    memset(s_loadSlots, 0, sizeof(s_loadSlots));
    memset(s_loadDateDisplay, 0, sizeof(s_loadDateDisplay));
    for (s32 i = 0; i < 6; i++)
    {
        char filename[TFE_MAX_PATH];
        if (i == 0)
            strcpy(filename, TFE_SaveSystem::c_quickSaveName);
        else
            sprintf(filename, "save%03d.tfe", i - 1);

        const bool valid = TFE_SaveSystem::loadGameHeader(filename, &s_loadHeaders[i]);
        if (valid)
        {
            char dow[8], mon[8];
            int day = 0, hour = 0, minute = 0, second = 0, year = 0;
            if (sscanf(s_loadHeaders[i].dateTime, "%7s %7s %d %d:%d:%d %d", dow, mon, &day, &hour, &minute, &second, &year) == 7)
            {
                sprintf(s_loadDateDisplay[i], "%s %02d, %04d %02d:%02d", mon, day, year, hour, minute);
            }
            else
            {
                strncpy(s_loadDateDisplay[i], s_loadHeaders[i].dateTime, 31);
                s_loadDateDisplay[i][31] = 0;
            }
        }
        s_loadSlots[i].valid = valid;
        s_loadSlots[i].autosave = (i == 0);
        s_loadSlots[i].fileName = valid ? s_loadHeaders[i].fileName : filename;
        s_loadSlots[i].saveName = valid ? s_loadHeaders[i].saveName : "";
        s_loadSlots[i].dateTime = valid ? s_loadDateDisplay[i] : "";
        s_loadSlots[i].levelName = valid ? s_loadHeaders[i].levelName : "";
        s_loadSlots[i].levelId = valid ? s_loadHeaders[i].levelId : "";
        s_loadSlots[i].imageData = valid ? s_loadHeaders[i].imageData : NULL;
    }
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "slots refreshed");
}

static void copyString(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = 0;
}

static char* trimText(char* text)
{
    if (!text) return text;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
    return text;
}

static void assignModDefaults(XboxModEntry* mod, const char* folderPath, const char* archiveName)
{
    memset(mod, 0, sizeof(XboxModEntry));
    mod->ui.valid = true;
    copyString(mod->path, TFE_MAX_PATH, folderPath);
    copyString(mod->archiveName, sizeof(mod->archiveName), archiveName);
    char title[96];
    FileUtil::getFileNameFromPath(archiveName && archiveName[0] ? archiveName : folderPath, title, false);
    if (!title[0]) strcpy(title, "Installed Mod");
    copyString(mod->title, sizeof(mod->title), title);
    copyString(mod->author, sizeof(mod->author), "-");
    copyString(mod->version, sizeof(mod->version), "-");
    copyString(mod->description, sizeof(mod->description), "No description provided.");
    mod->levelName[0] = 0;
    TFE_SaveSystem::getQuickSaveFilenameForMod(mod->archiveName, mod->quickSaveName, TFE_MAX_PATH);
    mod->hasQuickSave = TFE_SaveSystem::loadGameHeader(mod->quickSaveName, &s_loadHeaders[0]);
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    mod->ui.missionCount = archiveName && archiveName[0] ? 1 : 0;
    mod->ui.hasQuickSave = mod->hasQuickSave;
}

static void parseModManifestLine(XboxModEntry* mod, char* line)
{
    char* sep = strchr(line, '=');
    if (!sep) sep = strchr(line, ':');
    if (!sep) return;
    *sep = 0;
    char* key = trimText(line);
    char* value = trimText(sep + 1);
    if (!key[0] || !value[0]) return;

    if (!strcasecmp(key, "title") || !strcasecmp(key, "name"))
        copyString(mod->title, sizeof(mod->title), value);
    else if (!strcasecmp(key, "author"))
        copyString(mod->author, sizeof(mod->author), value);
    else if (!strcasecmp(key, "version"))
        copyString(mod->version, sizeof(mod->version), value);
    else if (!strcasecmp(key, "description") || !strcasecmp(key, "desc"))
        copyString(mod->description, sizeof(mod->description), value);
    else if (!strcasecmp(key, "missions"))
        mod->ui.missionCount = atoi(value);
    else if (!strcasecmp(key, "level") || !strcasecmp(key, "startlevel"))
        copyString(mod->levelName, sizeof(mod->levelName), value);
}

static void parseModManifestBuffer(XboxModEntry* mod, char* buffer)
{
    char* line = buffer;
    while (line && *line)
    {
        char* next = strchr(line, '\n');
        if (next) *next++ = 0;
        parseModManifestLine(mod, line);
        line = next;
    }

    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    mod->ui.hasQuickSave = mod->hasQuickSave;
}

static bool readModManifestFile(XboxModEntry* mod, const char* filename)
{
    FileStream file;
    if (!file.open(filename, Stream::MODE_READ)) return false;

    size_t size = file.getSize();
    if (size > 2047) size = 2047;
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    file.readBuffer(buffer, 1, (u32)size);
    file.close();
    parseModManifestBuffer(mod, buffer);
    return true;
}

static bool readModManifestFromZip(XboxModEntry* mod)
{
    const size_t archiveLen = strlen(mod->archiveName);
    if (archiveLen < 4) return false;
    const char* ext3 = archiveLen >= 3 ? &mod->archiveName[archiveLen - 3] : "";
    if (strcasecmp(ext3, "zip") != 0)
    {
        return false;
    }

    char archivePath[TFE_MAX_PATH];
    snprintf(archivePath, TFE_MAX_PATH, "%s%s", mod->path, mod->archiveName);
    ZipArchive zip;
    if (!zip.open(archivePath)) return false;

    const u32 count = zip.getFileCount();
    for (u32 i = 0; i < count; i++)
    {
        char fileName[TFE_MAX_PATH];
        FileUtil::getFileNameFromPath(zip.getFileName(i), fileName, true);
        if (strcasecmp(fileName, "metadata.txt") == 0 ||
            strcasecmp(fileName, "mod.txt") == 0 ||
            strcasecmp(fileName, "manifest.txt") == 0 ||
            strcasecmp(fileName, "mod.ini") == 0)
        {
            u32 bufferLen = (u32)zip.getFileLength(i);
            if (bufferLen > 2047) bufferLen = 2047;
            char buffer[2048];
            memset(buffer, 0, sizeof(buffer));
            zip.openFile(i);
            zip.readFile(buffer, bufferLen);
            zip.closeFile();
            parseModManifestBuffer(mod, buffer);
            zip.close();
            return true;
        }
    }
    zip.close();
    return false;
}

static bool readFirstLevelFromGobBuffer(const u8* data, u32 size, char* outLevel, size_t outSize)
{
    if (!data || !size || !outLevel || outSize == 0) return false;

    GobMemoryArchive gob;
    if (!gob.open(data, size)) return false;

    const u32 count = gob.getFileCount();
    for (u32 i = 0; i < count; i++)
    {
        const char* name = gob.getFileName(i);
        if (!name) continue;
        const size_t len = strlen(name);
        if (len < 5) continue;
        if (strcasecmp(&name[len - 3], "lev") == 0)
        {
            char levelName[32];
            FileUtil::getFileNameFromPath(name, levelName, false);
            copyString(outLevel, outSize, levelName);
            gob.close();
            return outLevel[0] != 0;
        }
    }

    gob.close();
    return false;
}

static bool readFirstLevelFromZip(XboxModEntry* mod)
{
    if (mod->levelName[0]) return true;

    char archivePath[TFE_MAX_PATH];
    snprintf(archivePath, TFE_MAX_PATH, "%s%s", mod->path, mod->archiveName);
    ZipArchive zip;
    if (!zip.open(archivePath)) return false;

    const u32 count = zip.getFileCount();
    for (u32 i = 0; i < count; i++)
    {
        const char* name = zip.getFileName(i);
        if (!name) continue;
        const size_t len = strlen(name);
        if (len < 5 || strcasecmp(&name[len - 3], "gob") != 0) continue;

        char gobFileName[TFE_MAX_PATH];
        FileUtil::getFileNameFromPath(name, gobFileName, true);
        if (gobFileName[0] == '.' && gobFileName[1] == '_') continue;

        u32 bufferLen = (u32)zip.getFileLength(i);
        u8* buffer = (u8*)malloc(bufferLen);
        if (!buffer)
        {
            zip.close();
            return false;
        }

        zip.openFile(i);
        zip.readFile(buffer, bufferLen);
        zip.closeFile();
        const bool found = readFirstLevelFromGobBuffer(buffer, bufferLen, mod->levelName, sizeof(mod->levelName));
        zip.close();
        TFE_System::logWrite(found ? LOG_MSG : LOG_WARNING, "ModMenu", "zip level scan archive='%s' level='%s'", mod->archiveName, mod->levelName);
        return found;
    }

    zip.close();
    return false;
}

static void readModManifest(XboxModEntry* mod)
{
    char baseName[96];
    FileUtil::getFileNameFromPath(mod->archiveName, baseName, false);

    bool metadataLoaded = false;
    char filename[TFE_MAX_PATH];
    if (baseName[0])
    {
        snprintf(filename, TFE_MAX_PATH, "%s%s_metadata.txt", mod->path, baseName);
        metadataLoaded = readModManifestFile(mod, filename);
    }

    static const char* names[] = { "metadata.txt", "mod.txt", "manifest.txt", "mod.ini" };
    for (s32 i = 0; !metadataLoaded && i < 4; i++)
    {
        snprintf(filename, TFE_MAX_PATH, "%s%s", mod->path, names[i]);
        metadataLoaded = readModManifestFile(mod, filename);
    }

    if (!metadataLoaded)
    {
        readModManifestFromZip(mod);
    }
    readFirstLevelFromZip(mod);

    FileList levels;
    FileUtil::readDirectory(mod->path, "lev", levels);
    if (mod->ui.missionCount == 0 && levels.size() > 0)
    {
        mod->ui.missionCount = (s32)levels.size();
    }
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
}

static bool findFirstModArchive(const char* dir, char* outName, size_t outSize)
{
    FileList files;
    FileUtil::readDirectory(dir, "zip", files);
    if (!files.empty())
    {
        copyString(outName, outSize, files[0].c_str());
        return true;
    }
    outName[0] = 0;
    return false;
}

static void addModEntry(const char* dir, const char* archiveName)
{
    if (s_modCount >= 12 || !archiveName || !archiveName[0]) return;
    XboxModEntry* mod = &s_modEntries[s_modCount];
    assignModDefaults(mod, dir, archiveName);
    readModManifest(mod);
    s_modUi[s_modCount] = mod->ui;
    s_modCount++;
}

static void addRemasterExtrasEntry()
{
    if (s_modCount >= 12) return;

    char extrasPath[TFE_MAX_PATH];
    snprintf(extrasPath, TFE_MAX_PATH, "%sextras.gob", TFE_Paths::getPath(PATH_SOURCE_DATA));
    if (!FileUtil::exists(extrasPath)) return;

    XboxModEntry* mod = &s_modEntries[s_modCount];
    assignModDefaults(mod, TFE_Paths::getPath(PATH_SOURCE_DATA), "extras.gob");
    copyString(mod->title, sizeof(mod->title), "Avenger Prototype");
    copyString(mod->author, sizeof(mod->author), "LucasArts");
    copyString(mod->version, sizeof(mod->version), "Remaster Extras");
    copyString(mod->description, sizeof(mod->description),
        "Original Avenger prototype level from Dark Forces Remaster extras.gob.");
    copyString(mod->levelName, sizeof(mod->levelName), "AVENGER");
    mod->ui.title = mod->title;
    mod->ui.author = mod->author;
    mod->ui.version = mod->version;
    mod->ui.description = mod->description;
    mod->ui.missionCount = 1;
    mod->ui.hasQuickSave = mod->hasQuickSave;
    s_modUi[s_modCount] = mod->ui;
    s_modCount++;
    TFE_System::logWrite(LOG_MSG, "ModMenu", "added Remaster extras map '%s'", extrasPath);
}

static void refreshModSlots()
{
    memset(s_modEntries, 0, sizeof(s_modEntries));
    memset(s_modUi, 0, sizeof(s_modUi));
    s_modCount = 0;

    addRemasterExtrasEntry();

    char modsRoot[TFE_MAX_PATH];
    snprintf(modsRoot, TFE_MAX_PATH, "%sMods\\", TFE_Paths::getPath(PATH_PROGRAM));
    FileUtil::makeDirectory(modsRoot);

    FileList dirs;
    FileUtil::readSubdirectories(modsRoot, dirs);
    for (size_t i = 0; i < dirs.size() && s_modCount < 12; i++)
    {
        char archiveName[96];
        if (findFirstModArchive(dirs[i].c_str(), archiveName, sizeof(archiveName)))
        {
            addModEntry(dirs[i].c_str(), archiveName);
        }
    }

    FileList files;
    FileUtil::readDirectory(modsRoot, "zip", files);
    for (size_t f = 0; f < files.size() && s_modCount < 12; f++)
    {
        addModEntry(modsRoot, files[f].c_str());
    }

    if (s_modMenuSelection >= s_modCount) s_modMenuSelection = s_modCount > 0 ? s_modCount - 1 : 0;
    TFE_System::logWrite(LOG_MSG, "ModMenu", "mods refreshed count=%d root='%s'", s_modCount, modsRoot);
}

static void startMenuMove(s32 delta)
{
    s_startMenuSelection += delta;
    if (s_startMenuSelection < 0) s_startMenuSelection = 3;
    if (s_startMenuSelection > 3) s_startMenuSelection = 0;
    TFE_System::logWrite(LOG_MSG, "StartMenu", "selection=%d", s_startMenuSelection);
}

static s32 optionPercent(float value)
{
    s32 pct = (s32)(value * 100.0f + 0.5f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static void refreshOptionsItems()
{
    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    s_optionsItems[0].label = "LOOK SENSITIVITY";
    s_optionsItems[0].value = (s32)(TFE_InputXbox::getLookSensitivity() * 100.0f + 0.5f);
    s_optionsItems[0].minValue = 25;
    s_optionsItems[0].maxValue = 250;

    s_optionsItems[1].label = "STICK DEADZONE";
    s_optionsItems[1].value = (s32)(TFE_InputXbox::getStickDeadzone() * 100.0f + 0.5f);
    s_optionsItems[1].minValue = 0;
    s_optionsItems[1].maxValue = 30;

    s_optionsItems[2].label = "MASTER VOLUME";
    s_optionsItems[2].value = optionPercent(sound->masterVolume);
    s_optionsItems[2].minValue = 0;
    s_optionsItems[2].maxValue = 100;

    s_optionsItems[3].label = "SFX VOLUME";
    s_optionsItems[3].value = optionPercent(sound->soundFxVolume);
    s_optionsItems[3].minValue = 0;
    s_optionsItems[3].maxValue = 100;

    s_optionsItems[4].label = "MUSIC VOLUME";
    s_optionsItems[4].value = optionPercent(sound->musicVolume);
    s_optionsItems[4].minValue = 0;
    s_optionsItems[4].maxValue = 100;

    s_optionsItems[5].label = "CUTSCENE SFX";
    s_optionsItems[5].value = optionPercent(sound->cutsceneSoundFxVolume);
    s_optionsItems[5].minValue = 0;
    s_optionsItems[5].maxValue = 100;

    s_optionsItems[6].label = "CUTSCENE MUSIC";
    s_optionsItems[6].value = optionPercent(sound->cutsceneMusicVolume);
    s_optionsItems[6].minValue = 0;
    s_optionsItems[6].maxValue = 100;
}

static void applyOptionValue(s32 index, s32 value)
{
    if (index < 0 || index >= 7) return;
    if (value < s_optionsItems[index].minValue) value = s_optionsItems[index].minValue;
    if (value > s_optionsItems[index].maxValue) value = s_optionsItems[index].maxValue;

    TFE_Settings_Sound* sound = TFE_Settings::getSoundSettings();
    TFE_Settings_System* system = TFE_Settings::getSystemSettings();
    switch (index)
    {
        case 0:
            system->xboxLookSensitivity = (float)value / 100.0f;
            TFE_InputXbox::setLookSensitivity(system->xboxLookSensitivity);
            break;
        case 1:
            system->xboxStickDeadzone = (float)value / 100.0f;
            TFE_InputXbox::setStickDeadzone(system->xboxStickDeadzone);
            break;
        case 2: sound->masterVolume = (float)value / 100.0f; break;
        case 3: sound->soundFxVolume = (float)value / 100.0f; break;
        case 4: sound->musicVolume = (float)value / 100.0f; break;
        case 5: sound->cutsceneSoundFxVolume = (float)value / 100.0f; break;
        case 6: sound->cutsceneMusicVolume = (float)value / 100.0f; break;
    }

    sound = TFE_Settings::getSoundSettings();
    TFE_MidiPlayer::setVolume(sound->musicVolume * sound->masterVolume);
    refreshOptionsItems();
}

static void optionsMove(s32 delta)
{
    s_optionsSelection += delta;
    if (s_optionsSelection < 0) s_optionsSelection = 6;
    if (s_optionsSelection > 6) s_optionsSelection = 0;
    if (s_optionsSelection < s_optionsScroll) s_optionsScroll = s_optionsSelection;
    if (s_optionsSelection >= s_optionsScroll + 7) s_optionsScroll = s_optionsSelection - 6;
    TFE_System::logWrite(LOG_MSG, "Options", "selection=%d", s_optionsSelection);
}

static void openModMenu();

static void openOptionsMenu(bool pauseStyle)
{
    (void)pauseStyle;
    refreshOptionsItems();
    s_optionsSelection = 0;
    s_optionsScroll = 0;
    s_optionsStickUpHeld = s_optionsStickDownHeld = false;
    s_optionsStickLeftHeld = s_optionsStickRightHeld = false;
    s_curState = APP_STATE_OPTIONS;
    TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetOptionsScreen(true, false, s_optionsSelection, s_optionsScroll, s_optionsFrame, s_optionsItems, 7);
}

static void closeOptionsMenu()
{
    TFE_Settings::writeToDisk();
    s_curState = APP_STATE_MENU;
    TFE_RenderBackend::xboxSetOptionsScreen(false, false, 0, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
    startMenuMusic();
}

static void updateOptionsMenu()
{
    const f32 lx = TFE_Input::getAxis(AXIS_LEFT_X);
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;
    const bool stickLeft = lx < -0.55f;
    const bool stickRight = lx > 0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) || (stickUp && !s_optionsStickUpHeld)) optionsMove(-1);
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) || (stickDown && !s_optionsStickDownHeld)) optionsMove(1);
    s_optionsStickUpHeld = stickUp;
    s_optionsStickDownHeld = stickDown;

    s32 delta = 0;
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_LEFT) || (stickLeft && !s_optionsStickLeftHeld)) delta = -5;
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_RIGHT) || (stickRight && !s_optionsStickRightHeld)) delta = 5;
    s_optionsStickLeftHeld = stickLeft;
    s_optionsStickRightHeld = stickRight;
    if (delta) applyOptionValue(s_optionsSelection, s_optionsItems[s_optionsSelection].value + delta);

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        TFE_Settings::writeToDisk();
        TFE_System::logWrite(LOG_MSG, "Options", "applied");
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
    {
        closeOptionsMenu();
        return;
    }

    TFE_RenderBackend::xboxSetOptionsScreen(s_curState == APP_STATE_OPTIONS, false, s_optionsSelection, s_optionsScroll, s_optionsFrame++, s_optionsItems, 7);
}

static void updateStartMenu()
{
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) ||
        (stickUp && !s_startStickUpHeld))
    {
        startMenuMove(-1);
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) ||
        (stickDown && !s_startStickDownHeld))
    {
        startMenuMove(1);
    }
    s_startStickUpHeld = stickUp;
    s_startStickDownHeld = stickDown;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) ||
        TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        TFE_System::logWrite(LOG_MSG, "StartMenu", "activate selection=%d", s_startMenuSelection);
        if (s_startMenuSelection == 0)
        {
            const char* gameArgv[] = { "tfe_xbox", "-xbriefing", "-lSECBASE" };
            TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
            TFE_System::logWrite(LOG_MSG, "Main", "Starting Dark Forces from start menu.");
            startGame(3, gameArgv);
        }
        else if (s_startMenuSelection == 1)
        {
            refreshLoadSlots();
            s_curState = APP_STATE_LOAD;
            s_loadMenuSelection = 0;
            TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
            TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
            TFE_RenderBackend::xboxSetLoadScreen(true, s_loadMenuSelection, s_loadMenuFrame, s_loadSlots, 6);
            TFE_System::logWrite(LOG_MSG, "StartMenu", "opened load screen");
        }
        else if (s_startMenuSelection == 2)
        {
            openModMenu();
        }
        else if (s_startMenuSelection == 3)
        {
            TFE_System::logWrite(LOG_MSG, "StartMenu", "opened options screen");
            openOptionsMenu(false);
        }
        else
        {
            TFE_System::logWrite(LOG_WARNING, "StartMenu", "selection %d is not wired yet", s_startMenuSelection);
        }
    }

    TFE_RenderBackend::xboxSetStartScreen(s_curState == APP_STATE_MENU, s_startMenuSelection, s_startMenuFrame++);
}

static void loadMenuMove(s32 delta)
{
    s_loadMenuSelection += delta;
    if (s_loadMenuSelection < 0) s_loadMenuSelection = 5;
    if (s_loadMenuSelection > 5) s_loadMenuSelection = 0;
    TFE_System::logWrite(LOG_MSG, "LoadMenu", "selection=%d valid=%d",
        s_loadMenuSelection, s_loadSlots[s_loadMenuSelection].valid ? 1 : 0);
}

static void closeLoadMenu()
{
    s_curState = APP_STATE_MENU;
    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
    startMenuMusic();
}

static void openModMenu()
{
    refreshModSlots();
    s_curState = APP_STATE_MODS;
    s_modMenuSelection = 0;
    s_modStickUpHeld = false;
    s_modStickDownHeld = false;
    TFE_RenderBackend::xboxSetStartScreen(false, 0, 0);
    TFE_RenderBackend::xboxSetModScreen(true, s_modMenuSelection, s_modMenuFrame, s_modUi, s_modCount);
    TFE_System::logWrite(LOG_MSG, "StartMenu", "opened mod screen");
}

static void closeModMenu()
{
    s_curState = APP_STATE_MENU;
    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
    startMenuMusic();
}

static void modMenuMove(s32 delta)
{
    if (s_modCount <= 0) return;
    s_modMenuSelection += delta;
    if (s_modMenuSelection < 0) s_modMenuSelection = s_modCount - 1;
    if (s_modMenuSelection >= s_modCount) s_modMenuSelection = 0;
    TFE_System::logWrite(LOG_MSG, "ModMenu", "selection=%d title='%s'", s_modMenuSelection, s_modEntries[s_modMenuSelection].title);
}

static void startSelectedMod()
{
    if (s_modMenuSelection < 0 || s_modMenuSelection >= s_modCount) return;
    XboxModEntry* mod = &s_modEntries[s_modMenuSelection];
    if (!mod->ui.valid || !mod->archiveName[0]) return;

    TFE_Paths::addAbsoluteSearchPathToHead(mod->path);
    static char modArg[128];
    static char levelArg[64];
    snprintf(modArg, sizeof(modArg), "-u%s", mod->archiveName);
    if (mod->levelName[0])
    {
        snprintf(levelArg, sizeof(levelArg), "-l%s", mod->levelName);
        const char* gameArgv[] = { "tfe_xbox", "-c0", modArg, levelArg };
        TFE_System::logWrite(LOG_MSG, "ModMenu", "starting mod title='%s' path='%s' archive='%s' level='%s'",
            mod->title, mod->path, mod->archiveName, mod->levelName);
        startGame(4, gameArgv);
    }
    else
    {
        snprintf(levelArg, sizeof(levelArg), "-lSECBASE");
        const char* gameArgv[] = { "tfe_xbox", "-c0", modArg, levelArg };
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "starting mod without detected level; falling back to SECBASE title='%s' path='%s' archive='%s'",
            mod->title, mod->path, mod->archiveName);
        startGame(4, gameArgv);
    }
}

static void resumeSelectedMod()
{
    if (s_modMenuSelection < 0 || s_modMenuSelection >= s_modCount) return;
    XboxModEntry* mod = &s_modEntries[s_modMenuSelection];
    if (!mod->ui.valid || !mod->archiveName[0] || !mod->hasQuickSave || !mod->quickSaveName[0]) return;

    TFE_Paths::addAbsoluteSearchPathToHead(mod->path);
    TFE_System::logWrite(LOG_MSG, "ModMenu", "resuming mod title='%s' save='%s' path='%s' archive='%s'",
        mod->title, mod->quickSaveName, mod->path, mod->archiveName);
    loadGameFromMenu(mod->quickSaveName);
}

static void updateModMenu()
{
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) || (stickUp && !s_modStickUpHeld))
    {
        modMenuMove(-1);
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) || (stickDown && !s_modStickDownHeld))
    {
        modMenuMove(1);
    }
    s_modStickUpHeld = stickUp;
    s_modStickDownHeld = stickDown;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
    {
        closeModMenu();
        return;
    }

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) || TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        if (s_modCount > 0)
        {
            startSelectedMod();
            return;
        }
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "start pressed with no mods installed");
    }

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_X))
    {
        if (s_modCount > 0 && s_modEntries[s_modMenuSelection].hasQuickSave)
        {
            resumeSelectedMod();
            return;
        }
        TFE_System::logWrite(LOG_WARNING, "ModMenu", "resume pressed without mod quicksave selection=%d", s_modMenuSelection);
    }

    TFE_RenderBackend::xboxSetModScreen(s_curState == APP_STATE_MODS, s_modMenuSelection, s_modMenuFrame++, s_modUi, s_modCount);
}

static void updateLoadMenu()
{
    const f32 ly = TFE_Input::getAxis(AXIS_LEFT_Y);
    const bool stickUp = ly > 0.55f;
    const bool stickDown = ly < -0.55f;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_UP) ||
        (stickUp && !s_loadStickUpHeld))
    {
        loadMenuMove(-1);
    }
    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_DPAD_DOWN) ||
        (stickDown && !s_loadStickDownHeld))
    {
        loadMenuMove(1);
    }
    s_loadStickUpHeld = stickUp;
    s_loadStickDownHeld = stickDown;

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_B) ||
        TFE_Input::buttonPressed(CONTROLLER_BUTTON_BACK))
    {
        closeLoadMenu();
        return;
    }

    if (TFE_Input::buttonPressed(CONTROLLER_BUTTON_A) ||
        TFE_Input::buttonPressed(CONTROLLER_BUTTON_START))
    {
        if (s_loadSlots[s_loadMenuSelection].valid)
        {
            loadGameFromMenu(s_loadSlots[s_loadMenuSelection].fileName);
            return;
        }
        TFE_System::logWrite(LOG_WARNING, "LoadMenu", "empty slot selected=%d", s_loadMenuSelection);
    }

    TFE_RenderBackend::xboxSetLoadScreen(s_curState == APP_STATE_LOAD, s_loadMenuSelection, s_loadMenuFrame++, s_loadSlots, 6);
}

// ---------------------------------------------------------------------------
// XBE entry point
// The XDK expects void __cdecl main() with no arguments.
// ---------------------------------------------------------------------------
void __cdecl main()
{
    // Open the log file as the very first thing, before any other init,
    // so early-boot failures are captured. Hardcoded to E:\tfe_xbox_log.txt;
    // overwritten each boot (no rotation).
    TFE_System::openRotatingLog("tfe_xbox_log.txt");
    TFE_XboxLogf("Main", "XBE entry");

    // -----------------------------------------------------------------------
    // XInput device enumeration - MUST come before D3D init.
    // XDK requirement: XInitDevices must be called before
    // Direct3D_CreateDevice (ordering required by the USB host controller
    // initialisation sequence on NV2A hardware). Without this call,
    // XInputOpen returns NULL and no controllers are detected.
    // Pattern lifted verbatim from OpenJKDF2's main_xbox.c.
    // -----------------------------------------------------------------------
    {
        XDEVICE_PREALLOC_TYPE xdpt[2];
        xdpt[0].DeviceType      = XDEVICE_TYPE_GAMEPAD;
        xdpt[0].dwPreallocCount = 4;
        xdpt[1].DeviceType      = XDEVICE_TYPE_MEMORY_UNIT;
        xdpt[1].dwPreallocCount = 8;
        TFE_XboxLogf("Main", "calling XInitDevices");
        XInitDevices(2, xdpt);
        TFE_XboxLogf("Main", "XInitDevices done");
    }

    // -----------------------------------------------------------------------
    // Paths
    // -----------------------------------------------------------------------
    TFE_XboxLogf("Main", "initialising paths");
    TFE_XboxLogf("Main", "calling setProgramPath");
    TFE_Paths::setProgramPath();
    TFE_XboxLogf("Main", "setProgramPath returned");
    TFE_XboxLogf("Main", "calling setProgramDataPath");
    TFE_Paths::setProgramDataPath("TFE");
    TFE_XboxLogf("Main", "setProgramDataPath returned");
    TFE_XboxLogf("Main", "calling setUserDocumentsPath");
    TFE_Paths::setUserDocumentsPath("TFE");
    TFE_XboxLogf("Main", "setUserDocumentsPath returned");

    // ----- CP block: pre-logWrite probe. logWrite uses time()/localtime()/
    // strftime() which may hang if the Xbox CRT time-of-day isn't set up.
    // If the next CP fires but no [Main] line follows, logWrite is the hang.
    TFE_System::logWrite(LOG_MSG, "Main", "The Force Engine %s (Xbox)", c_gitVersion);

    TFE_System::logWrite(LOG_MSG, "Paths", "Program:   \"%s\"", TFE_Paths::getPath(PATH_PROGRAM));
    TFE_System::logWrite(LOG_MSG, "Paths", "SaveData:  \"%s\"", TFE_Paths::getPath(PATH_PROGRAM_DATA));

    // -----------------------------------------------------------------------
    // Messages (skipped on Xbox)
    // -----------------------------------------------------------------------
    // We deliberately do NOT probe for D:\UI_Text\TfeMessages.txt on Xbox.
    // The xquake gold-standard pattern (xbox/private/test/games/xquake) is:
    // only call file APIs on paths under registered, validated search dirs.
    // If we blind-probe a path whose parent dir doesn't exist, FATX's path
    // resolver hangs (confirmed empirically with fopen, CreateFileA,
    // GetFileAttributesA, and FindFirstFileA — all hang on missing
    // intermediate dirs). UI_Text\ isn't shipped with the XBE FTP, so we
    // skip the call entirely. Loadcaller is already designed to handle
    // missing messages.
    TFE_System::logWrite(LOG_WARNING, "Main", "TfeMessages.txt skipped on Xbox - continuing.");

    // -----------------------------------------------------------------------
    // Settings
    // -----------------------------------------------------------------------
    bool firstRun = false;
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising settings.");
    if (!TFE_Settings::init(firstRun))
    {
        TFE_System::logWrite(LOG_CRITICAL, "Main", "Cannot load settings.");
        TFE_System::logClose();
        return;
    }
    TFE_System::logWrite(LOG_MSG, "Main", "Settings initialised firstRun=%d", firstRun ? 1 : 0);

    // Set source data path from program directory.
    setupSourceDataPath();

    // Override settings for Xbox: always fullscreen with a native 640x480
    // game framebuffer presented through the D3D8 backend.
    TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
    windowSettings->fullscreen = true;
    windowSettings->width      = 1280;
    windowSettings->height     = 720;

    TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
    graphics->gameResolution.x = 640;
    graphics->gameResolution.z = 480;
    graphics->widescreen = false;
    graphics->rendererIndex = 0;  // RENDERER_SOFTWARE
    graphics->colorMode = (ColorMode)0;  // COLORMODE_8BIT
    graphics->useMipmapping = false;
    TFE_System::logWrite(LOG_MSG, "Main",
        "Xbox settings applied: window=%dx%d game=%dx%d renderer=%d colorMode=%d vsync=%d",
        windowSettings->width, windowSettings->height,
        graphics->gameResolution.x, graphics->gameResolution.z,
        graphics->rendererIndex, (int)graphics->colorMode, graphics->vsync ? 1 : 0);

    // Ensure saves directory exists.
    char savesDir[TFE_MAX_PATH];
    snprintf(savesDir, TFE_MAX_PATH, "%sSaves\\", TFE_Paths::getPath(PATH_PROGRAM));
    FileUtil::makeDirectory(savesDir);
    TFE_System::logWrite(LOG_MSG, "Main", "Save directory ensured: '%s'", savesDir);

    // -----------------------------------------------------------------------
    // System init (timing)
    // -----------------------------------------------------------------------
    const f32 refreshRate = 60.0f;
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising system timing.");
    TFE_System::init(refreshRate, graphics->vsync, c_gitVersion);

    // -----------------------------------------------------------------------
    // Render backend (D3D8)
    // -----------------------------------------------------------------------
    WindowState windowState;
    memset(&windowState, 0, sizeof(windowState));
    strcpy(windowState.name, "The Force Engine");
    windowState.width          = 1280;
    windowState.height         = 720;
    windowState.baseWindowWidth  = 1280;
    windowState.baseWindowHeight = 720;
    windowState.monitorWidth   = 1280;
    windowState.monitorHeight  = 720;
    windowState.flags          = WINFLAG_FULLSCREEN | (graphics->vsync ? WINFLAG_VSYNC : 0);
    windowState.refreshRate    = refreshRate;

    bool rbOk = TFE_RenderBackend::init(windowState);
    if (!rbOk)
    {
        TFE_System::logWrite(LOG_CRITICAL, "GPU", "Cannot initialise D3D8 render backend.");
        TFE_System::logClose();
        return;
    }
    TFE_System::logWrite(LOG_MSG, "GPU", "Render backend initialised.");

    // -----------------------------------------------------------------------
    // Audio (PCM + OGG via STB Vorbis - MIDI is baked)
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising audio.");
    // audioDevice_xbox.cpp is now polled (matches xquake / OpenJKDF2 /
    // Mercs). The earlier notification-thread implementation crashed in
    // CXBX-R's HLE; the polled model has no audio thread and runs each
    // pump() from the main loop. We pass useNullDevice=false to enable it.
    TFE_Audio::init(false, TFE_Settings::getSoundSettings()->audioDevice);
    TFE_System::logWrite(LOG_MSG, "Main", "Audio init returned.");

    // MidiPlayer: OPL3 software synth (Fm4Opl3Device). Default device type
    // matches iMuse expectation; rendering happens inside the audio mixer
    // callback chain via synthesizeMidi().
    TFE_MidiPlayer::init(-1, MIDI_TYPE_OPL3);
    TFE_System::logWrite(LOG_MSG, "Main", "MidiPlayer init returned.");

    // -----------------------------------------------------------------------
    // Asset systems
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising image and palette systems.");
    TFE_Image::init();
    TFE_Palette::createDefault256();

    // -----------------------------------------------------------------------
    // Game systems
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising game/input/save systems.");
    game_init();
    inputMapping_startup();
    TFE_SaveSystem::init();
    TFE_SaveSystem::setCurrentGame(TFE_Settings::getGame()->id);
    TFE_InputXbox::init();
    TFE_InputXbox::setLookSensitivity(TFE_Settings::getSystemSettings()->xboxLookSensitivity);
    TFE_InputXbox::setStickDeadzone(TFE_Settings::getSystemSettings()->xboxStickDeadzone);

    // -----------------------------------------------------------------------
    // Reticle
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Initialising reticle.");
    reticle_init();

    // -----------------------------------------------------------------------
    // Validate game data
    // -----------------------------------------------------------------------
    TFE_System::logWrite(LOG_MSG, "Main", "Validating game data.");
    bool pathOk = validatePath();
    if (!pathOk)
    {
        TFE_System::logWrite(LOG_CRITICAL, "Main",
            "Game data not found. Place DARK.GOB in the DARK\\ folder next to the XBE.");
        // Spin with a black screen and error in log - no recovery UI on Xbox.
        while (true)
        {
            TFE_RenderBackend::clearWindow();
            TFE_RenderBackend::swap(false);
            Sleep(1000);
        }
    }

    // -----------------------------------------------------------------------
    // Frame limiter
    // -----------------------------------------------------------------------
    TFE_System::frameLimiter_set(graphics->frameRateLimit);
    TFE_System::logWrite(LOG_MSG, "Main", "Frame limiter set to %d", graphics->frameRateLimit);

    // -----------------------------------------------------------------------
    // Start with the original Landru intro sequence, then return to the
    // Xbox-native menu. Start Game itself jumps to the first briefing so the
    // intro does not replay every time.
    // -----------------------------------------------------------------------
    TFE_Input::enableRelativeMode(false);
    {
        const char* introArgv[] = { "tfe_xbox", "-xintro" };
        TFE_System::logWrite(LOG_MSG, "Main", "Starting Xbox startup intro.");
        startGame(2, introArgv);
    }

    TFE_System::logWrite(LOG_MSG, "Main", "Entering app loop.");

    // -----------------------------------------------------------------------
    // Game loop
    // -----------------------------------------------------------------------
    while (s_loop && !TFE_System::quitMessagePosted())
    {
        TFE_System::frameLimiter_begin();

        TFE_InputXbox::pollInput();

        // Check Start+Back as quit combo (held together).
        if (TFE_Input::buttonDown(CONTROLLER_BUTTON_START) &&
            TFE_Input::buttonDown(CONTROLLER_BUTTON_BACK))
        {
            TFE_System::logWrite(LOG_MSG, "Main", "Start+Back held - quitting.");
            s_loop = false;
            break;
        }

        if (s_curState == APP_STATE_MENU)
        {
            updateStartMenu();
        }
        else if (s_curState == APP_STATE_LOAD)
        {
            updateLoadMenu();
        }
        else if (s_curState == APP_STATE_MODS)
        {
            updateModMenu();
        }
        else if (s_curState == APP_STATE_OPTIONS)
        {
            updateOptionsMenu();
        }
        else if (!inputMapping_handleInputs())
        {
            TFE_Input::endFrame();
            inputMapping_endFrame();
            continue;
        }

        TFE_System::update();

        bool endInputFrame = true;
        if (s_curState == APP_STATE_GAME)
        {
            if (!s_curGame)
            {
                // Game exited cleanly (e.g. credits ended).
                s_loop = false;
            }
            else
            {
                TFE_SaveSystem::update();
                s_curGame->loopGame();
                if (s_returnToStartRequested)
                {
                    freeGame(s_curGame);
                    s_curGame = NULL;
                    s_curState = APP_STATE_MENU;
                    s_returnToStartRequested = false;
                    TFE_Input::enableRelativeMode(false);
                    TFE_RenderBackend::xboxSetStartScreen(true, s_startMenuSelection, s_startMenuFrame);
                    TFE_RenderBackend::xboxSetLoadScreen(false, 0, 0, NULL, 0);
                    TFE_RenderBackend::xboxSetModScreen(false, 0, 0, NULL, 0);
                    TFE_RenderBackend::xboxSetOptionsScreen(false, false, 0, 0, 0, NULL, 0);
                    startMenuMusic();
                    endInputFrame = true;
                }
                else
                {
                    endInputFrame = TFE_Jedi::task_run() != 0;
                }
            }
        }

        TFE_RenderBackend::swap(s_curState == APP_STATE_GAME);

        // Refill the audio ring buffer (polled streaming - no audio thread).
        TFE_AudioDevice::pump();

        TFE_System::frameLimiter_end();

        if (endInputFrame)
        {
            TFE_Input::endFrame();
            inputMapping_endFrame();
        }
    }

    TFE_System::logWrite(LOG_MSG, "Main", "Game loop ended. Shutting down.");
    stopMenuMusic();

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    TFE_InputXbox::shutdown();

    if (s_curGame)
    {
        freeGame(s_curGame);
        s_curGame = NULL;
    }

    game_destroy();
    reticle_destroy();
    inputMapping_shutdown();

    TFE_Audio::shutdown();
    TFE_MidiPlayer::destroy();
    TFE_Image::shutdown();
    TFE_Palette::freeAll();
    TFE_Settings::shutdown();
    TFE_Jedi::texturepacker_freeGlobal();
    TFE_RenderBackend::destroy();
    TFE_SaveSystem::destroy();
    TFE_System::freeMessages();

    TFE_System::logWrite(LOG_MSG, "Main", "Shutdown complete.");
    TFE_System::logClose();
}
