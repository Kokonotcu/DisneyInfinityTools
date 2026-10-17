// building10: a 1.0 SpawnBuilding node builds its building on the lot again (runtime side). Included by di1compat.cpp
// after its helpers (logf, hook, rt, g_cfg, safeCopy).
//
// 1.0 playsets place their towns through building lots: a lot actor from the world's .ap (AddComponent_BuildingLot,
// creationKey BuildingLot, order N) and a SpawnBuilding node fed by the lot's Actor node ('Source') and fired at start
// (Cars, LR, TS, MU, Pirates: Catalyst / Gate / BatchImpulse; Metroville's four fire only from a DebugInput). Its
// Link_In handler (1.0 FUN_00d3ab50, 3.0 FUN_011854b0, same code) asks the lot-state manager for the lot's key
// (FUN_00cc9d20: record[2] == actor -> record[0] = order) and calls BuildingLotManager's spawn (1.0 FUN_00b9f410,
// 3.0 FUN_00cef120; thiscall (out handle, lot key, StringT name by value, char useOwnMatrix), ret 0x10).
//   1.0: pending name on the lot (FUN_00b8c640), then the building is CREATED: the lot's matrix (FUN_00b68d00),
//        RumpusUIBridge FUN_00cc73c0 -> FUN_00a7b120: zone at the lot position (FUN_00ef1f80), create by name
//        (FUN_00a19600 -> FUN_00d1da30(out, name, 0, zone, 0, 1, &matrix)), then the object's vf+0x18(1); out = handle.
//   3.0: pending name on the lot (FUN_00ce4f20, same as 1.0's), the lot matrix still computed and never used, *out = 0.
// Nothing in 3.0 reads the pending name to build (its readers are UI getters and the BuildingActor data source), so no
// 1.0 building actor ever exists: no model request (Cars: none of cr_flos/cr_luigis/... opened), no building
// collision, and LR's datamap-preloaded buildings never placed (floating water tower / saloon doors on bare lots).
// 3.0 kept the creation itself: its Lua native RumpusSpawn (FUN_00aa1350, 1.0's is FUN_00a1bcd0 and calls the same
// FUN_00d1da30) creates by name through FUN_00a9de70 -> FUN_00f7e3e0 (twin of FUN_00d1da30, ret 0x1c) with the zone
// at the position (FUN_01390e30 on the zone manager, twin of FUN_00ef1f80), stores the matrix (FUN_00f9dbd0 on a live
// object, else obj+0x40) and activates with vf+0x1c(1) (1.0's vf+0x18(1): RumpusSpawn does the same in both games).
// So after 3.0's spawn returns no handle for a call from the SpawnBuilding node, the building is created that way at
// the lot's matrix and its handle returned, and the node's own tail (FUN_00e21870 / FUN_00e08020) runs as in 1.0.
// Only the node's call site (0x0118559A) is completed: 3.0's construction timer (FUN_00cfd750) calls the same spawn.
// Not ported (unknown 3.0 twins): 1.0's owner hand-over FUN_00d1bd60 and the object flag 8 (FUN_00d10890(8, 1)).
#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include "core/hooks.hpp"
#include "core/log.hpp"
#include "core/memcheck.hpp"
#include "games/di1/config.hpp"
#include "sys.hpp"

namespace bldrt {

// core's services this file calls by their short names (declarations: N26 forbids only using namespace)
using core::hook;
using core::installFeature;
using core::logf;
using core::rt;
using core::safeCopy;

struct State {
    di1::Config cfg;   // the keys its hooks read: set once in install(), before them
};
inline State g_state;

typedef uint32_t*(__fastcall* LotSpawnFn)(void* self, void* edx, uint32_t* out, int lot, const char* name, uint32_t useOwn);
typedef void*(__fastcall* LotMatrixFn)(void* self, void* edx, float* out, int lot);                              // FUN_00cd2150
typedef void(__fastcall* ZoneAtFn)(void* self, void* edx, uint32_t* out, const float* pos, uint32_t hint);       // FUN_01390e30
typedef void*(__cdecl* GetterFn)();                                                                              // FUN_00a9de70
typedef uint32_t*(__fastcall* CreateFn)(void* self, void* edx, uint32_t* out, char* name, uint32_t parent, uint32_t zone,
                                        uint32_t a5, uint32_t a6, uint32_t a7);                                  // FUN_00f7e3e0
typedef uint8_t*(__cdecl* ObjFn)(uint32_t handle, uint32_t a2);                                                  // FUN_00f32070
typedef uint8_t(__fastcall* FlagFn)(void* self, void* edx, uint32_t bit);                                        // FUN_00f9e170
typedef void(__cdecl* MoveFn)(uint32_t handle, const float* m, uint32_t notify);                                 // FUN_00f9dbd0
typedef void*(__fastcall* MatCopyFn)(void* self, void* edx, const float* src);                                   // FUN_00445930
typedef void*(__fastcall* StrCtorFn)(void* self, void* edx, const char* s);                                      // FUN_00418310
typedef void(__fastcall* ActivateFn)(void* self, void* edx, uint32_t on);
typedef void(__fastcall* LotStateFn)(void* self, void* edx, int order, uint8_t isStatic, uint8_t enabled, uint32_t actor);
typedef void(__cdecl* HandlerFn)(void* call);

static LotSpawnFn g_lotSpawn;
static LotStateFn g_lotState;
static HandlerFn g_node;

// 1.0 FUN_00b9f410's tail with 3.0's RumpusSpawn calls; returns the building's handle, 0 if not created
static uint32_t buildOnLot(int lot, const char* name)
{
    void* lots = *(void**)rt(0x02221348);   // lot-state manager (FUN_00cef120 passes it to FUN_00cd2150 / FUN_00ce4f20)
    void* zones = *(void**)rt(0x0225F548);
    if (!lots || !zones) { logf("building10: '%s' lot %d not built: lot or zone manager not up", name, lot); return 0; }
    alignas(16) float m[16];
    ((LotMatrixFn)rt(0x00CD2150))(lots, nullptr, m, lot);
    alignas(16) float pos[4] = {m[12], m[13], m[14], 1.f};
    uint32_t zone = 0;
    ((ZoneAtFn)rt(0x01390E30))(zones, nullptr, &zone, pos, 0);
    char* str = nullptr;   // StringT by value: the callee releases it
    ((StrCtorFn)rt(0x00418310))(&str, nullptr, name);
    uint32_t h = 0;
    ((CreateFn)rt(0x00F7E3E0))(((GetterFn)rt(0x00A9DE70))(), nullptr, &h, str, 0, zone, 0, 1, 0);
    uint8_t* obj = h ? ((ObjFn)rt(0x00F32070))(h, 1) : nullptr;
    if (!obj) {
        logf("building10: '%s' lot %d not built: no Toy Box object created by that name (handle %08X)", name, lot, h);
        return h;
    }
    if (((FlagFn)rt(0x00F9E170))(obj, nullptr, 0xc)) ((MoveFn)rt(0x00F9DBD0))(h, m, 1);
    else ((MatCopyFn)rt(0x00445930))(obj + 0x40, nullptr, m);
    obj = ((ObjFn)rt(0x00F32070))(h, 1);
    if (obj) ((ActivateFn)(*(void***)obj)[0x1c / 4])(obj, nullptr, 1);
    logf("building10: '%s' built on lot %d at (%.1f, %.1f, %.1f), zone %08X, handle %08X", name, lot, m[12], m[13], m[14], zone, h);
    return h;
}

static uint32_t* __fastcall hkLotSpawn(void* self, void*, uint32_t* out, int lot, const char* name, uint32_t useOwn)
{
    char n[128];
    safeCopy(n, sizeof(n), name);   // before the call: the callee releases the by-value string
    bool node = _ReturnAddress() == (void*)rt(0x0118559A);   // the SpawnBuilding node's call (FUN_011854b0)
    uint32_t* r = g_lotSpawn(self, nullptr, out, lot, name, useOwn);
    uint32_t h = out ? *out : 0;
    if (g_state.cfg.spawnTrace10)
        logf("spawntrace10: lot spawn '%s' lot %d own matrix %u from %s -> handle %08X", n, lot, useOwn & 0xff,
             node ? "SpawnBuilding node" : "other caller", h);
    if (g_state.cfg.building10 && node && out && !h && lot >= 0 && !(useOwn & 0xff) && n[0]) *out = buildOnLot(lot, n);
    return r;
}

// spawntrace10: lot-state records (FUN_00cefe60 on the lot-state manager, ret 0x10) and the node firing
static void __fastcall hkLotState(void* self, void*, int order, uint8_t isStatic, uint8_t enabled, uint32_t actor)
{
    logf("spawntrace10: lot registered: order %d static %u enabled %u actor %08X", order, isStatic, enabled, actor);
    g_lotState(self, nullptr, order, isStatic, enabled, actor);
}
static void __cdecl hkNode(void* call)
{
    char n[128] = "";
    uint8_t* node = sys::readable((uint8_t*)call + 4, 4) ? *(uint8_t**)((uint8_t*)call + 4) : nullptr;
    if (node && sys::readable(node + 0x1c, 4)) safeCopy(n, sizeof(n), *(const char**)(node + 0x1c));
    logf("spawntrace10: SpawnBuilding node fired: '%s'", n);
    g_node(call);
}

// The building runtime's hooks (H1).
inline bool install(const di1::Config& c)
{
    g_state.cfg = c;
    bool ok = true;
    if (c.building10 || c.spawnTrace10)
        ok &= hook("building lot spawn (building10)", 0x00CEF120, "538BDC83EC0883E4F083C404558B6B04",
                   (void*)&hkLotSpawn, (void**)&g_lotSpawn);
    if (c.spawnTrace10)
        ok &= installFeature("spawntrace10 buildings", {
            {"lot state register (spawntrace10)", 0x00CEFE60, "538B5C2408555657", (void*)&hkLotState,
             (void**)&g_lotState},
            {"SpawnBuilding Link_In (spawntrace10)", 0x011854B0, "6AFF68????????64A100000000", (void*)&hkNode,
             (void**)&g_node},
        });
    return ok;
}

}   // namespace bldrt
