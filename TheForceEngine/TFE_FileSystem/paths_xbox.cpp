// paths_xbox.cpp
// Xbox implementation of TFE_Paths.
// Replaces paths.cpp and paths-posix.cpp for the Xbox build configuration.
//
// On Xbox, all paths are relative to the XBE launch directory (D:\).
// Game data lives in DARK\ and user data lives in Saves\.
// No Windows shell APIs (SHGetFolderPath, shlwapi) are used.

#include "paths.h"
#include "fileutil.h"
#include "filestream.h"
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <string>
#include <vector>
#include <xtl.h>
#include <string.h>
#include <stdio.h>

// Early boot log function defined in system_xbox.cpp.
extern "C" void TFE_XboxLog(const char* msg);

namespace TFE_Paths
{
    struct FileMapping
    {
        std::string fileName;
        std::string realPath;
    };

    static char s_paths[PATH_COUNT][TFE_MAX_PATH];
    static std::vector<Archive*> s_localArchives;
    static std::vector<std::string> s_searchPaths;
    static std::vector<FileMapping> s_fileMappings;

    // -----------------------------------------------------------------------
    void setPath(TFE_PathType pathType, const char* path)
    {
        if (!path)
        {
            s_paths[pathType][0] = 0;
        }
        else
        {
            strncpy(s_paths[pathType], path, TFE_MAX_PATH - 1);
            s_paths[pathType][TFE_MAX_PATH - 1] = 0;
        }
        TFE_XboxLogf("Paths", "setPath type=%d path=%s", (int)pathType, s_paths[pathType]);
    }

    bool setProgramDataPath(const char* append)
    {
        // On Xbox, program data goes to the title directory.
        strncpy(s_paths[PATH_PROGRAM_DATA], s_paths[PATH_PROGRAM], TFE_MAX_PATH - 1);
        s_paths[PATH_PROGRAM_DATA][TFE_MAX_PATH - 1] = 0;
        TFE_XboxLogf("Paths", "setProgramDataPath append=%s -> %s",
            append ? append : "", s_paths[PATH_PROGRAM_DATA]);
        return s_paths[PATH_PROGRAM_DATA][0] != 0;
    }

    bool setUserDocumentsPath(const char* append)
    {
        // On Xbox, user documents go alongside the XBE. Many softmod / HDD
        // setups don't have E:\ mounted for the running title — writes there
        // fail with ERROR_PATH_NOT_FOUND, which silently cascades into the
        // pilot file (DARKPILO.CFG) never being created, no agent loading,
        // and the agent menu sitting forever with zero entries.
        // PATH_PROGRAM is the launch dir (e.g. D:\TFE\), which is always
        // readable; on FATX HDD installs it's also writable. This matches
        // OpenJKDF2's convention of writing saves next to the XBE.
        strncpy(s_paths[PATH_USER_DOCUMENTS], s_paths[PATH_PROGRAM], TFE_MAX_PATH - 1);
        s_paths[PATH_USER_DOCUMENTS][TFE_MAX_PATH - 1] = 0;
        TFE_XboxLogf("Paths", "setUserDocumentsPath append=%s -> %s",
            append ? append : "", s_paths[PATH_USER_DOCUMENTS]);
        return true;
    }

    bool setRemasterDocsPath(GameID game)
    {
        // Not applicable on Xbox.
        (void)game;
        return false;
    }

    bool setProgramPath()
    {
        // On Xbox the program path is the XBE launch directory.
        char path[TFE_MAX_PATH];
        TFE_XboxLogf("Paths", "setProgramPath begin");
        FileUtil::getCurrentDirectory(path);
        TFE_XboxLogf("Paths", "getCurrentDirectory -> %s", path);
        strncpy(s_paths[PATH_PROGRAM], path, TFE_MAX_PATH - 1);
        s_paths[PATH_PROGRAM][TFE_MAX_PATH - 1] = 0;

        // Ensure trailing backslash.
        size_t len = strlen(s_paths[PATH_PROGRAM]);
        if (len > 0 && s_paths[PATH_PROGRAM][len - 1] != '\\' &&
            s_paths[PATH_PROGRAM][len - 1] != '/')
        {
            if (len < TFE_MAX_PATH - 1)
            {
                s_paths[PATH_PROGRAM][len] = '\\';
                s_paths[PATH_PROGRAM][len + 1] = 0;
            }
        }

        TFE_XboxLog("paths_xbox: setProgramPath = ");
        TFE_XboxLog(s_paths[PATH_PROGRAM]);
        TFE_XboxLog("\r\n");
        return true;
    }

    bool mapSystemPath(char* path)
    {
        // No mapping needed on Xbox.
        return false;
    }

    const char* getPath(TFE_PathType pathType)
    {
        return s_paths[pathType];
    }

    bool hasPath(TFE_PathType pathType)
    {
        return s_paths[pathType][0] != 0;
    }

    void appendPath(TFE_PathType pathType, const char* filename, char* path, size_t bufferLen)
    {
        snprintf(path, bufferLen, "%s%s", getPath(pathType), filename);
    }

    void fixupPathAsDirectory(char* fullPath)
    {
        size_t len = strlen(fullPath);
        // Fix-up slashes.
        for (size_t i = 0; i < len; i++)
        {
            if (fullPath[i] == '\\')
            {
                fullPath[i] = '/';
            }
        }
        // Make sure it ends with a slash.
        if (len > 0 && fullPath[len - 1] != '/')
        {
            fullPath[len] = '/';
            fullPath[len + 1] = 0;
        }
    }

    // -----------------------------------------------------------------------
    // Search path management
    // -----------------------------------------------------------------------
    static void addSearchPath(const char* fullPath)
    {
        if (FileUtil::directoryExists(fullPath))
        {
            const size_t count = s_searchPaths.size();
            for (size_t i = 0; i < count; i++)
            {
                if (!strcasecmp(s_searchPaths[i].c_str(), fullPath))
                {
                    return;
                }
            }
            s_searchPaths.push_back(fullPath);
        }
    }

    static void addSearchPathToHead(const char* fullPath)
    {
        if (FileUtil::directoryExists(fullPath))
        {
            const size_t count = s_searchPaths.size();
            for (size_t i = 0; i < count; i++)
            {
                if (!strcasecmp(s_searchPaths[i].c_str(), fullPath))
                {
                    return;
                }
            }
            s_searchPaths.insert(s_searchPaths.begin(), fullPath);
        }
    }

    void clearSearchPaths()
    {
        TFE_XboxLogf("Paths", "clearSearchPaths paths=%u mappings=%u",
            (u32)s_searchPaths.size(), (u32)s_fileMappings.size());
        s_searchPaths.clear();
        s_fileMappings.clear();
    }

    void clearLocalArchives()
    {
        TFE_XboxLogf("Paths", "clearLocalArchives count=%u", (u32)s_localArchives.size());
        const size_t count = s_localArchives.size();
        Archive** archive = &s_localArchives[0];
        for (size_t i = 0; i < count; i++)
        {
            Archive::freeArchive(archive[i]);
        }
        s_localArchives.clear();
    }

    void addSingleFilePath(const char* fileName, const char* filePath)
    {
        char fileNameLC[TFE_MAX_PATH];
        strcpy(fileNameLC, fileName);
        _strlwr(fileNameLC);

        char filePathFixed[TFE_MAX_PATH];
        strcpy(filePathFixed, filePath);
        FileUtil::fixupPath(filePathFixed);

        FileMapping mapping;
        mapping.fileName = fileNameLC;
        mapping.realPath = filePathFixed;
        s_fileMappings.push_back(mapping);
        TFE_XboxLogf("Paths", "addSingleFilePath %s -> %s", fileNameLC, filePathFixed);
    }

    void addLocalSearchPath(const char* localSearchPath)
    {
        char fullPath[TFE_MAX_PATH];
        snprintf(fullPath, TFE_MAX_PATH, "%s%s", getPath(PATH_SOURCE_DATA), localSearchPath);
        fixupPathAsDirectory(fullPath);
        addSearchPath(fullPath);
        TFE_XboxLogf("Paths", "addLocalSearchPath %s", fullPath);
    }

    void addAbsoluteSearchPathToHead(const char* absoluteSearchPath)
    {
        char fullPath[TFE_MAX_PATH];
        strcpy(fullPath, absoluteSearchPath);
        fixupPathAsDirectory(fullPath);
        addSearchPathToHead(fullPath);
        TFE_XboxLogf("Paths", "addAbsoluteSearchPathToHead %s", fullPath);
    }

    void addAbsoluteSearchPath(const char* absoluteSearchPath)
    {
        char fullPath[TFE_MAX_PATH];
        strcpy(fullPath, absoluteSearchPath);
        fixupPathAsDirectory(fullPath);
        addSearchPath(fullPath);
        TFE_XboxLogf("Paths", "addAbsoluteSearchPath %s", fullPath);
    }

    void addLocalArchiveToFront(Archive* archive)
    {
        s_localArchives.insert(s_localArchives.begin(), archive);
        TFE_XboxLogf("Paths", "addLocalArchiveToFront archive=%p count=%u", archive, (u32)s_localArchives.size());
    }

    void removeFirstArchive()
    {
        TFE_XboxLogf("Paths", "removeFirstArchive countBefore=%u", (u32)s_localArchives.size());
        s_localArchives.erase(s_localArchives.begin());
    }

    void addLocalArchive(Archive* archive)
    {
        s_localArchives.push_back(archive);
        TFE_XboxLogf("Paths", "addLocalArchive archive=%p count=%u", archive, (u32)s_localArchives.size());
    }

    void removeLastArchive()
    {
        TFE_XboxLogf("Paths", "removeLastArchive countBefore=%u", (u32)s_localArchives.size());
        s_localArchives.pop_back();
    }

    bool getFilePath(const char* fileName, FilePath* outPath)
    {
        outPath->archive = NULL;
        outPath->index = INVALID_FILE;
        outPath->path[0] = 0;

        // Search for any filemappings.
        const size_t mappingCount  = s_fileMappings.size();
        const FileMapping* mapping = &s_fileMappings[0];
        for (size_t i = 0; i < mappingCount; i++, mapping++)
        {
            if (mapping->fileName[0] == tolower(fileName[0]) &&
                strcasecmp(mapping->fileName.c_str(), fileName) == 0)
            {
                strcpy(outPath->path, mapping->realPath.c_str());
                return true;
            }
        }

        // Search in the local search paths before local archives.
        const size_t pathCount = s_searchPaths.size();
        const std::string* localPath = &s_searchPaths[0];
        for (size_t i = 0; i < pathCount; i++, localPath++)
        {
            char fullName[TFE_MAX_PATH];
            sprintf(fullName, "%s%s", localPath->c_str(), fileName);

            FileStream file;
            if (file.exists(fullName))
            {
                strncpy(outPath->path, fullName, TFE_MAX_PATH);
                return true;
            }
        }

        // Then archives.
        const size_t archiveCount = s_localArchives.size();
        Archive** archive = &s_localArchives[0];
        for (size_t i = 0; i < archiveCount; i++, archive++)
        {
            if (!(*archive)) { continue; }

            u32 index = (*archive)->getFileIndex(fileName);
            if (index != INVALID_FILE)
            {
                outPath->archive = *archive;
                outPath->index = index;
                return true;
            }
        }

        return false;
    }

    void getAllFilesFromSearchPaths(const char* subdirectory, const char* ext, FileList& allFiles)
    {
        size_t pathCount = s_searchPaths.size();
        for (size_t p = 0; p < pathCount; p++)
        {
            std::vector<string> fileList;
            char dir[TFE_MAX_PATH];
            sprintf(dir, "%s%s%s", s_searchPaths[p].c_str(), subdirectory, "/");
            FileUtil::readDirectory(dir, ext, fileList);

            for (size_t f = 0; f < fileList.size(); f++)
            {
                char filePath[TFE_MAX_PATH];
                string file = fileList[f];
                sprintf(filePath, "%s%s", dir, file.c_str());
                allFiles.push_back(filePath);
            }
        }
    }
}
