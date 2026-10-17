// Playset DataMaps: DataMap.<Group>.<Name> = {Files, Includes} read, closed over Includes, written. Any thread.
#pragma once
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../base/text.hpp"
#include "luaconst.hpp"

namespace datamap {

struct Record {
    std::vector<std::string> files, includes;
};
// Keyed by lower-case (group, name): the engine compares both case-insensitively (FUN_006fd430, FUN_00705860).
using DataMap = std::map<std::pair<std::string, std::string>, Record>;
using Key = std::pair<std::string, std::string>;

// The groups in the engine's kind order (FUN_006fd430: 0 Playset .. 12 Include), as written and as keys.
struct Kind {
    const char* group;
    const char* key;
};
inline constexpr Kind kKinds[] = {
    {"Playset", "playset"}, {"GameMode", "gamemode"}, {"Zone", "zone"}, {"Actor", "actor"}, {"Toy", "toy"},
    {"Activity", "activity"}, {"ZoneBackgroundLoad", "zonebackgroundload"}, {"Realm", "realm"},
    {"Munition", "munition"}, {"SurfaceImpact", "surfaceimpact"}, {"Costume", "costume"}, {"PowerDisc", "powerdisc"},
    {"Include", "include"}};

// A DataMap chunk's records; a repeated key keeps its last value, as in Lua. All shipped ones evaluate (luaops.py).
inline bool parse(const std::string& chunk, DataMap& out, std::string* why = nullptr);
// 'GameDB|<path>|1|<type>' -> (type, path)
inline bool gamedbEntry(const std::string& e, std::pair<std::string, std::string>& tp);
// The record's DNA path, lower case with '/', without .dnax; "" if it has none.
inline std::string dnaOf(const Record& r);
// The records reachable from k through Includes ("<name>|<Group>"), the engine's expansion (FUN_007058c0).
inline void reach(const DataMap& m, const Key& k, std::set<Key>& seen);
// The DataMap as Lua text, its groups in kind order; a group outside kKinds is not written.
inline std::string text(const DataMap& m);

namespace detail {

// A table's string values, in order.
inline void strings(const std::vector<luaconst::LooseTable>& tabs, const luaconst::LooseVal& t,
                    std::vector<std::string>& to)
{
    if (t.t != 't') return;
    for (auto& [k, v] : tabs[t.tab].kv)
        if (v.t == 's') to.push_back(v.s);
}

// The records of one group table.
inline void group(const std::vector<luaconst::LooseTable>& tabs, const std::string& name,
                  const luaconst::LooseVal& g, DataMap& out)
{
    for (auto& [nk, nv] : tabs[g.tab].kv) {
        if (nk.t != 's' || nv.t != 't') continue;
        Record r;
        for (auto& [fk, fv] : tabs[nv.tab].kv) {
            if (fk.s == "Files") strings(tabs, fv, r.files);
            else if (fk.s == "Includes") strings(tabs, fv, r.includes);
        }
        out[{text::lower(name), text::lower(nk.s)}] = std::move(r);
    }
}

inline std::string recordText(const std::string& name, const Record& r)
{
    std::string body = "\t\t[" + luaconst::quoteAscii(name) + "] =\n\t\t{\n\t\t\tFiles =\n\t\t\t{\n";
    for (auto& f : r.files) body += "\t\t\t\t" + luaconst::quoteAscii(f) + ",\n";
    body += "\t\t\t},\n\t\t\tIncludes =\n\t\t\t{\n";
    for (auto& i : r.includes) body += "\t\t\t\t" + luaconst::quoteAscii(i) + ",\n";
    return body + "\t\t\t},\n\t\t},\n";
}

}  // namespace detail

inline bool parse(const std::string& chunk, DataMap& out, std::string* why)
{
    std::vector<luaconst::LooseTable> tabs;
    std::vector<std::pair<std::string, luaconst::LooseVal>> g;
    if (!luaconst::evalLoose(chunk, tabs, g, why)) return false;
    const luaconst::LooseVal* dm = nullptr;
    for (auto& [k, v] : g)
        if (k == "DataMap" && v.t == 't') dm = &v;
    if (!dm) {
        if (why) *why = "no DataMap table";
        return false;
    }
    for (auto& [gk, gv] : tabs[dm->tab].kv)
        if (gk.t == 's' && gv.t == 't') detail::group(tabs, gk.s, gv, out);
    return true;
}

inline bool gamedbEntry(const std::string& e, std::pair<std::string, std::string>& tp)
{
    constexpr size_t kKind = sizeof("GameDB|") - 1;
    if (e.compare(0, kKind, "GameDB|")) return false;
    size_t a = e.find('|', kKind), b = a == std::string::npos ? a : e.find('|', a + 1);
    if (b == std::string::npos) return false;
    tp = {e.substr(b + 1), e.substr(kKind, a - kKind)};
    return true;
}

inline std::string dnaOf(const Record& r)
{
    constexpr size_t kKind = sizeof("DNA|") - 1;
    constexpr size_t kExt = sizeof(".dnax") - 1;
    for (auto& f : r.files) {
        if (f.compare(0, kKind, "DNA|")) continue;
        std::string p = text::slashLower(f.substr(kKind));
        if (p.size() > kExt && !p.compare(p.size() - kExt, kExt, ".dnax")) p.resize(p.size() - kExt);
        return p;
    }
    return "";
}

inline void reach(const DataMap& m, const Key& k, std::set<Key>& seen)
{
    auto it = m.find(k);
    if (it == m.end()) return;
    if (auto [at, fresh] = seen.insert(k); !fresh) return;
    for (auto& inc : it->second.includes) {
        size_t bar = inc.find('|');
        const std::string group = bar == std::string::npos ? "include" : text::lower(inc.substr(bar + 1));
        reach(m, {group, text::lower(inc.substr(0, bar))}, seen);
    }
}

inline std::string text(const DataMap& m)
{
    std::string t = "DataMap =\n{\n";
    for (const Kind& g : kKinds) {
        std::string body;
        for (auto& [k, r] : m)
            if (k.first == g.key) body += detail::recordText(k.second, r);
        if (!body.empty()) t += std::string("\t") + g.group + " =\n\t{\n" + body + "\t},\n";
    }
    return t + "}\n";
}

}  // namespace datamap
