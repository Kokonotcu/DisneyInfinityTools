// 1.0 expendables drawn while shut down (shutdown10), exploding into their DebrisModel (debris10).
// core, popin. Spawn threads.
#pragma once
#include <windows.h>
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

// Installs shutdown10's four hooks and debris10's, as c switches them on (H1, H2).
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
constexpr uint32_t kExpManager = 0x0228AA40;    // *: the expendables manager
constexpr size_t kManagerDefs = 0x1C;           // its definition table; a pending record's +0 indexes it
constexpr size_t kRecordKilled = 0x80;          // a pending record's cancel flag: the completion kills the actor
constexpr size_t kActorHandle = 4;
constexpr size_t kActorFlags = 0x10C;           // bit 8: shut down
constexpr uint8_t kShutDown = 8;
constexpr size_t kDefPopin = 0x9C;              // a definition's pop-in range squared: popin10's stamp
constexpr size_t kGeomMatrix = 0x258;           // a geometry node's matrix (FUN_01604070)
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
constexpr size_t kNames = 96;         // definition name buffers

typedef uint32_t*(__fastcall* SpawnFn)(uint8_t* def, void* edx, uint32_t* out, const float* t, uint8_t* params);
typedef void(__cdecl* DoneFn)(uint32_t handle, uint8_t* record);
typedef void(__fastcall* ShutFn)(void* mgr, void* edx, uint8_t* actor);
typedef void(__fastcall* RenderShutFn)(void* comp, void* edx, uint8_t on);
typedef uint8_t*(__cdecl* ActorFn)(uint32_t handle);
typedef void*(__cdecl* CompFn)(uint32_t handle, uint32_t type);
typedef const float*(__fastcall* XformFn)(uint32_t* node, void* edx);
typedef void*(__cdecl* FastDebrisFn)(uint8_t* params, const float* t, uint32_t parent, const char* model);
typedef void(__fastcall* ParamSetFn)(uint8_t* params, void* edx, const char* key, const char* value, uint32_t interned);
typedef void(__fastcall* ParamsFreeFn)(uint8_t* params, void* edx);
typedef uint32_t(__fastcall* ThisArgFn)(void* self, void* edx, uintptr_t a1);
typedef uint32_t(__fastcall* BoomFn)(uint8_t* data, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                     uint32_t a5, uint32_t a6);

struct State {
    bool shutdown10 = false;   // set once in install()
    // originals: set once by installFeature in install(); g_base is the base spawn's, not core::g_base (B-19)
    SpawnFn g_base = nullptr;
    DoneFn g_done = nullptr;
    ShutFn g_shut = nullptr;
    RenderShutFn g_renderShut = nullptr;   // null while shutdown10 is off: keep() checks it
    std::mutex keepMx;                     // guards keep; held across actorOf while pruning
    std::unordered_set<uint32_t> keep;     // handles of actors made from 1.0 definitions
    BoomFn g_boom = nullptr;               // debris10's original; set once in install()
    std::mutex debrisMx;                   // guards debris; a leaf
    std::unordered_map<std::string, std::pair<std::string, std::string>> debris;   // lowercased entry: model, dna
    std::atomic<int> debrisLines{0};
};
inline State g_state;

// Per shutdown apply on its thread: set around one engine call, cleared after it.
inline thread_local int t_keep;

// A definition from a 1.0 package: popin10 stamped it (1.0 entries never carry PopinDistance).
inline bool def10(const uint8_t* def)
{
    return def && core::readable(def + eng::kDefPopin, 4) && popin::isStamped(def);
}

// ---- shutdown10 ----

inline bool kept(uint32_t h)
{
    State& s = g_state;
    std::lock_guard<std::mutex> l(s.keepMx);
    return s.keep.count(h) != 0;
}

// Handle h's actor stays drawn while shut down; one already shut down is shown again, as its restore does.
inline void keep(uint32_t h)
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
    if (!shutDown) return;
    void* comp = ((CompFn)core::rt(eng::kComponentOf))(h, *(uint16_t*)core::rt(eng::kRenderType));
    if (comp && s.g_renderShut) s.g_renderShut(comp, nullptr, 0);
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

// 0x0160A270, cdecl (handle, record), per async spawn completion of any expendable; it frees the record.
inline void __cdecl hkDone(uint32_t handle, uint8_t* record)
{
    State& s = g_state;
    bool killed = record && record[eng::kRecordKilled];
    bool is10 = def10(pendingDef(record));
    s.g_done(handle, record);
    if (is10 && !killed && s.shutdown10) keep(handle);
}

// FUN_0160a3e0, thiscall (out, transform, params; ret 0xc), per base spawn of any package; 0 out: queued (async).
inline uint32_t* __fastcall hkBase(uint8_t* def, void*, uint32_t* out, const float* t, uint8_t* params)
{
    State& s = g_state;
    uint32_t* r = s.g_base(def, nullptr, out, t, params);
    if (!def10(def)) return r;
    uint32_t h = out ? *out : 0;   // 0: queued (async), the completion marks it
    if (h && s.shutdown10) keep(h);
    return r;
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

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.shutdown10 = c.shutdown10;
    if (c.shutdown10 && !c.popin10) core::logf("shutdown10: off, it finds 1.0 definitions by popin10's stamp");
    const bool keep10 = c.shutdown10 && c.popin10;
    const bool spawnsHooked = keep10 && core::installFeature("shutdown10", {
        {"actor shutdown apply (shutdown10)", 0x007534C0, "568B7424088B4604578BF9", (void*)&detail::hkShut,
         (void**)&s.g_shut},
        {"render component shutdown (shutdown10)", 0x01860B40, "83EC08E8????????85C0745C",
         (void*)&detail::hkRenderShut, (void**)&s.g_renderShut},
        {"expendable base spawn (shutdown10)", 0x0160A3E0, "558BEC83E4F06AFF68????????64A1", (void*)&detail::hkBase,
         (void**)&s.g_base},
        {"expendable async spawn done (shutdown10)", 0x0160A270, "8B0D????????568B74240C", (void*)&detail::hkDone,
         (void**)&s.g_done},
    });
    bool ok = spawnsHooked || !keep10;
    if (c.debris10 && c.popin10)
        ok = core::installFeature("debris10", {{"exploder explode, debris (debris10)", 0x0151DBA0,
                                                "6AFF68????????64A100000000", (void*)&detail::hkBoom,
                                                (void**)&s.g_boom}}) && ok;
    return ok;
}

inline void addDebris(const std::string& def, const std::string& model, const std::string& dna)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.debrisMx);
    s.debris[text::lower(def)] = {model, dna};
}

}  // namespace di1::shutdown
