// spawntrace10 probe: where the player is put (the Pirates fall), trace's two start hooks alone. core. Spawn threads.
#pragma once
#include "../core/hooks.hpp"
#include "start.hpp"

namespace trace::spawntrace {

struct Config {
    bool on = false;   // spawntrace10 without trace (trace installs the same two hooks itself)
};

// Installs both hooks when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c)
{
    if (!c.on) return true;
    start::detail::State& s = start::detail::g_state;
    return core::installFeature("spawntrace10", {
        {"start lookup (spawntrace10)", 0x013A85D0, "6AFF68????????64A100000000", (void*)&start::detail::hkStartLookup,
         (void**)&s.g_startLookup},
        {"start query (spawntrace10)", 0x0145F720, "8B44240C83EC30", (void*)&start::detail::hkStartQuery,
         (void**)&s.g_startQuery},
    });
}

}  // namespace trace::spawntrace
