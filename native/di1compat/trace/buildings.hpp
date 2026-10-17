// spawntrace10's building probes: lot-state registrations and SpawnBuilding node firings, logged. core. Spawn threads.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"

namespace trace::buildings {

struct Config {
    bool on = false;   // spawntrace10
};

// Installs both probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr size_t kCallNode = 4;     // a Link_In call's node (call[1])
constexpr size_t kNodeName = 0x1C;  // the SpawnBuilding node's name
}  // namespace eng

typedef void(__fastcall* LotStateFn)(void* self, void* edx, int order, uint8_t isStatic, uint8_t enabled,
                                     uint32_t actor);
typedef void(__cdecl* HandlerFn)(void* call);

struct State {
    // originals: set once by installFeature in install()
    LotStateFn g_lotState = nullptr;
    HandlerFn g_node = nullptr;
};
inline State g_state;

// FUN_00cefe60, thiscall on the lot-state manager (ret 0x10), per lot registration.
inline void __fastcall hkLotState(void* self, void*, int order, uint8_t isStatic, uint8_t enabled, uint32_t actor)
{
    core::logf("spawntrace10: lot registered: order %d static %u enabled %u actor %08X", order, isStatic, enabled,
               actor);
    g_state.g_lotState(self, nullptr, order, isStatic, enabled, actor);
}

// FUN_011854b0, cdecl (call), per SpawnBuilding node firing: the node's Link_In.
inline void __cdecl hkNode(void* call)
{
    char n[128] = "";
    uint8_t* at = (uint8_t*)call + eng::kCallNode;
    uint8_t* node = core::readable(at, 4) ? *(uint8_t**)at : nullptr;
    if (node && core::readable(node + eng::kNodeName, 4))
        core::safeCopy(n, sizeof(n), *(const char**)(node + eng::kNodeName));
    core::logf("spawntrace10: SpawnBuilding node fired: '%s'", n);
    g_state.g_node(call);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("spawntrace10 buildings", {
        {"lot state register (spawntrace10)", 0x00CEFE60, "538B5C2408555657", (void*)&detail::hkLotState,
         (void**)&s.g_lotState},
        {"SpawnBuilding Link_In (spawntrace10)", 0x011854B0, "6AFF68????????64A100000000", (void*)&detail::hkNode,
         (void**)&s.g_node},
    });
}

}  // namespace trace::buildings
