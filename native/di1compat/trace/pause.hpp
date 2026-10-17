// uitrace10 probe: every pause the game loop applies or lifts, with its player and kind. core. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../core/hooks.hpp"
#include "../core/log.hpp"

namespace trace::pause {

struct Config {
    bool on = false;   // uitrace10
};

// Installs both probes when c asks for them, or neither (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kPaused = 0x0225F478;   // bool per player: FUN_0138da80 sets it, FUN_01385bb0 clears it
// the "Pause" message as FUN_014314b0 fills it: kind (4 pause, 5 unpause), +0xa pause menu, +0xb mission screen
constexpr size_t kMsgKind = 0;
constexpr size_t kMsgPauseMenu = 0xa;
constexpr size_t kMsgMissionScreen = 0xb;
constexpr size_t kMsgPlayer = 0x10;
}  // namespace eng

typedef void(__fastcall* LoopPauseFn)(void* loop, void* edx, const uint8_t* msg);

struct State {
    LoopPauseFn g_pauseOn = nullptr, g_pauseOff = nullptr;   // set once by installFeature in install()
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

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("uitrace10", {
        {"game loop pause (uitrace10)", 0x0138DA80, "535556578BF9B9????????", (void*)&detail::hkPauseOn,
         (void**)&s.g_pauseOn},
        {"game loop unpause (uitrace10)", 0x01385BB0, "56578BF1E8????????8B7C240C", (void*)&detail::hkPauseOff,
         (void**)&s.g_pauseOff},
    });
}

}  // namespace trace::pause
