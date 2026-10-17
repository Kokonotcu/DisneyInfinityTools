// gamedb30, playset30, rewards10: what a 1.0 playset's DataMap cannot name in 3.0. formats/datamap. Any thread.
#pragma once
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../formats/datamap.hpp"

namespace di1::supplement {

using Table = std::pair<std::string, std::string>;   // a GameDB entry: (type, path)

// 3.0 includes DataMap.Playset.<name> for every playset (FUN_0070d9b0); a 1.0 DataMap has no Playset group.
struct GameDb {
    std::vector<Table> playset;                         // ToyBox's Playset tables of types the 1.0 map never names
    std::map<std::string, std::vector<Table>> actor;    // 1.0 actor -> the 3.0 tables its served 3.0 DNA reads
    datamap::DataMap m10;
};
// The tables taken from 3.0's ToyBox map (pstypes.py, psdna.py).
inline GameDb gamedb30(const datamap::DataMap& m10, const datamap::DataMap& m30);

constexpr size_t kExpendablesKind = sizeof("Expendables|") - 1;
// rewards10: the 'Expendables|' entries of 1.0's CoreDataMap Zone._core_, which a 3.0 session never reads.
inline std::vector<std::string> coreExpendables10(const datamap::DataMap& core10);

// playset30: the Playset record a 1.0 DataMap lacks.
struct PlaysetRecord {
    std::string playset;
    std::vector<Table> tables;        // gamedb30's Playset tables
    std::vector<std::string> files;   // more entries as they are: rewards10's packages
    bool hudMarker = true;            // hudmarker10: + Include.hudmarker_inc, 3.0's mission compass
};
// The record and the 3.0 standard includes as one more DataMap, in Lua text (3.0 loads GameDB Lua text too).
inline std::string playsetText(const PlaysetRecord& p, const datamap::DataMap& toybox, const datamap::DataMap& other);

namespace detail {

// A GameDB entry of a type the 1.0 DataMap does not name.
inline bool newType(const std::string& f, const std::set<std::string>& types10, Table& tp)
{
    return datamap::gamedbEntry(f, tp) && !types10.count(tp.first);
}

// The tables reachable from 3.0's actor record actor30, each once.
inline std::vector<Table> actorTables(const datamap::DataMap& m30, const std::string& actor30,
                                      const std::set<std::string>& types10)
{
    std::set<datamap::Key> seen;
    datamap::reach(m30, {"actor", actor30}, seen);
    std::set<Table> got;
    std::vector<Table> out;
    Table tp;
    for (auto& rk : seen)
        for (auto& f : m30.at(rk).files) {
            if (!newType(f, types10, tp)) continue;
            if (auto [at, fresh] = got.insert(tp); fresh) out.push_back(tp);
        }
    return out;
}

// The 3.0 actor whose DNA is d, else d without a trailing _ps; null if none.
inline const std::string* actorByDna(const std::map<std::string, std::string>& byDna, const std::string& d)
{
    auto hit = byDna.find(d);
    const bool ps = d.size() > 3 && !d.compare(d.size() - 3, 3, "_ps");
    if (hit == byDna.end() && ps) hit = byDna.find(d.substr(0, d.size() - 3));
    return hit == byDna.end() ? nullptr : &hit->second;
}

// 3.0's standard Playset includes: identical in every map that defines them, avatar.auto taken from Toy Box.
struct StandardInclude {
    bool toybox;
    const char* name;
};
inline constexpr StandardInclude kStandardIncludes[] = {
    {true, "avatar.auto"}, {true, "avatar.btx.auto"}, {false, "includeforallnontoyboxplaysets"},
    {true, "hudmarker_inc"}};

}  // namespace detail

inline GameDb gamedb30(const datamap::DataMap& m10, const datamap::DataMap& m30)
{
    GameDb g;
    g.m10 = m10;
    std::set<std::string> types10;
    Table tp;
    for (auto& [k, r] : m10)
        for (auto& f : r.files)
            if (datamap::gamedbEntry(f, tp)) types10.insert(tp.first);
    if (auto pt = m30.find({"playset", "toybox"}); pt != m30.end())
        for (auto& f : pt->second.files)   // assetDefs: the asset defs fallback; RumpusSpawnData: Toy Box only
            if (detail::newType(f, types10, tp) && tp.first != "assetDefs" && tp.first != "RumpusSpawnData")
                g.playset.push_back(tp);
    std::map<std::string, std::string> byDna;   // DataMap is ordered: the first record per DNA has the smallest name
    for (auto& [k, r] : m30) {
        std::string d = k.first == "actor" ? datamap::dnaOf(r) : "";
        if (!d.empty()) byDna.emplace(d, k.second);
    }
    for (auto& [k, r] : m10) {
        std::string d = k.first == "actor" ? datamap::dnaOf(r) : "";
        const std::string* hit = d.empty() ? nullptr : detail::actorByDna(byDna, d);
        if (!hit) continue;
        std::vector<Table> t = detail::actorTables(m30, *hit, types10);
        if (!t.empty()) g.actor[k.second] = std::move(t);
    }
    return g;
}

inline std::vector<std::string> coreExpendables10(const datamap::DataMap& core10)
{
    auto r = core10.find({"zone", "_core_"});
    std::vector<std::string> out;
    if (r == core10.end()) return out;
    for (auto& f : r->second.files)
        if (!f.compare(0, kExpendablesKind, "Expendables|")) out.push_back(f);
    return out;
}

inline std::string playsetText(const PlaysetRecord& p, const datamap::DataMap& toybox, const datamap::DataMap& other)
{
    datamap::DataMap out;
    datamap::Record& ps = out[{"playset", text::lower(p.playset)}];
    for (auto& [t, path] : p.tables) ps.files.push_back("GameDB|" + path + "|1|" + t);
    ps.files.insert(ps.files.end(), p.files.begin(), p.files.end());
    for (const detail::StandardInclude& inc : detail::kStandardIncludes) {
        if (!p.hudMarker && !strcmp(inc.name, "hudmarker_inc")) continue;
        const datamap::DataMap& m = inc.toybox ? toybox : other;
        std::set<datamap::Key> seen;
        datamap::reach(m, {"include", inc.name}, seen);
        if (!seen.empty()) ps.includes.push_back(std::string(inc.name) + "|Include");
        for (auto& k : seen) out.emplace(k, m.at(k));
    }
    return datamap::text(out);
}

}  // namespace di1::supplement
