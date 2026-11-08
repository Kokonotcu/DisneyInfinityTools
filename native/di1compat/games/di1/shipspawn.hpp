// shipspawn10 (test, off): a 1.0 player ship deck spawns its rigid body and rides it. slangvm, engine/handles.

// Port on script threads, followBody on the game thread.
#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/handles.hpp"
#include "../../engine/slangvm.hpp"
#include "config.hpp"
#include "rowboatmount.hpp"

namespace di1::shipspawn {

using engine::slangvm::Cell;
using engine::slangvm::Native;
using engine::slangvm::Vm;

namespace detail {

namespace eng {
// H4 open: the 3.0 natives the port calls run without a prologue check (X-12), as sxnatives' ports
constexpr uint32_t kActorGetPosition = 0x0074B790;      // handle -> x, y, z
constexpr uint32_t kActorGetRotationY = 0x0074C580;     // handle -> float
constexpr uint32_t kCreateFromInitString = 0x0075A3F0;  // init, x, y, z, rotation, name -> handle (rowboatriding)
constexpr uint32_t kSlaveActorToActor = 0x006F69C0;     // slave, master, offset, 3 matrix rows, int, bool
// FUN_006f69c0 -> FUN_018a8d40: the slave component goes on arg 1 (FUN_0069ecb0), so the deck is the slave
constexpr uint32_t kSlaveSetPlacedPassthrough = 0x006F6D10;   // slave, bool (-> FUN_0186a4a0)
constexpr uint32_t kSlaveGetMaster = 0x006F6D50;              // slave -> master handle, 0 when none
constexpr size_t kSelfAt = 0x34;                        // VM ctx +0x34: the script's actor (sxmisc)
}  // namespace eng

constexpr uint32_t kDeckUid = 2110;   // ActorType AV_PirateShip (gamedb/pirates bitmasks.lua)
// av_pirateship.dnax ShipRigidBodyConnector.RigidBodyName, as CreateFromInitString's init string and name
constexpr char kBodyInit[] = "DNAFile=characters/AV_PirateShipRB.dnax;";
constexpr char kBodyName[] = "AV_PirateShipRB";
constexpr int kSlaveMode = 1;   // mgshipriding.btl.sx (3.0's own ship slave) passes 1, true
constexpr int kSlaveFlag = 1;
constexpr int kVec = 3;

// The deck follows its body by the setters ActorSetPosition (0x1876690) and ActorSetRotationY (0x74c6f0) use:

// thiscall (actor*; value*; ret 4); position at +0x2c, quaternion at +0x1c
constexpr uint32_t kSetPosition = 0x00742F90;
constexpr uint32_t kSetOrientation = 0x007465F0;
constexpr size_t kActorPosition = 0x2c;
constexpr size_t kActorOrientation = 0x1c;
constexpr size_t kTransformBytes = 0x1c;   // quaternion and position, read together
constexpr int kTraced = 3;                 // followBody calls logged with the deck read back
constexpr float kJumpSq = 25.f;            // a body move over 5 m between calls is a teleport worth a line
constexpr int kJumps = 6;
typedef void(__fastcall* SetFn)(uint8_t** actor, void* edx, const float* value);

struct State {
    std::mutex m;                                       // guards decks and deckOf
    std::unordered_set<int> decks;                      // decks that spawned their body
    std::unordered_map<uint32_t, uint32_t> deckOf;      // body -> deck; ponytail: never pruned, a few per session
    bool follow = false;                                // setters' prologues matched; set once in install()
    std::atomic<int> traced{0};                         // followBody lines logged
    float lastX = 0, lastZ = 0;                         // the last logged body x / z; game thread only
    int jumps = 0;                                      // jump lines logged; game thread only
};
inline State g_state;

inline void call(uint32_t va, uint8_t* ctx) { ((engine::slangvm::NativeFn)core::rt(va))(ctx); }

inline void pushFloat(const Vm& v, float f)
{
    Cell c;
    c.type = engine::slangvm::kFloat;
    c.f = f;
    v.push(c);
}

inline void pushVec(const Vm& v, float x, float y, float z)
{
    pushFloat(v, x);
    pushFloat(v, y);
    pushFloat(v, z);
}

// The body at the deck's place and heading, through 3.0's CreateFromInitString; its handle, 0 when none.
inline int spawnBody(uint8_t* ctx, int deck)
{
    Vm v{ctx};
    v.pushInt(deck);
    call(eng::kActorGetPosition, ctx);
    float p[kVec];
    for (int i = kVec - 1; i >= 0; --i) p[i] = v.pop().f;
    v.pushInt(deck);
    call(eng::kActorGetRotationY, ctx);
    const float rot = v.pop().f;
    v.pushPointer(kBodyInit);
    pushVec(v, p[0], p[1], p[2]);
    pushFloat(v, rot);
    v.pushPointer(kBodyName);
    call(eng::kCreateFromInitString, ctx);
    const int body = v.pop().i;
    core::logf("shipspawn10: deck %08X spawned rigid body %08X at (%.1f, %.1f, %.1f)", deck, body, p[0], p[1], p[2]);
    return body;
}

// The deck slaved to the body at no offset and no turn, as 1.0's connector kept it on the hull.
inline void slaveDeck(uint8_t* ctx, int deck, int body)
{
    Vm v{ctx};
    v.pushInt(deck);
    v.pushInt(body);
    pushVec(v, 0.f, 0.f, 0.f);
    pushVec(v, 1.f, 0.f, 0.f);
    pushVec(v, 0.f, 1.f, 0.f);
    pushVec(v, 0.f, 0.f, 1.f);
    v.pushInt(kSlaveMode);
    v.pushInt(kSlaveFlag);
    call(eng::kSlaveActorToActor, ctx);
    // a placed (teleported) master moves its slave only with passthrough on, as rowboatriding sets it on its rider
    v.pushInt(deck);
    v.pushInt(1);
    call(eng::kSlaveSetPlacedPassthrough, ctx);
    v.pushInt(deck);
    call(eng::kSlaveGetMaster, ctx);
    const int master = v.pop().i;
    core::logf("shipspawn10: deck %08X slaved (mode %d), its master now %08X%s", deck, kSlaveMode, master,
               master == body ? "" : " - not the body: the slave call did not take");
}

// ShipBattle_RegisterShip (1.0 0xa094d0, ship), from ship.btl.sx's Setup: a player deck spawns its body once.

// 3.0 has no ShipBattleQuarterback, so the registration itself stays a no-op
inline void __cdecl registerShip(uint8_t* ctx)
{
    Vm v{ctx};
    const Cell ship = v.pop();
    const int deck = ship.type == engine::slangvm::kInt ? ship.i : *(int*)(ctx + eng::kSelfAt);
    if (!rowboatmount::hasActorType((uint32_t)deck, kDeckUid)) return;
    {
        std::lock_guard<std::mutex> lock(g_state.m);
        if (!g_state.decks.insert(deck).second) return;
    }
    const int body = spawnBody(ctx, deck);
    if (!body) return;
    slaveDeck(ctx, deck, body);
    std::lock_guard<std::mutex> lock(g_state.m);
    g_state.deckOf[(uint32_t)body] = (uint32_t)deck;
}

}  // namespace detail

// From the boat update hook (oceanboat10) after each body's step: its deck takes the body's transform.
inline void followBody(uint32_t body)
{
    detail::State& s = detail::g_state;
    if (!s.follow || !body) return;
    uint32_t deck = 0;
    {
        std::unique_lock<std::mutex> lock(s.m, std::try_to_lock);   // per frame: never wait (L4)
        if (!lock.owns_lock()) return;
        auto it = s.deckOf.find(body);
        if (it == s.deckOf.end()) return;
        deck = it->second;
    }
    uint8_t* from = engine::handles::actorOf(body);
    uint8_t* to = engine::handles::actorOf(deck);
    const bool fromOk = from && core::readable(from + detail::kActorOrientation, detail::kTransformBytes);
    const bool toOk = to && core::readable(to + detail::kActorOrientation, detail::kTransformBytes);
    if (!fromOk || !toOk) return;
    const float* p = (const float*)(from + detail::kActorPosition);
    const float* d = (const float*)(to + detail::kActorPosition);
    const float before[] = {d[0], d[1], d[2]};
    ((detail::SetFn)core::rt(detail::kSetPosition))(&to, nullptr, p);
    ((detail::SetFn)core::rt(detail::kSetOrientation))(&to, nullptr, (const float*)(from + detail::kActorOrientation));
    // the first calls, then each jump of the body (a teleport), up to kJumps more lines
    const float dx = p[0] - s.lastX, dz = p[2] - s.lastZ;
    const bool jumped = dx * dx + dz * dz > detail::kJumpSq && s.jumps < detail::kJumps;
    if (s.traced.fetch_add(1) >= detail::kTraced && !jumped) return;
    s.jumps += jumped;
    s.lastX = p[0];
    s.lastZ = p[2];
    core::logf("shipspawn10: deck %08X follows body %08X: body (%.1f, %.1f, %.1f), deck (%.1f, %.1f, %.1f) -> "
               "(%.1f, %.1f, %.1f)", deck, body, p[0], p[1], p[2], before[0], before[1], before[2], d[0], d[1], d[2]);
}

// Checks the setters followBody calls (H4); the port itself is added by sxnatives with shipSpawn10.
inline bool install(const Config& c)
{
    if (!c.shipSpawn10) return true;
    const char* setter = "8B09668B4106BAFF0300006623C2663B";   // FUN_00742f90 and FUN_007465f0 open alike
    const bool pos = core::prologueMatches(detail::kSetPosition, setter);
    const bool rot = core::prologueMatches(detail::kSetOrientation, setter);
    const bool resolve = core::prologueMatches(engine::handles::eng::kResolve, "8B4424088B0085C07503C20800");
    detail::g_state.follow = pos && rot && resolve;
    if (!detail::g_state.follow) core::logf("shipspawn10: deck follow off, a setter's prologue differs");
    return detail::g_state.follow;
}

// Rows replace g_natives' stubs by name; sxnatives adds them with sxPorts10 and shipSpawn10.
inline Native g_ports[] = {
    {"ShipBattle_RegisterShip", 1, 0, &detail::registerShip},
};

}  // namespace di1::shipspawn
