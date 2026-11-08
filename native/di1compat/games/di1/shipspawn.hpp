// shipspawn10 (test, off): a 1.0 player ship deck spawns its rigid body and rides it.
// slangvm, rowboatmount. Script threads.
#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/slangvm.hpp"
#include "rowboatmount.hpp"
#include "shipdeck.hpp"

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

struct State {
    std::mutex m;                    // guards decks
    std::unordered_set<int> decks;   // decks that spawned their body; ponytail: never pruned, a few per session
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
    // a deck the Tardis hull spawned (shipdeck) already has its body
    if (!rowboatmount::hasActorType((uint32_t)deck, kDeckUid) || shipdeck::isOurs((uint32_t)deck)) return;
    {
        std::lock_guard<std::mutex> lock(g_state.m);
        if (!g_state.decks.insert(deck).second) return;
    }
    const int body = spawnBody(ctx, deck);
    if (body) slaveDeck(ctx, deck, body);
}

}  // namespace detail

// Rows replace g_natives' stubs by name; sxnatives adds them with sxPorts10 and shipSpawn10.
inline Native g_ports[] = {
    {"ShipBattle_RegisterShip", 1, 0, &detail::registerShip},
};

}  // namespace di1::shipspawn
