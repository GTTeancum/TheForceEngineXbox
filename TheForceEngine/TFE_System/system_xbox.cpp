// system_xbox.cpp
// Xbox implementation of TFE_System (timing, sleep, quit, log).
// Replaces system.cpp and log.cpp for the Xbox build configuration.
//
// Key changes from PC:
//  - SDL timing replaced with QueryPerformanceCounter (XDK)
//  - SDL_Delay replaced with Sleep (XDK)
//  - std::chrono removed; time formatting uses time()/localtime()
//  - TFE_FrontEndUI::logToConsole calls removed (no ImGui console)
//  - osShellExecute / postErrorMessageBox stubbed (no shell on Xbox)
//  - TFE_XboxLog() exported for early boot logging before log file is open
//  - Log writes to D:\<filename> only. Debug-output mirroring and write-through
//    are disabled in normal runtime because they stall real Xbox hardware
//    during menu/level transitions. Single log overwritten per boot.
//  - vsync state read from settings; no RenderBackend query in update()
//    (RenderBackend handles its own present interval on Xbox)

#include <TFE_System/system.h>
#include <TFE_System/profiler.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Settings/settings.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <xtl.h>   // XDK umbrella header

// ---------------------------------------------------------------------------
// Log buffer sizes
// ---------------------------------------------------------------------------
#define LOG_WORK_BUF  4096
#define LOG_MSG_BUF   4096

// OutputDebugStringA and FILE_FLAG_WRITE_THROUGH are both expensive on real
// Xbox hardware. Keep this at 0 for playable builds; the file log remains.
#define XBOX_LOG_MIRROR_DEBUG_OUTPUT 0
#define XBOX_LOG_WRITE_THROUGH       0

namespace TFE_System
{
    // -----------------------------------------------------------------------
    // Timing state
    // -----------------------------------------------------------------------
    f64 c_gameTimeScale = 1.02;

    static LARGE_INTEGER s_perfFreq;
    static LARGE_INTEGER s_startTime;
    static LARGE_INTEGER s_time;
    static f64           s_freq          = 0.0;
    static f64           s_refreshRate   = 60.0;
    static f64           s_dt            = 1.0 / 60.0;
    static f64           s_dtRaw         = 1.0 / 60.0;
    static const f64     c_maxDt         = 0.05;
    static bool          s_synced        = false;
    static bool          s_resetStartTime= false;
    static bool          s_quitMessagePosted     = false;
    static bool          s_systemUiRequestPosted = false;
    static u32           s_frame         = 0;
    static char          s_versionString[64];

    // -----------------------------------------------------------------------
    // Log state
    // -----------------------------------------------------------------------
    static HANDLE       s_logFile        = INVALID_HANDLE_VALUE;
    static char         s_workStr[LOG_WORK_BUF];
    static char         s_msgStr[LOG_MSG_BUF];
    static bool         s_logTimeEnabled = true;

    // Serialise access to the shared format buffers above. Without this the
    // midi thread (firing every ~6.8ms via the iMuse callback) and the main
    // thread race into s_msgStr / s_workStr. Once corrupted, vsprintf can
    // spin forever following a wild format pointer - manifests as a hard
    // freeze with no further log output (seen during mission load on SECBASE
    // right after the midi callback was registered for the first time).
    // Lazily initialised on first call - all log entry points route through
    // ensureLogCS() before touching the shared buffers. Three-state init
    // (0 uninit -> 1 initing -> 2 ready) avoids the classic CAS race where
    // a second thread enters EnterCriticalSection on a not-yet-initialised
    // CS.
    static CRITICAL_SECTION s_logCS;
    static LONG             s_logCSState = 0;
    static inline void ensureLogCS()
    {
        if (InterlockedCompareExchange((LPLONG)&s_logCSState, 1, 0) == 0)
        {
            InitializeCriticalSection(&s_logCS);
            InterlockedExchange((LPLONG)&s_logCSState, 2);
        }
        else
        {
            while (s_logCSState != 2) { Sleep(0); }
        }
    }

    // -----------------------------------------------------------------------
    // s_logWriteRaw
    // -----------------------------------------------------------------------
    static void s_logWriteRaw(const char* msg, DWORD len)
    {
        if (s_logFile == INVALID_HANDLE_VALUE || !msg || len == 0) return;
        DWORD written = 0;
        WriteFile(s_logFile, msg, len, &written, NULL);
    }

    static const char* c_typeNames[] =
    {
        "",         // LOG_MSG
        "Warning",  // LOG_WARNING
        "Error",    // LOG_ERROR
        "Critical", // LOG_CRITICAL
    };

    // -----------------------------------------------------------------------
    // TFE_XboxLog - raw log shim used before the log file is open.
    // Safe to call at any time.
    // Exported (extern "C" linkage) so paths_xbox.cpp can call it without
    // pulling in the full TFE_System namespace.
    // -----------------------------------------------------------------------
    extern "C" void TFE_XboxLog(const char* msg)
    {
        if (!msg) return;
#if XBOX_LOG_MIRROR_DEBUG_OUTPUT
        OutputDebugStringA(msg);
#endif
        // Forward to the log file if open.
        s_logWriteRaw(msg, (DWORD)strlen(msg));
    }

    extern "C" void TFE_XboxLogf(const char* tag, const char* fmt, ...)
    {
        if (!tag || !fmt) return;
        ensureLogCS();
        EnterCriticalSection(&s_logCS);

        va_list arg;
        va_start(arg, fmt);
        vsprintf(s_msgStr, fmt, arg);
        va_end(arg);

        sprintf(s_workStr, "[%s] %s\r\n", tag, s_msgStr);
        TFE_XboxLog(s_workStr);
        LeaveCriticalSection(&s_logCS);
    }

    // -----------------------------------------------------------------------
    // Frame counter
    // -----------------------------------------------------------------------
    void setFrame(u32 frame) { s_frame = frame; }
    u32  getFrame()          { return s_frame; }

    // -----------------------------------------------------------------------
    // init / shutdown
    // -----------------------------------------------------------------------
    void init(f32 refreshRate, bool synced, const char* versionString)
    {
        // %f intentionally avoided in TFE_XboxLogf - MSVC 2005 vsprintf
        // float formatting hangs on Xbox in this build configuration.
        TFE_XboxLogf("System", "init begin refresh=%d (x100) synced=%d version=%s",
            (int)(refreshRate * 100.0f), synced ? 1 : 0, versionString ? versionString : "");

        QueryPerformanceFrequency(&s_perfFreq);
        QueryPerformanceCounter(&s_startTime);
        s_time      = s_startTime;
        s_freq      = 1.0 / (f64)s_perfFreq.QuadPart;

        s_refreshRate = (f64)refreshRate;
        s_synced      = synced;

        strncpy(s_versionString, versionString ? versionString : "", 63);
        s_versionString[63] = 0;

        TFE_System::logWrite(LOG_MSG, "Startup", "TFE_System::init (Xbox)");
        TFE_XboxLogf("System", "init complete perfFreq=%I64d", s_perfFreq.QuadPart);
    }

    void shutdown()
    {
        TFE_XboxLogf("System", "shutdown");
        logClose();
    }

    // -----------------------------------------------------------------------
    // Vsync
    // -----------------------------------------------------------------------
    void setVsync(bool sync)
    {
        TFE_XboxLogf("System", "setVsync %d", sync ? 1 : 0);
        s_synced = sync;
        TFE_Settings::getGraphicsSettings()->vsync = sync;
        // RenderBackend on Xbox controls present interval directly.
    }

    bool getVSync() { return s_synced; }

    const char* getVersionString() { return s_versionString; }

    void resetStartTime()
    {
        TFE_XboxLogf("System", "resetStartTime requested");
        s_resetStartTime = true;
    }

    // -----------------------------------------------------------------------
    // Timing
    // -----------------------------------------------------------------------
    static inline u64 perfToU64(const LARGE_INTEGER& li)
    {
        return (u64)li.QuadPart;
    }

    void update()
    {
        LARGE_INTEGER curLI;
        QueryPerformanceCounter(&curLI);
        const u64 curTime = perfToU64(curLI);
        const u64 prevTime = perfToU64(s_time);
        s_time = curLI;

        const u64 uDt = (curTime > prevTime) ? (curTime - prevTime) : 1;

        if (s_resetStartTime)
        {
            s_startTime = s_time;
            s_resetStartTime = false;
        }

        f64 dt = (f64)uDt * s_freq;

        // Vsync rounding - same logic as PC, capped at 120 Hz.
        if (s_synced && s_refreshRate > 0.0 && s_refreshRate < 120.0)
        {
            f64 intervals = (f64)(int)(dt * s_refreshRate + 0.1);
            if (intervals < 1.0) intervals = 1.0;
            f64 newDt = intervals / s_refreshRate;
            f64 roundDiff = newDt - dt;
            if (roundDiff < 0.0) roundDiff = -roundDiff;
            if (roundDiff <= 0.25 / s_refreshRate)
                dt = newDt;
        }

        s_dtRaw = dt;
        s_dt    = (dt < c_maxDt) ? dt : c_maxDt;
    }

    f64 updateThreadLocal(u64* localTime)
    {
        LARGE_INTEGER curLI;
        QueryPerformanceCounter(&curLI);
        const u64 curTime = perfToU64(curLI);
        const u64 uDt = (*localTime > 0u) ? (curTime - *localTime) : 0u;
        *localTime = curTime;
        return (f64)uDt * s_freq;
    }

    f64 getDeltaTime()    { return s_dt; }
    f64 getDeltaTimeRaw() { return s_dtRaw; }

    f64 getTime()
    {
        const u64 uDt = perfToU64(s_time) - perfToU64(s_startTime);
        return (f64)uDt * s_freq;
    }

    u64 getCurrentTimeInTicks()
    {
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return (u64)li.QuadPart - (u64)s_startTime.QuadPart;
    }

    f64 convertFromTicksToSeconds(u64 ticks) { return (f64)ticks * s_freq; }
    f64 convertFromTicksToMillis(u64 ticks)  { return (f64)ticks * 1000.0 * s_freq; }
    f64 microsecondsToSeconds(f64 mu)        { return mu / 1000000.0; }

    u64 getStartTime()              { return (u64)s_startTime.QuadPart; }
    void setStartTime(u64 startTime){ s_startTime.QuadPart = (LONGLONG)startTime; }

    void getDateTimeString(char* output)
    {
        time_t t = time(NULL);
        struct tm* ct = localtime(&t);
        static const char* monthNames[12] =
        {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        if (!ct)
        {
            strcpy(output, "Jan 01, 2000 00:00");
            return;
        }
        s32 month = ct->tm_mon;
        if (month < 0) month = 0;
        if (month > 11) month = 11;
        sprintf(output, "%s %02d, %04d %02d:%02d",
            monthNames[month], ct->tm_mday, ct->tm_year + 1900,
            ct->tm_hour, ct->tm_min);
    }

    void getDateTimeStringForFile(char* output)
    {
        time_t t = time(NULL);
        struct tm* ct = localtime(&t);
        sprintf(output, "%0.2d-%0.2d-%0.4d-%0.2d-%0.2d",
            ct->tm_mon + 1, ct->tm_mday, ct->tm_year + 1900,
            ct->tm_hour + 1, ct->tm_min);
    }

    // -----------------------------------------------------------------------
    // System
    // -----------------------------------------------------------------------
    bool osShellExecute(const char* /*pathToExe*/, const char* /*exeDir*/,
                        const char* /*param*/, bool /*waitForCompletion*/)
    {
        // No shell on Xbox.
        return false;
    }

    void postErrorMessageBox(const char* msg, const char* title)
    {
        TFE_XboxLogf("System", "postErrorMessageBox title=%s msg=%s",
            title ? title : "", msg ? msg : "");
        TFE_System::logWrite(LOG_ERROR, title, msg);
        // No message box on Xbox; error is in the log file.
    }

    void sleep(u32 sleepDeltaMS)
    {
        Sleep(sleepDeltaMS);
    }

    void postQuitMessage()              { TFE_XboxLogf("System", "postQuitMessage"); s_quitMessagePosted = true; }
    void postSystemUiRequest()          { TFE_XboxLogf("System", "postSystemUiRequest"); s_systemUiRequestPosted = true; }
    bool quitMessagePosted()            { return s_quitMessagePosted; }

    bool systemUiRequestPosted()
    {
        bool req = s_systemUiRequestPosted;
        s_systemUiRequestPosted = false;
        return req;
    }

    // -----------------------------------------------------------------------
    // Log
    // -----------------------------------------------------------------------
    void logTimeToggle() { s_logTimeEnabled = !s_logTimeEnabled; }

    // Xbox log path is deliberately fixed to D:\ only; other roots made
    // hardware behavior unpredictable and can stop some boxes from launching.
    bool logOpen(const char* filename, bool /*append*/)
    {
        if (!filename || !filename[0]) return false;

        char logPath[TFE_MAX_PATH];
        sprintf(logPath, "D:\\%s", filename);
        s_logFile = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL
#if XBOX_LOG_WRITE_THROUGH
                                | FILE_FLAG_WRITE_THROUGH
#endif
                                ,
                                NULL);
        if (s_logFile != INVALID_HANDLE_VALUE)
        {
            TFE_XboxLogf("Log", "open success path=%s (Win32)", logPath);
            return true;
        }

        s_logFile = INVALID_HANDLE_VALUE;
#if XBOX_LOG_MIRROR_DEBUG_OUTPUT
        OutputDebugStringA("[Log] D:\\ log open failed\r\n");
#endif
        return false;
    }

    void logClose()
    {
        if (s_logFile != INVALID_HANDLE_VALUE)
        {
            TFE_XboxLogf("Log", "close");
            CloseHandle(s_logFile);
            s_logFile = INVALID_HANDLE_VALUE;
        }
    }

    void debugWrite(const char* tag, const char* str, ...)
    {
        if (!tag || !str) return;
        ensureLogCS();
        EnterCriticalSection(&s_logCS);
        va_list arg;
        va_start(arg, str);
        vsprintf(s_msgStr, str, arg);
        va_end(arg);
        sprintf(s_workStr, "[%s] %s\r\n", tag, s_msgStr);
#if XBOX_LOG_MIRROR_DEBUG_OUTPUT
        OutputDebugStringA(s_workStr);
#endif
        s_logWriteRaw(s_workStr, (DWORD)strlen(s_workStr));
        LeaveCriticalSection(&s_logCS);
    }

    void logWrite(LogWriteType type, const char* tag, const char* str, ...)
    {
        if (type >= LOG_COUNT || !tag || !str) return;
        ensureLogCS();
        EnterCriticalSection(&s_logCS);

        va_list arg;
        va_start(arg, str);
        vsprintf(s_msgStr, str, arg);
        va_end(arg);

        // Use GetTickCount, NOT time()/localtime()/strftime().
        // The Xbox CRT's time-of-day routines depend on the kernel having
        // a valid clock - which isn't guaranteed before XInputOpen / audio
        // init - and can hang on retail hardware. GetTickCount is millisec
        // since power-on and is always safe to call from any boot phase.
        char timeStr[40];
        if (s_logTimeEnabled)
        {
            DWORD ms = GetTickCount();
            sprintf(timeStr, "[%lu.%03lu] ", (unsigned long)(ms / 1000),
                                             (unsigned long)(ms % 1000));
        }
        else
        {
            timeStr[0] = 0;
        }

        if (type != LOG_MSG)
            sprintf(s_workStr, "%s[%s : %s] %s\r\n", timeStr, c_typeNames[type], tag, s_msgStr);
        else
            sprintf(s_workStr, "%s[%s] %s\r\n", timeStr, tag, s_msgStr);

        // Debug output mirroring is disabled for playable Xbox builds. It can
        // stall the title on every transition when a debugger/listener is
        // attached.
#if XBOX_LOG_MIRROR_DEBUG_OUTPUT
        OutputDebugStringA(s_workStr);
#endif

        // Write to log file if open.
        s_logWriteRaw(s_workStr, (DWORD)strlen(s_workStr));
        LeaveCriticalSection(&s_logCS);
    }

    // Single log overwritten each boot; rotation removed.
    void openRotatingLog(const char* fileName, bool append)
    {
        if (!fileName) return;
        logOpen(fileName, append);
    }

} // namespace TFE_System
