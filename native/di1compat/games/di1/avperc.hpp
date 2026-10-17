// avperc10: the perceptions the 1.0 avatar's list enables in a zone and 3.0's descendant list lacks. Any thread.
#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../formats/luaconst.hpp"

namespace di1::avperc {

// "Name" (kind 'p', always) or a zone entry {prefixes, ifMatch, ifNot [, "hostonly"]} (kind 'z'; FUN_0154a470).
struct PercEntry {
    char kind = 0;
    std::string on, off, pre;
    bool host = false;
};
using PercList = std::vector<PercEntry>;
using PercAdds = std::vector<std::pair<std::string, bool>>;   // name, hostonly

// Every distinct Perceptions list of a .btx chunk, in walk order (tables in insertion order, shared tables once).
inline bool percLists(const std::string& chunk, std::vector<PercList>& out, std::string* why = nullptr);
// lower-case name -> (name, hostonly) a list enables in a zone (zone lower-case)
inline std::map<std::string, std::pair<std::string, bool>> percActive(const PercList& l, const std::string& zone);
// Per 3.0 list: the 1.0 list it descends from (-1: none) and the additions for the zone.
inline std::vector<std::pair<int, PercAdds>> percAdds(const std::vector<PercList>& l10,
                                                      const std::vector<PercList>& l30, const std::string& zone);

namespace detail {

using luaconst::LooseTable;
using luaconst::LooseVal;
constexpr size_t kActivity = sizeof("activity") - 1;

// kv's value under the number key i, nullptr if none.
inline const LooseVal* numberKey(const std::vector<std::pair<LooseVal, LooseVal>>& kv, double i)
{
    for (auto& [a, b] : kv)
        if (a.t == 'n' && a.n == i) return &b;
    return nullptr;
}

// One list entry; false for a layer {name, "FullBody"} or anything else.
inline bool entry(const std::vector<LooseTable>& tabs, const LooseVal& v, PercEntry& e)
{
    if (v.t == 's') {
        e.kind = 'p';
        e.on = v.s;
        return true;
    }
    if (v.t != 't') return false;
    const auto& kv = tabs[v.tab].kv;
    const LooseVal *p = numberKey(kv, 1), *m = numberKey(kv, 2), *n = numberKey(kv, 3), *f = numberKey(kv, 4);
    const bool parts = p && m && n;
    if (!parts || p->t != 's') return false;
    e.kind = 'z';
    e.pre = p->s;
    if (m->t == 's') e.on = m->s;
    if (n->t == 's') e.off = n->s;
    e.host = f && f->t == 's' && text::lower(f->s) == "hostonly";
    return true;
}

// One Perceptions table as a list, kept unless an equal list was kept before.
inline void addList(const std::vector<LooseTable>& tabs, const LooseTable& t, std::set<std::string>& have,
                    std::vector<PercList>& out)
{
    PercList l;
    std::string key;
    for (auto& [k, v] : t.kv) {
        PercEntry e;
        if (!entry(tabs, v, e)) continue;
        key += std::string(1, e.kind) + e.pre + '\x1f' + e.on + '\x1f' + e.off + (e.host ? "!" : "") + '\x1e';
        l.push_back(e);
    }
    if (auto [at, fresh] = have.insert(key); fresh) out.push_back(l);
}

// The lists under table t and the tables it holds, depth first; seen: the tables walked.
inline void walk(const std::vector<LooseTable>& tabs, int t, std::set<int>& seen, std::set<std::string>& have,
                 std::vector<PercList>& out)
{
    if (auto [at, fresh] = seen.insert(t); !fresh) return;
    for (auto& [k, v] : tabs[t].kv) {
        if (v.t != 't') continue;
        if (k.t == 's' && k.s == "Perceptions") addList(tabs, tabs[v.tab], have, out);
        walk(tabs, v.tab, seen, have, out);
    }
}

// Whether the zone starts with one of the ','/' '-separated prefixes, as written (strstr == 0).
inline bool zoneMatches(const std::string& pre, const std::string& zone)
{
    for (size_t a = 0, b; a < pre.size(); a = b + 1) {
        b = pre.find_first_of(", ", a);
        if (b == std::string::npos) b = pre.size();
        if (b > a && !zone.compare(0, b - a, pre, a, b - a)) return true;
    }
    return false;
}

// The lower-case names a list's zone entries name, either side.
inline std::set<std::string> zoneNames(const PercList& l)
{
    std::set<std::string> s;
    for (auto& e : l) {
        if (e.kind != 'z') continue;
        if (!e.on.empty()) s.insert(text::lower(e.on));
        if (!e.off.empty()) s.insert(text::lower(e.off));
    }
    return s;
}

// The 1.0 list sharing the most zone-entry names with z, unique and above 0; -1 if none.
inline int descendsFrom(const std::vector<std::set<std::string>>& z10, const std::set<std::string>& z)
{
    int best = -1;
    size_t bs = 0, second = 0;
    for (size_t i = 0; i < z10.size(); ++i) {
        size_t s = 0;
        for (auto& n : z10[i]) s += z.count(n);
        if (s > bs) {
            second = bs;
            bs = s;
            best = (int)i;
        } else if (s > second) {
            second = s;
        }
    }
    return bs == 0 || second == bs ? -1 : best;
}

}  // namespace detail

inline bool percLists(const std::string& chunk, std::vector<PercList>& out, std::string* why)
{
    std::vector<luaconst::LooseTable> tabs;
    std::vector<std::pair<std::string, luaconst::LooseVal>> g;
    if (!luaconst::evalLoose(chunk, tabs, g, why)) return false;
    std::set<int> seen;
    std::set<std::string> have;
    for (auto& [k, v] : g)
        if (v.t == 't') detail::walk(tabs, v.tab, seen, have, out);
    return true;
}

inline std::map<std::string, std::pair<std::string, bool>> percActive(const PercList& l, const std::string& zone)
{
    std::map<std::string, std::pair<std::string, bool>> out;
    for (auto& e : l) {
        if (e.kind == 'p') {
            out.emplace(text::lower(e.on), std::make_pair(e.on, false));
            continue;
        }
        const std::string& n = detail::zoneMatches(e.pre, zone) ? e.on : e.off;
        if (!n.empty()) out.emplace(text::lower(n), std::make_pair(n, e.host));
    }
    return out;
}

// 1.0's ActivityInitiate and the like are 3.0's Challenge* (the giver handlers), already in the 3.0 list.
inline std::vector<std::pair<int, PercAdds>> percAdds(const std::vector<PercList>& l10,
                                                      const std::vector<PercList>& l30, const std::string& zone)
{
    std::vector<std::set<std::string>> z10;
    for (auto& l : l10) z10.push_back(detail::zoneNames(l));
    std::vector<std::pair<int, PercAdds>> out;
    std::string zl = text::lower(zone);
    for (auto& l : l30) {
        int best = detail::descendsFrom(z10, detail::zoneNames(l));
        if (best < 0) {
            out.push_back({-1, {}});
            continue;
        }
        auto a10 = percActive(l10[best], zl), a30 = percActive(l, zl);
        PercAdds add;
        for (auto& [k, v] : a10) {
            const bool renamed = k.compare(0, detail::kActivity, "activity") == 0 &&
                                 a30.count("challenge" + k.substr(detail::kActivity));
            if (!a30.count(k) && !renamed) add.push_back(v);
        }
        out.push_back({best, add});
    }
    return out;
}

}  // namespace di1::avperc
