// fileutil_xbox.cpp
// Xbox implementation of FileUtil.
// Replaces fileutil.cpp and fileutil-posix.cpp for the Xbox build configuration.
//
// Notes:
// - Xbox uses Win32-derived kernel calls available in the XDK.
// - _findfirst/_findnext are available in the XDK CRT.
// - SHGetFolderPath, shlwapi, OneDrive logic all removed.
// - FileList is redefined without STL in fileutil_xbox.h;
//   here we use the char[TFE_MAX_PATH] array form via the Xbox-specific header.
// - getModifiedTime returns 0 - not needed for gameplay, saves are versioned by slot.

#pragma once
#include "fileutil.h"
#include "filestream.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <xtl.h>   // XDK umbrella header (replaces windows.h on Xbox)

namespace FileUtil
{
    // -----------------------------------------------------------------------
    // readDirectory
    // Appends matching filenames (name only, no path) into fileList.
    // Uses _findfirst/_findnext which are present in the XDK CRT.
    // -----------------------------------------------------------------------
    void readDirectory(const char* dir, const char* ext, FileList& fileList)
    {
        char searchStr[TFE_MAX_PATH];
        _finddata_t fileInfo;

        snprintf(searchStr, TFE_MAX_PATH, "%s*.%s", dir, ext);
        intptr_t hFile = _findfirst(searchStr, &fileInfo);
        if (hFile != -1)
        {
            do
            {
                // FileList on Xbox is a fixed array helper - see fileutil_xbox.h.
                fileList.push(fileInfo.name);
            }
            while (_findnext(hFile, &fileInfo) == 0);
            _findclose(hFile);
        }
    }

    // -----------------------------------------------------------------------
    // readSubdirectories
    // -----------------------------------------------------------------------
    void readSubdirectories(const char* dir, FileList& dirList)
    {
        char baseDir[TFE_MAX_PATH];
        strncpy(baseDir, dir, TFE_MAX_PATH - 2);
        baseDir[TFE_MAX_PATH - 2] = 0;

        size_t len = strlen(baseDir);
        baseDir[len]     = '*';
        baseDir[len + 1] = 0;

        WIN32_FIND_DATAA fi;
        HANDLE h = FindFirstFileA(baseDir, &fi);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (strcmp(fi.cFileName, ".") != 0 && strcmp(fi.cFileName, "..") != 0)
                    {
                        char entry[TFE_MAX_PATH];
                        snprintf(entry, TFE_MAX_PATH, "%s%s\\", dir, fi.cFileName);
                        dirList.push(entry);
                    }
                }
            }
            while (FindNextFileA(h, &fi));
            FindClose(h);
        }
    }

    // -----------------------------------------------------------------------
    // makeDirectory
    // -----------------------------------------------------------------------
    bool makeDirectory(const char* dir)
    {
        if (CreateDirectoryA(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
            return true;
        return false;
    }

    // -----------------------------------------------------------------------
    // getCurrentDirectory / setCurrentDirectory / getExecutionDirectory
    // -----------------------------------------------------------------------
    void getCurrentDirectory(char* dir)
    {
        GetCurrentDirectoryA(TFE_MAX_PATH, dir);
    }

    void getExecutionDirectory(char* dir)
    {
        if (GetModuleFileNameA(NULL, dir, TFE_MAX_PATH) == 0)
        {
            dir[0] = 0;
            return;
        }
        size_t len = strlen(dir);
        size_t lastSlash = 0;
        for (size_t i = 0; i < len; i++)
        {
            if (dir[i] == '/' || dir[i] == '\\')
                lastSlash = i;
        }
        dir[lastSlash] = 0;
    }

    void setCurrentDirectory(const char* dir)
    {
        SetCurrentDirectoryA(dir);
    }

    // -----------------------------------------------------------------------
    // Path manipulation helpers
    // -----------------------------------------------------------------------
    void getFilePath(const char* filename, char* path)
    {
        s32 lastSlash = -1;
        u32 len = (u32)strlen(filename);
        for (u32 c = 0; c < len; c++)
        {
            if (filename[c] == '\\' || filename[c] == '/')
                lastSlash = c;
        }
        if (lastSlash >= 0)
        {
            for (s32 c = 0; c <= lastSlash; c++)
                path[c] = filename[c];
            path[lastSlash + 1] = 0;
        }
        else
        {
            path[0] = 0;
        }
    }

    void getFileExtension(const char* filename, char* extension)
    {
        assert(filename && extension);
        s32 len = (s32)strlen(filename);
        s32 lastPeriod = -1;
        for (s32 c = 0; c < len; c++)
        {
            if (filename[c] == '.') lastPeriod = c;
        }
        if (lastPeriod < 0) { extension[0] = 0; return; }
        for (s32 c = lastPeriod + 1; c < len; c++)
            extension[c - lastPeriod - 1] = filename[c];
        extension[len - lastPeriod - 1] = 0;
    }

    void getFileNameFromPath(const char* path, char* name, bool includeExt)
    {
        s32 lastSlash  = -1;
        s32 firstNonSpace = -1;
        s32 lastNonSpace  = -1;
        s32 lastPeriod    = -1;
        u32 len = (u32)strlen(path);

        for (u32 c = 0; c < len; c++)
        {
            if (path[c] == '\\' || path[c] == '/') lastSlash = c;
            if (firstNonSpace < 0 && path[c] != ' ') firstNonSpace = c;
            else if (path[c] != ' ') lastNonSpace = c;
            if (path[c] == '.') lastPeriod = c;
        }

        s32 start = (lastSlash + 1 > firstNonSpace) ? lastSlash + 1 : firstNonSpace;
        if (start < 0) start = 0;
        s32 end = (s32)len - 1;
        if (lastPeriod >= 0 && !includeExt)
            end = lastPeriod - 1;
        else if (lastNonSpace >= 0)
            end = lastNonSpace;

        s32 c = start;
        for (; c <= end; c++)
            name[c - start] = path[c];
        name[c - start] = 0;
    }

    // -----------------------------------------------------------------------
    // File operations
    // -----------------------------------------------------------------------
    void copyFile(const char* srcFile, const char* dstFile)
    {
        CopyFileA(srcFile, dstFile, FALSE);
    }

    void deleteFile(const char* srcFile)
    {
        DeleteFileA(srcFile);
    }

    bool directoryExists(const char* path, char* outPath)
    {
        DWORD attr = GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES) return false;
        if (outPath) strncpy(outPath, path, TFE_MAX_PATH - 1);
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool exists(const char* path)
    {
        DWORD attr = GetFileAttributesA(path);
        return (attr != INVALID_FILE_ATTRIBUTES);
    }

    // Not used for gameplay on Xbox; return 0.
    u64 getModifiedTime(const char* /*path*/)
    {
        return 0;
    }

    // -----------------------------------------------------------------------
    // Path string helpers
    // -----------------------------------------------------------------------
    void fixupPath(char* path)
    {
        size_t len = strlen(path);
        for (size_t i = 0; i < len; i++)
        {
            if (path[i] == '/') path[i] = '\\';
        }
    }

    void convertToOSPath(const char* path, char* pathOS)
    {
        size_t len = strlen(path);
        for (size_t i = 0; i < len; i++)
        {
            if (path[i] == '/')  pathOS[i] = '\\';
            else                 pathOS[i] = path[i];
        }
        pathOS[len] = 0;
    }

    void replaceExtension(const char* srcPath, const char* newExt, char* outPath)
    {
        strcpy(outPath, srcPath);
        size_t len = strlen(srcPath);
        s32 lastDot = -1;
        for (size_t i = 0; i < len; i++)
        {
            if (srcPath[i] == '.') lastDot = (s32)i;
        }
        if (lastDot >= 0)
            strcpy(&outPath[lastDot + 1], newExt);
        else
        {
            strcat(outPath, ".");
            strcat(outPath, newExt);
        }
    }

    void stripExtension(const char* srcPath, char* outPath)
    {
        size_t len = strlen(srcPath);
        s32 lastDot = -1;
        for (size_t i = 0; i < len; i++)
        {
            if (srcPath[i] == '.') lastDot = (s32)i;
        }
        if (lastDot >= 0)
        {
            strncpy(outPath, srcPath, (size_t)lastDot);
            outPath[lastDot] = 0;
        }
        else
        {
            strcpy(outPath, srcPath);
        }
    }

} // namespace FileUtil
