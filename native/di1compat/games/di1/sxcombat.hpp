// sxcombat10: 1.0 Combat_* natives 3.0 dropped, ported over 3.0's Combat, its routines and combat10. Script threads.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/slangvm.hpp"
#include "combat10.hpp"
#include "sxcombatfields.hpp"

namespace di1::sxcombat {

// Checks the 3.0 routines the ports call and installs combat10 (H2, H4); false: bind none of g_combatPorts.
inline bool install();

namespace detail {

// ---- 3.0 routines with 1.0's intent ----

// Combat_CanChooseAttack (1.0 0xaf94b0 -> 0xaf0630): 3.0's Combat_CanExecuteAttack (QB vtable +0x84, abort range).
inline void __cdecl canChooseAttack(uint8_t* ctx) { call(eng::kCanExecuteAttack, ctx); }

// Combat_KillCho (1.0 0xaf9630 -> 0xaf10b0): 3.0's Combat_KillFX keeps the same keyed list.
inline void __cdecl killCho(uint8_t* ctx) { call(eng::kKillFx, ctx); }

// Combat_StoreCho (1.0 0xb074b0: actor, cho, key): 3.0's Combat_StoreFX with kind 0, a choreography (0xfee150).
inline void __cdecl storeCho(uint8_t* ctx)
{
    Vm{ctx}.pushInt(0);
    call(eng::kStoreFx, ctx);
}

// Combat_QBForceUpdate (1.0 0xab1b90 -> 0xa935a0): 3.0's Combat_ForceQBUpdate updates, null-checked as 1.0.
inline void __cdecl qbForceUpdate(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    void* qb = c ? qbOf(c) : nullptr;
    if (!qb) return;
    for (uint32_t va : eng::kQbUpdates) ((QbUpdateFn)core::rt(va))(qb, nullptr);
}

inline bool canExecute(uint8_t* c, int attack)
{
    return attack != -1 && ((CanExecuteFn)core::rt(eng::kCanExecute))(c, nullptr, attack);
}

// FUN_00b0fa50's choice: the asked attack when 3.0 can run it, else 3.0's own pick; -1 for none.
inline int chooseAttack(uint8_t* c, int asked)
{
    if (canExecute(c, asked)) return asked;
    const int picked = ((ChooseFn)core::rt(eng::kChooseAttack))(c, nullptr, 0);
    return canExecute(c, picked) ? picked : -1;
}

// Combat_CanAttackNow (1.0 0xb12310 -> 0xb0fa50(force, attack, 0)): a pending attack, or wait for the team's turn.
inline void __cdecl canAttackNow(uint8_t* ctx)
{
    Vm v{ctx};
    const int asked = popInt(v);
    const bool force = popInt(v) != 0;
    uint8_t* c = combat10::combat(popInt(v));
    const int attack = c ? chooseAttack(c, asked) : -1;
    if (attack == -1) {
        v.pushInt(0);
        return;
    }
    std::lock_guard<std::mutex> lock(lockOf());
    Side& s = side(c);
    s.attackFailedInvalid = 0;
    const auto& times = combat10::detail::g_state.teamTimes[qbOf(c)];
    const auto t = times.find(at<int>(c, eng::kTarget));
    const float now = combat10::gameTime();
    if (!force && t != times.end() && now < t->second) {
        s.waiting = 1;
        s.waitingSince = now;
        v.pushInt(0);
        return;
    }
    s.pending = attack;
    v.pushInt(1);
}

// Combat_CanAttackNowCheck (1.0 0xb20e60 -> 0xb0fa50(0, attack, 1)): the same test, nothing made pending.
inline void __cdecl canAttackNowCheck(uint8_t* ctx)
{
    Vm v{ctx};
    const int asked = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    const int attack = c ? chooseAttack(c, asked) : -1;
    if (attack != -1) {
        std::lock_guard<std::mutex> lock(lockOf());
        side(c).checked = attack;
    }
    v.pushInt(attack != -1 ? 1 : 0);
}

// Combat_ShouldApproachToAttack (1.0 0xb073b0 -> 0xb017a0). ponytail: 1.0's cover-point branch skipped.
inline void __cdecl shouldApproachToAttack(uint8_t* ctx)
{
    Vm v{ctx};
    const int attack = popInt(v), actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    if (!c || !qbOf(c)) {
        v.pushInt(0);
        return;
    }
    const int postAttack = at<int>(c, eng::kApproachState) != 0;
    std::unique_lock<std::mutex> lock(lockOf());
    Side& s = side(c);
    const bool first = s.approachFirst != 0;
    s.approachFirst = 0;
    const float chance = prop(s, attack, postAttack ? kApproachPostAttack : kApproachPostMove);
    const bool rolled = combat10::dice() * kHundred <= chance;
    const bool forwardOnly = prop(s, attack, kApproachForwardOnly) > 0;
    const float maxApproach = prop(s, attack, kMaxApproachRange);
    lock.unlock();
    if (first || !rolled) {
        v.pushInt(0);
        return;
    }
    v.pushInt(forwardOnly && distanceToTarget(ctx, actor, myPos(c)) <= maxApproach ? 0 : 1);
}

// Combat_ChooseAttackGoalPos (1.0 0xb12290 -> 0xb0e930). ponytail: 1.0's straight-line pick only, no nav probes.
inline void __cdecl chooseAttackGoalPos(uint8_t* ctx)
{
    Vm v{ctx};
    popInt(v);
    const float range = popFloat(v);
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    Vec3 target{};
    const bool aimed = c && at<int>(c, eng::kGoalType) != -1 && combat10::actorPos(at<int>(c, eng::kTarget), &target);
    if (c) {
        std::lock_guard<std::mutex> lock(lockOf());
        side(c).hasChosenPoint = 0;
    }
    const Vec3 me = aimed ? myPos(c) : Vec3{};
    const Vec3 away = combat10::sub(me, target);
    const Vec3 goal = combat10::scaled(away, range * combat10::inverseLength(away));
    const Vec3 at3 = {target.x + goal.x, target.y + goal.y, target.z + goal.z};
    const Vec3 left = combat10::sub(me, at3);
    if (!aimed || combat10::dot(left, left) < kArrived) {
        v.pushInt(0);
        return;
    }
    v.pushInt(actor);
    pushVec3(v, at3);
    call(eng::kSetGoalPos, ctx);
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).moveMethod = 0;
    v.pushInt(1);
}

// ---- 1.0 logic on 1.0 attack tables ----

// The attacks of the actor in range now (FUN_00af0f90); kind picks the 1.0 caller's use of them.
enum class InRange { kCount, kInterruptChance, kFromPos };

template <InRange kKind>
void __cdecl attacksInRange(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    const bool aimed = c && qbOf(c) && at<int>(c, eng::kTarget) != 0;
    const float d = aimed ? distanceToTarget(ctx, actor, myPos(c)) : 0.0f;
    float r = 0;
    std::lock_guard<std::mutex> lock(lockOf());
    const Side* s = aimed ? &side(c) : nullptr;
    // FUN_00b01ed0 skips attack 0; ponytail: its line-of-sight probe from +0x308 is skipped
    const int first = kKind == InRange::kFromPos ? 1 : 0;
    for (int i = first; s && i < (int)s->attacks.size(); ++i) {
        if (!inRange(*s, i, d)) continue;
        if constexpr (kKind == InRange::kCount) r += 1;
        if constexpr (kKind == InRange::kInterruptChance) r = (std::max)(r, prop(*s, i, kInterruptToAttack));
        const bool seen = prop(*s, i, kMustSeeTarget) > 0 && prop(*s, i, kNavigationRequired) <= 0;
        if constexpr (kKind == InRange::kFromPos) r = seen ? 1.0f : r;
    }
    if constexpr (kKind == InRange::kInterruptChance) pushFloat(v, r);
    else v.pushInt((int)r);
}

// Combat_GetAttackIdleChance (1.0 0xaad1f0 -> 0xa92f60).
inline void __cdecl getAttackIdleChance(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    const int type = c ? at<int>(c, eng::kAttackType) : -1;
    float r = 0;
    if (c && type == -1) r = tuned(side(c).idleChance, c) * side(c).scale;
    else if (c && qbOf(c)) r = prop(side(c), type, kIdleProbability) * kPercent;
    pushFloat(v, r);
}

// Combat_GetAttackIdleMin/MaxTime (1.0 0xa92fc0 / 0xa93030): the tuned time, raised to the attack's own.
template <Variant Side::*M, int kProp>
void __cdecl getAttackIdleTime(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    if (!c) {
        pushFloat(v, 0.0f);
        return;
    }
    const int type = at<int>(c, eng::kAttackType);
    float r = tuned(side(c).*M, c) * side(c).scale;
    if (type != -1 && qbOf(c)) r = (std::max)(r, prop(side(c), type, kProp));
    pushFloat(v, r);
}

// Combat_GetMoveToAttackUseDeceleration (1.0 0xaabe40 -> 0xa92cf0).
inline void __cdecl getMoveToAttackUseDeceleration(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    const int s = c ? sizeClass(c) : 0;
    const size_t slot = s == combat10::detail::kSmall ? 1 : s == combat10::detail::kLarge ? 2 : 0;
    const int goal = c ? at<int>(c, eng::kGoalType) : -1;
    const bool decel = c && side(c).useDecel[slot];
    const bool noStop = goal != -1 && qbOf(c) && prop(side(c), goal, kPreAttackStop) <= 0;
    v.pushInt(decel && !noStop ? 1 : 0);
}

// Combat_AttackGoalAdvanceOnly (1.0 0xaade10 -> 0xa92060).
inline void __cdecl attackGoalAdvanceOnly(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    const int goal = c ? at<int>(c, eng::kGoalType) : -1;
    v.pushInt(goal != -1 && prop(side(c), goal, kApproachAdvanceOnly) != 0 ? 1 : 0);
}

// CombatQB_GetAttackFloatProp (1.0 0x9fef60: qb, actor, attack, property).
inline void __cdecl qbGetAttackFloatProp(uint8_t* ctx)
{
    Vm v{ctx};
    const int id = popInt(v), attack = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    const int qb = popInt(v);
    std::lock_guard<std::mutex> lock(lockOf());
    pushFloat(v, qb && c ? prop(side(c), attack, id) : 0.0f);
}

// ---- team turns (1.0 QB +0x114 map, Combat +0x13c record) ----

// Combat_SetTeamNextAttackTime (1.0 0xaf95c0 -> 0xaf01f0: actor, target, time).
inline void __cdecl setTeamNextAttackTime(uint8_t* ctx)
{
    Vm v{ctx};
    const float time = popFloat(v);
    const int target = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    void* qb = c ? qbOf(c) : nullptr;
    if (!qb) return;
    std::lock_guard<std::mutex> lock(lockOf());
    side(c).teamRecord[target] = time;
    combat10::detail::g_state.teamTimes[qb][target] = time;
}

inline float teamTime(void* qb, int target)
{
    const auto& times = combat10::detail::g_state.teamTimes[qb];
    const auto t = times.find(target);
    return t == times.end() ? 0.0f : t->second;
}

// Combat_GetTeamNextAttackTime (1.0 0xaae980 -> 0xa91e90).
inline void __cdecl getTeamNextAttackTime(uint8_t* ctx)
{
    Vm v{ctx};
    const int target = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    void* qb = c ? qbOf(c) : nullptr;
    std::lock_guard<std::mutex> lock(lockOf());
    pushFloat(v, qb ? teamTime(qb, target) : 0.0f);
}

// Combat_ReleaseSetTeamNextAttackTime (1.0 0xaae9f0 -> 0xa9fec0): only while the team time is still mine.
inline void __cdecl releaseSetTeamNextAttackTime(uint8_t* ctx)
{
    Vm v{ctx};
    const float time = popFloat(v);
    const int target = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    void* qb = c ? qbOf(c) : nullptr;
    if (!qb) return;
    std::lock_guard<std::mutex> lock(lockOf());
    const auto& rec = side(c).teamRecord;
    const auto mine = rec.find(target);
    const float own = mine == rec.end() ? 0.0f : mine->second;
    if (teamTime(qb, target) == own) combat10::detail::g_state.teamTimes[qb][target] = time;
}

// CombatQB_GetAnyWaitingToAttack (1.0 0x9fefd0 -> 0xc75eb0: qb, actor): a teammate waiting on my target.
inline void __cdecl qbGetAnyWaitingToAttack(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    void* qb = (void*)(uintptr_t)popInt(v);
    uint8_t* c = combat10::combat(actor);
    const int target = c ? at<int>(c, eng::kTarget) : 0;
    const int n = qb && target ? qbCount(qb) : 0;
    bool any = false;
    for (int i = 0; i < n && !any; ++i) {
        const int h = qbMember(qb, i);
        uint8_t* m = h != actor ? combat10::combat(h) : nullptr;
        if (!m || at<int>(m, eng::kTarget) != target) continue;
        std::lock_guard<std::mutex> lock(lockOf());
        any = side(m).waiting != 0;
    }
    v.pushInt(any ? 1 : 0);
}

// ---- ranking (CombatRanking keeps 1.0's +0x10 / +0x14 / +0x1c) ----

enum class Rank { kCount, kTimeout, kInAttackMode };

// Combat_GetNumRankedToAttack 0xaaedf0, IsRankingTimeoutActive 0xabf010, IsRankedToAttackAndInAttackMode 0xabefc0.
template <Rank kWhat>
void __cdecl ranking(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    uint8_t* r = c && rankingActive(c) ? combat10::component(actor, combat10::eng::kRankingType) : nullptr;
    const int count = r ? at<int>(r, eng::kRankCount) : 0;
    const bool timeout = r && at<float>(r, eng::kRankTimeout) != 0.0f;
    const bool attacking = r && !timeout && at<int>(r, eng::kRankIndex) < count;
    if constexpr (kWhat == Rank::kCount) v.pushInt(count);
    if constexpr (kWhat == Rank::kTimeout) v.pushInt(timeout ? 1 : 0);
    if constexpr (kWhat == Rank::kInAttackMode) v.pushInt(attacking ? 1 : 0);
}

// ---- teammates (1.0 QB members, positions, RelativeSize) ----

inline int relativeSize(uint8_t* c) { return (int)tuned(side(c).relativeSize, c); }

// Combat_IsLargerThan (1.0 0xaaff20 -> 0xaa13e0: actor, other).
inline void __cdecl isLargerThan(uint8_t* ctx)
{
    Vm v{ctx};
    uint8_t* other = combat10::combat(popInt(v));
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    v.pushInt(c && other && relativeSize(other) < relativeSize(c) ? 1 : 0);
}

// Combat_GetAnimTreeEventName (1.0 0xaf96d0 -> 0xaf13f0): the DNA's AnimTreeEvent_* text by slot, else "".
inline void __cdecl getAnimTreeEventName(uint8_t* ctx)
{
    Vm v{ctx};
    const int slot = popInt(v);
    uint8_t* c = combat10::combat(popInt(v));
    std::lock_guard<std::mutex> lock(lockOf());
    const bool parsed = c && slot >= 0 && slot < combat10::kAnimEvents10;
    v.pushPointer(parsed ? side(c).animEvents[slot] : c ? "" : nullptr);
}

enum class Mates { kLarger, kRanked };

// A teammate the member test picks whose path to its target I block; writes my inWay (FUN_00ace890 / 0xad1680).
template <Mates kWho>
int inWayOfMate(uint8_t* c, int actor)
{
    void* qb = qbOf(c);
    const int n = qb && at<int>(c, eng::kTarget) ? qbCount(qb) : 0;
    const Vec3 me = myPos(c);
    const float radius = pathRadius(c);
    for (int i = 0; i < n; ++i) {
        const int h = qbMember(qb, i);
        uint8_t* m = h != actor ? combat10::combat(h) : nullptr;
        Vec3 target{}, perp{};
        if (!m || !combat10::actorPos(at<int>(m, eng::kTarget), &target)) continue;
        std::unique_lock<std::mutex> lock(lockOf());
        const bool larger = relativeSize(c) < relativeSize(m);
        lock.unlock();
        const bool ranked = (at<uint8_t>(m, eng::kRankedFlags) & eng::kRankedBit) != 0;
        if (!(kWho == Mates::kLarger ? larger : ranked)) continue;
        if (!combat10::inWayOf(me, myPos(m), target, radius, &perp)) continue;
        std::lock_guard<std::mutex> relock(lockOf());
        side(c).inWay = perp;
        return h;
    }
    return 0;
}

// Combat_AmInWayOfLarger (1.0 0xae24c0).
inline void __cdecl amInWayOfLarger(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    v.pushInt(c ? inWayOfMate<Mates::kLarger>(c, actor) : 0);
}

// Combat_AmInWayOfRankedToAttack (1.0 0xae2460 -> 0xad1680): only while I am not attacking and few are ranked.
inline void __cdecl amInWayOfRankedToAttack(uint8_t* ctx)
{
    Vm v{ctx};
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    uint8_t* r = c ? combat10::component(actor, combat10::eng::kRankingType) : nullptr;
    const bool active = r && rankingActive(c);
    const int count = r ? at<int>(r, eng::kRankCount) : 0;
    const bool timeout = active && at<float>(r, eng::kRankTimeout) != 0.0f;
    const bool attacking = active && !timeout && at<int>(r, eng::kRankIndex) < count;
    const bool free = r && (!attacking || timeout) && count < kFewRanked;
    v.pushInt(free ? inWayOfMate<Mates::kRanked>(c, actor) : 0);
}

// Combat_IsClosestInGroupToTarget (1.0 0xae22d0 -> 0xace720): within 6 m and no teammate closer to pos.
inline void __cdecl isClosestInGroupToTarget(uint8_t* ctx)
{
    Vm v{ctx};
    const Vec3 pos = popVec3(v);
    const int actor = popInt(v);
    uint8_t* c = combat10::combat(actor);
    void* qb = c ? qbOf(c) : nullptr;
    if (!c || !qb) {
        v.pushInt(c ? 1 : 0);
        return;
    }
    const Vec3 mine = combat10::sub(myPos(c), pos);
    const float best = combat10::dot(mine, mine);
    bool closest = best <= kClosestRange;
    const int n = closest ? qbCount(qb) : 0;
    for (int i = 0; i < n && closest; ++i) {
        const int h = qbMember(qb, i);
        Vec3 p{};
        if (h == actor || !combat10::actorPos(h, &p)) continue;
        const Vec3 d = combat10::sub(pos, p);
        closest = !(combat10::dot(d, d) < best);
    }
    v.pushInt(closest ? 1 : 0);
}

}  // namespace detail

// 1.0 semantics over 3.0; (args, results) are g_natives' (SX-9). Rows live in static storage (S5).
inline Native g_combatPorts[] = {
    {"Combat_GetIdleInterruptible", 1, 1, &detail::getInt<&Side::idleInterruptible>},
    {"Combat_GetAttackInterruptible", 1, 1, &detail::getInt<&Side::attackInterruptible>},
    {"Combat_SetAttackInterruptible", 2, 0, &detail::setFlag<&Side::attackInterruptible>},
    {"Combat_GetCurrentAttackLeaf", 1, 1, &detail::getInt<&Side::attackLeaf>},
    {"Combat_SetCurrentAttackLeaf", 2, 0, &detail::setInt<&Side::attackLeaf>},
    {"Combat_HasAttackTargetStoredPos", 1, 1, &detail::getInt<&Side::hasStoredPos>},
    {"Combat_SetHasAttackTargetStoredPos", 2, 0, &detail::setFlag<&Side::hasStoredPos>},
    {"Combat_SetAttackTargetStoredPos", 4, 0, &detail::setVec3<&Side::storedPos>},
    {"Combat_SetLastAttackGoalPos", 4, 0, &detail::setVec3<&Side::lastGoalPos>},
    {"Combat_SetLastAttackGoalType", 2, 0, &detail::setInt<&Side::lastGoalType>},
    {"Combat_CancelWaitingToAttack", 1, 0, &detail::cancelWaitingToAttack},
    {"Combat_SetWaitToAttackDoneTime", 2, 0, &detail::setFloat<&Side::waitDone>},
    {"Combat_GetMoveToAttackMethod", 1, 1, &detail::getInt<&Side::moveMethod>},
    {"Combat_SetMoveToAttackMethod", 2, 0, &detail::setInt<&Side::moveMethod>},
    {"Combat_GetAvoidanceTarget", 1, 1, &detail::getInt<&Side::avoidTarget>},
    {"Combat_SetHasAvoidanceGoalPos", 2, 0, &detail::setHasAvoidanceGoalPos},
    {"Combat_GetUpdateAttackGoalTime", 1, 1, &detail::getFloat<&Side::updateGoalTime>},
    {"Combat_SetUpdateAttackGoalTime", 2, 0, &detail::setFloat<&Side::updateGoalTime>},
    {"Combat_GetHasChosenAttackPoint", 1, 1, &detail::getInt<&Side::hasChosenPoint>},
    {"Combat_GetAttackFailedInvalidState", 1, 1, &detail::getInt<&Side::attackFailedInvalid>},
    {"Combat_GetWaitToAttackLowerProb", 1, 1, &detail::getFloat<&Side::lowerProb>},
    {"Combat_GetWaitToAttackUpperProb", 1, 1, &detail::getFloat<&Side::upperProb>},
    {"Combat_IncrementWaitToAttackProb", 1, 0, &detail::stepWaitProb<true>},
    {"Combat_DecrementWaitToAttackProb", 1, 0, &detail::stepWaitProb<false>},
    {"Combat_AttackCounterIncrement", 1, 0, &detail::attackCounterIncrement},
    {"Combat_GetPursuePending", 1, 1, &detail::getInt<&Side::pursuePending>},
    {"Combat_SetPursuePending", 2, 0, &detail::setFlag<&Side::pursuePending>},
    {"Combat_GetInWayPerpendicular", 1, 3, &detail::getInWayPerpendicular},
    {"Combat_GetPendingAttackType", 1, 1, &detail::getInt<&Side::pending, -1>},
    {"Combat_WillReactToCollisions", 1, 1, &detail::getInt<&Side::willReact, 1>},
    {"Combat_GetTurnTakingInterruptMoving", 1, 1, &detail::getInt<&Side::turnInterruptMoving>},
    {"Combat_CanBypassStop", 1, 1, &detail::getInt<&Side::canBypassStop>},
    {"Combat_CanDoAvoidance", 1, 1, &detail::getInt<&Side::canDoAvoidance>},
    {"Combat_GetCelebrateTauntChance", 1, 1, &detail::getTuned<&Side::celebrate, false>},
    {"Combat_GetMinMoveInterval", 1, 1, &detail::getTuned<&Side::minMove, false>},
    {"Combat_GetDeferIfOthersWaitingToAttackChance", 1, 1, &detail::getTuned<&Side::defer, false>},
    {"Combat_GetWaitToAttackMinRange", 1, 1, &detail::getTuned<&Side::waitMinRange, false>},
    {"Combat_GetWaitToAttackMaxRange", 1, 1, &detail::getTuned<&Side::waitMaxRange, false>},
    {"Combat_GetWaitToAttackChance", 1, 1, &detail::getTuned<&Side::waitChance, true>},
    {"Combat_GetWaitToAttackMinTime", 1, 1, &detail::getTuned<&Side::waitMinTime, true>},
    {"Combat_GetWaitToAttackMaxTime", 1, 1, &detail::getTuned<&Side::waitMaxTime, true>},
    {"Combat_GetTauntChance", 1, 1, &detail::getTuned<&Side::taunt, true>},
    {"Combat_GetFrustratedTauntChance", 1, 1, &detail::getTuned<&Side::frustrated, true>},
    {"Combat_GetMinAttackModTimerInterval", 1, 1, &detail::getTimer<&Side::minModTimer, 0>},
    {"Combat_GetMaxAttackModTimerInterval", 1, 1, &detail::getTimer<&Side::maxModTimer, 1>},
    {"Combat_GetUseDeceleration", 1, 1, &detail::getUseDeceleration},
    {"Combat_CanPursueDistance", 1, 1, &detail::canPursue<&Side::canPursueDistance>},
    {"Combat_CanPursueNavMesh", 1, 1, &detail::canPursue<&Side::canPursueNavMesh>},
    {"Combat_GetPursueDist", 1, 1, &detail::getPursueTuned<&Side::pursueDist>},
    {"Combat_GetBreakOffDist", 1, 1, &detail::getPursueTuned<&Side::breakOff>},
    {"Combat_GetShouldPursue", 1, 1, &detail::getShouldPursue},
    {"Combat_GetMoveToAttackStartTime", 1, 1, &detail::get30<float, detail::eng::kMoveStart>},
    {"Combat_GetLoseAwarenessTime", 1, 1, &detail::get30<float, detail::eng::kLoseAwareness>},
    {"Combat_SetAttackRange", 2, 0, &detail::setAttackRange},
    {"Combat_SetToExploded", 2, 0, &detail::setToExploded},
    {"Combat_GetAttackRangeTolerance", 1, 1, &detail::getAttackRangeTolerance},
    {"Combat_CheckAndMigrateToAttackTargetMaster", 1, 1, &detail::checkAndMigrate},
    {"Combat_CanChooseAttack", 2, 1, &detail::canChooseAttack},
    {"Combat_KillCho", 2, 0, &detail::killCho},
    {"Combat_StoreCho", 3, 0, &detail::storeCho},
    {"Combat_QBForceUpdate", 1, 0, &detail::qbForceUpdate},
    {"Combat_CanAttackNow", 3, 1, &detail::canAttackNow},
    {"Combat_CanAttackNowCheck", 2, 1, &detail::canAttackNowCheck},
    {"Combat_ShouldApproachToAttack", 2, 1, &detail::shouldApproachToAttack},
    {"Combat_ChooseAttackGoalPos", 3, 1, &detail::chooseAttackGoalPos},
    {"Combat_NumAttacksInRange", 1, 1, &detail::attacksInRange<detail::InRange::kCount>},
    {"Combat_GetInterruptToAttackInRangeChance", 1, 1, &detail::attacksInRange<detail::InRange::kInterruptChance>},
    {"Combat_CanAttackFromPos", 1, 1, &detail::attacksInRange<detail::InRange::kFromPos>},
    {"Combat_GetAttackIdleChance", 1, 1, &detail::getAttackIdleChance},
    {"Combat_GetAttackIdleMinTime", 1, 1, &detail::getAttackIdleTime<&Side::idleMin, detail::kIdleTimeMin>},
    {"Combat_GetAttackIdleMaxTime", 1, 1, &detail::getAttackIdleTime<&Side::idleMax, detail::kIdleTimeMax>},
    {"Combat_GetMoveToAttackUseDeceleration", 1, 1, &detail::getMoveToAttackUseDeceleration},
    {"Combat_AttackGoalAdvanceOnly", 1, 1, &detail::attackGoalAdvanceOnly},
    {"CombatQB_GetAttackFloatProp", 4, 1, &detail::qbGetAttackFloatProp},
    {"Combat_SetTeamNextAttackTime", 3, 0, &detail::setTeamNextAttackTime},
    {"Combat_GetTeamNextAttackTime", 2, 1, &detail::getTeamNextAttackTime},
    {"Combat_ReleaseSetTeamNextAttackTime", 3, 0, &detail::releaseSetTeamNextAttackTime},
    {"CombatQB_GetAnyWaitingToAttack", 2, 1, &detail::qbGetAnyWaitingToAttack},
    {"Combat_GetNumRankedToAttack", 1, 1, &detail::ranking<detail::Rank::kCount>},
    {"Combat_IsRankingTimeoutActive", 1, 1, &detail::ranking<detail::Rank::kTimeout>},
    {"Combat_IsRankedToAttackAndInAttackMode", 1, 1, &detail::ranking<detail::Rank::kInAttackMode>},
    {"Combat_IsLargerThan", 2, 1, &detail::isLargerThan},
    {"Combat_GetAnimTreeEventName", 2, 1, &detail::getAnimTreeEventName},
    {"Combat_AmInWayOfLarger", 1, 1, &detail::amInWayOfLarger},
    {"Combat_AmInWayOfRankedToAttack", 1, 1, &detail::amInWayOfRankedToAttack},
    {"Combat_IsClosestInGroupToTarget", 4, 1, &detail::isClosestInGroupToTarget},
};

inline bool install()
{
    for (const combat10::detail::Callee& c : detail::CALLEES) {
        if (core::prologueMatches(c.va, c.prologue)) continue;
        core::logf("feature sxcombat10 off: FUN_%08x's prologue differs", c.va);
        return false;
    }
    return combat10::install();
}

}  // namespace di1::sxcombat
