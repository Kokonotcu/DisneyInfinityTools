// 1.0 non-combat natives: attack move, ledge OBB, carry, spawner delay, absent systems. slangvm, sxkeys. Script thread.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <unordered_map>
#include "../../core/hooks.hpp"
#include "../../engine/slangvm.hpp"
#include "sxkeys.hpp"

namespace di1::sxmisc {

using engine::slangvm::Cell;
using engine::slangvm::Native;
using engine::slangvm::Vm;

namespace detail {

namespace eng {
// H4 open: the 3.0 natives the ports call run without a prologue check (X-12), as sxnatives' ports
constexpr uint32_t kBuildMoveHash = 0x00EF3080;   // Combat_BuildMoveHash: 11 cells -> hash (builder 0xef2e70)
constexpr uint32_t kSendMove = 0x00EB6CC0;        // Combat_SendAndTriggerMove: actor, hash (sender 0xeb6c00)
constexpr uint32_t kHashAdd = 0x01349D20;         // HashAdd: table, key, value -> ok (sxnatives' eng)
constexpr uint32_t kHashRemove = 0x01349D70;      // HashRemove: table, key -> ok (sxnatives' eng)
constexpr uint32_t kLedgeObb = 0x00A831D0;        // BipedCOM_UseLedgeOBBCollision: 11 cells
constexpr uint32_t kUpdateCarry = 0x00A98E20;     // Game_UpdateCarryStyle: actor -> 2 results
constexpr size_t kSelfAt = 0x34;                  // VM ctx +0x34: the script's actor (opcode 0xf, both VMs)
}  // namespace eng

constexpr int kMoveCells = 9;        // AttackMoveAllParms: actor, vec3, type, move, turn, event, chase
constexpr int kLedgeTail = 5;        // UseLedgeOBBCollision: bool and f1..f4 above the two inserted names
constexpr int kCarryResults = 2;     // Game_UpdateCarryStyle's pushes; 1.0's SetCarryStyle pushed none
constexpr int kVec = 3;
constexpr int kChaseOn = 1;          // 1.0 0xb20f90 writes Chase = 1 when the flag is set

// 1.0 SpawnerQuarterback entry fields the natives read back; the rest only feed the spawn (SpawnFromActorNode).
struct SpawnEntry {
    float delay = 0.0f;       // +0x50
    bool delaySet = false;    // +0x60 bit 8: the first SetSpawnDelay wins
};

struct State {
    std::mutex mx;
    // (QB, spawner handle) -> entry; guarded by mx. ponytail: never pruned, QBs live per zone (a few dozen entries)
    std::unordered_map<uint64_t, SpawnEntry> spawn;
};
inline State g_state;

inline void call(uint32_t va, uint8_t* ctx) { ((engine::slangvm::NativeFn)core::rt(va))(ctx); }

// 1.0's pop int (0x70d6d0): floats truncate.
inline int cellInt(const Cell& c) { return c.type == engine::slangvm::kFloat ? (int)c.f : c.i; }

inline void pushFloat(const Vm& v, float f)
{
    Cell c;
    c.type = engine::slangvm::kFloat;
    c.f = f;
    v.push(c);
}

// Pops the args of a 1.0 body whose effect 3.0 cannot have (reason at each row).
template <int args>
void __cdecl drop(uint8_t* ctx)
{
    Vm{ctx}.top() -= args;
}

// 1.0's component-absent path: pops args, pushes floats zeros then ints zeros, as the 1.0 pushes type them.
template <int args, int floats, int ints>
void __cdecl zeros(uint8_t* ctx)
{
    Vm v{ctx};
    v.top() -= args;
    for (int i = 0; i < floats; ++i) pushFloat(v, 0.0f);
    for (int i = 0; i < ints; ++i) v.pushInt(0);
}

inline void hashKey(uint8_t* ctx, const Cell& hash, uint32_t va, int key)
{
    Vm v{ctx};
    v.push(hash);
    v.pushInt(key);
    call(va, ctx);
    v.top() -= 1;
}

// 1.0 0xb211a0 -> 0xb20f90: the script's own actor gets the move; 3.0 split it into build and send.
inline void __cdecl attackMoveAllParms(uint8_t* ctx)
{
    Vm v{ctx};
    const Cell chase = v.pop(), event = v.pop(), turn = v.pop(), move = v.pop(), type = v.pop();
    Cell vec[kVec];
    for (int i = kVec - 1; i >= 0; --i) vec[i] = v.pop();
    const Cell face = v.pop();
    const int self = *(int*)(ctx + eng::kSelfAt);
    v.pushInt(self);
    v.push(face);
    for (const Cell& c : vec) v.push(c);
    v.push(type);
    v.push(move);
    v.push(turn);
    pushFloat(v, 0.0f);   // 3.0's third speed (key 0x264): 1.0 wrote none, 0 keeps it out
    v.push(event);
    for (int i = 0; i < kVec; ++i) v.pushInt(0);
    call(eng::kBuildMoveHash, ctx);
    const Cell hash = v.pop();
    // 1.0's message had no nav mesh / acceleration keys: removed, so 3.0 reads them as absent
    for (const char* k : {"UseNavMesh", "UseAcceleration", "UseDeceleration"})
        hashKey(ctx, hash, eng::kHashRemove, sxkeys::keyId(k));
    if (cellInt(chase) != 0) {
        v.push(hash);
        v.pushInt(sxkeys::keyId("Chase"));
        v.pushInt(kChaseOn);
        call(eng::kHashAdd, ctx);
        v.top() -= 1;
    }
    v.pushInt(self);
    v.push(hash);
    call(eng::kSendMove, ctx);
}

// 1.0 0xa07d60 (actor, f7..f1, bool); 3.0 takes two optional names between f5/f4 and f4/f3, null skips them.
inline void __cdecl useLedgeObbCollision(uint8_t* ctx)
{
    Vm v{ctx};
    Cell tail[kLedgeTail];   // bool, f1, f2, f3, f4 from the top
    for (Cell& c : tail) c = v.pop();
    v.pushInt(0);
    v.push(tail[4]);
    v.pushInt(0);
    for (int i = 3; i >= 0; --i) v.push(tail[i]);
    call(eng::kLedgeObb, ctx);
}

// 1.0 0xa1b950 (actor, style name); 3.0 scripts call Game_UpdateCarryStyle where 1.0's called it (pickupupper).
inline void __cdecl setCarryStyle(uint8_t* ctx)
{
    Vm v{ctx};
    v.top() -= 1;
    call(eng::kUpdateCarry, ctx);
    v.top() -= kCarryResults;
}

inline uint64_t spawnKey(const Cell& qb, const Cell& spawner)
{
    return (uint64_t)(uint32_t)cellInt(qb) << 32 | (uint32_t)cellInt(spawner);
}

// 1.0 0xed8070 (QB, spawner, delay): the first writer of the entry's delay wins.
inline void __cdecl setSpawnDelay(uint8_t* ctx)
{
    Vm v{ctx};
    const Cell delay = v.pop(), spawner = v.pop(), qb = v.pop();
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    SpawnEntry& e = s.spawn[spawnKey(qb, spawner)];
    if (e.delaySet) return;
    e.delay = delay.type == engine::slangvm::kFloat ? delay.f : (float)delay.i;
    e.delaySet = true;
}

// 1.0 0xed8030 (QB, spawner) -> the entry's delay (+0x50).
inline void __cdecl getSpawnDelay(uint8_t* ctx)
{
    Vm v{ctx};
    const Cell spawner = v.pop(), qb = v.pop();
    float delay = 0.0f;
    {
        State& s = g_state;
        std::lock_guard<std::mutex> lock(s.mx);
        auto it = s.spawn.find(spawnKey(qb, spawner));
        if (it != s.spawn.end()) delay = it->second.delay;
    }
    pushFloat(v, delay);
}

}  // namespace detail

// Rows replace g_natives' stubs by name (later rows win). drop/zeros: 1.0's own result without the system.
inline Native g_miscPorts[] = {
    {"SendAndTriggerAttackMoveAllParms", 9, 0, &detail::attackMoveAllParms},
    {"BipedCOM_UseLedgeOBBCollision", 9, 0, &detail::useLedgeObbCollision},
    {"Game_SetCarryStyle", 2, 0, &detail::setCarryStyle},
    {"SpawnerQuarterback_SetSpawnDelay", 3, 0, &detail::setSpawnDelay},
    {"SpawnerQuarterback_GetSpawnDelay", 2, 1, &detail::getSpawnDelay},
    // Entries activate only through SpawnFromActorNode (1.0 0x10c9ec0), which 3.0 lacks: inactive-entry results
    {"SpawnerQuarterback_SetMaxNumberOfSpawnableActors", 3, 0, &detail::drop<3>},
    {"SpawnerQuarterback_SetMinNumberOfSpawnableActors", 3, 0, &detail::drop<3>},
    {"SpawnerQuarterback_SetMakeRoomForSpawned", 3, 0, &detail::drop<3>},
    {"SpawnerQuarterback_SetDeathDelay", 3, 0, &detail::drop<3>},
    {"SpawnerQuarterback_SkipSpawn", 3, 0, &detail::drop<3>},
    {"SpawnerQuarterback_SetAdditionalActorParms", 3, 0, &detail::drop<3>},
    {"SpawnerQuarterback_StopSpawning", 2, 0, &detail::drop<2>},
    {"SpawnerQuarterback_SpawnActorInstantly", 5, 0, &detail::drop<5>},
    {"SpawnerQuarterback_SpawnExpendableInstantly", 5, 0, &detail::drop<5>},
    {"SpawnerQuarterback_NeedsReconciling", 2, 1, &detail::zeros<2, 0, 1>},
    {"SpawnerQuarterback_GetLastActorSpawned", 2, 1, &detail::zeros<2, 0, 1>},
    // 3.0 has no WallJumpDetect component (no strings): 1.0's component-absent path (0xa04630, 0xa06e60, 0xa06e00)
    {"WallJump_GetWallJump_JumpToPoint", 1, 4, &detail::zeros<1, 3, 1>},
    {"WallDetect_GetLastWallPointAndNormal", 1, 6, &detail::zeros<1, 6, 0>},
    {"WallDetect_CanWallSlide", 2, 1, &detail::zeros<2, 0, 1>},
    {"WallJump_EnableStickRequirement", 2, 0, &detail::drop<2>},
    {"WallJump_SetDisableTimer", 2, 0, &detail::drop<2>},
    // 3.0 has no ActorTypeVars gadget: live values read 0.0 as on a 1.0 actor without it (0xee0370)
    {"ActorTypeVars_GetLiveVarValue", 2, 1, &detail::zeros<2, 1, 0>},
    {"ActorTypeVars_SetLiveVarValue", 3, 0, &detail::drop<3>},
    // 1.0 0xa00510 pops its two args and does nothing
    {"NetDeadReckoning_SetEnableApply", 2, 0, &detail::drop<2>},
    // 3.0 has no ShipManagementQuarterback (0xa093d0) nor Rowboat component (0xa0e8b0)
    {"ShipManagement_SetDefaultShipForNewPlayers", 1, 0, &detail::drop<1>},
    {"Rowboat_SetupBoarding", 2, 0, &detail::drop<2>},
    // 1.0 0xa22250 drove the Inc combat cameras; 3.0's CombatCamera gadget update (0x1065b60) does it itself
    {"IncActivateCombatCamera", 6, 0, &detail::drop<6>},
};

}  // namespace di1::sxmisc
