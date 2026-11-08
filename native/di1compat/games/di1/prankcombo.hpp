// prankcombo10: 1.0 prank combos (RegisterPrankCombo) and their end (PrankComboEnded). mernode.
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

// Adds RegisterPrankCombo and PrankComboEnded and hooks Meridian's frame when c.prankCombo10 is on (H1, H2).
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kMerFrame = 0x006BC1F0;   // cdecl (dt): Meridian's frame, its updatables via FUN_006b6110
inline constexpr char kMerFrameBytes[] = "E8????????F30F10442404";
}  // namespace eng

constexpr float kComboEnd = 2.5f;   // seconds after the last launch: 1.0 FUN_00b731e0 (DAT_01332960)

// Pins in the 1.0 graphs' numbering (describes FUN_00d8d1c0, FUN_00d60120)
enum ComboPin : int { kComboIn, kWho, kEnder };
enum EndedPin : int { kLinkOut, kEndedWho, kEndedEnder, kLaunchCount };

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

typedef void(__cdecl* MerFrameFn)(float dt);

struct State {
    std::mutex mx;                     // guards combos and enders; never held across an engine call
    std::vector<Combo> combos;
    std::vector<uint8_t*> enders;      // live PrankComboEnded nodes
    std::atomic<bool> any{false};      // combos is not empty: the per-frame early out (F1)
    float now = 0.0f;                  // seconds of Meridian frames; Meridian thread only
    int comboClass = -1, endedClass = -1;   // set once in install()
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

// A new PrankComboEnded node hears every combo end (1.0 0xd77ae0 registers for the message)
inline void endedCreate(uint8_t* node)
{
    State& s = g_state;
    setEnded(node, 0, 0, 0);
    try {
        std::lock_guard<std::mutex> lock(s.mx);
        s.enders.push_back(node);
    } catch (...) {   // bad_alloc only; this node hears no combo end
    }
}

// Before a PrankComboEnded node goes: no combo end reaches it any more (H11)
inline void endedDestroy(uint8_t* node)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    s.enders.erase(std::remove(s.enders.begin(), s.enders.end(), node), s.enders.end());
}

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

// --- the frame

// Whether node is still a live PrankComboEnded (unload may run beside the frame)
inline bool liveEnder(uint8_t* node)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    return std::find(s.enders.begin(), s.enders.end(), node) != s.enders.end();
}

// One ended combo through every PrankComboEnded: outputs set, Link_Out fired with Who (1.0 0xd603b0)
inline void announce(const Combo& k, const std::vector<uint8_t*>& nodes)
{
    core::logf("prankcombo10: combo of actor %d ended: %d launches, ender actor %d; %u PrankComboEnded nodes", k.who,
               k.launches, k.ender, (unsigned)nodes.size());
    for (uint8_t* n : nodes) {
        if (!liveEnder(n)) continue;
        setEnded(n, k.who, k.ender, k.launches);
        mernode::fireAgent(n, g_state.endedClass, kLinkOut, k.who);
    }
}

// 1.0 FUN_00b731e0: combos quiet for kComboEnd seconds end and are announced
inline void tick(float dt)
{
    State& s = g_state;
    s.now += dt;
    if (!s.any) return;
    std::vector<Combo> done;
    std::vector<uint8_t*> nodes;
    {
        std::lock_guard<std::mutex> lock(s.mx);
        const float now = s.now;
        const auto quiet = [now](const Combo& k) { return now - k.last > kComboEnd; };
        std::copy_if(s.combos.begin(), s.combos.end(), std::back_inserter(done), quiet);
        s.combos.erase(std::remove_if(s.combos.begin(), s.combos.end(), quiet), s.combos.end());
        s.any = !s.combos.empty();
        if (!done.empty()) nodes = s.enders;
    }
    for (const Combo& k : done) announce(k, nodes);
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
    if (s.comboClass < 0 || s.endedClass < 0) core::logf("prankcombo10: a class not added (native class table full)");
    return true;
}

}  // namespace di1::prankcombo
