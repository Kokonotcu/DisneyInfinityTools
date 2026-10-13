// 1.0 gameplay data (track gp: traversal, vehicles, traffic, tools, activities, store) in the 3.0 systems that
// replaced it, at load time. The 1.0 files are never rewritten; each transform is checked offline (octtest, scratch
// oracles) against the shipped 3.0 corpus.
#pragma once
#include <cctype>
#include <cstring>
#include <functional>
#include <set>
#include "octx.hpp"
#include "luachunk.hpp"

namespace gameplay {

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
        int v = f.ints(r)[0];
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
// it (0 of 16). Returns the sections changed.
inline int apGivers30(std::string& ap)
{
    int n = 0;
    for (size_t sec = ap.find('['); sec != std::string::npos;) {
        size_t next = ap.find("\n[", sec + 1);
        size_t end = next == std::string::npos ? ap.size() : next + 1;
        size_t icon = ap.find("\nActivityIcon=", sec);
        if (icon != std::string::npos && icon < end && ap.find("\nChallengeType=", sec) >= end) {
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

// A 1.0 DNA chunk's MissionGiverActivity gadget as 3.0's MissionGiverChallenge. Returns constants renamed.
inline int dnaGadgets30(const std::string& in, std::string& out)
{
    return luachunk::rewrite(in, out, [](std::string& s) {
        if (s != "MissionGiverActivity") return false;
        s = "MissionGiverChallenge";
        return true;
    });
}

}  // namespace gameplay
