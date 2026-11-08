// rowboatdrive10: a DriveRowboat avatar action goes out as Drive; 3.0's avatar tree has no rowboat riding.
// core. Game thread.
#pragma once
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"
#include "rowboatmount.hpp"

namespace di1::rowboatdrive {

// Installs the action translation when c.rowboatDrive10 is on; false when its hook was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_00c9d510, thiscall(agent component, int action UID, hash; ret 8): 'TriggerAction' to the agent's tree.

// DriveRowboat senders for PIR_Rowboat: Tardis transport rule 0xdf4f54, ForceMount 0x1182fb6, 0xdd8f79
constexpr uint32_t kTriggerAction = 0x00C9D510;
}  // namespace eng

// AvatarAction UIDs (startup2 gamedb/core/bitmasks.lua). avatar.btx/.bmx handle Drive, not DriveRowboat.
constexpr int kDriveRowboat = 3411;
constexpr int kDrive = 1448;

typedef void(__fastcall* TriggerActionFn)(void* agent, void* edx, int action, void* hash);

struct State {
    TriggerActionFn triggerAction = nullptr;   // the original; set once in install()
    std::atomic<bool> logged{false};
};
inline State g_state;

// FUN_00c9d510: DriveRowboat as Drive, so the avatar mounts the rowboat as the 3.0 vehicle rowboat10 made it.
inline void __fastcall hkTriggerAction(void* agent, void* edx, int action, void* hash)
{
    State& s = g_state;
    rowboatmount::onAction();   // an avatar swap after a rowboat mount: its actions run through here (6039)
    if (action == kDriveRowboat) {
        action = kDrive;
        if (!s.logged.exchange(true)) core::logf("rowboatdrive10: DriveRowboat sent as Drive (first mount)");
    }
    s.triggerAction(agent, edx, action, hash);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.rowboatDrive10) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("rowboatdrive10", {{"trigger action (rowboatdrive10)", detail::eng::kTriggerAction,
                                                    "8B442408568BF18B4C240850518BCEE8",
                                                    (void*)&detail::hkTriggerAction, (void**)&s.triggerAction}});
}

}  // namespace di1::rowboatdrive
