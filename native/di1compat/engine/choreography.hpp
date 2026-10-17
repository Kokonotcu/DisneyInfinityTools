// A choreography's start, told to the game before and after it runs. core. Threads unverified.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::choreography {

// cho: the instance; name: its path (at most 99 characters); flags: its +0x1b4 flags (0x20: audio sync).
using Notify = std::function<void(const uint8_t* cho, const char* name, uint32_t flags)>;

struct Config {
    Notify starting;   // before the start; empty: none
    Notify started;    // after it; empty: none
};

// Installs the start hook when c has a callback (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr size_t kPath = 0x38;    // the instance's path (char*)
constexpr size_t kFlags = 0x1b4;  // u32 flags
constexpr size_t kNameMax = 100;
}  // namespace eng

typedef void(__fastcall* ChoStartFn)(uint8_t* self, void* edx);

struct State {
    ChoStartFn g_choStart = nullptr;   // set once by hook() in install()
    Config cfg;                        // set once in install()
};
inline State g_state;

// FUN_01449390, thiscall on a choreography instance: its start. Per choreography played.
inline void __fastcall hkChoStart(uint8_t* self, void*)
{
    State& s = g_state;
    char name[eng::kNameMax] = "";
    core::safeCopy(name, sizeof(name), *(const char**)(self + eng::kPath));
    const uint32_t flags = *(uint32_t*)(self + eng::kFlags);
    if (s.cfg.starting) s.cfg.starting(self, name, flags);
    s.g_choStart(self, nullptr);
    if (s.cfg.started) s.cfg.started(self, name, flags);
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.starting && !c.started) return true;
    s.cfg = c;
    return core::hook("choreography start (cinesync10 / breaktrace)", 0x01449390, "83EC18568BF1E8",
                      (void*)&detail::hkChoStart, (void**)&s.g_choStart);
}

}  // namespace engine::choreography
