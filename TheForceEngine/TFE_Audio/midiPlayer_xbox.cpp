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
        TFE_XboxLogf("MidiPlayer", "init stub");
        TFE_System::logWrite(LOG_MSG, "MidiPlayer", "Xbox stub - MIDI baked to OGG");
        return true;
    }

    void setDeviceType(MidiDeviceType type)        { TFE_XboxLogf("MidiPlayer", "setDeviceType ignored type=%d", (int)type); }
    void selectDeviceOutput(s32 output)            { TFE_XboxLogf("MidiPlayer", "selectDeviceOutput ignored output=%d", output); }
    MidiDeviceType getDeviceType()                 { return MIDI_TYPE_OPL3; }
    void destroy()                                 { TFE_XboxLogf("MidiPlayer", "destroy stub"); }

    TFE_Audio::MidiDevice* getMidiDevice()         { return NULL; }

    const char* getMidiDeviceTypeName(MidiDeviceType /*type*/) { return "Xbox Stub"; }

    // %f avoided - MSVC 2005 vsprintf float formatting hangs on Xbox.
    void setVolume(f32 volume)                     { TFE_XboxLogf("MidiPlayer", "setVolume ignored %d(x1000)", (int)(volume * 1000.0f)); }
    void setMaximumNoteLength(f32 dt)              { TFE_XboxLogf("MidiPlayer", "setMaximumNoteLength ignored %d(x1000)", (int)(dt * 1000.0f)); }
    void sendMessageDirect(u8 a, u8 b, u8 c)       { TFE_XboxLogf("MidiPlayer", "sendMessageDirect ignored %u %u %u", a, b, c); }
    void midiSetCallback(void(*)(void), f64 dt)    { TFE_XboxLogf("MidiPlayer", "midiSetCallback ignored dt=%d(x1000)", (int)(dt * 1000.0)); }
    void midiClearCallback()                       { TFE_XboxLogf("MidiPlayer", "midiClearCallback ignored"); }
    void pauseThread()                             { TFE_XboxLogf("MidiPlayer", "pauseThread ignored"); }
    void resumeThread()                            { TFE_XboxLogf("MidiPlayer", "resumeThread ignored"); }
    void pause()                                   { TFE_XboxLogf("MidiPlayer", "pause ignored"); }
    void resume()                                  { TFE_XboxLogf("MidiPlayer", "resume ignored"); }
    void stopMidiSound()                           { TFE_XboxLogf("MidiPlayer", "stopMidiSound ignored"); }
    void synthesizeMidi(f32*, u32, bool)           {}
    f32  getVolume()                               { return 1.0f; }
}
