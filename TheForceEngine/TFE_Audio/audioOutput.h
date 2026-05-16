#pragma once
#include <TFE_System/types.h>

// OutputDeviceInfo - Xbox uses fixed char array instead of std::string
// to avoid STL dependency in audio headers.
struct OutputDeviceInfo
{
#ifdef _XBOX
    char name[256];
    u32  id;
    OutputDeviceInfo() : id(0) { name[0] = 0; }
    OutputDeviceInfo(const char* n, u32 i) : id(i)
    {
        strncpy(name, n ? n : "", 255);
        name[255] = 0;
    }
#else
    #include <string>
    std::string name;
    u32 id;
#endif
};
