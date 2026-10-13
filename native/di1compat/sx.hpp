// 1.0 Slang (.sx) behaviour scripts on 3.0's Slang VM. The 1.0 file is never rewritten: the container is adapted when
// served (SX-1) and its imports are bound to 1.0-semantics natives when the engine links it (SX-3/4/5).
//
// Evidence (static VAs; tools in wt\_scratch\sx):
//  - 3.0 fix-up FUN_007366f0 accepts only version 0x100d; all 699 1.0 .sx are 0x100c. The containers differ only in the
//    import entry: 1.0 {name, hash, slot, sig} 16 B, 3.0 {name, hash, slot, 0, sig, 0} 24 B (verify-lua C4). The fix-up,
//    the linker FUN_00736da0 and the CALL handler (0x738345) are the only readers of [hdr+0x54]; the linker writes +8,
//    the CALL handler reads +8; +0xc and +0x14 are never read (0 in 53,742/53,742 shipped 3.0 entries).
//  - Native ABI: CALL handler `push esi; call [entry+8]; add esp,4` = void __cdecl fn(ctx); a null slot is skipped
//    (0x738360) without popping the arguments, so a missing native leaves the VM stack off by its signature.
//    ctx+4 = script header, +0x1c = pc of the CALL in u16 words from [hdr+0x40], cells = ***(ctx+0xc) {type, value},
//    top index = **(ctx+0x20). Types: 0 int, 1 float, 2 pointer (string literal / object), 3 hash (3.0 only).
//  - 480 natives that 1.0 scripts import are missing from 3.0 (451) or registered with another stack signature (29).
//    Their (args, results) come from the 1.0 bodies (sig.py: every path of the body walked; the pair agrees with the
//    1.0 compiler's signature word for 1792 of 1792 analysed natives; 465 of the 480 are 1.0 natives -- the other 15
//    exist in neither exe and only 3.0-shadowed scripts import them).
//  - 3.0 Hash* keys are compiler ids (a global enum numbered in case-insensitive name order: 188 names paired from
//    twin scripts, 0 order violations); 1.0 scripts pass the key as a string literal, which 3.0's pop (0x731fd0) reads as
//    0. 210 of the 269 1.0-only scripts use Hash*.
#pragma once
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace sx {

constexpr uint32_t MAGIC = 0x474e4c53, V10 = 0x100c, V30 = 0x100d, NATIVE_ORDER = 0x12345678;
constexpr uint32_t DI1 = 0x00314944;   // 'DI1': import entry word 5 of a served 1.0 script (never read by 3.0)

inline uint32_t rd(const void* p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline void wr(std::string& s, size_t o, uint32_t v) { memcpy(&s[o], &v, 4); }

inline bool is10(const void* d, size_t n) { return n >= 0x68 && rd(d) == MAGIC && rd((const uint8_t*)d + 4) == V10; }

// ---- SX-1: 1.0 container -> 3.0 container, at serve time ----
// Append form (scratch\lua\sxconv.py 'append', 699/699 structurally valid): the 1.0 bytes stay where they are, a 24-B
// import table goes after them and header idx21 points at it, so no code word or data offset moves. Entry word 3 of
// entry 0 holds the file offset of the script's path (appended too) until bind() names the script in the log.
// -> import count, or -1 (why) for a file outside what the corpus shows (other byte order, a 1.0 idx10 section).
inline int transcode(const void* data, size_t n, const std::string& path, std::string& out, std::string* why)
{
    const uint8_t* b = (const uint8_t*)data;
    auto fail = [&](const char* w) { if (why) *why = w; return -1; };
    if (!is10(data, n)) return fail("not a 1.0 SLNG");
    if (rd(b + 8) != NATIVE_ORDER) return fail("byte order");
    if (rd(b + 0x28)) return fail("1.0 idx10 section used");   // 0 in 699/699; 3.0 has no reader for it
    uint32_t k = rd(b + 0x2c), rel = rd(b + 0x54);
    size_t tbl = 0x54 + (size_t)(int32_t)rel + 1;
    if (k && (!rel || tbl + 16ull * k > n)) return fail("import table outside the file");
    out.assign((const char*)data, n);
    wr(out, 4, V30);
    while (out.size() % 4) out.push_back(0);
    size_t at = out.size();
    out.append(24ull * k, '\0');
    for (uint32_t i = 0; i < k; ++i) {
        size_t e = tbl + 16ull * i, f = at + 24ull * i;
        uint32_t nameRel = rd(b + e);
        if (nameRel) {
            size_t name = e + (size_t)(int32_t)nameRel + 1;
            if (name >= n) return fail("import name outside the file");
            wr(out, f, (uint32_t)(name - f - 1));
        }
        wr(out, f + 4, rd(b + e + 4));     // hash
        wr(out, f + 16, rd(b + e + 12));   // signature word
        wr(out, f + 20, DI1);
    }
    if (k) {
        wr(out, 0x54, (uint32_t)(at - 0x54 - 1));
        wr(out, at + 12, (uint32_t)out.size());
        out.append(path.c_str(), path.size() + 1);
        while (out.size() % 4) out.push_back(0);
    }
    wr(out, 0xc, (uint32_t)out.size());
    return (int)k;
}

// ---- VM access (3.0 layout, from the helpers 0x731df0 push float, 0x731fd0 pop int, 0x732140 pop pointer) ----
struct Cell { int32_t type; union { int32_t i; float f; const char* s; void* p; }; };
struct Vm {
    uint8_t* c;
    int& top() const { return **(int**)(c + 0x20); }
    Cell* base() const { return *(Cell**)*(uint8_t**)*(uint8_t**)(c + 0xc); }
    Cell& at(int k) const { return base()[top() - 1 - k]; }   // 0 = top
    Cell pop() const { return base()[--top()]; }
    void push(Cell v) const { base()[top()++] = v; }
    void pushInt(int v) const { Cell x; x.type = 0; x.i = v; push(x); }
};

inline uintptr_t g_exe = 0x400000;                        // runtime base of DisneyInfinity3.exe
inline void (*g_log)(const char* fmt, ...) = nullptr;
typedef void(__cdecl* NativeFn)(uint8_t* ctx);
inline void call30(uint32_t va, uint8_t* ctx) { ((NativeFn)(g_exe + va - 0x400000))(ctx); }

struct Native {
    const char* name;
    int8_t args, results;    // the 1.0 body's pops and pushes
    NativeFn fn;             // nullptr: stack-balanced stub (pops args, pushes int 0 per result)
    long hits;
};

// The import a native was called through (CALL handler 0x738345: ctx+0x1c = pc, u16 import index at ip+2).
inline uint8_t* callingEntry(uint8_t* ctx)
{
    uint8_t* hdr = *(uint8_t**)(ctx + 4);
    uint8_t* ip = *(uint8_t**)(hdr + 0x40) + 2 * *(int*)(ctx + 0x1c);
    return *(uint8_t**)(hdr + 0x54) + 24 * *(uint16_t*)(ip + 2);
}

// SX-4: one stub for every 1.0 native without a port; bind() stores its Native in entry word 3.
inline void __cdecl stub(uint8_t* ctx)
{
    Native* n = *(Native**)(callingEntry(ctx) + 12);
    Vm v{ctx};
    v.top() -= n->args;
    for (int i = 0; i < n->results; ++i) v.pushInt(0);
    if (_InterlockedIncrement(&n->hits) == 1 && g_log) g_log("sx stub %s first called (pops %d, pushes %d zero)", n->name, n->args, n->results);
}

// ---- SX-5 ports ----
inline bool readStr(const char* p, char* out, size_t cap)   // a string cell's text; script memory, guarded anyway
{
    __try {
        size_t i = 0;
        for (; p && i + 1 < cap && p[i]; ++i) out[i] = p[i];
        out[i] = 0;
        return p != nullptr;
    } __except (1) { out[0] = 0; return false; }
}

// 1.0 Hash* key name -> 3.0 key id (KEYS30); names no paired 3.0 script uses get ids above the 3.0 enum (max 719),
// handed out once per name, so 1.0 scripts still agree among themselves.
struct Key { const char* name; int id; };
inline const Key KEYS30[] = {   // scratch\sx\hashmap.py -> hashids.tsv
    {"Distance",128}, {"DoLand",133}, {"DoPreLaunch",136}, {"DoSmash",138}, {"Duration",143}, {"EnvInfo_GroupMask",148},
    {"EquipNamedShield",151}, {"EventName",152}, {"EventSender",153}, {"EventType",154}, {"ExitCode",157},
    {"ExitOnAnimTreeSync",158}, {"ExitType",159}, {"ExitUID",160}, {"ExitWhenDone",161}, {"faceBackward",164},
    {"FaceTarget",166}, {"FaceTargetX",167}, {"FaceTargetY",168}, {"FaceTargetZ",169}, {"FlyIn",177}, {"FourCC",192},
    {"GenericPerformFlags",194}, {"GoalPosX",198}, {"GoalPosY",199}, {"GoalPosZ",200}, {"Gravity",201},
    {"gravityStrength",202}, {"Group",203}, {"HealthZero_Handle",210}, {"HealthZero_Owner",211},
    {"HealthZero_Reason",212}, {"HealthZero_Shooter",213}, {"HerdOverride",215}, {"Hit",217}, {"IdleAudioEvent",221},
    {"IdleType",223}, {"IgnoreActorCollisions",224}, {"IgnoredActor",229}, {"IgnorePerception",232},
    {"IgnoreSignificant",233}, {"IgnoreWorldCollisions",235}, {"ImpactDirX",237}, {"ImpactDirY",238}, {"ImpactDirZ",239},
    {"ImpactLocX",240}, {"ImpactLocY",241}, {"ImpactLocZ",242}, {"ImpactVelX",244}, {"ImpactVelY",245},
    {"ImpactVelZ",246}, {"Index",247}, {"InitialAction",248}, {"Instant",252}, {"Interact_Action",253},
    {"InteractionEventSender",254}, {"InteractionEventTypeIndex",256}, {"InteractionSender",257},
    {"InteractionTarget",258}, {"InteractionType",259}, {"InteractionTypeIndex",260}, {"InteractionValid",261},
    {"LandAction",279}, {"LandType",280}, {"LastAttack",282}, {"LateJoin",284}, {"LaunchType",285}, {"LeftOrRight",287},
    {"LocomotionMode",289}, {"LocomotionSet",290}, {"LookDestinationX",293}, {"LookDestinationY",294},
    {"LookDestinationZ",295}, {"MaxMoveSpeed",306}, {"MaxTurnRate",312}, {"MessageOwner",316}, {"MinMoveSpeed",320},
    {"MinTargetThreshold",326}, {"MinTurnRate",327}, {"MoveAction",331}, {"MoveClipIgnore",335}, {"MoveEvent",337},
    {"MovementType",340}, {"MoveSpeed",341}, {"MoveTargetLocX",344}, {"MoveTargetLocY",345}, {"MoveTargetLocZ",346},
    {"MovingAttack",349}, {"myAIBMStringValueName",354}, {"OffsetX",384}, {"OffsetY",385}, {"OffsetZ",386},
    {"OrientX",391}, {"OrientY",392}, {"OrientZ",393}, {"OverrideGravity",398}, {"OverrideSpeed",399}, {"PlatformId",420},
    {"PlatformType",421}, {"PursueReason",438}, {"QuickLand",441}, {"QuickLaunch",442}, {"RadiusOffset",445},
    {"RandomSleep",449}, {"Range",450}, {"Repeatable",454}, {"Repetitions",455}, {"ResumeOnInterrupt",459},
    {"RideTarget",461}, {"RotateOffset",466}, {"RotateToTarget",476}, {"RotSpeed",478}, {"rowboatHandle",479},
    {"SelectedSubrange",489}, {"Shuffle",501}, {"ShuffleSide",503}, {"SigNodeIndex",508}, {"SourcePosX",512},
    {"SourcePosY",513}, {"SourcePosZ",514}, {"Speed",549}, {"SpeedProperty",551}, {"SplineHandle",552},
    {"StartDelay",561}, {"StartPosX",567}, {"StartPosY",568}, {"StartPosZ",569}, {"StateBits",571}, {"StopDistance",578},
    {"StopOnExit",584}, {"STravParm1",587}, {"STravParm2",588}, {"SucceedOnAnimTreeSync",589}, {"Success",590},
    {"target",592}, {"TargetActor",593}, {"TargetActor1",594}, {"TargetActor2",595}, {"TargetActorNavigate",606},
    {"targetControl",608}, {"TargetLocX",614}, {"TargetLocY",615}, {"TargetLocZ",616}, {"TargetPositionX",620},
    {"TargetPositionY",621}, {"TargetPositionZ",622}, {"TargetPosX",623}, {"TargetPosY",624}, {"TargetPosZ",625},
    {"TargetSplineHandle",629}, {"TargetThreshold",630}, {"TargetType",631}, {"TargetVelX",632}, {"TargetVelY",633},
    {"TargetVelZ",634}, {"TargetVisible",635}, {"TauntType",637}, {"Teleport",638}, {"TetherDistance",640},
    {"TetherPosX",642}, {"TetherPosY",643}, {"TetherPosZ",644}, {"Threshold",645}, {"Timeout",647},
    {"TriggeredAction",651}, {"TriggeredTime",652}, {"TurnSpeed",655}, {"UID",660}, {"UpperType",661},
    {"UpperTypeLeft",662}, {"UseCustomHashParams",665}, {"UseDuration",668}, {"UseFacing",669}, {"UseNavMesh",674},
    {"UseNavMesh_SetGoalRange",677}, {"UseNavMesh_WaypointRange",678}, {"UseNodeName",681}, {"UseSTravParm1",685},
    {"UseSTravParm2",686}, {"UseTarget",687}, {"UseWalkSpeed",689}, {"Victim",690}, {"WalkSpeed",696},
    {"WasControllerOwner",709}, {"WeaponName",715}, {"Who",717}, {"WillFaceTarget",718}, {"Withdraw",719},
};
inline int keyId(const char* name)
{
    static std::mutex mx;
    static std::unordered_map<std::string, int> ids;
    std::lock_guard<std::mutex> lock(mx);
    if (ids.empty())
        for (const Key& k : KEYS30) ids.emplace(k.name, k.id);
    auto it = ids.find(name);
    if (it != ids.end()) return it->second;
    static int next = 0x100000;
    int id = next++;
    ids.emplace(name, id);
    if (g_log) g_log("sx hash key '%s': no 3.0 id paired, private id %#x", name, id);
    return id;
}
// A 1.0 key cell (string literal) as the 3.0 id cell; other cells (1.0 int keys) pass unchanged.
inline void keyCell(Cell& c)
{
    char s[128];
    if (c.type != 2 || !readStr(c.s, s, sizeof(s))) return;
    c.type = 0;
    c.i = keyId(s);
}
// HashFind/Int/Str (1.0 0xeed390, 3.0 0x13460d0): table, key, default -> found, value
inline void __cdecl hashFind(uint8_t* ctx) { Vm v{ctx}; keyCell(v.at(1)); call30(0x013460D0, ctx); }
// HashAdd/Int/Str (1.0 0xf05440, 3.0 0x1349d20): table, key, value -> ok
inline void __cdecl hashAdd(uint8_t* ctx) { Vm v{ctx}; keyCell(v.at(1)); call30(0x01349D20, ctx); }
// HashRemove (1.0 0xefbd20, 3.0 0x1349d70): table, key -> ok
inline void __cdecl hashRemove(uint8_t* ctx) { Vm v{ctx}; keyCell(v.at(0)); call30(0x01349D70, ctx); }
// HashFindInRange: 1.0 0xf05490 (table, name, default) looks up name+"Min" / name+"Max" (strings 0x12a733c/0x12a7338)
// and returns a random value between them, else name; 3.0 0x1345f60 does the same on ids: table, min, max, key, default.
inline void __cdecl hashFindInRange(uint8_t* ctx)
{
    Vm v{ctx};
    Cell def = v.pop(), name = v.pop(), table = v.pop();
    char s[128];
    int idMin = 0x7fffffff, idMax = 0x7fffffff, id = name.i;
    if (name.type == 2 && readStr(name.s, s, sizeof(s) - 4)) {
        id = keyId(s);
        size_t l = strlen(s);
        strcpy_s(s + l, 4, "Min"); idMin = keyId(s);
        strcpy_s(s + l, 4, "Max"); idMax = keyId(s);
    }
    v.push(table); v.pushInt(idMin); v.pushInt(idMax); v.pushInt(id); v.push(def);
    call30(0x01345F60, ctx);
}
// PauseAtEndWhenDoneChoreography: 1.0 0xedeb70 (cho) sets the cho's pause flags 0x30000|0x10000 (+0x148) or the
// instance state 5 (+0x65); 3.0 0x1383520 (cho, flag) does exactly that through 0xacc230 when flag is true (+0x1b4,
// +0x62) and clears them when false.
inline void __cdecl pauseAtEndWhenDoneCho(uint8_t* ctx) { Vm{ctx}.pushInt(1); call30(0x01383520, ctx); }
// ActorGetNamedLocatorPosition: 1.0 0x734090 (actor, name) -> locator position; 3.0 keeps it only as
// ActorGetNamedLocatorPositionAndFacing 0x759470 (actor, name) -> position, facing (pushed after the position).
inline void __cdecl actorGetNamedLocatorPosition(uint8_t* ctx) { call30(0x00759470, ctx); Vm{ctx}.top() -= 3; }
// Matrix3x3BuildFromVector: 1.0 0x70f2f0 builds with axes (2,1,1) fixed (0x415fe0); 3.0 0x733bc0 takes them from the
// script (vector, a, b, c -> 0x417e90(v, a, b, c), the same function).
inline void __cdecl matrix3x3BuildFromVector(uint8_t* ctx)
{
    Vm v{ctx};
    v.pushInt(2); v.pushInt(1); v.pushInt(1);
    call30(0x00733BC0, ctx);
}
// ReactionInfo_GetReactionScript: 1.0 0xedf6b0 (name, fourcc, variant) -> script; 3.0 0x137c4a0 (name, fourcc) dropped
// the variant (1.0 passed it on to 0xf7abc0 next to 1, 0, 0; 3.0's 0x152f160 takes name, fourcc, 1).
inline void __cdecl reactionInfoGetReactionScript(uint8_t* ctx) { Vm{ctx}.top() -= 1; call30(0x0137C4A0, ctx); }
// Combat_AttackStart: 1.0 0xae20e0 (actor, attack) -> nothing; 3.0 0xeb2020 does the same and pushes the result.
inline void __cdecl combatAttackStart(uint8_t* ctx) { call30(0x00EB2020, ctx); Vm{ctx}.top() -= 1; }

inline Native g_natives[] = {   // scratch\sx\sig.py + need.py -> gentab.py (name, args, results)
    {"ActorGetNamedLocatorPosition",2,3}, {"ActorIsExploder",1,2}, {"ActorTypeVars_GetLiveVarValue",2,1},
    {"ActorTypeVars_GetVarIndexFromName",1,1}, {"ActorTypeVars_SetLiveVarValue",3,0}, {"AipDetermineTarget",0,1},
    {"AipGetTargetPosition",0,3}, {"AipIsEnemy",1,1}, {"AipSetAlert",1,0}, {"AvatarInteractionHudData_AddUIText",7,0},
    {"AvatarInteractionHudData_RemoveUIText",3,0}, {"AvatarSetInvisibilityAlpha",2,0}, {"BHCheckPlayset",1,1},
    {"BHGetAccessoryTypeIndex",2,1}, {"BHGetAccessoryTypeVolatile",2,1}, {"BHNotifyTrafficQBOfHulk",1,0},
    {"BHSetAccessory",2,0}, {"BHSetAccessoryTypeIndex",3,0}, {"BHSetActorType",2,0}, {"BH_Activate_Super",1,0},
    {"BH_ClearMyTimer",1,0}, {"BH_Deactivate_Super",1,0}, {"BH_GetClosestPlayerVehicle",1,1}, {"BH_GetEnemyHits",1,1},
    {"BH_GetEnemyMaxDistance",1,1}, {"BH_GetEnemySpeed",1,1}, {"BH_GetIdleAnimEventName",1,1}, {"BH_GetMonsterLegs",1,1},
    {"BH_GetMoveToPoint_Waypoint",2,3}, {"BH_GetMyTimer",1,1}, {"BH_GetNoCarryFlag",1,1}, {"BH_GetSpawnPoint",1,3},
    {"BH_GetSplineEndPos",1,3}, {"BH_GetSplineStartPos",1,3}, {"BH_GetSuperStartFlag",1,1}, {"BH_MarkForDeathForced",2,0},
    {"BH_PauseMyTimer",1,0}, {"BH_RegisterSpecialPerform",9,0}, {"BH_SetActorContributionCulling",5,0},
    {"BH_SetAlpha",2,0}, {"BH_SetIdleAnimEventName",2,0}, {"BH_SetSplineEndPos",4,0}, {"BH_SetSplineStartPos",4,0},
    {"BH_SetSuperStartFlag",2,0}, {"BH_SetWalkAnimEventName",2,0}, {"BH_SpecialPerformEndAnimation",1,1},
    {"BH_SpecialPerformLoopAnimation",1,1}, {"BH_SpecialPerformOrient",1,3}, {"BH_SpecialPerformPosition",1,3},
    {"BH_SpecialPerformStartAnimation",1,1}, {"BH_StartMyTimer",1,0}, {"BH_UpdateStatBaseballComplete",0,0},
    {"BH_UpdateStatBrokenFixed",0,0}, {"BH_UpdateStatSinkyFixed",0,0}, {"BipedCOM_UseLedgeOBBCollision",9,0},
    {"BlockheadAreThereEnemies",0,1}, {"BlockheadCheckIfSplineOnExcludeList",1,1}, {"BlockheadCheckMaster",2,1},
    {"BlockheadCheckSmallAlienLeader",0,1}, {"BlockheadClearSmallAlienLeader",0,0}, {"BlockheadConstrainToNavMesh",7,3},
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
    {"Combat_GetShotAccuracyInnerRadius",1,1}, {"Combat_GetShotAccuracyOuterRadius",1,1}, {"Combat_GetShotsInBurst",1,1},
    {"Combat_GetShotsInSequence",1,1}, {"Combat_GetShouldPursue",1,1}, {"Combat_GetTauntChance",1,1},
    {"Combat_GetTeamNextAttackTime",2,1}, {"Combat_GetTrainChaseTarget",1,1}, {"Combat_GetTrainChaseTargetOffset",1,1},
    {"Combat_GetTrainLane",1,1}, {"Combat_GetTrainSpline",1,1}, {"Combat_GetTurnTakingInterruptMoving",1,1},
    {"Combat_GetUpdateAttackGoalTime",1,1}, {"Combat_GetUseDeceleration",1,1}, {"Combat_GetWaitToAttackChance",1,1},
    {"Combat_GetWaitToAttackLowerProb",1,1}, {"Combat_GetWaitToAttackMaxRange",1,1},
    {"Combat_GetWaitToAttackMaxTime",1,1}, {"Combat_GetWaitToAttackMinRange",1,1}, {"Combat_GetWaitToAttackMinTime",1,1},
    {"Combat_GetWaitToAttackUpperProb",1,1}, {"Combat_HasAttackTargetStoredPos",1,1}, {"Combat_HasAvoidanceGoalPos",1,1},
    {"Combat_InToybox",1,1}, {"Combat_IncrementWaitToAttackProb",1,0}, {"Combat_IsClosestInGroupToTarget",4,1},
    {"Combat_IsLargerThan",2,1}, {"Combat_IsNonParticipant",2,1}, {"Combat_IsRankedToAttackAndInAttackMode",1,1},
    {"Combat_IsRankingTimeoutActive",1,1}, {"Combat_KillCho",2,0}, {"Combat_NumAttacksInRange",1,1},
    {"Combat_QBForceUpdate",1,0}, {"Combat_RecoverAttackProbabilities",1,0}, {"Combat_ReleaseSetTeamNextAttackTime",3,0},
    {"Combat_RemoveCarriable",1,0}, {"Combat_SetAttackInterruptible",2,0}, {"Combat_SetAttackRange",2,0},
    {"Combat_SetAttackTargetStoredPos",4,0}, {"Combat_SetChaseTrainDirect",2,0}, {"Combat_SetChasingTrain",2,0},
    {"Combat_SetCurrentAttackLeaf",2,0}, {"Combat_SetHasAttackTargetStoredPos",2,0},
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
    {"Kraken_GetHealthLossInSwipe",1,1}, {"Kraken_GetHealthToAttackEnemyShip",1,1}, {"Kraken_GetKillAreaDistInFront",1,1},
    {"Kraken_GetKillAreaRadius",1,1}, {"Kraken_GetKrakenAttackPlayer",1,1}, {"Kraken_GetKrakenBaneTime",1,1},
    {"Kraken_GetKrakenHammerTarget",1,1}, {"Kraken_GetMinTimeBetweenEnemyAttacks",1,1}, {"Kraken_GetPrepareSpeed",1,1},
    {"Kraken_GetTimeApproaching",1,1}, {"Kraken_GetTimeUntilNextAttack",1,1}, {"Kraken_ResetKrakenHammerTarget",1,0},
    {"Matrix3x3BuildFromVector",3,9}, {"MoveCameraBehindPlayer",2,0}, {"MovementForceDSDestinationAndSpeed",5,0},
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
    {"UseNavMesh_GetPointOnMesh_Y",4,1}, {"VehicleManager_BipedInteractionConnect",3,0}, {"WallDetect_CanWallSlide",2,1},
    {"WallDetect_GetLastWallPointAndNormal",1,6}, {"WallJump_EnableStickRequirement",2,0},
    {"WallJump_GetWallJump_JumpToPoint",1,4}, {"WallJump_SetDisableTimer",2,0}, {"ZurgbotQB_GetMeleeCount",2,1},
    {"ZurgbotQB_GetRangedCount",2,1},
};
inline Native g_ports[] = {
    {"HashFind", 3, 2, &hashFind}, {"HashFindInt", 3, 2, &hashFind}, {"HashFindStr", 3, 2, &hashFind},
    {"HashAdd", 3, 1, &hashAdd}, {"HashAddInt", 3, 1, &hashAdd}, {"HashAddStr", 3, 1, &hashAdd},
    {"HashRemove", 2, 1, &hashRemove}, {"HashFindInRange", 3, 2, &hashFindInRange},
    {"PauseAtEndWhenDoneChoreography", 1, 0, &pauseAtEndWhenDoneCho},
    {"ActorGetNamedLocatorPosition", 2, 3, &actorGetNamedLocatorPosition},
    {"Matrix3x3BuildFromVector", 3, 9, &matrix3x3BuildFromVector},
    {"ReactionInfo_GetReactionScript", 3, 1, &reactionInfoGetReactionScript},
    {"Combat_AttackStart", 2, 0, &combatAttackStart},
};

inline std::unordered_map<std::string, Native*>& table()
{
    static std::unordered_map<std::string, Native*> t;
    return t;
}
// stubs for every listed native, ports over them (ports=false: stubs only; same-signature natives such as HashFind
// then stay on the engine's 3.0 version)
inline void init(bool ports)
{
    auto& t = table();
    for (Native& n : g_natives) t[n.name] = &n;
    if (ports)
        for (Native& n : g_ports) t[n.name] = &n;
}

// ---- SX-3: after the engine linked a script (FUN_00736da0), a served 1.0 script's imports go to the DLL's natives
// first; the engine's binding stays for the rest. Idempotent: the engine relinks cached scripts on every request.
inline void bind(uint8_t* hdr)
{
    int n = *(int*)(hdr + 0x2c);
    uint8_t* t = *(uint8_t**)(hdr + 0x54);
    if (n <= 0 || !t || rd(t + 20) != DI1) return;
    static std::mutex mx;
    static std::unordered_map<uint8_t*, std::string> names;
    uint32_t size = rd(hdr + 0xc), w3 = rd(t + 12);   // a fresh buffer: entry 0 word 3 = offset of the path
    std::string name;
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(mx);
        if (w3 && w3 < size) { names[hdr] = (const char*)hdr + w3; first = true; *(uint32_t*)(t + 12) = 0; }
        name = names[hdr];
    }
    int dll = 0, ported = 0, eng = 0, none = 0;
    std::string unbound;
    for (int i = 0; i < n; ++i) {
        uint8_t* e = t + 24 * i;
        const char* nm = *(const char**)e;
        auto it = nm ? table().find(nm) : table().end();
        if (it != table().end()) {
            Native* nat = it->second;
            *(Native**)(e + 12) = nat;
            *(NativeFn*)(e + 8) = nat->fn ? nat->fn : &stub;
            ++dll;
            ported += nat->fn != nullptr;
        } else if (*(void**)(e + 8)) ++eng;
        else { ++none; unbound += std::string(" ") + (nm ? nm : "?"); }
    }
    if (first && g_log)
        g_log("sx link %s: %d imports: %d engine, %d DI1 natives (%d ported, %d stubs), %d unbound%s", name.c_str(), n, eng,
              dll, ported, dll - ported, none, unbound.c_str());
}

// Detour of the linker FUN_00736da0 (thiscall: ECX = native registry [0x22040d4], arg = script header; ret 4).
typedef uint8_t(__fastcall* LinkFn)(void* reg, void* edx, uint8_t* hdr);
inline LinkFn g_link;
inline uint8_t __fastcall hkLink(void* reg, void*, uint8_t* hdr)
{
    uint8_t ok = g_link(reg, nullptr, hdr);
    if (ok) {
        try { bind(hdr); } catch (...) {}   // never unwind into engine code
    }
    return ok;
}

}  // namespace sx
