// FMOD Ex brought up as the source game did, its host thread, and the lock of every FMOD call. fmod.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include "fmod.hpp"
#include "log.hpp"

namespace engine::audio::system {

// The source game's own FMOD init call, value for value (the game passes them).
struct InitValues {
    float doppler = 0, distance = 0, rolloff = 0;   // System::set3DSettings
    int softwareOutputs = 0;                        // setSoftwareFormat's output channels
    unsigned streamBufferMs = 0;
    size_t advancedField = 0;                       // the one FMOD_ADVANCEDSETTINGS field set, and its value
    uint32_t advancedValue = 0;
    int maxChannels = 0;                            // EventSystem::init
    unsigned initFlags = 0, eventFlags = 0;
};

struct Config {
    std::string dllDir;     // fmodex.dll and fmod_event.dll
    std::string soundDir;   // FMOD's media path
    bool nosound = false;   // octtest: FMOD's no-sound output
    int testSpeakerMode = -1;   // octtest: the no-sound system's speaker mode
    InitValues init;
    std::function<void()> boot;   // host thread, after init and before ready() answers
    std::function<void()> tick;   // host thread, every kTickMs after that
};

constexpr DWORD kTickMs = 16;
constexpr DWORD kReadyWaitMs = 20000;

// Keeps c; no FMOD call. octtest uses it with init().
inline void configure(const Config& c);
// configure(), then the host thread: init, boot, then tick forever. False (logged) if it did not start.
inline bool install(const Config& c);
// FMOD up as the source game brings it up; false (logged) on failure.
inline bool init();
// True once FMOD is up. The first caller waits for its init; never call it under the loader lock.
inline bool ready();
// Every FMOD call and every host member; taken after dialogue's.
inline std::unique_lock<std::recursive_mutex> lock();
// The lock if free now (per-frame callers never wait out FMOD's update).
inline std::unique_lock<std::recursive_mutex> tryLock();
inline const fmod::Api& api();
inline void* eventSystem();
inline void* soundSystem();
inline void* musicSystem();
inline const std::string& soundDir();
// The file's name as spelled in the sound folder ("" if absent): requests arrive lower-cased.
inline std::string installedName(const std::string& file);

namespace detail {

constexpr LONG kStarting = 0;
constexpr LONG kUp = 1;
constexpr LONG kFailed = -1;

struct State {
    Config cfg;            // set once by configure(), before the host thread starts
    fmod::Api f{};         // set once by init(), before ready() answers
    void* es = nullptr;    // set once by init(), under mx
    void* sys = nullptr;   // set once by init(), under mx
    void* ms = nullptr;    // set once by init(), under mx
    std::recursive_mutex mx;          // serialises every FMOD call; held across project and bank loads (AU-07)
    HANDLE done = nullptr;            // set once by install(); signalled after init and boot
    bool ok = false;                  // host thread, before done is signalled
    std::atomic<LONG> up{kStarting};  // ready()'s latch: kStarting, kUp or kFailed
};
inline State g_state;

// No driver or nosound: FMOD's no-sound output (as 1.0 does); else the default driver's speaker mode.
inline void setOutput(int drivers)
{
    State& s = g_state;
    const fmod::Api& f = s.f;
    if (s.cfg.nosound || drivers == 0) {
        f.System_SetOutput(s.sys, fmod::kOutputNoSound);
        if (s.cfg.testSpeakerMode >= 0) f.System_SetSpeakerMode(s.sys, s.cfg.testSpeakerMode);
        return;
    }
    unsigned caps = 0;
    int mode = 0;
    if (const fmod::Result r = f.System_GetDriverCaps(s.sys, fmod::kDefaultDriver, &caps, nullptr, &mode); !r)
        f.System_SetSpeakerMode(s.sys, mode);
}

// Software format, stream buffer and advanced settings with the game's values.
inline fmod::Result setFormat()
{
    State& s = g_state;
    const fmod::Api& f = s.f;
    const InitValues& v = s.cfg.init;
    int rate = 0, fmt = 0, nout = 0, maxin = 0, resampler = 0, bits = 1;
    fmod::Result r = f.System_GetSoftwareFormat(s.sys, &rate, &fmt, &nout, &maxin, &resampler, &bits);
    if (!r) r = f.System_SetSoftwareFormat(s.sys, rate, fmt, v.softwareOutputs, maxin, resampler);
    if (!r) r = f.System_SetStreamBufferSize(s.sys, v.streamBufferMs, fmod::kTimeUnitMs);
    if (r) return r;
    uint8_t adv[fmod::kAdvancedSettingsSize] = {};
    const uint32_t size = (uint32_t)fmod::kAdvancedSettingsSize;
    memcpy(adv, &size, sizeof size);
    memcpy(adv + v.advancedField, &v.advancedValue, sizeof v.advancedValue);
    f.System_SetAdvancedSettings(s.sys, adv);
    return 0;
}

inline DWORD WINAPI hostThread(void*)
{
    State& s = g_state;
    const bool ok = init();
    if (ok && s.cfg.boot) s.cfg.boot();
    s.ok = ok;
    SetEvent(s.done);
    if (!ok) return 0;
    for (;;) {
        Sleep(kTickMs);
        if (s.cfg.tick) s.cfg.tick();
    }
}

}  // namespace detail

inline void configure(const Config& c) { detail::g_state.cfg = c; }

// The host thread runs FMOD's init: fmodex.dll's CoInitialize (STA) never meets the game's own COM apartments.
inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    configure(c);
    s.done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE th = s.done ? CreateThread(nullptr, 0, detail::hostThread, nullptr, 0, nullptr) : nullptr;
    if (!th) {
        log("audio10: FMOD host thread not started (%lu)", GetLastError());
        return false;
    }
    CloseHandle(th);
    return true;
}

// 1.0 FUN_011372c0, call for call, without the engine allocators and file callbacks (the media path instead).
inline bool init()
{
    detail::State& s = detail::g_state;
    const Config& c = s.cfg;
    const fmod::Api& f = s.f;
    const bool loaded = s.f.load(c.dllDir);
    if (!loaded) {
        log("audio10: FMOD DLLs not loaded from %s (%lu)", c.dllDir.c_str(), GetLastError());
        return false;
    }
    std::lock_guard<std::recursive_mutex> l(s.mx);
    fmod::Result r = f.EventSystem_Create(&s.es);
    if (!r) r = f.EventSystem_GetSystemObject(s.es, &s.sys);
    if (!r) r = f.EventSystem_GetMusicSystem(s.es, &s.ms);
    if (!r) r = f.System_Set3DSettings(s.sys, c.init.doppler, c.init.distance, c.init.rolloff);
    int drivers = 0;
    if (!r) r = f.System_GetNumDrivers(s.sys, &drivers);
    if (r) {
        log("audio10: FMOD setup failed (%d)", r);
        return false;
    }
    detail::setOutput(drivers);
    r = detail::setFormat();
    if (r) {
        log("audio10: FMOD format setup failed (%d)", r);
        return false;
    }
    r = f.EventSystem_Init(s.es, c.init.maxChannels, c.init.initFlags, nullptr, c.init.eventFlags);
    if (r) {
        log("audio10: EventSystem::init failed (%d)", r);
        return false;
    }
    f.EventSystem_SetMediaPath(s.es, (c.soundDir + "\\").c_str());
    unsigned v1 = 0, v2 = 0;
    f.System_GetVersion(s.sys, &v1);
    f.EventSystem_GetVersion(s.es, &v2);
    log("audio10: FMOD Ex %08X / Event %08X up (%d drivers%s)", v1, v2, drivers, c.nosound ? ", no sound" : "");
    return true;
}

inline bool ready()
{
    detail::State& s = detail::g_state;
    const LONG st = s.up.load();
    if (st) return st > 0;
    const DWORD w = WaitForSingleObject(s.done, kReadyWaitMs);
    const bool up = w == WAIT_OBJECT_0 && s.ok;
    LONG starting = detail::kStarting;
    if (const bool won = s.up.compare_exchange_strong(starting, up ? detail::kUp : detail::kFailed); won && !up)
        log("audio10: FMOD host %s: 1.0 audio off", w != WAIT_OBJECT_0 ? "did not finish its init in 20 s" : "failed");
    return s.up.load() > 0;
}

inline std::unique_lock<std::recursive_mutex> lock()
{
    return std::unique_lock<std::recursive_mutex>(detail::g_state.mx);
}
inline std::unique_lock<std::recursive_mutex> tryLock()
{
    return std::unique_lock<std::recursive_mutex>(detail::g_state.mx, std::try_to_lock);
}
inline const fmod::Api& api() { return detail::g_state.f; }
inline void* eventSystem() { return detail::g_state.es; }
inline void* soundSystem() { return detail::g_state.sys; }
inline void* musicSystem() { return detail::g_state.ms; }
inline const std::string& soundDir() { return detail::g_state.cfg.soundDir; }

inline std::string installedName(const std::string& file)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((detail::g_state.cfg.soundDir + "\\" + file).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    FindClose(h);
    return fd.cFileName;
}

}  // namespace engine::audio::system
