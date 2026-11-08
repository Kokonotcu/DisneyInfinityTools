// Choreography hook users: cinesync10 (audio-sync sound event), vosync10 (dialog clock). engine, audio.
// Boot thread installs; updates on the game thread.
#pragma once
#include <cstdint>
#include <optional>
#include "../../engine/audio/dialogue.hpp"
#include "../../engine/audio/host.hpp"
#include "../../engine/choreography.hpp"
#include "config.hpp"

namespace di1::chostart {

// Installs engine/choreography with the callbacks of the features that are on (H1).
inline bool install(const Config& c);

namespace detail {

constexpr uint32_t kAudioSync = 0x20;   // the instance flag enableAudioSync sets (FUN_01449390's +0x1b4)
constexpr uint32_t kPlaying = 0x80;     // set by FUN_01449390; FUN_0144a4d0 then runs its playing branch

// 1.0 FUN_00ec3820: an audio-synced choreography's time clocks its type 4 dialog run, per update.
inline void voSyncUpdated(const uint8_t* cho, uint32_t flags)
{
    if (!(flags & kAudioSync)) return;
    const std::optional<float> t = flags & kPlaying ? engine::choreography::clock(cho) : std::nullopt;
    if (t)
        engine::audio::dialogue::driverTime(cho, *t);
    else
        engine::audio::dialogue::driverStopped(cho);
}

struct Picks {
    bool cineSync, voSync;
};

// The start callback: cinesync10 first (its order before vosync10), then vosync10's binding.
inline engine::choreography::Notify startedOf(Picks p)
{
    return [p](const uint8_t* cho, const char* name, uint32_t flags) {
        if (p.cineSync) engine::audio::host::choreographyStarted(name, flags);
        if (p.voSync && (flags & kAudioSync)) engine::audio::dialogue::driverStarted(cho, name);
    };
}

}  // namespace detail

inline bool install(const Config& c)
{
    engine::choreography::Config cho;
    const detail::Picks p{c.audio10 && c.cineSync10, c.audio10 && c.vo10 && c.voSync10};
    if (p.cineSync || p.voSync) cho.started = detail::startedOf(p);
    if (p.voSync) cho.updated = detail::voSyncUpdated;
    return engine::choreography::install(cho);
}

}  // namespace di1::chostart
