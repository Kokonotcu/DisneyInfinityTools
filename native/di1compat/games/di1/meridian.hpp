// 1.0's Meridian classes registered after 3.0's: aliases10 twins, native10 classes. mer, mernode, mission. Boot thread.
#pragma once
#include <windows.h>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../formats/mer.hpp"
#include "../../engine/mernode.hpp"
#include "mer.hpp"
#include "mission.hpp"

namespace di1::meridian {

struct Config {
    bool aliases = false;        // aliases10: mission.hpp kAliases, each with its 3.0 twin's template
    bool native = false;         // native10: mernode.hpp's native classes
    mer::Table* mer = nullptr;   // the loaded mer table; nullptr: none, hook off
    bool mana = false;           // mana10 (with aliases10): SpawnManaNode through SpawnGroupKiller's template
};

// Installs the class registry hook when c registers classes (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

typedef void(__cdecl* VoidFn)();

struct State {
    VoidFn g_merRegistry = nullptr;   // set once by hook() in install()
    Config cfg;                       // set once in install()
};
inline State g_state;

// aliases10: each 1.0 class through its twin's template (H4 open: called without a prologue check, X-12).
inline void registerAliases(mer::Table& mer, bool mana10)
{
    typedef void(__cdecl * RegFn)(const char* name);
    for (const mission::Alias* a : mission::aliases(mana10)) {
        ((RegFn)core::rt(a->templateVa))(a->name);
        core::logf("meridian: 1.0 class %s registered with the template of %s", a->name, a->twin);
    }
    mer::addAliases(mer, mana10);
}

// native10: every native class added with mernode::add before this.
inline void registerNatives(mer::Table& mer)
{
    const int n = mernode::registerAll(core::rt, static_cast<void (*)(const char*, ...)>(core::logf), mer);
    core::logf("meridian native: %d of %d classes registered", n, mernode::count());
}

// FUN_0178e330, cdecl: the class registry, once at boot (FUN_013aaba0). The transcoder sees 1.0's classes after it.
inline void __cdecl hkMerRegistry()
{
    State& s = g_state;
    s.g_merRegistry();
    if (s.cfg.aliases) registerAliases(*s.cfg.mer, s.cfg.mana);
    if (s.cfg.native) registerNatives(*s.cfg.mer);
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    const bool classes = c.aliases || c.native;
    if (!classes || !c.mer) return true;
    s.cfg = c;
    return core::hook("Meridian class registry", 0x0178E330, "6A006A0068????????68????????",
                      (void*)&detail::hkMerRegistry, (void**)&s.g_merRegistry);
}

}  // namespace di1::meridian
