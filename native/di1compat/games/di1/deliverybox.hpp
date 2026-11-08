// rcbox10: a 1.0 delivery box DNA collects on touch, as 1.0's DeliveryNode opened it nearby. luachunk.
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"

namespace di1::deliverybox {

// A DNA typed DeliveryBox without collection events, given 3.0's touch collection, as Lua text into out
inline bool touch30(const std::string& chunk, std::string& out);

namespace detail {

using luachunk::Value;

inline Value table()
{
    Value v;
    v.t = Value::TAB;
    v.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    return v;
}
inline Value str(const std::string& s)
{
    Value v;
    v.t = Value::STR;
    v.s = s;
    return v;
}

// { { EventName = 'CollisionDetect' } }: 3.0's touch pickups (challenge_checkpointrace_*, challenge_slug)
inline Value touchEvents()
{
    Value one = table(), list = table(), key;
    one.tab->push_back({str("EventName"), str("CollisionDetect")});
    key.t = Value::NUM;
    key.n = 1;
    list.tab->push_back({key, one});
    return list;
}

}  // namespace detail

inline bool touch30(const std::string& chunk, std::string& out)
{
    if (chunk.find("DeliveryBox") == std::string::npos) return false;
    auto g = luachunk::eval(chunk);
    auto dna = g.find("DNA");
    luachunk::Value* gadgets = dna != g.end() ? dna->second.get("AgentGadgets") : nullptr;
    if (!gadgets || gadgets->t != luachunk::Value::TAB) return false;
    const luachunk::Value* id = gadgets->get("Identification");
    luachunk::Value* exp = gadgets->get("Expendables");
    // IN_DeliveryBox only: IN_DeliveryBox_CHO (DeliveryBox_CHO) is the choreography prop, not the package
    const bool box = id && id->str("ActorTypeMask") == "DeliveryBox" && exp && exp->t == luachunk::Value::TAB;
    if (!box || exp->get("CollectionEvents")) return false;
    exp->tab->push_back({detail::str("CollectionEvents"), detail::touchEvents()});
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return true;
}

}  // namespace di1::deliverybox
