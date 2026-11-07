// 1.0 expendables drawn while shut down (shutdown10), exploding into their DebrisModel (debris10); probes.
// core, popin. Spawn threads.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "../../base/text.hpp"
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "config.hpp"
#include "popin.hpp"

namespace di1::shutdown {

// Installs shutdown10's four hooks, debris10's and capsuletrace10's probes, as c switches them on (H1, H2).
inline bool install(const Config& c);
// debris10: entry `def` of a 1.0 package explodes into model with dna (groups::debrisEntries).
inline void addDebris(const std::string& def, const std::string& model, const std::string& dna);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kActorOf = 0x0074B200;       // FUN_0074b200: cdecl (handle) -> actor, null when dead
constexpr uint32_t kComponentOf = 0x0069ECD0;   // FUN_0069ecd0: cdecl (handle, type) -> component
constexpr uint32_t kRenderType = 0x0228E56C;    // u16: the render component's type id
constexpr uint32_t kExpType = 0x02289AF8;       // u16: the Expendables component's type id (FUN_01232de0)
constexpr size_t kCompDef = 0xC;                // that component's definition index (FUN_01604070)
constexpr size_t kPartActor = 4;                // FUN_0074da00's this (actor + 0x1c): +4 its actor
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
constexpr size_t kActorPos = 0x128;             // its position for the shutdown range test (FUN_00747b00)
constexpr size_t kActorMode = 0x10E;            // shutdown mode: 1 by range, 2 always (FUN_007537d0)
constexpr size_t kActorBytes = 0x140;           // the actor fields read here
constexpr size_t kRenderNode = 0x14;            // the render component's scene node + kNodePart (FUN_0106d9d0)
constexpr size_t kNodePart = 0x1A0;
constexpr size_t kNodeBytes = 0x2A0;            // the node fields read here
// FUN_018383e0's query 1 (drawn) refuses on +0x24 bit 0x40, +0x1d0 bit 1, +0x24d bit 0x80
constexpr size_t kNodeOffA = 0x24, kNodeOffB = 0x1D0, kNodeHidden = 0x24D;
constexpr size_t kNodeType = 6;                 // u16, low 10 bits: the node type (FUN_01604070)
constexpr uint32_t kGeomType = 0x0228E4AC;      // u16: the type whose matrix is at +0x258, others +0x1c
constexpr size_t kGeomMatrix = 0x258, kNodeMatrix = 0x1C, kMatrixPos = 0x30;
constexpr uint32_t kTypeMask = 0x3FF;
constexpr uint32_t kModelType = 0x0228E648;     // u16: the model component's type id (FUN_007470a0, FUN_007567c0)
constexpr size_t kModelFlags = 8, kModelNode = 0xC, kModelBytes = 0x10;
constexpr size_t kSceneFlags = 0x28, kSceneChild = 0x14, kSceneNext = 0x18, kSceneBytes = 0x2C;   // FUN_00469390
constexpr uint32_t kSceneHidden = 3;
constexpr int kChildCap = 64;
// checked in install() (H4)
constexpr uint32_t kDrawQuery = 0x018383E0;     // FUN_018383e0: thiscall (query; ret 4) on a scene node
constexpr const char* kDrawQueryPrologue = "8B442404568BF183F803";
// debris10
constexpr size_t kDefName = 4;                  // a definition's Name (FUN_0160a3e0's "name" param)
constexpr uint32_t kXformOf = 0x006867E0;       // FUN_006867e0: thiscall on &node -> quaternion, position
constexpr size_t kXformFloats = 8;
constexpr uint32_t kFastDebris = 0x01645280;    // FUN_01645280: cdecl (params, transform, parent, model)
constexpr uint32_t kParamSet = 0x00703850;      // FUN_00703850: thiscall on params (key, value, 0; ret 0xc)
constexpr uint32_t kParamsFree = 0x00703840;    // FUN_00703840: thiscall on params
constexpr size_t kParamsBytes = 0x10;           // FUN_019756f0 zeroes them
// what FUN_0151dba0 does with the FastDebris next: thiscall (1 arg; ret 4) each
constexpr uint32_t kDebrisSettings = 0x01645490;   // on it (ExploderData +0x9c): the exploder's FastDebris settings
constexpr uint32_t kDebrisLaunch = 0x0150D9D0;     // on the Exploder (it): chunks launched, explosion velocity added
constexpr size_t kDataDebrisSettings = 0x9C;
constexpr size_t kDebrisChunks = 0x8C, kDebrisCount = 0x90, kChunkVelocity = 0x30;   // chunks: 0x9c bytes each
}  // namespace eng

constexpr int kDebrisLines = 12;                // debris10 lines logged

constexpr size_t kKeepPrune = 8192;   // kept handles before dead ones are pruned
constexpr int kLinesPerDef = 4;       // probe lines per (kind, definition): 300 sockets once starved every capsule
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
typedef void(__fastcall* DestroyFn)(uint8_t* part, void* edx, float delay, int mode);
typedef uint32_t(__fastcall* QueryFn)(const uint8_t* node, void* edx, uint32_t query);
typedef const float*(__fastcall* XformFn)(uint32_t* node, void* edx);
typedef void*(__cdecl* FastDebrisFn)(uint8_t* params, const float* t, uint32_t parent, const char* model);
typedef void(__fastcall* ParamSetFn)(uint8_t* params, void* edx, const char* key, const char* value, uint32_t interned);
typedef void(__fastcall* ParamsFreeFn)(uint8_t* params, void* edx);
typedef uint32_t(__fastcall* ThisArgFn)(void* self, void* edx, uintptr_t a1);
typedef uint32_t(__fastcall* BoomFn)(uint8_t* data, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                     uint32_t a5, uint32_t a6);

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
    DestroyFn g_destroy = nullptr;
    ShutFn g_restore = nullptr;
    bool drawQuery = false;                // FUN_018383e0's prologue matched; set once in install()
    std::mutex keepMx;                     // guards keep; held across actorOf while pruning
    std::unordered_set<uint32_t> keep;     // handles of actors made from 1.0 definitions
    BoomFn g_boom = nullptr;               // debris10's original; set once in install()
    std::mutex debrisMx;                   // guards debris; a leaf
    std::unordered_map<std::string, std::pair<std::string, std::string>> debris;   // lowercased entry: model, dna
    std::atomic<int> debrisLines{0};
    core::LogCount<std::string> fewLines{8000};   // capsuletrace10 lines per name, for 8000 names
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

// A probe line of this kind for this definition (data and model names) is still within its own budget.
inline bool defLine(const char* kind, const char* data, const char* model)
{
    return g_state.fewLines.first(std::string(kind).append(data).append(model), kLinesPerDef);
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
    if (!s.capsuleTrace10) return;
    char d[kNames], mo[kNames];
    defText(def, d, mo);
    if (!defLine("kept ", d, mo)) return;
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

// Definition idx of the manager's table; null when unreadable.
inline uint8_t* defAt(uint32_t idx)
{
    uint8_t* mgr = *(uint8_t**)core::rt(eng::kExpManager);
    if (!mgr || !core::readable(mgr + eng::kManagerDefs, 4)) return nullptr;
    uint8_t** table = *(uint8_t***)(mgr + eng::kManagerDefs);
    return table && core::readable(table + idx, 4) ? table[idx] : nullptr;
}

// The definition of a pending record (its +0 indexes the table); null when unreadable.
inline uint8_t* pendingDef(const uint8_t* record) { return record ? defAt(*(const uint32_t*)record) : nullptr; }

// The definition handle h's actor was spawned from, through its Expendables component; null for every other actor.
inline uint8_t* handleDef(uint32_t h)
{
    const uint8_t* comp = (const uint8_t*)((CompFn)core::rt(eng::kComponentOf))(h, *(uint16_t*)core::rt(eng::kExpType));
    if (!comp || !core::readable(comp + eng::kCompDef, 4)) return nullptr;
    return defAt(*(const uint32_t*)(comp + eng::kCompDef));
}

// The same for FUN_0074da00's this (actor + 0x1c).
inline uint8_t* actorDef(const uint8_t* part)
{
    if (!core::readable(part + eng::kPartActor, 4)) return nullptr;
    const uint8_t* actor = *(uint8_t* const*)(part + eng::kPartActor);
    if (!actor || !core::readable(actor + eng::kActorHandle, 4)) return nullptr;
    return handleDef(*(const uint32_t*)(actor + eng::kActorHandle));
}

// Handle h's render scene node (its component's +0x14 less 0x1a0); null when it has none.
inline const uint8_t* renderNode(uint32_t h)
{
    const CompFn componentOf = (CompFn)core::rt(eng::kComponentOf);
    const uint8_t* comp = (const uint8_t*)componentOf(h, *(uint16_t*)core::rt(eng::kRenderType));
    if (!comp || !core::readable(comp + eng::kRenderNode, 4)) return nullptr;
    const uintptr_t part = *(const uintptr_t*)(comp + eng::kRenderNode);
    const uint8_t* n = part > eng::kNodePart ? (const uint8_t*)(part - eng::kNodePart) : nullptr;
    return n && core::readable(n, eng::kNodeBytes) ? n : nullptr;
}

// A render node's world position: its matrix at +0x258 for the geometry type, else +0x1c (FUN_01604070).
inline const float* nodePos(const uint8_t* n)
{
    const uint16_t type = *(const uint16_t*)(n + eng::kNodeType) & eng::kTypeMask;
    const bool geom = type == *(const uint16_t*)core::rt(eng::kGeomType);
    return (const float*)(n + (geom ? eng::kGeomMatrix : eng::kNodeMatrix) + eng::kMatrixPos);
}

// FUN_018383e0's query 1 on n, as the renderer asks it; -1 when it cannot be asked safely.
inline int drawn(const uint8_t* n)
{
    const bool ask = g_state.drawQuery && core::readable(*(void* const*)(n + eng::kNodePart), 4);
    return ask ? (int)((QueryFn)core::rt(eng::kDrawQuery))(n, nullptr, 1) : -1;
}

// The actor's model component (FUN_007470a0, ActorHide's): +8 bit 0 shown, +0xc the model's scene node.
struct ModelState {
    const uint8_t* comp = nullptr;
    unsigned show = 0;
    const uint8_t* node = nullptr;
    unsigned hidden = 0;   // the node's +0x28 bits 0 (itself) and 1 (a parent) hidden (FUN_00469390)
    int children = -1;     // child nodes under node, -1 when unreadable
    std::string kids;      // per child: its hidden bits / its own child count
};

inline int childCount(const uint8_t* n)
{
    int k = 0;
    const uint8_t* c = *(const uint8_t* const*)(n + eng::kSceneChild);
    for (; c && k < eng::kChildCap && core::readable(c, eng::kSceneBytes); ++k)
        c = *(const uint8_t* const*)(c + eng::kSceneNext);
    return k;
}

inline ModelState modelState(uint32_t h)
{
    ModelState m;
    const CompFn componentOf = (CompFn)core::rt(eng::kComponentOf);
    m.comp = (const uint8_t*)componentOf(h, *(uint16_t*)core::rt(eng::kModelType));
    if (!m.comp || !core::readable(m.comp, eng::kModelBytes)) return m;
    m.show = m.comp[eng::kModelFlags] & 1;
    m.node = *(const uint8_t* const*)(m.comp + eng::kModelNode);
    if (!m.node || !core::readable(m.node, eng::kSceneBytes)) return m;
    m.hidden = *(const uint32_t*)(m.node + eng::kSceneFlags) & eng::kSceneHidden;
    m.children = 0;
    const uint8_t* c = *(const uint8_t* const*)(m.node + eng::kSceneChild);
    while (c && m.children < eng::kChildCap && core::readable(c, eng::kSceneBytes)) {
        ++m.children;
        m.kids += std::to_string(*(const uint32_t*)(c + eng::kSceneFlags) & eng::kSceneHidden) + "/" +
                  std::to_string(childCount(c)) + " ";
        c = *(const uint8_t* const*)(c + eng::kSceneNext);
    }
    return m;
}

// capsuletrace10: where a 1.0 definition's actor is, its shutdown state and whether its model is there and shown.
inline void logActor(const char* what, const uint8_t* actor)
{
    const uint32_t h = *(const uint32_t*)(actor + eng::kActorHandle);
    const uint8_t* def = handleDef(h);
    if (!def10(def)) return;
    char d[kNames], mo[kNames];
    defText(def, d, mo);
    if (!defLine(what, d, mo)) return;
    const float* p = (const float*)(actor + eng::kActorPos);
    const ModelState m = modelState(h);
    core::logf("capsuletrace10: actor %08X '%s' (%s) %s at (%.1f, %.1f, %.1f), flags %02X %02X mode %u; model comp %p "
               "show %u node %p hidden bits %X children %d [%s]", h, d, mo, what, p[0], p[1], p[2],
               actor[eng::kActorFlags], actor[eng::kActorFlags + 1], actor[eng::kActorMode], m.comp, m.show, m.node,
               m.hidden, m.children, m.kids.c_str());
}

// 0x0160A270, cdecl (handle, record), per async spawn completion of any expendable; it frees the record.
inline void __cdecl hkDone(uint32_t handle, uint8_t* record)
{
    State& s = g_state;
    bool killed = record && record[eng::kRecordKilled];
    uint8_t* def = pendingDef(record);
    bool is10 = def10(def);
    s.g_done(handle, record);
    char d[kNames] = "", mo[kNames] = "";
    if (s.capsuleTrace10) defText(def, d, mo);
    if (is10 && s.capsuleTrace10 && defLine("done ", d, mo))
        core::logf("capsuletrace10: async spawn of '%s' (%s) done: handle %08X -> actor %p%s", d, mo, handle,
                   ((ActorFn)core::rt(eng::kActorOf))(handle), killed ? ", killed (pending record cancelled)" : "");
    if (is10 && !killed && s.shutdown10) keep(handle, def);
    const uint8_t* a = is10 && !killed && s.capsuleTrace10 ? ((ActorFn)core::rt(eng::kActorOf))(handle) : nullptr;
    try {
        if (a && core::readable(a, eng::kActorBytes)) logActor("made", a);
    } catch (std::exception&) {
    }
}

// FUN_007535b0, thiscall on the shutdown manager (actor; ret 4), per restore: a camera or avatar came within range.
inline void __fastcall hkRestore(void* mgr, void*, uint8_t* actor)
{
    g_state.g_restore(mgr, nullptr, actor);
    try {
        if (actor && core::readable(actor, eng::kActorBytes)) logActor("restored", actor);
    } catch (std::exception&) {
    }
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
    char d[kNames], mo[kNames];
    defText(def, d, mo);
    if (defLine("cust ", d, mo)) {
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
    char d[kNames] = "", mo[kNames] = "";
    if (s.capsuleTrace10) defText(def, d, mo);
    if (s.capsuleTrace10 && defLine("base ", d, mo)) {
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

// An actor from a 1.0 definition is destroyed: which, and by whom (despawns: FUN_015f7520, FUN_015f4430).
inline void logDestroyed(const uint8_t* part, float delay, int how, void* ret)
{
    const uint8_t* def = actorDef(part);
    if (!def10(def)) return;
    char d[kNames], mo[kNames];
    defText(def, d, mo);
    if (!defLine("dead ", d, mo)) return;
    const uint8_t* actor = *(uint8_t* const*)(part + eng::kPartActor);
    core::logf("capsuletrace10: actor %08X '%s' (%s) destroyed (delay %.1f, mode %d) by %08X",
               *(const uint32_t*)(actor + eng::kActorHandle), d, mo, delay, how, core::toStaticVa((uintptr_t)ret));
}

// ---- debris10 ----

// The debris (model, dna) handle h's 1.0 definition explodes into; empty for none.
inline std::pair<std::string, std::string> debrisOf(uint32_t h)
{
    State& s = g_state;
    const uint8_t* def = handleDef(h);
    if (!def10(def) || !core::readable(def + eng::kDefName, 4)) return {};
    char name[kNames] = "";
    core::safeCopy(name, sizeof(name), *(const char* const*)(def + eng::kDefName));
    std::lock_guard<std::mutex> lock(s.debrisMx);
    auto it = s.debris.find(text::lower(name));
    return it == s.debris.end() ? std::pair<std::string, std::string>{} : it->second;
}

// The exploding node's transform as 3.0's explode reads it (FUN_006867e0): quaternion, position; false if odd.
inline bool transformOf(uint32_t node, float (&t)[eng::kXformFloats])
{
    if (!node || !core::readable((const uint8_t*)(uintptr_t)node, eng::kGeomMatrix + sizeof(t))) return false;
    memcpy(t, ((XformFn)core::rt(eng::kXformOf))(&node, nullptr), sizeof(t));
    const float q = t[0] * t[0] + t[1] * t[1] + t[2] * t[2] + t[3] * t[3];
    return q > 0.9f && q < 1.1f;
}

// 1.0's debris spawn (FUN_00f544f0: DNAFile, model, the exploder's handle) through 3.0's; its FastDebris or null.
inline void* spawnDebris(const std::string& model, const std::string& dna, const float* t, uint32_t parent)
{
    uint8_t params[eng::kParamsBytes] = {};
    ((ParamSetFn)core::rt(eng::kParamSet))(params, nullptr, "DNAFile", dna.c_str(), 0);
    void* fd = ((FastDebrisFn)core::rt(eng::kFastDebris))(params, t, parent, model.c_str());
    ((ParamsFreeFn)core::rt(eng::kParamsFree))(params, nullptr);
    return fd;
}

// FUN_0151dba0, thiscall on the Exploder component (node, 5 more; ret 0x18), per explode: 1.0's also spawns debris
// (FUN_00f544f0).
inline uint32_t __fastcall hkBoom(uint8_t* data, void*, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                  uint32_t a5, uint32_t a6)
{
    State& s = g_state;
    const uint32_t h = data ? *(const uint32_t*)data : 0;
    std::pair<std::string, std::string> debris;
    float t[eng::kXformFloats] = {};
    bool placed = false;
    try {
        if (h) debris = debrisOf(h);
        placed = !debris.first.empty() && transformOf(a1, t);
    } catch (std::exception&) {
    }
    const uint32_t r = s.g_boom(data, nullptr, a1, a2, a3, a4, a5, a6);
    if (debris.first.empty()) return r;
    uint8_t* fd = r && placed ? (uint8_t*)spawnDebris(debris.first, debris.second, t, h) : nullptr;
    const uint8_t* xd = (const uint8_t*)(uintptr_t)a4;
    const bool launch = fd && ((ActorFn)core::rt(eng::kActorOf))(h);
    if (launch) {
        const uintptr_t settings = core::readable(xd + eng::kDataDebrisSettings, 4)
                                       ? *(const uintptr_t*)(xd + eng::kDataDebrisSettings) : 0;
        ((ThisArgFn)core::rt(eng::kDebrisSettings))(fd, nullptr, settings);
        ((ThisArgFn)core::rt(eng::kDebrisLaunch))(data, nullptr, (uintptr_t)fd);
    }
    if (++s.debrisLines > kDebrisLines) return r;
    const uint32_t chunks = fd ? *(const uint32_t*)(fd + eng::kDebrisCount) : 0;
    const uint8_t* c0 = chunks ? *(const uint8_t* const*)(fd + eng::kDebrisChunks) : nullptr;
    const float* v = c0 && core::readable(c0 + eng::kChunkVelocity, 12) ? (const float*)(c0 + eng::kChunkVelocity)
                                                                          : nullptr;
    core::logf("debris10: actor %08X explodes (%u) into %s (%s) at (%.1f, %.1f, %.1f), q (%.2f %.2f %.2f %.2f) "
               "-> FastDebris %p%s%s, %u chunks, chunk 0 velocity (%.2f, %.2f, %.2f)", h, r, debris.first.c_str(),
               debris.second.c_str(), t[4], t[5], t[6], t[0], t[1], t[2], t[3], fd,
               placed ? "" : " (no transform: not spawned)", fd && !launch ? " (exploder gone: not launched)" : "",
               chunks, v ? v[0] : 0, v ? v[1] : 0, v ? v[2] : 0);
    return r;
}

// FUN_0074da00, thiscall on actor + 0x1c (delay, mode; ret 8), per actor destruction, any thread.
inline void __fastcall hkDestroy(uint8_t* part, void*, float delay, int how)
{
    try {
        logDestroyed(part, delay, how, _ReturnAddress());
    } catch (std::exception&) {
    }
    g_state.g_destroy(part, nullptr, delay, how);
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
    if (c.debris10 && c.popin10)
        ok = core::installFeature("debris10", {{"exploder explode, debris (debris10)", 0x0151DBA0,
                                                "6AFF68????????64A100000000", (void*)&detail::hkBoom,
                                                (void**)&s.g_boom}}) && ok;
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
        {"actor destroy (capsuletrace10)", 0x0074DA00, "83EC08803D????????0053568BF1", (void*)&detail::hkDestroy,
         (void**)&s.g_destroy},
        {"actor shutdown restore (capsuletrace10)", 0x007535B0, "568B7424088B4604578BF90FB70D????????",
         (void*)&detail::hkRestore, (void**)&s.g_restore},
    });
    s.drawQuery = core::prologueMatches(detail::eng::kDrawQuery, detail::eng::kDrawQueryPrologue);
    if (!s.drawQuery) core::logf("capsuletrace10: FUN_018383e0's prologue differs; 'drawn' is logged as -1");
    return ok;
}

inline void addDebris(const std::string& def, const std::string& model, const std::string& dna)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.debrisMx);
    s.debris[text::lower(def)] = {model, dna};
}

}  // namespace di1::shutdown
