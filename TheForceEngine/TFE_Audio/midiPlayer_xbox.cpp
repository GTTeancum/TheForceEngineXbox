// midiPlayer_xbox.cpp
// Xbox stub for TFE_MidiPlayer.
// MIDI has been baked to OGG audio files; the iMuse MIDI sequencer sends
// messages here which are safely ignored. OGG playback is handled by
// TFE_Audio directly.
// Exclude midiPlayer.cpp, systemMidiDevice.cpp, RtMidi.cpp from Xbox build.

#include "midiPlayer.h"
#include "midiDevice.h"
#include <TFE_System/system.h>

namespace TFE_MidiPlayer
{
    bool init(s32 /*midiDeviceIndex*/, MidiDeviceType /*type*/)
    {
        TFE_System::logWrite(LOG_MSG, "MidiPlayer", "Xbox stub - MIDI not yet wired (synth pending)");
        return true;
    }

    // All-stub MIDI path. Logged once at init; per-message calls are silent
    // to avoid log spam (iMuse emits hundreds of CC/program-change events
    // per level start). When fm4Opl3 is wired up, these become real calls.
    void setDeviceType(MidiDeviceType)             {}
    void selectDeviceOutput(s32)                   {}
    MidiDeviceType getDeviceType()                 { return MIDI_TYPE_OPL3; }
    void destroy()                                 {}

    TFE_Audio::MidiDevice* getMidiDevice()         { return NULL; }

    const char* getMidiDeviceTypeName(MidiDeviceType /*type*/) { return "Xbox Stub"; }

    void setVolume(f32)                            {}
    void setMaximumNoteLength(f32)                 {}
    void sendMessageDirect(u8, u8, u8)             {}
    void midiSetCallback(void(*)(void), f64)       {}
    void midiClearCallback()                       {}
    void pauseThread()                             {}
    void resumeThread()                            {}
    void pause()                                   {}
    void resume()                                  {}
    void stopMidiSound()                           {}
    void synthesizeMidi(f32*, u32, bool)           {}
    f32  getVolume()                               { return 1.0f; }
}
