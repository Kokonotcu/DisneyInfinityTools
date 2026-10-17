// The render and Meridian tables beside the DLL, loaded before any hook. core, rentable, mer. Read by any thread.
#pragma once
#include <cstdio>
#include <string>
#include "../../core/log.hpp"
#include "../../core/paths.hpp"
#include "../../formats/rentable.hpp"
#include "../../mer.hpp"
#include "config.hpp"

namespace di1::tables {

// Loads both tables from the plugin's folder, the Meridian one with c's keys, and logs each; installs no hook.
inline bool install(const Config& c);
// The render table as it loaded: empty or partial when renderOk() is false.
inline const rentable::Table& render();
// Whether the render table loaded.
inline bool renderOk();
// The Meridian table; nullptr when it did not load.
inline mer::Table* mer();

namespace detail {

struct State {
    rentable::Table render;   // set once in install()
    bool renderOk = false;    // set once in install()
    mer::Table mer;           // set once in install(); di1::meridian adds 1.0's classes at boot, before any transcode
    bool merOk = false;       // set once in install()
};
inline State g_state;

// A file in dir, read whole; "" when it cannot be opened.
inline std::string slurpFile(const std::string& dir, const char* name)
{
    std::string text;
    FILE* tf = nullptr;
    if (fopen_s(&tf, (dir + "\\" + name).c_str(), "rb") || !tf) return text;
    char buf[65536];
    for (size_t got = fread(buf, 1, sizeof(buf), tf); got > 0; got = fread(buf, 1, sizeof(buf), tf))
        text.append(buf, got);
    fclose(tf);
    return text;
}

}  // namespace detail

inline const rentable::Table& render() { return detail::g_state.render; }

inline bool renderOk() { return detail::g_state.renderOk; }

inline mer::Table* mer() { return detail::g_state.merOk ? &detail::g_state.mer : nullptr; }

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    const std::string dir = core::pluginDir();
    s.renderOk = s.render.load(detail::slurpFile(dir, "di1compat_render.txt"));
    core::logf("render table: %s (%u shaders, %u donors, %u materials, %u vertex plans)",
               s.renderOk ? "ok" : "MISSING", (unsigned)s.render.shaders.size(), (unsigned)s.render.donors.size(),
               (unsigned)s.render.matindex.size(), (unsigned)s.render.plans.size());
    s.merOk = s.mer.load(detail::slurpFile(dir, "di1compat_mer.txt"));
    s.mer.events10 = c.events10;
    s.mer.portRefs = c.portRefs10;
    s.mer.ports10 = c.ports10;
    s.mer.pins10 = c.pins10;
    s.mer.gate10 = c.gate10;
    s.mer.toggleType10 = c.toggleType10;
    core::logf("meridian table: %s (%u registered classes, %u described)", s.merOk ? "ok" : "MISSING",
               (unsigned)s.mer.registered.size(), (unsigned)s.mer.classes.size());
    return s.renderOk && s.merOk;
}

}  // namespace di1::tables
