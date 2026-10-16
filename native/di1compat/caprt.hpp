// 1.0 expendables at run time. Included by di1compat.cpp after its helpers.
//
// shutdown10 (fix, cfg shutdown10, default on): a 1.0 expendable's actor keeps its model drawn while shut down, as in
// 1.0. Actor shutdown is the same code in both games -- DNA ActorShutdownEnabled / ActorShutdownDistance (3.0
// FUN_00755fc0, 1.0 FUN_007319c0: the distance at actor+0x38), EnableShutdown (FUN_0074e550 / FUN_00729980), the
// distance test against cameras and avatars (FUN_0074f650 / FUN_0072a6f0; gathered by FUN_018b5360), the restore test
// (FUN_00747b00 / FUN_0071faf0), the manager loops (FUN_00753720/7d0/860 / FUN_0072e8c0/970/ea00) and the
// SpawnExpendable node's enableShutdown/shutdownDistance (FUN_017a1090 / FUN_010dd640: -1 keeps the DNA value) -- up
// to the apply, FUN_007534c0 / FUN_0072e6a0: state flag 8, the entity suspended (FUN_006a3f10 / FUN_00688b30, same),
// then the render component's shutdown call. 1.0 calls 0x00D59680 there, which is `ret 4`: nothing. 3.0 calls
// FUN_01860b40(1), which sets bit 0x80 of its scene node's +0xad (node base +0x24d), and the node's draw query
// (FUN_018383e0, query 1) answers 0 while it is set: the model is hidden. So a 1.0 capsule (capsule.dnax: enabled, 40)
// or rescue debris (inc_rescuebus/-dumpster/-phonebooth.dnax: enabled, 50) farther than that from every camera and
// avatar froze in 1.0 but stayed visible (user: capsules seen 100 m away and in cutscenes), and vanishes in 3.0.
// Which actors: those spawned from an expendable definition of a package 3.0 does not ship -- the definitions popin10
// stamps (sys::applyPopin10; 1.0 entries never carry PopinDistance) -- marked by handle when they are created: at once
// (base spawn FUN_0160a3e0's out handle) or by the async completion 0x0160A270. Their shutdown apply skips only the
// render hide; flag 8, the suspend and the restore stay 3.0's.
//
// capsuletrace10 (probe, log only): where a 1.0 expendable's spawn ends.
// 1.0 places capsules with SpawnExpendable nodes (in_city.mer IN_City_platforming: Catalyst -> SpawnExpendable
// 'INC_CustCap_<x>' at a LocatorNode, pin 5 Transform -> LocatorList, the form 36 shipped 3.0 links also use) whose
// entries come from <playset>_Capsules.lua (Inc: 149 CUSTOMIZATION capsules, 18 GREENCAPSULE, 19 HELP, 10 DECORATION);
// the rescue missions' debris the same way (in_city_teachtrapped.mer: 6 SpawnExpendable INC_rescueBus / -Dumpster /
// -PhoneB at the mission's LOC_* locators, fired by the presentation list's Begin; ExpendableHandle -> the focus
// points; Collected -> the trapped civilian's SpawnGroup).
// 3.0's SpawnExpendable handler (FUN_017bf050, cdecl (call, impulse data); its locators are fetched inside, into a
// local list) queues one request per locator (FUN_0160ebd0, thiscall (request, 1), ret 8; request +0 name, +4 the
// spawn transform: [4..6] = position, which the request reads for the zone, FUN_01390e30(request + 0x14)). The
// definition's spawn is its vf[1]: base FUN_0160a3e0 (Help, GreenCapsule ...), CustomizationExpendableReward
// FUN_00cbc5c0 (3.0 only: the lock named by Data's first item, FUN_01610bf0 on DAT_0228ac9c; not found ->
// 'Spark_Capsule' (FUN_0160f370) instead of the capsule; found and already unlocked -> nothing; else the base spawn).
// Each is thiscall (out handle, transform, spawn params), ret 0xc. The base spawn creates the actor at once when
// params+0x1c is 0, else queues a pending record (a copy of the params: +0 the definition's index in the manager's
// table, *(DAT_0228aa40+0x1c)) and an AsyncActorCreator, and leaves out = 0; the completion 0x0160A270 (cdecl
// (handle, record)) kills the new actor when record+0x80 is set (FUN_0074da00), else registers it (FUN_01604070).
// Run 2 (2026-10-15 11:24, Metroville): 80 nodes, 64 base spawns, all async; the creator log's 150-line cap filled
// with other creators in 55 ms, so whether a capsule actor was made stayed open. Run 3 logs the completion instead.
#pragma once

namespace caprt {

typedef uint32_t(__fastcall* ReqFn)(void* self, void* edx, uint8_t* req, uint32_t a2);
typedef uint32_t*(__fastcall* SpawnFn)(uint8_t* def, void* edx, uint32_t* out, const float* t, uint8_t* params);
typedef uint32_t(__fastcall* ByNameFn)(void* self, void* edx, const char* name, const float* t, uint32_t a3, uint32_t a4);
typedef void(__cdecl* DoneFn)(uint32_t handle, uint8_t* record);
typedef void(__fastcall* ShutFn)(void* mgr, void* edx, uint8_t* actor);
typedef void(__fastcall* RenderShutFn)(void* comp, void* edx, uint8_t on);
typedef uint8_t*(__cdecl* ActorFn)(uint32_t handle);
typedef void*(__cdecl* CompFn)(uint32_t handle, uint32_t type);
static ReqFn g_req;
static SpawnFn g_cust, g_base;
static ByNameFn g_byName;
static DoneFn g_done;
static ShutFn g_shut;
static RenderShutFn g_renderShut;
static volatile LONG g_spawnLogged, g_doneLogged, g_keptLogged;
static __declspec(thread) int t_inCust, t_base, t_spark, t_keep;

// a definition from a 1.0 package: popin10's stamp
static bool def10(const uint8_t* def)
{
    return def && sys::readable(def + 0x9c, 4) && *(const float*)(def + 0x9c) == sys::kPopin10 * sys::kPopin10;
}
static void defText(const uint8_t* def, char* data, char* model)
{
    data[0] = model[0] = 0;
    if (!def || !sys::readable(def + 0xc, 8)) return;
    safeCopy(data, 96, *(const char**)(def + 0xc));
    safeCopy(model, 96, *(const char**)(def + 0x10));
}
static const char* mode(const uint8_t* params) { return !sys::readable(params + 0x1c, 1) ? "?" : params[0x1c] ? "async" : "sync"; }

// ---- shutdown10 ----
static std::mutex g_keepLock;
static std::unordered_set<uint32_t> g_keep;   // handles of actors made from 1.0 definitions
static bool kept(uint32_t h)
{
    std::lock_guard<std::mutex> l(g_keepLock);
    return g_keep.count(h) != 0;
}
static void keep(uint32_t h, const uint8_t* def)
{
    if (!h) return;
    ActorFn actorOf = (ActorFn)rt(0x0074B200);
    {
        std::lock_guard<std::mutex> l(g_keepLock);
        // ponytail: dead handles pruned only past 8192 entries (handles carry a generation, so a stale one never
        // names a new actor); prune on actor death if a playset ever spawns that many 1.0 expendables
        if (g_keep.size() > 8192)
            for (auto it = g_keep.begin(); it != g_keep.end();) it = actorOf(*it) ? std::next(it) : g_keep.erase(it);
        if (!g_keep.insert(h).second) return;
    }
    // already shut down before it was marked (actor+0x10c bit 8): undo the hide 3.0 gave it, as its restore does
    uint8_t* a = actorOf(h);
    if (a && (a[0x10c] & 8)) {
        void* comp = ((CompFn)rt(0x0069ECD0))(h, *(uint16_t*)rt(0x0228E56C));
        if (comp && g_renderShut) g_renderShut(comp, nullptr, 0);   // null while the shutdown10 feature is off
    }
    if (g_cfg.capsuleTrace10 && InterlockedIncrement(&g_keptLogged) <= 300) {
        char d[96], mo[96];
        defText(def, d, mo);
        logf("shutdown10: actor %08X '%s' (%s) drawn while shut down%s", h, d, mo, a && (a[0x10c] & 8) ? " (was shut down: shown)" : "");
    }
}
static void __fastcall hkShut(void* mgr, void*, uint8_t* actor)
{
    t_keep = actor && kept(*(uint32_t*)(actor + 4));
    g_shut(mgr, nullptr, actor);
    t_keep = 0;
}
static void __fastcall hkRenderShut(void* comp, void*, uint8_t on)
{
    if (on && t_keep) return;   // 1.0: the render component's shutdown call is `ret 4`
    g_renderShut(comp, nullptr, on);
}
static void __cdecl hkDone(uint32_t handle, uint8_t* record)
{
    uint8_t* mgr = *(uint8_t**)rt(0x0228AA40);
    uint8_t* def = nullptr;
    bool killed = record && record[0x80];
    if (mgr && record && sys::readable(mgr + 0x1c, 4)) {
        uint8_t** table = *(uint8_t***)(mgr + 0x1c);
        uint32_t idx = *(uint32_t*)record;
        if (table && sys::readable(table + idx, 4)) def = table[idx];
    }
    bool is10 = def10(def);
    g_done(handle, record);   // frees the record
    if (is10 && g_cfg.capsuleTrace10 && InterlockedIncrement(&g_doneLogged) <= 300) {
        char d[96], mo[96];
        defText(def, d, mo);
        logf("capsuletrace10: async spawn of '%s' (%s) done: handle %08X -> actor %p%s", d, mo, handle,
             ((ActorFn)rt(0x0074B200))(handle), killed ? ", killed (pending record cancelled)" : "");
    }
    if (is10 && !killed && g_cfg.shutdown10) keep(handle, def);
}

// ---- capsuletrace10 ----
static bool firstFew(const char* name, int n)   // each name's first n
{
    static std::mutex m;
    static std::unordered_map<std::string, int> seen;
    std::lock_guard<std::mutex> l(m);
    return seen.size() < 2000 && ++seen[name] <= n;
}
// the node's Link_In (FUN_017bf050): call[1] = node, node+0x88 = Expendable_Name
typedef void(__cdecl* NodeFn)(void** call, uint8_t* data);
static NodeFn g_node;
static void __cdecl hkNode(void** call, uint8_t* data)
{
    char n[96] = "";
    uint8_t* node = sys::readable(call, 8) ? (uint8_t*)call[1] : nullptr;
    if (node && sys::readable(node + 0x88, 4)) safeCopy(n, sizeof(n), *(const char**)(node + 0x88));
    if (firstFew(std::string("node ").append(n).c_str(), 2)) logf("capsuletrace10: SpawnExpendable '%s' fired", n);
    g_node(call, data);
}
static uint32_t __fastcall hkReq(void* self, void*, uint8_t* req, uint32_t a2)
{
    char n[96] = "";
    if (sys::readable(req, 0x40)) safeCopy(n, sizeof(n), *(const char**)req);
    if (firstFew(std::string("req ").append(n).c_str(), 8)) {
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
    if (InterlockedIncrement(&g_spawnLogged) <= 300)
        logf("capsuletrace10: Customization spawn '%s' -> %s, handle %08X", d,
             t_spark ? "lock not found: Spark_Capsule instead" : t_base ? "lock locked: base spawn" : "lock already unlocked: nothing",
             out ? *out : 0);
    return r;
}
static uint32_t* __fastcall hkBase(uint8_t* def, void*, uint32_t* out, const float* t, uint8_t* params)
{
    if (t_inCust) t_base = 1;
    uint32_t* r = g_base(def, nullptr, out, t, params);
    if (!def10(def)) return r;
    uint32_t h = out ? *out : 0;   // 0: queued (async), the completion marks it
    if (g_cfg.capsuleTrace10 && InterlockedIncrement(&g_spawnLogged) <= 300) {
        char d[96], mo[96];
        defText(def, d, mo);
        logf("capsuletrace10: base spawn '%s' model '%s' at (%.1f, %.1f, %.1f), %s -> handle %08X", d, mo, t ? t[4] : 0.f,
             t ? t[5] : 0.f, t ? t[6] : 0.f, mode(params), h);
    }
    if (h && g_cfg.shutdown10) keep(h, def);
    return r;
}
static uint32_t __fastcall hkByName(void* self, void*, const char* name, const float* t, uint32_t a3, uint32_t a4)
{
    if (t_inCust) t_spark = 1;
    return g_byName(self, nullptr, name, t, a3, a4);
}

static void install()
{
    if (g_cfg.shutdown10 && !g_cfg.popin10) logf("shutdown10: off, it finds 1.0 definitions by popin10's stamp");
    bool keep10 = g_cfg.shutdown10 && g_cfg.popin10;
    const core::HookSpec spawn = {"expendable base spawn (shutdown10)", 0x0160A3E0, "558BEC83E4F06AFF68????????64A1",
                                  (void*)&hkBase, (void**)&g_base};
    const core::HookSpec done = {"expendable async spawn done (shutdown10)", 0x0160A270, "8B0D????????568B74240C",
                                 (void*)&hkDone, (void**)&g_done};
    bool spawnsHooked = keep10 && installFeature("shutdown10", {
        {"actor shutdown apply (shutdown10)", 0x007534C0, "568B7424088B4604578BF9", (void*)&hkShut, (void**)&g_shut},
        {"render component shutdown (shutdown10)", 0x01860B40, "83EC08E8????????85C0745C", (void*)&hkRenderShut,
         (void**)&g_renderShut},
        spawn,
        done,
    });
    if (!g_cfg.capsuleTrace10) return;
    if (!spawnsHooked) installFeature("capsuletrace10 spawns", {spawn, done});
    installFeature("capsuletrace10", {
        {"SpawnExpendable Link_In (capsuletrace10)", 0x017BF050, "6AFF68????????64A100000000", (void*)&hkNode,
         (void**)&g_node},
        {"expendable spawn request (capsuletrace10)", 0x0160EBD0, "6AFF68????????64A100000000", (void*)&hkReq,
         (void**)&g_req},
        {"Customization reward spawn (capsuletrace10)", 0x00CBC5C0, "6AFF68????????64A100000000", (void*)&hkCust,
         (void**)&g_cust},
        {"expendable spawn by name (capsuletrace10)", 0x0160F370, "6AFF68????????64A100000000", (void*)&hkByName,
         (void**)&g_byName},
    });
}

}   // namespace caprt
