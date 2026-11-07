// dnachos10 / dnaexploder10: a 1.0 DNA's choreography effects and exploder in 3.0's form. luachunk. Any thread.
#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"

namespace di1::dnachos {

// The rewrites to apply.
struct Steps {
    bool chos = false;       // Create's Name -> Chos = {Name}
    bool exploder = false;   // Exploder's PieceDNA / DebrisDNAFile -> Exploder.DNAs
};

// What a rewrite changed.
struct Changed {
    int chos = 0;       // Create steps
    int exploder = 0;   // 1: the exploder's DNA names moved
};

// chunk in 3.0's form per s, as Lua text into out; out untouched when nothing changed.
inline Changed dna30(const std::string& chunk, const Steps& s, std::string& out);

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

// DNA.AgentGadgets.<name> as a table, or null.
inline Value* gadget(std::map<std::string, Value>& g, const char* name)
{
    auto dna = g.find("DNA");
    Value* gadgets = dna != g.end() ? dna->second.get("AgentGadgets") : nullptr;
    Value* v = gadgets ? gadgets->get(name) : nullptr;
    return v && v->t == Value::TAB ? v : nullptr;
}

inline bool isExploderDna(const std::pair<Value, Value>& f)
{
    const bool named = f.first.t == Value::STR && (f.first.s == "PieceDNA" || f.first.s == "DebrisDNAFile");
    return named && f.second.t == Value::STR;
}

// 3.0's exploder parser 0x1517bf0 reads PieceDNA from Exploder.DNAs (1485 of 1485 shipped); 1.0 wrote it flat (213).
inline int exploderTo30(Value& ex)
{
    if (ex.get("DNAs")) return 0;
    Value dnas;
    dnas.t = Value::TAB;
    dnas.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    for (auto& f : *ex.tab)
        if (isExploderDna(f)) dnas.tab->push_back(f);
    if (dnas.tab->empty()) return 0;
    auto& fields = *ex.tab;
    fields.erase(std::remove_if(fields.begin(), fields.end(), isExploderDna), fields.end());
    fields.push_back({str("DNAs"), dnas});
    return 1;
}

}  // namespace detail

inline Changed dna30(const std::string& chunk, const Steps& s, std::string& out)
{
    // most DNAs need neither rewrite: skip their evaluation
    const bool cho = chunk.find("CHOREOGRAPHY") != std::string::npos && chunk.find("Create") != std::string::npos;
    const bool chos = s.chos && cho;
    const bool names = chunk.find("PieceDNA") != std::string::npos || chunk.find("DebrisDNAFile") != std::string::npos;
    const bool exploder = s.exploder && names;
    Changed c;
    if (!chos && !exploder) return c;
    auto g = luachunk::eval(chunk);
    luachunk::Value* fxs = chos ? detail::gadget(g, "Effects") : nullptr;
    luachunk::Value* fx = fxs ? fxs->get("FX") : nullptr;
    if (fx && fx->t == luachunk::Value::TAB)
        for (auto& [k, e] : *fx->tab) c.chos += detail::effectTo30(e);
    luachunk::Value* ex = exploder ? detail::gadget(g, "Exploder") : nullptr;
    if (ex) c.exploder = detail::exploderTo30(*ex);
    if (!c.chos && !c.exploder) return c;
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return c;
}

}  // namespace di1::dnachos
