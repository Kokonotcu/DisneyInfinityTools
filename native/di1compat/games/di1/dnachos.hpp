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
    const char* gadget;
    const char* sub;   // Chos or DNAs
    const char* key;
};

// The pairs 1.0 DNAs write flat. Parsers checked: 0x1517bf0 0x151a830 0x151c000 0xd4c4a0 0xd5ddf0 0x104ebe0 0xc1ca70.
inline constexpr Nested kNested[] = {
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

// DNA.AgentGadgets.<name> as a table, or null.
inline Value* gadget(std::map<std::string, Value>& g, const char* name)
{
    auto dna = g.find("DNA");
    Value* gadgets = dna != g.end() ? dna->second.get("AgentGadgets") : nullptr;
    Value* v = gadgets ? gadgets->get(name) : nullptr;
    return v && v->t == Value::TAB ? v : nullptr;
}

// t's string field n.key moves into t's sub-table n.sub, made when missing. -> 1 when moved
inline int nest(Value& t, const Nested& n)
{
    const Value* v = t.get(n.key);
    if (!v || v->t != Value::STR) return 0;
    const Value moved = *v;
    if (!t.get(n.sub)) t.tab->push_back({str(n.sub), table()});
    Value* sub = t.get(n.sub);
    if (sub->t != Value::TAB || sub->get(n.key)) return 0;
    sub->tab->push_back({str(n.key), moved});
    auto named = [&n](const std::pair<Value, Value>& f) { return f.first.t == Value::STR && f.first.s == n.key; };
    t.tab->erase(std::remove_if(t.tab->begin(), t.tab->end(), named), t.tab->end());
    return 1;
}

// Whether chunk names any kNested key: most DNAs do not, and skip their evaluation.
inline bool namesNested(const std::string& chunk)
{
    auto in = [&chunk](const Nested& n) { return chunk.find(n.key) != std::string::npos; };
    return std::any_of(std::begin(kNested), std::end(kNested), in);
}

}  // namespace detail

inline Changed dna30(const std::string& chunk, const Steps& s, std::string& out)
{
    const bool cho = chunk.find("CHOREOGRAPHY") != std::string::npos && chunk.find("Create") != std::string::npos;
    const bool chos = s.chos && cho;
    const bool nest = s.nest && detail::namesNested(chunk);
    Changed c;
    if (!chos && !nest) return c;
    auto g = luachunk::eval(chunk);
    luachunk::Value* fxs = chos ? detail::gadget(g, "Effects") : nullptr;
    luachunk::Value* fx = fxs ? fxs->get("FX") : nullptr;
    if (fx && fx->t == luachunk::Value::TAB)
        for (auto& [k, e] : *fx->tab) c.chos += detail::effectTo30(e);
    for (const Nested& n : kNested) {
        luachunk::Value* t = nest ? detail::gadget(g, n.gadget) : nullptr;
        if (t) c.nested += detail::nest(*t, n);
    }
    if (!c.chos && !c.nested) return c;
    out.clear();
    for (auto& [name, v] : g) out += name + " = " + luachunk::text(v) + "\n";
    return c;
}

}  // namespace di1::dnachos
