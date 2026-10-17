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

namespace di1::buildings {

// Installs the lot spawn hook when c has building10 or spawntrace10 (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kNodeCall = 0x0118559A;   // the return address of the SpawnBuilding node's spawn call (FUN_011854b0)
constexpr uint32_t kLotStates = 0x02221348;  // *: the lot-state manager FUN_00cef120 passes on
constexpr uint32_t kLotMatrix = 0x00CD2150;  // FUN_00cd2150: thiscall (float out[16], lot) on the lot-state manager
constexpr uint32_t kZoneAt = 0x01390E30;     // FUN_01390e30: thiscall (out zone, pos, hint) on the zone manager
constexpr uint32_t kStrCtor = 0x00418310;    // FUN_00418310: a StringT's ctor (thiscall)
constexpr uint32_t kSpawner = 0x00A9DE70;    // FUN_00a9de70: cdecl () -> what RumpusSpawn creates through
constexpr uint32_t kCreate = 0x00F7E3E0;     // FUN_00f7e3e0: create by name (1.0's FUN_00d1da30), ret 0x1c
constexpr uint32_t kObject = 0x00F32070;     // FUN_00f32070: cdecl (handle, 1) -> the object
constexpr uint32_t kHasFlag = 0x00F9E170;    // FUN_00f9e170: thiscall (bit) on an object
constexpr uint32_t kMove = 0x00F9DBD0;       // FUN_00f9dbd0: cdecl (handle, matrix, notify): a live object
constexpr uint32_t kMatrixCopy = 0x00445930; // FUN_00445930: thiscall (src) on a matrix
constexpr uint32_t kLiveFlag = 0xC;          // the object is live: move it, else copy its matrix
constexpr size_t kObjectMatrix = 0x40;
constexpr size_t kActivate = 0x1C / 4;       // the object's vt slot of activate(1), RumpusSpawn's last step
constexpr size_t kTranslation = 12;          // m[12..14]: the lot matrix's position
}  // namespace eng

typedef uint32_t*(__fastcall* LotSpawnFn)(void* self, void* edx, uint32_t* out, int lot, const char* name,
                                          uint32_t useOwn);
typedef void*(__fastcall* LotMatrixFn)(void* self, void* edx, float* out, int lot);
typedef void(__fastcall* ZoneAtFn)(void* self, void* edx, uint32_t* out, const float* pos, uint32_t hint);
typedef void*(__cdecl* SpawnerFn)();
typedef uint32_t*(__fastcall* CreateFn)(void* self, void* edx, uint32_t* out, char* name, uint32_t parent,
                                        uint32_t zone, uint32_t a5, uint32_t a6, uint32_t a7);
typedef uint8_t*(__cdecl* ObjFn)(uint32_t handle, uint32_t a2);
typedef uint8_t(__fastcall* FlagFn)(void* self, void* edx, uint32_t bit);
typedef void(__cdecl* MoveFn)(uint32_t handle, const float* m, uint32_t notify);
typedef void*(__fastcall* MatCopyFn)(void* self, void* edx, const float* src);
typedef void*(__fastcall* StrCtorFn)(void* self, void* edx, const char* s);
typedef void(__fastcall* ActivateFn)(void* self, void* edx, uint32_t on);

struct State {
    bool building10 = false, spawnTrace10 = false;   // set once in install()
    LotSpawnFn g_lotSpawn = nullptr;                 // set once by hook() in install()
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
    alignas(16) float pos[4] = {t[0], t[1], t[2], 1.f};
    uint32_t zone = 0;
    ((ZoneAtFn)core::rt(eng::kZoneAt))(zones, nullptr, &zone, pos, 0);
    char* str = nullptr;   // StringT by value: the callee releases it
    ((StrCtorFn)core::rt(eng::kStrCtor))(&str, nullptr, name);
    uint32_t h = 0;
    // parent 0, a5..a7 = 0, 1, 0: as RumpusSpawn passes them
    ((CreateFn)core::rt(eng::kCreate))(((SpawnerFn)core::rt(eng::kSpawner))(), nullptr, &h, str, 0, zone, 0, 1, 0);
    uint8_t* obj = h ? ((ObjFn)core::rt(eng::kObject))(h, 1) : nullptr;
    if (!obj) {
        core::logf("building10: '%s' lot %d not built: no Toy Box object created by that name (handle %08X)", name,
                   lot, h);
        return h;   // the caller stores even a handle without an object (GP-39)
    }
    if (((FlagFn)core::rt(eng::kHasFlag))(obj, nullptr, eng::kLiveFlag)) ((MoveFn)core::rt(eng::kMove))(h, m, 1);
    else ((MatCopyFn)core::rt(eng::kMatrixCopy))(obj + eng::kObjectMatrix, nullptr, m);
    obj = ((ObjFn)core::rt(eng::kObject))(h, 1);
    if (obj) ((ActivateFn)(*(void***)obj)[eng::kActivate])(obj, nullptr, 1);
    // not ported, no 3.0 twin known: 1.0's owner hand-over FUN_00d1bd60 and object flag 8
    core::logf("building10: '%s' built on lot %d at (%.1f, %.1f, %.1f), zone %08X, handle %08X", name, lot, t[0], t[1],
               t[2], zone, h);
    return h;
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
    if (s.spawnTrace10)
        core::logf("spawntrace10: lot spawn '%s' lot %d own matrix %u from %s -> handle %08X", n, lot, useOwn & 0xff,
                   node ? "SpawnBuilding node" : "other caller", h);
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
    s.spawnTrace10 = c.spawnTrace10;
    if (!c.building10 && !c.spawnTrace10) return true;
    return core::hook("building lot spawn (building10)", 0x00CEF120, "538BDC83EC0883E4F083C404558B6B04",
                      (void*)&detail::hkLotSpawn, (void**)&s.g_lotSpawn);
}

}  // namespace di1::buildings
