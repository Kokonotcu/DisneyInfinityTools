// capsuletrace10 (probe, log only): where a 1.0 capsule's spawn ends. Included by di1compat.cpp after its helpers.
//
// 1.0 places capsules with SpawnExpendable nodes (in_city.mer IN_City_platforming: Catalyst -> SpawnExpendable
// 'INC_CustCap_<x>' at a LocatorNode) whose entries come from <playset>_Capsules.lua (Inc: 149 CUSTOMIZATION
// capsules, 18 GREENCAPSULE, 19 HELP, 10 DECORATION). 3.0's SpawnExpendable handler (FUN_017bf050) queues one request
// per locator (FUN_0160ebd0, thiscall (request, 1), ret 8; request +0 name, +4 matrix). The definition's spawn is its
// vf[1]: base FUN_0160a3e0 (Help, GreenCapsule ...), CustomizationExpendableReward FUN_00cbc5c0 (3.0 only: the lock
// named by Data's first item, FUN_01610bf0 on DAT_0228ac9c; not found -> 'Spark_Capsule' (FUN_0160f370) instead of the
// capsule; found and already unlocked -> nothing; else the base spawn). Each is thiscall (out handle, matrix, a3),
// ret 0xc. This logs the capsule nodes firing, their requests and, for capsule models, which branch each spawn took.
// Known offline: the CUST_ locks the capsules name are 1.0 core lock records (startup2 gamedb/core/locks__lua.lua, 1019
// CUST_; 3.0's core table has 1) that locks10 adds (no UID collision for the INC ones), and their stateData decrypted
// with the game's own routine (FUN_012a4810, key DAT_021150a0) holds UID, byte 5 = byte 10 = 1: locked, so the lock
// check should take the base spawn. Unknown: whether the nodes fire, the requests resolve, and what the base spawn returns.
#pragma once

namespace caprt {

typedef uint32_t(__fastcall* ReqFn)(void* self, void* edx, uint8_t* req, uint32_t a2);
typedef uint32_t*(__fastcall* SpawnFn)(uint8_t* def, void* edx, uint32_t* out, const float* m, uint32_t a3);
typedef uint32_t(__fastcall* ByNameFn)(void* self, void* edx, const char* name, const float* m, uint32_t a3, uint32_t a4);
static ReqFn g_req;
static SpawnFn g_cust, g_base;
static ByNameFn g_byName;
static volatile LONG g_reqLogged, g_spawnLogged;
static __declspec(thread) int t_inCust, t_base, t_spark;

static bool capsuleLike(const char* s)
{
    char low[160];
    size_t i = 0;
    for (; s[i] && i + 1 < sizeof(low); ++i) low[i] = (char)tolower((unsigned char)s[i]);
    low[i] = 0;
    return strstr(low, "cap") != nullptr;
}
static void defText(uint8_t* def, char* name, char* data, char* model)
{
    name[0] = data[0] = model[0] = 0;
    if (!sys::readable(def + 8, 12)) return;
    safeCopy(name, 96, *(const char**)(def + 8));
    safeCopy(data, 96, *(const char**)(def + 0xc));
    safeCopy(model, 96, *(const char**)(def + 0x10));
}

// the node's Link_In (FUN_017bf050, cdecl (call, locator list)): call[1] = node, node+0x88 = Expendable_Name,
// list +4 elements / +8 count
typedef void(__cdecl* NodeFn)(void** call, uint8_t* list);
static NodeFn g_node;
static volatile LONG g_nodeLogged;
static void __cdecl hkNode(void** call, uint8_t* list)
{
    char n[96] = "";
    uint8_t* node = sys::readable(call, 8) ? (uint8_t*)call[1] : nullptr;
    if (node && sys::readable(node + 0x88, 4)) safeCopy(n, sizeof(n), *(const char**)(node + 0x88));
    uint32_t count = list && sys::readable(list + 8, 4) ? *(uint32_t*)(list + 8) : 0;
    if (capsuleLike(n) && InterlockedIncrement(&g_nodeLogged) <= 80)
        logf("capsuletrace10: SpawnExpendable '%s' fired with %u locator(s)%s", n, count, list ? "" : " (no list)");
    g_node(call, list);
}

static uint32_t __fastcall hkReq(void* self, void*, uint8_t* req, uint32_t a2)
{
    char n[96] = "";
    if (sys::readable(req, 0x40)) safeCopy(n, sizeof(n), *(const char**)req);
    if (capsuleLike(n) && InterlockedIncrement(&g_reqLogged) <= 80) {
        const float* m = (const float*)(req + 4);
        logf("capsuletrace10: spawn request '%s' at (%.1f, %.1f, %.1f)", n, m[12], m[13], m[14]);
    }
    return g_req(self, nullptr, req, a2);
}
static uint32_t* __fastcall hkCust(uint8_t* def, void*, uint32_t* out, const float* m, uint32_t a3)
{
    ++t_inCust;
    t_base = t_spark = 0;
    uint32_t* r = g_cust(def, nullptr, out, m, a3);
    --t_inCust;
    char n[96], d[96], mo[96];
    defText(def, n, d, mo);
    if (InterlockedIncrement(&g_spawnLogged) <= 120)
        logf("capsuletrace10: Customization spawn '%s' data '%s' model '%s' -> %s, handle %08X", n, d, mo,
             t_spark ? "lock not found: Spark_Capsule instead" : t_base ? "lock locked: base spawn" : "lock already unlocked: nothing",
             out ? *out : 0);
    return r;
}
static uint32_t* __fastcall hkBase(uint8_t* def, void*, uint32_t* out, const float* m, uint32_t a3)
{
    if (t_inCust) t_base = 1;
    uint32_t* r = g_base(def, nullptr, out, m, a3);
    char n[96], d[96], mo[96];
    defText(def, n, d, mo);
    if (!t_inCust && capsuleLike(mo) && InterlockedIncrement(&g_spawnLogged) <= 120)
        logf("capsuletrace10: base spawn '%s' data '%s' model '%s' at (%.1f, %.1f, %.1f) -> handle %08X", n, d, mo,
             m ? m[12] : 0.f, m ? m[13] : 0.f, m ? m[14] : 0.f, out ? *out : 0);
    else if (t_inCust && InterlockedIncrement(&g_spawnLogged) <= 120)
        logf("capsuletrace10:   base spawn of it at (%.1f, %.1f, %.1f) -> handle %08X", m ? m[12] : 0.f, m ? m[13] : 0.f,
             m ? m[14] : 0.f, out ? *out : 0);
    return r;
}
static uint32_t __fastcall hkByName(void* self, void*, const char* name, const float* m, uint32_t a3, uint32_t a4)
{
    if (t_inCust) t_spark = 1;
    return g_byName(self, nullptr, name, m, a3, a4);
}

static void install()
{
    if (!g_cfg.capsuleTrace10) return;
    hook("SpawnExpendable Link_In (capsuletrace10)", 0x017BF050, "6AFF68????????64A100000000", (void*)&hkNode, (void**)&g_node);
    hook("expendable spawn request (capsuletrace10)", 0x0160EBD0, "6AFF68????????64A100000000", (void*)&hkReq, (void**)&g_req);
    hook("Customization reward spawn (capsuletrace10)", 0x00CBC5C0, "6AFF68????????64A100000000", (void*)&hkCust, (void**)&g_cust);
    hook("expendable base spawn (capsuletrace10)", 0x0160A3E0, "558BEC83E4F06AFF68????????64A1", (void*)&hkBase, (void**)&g_base);
    hook("expendable spawn by name (capsuletrace10)", 0x0160F370, "6AFF68????????64A100000000", (void*)&hkByName, (void**)&g_byName);
}

}   // namespace caprt
