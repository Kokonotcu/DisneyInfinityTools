// sxnatives10: 1.0 scripts' imports bound to natives with 1.0 semantics (SX-3/4/5). slangvm, sxkeys. Script threads.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include "../../core/hooks.hpp"
#include "../../engine/slangvm.hpp"
#include "config.hpp"
#include "sxkeys.hpp"

namespace di1::sxnatives {

// The VM types the natives below use (declarations: N26 forbids only using namespace)
using engine::slangvm::Cell;
using engine::slangvm::Native;
using engine::slangvm::Vm;

// Binds 1.0 scripts' imports to the natives below when c has sx10 and sxNatives10 (H1, H2).
inline bool install(const Config& c);
// A 1.0 TriggerEvent type as 3.0's: 3.0 inserted Mounted and Dismounted at 10 and 11 (evtypes.py).
inline int eventTo30(int t10);
// ReactionInfo_FlipMotionName's text (1.0 0xed8270): BACK and FRONT swapped, case-sensitive, left to right.
inline void flipMotionName(const char* s, char* buf, size_t cap);

// AI behavior types in 1.0's order (table 0x15f5c04, bound 0x24); 3.0 moved every one (behtwins.py).
inline const char* const BEH10[36] = {
    "ACTION", "ATTACK", "BALLISTICTOPOINT", "CHARGE", "CINEMATIC", "CUSTOM", "CUSTOMIZE", "DEATH", "FOLLOWSPLINE",
    "LOOKATTARGET", "MOVETOPOINT", "MOVETURN", "MUNITIONIMPACT", "SLEEP", "SPAWN", "SPECIALCASE", "TELEPORT", "ALERT",
    "BLOCK", "CAPTURE", "INVESTIGATE", "NAVBEACON", "PLAYERCONTROL", "RETURNTONAVMESH", "STANDGUARD", "TAUNT",
    "APPROACH", "CIRCLE", "FLEE", "FOLLOW", "MORALSUPPORT", "PATROL", "PROTECT", "ROAM", "SNEAK", "WANDER",
};

namespace detail {

namespace eng {
// H4 open: the 3.0 natives the ports call run without a prologue check (X-12)
constexpr uint32_t kHashFind = 0x013460D0;          // HashFind/Int/Str: table, key, default -> found, value
constexpr uint32_t kHashAdd = 0x01349D20;           // HashAdd/Int/Str: table, key, value -> ok
constexpr uint32_t kHashRemove = 0x01349D70;        // HashRemove: table, key -> ok
constexpr uint32_t kHashFindInRange = 0x01345F60;   // table, min id, max id, key, default -> found, value
constexpr uint32_t kPauseAtEnd = 0x01383520;        // PauseAtEndWhenDoneChoreography: cho, flag (through 0xacc230)
constexpr uint32_t kLocatorFacing = 0x00759470;     // ActorGetNamedLocatorPositionAndFacing -> position, facing
constexpr uint32_t kMatrixFromVector = 0x00733BC0;  // Matrix3x3BuildFromVector: vector, a, b, c (0x417e90)
constexpr uint32_t kReactionScript = 0x0137C4A0;    // ReactionInfo_GetReactionScript: name, fourcc -> script
constexpr uint32_t kAttackStart = 0x00EB2020;       // Combat_AttackStart: actor, attack -> result
constexpr uint32_t kSliderFloat = 0x00A77540;       // _GetSliderFloat: f, f, p, p, f -> float
constexpr uint32_t kSlaveToVehicle = 0x00AA2B30;    // SlaveActorToVehicle: name, name, actor, bool, actor -> vehicle
constexpr uint32_t kTriggerEvent = 0x01860E50;      // TriggerEvent: type, actor, value; types 1..20
constexpr uint32_t kBehaviorString = 0x01375C50;    // AiBehaviorGetStringProperty: type, name -> value
constexpr uint32_t kBehaviorFloat = 0x01375CA0;     // AiBehaviorGetFloatProperty: type, name -> value
constexpr uint32_t kCurrentBehavior = 0x01375D20;   // AiBehaviorGetCurrentBehaviorType: actor -> type, -1 without
constexpr uint32_t kBehaviorName = 0x01523C30;      // FUN_01523c30, cdecl (type) -> its name in table 0x210455c
}  // namespace eng

constexpr size_t kKeyCap = 128;         // a key name's buffer
constexpr size_t kSuffix = sizeof("Min");
constexpr int kNoKey = 0x7fffffff;      // an id no key has: 3.0's range lookup then misses
constexpr int kBehaviors30 = 30;        // 3.0's table 0x210455c (bound 0x1d in 0x13b7430)
constexpr int kPauseFlag = 1;           // true: 3.0 sets the pause flags 1.0 set
constexpr int kFacingCells = 3;         // the facing 3.0 pushes after the position
constexpr int kVariantCells = 1;        // the variant, on top, which 3.0 dropped
constexpr int kExtraResult = 1;         // the result 3.0 pushes and 1.0 did not
constexpr int kAxes10[] = {2, 1, 1};    // Matrix3x3BuildFromVector's axes, fixed in 1.0 (0x70f2f0 -> 0x415fe0)
constexpr int kEventTypeSlot = 2;       // TriggerEvent's type, below actor and value
constexpr int kFirstMovedEvent = 10;    // 1.0 types 10..14 are 3.0's 12..16 (enumdrift.py: 7 of 7 twin pairs)
constexpr int kLastMovedEvent = 14;
constexpr int kInsertedEvents = 2;
constexpr size_t kFlipCap = 512;
constexpr size_t kFlipInput = 256;

struct State {
    char flipped[kFlipCap];   // S5: scripts keep the pointer. One buffer as 1.0's 0x1757378: calls race (SX-12)
};
inline State g_state;

inline void call(uint32_t va, uint8_t* ctx) { ((engine::slangvm::NativeFn)core::rt(va))(ctx); }

// A 1.0 key cell, a string literal 3.0's pop 0x731fd0 reads as 0, as the 3.0 id; int keys pass.
inline void keyCell(uint8_t* ctx, Cell& c)
{
    char s[kKeyCap];
    if (c.type != engine::slangvm::kPointer || !engine::slangvm::cellText(ctx, c.s, s, sizeof(s))) return;
    c.type = engine::slangvm::kInt;
    c.i = sxkeys::keyId(s);
}

// Hash* ports (SX-10): the key keyAt cells below the top as its id, then the 3.0 native.
template <int keyAt, uint32_t va>
void __cdecl keyedPort(uint8_t* ctx)
{
    keyCell(ctx, Vm{ctx}.at(keyAt));
    call(va, ctx);
}

// HashFindInRange: 1.0 0xf05490 (table, name, default) looks up name+"Min" / name+"Max"; 3.0 takes their ids.
inline void __cdecl hashFindInRange(uint8_t* ctx)
{
    Vm v{ctx};
    Cell def = v.pop(), name = v.pop(), table = v.pop();
    char s[kKeyCap];
    int idMin = kNoKey, idMax = kNoKey, id = name.i;
    if (name.type == engine::slangvm::kPointer && engine::slangvm::cellText(ctx, name.s, s, sizeof(s) - kSuffix)) {
        id = sxkeys::keyId(s);
        size_t l = strlen(s);
        memcpy(s + l, "Min", kSuffix);
        idMin = sxkeys::keyId(s);
        memcpy(s + l, "Max", kSuffix);
        idMax = sxkeys::keyId(s);
    }
    v.push(table);
    v.pushInt(idMin);
    v.pushInt(idMax);
    v.pushInt(id);
    v.push(def);
    call(eng::kHashFindInRange, ctx);
}

// 1.0 0xedeb70 (cho) set the pause flags; 3.0 takes the flag as an argument.
inline void __cdecl pauseAtEndWhenDoneCho(uint8_t* ctx)
{
    Vm{ctx}.pushInt(kPauseFlag);
    call(eng::kPauseAtEnd, ctx);
}

// 1.0 0x734090 (actor, name) -> position; 3.0 kept only the version that also pushes the facing.
inline void __cdecl actorGetNamedLocatorPosition(uint8_t* ctx)
{
    call(eng::kLocatorFacing, ctx);
    Vm{ctx}.top() -= kFacingCells;
}

// 1.0 0x70f2f0 fixed the axes; 3.0 takes them from the script.
inline void __cdecl matrix3x3BuildFromVector(uint8_t* ctx)
{
    Vm v{ctx};
    for (int axis : kAxes10) v.pushInt(axis);
    call(eng::kMatrixFromVector, ctx);
}

// 1.0 0xedf6b0 (name, fourcc, variant); 3.0 dropped the variant.
inline void __cdecl reactionInfoGetReactionScript(uint8_t* ctx)
{
    Vm{ctx}.top() -= kVariantCells;
    call(eng::kReactionScript, ctx);
}

// 1.0 0xae20e0 pushes nothing.
inline void __cdecl combatAttackStart(uint8_t* ctx)
{
    call(eng::kAttackStart, ctx);
    Vm{ctx}.top() -= kExtraResult;
}

// 1.0 0xa00420 pops and pushes the same; 3.0 passes one more constant 0 to its slider lookup.
inline void __cdecl getSliderFloat(uint8_t* ctx) { call(eng::kSliderFloat, ctx); }

// 1.0 copies into its one static buffer and pushes it; 3.0 has no such native.
inline void __cdecl reactionInfoFlipMotionName(uint8_t* ctx)
{
    char* buf = g_state.flipped;
    Vm v{ctx};
    Cell c = v.pop();
    char s[kFlipInput];
    if (c.type != engine::slangvm::kPointer || !engine::slangvm::cellText(ctx, c.s, s, sizeof(s))) s[0] = 0;
    flipMotionName(s, buf, kFlipCap);
    v.pushPointer(buf);
}

// 1.0 0xa1de30 pushes nothing; 3.0 pushes the vehicle (0x731d10).
inline void __cdecl slaveActorToVehicle(uint8_t* ctx)
{
    call(eng::kSlaveToVehicle, ctx);
    Vm{ctx}.top() -= kExtraResult;
}

// TriggerEvent (1.0 0x113c1b0, types 1..14): the type in 3.0's numbers.
inline void __cdecl triggerEvent(uint8_t* ctx)
{
    Cell& t = Vm{ctx}.at(kEventTypeSlot);
    if (t.type == engine::slangvm::kInt) t.i = eventTo30(t.i);
    else if (t.type == engine::slangvm::kFloat) t.f = (float)eventTo30((int)t.f);   // the pop truncates floats
    call(eng::kTriggerEvent, ctx);
}

inline const char* beh30Name(int t30)
{
    return ((const char*(__cdecl*)(unsigned))core::rt(eng::kBehaviorName))(t30);
}

// A 1.0 behavior type as 3.0's, by name; -1 for a name 3.0 lacks.
inline int behTo30(int t10)
{
    if (t10 < 0 || t10 >= (int)std::size(BEH10)) return -1;
    for (int i = 0; i < kBehaviors30; ++i)
        if (!_stricmp(beh30Name(i), BEH10[t10])) return i;
    return -1;
}

// A 3.0 behavior type as 1.0's, by name; -1 for a name 1.0 lacks.
inline int behTo10(int t30)
{
    if (t30 < 0 || t30 >= kBehaviors30) return -1;
    const char* n = beh30Name(t30);
    for (int i = 0; i < (int)std::size(BEH10); ++i)
        if (!_stricmp(n, BEH10[i])) return i;
    return -1;
}

// AiBehaviorGet{String,Float}Property (1.0 0xed7250 / 0xed72b0): type, name; the name is a key, as HashFind's.
template <uint32_t va>
void __cdecl behaviorProperty(uint8_t* ctx)
{
    Vm v{ctx};
    keyCell(ctx, v.at(0));
    Cell& type = v.at(1);
    if (type.type == engine::slangvm::kInt) type.i = behTo30(type.i);
    call(va, ctx);
}

// AiBehaviorGetBehaviorStringFromType (1.0 0xed7310): the 1.0 table's name, "UNDEFINED" past it (0xfd16c0).
inline void __cdecl aiBehaviorGetBehaviorStringFromType(uint8_t* ctx)
{
    Vm v{ctx};
    Cell c = v.pop();
    int t = c.type == engine::slangvm::kFloat ? (int)c.f : c.i;
    v.pushPointer((unsigned)t < std::size(BEH10) ? BEH10[t] : "UNDEFINED");
}

// AiBehaviorGetCurrentBehaviorType (1.0 0xed7330): 3.0's type in 1.0's numbers.
inline void __cdecl aiBehaviorGetCurrentBehaviorType(uint8_t* ctx)
{
    call(eng::kCurrentBehavior, ctx);
    Cell& c = Vm{ctx}.at(0);
    if (c.type == engine::slangvm::kInt && c.i >= 0) c.i = behTo10(c.i);
}

// A served script's SetInitialParams gets the behavior type in 1.0's numbers (FUN_013fa680 pushes it last).
inline void behaviorTypeTo10(uint8_t* ctx, const char* name)
{
    const uint8_t* hdr = engine::slangvm::script(ctx);
    const bool initParams = name && !strcmp(name, "SetInitialParams");
    if (!initParams || !hdr || !engine::slangvm::isServed(hdr)) return;
    Cell& c = Vm{ctx}.at(0);
    if (c.type == engine::slangvm::kInt) c.i = behTo10(c.i);
}

}  // namespace detail

inline void flipMotionName(const char* s, char* buf, size_t cap)
{
    constexpr size_t kBack = 4, kFront = 5;
    size_t n = strlen(s), o = 0;
    // i == n copies the terminator, as 1.0's `cmp edx,ebx; jle`; FRONT and a byte must still fit
    for (size_t i = 0; i <= n && o + kFront + 1 < cap;) {
        if (i + kBack <= n && !memcmp(s + i, "BACK", kBack)) {
            memcpy(buf + o, "FRONT", kFront);
            o += kFront;
            i += kBack;
        } else if (i + kFront <= n && !memcmp(s + i, "FRONT", kFront)) {
            memcpy(buf + o, "BACK", kBack);
            o += kBack;
            i += kFront;
        } else {
            buf[o++] = s[i++];
        }
    }
    buf[cap - 1] = 0;
}

inline int eventTo30(int t10)
{
    const bool moved = t10 >= detail::kFirstMovedEvent && t10 <= detail::kLastMovedEvent;
    return moved ? t10 + detail::kInsertedEvents : t10;
}

// 1.0 natives 3.0 lacks or registers with another signature: 1.0 (args, results) for the stub (sig.py -> gentab.py).
inline Native g_natives[] = {
    {"ActorGetNamedLocatorPosition",2,3}, {"ActorIsExploder",1,2}, {"ActorTypeVars_GetLiveVarValue",2,1},
    {"ActorTypeVars_GetVarIndexFromName",1,1}, {"ActorTypeVars_SetLiveVarValue",3,0}, {"AipDetermineTarget",0,1},
    {"AipGetTargetPosition",0,3}, {"AipIsEnemy",1,1}, {"AipSetAlert",1,0}, {"AvatarInteractionHudData_AddUIText",7,0},
    {"AvatarInteractionHudData_RemoveUIText",3,0}, {"AvatarSetInvisibilityAlpha",2,0}, {"BHCheckPlayset",1,1},
    {"BHGetAccessoryTypeIndex",2,1}, {"BHGetAccessoryTypeVolatile",2,1}, {"BHNotifyTrafficQBOfHulk",1,0},
    {"BHSetAccessory",2,0}, {"BHSetAccessoryTypeIndex",3,0}, {"BHSetActorType",2,0}, {"BH_Activate_Super",1,0},
    {"BH_ClearMyTimer",1,0}, {"BH_Deactivate_Super",1,0}, {"BH_GetClosestPlayerVehicle",1,1}, {"BH_GetEnemyHits",1,1},
    {"BH_GetEnemyMaxDistance",1,1}, {"BH_GetEnemySpeed",1,1}, {"BH_GetIdleAnimEventName",1,1},
    {"BH_GetMonsterLegs",1,1}, {"BH_GetMoveToPoint_Waypoint",2,3}, {"BH_GetMyTimer",1,1}, {"BH_GetNoCarryFlag",1,1},
    {"BH_GetSpawnPoint",1,3}, {"BH_GetSplineEndPos",1,3}, {"BH_GetSplineStartPos",1,3}, {"BH_GetSuperStartFlag",1,1},
    {"BH_MarkForDeathForced",2,0}, {"BH_PauseMyTimer",1,0}, {"BH_RegisterSpecialPerform",9,0},
    {"BH_SetActorContributionCulling",5,0}, {"BH_SetAlpha",2,0}, {"BH_SetIdleAnimEventName",2,0},
    {"BH_SetSplineEndPos",4,0}, {"BH_SetSplineStartPos",4,0}, {"BH_SetSuperStartFlag",2,0},
    {"BH_SetWalkAnimEventName",2,0}, {"BH_SpecialPerformEndAnimation",1,1}, {"BH_SpecialPerformLoopAnimation",1,1},
    {"BH_SpecialPerformOrient",1,3}, {"BH_SpecialPerformPosition",1,3}, {"BH_SpecialPerformStartAnimation",1,1},
    {"BH_StartMyTimer",1,0}, {"BH_UpdateStatBaseballComplete",0,0}, {"BH_UpdateStatBrokenFixed",0,0},
    {"BH_UpdateStatSinkyFixed",0,0}, {"BipedCOM_UseLedgeOBBCollision",9,0}, {"BlockheadAreThereEnemies",0,1},
    {"BlockheadCheckIfSplineOnExcludeList",1,1}, {"BlockheadCheckMaster",2,1}, {"BlockheadCheckSmallAlienLeader",0,1},
    {"BlockheadClearSmallAlienLeader",0,0}, {"BlockheadConstrainToNavMesh",7,3},
    {"BlockheadDistanceToNearestSplineSquared",2,1}, {"BlockheadFTTeamCheck",1,1},
    {"BlockheadGetBuildingEnterSpline",1,1}, {"BlockheadGetBuildingExitSpline",1,1},
    {"BlockheadGetLocatorOrientation",1,3}, {"BlockheadGetLocatorPos",1,3}, {"BlockheadGetLocatorPosition",2,3},
    {"BlockheadGetPaintPos",1,3}, {"BlockheadQB_GetSuperDirection",2,1}, {"BlockheadRegisterSmallAlienLeader",1,1},
    {"BlockheadRemoveSplineFromExcludeList",1,0}, {"BlockheadSetDistToCheckForRandomBuildingSpline",2,0},
    {"BlockheadSuper_Start",1,0}, {"BlockheadSuper_Stop",1,0}, {"BlockheadZombieCheck",1,1}, {"Cannon_Fire",2,1},
    {"Cannon_FireStop",1,1}, {"Cannon_Flip",2,1}, {"Cannon_GetControlPosition",1,3}, {"Cannon_GetFireInfo",1,7},
    {"Cannon_NetFire",8,0}, {"Cannon_SetUser",2,0}, {"Cannon_SwapAmmo",2,1}, {"CheckLineForObstaclesRedux",12,4},
    {"ChoLayoutDriveChoreographyByLocator",4,0}, {"ClosestPointOnMesh",4,4},
    {"CombatPerceptionData_OverridePerception_NotMovingFOV",3,0}, {"CombatQB_GetAnyWaitingToAttack",2,1},
    {"CombatQB_GetAttackFloatProp",4,1}, {"CombatQB_SetAttackFloatProp",5,0}, {"Combat_AddCarriable",1,0},
    {"Combat_AmInWayOfLarger",1,1}, {"Combat_AmInWayOfRankedToAttack",1,1}, {"Combat_AtAttackPoint",1,1},
    {"Combat_AttackCounterIncrement",1,0}, {"Combat_AttackGoalAdvanceOnly",1,1}, {"Combat_AttackStart",2,0},
    {"Combat_CalcAvoidanceGoalPos",1,0}, {"Combat_CanAttackFromPos",1,1}, {"Combat_CanAttackNow",3,1},
    {"Combat_CanAttackNowCheck",2,1}, {"Combat_CanBypassStop",1,1}, {"Combat_CanChooseAttack",2,1},
    {"Combat_CanDoAvoidance",1,1}, {"Combat_CanPursueDistance",1,1}, {"Combat_CanPursueNavMesh",1,1},
    {"Combat_CancelWaitingToAttack",1,0}, {"Combat_CheckAndMigrateToAttackTargetMaster",1,1},
    {"Combat_CheckAttackValidState",2,1}, {"Combat_ChooseAttackGoalPos",3,1}, {"Combat_DecrementWaitToAttackProb",1,0},
    {"Combat_GetAnimTreeEventName",2,1}, {"Combat_GetAttackAimPositionDegrees",11,3},
    {"Combat_GetAttackFailedInvalidState",1,1}, {"Combat_GetAttackIdleChance",1,1}, {"Combat_GetAttackIdleMaxTime",1,1},
    {"Combat_GetAttackIdleMinTime",1,1}, {"Combat_GetAttackInterruptible",1,1}, {"Combat_GetAttackRangeTolerance",1,1},
    {"Combat_GetAvoidanceGoalPos",1,3}, {"Combat_GetAvoidanceTarget",1,1}, {"Combat_GetBreakOffDist",1,1},
    {"Combat_GetCelebrateTauntChance",1,1}, {"Combat_GetChaseTrainDirect",1,1}, {"Combat_GetChaseTrainDirectGoto",1,3},
    {"Combat_GetCho",2,1}, {"Combat_GetCombatAccelerationFactor",1,1}, {"Combat_GetCurrentAttackLeaf",1,1},
    {"Combat_GetDecelerationFriction",1,1}, {"Combat_GetDeferIfOthersWaitingToAttackChance",1,1},
    {"Combat_GetEvadeChance",1,1}, {"Combat_GetFrustratedTauntChance",1,1}, {"Combat_GetHasChosenAttackPoint",1,1},
    {"Combat_GetIdleInterruptible",1,1}, {"Combat_GetInWayPerpendicular",1,3},
    {"Combat_GetInterruptToAttackInRangeChance",1,1}, {"Combat_GetLoseAwarenessTime",1,1},
    {"Combat_GetMaxAttackModTimerInterval",1,1}, {"Combat_GetMinAttackModTimerInterval",1,1},
    {"Combat_GetMinMoveInterval",1,1}, {"Combat_GetMoveToAttackMethod",1,1}, {"Combat_GetMoveToAttackStartTime",1,1},
    {"Combat_GetMoveToAttackUseDeceleration",1,1}, {"Combat_GetMoveToPoint_Waypoint",2,3},
    {"Combat_GetNextEvadeDelay",1,1}, {"Combat_GetNextEvadeDelayMunitionImpact",1,1}, {"Combat_GetNoRecoilOnFire",1,1},
    {"Combat_GetNumRankedToAttack",1,1}, {"Combat_GetPendingAttackType",1,1}, {"Combat_GetPursueDist",1,1},
    {"Combat_GetPursuePending",1,1}, {"Combat_GetReactionInterruptible",1,1}, {"Combat_GetResourceTarget",1,1},
    {"Combat_GetShotAccuracyInnerRadius",1,1}, {"Combat_GetShotAccuracyOuterRadius",1,1},
    {"Combat_GetShotsInBurst",1,1}, {"Combat_GetShotsInSequence",1,1}, {"Combat_GetShouldPursue",1,1},
    {"Combat_GetTauntChance",1,1}, {"Combat_GetTeamNextAttackTime",2,1}, {"Combat_GetTrainChaseTarget",1,1},
    {"Combat_GetTrainChaseTargetOffset",1,1}, {"Combat_GetTrainLane",1,1}, {"Combat_GetTrainSpline",1,1},
    {"Combat_GetTurnTakingInterruptMoving",1,1}, {"Combat_GetUpdateAttackGoalTime",1,1},
    {"Combat_GetUseDeceleration",1,1}, {"Combat_GetWaitToAttackChance",1,1}, {"Combat_GetWaitToAttackLowerProb",1,1},
    {"Combat_GetWaitToAttackMaxRange",1,1}, {"Combat_GetWaitToAttackMaxTime",1,1},
    {"Combat_GetWaitToAttackMinRange",1,1}, {"Combat_GetWaitToAttackMinTime",1,1},
    {"Combat_GetWaitToAttackUpperProb",1,1}, {"Combat_HasAttackTargetStoredPos",1,1},
    {"Combat_HasAvoidanceGoalPos",1,1}, {"Combat_InToybox",1,1}, {"Combat_IncrementWaitToAttackProb",1,0},
    {"Combat_IsClosestInGroupToTarget",4,1}, {"Combat_IsLargerThan",2,1}, {"Combat_IsNonParticipant",2,1},
    {"Combat_IsRankedToAttackAndInAttackMode",1,1}, {"Combat_IsRankingTimeoutActive",1,1}, {"Combat_KillCho",2,0},
    {"Combat_NumAttacksInRange",1,1}, {"Combat_QBForceUpdate",1,0}, {"Combat_RecoverAttackProbabilities",1,0},
    {"Combat_ReleaseSetTeamNextAttackTime",3,0}, {"Combat_RemoveCarriable",1,0}, {"Combat_SetAttackInterruptible",2,0},
    {"Combat_SetAttackRange",2,0}, {"Combat_SetAttackTargetStoredPos",4,0}, {"Combat_SetChaseTrainDirect",2,0},
    {"Combat_SetChasingTrain",2,0}, {"Combat_SetCurrentAttackLeaf",2,0}, {"Combat_SetHasAttackTargetStoredPos",2,0},
    {"Combat_SetHasAvoidanceGoalPos",2,0}, {"Combat_SetIdleInterruptible",2,0}, {"Combat_SetLastAttackGoalPos",4,0},
    {"Combat_SetLastAttackGoalType",2,0}, {"Combat_SetMoveToAttackMethod",2,0}, {"Combat_SetPursuePending",2,0},
    {"Combat_SetReactionInterruptible",2,0}, {"Combat_SetResourceTarget",2,0}, {"Combat_SetTeamNextAttackTime",3,0},
    {"Combat_SetToExploded",2,0}, {"Combat_SetTrainChaseTargetPathPos",4,0}, {"Combat_SetTrainEngine",2,0},
    {"Combat_SetTrainSpline",2,0}, {"Combat_SetUpdateAttackGoalTime",2,0}, {"Combat_SetWaitToAttackDoneTime",2,0},
    {"Combat_ShouldApproachToAttack",2,1}, {"Combat_StartCanPursueDelay",1,0}, {"Combat_StoreCho",3,0},
    {"Combat_UseAcceleration",1,1}, {"Combat_WillReactToCollisions",1,1}, {"CommandStreamCustomEvent",5,0},
    {"Diffusinate",2,0}, {"DisableBipedComSweepTests",1,0}, {"DynamicProjectileCreate",6,1},
    {"EnableBipedComSweepTests",4,0}, {"EnableShipMount",2,0}, {"Expendables_DispenseWithCount",5,0},
    {"Game_ComputeCarryStyle",2,1}, {"Game_SetCarryStyle",2,0}, {"GetGravity",0,1}, {"GetOceanHeightAtPos",3,2},
    {"GetOceanSeaLevel",3,1}, {"GetShipTurretSide",2,1}, {"GetSliderFloat",5,1}, {"HashFindInRange",3,2},
    {"HorseplayEventsManager_AddEvent",4,0}, {"IncActivateCombatCamera",6,0}, {"InstanceChoFromShipFXPool",1,1},
    {"IsBlockheadPerforming",1,1}, {"Kraken_GetApproachCircleRadius",1,1}, {"Kraken_GetApproachMaxAngle",1,1},
    {"Kraken_GetApproachSpeed",1,1}, {"Kraken_GetChancesToAttackEnemyShip",1,1}, {"Kraken_GetCloseAreaRadius",1,1},
    {"Kraken_GetDistanceInFrontOfPlayerToAppear",1,1}, {"Kraken_GetFloatingHeight",1,1}, {"Kraken_GetGrabSpeed",1,1},
    {"Kraken_GetHealthLossInSwipe",1,1}, {"Kraken_GetHealthToAttackEnemyShip",1,1},
    {"Kraken_GetKillAreaDistInFront",1,1}, {"Kraken_GetKillAreaRadius",1,1}, {"Kraken_GetKrakenAttackPlayer",1,1},
    {"Kraken_GetKrakenBaneTime",1,1}, {"Kraken_GetKrakenHammerTarget",1,1},
    {"Kraken_GetMinTimeBetweenEnemyAttacks",1,1}, {"Kraken_GetPrepareSpeed",1,1}, {"Kraken_GetTimeApproaching",1,1},
    {"Kraken_GetTimeUntilNextAttack",1,1}, {"Kraken_ResetKrakenHammerTarget",1,0}, {"Matrix3x3BuildFromVector",3,9},
    {"MoveCameraBehindPlayer",2,0}, {"MovementForceDSDestinationAndSpeed",5,0},
    {"NavigateToAgentWithEligibilityFilter",6,1}, {"NavigateToPoint",8,1}, {"NavigateToPointWithEligibilityFilter",8,1},
    {"Navigation_GetBaseEligibilityFilter",1,1}, {"Navigation_GetCurrentNavEligibilityBits",1,1},
    {"Navigation_GetEligibleAITreeNodeName",2,1}, {"Navigation_GetLastEligibleWayoint",1,3},
    {"NetDeadReckoning_SetEnableApply",2,0}, {"OceanForceFieldData_SampleHeightValue",3,1},
    {"PauseAtEndWhenDoneChoreography",1,0}, {"PerformanceManager_ActorIsLoaded",1,1},
    {"PhysicsRigidBody_SetUserData",2,0}, {"PirActivateCombatCamera",5,0}, {"PlayDeckCannonBallImpactFx",7,0},
    {"PlayOceanWaveRippleEffect",8,0}, {"PlayerCameraShake",12,0}, {"ReactionInfo_FlipMotionName",1,1},
    {"ReactionInfo_GetFourCCFromReactionStart",2,1}, {"ReactionInfo_GetReactionScript",3,1},
    {"ReactionInfo_GetVarInt",5,1}, {"ReactionInfo_registerCommand",6,0}, {"ResetShipLocomotionMode",1,0},
    {"RowboatAIData_GetChargeTargetOffset",1,3}, {"RowboatAIData_GetPrepareChargeOffset",1,3},
    {"RowboatAIData_GetTimeBetweenShots",1,1}, {"RowboatDocking_FinalizeDocking",2,0}, {"RowboatDocking_SetStates",2,0},
    {"RowboatDocking_SetupDocking",2,0}, {"RowboatDocking_Undock",2,0}, {"Rowboat_PreDestroy",1,0},
    {"Rowboat_SetupBoarding",2,0}, {"Rowboat_SetupDisembarking",4,0}, {"RumpusSubSys_GetRumpusObjectFlags",2,4},
    {"RumpusSubSys_Place_EnterPickMode",2,0}, {"SendAndTriggerAttackMoveAllParms",9,0},
    {"SendAndTriggerAttackMoveClip",8,0}, {"SendAndTriggerBeginFace",2,0}, {"SendAndTriggerPatrolMove",5,0},
    {"SendSubActorEvent",4,0}, {"SetBaseGravity",1,0}, {"SetBipedComSweepTestIgnoreActor",2,0},
    {"SetRealmsForShipInOceanLayer",2,0}, {"SfxChangeVolume",2,0}, {"SfxHandleSetEventParameterValue",3,1},
    {"SfxSetEventParameterValue",4,1}, {"ShipAIData_GetAttackLongCirclingRadius",1,1},
    {"ShipAIData_GetAttackMediumCirclingRadius",1,1}, {"ShipAIData_GetAttackMinCirclingRadiusProp",1,1},
    {"ShipAIData_GetAttackShortCirclingRadius",1,1}, {"ShipAIData_GetBattleCenter",1,3},
    {"ShipAIData_GetBattleCenterAvailable",1,1}, {"ShipAIData_GetBroadsidePowerMultiplier",1,1},
    {"ShipAIData_GetChancesFullBroadsideDischarge",1,1}, {"ShipAIData_GetDefendPoint",1,3},
    {"ShipAIData_GetDefendRadius",1,1}, {"ShipAIData_GetDefendReactDist",1,1}, {"ShipAIData_GetFleeSafeDistance",1,1},
    {"ShipAIData_GetFleeSpeed",1,1}, {"ShipAIData_GetForcedAttackSide",1,1}, {"ShipAIData_GetHangOutDistance",1,1},
    {"ShipAIData_GetHangOutSpeed",1,1}, {"ShipAIData_GetInitialGoal",1,1}, {"ShipAIData_GetInitialTargetActor",1,1},
    {"ShipAIData_GetInitialTeam",1,1}, {"ShipAIData_GetLockTarget",1,1}, {"ShipAIData_GetMaxCloseFiringFreq",1,1},
    {"ShipAIData_GetMaxDistanceToTargetToShoot",1,1}, {"ShipAIData_GetMaxHangOutFiringFreq",1,1},
    {"ShipAIData_GetMaxStaticAttackDist",1,1}, {"ShipAIData_GetMinCloseFiringFreq",1,1},
    {"ShipAIData_GetMinHangOutFiringFreq",1,1}, {"ShipAIData_GetMinStaticAttackDist",1,1},
    {"ShipAIData_GetNumPatrolPoints",1,1}, {"ShipAIData_GetPatrolPoint",2,3},
    {"ShipAIData_GetRadiusToBattleCenterToDisengage",1,1}, {"ShipAIData_GetRadiusToBattleCenterToEngage",1,1},
    {"ShipAIData_GetRenderDebugInfo",1,1}, {"ShipAIData_GetSailingSpeed",1,1},
    {"ShipAIData_GetSailingSpeedInFrontOfTarget",1,1}, {"ShipAIData_GetUseApproachAndFaceAttack",1,1},
    {"ShipAIData_GetUseHorseshoeShapeAttacks",1,1}, {"ShipAIData_SetDebugInfoStr",2,0},
    {"ShipAIData_SetForcedAttackSide",2,0}, {"ShipAIData_SetLockTarget",2,0},
    {"ShipAIData_StartIncreasingBroadsidePowerMultiplierOverTime",1,0},
    {"ShipAIData_StopIncreasingBroadsidePowerMultiplierOverTime",1,0}, {"ShipBattle_AdaptTargetPos",7,3},
    {"ShipBattle_CanEngageInCloseCombat",2,1}, {"ShipBattle_CanFireCannons",2,1},
    {"ShipBattle_FindSuitableRespawnPos",4,3}, {"ShipBattle_GetCirclingDirection",3,1},
    {"ShipBattle_GetClosestAttacker",1,1}, {"ShipBattle_GetIdealRadius",5,1},
    {"ShipBattle_GetSpeedModulationDueToObstacleAvoidance",1,1}, {"ShipBattle_GetTurretState",1,1},
    {"ShipBattle_MustReassessAttackStrategy",1,1}, {"ShipBattle_NotifyPlayerUsingTurret",3,0},
    {"ShipBattle_NotifyShipAttack",2,0}, {"ShipBattle_NotifyShipEngage",4,0}, {"ShipBattle_NotifyShipFired",1,0},
    {"ShipBattle_NotifyStopAttack",1,0}, {"ShipBattle_RegisterShip",1,0}, {"ShipBattle_RemoveShip",1,0},
    {"ShipBattle_SetKrakenActor",1,0}, {"ShipBattle_SetStrategyReassessed",1,0}, {"ShipCheckOKToUndock",2,1},
    {"ShipDocking_FinalizeDocking",2,0}, {"ShipDocking_GetDockForBucBayCho",1,1}, {"ShipDocking_GetDockingSpline",4,1},
    {"ShipDocking_SetupDocking",2,0}, {"ShipDocking_SmallIslandTractorBeam",2,0}, {"ShipDocking_Undock",2,0},
    {"ShipFireBroadsideCannons",2,1}, {"ShipFireBroadsideCannonsToTarget",3,1}, {"ShipForceField",2,0},
    {"ShipGetRigidBodyActor",1,1}, {"ShipLocomotionAutoThrottleStop",1,0}, {"ShipLocomotionDropAnchor",1,0},
    {"ShipLocomotionEnableAutoThrottle",2,0}, {"ShipLocomotionGetAutoThrottlePower",1,1},
    {"ShipLocomotionLiftAnchor",1,0}, {"ShipLocomotionSetDiving",2,0}, {"ShipLocomotionSetNormal",1,0},
    {"ShipLocomotionSetRising",2,0}, {"ShipLocomotionSinking",2,0}, {"ShipManagement_SetDefaultShipForNewPlayers",1,0},
    {"ShipSetPosition",8,0}, {"ShipSetRigidBodyEnabled",2,0}, {"ShipStartChargingBroadsides",1,0},
    {"ShipSwitchToNextCam",2,0}, {"ShipTeams_IsPlayerShip",1,1}, {"ShipTeams_NotifyTeamOfMunitionHit",2,0},
    {"ShipTeams_RegisterShipInTeam",2,0}, {"ShipTeams_RemoveShipFromTeam",1,0}, {"ShipTeleportShipToOceanSurface",2,0},
    {"ShipUseAbility",1,0}, {"ShipUseAbilityDone",1,0}, {"Ship_GetGibbsStandHereActor",1,1},
    {"Ship_GetPassengerHandle",2,1}, {"Ship_SetupBoarding",3,4}, {"Ship_SetupDisembarking",3,4},
    {"Ship_TeleportShip",4,0}, {"SlaveActorToVehicle",5,0}, {"SpawnGroundPoundAOE",7,0},
    {"SpawnerQuarterback_GetLastActorSpawned",2,1}, {"SpawnerQuarterback_GetSpawnDelay",2,1},
    {"SpawnerQuarterback_NeedsReconciling",2,1}, {"SpawnerQuarterback_SetAdditionalActorParms",3,0},
    {"SpawnerQuarterback_SetDeathDelay",3,0}, {"SpawnerQuarterback_SetMakeRoomForSpawned",3,0},
    {"SpawnerQuarterback_SetMaxNumberOfSpawnableActors",3,0}, {"SpawnerQuarterback_SetMinNumberOfSpawnableActors",3,0},
    {"SpawnerQuarterback_SetSpawnDelay",3,0}, {"SpawnerQuarterback_SkipSpawn",3,0},
    {"SpawnerQuarterback_SpawnActorInstantly",5,0}, {"SpawnerQuarterback_SpawnExpendableInstantly",5,0},
    {"SpawnerQuarterback_StopSpawning",2,0}, {"SphereCheckForObstacles",11,1}, {"StartDirectionCamera",3,1},
    {"StartTopDownCamera",3,1}, {"StealthQB_AddCarryAndThrowThreat",1,0}, {"StealthQB_AddCombatAgent",1,0},
    {"StealthQB_AddScareThreat",1,0}, {"StealthQB_AddWeaponThreat",1,0}, {"StealthQB_GetCombatData",1,2},
    {"StealthQB_GetJockFleePos",1,3}, {"StealthQB_RemoveCombatAgent",1,0}, {"StealthQB_TriggerChaseMusic",2,0},
    {"StealthQB_TriggerClosestAlarm",2,0}, {"Stealth_AllTargetsLost",1,0}, {"Stealth_EnableHealth",1,0},
    {"Stealth_GetActiveTarget",1,1}, {"Stealth_GetNoisyTarget",1,1}, {"Stealth_GetParam",2,1},
    {"Stealth_GetPerceptionTarget",2,1}, {"Stealth_GetPerceptionTargetQualified",3,1}, {"Stealth_GetSpawnFacing",1,3},
    {"Stealth_GetSpawnPosition",1,3}, {"Stealth_NonSightedTargetAcquire",2,0}, {"Stealth_RestartCombat",3,0},
    {"Stealth_SetBreakOffDistance",2,0}, {"Stealth_SetParam",3,0}, {"Stealth_ShootItem",2,0},
    {"Stealth_ShoulderCharge",2,0}, {"Stealth_ShoulderChargeOrScare",2,0}, {"Stealth_StopCombat",1,0},
    {"Stealth_TargetAcquired",3,0}, {"Stealth_TargetLost",3,0}, {"Stealth_ThrowItem",2,0},
    {"Stealth_TurnOffFlashLight",2,0}, {"StickyHand_OrientActorToSurface",4,4},
    {"StickyHand_OrientActorToSurfaceTwist",6,4}, {"StopDirectionCamera",2,0}, {"StopDrivingProceduralInterface",1,0},
    {"StopTopDownCamera",2,0}, {"UI_SetRumpusEditButtons",6,0}, {"UnDocking_GetShipDock",1,1},
    {"UseNavMesh_GetPointOnMesh_Y",4,1}, {"VehicleManager_BipedInteractionConnect",3,0},
    {"WallDetect_CanWallSlide",2,1}, {"WallDetect_GetLastWallPointAndNormal",1,6},
    {"WallJump_EnableStickRequirement",2,0}, {"WallJump_GetWallJump_JumpToPoint",1,4}, {"WallJump_SetDisableTimer",2,0},
    {"ZurgbotQB_GetMeleeCount",2,1}, {"ZurgbotQB_GetRangedCount",2,1},
};
// The ports: 1.0 semantics on 3.0's natives; their stack adjustments are the signature differences (SX-9).
inline Native g_ports[] = {
    {"HashFind", 3, 2, &detail::keyedPort<1, detail::eng::kHashFind>},
    {"HashFindInt", 3, 2, &detail::keyedPort<1, detail::eng::kHashFind>},
    {"HashFindStr", 3, 2, &detail::keyedPort<1, detail::eng::kHashFind>},
    {"HashAdd", 3, 1, &detail::keyedPort<1, detail::eng::kHashAdd>},
    {"HashAddInt", 3, 1, &detail::keyedPort<1, detail::eng::kHashAdd>},
    {"HashAddStr", 3, 1, &detail::keyedPort<1, detail::eng::kHashAdd>},
    {"HashRemove", 2, 1, &detail::keyedPort<0, detail::eng::kHashRemove>},
    {"HashFindInRange", 3, 2, &detail::hashFindInRange},
    {"PauseAtEndWhenDoneChoreography", 1, 0, &detail::pauseAtEndWhenDoneCho},
    {"ActorGetNamedLocatorPosition", 2, 3, &detail::actorGetNamedLocatorPosition},
    {"Matrix3x3BuildFromVector", 3, 9, &detail::matrix3x3BuildFromVector},
    {"ReactionInfo_GetReactionScript", 3, 1, &detail::reactionInfoGetReactionScript},
    {"Combat_AttackStart", 2, 0, &detail::combatAttackStart},
    {"GetSliderFloat", 5, 1, &detail::getSliderFloat},
    {"ReactionInfo_FlipMotionName", 1, 1, &detail::reactionInfoFlipMotionName},
    {"AiBehaviorGetStringProperty", 2, 1, &detail::behaviorProperty<detail::eng::kBehaviorString>},
    {"AiBehaviorGetFloatProperty", 2, 1, &detail::behaviorProperty<detail::eng::kBehaviorFloat>},
    {"AiBehaviorGetBehaviorStringFromType", 1, 1, &detail::aiBehaviorGetBehaviorStringFromType},
    {"AiBehaviorGetCurrentBehaviorType", 1, 1, &detail::aiBehaviorGetCurrentBehaviorType},
    {"SlaveActorToVehicle", 5, 0, &detail::slaveActorToVehicle},
    {"TriggerEvent", 3, 0, &detail::triggerEvent},
};

// Stubs for every listed native, the ports over them with sxPorts10; behavior types with sxBeh10 too.
inline bool install(const Config& c)
{
    if (!c.sx10 || !c.sxNatives10) return true;
    engine::slangvm::Config vm;
    for (Native& n : g_natives) vm.natives.push_back(&n);
    if (c.sxPorts10)
        for (Native& n : g_ports) vm.natives.push_back(&n);
    if (c.sxPorts10 && c.sxBeh10) vm.onCallByName = detail::behaviorTypeTo10;
    return engine::slangvm::install(vm);
}

}  // namespace di1::sxnatives
