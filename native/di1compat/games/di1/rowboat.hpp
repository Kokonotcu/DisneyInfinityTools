// rowboat10: 1.0's player rowboat DNA as a 3.0 boat vehicle (3.0 lacks PhysicsShip, ShipController). luachunk.
// Any thread.
#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"

namespace di1::rowboat {

using Globals = std::map<std::string, luachunk::Value>;

// 3.0's boat DNA (vehicles/boat/boat.mer and vehicles/ship/ship.mer spawn it): the defaults 1.0's rowboat lacks.
constexpr char kBoatDna30[] = "characters/TB_Boat.dnax";

// Whether g is 1.0's player rowboat: ShipController typed Rowboat, PhysicsShip, no RowboatAI gadget.
inline bool isPlayerRowboat(const Globals& g);
// g's rowboat as 3.0's boat vehicle; base30 is 3.0's TB_Boat DNA (empty: no 3.0 boat defaults). Returns the edits.
inline int vehicle30(Globals& g, const std::string& base30);

namespace detail {

using luachunk::Value;

// Same keys in 3.0: PhysicsShip's are ToyBoxVehicle's (tbx_cruiseboat.dnax), ShipController's BoatController's.
constexpr char kPhysics10[] = "PhysicsShip";
constexpr char kPhysics30[] = "ToyBoxVehicle";
constexpr char kController10[] = "ShipController";
constexpr char kController30[] = "BoatController";
// The keys 3.0's BoatController parser reads (0xd563c7 on); 1.0's AttributesType, BoatSinkingSpeed it does not.
inline constexpr const char* kBoatKeys[] = {
    "BoatDepth", "BoatEnginePower", "BoatEnginePowerFilter", "BoatEnginePowerReverseSensitivity",
    "BoatFlotationImpulseValue", "BoatFrictionLinearNonForward", "BoatFrictionLinearUp", "BoatFrictionRot",
    "BoatSteeringLean", "BoatSteeringPower", "BoatSteeringPowerFilter", "BoatVelRotIncrement", "BoatVelRotLimit",
};
// 3.0 vehicles' gadget values (tb_boat, tbx_cruiseboat, 1.0 cars alike): AI tree, script, net type, collision.
constexpr char kBehaviorMap[] = "Characters/Car.bmx";
constexpr char kBehaviorTree[] = "Characters/Car.btx";
constexpr char kScript[] = "objects/vehicle.sx";
constexpr char kNetType[] = "Vehicle";
constexpr char kResolution[] = "None";
constexpr char kPerception[] = "VehicleTalkToMissionGiver|PassengerRideCar|Driving|DriveHelicopter";   // tb_boat
constexpr char kLocomotion[] = "Locomotion";   // 1.0's AI movement gadget, removed
constexpr double kMaxPassengers = 1;   // tb_boat's ToyBoxVehicleData; TransportVehicle lets a Tardis rule mount it

inline Value str(const std::string& s)
{
    Value v;
    v.t = Value::STR;
    v.s = s;
    return v;
}
inline Value table()
{
    Value v;
    v.t = Value::TAB;
    v.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    return v;
}
// DNA[key] when it is a table, else null.
inline const Value* sub(const Globals& g, const char* key)
{
    auto dna = g.find("DNA");
    const Value* t = dna != g.end() ? dna->second.get(key) : nullptr;
    return t && t->t == Value::TAB ? t : nullptr;
}
inline Value* sub(Globals& g, const char* key) { return const_cast<Value*>(sub(static_cast<const Globals&>(g), key)); }

// t[key] = v, added at the end when t lacks key.
inline void put(Value& t, const std::string& key, const Value& v)
{
    if (Value* old = t.get(key)) {
        *old = v;
        return;
    }
    t.tab->push_back({str(key), v});
}

// gadgets[gadget][key] = value, making the gadget table when missing.
inline void putIn(Value& gadgets, const char* gadget, const char* key, const Value& value)
{
    Value* t = gadgets.get(gadget);
    if (!t || t->t != Value::TAB) {
        put(gadgets, gadget, table());
        t = gadgets.get(gadget);
    }
    put(*t, key, value);
}

// BoatController from ShipController: the keys 3.0 reads, then base's for those 1.0 left out.
inline Value boatController(const Value& ship10, const Value* base)
{
    Value boat = table();
    for (const char* k : kBoatKeys) {
        const Value* v = ship10.get(k);
        if (!v && base) v = base->get(k);
        if (v) boat.tab->push_back({str(k), *v});
    }
    return boat;
}

// The components renamed in place; returns the renames.
inline int renameComponents(Value& comps, const Value* base30)
{
    int n = 0;
    for (auto& [key, v] : *comps.tab) {
        if (key.t != Value::STR) continue;
        if (key.s == kPhysics10) {
            key.s = kPhysics30;
            ++n;
        } else if (key.s == kController10 && v.t == Value::TAB) {
            key.s = kController30;
            v = boatController(v, base30);
            ++n;
        }
    }
    return n;
}

// 3.0's vehicle gadgets: transport data, script, AI tree, net type, collision mode, driving perception; no Locomotion.
inline void vehicleGadgets(Value& gadgets)
{
    Value yes;
    yes.t = Value::BOOL;
    yes.b = true;
    Value seats;
    seats.t = Value::NUM;
    seats.n = kMaxPassengers;
    putIn(gadgets, "ToyBoxVehicleData", "MaxPassengers", seats);
    putIn(gadgets, "ToyBoxVehicleData", "TransportVehicle", yes);
    putIn(gadgets, "Script", "ScriptName", str(kScript));
    putIn(gadgets, "AiTree", "BehaviorMapFile", str(kBehaviorMap));
    putIn(gadgets, "AiTree", "BehaviorTreeFile", str(kBehaviorTree));
    putIn(gadgets, "Network", "networkType", str(kNetType));
    putIn(gadgets, "Collision", "CollisionResolutionMode", str(kResolution));
    putIn(gadgets, "PerceptionHelper", "PerceptionInclude", str(kPerception));
    // no 3.0 DNA with ToyBoxVehicleData or BoatController has Locomotion; its AllowVertical false would pin the hull
    auto& t = *gadgets.tab;
    t.erase(std::remove_if(t.begin(), t.end(), [](const auto& e) { return e.first.s == kLocomotion; }), t.end());
}

// base30's BoatController component, or null.
inline const Value* baseBoat(const Globals& base)
{
    const Value* comps = sub(base, "Components");
    const Value* boat = comps ? comps->get(kController30) : nullptr;
    return boat && boat->t == Value::TAB ? boat : nullptr;
}

}  // namespace detail

inline bool isPlayerRowboat(const Globals& g)
{
    const luachunk::Value* comps = detail::sub(g, "Components");
    const luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    const luachunk::Value* ship = comps ? comps->get(detail::kController10) : nullptr;
    const bool rowboat = ship && ship->str("AttributesType") == "Rowboat";
    const bool physics = comps && comps->get(detail::kPhysics10);
    return rowboat && physics && gadgets && !gadgets->get("RowboatAI");
}

inline int vehicle30(Globals& g, const std::string& base30)
{
    luachunk::Value* comps = detail::sub(g, "Components");
    luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    if (!comps || !gadgets) return 0;
    Globals base;
    if (!base30.empty()) base = luachunk::eval(base30);
    const int n = detail::renameComponents(*comps, detail::baseBoat(base));
    detail::vehicleGadgets(*gadgets);
    return n;
}

}  // namespace di1::rowboat
