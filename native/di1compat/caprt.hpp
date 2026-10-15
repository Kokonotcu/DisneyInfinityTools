// capsuletrace10 (probe, log only): where a 1.0 capsule's spawn ends. Included by di1compat.cpp after its helpers.
//
// 1.0 places capsules with SpawnExpendable nodes (in_city.mer IN_City_platforming: Catalyst -> SpawnExpendable
// 'INC_CustCap_<x>' at a LocatorNode, pin 5 Transform -> LocatorList, the form 36 shipped 3.0 links also use) whose
// entries come from <playset>_Capsules.lua (Inc: 149 CUSTOMIZATION capsules, 18 GREENCAPSULE, 19 HELP, 10 DECORATION).
// 3.0's SpawnExpendable handler (FUN_017bf050, cdecl (call, impulse data); its locators are fetched inside, into a
// local list) queues one request per locator (FUN_0160ebd0, thiscall (request, 1), ret 8; request +0 name, +4 the
// spawn transform: [4..6] = position, which the request reads for the zone, FUN_01390e30(request + 0x14)). The
// definition's spawn is its vf[1]: base FUN_0160a3e0 (Help, GreenCapsule ...), CustomizationExpendableReward
// FUN_00cbc5c0 (3.0 only: the lock named by Data's first item, FUN_01610bf0 on DAT_0228ac9c; not found ->
// 'Spark_Capsule' (FUN_0160f370) instead of the capsule; found and already unlocked -> nothing; else the base spawn).
// Each is thiscall (out handle, transform, spawn params), ret 0xc. The base spawn creates the actor at once when
// params+0x1c is 0, else queues an AsyncActorCreator and leaves out = 0; its Execute (FUN_0075a290) creates it from
// +0x20 (the transform), flags +0x70 (0x40: created deactivated).
// Run 1 (2026-10-15 11:05, Metroville): all 80 nodes fired, every request and lock check (locked -> base spawn) went
// through, the base spawn returned handle 0, and the capsule models opened 236 ms later (objects\uni_capsuletoyred,
// uni_capsuletoybox; uni_capsulehelp at 11:05:51). Its "0 locator(s) (no list)" read the impulse data, not the
// locators, and its positions read transform[12..14] (1, 1, 1), not the position. Run 2 logs the real position,
// sync or async, and each creator executed within 5 s of a capsule spawn.
#pragma once

namespace caprt {

typedef uint32_t(__fastcall* ReqFn)(void* self, void* edx, uint8_t* req, uint32_t a2);
typedef uint32_t*(__fastcall* SpawnFn)(uint8_t* def, void* edx, uint32_t* out, const float* t, uint8_t* params);
typedef uint32_t(__fastcall* ByNameFn)(void* self, void* edx, const char* name, const float* t, uint32_t a3, uint32_t a4);
typedef void(__fastcall* ExecFn)(uint8_t* self, void* edx);
static ReqFn g_req;
static SpawnFn g_cust, g_base;
static ByNameFn g_byName;
static ExecFn g_exec;
static volatile LONG g_reqLogged, g_spawnLogged, g_execLogged;
static volatile ULONGLONG g_lastCapsule;
static __declspec(thread) int t_inCust, t_base, t_spark;

static bool capsuleLike(const char* s)
{
    char low[160];
    size_t i = 0;
    for (; s[i] && i + 1 < sizeof(low); ++i) low[i] = (char)tolower((unsigned char)s[i]);
    low[i] = 0;
    return strstr(low, "cap") != nullptr;
}
static void defText(uint8_t* def, char* data, char* model)
{
    data[0] = model[0] = 0;
    if (!sys::readable(def + 0xc, 8)) return;
    safeCopy(data, 96, *(const char**)(def + 0xc));
    safeCopy(model, 96, *(const char**)(def + 0x10));
}
static const char* mode(const uint8_t* params) { return !sys::readable(params + 0x1c, 1) ? "?" : params[0x1c] ? "async" : "sync"; }

// the node's Link_In (FUN_017bf050): call[1] = node, node+0x88 = Expendable_Name
typedef void(__cdecl* NodeFn)(void** call, uint8_t* data);
static NodeFn g_node;
static volatile LONG g_nodeLogged;
static void __cdecl hkNode(void** call, uint8_t* data)
{
    char n[96] = "";
    uint8_t* node = sys::readable(call, 8) ? (uint8_t*)call[1] : nullptr;
    if (node && sys::readable(node + 0x88, 4)) safeCopy(n, sizeof(n), *(const char**)(node + 0x88));
    if (capsuleLike(n) && InterlockedIncrement(&g_nodeLogged) <= 80) logf("capsuletrace10: SpawnExpendable '%s' fired", n);
    g_node(call, data);
}

static uint32_t __fastcall hkReq(void* self, void*, uint8_t* req, uint32_t a2)
{
    char n[96] = "";
    if (sys::readable(req, 0x40)) safeCopy(n, sizeof(n), *(const char**)req);
    if (capsuleLike(n) && InterlockedIncrement(&g_reqLogged) <= 80) {
        const float* t = (const float*)(req + 4);
        logf("capsuletrace10: spawn request '%s' at (%.1f, %.1f, %.1f)", n, t[4], t[5], t[6]);
    }
    return g_req(self, nullptr, req, a2);
}
static uint32_t* __fastcall hkCust(uint8_t* def, void*, uint32_t* out, const float* t, uint8_t* params)
{
    ++t_inCust;
    t_base = t_spark = 0;
    uint32_t* r = g_cust(def, nullptr, out, t, params);
    --t_inCust;
    char d[96], mo[96];
    defText(def, d, mo);
    g_lastCapsule = GetTickCount64();
    if (InterlockedIncrement(&g_spawnLogged) <= 120)
        logf("capsuletrace10: Customization spawn '%s' -> %s, handle %08X", d,
             t_spark ? "lock not found: Spark_Capsule instead" : t_base ? "lock locked: base spawn" : "lock already unlocked: nothing",
             out ? *out : 0);
    return r;
}
static uint32_t* __fastcall hkBase(uint8_t* def, void*, uint32_t* out, const float* t, uint8_t* params)
{
    if (t_inCust) t_base = 1;
    uint32_t* r = g_base(def, nullptr, out, t, params);
    char d[96], mo[96];
    defText(def, d, mo);
    bool cap = t_inCust || capsuleLike(mo);
    if (cap) g_lastCapsule = GetTickCount64();
    if (cap && InterlockedIncrement(&g_spawnLogged) <= 120)
        logf("capsuletrace10: base spawn '%s' model '%s' at (%.1f, %.1f, %.1f), %s -> handle %08X", d, mo, t ? t[4] : 0.f,
             t ? t[5] : 0.f, t ? t[6] : 0.f, mode(params), out ? *out : 0);
    return r;
}
static uint32_t __fastcall hkByName(void* self, void*, const char* name, const float* t, uint32_t a3, uint32_t a4)
{
    if (t_inCust) t_spark = 1;
    return g_byName(self, nullptr, name, t, a3, a4);
}
// AsyncActorCreator::Execute (FUN_0075a290, thiscall, no args): mode +0x18, the spawn transform at +0x20 (FUN_00749740
// copies it, [4..6] at +0x30), flags +0x70 (ctor FUN_00746e60). The base spawn's flags are 0x22, or 0x23 after its
// zone check, | 0x10 (params+0x81) | FUN_00742620(def+0x20) (0x2000 / 0x4000): never 0x40, the bit on which Execute
// creates the actor deactivated (vf+0x1c(0)). Its completion callback 0x0160A270 kills the new actor when the pending
// record's +0x80 is set (FUN_0074da00).
static void __fastcall hkExec(uint8_t* self, void*)
{
    ULONGLONG last = g_lastCapsule;
    if (last && GetTickCount64() - last < 5000 && sys::readable(self, 0x80) && InterlockedIncrement(&g_execLogged) <= 150) {
        char n[96] = "";   // +0x64 is a 4x4 matrix copy (or 0), not text; +0x6c a char* name (or 0)
        if (*(const char**)(self + 0x6c)) safeCopy(n, sizeof(n), *(const char**)(self + 0x6c));
        const float* t = (const float*)(self + 0x20);
        logf("capsuletrace10: creator %p executes: mode %u flags %X at (%.1f, %.1f, %.1f) name '%s'", self,
             *(uint32_t*)(self + 0x18), *(uint32_t*)(self + 0x70), t[4], t[5], t[6], n);
    }
    g_exec(self, nullptr);
}

static void install()
{
    if (!g_cfg.capsuleTrace10) return;
    hook("SpawnExpendable Link_In (capsuletrace10)", 0x017BF050, "6AFF68????????64A100000000", (void*)&hkNode, (void**)&g_node);
    hook("expendable spawn request (capsuletrace10)", 0x0160EBD0, "6AFF68????????64A100000000", (void*)&hkReq, (void**)&g_req);
    hook("Customization reward spawn (capsuletrace10)", 0x00CBC5C0, "6AFF68????????64A100000000", (void*)&hkCust, (void**)&g_cust);
    hook("expendable base spawn (capsuletrace10)", 0x0160A3E0, "558BEC83E4F06AFF68????????64A1", (void*)&hkBase, (void**)&g_base);
    hook("expendable spawn by name (capsuletrace10)", 0x0160F370, "6AFF68????????64A100000000", (void*)&hkByName, (void**)&g_byName);
    if (!g_cfg.trace)   // trace hooks Execute itself
        hook("AsyncActorCreator execute (capsuletrace10)", 0x0075A290, "6AFF68????????64A100000000", (void*)&hkExec, (void**)&g_exec);
}

}   // namespace caprt
