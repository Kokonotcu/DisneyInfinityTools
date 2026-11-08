// prankcombo10 / prankmission10: prank combos, PrankComboEnded, MissionTask_Pranked counts. mernode.
// Threads: Meridian (impulses, frame); graph load / unload (create, destroy).
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/mernode.hpp"
#include "../../formats/mer.hpp"
#include "config.hpp"

namespace di1::prankcombo {

// With c.prankCombo10: RegisterPrankCombo, PrankComboEnded, the frame hook (H1, H2); MissionTask_Pranked too.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kMerFrame = 0x006BC1F0;   // cdecl (dt): Meridian's frame, its updatables via FUN_006b6110
inline constexpr char kMerFrameBytes[] = "E8????????F30F10442404";
// an actor's type against a name, as ActorTypeImpulse (0x14863b0) tests it
constexpr uint32_t kTagFind = 0x0070F900;     // thiscall (kind, name) on *kTagDb -> tag or 0, ret 8
constexpr uint32_t kTagDb = 0x022039C8;       // data: the object kTagFind runs on
constexpr uint32_t kComponent = 0x0069ECD0;   // cdecl (handle, type) -> component or null
constexpr uint32_t kIdentType = 0x0228E4A8;   // data: u16, the component holding an actor's type tag
constexpr size_t kTypeTagAt = 0x20;           // that component: the actor's ActorType tag
constexpr uint32_t kTagMatch = 0x006F9B40;    // cdecl (actor tag, wanted tag) -> bool
}  // namespace eng

struct Call {
    uint32_t va;
    const char* bytes;
};
// The type test's calls, each checked before the first use (H4)
inline constexpr Call kTypeCalls[] = {
    {eng::kTagFind, "8B442404566A01508BF1E8"},
    {eng::kComponent, "8B5424088B0D????????8B018B4010"},
    {eng::kTagMatch, "8B5424045685D274288B7424"},
};

constexpr float kComboEnd = 2.5f;   // seconds after the last launch: 1.0 FUN_00b731e0 (DAT_01332960)

// Pins in the 1.0 graphs' numbering (describes FUN_00d8d1c0, FUN_00d60120)
enum ComboPin : int { kComboIn, kWho, kEnder };
enum EndedPin : int { kLinkOut, kEndedWho, kEndedEnder, kLaunchCount };
// MissionTask_Pranked's pins as its graphs name them (Trigger=0 .. Pranked=4; 1.0 describe FUN_00d62180 differs)
enum TaskPin : int { kTrigger, kTaskWho, kPrank, kTaskLaunches, kPranked };

// A running combo, as 1.0's SlotManager FUN_00b7ae90 keeps one per victim
struct Combo {
    int32_t who, ender;
    int launches;
    float last;   // State::now of its last launch
};

// PrankComboEnded's outputs (1.0 node +0x24 Who, +0x30 Ender, +0x3c LaunchCount)
struct EndedState {
    mernode::AgentPayload who, ender;
    mernode::IntElement launchCount;
};

// MissionTask_Pranked's outputs and its count (1.0 Eval_Pranked: PrankCount pranks of PrankedCharacter)
struct TaskState {
    mernode::AgentPayload who;
    mernode::IntElement prank, launches;   // Prank: the pranks counted so far
    int count;                             // Meridian thread only
};

typedef void(__cdecl* MerFrameFn)(float dt);

struct State {
    std::mutex mx;                     // guards combos, enders and tasks; never held across an engine call
    std::vector<Combo> combos;
    std::vector<uint8_t*> enders;      // live PrankComboEnded nodes
    std::vector<uint8_t*> tasks;       // live MissionTask_Pranked nodes (prankmission10)
    std::atomic<bool> any{false};      // combos is not empty: the per-frame early out (F1)
    float now = 0.0f;                  // seconds of Meridian frames; Meridian thread only
    int comboClass = -1, endedClass = -1, taskClass = -1;   // set once in install()
    bool typeTest = false;             // kTypeCalls matched: set once in install()
    MerFrameFn g_merFrame = nullptr;   // set once by installFeature() in install()
    core::LogOnce<int> told;           // its own leaf lock
};
inline State g_state;

// --- RegisterPrankCombo

// 1.0 0xd87e70 -> FUN_00b7ae90: one more launch of Who; the combo ends kComboEnd seconds after its last one.
inline void comboImpulse(uint8_t* node, int, void*)
{
    State& s = g_state;
    try {
        const int32_t who = mernode::fetchAgent(node, s.comboClass, kWho).value_or(0);
        const int32_t ender = mernode::fetchAgent(node, s.comboClass, kEnder).value_or(0);
        if (!who) {
            if (s.told.first(0)) core::logf("prankcombo10: RegisterPrankCombo without a Who; no combo");
            return;
        }
        int launches = 1;
        {
            std::lock_guard<std::mutex> lock(s.mx);
            auto it = std::find_if(s.combos.begin(), s.combos.end(), [who](const Combo& k) { return k.who == who; });
            if (it == s.combos.end()) s.combos.push_back({who, ender, 1, s.now});
            else {
                launches = ++it->launches;
                it->last = s.now;
                if (ender) it->ender = ender;
            }
            s.any = true;
        }
        core::logf("prankcombo10: combo of actor %d: launch %d, ender actor %d", who, launches, ender);
    } catch (...) {   // H6: bad_alloc only; this launch is not counted
    }
}

// 1.0 describe FUN_00d8d1c0; no parameters (node 0x30 B)
inline mernode::Spec comboSpec()
{
    return {"RegisterPrankCombo",
            {{"Link_In", mernode::In}, {"Who", mernode::DataV}, {"Ender", mernode::DataV}},
            {},
            0,
            &comboImpulse};
}

// --- PrankComboEnded

inline EndedState& endedState(uint8_t* node) { return *(EndedState*)mernode::stateOf(node); }

// The outputs of a combo of who (0: none yet, as 1.0's node ctor FUN_00d60180 leaves them)
inline void setEnded(uint8_t* node, int32_t who, int32_t ender, int launches)
{
    const mernode::Engine& e = mernode::g_state.engine;
    EndedState& es = endedState(node);
    es.who = {e.agentElemVt, e.agentType, who};
    es.ender = {e.agentElemVt, e.agentType, ender};
    es.launchCount = {e.intElemVt, e.intType, launches};
}

// --- node lists (enders, tasks): learnt at creation, forgotten before the memory goes (H11)

inline void remember(std::vector<uint8_t*> State::*list, uint8_t* node)
{
    State& s = g_state;
    try {
        std::lock_guard<std::mutex> lock(s.mx);
        (s.*list).push_back(node);
    } catch (...) {   // bad_alloc only; this node hears no combo end
    }
}

inline void forget(std::vector<uint8_t*> State::*list, uint8_t* node)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    (s.*list).erase(std::remove((s.*list).begin(), (s.*list).end(), node), (s.*list).end());
}

// Whether node is still in list (unload may run beside the frame)
inline bool alive(std::vector<uint8_t*> State::*list, uint8_t* node)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    return std::find((s.*list).begin(), (s.*list).end(), node) != (s.*list).end();
}

// A new PrankComboEnded node hears every combo end (1.0 0xd77ae0 registers for the message)
inline void endedCreate(uint8_t* node)
{
    setEnded(node, 0, 0, 0);
    remember(&State::enders, node);
}

inline void endedDestroy(uint8_t* node) { forget(&State::enders, node); }

// 1.0 getters 0xd57bf0 / 0x109ad90 / 0xd58a60: the last combo that ended
inline void* endedOut(uint8_t* node, int pin)
{
    EndedState& es = endedState(node);
    if (pin == kLaunchCount) return &es.launchCount;
    return pin == kEndedEnder ? &es.ender : &es.who;
}

// 1.0 describe FUN_00d60120; no input, no parameters (node 0x88 B)
inline mernode::Spec endedSpec()
{
    using mernode::DataOut;
    return {"PrankComboEnded",
            {{"Link_Out", mernode::Out}, {"Who", DataOut}, {"Ender", DataOut}, {"LaunchCount", DataOut}},
            {},
            sizeof(EndedState),
            nullptr,
            &endedDestroy,
            &endedOut,
            &endedCreate};
}

// --- MissionTask_Pranked (prankmission10)

inline TaskState& taskState(uint8_t* node) { return *(TaskState*)mernode::stateOf(node); }

inline void setTask(uint8_t* node, int32_t who, int launches)
{
    const mernode::Engine& e = mernode::g_state.engine;
    TaskState& ts = taskState(node);
    ts.who = {e.agentElemVt, e.agentType, who};
    ts.prank = {e.intElemVt, e.intType, ts.count};
    ts.launches = {e.intElemVt, e.intType, launches};
}

// 1.0 Eval_Pranked listened for GotPranked / PrankComboEnded (registration 0xb5cd91 / 0xb5cdb5)
inline void taskCreate(uint8_t* node)
{
    setTask(node, 0, 0);
    remember(&State::tasks, node);
}

inline void taskDestroy(uint8_t* node) { forget(&State::tasks, node); }

inline void* taskOut(uint8_t* node, int pin)
{
    TaskState& ts = taskState(node);
    if (pin == kPrank) return &ts.prank;
    return pin == kTaskLaunches ? (void*)&ts.launches : &ts.who;
}

// Whether actor h is of ActorType name (true when the test is off or the name unknown, as "ALL")
inline bool isType(int32_t h, const std::string& name)
{
    typedef void*(__fastcall * TagFindFn)(void* db, void* edx, const char* kind, const char* name);
    typedef uint8_t*(__cdecl * ComponentFn)(int32_t handle, uint32_t type);
    typedef bool(__cdecl * TagMatchFn)(void* have, void* want);
    if (!g_state.typeTest || name.empty() || name == "ALL") return true;
    void* want = ((TagFindFn)core::rt(eng::kTagFind))(*(void**)core::rt(eng::kTagDb), nullptr, "ActorType",
                                                       name.c_str());
    if (!want) return true;
    const uint32_t type = *(const uint16_t*)core::rt(eng::kIdentType);
    const uint8_t* comp = ((ComponentFn)core::rt(eng::kComponent))(h, type);
    void* have = comp ? *(void* const*)(comp + eng::kTypeTagAt) : nullptr;
    return have && ((TagMatchFn)core::rt(eng::kTagMatch))(have, want);
}

// One ended combo of the wanted character counted as a prank (1.0 Eval_Pranked FUN_00b547d0); Pranked fires
inline void countPrank(uint8_t* node, const Combo& k)
{
    TaskState& ts = taskState(node);
    const std::string wanted = mernode::strParam(node, "PrankedCharacter");
    if (!isType(k.who, wanted)) return;
    const int need = (int)mernode::rawParam(node, "PrankCount", 1);
    ++ts.count;
    setTask(node, k.who, k.launches);
    const std::string id = mernode::strParam(node, "MissionID");
    const std::string ender = mernode::strParam(node, "Ender");
    core::logf("prankmission10: %s: prank %d of %d (wanted %s by %s; actor %d, %d launches)%s", id.c_str(), ts.count,
               need, wanted.c_str(), ender.c_str(), k.who, k.launches,
               ts.count == need ? "; done, but 3.0's mission system does not see this task" : "");
    mernode::fireAgent(node, g_state.taskClass, kPranked, k.who);
}

// Pins as the 11 MU graphs number them; parameters the Eval_Pranked fields they carry
inline mernode::Spec taskSpec()
{
    using mernode::DataOut;
    return {"MissionTask_Pranked",
            {{"Trigger", mernode::Data}, {"Who", DataOut}, {"Prank", DataOut}, {"LaunchCount", DataOut},
             {"Pranked", mernode::Out}},
            {{"Type", ::mer::T_STRING}, {"PrankedCharacter", ::mer::T_STRING}, {"Ender", ::mer::T_STRING},
             {"PrankCount", ::mer::T_INT}, {"PrankZone", ::mer::T_INT}, {"HideProgress", ::mer::T_BOOL},
             {"MissionID", ::mer::T_STRING}, {"EvalPriority", ::mer::T_INT}},
            sizeof(TaskState),
            nullptr,
            &taskDestroy,
            &taskOut,
            &taskCreate};
}

// --- the frame

// One ended combo through every PrankComboEnded (Link_Out with Who, 1.0 0xd603b0) and every pranked task
inline void announce(const Combo& k, const std::vector<uint8_t*>& enders, const std::vector<uint8_t*>& tasks)
{
    core::logf("prankcombo10: combo of actor %d ended: %d launches, ender actor %d; %u PrankComboEnded nodes", k.who,
               k.launches, k.ender, (unsigned)enders.size());
    for (uint8_t* n : enders) {
        if (!alive(&State::enders, n)) continue;
        setEnded(n, k.who, k.ender, k.launches);
        mernode::fireAgent(n, g_state.endedClass, kLinkOut, k.who);
    }
    for (uint8_t* n : tasks)
        if (alive(&State::tasks, n)) countPrank(n, k);
}

// 1.0 FUN_00b731e0: combos quiet for kComboEnd seconds end and are announced
inline void tick(float dt)
{
    State& s = g_state;
    s.now += dt;
    if (!s.any) return;
    std::vector<Combo> done;
    std::vector<uint8_t*> enders, tasks;
    {
        std::lock_guard<std::mutex> lock(s.mx);
        const float now = s.now;
        const auto quiet = [now](const Combo& k) { return now - k.last > kComboEnd; };
        std::copy_if(s.combos.begin(), s.combos.end(), std::back_inserter(done), quiet);
        s.combos.erase(std::remove_if(s.combos.begin(), s.combos.end(), quiet), s.combos.end());
        s.any = !s.combos.empty();
        if (!done.empty()) {
            enders = s.enders;
            tasks = s.tasks;
        }
    }
    for (const Combo& k : done) announce(k, enders, tasks);
}

// FUN_006bc1f0, cdecl (dt): Meridian's frame, Meridian thread, per frame. Stands in for 1.0's combo update.
inline void __cdecl hkMerFrame(float dt)
{
    g_state.g_merFrame(dt);
    try {
        tick(dt);
    } catch (...) {   // H6: bad_alloc only; the combos stay for the next frame
    }
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.prankCombo10) return true;
    const core::HookSpec frame{"Meridian frame (prankcombo10)", detail::eng::kMerFrame, detail::eng::kMerFrameBytes,
                               (void*)&detail::hkMerFrame, (void**)&s.g_merFrame};
    if (!core::installFeature("prankcombo10", {frame})) return false;
    s.comboClass = mernode::add(detail::comboSpec());
    s.endedClass = mernode::add(detail::endedSpec());
    if (c.prankMission10) s.taskClass = mernode::add(detail::taskSpec());
    s.typeTest = true;
    for (const detail::Call& k : detail::kTypeCalls) {
        if (core::prologueMatches(k.va, k.bytes)) continue;
        core::logf("prankmission10: %08X differs from the analysed bytes; every victim counts", k.va);
        s.typeTest = false;
        break;
    }
    const bool taskMissing = c.prankMission10 && s.taskClass < 0;
    const bool comboMissing = s.comboClass < 0 || s.endedClass < 0;
    if (comboMissing || taskMissing) core::logf("prankcombo10: a class not added (native class table full)");
    return true;
}

}  // namespace di1::prankcombo
