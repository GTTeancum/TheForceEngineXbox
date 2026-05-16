#pragma once
#include <TFE_System/types.h>

// ---------------------------------------------------------------------------
// FileList
//
// PC builds:  std::vector<std::string>  (unchanged)
// Xbox build: fixed-capacity array of char buffers, with an XboxString proxy
//             so that call sites using .data() / ->c_str() / ->length()
//             compile without modification.
// ---------------------------------------------------------------------------

#ifdef _XBOX

#include <string>

#define FILELIST_MAX_ENTRIES 256

#ifndef XBOX_STRING_DEFINED
#define XBOX_STRING_DEFINED
// Thin proxy that mimics the std::string interface used at FileList call sites.
// Also defined (identically) in parser.h; XBOX_STRING_DEFINED guards both.
struct XboxString
{
    char buf[TFE_MAX_PATH];

    XboxString() { buf[0] = 0; }

    const char* c_str()  const { return buf; }
    size_t      length() const { return strlen(buf); }
    size_t      size()   const { return strlen(buf); }
    bool        empty()  const { return buf[0] == 0; }

    void operator=(const char* src)
    {
        strncpy(buf, src ? src : "", TFE_MAX_PATH - 1);
        buf[TFE_MAX_PATH - 1] = 0;
    }

    bool operator==(const char* rhs) const
    {
        return strcmp(buf, rhs ? rhs : "") == 0;
    }

    operator std::string() const { return std::string(buf); }
};
#endif // XBOX_STRING_DEFINED

struct FileList
{
    XboxString entries[FILELIST_MAX_ENTRIES];
    int        count;

    FileList() : count(0) {}

    // Append an entry.
    void push(const char* name)
    {
        if (count < FILELIST_MAX_ENTRIES)
        {
            entries[count] = name;
            count++;
        }
    }

    void push_back(const char* name) { push(name); }

    void   clear()  { count = 0; }
    int    size()   const { return count; }
    bool   empty()  const { return count == 0; }

    // data() returns pointer to first XboxString, matching the
    // 'const std::string* file = fileList.data()' pattern at call sites.
    const XboxString* data() const { return entries; }
          XboxString* data()       { return entries; }

    const XboxString& operator[](int i) const { return entries[i]; }
          XboxString& operator[](int i)       { return entries[i]; }
};

#else // PC build

#include <vector>
#include <string>
using namespace std;
typedef vector<string> FileList;

#endif // _XBOX

// ---------------------------------------------------------------------------
// FileUtil namespace
// ---------------------------------------------------------------------------
namespace FileUtil
{
    void readDirectory(const char* dir, const char* ext, FileList& fileList);
    bool makeDirectory(const char* dir);
    void getCurrentDirectory(char* dir);
    void getExecutionDirectory(char* dir);
    void setCurrentDirectory(const char* dir);

    void readSubdirectories(const char* dir, FileList& dirList);

    void getFileNameFromPath(const char* path, char* name, bool includeExt=false);
    void getFilePath(const char* filename, char* path);
    void getFileExtension(const char* filename, char* extension);

    void copyFile(const char* srcFile, const char* dstFile);
    void deleteFile(const char* srcFile);

    bool exists(const char* path);
    bool directoryExists(const char* path, char* outPath = NULL);
    u64  getModifiedTime(const char* path);

    void fixupPath(char* path);
    void convertToOSPath(const char* path, char* pathOS);

    void replaceExtension(const char* srcPath, const char* newExt, char* outPath);
    void stripExtension(const char* srcPath, char* outPath);
}
