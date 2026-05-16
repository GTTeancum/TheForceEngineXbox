#pragma once
// replay.h - Xbox build
// All replay/demo recording functionality is stubbed out.
// No STL dependencies.

#include <TFE_System/types.h>
#include <TFE_Input/inputEnum.h>
#include <TFE_FileSystem/paths.h>

namespace TFE_Input
{
    extern char s_replayDir[TFE_MAX_PATH];

    // Stubs - all no-ops or safe default returns.
    inline void   initReplays()                                          {}
    inline bool   isRecording()                                          { return false; }
    inline void   setRecording(bool)                                     {}
    inline bool   isDemoPlayback()                                       { return false; }
    inline void   setDemoPlayback(bool)                                  {}
    inline bool   isReplaySystemLive()                                   { return false; }
    inline bool   sendHudStartMessage()                                  { return false; }
    inline bool   isReplayPaused()                                       { return false; }
    inline void   increaseReplayFrameRate()                              {}
    inline void   decreaseReplayFrameRate()                              {}
    inline bool   startReplayStatus()                                    { return false; }
    inline void   recordReplaySeed()                                     {}
    inline void   restoreReplaySeed()                                    {}
    inline void   recordReplayTime(u64)                                  {}
    inline void   logReplayPosition(int)                                 {}
    inline void   saveTick()                                             {}
    inline void   loadTick()                                             {}
    inline void   sendEndPlaybackMsg()                                   {}
    inline void   sendEndRecordingMsg()                                  {}
    inline void   recordEvent(int, KeyboardCode, bool)                   {}
    inline void   replayEvent()                                          {}
    inline void   startRecording()                                       {}
    inline void   endRecording()                                         {}
    inline void   loadReplay()                                           {}
    inline void   endReplay()                                            {}
    inline void   loadReplayFromPath(const char*)                        {}
    inline void   getAgentPath(char* p)                                  { if(p) p[0]=0; }
    inline void   storePDAPosition(Vec2i)                                {}
    inline Vec2i  getPDAPosition()                                       { Vec2i v; v.x=0; v.z=0; return v; }

    // Counter helpers used by inputMapping
    // On Xbox, inputMapping_resetCounter, inputMapping_getCounter,
    // inputMapping_setReplayCounter, inputMapping_setMaxCounter are
    // stubbed here since replay is disabled. inputMapping_handleInputs
    // is defined in inputMapping.cpp (not stubbed).
    inline void   inputMapping_resetCounter()                            {}
    inline int    inputMapping_getCounter()                              { return 0; }
    inline void   inputMapping_setReplayCounter(int)                    {}
    inline void   inputMapping_setMaxCounter(int)                        {}
}
