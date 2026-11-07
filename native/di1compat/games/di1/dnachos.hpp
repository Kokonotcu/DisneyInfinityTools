// dnachos10 / dnaexploder10 / dnarespawn10: 1.0 DNA tables 3.0 reads nested, in 3.0's form. luachunk. Any thread.
#pragma once
#include <algorithm>
#include <iterator>
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
    bool respawn = false;    // TardisRespawn's RespawnCho -> TardisRespawn.Chos
};

// What a rewrite changed.
struct Changed {
    int chos = 0;       // Create steps
    int exploder = 0;   // 1: the exploder's DNA names moved
    int respawn = 0;    // 1: the respawn choreography moved
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

// t's string fields a and b move into a new sub-table sub, unless t has one. -> 1 when moved
inline int nest(Value& t, const char* sub, const char* a, const char* b)
{
    if (t.get(sub)) return 0;
    auto named = [a, b](const std::pair<Value, Value>& f) {
        const bool key = f.first.t == Value::STR && (f.first.s == a || f.first.s == b);
        return key && f.second.t == Value::STR;
    };
    Value inner;
    inner.t = Value::TAB;
    inner.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    std::copy_if(t.tab->begin(), t.tab->end(), std::back_inserter(*inner.tab), named);
    if (inner.tab->empty()) return 0;
    t.tab->erase(std::remove_if(t.tab->begin(), t.tab->end(), named), t.tab->end());
    t.tab->push_back({str(sub), inner});
    return 1;
}

// 3.0's exploder parser 0x1517bf0 reads PieceDNA from Exploder.DNAs (1485 of 1485 shipped); 1.0 wrote it flat (213).
inline int exploderTo30(Value& ex) { return nest(ex, "DNAs", "PieceDNA", "DebrisDNAFile"); }

// 3.0's TardisRespawn parser 0x151a830 reads Chos.RespawnCho (532 of 532 shipped); 1.0 wrote it flat (102).
inline int respawnTo30(Value& tr) { return nest(tr, "Chos", "RespawnCho", "RespawnCho"); }

}  // namespace detail

inline Changed dna30(const std::string& chunk, const Steps& s, std::string& out)
{
    // most DNAs need neither rewrite: skip their evaluation
    const bool cho = chunk.find("CHOREOGRAPHY") != std::string::npos && chunk.find("Create") != std::string::npos;
    const bool chos = s.chos && cho;
    const bool names = chunk.find("PieceDNA") != std::string::npos || chunk.find("DebrisDNAFile") != std::string::npos;
    const bool exploder = s.exploder && names;
    const bool respawn = s.respawn && chunk.find("RespawnCho") != std::string::npos;
    Changed c;
    if (!chos && !exploder && !respawn) return c;
    auto g = luachunk::eval(chunk);
    luachunk::Value* fxs = chos ? detail::gadget(g, "Effects") : nullptr;
    luachunk::Value* fx = fxs ? fxs->get("FX") : nullptr;
    if (fx && fx->t == luachunk::Value::TAB)
        for (auto& [k, e] : *fx->tab) c.chos += detail::effectTo30(e);
    luachunk::Value* ex = exploder ? detail::gadget(g, "Exploder") : nullptr;
    if (ex) c.exploder = detail::exploderTo30(*ex);
    luachunk::Value* tr = respawn ? detail::gadget(g, "TardisRespawn") : nullptr;
    if (tr) c.respawn = detail::respawnTo30(*tr);
    if (!c.chos && !c.exploder && !c.respawn) return c;
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return c;
}

}  // namespace di1::dnachos
