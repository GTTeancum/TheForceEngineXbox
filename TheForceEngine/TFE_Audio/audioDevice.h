#pragma once
#include <TFE_System/types.h>
#include <TFE_FileSystem/paths.h>
#include "audioOutput.h"

// Audio callback type - matches SDL_AudioCallback signature so audioSystem.cpp
// needs no changes to the callback itself.
// On Xbox this is called from our DirectSound notify thread.
typedef void (*TFE_AudioCallback)(void* userdata, unsigned char* stream, int len);

namespace TFE_AudioDevice
{
    bool init(u32 audioFrameSize = 256u, s32 deviceId = -1, bool useNullDevice = false);
    void destroy();

    bool startOutput(TFE_AudioCallback callback, void* userData = 0,
                     u32 channels = 2, u32 sampleRate = 44100);
    void stopOutput();

    s32 getDefaultOutputDevice();
    s32 getOutputDeviceId();
    s32 getOutputDeviceCount();

    const OutputDeviceInfo* getOutputDeviceList(s32& count, s32& curOutput);

#ifdef _XBOX
    // Xbox runs a lightweight DirectSound pump thread. Main-loop calls are
    // still allowed as a fallback/catch-up path.
    void pump();
#endif
}
