// The choreography start hook's user: cinesync10 (1.0's audio-sync sound event). engine, audio.
// Boot thread installs.
#pragma once
#include <cstdint>
#include "../../engine/audio/host.hpp"
#include "../../engine/choreography.hpp"
#include "config.hpp"

namespace di1::chostart {

// Installs engine/choreography with cinesync10's callback when it is on (H1).
inline bool install(const Config& c)
{
    engine::choreography::Config cho;
    const bool cineSync = c.audio10 && c.cineSync10;
    if (cineSync)
        cho.started = [](const uint8_t*, const char* name, uint32_t flags) {
            engine::audio::host::choreographyStarted(name, flags);
        };
    return engine::choreography::install(cho);
}

}  // namespace di1::chostart
