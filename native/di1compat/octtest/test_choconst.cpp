// Acceptance tests of cho::const30 with the counts' constant mode, and of octx's variant groups.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include "../cho.hpp"
#include "chotree.hpp"
#include "files.hpp"
#include "run.hpp"
#include "tuples.hpp"
#include "modes.hpp"
#include "plugin.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

constexpr char kConstantPrefix[] = "Constant";   // const30 names a nested Static node's header 'Constant'+path
constexpr size_t kConstantPrefixLen = sizeof(kConstantPrefix) - 1;
constexpr size_t kShownStaticAssocs = 5, kShownNotConverted = 20, kShownBadSubnets = 4, kShownVariantTwins = 4;

const char* yesNo(bool b) { return b ? "yes" : "NO"; }

bool isIn(const std::vector<size_t>& v, size_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// A rule's tally key with the verdict "NO" (rule keys end in "yes" or "NO").
bool saysNo(const std::string& k)
{
    constexpr char kNo[] = "NO";
    constexpr size_t kNoLen = sizeof(kNo) - 1;
    return k.size() > kNoLen + 1 && k.compare(k.size() - kNoLen, kNoLen, kNo) == 0;
}

// The Static DataNodes of s, in node order.
std::vector<size_t> staticNodes(const oct::File& f, const SubNet& s)
{
    std::vector<size_t> v;
    for (size_t d : s.dataNodes) {
        long st = child(f, d, "Static");
        if (st >= 0 && cho::first(f, f.recs[st], 0)) v.push_back(d);
    }
    return v;
}

// A level-3 count of the SubNetwork at record sn, -1 without it.
int snCount(const oct::File& f, size_t sn, const char* k)
{
    long r = child(f, sn, k);
    return r < 0 ? -1 : cho::first(f, f.recs[r], -1);
}

// An association's SubNetwork index (SubNetworkRef), -1 without one.
int assocSubnet(const oct::File& f, size_t i)
{
    long sr = child(f, i, "SubNetworkRef");
    return sr < 0 ? -1 : cho::first(f, f.recs[sr], -1);
}

int headerNumber(const oct::File& g, size_t rec)
{
    long h = child(g, rec, "Header");
    return h < 0 ? -1 : cho::first(g, g.recs[h], -1);
}

// ---- Static node uses: measured on the 3.0 corpus, checked on the 1.0 output ----

enum class StaticUse { Shape, VariableBelow, Input, ConnectionMap, Conversion };
using StaticVisit = std::function<void(StaticUse, const std::string&)>;

// What the 3.0 corpus ships: processor inputs fed by a Static node, Static node shapes, conversion refs.
struct StaticShapes {
    std::set<std::string> inputs, shapes;
    size_t conversions = 0;
};

bool isStaticRef(const SubNet& s, const std::set<size_t>& st, int32_t d)
{
    return d >= 0 && (size_t)d < s.dataNodes.size() && st.count(s.dataNodes[d]);
}

bool anyBelow(const std::vector<std::string>& paths, const std::string& prefix)
{
    for (const std::string& v : paths)
        if (v.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

// Each Static node's shape ('Type External?'), and whether a variable node or processor sits below it.
void visitStaticNodes(const oct::File& f, const SubNet& s, const std::set<size_t>& st, const StaticVisit& visit)
{
    std::vector<std::string> varPaths;
    for (size_t d : s.dataNodes)
        if (!st.count(d)) varPaths.push_back(headerPath(f, s, d));
    for (size_t q : s.processors) varPaths.push_back(headerPath(f, s, q));
    for (size_t d : st) {
        long ex = child(f, d, "External");
        const bool external = ex >= 0 && cho::first(f, f.recs[ex], 0);
        visit(StaticUse::Shape, childName(f, d, "Type") + (external ? " External" : ""));
        if (anyBelow(varPaths, headerPath(f, s, d) + "/")) visit(StaticUse::VariableBelow, "");
    }
}

// Each processor input ('Type.Attr') a Static node feeds.
void visitStaticInputs(const oct::File& f, const SubNet& s, const std::set<size_t>& st, const StaticVisit& visit)
{
    for (size_t q : s.processors) {
        long ar = child(f, q, "DataNodeAttributes"), rr = child(f, q, "DataNodeRefs");
        if (ar < 0 || rr < 0) continue;
        auto at = f.nameValues(f.recs[ar]);
        auto rf = f.ints(f.recs[rr]);
        for (size_t z = 0; z < at.size() && z < rf.size(); ++z)
            if (isStaticRef(s, st, rf[z]))
                visit(StaticUse::Input, childName(f, q, "Type") + "." + at[z].substr(0, at[z].find('[')));
    }
}

// Static nodes in ConnectionMapUsedDataNodes and in BasisConversion DataNodeRefs.
void visitStaticRefs(const oct::File& f, const SubNet& s, const std::set<size_t>& st, const StaticVisit& visit)
{
    long cm = child(f, s.at, "ConnectionMapUsedDataNodes");
    if (cm >= 0) {
        for (int32_t d : f.ints(f.recs[cm]))
            if (isStaticRef(s, st, d)) visit(StaticUse::ConnectionMap, "");
    }
    for (size_t j = s.at + 1, e = f.blockEnd(s.at); j < e; ++j) {
        if (f.recs[j].level != 5 || f.name(f.recs[j]) != "DataNodeRef") continue;
        if (isStaticRef(s, st, cho::first(f, f.recs[j], -1))) visit(StaticUse::Conversion, "");
    }
}

// Every Static node use in f's processor SubNetworks.
void visitStaticUses(const oct::File& f, const StaticVisit& visit)
{
    for (const SubNet& s : subnets(f)) {
        if (s.processors.empty()) continue;
        const std::vector<size_t> sv = staticNodes(f, s);
        const std::set<size_t> st(sv.begin(), sv.end());
        visitStaticNodes(f, s, st, visit);
        visitStaticInputs(f, s, st, visit);
        visitStaticRefs(f, s, st, visit);
    }
}

void collectUse(StaticShapes& s, StaticUse u, const std::string& v)
{
    switch (u) {
    case StaticUse::Shape:
        s.shapes.insert(v);
        break;
    case StaticUse::VariableBelow:
        s.shapes.insert("(Static with variable descendants)");
        break;
    case StaticUse::Input:
        s.inputs.insert(v);
        break;
    case StaticUse::ConnectionMap:
        s.shapes.insert("(ConnectionMapUsedDataNodes lists a Static node)");
        break;
    case StaticUse::Conversion:
        ++s.conversions;
        break;
    }
}

void tallyUnshipped(const StaticShapes& shipped, Tally& bad, StaticUse u, const std::string& v)
{
    switch (u) {
    case StaticUse::Shape:
        if (!shipped.shapes.count(v)) ++bad["Static node shape never shipped: " + v];
        break;
    case StaticUse::VariableBelow:
        ++bad["Static node with a variable node / processor below it"];
        break;
    case StaticUse::Input:
        if (!shipped.inputs.count(v)) ++bad["Static processor input never shipped: " + v];
        break;
    case StaticUse::ConnectionMap:
        ++bad["ConnectionMapUsedDataNodes lists a Static node"];
        break;
    case StaticUse::Conversion:
        ++bad["BasisConversion DataNodeRef to a Static node"];
        break;
    }
}

// The 3.0 corpus's Static uses. Returns each folder's index, walked once for the twins too (OT-25).
std::vector<Corpus> shippedStaticUses(const char* assets30, StaticShapes& s)
{
    auto collect = [&s](StaticUse u, const std::string& v) { collectUse(s, u, v); };
    std::vector<Corpus> idx;
    for (const char* sub : kChoSubs) {
        idx.push_back(choIndex(assets30, sub));
        for (auto& [n, p] : idx.back()) {
            std::string b = slurp(p);
            if (!isTuple(b) || !hasName(b, "ProcessorNodePool")) continue;
            visitStaticUses(parse(b), collect);
        }
    }
    return idx;
}

// ---- --cho-const: one 1.0 file ----

struct ConstRun {
    Tally c;
    size_t badShown = 0;   // bad SubNetworks printed so far
    StaticShapes shipped;
    octx::Options octx;
};

// A 1.0 file's SubNetworks before const30: header paths, and which DataNodes were Static.
struct ConstBefore {
    std::vector<SubNet> subnets;
    std::vector<std::vector<std::string>> paths;   // DataNode paths, then ProcessorNode paths
    std::vector<std::vector<bool>> wasStatic;      // per DataNode
};

ConstBefore beforeConst(const oct::File& f)
{
    ConstBefore b;
    b.subnets = subnets(f);
    for (auto& s : b.subnets) {
        std::vector<std::string> v;
        for (size_t d : s.dataNodes) v.push_back(headerPath(f, s, d));
        for (size_t q : s.processors) v.push_back(headerPath(f, s, q));
        b.paths.push_back(v);
        std::vector<bool> st;
        const auto statics = staticNodes(f, s);
        for (size_t d : s.dataNodes) st.push_back(isIn(statics, d));
        b.wasStatic.push_back(st);
    }
    return b;
}

// Associations that name a Static node by its path before const30: that node must stay variable.
void tallyStaticAssociations(const oct::File& f, const ConstBefore& before, const std::string& name, Tally& c)
{
    long ap = f.pool("AssociationPool");
    for (size_t i = ap < 0 ? 0 : ap + 1, e = ap < 0 ? 0 : f.blockEnd(ap); i < e; i = f.blockEnd(i)) {
        int k = assocSubnet(f, i);
        if (k < 0 || k >= (int)before.subnets.size()) continue;
        std::string nn = childName(f, i, "NodeName");
        std::replace(nn.begin(), nn.end(), '|', '/');
        for (size_t d = 0; d < before.subnets[k].dataNodes.size(); ++d) {
            if (!before.wasStatic[k][d] || before.paths[k][d] != nn) continue;
            const size_t n = ++c["association binds a 1.0 Static node (must stay variable)"];
            if (n <= kShownStaticAssocs) printf("assoc %s names Static '%s'\n", name.c_str(), nn.c_str());
        }
    }
}

// One converted SubNetwork of the output, with what it was before const30.
struct ConstSn {
    const std::string& file;
    size_t index;
    const oct::File& g;
    const SubNet& s;
    const std::vector<std::string>& pathsBefore;
    const std::vector<bool>& wasStatic;
    int constantHeaders, variableHeaders, constantNodes, variableNodes, constantLayout;   // its count records
    std::vector<size_t> statics;                                                        // Static DataNodes now
};

// Rule a: Static nodes, and only they, under headers 1..CHC. Tallies each Static node made variable.
bool ruleStaticHeaders(const ConstSn& x, Tally& c)
{
    bool ok = true;
    for (size_t d = 0; d < x.s.dataNodes.size(); ++d) {
        const bool now = isIn(x.statics, x.s.dataNodes[d]);
        if (x.wasStatic[d] && !now) ++c["Static node made variable (" + childName(x.g, x.s.dataNodes[d], "Type") + ")"];
        if (!x.wasStatic[d] && now) ok = false;
    }
    for (size_t d : x.s.dataNodes) {
        const int h = headerNumber(x.g, d);
        const bool misplaced = isIn(x.statics, d) ? h > x.constantHeaders : h <= x.constantHeaders;
        if (h >= 0 && misplaced) ok = false;
    }
    for (size_t q : x.s.processors)
        if (headerNumber(x.g, q) >= 0 && headerNumber(x.g, q) <= x.constantHeaders) ok = false;
    return ok;
}

// Rule b: the constant headers are the prefixes of the Static paths. Collects those paths into mine.
bool ruleConstantPrefixes(const ConstSn& x, std::set<std::string>& mine)
{
    const auto& paths = x.s.paths;
    const size_t chc = (std::min<size_t>)(x.constantHeaders, paths.size() - 1);
    std::set<std::string> want, have(paths.begin() + 1, paths.begin() + 1 + chc);
    for (size_t d : x.statics) {
        std::string pth = headerPath(x.g, x.s, d);
        mine.insert(pth);
        for (size_t q = 0; q <= pth.size(); ++q)
            if (q == pth.size() || pth[q] == '/') want.insert(pth.substr(0, q));
    }
    return want == have;
}

struct CountsRule {
    bool ok;
    int layoutInts;   // HeaderLayout ints covering headers 1..CHC
    size_t headers;   // HeaderStringIndices entries
};

// Rule c: CHC + VHC = headers; CDC, VDC = Static, variable nodes; CLC = layout ints of headers 1..CHC.
CountsRule ruleCounts(const ConstSn& x)
{
    long hlr = child(x.g, x.s.at, "HeaderLayout");
    auto lay = x.g.ints(x.g.recs[hlr]);
    int got = 0, ints = 0;
    while (ints + 1 < (int)lay.size() && got < x.constantHeaders) {
        got += lay[ints + 1];
        ints += 2;
    }
    long hsir = child(x.g, x.s.at, "HeaderStringIndices");
    const size_t nh = x.g.ints(x.g.recs[hsir]).size(), nst = x.statics.size();
    const bool headers = x.constantHeaders + x.variableHeaders == (int)nh && got == x.constantHeaders;
    const bool nodes = x.constantNodes == (int)nst && x.variableNodes == (int)(x.s.dataNodes.size() - nst);
    return {headers && nodes && x.constantLayout == ints, ints, nh};
}

// Rule d: no nested Static node's plain path is left among the variable headers.
bool rulePlainPathsGone(const ConstSn& x, const std::set<std::string>& mine)
{
    std::set<std::string> var(x.s.paths.begin() + 1 + x.constantHeaders, x.s.paths.end());
    for (auto& m : mine) {
        std::string plain = m.rfind(kConstantPrefix, 0) == 0 ? m.substr(kConstantPrefixLen) : m;
        if (m.find('/') != std::string::npos && var.count(plain)) return false;
    }
    return true;
}

struct PathsRule {
    bool kept = true;       // other nodes and processors keep their header paths
    bool constant = true;   // each Static node gets 'Constant'+path (a root-level one keeps its own)
};

PathsRule rulePaths(const ConstSn& x)
{
    PathsRule r;
    size_t i = 0;
    for (size_t d = 0; d < x.s.dataNodes.size(); ++d, ++i) {
        const std::string now = headerPath(x.g, x.s, x.s.dataNodes[d]), was = x.pathsBefore[i];
        if (!isIn(x.statics, x.s.dataNodes[d])) {
            if (now != was) r.kept = false;
            continue;
        }
        if (now != (was.find('/') == std::string::npos ? was : kConstantPrefix + was)) r.constant = false;
    }
    for (size_t q = 0; q < x.s.processors.size(); ++q, ++i)
        if (headerPath(x.g, x.s, x.s.processors[q]) != x.pathsBefore[i]) r.kept = false;
    return r;
}

void checkConverted(const ConstSn& x, ConstRun& run, std::set<std::string>& mine)
{
    Tally& c = run.c;
    ++c["1.0: Static processor subnetworks converted"];
    const bool a = ruleStaticHeaders(x, c);
    const bool b = ruleConstantPrefixes(x, mine);
    const CountsRule counts = ruleCounts(x);
    const bool d = rulePlainPathsGone(x, mine);
    const PathsRule paths = rulePaths(x);
    ++c[std::string("rule a) Static headers 1..CHC only: ") + yesNo(a)];
    ++c[std::string("rule b) constant headers = prefixes of Static paths: ") + yesNo(b)];
    ++c[std::string("rule c+counts) CLC / CHC / VHC / CDC / VDC: ") + yesNo(counts.ok)];
    ++c[std::string("rule d) plain Static paths gone from the variable headers: ") + yesNo(d)];
    ++c[std::string("other nodes / processors keep their paths: ") + yesNo(paths.kept)];
    ++c[std::string("Static nodes get Constant+path: ") + yesNo(paths.constant)];
    const bool rules = a && b && counts.ok, rest = d && paths.kept && paths.constant;
    const bool allOk = rules && rest;
    if (allOk) return;
    const size_t shown = run.badShown++;
    if (shown >= kShownBadSubnets) return;
    printf("bad %s sn %zu: a=%d b=%d counts=%d d=%d keep=%d cpath=%d (chc %d vhc %d cdc %d vdc %d clc %d/%d nh %zu)\n",
           x.file.c_str(), x.index, a, b, counts.ok, d, paths.kept, paths.constant, x.constantHeaders,
           x.variableHeaders, x.constantNodes, x.variableNodes, x.constantLayout, counts.layoutInts, counts.headers);
}

// The output of one 1.0 file, with its SubNetworks before const30.
struct ConstFile {
    const std::string& name;
    const oct::File& g;
    const ConstBefore& before;
    std::vector<SubNet> s;
    std::vector<std::set<std::string>> mine;   // per SubNetwork: its Static nodes' constant paths
};

// One SubNetwork of the output: skipped without processors or former Static nodes, else checked.
void checkConstSubNet(ConstFile& cf, size_t k, ConstRun& run)
{
    const SubNet& s = cf.s[k];
    bool anyStatic = false;
    for (bool x : cf.before.wasStatic[k]) anyStatic |= x;
    if (s.processors.empty() || !anyStatic) return;
    ConstSn x{cf.name, k, cf.g, s, cf.before.paths[k], cf.before.wasStatic[k],
              snCount(cf.g, s.at, "ConstantHeaderCount"), snCount(cf.g, s.at, "VariableHeaderCount"),
              snCount(cf.g, s.at, "ConstantDataNodeCount"), snCount(cf.g, s.at, "VariableDataNodeCount"),
              snCount(cf.g, s.at, "ConstantLayoutCount"), {}};
    if (x.constantHeaders <= 0) {
        if (const size_t n = ++run.c["1.0: Static processor subnetwork NOT converted"]; n <= kShownNotConverted)
            printf("not converted %s sn %zu\n", cf.name.c_str(), k);
        return;
    }
    x.statics = staticNodes(cf.g, s);
    checkConverted(x, run, cf.mine[k]);
}

// The 3.0 rule: a Transform's AltNodeName is 'Constant'+NodeName exactly when that header exists.
void tallyTransformAssociations(const ConstFile& cf, Tally& c)
{
    const oct::File& g = cf.g;
    long ap = g.pool("AssociationPool");
    for (size_t i = ap < 0 ? 0 : ap + 1, e = ap < 0 ? 0 : g.blockEnd(ap); i < e; i = g.blockEnd(i)) {
        if (childName(g, i, "Type") != "Transform") continue;
        int k = assocSubnet(g, i);
        if (k < 0 || k >= (int)cf.s.size()) continue;
        std::string node = childName(g, i, "NodeName"), alt = childName(g, i, "AltNodeName");
        std::string wantH = kConstantPrefix + node;
        std::replace(wantH.begin(), wantH.end(), '|', '/');
        const auto& paths = cf.s[k].paths;
        const bool exists = std::find(paths.begin(), paths.end(), wantH) != paths.end();
        const char* withHeader = alt == kConstantPrefix + node ? "AltNodeName as 3.0" : "AltNodeName MISSING";
        const char* without = alt.empty() ? "no Constant header, no AltNodeName" : "AltNodeName WITHOUT a header";
        const char* verdict = exists ? withHeader : without;
        ++c[std::string("Transform association: ") + verdict];
    }
}

// Twins: each Static node's constant path is a Static node path of the twin's same-index SubNetwork.
void tallyConstTwin(const std::vector<std::set<std::string>>& mine, const fs::path& p30, Tally& c)
{
    std::string b3 = slurp(p30);
    if (!isTuple(b3)) return;
    oct::File t = parse(b3);
    auto T = subnets(t);
    for (size_t k = 0; k < mine.size() && k < T.size(); ++k) {
        if (mine[k].empty()) continue;
        std::set<std::string> theirs;
        for (size_t d : staticNodes(t, T[k])) theirs.insert(headerPath(t, T[k], d));
        for (auto& m : mine[k])
            ++c[std::string("twin: Static node constant path in the 3.0 twin: ") + (theirs.count(m) ? "yes" : "no")];
    }
}

// A 1.0 choreography or exploder through the plugin's .oct steps, const30 and the counts, then every check.
void checkConstFile(const std::string& n, const fs::path& p, const Corpus& twins, ConstRun& run)
{
    std::string b = slurp(p);
    if (!isTuple(b) || !hasName(b, "ProcessorNodePool")) return;
    oct::File f = parse(b);
    const di1::OctSteps steps = pluginOctSteps(run.octx);
    const std::string ep = enginePath(p);
    std::string note;
    di1::octSteps(ep, f, steps, note);   // the plugin's steps before const30
    const ConstBefore before = beforeConst(f);
    tallyStaticAssociations(f, before, n, run.c);
    di1::octFinish(ep, f, steps, note);   // const30, then the counts
    const std::string w = f.write();
    oct::File g = parse(w);
    if (g.write() != w) ++run.c["1.0: re-parse DIFFERS"];
    Tally bad;
    visitStaticUses(g, [&](StaticUse u, const std::string& v) { tallyUnshipped(run.shipped, bad, u, v); });
    for (auto& [k, v] : bad) run.c["1.0 output: " + k] += v;
    ConstFile cf{n, g, before, subnets(g), {}};
    cf.mine.resize(cf.s.size());
    for (size_t k = 0; k < cf.s.size() && k < before.subnets.size(); ++k) checkConstSubNet(cf, k, run);
    tallyTransformAssociations(cf, run.c);
    auto twin = twins.find(n);
    if (twin != twins.end()) tallyConstTwin(cf.mine, twin->second, run.c);
}

// ---- --variants ----

struct VariantPools {
    std::vector<std::vector<int32_t>> groups;   // VariantGroupPool entries
    int groupCount = -1, memberCount = -1;      // VariantGroupCount, VariantMemberCount
};
using GroupPaths = std::set<std::set<std::string>>;   // a SubNetwork's groups by member header paths

struct VariantRun {
    Tally c;
    octx::Options octx;
};

VariantPools variantPools(const oct::File& f, size_t sn)
{
    VariantPools v;
    std::string pool;
    for (size_t j = sn + 1, e = f.blockEnd(sn); j < e; ++j) {
        const oct::Rec& r = f.recs[j];
        if (r.level == 3) {
            pool = f.name(r);
            if (pool == "VariantGroupCount") v.groupCount = cho::first(f, r, -1);
            else if (pool == "VariantMemberCount") v.memberCount = cho::first(f, r, -1);
        } else if (r.level == 4 && pool == "VariantGroupPool") v.groups.push_back(f.ints(r));
    }
    return v;
}

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// A *VariantProcessor's [Input, Output] DataNode refs, -1 where unbound.
std::vector<int32_t> inputOutput(const oct::File& f, size_t pn)
{
    long ar = child(f, pn, "DataNodeAttributes"), rr = child(f, pn, "DataNodeRefs");
    auto at = f.nameValues(f.recs[ar]);
    auto rf = f.ints(f.recs[rr]);
    std::vector<int32_t> io(2, -1);
    for (size_t q = 0; q < at.size() && q < rf.size(); ++q) {
        if (at[q] == "Input") io[0] = rf[q];
        else if (at[q] == "Output") io[1] = rf[q];
    }
    return io;
}

bool inSomeGroup(const VariantPools& v, const std::vector<int32_t>& io)
{
    for (auto& g : v.groups)
        if (std::count(g.begin(), g.end(), io[0]) && std::count(g.begin(), g.end(), io[1])) return true;
    return false;
}

void tallyVariantProcessors(const oct::File& f, const SubNet& s, const VariantPools& v, const char* tag, Tally& c)
{
    for (size_t pn : s.processors) {
        const std::string ty = childName(f, pn, "Type");
        if (!endsWith(ty, "VariantProcessor")) continue;
        const std::vector<int32_t> io = inputOutput(f, pn);
        const bool exact = std::find(v.groups.begin(), v.groups.end(), io) != v.groups.end();
        const char* where = exact ? " group == [Input, Output]"
                            : inSomeGroup(v, io) ? " in a larger/reversed group"
                                                 : " NO GROUP";
        ++c[std::string(tag) + ": " + ty + where];
    }
}

GroupPaths groupPaths(const oct::File& f, const SubNet& s, const VariantPools& v)
{
    GroupPaths out;
    for (auto& g : v.groups) {
        std::set<std::string> m;
        for (int32_t d : g) m.insert(d >= 0 && (size_t)d < s.dataNodes.size() ? headerPath(f, s, s.dataNodes[d]) : "?");
        out.insert(m);
    }
    return out;
}

// Per SubNetwork under tag: counts against the pool, a group for each *VariantProcessor. Returns the groups.
std::vector<GroupPaths> checkVariants(const oct::File& f, const char* tag, Tally& c)
{
    std::vector<GroupPaths> out;
    for (const SubNet& s : subnets(f)) {
        const VariantPools v = variantPools(f, s.at);
        if (v.groupCount >= 0) {
            size_t m = 0;
            for (auto& g : v.groups) m += g.size();
            const bool match = v.groupCount == (int)v.groups.size() && v.memberCount == (int)m;
            ++c[std::string(tag) + (match ? ": counts match the pool" : ": counts DIFFER from the pool")];
        }
        tallyVariantProcessors(f, s, v, tag, c);
        out.push_back(groupPaths(f, s, v));
    }
    return out;
}

// The plugin's .oct pipeline on f; true when it redirected skin influences or locators.
bool redirected(oct::File& f, const fs::path& p, const octx::Options& octx)
{
    std::string note;
    di1::transcodeOct(enginePath(p), f, pluginOctSteps(octx), note);
    return note.find("+skin(") != std::string::npos || note.find("+locators(") != std::string::npos;
}

void variantsCorpus10(const fs::path& p, VariantRun& run)
{
    std::string b = slurp(p);
    if (!isTuple(b)) return;
    oct::File f = parse(b);
    if (!redirected(f, p, run.octx)) return;
    ++run.c["1.0: files with redirected skin influences / locators"];
    const std::string w = f.write();
    oct::File g = parse(w);
    if (g.write() != w) ++run.c["1.0: re-parse DIFFERS"];
    checkVariants(g, "1.0 converted", run.c);
}

std::string quotedPaths(const std::set<std::string>& m)
{
    std::string s;
    for (auto& x : m) s += "'" + x + "' ";
    return s;
}

// Twins: each group made for the 1.0 file names the members of a group of the twin's same-index SubNetwork.
void variantsTwin(const std::string& n, const fs::path& p, oct::File& f, const oct::File& g, VariantRun& run)
{
    const auto before = checkVariants(f, "twin 1.0 raw", run.c);
    if (!redirected(f, p, run.octx)) return;
    const auto after = checkVariants(f, "twin 1.0 converted", run.c);
    const auto t30 = checkVariants(g, "twin 3.0", run.c);
    for (size_t k = 0; k < after.size(); ++k)
        for (auto& m : after[k]) {
            if (k < before.size() && before[k].count(m)) continue;   // a 1.0 group, not ours
            bool hit = k < t30.size() && t30[k].count(m);
            ++run.c[std::string("twin: group made for 1.0 ") +
                    (hit ? "equals a 3.0 twin group (by header paths)" : "not in the 3.0 twin")];
            if (!hit && run.c["twin: group made for 1.0 not in the 3.0 twin"] <= kShownVariantTwins)
                printf("twin %s sn %zu: %s\n", n.c_str(), k, quotedPaths(m).c_str());
        }
}

}  // namespace

int testChoConst(const Args& a)
{
    ConstRun run;
    run.octx = a.octx;
    const std::vector<Corpus> idx30 = shippedStaticUses(a.argv[3], run.shipped);
    run.c["3.0: Static processor input kinds"] = run.shipped.inputs.size();
    run.c["3.0: Static node shapes"] = run.shipped.shapes.size();
    run.c["3.0: BasisConversion refs to Static nodes"] = run.shipped.conversions;
    for (size_t s = 0; s < std::size(kChoSubs); ++s) {
        const Corpus A = choIndex(a.argv[2], kChoSubs[s]);
        for (auto& [n, p] : A) checkConstFile(n, p, idx30[s], run);
    }
    printTally(run.c, 7);
    bool bad = run.c.count("1.0: re-parse DIFFERS");
    for (auto& [k, v] : run.c) bad |= saysNo(k);
    return bad ? 1 : 0;
}

int testVariants(const Args& a)
{
    VariantRun run{{}, a.octx};
    if (a.argc > 4) run.octx.variantGroups = atoi(a.argv[4]) != 0;
    for (auto& p : choAndWorldFiles(a.argv[2])) variantsCorpus10(p, run);
    auto twin = [&run](const std::string& n, const fs::path& p, oct::File& f, oct::File& g) {
        variantsTwin(n, p, f, g, run);
    };
    for (const char* sub : kChoSubs) {
        const Corpus A = choIndex(a.argv[2], sub), B = choIndex(a.argv[3], sub);
        forEachTupleTwin(A, B, twin);
    }
    printTally(run.c, 7);
    bool bad = false;
    for (auto& [k, v] : run.c) {
        const bool converted = k.find("converted") != std::string::npos;
        bad |= k.find("NO GROUP") != std::string::npos && converted;
    }
    const bool reparse = run.c.count("1.0: re-parse DIFFERS") != 0;
    const bool counts = run.c.count("1.0 converted: counts DIFFER from the pool") != 0;
    return bad || reparse || counts ? 1 : 0;
}

}  // namespace octtest
