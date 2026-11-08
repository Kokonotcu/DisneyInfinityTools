// rowboat10: 1.0's player rowboat DNA as a 3.0 boat vehicle (3.0 lacks PhysicsShip, ShipController). luachunk.
// Any thread.
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"
#include "dnachos.hpp"

namespace di1::rowboat {

using Globals = std::map<std::string, luachunk::Value>;

// 3.0's boat DNA (vehicles/boat/boat.mer and vehicles/ship/ship.mer spawn it): the defaults 1.0's rowboat lacks.
constexpr char kBoatDna30[] = "characters/TB_Boat.dnax";

// Whether g is 1.0's player rowboat: ShipController typed Rowboat, PhysicsShip, no RowboatAI gadget.
inline bool isPlayerRowboat(const Globals& g);
// What vehicle30 changed: components renamed, TB_Boat vehicle gadgets and components copied in.
struct Edits {
    int renamed = 0;
    int parts = 0;
};
// g's rowboat as 3.0's boat vehicle; base30 is 3.0's TB_Boat DNA (empty: no 3.0 boat defaults).
inline Edits vehicle30(Globals& g, const std::string& base30);
// dnachos' steps s, then vehicle30, as Lua text; the note gets what changed (after the caller's tag).
inline std::string finish30(Globals& g, const std::string& chunk, const dnachos::Steps& s, const std::string& base30,
                            std::string& note);

using Read = std::function<bool(const std::string& path, std::string& out)>;
// What a boat DNA rule needs: dnachos' steps, a 1.0 read (a ship's rigid body) and a 3.0 read (TB_Boat).
struct Steps {
    dnachos::Steps chos;
    Read read10, read30;     // empty: that read fails
    bool rowboat = true;     // rowboat10: the player rowboat
    bool shipBody = false;   // shipbody10: the player ship's rigid body actor
};
// Whether g is the rigid body actor 1.0's player ship deck spawns (kShipBodies) with its ship components.
inline bool isPlayerShipBody(const Globals& g);
// A player rowboat or ship body chunk as 3.0's boat vehicle text into out, tagged in note; false: untouched.
inline bool rowboat30(const std::string& chunk, const Steps& s, std::string& out, std::string& note);
// 3.0's TB_Boat DNA through s.read30; "" when it cannot be read.
inline std::string boatDefaults(const Steps& s);

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
constexpr char kDrivable[] = "Drivable";
constexpr char kNoStates[] = "NONE";
constexpr char kVehicleStates[] = "Perceivable|Targetable|Drivable";   // tb_boat.dnax, for a 1.0 mask of NONE
// Rigid body actors of player ship decks (av_pirateship.dnax ShipRigidBodyConnector.RigidBodyName)
inline constexpr const char* kShipBodies[] = {"AV_PirateShipRB"};

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

// The actor state Drive needs on the vehicle: TB_Boat's mask has Drivable; the ship body's 1.0 mask is NONE.
inline void drivable(Value& gadgets)
{
    Value* states = gadgets.get("StateBitmask");
    const std::string mask = states ? states->str("ActorStateMask") : "";
    if (mask.find(kDrivable) != std::string::npos) return;
    const bool none = mask.empty() || mask == kNoStates;
    putIn(gadgets, "StateBitmask", "ActorStateMask", str(none ? kVehicleStates : mask + "|" + kDrivable));
}

// 3.0's vehicle gadgets: transport data, script, AI tree, net type, collision mode, driving perception; no Locomotion.
inline void vehicleGadgets(Value& gadgets)
{
    drivable(gadgets);
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

// TB_Boat's vehicle parts no 1.0 boat has (tb_boat.dnax): driver HUD, rigid body params and messages, vehicle feel
inline constexpr const char* kVehicleGadgets[] = {
    "AvatarInteractionHud", "OcclusionColoringData", "RigidBodyParamManager", "VehicleCollisionDamper",
    "VehicleMusic", "VehicleQuickTurn", "VehicleTricksPhysics",
};
inline constexpr const char* kVehicleComponents[] = {"CarsReactionMonitor", "RigidBodyMessageHandler"};

// to[key] = from[key] when from has it and to does not; returns whether it copied.
inline bool copyMissing(Value& to, const Value* from, const char* key)
{
    const Value* v = from ? from->get(key) : nullptr;
    if (!v || to.get(key)) return false;
    to.tab->push_back({str(key), *v});
    return true;
}

// base's vehicle gadgets and components the boat lacks; returns how many it copied.
inline int vehicleParts(Value& gadgets, Value& comps, const Globals& base)
{
    int n = 0;
    for (const char* k : kVehicleGadgets) n += copyMissing(gadgets, sub(base, "AgentGadgets"), k);
    for (const char* k : kVehicleComponents) n += copyMissing(comps, sub(base, "Components"), k);
    return n;
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

inline Edits vehicle30(Globals& g, const std::string& base30)
{
    luachunk::Value* comps = detail::sub(g, "Components");
    luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    if (!comps || !gadgets) return {};
    Globals base;
    if (!base30.empty()) base = luachunk::eval(base30);
    Edits e;
    e.renamed = detail::renameComponents(*comps, detail::baseBoat(base));
    detail::vehicleGadgets(*gadgets);
    e.parts = detail::vehicleParts(*gadgets, *comps, base);
    return e;
}

inline std::string finish30(Globals& g, const std::string& chunk, const dnachos::Steps& s, const std::string& base30,
                            std::string& note)
{
    const dnachos::Changed c = dnachos::edit30(g, chunk, s);
    const Edits e = vehicle30(g, base30);
    note += "(" + std::to_string(e.renamed) + " components as ToyBoxVehicle/BoatController, " +
            (base30.empty() ? "no 3.0 boat defaults" : "TB_Boat defaults") + ", transport vehicle gadgets";
    if (e.parts) note += ", " + std::to_string(e.parts) + " TB_Boat vehicle parts";
    note += ") ";
    if (c.nested) note += "+nest(" + std::to_string(c.nested) + " gadget fields) ";
    std::string out;
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return out;
}

inline std::string boatDefaults(const Steps& s)
{
    std::string base;
    if (!s.read30 || !s.read30(kBoatDna30, base)) base.clear();
    return base;
}

inline bool isPlayerShipBody(const Globals& g)
{
    const luachunk::Value* comps = detail::sub(g, "Components");
    const luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    const luachunk::Value* id = gadgets ? gadgets->get("Identification") : nullptr;
    const std::string name = id ? id->str("Name") : "";
    bool listed = false;
    for (const char* n : detail::kShipBodies) listed = listed || name == n;
    const bool ship = comps && comps->get(detail::kController10) && comps->get(detail::kPhysics10);
    return listed && ship;
}

inline bool rowboat30(const std::string& chunk, const Steps& s, std::string& out, std::string& note)
{
    if (chunk.find("ShipController") == std::string::npos) return false;
    auto g = luachunk::eval(chunk);
    const bool rowboat = s.rowboat && isPlayerRowboat(g);
    const bool body = !rowboat && s.shipBody && isPlayerShipBody(g);
    if (!rowboat && !body) return false;
    note += rowboat ? "+rowboat10" : "+shipbody10";
    out = finish30(g, chunk, s.chos, boatDefaults(s), note);
    return true;
}

}  // namespace di1::rowboat
