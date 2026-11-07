// combat10: 1.0 Combat state 3.0 dropped, per 3.0 component; DNA tuning via 3.0's parser hook. Loader, script threads.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"

namespace di1::combat10 {

struct Vec3 {
    float x, y, z;
};
// A 1.0 tuned value: Normal, Small, Large (1.0 0xa92190 slots; 3.0 0xfee1b0 the same).
struct Variant {
    float v[3];
};

constexpr int kProps10 = 57;   // 1.0 attack properties: FUN_00c700f0's names 0..0x38
constexpr int kAnimEvents10 = 7;   // AnimTreeEvent_* keys 1.0's parser stores (+0x3f8..+0x410); slots to 30 stay ""

// One 1.0 attack's properties straight from its DNA table, by 1.0 property id.
struct Attack10 {
    float prop[kProps10] = {};
};

// The 1.0 fields: constructor FUN_00b36190 defaults, tuning from the DNA (parser FUN_00b2a060, pursue FUN_00aa28e0).
struct Side {
    uint8_t idleInterruptible = 1;   // 1.0 +0x949 (ctor writes word 0x100 at +0x948)
    uint8_t attackInterruptible = 0;   // +0x948
    uint8_t hasStoredPos = 0;   // +0x32c
    uint8_t waiting = 0;   // +0x5bc: waiting to attack (CombatQB_GetAnyWaitingToAttack reads it)
    uint8_t hasAvoidGoal = 0;   // +0x2d8
    uint8_t hasChosenPoint = 0;   // +0x758
    uint8_t attackFailedInvalid = 0;   // +0xa34
    uint8_t pursuePending = 0;   // CombatPursue +0x8
    uint8_t approachFirst = 1;   // +0x8dc: ShouldApproachToAttack's first call answers no
    Vec3 storedPos{}, lastGoalPos{}, inWay{};   // +0x314, +0x348, +0x824
    int lastGoalType = -1, attackLeaf = 0, moveMethod = -1, avoidTarget = 0;   // +0x340, +0x7e0, +0x768, +0x2dc
    int attackCounter = 0, pending = -1, checked = -1;   // +0x9c0, +0x5d4, +0x5cc
    float waitingSince = 0, waitDone = 0, updateGoalTime = 0, avoidRadius = 0;   // +0x5c0, +0x770, +0x2ec, +0x2d4
    float lowerProb = 0, upperProb = 1.0f, scale = 1.0f;   // +0x654, +0x650, +0x818 (only the ctor writes it)
    Variant taunt{{0.125f, 0.125f, 0.125f}}, celebrate{{0.125f, 0.125f, 0.125f}};   // +0x6b8, +0x6c8
    Variant frustrated{{0.125f, 0.125f, 0.125f}}, defer{{0.5f, 0.5f, 0.5f}};   // +0x6d8, +0x774
    Variant minMove{{1, 1, 1}}, idleChance{{0.125f, 0.125f, 0.125f}};   // +0x728, +0x784
    Variant idleMin{{0.5f, 0.5f, 0.5f}}, idleMax{{1, 1, 1}};   // +0x6e8, +0x6f8
    Variant waitChance{}, waitDelta{}, waitMinRange{}, waitMaxRange{}, waitMinTime{}, waitMaxTime{};   // +0x640..
    Variant pursueDist{{40, 40, 40}}, breakOff{{100, 100, 100}};   // CombatPursue +0x28, +0x38 (ctor FUN_00aa2820)
    Variant relativeSize{{10, 10, 10}};   // +0x984, an int variant
    Variant minModTimer{{1, 1, 1}}, maxModTimer{{2, 2, 2}};   // +0x9c8, +0x9cc (plain floats: slot 0)
    uint8_t useDecel[3] = {};   // +0x880
    uint8_t canBypassStop = 1, canDoAvoidance = 0, canPursueDistance = 0, canPursueNavMesh = 0;   // +0x3a2.. +0x3ac
    uint8_t willReact = 1, turnInterruptMoving = 0;   // +0x7d4, +0x804
    std::vector<Attack10> attacks;   // in the DNA's Attacks order
    const char* animEvents[kAnimEvents10] = {"", "", "", "", "", "", ""};   // interned, never freed (S5)
    std::unordered_map<int, float> teamRecord;   // +0x13c: target -> the next attack time this actor set
};

// Hooks 3.0's Combat parser after checking every engine function sxcombat calls (H2, H4); false: none installed.
inline bool install();

// A 1.0 variant as 1.0's 0xa92210 picks it; 3.0 keeps the actor's size class at Combat +0x678.
inline float pick(const Variant& v, int sizeClass);
// FUN_00aa00e0 / FUN_00aa0180: the wait-to-attack window moves by delta, bounded by chance (1.0's quirks kept).
inline void decrementWaitProb(Side& s, float chance, float delta);
inline void incrementWaitProb(Side& s, float chance, float delta);
// FUN_00ace890: me within radius of a teammate's path to its target, under 40 m ahead; *inWay gets the perpendicular.
inline bool inWayOf(const Vec3& me, const Vec3& mate, const Vec3& target, float radius, Vec3* inWay);

namespace eng {
// Engine data and functions; install() checks every called prologue below (H4).
constexpr uint32_t kComponentOf = 0x0069ECD0;     // FUN_0069ecd0, cdecl (actor, type word) -> component or null
constexpr uint32_t kCombatType = 0x02224020;      // word: Combat's component type (registered by FUN_006a5d20)
constexpr uint32_t kPursueType = 0x02224DB0;      // word: CombatPursue's type
constexpr uint32_t kRankingType = 0x02224DC4;     // word: CombatRanking's type
constexpr uint32_t kGameTime = 0x02201910;        // float: game time (3.0 FUN_00fefa20 stamps it; 1.0 0x167dfa0)
constexpr uint32_t kParse = 0x0104FCF0;           // FUN_0104fcf0, cdecl (component, AgentGadgets ref) Combat parser
constexpr uint32_t kField = 0x004510B0;           // thiscall (ref; out, name) -> out: a table's field
constexpr uint32_t kIsTable = 0x00428F50;         // thiscall (ref) -> its value is a table
constexpr uint32_t kLength = 0x00749180;          // thiscall (ref) -> the table's array length
constexpr uint32_t kIndex = 0x0063DC40;           // thiscall (ref; out, i) -> out: element i (1-based)
constexpr uint32_t kText = 0x00445160;            // thiscall (ref) -> lua_tolstring
constexpr uint32_t kType = 0x0130D2D0;            // lua_type, cdecl (L, index)
constexpr uint32_t kRelease = 0x0130E4D0;         // cdecl (L, index): drops a ref
constexpr uint32_t kReadNumber = 0x00FF3FC0;      // cdecl (name, Variant*, table ref) -> found (number or N/S/L)
constexpr uint32_t kReadBool = 0x00FF3F00;        // cdecl (name, bool*, table ref) -> found
constexpr uint32_t kActorMap = 0x02202A38;        // the handle -> node map ActorGetPosition 0x74b790 reads
constexpr uint32_t kActorFind = 0x00492E20;       // thiscall (map; out, &handle) -> entry, node at +8
constexpr uint32_t kMovedType = 0x0228E4AC;       // word: node type whose position lives at +0x268
constexpr uint32_t kMovedRefresh = 0x01887040;    // thiscall (node): refreshes that position (flag +0x51 bit 0)
constexpr int kLuaString = 4;                     // lua_type's string
constexpr size_t kNodeType = 6, kNodeTypeMask = 0x3ff, kNodeFlags = 0x51;
constexpr size_t kNodePos = 0x2c, kMovedPos = 0x268;
}  // namespace eng

namespace detail {

struct LuaRef {
    void* L;
    int index;
};
struct Callee {
    uint32_t va;
    const char* prologue;
};

typedef uint8_t*(__cdecl* ComponentFn)(int actor, int type);
typedef void*(__cdecl* ParseFn)(void* comp, const LuaRef* gadgets);
typedef LuaRef*(__fastcall* FieldFn)(const LuaRef* t, void* edx, LuaRef* out, const char* name);
typedef bool(__fastcall* IsTableFn)(const LuaRef* t, void* edx);
typedef unsigned(__fastcall* LengthFn)(const LuaRef* t, void* edx);
typedef LuaRef*(__fastcall* IndexFn)(const LuaRef* t, void* edx, LuaRef* out, int i);
typedef const char*(__fastcall* TextFn)(const LuaRef* r, void* edx);
typedef int(__cdecl* TypeFn)(void* L, int index);
typedef void(__cdecl* ReleaseFn)(void* L, int index);
typedef bool(__cdecl* ReadNumberFn)(const char* name, Variant* out, const LuaRef* t);
typedef bool(__cdecl* ReadBoolFn)(const char* name, bool* out, const LuaRef* t);
typedef uint8_t*(__fastcall* ActorFindFn)(void* map, void* edx, void* out, const int* handle);
typedef void(__fastcall* RefreshFn)(void* node, void* edx);

// 1.0 attack property names in id order (FUN_00c700f0).
inline const char* const PROPS10[kProps10] = {
    "MinRange", "MaxRange", "MinApproachRange", "MaxApproachRange", "AttackProbability_InRange",
    "AttackProbability_OutRange", "ApproachProbability_InRange_PostMove", "ApproachProbability_InRange_PostAttack",
    "ApproachForwardOnly", "MustApproach", "AttackProbabilityDegrade", "AttackProbabilityRecover",
    "AttackProbabilityRecoverTime", "AttackWaitTimeAdjust", "ApproachTimeout", "RetreatDist", "RepeatProbability",
    "RetreatProbability", "AttackTimeLength", "PostAttackTimeLength", "NavigationRequiredToTarget",
    "UpdateTargetGoalDist", "RecoilVelocity", "MustSeeTarget", "AbortMinDist", "AbortMaxDist", "FacingToAttack",
    "InterruptToAttack", "HalfCircleSteps", "ReactingTimeOverride", "AttackRankingLockedTime",
    "PostAttackRankingLockedTime", "MoveBufferDist", "PreAttackStop", "PreAttackSkipPlayingStop", "AttackPriority",
    "CooldownProbability", "CooldownTimeMin", "CooldownTimeMax", "MovingCooldownProbability",
    "MovingCooldownTimeMin", "MovingCooldownTimeMax", "MaxHeightDelta", "MustTakeTurns", "TakeTurnLength",
    "ApproachLateralProbability", "ApproachAdvanceOnly", "ApproachDirectOnly", "PreAttackIdleProbability",
    "PreAttackIdleTimeMin", "PreAttackIdleTimeMax", "NumInSequence", "SequenceDelay", "AccuracyMin",
    "AccuracyMax", "AccuracySkew", "AccuracyDelta",
};
// 1.0's parser keys for the anim event table, in slot order (parse10 offsets +0x3f8..+0x410).
inline const char* const ANIM_EVENTS10[kAnimEvents10] = {
    "AnimTreeEvent_AttackMoveStart", "AnimTreeEvent_AttackMoveFastStart", "AnimTreeEvent_AttackMoveStop",
    "AnimTreeEvent_AttackIdle", "AnimTreeEvent_PatrolMoveStart", "AnimTreeEvent_PatrolMoveStop",
    "AnimTreeEvent_PatrolIdle",
};

struct NumberKey {
    const char* key;
    Variant Side::*field;
};
struct FlagKey {
    const char* key;
    uint8_t Side::*field;
};
// 1.0 keys 3.0's parser drops or reads under another name; all from the Combat table.
inline const NumberKey NUMBER_KEYS[] = {
    {"Taunt_Chance", &Side::taunt}, {"CelebrateTaunt_Chance", &Side::celebrate},
    {"FrustratedTaunt_Chance", &Side::frustrated}, {"DeferIfOthersWaitingToAttack_Chance", &Side::defer},
    {"Min_MoveInterval", &Side::minMove}, {"WaitToAttack_Chance", &Side::waitChance},
    {"WaitToAttack_Delta", &Side::waitDelta}, {"WaitToAttack_MinRange", &Side::waitMinRange},
    {"WaitToAttack_MaxRange", &Side::waitMaxRange}, {"WaitToAttack_MinTime", &Side::waitMinTime},
    {"WaitToAttack_MaxTime", &Side::waitMaxTime}, {"AttackIdle_Chance", &Side::idleChance},
    {"AttackIdle_MinTime", &Side::idleMin}, {"AttackIdle_MaxTime", &Side::idleMax},
    {"PursueDist", &Side::pursueDist}, {"BreakOffDist", &Side::breakOff}, {"RelativeSize", &Side::relativeSize},
    {"MinAttackModTimerInterval", &Side::minModTimer}, {"MaxAttackModTimerInterval", &Side::maxModTimer},
};
inline const FlagKey FLAG_KEYS[] = {
    {"CanBypassStop", &Side::canBypassStop}, {"CanDoAvoidance", &Side::canDoAvoidance},
    {"CanPursueDistance", &Side::canPursueDistance}, {"CanPursueNavMesh", &Side::canPursueNavMesh},
    {"WillReactToCollisions", &Side::willReact}, {"TakingTurnsInterruptMoving", &Side::turnInterruptMoving},
};

// Every engine function combat10 and sxcombat call, checked once in install().
inline const Callee CALLEES[] = {
    {eng::kComponentOf, "8B5424088B0D"}, {eng::kField, "518B44240C568B74240C"}, {eng::kIsTable, "8B0185C0750332C0"},
    {eng::kLength, "83EC1C568BF18B06"}, {eng::kIndex, "518B44240C568B74240C"}, {eng::kText, "568BF18B0685C0740D"},
    {eng::kType, "8B4C24088B542404"}, {eng::kRelease, "53BB807BE1FF2B5C240C"},
    {eng::kReadNumber, "83EC1C8B44242053"}, {eng::kReadBool, "8B44240483EC0856"},
    {eng::kActorFind, "8B4424088B0085C0"}, {eng::kMovedRefresh, "568BF1F6460C20"},
};

constexpr uint32_t kSeedMul = 1664525u, kSeedAdd = 1013904223u;   // Numerical Recipes LCG
constexpr float kSeedScale = 1.0f / 4294967296.0f;
constexpr float kEpsilon = 1.1754944e-38f;   // FLT_MIN, 1.0's guard before 1/length
constexpr float kAheadLimit = 40.0f;         // FUN_00ace890: only the first 40 m ahead of a teammate count
constexpr int kSmall = 2, kLarge = 3;        // 3.0 Combat +0x678 size classes (FUN_00ff6390)

struct State {
    std::mutex mx;                                            // guards everything below except g_parse
    std::unordered_map<const void*, Side> sides;              // 3.0 Combat component -> its 1.0 state
    std::unordered_map<const void*, std::unordered_map<int, float>> teamTimes;   // QB -> target -> next attack
    std::unordered_set<std::string> names;                    // S5: interned strings scripts may keep
    uint32_t seed = 1;                                        // the dice of ShouldApproachToAttack
    ParseFn g_parse = nullptr;                                // set once by installFeature
};
inline State g_state;

template <typename F>
F fn(uint32_t va)
{
    return (F)core::rt(va);
}

inline void release(LuaRef& r)
{
    if (r.L) fn<ReleaseFn>(eng::kRelease)(r.L, r.index);
    r.L = nullptr;
}

// A field of t as a fresh ref; the caller releases it.
inline LuaRef field(const LuaRef& t, const char* name)
{
    LuaRef out{};
    fn<FieldFn>(eng::kField)(&t, nullptr, &out, name);
    return out;
}

inline bool isTable(const LuaRef& r) { return r.L && fn<IsTableFn>(eng::kIsTable)(&r, nullptr); }

// r's string, or "" when r holds none.
inline std::string text(const LuaRef& r)
{
    if (!r.L || fn<TypeFn>(eng::kType)(r.L, r.index) != eng::kLuaString) return std::string();
    const char* s = fn<TextFn>(eng::kText)(&r, nullptr);
    return s ? std::string(s) : std::string();
}

inline void readTuning(const LuaRef& combat, Side& s)
{
    for (const NumberKey& k : NUMBER_KEYS) fn<ReadNumberFn>(eng::kReadNumber)(k.key, &(s.*k.field), &combat);
    for (const FlagKey& k : FLAG_KEYS) {
        bool b = s.*k.field != 0;
        if (fn<ReadBoolFn>(eng::kReadBool)(k.key, &b, &combat)) s.*k.field = b;
    }
    bool decel = false;
    if (fn<ReadBoolFn>(eng::kReadBool)("Combat_UseDeceleration", &decel, &combat))
        for (uint8_t& d : s.useDecel) d = decel;
}

inline Attack10 readAttack(const LuaRef& attack)
{
    Attack10 a;
    for (int p = 0; p < kProps10; ++p) {
        Variant v{};
        if (fn<ReadNumberFn>(eng::kReadNumber)(PROPS10[p], &v, &attack)) a.prop[p] = v.v[0];
    }
    return a;
}

// Combat.Attacks names each attack's table in Combat, in the order 3.0's parser loops them.
inline void readAttacks(const LuaRef& combat, Side& s)
{
    LuaRef list = field(combat, "Attacks");
    const unsigned n = isTable(list) ? fn<LengthFn>(eng::kLength)(&list, nullptr) : 0;
    for (unsigned i = 1; i <= n; ++i) {
        LuaRef name{};
        fn<IndexFn>(eng::kIndex)(&list, nullptr, &name, (int)i);
        const std::string key = text(name);
        LuaRef attack = key.empty() ? LuaRef{} : field(combat, key.c_str());
        s.attacks.push_back(isTable(attack) ? readAttack(attack) : Attack10{});
        release(attack);
        release(name);
    }
    release(list);
}

inline void readAnimEvents(const LuaRef& combat, Side& s)
{
    std::string names[kAnimEvents10];
    for (int i = 0; i < kAnimEvents10; ++i) {
        LuaRef r = field(combat, ANIM_EVENTS10[i]);
        names[i] = text(r);
        release(r);
    }
    std::lock_guard<std::mutex> lock(g_state.mx);
    for (int i = 0; i < kAnimEvents10; ++i) s.animEvents[i] = g_state.names.insert(names[i]).first->c_str();
}

// A new component's 1.0 state: ctor defaults plus its DNA, as 1.0's ctor and parser built it.
inline void capture(const void* comp, const LuaRef& gadgets)
{
    LuaRef combat = field(gadgets, "Combat");
    if (!isTable(combat)) {
        release(combat);
        return;
    }
    Side s;
    readTuning(combat, s);
    readAttacks(combat, s);
    readAnimEvents(combat, s);
    release(combat);
    std::lock_guard<std::mutex> lock(g_state.mx);
    g_state.sides[comp] = std::move(s);
}

// FUN_0104fcf0, cdecl (Combat component, AgentGadgets ref), loader thread, once per Combat component's creation.
inline void* __cdecl hkParse(void* comp, const LuaRef* gadgets)
{
    void* r = g_state.g_parse(comp, gadgets);
    try {
        if (comp && gadgets) capture(comp, *gadgets);
    } catch (const std::exception& e) {
        core::logf("combat10: 1.0 Combat tuning not kept: %s", e.what());
    }
    return r;
}

}  // namespace detail

// The component of the given type word, or null.
inline uint8_t* component(int actor, uint32_t typeWordVa)
{
    const int type = *(const uint16_t*)core::rt(typeWordVa);
    return actor ? detail::fn<detail::ComponentFn>(eng::kComponentOf)(actor, type) : nullptr;
}

inline uint8_t* combat(int actor) { return component(actor, eng::kCombatType); }

inline float gameTime() { return *(const float*)core::rt(eng::kGameTime); }

// An actor's position as ActorGetPosition (0x74b790) reads it; false for an unknown handle.
inline bool actorPos(int handle, Vec3* out)
{
    uint8_t buf[16] = {};
    void* map = *(void**)core::rt(eng::kActorMap);
    uint8_t* entry = handle ? detail::fn<detail::ActorFindFn>(eng::kActorFind)(map, nullptr, buf, &handle) : nullptr;
    uint8_t* node = entry ? *(uint8_t**)(entry + 8) : nullptr;
    if (!node) return false;
    const uint16_t moved = *(const uint16_t*)core::rt(eng::kMovedType);
    const bool movedNode = (*(uint16_t*)(node + eng::kNodeType) & eng::kNodeTypeMask) == moved;
    if (movedNode && (node[eng::kNodeFlags] & 1)) detail::fn<detail::RefreshFn>(eng::kMovedRefresh)(node, nullptr);
    *out = *(const Vec3*)(node + (movedNode ? eng::kMovedPos : eng::kNodePos));
    return true;
}

// A uniform draw in [0, 1); the caller holds g_state.mx.
inline float dice()
{
    uint32_t& s = detail::g_state.seed;
    s = s * detail::kSeedMul + detail::kSeedAdd;
    return (float)s * detail::kSeedScale;
}

inline float pick(const Variant& v, int sizeClass)
{
    if (sizeClass == detail::kSmall) return v.v[1];
    return sizeClass == detail::kLarge ? v.v[2] : v.v[0];
}

inline void decrementWaitProb(Side& s, float chance, float delta)
{
    s.lowerProb += delta;
    if (s.lowerProb > 1.0f) s.lowerProb = 1.0f;
    if (chance < s.lowerProb) s.lowerProb = chance;
    s.upperProb += delta + delta;
    // 1.0 0xaa0138 clamps the lower bound here, not the upper: kept
    if (s.upperProb > 1.0f) s.lowerProb = 1.0f;
}

inline void incrementWaitProb(Side& s, float chance, float delta)
{
    if (s.upperProb < 0) s.upperProb = 0;
    if (s.upperProb < chance) s.upperProb = chance;
    s.lowerProb -= delta + delta;
    if (s.lowerProb < 0) s.lowerProb = 0;
}

inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 scaled(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }

inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline float inverseLength(const Vec3& a) { return 1.0f / (std::max)(length(a), detail::kEpsilon); }

inline bool inWayOf(const Vec3& me, const Vec3& mate, const Vec3& target, float radius, Vec3* inWay)
{
    const Vec3 d = sub(target, mate);
    const float len = length(d);
    const Vec3 n = scaled(d, inverseLength(d));
    const float ahead = dot(sub(me, mate), n);
    const bool between = ahead >= 0 && ahead < detail::kAheadLimit && ahead < len;
    if (!between) return false;
    const Vec3 closest = {mate.x + n.x * ahead, mate.y + n.y * ahead, mate.z + n.z * ahead};
    const Vec3 perp = sub(me, closest);
    if (!(length(perp) < radius)) return false;
    *inWay = scaled(perp, inverseLength(perp));
    return true;
}

inline bool install()
{
    for (const detail::Callee& c : detail::CALLEES) {
        if (core::prologueMatches(c.va, c.prologue)) continue;
        core::logf("feature sxcombat10 off: FUN_%08x's prologue differs", c.va);
        return false;
    }
    return core::installFeature("sxcombat10", {
        {"Combat parser (sxcombat10)", eng::kParse, "558BEC83E4C06AFF68????????64A100000000",
         (void*)&detail::hkParse, (void**)&detail::g_state.g_parse},
    });
}

}  // namespace di1::combat10
