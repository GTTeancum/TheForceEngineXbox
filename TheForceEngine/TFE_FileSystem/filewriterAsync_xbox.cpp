// filewriterAsync_xbox.cpp
// Xbox implementation of FileWriterAsync.
// Replaces filewriterAsync.cpp for the Xbox build.
// Uses synchronous WriteFile since async IO via WriteFileEx requires
// alertable thread waits which complicate the architecture.
// The caller callback is invoked synchronously on success.
// This is acceptable since filewriterAsync has no call sites in gameplay code.

#include "filewriterAsync.h"
#include <TFE_System/system.h>
#include <xtl.h>
#include <string.h>

namespace FileWriterAsync
{
    bool writeFileToDisk(const char* path, u8* data, size_t dataSize,
                         FileWriteCompletionCallback completionCallback,
                         void* userData)
    {
        TFE_XboxLogf("AsyncFileWrite", "writeFileToDisk path=%s size=%u callback=%p",
            path ? path : "", (u32)dataSize, completionCallback);

        if (!path || !data || dataSize == 0)
        {
            TFE_XboxLogf("AsyncFileWrite", "invalid write request");
            return false;
        }

        HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            TFE_XboxLogf("AsyncFileWrite", "CreateFile failed path=%s err=%lu", path, GetLastError());
            TFE_System::logWrite(LOG_ERROR, "AsyncFileWrite",
                "Cannot create file: %s", path);
            return false;
        }

        DWORD written = 0;
        BOOL ok = WriteFile(hFile, data, (DWORD)dataSize, &written, NULL);
        CloseHandle(hFile);

        if (!ok || written != (DWORD)dataSize)
        {
            TFE_XboxLogf("AsyncFileWrite", "WriteFile failed path=%s written=%lu expected=%u err=%lu",
                path, written, (u32)dataSize, ok ? 0 : GetLastError());
            TFE_System::logWrite(LOG_ERROR, "AsyncFileWrite",
                "Write failed for: %s", path);
            if (completionCallback)
                completionCallback(0, userData, 1);
            return false;
        }

        if (completionCallback)
            completionCallback(written, userData, 0);

        TFE_XboxLogf("AsyncFileWrite", "write complete path=%s bytes=%lu", path, written);
        return true;
    }
}
