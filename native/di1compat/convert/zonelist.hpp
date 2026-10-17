// A zone list's text (gamedb/core/zonelist.lua): its Records, ids, OrderedPlaysets; records added. lua_lex, luachunk.
// Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../base/lua_lex.hpp"
#include "../formats/luachunk.hpp"

// Records are written in zonereg.py's form, the form 3.0's registration loop reads (FUN_0139e820).
namespace zonelist {

// What a zone list text holds: ZoneList.Records' tables, their playset and zone names, the ids, OrderedPlaysets.
struct Text {
    std::map<std::string, size_t> recs;          // Records table name -> its '{'
    std::set<std::string> playsets, zones;       // PlaysetProperties and Zones entry names
    std::set<int> used;                          // PlaysetUniqueIDs taken
    std::string ordered;                         // OrderedPlaysets' table text
    size_t opOpen = std::string::npos;           // its '{'
};

// Records added to a zone list: zone records, PlaysetProperties records, OrderedPlaysets entries.
struct Additions {
    std::string zones, playsets, ordered;
};

// The direct entry names of Records table `table` (none when the text has no such table).
inline std::set<std::string> names(const std::string& t, const std::vector<lualex::Tok>& toks, const Text& z,
                                   const char* table)
{
    std::set<std::string> s;
    if (!z.recs.count(table)) return s;
    for (auto& e : lualex::tableEntries(t, toks, z.recs.at(table))) s.insert(e.first);
    return s;
}

// The zone list text t; nullopt without a ZoneList table.
inline std::optional<Text> scan(const std::string& t)
{
    auto toks = lualex::tokens(t);
    auto zl = lualex::globalTable(t, toks, "ZoneList");
    if (zl.first == std::string::npos) return std::nullopt;
    Text z;
    for (auto& e : lualex::tableEntries(t, toks, zl.first)) {
        if (e.first != "Records") continue;
        for (auto& r : lualex::tableEntries(t, toks, t.find('{', e.second.first)))
            z.recs[r.first] = t.find('{', r.second.first);
    }
    z.playsets = names(t, toks, z, "PlaysetProperties");
    z.zones = names(t, toks, z, "Zones");
    for (auto& k : toks)
        if (k.kind == 'i' && !t.compare(k.at, k.len, "PlaysetUniqueID") && lualex::assignedAt(t, k.at + k.len))
            z.used.insert(atoi(t.c_str() + t.find('=', k.at) + 1));
    if (!z.recs.count("GlobalData")) return z;
    for (auto& e : lualex::tableEntries(t, toks, z.recs["GlobalData"])) {
        if (e.first != "OrderedPlaysets") continue;
        z.opOpen = t.find('{', e.second.first);
        z.ordered = t.substr(z.opOpen, e.second.second - z.opOpen);
    }
    return z;
}

// A record as zonereg.py's record_text writes it: keys sorted; over's values, then the record's own.
inline std::string recordText(const std::string& name, const luachunk::Value& r,
                              const std::map<std::string, std::string>& over)
{
    std::map<std::string, std::string> kv = over;
    for (auto& e : *r.tab) {
        std::string k = e.first.t == luachunk::Value::STR ? e.first.s : luachunk::text(e.first);
        if (!over.count(k)) kv[k] = luachunk::text(e.second);
    }
    std::string s = "\t\t\t" + name + " = \n\t\t\t{\n";
    for (auto& [k, v] : kv)
        s += "\t\t\t\t" + (luachunk::ident(k) ? k : "[" + luachunk::quote(k) + "]") + " = " + v + ",\n";
    return s + "\t\t\t},\n";
}

// The lowest PlaysetUniqueID above 0 that z does not use, taken from then on.
inline int takeId(Text& z)
{
    int id = 0;
    while (z.used.count(++id)) {}
    z.used.insert(id);
    return id;
}

inline std::string orderedEntry(const std::string& name)
{
    return "\t\t\t\t{ Name = " + luachunk::quote(name) +
           ", availableInConsumerBuild = true, availableInShippingBuild = true,\t\t\t\t},\n";
}

// Where OrderedPlaysets lists the last of these names (its '{' if none): new entries go on the next line.
inline size_t orderedAfter(const Text& z, const std::vector<std::string>& listed)
{
    size_t at = z.opOpen;
    for (auto& n : listed) {
        const size_t p = z.ordered.find(luachunk::quote(n));
        if (p != std::string::npos) at = (std::max)(at, z.opOpen + p);
    }
    return at;
}

// The additions on the line after each table's '{' (OrderedPlaysets: after opAt), the last first.
inline void insert(std::string& t, Text& z, const Additions& a, size_t opAt)
{
    auto lineAfter = [&](size_t at) { size_t nl = t.find('\n', at); return nl == std::string::npos ? at + 1 : nl + 1; };
    std::vector<std::pair<size_t, std::string>> ed;
    if (!a.zones.empty()) ed.push_back({lineAfter(z.recs["Zones"]), a.zones});
    if (!a.playsets.empty()) ed.push_back({lineAfter(z.recs["PlaysetProperties"]), a.playsets});
    if (!a.ordered.empty()) ed.push_back({lineAfter(opAt), a.ordered});
    std::sort(ed.begin(), ed.end(), [](auto& x, auto& y) { return x.first > y.first; });
    for (auto& e : ed) t.insert(e.first, e.second);
}

}  // namespace zonelist
