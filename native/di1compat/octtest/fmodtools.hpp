// What octtest's FMOD modes share: the log sink, the host tick, category chains, FMOD and Wwise ids. Main thread.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include "../games/di1/audio.hpp"

namespace octtest {

namespace ea = engine::audio;   // the audio modules, as octtest's audio modes call them

// FMOD_EVENTPROPERTY indices (fmod_event.h, FMOD Ex 4.44.14) octtest reads.
namespace fmodprop {
constexpr int kVolume = 1, kPriority = 11, kMaxPlaybacks = 12, kMaxPlaybacksBehavior = 13, kMode = 14;
constexpr int kMinDistance = 17, kMaxDistance = 18, kPosition3d = 19;
}  // namespace fmodprop
constexpr int kEventValue = 0, kInstanceValue = 1;    // Event_GetPropertyByIndex: the event's or this instance's
constexpr unsigned kEventDefault = 0, kInfoOnly = 4;   // FMOD_EVENT_DEFAULT, FMOD_EVENT_INFOONLY

// Wwise ids as the plugin's host delivers them (engine/audio/wwise.hpp, 3.0 LAB_01830120).
constexpr uint32_t kAkEndOfEvent = 1, kAkDuration = 8;   // callback types, also the post flags that ask for them
constexpr uint32_t kAkStop = 0;                          // AkActionOnEventType_Stop
constexpr size_t kAkPlayingIdAt = 2;                     // u32 index in AkEventCallbackInfo
constexpr size_t kAkDurationAt = 4;                      // float index of fDuration in AkDurationCallbackInfo
constexpr uint32_t kTestObject = 7;                      // the game object octtest posts on
constexpr LONG kHostUp = 1;                              // system's ready() latch once the host runs

constexpr int kTickMs = 16;     // the plugin's host tick period
constexpr int kChainDepth = 8;  // categories followed up a chain
constexpr char kDialog10[] = "\\assets\\sound\\dialog";   // the 1.0 install's dialog folder
constexpr char kPlayset10[] = "Inc";                       // the Incredibles playset: Music_Inc.fev

// FMOD_MUSIC_ENTITY and FMOD_MUSIC_ITERATOR (fmod_event.h).
struct MusicEntity {
    const char* name;
    unsigned id;
};
struct MusicIterator {
    const MusicEntity* value;
    const char* filter;
};

// The audio log sink for octtest: each line to stdout.
inline void stdoutLog(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}

// The audio modules' state, as the offline audio modes look into it (docs/audit AU-04).
inline ea::system::detail::State& g_sys = ea::system::detail::g_state;
inline ea::host::detail::State& g_host = ea::host::detail::g_state;
inline ea::dialogue::detail::State& g_vo = ea::dialogue::detail::g_state;

// The plugin's audio config on a 1.0 install with FMOD's no-sound output, for the modes that run the host here.
inline ea::wwise::Config offlineAudio(const std::string& inst10)
{
    di1::audio::Cfg c;
    c.install10 = inst10;
    c.log = stdoutLog;
    ea::wwise::Config w = di1::audio::config(c);
    w.systemCfg.nosound = true;
    return w;
}

// Every audio module configured, then FMOD up on this thread (no hook, no host thread).
inline bool offlineHost(const ea::wwise::Config& w)
{
    ea::wwise::configure(w);
    return ea::system::init();
}

// The host tick and callback delivery for ms, on this thread (the plugin runs them on its tick thread).
inline void tickFor(int ms)
{
    for (int t = 0; t < ms; t += kTickMs) {
        Sleep(kTickMs);
        ea::wwise::tick();
    }
}

// The hooks' Wwise originals as stand-ins (octtest has no Wwise), the listener at the origin facing +z.
inline void listenerAtOrigin()
{
    ea::wwise::setOriginals([](const float*, uint32_t) { return 1; }, [](uint32_t, const float*) { return 1; });
    const float lis[9] = {0, 0, 1, 0, 1, 0, 0, 0, 0};
    ea::wwise::hkListener(lis, 0);
}

// probe.cpp: what --fmod runs only on request (FMOD_CAPS, CUE_RUN, EV_RUN).
int probeFmodCaps(const char* install10);
void probeCueRun(const char* spec);
void probeEvRun(const char* events, ea::wwise::detail::AkCallback cb);

// A category's label in a chain, from its name and the category.
using CategoryLabel = std::string (*)(const char* name, void* cat);

// The chain from category c up, at most depth categories, labels joined by " < ".
inline std::string categoryChain(void* c, int depth, CategoryLabel label)
{
    std::string chain;
    for (int d = 0; c && d < depth; ++d) {
        char* cn = nullptr;
        g_sys.f.EventCategory_GetInfo(c, nullptr, &cn);
        chain += std::string(chain.empty() ? "" : " < ") + label(cn ? cn : "?", c);
        void* parent = nullptr;
        if (g_sys.f.EventCategory_GetParentCategory(c, &parent)) break;
        c = parent;
    }
    return chain;
}

}  // namespace octtest
