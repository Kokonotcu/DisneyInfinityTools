// delivery10, decosockets10, prank10, prankcombo10: 1.0 classes whose system 3.0 lacks; links kept, inputs logged.
// Meridian thread.
#pragma once
#include <cstdint>
#include "../../core/log.hpp"
#include "../../engine/mernode.hpp"
#include "../../formats/mer.hpp"
#include "config.hpp"

namespace di1::inertnodes {

// Adds DeliveryNode (delivery10), the decoration socket classes (decosockets10), the prank combo classes (prank10 /
// prankcombo10).
inline void install(const Config& c);

namespace detail {

// The node's own state: OutLocatorIndex's DataElement (1.0 node +0x9c, a DataElement<long>)
struct DeliveryState {
    mernode::IntElement outIndex;
};

// What a log line is about: one key each for LogOnce
enum Told : int { kDeliveryInput, kSpawnSockets, kActivateSockets, kPrankCombo, kComboEnded };

struct State {
    core::LogOnce<int> told;   // Told keys logged; its own leaf lock
};
inline State g_state;

inline void tellOnce(Told key, const char* line)
{
    try {
        if (g_state.told.first(key)) core::logf("%s", line);
    } catch (...) {   // H6: bad_alloc in LogOnce only; the line is not logged
    }
}

// 1.0 FUN_00d89e60, FUN_00d7f820, FUN_00d63460 drive DeliveryManager platforms; 3.0 has no DeliveryManager.
inline void deliveryImpulse(uint8_t*, int, void*)
{
    tellOnce(kDeliveryInput, "delivery10: DeliveryNode input: 3.0 has no DeliveryManager, so no package comes; "
                             "StartChoreography / Open / Collected never fire");
}

// OutLocatorIndex (1.0 getter 0xd5a570 -> node +0x9c): the platform last delivered to; none delivers, so 0.
inline void* outIndex(uint8_t* node, int)
{
    DeliveryState& ds = *(DeliveryState*)mernode::stateOf(node);
    ds.outIndex.vft = mernode::g_state.engine.intElemVt;
    ds.outIndex.type = mernode::g_state.engine.intType;
    return &ds.outIndex;
}

// Pins in the 1.0 graphs' numbering (describe FUN_00d904b0); parameters FUN_00d96730
inline mernode::Spec deliverySpec()
{
    using mernode::Data;
    using mernode::DataOut;
    using mernode::DataV;
    using mernode::In;
    using mernode::Out;
    return {"DeliveryNode",
            {{"Initialize", In}, {"EndChoreography", In}, {"Opened", In},
             {"DropOffLocations", Data}, {"InLocatorIndex", DataV},
             {"StartChoreography", Out}, {"Open", Out}, {"Collected", Out},
             {"OutLocatorIndex", DataOut}},
            {{"NodeName", ::mer::T_STRING}, {"Radius", ::mer::T_FLOAT}, {"KillAfterCollected", ::mer::T_BOOL},
             {"SlaveDelivery", ::mer::T_BOOL}, {"PushDistance", ::mer::T_FLOAT}},
            sizeof(DeliveryState),
            &deliveryImpulse,
            nullptr,
            &outIndex};
}

// 1.0 0xd77040: once, a SlotManager socket per locator (FUN_00ba0390); 3.0's SlotManager has no socket add.
inline void spawnSocketsImpulse(uint8_t*, int, void*)
{
    tellOnce(kSpawnSockets, "decosockets10: SpawnDecorationSockets: 3.0's SlotManager adds no decoration sockets; "
                            "1.0 decorations have nowhere to stand");
}

// 1.0 FUN_00d77450: each socket at the locators (or tagged actors) on / off (FUN_00b814e0 / FUN_00b812e0).
inline void activateSocketsImpulse(uint8_t*, int, void*)
{
    tellOnce(kActivateSockets, "decosockets10: ActivateDecorationSockets: no decoration sockets exist in 3.0");
}

// 1.0 describe FUN_00d7eb40, node ctor FUN_00d76ef0
inline mernode::Spec spawnSocketsSpec()
{
    return {"SpawnDecorationSockets",
            {{"Link_In", mernode::In}, {"LocatorList", mernode::Data}},
            {{"TargetName", ::mer::T_STRING}, {"UseTag", ::mer::T_BOOL}},
            0,
            &spawnSocketsImpulse};
}

// 1.0 describe FUN_00d7eb80, node ctor FUN_00d772b0
inline mernode::Spec activateSocketsSpec()
{
    return {"ActivateDecorationSockets",
            {{"Link_In", mernode::In}, {"LocatorList", mernode::Data}},
            {{"UseTag", ::mer::T_BOOL}, {"Activate", ::mer::T_BOOL}, {"TargetName", ::mer::T_STRING},
             {"SocketName", ::mer::T_STRING}},
            0,
            &activateSocketsImpulse};
}

// 1.0 0xd87e70 -> SlotManager FUN_00b7ae90: a combo of Who and its Ender; 3.0 keeps no combos.
inline void prankComboImpulse(uint8_t*, int, void*)
{
    tellOnce(kPrankCombo, "prank10: RegisterPrankCombo: 3.0 has no prank combos; PrankComboEnded never fires");
}

// 1.0 describe FUN_00d8d1c0; no parameters (node 0x30 B)
inline mernode::Spec prankComboSpec()
{
    return {"RegisterPrankCombo",
            {{"Link_In", mernode::In}, {"Who", mernode::DataV}, {"Ender", mernode::DataV}},
            {},
            0,
            &prankComboImpulse};
}

// PrankComboEnded's outputs: Who and Ender actors, LaunchCount an int (1.0 node +0x24 / +0x30 / +0x3c)
struct ComboEndedState {
    mernode::AgentPayload who, ender;
    mernode::IntElement launchCount;
};

// PrankComboEnded's pins in 1.0's numbering (describe FUN_00d60120)
enum ComboEndedPin : int { kLinkOut, kWho, kEnder, kLaunchCount };

// 1.0 getters 0xd57bf0 / 0x109ad90 / 0xd58a60; no combo ends in 3.0, so handles and count stay 0.
inline void* comboEndedOut(uint8_t* node, int pin)
{
    const mernode::Engine& e = mernode::g_state.engine;
    ComboEndedState& cs = *(ComboEndedState*)mernode::stateOf(node);
    tellOnce(kComboEnded, "prankcombo10: PrankComboEnded read: no combo ended in 3.0; Who / Ender none, LaunchCount 0");
    if (pin == kLaunchCount) {
        cs.launchCount.vft = e.intElemVt;
        cs.launchCount.type = e.intType;
        return &cs.launchCount;
    }
    mernode::AgentPayload& a = pin == kEnder ? cs.ender : cs.who;
    a.vft = e.agentElemVt;
    a.type = e.agentType;
    return &a;
}

// 1.0 0xd77ae0 listens for the "PrankComboEnded" message, which 3.0 never posts; no input pin.
inline mernode::Spec comboEndedSpec()
{
    using mernode::DataOut;
    return {"PrankComboEnded",
            {{"Link_Out", mernode::Out}, {"Who", DataOut}, {"Ender", DataOut}, {"LaunchCount", DataOut}},
            {},
            sizeof(ComboEndedState),
            nullptr,
            nullptr,
            &comboEndedOut};
}

inline void add(const mernode::Spec& s)
{
    if (mernode::add(s) < 0) core::logf("inertnodes: %s not added (native class table full)", s.name);
}

}  // namespace detail

inline void install(const Config& c)
{
    if (c.delivery10) detail::add(detail::deliverySpec());
    if (c.prankCombo10) detail::add(detail::comboEndedSpec());
    if (c.prank10) detail::add(detail::prankComboSpec());
    if (!c.decoSockets10) return;
    detail::add(detail::spawnSocketsSpec());
    detail::add(detail::activateSocketsSpec());
}

}  // namespace di1::inertnodes
