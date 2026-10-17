// zones10: 1.0's playsets in a 3.0 zone list, their currencies and zone prefixes. convert/zonelist. Any thread.
#pragma once
#include <windows.h>
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "../../convert/zonelist.hpp"
#include "../../formats/luachunk.hpp"

namespace di1 {

// A 1.0 playset: name, StartWorld, its zones sorted, and its PlaysetUniqueID when this registration added it.
struct Playset10 {
    std::string name, start;
    int id = 0;
    bool added = false;
    std::vector<std::string> zones;
};

// With add, t (3.0's zone list) gains each installed 1.0 playset it lacks, in 1.0's order. -> those t holds
inline std::vector<Playset10> playsets10(std::string& t, const luachunk::Value& zoneList10, bool add,
                                         const std::function<bool(const std::string&)>& worldExists);
// currency10: each inventoryFilter's 1.0 currency; 3.0's FUN_00e21850 answers INV_SPARKS for every filter.
inline std::map<std::string, std::string> currencies10(const luachunk::Value& zoneList10);

// A 1.0 playset's start world is in the 1.0 install: assets\worlds\<zone>.zip or the unpacked folder.
inline bool worldInstalled(const char* install10, const std::string& zone)
{
    std::string w = std::string(install10) + "\\assets\\worlds\\" + zone;
    const bool zip = GetFileAttributesA((w + ".zip").c_str()) != INVALID_FILE_ATTRIBUTES;
    return zip || GetFileAttributesA(w.c_str()) != INVALID_FILE_ATTRIBUTES;
}

struct Zones10Options {
    bool add = false;          // zones10: register the 1.0 playsets 3.0's list lacks (off: report them only)
    bool currencies = false;   // currency10: also each playset's currency
    std::function<bool(const std::string& zone)> world;   // a zone's world is installed
};

struct Zones10 {
    std::vector<Playset10> playsets;
    std::map<std::string, std::string> currencies;   // currency10 only
};

// t: the 3.0 zone list (records added to it); zoneList10: 1.0's chunk. Throws when the chunk does not evaluate.
inline Zones10 zones10(std::string& t, const std::string& zoneList10, const Zones10Options& o)
{
    Zones10 z;
    auto g = luachunk::eval(zoneList10);
    z.playsets = playsets10(t, g["ZoneList"], o.add, o.world);
    if (o.currencies) z.currencies = currencies10(g["ZoneList"]);
    return z;
}

// The playsets' zone name prefixes ("IN_", "MU_" ...: their missions' names start so), first seen first.
inline std::vector<std::string> zonePrefixes(const std::vector<Playset10>& playsets)
{
    std::vector<std::string> pre;
    for (auto& x : playsets)
        for (auto& zone : x.zones) {
            size_t us = zone.find('_');
            if (us == std::string::npos) continue;
            std::string p = zone.substr(0, us + 1);
            if (std::find(pre.begin(), pre.end(), p) == pre.end()) pre.push_back(p);
        }
    return pre;
}

namespace detail {

// A PlaysetProperties record's MissionParams.DefaultRewardType, "" for none.
inline std::string rewardType(const luachunk::Value& p)
{
    const luachunk::Value* mp = p.get("MissionParams");
    return mp ? mp->str("DefaultRewardType") : "";
}
using WorldFn = std::function<bool(const std::string&)>;

// 1.0's playset order: GlobalData.OrderedPlaysets, then the other PlaysetProperties names, sorted.
inline std::vector<std::string> order10(const luachunk::Value& pp10, const luachunk::Value* op10)
{
    using luachunk::Value;
    std::vector<std::string> order, rest;
    if (op10 && op10->t == Value::TAB)
        for (auto& e : *op10->tab)
            if (e.second.t == Value::TAB) order.push_back(e.second.str("Name"));
    for (auto& e : *pp10.tab)
        if (e.first.t == Value::STR && std::find(order.begin(), order.end(), e.first.s) == order.end())
            rest.push_back(e.first.s);
    std::sort(rest.begin(), rest.end());
    order.insert(order.end(), rest.begin(), rest.end());
    return order;
}

// A PlaysetProperties record of Type "Playset" whose StartWorld is one of its zones and installed; else nullopt.
inline std::optional<Playset10> playset10(const std::string& name, const luachunk::Value& pp10,
                                          const luachunk::Value& z10, const WorldFn& worldExists)
{
    using luachunk::Value;
    const Value* pp = pp10.get(name);
    if (!pp || pp->t != Value::TAB || pp->str("Type") != "Playset") return std::nullopt;
    Playset10 ps{name, pp->str("StartWorld")};
    for (auto& e : *z10.tab) {
        const bool zone = e.first.t == Value::STR && e.second.t == Value::TAB;
        if (zone && e.second.str("PlaysetProperties") == name) ps.zones.push_back(e.first.s);
    }
    std::sort(ps.zones.begin(), ps.zones.end());
    const bool own = std::find(ps.zones.begin(), ps.zones.end(), ps.start) != ps.zones.end();
    if (!own || !worldExists(ps.start)) return std::nullopt;
    return ps;
}

// The keys 3.0 reads that the 1.0 record lacks or spells otherwise, as zonereg.py wrote them.
inline std::map<std::string, std::string> overrides10(const std::string& name, int id, const luachunk::Value& pp)
{
    std::string dict = pp.str("playsetDict");
    std::map<std::string, std::string> over = {
        {"PlaysetUniqueID", std::to_string(id)}, {"WorldIsPlayset", "true"},
        {"SaveGameKey", luachunk::quote(name)}, {"_ShowInMenu_", "true"},
        {"availableInConsumerBuild", "true"}, {"availableInShippingBuild", "true"}};
    if (!dict.empty()) {
        over["SaveGamePrefix"] = luachunk::quote(dict);
        over["LoadingScreens"] = luachunk::quote(dict + "/" + pp.str("LoadingScreen"));
    }
    return over;
}

// A playset the text lacks: its id, record and missing zones, and an OrderedPlaysets entry unless listed.
inline void addPlayset(zonelist::Text& z, Playset10& ps, const luachunk::Value& pp, const luachunk::Value& z10,
                       zonelist::Additions& a)
{
    ps.id = zonelist::takeId(z);
    ps.added = true;
    a.playsets += zonelist::recordText(ps.name, pp, overrides10(ps.name, ps.id, pp));
    for (auto& zone : ps.zones)
        if (!z.zones.count(zone)) a.zones += zonelist::recordText(zone, *z10.get(zone), {});
    if (z.ordered.find(luachunk::quote(ps.name)) == std::string::npos) a.ordered += zonelist::orderedEntry(ps.name);
}

}  // namespace detail

inline std::vector<Playset10> playsets10(std::string& t, const luachunk::Value& zoneList10, bool add,
                                         const std::function<bool(const std::string&)>& worldExists)
{
    using luachunk::Value;
    std::vector<Playset10> out;
    const Value* rec = zoneList10.get("Records");
    const Value *pp10 = rec ? rec->get("PlaysetProperties") : nullptr, *z10 = rec ? rec->get("Zones") : nullptr;
    const Value* gd10 = rec ? rec->get("GlobalData") : nullptr;
    const Value* op10 = gd10 ? gd10->get("OrderedPlaysets") : nullptr;
    const bool both = pp10 && z10;
    if (!both || pp10->t != Value::TAB || z10->t != Value::TAB) return out;
    const std::vector<std::string> order = detail::order10(*pp10, op10);
    std::optional<zonelist::Text> z = zonelist::scan(t);
    if (!z) return out;
    const bool tables = z->recs.count("Zones") && z->recs.count("PlaysetProperties") && z->opOpen != std::string::npos;
    add = add && tables;
    zonelist::Additions adds;
    for (auto& name : order) {
        std::optional<Playset10> ps = detail::playset10(name, *pp10, *z10, worldExists);
        if (!ps) continue;
        const bool present = z->playsets.count(name) > 0;
        if (!present && !add) continue;
        if (!present) detail::addPlayset(*z, *ps, *pp10->get(name), *z10, adds);
        out.push_back(*ps);
    }
    std::vector<std::string> listed;   // the ones the text held already keep their OrderedPlaysets place
    for (auto& ps : out)
        if (!ps.added) listed.push_back(ps.name);
    zonelist::insert(t, *z, adds, zonelist::orderedAfter(*z, listed));
    return out;
}

inline std::map<std::string, std::string> currencies10(const luachunk::Value& zoneList10)
{
    std::map<std::string, std::string> out;
    const luachunk::Value* rec = zoneList10.get("Records");
    const luachunk::Value* pp = rec ? rec->get("PlaysetProperties") : nullptr;
    if (!pp || pp->t != luachunk::Value::TAB) return out;
    for (auto& [k, p] : *pp->tab) {
        std::string filter = p.str("inventoryFilter"), cur = detail::rewardType(p);
        if (!filter.empty() && !cur.empty()) out[filter] = cur;
    }
    // a zone record may carry the filter itself (LR_Colby); 3.0 asks the current zone's (FUN_0139e0b0)
    const luachunk::Value* zs = rec->get("Zones");
    if (!zs || zs->t != luachunk::Value::TAB) return out;
    for (auto& [k, z] : *zs->tab) {
        std::string filter = z.str("inventoryFilter");
        const luachunk::Value* p = pp->get(z.str("PlaysetProperties"));
        const bool fresh = !filter.empty() && p && !out.count(filter);
        if (fresh && !detail::rewardType(*p).empty()) out[filter] = detail::rewardType(*p);
    }
    return out;
}

}  // namespace di1
