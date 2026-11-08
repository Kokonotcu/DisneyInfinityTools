// bhteam10: 3.0's blockhead team check answers 'not on the team' for an actor without the team component.
// core. Game thread.
#pragma once
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::bhteam {

// Installs the team check guard when c.bhteam10 is on; false when its hook was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_0106c440, thiscall(group, component; ret 4): component in the BlockheadQuarterback group
constexpr uint32_t kTeamHas = 0x0106C440;
}  // namespace eng

typedef bool(__fastcall* TeamHasFn)(void* group, void* edx, void* component);

struct State {
    TeamHasFn teamHas = nullptr;   // the original; set once in install()
    std::atomic<bool> logged{false};
};
inline State g_state;

// 3.0's other callers skip a null component; BH_IsOnCombatTeam (FUN_00eadf10) passes it (1.0 sky spawner: AV)
inline bool __fastcall hkTeamHas(void* group, void* edx, void* component)
{
    State& s = g_state;
    if (component) return s.teamHas(group, edx, component);
    if (!s.logged.exchange(true)) core::logf("bhteam10: team check of an actor without the team component -> no");
    return false;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.bhteam10) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("bhteam10", {{"blockhead team has (bhteam10)", detail::eng::kTeamHas,
                                              "68C81E0000E8????????8B4C2408", (void*)&detail::hkTeamHas,
                                              (void**)&s.teamHas}});
}

}  // namespace di1::bhteam
