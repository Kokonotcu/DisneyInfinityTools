// 1.0 Activity data in 3.0's Challenge system (plan SYS-6): type tables, beacons, zone list keys. base/lua_lex.
// Any thread.
#pragma once
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/lua_lex.hpp"

namespace di1::challenges {

// ActivityTypePackage keys as 3.0 reads them (1.0 FUN_00f938a0, 3.0 FUN_015c6400). -> renamed (_in.lua: 58)
inline int challengeTypes30(std::string& lua);
// t30 (3.0 ChallengeData.lua) gains the 1.0 beacons (DefaultActivityBeacons.lua) it lacks. -> records added
inline int beacons30(std::string& t30, const std::string& t10);
// Playset records naming ActivityTypeDataFile also get ChallengeTypeDataFile, the key 3.0 reads (0x143d603). -> records
inline int zonelist30(std::string& t);
// A normalised 'challengedata/activitytypedata_<x>.lua' (zonelist30 kept 1.0's file name) -> its 1.0 path, else "".
inline std::string challengeTypePath(const std::string& normPath);

namespace detail {

constexpr char kTypeFile10[] = "ActivityTypeDataFile";
constexpr char kTypeFile30[] = "ChallengeTypeDataFile";

// Whether the table around key token k holds kTypeFile30 at k's depth.
inline bool hasTypeFile30(const std::string& t, const std::vector<lualex::Tok>& toks, size_t k)
{
    const int d = toks[k].depth;
    auto named = [&](size_t j) {
        return toks[j].kind == 'i' && toks[j].depth == d && !t.compare(toks[j].at, toks[j].len, kTypeFile30);
    };
    bool has = false;
    for (size_t j = k; j-- > 0;) {
        if (toks[j].kind == '{' && toks[j].depth < d) break;
        if (named(j)) has = true;
    }
    for (size_t j = k + 1; j < toks.size(); ++j) {
        if (toks[j].kind == '}' && toks[j].depth < d) break;
        if (named(j)) has = true;
    }
    return has;
}

// The key at `at` (kTypeFile10 = "file") gains kTypeFile30 = the same "file"; false if its value is no string.
inline bool addTypeFile30(std::string& t, size_t at)
{
    size_t a = t.find('"', at), b = a == std::string::npos ? a : t.find('"', a + 1);
    if (b == std::string::npos || t.find_first_not_of(" \t=", at + sizeof(kTypeFile10) - 1) != a) return false;
    std::string v = t.substr(a, b + 1 - a);
    size_t c = t.find_first_not_of(" \t", b + 1);
    if (c != std::string::npos && t[c] == ',') t.insert(c + 1, " ChallengeTypeDataFile = " + v + ",");
    else t.insert(b + 1, ", ChallengeTypeDataFile = " + v);
    return true;
}

}  // namespace detail

inline int challengeTypes30(std::string& lua)
{
    return lualex::renameKeys(lua, {{"ActivityTypePackage", "ChallengeTypePackage"},
                                    {"ActivityTypes", "ChallengeTypes"},
                                    {"ActivityTypeKey", "ChallengeTypeKey"},
                                    {"ActivityDescriptionKey", "DescriptionKey"},
                                    {"ActivityLockedAdvertKey", "LockedAdvertKey"},
                                    {"ActivityOffAdvertKey", "OffAdvertKey"},
                                    {"ActivityFMV", "FMVName"}});
}

inline int beacons30(std::string& t30, const std::string& t10)
{
    auto toks10 = lualex::tokens(t10), toks30 = lualex::tokens(t30);
    auto p10 = lualex::globalTable(t10, toks10, "ActivityBeaconPackage");
    auto p30 = lualex::globalTable(t30, toks30, "BeaconTypePackage");
    if (p10.first == std::string::npos || p30.first == std::string::npos) return 0;
    // 3.0's reader (FUN_015d0d50) takes 1.0 beacons as they are; 1.0 proximity evals name their CullRadius
    std::set<std::string> have;
    for (auto& e : lualex::tableEntries(t30, toks30, p30.first)) have.insert(e.first);
    std::string add;
    int n = 0;
    for (auto& e : lualex::tableEntries(t10, toks10, p10.first)) {
        if (have.count(e.first)) continue;
        std::string rec = t10.substr(e.second.first, e.second.second - e.second.first);
        // the giver's perception handlers: 1.0 FUN_00c0ee50 Activity*, 3.0 FUN_00d3f0a0 Challenge*
        lualex::renameStrings(rec, {{"ActivityInitiate", "ChallengeInitiate"},
                                    {"ActivityOffAdvert", "ChallengeOffAdvert"},
                                    {"ActivityLockedAdvert", "ChallengeLockedAdvert"}});
        add += "\n\t" + rec + ",";
        ++n;
    }
    t30.insert(p30.first + 1, add);
    return n;
}

inline int zonelist30(std::string& t)
{
    auto toks = lualex::tokens(t);
    std::vector<size_t> at;
    for (size_t k = 0; k < toks.size(); ++k) {
        const bool key = toks[k].kind == 'i' && !t.compare(toks[k].at, toks[k].len, detail::kTypeFile10);
        if (!key || !lualex::assignedAt(t, toks[k].at + toks[k].len)) continue;
        if (!detail::hasTypeFile30(t, toks, k)) at.push_back(toks[k].at);
    }
    int n = 0;
    for (size_t q = at.size(); q-- > 0;)   // the last first: the earlier offsets hold
        n += detail::addTypeFile30(t, at[q]);
    return n;
}

inline std::string challengeTypePath(const std::string& normPath)
{
    static const std::string pre = "challengedata/";
    if (normPath.compare(0, pre.size(), pre) || normPath.find("activitytypedata", pre.size()) != pre.size()) return "";
    return "activitydata/" + normPath.substr(pre.size());
}

}  // namespace di1::challenges
