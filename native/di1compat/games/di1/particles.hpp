// vfx_universal10: 1.0's universal particle package (alias universal10) before each 1.0 zone package. session.
// Package loader threads.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/assets.hpp"
#include "config.hpp"
#include "session.hpp"

namespace di1::particles {

// Puts the alias in the session overlay, then installs the package hook, when c has vfx10 and vfxUniversal10 (H1).
inline bool install(const Config& c);

namespace detail {

constexpr size_t kPrefix = sizeof("particles/") - 1;

typedef uint32_t*(__cdecl* PkgFn)(const char* name, uint32_t flags);

struct State {
    std::string install10;     // set once in install()
    PkgFn g_pkg = nullptr;     // set once by hook() in install()
    // held across the engine's package lookup + insert, which is not atomic: one at a time (L3, RE-36)
    std::mutex uniLock;
};
inline State g_state;

// particles/<leaf>.zip is in the 1.0 install and not in 3.0's: the zone package of a 1.0 zone.
inline bool only10(const char* leaf)
{
    std::string a = g_state.install10 + "\\assets\\particles\\" + leaf + ".zip";
    std::string b = engine::assets::gameDir() + "\\assets\\particles\\" + leaf + ".zip";
    const bool in10 = GetFileAttributesA(a.c_str()) != INVALID_FILE_ATTRIBUTES;
    return in10 && GetFileAttributesA(b.c_str()) == INVALID_FILE_ATTRIBUTES;
}

// FUN_00672620, cdecl (name, flags), per package request: 1.0 loads universal before each zone's (FUN_00cc16a0).
inline uint32_t* __cdecl hkPkg(const char* name, uint32_t flags)
{
    State& s = g_state;
    char n[128];
    core::safeCopy(n, sizeof(n), name);
    // asked before every 1.0 zone package: a later registration of a hash wins (FUN_0066ef20), as in 1.0
    if (!_strnicmp(n, "particles/", kPrefix) && only10(n + kPrefix)) {
        std::lock_guard<std::mutex> lock(s.uniLock);
        uint32_t* u = s.g_pkg("particles/universal10", flags);
        core::logf("vfx: 1.0 universal package (particles/universal10) %s before %s",
                   u ? "registered" : "NOT registered", n);
    }
    uint32_t* r = s.g_pkg(name, flags);
    const uint32_t caller = core::toStaticVa((uintptr_t)_ReturnAddress());   // here: a helper would see its own caller
    core::logf("vfx: particle package %s flags=%X caller %08X -> %s", n, flags, caller, r ? "ok" : "none");
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.install10 = c.install10;
    if (!c.vfx10 || !c.vfxUniversal10) return true;
    session::Files alias;
    for (const char* e : {".particledata", ".mtb"})   // the alias hkPkg loads -> 1.0 particles/universal.zip
        alias.push_back({std::string("particles/universal10") + e, std::string("particles/universal/universal") + e});
    session::addOverlay(alias);   // before the hook: AssetOpen is live already
    return core::hook("particle package load (vfx)", 0x00672620, "6AFF68????????64A100000000", (void*)&detail::hkPkg,
                      (void**)&s.g_pkg);
}

}  // namespace di1::particles
