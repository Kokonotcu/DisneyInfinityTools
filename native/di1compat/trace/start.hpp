// Player start probes trace and spawntrace10 both install (no install of its own). core. Spawn threads, unverified.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include "../core/log.hpp"

namespace trace::start {

namespace detail {

namespace eng {
constexpr size_t kOutX = 4;           // the out transform's translation: out[4..6]
constexpr size_t kNodeZone = 0x88;    // StartPositionBase +0x88: its zone (0 matches any)
constexpr size_t kNodePlayers = 0x85; // +0x85: one bit per player
}  // namespace eng

constexpr LONG kLookupsLogged = 20;
constexpr LONG kQueriesLogged = 12;

typedef uint32_t(__fastcall* StartLookupFn)(void* self, void* edx, uint32_t player, uint32_t zone, float* out,
                                            uint32_t* o2, uint32_t flags);
typedef uint8_t(__fastcall* StartQueryFn)(uint8_t* self, void* edx, uint32_t a1, int player, uint32_t zone, float* out,
                                          uint32_t a5);

struct State {
    // originals: set once by hook() in trace's or spawntrace10's install(), never both (their switches exclude)
    StartLookupFn g_startLookup = nullptr;
    StartQueryFn g_startQuery = nullptr;
    std::atomic<LONG> lookups{0}, queries{0};   // budgets; any thread
};
inline State g_state;

inline float outAt(const float* out, size_t i) { return out ? out[eng::kOutX + i] : 0.f; }

// FUN_013a85d0, thiscall (ret 0x14): StartPositions lookup; 0 = no registered start (the caller uses the origin).
inline uint32_t __fastcall hkStartLookup(void* self, void*, uint32_t player, uint32_t zone, float* out, uint32_t* o2,
                                         uint32_t flags)
{
    State& s = g_state;
    uint32_t r = s.g_startLookup(self, nullptr, player, zone, out, o2, flags);
    const LONG nth = ++s.lookups;
    if (nth <= kLookupsLogged)
        core::logf("start lookup player %u zone %08X flags %X -> %u at (%.2f, %.2f, %.2f)", player, zone, flags, r,
                   outAt(out, 0), outAt(out, 1), outAt(out, 2));
    return r;
}

// FUN_0145f720, thiscall on a StartPositionBase (ret 0x14): matches zone and player, its vf+0x68 fills out.
inline uint8_t __fastcall hkStartQuery(uint8_t* self, void*, uint32_t a1, int player, uint32_t zone, float* out,
                                       uint32_t a5)
{
    State& s = g_state;
    uint8_t r = s.g_startQuery(self, nullptr, a1, player, zone, out, a5);
    const LONG nth = ++s.queries;
    if (nth <= kQueriesLogged)
        core::logf("start query node %p (zone %08X, players %02X) for player %d zone %08X -> %u at (%.2f, %.2f, %.2f)",
                   self, *(uint32_t*)(self + eng::kNodeZone), self[eng::kNodePlayers], player, zone, r, outAt(out, 0),
                   outAt(out, 1), outAt(out, 2));
    return r;
}

}  // namespace detail

}  // namespace trace::start
