// playership10: 1.0's player ship deck DNA takes its rigid body actor's ship components (3.0 lacks the connector).
// luachunk. Any thread.
#pragma once
#include <algorithm>
#include <map>
#include <string>
#include "../../formats/luachunk.hpp"
#include "rowboat.hpp"

namespace di1::playership {

using Globals = rowboat::Globals;

// Whether g is 1.0's player ship deck: Properties.PlayerShip true and a ShipRigidBodyConnector gadget.
inline bool isPlayerShip(const Globals& g);
// The engine path of the rigid body DNA the deck's connector names; "" when it names none.
inline std::string rigidBodyPath(const Globals& g);
// The deck keeps its ship pair, else takes rb's with its body parts; drops the connector. False: g untouched.
inline bool foldRigidBody(Globals& g, const Globals& rb);
// A deck chunk with its rigid body folded in, as 3.0 boat vehicle text into out; false: out untouched.
inline bool ship30(const std::string& chunk, const rowboat::Steps& s, std::string& out, std::string& note);

namespace detail {

using luachunk::Value;

constexpr char kConnector[] = "ShipRigidBodyConnector";   // 1.0's deck <-> rigid body actor link; no 3.0 class
constexpr char kRigidBodyName[] = "RigidBodyName";        // av_pirateship.dnax: 'AV_PirateShipRB'
constexpr char kDnaDir[] = "characters/";
constexpr char kDnaExt[] = ".dnax";
// What the deck takes of the rigid body: the ship pair, then the body parts (pc_pirateship has both controls)
inline constexpr const char* kShipComponents[] = {"PhysicsShip", "ShipController"};
inline constexpr const char* kBodyComponents[] = {"CollOffsetMgr"};
constexpr char kBodyGadget[] = "RigidBody";

inline const Value* sub(const Globals& g, const char* key)
{
    auto dna = g.find("DNA");
    const Value* t = dna != g.end() ? dna->second.get(key) : nullptr;
    return t && t->t == Value::TAB ? t : nullptr;
}
inline Value* sub(Globals& g, const char* key) { return const_cast<Value*>(sub(static_cast<const Globals&>(g), key)); }

inline bool hasShipPair(const Value* comps)
{
    if (!comps) return false;
    for (const char* k : kShipComponents)
        if (!comps->get(k)) return false;
    return true;
}

// to[key] = from[key] when from has it and to does not.
inline void copyMissing(Value& to, const Value* from, const char* key)
{
    const Value* v = from ? from->get(key) : nullptr;
    if (!v || to.get(key)) return;
    Value k;
    k.t = Value::STR;
    k.s = key;
    to.tab->push_back({k, *v});
}

inline void dropKey(Value& t, const char* key)
{
    auto& e = *t.tab;
    e.erase(std::remove_if(e.begin(), e.end(), [key](const auto& p) { return p.first.s == key; }), e.end());
}

}  // namespace detail

inline bool isPlayerShip(const Globals& g)
{
    const luachunk::Value* props = detail::sub(g, "Properties");
    const luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    const luachunk::Value* flag = props ? props->get("PlayerShip") : nullptr;
    const luachunk::Value* on = flag ? flag->get("value") : nullptr;
    const bool player = on && on->t == luachunk::Value::BOOL && on->b;
    return player && gadgets && gadgets->get(detail::kConnector);
}

inline std::string rigidBodyPath(const Globals& g)
{
    const luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    const luachunk::Value* conn = gadgets ? gadgets->get(detail::kConnector) : nullptr;
    const std::string name = conn ? conn->str(detail::kRigidBodyName) : "";
    return name.empty() ? name : detail::kDnaDir + name + detail::kDnaExt;
}

inline bool foldRigidBody(Globals& g, const Globals& rb)
{
    luachunk::Value* comps = detail::sub(g, "Components");
    luachunk::Value* gadgets = detail::sub(g, "AgentGadgets");
    const luachunk::Value* rbComps = detail::sub(rb, "Components");
    const bool own = detail::hasShipPair(comps);
    const bool pair = own || detail::hasShipPair(rbComps);
    if (!comps || !gadgets || !pair) return false;
    if (!own) {
        for (const char* k : detail::kShipComponents) detail::copyMissing(*comps, rbComps, k);
        for (const char* k : detail::kBodyComponents) detail::copyMissing(*comps, rbComps, k);
        detail::copyMissing(*gadgets, detail::sub(rb, "AgentGadgets"), detail::kBodyGadget);
    }
    detail::dropKey(*gadgets, detail::kConnector);
    return true;
}

inline bool ship30(const std::string& chunk, const rowboat::Steps& s, std::string& out, std::string& note)
{
    if (chunk.find(detail::kConnector) == std::string::npos) return false;
    auto g = luachunk::eval(chunk);
    if (!isPlayerShip(g)) return false;
    const std::string path = rigidBodyPath(g);
    std::string rbChunk;
    Globals rb;
    if (!path.empty() && s.read10 && s.read10(path, rbChunk)) rb = luachunk::eval(rbChunk);
    if (!foldRigidBody(g, rb)) {
        note += "playership10: no ship components (rigid body '" + path + "') ";
        return false;
    }
    note += "+playership10(rigid body " + path + ")";
    out = rowboat::finish30(g, chunk, s.chos, rowboat::boatDefaults(s), note);
    return true;
}

}  // namespace di1::playership
