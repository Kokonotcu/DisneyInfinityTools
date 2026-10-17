// popin10 (PS-8): a 1.0 package's expendables pop in as in 1.0. core. Package load threads, per definition.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::popin {

// 100 km: every distance inside a zone. 1.0 has no pop-in; its socket handler FUN_00f8b930 binds every expendable.
inline constexpr float kPopin10 = 100000.0f;

// Installs the definition ctor hook when c has popin10 (H1); false when it was refused.
inline bool install(const Config& c);
// A package starts loading: a 1.0 one (not shipped) counts the definitions it stamps; per thread (B-10).
inline void packageLoading(const char* path, bool shipped);
// ... has loaded: its count is logged.
inline void packageLoaded(const char* path);
// Whether a definition carries applyPopin10's stamp; the caller has checked def + 0x9c is readable.
inline bool isStamped(const void* def);

namespace detail {

namespace eng {
// ExpendablesDefBase (ctor FUN_016022c0): range squared at +0x9c, (range + 14)^2 at +0xa0; 0 without PopinDistance
constexpr size_t kRangeSquared = 0x9c;
constexpr float kRangeMargin = 14;
}  // namespace eng

typedef void*(__fastcall* ExpDefFn)(void* self, void* edx, void* entry);

struct State {
    ExpDefFn g_expDef = nullptr;   // set once by hook() in install()
    // Per thread (B-10): definitions stamped in the 1.0 package loading on this thread; -1: none
    inline static thread_local int pkg10 = -1;
};
inline State g_state;

// 3.0's own packages leave PopinDistance out on purpose, so only 1.0 packages' entries get this (FORMATS 11).
inline bool applyPopin10(void* def)
{
    float* f = (float*)((uint8_t*)def + eng::kRangeSquared);
    if (f[0] != 0 || f[1] != 0) return false;
    f[0] = kPopin10 * kPopin10;
    f[1] = (kPopin10 + eng::kRangeMargin) * (kPopin10 + eng::kRangeMargin);
    return true;
}

// FUN_016022c0, thiscall (entry; ret 4): a definition's ctor; returns this, which FUN_01603930 uses. Per definition.
inline void* __fastcall hkExpDef(void* self, void*, void* entry)
{
    void* r = g_state.g_expDef(self, nullptr, entry);
    if (g_state.pkg10 < 0) return r;
    const bool stamped = applyPopin10(self);
    if (stamped) ++g_state.pkg10;
    return r;
}

}  // namespace detail

inline void packageLoading(const char*, bool shipped) { detail::g_state.pkg10 = shipped ? -1 : 0; }

inline void packageLoaded(const char* path)
{
    detail::State& s = detail::g_state;
    if (s.pkg10 >= 0)
        core::logf("expendables %s (a 1.0 package): %d entries without PopinDistance pop in as in 1.0", path, s.pkg10);
    s.pkg10 = -1;
}

inline bool isStamped(const void* def)
{
    return *(const float*)((const uint8_t*)def + detail::eng::kRangeSquared) == kPopin10 * kPopin10;
}

inline bool install(const Config& c)
{
    if (!c.popin10) return true;
    return core::hook("expendable definition ctor", 0x016022C0, "6AFF68????????64A100000000",
                      (void*)&detail::hkExpDef, (void**)&detail::g_state.g_expDef);
}

}  // namespace di1::popin
