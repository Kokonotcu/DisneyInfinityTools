// 1.0 Hash* key names as 3.0's compiled key ids (keymerge.py -> keys30.tsv). core/log. Script and load threads.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::sxkeys {

// Logs each unpaired key once from now on (SX-19: never before), as the plugin's start did.
inline bool install(const Config& c);
// A 1.0 key name's 3.0 id: the paired id, else keyFallback's id outside 3.0's enum, logged once.
inline int keyId(const char* name);

// 3.0 compiles key names to one alphabetical enum (0..722). 429 pairs: twin scripts and twin .bmx Data rows.
struct Key {
    const char* name;
    int id;
};
inline constexpr Key KEYS30[] = {
    {"Acceleration",1}, {"Action",2}, {"ActionType",4}, {"ActorCollidedWith",6}, {"ActorHandle",7}, {"ActorOn",10},
    {"ActorTargetForceRotate",11}, {"AddStat",12}, {"AdjustSpeedByAngleToTarget",13}, {"AgentHandle",14},
    {"AimDestinationX",15}, {"AimDestinationY",16}, {"AimDestinationZ",17}, {"AirAction",18}, {"AnimalsMoveType",23},
    {"AnimTreeEventHash",24}, {"AnimTreeEventIndex",25}, {"AnimTreeEventName",26}, {"apexHeight",27},
    {"AppearChoAnchor",28}, {"AppearChoDelay",29}, {"AppearChoName",30}, {"ArcHeight",31}, {"AscendDist",33},
    {"Attack",35}, {"AttackDamage",36}, {"AttackDistance",37}, {"AttackGoalPosZ",41}, {"AttackGoalType",42},
    {"AttackLoc",44}, {"AttackOwner",46}, {"AttackRange",47}, {"AttackTarget",49}, {"AttackType",50}, {"AutoReload",51},
    {"AuxTarget",52}, {"BehaviorCC",54}, {"BehaviorHandlesMove",55}, {"BehaviorHandlesTurn",56}, {"blendFrames",58},
    {"BlockType",61}, {"BreakDistance",62}, {"Chase",71}, {"CheckTargetSighted",75}, {"CircleRadius",76},
    {"CircleStrafeTetherDistance",78}, {"ClearVelOnExit",79}, {"CollisionNormalX",81}, {"CollisionNormalY",82},
    {"CollisionNormalZ",83}, {"CollisionPosX",84}, {"CollisionPosY",85}, {"CollisionPosZ",86}, {"ColorR",89},
    {"Combo",92}, {"Connect",93}, {"connectControl",94}, {"ConstrainTargetLoc",95}, {"CoverType",100},
    {"CustomType",102}, {"DamageLevel",104}, {"Deceleration",108}, {"DecelerationThreshold",109}, {"DecoupleTurn",110},
    {"DeEquipNamedShield",111}, {"Delay",117}, {"DescendDist",118}, {"Destroy",119}, {"Difficulty",121},
    {"Direction",122}, {"DisableGravity",124}, {"DisappearChoAnchor",125}, {"DisappearChoDelay",126},
    {"DisappearChoName",127}, {"Distance",128}, {"distanceAlongPath",129}, {"DoHide",132}, {"DoLand",133},
    {"DoLandAnim",134}, {"DoNotExit",135}, {"DoPreLaunch",136}, {"DoSmash",138}, {"DoSpecialLandAnim",139},
    {"DriftAcceleration",140}, {"Duration",143}, {"DurationMax",144}, {"DurationMin",145}, {"EnableNavmeshClear",146},
    {"EnableUseNavMesh",147}, {"EnvInfo_GroupMask",148}, {"EquipNamedShield",151}, {"EventName",152},
    {"EventSender",153}, {"EventType",154}, {"ExitAtTarget",156}, {"ExitCode",157}, {"ExitOnAnimTreeSync",158},
    {"ExitType",159}, {"ExitUID",160}, {"ExitWhenDone",161}, {"FaceAttacker",163}, {"faceBackward",164},
    {"FaceSpeed",165}, {"FaceTarget",166}, {"FaceTargetX",167}, {"FaceTargetY",168}, {"FaceTargetZ",169},
    {"FadeInDelay",171}, {"FadeOutDelay",172}, {"FiredBy",173}, {"FleeFrom",174}, {"FleeType",175}, {"fleeWhenHit",176},
    {"FlyIn",177}, {"ForceDirection",188}, {"ForcePlayStop",190}, {"ForcePositionToTarget",191}, {"FourCC",192},
    {"GenericPerformFlags",194}, {"GoalPosX",198}, {"GoalPosY",199}, {"GoalPosZ",200}, {"Gravity",201},
    {"gravityStrength",202}, {"Group",203}, {"HangTime",204}, {"HangTimeGravity",205}, {"HangTimeSpeedThreshold",206},
    {"HasTimeLimit",208}, {"HealthZero_Handle",210}, {"HealthZero_Owner",211}, {"HealthZero_Reason",212},
    {"HealthZero_Shooter",213}, {"HeightOfFan",214}, {"HerdOverride",215}, {"HideDelay",216}, {"Hit",217},
    {"HitReaction4CC",218}, {"IdleAction",220}, {"IdleAudioEvent",221}, {"IdleType",223}, {"IgnoreActorCollisions",224},
    {"IgnoreAttackerCollision",225}, {"IgnoreAttackerCollisionFrameCount",226}, {"IgnoreCollisionsWithActor",228},
    {"IgnoredActor",229}, {"IgnorePerception",232}, {"IgnoreSignificant",233}, {"IgnoreWorldCollisions",235},
    {"ImpactBlocked",236}, {"ImpactDirX",237}, {"ImpactDirY",238}, {"ImpactDirZ",239}, {"ImpactLocX",240},
    {"ImpactLocY",241}, {"ImpactLocZ",242}, {"ImpactPassThru",243}, {"ImpactVelX",244}, {"ImpactVelY",245},
    {"ImpactVelZ",246}, {"Index",247}, {"InitialAction",248}, {"InitialWanderAngle",249}, {"InputOnlyWhenFalling",251},
    {"Instant",252}, {"Interact_Action",253}, {"InteractionEventSender",254}, {"InteractionEventTypeIndex",256},
    {"InteractionSender",257}, {"InteractionTarget",258}, {"InteractionType",259}, {"InteractionTypeIndex",260},
    {"InteractionValid",261}, {"KnockBackDirection",275}, {"KnockBackOthers",276}, {"KnockBackOthersMinSpeed",277},
    {"KnockBackSpeed",278}, {"LandAction",279}, {"LandType",280}, {"LastAgentHandle",281}, {"LastAttack",282},
    {"LateJoin",284}, {"LaunchType",285}, {"LeftOrRight",287}, {"LocalNavMode",288}, {"LocomotionMode",289},
    {"LocomotionSet",290}, {"LookDestinationX",293}, {"LookDestinationY",294}, {"LookDestinationZ",295},
    {"ManualRotate",301}, {"MaxDistance",303}, {"MaxDriftSpeedRatio",304}, {"MaxMoveSpeed",306}, {"MaxTargetDist",309},
    {"MaxTargetRot",310}, {"MaxTurnRate",312}, {"MaxTurnSpeed",313}, {"MaxWallBounces",314}, {"MessageOwner",316},
    {"Method_Custom",317}, {"MinDuration",318}, {"MinMoveSpeed",320}, {"MinTargetDist",323}, {"MinTargetRot",324},
    {"MinTargetThreshold",326}, {"MinTurnRate",327}, {"MinTurnSpeed",328}, {"Mission",329}, {"MoveAction",331},
    {"MoveAnimName",332}, {"MoveClipIgnore",335}, {"MoveDuringStartEvent",336}, {"MoveEvent",337},
    {"MoveEventHash",338}, {"MoveForwardOnly",339}, {"MovementType",340}, {"MoveSpeed",341}, {"MoveSpeedMax",342},
    {"MoveSpeedMin",343}, {"MoveTargetLocX",344}, {"MoveTargetLocY",345}, {"MoveTargetLocZ",346}, {"MovingAttack",349},
    {"MovingTarget",350}, {"myAIBMStringValueName",354}, {"myAIBMValueName",355}, {"NavmeshClearTimeout",358},
    {"NavmeshFailedGotoAnyway",359}, {"NavmeshResetAllVars",361}, {"NavmeshTimeout",362}, {"NavMeshWaitAction",363},
    {"NonProgressTimeout",371}, {"NotifyAttacker",373}, {"NowLarge",374}, {"NowSmall",375}, {"OffNavMeshAppearCho",377},
    {"OffNavMeshAppearChoAnchor",378}, {"OffNavMeshDisppearCho",380}, {"OffNavMeshDisppearChoAnchor",381},
    {"OffNavMeshRecoveryType",382}, {"OffsetX",384}, {"OffsetY",385}, {"OffsetZ",386}, {"OnGroundAtImpact",387},
    {"operation",388}, {"OrientX",391}, {"OrientY",392}, {"OrientZ",393}, {"OverrideGravity",398},
    {"OverrideSpeed",399}, {"PassedToPartner",401}, {"PathEndDistance",402}, {"PathGoAcceleration",403},
    {"PathName",404}, {"PathTetherDistance",405}, {"PauseAfterAnim",411}, {"PlatformId",420}, {"PlatformType",421},
    {"PlayerId",423}, {"PlayStop",424}, {"PlayStopMoveEventOnInterrupt",425}, {"PopUpSpeed",429},
    {"ProtectAfterBlock",437}, {"PursueReason",438}, {"QuickLand",441}, {"QuickLaunch",442},
    {"RadiusForNavMeshTest",443}, {"RadiusOffset",445}, {"RandomChance",447}, {"RandomDist",448}, {"RandomSleep",449},
    {"Range",450}, {"RangeVariance",453}, {"Repeatable",454}, {"Repetitions",455}, {"RespondToActorCollisions",458},
    {"ResumeOnInterrupt",459}, {"RideTarget",461}, {"RotateOffset",466}, {"RotateToTarget",476}, {"RotSpeed",478},
    {"rowboatHandle",479}, {"RunMaxTurnSpeed",480}, {"RunStartTurnSpeed",481}, {"RunTurnAcc",483},
    {"RunWaitToTurn",484}, {"RunWaitToTurnAgain",485}, {"SelectedSubrange",489}, {"SetRandomScale",494},
    {"ShowDelay",500}, {"Shuffle",501}, {"ShuffleSide",503}, {"SigNodeIndex",508}, {"SnapToGround",510},
    {"SourcePosX",512}, {"SourcePosY",513}, {"SourcePosZ",514}, {"SpawnLoc0X",518}, {"SpawnLoc0Y",524},
    {"SpawnLoc0Z",530}, {"Speed",549}, {"SpeedProperty",551}, {"SplineHandle",552}, {"SplinePath",554},
    {"StartAnimtreeEvents",559}, {"StartAnimtreeEventsHash",560}, {"StartDelay",561}, {"StartEventHash",563},
    {"StartMoveAction",564}, {"StartMoveEvent",565}, {"StartMoveEventHash",566}, {"StartPosX",567}, {"StartPosY",568},
    {"StartPosZ",569}, {"StateBits",571}, {"Stationary",572}, {"StopAfterStopAction",575}, {"StopAnim",576},
    {"StopAnimName",577}, {"StopDistance",578}, {"StopEventHash",580}, {"StopMoveAction",581}, {"StopMoveEvent",582},
    {"StopMoveEventHash",583}, {"StopOnExit",584}, {"StoreWayPoint",585}, {"STravParm1",587}, {"STravParm2",588},
    {"SucceedOnAnimTreeSync",589}, {"Success",590}, {"target",592}, {"TargetActor",593}, {"TargetActor1",594},
    {"TargetActor2",595}, {"TargetActorNavigate",606}, {"targetControl",608}, {"targetDistanceAlongPath",609},
    {"TargetHeading",611}, {"TargetLocator",613}, {"TargetLocX",614}, {"TargetLocY",615}, {"TargetLocZ",616},
    {"TargetObject",617}, {"TargetOnGround",618}, {"TargetPlayerByDefault",619}, {"TargetPositionX",620},
    {"TargetPositionY",621}, {"TargetPositionZ",622}, {"TargetPosX",623}, {"TargetPosY",624}, {"TargetPosZ",625},
    {"TargetProperty",626}, {"TargetSplineHandle",629}, {"TargetThreshold",630}, {"TargetType",631}, {"TargetVelX",632},
    {"TargetVelY",633}, {"TargetVelZ",634}, {"TargetVisible",635}, {"TauntType",637}, {"Teleport",638},
    {"TerrainCollisionResponse",639}, {"TetherDistance",640}, {"TetherExceededReturn",641}, {"TetherPosX",642},
    {"TetherPosY",643}, {"TetherPosZ",644}, {"Threshold",645}, {"TimeAfterImpact",646}, {"Timeout",647},
    {"TriggeredAction",651}, {"TriggeredTime",652}, {"TurnOffCollision",653}, {"TurnOffGravity",654}, {"TurnSpeed",655},
    {"UID",660}, {"UpperType",661}, {"UpperTypeLeft",662}, {"UseAcceleration",663}, {"UseCustomHashParams",665},
    {"UseDeceleration",666}, {"UseDest",667}, {"UseDuration",668}, {"UseFacing",669}, {"UseHipsForPosition",672},
    {"UseLookDestination",673}, {"UseNavMesh",674}, {"UseNavMesh_ConstrainTargetToNavMesh",675},
    {"UseNavMesh_NavRadius",677}, {"UseNavMesh_SafeDistOnMesh",678}, {"UseNavMesh_SetGoalRange",679},
    {"UseNavMesh_WaypointRange",680}, {"UseNodeName",681}, {"UseRigidBodyFuncs",683}, {"UseSendPoints",684},
    {"UseSTravParm1",685}, {"UseSTravParm2",686}, {"UseTarget",687}, {"UseWalkSpeed",689}, {"Victim",690},
    {"WaitForGround",691}, {"WaitMoveAction",692}, {"WaitMoveEventHash",693}, {"WalkAction",694},
    {"WalkMaxTurnSpeed",695}, {"WalkSpeed",696}, {"WalkStartAction",697}, {"WalkStartTurnSpeed",698},
    {"WalkStopAction",699}, {"WalkTurnAcc",700}, {"WalkWaitToTurn",701}, {"WalkWaitToTurnAgain",702},
    {"WallBounceMaxSpeed",703}, {"WallBounceMinSpeed",704}, {"WallBouncePopUpSpeed",705},
    {"WallBounceRestitutionCoef",706}, {"WanderChangeDirectionDistance",707}, {"WanderChoiceArc",708},
    {"WasControllerOwner",709}, {"WasLarge",710}, {"WasSmall",711}, {"Weapon4CC",713}, {"WeaponName",715},
    {"WhichAnim",716}, {"Who",717}, {"WillFaceTarget",718}, {"Withdraw",719}, {"YAimOffset",722},
};

namespace detail {

constexpr uint32_t kFnvBasis = 2166136261u;   // FNV-1a, 32 bits
constexpr uint32_t kFnvPrime = 16777619u;
constexpr uint32_t kFallbackBase = 0x40000000u;   // fallback ids in [0x40000000, 0x7fffffff], far above the enum
constexpr uint32_t kFallbackMask = 0x3fffffffu;

constexpr char asciiLower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

// a against b with ASCII case ignored: below 0, 0 or above 0.
constexpr int compareNoCase(const char* a, const char* b)
{
    while (*a && asciiLower(*a) == asciiLower(*b)) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)asciiLower(*a) - (int)(unsigned char)asciiLower(*b);
}

// KEYS30 strictly ascends with case ignored, so keyId's binary search finds every exact name.
constexpr bool ascending()
{
    for (size_t i = 1; i < sizeof(KEYS30) / sizeof(KEYS30[0]); ++i)
        if (compareNoCase(KEYS30[i - 1].name, KEYS30[i].name) >= 0) return false;
    return true;
}
static_assert(ascending(), "KEYS30 must ascend, ASCII case ignored, without case-only twins");

struct State {
    std::atomic<bool> log{false};   // set once in install(); read on any thread
    core::LogOnce<int> unpaired;    // the fallback ids seen, logged or not; its own leaf lock
};
inline State g_state;

}  // namespace detail

// FNV-1a of the exact name (1.0 interns key strings byte-exact, 0x416dc0): no collision among 570 names (keycheck.py).
inline int keyFallback(const char* s)
{
    uint32_t h = detail::kFnvBasis;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * detail::kFnvPrime;
    return (int)(detail::kFallbackBase | (h & detail::kFallbackMask));
}

inline int keyId(const char* name)
{
    const Key* end = KEYS30 + sizeof(KEYS30) / sizeof(KEYS30[0]);
    auto less = [](const Key& e, const char* n) { return detail::compareNoCase(e.name, n) < 0; };
    const Key* k = std::lower_bound(KEYS30, end, name, less);
    if (k != end && !strcmp(k->name, name)) return k->id;
    int id = keyFallback(name);
    detail::State& s = detail::g_state;
    const bool first = s.unpaired.first(id);   // recorded even before install(), as the old set was
    if (first && s.log) core::logf("sx key '%s': not in the 3.0 enum, id %#x", name, id);
    return id;
}

inline bool install(const Config&)
{
    detail::g_state.log = true;
    return true;
}

}  // namespace di1::sxkeys
