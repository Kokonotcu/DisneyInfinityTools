// 1.0 choreographies' Static nodes under constant headers (FORMATS.md 4.10). convert/records; any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "../../convert/records.hpp"
#include "../../formats/oct.hpp"

namespace di1::cho {

// ---- constant section ----
namespace detail {

// A DataNode as const30 reads it
struct ConstNode {
    long hd = -1, st = -1;   // its Header and Static records
    bool stat = false, ext = false;
    std::string type;
};
struct ConstProc {
    std::string type;
    std::vector<std::string> attrs;   // DataNodeAttributes
    std::vector<int32_t> refs;        // DataNodeRefs
};
// What const30 reads of one SubNetwork
struct ConstScan {
    long hs = -1, hsi = -1, hl = -1;   // HeaderStrings, HeaderStringIndices, HeaderLayout
    long after = -1;                   // its last level-3 Type or Name: ConstantHeaderCount goes after it
    bool counted = false;              // it has a VariableHeaderCount or ConstantHeaderCount already
    std::vector<ConstNode> dns;
    std::vector<long> pn;              // ProcessorNode Header records
    std::vector<ConstProc> procs;
    std::set<int> convRefs;            // BasisConversion DataNodeRefs and ConnectionMapUsedDataNodes
    std::vector<std::string> strs;     // HeaderStrings, as const30 extends them
};

inline void lastTypeOrName(const oct::File&, size_t i, ConstScan& s) { s.after = (std::max)(s.after, (long)i); }
inline void alreadyCounted(const oct::File&, size_t, ConstScan& s) { s.counted = true; }
inline constexpr octx::FieldRule<ConstScan> kConstLevel3[] = {
    {"HeaderStrings", [](const oct::File&, size_t i, ConstScan& s) { s.hs = (long)i; }},
    {"HeaderStringIndices", [](const oct::File&, size_t i, ConstScan& s) { s.hsi = (long)i; }},
    {"HeaderLayout", [](const oct::File&, size_t i, ConstScan& s) { s.hl = (long)i; }},
    {"VariableHeaderCount", alreadyCounted},
    {"ConstantHeaderCount", alreadyCounted},
    {"Type", lastTypeOrName},
    {"Name", lastTypeOrName},
};
inline constexpr octx::FieldRule<ConstNode> kConstDataField[] = {
    {"Header", [](const oct::File&, size_t i, ConstNode& d) { d.hd = (long)i; }},
    {"Static",
     [](const oct::File& f, size_t i, ConstNode& d) {
         d.st = (long)i;
         d.stat = octx::firstInt(f, f.recs[i], 0) != 0;
     }},
    {"External", [](const oct::File& f, size_t i, ConstNode& d) { d.ext = octx::firstInt(f, f.recs[i], 0) != 0; }},
    {"Type",
     [](const oct::File& f, size_t i, ConstNode& d) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) d.type = v[0];
     }},
};
inline constexpr octx::FieldRule<ConstProc> kConstProcField[] = {
    {"Type",
     [](const oct::File& f, size_t i, ConstProc& p) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) p.type = v[0];
     }},
    {"DataNodeAttributes", [](const oct::File& f, size_t i, ConstProc& p) { p.attrs = f.nameValues(f.recs[i]); }},
    {"DataNodeRefs", [](const oct::File& f, size_t i, ConstProc& p) { p.refs = f.ints(f.recs[i]); }},
};

// A level-5 field of the pool named pool3
inline void scanConstField(const oct::File& f, size_t i, const std::string& pool3, ConstScan& s)
{
    const oct::Rec& r = f.recs[i];
    if (pool3 == "DataNodePool" && !s.dns.empty()) octx::readField(f, i, kConstDataField, s.dns.back());
    else if (pool3 == "ProcessorNodePool" && !s.pn.empty()) {
        if (f.name(r) == "Header") s.pn.back() = (long)i;
        octx::readField(f, i, kConstProcField, s.procs.back());
    } else if (pool3 == "BasisConversionPool" && f.name(r) == "DataNodeRef") {
        s.convRefs.insert(octx::firstInt(f, r, -1));
    }
}

// A level-4 entry of the pool named pool3
inline void scanConstEntry(const std::string& pool3, ConstScan& s)
{
    if (pool3 == "DataNodePool") s.dns.emplace_back();
    else if (pool3 == "ProcessorNodePool") {
        s.pn.push_back(-1);
        s.procs.emplace_back();
    }
}

inline ConstScan scanConst(const oct::File& f, size_t sn)
{
    ConstScan s;
    std::string pool3;   // the level-3 record the scan is under
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        if (r.level == 3) {
            pool3 = f.name(r);
            octx::readField(f, i, kConstLevel3, s);
            if (pool3 == "ConnectionMapUsedDataNodes")
                for (int32_t d : f.ints(r)) s.convRefs.insert(d);   // same treatment: must stay variable
        } else if (r.level == 4) scanConstEntry(pool3, s);
        else if (r.level == 5) scanConstField(f, i, pool3, s);
    }
    return s;
}

// A SubNetwork's header tree, headers 1..H: parent (0 = the root), name and children each
struct HeaderTree {
    size_t H = 0;
    std::vector<int> parent;
    std::vector<std::string> name;
    std::vector<std::vector<int>> kids;
};

// HeaderLayout's (parent, child count) pairs decoded; nullopt if they name a later parent or a missing string
inline std::optional<HeaderTree> headerTree(const std::vector<std::string>& strs, const std::vector<int32_t>& idx,
                                            const std::vector<int32_t>& lay)
{
    HeaderTree t;
    t.H = idx.size();
    t.parent.assign(t.H + 1, -1);
    t.name.assign(t.H + 1, std::string());
    t.kids.assign(t.H + 1, std::vector<int>());
    size_t k = 0;
    for (size_t p = 0; p + 1 < lay.size(); p += 2)
        for (int32_t c = 0; c < lay[p + 1]; ++c) {
            ++k;
            const bool parentOk = k <= t.H && lay[p] >= 0 && lay[p] < (int)k;
            if (!parentOk || idx[k - 1] < 0 || idx[k - 1] >= (int)strs.size()) return std::nullopt;
            t.parent[k] = lay[p];
            t.name[k] = strs[idx[k - 1]];
            t.kids[lay[p]].push_back((int)k);
        }
    if (k != t.H) return std::nullopt;
    return t;
}

// The header a Header record names, -1 for none or one outside 1..H
inline int headerOf(const oct::File& f, const HeaderTree& t, long rec)
{
    int h = rec < 0 ? -1 : octx::firstInt(f, f.recs[rec], -1);
    return h >= 1 && h <= (int)t.H ? h : -1;
}

// Header h's path from the root, 'a/b/c'
inline std::string pathOf(const HeaderTree& t, int h)
{
    std::string s;
    for (int x = h; x > 0; x = t.parent[x]) s = s.empty() ? t.name[x] : t.name[x] + "/" + s;
    return s;
}

// Every processor attribute 3.0 ships with a Static node (FORMATS.md 4.10, staticrefs.py), sorted for the search
inline constexpr std::string_view kStaticInputs30[] = {
    "AimConstraintProcessorNode.AlignReference", "AimConstraintProcessorNode.Offset",
    "AimConstraintProcessorNode.UpReference", "ClipProcessor.ClipData", "DilationOrientationConnection.RotateOffset",
    "DilationTranslationConnection.TranslateOffset", "DilationTranslationOrientationConnection.RotateOffset",
    "DilationTranslationOrientationConnection.TranslateOffset", "EventList.EventListData",
    "EventList.ReverseEventListData", "IK2BRPLengthSquashZConstraint.InputDistanceThreshold",
    "IK2BRPLengthSquashZConstraint.InputEndInitialPosition", "IK2BRPLengthSquashZConstraint.InputMidInitialPosition",
    "IK2BRPLengthSquashZConstraint.InputMidLocalToParentInitialOrientation",
    "IK2BRPLengthSquashZConstraint.InputMidLocalToStartParentInitialOrientation",
    "IK2BRPLengthSquashZConstraint.InputStartInitialPosition",
    "IK2BRPLengthSquashZConstraint.InputStartLocalToParentInitialOrientation",
    "IK2BRPLengthSquashZConstraint.InputStartLocalToStartParentInitialOrientation", "IkLean2LinkProcessor.Axis",
    "MotionPathOrientationProcessor.Spline", "MotionPathPositionProcessor.Spline", "Position3dOffsetProcessor.Offset",
    "Position3dWeightedAverageProcessor.TargetWeight", "RadialBasisApplyFloat.Neutral", "RadialBasisApplyFloat.Weights",
    "RadialBasisPosition.ControllerKeys", "RadialBasisPosition.StandardDeviationValues",
    "RelationshipProcessor.RelationshipData", "SplineInputProcessor.Spline",
    "TransformTranslationOrientationConnection.RotateOffset",
    "TransformTranslationOrientationScaleConnection.TranslateOffset", "UnitRotor3dOffsetProcessor.Offset",
    "UnitRotor3dWeightedAverageProcessor.TargetWeight"};

inline bool staticInput30(const std::string& typeDotAttribute)
{
    auto less = [](std::string_view a, const std::string& b) { return a < std::string_view(b); };
    auto it = std::lower_bound(std::begin(kStaticInputs30), std::end(kStaticInputs30), typeDotAttribute, less);
    return it != std::end(kStaticInputs30) && typeDotAttribute == *it;
}

// The DataNodes some processor takes through an input 3.0 never feeds from a Static node
inline std::vector<bool> offListInputs(const ConstScan& s)
{
    std::vector<bool> offList(s.dns.size(), false);
    for (auto& pr : s.procs)
        for (size_t q = 0; q < pr.attrs.size() && q < pr.refs.size(); ++q) {
            std::string a = pr.attrs[q].substr(0, pr.attrs[q].find('['));
            const bool node = pr.refs[q] >= 0 && (size_t)pr.refs[q] < s.dns.size();
            if (node && !staticInput30(pr.type + "." + a)) offList[pr.refs[q]] = true;
        }
    return offList;
}

// Makes a Static node variable, in the file too (it stays so when const30 skips the SubNetwork)
inline void demote(oct::File& f, ConstNode& d)
{
    f.setInt(f.recs[d.st], 0);
    d.stat = false;
}

// True when an Association binds header h's path (named: the SubNetwork's bound paths, null for none)
inline bool boundHeader(const HeaderTree& t, int h, const std::set<std::string>* named)
{
    return h > 0 && named && named->count(pathOf(t, h));
}

// Static nodes of a shape 3.0 never keeps Static become variable (FORMATS.md 4.10); named: Association paths
inline void demoteStatics(oct::File& f, ConstScan& s, const HeaderTree& t, const std::set<std::string>* named)
{
    const std::vector<bool> offList = offListInputs(s);
    for (size_t di = 0; di < s.dns.size(); ++di) {
        ConstNode& d = s.dns[di];
        if (!d.stat) continue;
        const int h = headerOf(f, t, d.hd);
        const bool neverStatic = d.type == "Bool" || d.type == "Int" || d.type == "Visibility";
        const bool external = d.ext && d.type != "ClipData";
        if (neverStatic || external) {
            demote(f, d);
            continue;
        }
        const bool listed = s.convRefs.count((int)di) || offList[di];
        if (listed || boundHeader(t, h, named)) demote(f, d);   // boundHeader last: it builds the path
    }
}

// True when some path of var is p or lies below it
inline bool onOrBelow(const std::vector<std::string>& var, const std::string& p)
{
    for (const std::string& v : var) {
        const bool below = v.size() > p.size() && v.compare(0, p.size(), p) == 0 && v[p.size()] == '/';
        if (v == p || below) return true;
    }
    return false;
}

// The header paths of the variable nodes and the processors
inline std::vector<std::string> variablePaths(const oct::File& f, const ConstScan& s, const HeaderTree& t)
{
    std::vector<std::string> var;
    for (auto& d : s.dns)
        if (!d.stat && headerOf(f, t, d.hd) > 0) var.push_back(pathOf(t, headerOf(f, t, d.hd)));
    for (long r : s.pn)
        if (headerOf(f, t, r) > 0) var.push_back(pathOf(t, headerOf(f, t, r)));
    return var;
}

// A Static node with a variable node or processor on or below its header becomes variable, until none is left
inline void dropClashes(oct::File& f, ConstScan& s, const HeaderTree& t)
{
    for (bool again = true; again;) {
        again = false;
        const std::vector<std::string> var = variablePaths(f, s, t);
        for (auto& d : s.dns) {
            if (!d.stat || headerOf(f, t, d.hd) <= 0) continue;
            if (!onOrBelow(var, pathOf(t, headerOf(f, t, d.hd)))) continue;
            demote(f, d);
            again = true;
        }
    }
}

// A Static node's variable header goes when nothing else uses it and all its children went
inline std::vector<bool> droppedHeaders(const HeaderTree& t, const std::set<int>& moving, const std::set<int>& used)
{
    std::vector<bool> dropped(t.H + 1, false);
    for (int h = (int)t.H; h >= 1; --h) {   // headers are numbered parents first: children are decided first
        bool d = moving.count(h) && !used.count(h);
        for (int c : t.kids[h]) d = d && dropped[c];
        dropped[h] = d;
    }
    return dropped;
}

// The constant headers: (parent, name) -> id, ids 1.. in creation order
struct ConstTrie {
    std::vector<int> cpar{-1};
    std::vector<std::string> cname{""};
    std::vector<std::vector<int>> ckids = std::vector<std::vector<int>>(1);
    std::map<std::pair<int, std::string>, int> ids;
    std::map<int, int> constOf;   // old header of a Static node -> constant id
};

// Constant id of the path comps, the missing headers created
inline int trieAdd(ConstTrie& c, const std::vector<std::string>& comps)
{
    int at = 0;
    for (auto& name : comps) {
        auto t = c.ids.find({at, name});
        if (t == c.ids.end()) {
            int id = (int)c.cpar.size();
            c.cpar.push_back(at);
            c.cname.push_back(name);
            c.ckids.push_back({});
            c.ckids[at].push_back(id);
            t = c.ids.emplace(std::make_pair(at, name), id).first;
        }
        at = t->second;
    }
    return at;
}

// Each moving header's constant path: its variable path with the root prefixed 'Constant' below the root
inline ConstTrie constantTrie(const HeaderTree& t, const std::set<int>& moving)
{
    ConstTrie c;
    for (int h : moving) {
        std::vector<std::string> comps;
        for (int x = h; x > 0; x = t.parent[x]) comps.insert(comps.begin(), t.name[x]);
        if (comps.size() > 1) comps[0] = "Constant" + comps[0];
        c.constOf[h] = trieAdd(c, comps);
    }
    return c;
}

// The new header numbering: constant headers 1..chc, then the kept variable headers breadth first
struct ConstLayout {
    std::vector<std::string> newName{""};
    std::vector<int32_t> newLay;   // (parent, child count) pairs
    std::vector<int> cnum, vnum;   // per constant id / old variable header: its number in newName
    int chc = 0;
};

inline ConstLayout numberHeaders(const HeaderTree& t, const ConstTrie& c, const std::vector<bool>& dropped)
{
    ConstLayout L;
    L.cnum.assign(c.cpar.size(), 0);
    L.vnum.assign(t.H + 1, 0);
    for (size_t q = 0; q < c.cpar.size(); ++q) {   // ids are created parents-first, so q's number exists
        int me = q == 0 ? 0 : L.cnum[q];
        if (c.ckids[q].empty()) continue;
        L.newLay.insert(L.newLay.end(), {me, (int32_t)c.ckids[q].size()});
        for (int k : c.ckids[q]) {
            L.cnum[k] = (int)L.newName.size();
            L.newName.push_back(c.cname[k]);
        }
    }
    L.chc = (int)L.newName.size() - 1;
    std::vector<int> order{0};   // variable headers breadth first from the root, kept ones only
    for (size_t q = 0; q < order.size(); ++q) {
        std::vector<int> keep;
        for (int k : t.kids[order[q]]) if (!dropped[k]) keep.push_back(k);
        if (keep.empty()) continue;
        L.newLay.insert(L.newLay.end(), {order[q] == 0 ? 0 : L.vnum[order[q]], (int32_t)keep.size()});
        for (int k : keep) {
            L.vnum[k] = (int)L.newName.size();
            L.newName.push_back(t.name[k]);
            order.push_back(k);
        }
    }
    return L;
}

// The header records rewritten to the new numbering, strings appended as needed; returns ConstantHeaderCount
inline oct::Rec rewriteHeaders(oct::File& f, ConstScan& s, const HeaderTree& t, const ConstTrie& c,
                               const ConstLayout& L)
{
    std::vector<std::string>& strs = s.strs;
    std::vector<int32_t> newIdx;
    for (size_t h = 1; h < L.newName.size(); ++h) {
        auto it = std::find(strs.begin(), strs.end(), L.newName[h]);   // the first match
        if (it == strs.end()) {
            strs.push_back(L.newName[h]);
            it = strs.end() - 1;
        }
        newIdx.push_back((int32_t)(it - strs.begin()));
    }
    f.setInts(f.recs[s.hsi], newIdx);
    f.setInts(f.recs[s.hl], L.newLay);
    octx::setNames(f, f.recs[s.hs], strs);
    for (auto& d : s.dns) {
        int h = headerOf(f, t, d.hd);
        if (h < 0) continue;
        auto id = c.constOf.find(h);
        f.setInt(f.recs[d.hd], d.stat ? L.cnum[id == c.constOf.end() ? 0 : id->second] : L.vnum[h]);
    }
    for (long r : s.pn) {
        int h = headerOf(f, t, r);
        if (h >= 0) f.setInt(f.recs[r], L.vnum[h]);
    }
    return f.intScalar(3, "ConstantHeaderCount", L.chc);
}

// The constant header paths, 'ConstantA/b'
inline std::set<std::string> constantPaths(const ConstTrie& c)
{
    std::set<std::string> paths;
    for (size_t q = 1; q < c.cpar.size(); ++q) {
        std::string pth = c.cname[q];
        for (int x = c.cpar[q]; x > 0; x = c.cpar[x]) pth = c.cname[x] + "/" + pth;
        paths.insert(pth);
    }
    return paths;
}

// One SubNetwork's Static nodes under constant headers (its insert into edits): its constant paths, nullopt if none
inline std::optional<std::set<std::string>> constSubNetwork(oct::File& f, size_t sn, const std::set<std::string>* named,
                                                           std::vector<octx::RangeEdit>& edits)
{
    ConstScan s = scanConst(f, sn);
    const bool headers = s.hs >= 0 && s.hsi >= 0 && s.hl >= 0;
    const bool usable = headers && s.after >= 0 && !s.pn.empty();
    if (s.counted || !usable) return std::nullopt;
    const oct::Spec hsSpec = oct::spec(f.recs[s.hs].type);
    if (hsSpec.kind != oct::NAME || hsSpec.keyed) return std::nullopt;
    s.strs = f.nameValues(f.recs[s.hs]);
    const std::optional<HeaderTree> t = headerTree(s.strs, f.ints(f.recs[s.hsi]), f.ints(f.recs[s.hl]));
    if (!t) return std::nullopt;
    demoteStatics(f, s, *t, named);
    dropClashes(f, s, *t);
    if (std::none_of(s.dns.begin(), s.dns.end(), [](const ConstNode& d) { return d.stat; }))
        return std::nullopt;   // addCounts' variable form is then the 3.0 one
    std::set<int> moving, used;
    for (auto& d : s.dns) (d.stat ? moving : used).insert(headerOf(f, *t, d.hd));
    for (long r : s.pn) used.insert(headerOf(f, *t, r));
    moving.erase(-1);
    if (moving.empty()) return std::nullopt;
    const std::vector<bool> dropped = droppedHeaders(*t, moving, used);
    const ConstTrie c = constantTrie(*t, moving);
    edits.push_back({(size_t)s.after + 1, 0, {rewriteHeaders(f, s, *t, c, numberHeaders(*t, c, dropped))}});
    return constantPaths(c);
}

// SubNetwork ordinal -> the paths its Associations bind ('a/b'), from NodeName and SubNetworkRef
inline std::map<int, std::set<std::string>> boundPaths(const oct::File& f)
{
    std::map<int, std::set<std::string>> named;
    long ap = f.pool("AssociationPool");
    for (size_t i = ap < 0 ? 0 : ap + 1, e = ap < 0 ? 0 : f.blockEnd(ap); i < e; i = f.blockEnd(i)) {
        std::string node;
        int snRef = -1;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) {
            const std::string& k = f.name(f.recs[j]);
            if (k == "NodeName") {
                auto v = f.nameValues(f.recs[j]);
                if (!v.empty()) node = v[0];
            } else if (k == "SubNetworkRef") snRef = octx::firstInt(f, f.recs[j], -1);
        }
        std::replace(node.begin(), node.end(), '|', '/');
        if (snRef >= 0 && !node.empty()) named[snRef].insert(node);
    }
    return named;
}

// An Association as the AltNodeName pass reads it
struct AltScan {
    std::string type, node;
    int snRef = -1;
    bool alt = false;
};
inline constexpr octx::FieldRule<AltScan> kAltFields[] = {
    {"Type",
     [](const oct::File& f, size_t i, AltScan& a) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) a.type = v[0];
     }},
    {"NodeName",
     [](const oct::File& f, size_t i, AltScan& a) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) a.node = v[0];
     }},
    {"SubNetworkRef", [](const oct::File& f, size_t i, AltScan& a) { a.snRef = octx::firstInt(f, f.recs[i], -1); }},
    {"AltNodeName", [](const oct::File&, size_t, AltScan& a) { a.alt = true; }},
};

// AltNodeName on each Transform Association whose 'Constant'+NodeName header exists (FORMATS.md 4.10)
inline void addAltNodeNames(oct::File& f, const std::map<int, std::set<std::string>>& constPaths,
                            std::vector<octx::RangeEdit>& edits)
{
    long ap = f.pool("AssociationPool");
    if (ap < 0) return;
    std::vector<std::pair<size_t, std::string>> adds;   // insert position, AltNodeName
    for (size_t i = ap + 1, e = f.blockEnd(ap); i < e; i = f.blockEnd(i)) {
        if (f.recs[i].level != 2) continue;
        AltScan a;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) octx::readField(f, j, kAltFields, a);
        std::string want = "Constant" + a.node;
        std::replace(want.begin(), want.end(), '|', '/');
        auto paths = constPaths.find(a.snRef);
        const bool has = paths != constPaths.end() && paths->second.count(want);
        if (a.type == "Transform" && !a.alt && has) adds.push_back({f.blockEnd(i), "Constant" + a.node});
    }
    for (auto it = adds.rbegin(); it != adds.rend(); ++it) {   // last first: the order names were added in
        const uint16_t key = f.addName("AltNodeName");
        edits.push_back({it->first, 0, {f.nameScalar(3, key, it->second)}});
    }
}

}  // namespace detail

// const30 (choconst10): processor SubNetworks' Static nodes under constant headers (FORMATS.md 4.10)
inline int const30(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    const std::vector<size_t> sns = octx::subNetworks(f, pool);
    int done = 0;
    std::map<int, std::set<std::string>> constPaths;   // SubNetwork ordinal -> its constant header paths
    const std::map<int, std::set<std::string>> named = detail::boundPaths(f);
    std::vector<octx::RangeEdit> edits;
    for (size_t q = sns.size(); q-- > 0;) {   // last first: the order names were added in
        auto bound = named.find((int)q);
        const std::set<std::string>* paths = bound == named.end() ? nullptr : &bound->second;
        std::optional<std::set<std::string>> made = detail::constSubNetwork(f, sns[q], paths, edits);
        if (!made) continue;
        constPaths[(int)q] = std::move(*made);
        ++done;
    }
    if (done) detail::addAltNodeNames(f, constPaths, edits);
    octx::applyEdits(f, edits);
    return done;
}

}  // namespace di1::cho
