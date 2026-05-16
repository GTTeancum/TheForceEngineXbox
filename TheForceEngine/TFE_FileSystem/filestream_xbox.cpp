// filestream_xbox.cpp
// Xbox implementation of FileStream.
// Replaces filestream.cpp for the Xbox build configuration.
//
// Mirrors the canonical Microsoft xquake pattern (xbox/private/test/games/
// xquake/sys_win.c): plain fopen/fread/fwrite/fseek/fclose. The Xbox CRT
// stdio is what shipping retail Xbox titles (xquake, Re-Volt, etc.) use.
//
// IMPORTANT: do NOT pass pathological paths (paths with non-existent
// intermediate directories) to fopen on Xbox - FATX path resolution
// hangs on those. The caller's responsibility is to only probe within
// known-existing search paths. xquake does this via COM_FindFile +
// pre-validated com_searchpaths. TFE main_xbox.cpp must do the same:
// don't blind-probe paths whose parent dir might not exist.

#include "filestream.h"
#include <TFE_Archive/archive.h>
#include <cassert>
#include <cstring>
#include <stdio.h>
#include <stdarg.h>

// Defined in memorystream.cpp - shared scratch buffers used by the
// std::string read/write helpers.
extern u32  s_workBufferU32[1024];
extern char s_workBufferChar[32768];

// Xbox FATX rejects forward slashes. Game code freely builds paths with
// "/"; normalize before any kernel/CRT file call. (Same fix lives in
// fileutil_xbox.cpp for the directoryExists/exists/makeDirectory family.)
static void normalizeSlashes(const char* in, char* out)
{
    if (!in || !out) { if (out) out[0] = 0; return; }
    size_t i = 0;
    for (; in[i] && i < TFE_MAX_PATH - 1; i++)
        out[i] = (in[i] == '/') ? '\\' : in[i];
    out[i] = 0;
}

FileStream::FileStream() : Stream()
{
    m_file    = NULL;
    m_archive = NULL;
    m_mode    = MODE_INVALID;
}

FileStream::~FileStream()
{
    close();
}

bool FileStream::exists(const char* filename)
{
    // xquake pattern: try to fopen, success means it exists.
    if (!filename || !filename[0]) return false;
    char normPath[TFE_MAX_PATH];
    normalizeSlashes(filename, normPath);
    FILE* f = fopen(normPath, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool FileStream::open(const char* filename, AccessMode mode)
{
    if (!filename || !filename[0]) return false;
    if (mode >= MODE_COUNT) return false;

    char normPath[TFE_MAX_PATH];
    normalizeSlashes(filename, normPath);
    static const char* modeStrings[] = { "rb", "wb", "rb+", "ab" };
    m_file = fopen(normPath, modeStrings[mode]);
    m_mode = mode;
    return m_file != NULL;
}

bool FileStream::open(const FilePath* filePath, AccessMode mode)
{
    if (filePath->archive)
    {
        assert(mode == Stream::MODE_READ);
        if (filePath->index < 0) return false;
        m_mode    = mode;
        m_file    = NULL;
        m_archive = filePath->archive;
        return filePath->archive->openFile(filePath->index);
    }
    return open(filePath->path, mode);
}

void FileStream::close()
{
    if (m_file)
    {
        if (m_mode == MODE_WRITE || m_mode == MODE_READWRITE || m_mode == MODE_APPEND)
        {
            fflush(m_file);
        }
        fclose(m_file);
        m_file = NULL;
    }
    else if (m_archive)
    {
        m_archive->closeFile();
        m_archive = NULL;
    }
    m_mode = MODE_INVALID;
}

bool FileStream::seek(s32 offset, Origin origin)
{
    static const s32 forigin[] = { SEEK_SET, SEEK_END, SEEK_CUR };
    if (m_file)   return fseek(m_file, offset, forigin[origin]) == 0;
    if (m_archive) return m_archive->seekFile(offset, forigin[origin]);
    return false;
}

size_t FileStream::getLoc()
{
    if (m_file)    return ftell(m_file);
    if (m_archive) return m_archive->getLocInFile();
    return 0;
}

size_t FileStream::getSize()
{
    if (m_file)
    {
        seek(0, ORIGIN_END);
        size_t sz = getLoc();
        seek(0, ORIGIN_START);
        return sz;
    }
    if (m_archive) return m_archive->getFileLength();
    return 0;
}

bool FileStream::isOpen() const
{
    return m_file != NULL || m_archive != NULL;
}

u32 FileStream::readBuffer(void* ptr, u32 size, u32 count)
{
    assert(m_mode == MODE_READ || m_mode == MODE_READWRITE || m_mode == MODE_APPEND);
    if (m_file)    return (u32)fread(ptr, size, count, m_file) * size;
    if (m_archive) return (u32)m_archive->readFile(ptr, size * count);
    return 0;
}

void FileStream::writeBuffer(const void* ptr, u32 size, u32 count)
{
    assert(m_mode == MODE_WRITE || m_mode == MODE_READWRITE || m_mode == MODE_APPEND);
    if (m_file) fwrite(ptr, size, count, m_file);
}

void FileStream::writeString(const char* fmt, ...)
{
    static char tmpStr[4096];
    assert(m_mode == MODE_WRITE || m_mode == MODE_READWRITE || m_mode == MODE_APPEND);
    if (!m_file) return;

    va_list arg;
    va_start(arg, fmt);
    vsprintf(tmpStr, fmt, arg);
    va_end(arg);

    fwrite(tmpStr, strlen(tmpStr), 1, m_file);
}

void FileStream::flush()
{
    if (m_file) fflush(m_file);
}

void FileStream::readString(std::string* ptr, u32 count)
{
    assert(m_mode == MODE_READ || m_mode == MODE_READWRITE || m_mode == MODE_APPEND);
    assert(count <= 256);
    readBuffer(s_workBufferU32, sizeof(u32), count);
    for (u32 s = 0; s < count; s++)
    {
        assert(s_workBufferU32[s] <= 32768);
        readBuffer(s_workBufferChar, 1, s_workBufferU32[s]);
        s_workBufferChar[s_workBufferU32[s]] = 0;
        ptr[s] = s_workBufferChar;
    }
}

void FileStream::writeString(const std::string* ptr, u32 count)
{
    assert(m_mode == MODE_WRITE || m_mode == MODE_READWRITE || m_mode == MODE_APPEND);
    assert(m_file);
    assert(count <= 256);
    for (u32 s = 0; s < count; s++)
    {
        s_workBufferU32[s] = (u32)ptr[s].length();
    }
    fwrite(s_workBufferU32, sizeof(u32), count, m_file);
    for (u32 s = 0; s < count; s++)
    {
        fwrite(ptr[s].data(), 1, s_workBufferU32[s], m_file);
    }
}

// Static helpers - identical to PC FileStream::readContents.
u32 FileStream::readContents(const char* filePath, void** output)
{
    assert(output);
    u32 size = 0;
    FileStream file;
    if (file.open(filePath, MODE_READ))
    {
        size = (u32)file.getSize();
        *output = realloc(*output, size + 1);
        file.readBuffer(*output, size);
        file.close();
    }
    return size;
}

u32 FileStream::readContents(const char* filePath, void* output, size_t size)
{
    assert(output);
    FileStream file;
    if (file.open(filePath, MODE_READ))
    {
        size_t fileSize = file.getSize();
        size = size <= fileSize ? size : fileSize;
        file.readBuffer(output, (u32)size);
        file.close();
        return u32(size);
    }
    return 0;
}

u32 FileStream::readContents(const FilePath* filePath, void** output)
{
    assert(output);
    u32 size = 0;
    FileStream file;
    if (file.open(filePath, MODE_READ))
    {
        size = (u32)file.getSize();
        *output = realloc(*output, size + 1);
        file.readBuffer(*output, size);
        file.close();
    }
    return size;
}

u32 FileStream::readContents(const FilePath* filePath, void* output, size_t size)
{
    FileStream file;
    if (file.open(filePath, MODE_READ))
    {
        size_t fileSize = file.getSize();
        size = size <= fileSize ? size : fileSize;
        file.readBuffer(output, (u32)size);
        file.close();
        return u32(size);
    }
    return 0;
}
