// spawner10 (SYS-3): 1.0 SpawnerQuarterback groups get their AIQuarterback base from 3.0's QB factory. core.
#pragma once
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::spawner {

// Installs the QB factory hook when c has spawner10 (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

// 3.0's QB type table 0x2088228 names SpawnerQuarterback type 10, but its factory has no case 10 (returns 0).
constexpr int kSpawnerQb = 10;
// The 1.0 type subclasses AIQuarterback (1.0 vtable 0x146f3e4): 3.0's type 0 (0xc0 B, ctor FUN_0153e1b0).
constexpr int kAiQb = 0;

typedef void*(__cdecl* QbFactoryFn)(int type, int param);

struct State {
    QbFactoryFn g_qbFactory = nullptr;   // set once by hook() in install()
};
inline State g_state;

// FUN_010c2f70, cdecl (type, param) -> QB or null; SetActive / AddActor call through it unchecked. Per QB creation.
inline void* __cdecl hkQbFactory(int type, int param)
{
    if (type != kSpawnerQb) return g_state.g_qbFactory(type, param);
    void* qb = g_state.g_qbFactory(kAiQb, param);
    core::logf("spawner10: SpawnerQuarterback (QB type 10, no 3.0 factory case) made as its AIQuarterback base -> %p",
               qb);
    return qb;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.spawner10) return true;
    return core::hook("quarterback factory (spawner10)", 0x010C2F70, "64A1000000006AFF68????????50",
                      (void*)&detail::hkQbFactory, (void**)&detail::g_state.g_qbFactory);
}

}  // namespace di1::spawner
