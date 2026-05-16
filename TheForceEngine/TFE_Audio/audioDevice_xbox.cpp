// audioDevice_xbox.cpp
// Xbox DirectSound8 audio device - polled streaming model.
//
// Replaces the previous notification-thread implementation, which crashed in
// CXBX-R's HLE (NULL deref in CDirectSoundBuffer_Lock during the dedicated
// audio thread's notify-event loop). The polled model is what every working
// retail Xbox audio path uses (xquake, OpenJKDF2, Mercs):
//
//   * One stereo 16-bit 44.1kHz secondary buffer, looping.
//   * No notification events, no audio thread.
//   * Each main-loop tick calls pump() which polls GetCurrentPosition,
//     computes how much of the ring buffer has been consumed since last
//     pump, and refills that space by calling the TFE audio callback.
//   * Playback only starts (DSBPLAY_LOOPING) once enough data is queued
//     to avoid an immediate underrun.
//
// Architecture lifted from OpenJKDF2 stdSound_xbox.c (XboxPCMStream).

#include "audioDevice.h"
#include <TFE_System/system.h>
#include <xtl.h>
#include <dsound.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_CHANNELS          2
#define AUDIO_BITS              16
#define AUDIO_BYTES_PER_SAMPLE  (AUDIO_BITS / 8)
#define AUDIO_BLOCK_ALIGN       (AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE)   // 4 bytes (s16 stereo)
#define AUDIO_FRAME_SAMPLES     1024                                        // must match audioSystem.cpp AUDIO_FRAME_SIZE
// The TFE audio callback writes 32-bit FLOAT stereo samples (PC uses SDL
// float audio). Each chunk: AUDIO_FRAME_SAMPLES * 2 channels * sizeof(f32) =
// 8192 bytes of float data, which we then convert to s16 PCM (4096 bytes)
// for DirectSound. Sizing the mix buffer too small overruns it during the
// callback's upsample step (this caused the previous null-deref crash).
#define AUDIO_CHUNK_FLT_BYTES   (AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * (s32)sizeof(f32))   // 8192
#define AUDIO_CHUNK_PCM_BYTES   (AUDIO_FRAME_SAMPLES * AUDIO_BLOCK_ALIGN)                   // 4096
#define AUDIO_BUFFER_BYTES      (AUDIO_CHUNK_PCM_BYTES * 8)                                 // 32 KB ring (~186 ms)

namespace TFE_AudioDevice
{
    static IDirectSound8*        s_ds            = NULL;
    static IDirectSoundBuffer*   s_dsBuf         = NULL;

    static TFE_AudioCallback     s_callback      = NULL;
    static void*                 s_callbackData  = NULL;
    static u32                   s_frameSize     = AUDIO_FRAME_SAMPLES;
    static bool                  s_nullDevice    = false;

    // Polled-stream state (mirrors OpenJKDF2 XboxPCMStream).
    static DWORD                 s_writePos      = 0;
    static DWORD                 s_queuedBytes   = 0;
    static DWORD                 s_prefillBytes  = 0;
    static DWORD                 s_lastPlayPos   = 0;
    static int                   s_playPosValid  = 0;
    static int                   s_started       = 0;

    static OutputDeviceInfo      s_deviceList[2];
    static s32                   s_deviceCount   = 0;
    static s32                   s_outputDevice  = 0;

    // Scratch buffers: callback fills s_mixBufF32 with floats, we then
    // convert to s16 PCM in s_mixBufS16 before writing to the ring.
    static f32                   s_mixBufF32[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
    static s16                   s_mixBufS16[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];

    // Convert N stereo float samples (range ~[-1,+1]) to s16 PCM with clamp.
    static void floatToS16(const f32* src, s16* dst, u32 sampleCount)
    {
        for (u32 i = 0; i < sampleCount; i++)
        {
            f32 v = src[i] * 32767.0f;
            if      (v >  32767.0f) v =  32767.0f;
            else if (v < -32768.0f) v = -32768.0f;
            dst[i] = (s16)v;
        }
    }

    // ---------------------------------------------------------------------------
    // Compute distance from `from` to `to` in a circular buffer of `size` bytes.
    static DWORD streamDistance(DWORD from, DWORD to, DWORD size)
    {
        return (to >= from) ? (to - from) : (size - from + to);
    }

    // Update s_queuedBytes by polling the play cursor.
    static void streamUpdateQueued()
    {
        if (!s_dsBuf || !s_started) return;

        DWORD play = 0, write = 0;
        if (FAILED(s_dsBuf->GetCurrentPosition(&play, &write))) return;

        if (!s_playPosValid)
        {
            s_lastPlayPos  = play;
            s_playPosValid = 1;
            return;
        }

        DWORD played = streamDistance(s_lastPlayPos, play, AUDIO_BUFFER_BYTES);
        s_lastPlayPos = play;

        if (played >= s_queuedBytes)  s_queuedBytes = 0;
        else                          s_queuedBytes -= played;
    }

    // Write `bytes` from src into the ring at s_writePos. Advances state.
    static int streamWrite(const unsigned char* src, DWORD bytes)
    {
        if (!s_dsBuf || !src || !bytes) return 0;
        bytes -= bytes % AUDIO_BLOCK_ALIGN;
        if (!bytes) return 0;

        // Cap by remaining ring space (leave one block headroom so write
        // pointer can never catch up to play pointer).
        DWORD writable;
        if (s_queuedBytes >= AUDIO_BUFFER_BYTES - AUDIO_BLOCK_ALIGN)
            writable = 0;
        else
            writable = AUDIO_BUFFER_BYTES - s_queuedBytes - AUDIO_BLOCK_ALIGN;

        if (bytes > writable) bytes = writable - (writable % AUDIO_BLOCK_ALIGN);
        if (!bytes) return 0;

        void* p1 = NULL; DWORD s1 = 0;
        void* p2 = NULL; DWORD s2 = 0;
        if (FAILED(s_dsBuf->Lock(s_writePos, bytes, &p1, &s1, &p2, &s2, 0)))
            return 0;

        if (p1 && s1) memcpy(p1, src,        s1);
        if (p2 && s2) memcpy(p2, src + s1,   s2);
        s_dsBuf->Unlock(p1, s1, p2, s2);

        s_writePos     = (s_writePos + bytes) % AUDIO_BUFFER_BYTES;
        s_queuedBytes += bytes;
        return (int)bytes;
    }

    // ---------------------------------------------------------------------------
    bool init(u32 audioFrameSize, s32 /*deviceId*/, bool useNullDevice)
    {
        TFE_XboxLogf("AudioDevice", "init enter frameSize=%u useNull=%d",
            audioFrameSize, useNullDevice ? 1 : 0);
        s_nullDevice = useNullDevice;
        s_frameSize  = audioFrameSize ? audioFrameSize : AUDIO_FRAME_SAMPLES;

        if (useNullDevice)
        {
            TFE_System::logWrite(LOG_WARNING, "AudioDevice", "Null audio device selected.");
            return false;
        }

        s_deviceList[0] = OutputDeviceInfo("Xbox Default", 0);
        s_deviceCount   = 1;
        s_outputDevice  = 0;

        HRESULT hr = DirectSoundCreate(NULL, &s_ds, NULL);
        TFE_XboxLogf("AudioDevice", "DirectSoundCreate hr=0x%08x ds=%p", hr, s_ds);
        if (FAILED(hr) || !s_ds)
        {
            TFE_System::logWrite(LOG_ERROR, "AudioDevice", "DirectSoundCreate failed 0x%08x", hr);
            return false;
        }

        TFE_System::logWrite(LOG_MSG, "AudioDevice", "DirectSound init OK (polled stream)");
        return true;
    }

    // ---------------------------------------------------------------------------
    bool startOutput(TFE_AudioCallback callback, void* userData,
                     u32 /*channels*/, u32 /*sampleRate*/)
    {
        TFE_XboxLogf("AudioDevice", "startOutput callback=%p user=%p", callback, userData);
        if (s_nullDevice || !s_ds) return false;

        s_callback     = callback;
        s_callbackData = userData;

        WAVEFORMATEX wfx;
        memset(&wfx, 0, sizeof(wfx));
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = AUDIO_CHANNELS;
        wfx.nSamplesPerSec  = AUDIO_SAMPLE_RATE;
        wfx.wBitsPerSample  = AUDIO_BITS;
        wfx.nBlockAlign     = AUDIO_BLOCK_ALIGN;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        DSBUFFERDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize        = sizeof(DSBUFFERDESC);
        desc.dwFlags       = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY;
        desc.dwBufferBytes = AUDIO_BUFFER_BYTES;
        desc.lpwfxFormat   = &wfx;

        HRESULT hr = s_ds->CreateSoundBuffer(&desc, &s_dsBuf, NULL);
        TFE_XboxLogf("AudioDevice", "CreateSoundBuffer hr=0x%08x buf=%p", hr, s_dsBuf);
        if (FAILED(hr) || !s_dsBuf)
        {
            TFE_System::logWrite(LOG_ERROR, "AudioDevice", "CreateSoundBuffer failed 0x%08x", hr);
            return false;
        }

        // Zero the entire buffer so we don't get a click before the first
        // pump() submits real samples.
        void* p1 = NULL; DWORD s1 = 0;
        void* p2 = NULL; DWORD s2 = 0;
        if (SUCCEEDED(s_dsBuf->Lock(0, 0, &p1, &s1, &p2, &s2, DSBLOCK_ENTIREBUFFER)))
        {
            if (p1) memset(p1, 0, s1);
            if (p2) memset(p2, 0, s2);
            s_dsBuf->Unlock(p1, s1, p2, s2);
        }

        // Reset stream state.
        s_writePos     = 0;
        s_queuedBytes  = 0;
        s_lastPlayPos  = 0;
        s_playPosValid = 0;
        s_started      = 0;

        // Prefill: enough to cover ~2 game frames (~33 ms @ 60 fps) so a
        // short stall in the main loop doesn't underrun. 2 PCM chunks.
        s_prefillBytes = AUDIO_CHUNK_PCM_BYTES * 2;

        TFE_System::logWrite(LOG_MSG, "AudioDevice",
            "DirectSound output started (polled, %lu byte ring, %lu prefill)",
            (unsigned long)AUDIO_BUFFER_BYTES, (unsigned long)s_prefillBytes);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Called from the main loop each frame. Refills the ring buffer with
    // freshly-mixed audio from the TFE callback.
    // ---------------------------------------------------------------------------
    void pump()
    {
        if (!s_dsBuf || !s_callback) return;

        streamUpdateQueued();

        // Fill until the ring is mostly full (one block of headroom). We may
        // call the callback multiple times per pump if we're catching up.
        int safety = 8;   // cap iterations so a runaway callback can't hang
        while (safety-- > 0)
        {
            DWORD writable;
            if (s_queuedBytes >= AUDIO_BUFFER_BYTES - AUDIO_BLOCK_ALIGN)
                writable = 0;
            else
                writable = AUDIO_BUFFER_BYTES - s_queuedBytes - AUDIO_BLOCK_ALIGN;

            if (writable < AUDIO_CHUNK_PCM_BYTES) break;

            // Pull one chunk of FLOAT samples from the TFE mixer, then
            // convert to s16 PCM for DirectSound.
            memset(s_mixBufF32, 0, AUDIO_CHUNK_FLT_BYTES);
            s_callback(s_callbackData, (unsigned char*)s_mixBufF32, AUDIO_CHUNK_FLT_BYTES);
            floatToS16(s_mixBufF32, s_mixBufS16, AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS);
            streamWrite((const unsigned char*)s_mixBufS16, AUDIO_CHUNK_PCM_BYTES);
        }

        // Once enough data is queued, kick off looping playback.
        if (!s_started && s_queuedBytes >= s_prefillBytes)
        {
            s_dsBuf->SetCurrentPosition(0);
            HRESULT hr = s_dsBuf->Play(0, 0, DSBPLAY_LOOPING);
            s_started      = SUCCEEDED(hr) ? 1 : 0;
            s_lastPlayPos  = 0;
            s_playPosValid = s_started;
            TFE_System::logWrite(LOG_MSG, "AudioDevice",
                "DirectSound playback %s (queued=%lu)",
                s_started ? "started" : "FAILED", (unsigned long)s_queuedBytes);
        }
    }

    // ---------------------------------------------------------------------------
    void stopOutput()
    {
        TFE_XboxLogf("AudioDevice", "stopOutput started=%d", s_started);
        if (s_dsBuf)
        {
            s_dsBuf->Stop();
            s_dsBuf->Release();
            s_dsBuf = NULL;
        }
        s_callback     = NULL;
        s_callbackData = NULL;
        s_writePos     = 0;
        s_queuedBytes  = 0;
        s_started      = 0;
        s_playPosValid = 0;
    }

    // ---------------------------------------------------------------------------
    void destroy()
    {
        TFE_XboxLogf("AudioDevice", "destroy");
        stopOutput();
        if (s_ds) { s_ds->Release(); s_ds = NULL; }
    }

    s32 getDefaultOutputDevice() { return 0; }
    s32 getOutputDeviceId()      { return s_outputDevice; }
    s32 getOutputDeviceCount()   { return s_deviceCount; }

    const OutputDeviceInfo* getOutputDeviceList(s32& count, s32& curOutput)
    {
        count     = s_deviceCount;
        curOutput = s_outputDevice;
        return s_deviceList;
    }
}
