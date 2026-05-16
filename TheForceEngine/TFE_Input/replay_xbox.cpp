// replay_xbox.cpp
// Provides storage for the one non-inline symbol declared in replay.h (Xbox build).
#include "replay.h"
#include <string.h>

namespace TFE_Input
{
    char s_replayDir[TFE_MAX_PATH] = { 0 };
}
