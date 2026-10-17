// 1.0 gameplay data (track gp: traversal, vehicles, traffic, tools, activities, store) in the 3.0 systems that
// replaced it, at load time. The 1.0 files are never rewritten; each transform is checked offline (octtest, scratch
// oracles) against the shipped 3.0 corpus.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include "octx.hpp"
#include "luachunk.hpp"
#include "base/bytes.hpp"
#include "formats/mer.hpp"

namespace gameplay {

namespace detail {
// A record's first value; an empty record is damaged input: hkOpen's catch logs it, serves the raw file.
template <class T>
T first(std::vector<T> v, const oct::File& f, const oct::Rec& r)
{
    if (v.empty()) throw std::runtime_error("gameplay: " + f.name(r) + " record without a value");
    return std::move(v[0]);
}
inline int32_t firstInt(const oct::File& f, const oct::Rec& r) { return first(f.ints(r), f, r); }
inline std::string firstName(const oct::File& f, const oct::Rec& r) { return first(f.nameValues(r), f, r); }
}  // namespace detail

// Ledges and rails (plan OCT-4). in_city.oct's CurvesPool holds 3,164 LH_* ledge-hang and 42 rail curves; every one
// of its 3,207 curves is bound by a Curve association {NodeName, SubNetworkRef, CurveIndices} to a scene node, and
// its points are local to that node (658/665 resolved root-basis curves sit near the origin, not at the node).
// 1.0's handler (FUN_006fc6e0) binds NodeName + '|Transform' (the node's Dilation, any basis); 3.0's (FUN_0071c2d0)
// resolves NodeName itself as a DataNode path (FUN_004ae9b0) and, when nothing resolves, never reads CurveIndices:
// the curve keeps its node-local points, so every Metroville ledge/rail lay near the world origin. Every shipped 3.0
// Curve association names a ROOT-basis Dilation (2,239 in 131 worlds, curvebind30.py): 'P|Transform' when that
// Dilation is root (679), 'P|Transform|In WorldBasis' -- the exporter's TransformDilationWithDilation chain output --
// when it is nested (743), or an existing 'X to WorldBasis' conversion node (800). This names the first two shapes
// (octx::SnEdit builds the chain exactly as locator30 does, minus its DilationToTransform step).
inline std::string worldDilation30(octx::SnEdit& e, const std::string& path)
{
    int loc = e.find(path);
    if (loc <= 0 || e.dnOf(loc) >= 0) return "";   // unresolved, or already names a node (3.0 form)
    int xf = e.child(loc, "Transform"), src = xf > 0 ? e.dnOf(xf) : -1;
    if (src < 0 || e.dns[src].type != "Dilation") return "";
    if (e.dns[src].basis == 0) return path + "|Transform";
    std::string to = path + "|Transform|In WorldBasis";
    if (e.child(xf, "In WorldBasis") >= 0) return to;   // a locator / collision chain on the same node made it
    bool found = false;
    auto steps = e.toWorld(e.dns[src].basis, found);
    if (!found) return "";
    int in = src;
    for (auto& s : steps) {
        std::string bn = s.second == 0 ? "WorldBasis" : "Basis" + std::to_string(s.second);
        int ho = e.header("In " + bn, xf), hp = e.header("Convert To " + bn, xf);
        int out = e.node(ho, "Dilation", s.second, false);
        e.processor(hp, "TransformDilationWithDilation", {in, s.first, out}, {"Input", "Converter", "Output"});
        in = out;
    }
    return to;
}

// Curves in 3.0 form: the Curve associations above, and CurvesPool Flags |= 0x1E0000. 3.0's CurveBSpline
// (FUN_0071bfe0) packs Flags bits 16..22 into a property byte (bits 4-6 = the capture distance of the nearest-curve
// query FUN_0071aef0, forced to >= 1; bits 0-3 copied into its result); 1.0 Flags are 3 / 0x23 (bits 0-5 only, read
// as NURBS flags by FUN_006fbfe0) and the 3.0 exporter re-exported the same curves with 0x1E0000 added (twins 6/6 in 4
// worlds; 23,931/23,932 shipped 3.0 curves carry 0x1E in bits 16..22). The ledge query itself (FUN_0071aa10) filters
// by the name-prefix category ('lh', 'rail': FUN_0071c860 registers the prefix before '_') and geometry only.
// Runs after locator30 / collision30 (reuses their world chains) and before the counts. Returns the curves bound.
inline int curves30(oct::File& f, int* flagged = nullptr)
{
    long cp = f.pool("CurvesPool");
    if (cp < 0) return 0;
    int nf = 0;
    for (size_t i = cp + 1, end = f.blockEnd(cp); i < end; ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level != 3 || f.name(r) != "Flags") continue;
        std::vector<int32_t> v = f.ints(r);
        bool any = false;
        for (int32_t& x : v)
            if (((uint32_t)x >> 16) == 0) { x |= 0x1E0000; any = true; ++nf; }
        if (any) f.setInts(r, v);
    }
    if (flagged) *flagged = nf;
    return octx::redirectAssociations(f, "Curve", worldDilation30);
}

// Surface types (plan OCT-5). A 1.0 Shape's SurfaceType indexes its file's own SurfaceTypesPool name list (1.0
// FUN_0072fcd0; the same 63 names in all 1,608 1.0 .oct); a 3.0 Shape's indexes the engine's global table (built by
// FUN_00754eb0 from startup2 surfacetypes.lua, 109 names; FUN_007482a0 looks names up with stricmp) and 3.0 ignores the
// 1.0 pool. 3.0 inserted snow@24 and noeffect@39, so 1.0 indices >= 24 named the wrong surface: in_city's 6 'metal'
// shapes (26) read as 'leaves'. Each value is mapped by name, as 1.0 resolved it (every 1.0 name is in the 3.0 table;
// twin: 'wood' 54 -> 56 as the 3.0 exporter wrote it, 8/8 shapes). names30 = the engine's table in index order.
// Returns the shapes changed (in_city 6, in_city_combat 5, the 194 mission .oct 0: surf.py).
inline int surfaces30(oct::File& f, const std::vector<std::string>& names30)
{
    long sp = f.pool("SurfaceTypesPool"), cp = f.pool("CollisionShapePool");
    if (sp < 0 || cp < 0 || names30.empty()) return 0;
    std::vector<std::string> names10;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; ++i)
        if (f.name(f.recs[i]) == "SurfaceNames") { names10 = f.nameValues(f.recs[i]); break; }
    std::vector<int> map(names10.size(), -1);
    for (size_t i = 0; i < names10.size(); ++i)
        for (size_t j = 0; j < names30.size() && map[i] < 0; ++j)
            if (_stricmp(names10[i].c_str(), names30[j].c_str()) == 0) map[i] = (int)j;
    int n = 0;
    for (size_t i = cp + 1, end = f.blockEnd(cp); i < end; ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level != 3 || f.name(r) != "SurfaceType") continue;
        int v = detail::firstInt(f, r);
        if (v >= 0 && v < (int)map.size() && map[v] >= 0 && map[v] != v) { f.setInt(r, map[v]); ++n; }
    }
    return n;
}

// ---- Activities -> the 3.0 Challenge system (plan SYS-6) ----
// 3.0 renamed the 1.0 Activity system to Challenge and kept its data shapes; each rename below is a pair of
// counterpart readers in the two exes:
//   type data   1.0 FUN_00f938a0 / 3.0 FUN_015c6400 read the same keys except ActivityTypeKey, ActivityDescriptionKey,
//               ActivityLockedAdvertKey, ActivityOffAdvertKey, ActivityFMV -> ChallengeTypeKey, DescriptionKey,
//               LockedAdvertKey, OffAdvertKey, FMVName; package ActivityTypePackage/ActivityTypes (1.0 FUN_00fa1290)
//               -> ChallengeTypePackage/ChallengeTypes (3.0 FUN_015d64c0/015d18f0), file 'ActivityData/%s' from the
//               playset key ActivityTypeDataFile (1.0 0xf09527) -> 'ChallengeData/%s' from ChallengeTypeDataFile
//               (3.0 0x143d603)
//   beacons     3.0's beacon reader (FUN_015d0d50) still reads every 1.0 key (Easy/Medium/Hard/Completed/AllInOne..
//               ChoreographyName, CullRadius, IconTimeToFade, BeaconStates, Proximity, IsOff, IsLocked); 1.0 loads
//               ActivityData/DefaultActivityBeacons.lua (ActivityBeaconPackage, 1.0 0xf095ad), 3.0 only
//               ChallengeData/ChallengeData.lua (BeaconTypePackage, 3.0 0x143d680). A named Distance in a proximity
//               eval is a beacon's CullRadius (3.0 FUN_01717f80 -> FUN_0157fd90), so the 1.0 missiongraph's
//               'DefaultActivityBeacon' needs the 1.0 beacon in 3.0's table
//   perceptions the giver's perception handlers: 1.0 FUN_00c0ee50 ActivityInitiate/OffAdvert/LockedAdvert, 3.0
//               FUN_00d3f0a0 ChallengeInitiate/OffAdvert/LockedAdvert (+ Exclusive/NonAbortable)
//   giver       DNA gadget MissionGiverActivity (1.0 registry 00cc2580) -> MissionGiverChallenge (3.0 010ce460); the
//               shipped 3.0 missiongiver_challenge.dnax is 1.0's missiongiver_activity.dnax with that gadget renamed.
//               Its .ap init (3.0 0x1507380) reads ChallengeType (returns at once without one), ActorTagName,
//               BeaconType; 1.0's (0xf46d38) read ActorTagName and ActivityIcon (default AnythingGoes).

// Lua source tokens outside strings and comments: identifiers (with brace depth) and braces.
struct LuaTok { size_t at, len; char kind; int depth; };   // kind 'i' identifier, '{', '}'
inline std::vector<LuaTok> luaTokens(const std::string& s)
{
    std::vector<LuaTok> out;
    int depth = 0;
    auto longEnd = [&](size_t from) { size_t e = s.find("]]", from); return e == std::string::npos ? s.size() : e + 2; };
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            if (i + 3 < s.size() && s[i + 2] == '[' && s[i + 3] == '[') i = longEnd(i + 4);
            else { size_t e = s.find('\n', i); i = e == std::string::npos ? s.size() : e; }
        } else if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += s[j] == '\\' ? 2 : 1;
            i = j + 1;
        } else if (c == '[' && i + 1 < s.size() && s[i + 1] == '[') i = longEnd(i + 2);
        else if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            out.push_back({i, j - i, 'i', depth});
            i = j;
        } else if (isdigit((unsigned char)c)) {
            while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '.')) ++i;
        } else {
            if (c == '{') out.push_back({i, 1, '{', depth++});
            else if (c == '}') out.push_back({i, 1, '}', --depth});
            ++i;
        }
    }
    return out;
}

inline bool assignedAt(const std::string& s, size_t after)   // 'name =' (not '==') follows
{
    size_t j = s.find_first_not_of(" \t\r\n", after);
    return j != std::string::npos && s[j] == '=' && (j + 1 >= s.size() || s[j + 1] != '=');
}

// Renames table keys / globals ('Old =' outside strings and comments). Returns the number renamed.
inline int luaRenameKeys(std::string& s, const std::vector<std::pair<std::string, std::string>>& map)
{
    auto toks = luaTokens(s);
    int n = 0;
    for (size_t k = toks.size(); k-- > 0;) {
        const LuaTok& t = toks[k];
        if (t.kind != 'i' || !assignedAt(s, t.at + t.len)) continue;
        for (auto& m : map)
            if (t.len == m.first.size() && !s.compare(t.at, t.len, m.first)) { s.replace(t.at, t.len, m.second); ++n; break; }
    }
    return n;
}

// Replaces whole quoted string values "Old" -> "New". Returns the count.
inline int luaRenameStrings(std::string& s, const std::vector<std::pair<std::string, std::string>>& map)
{
    int n = 0;
    for (auto& m : map) {
        std::string a = "\"" + m.first + "\"", b = "\"" + m.second + "\"";
        for (size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) { s.replace(p, a.size(), b); ++n; }
    }
    return n;
}

// The table assigned to global `name`: [open brace, one past its close); {npos, npos} if absent.
inline std::pair<size_t, size_t> luaGlobalTable(const std::string& s, const std::vector<LuaTok>& toks, const std::string& name)
{
    for (size_t k = 0; k + 1 < toks.size(); ++k) {
        if (toks[k].kind != 'i' || toks[k].depth != 0 || s.compare(toks[k].at, toks[k].len, name) || toks[k + 1].kind != '{') continue;
        for (size_t e = k + 2; e < toks.size(); ++e)
            if (toks[e].kind == '}' && toks[e].depth == 0) return {toks[k + 1].at, toks[e].at + 1};
    }
    return {std::string::npos, std::string::npos};
}

// Direct entries 'Key = { ... }' of the table opened at `open`: key -> [key start, one past the value's '}').
inline std::vector<std::pair<std::string, std::pair<size_t, size_t>>> luaTableEntries(const std::string& s, const std::vector<LuaTok>& toks, size_t open)
{
    std::vector<std::pair<std::string, std::pair<size_t, size_t>>> out;
    size_t k = 0;
    while (k < toks.size() && toks[k].at != open) ++k;
    if (k == toks.size()) return out;
    int d = toks[k].depth + 1;
    for (++k; k + 1 < toks.size(); ++k) {
        if (toks[k].kind == '}' && toks[k].depth < d) break;
        if (toks[k].kind != 'i' || toks[k].depth != d || toks[k + 1].kind != '{' || !assignedAt(s, toks[k].at + toks[k].len)) continue;
        for (size_t e = k + 2; e < toks.size(); ++e)
            if (toks[e].kind == '}' && toks[e].depth == d) { out.push_back({s.substr(toks[k].at, toks[k].len), {toks[k].at, toks[e].at + 1}}); k = e; break; }
    }
    return out;
}

// 1.0 ActivityTypePackage (ActivityData/ActivityTypeData_<playset>.lua) -> 3.0 ChallengeTypePackage. Returns the keys
// renamed (activitytypedata_in.lua: 1 + 1 + 16 + 16 + 6 + 13 + 5 = 58, challenge.py).
inline int challengeTypes30(std::string& lua)
{
    return luaRenameKeys(lua, {{"ActivityTypePackage", "ChallengeTypePackage"}, {"ActivityTypes", "ChallengeTypes"},
                               {"ActivityTypeKey", "ChallengeTypeKey"}, {"ActivityDescriptionKey", "DescriptionKey"},
                               {"ActivityLockedAdvertKey", "LockedAdvertKey"}, {"ActivityOffAdvertKey", "OffAdvertKey"},
                               {"ActivityFMV", "FMVName"}});
}

// 3.0 ChallengeData/ChallengeData.lua + the 1.0 beacon records (ActivityData/DefaultActivityBeacons.lua) that 3.0's
// BeaconTypePackage lacks, their trigger perceptions renamed. Returns the records added (DefaultActivityBeacon,
// SmallActivityBeacon).
inline int beacons30(std::string& t30, const std::string& t10)
{
    auto toks10 = luaTokens(t10), toks30 = luaTokens(t30);
    auto p10 = luaGlobalTable(t10, toks10, "ActivityBeaconPackage"), p30 = luaGlobalTable(t30, toks30, "BeaconTypePackage");
    if (p10.first == std::string::npos || p30.first == std::string::npos) return 0;
    std::set<std::string> have;
    for (auto& e : luaTableEntries(t30, toks30, p30.first)) have.insert(e.first);
    std::string add;
    int n = 0;
    for (auto& e : luaTableEntries(t10, toks10, p10.first)) {
        if (have.count(e.first)) continue;
        std::string rec = t10.substr(e.second.first, e.second.second - e.second.first);
        luaRenameStrings(rec, {{"ActivityInitiate", "ChallengeInitiate"}, {"ActivityOffAdvert", "ChallengeOffAdvert"},
                               {"ActivityLockedAdvert", "ChallengeLockedAdvert"}});
        add += "\n\t" + rec + ",";
        ++n;
    }
    t30.insert(p30.first + 1, add);
    return n;
}

// A zone list's playset records that name the 1.0 key ActivityTypeDataFile get ChallengeTypeDataFile with the same
// file (3.0 reads only that key, 0x143d603; served from ActivityData by challengeTypePath). Returns records changed.
inline int zonelist30(std::string& t)
{
    auto toks = luaTokens(t);
    std::vector<size_t> at;
    for (size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].kind != 'i' || t.compare(toks[k].at, toks[k].len, "ActivityTypeDataFile") || !assignedAt(t, toks[k].at + toks[k].len)) continue;
        int d = toks[k].depth;
        bool has = false;
        for (size_t j = k; j-- > 0 && !(toks[j].kind == '{' && toks[j].depth < d);)
            if (toks[j].kind == 'i' && toks[j].depth == d && !t.compare(toks[j].at, toks[j].len, "ChallengeTypeDataFile")) has = true;
        for (size_t j = k + 1; j < toks.size() && !(toks[j].kind == '}' && toks[j].depth < d); ++j)
            if (toks[j].kind == 'i' && toks[j].depth == d && !t.compare(toks[j].at, toks[j].len, "ChallengeTypeDataFile")) has = true;
        if (!has) at.push_back(toks[k].at);
    }
    int n = 0;
    for (size_t q = at.size(); q-- > 0;) {
        size_t a = t.find('"', at[q]), b = a == std::string::npos ? a : t.find('"', a + 1);
        if (b == std::string::npos || t.find_first_not_of(" \t=", at[q] + strlen("ActivityTypeDataFile")) != a) continue;
        std::string v = t.substr(a, b + 1 - a);
        size_t c = t.find_first_not_of(" \t", b + 1);
        if (c != std::string::npos && t[c] == ',') t.insert(c + 1, " ChallengeTypeDataFile = " + v + ",");
        else t.insert(b + 1, ", ChallengeTypeDataFile = " + v);
        ++n;
    }
    return n;
}

// currency10: a 1.0 playset's currency, from the 1.0 zone list -- PlaysetProperties.<playset>.inventoryFilter ->
// its MissionParams.DefaultRewardType (LVL_INCREDIBLES -> INV_NSA_BUCKS, LVL_MONSTERS -> INV_MONSTERS_CURRENCY,
// LVL_TOYSTORYINSPACE -> INV_SPACE_CRYSTAL, ...). 3.0 keeps these as GLOBAL-scope stats; its "currency of the zone's
// inventory filter" FUN_00e21850 answers INV_SPARKS for every filter, so coins paid sparks and the Toy Store priced
// in sparks (in-game 2026-10-15). 3.0's own legacy branches for them stay (FUN_0115e880: INV_SPACE_CRYSTAL /
// INV_CARS_CASH / INV_NSA_BUCKS reward groups).
inline std::map<std::string, std::string> currencies10(const luachunk::Value& zoneList10)
{
    std::map<std::string, std::string> out;
    const luachunk::Value* rec = zoneList10.get("Records");
    const luachunk::Value* pp = rec ? rec->get("PlaysetProperties") : nullptr;
    if (!pp || pp->t != luachunk::Value::TAB) return out;
    auto currency = [&](const luachunk::Value& p) { const luachunk::Value* mp = p.get("MissionParams"); return mp ? mp->str("DefaultRewardType") : ""; };
    for (auto& [k, p] : *pp->tab) {
        std::string filter = p.str("inventoryFilter"), cur = currency(p);
        if (!filter.empty() && !cur.empty()) out[filter] = cur;
    }
    // a zone record may carry the filter itself (Zones.LR_Colby: inventoryFilter LVL_LONERANGER, PlaysetProperties
    // 'LoneRanger'); 3.0 asks the current zone (FUN_0139e0b0 "inventoryFilter")
    if (const luachunk::Value* zs = rec->get("Zones"); zs && zs->t == luachunk::Value::TAB)
        for (auto& [k, z] : *zs->tab) {
            std::string filter = z.str("inventoryFilter");
            const luachunk::Value* p = pp->get(z.str("PlaysetProperties"));
            if (!filter.empty() && p && !out.count(filter) && !currency(*p).empty()) out[filter] = currency(*p);
        }
    return out;
}

// PS-1 (cfg zones10): every 1.0 playset in 3.0's zone list as it loads, from the 1.0 zone list (startup2.zip
// gamedb/core/zonelist.lua, a compiled table constructor: luachunk::eval). A playset is a 1.0 PlaysetProperties record
// of Type "Playset" whose StartWorld is one of its own zones (zone field PlaysetProperties = its name) and is in the
// 1.0 install (worldExists): Incredibles, MonstersU, Pirates, Cars, LoneRanger, ToyStoryInSpace (RumpusRoom is Type
// "RumpusRoom", Miscellaneous "Other"). One the 3.0 text already holds by name is left as it is (a zone list written
// by zonereg.py); the others get, in 1.0's own GlobalData.OrderedPlaysets order (Incredibles first, then MonstersU,
// Pirates, Cars, LoneRanger, ToyStoryInSpace), the lowest PlaysetUniqueID no record of the text uses (3.0: 1-10, and
// 12/13 in InheritList; the registration loop FUN_0139e820 skips a playset whose name or id is taken, zone ids are
// id*0x100 + 0x401 + n) -- 11, 14, 15 ... -- and are written as zonereg.py wrote Incredibles: their 1.0 zone records
// verbatim at the top of Zones (by name, the ones 3.0 lacks), the 1.0 PlaysetProperties record plus the keys 3.0
// reads there (PlaysetUniqueID; WorldIsPlayset, 1.0 spelled it worldIsPlayset; SaveGameKey = the name, SaveGamePrefix
// = playsetDict, as 3.0's own records pair them; LoadingScreens = playsetDict/LoadingScreen, 1.0's key; _ShowInMenu_
// and the two build flags) at the top of PlaysetProperties, and an OrderedPlaysets entry. Returns the 1.0 playsets
// the text holds afterwards, in that order.
struct Playset10 { std::string name, start; int id = 0; bool added = false; std::vector<std::string> zones; };
inline std::vector<Playset10> playsets10(std::string& t, const luachunk::Value& zoneList10, bool add,
                                         const std::function<bool(const std::string&)>& worldExists)
{
    using luachunk::Value;
    const size_t npos = std::string::npos;
    std::vector<Playset10> out;
    const Value* rec = zoneList10.get("Records");
    const Value *pp10 = rec ? rec->get("PlaysetProperties") : nullptr, *z10 = rec ? rec->get("Zones") : nullptr;
    const Value* gd10 = rec ? rec->get("GlobalData") : nullptr;
    const Value* op10 = gd10 ? gd10->get("OrderedPlaysets") : nullptr;
    if (!pp10 || !z10 || pp10->t != Value::TAB || z10->t != Value::TAB) return out;
    std::vector<std::string> order, rest;
    if (op10 && op10->t == Value::TAB)
        for (auto& e : *op10->tab) if (e.second.t == Value::TAB) order.push_back(e.second.str("Name"));
    for (auto& e : *pp10->tab)
        if (e.first.t == Value::STR && std::find(order.begin(), order.end(), e.first.s) == order.end()) rest.push_back(e.first.s);
    std::sort(rest.begin(), rest.end());
    order.insert(order.end(), rest.begin(), rest.end());

    // the 3.0 text: its Records tables, the playsets / zones it has, the ids its records use
    auto toks = luaTokens(t);
    auto zl = luaGlobalTable(t, toks, "ZoneList");
    if (zl.first == npos) return out;
    std::map<std::string, size_t> recs;   // table name -> its '{'
    for (auto& e : luaTableEntries(t, toks, zl.first))
        if (e.first == "Records")
            for (auto& r : luaTableEntries(t, toks, t.find('{', e.second.first))) recs[r.first] = t.find('{', r.second.first);
    auto names = [&](const char* table) {
        std::set<std::string> s;
        if (recs.count(table)) for (auto& e : luaTableEntries(t, toks, recs[table])) s.insert(e.first);
        return s;
    };
    std::set<std::string> have = names("PlaysetProperties"), zones = names("Zones");
    std::set<int> used;
    for (auto& k : toks)
        if (k.kind == 'i' && !t.compare(k.at, k.len, "PlaysetUniqueID") && assignedAt(t, k.at + k.len)) used.insert(atoi(t.c_str() + t.find('=', k.at) + 1));
    std::string ordered;
    size_t opOpen = npos;
    if (recs.count("GlobalData"))
        for (auto& e : luaTableEntries(t, toks, recs["GlobalData"]))
            if (e.first == "OrderedPlaysets") { opOpen = t.find('{', e.second.first); ordered = t.substr(opOpen, e.second.second - opOpen); }
    add = add && recs.count("Zones") && recs.count("PlaysetProperties") && opOpen != npos;

    auto record = [](const std::string& name, const Value& r, const std::map<std::string, std::string>& over) {
        std::map<std::string, std::string> kv = over;   // key -> Lua text, sorted by key (zonereg.py record_text)
        for (auto& e : *r.tab) {
            std::string k = e.first.t == Value::STR ? e.first.s : luachunk::text(e.first);
            if (!over.count(k)) kv[k] = luachunk::text(e.second);
        }
        std::string s = "\t\t\t" + name + " = \n\t\t\t{\n";
        for (auto& [k, v] : kv) s += "\t\t\t\t" + (luachunk::ident(k) ? k : "[" + luachunk::quote(k) + "]") + " = " + v + ",\n";
        return s + "\t\t\t},\n";
    };
    std::string zoneText, ppText, opText;
    for (auto& name : order) {
        const Value* pp = pp10->get(name);
        if (!pp || pp->t != Value::TAB || pp->str("Type") != "Playset") continue;
        Playset10 ps{name, pp->str("StartWorld")};
        for (auto& e : *z10->tab)
            if (e.first.t == Value::STR && e.second.t == Value::TAB && e.second.str("PlaysetProperties") == name) ps.zones.push_back(e.first.s);
        std::sort(ps.zones.begin(), ps.zones.end());
        if (std::find(ps.zones.begin(), ps.zones.end(), ps.start) == ps.zones.end() || !worldExists(ps.start)) continue;
        if (!have.count(name)) {
            if (!add) continue;
            while (used.count(++ps.id)) {}
            used.insert(ps.id);
            ps.added = true;
            std::string dict = pp->str("playsetDict");
            std::map<std::string, std::string> over = {{"PlaysetUniqueID", std::to_string(ps.id)}, {"WorldIsPlayset", "true"},
                                                       {"SaveGameKey", luachunk::quote(name)}, {"_ShowInMenu_", "true"},
                                                       {"availableInConsumerBuild", "true"}, {"availableInShippingBuild", "true"}};
            if (!dict.empty()) {
                over["SaveGamePrefix"] = luachunk::quote(dict);
                over["LoadingScreens"] = luachunk::quote(dict + "/" + pp->str("LoadingScreen"));
            }
            ppText += record(name, *pp, over);
            for (auto& z : ps.zones) if (!zones.count(z)) zoneText += record(z, *z10->get(z), {});
            if (ordered.find(luachunk::quote(name)) == npos)
                opText += "\t\t\t\t{ Name = " + luachunk::quote(name) + ", availableInConsumerBuild = true, availableInShippingBuild = true,\t\t\t\t},\n";
        }
        out.push_back(ps);
    }
    // inserted on the line after each table's '{' (OrderedPlaysets: after the last 1.0 playset it lists already, so
    // they keep 1.0's order and a listed one its place), the last table first so the earlier offsets hold
    auto lineAfter = [&](size_t at) { size_t nl = t.find('\n', at); return nl == npos ? at + 1 : nl + 1; };
    size_t opAt = opOpen;
    for (auto& ps : out)
        if (!ps.added && ordered.find(luachunk::quote(ps.name)) != npos) opAt = (std::max)(opAt, opOpen + ordered.find(luachunk::quote(ps.name)));
    std::vector<std::pair<size_t, std::string>> ed;
    if (!zoneText.empty()) ed.push_back({lineAfter(recs["Zones"]), zoneText});
    if (!ppText.empty()) ed.push_back({lineAfter(recs["PlaysetProperties"]), ppText});
    if (!opText.empty()) ed.push_back({lineAfter(opAt), opText});
    std::sort(ed.begin(), ed.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& e : ed) t.insert(e.first, e.second);
    return out;
}

// 'ChallengeData/ActivityTypeData_IN.lua' (the zone list's 1.0 file name under 3.0's folder) -> the 1.0 path, or "".
inline std::string challengeTypePath(const std::string& normPath)
{
    static const std::string pre = "challengedata/";
    if (normPath.compare(0, pre.size(), pre) || normPath.find("activitytypedata", pre.size()) != pre.size()) return "";
    return "activitydata/" + normPath.substr(pre.size());
}

// .ap activity giver sections (AddComponent_MG_Activity, 19 in Incredibles, all with ActivityIcon) get the 3.0 keys
// the MissionGiverChallenge init reads: ChallengeType = the activity type, BeaconType = the 1.0 beacon. 1.0 takes the
// beacon from the type's ActivityBeaconName, default DefaultActivityBeacon (FUN_00f938a0); no Incredibles type sets
// it (0 of 16). Only givers whose ActorTagName is in `only` (when given): a giver without ChallengeType stays inert in
// 3.0 (its init returns at once), so an activity whose mission cannot run yet is not offered. Returns sections changed.
inline int apGivers30(std::string& ap, const std::set<std::string>* only = nullptr)
{
    int n = 0;
    for (size_t sec = ap.find('['); sec != std::string::npos;) {
        size_t next = ap.find("\n[", sec + 1);
        size_t end = next == std::string::npos ? ap.size() : next + 1;
        size_t icon = ap.find("\nActivityIcon=", sec), tag = ap.find("\nActorTagName=", sec);
        bool want = !only;
        if (only && tag < end) {
            size_t te = ap.find_first_of("\r\n", tag + 14);
            want = only->count(ap.substr(tag + 14, (te == std::string::npos ? ap.size() : te) - tag - 14)) > 0;
        }
        if (want && icon != std::string::npos && icon < end && ap.find("\nChallengeType=", sec) >= end) {
            size_t vs = icon + 14, ve = ap.find_first_of("\r\n", vs);
            if (ve == std::string::npos) ve = ap.size();
            std::string eol = ve < ap.size() && ap[ve] == '\r' ? "\r\n" : "\n";
            std::string add = "ChallengeType=" + ap.substr(vs, ve - vs) + eol + "BeaconType=DefaultActivityBeacon" + eol;
            size_t ins = ap.find('\n', vs);
            ins = ins == std::string::npos ? ap.size() : ins + 1;
            if (ins == ap.size() && (ap.empty() || ap.back() != '\n')) { ap += eol; ins = ap.size(); }
            ap.insert(ins, add);
            end += add.size();
            ++n;
        }
        sec = end < ap.size() ? end : std::string::npos;
    }
    return n;
}

// ---- activity missions: the task classes with a 3.0 counterpart ----
// Pins bind by name in both engines (verify-meridian claim 2: 3.0 0x6c7dd0 / 1.0 0x6bd670 read each pin's index from
// the node field of that name), so a class swap renames pin records and keeps link pin numbers. Pin tables of the 3.0
// classes are the shipped ones (pins30.py, identical in all 61 / 9 shipped graphs); 3.0 keeps the task parameters in
// the node (011b4db0 / 011b4f20 declare them; the eval reads them from the bound node, e.g. 00cc0e20 via eval+0xc0).
//   StateMachine 1.0 pins StartPosition CompleteIntro EndPosition SetupStart IntroStart InProgStart ActivityExit
//                Participants; 3.0 StartPosition EndPosition Preload PreIntro PreInProgress PostInProgress
//                PostInProgressTeleport ParticipantCount. Census (pinuse.py, 35 1.0 / all 3.0 graphs): IntroStart
//                54 links ~ PreIntro 162, ActivityExit 17 (cleanup) ~ PostInProgress 176. CompleteIntro has no 3.0
//                input (3.0 runs its own intro). Participants (out list, 37 links) -> an AvatarList node with
//                ParticipantsOnly = 1 (a 1.0 and 3.0 class; 3.0's challenge graphs read participants that way)
//   CheckpointRace CheckpointPositions / RacePath identical; ShowLapHUD / ShowTimerHUD inputs have none (3.0 shows its
//                own HUD); Easy/Medium/HardTime -> Bronze/Silver/GoldTime (lenient -> strict in both: 1.0 60/50/45,
//                3.0 e.g. 135/115/55); GuideSpeed / RaceType left to the node defaults (no 1.0 value)
// CombatTimeTrial (fires SpawnNextEnemy per kill: 7 graphs need it) and Collector (spawns its own collectibles; 3.0's
// spawns through a Lua bridge) have no counterpart that keeps the 1.0 graph working; a graph or mission that uses one
// is left as it is.
struct ActClass {
    const char *cls10, *cls30, *eval10, *eval30;
    std::vector<std::pair<std::string, std::string>> pins;     // "" = no counterpart (links dropped), "*" = Participants
    std::vector<std::pair<std::string, std::string>> params;
};
inline const std::vector<ActClass>& actClasses()
{
    static const std::vector<ActClass> m = {
        {"MissionTask_ActivityStateMachine", "MissionTask_ChallengeStateMachine", "Eval_ActivityStateMachine", "Eval_ChallengeStateMachine",
         {{"StartPosition", "StartPosition"}, {"EndPosition", "EndPosition"}, {"SetupStart", "Preload"}, {"IntroStart", "PreIntro"},
          {"InProgStart", "PreInProgress"}, {"ActivityExit", "PostInProgress"}, {"CompleteIntro", ""}, {"Participants", "*"}},
         {{"LineUpSpacing", "Spacing"}, {"EndLineUpSpacing", "EndSpacing"}}},
        {"MissionTask_ActivityCheckpointRace", "MissionTask_Challenge_CheckpointRace", "Eval_Activity_CheckpointRace", "Eval_Challenge_CheckpointRace",
         {{"CheckpointPositions", "CheckpointPositions"}, {"RacePath", "RacePath"}, {"ShowLapHUD", ""}, {"ShowTimerHUD", ""}},
         {{"EasyTime", "BronzeTime"}, {"MediumTime", "SilverTime"}, {"HardTime", "GoldTime"}}},
    };
    return m;
}
// The class whose 1.0 task type is t, or null.
inline const ActClass* taskClass(const std::string& t)
{
    for (auto& c : actClasses()) if (t == c.cls10) return &c;
    return nullptr;
}
// The class whose 1.0 eval type is t, or null.
inline const ActClass* evalClass(const std::string& t)
{
    for (auto& c : actClasses()) if (t == c.eval10) return &c;
    return nullptr;
}

namespace detail {

using Pin = std::pair<std::string, std::string>;   // 1.0 pin -> 3.0 pin ("" none, "*" the participant list)

// One graph node: its record range and the name of its first Type field.
struct ActNode {
    size_t at, end;
    std::string type;
};

// activityGraph30's work: the graph's pools and nodes, its pin tables, the link edits and their counts.
struct ActGraph {
    oct::File& f;
    long pool = -1, lpool = -1;                                 // MeridianNodePool, MeridianLinkPool
    uint16_t nl = 0, fl = 0;                                    // node and field record levels
    std::vector<ActNode> nodes;
    std::map<std::string, std::map<std::string, int>> pinIdx;   // task class -> pin name -> index
    size_t newIdx = 0;                                          // the AvatarList's index: NameSpaceEnd's
    std::map<size_t, int> dIn, dOut;                            // links dropped, per node
    int nRe = 0, nDrop = 0;                                     // links rerouted to the AvatarList; dropped
    std::string smUuid;                                         // the state machine's Uuid payload
};

inline void collectNodes(ActGraph& g)
{
    oct::File& f = g.f;
    for (size_t i = g.pool + 1, e = f.blockEnd(g.pool); i < e; i = f.blockEnd(i)) {
        if (f.recs[i].level != g.nl) continue;
        ActNode n{i, f.blockEnd(i), ""};
        for (size_t j = i + 1; j < n.end; ++j) {
            if (f.recs[j].level != g.fl || f.name(f.recs[j]) != "Type") continue;
            n.type = firstName(f, f.recs[j]);
            break;
        }
        g.nodes.push_back(n);
    }
}

// The activity task nodes; 0 when one has no 3.0 class or the graph is not exactly one namespace.
inline int mappedNodes(const ActGraph& g)
{
    int begins = 0, mapped = 0;
    for (auto& n : g.nodes) {
        begins += n.type == "NameSpaceBegin";
        if (n.type.rfind("MissionTask_Activity", 0) != 0) continue;
        if (!taskClass(n.type)) return 0;
        ++mapped;
    }
    if (!mapped || begins != 1 || g.nodes.back().type != "NameSpaceEnd") return 0;
    return mapped;
}

inline bool isIntScalar(const oct::Rec& r)
{
    oct::Spec s = oct::spec(r.type);
    return s.shape == oct::SCALAR && s.kind == oct::INT && !s.keyed;
}

// The pin indices node n of class c lists; a class's first instance wins (1.0 writes one table per layout).
inline void notePins(ActGraph& g, const ActNode& n, const ActClass& c)
{
    for (size_t j = n.at + 1; j < n.end; ++j) {
        const oct::Rec& r = g.f.recs[j];
        if (r.level != g.fl || !isIntScalar(r)) continue;
        for (auto& p : c.pins)
            if (g.f.name(r) == p.first) g.pinIdx[n.type].emplace(p.first, g.f.ints(r)[0]);
    }
}

inline const Pin* pinOf(ActGraph& g, const std::string& cls, int idx)
{
    const ActClass* c = taskClass(cls);
    for (auto& p : c->pins) {
        auto it = g.pinIdx[cls].find(p.first);
        if (it != g.pinIdx[cls].end() && it->second == idx) return &p;
    }
    return nullptr;
}

// The pin at index idx of a link end's node; null unless that node is an activity task.
inline const Pin* endPin(ActGraph& g, size_t node, int idx)
{
    if (node >= g.nodes.size() || !taskClass(g.nodes[node].type)) return nullptr;
    return pinOf(g, g.nodes[node].type, idx);
}

// Whether link pool record i is a LinksBuffer's Data: the nearest Type above it at its level says so.
inline bool inLinksBuffer(const ActGraph& g, size_t i)
{
    const oct::File& f = g.f;
    const uint16_t level = f.recs[i].level;
    for (size_t j = i; j-- > (size_t)g.lpool + 1 && f.recs[j].level >= level;) {
        if (f.recs[j].level != level || f.name(f.recs[j]) != "Type") continue;
        return firstName(f, f.recs[j]) == "LinksBuffer";
    }
    return false;
}

// Links in the index form: u32 count, u8 0, the u16 codes, then u16 from / to pairs (FORMATS.md 5.3).
struct LinkRows {
    std::vector<uint16_t> code, from, to;
};

// Record r's Data as the index form: count and rows, the count width by the byte size.
inline void writeLinks(oct::Rec& r, const LinkRows& rows)
{
    const uint32_t count = (uint32_t)rows.code.size();
    std::string o((const char*)&count, 4);
    o.push_back('\0');
    for (uint16_t v : rows.code) o.append((const char*)&v, 2);
    for (size_t k = 0; k < count; ++k) {
        o.append((const char*)&rows.from[k], 2);
        o.append((const char*)&rows.to[k], 2);
    }
    r.pay.clear();
    const int cw = oct::countWidth(o.size());
    r.type = oct::makeType(oct::SCALAR, 0, oct::BYTE, cw);
    oct::File::put(r.pay, (uint32_t)o.size(), cw);
    r.pay += o;
}

// A LinksBuffer without the links on pins 3.0 lacks; Participants links leave the new AvatarList's List.
inline void rewriteLinkBuffer(ActGraph& g, oct::Rec& r)
{
    const std::optional<mer::IndexLinks> b = mer::indexLinks(r);
    if (!b || b->uu || !mer::sizeMatches(*b)) return;   // the index form: all 1.0 graphs (linkcodec.py)
    const std::map<std::string, size_t> noUuids;   // the index form names nodes by index
    LinkRows kept;
    for (uint32_t k = 0; k < b->n; ++k) {
        const mer::Link L = *mer::linkAt(*b, k, noUuids);
        const uint16_t from = (uint16_t)L.from, to = (uint16_t)L.to;
        const Pin* pa = endPin(g, from, L.code >> mer::kFromPinShift);
        const Pin* pz = endPin(g, to, (L.code >> mer::kToPinShift) & mer::kPinMask);
        const bool fromNone = pa && pa->second.empty();
        const bool toNone = pz && pz->second.empty();
        const bool toList = pz && pz->second == "*";
        if (fromNone || toNone || toList) {
            ++g.dOut[from];
            ++g.dIn[to];
            ++g.nDrop;
            continue;
        }
        const bool fromList = pa && pa->second == "*";
        if (fromList) ++g.dOut[from];
        if (fromList) ++g.nRe;
        kept.code.push_back(fromList ? (uint16_t)(L.code & mer::kBelowFromPin) : L.code);   // from pin 0: List
        kept.from.push_back(fromList ? (uint16_t)g.newIdx : from);
        kept.to.push_back(to);
    }
    // nRe counts every buffer so far: after the first reroute each later buffer is rewritten (GP-24)
    if (kept.code.size() == b->n && !g.nRe) return;
    writeLinks(r, kept);
}

inline void rewriteLinks(ActGraph& g)
{
    oct::File& f = g.f;
    for (size_t i = g.lpool + 1; i < f.blockEnd(g.lpool); ++i) {
        if (f.name(f.recs[i]) != "Data" || !inLinksBuffer(g, i)) continue;
        rewriteLinkBuffer(g, f.recs[i]);
    }
}

// A scalar name record's (or an MNode key's) value set to v.
inline void setNameValue(oct::File& f, oct::Rec& r, const std::string& v)
{
    r.pay.clear();
    oct::File::put(r.pay, f.addName(v), 2);
}

// The Participants list: an AvatarList (ParticipantsOnly) at NameSpaceEnd's index, then NameSpaceEnd one up.
inline void emitAvatarList(ActGraph& g, size_t k, std::vector<oct::Rec>& out)
{
    oct::File& f = g.f;
    const uint16_t fl = g.fl;
    const ActNode& n = g.nodes[k];
    oct::Rec hdr = f.recs[n.at];
    setNameValue(f, hdr, std::to_string(k));
    out.push_back(hdr);
    out.push_back(f.nameScalar(fl, f.addName("Type"), "AvatarList"));
    if (!g.smUuid.empty()) {
        oct::Rec u{fl, oct::makeType(oct::SCALAR, 0, oct::BYTE), f.addName("Uuid"), g.smUuid};
        u.pay.back() = (char)(u.pay.back() ^ 0x5a);   // unique beside the state machine it stands for
        out.push_back(u);
    }
    out.push_back(f.intScalar(fl, "List", 0));
    out.push_back(f.intScalar(fl, "NetSyncType", 2));
    out.push_back(f.intScalar(fl, "LocalsOnly", 0));
    out.push_back(f.intScalar(fl, "SpectatorsOnly", 0));
    out.push_back(f.intScalar(fl, "ParticipantsOnly", 1));
    out.push_back(f.intScalar(fl, "IsRootObject", 1));
    out.push_back(f.nameScalar(fl, f.addName("AuthoredName"), "Activity: Participants"));
    out.push_back(f.intScalar(fl, "NumOutLinks", g.nRe));
    oct::Rec end = f.recs[n.at];
    setNameValue(f, end, std::to_string(k + 1));
    out.push_back(end);
    for (size_t j = n.at + 1; j < n.end; ++j) out.push_back(f.recs[j]);
}

// Field r of a class-c node in 3.0 form: Type / eval type, pins, parameters renamed. false: drop it.
inline bool renameField(ActGraph& g, const ActClass& c, oct::Rec& r, const std::string& nm, bool& firstType)
{
    oct::File& f = g.f;
    if (nm == "Type") {
        setNameValue(f, r, firstType ? c.cls30 : c.eval30);
        firstType = false;
    } else if (nm == "Uuid" && g.smUuid.empty() && std::string(c.cls10) == "MissionTask_ActivityStateMachine") {
        g.smUuid = r.pay;
    }
    bool drop = false;
    for (auto& p : c.pins) {
        if (nm != p.first || !isIntScalar(r)) continue;
        if (p.second.empty() || p.second == "*") drop = true;
        else r.name = f.addName(p.second);
    }
    for (auto& p : c.params)
        if (nm == p.first) r.name = f.addName(p.second);
    return !drop;
}

// Node k's records: NumIn/OutLinks less the dropped links; an activity task's fields in 3.0 form.
inline void rewriteNode(ActGraph& g, size_t k, std::vector<oct::Rec>& out)
{
    oct::File& f = g.f;
    const ActNode& n = g.nodes[k];
    const ActClass* c = taskClass(n.type);
    out.push_back(f.recs[n.at]);
    bool firstType = true;
    for (size_t j = n.at + 1; j < n.end; ++j) {
        oct::Rec r = f.recs[j];
        const bool field = r.level == g.fl;
        const std::string nm = field ? f.name(r) : std::string();
        if (field && nm == "NumInLinks" && g.dIn.count(k)) f.setInt(r, firstInt(f, r) - g.dIn[k]);
        if (field && nm == "NumOutLinks" && g.dOut.count(k)) f.setInt(r, firstInt(f, r) - g.dOut[k]);
        if (c && field && !renameField(g, *c, r, nm, firstType)) continue;
        out.push_back(r);
    }
    if (c && std::string(c->cls30) == "MissionTask_ChallengeStateMachine")
        out.push_back(f.nameScalar(g.fl, f.addName("ChallengeZone"), "NONE"));   // 53/61 shipped graphs
}

}  // namespace detail

// A one-namespace 1.0 activity graph (.mer, before mer::transcode) in 3.0 Challenge classes. -> tasks, 0: unchanged
inline int activityGraph30(oct::File& f, int* rerouted = nullptr, int* dropped = nullptr)
{
    detail::ActGraph g{f};
    g.pool = f.pool("MeridianNodePool");
    g.lpool = f.pool("MeridianLinkPool");
    if (g.pool < 0 || g.lpool < 0) return 0;
    g.nl = (uint16_t)(f.recs[g.pool].level + 1);
    g.fl = (uint16_t)(g.nl + 1);
    detail::collectNodes(g);
    const int mapped = detail::mappedNodes(g);
    if (!mapped) return 0;
    for (auto& n : g.nodes)
        if (const ActClass* c = taskClass(n.type)) detail::notePins(g, n, *c);
    g.newIdx = g.nodes.size() - 1;   // NameSpaceEnd moves up one
    detail::rewriteLinks(g);
    std::vector<oct::Rec> out(f.recs.begin(), f.recs.begin() + g.pool + 1);
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        if (k == g.newIdx && g.nRe) detail::emitAvatarList(g, k, out);
        else detail::rewriteNode(g, k, out);
    }
    for (size_t i = f.blockEnd(g.pool); i < f.recs.size(); ++i) out.push_back(f.recs[i]);
    f.recs = std::move(out);
    if (rerouted) *rerouted = g.nRe;
    if (dropped) *dropped = g.nDrop;
    return mapped;
}

// A 1.0 mission table (.lua text) of an activity whose task types all have a 3.0 counterpart: each such Data table's
// Type and renamed keys (the same maps as activityGraph30; the 3.0 factory FUN_00df42a0 builds the eval from Type),
// ChallengeZone added to the state machine (61/61 shipped tables carry it). Returns the tables changed (0: unchanged).
inline int activityTasks30(std::string& lua)
{
    auto toks = luaTokens(lua);
    struct Edit { size_t at, len; std::string text; };
    std::vector<Edit> edits;
    int n = 0;
    for (size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].kind != 'i' || lua.compare(toks[k].at, toks[k].len, "Type") || !assignedAt(lua, toks[k].at + 4)) continue;
        size_t q = lua.find('"', toks[k].at), e = q == std::string::npos ? q : lua.find('"', q + 1);
        if (e == std::string::npos || lua.find_first_not_of(" \t=", toks[k].at + 4) != q) continue;
        std::string t = lua.substr(q + 1, e - q - 1);
        if (t.rfind("Eval_Activity", 0)) continue;
        const ActClass* c = evalClass(t);
        if (!c) return 0;
        edits.push_back({q + 1, t.size(), c->eval30});
        int d = toks[k].depth;
        size_t open = k, close = k;
        while (open > 0 && !(toks[open].kind == '{' && toks[open].depth < d)) --open;
        while (close < toks.size() && !(toks[close].kind == '}' && toks[close].depth < d)) ++close;
        for (size_t j = open + 1; j < close; ++j)
            if (toks[j].kind == 'i' && toks[j].depth == d && assignedAt(lua, toks[j].at + toks[j].len))
                for (auto& p : c->params)
                    if (!lua.compare(toks[j].at, toks[j].len, p.first)) edits.push_back({toks[j].at, toks[j].len, p.second});
        if (std::string(c->cls30) == "MissionTask_ChallengeStateMachine" && close < toks.size())
            edits.push_back({toks[close].at, 0, "\tChallengeZone = \"NONE\",\n"});
        ++n;
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.at > b.at; });
    for (auto& e : edits) lua.replace(e.at, e.len, e.text);
    return n;
}

// The activity givers to redirect: the MissionGiver tags of a missiongraph's records whose mission converts
// (activityTasks30 != 0 on the mission table read by readMission(name)).
inline std::set<std::string> activityGivers(const std::string& graphLua, const std::function<bool(const std::string&, std::string&)>& readMission)
{
    std::set<std::string> ok;
    static const std::string g = "MissionGiver = \"", m = "MissionName = \"";
    for (size_t p = graphLua.find(g); p != std::string::npos; p = graphLua.find(g, p + 1)) {
        size_t ge = graphLua.find('"', p + g.size()), q = graphLua.find(m, p), qe = q == std::string::npos ? q : graphLua.find('"', q + m.size());
        size_t nextG = graphLua.find(g, p + 1);
        if (ge == std::string::npos || qe == std::string::npos || (nextG != std::string::npos && q > nextG)) continue;
        std::string tag = graphLua.substr(p + g.size(), ge - p - g.size()), name = graphLua.substr(q + m.size(), qe - q - m.size()), t;
        if (!tag.empty() && !name.empty() && readMission(name, t) && activityTasks30(t)) ok.insert(tag);
    }
    return ok;
}

// A 1.0 DNA chunk's MissionGiverActivity gadget as 3.0's MissionGiverChallenge. Returns constants renamed.
inline int dnaGadgets30(const std::string& in, std::string& out)
{
    return luachunk::rewrite(in, out, [](std::string& s) {
        if (s != "MissionGiverActivity") return false;
        s = "MissionGiverChallenge";
        return true;
    });
}

// Tools and packs (plan TL-1): the level list of each 1.0 tool, from var/ToolSet/ToolSet.bin as 1.0's ToolSetInfo
// (FUN_00f768f0) lays it out: [u32 size] sections; 0 = string pool, 1 = -1-terminated lists of string offsets (u32
// words), 11 = the Tools records (u32 count, 76 B each: +0 name, +0x10 LevelPrefix list index, -1 = none).
// toolgate.py reads the same: 177 records, 78 with a list; PROP_CAPE / PROP_HOVERBOARD / ITEM_ZEROPOINTENERGY list
// rumpus, adv_, core, holo, in_.
inline std::map<std::string, std::vector<std::string>> toolLevels10(const uint8_t* d, size_t n)
{
    std::map<std::string, std::vector<std::string>> out;
    std::vector<std::pair<size_t, size_t>> secs;
    for (size_t o = 0; bytes::fits(n, o, 8);) {
        uint32_t sz = oct::u(d + o, 4);
        if (sz < 4 || !bytes::fits(n, o, sz)) return out;
        secs.push_back({o, sz});
        o += sz;
    }
    if (secs.size() < 12) return out;
    const char* pool = (const char*)d + 4;
    size_t poolN = secs[0].second - 4, lists = secs[1].first + 4, listsEnd = secs[1].first + secs[1].second;
    const size_t listWords = (listsEnd - lists) / 4;   // u32 words in section 1
    auto str = [&](int32_t off) { return off >= 0 && (size_t)off < poolN ? std::string(pool + off, strnlen(pool + off, poolN - off)) : std::string(); };
    size_t so = secs[11].first, sz = secs[11].second, cnt = oct::u(d + so + 4, 4);
    if (sz < 8 || (sz - 8) % 76 || cnt != (sz - 8) / 76) return out;   // (no 32-bit wrap of cnt * 76)
    for (size_t r = 0; r < cnt; ++r) {
        const uint8_t* rec = d + so + 8 + r * 76;
        std::vector<std::string>& lv = out[str((int32_t)oct::u(rec, 4))];
        for (int32_t i = (int32_t)oct::u(rec + 0x10, 4); i >= 0 && bytes::fits(listWords, (size_t)i, 1); ++i) {
            int32_t w = (int32_t)oct::u(d + lists + 4 * i, 4);
            if (w == -1) break;
            lv.push_back(str(w));
        }
    }
    return out;
}

// 3.0's test of a level list (FUN_0150f7d0): an empty list allows every level, else the lower-cased zone name must
// start with one entry (CString Find == 0).
inline bool levelAllows(const std::vector<std::string>& prefixes, const char* zone)
{
    if (prefixes.empty()) return true;
    std::string z = zone ? zone : "";
    for (char& c : z) c = (char)tolower((unsigned char)c);
    for (const std::string& p : prefixes)
        if (!p.empty() && z.compare(0, p.size(), p) == 0) return true;
    return false;
}

}  // namespace gameplay
