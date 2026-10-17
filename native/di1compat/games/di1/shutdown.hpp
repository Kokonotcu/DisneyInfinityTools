// 1.0 expendables drawn while shut down, as in 1.0 (shutdown10); capsuletrace10's spawn probes. core, popin.
// Spawn threads.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_set>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "config.hpp"
#include "popin.hpp"

namespace di1::shutdown {

// Installs shutdown10's four hooks and capsuletrace10's probes, as c switches them on (H1, H2).
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kActorOf = 0x0074B200;       // FUN_0074b200: cdecl (handle) -> actor, null when dead
constexpr uint32_t kComponentOf = 0x0069ECD0;   // FUN_0069ecd0: cdecl (handle, type) -> component
constexpr uint32_t kRenderType = 0x0228E56C;    // u16: the render component's type id
constexpr uint32_t kExpManager = 0x0228AA40;    // *: the expendables manager
constexpr size_t kManagerDefs = 0x1C;           // its definition table; a pending record's +0 indexes it
constexpr size_t kRecordKilled = 0x80;          // a pending record's cancel flag: the completion kills the actor
constexpr size_t kActorHandle = 4;
constexpr size_t kActorFlags = 0x10C;           // bit 8: shut down
constexpr uint8_t kShutDown = 8;
constexpr size_t kDefPopin = 0x9C;              // a definition's pop-in range squared: popin10's stamp
constexpr size_t kDefData = 0xC, kDefModel = 0x10;   // its data and model names
constexpr size_t kParamsAsync = 0x1C;           // spawn params: non-zero queues an async creation
constexpr size_t kCallNode = 1;                 // a Link_In call's node (call[1])
constexpr size_t kNodeExpendable = 0x88;        // the SpawnExpendable node's Expendable_Name
constexpr size_t kRequestBytes = 0x40;          // a spawn request: +0 the name, +4 the transform
constexpr size_t kRequestPlacement = 4;         // [4..6]: the position
}  // namespace eng

constexpr size_t kKeepPrune = 8192;   // kept handles before dead ones are pruned
constexpr long kProbeLines = 300;     // per probe budget, never reset
constexpr size_t kNames = 96;         // definition name buffers

typedef uint32_t(__fastcall* ReqFn)(void* self, void* edx, uint8_t* req, uint32_t a2);
typedef uint32_t*(__fastcall* SpawnFn)(uint8_t* def, void* edx, uint32_t* out, const float* t, uint8_t* params);
typedef uint32_t(__fastcall* ByNameFn)(void* self, void* edx, const char* name, const float* t, uint32_t a3,
                                       uint32_t a4);
typedef void(__cdecl* DoneFn)(uint32_t handle, uint8_t* record);
typedef void(__fastcall* ShutFn)(void* mgr, void* edx, uint8_t* actor);
typedef void(__fastcall* RenderShutFn)(void* comp, void* edx, uint8_t on);
typedef void(__cdecl* NodeFn)(void** call, uint8_t* data);
typedef uint8_t*(__cdecl* ActorFn)(uint32_t handle);
typedef void*(__cdecl* CompFn)(uint32_t handle, uint32_t type);

struct State {
    bool shutdown10 = false, capsuleTrace10 = false;   // set once in install()
    // originals: set once by installFeature in install(); g_base is the base spawn's, not core::g_base (B-19)
    ReqFn g_req = nullptr;
    SpawnFn g_cust = nullptr, g_base = nullptr;
    ByNameFn g_byName = nullptr;
    DoneFn g_done = nullptr;
    ShutFn g_shut = nullptr;
    RenderShutFn g_renderShut = nullptr;   // null while shutdown10 is off: keep() checks it
    NodeFn g_node = nullptr;
    std::atomic<long> spawnLogged{0}, doneLogged{0}, keptLogged{0};   // probe budgets; any thread
    std::mutex keepMx;                     // guards keep; held across actorOf while pruning
    std::unordered_set<uint32_t> keep;     // handles of actors made from 1.0 definitions
    core::LogCount<std::string> fewLines{2000};   // capsuletrace10 lines per name, for 2000 names
};
inline State g_state;

// Per spawn call on its thread: set around one engine call, cleared after it.
inline thread_local int t_inCust, t_base, t_spark, t_keep;

// A definition from a 1.0 package: popin10 stamped it (1.0 entries never carry PopinDistance).
inline bool def10(const uint8_t* def)
{
    return def && core::readable(def + eng::kDefPopin, 4) && popin::isStamped(def);
}

inline void defText(const uint8_t* def, char (&data)[kNames], char (&model)[kNames])
{
    data[0] = model[0] = 0;
    if (!def || !core::readable(def + eng::kDefData, 8)) return;
    core::safeCopy(data, kNames, *(const char**)(def + eng::kDefData));
    core::safeCopy(model, kNames, *(const char**)(def + eng::kDefModel));
}

inline const char* mode(const uint8_t* params)
{
    if (!core::readable(params + eng::kParamsAsync, 1)) return "?";
    return params[eng::kParamsAsync] ? "async" : "sync";
}

// ---- shutdown10 ----

inline bool kept(uint32_t h)
{
    State& s = g_state;
    std::lock_guard<std::mutex> l(s.keepMx);
    return s.keep.count(h) != 0;
}

// Handle h's actor stays drawn while shut down; one already shut down is shown again, as its restore does.
inline void keep(uint32_t h, const uint8_t* def)
{
    State& s = g_state;
    if (!h) return;
    ActorFn actorOf = (ActorFn)core::rt(eng::kActorOf);
    {
        std::lock_guard<std::mutex> l(s.keepMx);
        // ponytail: dead handles pruned past 8192 only (generations: a stale one names no new actor);
        // upgrade: prune on actor death
        if (s.keep.size() > kKeepPrune)
            for (auto it = s.keep.begin(); it != s.keep.end();) it = actorOf(*it) ? std::next(it) : s.keep.erase(it);
        if (!s.keep.insert(h).second) return;
    }
    uint8_t* a = actorOf(h);
    const bool shutDown = a && (a[eng::kActorFlags] & eng::kShutDown);
    if (shutDown) {
        void* comp = ((CompFn)core::rt(eng::kComponentOf))(h, *(uint16_t*)core::rt(eng::kRenderType));
        if (comp && s.g_renderShut) s.g_renderShut(comp, nullptr, 0);
    }
    if (!s.capsuleTrace10 || ++s.keptLogged > kProbeLines) return;
    char d[kNames], mo[kNames];
    defText(def, d, mo);
    core::logf("shutdown10: actor %08X '%s' (%s) drawn while shut down%s", h, d, mo,
               a && (a[eng::kActorFlags] & eng::kShutDown) ? " (was shut down: shown)" : "");
}

// FUN_007534c0, thiscall on the manager (actor; ret 4), per shutdown apply: shutdown is the same code in both games.
inline void __fastcall hkShut(void* mgr, void*, uint8_t* actor)
{
    t_keep = actor && kept(*(uint32_t*)(actor + eng::kActorHandle));
    g_state.g_shut(mgr, nullptr, actor);
    t_keep = 0;
}

// FUN_01860b40, thiscall (on; ret 4), per render shutdown: 3.0 hides the model, 1.0's call (0x00D59680) was `ret 4`.
inline void __fastcall hkRenderShut(void* comp, void*, uint8_t on)
{
    if (on && t_keep) return;
    g_state.g_renderShut(comp, nullptr, on);
}

// The definition of a pending record, read through the manager's table; null when unreadable.
inline uint8_t* pendingDef(const uint8_t* record)
{
    uint8_t* mgr = *(uint8_t**)core::rt(eng::kExpManager);
    if (!mgr || !record || !core::readable(mgr + eng::kManagerDefs, 4)) return nullptr;
    uint8_t** table = *(uint8_t***)(mgr + eng::kManagerDefs);
    uint32_t idx = *(const uint32_t*)record;
    return table && core::readable(table + idx, 4) ? table[idx] : nullptr;
}

// 0x0160A270, cdecl (handle, record), per async spawn completion of any expendable; it frees the record.
inline void __cdecl hkDone(uint32_t handle, uint8_t* record)
{
    State& s = g_state;
    bool killed = record && record[eng::kRecordKilled];
    uint8_t* def = pendingDef(record);
    bool is10 = def10(def);
    s.g_done(handle, record);
    const bool logDone = is10 && s.capsuleTrace10;
    if (logDone && ++s.doneLogged <= kProbeLines) {
        char d[kNames], mo[kNames];
        defText(def, d, mo);
        core::logf("capsuletrace10: async spawn of '%s' (%s) done: handle %08X -> actor %p%s", d, mo, handle,
                   ((ActorFn)core::rt(eng::kActorOf))(handle), killed ? ", killed (pending record cancelled)" : "");
    }
    if (is10 && !killed && s.shutdown10) keep(handle, def);
}

// ---- capsuletrace10 ----

// FUN_017bf050, cdecl (call, impulse data), per SpawnExpendable node firing.
inline void __cdecl hkNode(void** call, uint8_t* data)
{
    char n[kNames] = "";
    uint8_t* node = core::readable(call, 8) ? (uint8_t*)call[eng::kCallNode] : nullptr;
    if (node && core::readable(node + eng::kNodeExpendable, 4))
        core::safeCopy(n, sizeof(n), *(const char**)(node + eng::kNodeExpendable));
    if (g_state.fewLines.first(std::string("node ").append(n), 2))
        core::logf("capsuletrace10: SpawnExpendable '%s' fired", n);
    g_state.g_node(call, data);
}

// FUN_0160ebd0, thiscall (request, 1; ret 8), per spawn request: one per locator of a node.
inline uint32_t __fastcall hkReq(void* self, void*, uint8_t* req, uint32_t a2)
{
    char n[kNames] = "";
    if (core::readable(req, eng::kRequestBytes)) core::safeCopy(n, sizeof(n), *(const char**)req);
    if (g_state.fewLines.first(std::string("req ").append(n), 8)) {
        const float* t = (const float*)(req + eng::kRequestPlacement);
        core::logf("capsuletrace10: spawn request '%s' at (%.1f, %.1f, %.1f)", n, t[4], t[5], t[6]);
    }
    return g_state.g_req(self, nullptr, req, a2);
}

// FUN_00cbc5c0, thiscall (out, transform, params; ret 0xc), per Customization reward spawn (3.0 only).
inline uint32_t* __fastcall hkCust(uint8_t* def, void*, uint32_t* out, const float* t, uint8_t* params)
{
    State& s = g_state;
    ++t_inCust;
    t_base = t_spark = 0;
    uint32_t* r = s.g_cust(def, nullptr, out, t, params);
    --t_inCust;
    if (++s.spawnLogged <= kProbeLines) {
        char d[kNames], mo[kNames];
        defText(def, d, mo);
        const char* how = t_spark  ? "lock not found: Spark_Capsule instead"
                          : t_base ? "lock locked: base spawn"
                                   : "lock already unlocked: nothing";
        core::logf("capsuletrace10: Customization spawn '%s' -> %s, handle %08X", d, how, out ? *out : 0);
    }
    return r;
}

// FUN_0160a3e0, thiscall (out, transform, params; ret 0xc), per base spawn of any package; 0 out: queued (async).
inline uint32_t* __fastcall hkBase(uint8_t* def, void*, uint32_t* out, const float* t, uint8_t* params)
{
    State& s = g_state;
    if (t_inCust) t_base = 1;
    uint32_t* r = s.g_base(def, nullptr, out, t, params);
    if (!def10(def)) return r;
    uint32_t h = out ? *out : 0;   // 0: queued (async), the completion marks it
    if (s.capsuleTrace10 && ++s.spawnLogged <= kProbeLines) {
        char d[kNames], mo[kNames];
        defText(def, d, mo);
        core::logf("capsuletrace10: base spawn '%s' model '%s' at (%.1f, %.1f, %.1f), %s -> handle %08X", d, mo,
                   t ? t[4] : 0.f, t ? t[5] : 0.f, t ? t[6] : 0.f, mode(params), h);
    }
    if (h && s.shutdown10) keep(h, def);
    return r;
}

// FUN_0160f370, thiscall (name, transform, a3, a4), per spawn by name: Spark_Capsule when a lock is not found.
inline uint32_t __fastcall hkByName(void* self, void*, const char* name, const float* t, uint32_t a3, uint32_t a4)
{
    if (t_inCust) t_spark = 1;
    return g_state.g_byName(self, nullptr, name, t, a3, a4);
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.shutdown10 = c.shutdown10;
    s.capsuleTrace10 = c.capsuleTrace10;
    if (c.shutdown10 && !c.popin10) core::logf("shutdown10: off, it finds 1.0 definitions by popin10's stamp");
    const bool keep10 = c.shutdown10 && c.popin10;
    const core::HookSpec spawn = {"expendable base spawn (shutdown10)", 0x0160A3E0, "558BEC83E4F06AFF68????????64A1",
                                  (void*)&detail::hkBase, (void**)&s.g_base};
    const core::HookSpec done = {"expendable async spawn done (shutdown10)", 0x0160A270, "8B0D????????568B74240C",
                                 (void*)&detail::hkDone, (void**)&s.g_done};
    const bool spawnsHooked = keep10 && core::installFeature("shutdown10", {
        {"actor shutdown apply (shutdown10)", 0x007534C0, "568B7424088B4604578BF9", (void*)&detail::hkShut,
         (void**)&s.g_shut},
        {"render component shutdown (shutdown10)", 0x01860B40, "83EC08E8????????85C0745C",
         (void*)&detail::hkRenderShut, (void**)&s.g_renderShut},
        spawn,
        done,
    });
    bool ok = spawnsHooked || !keep10;
    if (!c.capsuleTrace10) return ok;
    // capsuletrace10 hooks the two spawns itself when shutdown10 did not
    if (!spawnsHooked) ok = core::installFeature("capsuletrace10 spawns", {spawn, done}) && ok;
    ok &= core::installFeature("capsuletrace10", {
        {"SpawnExpendable Link_In (capsuletrace10)", 0x017BF050, "6AFF68????????64A100000000", (void*)&detail::hkNode,
         (void**)&s.g_node},
        {"expendable spawn request (capsuletrace10)", 0x0160EBD0, "6AFF68????????64A100000000", (void*)&detail::hkReq,
         (void**)&s.g_req},
        {"Customization reward spawn (capsuletrace10)", 0x00CBC5C0, "6AFF68????????64A100000000",
         (void*)&detail::hkCust, (void**)&s.g_cust},
        {"expendable spawn by name (capsuletrace10)", 0x0160F370, "6AFF68????????64A100000000",
         (void*)&detail::hkByName, (void**)&s.g_byName},
    });
    return ok;
}

}  // namespace di1::shutdown
