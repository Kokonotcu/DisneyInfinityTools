// scourge10: 1.0 ScourgeWave as a native Meridian class (pods, Died, Empty). mernode, spawner. Meridian thread.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/mernode.hpp"
#include "config.hpp"
#include "spawner.hpp"

namespace di1::scourge {

// Adds ScourgeWave and hooks the Meridian update when spawnfrom10 is on (H1, H2); false when refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kMerUpdate = 0x006B6110;   // cdecl (dt): Meridian's per-frame update of updatables (FUN_006bc1f0)
constexpr uint32_t kTagFind = 0x0070F900;     // thiscall (type, name) on *kTagDb -> ActorTag handle or 0, ret 8
constexpr uint32_t kTagDb = 0x022039C8;       // data: the object kTagFind runs on (caller FUN_00dc71d0)
constexpr uint32_t kTagAdd = 0x01498930;      // thiscall (actor, tag, weight) on *kTagMgr, ret 0xc (1.0 FUN_00f77700)
constexpr uint32_t kTagMgr = 0x02289720;      // data: the actor tag manager (ActorTagNode Register FUN_0173a0f0)
// Combat_SetInvulnerableOn / Off's calls (natives 0xeb8be0 / 0xeb8c30), thiscall on the Combat component
constexpr uint32_t kShieldOn = 0x00FF5050;    // (duration; ret 4): ENEMY_CANNOT_BE_ATTACKED; 0 lasts until Off
constexpr uint32_t kShieldOff = 0x00FF50C0;   // (): clears it; its timer FUN_00ff5120 skips a duration <= 0
}  // namespace eng

// Pod types in 1.0's order (FUN_00c95e40 loops 0..4); Junior n guards vine n
constexpr int kTypes = 5;
constexpr int kMother = 4;
// 1.0's creation keys by pod type (table 0x015e28ac); incredibles_actors.lua names them
inline constexpr const char* kKeys[kTypes] = {"ScourgePod", "ScourgeJunior", "ScourgeJunior", "ScourgeJunior",
                                              "ScourgeMother"};
constexpr float kTagWeight = 0.25f;   // 1.0 FUN_00c95e40 passes 0x3e800000
// 1.0 replaces this ActorTagToUse with "" (DAT_0127d4f4), so no tag
inline constexpr const char* kCurrentMission = "<CurrentMission>";

// ScourgeWave's pins in the 1.0 graphs' numbering: impulse in, data in, impulse out, data out (describe 0xd3fa80)
enum Pin : int {
    kActivate, kDeactivate,
    kBabyLocators, kJunior1Locators, kJunior2Locators, kJunior3Locators, kMotherLocators,
    kVine1Collision, kVine2Collision, kVine3Collision,
    kStarted, kBabyDied, kJunior1Died, kJunior2Died, kJunior3Died, kMotherDied, kEmpty,
    kPodKiller
};

typedef void(__cdecl* MerUpdateFn)(float dt);
typedef uint32_t(__fastcall* TagFindFn)(void* db, void* edx, const char* type, const char* name);
typedef void(__fastcall* TagAddFn)(void* mgr, void* edx, int32_t actor, uint32_t tag, float weight);
typedef void(__fastcall* ShieldOnFn)(void* combat, void* edx, float duration);
typedef void(__fastcall* ShieldOffFn)(void* combat, void* edx);
constexpr float kUntilOff = 0.0f;

struct Pod {
    int32_t actor;
    int type;
};

// One activated node's pods (1.0's 0x30 B wave record, FUN_00c95e40)
struct Wave {
    uint8_t* node = nullptr;
    bool pending = true;      // spawns on the next Meridian update, as 1.0's QB queue (FUN_00c788e0)
    std::vector<Pod> pods;    // alive; a dead pod leaves (1.0 zeroes its slot, FUN_00c8bb60)
};

// The node's own state: PodKillerHandle's DataElement (1.0 node +0x70)
struct NodeState {
    mernode::AgentPayload killer;
};

// An output to fire once mx is released (outputs run other nodes)
struct Fire {
    uint8_t* node;
    int pin;
};

// What an update's deaths lead to, done once mx is released
struct Outcome {
    std::vector<Fire> fires;
    std::vector<int32_t> unshield;   // mothers whose last junior died
};

struct State {
    MerUpdateFn g_merUpdate = nullptr;   // set once by installFeature() in install()
    int nodeClass = -1;                  // set once in install(): mernode class
    std::mutex mx;
    std::vector<Wave> waves;             // guarded by mx
    std::atomic<bool> any{false};        // waves is not empty: the per-frame early out (F1)
    std::vector<int32_t> live;           // Meridian thread only: livePods' buffer, reused each frame (F1)
};
inline State g_state;

// Caller holds mx.
inline std::vector<Wave>::iterator waveOf(uint8_t* node)
{
    std::vector<Wave>& w = g_state.waves;
    auto it = w.begin();
    while (it != w.end() && it->node != node) ++it;
    return it;
}

inline void fireAll(const std::vector<Fire>& fires)
{
    for (const Fire& f : fires) mernode::fire(f.node, g_state.nodeClass, f.pin, nullptr);
}

// The ActorTag a wave's pods get, 0 for none (1.0 FUN_00c95e40: FUN_006edd30("ActorTag", name)).
inline uint32_t tagOf(uint8_t* node)
{
    const std::string name = mernode::strParam(node, "ActorTagToUse");
    void* db = *(void**)core::rt(eng::kTagDb);
    if (name.empty() || name == kCurrentMission || !db) return 0;
    return ((TagFindFn)core::rt(eng::kTagFind))(db, nullptr, "ActorTag", name.c_str());
}

inline void tagActor(int32_t actor, uint32_t tag)
{
    void* mgr = *(void**)core::rt(eng::kTagMgr);
    if (mgr) ((TagAddFn)core::rt(eng::kTagAdd))(mgr, nullptr, actor, tag, kTagWeight);
}

// 1.0 FUN_00c95e40: a pod of each type at each of its locators, tagged unless MotherOnlyMission spares it.
inline std::vector<Pod> spawnPods(uint8_t* node)
{
    const uint32_t tag = tagOf(node);
    const bool motherOnly = mernode::rawParam(node, "MotherOnlyMission", 0) != 0;
    std::vector<Pod> pods;
    for (int type = 0; type < kTypes; ++type) {
        // ponytail: 3.0's creation takes a heading, not 1.0's whole locator transform; pods face heading 0
        for (const auto& pos : mernode::fetchPositions(node, g_state.nodeClass, kBabyLocators + type)) {
            const int32_t h = spawner::createActor(kKeys[type], pos);
            if (!h) continue;
            pods.push_back({h, type});
            const bool tagged = !motherOnly || type == kMother;
            if (tag && tagged) tagActor(h, tag);
        }
    }
    return pods;
}

inline bool isJunior(const Pod& p) { return p.type > 0 && p.type < kMother; }

// The wave's mother, 0 when it has none (Caller holds mx, or owns pods).
inline int32_t motherOf(const std::vector<Pod>& pods)
{
    for (const Pod& p : pods)
        if (p.type == kMother) return p.actor;
    return 0;
}

// 1.0 FUN_00c7e400: a mother takes no damage while a junior of its wave lives; 3.0's Combat shield stands in.
inline void shield(int32_t mother, bool on)
{
    uint8_t* combat = spawner::combatOf(mother);
    if (!combat) {
        core::logf("scourge10: mother pod %08X has no Combat component; it stays unshielded", (unsigned)mother);
        return;
    }
    if (on) ((ShieldOnFn)core::rt(eng::kShieldOn))(combat, nullptr, kUntilOff);
    else ((ShieldOffFn)core::rt(eng::kShieldOff))(combat, nullptr);
}

// A new wave's mother is shielded when it has juniors.
inline void shieldNewMother(const std::vector<Pod>& pods)
{
    const int32_t mother = motherOf(pods);
    const bool juniors = std::any_of(pods.begin(), pods.end(), isJunior);
    if (mother && juniors) shield(mother, true);
}

// The pending waves spawn (engine calls without mx), then join their wave if it is still there.
inline void spawnPending()
{
    std::vector<uint8_t*> nodes;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        for (Wave& w : g_state.waves) {
            if (!w.pending) continue;
            w.pending = false;
            nodes.push_back(w.node);
        }
    }
    for (uint8_t* node : nodes) {
        std::vector<Pod> pods = spawnPods(node);
        core::logf("scourge10: ScourgeWave %p spawned %u pods", (void*)node, (unsigned)pods.size());
        shieldNewMother(pods);
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = waveOf(node);
        if (it != g_state.waves.end()) it->pods = std::move(pods);
    }
}

// Every live pod into g_state.live, copied under mx, so the engine checks run without it.
inline void livePods()
{
    g_state.live.clear();
    std::lock_guard<std::mutex> lock(g_state.mx);
    for (const Wave& w : g_state.waves)
        for (const Pod& p : w.pods) g_state.live.push_back(p.actor);
}

// 3.0's view of a pod's death: gone, or Combat's dead bit; pods lack 1.0's ScourgePlant component.
inline std::vector<int32_t> deadPods()
{
    livePods();
    std::vector<int32_t> dead;
    for (int32_t h : g_state.live)
        if (spawner::actorGone(h)) dead.push_back(h);
    return dead;
}

// One wave's dead pods leave: <type>Died each; its mother unshields with its last junior. Caller holds mx.
inline void removeDeadOf(Wave& w, const std::vector<int32_t>& dead, Outcome& o)
{
    const bool hadJuniors = std::any_of(w.pods.begin(), w.pods.end(), isJunior);
    std::vector<Pod> alive;
    for (const Pod& p : w.pods) {
        const bool gone = std::find(dead.begin(), dead.end(), p.actor) != dead.end();
        if (gone) o.fires.push_back({w.node, kBabyDied + p.type});
        else alive.push_back(p);
    }
    w.pods.swap(alive);
    const bool lastJunior = hadJuniors && !std::any_of(w.pods.begin(), w.pods.end(), isJunior);
    const int32_t mother = motherOf(w.pods);
    if (lastJunior && mother) o.unshield.push_back(mother);
}

// Dead pods leave; an emptied wave fires Empty and goes (1.0 FUN_00caf3e0, FUN_00c8baa0, FUN_00c961b0).
inline Outcome removeDead(const std::vector<int32_t>& dead)
{
    Outcome o;
    std::lock_guard<std::mutex> lock(g_state.mx);
    std::vector<Wave>& waves = g_state.waves;
    for (auto w = waves.begin(); w != waves.end();) {
        const size_t before = w->pods.size();
        removeDeadOf(*w, dead, o);
        const bool emptied = before && w->pods.empty();
        if (emptied) o.fires.push_back({w->node, kEmpty});
        w = emptied ? waves.erase(w) : w + 1;
    }
    g_state.any = !waves.empty();
    return o;
}

// One Meridian update: pending waves spawn, dead pods report, unguarded mothers lose their shield.
inline void tick()
{
    spawnPending();
    const std::vector<int32_t> dead = deadPods();
    if (dead.empty()) return;
    const Outcome o = removeDead(dead);
    for (int32_t mother : o.unshield) shield(mother, false);
    fireAll(o.fires);
}

// --- the node (1.0 vtable 0x013586c8, handlers 0xd3a5d0 / 0xd3a620)

// 1.0 0xd3a5d0: Started fires, then the wave queues; a node with a wave keeps it.
inline void activate(uint8_t* node)
{
    mernode::fire(node, g_state.nodeClass, kStarted, nullptr);
    std::lock_guard<std::mutex> lock(g_state.mx);
    // ponytail: 1.0 would queue a second wave of the same node; one per node here
    if (waveOf(node) != g_state.waves.end()) return;
    Wave w;
    w.node = node;
    g_state.waves.push_back(std::move(w));
    g_state.any = true;
}

// 1.0 0xd3a620 -> FUN_00c960f0(id, 1, 0): each live pod's Died fires, no Empty, the wave goes.
inline void deactivate(uint8_t* node)
{
    std::vector<Fire> fires;
    int32_t mother = 0;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = waveOf(node);
        if (it == g_state.waves.end()) return;
        // ponytail: 1.0's plant component kills the pods (FUN_00ab8d90); no 3.0 kill call, they stay standing
        for (const Pod& p : it->pods) fires.push_back({node, kBabyDied + p.type});
        mother = motherOf(it->pods);
        g_state.waves.erase(it);
        g_state.any = !g_state.waves.empty();
    }
    // a standing mother no wave guards any more must not stay invulnerable
    if (mother && !spawner::actorGone(mother)) shield(mother, false);
    fireAll(fires);
}

// The class's impulse handler (Meridian thread).
inline void impulse(uint8_t* node, int pin, void*)
{
    try {
        if (pin == kActivate) activate(node);
        if (pin == kDeactivate) deactivate(node);
    } catch (...) {   // H6: bad_alloc only; the waves stay as they were
    }
}

// The node is going: its wave goes too, without outputs (H11).
inline void destroy(uint8_t* node)
{
    std::lock_guard<std::mutex> lock(g_state.mx);
    auto it = waveOf(node);
    if (it != g_state.waves.end()) g_state.waves.erase(it);
    g_state.any = !g_state.waves.empty();
}

// PodKillerHandle (1.0 getter 0xd29e10 -> node +0x70): 3.0 knows no killer, so the handle stays 0.
inline void* killer(uint8_t* node, int)
{
    NodeState& ns = *(NodeState*)mernode::stateOf(node);
    ns.killer.vft = mernode::g_state.engine.agentElemVt;
    ns.killer.type = mernode::g_state.engine.agentType;
    return &ns.killer;
}

inline mernode::Spec nodeSpec()
{
    using mernode::Data;
    using mernode::DataOut;
    using mernode::In;
    using mernode::Out;
    return {"ScourgeWave",
            {{"Activate", In}, {"Deactivate", In},
             {"BabyLocators", Data}, {"Junior1Locators", Data}, {"Junior2Locators", Data},
             {"Junior3Locators", Data}, {"MotherLocators", Data},
             {"Vine1Collision", Data}, {"Vine2Collision", Data}, {"Vine3Collision", Data},
             {"Started", Out}, {"BabyDied", Out}, {"Junior1Died", Out}, {"Junior2Died", Out},
             {"Junior3Died", Out}, {"MotherDied", Out}, {"Empty", Out},
             {"PodKillerHandle", DataOut}},
            {{"NetSyncType", ::mer::T_INT}, {"ActorTagToUse", ::mer::T_STRING}, {"MotherOnlyMission", ::mer::T_BOOL},
             {"Vine1Choreography", ::mer::T_STRING}, {"Vine2Choreography", ::mer::T_STRING},
             {"Vine3Choreography", ::mer::T_STRING}},
            sizeof(NodeState),
            &impulse,
            &destroy,
            &killer};
}

// --- hooks

// FUN_006b6110, cdecl (dt): Meridian's updatables, Meridian thread, per frame. Stands in for 1.0's FUN_00c9aff0.
inline void __cdecl hkMerUpdate(float dt)
{
    g_state.g_merUpdate(dt);
    if (!g_state.any) return;
    try {
        tick();
    } catch (...) {   // H6: bad_alloc only; the waves keep their state for the next update
    }
}

struct Call {
    uint32_t va;
    const char* bytes;
};
// The engine functions scourge10 calls besides spawnfrom10's, each checked before the first use (H4)
inline constexpr Call kCalls[] = {
    {eng::kTagFind, "8B442404566A01508BF1E8"},
    {eng::kTagAdd, "8B44240457508BF9E8"},
    {eng::kShieldOn, "56578BF90FB70D????????8B0751"},
    {eng::kShieldOff, "0FB705????????8B09565051E8"},
};

inline bool callsMatch()
{
    for (const Call& k : kCalls) {
        if (core::prologueMatches(k.va, k.bytes)) continue;
        core::logf("scourge10 off: %08X differs from the analysed bytes", k.va);
        return false;
    }
    return true;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.scourge10) return true;
    if (!spawner::actorCallsOn()) {
        core::logf("scourge10 off: it needs spawnfrom10's actor calls");
        return true;
    }
    if (!detail::callsMatch()) return true;
    const core::HookSpec update{"Meridian update (scourge10)", detail::eng::kMerUpdate, "E8CBB6FFFF0F57C00F2F442404",
                                (void*)&detail::hkMerUpdate, (void**)&s.g_merUpdate};
    if (!core::installFeature("scourge10", {update})) return false;
    s.nodeClass = mernode::add(detail::nodeSpec());
    if (s.nodeClass < 0) core::logf("scourge10: ScourgeWave not added (native class table full)");
    return true;
}

}  // namespace di1::scourge
