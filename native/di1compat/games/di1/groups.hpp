// rewards10: 1.0 expendable groups in the keys 3.0's group reader takes; currency10's coin pickups. Any thread.
#pragma once
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include "../../formats/luachunk.hpp"

namespace di1::groups {

// A package's groups with 3.0's flat spawn keys (FUN_01603b10); coins typed with isSpark (currency10). 0: unchanged.
inline int groups30(const std::string& chunk, std::string& out, int* over = nullptr,
                    const std::function<bool(const std::string& dnaFile)>& isSpark = nullptr, int* coins = nullptr);

namespace detail {

using luachunk::Value;

constexpr int kSpawnKeys = 4;   // Expendables.1 .. .4: a 5th spawn has no 3.0 key and counts in *over
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

// Type 'Playset' pays the zone's currency (FUN_00ba1270 -> 0, FUN_00e08300), as 1.0 coins did; sparks keep 'Spark'.
inline int coinPickups(Value& ents, const std::function<bool(const std::string&)>& isSpark, int* coins)
{
    int n = 0;
    for (auto& [k, e] : *ents.tab) {
        const bool currency = e.t == Value::TAB && !_stricmp(e.str("RewardType").c_str(), "FastCurrency");
        if (!currency || e.get("FastCurrency.Type") || isSpark(e.str("DnaFile"))) continue;
        e.tab->push_back({str("FastCurrency.Type"), str("Playset")});
        ++n;
        if (coins) ++*coins;
    }
    return n;
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
inline int groups30(const std::string& chunk, std::string& out, int* over,
                    const std::function<bool(const std::string& dnaFile)>& isSpark, int* coins)
{
    using luachunk::Value;
    auto G = luachunk::eval(chunk);
    auto pkg = G.find("ExpendablesPackage");
    if (pkg == G.end()) return 0;
    int n = 0;
    Value* ents = pkg->second.get("Expendables");
    if (isSpark && ents && ents->t == Value::TAB) n += detail::coinPickups(*ents, isSpark, coins);
    Value* groups = pkg->second.get("Groups");
    if (groups && groups->t == Value::TAB)
        for (auto& [k, g] : *groups->tab) {
            const Value* found = g.get("ExpendablesSpawned");
            if (g.t != Value::TAB || !found || found->t != Value::TAB) continue;
            detail::spawnKeys(g, *found, over);
            ++n;
        }
    if (!n) return 0;
    out.clear();
    for (auto& [name, v] : G) out += name + " = " + luachunk::text(v) + "\n";
    return n;
}

}  // namespace di1::groups
