// paths_xbox.cpp
// Xbox implementation of TFE_Paths.
// Replaces paths.cpp and paths-posix.cpp for the Xbox build configuration.
// All paths are relative to the XBE launch directory (T:\ mapped by XDK at boot).
// Directory layout mirrors original Dark Forces:
//   <root>\              - XBE, settings.ini, TFE support files
//   <root>\DARK\         - DARK.GOB, SOUNDS.GOB, SPRITES.GOB, TEXTURES.GOB, MISSIONS\
//   <root>\Saves\        - Save games / program data
//
// NOTE: The XDK maps the boot partition as T:\ for title data reads.
//       We resolve the launch path at init and use it as the root for everything.

#pragma once
#include "paths.h"
#include "fileutil.h"
#include "filestream.h"
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Fixed-capacity simple list helpers (no STL)
// ---------------------------------------------------------------------------
#define MAX_SEARCH_PATHS   32
#define MAX_FILE_MAPPINGS  64
#define MAX_LOCAL_ARCHIVES 16

// ---------------------------------------------------------------------------
// Xbox log shim - writes via TFE_System log until that is up, then falls back
// to a raw file write. Defined in system_xbox.cpp; declared here for local use.
// ---------------------------------------------------------------------------
extern void TFE_XboxLog(const char* msg);

namespace TFE_Paths
{
    // -----------------------------------------------------------------------
    // Internal state - C-style, no STL
    // -----------------------------------------------------------------------
    struct FileMapping
    {
        char fileName[TFE_MAX_PATH];
        char realPath[TFE_MAX_PATH];
    };

    static char          s_paths[PATH_COUNT][TFE_MAX_PATH];
    static bool          s_pathSet[PATH_COUNT];

    static char          s_searchPaths[MAX_SEARCH_PATHS][TFE_MAX_PATH];
    static int           s_searchPathCount;

    static FileMapping   s_fileMappings[MAX_FILE_MAPPINGS];
    static int           s_fileMappingCount;

    static Archive*      s_localArchives[MAX_LOCAL_ARCHIVES];
    static int           s_localArchiveCount;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------
    static void xboxLog(const char* msg)
    {
        // Forward to system log once available; safe to call before init.
        TFE_XboxLog(msg);
    }

    static void strlowerCopy(char* dst, const char* src)
    {
        int i = 0;
        while (src[i])
        {
            char c = src[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            dst[i] = c;
            i++;
        }
        dst[i] = 0;
    }

    static int pathsCaseInsensitiveCompare(const char* a, const char* b)
    {
        while (*a && *b)
        {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return (int)ca - (int)cb;
            a++; b++;
        }
        return (int)*a - (int)*b;
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    void setPath(TFE_PathType pathType, const char* path)
    {
        strncpy(s_paths[pathType], path, TFE_MAX_PATH - 1);
        s_paths[pathType][TFE_MAX_PATH - 1] = 0;
        s_pathSet[pathType] = true;
    }

    // On Xbox, program data (saves, cache) lives in <root>\Saves\
    bool setProgramDataPath(const char* append)
    {
        if (!s_pathSet[PATH_PROGRAM])
        {
            xboxLog("[TFE_Paths] setProgramDataPath: PATH_PROGRAM not set yet\n");
            return false;
        }
        char path[TFE_MAX_PATH];
        snprintf(path, TFE_MAX_PATH, "%sSaves\\", s_paths[PATH_PROGRAM]);
        FileUtil::makeDirectory(path);
        setPath(PATH_PROGRAM_DATA, path);

        char msg[TFE_MAX_PATH + 64];
        snprintf(msg, sizeof(msg), "[TFE_Paths] ProgramData -> %s\n", path);
        xboxLog(msg);
        return true;
    }

    // On Xbox, user documents == program data (no user profile system).
    bool setUserDocumentsPath(const char* append)
    {
        if (!s_pathSet[PATH_PROGRAM_DATA])
        {
            setProgramDataPath(append);
        }
        setPath(PATH_USER_DOCUMENTS, s_paths[PATH_PROGRAM_DATA]);
        return s_pathSet[PATH_PROGRAM_DATA];
    }

    // Remaster docs path - not applicable on Xbox. No-op.
    bool setRemasterDocsPath(GameID /*game*/)
    {
        return false;
    }

    // Resolve launch directory via XDK XGetLaunchInfo or GetModuleFileName fallback.
    // On Xbox the XBE is launched from a lettered drive (usually D:\ for disc,
    // but for HDD installs the dashboard maps it differently - we use the
    // module path so it works regardless of install location).
    bool setProgramPath()
    {
        char path[TFE_MAX_PATH];
        path[0] = 0;

        // XDK: GetModuleFileName works on Xbox and returns the full XBE path.
        if (GetModuleFileNameA(NULL, path, TFE_MAX_PATH) == 0)
        {
            xboxLog("[TFE_Paths] WARNING: GetModuleFileNameA failed, defaulting to D:\\\n");
            strncpy(path, "D:\\", TFE_MAX_PATH);
        }
        else
        {
            // Strip the filename, keep the directory.
            int len = (int)strlen(path);
            int lastSlash = -1;
            for (int i = 0; i < len; i++)
            {
                if (path[i] == '\\' || path[i] == '/')
                    lastSlash = i;
            }
            if (lastSlash >= 0)
                path[lastSlash + 1] = 0;
            else
                strncpy(path, "D:\\", TFE_MAX_PATH);
        }

        setPath(PATH_PROGRAM, path);

        char msg[TFE_MAX_PATH + 64];
        snprintf(msg, sizeof(msg), "[TFE_Paths] ProgramPath -> %s\n", path);
        xboxLog(msg);
        return true;
    }

    // No system path remapping needed on Xbox.
    bool mapSystemPath(char* /*path*/)
    {
        return false;
    }

    const char* getPath(TFE_PathType pathType)
    {
        return s_paths[pathType];
    }

    bool hasPath(TFE_PathType pathType)
    {
        return s_pathSet[pathType] && s_paths[pathType][0] != 0;
    }

    void appendPath(TFE_PathType pathType, const char* filename, char* path, size_t bufferLen)
    {
        snprintf(path, bufferLen, "%s%s", getPath(pathType), filename);
    }

    void fixupPathAsDirectory(char* fullPath)
    {
        size_t len = strlen(fullPath);
        // Normalise slashes to backslash (Xbox/Win32 convention).
        for (size_t i = 0; i < len; i++)
        {
            if (fullPath[i] == '/')
                fullPath[i] = '\\';
        }
        // Ensure trailing backslash.
        if (len > 0 && fullPath[len - 1] != '\\')
        {
            fullPath[len]     = '\\';
            fullPath[len + 1] = 0;
        }
    }

    // -----------------------------------------------------------------------
    // Search paths
    // -----------------------------------------------------------------------
    void addSearchPath(const char* fullPath)
    {
        if (!FileUtil::directoryExists(fullPath))
            return;

        for (int i = 0; i < s_searchPathCount; i++)
        {
            if (pathsCaseInsensitiveCompare(s_searchPaths[i], fullPath) == 0)
                return; // already present
        }

        if (s_searchPathCount >= MAX_SEARCH_PATHS)
        {
            xboxLog("[TFE_Paths] WARNING: MAX_SEARCH_PATHS exceeded\n");
            return;
        }
        strncpy(s_searchPaths[s_searchPathCount], fullPath, TFE_MAX_PATH - 1);
        s_searchPaths[s_searchPathCount][TFE_MAX_PATH - 1] = 0;
        s_searchPathCount++;
    }

    void addSearchPathToHead(const char* fullPath)
    {
        if (!FileUtil::directoryExists(fullPath))
            return;

        for (int i = 0; i < s_searchPathCount; i++)
        {
            if (pathsCaseInsensitiveCompare(s_searchPaths[i], fullPath) == 0)
                return;
        }

        if (s_searchPathCount >= MAX_SEARCH_PATHS)
        {
            xboxLog("[TFE_Paths] WARNING: MAX_SEARCH_PATHS exceeded\n");
            return;
        }
        // Shift all entries up by one.
        for (int i = s_searchPathCount; i > 0; i--)
        {
            strncpy(s_searchPaths[i], s_searchPaths[i - 1], TFE_MAX_PATH - 1);
        }
        strncpy(s_searchPaths[0], fullPath, TFE_MAX_PATH - 1);
        s_searchPaths[0][TFE_MAX_PATH - 1] = 0;
        s_searchPathCount++;
    }

    void clearSearchPaths()
    {
        s_searchPathCount  = 0;
        s_fileMappingCount = 0;
    }

    void addLocalSearchPath(const char* localSearchPath)
    {
        char fullPath[TFE_MAX_PATH];
        snprintf(fullPath, TFE_MAX_PATH, "%s%s", getPath(PATH_SOURCE_DATA), localSearchPath);
        fixupPathAsDirectory(fullPath);
        addSearchPath(fullPath);
    }

    void addAbsoluteSearchPathToHead(const char* absoluteSearchPath)
    {
        char fullPath[TFE_MAX_PATH];
        strncpy(fullPath, absoluteSearchPath, TFE_MAX_PATH - 1);
        fullPath[TFE_MAX_PATH - 1] = 0;
        fixupPathAsDirectory(fullPath);
        addSearchPathToHead(fullPath);
    }

    void addAbsoluteSearchPath(const char* absoluteSearchPath)
    {
        char fullPath[TFE_MAX_PATH];
        strncpy(fullPath, absoluteSearchPath, TFE_MAX_PATH - 1);
        fullPath[TFE_MAX_PATH - 1] = 0;
        fixupPathAsDirectory(fullPath);
        addSearchPath(fullPath);
    }

    // -----------------------------------------------------------------------
    // Local archives
    // -----------------------------------------------------------------------
    void clearLocalArchives()
    {
        for (int i = 0; i < s_localArchiveCount; i++)
        {
            s_localArchives[i] = NULL;
        }
        s_localArchiveCount = 0;
    }

    void addLocalArchiveToFront(Archive* archive)
    {
        if (s_localArchiveCount >= MAX_LOCAL_ARCHIVES)
        {
            xboxLog("[TFE_Paths] WARNING: MAX_LOCAL_ARCHIVES exceeded\n");
            return;
        }
        for (int i = s_localArchiveCount; i > 0; i--)
            s_localArchives[i] = s_localArchives[i - 1];
        s_localArchives[0] = archive;
        s_localArchiveCount++;
    }

    void removeFirstArchive()
    {
        if (s_localArchiveCount == 0) return;
        for (int i = 0; i < s_localArchiveCount - 1; i++)
            s_localArchives[i] = s_localArchives[i + 1];
        s_localArchives[--s_localArchiveCount] = NULL;
    }

    void addLocalArchive(Archive* archive)
    {
        if (s_localArchiveCount >= MAX_LOCAL_ARCHIVES)
        {
            xboxLog("[TFE_Paths] WARNING: MAX_LOCAL_ARCHIVES exceeded\n");
            return;
        }
        s_localArchives[s_localArchiveCount++] = archive;
    }

    void removeLastArchive()
    {
        if (s_localArchiveCount == 0) return;
        s_localArchives[--s_localArchiveCount] = NULL;
    }

    // -----------------------------------------------------------------------
    // Single file mappings
    // -----------------------------------------------------------------------
    void addSingleFilePath(const char* fileName, const char* filePath)
    {
        if (s_fileMappingCount >= MAX_FILE_MAPPINGS)
        {
            xboxLog("[TFE_Paths] WARNING: MAX_FILE_MAPPINGS exceeded\n");
            return;
        }
        FileMapping& m = s_fileMappings[s_fileMappingCount++];
        strlowerCopy(m.fileName, fileName);
        strncpy(m.realPath, filePath, TFE_MAX_PATH - 1);
        m.realPath[TFE_MAX_PATH - 1] = 0;
        FileUtil::fixupPath(m.realPath);
    }

    // -----------------------------------------------------------------------
    // File resolution
    // -----------------------------------------------------------------------
    bool getFilePath(const char* fileName, FilePath* outPath)
    {
        outPath->archive = NULL;
        outPath->index   = INVALID_FILE;
        outPath->path[0] = 0;

        // 1. File mappings (mod overrides).
        for (int i = 0; i < s_fileMappingCount; i++)
        {
            if (s_fileMappings[i].fileName[0] == (char)tolower((unsigned char)fileName[0]) &&
                pathsCaseInsensitiveCompare(s_fileMappings[i].fileName, fileName) == 0)
            {
                strncpy(outPath->path, s_fileMappings[i].realPath, TFE_MAX_PATH - 1);
                outPath->path[TFE_MAX_PATH - 1] = 0;
                return true;
            }
        }

        // 2. Search paths (loose files).
        for (int i = 0; i < s_searchPathCount; i++)
        {
            char fullName[TFE_MAX_PATH];
            snprintf(fullName, TFE_MAX_PATH, "%s%s", s_searchPaths[i], fileName);

            FileStream file;
            if (file.exists(fullName))
            {
                strncpy(outPath->path, fullName, TFE_MAX_PATH - 1);
                outPath->path[TFE_MAX_PATH - 1] = 0;
                return true;
            }
        }

        // 3. Local archives (GOB files).
        for (int i = 0; i < s_localArchiveCount; i++)
        {
            if (!s_localArchives[i]) continue;
            u32 index = s_localArchives[i]->getFileIndex(fileName);
            if (index != INVALID_FILE)
            {
                outPath->archive = s_localArchives[i];
                outPath->index   = index;
                return true;
            }
        }

        return false;
    }

    void getAllFilesFromSearchPaths(const char* subdirectory, const char* ext, FileList& allFiles)
    {
        for (int p = 0; p < s_searchPathCount; p++)
        {
            char dir[TFE_MAX_PATH];
            snprintf(dir, TFE_MAX_PATH, "%s%s\\", s_searchPaths[p], subdirectory);
            FileUtil::readDirectory(dir, ext, allFiles);
        }
    }

} // namespace TFE_Paths
