// spawner10 / spawnfrom10 (SYS-3): 1.0 SpawnerQuarterback groups, their entries, SpawnFromActorNode. core, mernode.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/mernode.hpp"
#include "../../engine/slangvm.hpp"
#include "config.hpp"

namespace di1::spawner {

using engine::slangvm::Cell;
using engine::slangvm::Native;
using engine::slangvm::Vm;

// Installs the QB factory hook (spawner10) and the spawning feature (spawnfrom10) (H1, H2); false when refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kQbFactory = 0x010C2F70;    // cdecl (type, param) -> QB or null
constexpr uint32_t kAiQbUpdate = 0x01539C60;   // AIQuarterback vtable slot 3: thiscall (dt), ret 4
constexpr uint32_t kCreateByKey = 0x0075A4C0;  // cdecl (CString* key, vec3*, heading, 9 more) -> actor handle
constexpr uint32_t kStrCtor = 0x00418310;      // CString from char*: thiscall (s), ret 4
constexpr uint32_t kStrDtor = 0x00418230;      // CString destructor: thiscall ()
constexpr uint32_t kActorOf = 0x0074B200;      // cdecl (handle) -> actor or null (1.0 FUN_00726030)
constexpr uint32_t kComponent = 0x0069ECD0;    // cdecl (handle, type) -> component or null (1.0 FUN_00683e50)
constexpr uint32_t kCombatType = 0x02224020;   // data: u16, the Combat component's type (Combat_IsDead 0xeb2910)
constexpr uint32_t kDeadAt = 0x908;            // Combat component: dead flag byte, bit 7 (Combat_IsDead)
constexpr uint8_t kDeadBit = 0x80;
// 3.0's CreateActorFromCreationKey (0x75a830) passes these two constants; the other arguments are 0
constexpr int kCreateArg4 = 2;
constexpr int kCreateArg8 = 3;
}  // namespace eng

// 3.0's QB type table 0x2088228 names SpawnerQuarterback type 10, but its factory has no case 10 (returns 0).
constexpr int kSpawnerQb = 10;
// The 1.0 type subclasses AIQuarterback (1.0 vtable 0x146f3e4): 3.0's type 0 (0xc0 B, ctor FUN_0153e1b0).
constexpr int kAiQb = 0;

// A 1.0 spawner entry's state bits (+0x68, 1.0 FUN_00edcc20) and its set-once bits (+0x60, the setters)
constexpr uint32_t kSpawning = 1, kRepeat = 2, kMakeRoom = 4, kSkip = 0x10, kExpendable = 0x20, kEnabled = 0x40;
constexpr uint32_t kSetMax = 1, kSetMin = 2, kSetMakeRoom = 4, kSetDelay = 8, kSetDeath = 0x10;
constexpr int kVec = 3;

// SpawnFromActorNode's pins (1.0 0x10d19d0 binds them in this order)
enum Pin : int { kInit, kStart, kStop, kDecimate, kSpawner, kSpawnPos, kSpawnOri, kSpawned, kDied, kDecimated };

typedef void*(__cdecl* QbFactoryFn)(int type, int param);
typedef void(__fastcall* QbUpdateFn)(void* qb, void* edx, float dt);
typedef int32_t(__cdecl* CreateFn)(void* key, const float* pos, float heading, int, int, int, int, int, int, void*,
                                   int, int);
typedef void(__fastcall* StrCtorFn)(void* self, void* edx, const char* s);
typedef void(__fastcall* StrDtorFn)(void* self, void* edx);
typedef void*(__cdecl* ActorOfFn)(int32_t handle);
typedef uint8_t*(__cdecl* ComponentFn)(int32_t handle, uint32_t type);

// One spawner actor's entry (1.0 ctor FUN_00fe5010, 0x6c B): the fields its natives and the update use.
struct Entry {
    std::array<float, kVec> pos{};   // +0x14: where spawns appear
    std::string type;                // the creation key (SpawnActorType)
    std::vector<int32_t> spawned;    // +0x8: spawned actors still alive, oldest first
    int32_t max = 0, min = 0;        // +0x44 cap, +0x48 per wave
    float timer = 0.0f, delay = 0.0f, deathDelay = 0.0f;   // +0x4c, +0x50, +0x54
    uint32_t set = 0, state = 0;     // +0x60, +0x68
    int pending = 0;                 // spawns owed, made by the next QB update (engine thread)
    uint8_t* node = nullptr;         // +0x5c: the SpawnFromActorNode for its outputs; cleared when the node goes
};

// SpawnFromActorNode's own state (1.0 node +0xa8 spawner, +0xbc initialized)
struct NodeState {
    int32_t spawner;
    uint8_t initialized;
};

// What one update does to one entry, decided under mx and done without it (outputs run other nodes).
struct Plan {
    int32_t spawner = 0;
    uint8_t* node = nullptr;
    std::vector<int32_t> died;
    int spawn = 0;
    std::string type;
    std::array<float, kVec> pos{};
    bool decimated = false;
};

struct State {
    QbFactoryFn g_qbFactory = nullptr;   // set once by hook() in install()
    QbUpdateFn g_aiQbUpdate = nullptr;   // set once by installFeature() in install()
    int nodeClass = -1;                  // set once in install(): mernode class, -1 without spawnfrom10
    std::mutex mx;
    void* qb = nullptr;                           // guarded by mx: the live SpawnerQuarterback
    std::unordered_map<int32_t, Entry> entries;   // guarded by mx: by spawner actor handle
};
inline State g_state;

inline bool actorAlive(int32_t h) { return h && ((ActorOfFn)core::rt(eng::kActorOf))(h) != nullptr; }

// 3.0's Combat_IsDead: the component's dead bit; false without a Combat component.
inline bool actorDead(int32_t h)
{
    const uint32_t type = *(const uint16_t*)core::rt(eng::kCombatType);
    const uint8_t* c = ((ComponentFn)core::rt(eng::kComponent))(h, type);
    return c && (c[eng::kDeadAt] & eng::kDeadBit);
}

// An actor of creation key 'key' at pos, as 3.0's CreateActorFromCreationKey makes it; 0 for an unknown key.
inline int32_t createActor(const std::string& key, const std::array<float, kVec>& pos)
{
    void* str = nullptr;   // a CString is one pointer
    ((StrCtorFn)core::rt(eng::kStrCtor))(&str, nullptr, key.c_str());
    const int32_t h = ((CreateFn)core::rt(eng::kCreateByKey))(&str, pos.data(), 0.0f, eng::kCreateArg4, 0, 0, 0,
                                                              eng::kCreateArg8, 0, nullptr, 0, 0);
    ((StrDtorFn)core::rt(eng::kStrDtor))(&str, nullptr);
    return h;
}

// 1.0 FUN_00edcc20 (entry, dt, count, force): the spawn timer; -> actors to spawn now. Caller holds mx.
inline int timerStep(Entry& e, float dt, int count, bool force)
{
    if (!(e.state & kEnabled)) return 0;
    if (force) e.timer = e.delay;
    e.timer += dt;
    if (e.timer < e.delay) return 0;
    // ponytail: MakeRoomForSpawned's kill of the oldest (1.0 0xfdc9f0) needs a 3.0 kill call; a full entry waits
    if ((int32_t)e.spawned.size() >= e.max) return 0;
    e.timer = 0.0f;
    if (!(e.state & kRepeat)) e.state &= ~kSpawning;
    // ponytail: skip entries' scripts spawn on a world event 3.0 cannot route to the 1.0 tree; the timer spawns
    return force && (e.state & kSkip) ? 0 : count;
}

// 1.0 FUN_00fd58f0 / FUN_00fe0df0: spawned actors gone or dead leave the entry. Caller holds mx.
inline void reconcile(Entry& e, std::vector<int32_t>& died)
{
    std::vector<int32_t> alive;
    for (int32_t h : e.spawned) {
        const bool gone = !actorAlive(h) || actorDead(h);
        if (gone) died.push_back(h);
        else alive.push_back(h);
    }
    if (alive.size() != e.spawned.size()) e.timer = 0.0f;
    e.spawned.swap(alive);
}

// 1.0 FUN_010190a0's per-entry step: a dead spawner decimates, others reconcile and run the timer.
inline Plan plan(int32_t spawner, float dt)
{
    Plan p;
    std::lock_guard<std::mutex> lock(g_state.mx);
    auto it = g_state.entries.find(spawner);
    if (it == g_state.entries.end()) return p;
    Entry& e = it->second;
    p.spawner = spawner;
    p.node = e.node;
    if (!actorAlive(spawner)) {   // gone with its zone: its graph may be gone too, so no outputs
        g_state.entries.erase(it);
        p.node = nullptr;
        return p;
    }
    if (actorDead(spawner)) {   // 1.0 FUN_00fe50f0: the entry leaves the QB and fires Decimated
        p.decimated = true;
        g_state.entries.erase(it);
        return p;
    }
    reconcile(e, p.died);
    if (e.state & kSpawning) e.pending += timerStep(e, dt, e.min, false);
    p.spawn = e.pending;
    e.pending = 0;
    p.type = e.type;
    p.pos = e.pos;
    return p;
}

// 1.0 FUN_010135c0: a spawned actor joins its entry, then ActorSpawned(Who).
inline void spawnOne(const Plan& p)
{
    const int32_t h = createActor(p.type, p.pos);
    if (!h) return;
    uint8_t* node;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = g_state.entries.find(p.spawner);
        if (it == g_state.entries.end()) return;
        it->second.spawned.push_back(h);
        node = it->second.node;
    }
    if (node) mernode::fireAgent(node, g_state.nodeClass, kSpawned, h);
}

inline void run(const Plan& p)
{
    for (int32_t h : p.died)
        if (p.node) mernode::fireAgent(p.node, g_state.nodeClass, kDied, h);
    for (int i = 0; i < p.spawn && !p.type.empty(); ++i) spawnOne(p);
    if (p.decimated && p.node) mernode::fireAgent(p.node, g_state.nodeClass, kDecimated, p.spawner);
}

// The SpawnerQuarterback's update over its entries (engine thread).
inline void tick(float dt)
{
    std::vector<int32_t> keys;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        for (const auto& kv : g_state.entries) keys.push_back(kv.first);
    }
    for (int32_t k : keys) run(plan(k, dt));
}

// --- the 1.0 natives (FUN_00ed7ff0 .. FUN_00ee7da0): (QB, spawner[, value]); one QB per level, entries by spawner

inline int cellInt(const Cell& c) { return c.type == engine::slangvm::kFloat ? (int)c.f : c.i; }
inline float cellFloat(const Cell& c) { return c.type == engine::slangvm::kFloat ? c.f : (float)c.i; }

inline void pushFloat(const Vm& v, float f)
{
    Cell c;
    c.type = engine::slangvm::kFloat;
    c.f = f;
    v.push(c);
}

// The value and spawner of a (QB, spawner, value) call, popped; the QB handle goes unused.
struct Args3 {
    Cell value;
    int32_t spawner;
};
inline Args3 pop3(const Vm& v)
{
    Args3 a;
    a.value = v.pop();
    a.spawner = cellInt(v.pop());
    v.pop();
    return a;
}
inline int32_t pop2(const Vm& v)
{
    const int32_t spawner = cellInt(v.pop());
    v.pop();
    return spawner;
}

// 1.0 0xed80c0 / 0xed8110 / 0xed8070 / 0xed81b0: the first set wins (set-once bit), as 1.0's entry does.
template <uint32_t bit>
void __cdecl setOnce(uint8_t* ctx)
{
    const Args3 a = pop3(Vm{ctx});
    std::lock_guard<std::mutex> lock(g_state.mx);
    Entry& e = g_state.entries[a.spawner];
    if (e.set & bit) return;
    e.set |= bit;
    if constexpr (bit == kSetMax) e.max = cellInt(a.value);
    if constexpr (bit == kSetMin) e.min = cellInt(a.value);
    if constexpr (bit == kSetDelay) e.delay = cellFloat(a.value);
    if constexpr (bit == kSetDeath) e.deathDelay = cellFloat(a.value);
    if constexpr (bit == kSetMakeRoom) e.state = cellInt(a.value) ? e.state | kMakeRoom : e.state & ~kMakeRoom;
}

// 1.0 0xed8200: skip follows the value every call.
inline void __cdecl skipSpawn(uint8_t* ctx)
{
    const Args3 a = pop3(Vm{ctx});
    std::lock_guard<std::mutex> lock(g_state.mx);
    Entry& e = g_state.entries[a.spawner];
    e.state = cellInt(a.value) ? e.state | kSkip : e.state & ~kSkip;
}

// 1.0 0xed8030 -> +0x50.
inline void __cdecl getSpawnDelay(uint8_t* ctx)
{
    Vm v{ctx};
    const int32_t spawner = pop2(v);
    float delay = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = g_state.entries.find(spawner);
        if (it != g_state.entries.end()) delay = it->second.delay;
    }
    pushFloat(v, delay);
}

// 1.0 0xedf670: an enabled, spawning entry stops and its timer clears.
inline void __cdecl stopSpawning(uint8_t* ctx)
{
    const int32_t spawner = pop2(Vm{ctx});
    std::lock_guard<std::mutex> lock(g_state.mx);
    auto it = g_state.entries.find(spawner);
    if (it == g_state.entries.end()) return;
    Entry& e = it->second;
    if (!(e.state & kEnabled) || !(e.state & kSpawning)) return;
    e.state &= ~(kSpawning | kRepeat);
    e.timer = 0.0f;
}

// 1.0 0xed7ff0: bit 7 marks spawns still being created; 3.0 creates them at once, so never set.
inline void __cdecl needsReconciling(uint8_t* ctx)
{
    Vm v{ctx};
    pop2(v);
    v.pushInt(0);
}

// 1.0 0xed8240 -> FUN_00fd5ab0: the newest spawned actor of an enabled entry, 0 without one.
inline void __cdecl getLastActorSpawned(uint8_t* ctx)
{
    Vm v{ctx};
    const int32_t spawner = pop2(v);
    int32_t last = 0;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = g_state.entries.find(spawner);
        const bool any = it != g_state.entries.end() && (it->second.state & kEnabled) && !it->second.spawned.empty();
        if (any) last = it->second.spawned.back();
    }
    v.pushInt(actorAlive(last) ? last : 0);
}

// 1.0 0xee7da0 -> FUN_00ee6370: an enabled entry spawns one now at pos (skip still holds it back).
inline void __cdecl spawnActorInstantly(uint8_t* ctx)
{
    Vm v{ctx};
    std::array<float, kVec> pos{};
    for (int i = kVec - 1; i >= 0; --i) pos[i] = cellFloat(v.pop());
    const int32_t spawner = pop2(v);
    std::lock_guard<std::mutex> lock(g_state.mx);
    auto it = g_state.entries.find(spawner);
    if (it == g_state.entries.end() || !(it->second.state & kEnabled)) return;
    Entry& e = it->second;
    e.pos = pos;
    e.state |= kSpawning;
    e.pending += timerStep(e, 0.0f, 1, true);
}

// 1.0 0xf07ed0: creation parameters (SpawnSequence=Jump ...) for the spawned actor.
inline void __cdecl setAdditionalActorParms(uint8_t* ctx)
{
    // ponytail: 3.0's creation call takes no parameter string; the spawned actor keeps its DNA's defaults
    pop3(Vm{ctx});
}

}  // namespace detail

// Rows replace g_natives' stubs by name (later rows win); the SpawnerQuarterback_* natives of 1.0 scripts.
inline Native g_ports[] = {
    {"SpawnerQuarterback_SetMaxNumberOfSpawnableActors", 3, 0, &detail::setOnce<detail::kSetMax>},
    {"SpawnerQuarterback_SetMinNumberOfSpawnableActors", 3, 0, &detail::setOnce<detail::kSetMin>},
    {"SpawnerQuarterback_SetMakeRoomForSpawned", 3, 0, &detail::setOnce<detail::kSetMakeRoom>},
    {"SpawnerQuarterback_SetSpawnDelay", 3, 0, &detail::setOnce<detail::kSetDelay>},
    {"SpawnerQuarterback_SetDeathDelay", 3, 0, &detail::setOnce<detail::kSetDeath>},
    {"SpawnerQuarterback_SkipSpawn", 3, 0, &detail::skipSpawn},
    {"SpawnerQuarterback_SetAdditionalActorParms", 3, 0, &detail::setAdditionalActorParms},
    {"SpawnerQuarterback_GetSpawnDelay", 2, 1, &detail::getSpawnDelay},
    {"SpawnerQuarterback_StopSpawning", 2, 0, &detail::stopSpawning},
    {"SpawnerQuarterback_NeedsReconciling", 2, 1, &detail::needsReconciling},
    {"SpawnerQuarterback_GetLastActorSpawned", 2, 1, &detail::getLastActorSpawned},
    {"SpawnerQuarterback_SpawnActorInstantly", 5, 0, &detail::spawnActorInstantly},
};

namespace detail {

// --- SpawnFromActorNode (1.0 vtable 0x147e504)

inline NodeState& nodeState(uint8_t* node) { return *(NodeState*)mernode::stateOf(node); }

inline float floatParam(uint8_t* node, const char* name)
{
    const uint32_t raw = mernode::rawParam(node, name, 0);
    float f;
    memcpy(&f, &raw, sizeof(f));
    return f;
}

// The node's parameters (1.0 FUN_010cc470), read before taking mx.
struct NodeParams {
    int32_t max, min;
    bool makeRoom, expendable;
    float delay, deathDelay;
    std::string type;
    std::array<float, kVec> pos;
};
inline NodeParams readParams(uint8_t* node)
{
    NodeParams p;
    p.max = (int32_t)mernode::rawParam(node, "MaxNumSpawnedActors", 1);
    p.min = (int32_t)mernode::rawParam(node, "MinNumSpawnedActors", 1);
    p.makeRoom = mernode::rawParam(node, "MakeRoomForSpawned", 0) != 0;
    p.expendable = mernode::rawParam(node, "UseExpendable", 0) != 0;
    p.delay = floatParam(node, "SpawnDelay");
    p.deathDelay = floatParam(node, "DeathDelay");
    p.type = mernode::strParam(node, "SpawnActorType");
    p.pos = mernode::fetchVec3(node, g_state.nodeClass, kSpawnPos).value_or(std::array<float, kVec>{});
    return p;
}

// 1.0 FUN_010c9ec0: the first node to initialize an entry enables it; the scripts' own settings stay.
inline void enable(Entry& e, const NodeParams& p, uint8_t* node)
{
    e.state |= kEnabled;
    if (!(e.set & kSetMax)) e.max = p.max;
    if (!(e.set & kSetMin)) e.min = p.min;
    if (!(e.set & kSetMakeRoom) && p.makeRoom) e.state |= kMakeRoom;
    if (!(e.set & kSetDelay)) e.delay = p.delay;
    if (!(e.set & kSetDeath)) e.deathDelay = p.deathDelay;
    e.set |= kSetMax | kSetMin | kSetMakeRoom | kSetDelay | kSetDeath;
    e.state = p.expendable ? e.state | kExpendable : e.state & ~kExpendable;
    e.type = p.type;
    e.pos = p.pos;
    e.node = node;
}

// 1.0 FUN_010ca060: once per node, the Spawner input's entry.
inline void initialize(uint8_t* node)
{
    NodeState& ns = nodeState(node);
    if (ns.initialized) return;
    ns.initialized = 1;
    const auto spawner = mernode::fetchAgent(node, g_state.nodeClass, kSpawner);
    if (!spawner || !actorAlive(*spawner)) return;
    ns.spawner = *spawner;
    const NodeParams p = readParams(node);
    std::lock_guard<std::mutex> lock(g_state.mx);
    Entry& e = g_state.entries[*spawner];
    if (!(e.state & kEnabled)) enable(e, p, node);
}

// The node's enabled entry, or nullptr. Caller holds mx.
inline Entry* enabledEntry(uint8_t* node)
{
    auto it = g_state.entries.find(nodeState(node).spawner);
    if (it == g_state.entries.end() || !(it->second.state & kEnabled)) return nullptr;
    return &it->second;
}

// 1.0 FUN_010a5fd0: a stopped entry starts a wave now and repeats it.
inline void start(uint8_t* node)
{
    std::lock_guard<std::mutex> lock(g_state.mx);
    Entry* e = enabledEntry(node);
    if (!e || (e->state & kSpawning)) return;
    e->state |= kSpawning | kRepeat;
    e->timer = 0.0f;
    e->pending += timerStep(*e, 0.0f, e->min, true);
}

// 1.0 FUN_0109ede0: a spawning entry stops.
inline void stop(uint8_t* node)
{
    std::lock_guard<std::mutex> lock(g_state.mx);
    Entry* e = enabledEntry(node);
    if (!e || !(e->state & kSpawning)) return;
    e->state &= ~(kSpawning | kRepeat);
    e->timer = 0.0f;
}

// 1.0 FUN_0109ee30 -> FUN_00fe9f10 / FUN_00fe50f0: the entry leaves and Decimated fires.
inline void decimate(uint8_t* node)
{
    const int32_t spawner = nodeState(node).spawner;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = g_state.entries.find(spawner);
        if (it == g_state.entries.end() || !(it->second.state & kEnabled)) return;
        g_state.entries.erase(it);
    }
    mernode::fireAgent(node, g_state.nodeClass, kDecimated, spawner);
}

// The class's impulse handler (Meridian thread): 1.0's four input handlers (0x10d19d0).
inline void impulse(uint8_t* node, int pin, void*)
{
    try {
        switch (pin) {
        case kInit: initialize(node); break;
        case kStart: start(node); break;
        case kStop: stop(node); break;
        case kDecimate: decimate(node); break;
        default: break;
        }
    } catch (...) {   // H6: bad_alloc only; the node stays as it was
    }
}

// The node is going: entries stop pointing at it (H11).
inline void destroy(uint8_t* node)
{
    std::lock_guard<std::mutex> lock(g_state.mx);
    for (auto& kv : g_state.entries)
        if (kv.second.node == node) kv.second.node = nullptr;
}

inline mernode::Spec nodeSpec()
{
    using mernode::DataV;
    using mernode::In;
    using mernode::Out;
    return {"SpawnFromActorNode",
            {{"Initialize", In}, {"StartSpawning", In}, {"StopSpawning", In}, {"Decimate", In},
             {"Spawner", DataV}, {"SpawnPosition", DataV}, {"SpawnOrientation", DataV},
             {"ActorSpawned", Out}, {"SpawnedActorDied", Out}, {"Decimated", Out}},
            {{"NetSyncType", ::mer::T_INT}, {"MaxNumSpawnedActors", ::mer::T_INT},
             {"MinNumSpawnedActors", ::mer::T_INT}, {"MakeRoomForSpawned", ::mer::T_BOOL},
             {"SpawnDelay", ::mer::T_FLOAT}, {"DeathDelay", ::mer::T_FLOAT}, {"SpawnActorType", ::mer::T_STRING},
             {"UseExpendable", ::mer::T_BOOL}, {"SpawnExpendableType", ::mer::T_STRING}},
            sizeof(NodeState),
            &impulse,
            &destroy};
}

// --- hooks

// FUN_010c2f70, cdecl (type, param) -> QB or null; SetActive / AddActor call through it unchecked. Per QB creation.
inline void* __cdecl hkQbFactory(int type, int param)
{
    if (type != kSpawnerQb) return g_state.g_qbFactory(type, param);
    void* qb = g_state.g_qbFactory(kAiQb, param);
    core::logf("spawner10: SpawnerQuarterback (QB type 10, no 3.0 factory case) made as its AIQuarterback base -> %p",
               qb);
    std::lock_guard<std::mutex> lock(g_state.mx);
    g_state.qb = qb;
    return qb;
}

// FUN_01539c60, thiscall (dt; ret 4): every AIQuarterback's update, engine thread, per frame. 1.0 FUN_010190a0.
inline void __fastcall hkAiQbUpdate(void* qb, void* edx, float dt)
{
    g_state.g_aiQbUpdate(qb, edx, dt);
    bool spawner;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        spawner = qb && qb == g_state.qb;
    }
    if (!spawner) return;
    try {
        tick(dt);
    } catch (...) {   // H6: bad_alloc only; the entries keep their state for the next update
    }
}

struct Call {
    uint32_t va;
    const char* bytes;
};
// The engine functions spawnfrom10 calls, each checked before the first use (H4)
inline constexpr Call kCalls[] = {
    {eng::kCreateByKey, "64A1000000006AFF68????????506489250000000083EC10"},
    {eng::kStrCtor, "8B44240456508BF1E8"},
    {eng::kStrDtor, "568B3183EE0C817E08FFFFFF7F"},
    {eng::kActorOf, "83EC088B44240C8D0C2451"},
    {eng::kComponent, "8B5424088B0D????????8B018B4010"},
};

inline bool callsMatch()
{
    for (const Call& k : kCalls) {
        if (core::prologueMatches(k.va, k.bytes)) continue;
        core::logf("spawnfrom10 off: %08X differs from the analysed bytes", k.va);
        return false;
    }
    return true;
}

}  // namespace detail

// True once spawnfrom10 checked its actor calls and added its class: the calls below may run.
inline bool actorCallsOn() { return detail::g_state.nodeClass >= 0; }
// An actor of creation key 'key' at pos (CreateActorFromCreationKey 0x75a4c0); 0 for an unknown key.
inline int32_t createActor(const std::string& key, const std::array<float, detail::kVec>& pos)
{
    return detail::createActor(key, pos);
}
// The actor is gone, or Combat marks it dead.
inline bool actorGone(int32_t h) { return !detail::actorAlive(h) || detail::actorDead(h); }

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.spawner10) return true;
    const bool factory = core::hook("quarterback factory (spawner10)", detail::eng::kQbFactory,
                                    "64A1000000006AFF68????????50", (void*)&detail::hkQbFactory,
                                    (void**)&s.g_qbFactory);
    if (!factory || !c.spawnFrom10 || !detail::callsMatch()) return factory;
    const core::HookSpec update{"AIQuarterback update (spawnfrom10)", detail::eng::kAiQbUpdate, "53568BF18B463057",
                                (void*)&detail::hkAiQbUpdate, (void**)&s.g_aiQbUpdate};
    if (!core::installFeature("spawnfrom10", {update})) return false;
    s.nodeClass = mernode::add(detail::nodeSpec());
    if (s.nodeClass < 0) core::logf("spawnfrom10: SpawnFromActorNode not added (native class table full)");
    return true;
}

}  // namespace di1::spawner
