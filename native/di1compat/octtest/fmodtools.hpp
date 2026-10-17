// What octtest's FMOD modes share: the log sink, the host tick, category chains, FMOD and Wwise ids. Main thread.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include "../audio.hpp"

namespace octtest {

// FMOD_EVENTPROPERTY indices (fmod_event.h, FMOD Ex 4.44.14) octtest reads.
namespace fmodprop {
constexpr int kVolume = 1, kPriority = 11, kMaxPlaybacks = 12, kMaxPlaybacksBehavior = 13, kMode = 14;
constexpr int kMinDistance = 17, kMaxDistance = 18, kPosition3d = 19;
}  // namespace fmodprop
constexpr int kEventValue = 0, kInstanceValue = 1;    // Event_GetPropertyByIndex: the event's or this instance's
constexpr unsigned kEventDefault = 0, kInfoOnly = 4;   // FMOD_EVENT_DEFAULT, FMOD_EVENT_INFOONLY

// Wwise ids as the plugin's host delivers them (audio.hpp, 3.0 LAB_01830120).
constexpr uint32_t kAkEndOfEvent = 1, kAkDuration = 8;   // callback types, also the post flags that ask for them
constexpr uint32_t kAkStop = 0;                          // AkActionOnEventType_Stop
constexpr size_t kAkPlayingIdAt = 2;                     // u32 index in AkEventCallbackInfo
constexpr size_t kAkDurationAt = 4;                      // float index of fDuration in AkDurationCallbackInfo
constexpr uint32_t kTestObject = 7;                      // the game object octtest posts on
constexpr LONG kHostUp = 1;                              // audio::hostState once the host runs

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

// audio::logfn for octtest: each line to stdout.
inline void stdoutLog(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}

// The host tick and callback delivery for ms, on this thread (the plugin runs them on its tick thread).
inline void tickFor(int ms)
{
    for (int t = 0; t < ms; t += kTickMs) {
        Sleep(kTickMs);
        audio::tickAndDeliver();
    }
}

// The hooks' Wwise originals as stand-ins (octtest has no Wwise), the listener at the origin facing +z.
inline void listenerAtOrigin()
{
    audio::oListener = [](const float*, uint32_t) { return 1; };
    audio::oSetPos = [](uint32_t, const float*) { return 1; };
    const float lis[9] = {0, 0, 1, 0, 1, 0, 0, 0, 0};
    audio::hkListener(lis, 0);
}

// probe.cpp: what --fmod runs only on request (FMOD_CAPS, CUE_RUN, EV_RUN).
int probeFmodCaps(const char* install10);
void probeCueRun(const char* spec);
void probeEvRun(const char* events, audio::AkCallback cb);

// A category's label in a chain, from its name and the category.
using CategoryLabel = std::string (*)(const char* name, void* cat);

// The chain from category c up, at most depth categories, labels joined by " < ".
inline std::string categoryChain(void* c, int depth, CategoryLabel label)
{
    std::string chain;
    for (int d = 0; c && d < depth; ++d) {
        char* cn = nullptr;
        audio::g.f.EventCategory_GetInfo(c, nullptr, &cn);
        chain += std::string(chain.empty() ? "" : " < ") + label(cn ? cn : "?", c);
        void* parent = nullptr;
        if (audio::g.f.EventCategory_GetParentCategory(c, &parent)) break;
        c = parent;
    }
    return chain;
}

}  // namespace octtest
