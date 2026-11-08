// dnachos10 / dnanest10: 1.0 DNA fields 3.0 reads from a sub-table, in 3.0's form. luachunk. Any thread.
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
    bool chos = false;   // Create's Name -> Chos = {Name}
    bool nest = false;   // kNested: a gadget's flat cho / DNA name -> <gadget>.Chos / .DNAs
};

// What a rewrite changed.
struct Changed {
    int chos = 0;     // Create steps
    int nested = 0;   // gadget fields moved
};

// A gadget field 3.0 reads only from a sub-table (every shipped 3.0 DNA nests it; dnachosall.py).
struct Nested {
    const char* gadget;   // nullptr: AgentGadgets itself
    const char* sub;      // Chos, DNAs, EntityInstance
    const char* key;
};

// The pairs 1.0 DNAs write flat. Parsers checked: 0x1517bf0 0x151a830 0x151c000 0xd4c4a0 0xd5ddf0 0x104ebe0 0xc1ca70.

// FocusOffset: 1.0 0x11548c0 reads AgentGadgets', 3.0 0x1886600 AgentGadgets.EntityInstance's (3865 DNAs).
inline constexpr Nested kNested[] = {
    {nullptr, "EntityInstance", "FocusOffset"},
    {"BikeBeacon", "Chos", "Cho"},
    {"BombFlight", "Chos", "m_ExplosionChoName"},
    {"BombFlight", "Chos", "m_MuzzleFlashChoName"},
    {"BombFlight", "Chos", "m_WarnChoName"},
    {"Combat", "Chos", "VisorCho"},
    {"Combat", "Chos", "spawnCho"},
    {"DynamicExploder", "Chos", "dynexp_explodeCho"},
    {"DynamicExploder", "Chos", "dynexp_reassembleCho"},
    {"Exploder", "DNAs", "PieceDNA"},
    {"Exploder", "DNAs", "DebrisDNAFile"},
    {"MissileFlight", "Chos", "m_ExplosionChoName"},
    {"Mount", "Chos", "MovingFX"},
    {"Mount", "Chos", "StationaryFX"},
    {"RumpusPhysicsBlock", "Chos", "m_ExplosionChoName"},
    {"RumpusPhysicsBlock", "Chos", "m_HealthChoName"},
    {"TardisRespawn", "Chos", "RespawnCho"},
};

// chunk in 3.0's form per s, as Lua text into out; out untouched when nothing changed.
inline Changed dna30(const std::string& chunk, const Steps& s, std::string& out);
// The steps s that chunk's text can need: a cheap test before eval.
inline Steps stepsFor(const std::string& chunk, const Steps& s);
// dna30's edits on g, chunk's evaluated globals; for a rule that edits the same DNA further.
inline Changed edit30(std::map<std::string, luachunk::Value>& g, const std::string& chunk, const Steps& s);

namespace detail {

using luachunk::Value;

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

inline bool isName(const std::pair<Value, Value>& f) { return f.first.t == Value::STR && f.first.s == "Name"; }

// 3.0 reads a Create step's choreography from Chos.Name (224 of 224 shipped steps); 1.0 wrote Name (38 of 38).
inline bool createTo30(Value& step)
{
    const bool create = step.t == Value::TAB && step.str("Type") == "Create";
    const std::string name = create ? step.str("Name") : "";
    if (name.empty() || step.get("Chos")) return false;
    Value chos = table();
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

// DNA.AgentGadgets.<name> (name null: AgentGadgets) as a table, or null.
inline Value* gadget(std::map<std::string, Value>& g, const char* name)
{
    auto dna = g.find("DNA");
    Value* gadgets = dna != g.end() ? dna->second.get("AgentGadgets") : nullptr;
    Value* v = gadgets && name ? gadgets->get(name) : gadgets;
    return v && v->t == Value::TAB ? v : nullptr;
}

// t's field n.key (a string, or FocusOffset's vector) moves into t's sub-table n.sub, made when missing. -> 1 moved
inline int nest(Value& t, const Nested& n)
{
    const Value* v = t.get(n.key);
    if (!v || (v->t != Value::STR && v->t != Value::TAB)) return 0;
    const Value moved = *v;
    if (!t.get(n.sub)) t.tab->push_back({str(n.sub), table()});
    Value* sub = t.get(n.sub);
    if (sub->t != Value::TAB || sub->get(n.key)) return 0;
    sub->tab->push_back({str(n.key), moved});
    auto named = [&n](const std::pair<Value, Value>& f) { return f.first.t == Value::STR && f.first.s == n.key; };
    t.tab->erase(std::remove_if(t.tab->begin(), t.tab->end(), named), t.tab->end());
    return 1;
}

// 1.0 Detonate.DetonationFX {cho, anchor} -> 3.0 Chos.DetonationFX_Cho, DetonationFX_Anchor. -> 1 moved
inline int detonateTo30(Value& det)
{
    const Value* fx = det.get("DetonationFX");
    if (!fx || fx->t != Value::TAB || fx->tab->size() != 2) return 0;
    const Value cho = (*fx->tab)[0].second, anchor = (*fx->tab)[1].second;
    if (cho.t != Value::STR || anchor.t != Value::STR || det.get("DetonationFX_Anchor")) return 0;
    if (!det.get("Chos")) det.tab->push_back({str("Chos"), table()});
    Value* chos = det.get("Chos");
    if (chos->t != Value::TAB || chos->get("DetonationFX_Cho")) return 0;
    chos->tab->push_back({str("DetonationFX_Cho"), cho});
    det.tab->push_back({str("DetonationFX_Anchor"), anchor});
    auto named = [](const std::pair<Value, Value>& f) {
        return f.first.t == Value::STR && f.first.s == "DetonationFX";
    };
    det.tab->erase(std::remove_if(det.tab->begin(), det.tab->end(), named), det.tab->end());
    return 1;
}

// Whether chunk names any kNested key: most DNAs do not, and skip their evaluation.
inline bool namesNested(const std::string& chunk)
{
    auto in = [&chunk](const Nested& n) { return chunk.find(n.key) != std::string::npos; };
    return chunk.find("DetonationFX") != std::string::npos || std::any_of(std::begin(kNested), std::end(kNested), in);
}

}  // namespace detail

inline Steps stepsFor(const std::string& chunk, const Steps& s)
{
    const bool cho = chunk.find("CHOREOGRAPHY") != std::string::npos && chunk.find("Create") != std::string::npos;
    return {s.chos && cho, s.nest && detail::namesNested(chunk)};
}

inline Changed edit30(std::map<std::string, luachunk::Value>& g, const std::string& chunk, const Steps& s)
{
    const Steps on = stepsFor(chunk, s);
    const bool chos = on.chos, nest = on.nest;
    Changed c;
    if (!chos && !nest) return c;
    luachunk::Value* fxs = chos ? detail::gadget(g, "Effects") : nullptr;
    luachunk::Value* fx = fxs ? fxs->get("FX") : nullptr;
    if (fx && fx->t == luachunk::Value::TAB)
        for (auto& [k, e] : *fx->tab) c.chos += detail::effectTo30(e);
    for (const Nested& n : kNested) {
        luachunk::Value* t = nest ? detail::gadget(g, n.gadget) : nullptr;
        if (t) c.nested += detail::nest(*t, n);
    }
    luachunk::Value* det = nest ? detail::gadget(g, "Detonate") : nullptr;
    if (det) c.nested += detail::detonateTo30(*det);
    return c;
}

inline Changed dna30(const std::string& chunk, const Steps& s, std::string& out)
{
    const Steps on = stepsFor(chunk, s);
    if (!on.chos && !on.nest) return {};
    auto g = luachunk::eval(chunk);
    const Changed c = edit30(g, chunk, s);
    if (!c.chos && !c.nested) return c;
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return c;
}

}  // namespace di1::dnachos
