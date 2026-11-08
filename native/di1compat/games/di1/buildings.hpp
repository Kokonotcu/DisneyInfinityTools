// building10: a 1.0 SpawnBuilding node builds on its lot, as 1.0's spawn did. engine/zones. Spawn threads.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/zones.hpp"
#include "config.hpp"
#include "rumpus.hpp"

namespace di1::buildings {

// Installs the lot spawn hook when c has building10 (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kNodeCall = 0x0118559A;   // the return address of the SpawnBuilding node's spawn call (FUN_011854b0)
constexpr uint32_t kLotStates = 0x02221348;  // *: the lot-state manager FUN_00cef120 passes on
constexpr uint32_t kLotMatrix = 0x00CD2150;  // FUN_00cd2150: thiscall (float out[16], lot) on the lot-state manager
constexpr size_t kTranslation = 12;          // m[12..14]: the lot matrix's position
}  // namespace eng

typedef uint32_t*(__fastcall* LotSpawnFn)(void* self, void* edx, uint32_t* out, int lot, const char* name,
                                          uint32_t useOwn);
typedef void*(__fastcall* LotMatrixFn)(void* self, void* edx, float* out, int lot);

struct State {
    bool building10 = false;           // set once in install()
    LotSpawnFn g_lotSpawn = nullptr;   // set once by hook() in install()
};
inline State g_state;

// 1.0 FUN_00b9f410's tail with 3.0's RumpusSpawn calls (FUN_00aa1350). -> the handle; 0, or one without an object
inline uint32_t buildOnLot(int lot, const char* name)
{
    void* lots = *(void**)core::rt(eng::kLotStates);
    void* zones = engine::zones::manager();
    if (!lots || !zones) {
        core::logf("building10: '%s' lot %d not built: lot or zone manager not up", name, lot);
        return 0;
    }
    alignas(16) float m[16];
    ((LotMatrixFn)core::rt(eng::kLotMatrix))(lots, nullptr, m, lot);
    const float* t = m + eng::kTranslation;
    const rumpus::Spawned sp = rumpus::spawn(name, m);
    if (!sp.object) {
        core::logf("building10: '%s' lot %d not built: no Toy Box object created by that name (handle %08X)", name,
                   lot, sp.handle);
        return sp.handle;   // the caller stores even a handle without an object (GP-39)
    }
    // not ported, no 3.0 twin known: 1.0's owner hand-over FUN_00d1bd60 and object flag 8
    core::logf("building10: '%s' built on lot %d at (%.1f, %.1f, %.1f), zone %08X, handle %08X", name, lot, t[0], t[1],
               t[2], sp.zone, sp.handle);
    return sp.handle;
}

// FUN_00cef120, thiscall (out, lot, StringT name by value, useOwnMatrix; ret 0x10), spawn threads, per lot spawn.
inline uint32_t* __fastcall hkLotSpawn(void* self, void*, uint32_t* out, int lot, const char* name, uint32_t useOwn)
{
    // 3.0's spawn sets only the pending name (*out = 0); the node's call builds as 1.0's did, the timer's not
    State& s = g_state;
    char n[128];
    core::safeCopy(n, sizeof(n), name);   // before the call: the callee releases the by-value string
    bool node = _ReturnAddress() == (void*)core::rt(eng::kNodeCall);   // here: a helper would see its own caller
    uint32_t* r = s.g_lotSpawn(self, nullptr, out, lot, name, useOwn);
    uint32_t h = out ? *out : 0;
    const bool unbuilt = node && out && !h;
    const bool lotMatrix = lot >= 0 && !(useOwn & 0xff) && n[0];
    if (s.building10 && unbuilt && lotMatrix) *out = buildOnLot(lot, n);
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.building10 = c.building10;
    if (!c.building10) return true;
    return core::hook("building lot spawn (building10)", 0x00CEF120, "538BDC83EC0883E4F083C404558B6B04",
                      (void*)&detail::hkLotSpawn, (void**)&s.g_lotSpawn);
}

}  // namespace di1::buildings
