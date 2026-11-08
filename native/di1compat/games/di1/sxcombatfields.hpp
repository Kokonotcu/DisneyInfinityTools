// sxcombatfields: sxcombat's VM helpers and its field-shaped ports (1.0 state, tuning, 3.0 fields). combat10,
// slangvm. Script threads; combat10's lock.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/slangvm.hpp"
#include "combat10.hpp"

namespace di1::sxcombat {

using engine::slangvm::Cell;
using engine::slangvm::Native;
using engine::slangvm::Vm;
using combat10::Side;
using combat10::Variant;
using combat10::Vec3;

namespace detail {

namespace eng {
constexpr uint32_t kCanExecute = 0x01067190;       // FUN_01067190, thiscall (Combat; attack) -> bool
constexpr uint32_t kChooseAttack = 0x010672B0;     // FUN_010672b0, thiscall (Combat; 0) -> attack or -1
constexpr uint32_t kQbUpdates[] = {0x010702F0, 0x010702B0, 0x01070330, 0x01070370, 0x010703B0};   // 0xeb82e0's
constexpr uint32_t kCanExecuteAttack = 0x00EB9F60; // native Combat_CanExecuteAttack (actor, attack) -> bool
constexpr uint32_t kKillFx = 0x00EB35E0;           // native Combat_KillFX (actor, key)
constexpr uint32_t kStoreFx = 0x00EB3530;          // native Combat_StoreFX (actor, fx, key, kind)
constexpr uint32_t kSetGoalPos = 0x00EAF4D0;       // native Combat_SetAttackGoalPos (actor, vec3)
constexpr uint32_t kDistanceTo = 0x00EB46A0;       // native Combat_DistanceToAttackTarget (actor, vec3) -> float
// 3.0 Combat offsets; each from the 3.0 twin of the 1.0 native reading the field (twinoff.py)
constexpr size_t kTarget = 0xb8;            // Combat_GetAttackTargetAgentID (1.0 +0x238)
constexpr size_t kMyPos = 0x1b4;            // my position: FUN_01066b50's range test (1.0 +0x2f0, FUN_00af0ed0)
constexpr size_t kAttackRange = 0x250;      // Combat_GetAttackRange (1.0 +0x338)
constexpr size_t kGoalType = 0x254;         // Combat_GetAttackGoalType (1.0 +0x33c)
constexpr size_t kLoseAwareness = 0x2f0;    // Combat_SetLoseAwarenessTime (1.0 +0x3e4)
constexpr size_t kAttackType = 0x51c;       // Combat_GetAttackType (1.0 +0x5d0)
constexpr size_t kPathRadius = 0x5ac;       // Combat_GetPathRadius variant (1.0 +0x738)
constexpr size_t kQb = 0x5f0;               // Combat_GetCombatQuarterback (1.0 +0x76c)
constexpr size_t kMoveStart = 0x5fc;        // Combat_SetMoveToAttackStartTime (1.0 +0x7ac)
constexpr size_t kRankingActive = 0x644;    // Combat_RankingActive bool variant (1.0 FUN_00aa0d70)
constexpr size_t kSize = 0x678;             // size class FUN_00ff6390 picks by
constexpr size_t kApproachState = 0x7f0;    // Combat_SetApproachProbabilityState (1.0 +0x9a0)
constexpr size_t kRankedFlags = 0x907;      // Combat_IsRankedToAttack: bit 0 (1.0 FUN_00aba190)
constexpr size_t kExplodedFlags = 0x909;    // Combat_IsExploded: bit 5 (1.0 +0x7d3)
constexpr uint8_t kRankedBit = 1, kExplodedBit = 0x20;
constexpr size_t kPursueDelay = 0x80;       // CombatPursue: Combat_StartCanPursueDelay (1.0 Combat +0x3b0)
constexpr size_t kShouldPursue = 0x58;      // CombatPursue: FUN_00fefa20 (1.0 CombatPursue +0x10)
constexpr size_t kShouldPursueAt = 0xc;     // its time stamp (1.0 +0x14)
constexpr size_t kRankCount = 0x10;         // CombatRanking: ranked count, +0x14 timeout (ClearRankingLocked)
constexpr size_t kRankTimeout = 0x14;
constexpr size_t kRankIndex = 0x1c;         // my rank (FUN_00aa2b20: rank < count)
constexpr size_t kQbCount = 0x38 / 4;       // AIQuarterback vtable: count (AIQuarterback_GetActorCount)
constexpr size_t kQbMember = 0x30 / 4;      // and member i (AIQuarterback_GetActorByIndex)
}  // namespace eng

constexpr int32_t kHashCell = 3;            // a 3.0 object cell: pops read it as an int (0x731fd0)
constexpr int kFewRanked = 2;               // FUN_00ad1680: only while fewer than 2 are ranked
// 1.0 attack property ids (FUN_00c700f0)
enum Prop : int {
    kMinRange = 0, kMaxRange = 1, kMaxApproachRange = 3, kInRangeProbability = 4, kApproachPostMove = 6,
    kApproachPostAttack = 7, kApproachForwardOnly = 8, kNavigationRequired = 20, kMustSeeTarget = 23,
    kInterruptToAttack = 27, kPreAttackStop = 33, kApproachAdvanceOnly = 46, kIdleProbability = 48,
    kIdleTimeMin = 49, kIdleTimeMax = 50,
};
constexpr float kPercent = 0.01f, kHundred = 100.0f, kRangeSlack = 1.0f, kArrived = 1.0f;
constexpr float kClosestRange = 36.0f;   // FUN_00ace720: 6 m squared
constexpr float kAvoidRadius = 0.5f;     // FUN_00a92130
constexpr float kPursueWait = 1.0f;      // FUN_00a93b20: a second after SetShouldPursue
constexpr float kMinTimer = 1.0f, kMaxTimer = 2.0f;   // 1.0's results without a Combat component

typedef bool(__fastcall* CanExecuteFn)(uint8_t* comp, void* edx, int attack);
typedef int(__fastcall* ChooseFn)(uint8_t* comp, void* edx, int zero);
typedef void(__fastcall* QbUpdateFn)(void* qb, void* edx);
typedef int(__fastcall* QbCountFn)(void* qb, void* edx);
typedef int(__fastcall* QbMemberFn)(void* qb, void* edx, int i);

inline const combat10::detail::Callee CALLEES[] = {
    {eng::kCanExecute, "56578B7C240C6A01"}, {eng::kChooseAttack, "81ECC8010000568BF1"},
    {eng::kQbUpdates[0], "F30F10811C010000"}, {eng::kQbUpdates[1], "F30F108118010000"},
    {eng::kQbUpdates[2], "F30F108120010000"}, {eng::kQbUpdates[3], "F30F108124010000"},
    {eng::kQbUpdates[4], "F30F108128010000"}, {eng::kCanExecuteAttack, "51568B74240C578BCE"},
    {eng::kKillFx, "51568B74240C578BCE"}, {eng::kStoreFx, "515355568B742414"},
    {eng::kSetGoalPos, "83EC10568B742418"}, {eng::kDistanceTo, "83EC14568B74241C"},
};

inline std::mutex& lockOf() { return combat10::detail::g_state.mx; }
// The 1.0 state of a Combat component (caller holds lockOf()).
inline Side& side(const uint8_t* comp) { return combat10::detail::g_state.sides[comp]; }

template <typename T>
T& at(uint8_t* p, size_t off)
{
    return *(T*)(p + off);
}

inline void call(uint32_t va, uint8_t* ctx) { ((engine::slangvm::NativeFn)core::rt(va))(ctx); }

inline int popInt(const Vm& v)
{
    const Cell c = v.pop();
    if (c.type == engine::slangvm::kFloat) return (int)c.f;
    return c.type == engine::slangvm::kInt || c.type == kHashCell ? c.i : 0;
}

inline float popFloat(const Vm& v)
{
    const Cell c = v.pop();
    if (c.type == engine::slangvm::kFloat) return c.f;
    return c.type == engine::slangvm::kInt || c.type == kHashCell ? (float)c.i : 0.0f;
}

// 1.0 0x70d780: z on top.
inline Vec3 popVec3(const Vm& v)
{
    Vec3 p;
    p.z = popFloat(v);
    p.y = popFloat(v);
    p.x = popFloat(v);
    return p;
}

inline void pushFloat(const Vm& v, float f)
{
    Cell c;
    c.type = engine::slangvm::kFloat;
    c.f = f;
    v.push(c);
}

inline void pushVec3(const Vm& v, const Vec3& p)
{
    pushFloat(v, p.x);
    pushFloat(v, p.y);
    pushFloat(v, p.z);
}

inline int sizeClass(uint8_t* comp) { return at<int>(comp, eng::kSize); }
inline float tuned(const Variant& v, uint8_t* comp) { return combat10::pick(v, sizeClass(comp)); }
inline float pathRadius(uint8_t* comp) { return combat10::pick(at<Variant>(comp, eng::kPathRadius), sizeClass(comp)); }
inline Vec3 myPos(uint8_t* comp) { return at<Vec3>(comp, eng::kMyPos); }
inline void* qbOf(uint8_t* comp) { return at<void*>(comp, eng::kQb); }

inline bool rankingActive(uint8_t* comp)
{
    const int s = sizeClass(comp);
    const size_t slot = s == combat10::detail::kSmall ? 1 : s == combat10::detail::kLarge ? 2 : 0;
    return at<uint8_t>(comp, eng::kRankingActive + slot) != 0;
}

// FUN_00c75e00 on the actor's own 1.0 attack table; 0 out of range, as 1.0.
inline float prop(const Side& s, int attack, int id)
{
    const bool known = attack >= 0 && attack < (int)s.attacks.size() && id >= 0 && id < combat10::kProps10;
    return known ? s.attacks[attack].prop[id] : 0.0f;
}

// AIQuarterback members through its vtable (virtual: no prologue to check).
inline int qbCount(void* qb) { return ((QbCountFn)(*(void***)qb)[eng::kQbCount])(qb, nullptr); }
inline int qbMember(void* qb, int i) { return ((QbMemberFn)(*(void***)qb)[eng::kQbMember])(qb, nullptr, i); }

// 3.0's Combat_DistanceToAttackTarget (1.0 FUN_00af0490) through the VM: same ctx, stack balanced.
inline float distanceToTarget(uint8_t* ctx, int actor, const Vec3& from)
{
    Vm v{ctx};
    v.pushInt(actor);
    pushVec3(v, from);
    call(eng::kDistanceTo, ctx);
    return popFloat(v);
}

// FUN_00af0ed0's test for attack i: probable in range and within its MinRange..MaxRange (1 m slack).
inline bool inRange(const Side& s, int i, float distance)
{
    const bool probable = prop(s, i, kInRangeProbability) > 0;
    return probable && prop(s, i, kMinRange) - kRangeSlack < distance && distance < prop(s, i, kMaxRange) + kRangeSlack;
}

// ---- 1.0 fields 3.0 dropped (combat10's Side, 1.0 ctor defaults) ----

template <auto M, int kAbsent = 0>
void __cdecl getInt(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    v.pushInt(c ? (int)(side(c).*M) : kAbsent);
}

template <auto M>
void __cdecl setInt(uint8_t* ctx)
{
    Vm v{ctx};
    const int value = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).*M = (std::remove_reference_t<decltype(side(c).*M)>)value;
}

// 1.0 pop_b 0x70d700 keeps "not zero".
template <uint8_t Side::*M>
void __cdecl setFlag(uint8_t* ctx)
{
    Vm v{ctx};
    const bool value = popInt(v) != 0;
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).*M = value;
}

template <float Side::*M>
void __cdecl getFloat(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    pushFloat(v, c ? side(c).*M : 0.0f);
}

template <float Side::*M>
void __cdecl setFloat(uint8_t* ctx)
{
    Vm v{ctx};
    const float value = popFloat(v);
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).*M = value;
}

template <Vec3 Side::*M>
void __cdecl setVec3(uint8_t* ctx)
{
    Vm v{ctx};
    const Vec3 value = popVec3(v);
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).*M = value;
}

// Combat_GetInWayPerpendicular (1.0 0xaaf250): zeros without a component.
inline void __cdecl getInWayPerpendicular(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    pushVec3(v, c ? side(c).inWay : Vec3{});
}

// The tuned getters (1.0 0xa92210 picks); some 1.0 bodies scale by +0x818.
template <Variant Side::*M, bool kScaled>
void __cdecl getTuned(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    const Side* s = c ? &side(c) : nullptr;
    pushFloat(v, s ? tuned(s->*M, c) * (kScaled ? s->scale : 1.0f) : 0.0f);
}

// Combat_GetMin/MaxAttackModTimerInterval (1.0 0xab0a30 / 0xab0aa0): plain floats, 1 and 2 without a component.
template <Variant Side::*M, int kWhich>
void __cdecl getTimer(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    pushFloat(v, c ? (side(c).*M).v[0] : kWhich == 0 ? kMinTimer : kMaxTimer);
}

// Combat_GetUseDeceleration (1.0 0xaabe90): a tuned flag.
inline void __cdecl getUseDeceleration(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    const int s = c ? sizeClass(c) : 0;
    const size_t slot = s == combat10::detail::kSmall ? 1 : s == combat10::detail::kLarge ? 2 : 0;
    v.pushInt(c ? side(c).useDecel[slot] : 0);
}

// Combat_CanPursueDistance / NavMesh (1.0 0xaac190 / 0xaac200): no pursue delay running and the DNA flag.
template <uint8_t Side::*M>
void __cdecl canPursue(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    uint8_t* pursue = combat10::component(actor, combat10::eng::kPursueType);
    const bool idle = !pursue || at<float>(pursue, eng::kPursueDelay) == 0.0f;
    std::lock_guard<std::mutex> lock(lockOf());
    v.pushInt(c && idle ? side(c).*M : 0);
}

// Combat_GetPursueDist / GetBreakOffDist (1.0 0xaacb60 / 0xaacbc0): CombatPursue's tuned distances.
template <Variant Side::*M>
void __cdecl getPursueTuned(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    const bool pursue = combat10::component(actor, combat10::eng::kPursueType) != nullptr;
    std::lock_guard<std::mutex> lock(lockOf());
    pushFloat(v, c && pursue ? tuned(side(c).*M, c) : 0.0f);
}

// Combat_GetShouldPursue (1.0 0xaac340 -> 0xa93b20): set, and a second has passed. 3.0 CombatPursue fields.
inline void __cdecl getShouldPursue(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* p = combat10::component(popInt(v), combat10::eng::kPursueType);
    const bool set = p && at<uint8_t>(p, eng::kShouldPursue) != 0;
    v.pushInt(set && at<float>(p, eng::kShouldPursueAt) + kPursueWait < combat10::gameTime() ? 1 : 0);
}

// Combat_CancelWaitingToAttack (1.0 0xaad6b0).
inline void __cdecl cancelWaitingToAttack(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).waiting = 0;
    side(c).waitingSince = 0;
}

// Combat_AttackCounterIncrement (1.0 0xab09b0).
inline void __cdecl attackCounterIncrement(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    ++side(c).attackCounter;
}

// Combat_SetHasAvoidanceGoalPos (1.0 0xaab420 -> 0xa92130).
inline void __cdecl setHasAvoidanceGoalPos(uint8_t* ctx)
{
    Vm v{ctx};
    const bool value = popInt(v) != 0;
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    Side& s = side(c);
    const bool clearing = !value && s.hasAvoidGoal;
    if (!clearing) {
        s.hasAvoidGoal = value;
        return;
    }
    s.hasAvoidGoal = 0;
    if (s.avoidTarget) s.avoidRadius = kAvoidRadius;
}

// Combat_Increment/DecrementWaitToAttackProb (1.0 0xaad280 / 0xaad250).
template <bool kIncrement>
void __cdecl stepWaitProb(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    std::lock_guard<std::mutex> lock(lockOf());
    Side& s = side(c);
    const float chance = tuned(s.waitChance, c), delta = tuned(s.waitDelta, c);
    if constexpr (kIncrement) combat10::incrementWaitProb(s, chance, delta);
    else combat10::decrementWaitProb(s, chance, delta);
}

// ---- 3.0 fields ----

template <typename T, size_t kOff>
void __cdecl get30(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    pushFloat(v, c ? (float)at<T>(c, kOff) : 0.0f);
}

// Combat_SetAttackRange (1.0 0xaab520).
inline void __cdecl setAttackRange(uint8_t* ctx)
{
    Vm v{ctx};
    const float range = popFloat(v);
    uint8_t* c = combat10::combat(popInt(v));
    if (c) at<float>(c, eng::kAttackRange) = range;
}

// Combat_SetToExploded (1.0 0xaae700): 3.0 keeps it as bit 5 of +0x909.
inline void __cdecl setToExploded(uint8_t* ctx)
{
    Vm v{ctx};
    const bool value = popInt(v) != 0;
    uint8_t* c = combat10::combat(popInt(v));
    if (!c) return;
    uint8_t& f = at<uint8_t>(c, eng::kExplodedFlags);
    f = value ? (uint8_t)(f | eng::kExplodedBit) : (uint8_t)(f & ~eng::kExplodedBit);
}

// Combat_GetAttackRangeTolerance (1.0 0xaae740): 1 with a component.
inline void __cdecl getAttackRangeTolerance(uint8_t* ctx)
{
    Vm v{ctx};
    pushFloat(v, combat10::combat(popInt(v)) ? 1.0f : 0.0f);
}

// Combat_CheckAndMigrateToAttackTargetMaster (1.0 0xaaf2e0 -> 0x493940, which returns 0).
inline void __cdecl checkAndMigrate(uint8_t* ctx)
{
    Vm v{ctx};
    popInt(v);
    v.pushInt(0);
}

}  // namespace detail

}  // namespace di1::sxcombat
