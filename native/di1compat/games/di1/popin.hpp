// popin10 (PS-8): a 1.0 package's expendables pop in as in 1.0. core, sys.hpp. Package load threads, per definition.
#pragma once
#include <windows.h>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../sys.hpp"
#include "config.hpp"

namespace di1::popin {

// Installs the definition ctor hook when c has popin10 (H1); false when it was refused.
inline bool install(const Config& c);
// A package starts loading: a 1.0 one (not shipped) counts the definitions it stamps; per thread (B-10).
inline void packageLoading(const char* path, bool shipped);
// ... has loaded: its count is logged.
inline void packageLoaded(const char* path);

namespace detail {

typedef void*(__fastcall* ExpDefFn)(void* self, void* edx, void* entry);

struct State {
    ExpDefFn g_expDef = nullptr;   // set once by hook() in install()
};
inline State g_state;

// FUN_016022c0, thiscall (entry; ret 4): a definition's ctor; returns this, which FUN_01603930 uses. Per definition.
inline void* __fastcall hkExpDef(void* self, void*, void* entry)
{
    void* r = g_state.g_expDef(self, nullptr, entry);
    if (sys::g_pkg10 < 0) return r;
    const bool stamped = sys::applyPopin10(self);
    if (stamped) ++sys::g_pkg10;
    return r;
}

}  // namespace detail

inline void packageLoading(const char*, bool shipped) { sys::g_pkg10 = shipped ? -1 : 0; }

inline void packageLoaded(const char* path)
{
    if (sys::g_pkg10 >= 0)
        core::logf("expendables %s (a 1.0 package): %d entries without PopinDistance pop in as in 1.0", path,
                   sys::g_pkg10);
    sys::g_pkg10 = -1;
}

inline bool install(const Config& c)
{
    if (!c.popin10) return true;
    return core::hook("expendable definition ctor", 0x016022C0, "6AFF68????????64A100000000",
                      (void*)&detail::hkExpDef, (void**)&detail::g_state.g_expDef);
}

}  // namespace di1::popin
