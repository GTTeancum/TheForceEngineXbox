// fileutil_xbox.cpp
// Xbox implementation of FileUtil functions.
// Replaces fileutil.cpp for the Xbox build configuration.
//
// Uses XDK kernel file APIs (CreateFileA, FindFirstFileA, etc.)
// which are available on the original Xbox.
// All paths are relative to the XBE launch directory.

#include "fileutil.h"
#include "filestream.h"

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#include <TFE_FileSystem/paths.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

namespace FileUtil
{
    // Xbox FATX path resolution rejects forward slashes (GetFileAttributesA,
    // CreateDirectoryA, CreateFileA, FindFirstFileA all return ERROR_FILE_
    // NOT_FOUND for "D:/DARK/" but accept "D:\DARK\"). Game code freely mixes
    // both styles; we normalize before any kernel call.
    static void normalizeSlashes(const char* in, char* out)
    {
        if (!in || !out) { if (out) out[0] = 0; return; }
        size_t i = 0;
        for (; in[i] && i < TFE_MAX_PATH - 1; i++)
            out[i] = (in[i] == '/') ? '\\' : in[i];
        out[i] = 0;
    }

    void readDirectory(const char* dir, const char* ext, FileList& fileList)
    {
        char normDir[TFE_MAX_PATH];
        normalizeSlashes(dir, normDir);
        char searchStr[TFE_MAX_PATH];
        sprintf(searchStr, "%s*.%s", normDir, ext);

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchStr, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    fileList.push_back(string(fd.cFileName));
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    void readSubdirectories(const char* dir, FileList& dirList)
    {
        char normDir[TFE_MAX_PATH];
        normalizeSlashes(dir, normDir);
        char searchStr[TFE_MAX_PATH];
        size_t len = strlen(normDir);
        strcpy(searchStr, normDir);
        searchStr[len]   = '*';
        searchStr[len+1] = 0;

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchStr, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    string name = string(fd.cFileName);
                    if (name != "." && name != "..")
                    {
                        dirList.push_back(dir + name + "/");
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    bool makeDirectory(const char* dir)
    {
        if (!dir || !dir[0]) return false;
        char normDir[TFE_MAX_PATH];
        normalizeSlashes(dir, normDir);
        if (CreateDirectoryA(normDir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            TFE_XboxLogf("FileUtil", "makeDirectory ok %s", normDir);
            return true;
        }
        TFE_XboxLogf("FileUtil", "makeDirectory failed %s err=%lu", normDir, GetLastError());
        return false;
    }

    void getCurrentDirectory(char* dir)
    {
        // On Xbox, the current directory is the XBE launch directory.
        // Use D:\ as the default (Xbox maps D: to the title directory).
        strcpy(dir, "D:\\");
        TFE_XboxLogf("FileUtil", "getCurrentDirectory -> %s", dir);
    }

    void getExecutionDirectory(char* dir)
    {
        // On Xbox, the execution directory is the XBE launch directory.
        strcpy(dir, "D:\\");
        TFE_XboxLogf("FileUtil", "getExecutionDirectory -> %s", dir);
    }

    void setCurrentDirectory(const char* dir)
    {
        // No-op on Xbox; the working directory is always relative to XBE.
        (void)dir;
    }

    void getFilePath(const char* filename, char* path)
    {
        s32 lastSlash = -1;
        u32 len = (u32)strlen(filename);

        for (u32 c = 0; c < len; c++)
        {
            if (filename[c] == '\\' || filename[c] == '/')
            {
                lastSlash = c;
            }
        }

        if (lastSlash >= 0)
        {
            for (s32 c = 0; c <= lastSlash; c++)
            {
                path[c] = filename[c];
            }
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
            if (filename[c] == '.')
            {
                lastPeriod = c;
            }
        }

        if (lastPeriod < 0) { extension[0] = 0; return; }
        for (s32 c = lastPeriod + 1; c < len; c++)
        {
            extension[c - lastPeriod - 1] = filename[c];
        }
        extension[len - lastPeriod - 1] = 0;
    }

    void getFileNameFromPath(const char* path, char* name, bool includeExt)
    {
        s32 lastSlash = -1;
        s32 firstNonSpace = -1;
        s32 lastNonSpace = -1;
        s32 lastPeriod = -1;
        u32 len = (u32)strlen(path);

        for (u32 c = 0; c < len; c++)
        {
            if (path[c] == '\\' || path[c] == '/')
                lastSlash = c;
            if (firstNonSpace < 0 && path[c] != ' ')
                firstNonSpace = c;
            else if (path[c] != ' ')
                lastNonSpace = c;
            if (path[c] == '.')
                lastPeriod = c;
        }

        s32 start = lastSlash + 1;
        if (firstNonSpace > start) start = firstNonSpace;
        s32 end = len - 1;
        if (lastPeriod >= 0 && !includeExt)
            end = lastPeriod - 1;
        else if (lastNonSpace >= 0)
            end = lastNonSpace;

        s32 c = start;
        for (; c <= end; c++)
        {
            name[c - start] = path[c];
        }
        name[c - start] = 0;
    }

    void copyFile(const char* srcFile, const char* dstFile)
    {
        char normSrc[TFE_MAX_PATH], normDst[TFE_MAX_PATH];
        normalizeSlashes(srcFile, normSrc);
        normalizeSlashes(dstFile, normDst);
        BOOL ok = CopyFileA(normSrc, normDst, FALSE);
        TFE_XboxLogf("FileUtil", "copyFile %s -> %s ok=%d err=%lu",
            normSrc, normDst, ok ? 1 : 0, ok ? 0 : GetLastError());
    }

    void deleteFile(const char* srcFile)
    {
        char normSrc[TFE_MAX_PATH];
        normalizeSlashes(srcFile, normSrc);
        BOOL ok = DeleteFileA(normSrc);
        TFE_XboxLogf("FileUtil", "deleteFile %s ok=%d err=%lu",
            normSrc, ok ? 1 : 0, ok ? 0 : GetLastError());
    }

    bool directoryExists(const char* path, char* outPath)
    {
        if (!path || !path[0])
        {
            TFE_XboxLogf("FileUtil", "directoryExists empty path");
            return false;
        }
        char normPath[TFE_MAX_PATH];
        normalizeSlashes(path, normPath);
        // Strip trailing backslash - GetFileAttributesA returns
        // ERROR_INVALID_PARAMETER (87) on directory paths that end in
        // a slash. TFE builds search paths with trailing slashes
        // (e.g. "D:\DARK\LFD\"); without this strip every directory
        // check fails and the search path is silently dropped.
        size_t len = strlen(normPath);
        while (len > 3 && (normPath[len - 1] == '\\' || normPath[len - 1] == '/'))
        {
            normPath[--len] = 0;
        }
        DWORD attr = GetFileAttributesA(normPath);
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            TFE_XboxLogf("FileUtil", "directoryExists miss %s err=%lu", normPath, GetLastError());
            return false;
        }
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool exists(const char* path)
    {
        if (!path || !path[0]) return false;
        char normPath[TFE_MAX_PATH];
        normalizeSlashes(path, normPath);
        DWORD attr = GetFileAttributesA(normPath);
        bool ok = (attr != INVALID_FILE_ATTRIBUTES);
        if (!ok)
        {
            TFE_XboxLogf("FileUtil", "exists miss %s err=%lu", normPath, GetLastError());
        }
        return ok;
    }

    u64 getModifiedTime(const char* path)
    {
        FILETIME creationTime;
        FILETIME lastAccessTime;
        FILETIME lastWriteTime;

        char normPath[TFE_MAX_PATH];
        normalizeSlashes(path, normPath);
        HANDLE fileHandle = CreateFileA(normPath, GENERIC_READ, FILE_SHARE_READ,
                                        NULL, OPEN_EXISTING, 0, NULL);
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            TFE_XboxLogf("FileUtil", "getModifiedTime open failed %s err=%lu", path ? path : "", GetLastError());
            return 0;
        }

        u64 modTime = 0;
        if (GetFileTime(fileHandle, &creationTime, &lastAccessTime, &lastWriteTime))
        {
            modTime = u64(lastWriteTime.dwHighDateTime) << 32ULL | u64(lastWriteTime.dwLowDateTime);
        }

        CloseHandle(fileHandle);
        return modTime;
    }

    void fixupPath(char* path)
    {
        const size_t len = strlen(path);
        for (size_t i = 0; i < len; i++)
        {
            if (path[i] == '\\')
            {
                path[i] = '/';
            }
        }
    }

    void convertToOSPath(const char* path, char* pathOS)
    {
        // Xbox uses backslashes like Windows.
        const size_t len = strlen(path);
        for (size_t i = 0; i < len; i++)
        {
            if (path[i] == '/')
                pathOS[i] = '\\';
            else
                pathOS[i] = path[i];
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
            if (srcPath[i] == '.') { lastDot = (s32)i; }
        }
        if (lastDot >= 0)
        {
            strcpy(&outPath[lastDot + 1], newExt);
        }
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
            if (srcPath[i] == '.') { lastDot = (s32)i; }
        }
        if (lastDot >= 0)
        {
            outPath[0] = 0;
            strncpy(outPath, srcPath, lastDot);
            outPath[lastDot] = 0;
        }
        else
        {
            strcpy(outPath, srcPath);
        }
    }
}
