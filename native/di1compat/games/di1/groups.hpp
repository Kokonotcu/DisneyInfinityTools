// rewards10: 1.0 expendable groups in the keys 3.0's group reader takes; currency10's coin pickups. Any thread.
#pragma once
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/luachunk.hpp"

namespace di1::groups {

// A 1.0 DNA's FastCoin gadget (pays sparks or not, EntityInstance.ModelName) and its Exploder's DebrisDNAFile.
struct CoinDna {
    bool spark = false;
    std::string model;
    std::string debrisDna;   // "" for none
};
using CoinDnaOf = std::function<CoinDna(const std::string& dnaFile)>;
// Whether a 1.0 model ('exploders/x') exists; null: every one does.
using Has10 = std::function<bool(const std::string& model)>;

// What a package's entries are checked against.
struct Readers {
    CoinDnaOf dna;        // a 1.0 DNA's gadgets; null: no coin pickups typed, the default debris DNA
    bool coins = false;   // currency10: coin pickups typed through dna
    Has10 has10;          // null: every 1.0 model exists
};

// debris10: entry def explodes into model with dna (its '<def>_Debris10' entry preloads them).
struct Debris {
    std::string def, model, dna;
};

// What groups30 changed: spawns past the 4th dropped, coin pickups typed, models moved, debris entries added.
struct Counts {
    int over = 0, coins = 0, models = 0, debris = 0;
    std::vector<Debris> debrisDefs;
};

// A package's groups with 3.0's flat spawn keys (FUN_01603b10); coins and debris through r. 0: unchanged.
inline int groups30(const std::string& chunk, std::string& out, Counts* counts = nullptr, const Readers& r = {});

namespace detail {

using luachunk::Value;

constexpr int kSpawnKeys = 4;   // Expendables.1 .. .4: a 5th spawn has no 3.0 key and counts in *over
constexpr char kObjects[] = "objects/", kExploders[] = "Exploders/";
constexpr size_t kObjectsLen = sizeof(kObjects) - 1;
constexpr char kDebrisSuffix[] = "_Debris10";   // debris10: what a 1.0 entry explodes into (shutdown.hpp)
constexpr char kDebrisDna[] = "expendables_data/dna/defaultdebris.dnax";   // the Exploder's default, both games
constexpr double kDebrisLifetime = 3;   // s: its FastDebris gadget's TTL is 2.5
// 1.0 {ExpendableName, Min, Max, Chance} -> 3.0 "Expendables.<i>.<key>" (defaults 1, 1, 100)
inline constexpr std::pair<const char*, const char*> kSpawnFields[] = {
    {"ExpendableName", "Expendable"}, {"Min", "Min"}, {"Max", "Max"}, {"Chance", "Chance"}};

inline Value str(const std::string& s)
{
    Value v;
    v.t = Value::STR;
    v.s = s;
    return v;
}

// 3.0 draws pickups from '_32' batch models (FUN_00bd2d00): the DNA's. 'Playset' pays the zone currency (FUN_00ba1270).
inline int coinPickups(Value& ents, const CoinDnaOf& coinDna, int* coins)
{
    int n = 0;
    for (auto& [k, e] : *ents.tab) {
        const bool currency = e.t == Value::TAB && !_stricmp(e.str("RewardType").c_str(), "FastCurrency");
        if (!currency || e.get("FastCurrency.Type")) continue;
        const CoinDna dna = coinDna(e.str("DnaFile"));
        Value* model = e.get("Model");
        if (model && !dna.model.empty()) *model = str(dna.model);
        if (dna.spark) continue;
        e.tab->push_back({str("FastCurrency.Type"), str("Playset")});
        ++n;
        if (coins) ++*coins;
    }
    return n;
}

// 3.0 instances no actor model named under objects/ (capsules got no scene node); exploders/ works. vfs reads it back.
inline int actorModelsToExploders(Value& ents)
{
    int n = 0;
    for (auto& [k, e] : *ents.tab) {
        if (e.t != Value::TAB || !_stricmp(e.str("RewardType").c_str(), "FastCurrency")) continue;
        Value* m = e.get("Model");
        const bool objects = m && m->t == Value::STR && !_strnicmp(m->s.c_str(), kObjects, kObjectsLen);
        if (!objects) continue;
        *m = str(kExploders + m->s.substr(kObjectsLen));
        ++n;
    }
    return n;
}

inline Value num(double n)
{
    Value v;
    v.t = Value::NUM;
    v.n = n;
    return v;
}

// '<Name>_Debris10': an actor of DebrisModel (its 1.0 path) with the Exploder's DebrisDNAFile, living kDebrisLifetime.
inline Value debrisEntry(const std::string& name, const std::string& model, const std::string& dna)
{
    Value e;
    e.t = Value::TAB;
    e.tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
    const std::pair<const char*, Value> fields[] = {
        {"Name", str(name + kDebrisSuffix)}, {"Model", str(model)}, {"DnaFile", str(dna.empty() ? kDebrisDna : dna)},
        {"Lifetime", num(kDebrisLifetime)}, {"Respawn", num(-1)}, {"LocalOnly", num(1)}, {"Amount", num(1)},
        {"RewardType", str("EXPENDABLE")}};
    for (auto& [k, v] : fields) e.tab->push_back({str(k), v});
    return e;
}

// 1.0 explodes into an actor of the entry's DebrisModel and DebrisDNAFile (FUN_00f544f0); 3.0 never does. -> added
inline int debrisEntries(Value& ents, const Readers& r, Counts& c)
{
    std::vector<Value> add;
    double last = 0;
    for (auto& [k, e] : *ents.tab) {
        if (k.t == Value::NUM && k.n > last) last = k.n;
        if (e.t != Value::TAB) continue;
        const std::string d = e.str("DebrisModel"), name = e.str("Name");
        if (d.empty() || name.empty() || (r.has10 && !r.has10(d))) continue;
        // objects/ models get no scene node in 3.0 (actorModelsToExploders)
        const std::string m = _strnicmp(d.c_str(), kObjects, kObjectsLen) ? d : kExploders + d.substr(kObjectsLen);
        const std::string dna = r.dna ? r.dna(e.str("DnaFile")).debrisDna : "";
        add.push_back(debrisEntry(name, m, dna));
        c.debrisDefs.push_back({name, m, dna.empty() ? kDebrisDna : dna});
    }
    for (auto& v : add) ents.tab->push_back({num(++last), v});
    return (int)add.size();
}

// A group's ExpendablesSpawned rows as 3.0's flat keys, appended to the group.
inline void spawnKeys(Value& g, const Value& spawned, int* over)
{
    const Value sp = spawned;   // a copy sharing the table: the pushes below move g's entries
    for (int i = 1;; ++i) {
        Value key;
        key.t = Value::NUM;
        key.n = i;
        const Value* e = sp.get(key);
        if (!e) break;
        if (i > kSpawnKeys) {
            if (over) ++*over;
            continue;
        }
        std::string p = "Expendables." + std::to_string(i) + ".";
        for (auto [from, to] : kSpawnFields)
            if (const Value* v = e->get(from)) g.tab->push_back({str(p + to), *v});
    }
}

}  // namespace detail

// 1.0 rows ExpendablesSpawned = {{ExpendableName, Min, Max, Chance}, ..}, which 3.0 ignores: crates gave no coins.
inline int groups30(const std::string& chunk, std::string& out, Counts* counts, const Readers& r)
{
    using luachunk::Value;
    auto G = luachunk::eval(chunk);
    auto pkg = G.find("ExpendablesPackage");
    if (pkg == G.end()) return 0;
    Counts c;
    int n = 0;
    Value* ents = pkg->second.get("Expendables");
    const bool table = ents && ents->t == Value::TAB;
    if (table) c.models = detail::actorModelsToExploders(*ents);
    // after the move: a debris model keeps its 1.0 path, the one the zone loads
    if (table) c.debris = detail::debrisEntries(*ents, r, c);
    if (r.coins && r.dna && table) n += detail::coinPickups(*ents, r.dna, &c.coins);
    Value* groups = pkg->second.get("Groups");
    if (groups && groups->t == Value::TAB)
        for (auto& [k, g] : *groups->tab) {
            const Value* found = g.get("ExpendablesSpawned");
            if (g.t != Value::TAB || !found || found->t != Value::TAB) continue;
            detail::spawnKeys(g, *found, &c.over);
            ++n;
        }
    n += c.models + c.debris;
    if (counts) *counts = std::move(c);
    if (!n) return 0;
    out.clear();
    for (auto& [name, v] : G) out += name + " = " + luachunk::text(v) + "\n";
    return n;
}

}  // namespace di1::groups
