// uitrace10 probes: pauses the game loop applies or lifts; MissionAccepted reaching tutorial elements. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../chain.hpp"

namespace trace::pause {

struct Config {
    bool on = false;   // uitrace10
};

// Installs the probes when c asks for them, or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kPaused = 0x0225F478;   // bool per player: FUN_0138da80 sets it, FUN_01385bb0 clears it
// the "Pause" message as FUN_014314b0 fills it: kind (4 pause, 5 unpause), +0xa pause menu, +0xb mission screen
constexpr size_t kMsgKind = 0;
constexpr size_t kMsgPauseMenu = 0xa;
constexpr size_t kMsgMissionScreen = 0xb;
constexpr size_t kMsgPlayer = 0x10;
// a tutorial element as FUN_00dae4e0 reads it: type, done bit, UseMissionActivator bit (FUN_00de37c0)
constexpr size_t kElementType = 0xC0;
constexpr size_t kElementDone = 0xC5;
constexpr uint8_t kDoneBit = 0x20;
constexpr size_t kElementUse = 0xC6;
constexpr uint8_t kActivatorBit = 0x04;
}  // namespace eng

typedef void(__fastcall* LoopPauseFn)(void* loop, void* edx, const uint8_t* msg);
typedef void(__cdecl* ElementAcceptedFn)(uint32_t target, uint32_t a2, uint32_t a3, uint8_t* element);

struct State {
    LoopPauseFn g_pauseOn = nullptr, g_pauseOff = nullptr;   // set once by installFeature in install()
    ElementAcceptedFn g_elementAccepted = nullptr;           // set once by installFeature in install()
};
inline State g_state;

// One line per pause change, with the flags of the four local players after it.
inline void logPause(const char* what, const uint8_t* msg)
{
    const uint8_t* f = (const uint8_t*)core::rt(eng::kPaused);
    core::logf("uitrace10: pause %s for player %d (message kind %d, pause menu %d, mission screen %d); "
               "paused players 0-3: %d %d %d %d",
               what, *(const int32_t*)(msg + eng::kMsgPlayer), *(const int32_t*)(msg + eng::kMsgKind),
               msg[eng::kMsgPauseMenu], msg[eng::kMsgMissionScreen], f[0], f[1], f[2], f[3]);
}

// GameCommonLoop vt+0x40 FUN_0138da80, thiscall (message; ret 4), main thread, per "Pause" request: pause on.
inline void __fastcall hkPauseOn(void* loop, void* edx, const uint8_t* msg)
{
    g_state.g_pauseOn(loop, edx, msg);
    if (msg) logPause("ON", msg);
}

// GameCommonLoop vt+0x48 FUN_01385bb0, thiscall (message; ret 4), main thread, per unpause request: pause off.
inline void __fastcall hkPauseOff(void* loop, void* edx, const uint8_t* msg)
{
    g_state.g_pauseOff(loop, edx, msg);
    if (msg) logPause("off", msg);
}

// FUN_00dae4e0, cdecl (target, a2, a3, element), main thread, per MissionLog_MissionAccepted an element hears.
inline void __cdecl hkElementAccepted(uint32_t target, uint32_t a2, uint32_t a3, uint8_t* element)
{
    g_state.g_elementAccepted(target, a2, a3, element);
    if (!element) return;
    const bool done = (element[eng::kElementDone] & eng::kDoneBit) != 0;
    const bool activator = (element[eng::kElementUse] & eng::kActivatorBit) != 0;
    core::logf("uitrace10: tutorial element %d heard MissionAccepted for %08X: done %d, activator check %d",
               *(const int32_t*)(element + eng::kElementType), target, done, activator);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return dbg::chain::installFeature("uitrace10", {
        {"game loop pause (uitrace10)", 0x0138DA80, "535556578BF9B9????????", (void*)&detail::hkPauseOn,
         (void**)&s.g_pauseOn},
        {"game loop unpause (uitrace10)", 0x01385BB0, "56578BF1E8????????8B7C240C", (void*)&detail::hkPauseOff,
         (void**)&s.g_pauseOff},
        {"tutorial element mission accepted (uitrace10)", 0x00DAE4E0, "568B74241485F6",
         (void*)&detail::hkElementAccepted, (void**)&s.g_elementAccepted},
    });
}

}  // namespace trace::pause
