// mounttrace10 probe: vehicle mounts. Tardis transport rule runs, the actions agents get after a Drive,
// VehicleManager connects. core. Game thread.
#pragma once
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../chain.hpp"

namespace trace::mounttrace {

struct Config {
    bool on = false;   // mounttrace10
};

// Installs the probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_00c9d510, thiscall(agent component, int action UID, hash; ret 8): 'TriggerAction' (di1compat rowboatdrive10)
constexpr uint32_t kTriggerAction = 0x00C9D510;
// FUN_00df4cb0, TransportRule vtable 0x1d16f70 slot 4, thiscall(rule, a1, a2; ret 8): picks and sends the mount
constexpr uint32_t kTransportApply = 0x00DF4CB0;
// FUN_00cd2b20, thiscall(VehicleManager [0x222146c], a1, a2, a3, bool; ret 0x10), from VehicleManager_Biped..Connect
constexpr uint32_t kConnect = 0x00CD2B20;
constexpr size_t kAgentOwner = 4;   // agent component +4: its agent; agent +4: the actor handle (0x00c9d524)
constexpr size_t kAgentHandle = 4;
}  // namespace eng

// AvatarAction UIDs (startup2 gamedb/core/bitmasks.lua) that open the window of logged actions
constexpr int kDrive = 1448;
constexpr int kDriveRowboat = 3411;
constexpr int kWindow = 60;   // TriggerAction calls logged after each Drive

typedef void(__fastcall* TriggerActionFn)(uint8_t* agent, void* edx, int action, void* hash);
typedef void(__fastcall* ApplyFn)(void* rule, void* edx, uint32_t a1, uint32_t a2);
typedef void(__fastcall* ConnectFn)(void* mgr, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);

struct State {
    TriggerActionFn g_triggerAction = nullptr;   // set once by installFeature in install()
    ApplyFn g_apply = nullptr;
    ConnectFn g_connect = nullptr;
    std::atomic<int> left{0};                    // TriggerAction lines still to log
};
inline State g_state;

// The actor handle behind an agent component (as 0x00c9d524 reads it), 0 when a step is null or unreadable (H8).
inline uint32_t agentHandle(const uint8_t* agent)
{
    if (!agent || !core::readable(agent + eng::kAgentOwner, sizeof(void*))) return 0;
    const uint8_t* owner = *(uint8_t* const*)(agent + eng::kAgentOwner);
    if (!owner || !core::readable(owner + eng::kAgentHandle, sizeof(uint32_t))) return 0;
    return *(const uint32_t*)(owner + eng::kAgentHandle);
}

inline void __fastcall hkTriggerAction(uint8_t* agent, void* edx, int action, void* hash)
{
    State& s = g_state;
    if (action == kDrive || action == kDriveRowboat) s.left = kWindow;
    if (s.left.load() > 0) {
        --s.left;
        core::logf("mounttrace10: TriggerAction %d to agent %p (actor %08X)", action, agent, agentHandle(agent));
    }
    s.g_triggerAction(agent, edx, action, hash);
}

inline void __fastcall hkApply(void* rule, void* edx, uint32_t a1, uint32_t a2)
{
    core::logf("mounttrace10: transport rule %p applies (%08X, %08X)", rule, a1, a2);
    g_state.g_apply(rule, edx, a1, a2);
}

inline void __fastcall hkConnect(void* mgr, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    core::logf("mounttrace10: VehicleManager connect (%08X, %08X, %08X, %u)", a1, a2, a3, a4 & 0xff);
    g_state.g_connect(mgr, edx, a1, a2, a3, a4);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return dbg::chain::installFeature("mounttrace10", {
        {"trigger action (mounttrace10)", detail::eng::kTriggerAction, "8B442408568BF18B4C240850518BCEE8",
         (void*)&detail::hkTriggerAction, (void**)&s.g_triggerAction},
        {"transport rule apply (mounttrace10)", detail::eng::kTransportApply, "6AFF68B0A3A30164A100000000506489",
         (void*)&detail::hkApply, (void**)&s.g_apply},
        {"vehicle connect (mounttrace10)", detail::eng::kConnect, "6AFF68B818A30164A100000000506489",
         (void*)&detail::hkConnect, (void**)&s.g_connect},
    });
}

}  // namespace trace::mounttrace
