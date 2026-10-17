// accept10: a 1.0 mission activated in the world counts as accepted for the tutorial's log step. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/players.hpp"
#include "config.hpp"
#include "playsets.hpp"

namespace di1::accept {

// Installs the mission activation hook when c has accept10 (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kAcceptedEvent = 0x0221CDCC;   // u16: MissionLog_MissionAccepted's id, as FUN_00b67560 posts it
constexpr uint32_t kPostEvent = 0x0069EFE0;       // FUN_0069efe0: cdecl (id, data, a3)
constexpr size_t kActivation = 0x3DB;             // the MissionNode's one-time activation flag: bit 7
constexpr uint8_t kActivated = 0x80;
constexpr size_t kContext = 0x140;                // the mission's context: its name at +0x10
constexpr size_t kContextName = 0x10;
}  // namespace eng

typedef void(__fastcall* MissionActivateFn)(uint8_t* mission, void* edx);
typedef void(__cdecl* PostEventFn)(uint32_t id, uint32_t data, uint32_t a3);

struct State {
    MissionActivateFn g_missionActivate = nullptr;   // set once by hook() in install()
};
inline State g_state;

// FUN_015bcc10, thiscall (a vtable method), main thread, per activation call; its work happens once per mission.
inline void __fastcall hkMissionActivate(uint8_t* mission, void* edx)
{
    bool first = mission && !(mission[eng::kActivation] & eng::kActivated);
    g_state.g_missionActivate(mission, edx);
    // element 63 completes only on MissionAccepted (FUN_00dae4e0), and 3.0's log can be left before accepting
    if (!first || !(mission[eng::kActivation] & eng::kActivated) || !playsets::currentZone10()) return;
    uint8_t* pm = engine::players::manager();
    const uint8_t* players = pm ? engine::players::table(pm) : nullptr;
    uint32_t ev = *(const uint16_t*)core::rt(eng::kAcceptedEvent);
    char name[96] = "";
    uint8_t* ctx = *(uint8_t**)(mission + eng::kContext);
    core::safeCopy(name, sizeof(name), ctx ? *(const char**)(ctx + eng::kContextName) : nullptr);
    for (int p = 0; players && p < engine::players::eng::kMaxPlayers; ++p) {
        if (!engine::players::isLocal(players, p)) continue;   // the players FUN_015bcc10 selects for
        uint32_t id = engine::players::playerId(pm, p);
        ((PostEventFn)core::rt(eng::kPostEvent))(ev, id, 0);
        core::logf("accept10: %s activated in a 1.0 zone -> tutorial 'mission accepted' event %u for player %d", name,
                   ev, p);
    }
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.accept10) return true;
    return core::hook("mission activation (accept10)", 0x015BCC10, "83EC14568BF18A86DB03000084C0",
                      (void*)&detail::hkMissionActivate, (void**)&detail::g_state.g_missionActivate);
}

}  // namespace di1::accept
