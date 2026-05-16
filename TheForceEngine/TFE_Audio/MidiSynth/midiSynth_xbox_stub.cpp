// midiSynth_xbox_stub.cpp
// Xbox stub for OPL3 and SoundFont MIDI synthesizer devices.
// These are excluded on Xbox - MIDI will be baked to OGG instead.

#ifdef _XBOX
#include "fm4Opl3Device.h"
#include "soundFontDevice.h"

namespace TFE_Audio
{
	// Fm4Opl3Device stubs
	Fm4Opl3Device::~Fm4Opl3Device() {}
	void Fm4Opl3Device::exit() {}
	const char* Fm4Opl3Device::getName() { return "OPL3 (disabled)"; }
	bool Fm4Opl3Device::render(f32*, u32) { return false; }
	bool Fm4Opl3Device::canRender() { return false; }
	void Fm4Opl3Device::message(u8, u8, u8) {}
	void Fm4Opl3Device::message(const u8*, u32) {}
	void Fm4Opl3Device::noteAllOff() {}
	void Fm4Opl3Device::setVolume(f32) {}
	u32  Fm4Opl3Device::getOutputCount() { return 0; }
	void Fm4Opl3Device::getOutputName(s32, char*, u32) {}
	bool Fm4Opl3Device::selectOutput(s32) { return false; }
	s32  Fm4Opl3Device::getActiveOutput() { return -1; }

	// SoundFontDevice stubs
	SoundFontDevice::~SoundFontDevice() {}
	void SoundFontDevice::exit() {}
	const char* SoundFontDevice::getName() { return "SoundFont (disabled)"; }
	bool SoundFontDevice::render(f32*, u32) { return false; }
	bool SoundFontDevice::canRender() { return false; }
	void SoundFontDevice::message(u8, u8, u8) {}
	void SoundFontDevice::message(const u8*, u32) {}
	void SoundFontDevice::noteAllOff() {}
	void SoundFontDevice::setVolume(f32) {}
	u32  SoundFontDevice::getOutputCount() { return 0; }
	void SoundFontDevice::getOutputName(s32, char*, u32) {}
	bool SoundFontDevice::selectOutput(s32) { return false; }
	s32  SoundFontDevice::getActiveOutput() { return -1; }
}
#endif // _XBOX
