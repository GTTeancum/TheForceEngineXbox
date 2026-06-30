#include "midiPlayer.h"
#include "midiDevice.h"
#include "audioDevice.h"
#ifdef BUILD_SYSMIDI
#include "systemMidiDevice.h"
#endif
#include <TFE_Asset/gmidAsset.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#ifndef _XBOX
#include <TFE_FrontEndUI/console.h>
#endif
#include <TFE_Audio/MidiSynth/soundFontDevice.h>
#include <TFE_Audio/MidiSynth/fm4Opl3Device.h>
#include <assert.h>

#ifdef _XBOX
// Xbox uses Win32 CRITICAL_SECTION + CreateThread instead of SDL primitives.
// Keep the upstream threading model: a background midi update thread that
// processes the command buffer and ticks the iMuse callback at fixed
// intervals. Xbox XDK has full Win32 thread / sync APIs.
#include <xtl.h>
typedef CRITICAL_SECTION* TFE_MidiMutex_t;
static CRITICAL_SECTION s_midiThreadCS;
static CRITICAL_SECTION s_deviceChangeCS;
static inline TFE_MidiMutex_t TFE_Midi_CreateMutex(CRITICAL_SECTION* cs) { InitializeCriticalSection(cs); return cs; }
static inline void TFE_Midi_DestroyMutex(TFE_MidiMutex_t m) { if (m) DeleteCriticalSection(m); }
static inline void TFE_Midi_Lock(TFE_MidiMutex_t m)         { if (m) EnterCriticalSection(m); }
static inline void TFE_Midi_Unlock(TFE_MidiMutex_t m)       { if (m) LeaveCriticalSection(m); }
#define SDL_mutex             CRITICAL_SECTION
#define SDL_CreateMutex()     TFE_Midi_CreateMutex(&s_midiThreadCS)
#define SDL_LockMutex(m)      TFE_Midi_Lock(m)
#define SDL_UnlockMutex(m)    TFE_Midi_Unlock(m)
#define SDL_DestroyMutex(m)   TFE_Midi_DestroyMutex(m)
// We have two mutexes upstream (s_midiThreadMutex and s_deviceChangeMutex).
// Provide a second creator that uses the second CS.
#define SDL_CreateMutex2()    TFE_Midi_CreateMutex(&s_deviceChangeCS)
// Thread - upstream uses SDL_Thread (opaque struct ptr); we use Win32
// HANDLE under the hood and let `SDL_Thread*` reduce to `void*` (defined
// below) so the upstream `static SDL_Thread* s_thread` assignments work
// without a cast.
typedef DWORD                 (WINAPI* XboxMidiThreadFn)(LPVOID);
// SDL_CreateThread expects (int (*)(void*), name, data); shim it.
static inline HANDLE TFE_Midi_CreateThread(int (*fn)(void*), void* data)
{
    struct Trampoline {
        int (*fn)(void*);
        void* data;
        static DWORD WINAPI run(LPVOID p) {
            Trampoline* t = (Trampoline*)p;
            int r = t->fn(t->data);
            delete t;
            return (DWORD)r;
        }
    };
    Trampoline* t = new Trampoline;
    if (!t) return NULL;
    t->fn = fn; t->data = data;
    HANDLE thread = CreateThread(NULL, 0, &Trampoline::run, t, 0, NULL);
    if (!thread)
    {
        delete t;
    }
    return thread;
}
#define SDL_CreateThread(fn, name, data) TFE_Midi_CreateThread((fn), (data))
#define SDL_WaitThread(thr, statusPtr)   do { if (thr) { WaitForSingleObject((thr), INFINITE); CloseHandle((thr)); } (void)(statusPtr); } while(0)
// atomic_bool - upstream uses <atomic>. MSVC 2005 doesn't have it; use a
// volatile LONG with interlocked ops.
struct XboxAtomicBool {
    LONG v;
    XboxAtomicBool() : v(0) {}
    void store(bool b) { InterlockedExchange(&v, b ? 1 : 0); }
    bool load()        { return InterlockedCompareExchange(&v, 0, 0) != 0; }
};
#define atomic_bool XboxAtomicBool
// SDL_Thread is upstream's opaque struct; treat as void on Xbox so
// `SDL_Thread*` is just `void*` and assigns cleanly from HANDLE.
#define SDL_Thread void
#else
#include <SDL_mutex.h>
#include <SDL_thread.h>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#undef min
#undef max
#endif
#endif // _XBOX

using namespace TFE_Audio;

namespace TFE_MidiPlayer
{
	enum MidiPlayerCmd
	{
		MIDI_PAUSE,
		MIDI_RESUME,
		MIDI_CHANGE_VOL,
		MIDI_STOP_NOTES,
		MIDI_COUNT
	};

	struct MidiCmd
	{
		MidiPlayerCmd cmd;
		f32 newVolume;
	};

	enum { MAX_MIDI_CMD = 256 };
	static MidiCmd s_midiCmdBuffer[MAX_MIDI_CMD];
	static u32 s_midiCmdCount = 0;
	static f64 s_maxNoteLength = 16.0;		// defaults to 16 seconds.

	struct MidiCallback
	{
#ifdef _XBOX
		// C++03 (MSVC 2005) - no non-static member initializers.
		void(*callback)(void);
		f64 timeStep;
		f64 accumulator;
		MidiCallback() : callback(NULL), timeStep(0.0), accumulator(0.0) {}
#else
		void(*callback)(void) = nullptr;	// callback function to call.
		f64 timeStep = 0.0;					// delay between calls, this acts like an interrupt handler.
		f64 accumulator = 0.0;				// current accumulator.
#endif
	};
		
	static const f32 c_musicVolumeScale = 0.75f;
	static f32 s_masterVolume = 1.0f;
	static f32 s_masterVolumeScaled = s_masterVolume * c_musicVolumeScale;
	static SDL_Thread* s_thread = nullptr;
	static bool s_tPaused = false;

	static atomic_bool s_runMusicThread;
	static u8 s_channelSrcVolume[MIDI_CHANNEL_COUNT] = { 0 };
	static SDL_mutex* s_midiThreadMutex = nullptr;
	static SDL_mutex* s_deviceChangeMutex = nullptr;

	static MidiDevice* s_midiDevice = nullptr;
#ifdef _XBOX
	// MidiCallback has a user-defined ctor on Xbox (C++03) so the default-
	// initializer form below would be ill-formed. Default-construct instead.
	static MidiCallback s_midiCallback;
#else
	static MidiCallback s_midiCallback = {};
#endif

#ifdef _XBOX
	// Fixed-size scratch buffer for the MIDI synth. Sized to hold one full
	// audio chunk (1024 stereo samples = 2048 floats = 8 KB). audioCallback
	// passes frames = bufferSize/(channels*sizeof(f32)) = 1024 with our
	// 8 KB chunk - times 2 channels = 2048 floats. Allocate 4x that as
	// headroom in case the chunk size ever grows.
	enum { XBOX_MIDI_SAMPLE_BUF_FLOATS = 8192 };
	static f32  s_sampleBuffer[XBOX_MIDI_SAMPLE_BUF_FLOATS];
	static f32* s_sampleBufferPtr = s_sampleBuffer;
#else
	static std::vector<f32> s_sampleBuffer;
	static f32* s_sampleBufferPtr = nullptr;
#endif

	// Hanging note detection.
	struct Instrument
	{
		u32 channelMask;
		f64 time[MIDI_CHANNEL_COUNT];
	};
	static Instrument s_instrOn[MIDI_INSTRUMENT_COUNT] = { 0 };
	static f64 s_curNoteTime = 0.0;

	int midiUpdateFunc(void* userData);
	void stopAllNotes();
	void changeVolume();
	void allocateMidiDevice(MidiDeviceType type);

	// Console Functions
#ifndef _XBOX
	void setMusicVolumeConsole(const ConsoleArgList& args);
	void getMusicVolumeConsole(const ConsoleArgList& args);
#endif

	static const char* c_midiDeviceTypes[] =
	{
		"SF2 Synthesized Midi", // MIDI_TYPE_SF2
		"OPL3 Synthesized Midi",// MIDI_TYPE_OPL3
#ifdef BUILD_SYSMIDI
		"System Midi",		// MIDI_TYPE_SYSTEM
#endif
	};

	bool init(s32 midiDeviceIndex, MidiDeviceType type)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_MidiPlayer::init");
		bool res = false;

		s_midiThreadMutex = SDL_CreateMutex();
		if (!s_midiThreadMutex)
		{
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot initialize midi thread mutex");
			return false;
		}

#ifdef _XBOX
		s_deviceChangeMutex = SDL_CreateMutex2();
#else
		s_deviceChangeMutex = SDL_CreateMutex();
#endif
		if (!s_deviceChangeMutex)
		{
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot initialize device change mutex");
			SDL_DestroyMutex(s_midiThreadMutex);
			s_midiThreadMutex = nullptr;
			return false;
		}

		SDL_LockMutex(s_deviceChangeMutex);
		{
			allocateMidiDevice(type);
			if (s_midiDevice)
			{
				res = true;
				if (!s_midiDevice->selectOutput(midiDeviceIndex))
				{
					if (!s_midiDevice->selectOutput(0))
					{
						TFE_System::logWrite(LOG_ERROR, "Midi", "Cannot load soundfont '%s'.", "SoundFonts/SYNTHGM.sf2");
						res = false;
					}
				}
#ifdef _XBOX
				TFE_System::logWrite(LOG_MSG, "Midi", "device created type=%d canRender=%d",
					(int)s_midiDevice->getType(), s_midiDevice->canRender() ? 1 : 0);
#endif
			}
#ifdef _XBOX
			else
			{
				TFE_System::logWrite(LOG_ERROR, "Midi", "allocateMidiDevice returned NULL");
			}
#endif
		}
		SDL_UnlockMutex(s_deviceChangeMutex);

		if (!res)
		{
			delete s_midiDevice;
			s_midiDevice = nullptr;
			SDL_DestroyMutex(s_deviceChangeMutex);
			s_deviceChangeMutex = nullptr;
			SDL_DestroyMutex(s_midiThreadMutex);
			s_midiThreadMutex = nullptr;
			return false;
		}

		s_runMusicThread.store(true);
		s_thread = SDL_CreateThread(midiUpdateFunc, "TFE_MidiThread", nullptr);
		if (!s_thread)
		{
			TFE_System::logWrite(LOG_ERROR, "Midi", "cannot create Midi Thread!");
			s_runMusicThread.store(false);
			delete s_midiDevice;
			s_midiDevice = nullptr;
			SDL_DestroyMutex(s_deviceChangeMutex);
			s_deviceChangeMutex = nullptr;
			SDL_DestroyMutex(s_midiThreadMutex);
			s_midiThreadMutex = nullptr;
			return false;
		}
#ifdef _XBOX
		else
		{
			TFE_System::logWrite(LOG_MSG, "Midi", "midi thread created (handle=%p)", s_thread);
		}
#endif

#ifndef _XBOX
		CCMD("setMusicVolume", setMusicVolumeConsole, 1, "Sets the music volume, range is 0.0 to 1.0");
		CCMD("getMusicVolume", getMusicVolumeConsole, 0, "Get the current music volume where 0 = silent, 1 = maximum.");
#endif

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		setVolume(soundSettings->musicVolume);
		setMaximumNoteLength();

		return true;
	}

	void destroy()
	{
		s32 i;

		TFE_System::logWrite(LOG_MSG, "MidiPlayer", "Shutdown");
		// Destroy the thread before shutting down the Midi Device.
		s_runMusicThread.store(false);
		SDL_WaitThread(s_thread, &i);
		s_thread = nullptr;

		delete s_midiDevice;
		s_midiDevice = nullptr;

		if (s_midiThreadMutex)
		{
			SDL_DestroyMutex(s_midiThreadMutex);
			s_midiThreadMutex = nullptr;
		}
		if (s_deviceChangeMutex)
		{
			SDL_DestroyMutex(s_deviceChangeMutex);
			s_deviceChangeMutex = nullptr;
		}
	}

	MidiDevice* getMidiDevice()
	{
		return s_midiDevice;
	}

	const char* getMidiDeviceTypeName(MidiDeviceType type)
	{
		return c_midiDeviceTypes[type];
	}

	void setDeviceType(MidiDeviceType type)
	{
		SDL_LockMutex(s_deviceChangeMutex);
		{
			allocateMidiDevice(type);
			if (s_midiDevice)
			{
				if (!s_midiDevice->selectOutput(-1))	// -1 will select the default.
				{
					TFE_System::logWrite(LOG_ERROR, "Midi", "Cannot select midi output.");
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);
	}

	void selectDeviceOutput(s32 output)
	{
		SDL_LockMutex(s_deviceChangeMutex);
		{
			if (s_midiDevice)
			{
				if (!s_midiDevice->selectOutput(output))
				{
					TFE_System::logWrite(LOG_ERROR, "Midi", "Cannot select midi output.");
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);
	}

	MidiDeviceType getDeviceType()
	{
		if (s_midiDevice)
		{
			return s_midiDevice->getType();
		}
		return MIDI_TYPE_DEFAULT;
	}

	//////////////////////////////////////////////////
	// Command Buffer
	//////////////////////////////////////////////////
	MidiCmd* midiAllocCmd()
	{
		if (s_midiCmdCount >= MAX_MIDI_CMD) { return nullptr; }
		MidiCmd* cmd = &s_midiCmdBuffer[s_midiCmdCount];
		s_midiCmdCount++;
		return cmd;
	}

	void midiClearCmdBuffer()
	{
		s_midiCmdCount = 0;
	}

	//////////////////////////////////////////////////
	// Command Interface
	//////////////////////////////////////////////////
	void setVolume(f32 volume)
	{
		SDL_LockMutex(s_midiThreadMutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_CHANGE_VOL;
			midiCmd->newVolume = volume;
		}
		SDL_UnlockMutex(s_midiThreadMutex);
	}
	
	// Set the length in seconds that a note is allowed to play for in seconds.
	void setMaximumNoteLength(f32 dt)
	{
		s_maxNoteLength = f64(dt);
	}

	void pauseThread()
	{
		if (!s_tPaused && s_midiThreadMutex)
		{
			SDL_LockMutex(s_midiThreadMutex);
			s_tPaused = true;
		}
	}

	void resumeThread()
	{
		if (s_tPaused && s_midiThreadMutex)
		{
			SDL_UnlockMutex(s_midiThreadMutex);
			s_tPaused = false;
		}
	}

	void pause()
	{
		SDL_LockMutex(s_midiThreadMutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_PAUSE;
		}
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	void resume()
	{
		SDL_LockMutex(s_midiThreadMutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_RESUME;
		}
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	void stopMidiSound()
	{
		SDL_LockMutex(s_midiThreadMutex);
		MidiCmd* midiCmd = midiAllocCmd();
		if (midiCmd)
		{
			midiCmd->cmd = MIDI_STOP_NOTES;
		}
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	void synthesizeMidi(f32* buffer, u32 stereoSampleCount, bool updateBuffer)
	{
		// In some cases, such as when using the System Midi Device, the midi audio is generated externally so
		// rendering is not required.
		SDL_LockMutex(s_deviceChangeMutex);  // Make sure we don't synthesize when the device is being changed.
		if (s_midiDevice && s_midiDevice->canRender())
		{
			// Stereo samples -> actual samples.
			const s32 linearSampleCount = (s32)stereoSampleCount * 2;
#ifdef _XBOX
			// Fixed scratch buffer. Clamp request so the synth never writes
			// past the end. If a caller ever asks for more than the static
			// buffer holds, drop the request (silent for that chunk) rather
			// than risk corruption.
			if (linearSampleCount > XBOX_MIDI_SAMPLE_BUF_FLOATS)
			{
				SDL_UnlockMutex(s_deviceChangeMutex);
				return;
			}
#else
			// Make sure the sample buffer is large enough, this should only happen once.
			if (linearSampleCount > (s32)s_sampleBuffer.size() || !s_sampleBufferPtr)
			{
				s_sampleBuffer.resize(linearSampleCount);
				s_sampleBufferPtr = s_sampleBuffer.data();
			}
#endif

			// The midi device takes the number of stereo samples.
			s_midiDevice->render(s_sampleBufferPtr, stereoSampleCount);
			// Accumulate midi samples with existing audio samples (from soundFX).
			if (updateBuffer)
			{
				for (s32 i = 0; i < linearSampleCount; i++)
				{
					buffer[i] += s_sampleBufferPtr[i];
				}
			}
		}
		SDL_UnlockMutex(s_deviceChangeMutex);
	}

	f32 getVolume()
	{
		return s_masterVolume;
	}

	void midiSetCallback(void(*callback)(void), f64 timeStep)
	{
		SDL_LockMutex(s_midiThreadMutex);
		s_midiCallback.callback = callback;
		s_midiCallback.timeStep = timeStep;
		s_midiCallback.accumulator = 0.0;

		for (u32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
		{
			s_channelSrcVolume[i] = CHANNEL_MAX_VOLUME;
		}
		changeVolume();
		SDL_UnlockMutex(s_midiThreadMutex);
#ifdef _XBOX
		// One-shot: log the first time iMuse (or anything else) registers a
		// callback with us. If this never appears, iMuse's digital sound
		// init isn't running and music has no driver.
		static bool s_loggedFirstSet = false;
		if (!s_loggedFirstSet)
		{
			s_loggedFirstSet = true;
			TFE_System::logWrite(LOG_MSG, "Midi", "midiSetCallback registered (cb=%p timeStep=%d us)",
				callback, (int)(timeStep * 1000000.0));
		}
#endif
	}

	void midiClearCallback()
	{
		SDL_LockMutex(s_midiThreadMutex);
		s_midiCallback.callback = nullptr;
		s_midiCallback.timeStep = 0.0;
		s_midiCallback.accumulator = 0.0;
		SDL_UnlockMutex(s_midiThreadMutex);
	}

	//////////////////////////////////////////////////
	// Internal
	//////////////////////////////////////////////////
	void changeVolume()
	{
		if (s_midiDevice && s_midiDevice->hasGlobalVolumeCtrl())
		{
			s_midiDevice->setVolume(s_masterVolumeScaled);
		}
		else if (s_midiDevice)
		{
			for (u32 i = 0; i < MIDI_CHANNEL_COUNT; i++)
			{
				s_midiDevice->message(MID_CONTROL_CHANGE + i, MID_VOLUME_MSB, u8(s_channelSrcVolume[i] * s_masterVolumeScaled));
			}
		}
	}

	void stopAllNotes()
	{
		// Some devices don't support "all notes off" - so do it manually.
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			// Skip any instruments not being used.
			if (!s_instrOn[i].channelMask) { continue; }

			// Look for used channels.
			for (u32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
			{
				const u32 channelMask = 1u << c;
				if (s_instrOn[i].channelMask & channelMask)
				{
					// Turn off the note.
					if (s_midiDevice) { s_midiDevice->message(MID_NOTE_OFF | c, i); }

					// Reset the instrument channel information.
					s_instrOn[i].channelMask &= ~channelMask;
					s_instrOn[i].time[c] = 0.0;
				}
			}
		}

		if (s_midiDevice) { s_midiDevice->noteAllOff(); }
		memset(s_instrOn, 0, sizeof(Instrument) * MIDI_INSTRUMENT_COUNT);
		s_curNoteTime = 0.0;
	}
		
	void sendMessageDirect(u8 type, u8 arg1, u8 arg2)
	{
		u8 msg[] = { type, arg1, arg2 };
		u8 msgType = (type & 0xf0);
		u8 len;

		len = (msgType == MID_PROGRAM_CHANGE) ? 2 : 3;

		if (msgType == MID_CONTROL_CHANGE && arg1 == MID_VOLUME_MSB && s_midiDevice && !s_midiDevice->hasGlobalVolumeCtrl())
		{
			const s32 channelIndex = type & 0x0f;
			s_channelSrcVolume[channelIndex] = arg2;
			msg[2] = u8(s_channelSrcVolume[channelIndex] * s_masterVolumeScaled);
		}
#ifdef _XBOX
		// One-shot: log the first NOTE_ON we ever receive so we can tell
		// whether iMuse is actually requesting music playback.
		if (msgType == MID_NOTE_ON && arg2 != 0)
		{
			static bool s_loggedFirstNote = false;
			if (!s_loggedFirstNote)
			{
				s_loggedFirstNote = true;
				TFE_System::logWrite(LOG_MSG, "Midi", "First NOTE_ON received: ch=%u key=%u vel=%u device=%p",
					(unsigned)(type & 0x0f), (unsigned)arg1, (unsigned)arg2, s_midiDevice);
			}
		}
#endif
		if (s_midiDevice) { s_midiDevice->message(msg, len); }

		// Record currently playing instruments and the note-on times.
		if (msgType == MID_NOTE_OFF || msgType == MID_NOTE_ON)
		{
			const u8 instr   = arg1;
			const u8 channel = type & 0x0f;
			if (msgType == MID_NOTE_OFF || (msgType == MID_NOTE_ON && arg2 == 0))	// note on + velocity = 0 is the same as note off.
			{
				s_instrOn[instr].channelMask  &= ~(1 << channel);
				s_instrOn[instr].time[channel] = 0.0;
			}
			else  // MID_NOTE_ON
			{
				s_instrOn[instr].channelMask  |= (1 << channel);
				s_instrOn[instr].time[channel] = s_curNoteTime;
			}
		}
	}
	
	void detectHangingNotes()
	{
		for (s32 i = 0; i < MIDI_INSTRUMENT_COUNT; i++)
		{
			// Skip any instruments not being used.
			if (!s_instrOn[i].channelMask) { continue; }

			// Look for used channels.
			for (u32 c = 0; c < MIDI_CHANNEL_COUNT; c++)
			{
				const u32 channelMask = 1u << c;
				if ((s_instrOn[i].channelMask & channelMask) && (s_curNoteTime - s_instrOn[i].time[c] > s_maxNoteLength))
				{
					// Turn off the note.
					sendMessageDirect(MID_NOTE_OFF | c, i);

					// Reset the instrument channel information.
					s_instrOn[i].channelMask &= ~channelMask;
					s_instrOn[i].time[c] = 0.0;
				}
			}
		}
	}

	// Thread Function
	int midiUpdateFunc(void* userData)
	{
		bool runThread  = true;
		bool wasPlaying = false;
		bool isPlaying  = false;
		bool isPaused = false;
		s32 loopStart = -1;
		u64 localTime = 0;
		u64 localTimeCallback = 0;
		f64 dt = 0.0;
#ifdef _XBOX
		TFE_System::logWrite(LOG_MSG, "Midi", "midi thread entered loop");
#endif
		while (runThread)
		{
			SDL_LockMutex(s_midiThreadMutex);
						
			// Read from the command buffer.
			MidiCmd* midiCmd = s_midiCmdBuffer;
			for (u32 i = 0; i < s_midiCmdCount; i++, midiCmd++)
			{
				switch (midiCmd->cmd)
				{
					case MIDI_PAUSE:
					{
						localTimeCallback = 0;
						isPaused = true;
						stopAllNotes();
					} break;
					case MIDI_RESUME:
					{
						isPaused = false;
					} break;
					case MIDI_CHANGE_VOL:
					{
						s_masterVolume = midiCmd->newVolume;
						s_masterVolumeScaled = s_masterVolume * c_musicVolumeScale;
						changeVolume();
					} break;
					case MIDI_STOP_NOTES:
					{
						stopAllNotes();
						// Reset callback time.
						localTimeCallback = 0;
						s_midiCallback.accumulator = 0.0;
					} break;
				}
			}
			s_midiCmdCount = 0;

			// Process the midi callback, if it exists.
			if (s_midiCallback.callback && !isPaused)
			{
#ifdef _XBOX
				static bool s_loggedFirstCallback = false;
				if (!s_loggedFirstCallback)
				{
					s_loggedFirstCallback = true;
					TFE_System::logWrite(LOG_MSG, "Midi", "midi thread firing first callback (timeStep=%d us)",
						(int)(s_midiCallback.timeStep * 1000000.0));
				}
#endif
				s_midiCallback.accumulator += TFE_System::updateThreadLocal(&localTimeCallback);
				while (s_midiCallback.callback && s_midiCallback.accumulator >= s_midiCallback.timeStep)
				{
					s_midiCallback.callback();
					s_midiCallback.accumulator -= s_midiCallback.timeStep;
					s_curNoteTime += s_midiCallback.timeStep;
				}

				// Check for hanging notes.
				detectHangingNotes();
			}

			SDL_UnlockMutex(s_midiThreadMutex);
			runThread = s_runMusicThread.load();
#ifdef _XBOX
			// Yield. Xbox has a single CPU and the upstream SDL build relies
			// on SDL_mutex / OS scheduler quirks to keep this loop from
			// burning a core. Raw CRITICAL_SECTION on Xbox returns instantly
			// when uncontested, so without a sleep this loop pins the CPU
			// and starves the render+audio-pump main thread. The midi
			// callback timeStep is ~6ms (iMuse default), so polling at 1kHz
			// is still 6x faster than needed.
			Sleep(1);
#endif
		};
		
		return 0;
	}

#ifndef _XBOX
	// Console Functions (no console UI on Xbox)
	void setMusicVolumeConsole(const ConsoleArgList& args)
	{
		if (args.size() < 2) { return; }
		f32 volume = TFE_Console::getFloatArg(args[1]);
		setVolume(volume);

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		soundSettings->musicVolume = volume;
		TFE_Settings::writeToDisk();
	}

	void getMusicVolumeConsole(const ConsoleArgList& args)
	{
		char res[256];
		sprintf(res, "Sound Volume: %2.3f", s_masterVolume);
		TFE_Console::addToHistory(res);
	}
#endif

	void allocateMidiDevice(MidiDeviceType type)
	{
		if (s_midiDevice && s_midiDevice->getType() == type) { return; }
		delete s_midiDevice;
		s_midiDevice = nullptr;

		switch (type)
		{
#ifdef BUILD_SYSMIDI
			case MIDI_TYPE_SYSTEM:
				s_midiDevice = new SystemMidiDevice();
				break;
#endif
			case MIDI_TYPE_SF2:
				s_midiDevice = new SoundFontDevice();
				break;
			case MIDI_TYPE_OPL3:
				s_midiDevice = new Fm4Opl3Device();
				break;
			default:
				TFE_System::logWrite(LOG_ERROR, "Midi", "Invalid midi type selected: %d", (s32)type);
				s_midiDevice = new Fm4Opl3Device();
				break;
		}
	}
}
