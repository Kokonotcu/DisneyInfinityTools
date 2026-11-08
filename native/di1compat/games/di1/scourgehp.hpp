// scourgehp10: a 1.0 pod DNA's ScourgePlant health as 3.0's Health gadget (3.0 lacks ScourgePlant). luachunk.
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"

namespace di1::scourgehp {

// A DNA with AgentGadgets.ScourgePlant and no Health, given 1.0's pod health, as Lua text into out; false: untouched
inline bool health30(const std::string& chunk, std::string& out);

namespace detail {

using luachunk::Value;

// 1.0's ScourgePlant health (component +0xc): FUN_00b17400 sets 24, FUN_00a90a30 sets baby 1 and mother 30
constexpr double kDefaultHealth = 24.0;
struct PodHealth {
    const char* name;   // Identification.Name; its creation key gives the pod type
    double health;
};
inline constexpr PodHealth kHealth[] = {
    {"ScourgePlant_Small", 1.0},    // scourgepod: the baby, type 0
    {"ScourgePlant_Large", 30.0},   // scourgemother: type 4
};

inline Value scalar(Value::T t)
{
    Value v;
    v.t = t;
    return v;
}
inline Value str(const std::string& s)
{
    Value v = scalar(Value::STR);
    v.s = s;
    return v;
}

inline double healthOf(const Value& gadgets)
{
    const Value* id = gadgets.get("Identification");
    const std::string name = id ? id->str("Name") : "";
    for (const PodHealth& p : kHealth)
        if (name == p.name) return p.health;
    return kDefaultHealth;   // juniors (ScourgePlant_Medium) and the base keep the constructor's value
}

// 3.0's Health gadget: health, and destroyWhenDead as 1.0 removes a dead pod (FUN_00c8bb60 -> FUN_00ab8d90)
inline Value healthGadget(double health)
{
    Value hp = scalar(Value::NUM);
    hp.n = health;
    Value destroy = scalar(Value::BOOL);
    destroy.b = true;
    Value g = scalar(Value::TAB);
    g.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    g.tab->push_back({str("destroyWhenDead"), destroy});
    g.tab->push_back({str("health"), hp});
    return g;
}

}  // namespace detail

inline bool health30(const std::string& chunk, std::string& out)
{
    // only pod DNAs name the gadget: skip every other DNA's evaluation
    if (chunk.find("ScourgePlant") == std::string::npos) return false;
    auto g = luachunk::eval(chunk);
    auto dna = g.find("DNA");
    luachunk::Value* gadgets = dna != g.end() ? dna->second.get("AgentGadgets") : nullptr;
    const bool pod = gadgets && gadgets->t == luachunk::Value::TAB && gadgets->get("ScourgePlant");
    if (!pod || gadgets->get("Health")) return false;
    gadgets->tab->push_back({detail::str("Health"), detail::healthGadget(detail::healthOf(*gadgets))});
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return true;
}

}  // namespace di1::scourgehp
