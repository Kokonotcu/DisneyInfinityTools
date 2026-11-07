// dnachos10: a 1.0 DNA's choreography effects in 3.0's form, Create's Name -> Chos = {Name}. luachunk. Any thread.
#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"

namespace di1::dnachos {

// chunk with each CHOREOGRAPHY effect's Create step in 3.0's form, as Lua text into out. -> steps; 0: out untouched
inline int chos30(const std::string& chunk, std::string& out);

namespace detail {

using luachunk::Value;

inline Value str(const std::string& s)
{
    Value v;
    v.t = Value::STR;
    v.s = s;
    return v;
}

inline bool isName(const std::pair<Value, Value>& f) { return f.first.t == Value::STR && f.first.s == "Name"; }

// 3.0 reads a Create step's choreography from Chos.Name (224 of 224 shipped steps); 1.0 wrote Name (38 of 38).
inline bool createTo30(Value& step)
{
    const bool create = step.t == Value::TAB && step.str("Type") == "Create";
    const std::string name = create ? step.str("Name") : "";
    if (name.empty() || step.get("Chos")) return false;
    Value chos;
    chos.t = Value::TAB;
    chos.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    chos.tab->push_back({str("Name"), str(name)});
    auto& fields = *step.tab;
    fields.erase(std::remove_if(fields.begin(), fields.end(), isName), fields.end());
    fields.push_back({str("Chos"), chos});
    return true;
}

// Every Create step of one effect's Events lists.
inline int effectTo30(Value& fx)
{
    Value* events = fx.t == Value::TAB && fx.str("Type") == "CHOREOGRAPHY" ? fx.get("Events") : nullptr;
    if (!events || events->t != Value::TAB) return 0;
    int n = 0;
    for (auto& [trigger, steps] : *events->tab) {
        if (steps.t != Value::TAB) continue;
        for (auto& [k, step] : *steps.tab) n += createTo30(step);
    }
    return n;
}

// DNA.AgentGadgets.Effects.FX, or null.
inline Value* effects(std::map<std::string, Value>& g)
{
    auto dna = g.find("DNA");
    Value* gadgets = dna != g.end() ? dna->second.get("AgentGadgets") : nullptr;
    Value* fx = gadgets ? gadgets->get("Effects") : nullptr;
    fx = fx ? fx->get("FX") : nullptr;
    return fx && fx->t == Value::TAB ? fx : nullptr;
}

}  // namespace detail

inline int chos30(const std::string& chunk, std::string& out)
{
    // most DNAs have no choreography effect: skip their evaluation
    const bool candidate = chunk.find("CHOREOGRAPHY") != std::string::npos && chunk.find("Create") != std::string::npos;
    if (!candidate) return 0;
    auto g = luachunk::eval(chunk);
    luachunk::Value* fx = detail::effects(g);
    if (!fx) return 0;
    int n = 0;
    for (auto& [k, e] : *fx->tab) n += detail::effectTo30(e);
    if (!n) return 0;
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return n;
}

}  // namespace di1::dnachos
