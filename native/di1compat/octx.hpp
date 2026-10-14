// .oct transforms that turn a 1.0 world/mission/exploder tuple file into the form the 3.0 reader takes, in memory.
// Each one is checked against the shipped 3.0 corpus by octtest.exe (strip the 3.0 records, regenerate, compare).
#pragma once
#include <cmath>
#include <map>
#include <set>
#include "oct.hpp"

namespace octx {

// SubNetwork counts (plan OCT-1). 3.0's SubNetwork reader FUN_00500ac0 sizes its basis / basis-conversion /
// DataNode arenas from 14 count fields + VariableAccumulatedSize32/64 (default 0 when absent); 1.0 SubNetworks
// have none, so the first DataNodeBasisConversion allocation returns NULL (in-game 2026-10-12: fault in
// FUN_004b5ce0 from FUN_004d0160). Rule for processor-free SubNetworks without constant data (every 1.0 one),
// exact on the shipped 3.0 ones (research verify-oct-world 2.2; octtest --counts here):
//   VariableHeaderCount = #HeaderStringIndices, VariableDataNodeCount = #DataNodes,
//   BasisCount = #Basis - #Root behaviours, BasisConversionCount = #BasisConversion, the other ten 0,
//   VAS32['1'] = [4, sum of per-type sizes, #DataNodes] (Dilation 32, Transform 64, Visibility/Bool/Float 4),
//   VAS64['1'] = [8, ..., #DataNodes] (Visibility/Bool/Float 8).
// Records go where 3.0 writes them: the counts after Type/Name, ConstantLayoutCount after HeaderLayout.
static const char* const COUNTS_A[] = {"ConstantHeaderCount", "VariableHeaderCount", "ConstantDataNodeCount",
                                       "VariableDataNodeCount", "ProcessorNodeCount"};
static const char* const COUNTS_B[] = {"BasisCount", "BasisConversionCount", "ComponentFamilyCount", "ComponentMemberCount",
                                       "VariantGroupCount", "VariantMemberCount", "LayerBindCount", "BaseDataNodeLinksCount"};

inline int typeSize(const std::string& t, bool is64)
{
    if (t == "Dilation") return 32;
    if (t == "Transform") return 64;
    if (t == "Visibility" || t == "Bool" || t == "Float") return is64 ? 8 : 4;
    return -1;
}

inline oct::Rec blobRec(oct::File& f, uint16_t level, const std::string& n, const std::string& data)
{
    int cw = data.size() < 0x100 ? 1 : data.size() < 0x10000 ? 2 : 3;
    oct::Rec r{level, (uint16_t)(oct::SCALAR | oct::BYTE << 3 | (cw - 1) << 6), f.addName(n), {}};
    oct::File::put(r.pay, (uint32_t)data.size(), cw);
    r.pay += data;
    return r;
}

// Base DataNode links (characters, props: 767 1.0 files; 15 shipped 3.0 exploders, one of them a 1.0 twin,
// inc_phonebooth). 1.0 stores per link 'DataNodeLinks' = local DataNode index per port (-1 = none) and names the link
// '<node>-<link>'; 3.0 stores the same map as three u16 blobs and the bare link name (3.0 names: no prefix, 6/6):
//   LocalUsedDataNodeIndices = the locals used, in port order; LocalToPortIndices = the port of each of those;
//   PortToLocalIndices = per port, the position of its local in LocalUsedDataNodeIndices (0xffff = none).
//   Rule on every shipped 3.0 choreography link (3,706/3,706 in 571 files, linkrule.py): PortToLocalIndices[p] = k
//   -> k < used count and LocalToPortIndices[k] == p. It was written as the raw locals, which coincides with the
//   positions only when the locals are 0..n-1 (37 of the 3,706; inc_phonebooth [-1,0,1,-1,-1] -> used [0,1]); for
//   every other 1.0 link 3.0 looked past the used list and the attachment wrote nothing (in-game 2026-10-14: all 96
//   external Visibility nodes of the IN_City choreographies at 0, INC_CIN_OpeningA's actors never shown).
// Returns the link's additional size: its three blobs' data + 4 (fits 284 / 22 / 140 on the 15 shipped).
inline int convertLink(oct::File& f, size_t link)
{
    size_t end = f.blockEnd(link);
    long dnl = -1;
    int extra = 4;
    for (size_t j = link + 1; j < end; ++j) {
        const std::string& n = f.name(f.recs[j]);
        if (n == "Name") {
            auto v = f.nameValues(f.recs[j]);
            size_t dash = v.empty() ? std::string::npos : v[0].find('-');
            if (dash != std::string::npos) {
                f.recs[j].pay.clear();
                oct::File::put(f.recs[j].pay, f.addName(v[0].substr(dash + 1)), 2);
            }
        } else if (n == "DataNodeLinks") dnl = (long)j;
        else if (n == "LocalUsedDataNodeIndices" || n == "LocalToPortIndices" || n == "PortToLocalIndices") {
            oct::Spec s = oct::spec(f.recs[j].type);
            extra += (int)f.recs[j].pay.size() - s.cw;   // already 3.0 form
        }
    }
    if (dnl < 0) return extra;
    std::vector<int32_t> ports = f.ints(f.recs[dnl]), used, toPort, toUsed;
    for (size_t p = 0; p < ports.size(); ++p) {
        int32_t at = -1;
        for (size_t k = 0; k < used.size() && at < 0; ++k) if (used[k] == ports[p]) at = (int32_t)k;
        if (ports[p] >= 0 && at < 0) { at = (int32_t)used.size(); used.push_back(ports[p]); toPort.push_back((int32_t)p); }
        toUsed.push_back(ports[p] >= 0 ? at : -1);
    }
    auto u16s = [](const std::vector<int32_t>& v) { std::string s; for (int32_t x : v) { uint16_t u = (uint16_t)x; s.append((const char*)&u, 2); } return s; };
    uint16_t lvl = f.recs[dnl].level;
    std::vector<oct::Rec> blobs = {blobRec(f, lvl, "LocalUsedDataNodeIndices", u16s(used)),
                                   blobRec(f, lvl, "LocalToPortIndices", u16s(toPort)),
                                   blobRec(f, lvl, "PortToLocalIndices", u16s(toUsed))};
    f.recs.erase(f.recs.begin() + dnl);
    f.recs.insert(f.recs.begin() + dnl, blobs.begin(), blobs.end());
    return 4 + 2 * (int)(used.size() + toPort.size() + toUsed.size());
}

// Adds the count records to the SubNetwork whose level-2 record is at index sn. Returns false (file unchanged)
// if it already has counts or lacks Type/Name/HeaderLayout.
// SubNetworks with default data and base links (characters, props) also get DefaultDataSize
// (floor4 of the DefaultData + DefaultDataIndices payloads, count bytes included: 1,516/1,518 shipped, the other 2 are
// 4 short, ddfit.py),
// BaseDataNodeLinksCount and BaseDataNodeLinksAdditionalSize, their links in 3.0 form, and
// ConnectionMapUsedDataNodes in its 3.0 place (after VariantMemberCount), as the 15 shipped ones.
//
// Graphs outside the exact rule (processors: 1.0 character/prop rigs; or a DataNode type without a known size) get
// the content counts, exact on the 6,143 shipped 3.0 processor SubNetworks (countfit.py): ProcessorNodeCount =
// #ProcessorNode, ComponentFamilyCount = #ComponentFamily, ComponentMemberCount = sum #ComponentNameIndices,
// VariantGroupCount = #VariantGroup, VariantMemberCount = sum of their member lists, LayerBindCount = #LayerBind,
// ConstantDataNodeCount = #Static only for all-static graphs (clips); elsewhere the static nodes become variable and
// headers stay all variable (ConstantHeaderCount and ConstantLayoutCount 0): 3.0 puts constant headers first
// (FUN_004fe4d0 builds headers 1..CHC from the first ConstantLayoutCount layout entries), 1.0 interleaves them.
// Arena sizes are upper bounds, not fits: FUN_00500ac0 sums [align, size, count] triples per capability key
// (FUN_004b4970: count*(align-1)+size) into the variable (+0x88) / constant (+0x84) arenas, which grow on demand
// (FUN_00412220) and DataNodes fall back to the heap (FUN_0050a720); only basis / conversion slots trap, and those
// counts are exact. 3.0 processor graphs need at most 64 B per node in total over all keys (vasfit.py).
// Slack added to every arena size the rules compute (0 = the shipped-exact values octtest checks). An instanced
// SubNetwork copies its arena block and relocates the pointers inside (FUN_004f5900, FUN_004b4470), so anything that
// spilled past a precomputed size (heap fallback, grown chunk) becomes a wild pointer; 1.0 graphs get slack.
inline int g_slack = 0;

inline bool addCounts(oct::File& f, size_t sn, std::string* why = nullptr)
{
    size_t end = f.blockEnd(sn);
    for (size_t i = sn + 1; i < end; ++i)
        if (f.recs[i].level == 3 && f.name(f.recs[i]) == "VariableHeaderCount") { if (why) *why = "has counts"; return false; }
    // a ConstantHeaderCount here comes from cho::const30, which put the Static nodes under constant headers 1..chc:
    // keep them Static and write the constant counts (3.0 form) instead of making them variable
    int chc = -1;
    for (size_t i = sn + 1; i < end; ++i)
        if (f.recs[i].level == 3 && f.name(f.recs[i]) == "ConstantHeaderCount") {
            chc = (int)f.ints(f.recs[i])[0];
            f.recs.erase(f.recs.begin() + i);
            break;
        }
    // links first: their additional size is measured on the 3.0 form
    int links = 0, linksExtra = 0;
    bool hasLinkPool = false;
    for (size_t i = sn + 1, e = f.blockEnd(sn); i < e; ++i) {
        if (f.recs[i].level == 3 && f.name(f.recs[i]) == "BaseDataNodeLinksPool") hasLinkPool = true;
        if (f.recs[i].level == 4 && f.name(f.recs[i]) == "BaseDataNodeLinks") {
            size_t n = f.recs.size();
            ++links;
            linksExtra += convertLink(f, i);
            e += f.recs.size() - n;   // convertLink replaces one record by three
        }
    }
    end = f.blockEnd(sn);
    long typeAt = -1, nameAt = -1, layoutAt = -1, cmudnAt = -1;
    int hsi = 0, hl = 0, bases = 0, roots = 0, bconv = 0, dn = 0, pn = 0, s32 = 0, s64 = 0, defaultData = -1, defaultIdx = 0;
    int statics = 0, dnBytes = 0, cfam = 0, cmem = 0, vgroups = 0, vmem = 0, lbinds = 0, lbBytes = 0;
    bool sized = true;
    std::string pool;
    for (size_t i = sn + 1; i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            pool = n;
            if (n == "DefaultData") defaultData = (int)r.pay.size();   // whole payloads, count included
            else if (n == "DefaultDataIndices") defaultIdx = (int)r.pay.size();
            else if (n == "ConnectionMapUsedDataNodes") cmudnAt = (long)i;
            if (n == "Type") typeAt = (long)i;
            else if (n == "Name") nameAt = (long)i;
            else if (n == "HeaderLayout") { layoutAt = (long)i; hl = (int)f.ints(r).size(); }
            else if (n == "HeaderStringIndices") hsi = (int)f.ints(r).size();
        } else if (r.level == 4) {
            if (pool == "BasisPool" && n == "Basis") ++bases;
            else if (pool == "BasisConversionPool" && n == "BasisConversion") ++bconv;
            else if (pool == "DataNodePool" && n == "DataNode") ++dn;
            else if (pool == "ProcessorNodePool" && n == "ProcessorNode") ++pn;
            else if (pool == "ComponentFamilyPool") ++cfam;
            else if (pool == "VariantGroupPool") { ++vgroups; vmem += (int)f.ints(r).size(); }
            else if (pool == "LayerBindPool") ++lbinds;
        } else {
            if (pool == "DataNodePool") dnBytes += (int)r.pay.size();
            else if (pool == "LayerBindPool") lbBytes += (int)r.pay.size();
            if (r.level != 5) continue;
            if (pool == "BasisPool" && n == "Behavior") {
                auto v = f.nameValues(r);
                if (!v.empty() && v[0] == "Root") ++roots;
            } else if (pool == "DataNodePool" && n == "Type") {
                auto v = f.nameValues(r);
                int a = v.empty() ? -1 : typeSize(v[0], false), b = v.empty() ? -1 : typeSize(v[0], true);
                if (a < 0) sized = false;
                s32 += a;
                s64 += b;
            } else if (pool == "DataNodePool" && n == "Static" && f.ints(r)[0]) ++statics;
            else if (pool == "ComponentFamilyPool" && n == "ComponentNameIndices") cmem += (int)f.ints(r).size();
        }
    }
    if (typeAt < 0 || layoutAt < 0) { if (why) *why = "no Type/HeaderLayout"; return false; }   // clips have no Name
    bool exact = !pn && sized;
    // every DataNode static (clips: 2,945/2,945 shipped 3.0 ones so): all headers constant, as 3.0 writes them
    bool allStatic = !exact && !pn && dn && statics == dn;
    // Otherwise the static DataNodes become variable ones. 3.0 keeps constant headers first and gives them the
    // constant nodes; 1.0 interleaves them, and with all headers variable an instanced SubNetwork relocates every
    // header's node pointer by the instance offset (FUN_004b4470), a constant (or heap) node included -> wild pointer
    // (in-game 2026-10-12, once the 1.0 mission graphs' .mer loaded and instanced them). Static data is never written,
    // so a per-instance copy reads the same.
    bool constSection = chc > 0 && !allStatic && statics;
    if (!allStatic && statics && !constSection) {
        for (size_t i = sn + 1, e = f.blockEnd(sn); i < e; ++i)
            if (f.recs[i].level == 5 && f.name(f.recs[i]) == "Static" && f.ints(f.recs[i])[0]) f.setInt(f.recs[i], 0);
        statics = 0;
    }
    int clc = 0;   // ConstantLayoutCount: the layout ints that number headers 1..chc
    if (constSection) {
        std::vector<int32_t> lay = f.ints(f.recs[layoutAt]);
        for (int got = 0; clc + 1 < (int)lay.size() && got < chc; clc += 2) got += lay[clc + 1];
    }
    if (!exact) {   // ponytail: generous bounds (128 B/node + payload), fit per-type sizes if memory ever matters
        s32 = 128 * (dn - statics + pn) + dnBytes + 4096;
        s64 = 2 * s32;
    }

    std::vector<oct::Rec> a;
    int vals[] = {allStatic ? hsi : constSection ? chc : 0, allStatic ? 0 : constSection ? hsi - chc : hsi, statics, dn - statics, pn};
    for (int k = 0; k < 5; ++k) a.push_back(f.intScalar(3, COUNTS_A[k], vals[k]));
    s32 += g_slack;
    s64 += 2 * g_slack;
    if (statics || g_slack) {
        int c32 = 128 * statics + (statics ? dnBytes : 0) + g_slack;
        a.push_back(f.node(3, "ConstantAccumulatedSize32"));
        a.push_back(f.intList(4, "1", {4, c32, statics}));
        a.push_back(f.node(3, "ConstantAccumulatedSize64"));
        a.push_back(f.intList(4, "1", {8, 2 * c32, statics}));
    }
    int nvar = exact ? dn : dn - statics + pn;
    if (!allStatic) {   // 3.0 writes none when nothing is variable
        a.push_back(f.node(3, "VariableAccumulatedSize32"));
        a.push_back(f.intList(4, "1", {4, s32, nvar}));
        a.push_back(f.node(3, "VariableAccumulatedSize64"));
        a.push_back(f.intList(4, "1", {8, s64, nvar}));
    }
    int valsB[] = {bases - roots, bconv, cfam, cmem, vgroups, vmem};
    for (int k = 0; k < 6; ++k) a.push_back(f.intScalar(3, COUNTS_B[k], valsB[k]));
    if (cmudnAt >= 0) a.push_back(f.recs[cmudnAt]);                     // moved into its 3.0 place
    if (defaultData >= 0) a.push_back(f.intScalar(3, "DefaultDataSize", ((defaultData + defaultIdx) & ~3) + g_slack / 16));
    a.push_back(f.intScalar(3, COUNTS_B[6], lbinds));                     // LayerBindCount
    if (lbinds) a.push_back(f.intScalar(3, "LayerBindAdditionalSize", lbBytes));
    a.push_back(f.intScalar(3, COUNTS_B[7], links));                      // BaseDataNodeLinksCount
    if (hasLinkPool) a.push_back(f.intScalar(3, "BaseDataNodeLinksAdditionalSize", linksExtra + g_slack / 16));
    oct::Rec cl = f.intScalar(3, "ConstantLayoutCount", allStatic ? hl : constSection ? clc : 0);

    // later positions first so the earlier indices stay valid
    f.recs.insert(f.recs.begin() + layoutAt + 1, cl);
    if (cmudnAt > layoutAt) f.recs.erase(f.recs.begin() + cmudnAt + 1);
    size_t after = (size_t)(typeAt > nameAt ? typeAt : nameAt) + 1;   // no Name (clips): after Type
    if (cmudnAt >= 0 && cmudnAt < layoutAt) {
        f.recs.erase(f.recs.begin() + cmudnAt);
        if (cmudnAt < (long)after) --after;
    }
    f.recs.insert(f.recs.begin() + after, a.begin(), a.end());
    return true;
}

// A 1.0 animation clip (SubNetwork Type Clip: one static ClipData node + a LayerBind) in 3.0 form, on 2,995/2,995
// shipped 3.0 clips: the ClipData node has External = 1 (after Static), and MinTime / MaxTime (after VariantGroupPool)
// equal the first two floats of its ClipDataBlock. The reader takes the clip's time range from them (FUN_00500ac0 ->
// FUN_004b0f70, default FLT_MAX..-FLT_MAX). Returns true if it changed anything.
inline bool clip30(oct::File& f, size_t sn)
{
    size_t end = f.blockEnd(sn);
    bool isClip = false, hasTime = false, changed = false;
    long vgEnd = -1, lbAt = -1;
    std::string block;
    for (size_t i = sn + 1; i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 3 && n == "Type") isClip = f.nameValues(r) == std::vector<std::string>{"Clip"};
        else if (r.level == 3 && (n == "MinTime" || n == "MaxTime")) hasTime = true;
        else if (r.level == 3 && n == "VariantGroupPool") vgEnd = (long)f.blockEnd(i);
        else if (r.level == 3 && n == "LayerBindPool") lbAt = (long)i;
        else if (r.level == 5 && n == "ClipDataBlock" && block.empty()) block = r.pay.substr(oct::spec(r.type).cw);
    }
    if (!isClip) return false;
    if (!hasTime && block.size() >= 8 && (vgEnd >= 0 || lbAt >= 0)) {
        auto fl = [&](const char* name, size_t off) {
            oct::Rec r{3, (uint16_t)(oct::SCALAR | oct::FLOAT << 3), f.addName(name), block.substr(off, 4)};
            return r;
        };
        std::vector<oct::Rec> t = {fl("MinTime", 0), fl("MaxTime", 4)};
        size_t at = (size_t)(vgEnd >= 0 ? vgEnd : lbAt);
        f.recs.insert(f.recs.begin() + at, t.begin(), t.end());
        changed = true;
    }
    for (size_t i = sn + 1; i < f.blockEnd(sn); ++i) {   // External = 1 on ClipData DataNodes
        if (f.recs[i].level != 4 || f.name(f.recs[i]) != "DataNode") continue;
        size_t e = f.blockEnd(i), put = 0;
        bool clipData = false, ext = false;
        for (size_t j = i + 1; j < e; ++j) {
            const std::string& n = f.name(f.recs[j]);
            if (n == "Type") { clipData = f.nameValues(f.recs[j]) == std::vector<std::string>{"ClipData"}; put = j + 1; }
            else if (n == "Static") put = j + 1;
            else if (n == "External") ext = true;
        }
        if (clipData && !ext && put) { f.recs.insert(f.recs.begin() + put, f.intScalar(5, "External", 1)); changed = true; }
    }
    return changed;
}

// AssetPool (3.0 only). 3.0 loads a world's or graph's .mer only when the .oct has assets: FUN_0077cb60 builds the
// '<name>.mer' path only if the AssetPool (FUN_0076c100) is non-empty and ExternalFilePool lists Meridian
// (FUN_0077ca30); 1.0 files list PrimaryAsset / Asset associations instead (in-game 2026-10-12: in_city.mer and the
// mission graphs' .mer were never requested). Asset[i] = Name ('Root' for the PrimaryAsset, else its NodeName),
// SubNetworkRef, SceneTreeNodeRef, AggregateCount 0 (never read by 3.0); PrimaryAsset first, as 196/196 1.0
// Incredibles .oct list it (research oct-world 2.3). Placed before AssociationPool, the shipped order.
inline bool addAssetPool(oct::File& f)
{
    if (f.pool("AssetPool") >= 0) return false;
    long ap = f.pool("AssociationPool");
    if (ap < 0) return false;
    struct A { std::string name; int32_t sn, stn; };
    std::vector<A> assets;
    for (size_t i = ap + 1, end = f.blockEnd(ap); i < end; i = f.blockEnd(i)) {
        if (f.recs[i].level != f.recs[ap].level + 1) continue;
        std::string type, node;
        int32_t sn = -1, stn = -1;
        for (size_t j = i + 1; j < f.blockEnd(i); ++j) {
            const std::string& n = f.name(f.recs[j]);
            if (n == "Type") type = f.nameValues(f.recs[j])[0];
            else if (n == "NodeName") node = f.nameValues(f.recs[j])[0];
            else if (n == "SubNetworkRef") sn = f.ints(f.recs[j])[0];
            else if (n == "SceneTreeNodeRef") stn = f.ints(f.recs[j])[0];
        }
        if (type == "PrimaryAsset") assets.insert(assets.begin(), A{"Root", sn, stn});
        else if (type == "Asset" && sn >= 0) assets.push_back(A{node, sn, stn});   // pieces without a SubNetwork: not
        // assets in 3.0 (the 1.0 inc_phonebooth's piece_1..7 with SubNetworkRef -1 are absent from its 3.0 twin;
        // instancing one hit a NULL root in FUN_004f5900, in-game 2026-10-12)
    }
    if (assets.empty()) return false;
    std::vector<oct::Rec> add = {f.node(1, "AssetPool")};
    for (size_t k = 0; k < assets.size(); ++k) {
        oct::Rec a{2, (uint16_t)(oct::NODE | 4), f.addName("Asset"), {}};   // keyed node: Asset = ['k']
        oct::File::put(a.pay, f.addName(std::to_string(k)), 2);
        add.push_back(a);
        add.push_back(f.nameScalar(3, f.addName("Name"), assets[k].name));
        add.push_back(f.intScalar(3, "SubNetworkRef", assets[k].sn));
        add.push_back(f.intScalar(3, "SceneTreeNodeRef", assets[k].stn));
        add.push_back(f.intScalar(3, "AggregateCount", 0));
    }
    f.recs.insert(f.recs.begin() + ap, add.begin(), add.end());
    return true;
}

// A name list record's values replaced (keeps its key).
inline void setNames(oct::File& f, oct::Rec& r, const std::vector<std::string>& v)
{
    oct::Spec s = oct::spec(r.type);
    int cw = s.cw < 1 ? 1 : s.cw;
    while (v.size() >= ((size_t)1 << (8 * cw))) ++cw;
    r.type = (uint16_t)(oct::LIST | (s.keyed << 2) | oct::NAME << 3 | (cw - 1) << 6);
    std::string pay = r.pay.substr(0, 2 * s.keyed);
    oct::File::put(pay, (uint32_t)v.size(), cw);
    for (auto& x : v) oct::File::put(pay, f.addName(x), 2);
    r.pay = pay;
}
inline oct::Rec nameList(oct::File& f, uint16_t level, const std::string& n, const std::vector<std::string>& v)
{
    oct::Rec r{level, (uint16_t)(oct::LIST | oct::NAME << 3), f.addName(n), {}};
    setNames(f, r, v);
    return r;
}
inline oct::Rec floatList(oct::File& f, uint16_t level, const std::string& n, const std::vector<float>& v)
{
    oct::Rec r{level, (uint16_t)(oct::LIST | oct::FLOAT << 3), f.addName(n), {}};
    oct::File::put(r.pay, (uint32_t)v.size(), 1);
    r.pay.append((const char*)v.data(), v.size() * 4);
    return r;
}
inline oct::Rec keyedNode(oct::File& f, uint16_t level, const std::string& n, int key)
{
    oct::Rec r{level, (uint16_t)(oct::NODE | 4), f.addName(n), {}};
    oct::File::put(r.pay, f.addName(std::to_string(key)), 2);
    return r;
}

// A variant processor's Input / Output pair as a VariantGroup (cfg variant10). Every *VariantProcessor of both shipped
// corpora has its pair in a VariantGroup of its SubNetwork (choreographies + exploders + motions: 1.0 324/324, 3.0
// 39,182/39,182; for the three kinds made here, DilationToTransform / DilationToMatrix4x4 / TransformToMatrix4x4,
// 3.0 lists it [Input, Output] in 38,981 of 39,071, variants.py); the reader (FUN_004be770 via FUN_004cf2a0) links the
// members (FUN_00614c10) and the DataNode dtor (FUN_005076f0) unlinks them. skin30 / worldTransform30 made such
// processors without one: the only ungrouped variant processors anywhere, and present in every 1.0 choreography with
// a locator (TS_WaterSplashdown among them, whose unload faulted in-game 2026-10-13).
inline bool g_variantGroups = true;

// One SubNetwork's header tree, DataNodes, processors and basis conversions, with appends that keep every existing
// index: new headers go at the end of the layout, nodes and processors at the end of their pools (the 3.0 exporter's
// constructs below are additions beside the 1.0 nodes, never rewrites of them).
struct SnEdit {
    oct::File& f;
    size_t sn;
    long hsAt = -1, hsiAt = -1, hlAt = -1, dnPool = -1, pnPool = -1;
    std::vector<oct::Rec> addVg;
    struct DN { int header = -1, basis = 0; std::string type; };
    std::vector<DN> dns;
    int npn = 0;
    std::vector<std::string> hs;
    std::vector<int32_t> hsi, hl, parent{-1};   // parent[k] of header k (1-based; 0 = the SubNetwork root)
    std::map<int, int> basisParent;
    std::map<std::pair<int, int>, int> conv;   // (from basis, to basis) -> converter DataNode
    std::vector<oct::Rec> addDn, addPn;

    SnEdit(oct::File& file, size_t snRec) : f(file), sn(snRec)
    {
        std::string pool;
        int basis = -1;
        std::map<std::string, int> c;
        for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
            const oct::Rec& r = f.recs[i];
            const std::string& n = f.name(r);
            if (r.level == 3) {
                pool = n;
                if (n == "HeaderStrings") hsAt = (long)i;
                else if (n == "HeaderStringIndices") hsiAt = (long)i;
                else if (n == "HeaderLayout") hlAt = (long)i;
                else if (n == "DataNodePool") dnPool = (long)i;
                else if (n == "ProcessorNodePool") pnPool = (long)i;
            } else if (r.level == 4) {
                if (pool == "DataNodePool" && n == "DataNode") dns.emplace_back();
                else if (pool == "ProcessorNodePool" && n == "ProcessorNode") ++npn;
                else if (pool == "BasisPool") basis = std::stoi(f.nameValues(r)[0]);
                else if (pool == "BasisConversionPool") c.clear();
            } else if (r.level == 5) {
                if (pool == "DataNodePool" && !dns.empty()) {
                    if (n == "Header") dns.back().header = f.ints(r)[0];
                    else if (n == "BasisRef") dns.back().basis = f.ints(r)[0];
                    else if (n == "Type") dns.back().type = f.nameValues(r)[0];
                } else if (pool == "BasisPool" && n == "ParentRef") basisParent[basis] = f.ints(r)[0];
                else if (pool == "BasisConversionPool") {
                    c[n] = f.ints(r)[0];
                    if (c.count("FromBasisRef") && c.count("ToBasisRef") && c.count("DataNodeRef"))
                        conv[{c["FromBasisRef"], c["ToBasisRef"]}] = c["DataNodeRef"];
                }
            }
        }
        if (!ok()) return;
        hs = f.nameValues(f.recs[hsAt]);
        hsi = f.ints(f.recs[hsiAt]);
        hl = f.ints(f.recs[hlAt]);
        for (size_t i = 0; i + 1 < hl.size(); i += 2)
            for (int k = 0; k < hl[i + 1]; ++k) parent.push_back(hl[i]);
        for (size_t k = 1; k < parent.size(); ++k) kids[parent[k]].push_back((int)k);
        for (size_t d = 0; d < dns.size(); ++d) dnAt[dns[d].header] = (int)d;
    }
    std::map<int, std::vector<int>> kids;
    std::map<int, int> dnAt;
    bool ok() const { return hsAt >= 0 && hsiAt >= 0 && hlAt >= 0 && dnPool >= 0; }
    const std::string& name(int h) const   // a reference: child() compares it for every sibling
    {
        static const std::string none;
        return h >= 1 && h <= (int)hsi.size() && hsi[h - 1] >= 0 && hsi[h - 1] < (int)hs.size() ? hs[hsi[h - 1]] : none;
    }
    int child(int h, const std::string& n) const
    {
        auto it = kids.find(h);
        if (it != kids.end())
            for (int k : it->second) if (name(k) == n) return k;
        return -1;
    }
    int find(const std::string& path) const   // 'A|B|C' from the root -> header, or -1
    {
        int h = 0;
        for (size_t p = 0;;) {
            size_t q = path.find('|', p);
            h = child(h, path.substr(p, q == std::string::npos ? std::string::npos : q - p));
            if (h < 0 || q == std::string::npos) return h;
            p = q + 1;
        }
    }
    int dnOf(int h) const { auto it = dnAt.find(h); return it == dnAt.end() ? -1 : it->second; }
    std::unordered_map<std::string, int> hsIndex;   // header string -> its first index (a linear scan per new header
                                                    // cost ~0.1 s on in_city's 1,251 locators)
    int header(const std::string& n, int par)
    {
        if (hsIndex.empty())
            for (size_t i = 0; i < hs.size(); ++i) hsIndex.emplace(hs[i], (int)i);
        auto it = hsIndex.find(n);
        int s = it != hsIndex.end() ? it->second : -1;
        if (s < 0) { hs.push_back(n); s = (int)hs.size() - 1; hsIndex.emplace(n, s); }
        hsi.push_back(s);
        hl.insert(hl.end(), {par, 1});
        parent.push_back(par);
        int h = (int)parent.size() - 1;
        kids[par].push_back(h);
        return h;
    }
    // A DataNode with identity data of its type (Dilation / Transform / Matrix4x4) -> its index.
    int node(int h, const std::string& type, int basis, bool external)
    {
        int nd = (int)dns.size();
        dns.push_back({h, basis, type});
        dnAt[h] = nd;
        addDn.push_back(keyedNode(f, 4, "DataNode", nd));
        addDn.push_back(f.intScalar(5, "Header", h));
        addDn.push_back(f.nameScalar(5, f.addName("Type"), type));
        if (external) addDn.push_back(f.intScalar(5, "External", 1));
        addDn.push_back(f.intScalar(5, "BasisRef", basis));
        addDn.push_back(f.node(5, "Data"));
        if (type == "Matrix4x4") {
            addDn.push_back(floatList(f, 6, "Matrix", {1, 0, 0, 0, 1, 0, 0, 0, 1}));
            addDn.push_back(floatList(f, 6, "Translation", {0, 0, 0}));
        } else {
            addDn.push_back(floatList(f, 6, "Translation", {0, 0, 0}));
            addDn.push_back(floatList(f, 6, "Orientation", {0, 0, 0, 1}));
            if (type == "Dilation") {
                float one = 1;
                addDn.push_back(oct::Rec{6, (uint16_t)(oct::SCALAR | oct::FLOAT << 3), f.addName("Scale"), std::string((const char*)&one, 4)});
            } else {
                addDn.push_back(floatList(f, 6, "Scale", {1, 1, 1, 1}));
                addDn.push_back(floatList(f, 6, "Unscale", {1, 1, 1, 1}));
            }
        }
        return nd;
    }
    void processor(int h, const std::string& type, const std::vector<int32_t>& refs, const std::vector<std::string>& attrs)
    {
        addPn.push_back(keyedNode(f, 4, "ProcessorNode", npn++));
        addPn.push_back(f.intScalar(5, "Header", h));
        addPn.push_back(f.nameScalar(5, f.addName("Type"), type));
        addPn.push_back(f.intList(5, "DataNodeRefs", refs));
        addPn.push_back(nameList(f, 5, "DataNodeAttributes", attrs));
        size_t tl = type.size(), vl = strlen("VariantProcessor");
        if (g_variantGroups && refs.size() == 2 && tl > vl && type.compare(tl - vl, vl, "VariantProcessor") == 0)
            addVg.push_back(f.intList(4, "VariantGroup", refs));   // refs = {Input, Output}
    }
    // Basis b to the root basis 0 as converter steps (converter DataNode, target basis): directly when a b -> 0
    // conversion exists, else through the parent basis. found = false when no chain exists.
    std::vector<std::pair<int, int>> toWorld(int b, bool& found) const
    {
        std::vector<std::pair<int, int>> steps;
        for (int guard = 0; b != 0 && guard < 64; ++guard) {
            auto d = conv.find({b, 0});
            if (d != conv.end()) { steps.push_back({d->second, 0}); b = 0; break; }
            auto p = basisParent.find(b);
            if (p == basisParent.end()) break;
            auto e = conv.find({b, p->second});
            if (e == conv.end()) break;
            steps.push_back({e->second, p->second});
            b = p->second;
        }
        found = b == 0;
        return steps;
    }
    void apply()
    {
        if (addDn.empty() && addPn.empty()) return;
        f.setInts(f.recs[hlAt], hl);   // in place, before the inserts move anything
        f.setInts(f.recs[hsiAt], hsi);
        setNames(f, f.recs[hsAt], hs);
        // later records first
        if (pnPool > dnPool) f.recs.insert(f.recs.begin() + f.blockEnd(pnPool), addPn.begin(), addPn.end());
        size_t dnEnd = f.blockEnd(dnPool);
        if (pnPool < 0 && !addPn.empty()) {
            addPn.insert(addPn.begin(), f.node(3, "ProcessorNodePool"));
            f.recs.insert(f.recs.begin() + dnEnd, addPn.begin(), addPn.end());
        }
        f.recs.insert(f.recs.begin() + dnEnd, addDn.begin(), addDn.end());
        if (pnPool >= 0 && pnPool < dnPool) f.recs.insert(f.recs.begin() + f.blockEnd(pnPool), addPn.begin(), addPn.end());
        if (addVg.empty()) return;
        // after the other inserts: find the pool again
        for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i)
            if (f.recs[i].level == 3 && f.name(f.recs[i]) == "VariantGroupPool") {
                f.recs.insert(f.recs.begin() + f.blockEnd(i), addVg.begin(), addVg.end());
                return;
            }
        // ponytail: a SubNetwork without a VariantGroupPool keeps its processors ungrouped (none seen in 1.0 files)
    }
};

// Edits children of a top-level pool (SubNetworks) one at a time, each in a File of its own -- the child's block at
// record 0, f's name table swapped in (not copied, so names are added in the same order) -- and writes the pool back
// once. Editing in place shifted every record after each insert: in_city has 283 SubNetworks among ~200k records, and
// each pass that inserted per SubNetwork (counts, locators, collision) cost ~0.25 s at load. kids = the children's
// record indices in file order; edit(g, k) runs for k in `order`.
template <class Edit> inline void editPoolChildren(oct::File& f, long pool, const std::vector<size_t>& kids, const std::vector<size_t>& order, Edit edit)
{
    if (order.empty()) return;
    std::vector<size_t> ends(kids.size());
    for (size_t k = 0; k < kids.size(); ++k) ends[k] = f.blockEnd(kids[k]);
    size_t poolEnd = f.blockEnd(pool);
    std::vector<std::vector<oct::Rec>> edited(kids.size());
    std::vector<char> done(kids.size(), 0);
    struct Names {   // f's name table lives in g while the edits run, and goes back even on an exception
        oct::File &f, &g;
        Names(oct::File& a, oct::File& b) : f(a), g(b) { swap(); }
        ~Names() { swap(); }
        void swap() { f.names.swap(g.names); f.nameIndex.swap(g.nameIndex); std::swap(f.nameIndexed, g.nameIndexed); }
    };
    oct::File g;
    {
        Names shared(f, g);
        for (size_t k : order) {
            g.recs.assign(std::make_move_iterator(f.recs.begin() + kids[k]), std::make_move_iterator(f.recs.begin() + ends[k]));
            edit(g, k);
            edited[k].swap(g.recs);
            done[k] = 1;
        }
    }
    std::vector<oct::Rec> out;
    size_t grown = 0;
    for (size_t k = 0; k < kids.size(); ++k) if (done[k]) grown += edited[k].size();
    out.reserve(f.recs.size() + grown);
    auto take = [&](size_t a, size_t b) { out.insert(out.end(), std::make_move_iterator(f.recs.begin() + a), std::make_move_iterator(f.recs.begin() + b)); };
    take(0, pool + 1);
    size_t at = pool + 1;
    for (size_t k = 0; k < kids.size(); ++k) {
        take(at, kids[k]);
        if (done[k]) out.insert(out.end(), std::make_move_iterator(edited[k].begin()), std::make_move_iterator(edited[k].end()));
        else take(kids[k], ends[k]);
        at = ends[k];
    }
    take(at, poolEnd);
    take(poolEnd, f.recs.size());
    f.recs.swap(out);
}

// Calls fn(NodeName record, SubNetworkRef) for every association of this Type.
template <class Fn> inline void eachAssociation(oct::File& f, const std::string& type, Fn fn)
{
    long a = f.pool("AssociationPool");
    if (a < 0) return;
    for (size_t i = a + 1, end = f.blockEnd(a); i < end; i = f.blockEnd(i)) {
        std::string t;
        long nn = -1;
        int sn = -1;
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
            const std::string& n = f.name(f.recs[j]);
            if (n == "Type") t = f.nameValues(f.recs[j])[0];
            else if (n == "NodeName") nn = (long)j;
            else if (n == "SubNetworkRef") sn = f.ints(f.recs[j])[0];
        }
        if (t == type && nn >= 0 && sn >= 0) fn(f.recs[nn], sn);
    }
}

// Rewrites the NodeName of each association of a Type that edit(SnEdit&, path) redirects (it returns the new path,
// or "" to leave it). SubNetworks are edited last to first so earlier record indices stay valid.
template <class Edit> inline int redirectAssociations(oct::File& f, const std::string& type, Edit edit)
{
    long sp = f.pool("SubNetworkPool");
    if (sp < 0) return 0;
    std::vector<size_t> sns;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; i = f.blockEnd(i))
        if (f.recs[i].level == 2) sns.push_back(i);
    std::map<int, std::vector<std::string>> want;
    eachAssociation(f, type, [&](oct::Rec& r, int sn) { if (sn < (int)sns.size()) want[sn].push_back(f.nameValues(r)[0]); });
    std::map<std::pair<int, std::string>, std::string> renamed;
    std::vector<size_t> order;   // last to first, the order the names were always added in
    for (auto it = want.rbegin(); it != want.rend(); ++it) order.push_back((size_t)it->first);
    editPoolChildren(f, sp, sns, order, [&](oct::File& g, size_t k) {
        SnEdit e(g, 0);
        if (!e.ok()) return;
        std::map<std::string, std::string> mine;
        for (const std::string& path : want[(int)k]) {
            if (mine.count(path)) continue;
            mine[path] = edit(e, path);
            if (!mine[path].empty()) renamed[{(int)k, path}] = mine[path];
        }
        e.apply();
    });
    int n = 0;
    eachAssociation(f, type, [&](oct::Rec& r, int sn) {
        auto k = renamed.find({sn, f.nameValues(r)[0]});
        if (k == renamed.end()) return;
        r.pay.clear();
        oct::File::put(r.pay, f.addName(k->second), 2);
        ++n;
    });
    return n;
}

inline std::string parentPath(const std::string& path) { size_t c = path.rfind('|'); return c == std::string::npos ? "" : path.substr(0, c + 1); }

// SkinInfluence in 3.0 form. A 1.0 SkinInfluence association names a Transform/Dilation DataNode
// ('Anchor|Center|Transform|Center to WorldBasis'); the 3.0 exporter adds beside it a Matrix4x4 DataNode
// '<leaf>.Matrix4x4' (External, same basis, identity data) fed by a ConvertTransformToMatrix4x4 /
// ConvertDilationToMatrix4x4 processor (TransformToMatrix4x4VariantProcessor / DilationToMatrix4x4VariantProcessor,
// Input/Output, its header under the new node's) and names that node (tb_tron_disc, _proxyavatar). Without it
// 3.0 binds the influence through an 'AsXfrmTransMat' adapter header made at load outside the SubNetwork's header
// arena (FUN_004a5ee0 -> FUN_005077e0 -> FUN_0056afc0 'As%s'), which instancing cannot map: the clone reads an empty
// temp slot at 0x4cdabe (in-game 2026-10-13, 1.0 inc_delivery_box). Returns the influences redirected.
inline int skin30(oct::File& f)
{
    return redirectAssociations(f, "SkinInfluence", [&](SnEdit& e, const std::string& path) -> std::string {
        int h = e.find(path), src = h > 0 ? e.dnOf(h) : -1;
        if (src < 0) return "";
        std::string t = e.dns[src].type, leaf = e.name(h) + ".Matrix4x4";
        if (t != "Transform" && t != "Dilation") return "";   // Matrix4x4 already, or a type 3.0 never converts
        int have = e.child(e.parent[h], leaf);
        if (have < 0) {
            int h1 = e.header(leaf, e.parent[h]), h2 = e.header("Convert" + t + "ToMatrix4x4", h1);
            int nd = e.node(h1, "Matrix4x4", e.dns[src].basis, true);
            e.processor(h2, t + "ToMatrix4x4VariantProcessor", {src, nd}, {"Input", "Output"});
        }
        return parentPath(path) + leaf;
    });
}

// MeridianLocator in 3.0 form. 3.0 binds a locator to a Transform DataNode in the root basis (FUN_006c1000 ->
// FUN_006b6830 takes the named header's node and checks its class): 'Layout|LOC_X|Transform|In WorldBasis.Transform',
// which the exporter derives from the locator's Dilation 'Transform' (basis b) with TransformDilationWithDilation
// [Input, Converter = the b -> root conversion node, Output 'In WorldBasis'] (header 'Convert To WorldBasis') and
// DilationToTransformVariantProcessor ['In WorldBasis' -> 'In WorldBasis.Transform'] (header
// 'ConvertDilationToTransform'); shipped values are identity, the processors compute them (emp_tatooine_details).
// 1.0 names the locator header itself ('IN_City_StartPoint': no node, its 'Transform' child is the Dilation), so 3.0
// bound nothing and the start lookup found no start (in-game 2026-10-13: PlayerStartPosNode built, its transform query
// failed every respawn). A basis without a direct root conversion goes through its parents' conversions ('In Basis<n>'
// per hop); a root-basis Dilation only gets the DilationToTransform step. Returns the locators redirected.
// The node a 1.0 binding path names, in 3.0 form: a graph header whose 'Transform' child is a Dilation becomes
// '<path>|Transform|In WorldBasis.Transform' with the exporter's chain (see locator30); "" if the path does not have
// that shape or already names a node.
// The exporter's steps from node src (header xf, basis b) to the root basis: per basis conversion an 'In <basis>' node
// of src's type written by 'Convert To <basis>' = Transform<src type>With<converter type> [Input, Converter, Output]
// (one step when b converts to the root directly); the last is 'In WorldBasis'. The processor names the converter's
// own type: the 3.0 corpus never feeds a converter of another type (concat.py; until 2026-10-14 this always wrote
// TransformDilationWithDilation, 153 of them over a Transform converter in the 827 1.0 files with cameras, which 3.0
// would fetch through a load-time 'As' adapter, the header kind instancing cannot map). Returns the last node, -1
// (nothing added) when there is no chain or a converter is not Transform / Dilation / Matrix4x4.
inline int worldChain(SnEdit& e, int xf, int src, bool lastExternal)
{
    bool found = false;
    auto steps = e.toWorld(e.dns[src].basis, found);
    if (!found) return -1;   // a root-basis src: no step, src itself
    for (auto& s : steps) {
        const std::string& ct = e.dns[s.first].type;
        if (ct != "Transform" && ct != "Dilation" && ct != "Matrix4x4") return -1;
    }
    std::string t = e.dns[src].type;
    int in = src;
    for (auto& s : steps) {
        std::string bn = s.second == 0 ? "WorldBasis" : "Basis" + std::to_string(s.second);
        int ho = e.header("In " + bn, xf), hp = e.header("Convert To " + bn, xf);
        int out = e.node(ho, t, s.second, lastExternal && s.second == 0);
        e.processor(hp, "Transform" + t + "With" + e.dns[s.first].type, {in, s.first, out}, {"Input", "Converter", "Output"});
        in = out;
    }
    return in;
}

inline std::string worldTransform30(SnEdit& e, const std::string& path)
{
    int loc = e.find(path);
    if (loc <= 0 || e.dnOf(loc) >= 0) return "";   // unresolved, or already names a node (3.0 form)
    int xf = e.child(loc, "Transform"), src = xf > 0 ? e.dnOf(xf) : -1;
    if (src >= 0 && e.dns[src].type == "Transform" && e.dns[src].basis == 0) return path + "|Transform";   // already one
    if (src < 0 || e.dns[src].type != "Dilation") return "";
    std::string to = path + "|Transform|In WorldBasis.Transform";
    if (e.child(xf, "In WorldBasis.Transform") >= 0) return to;
    int in = worldChain(e, xf, src, false);
    if (in < 0) return "";
    int ht = e.header("In WorldBasis.Transform", xf), hc = e.header("ConvertDilationToTransform", ht);
    int nt = e.node(ht, "Transform", 0, true);
    e.processor(hc, "DilationToTransformVariantProcessor", {in, nt}, {"Input", "Output"});
    return to;
}

inline int locator30(oct::File& f) { return redirectAssociations(f, "MeridianLocator", worldTransform30); }

// Camera in 3.0 form (cfg camworld10). The 3.0 Camera association binder (Association::Camera FUN_004a6c80 ->
// AnimationHelper::CameraAnimation vtable+0x24 = FUN_004a5d80) takes the camera header's 'Transform' node and, when
// that is not in the root basis, its world-basis twin through FUN_0051c980 -> FUN_0051c810: FUN_00507c20 looks for a
// child header of 'Transform' whose node is in the root basis and written by a converter processor; without one the
// binder builds the chain at load, on the template (a ConcatenateChain over the basis conversions, each fetched as
// the camera's type: a Dilation conversion under a Transform camera gets an 'AsTransform3d' adapter header outside
// the header arena, FUN_005077e0 -> FUN_0056afc0). Instancing then reads that header's empty temp slot (in-game
// 2026-10-14 03:06, INC_CIN_OpeningA: AV 004CDABE under FUN_004f5900 at variable header 72 =
// 'Layout|Placement_All|Transform', the 5 -> 1 conversion of camera1's chain Transform <- Transform <- Dilation <-
// Dilation; crash dump headers matched against the transcode).
// 3.0 ships the twin: 489 of 502 cameras outside the root basis have 'Transform/In WorldBasis' (root basis,
// External) written by 'Transform/Convert To WorldBasis' = Transform<camera type>With<converter type> [Input = the
// camera node, Converter, Output] (camworld.py); 1.0: 0 of 683. Made with worldChain (a direct conversion gives
// exactly the 3.0 shape; a longer chain stays within the processor signatures 3.0 ships: TransformTransformWithDilation
// 3,226, ...WithTransform 569, TransformDilationWithDilation 33,551, ...WithTransform 770; 3.0 never mixes types in a
// ConcatenateChain, concat.py). The association keeps naming the camera. Returns the cameras given one.
inline int camera30(oct::File& f)
{
    int n = 0;
    redirectAssociations(f, "Camera", [&](SnEdit& e, const std::string& path) -> std::string {
        int cam = e.find(path), xf = cam > 0 ? e.child(cam, "Transform") : -1, src = xf > 0 ? e.dnOf(xf) : -1;
        if (src < 0 || e.dns[src].basis == 0 || e.child(xf, "In WorldBasis") >= 0) return "";
        if (e.dns[src].type != "Transform" && e.dns[src].type != "Dilation") return "";
        if (worldChain(e, xf, src, true) >= 0) ++n;
        return "";
    });
    return n;
}

// Collision in 3.0 form, as every shipped instance (24,287 aggregates in the 3.0 worlds + exploders checked):
//   CollisionInstanceToDataNode names '<slot>|Transform|In WorldBasis.Transform' (1.0: the slot header, e.g.
//     'FountainCol' in its own SubNetwork) and carries AssetIndex = the AssetPool entry with its SubNetworkRef
//     (48,574/48,574);
//   right after it a CollisionAggregate with the same NodeName, CollisionInstanceRef, SubNetworkRef,
//     SceneTreeNodeRef = that of the Transform/Geometry association named by the path before '|Transform'
//     (else -1; 24,287/24,287) and AssetIndex;
//   each asset's AggregateCount = the aggregates with its index (5,998/5,998).
// 1.0 has neither the suffix, the index nor any aggregate (in_city: 947 instances, no collision in-game: the avatar
// fell through the start, 2026-10-13). Runs after addAssetPool. Returns the aggregates added.
inline int collision30(oct::File& f)
{
    redirectAssociations(f, "CollisionInstanceToDataNode", worldTransform30);
    long ap = f.pool("AssociationPool"), asp = f.pool("AssetPool");
    if (ap < 0 || asp < 0) return 0;
    std::map<int, int> assetOf;   // SubNetworkRef -> asset index
    std::vector<long> aggAt;      // per asset: its AggregateCount record
    for (size_t i = asp + 1, k = 0, end = f.blockEnd(asp); i < end; i = f.blockEnd(i), ++k) {
        aggAt.push_back(-1);
        for (size_t j = i + 1; j < f.blockEnd(i); ++j) {
            const std::string& n = f.name(f.recs[j]);
            if (n == "SubNetworkRef" && !assetOf.count(f.ints(f.recs[j])[0])) assetOf[f.ints(f.recs[j])[0]] = (int)k;
            else if (n == "AggregateCount") aggAt.back() = (long)j;
        }
    }
    struct A { size_t at, end; std::string type, node; int sn = -1, inst = -1, stn = -1; bool hasIndex = false; };
    std::vector<A> as;
    for (size_t i = ap + 1, end = f.blockEnd(ap); i < end; i = f.blockEnd(i)) {
        A a{i, f.blockEnd(i)};
        for (size_t j = i + 1; j < a.end; ++j) {
            const oct::Rec& r = f.recs[j];
            const std::string& n = f.name(r);
            if (n == "Type") a.type = f.nameValues(r)[0];
            else if (n == "NodeName") a.node = f.nameValues(r)[0];
            else if (n == "SubNetworkRef") a.sn = f.ints(r)[0];
            else if (n == "CollisionInstanceRef") a.inst = f.ints(r)[0];
            else if (n == "SceneTreeNodeRef") a.stn = f.ints(r)[0];
            else if (n == "AssetIndex") a.hasIndex = true;
        }
        if (a.type == "CollisionAggregate") return 0;   // already 3.0 form
        as.push_back(a);
    }
    std::map<std::pair<int, std::string>, int> scene;
    for (auto& a : as)
        if ((a.type == "Transform" || a.type == "Geometry") && a.stn != -1 && !scene.count({a.sn, a.node})) scene[{a.sn, a.node}] = a.stn;
    std::map<int, int> perAsset;
    int added = 0;
    // one forward rebuild of the pool (inserting per association into a ~200k-record file took over a second):
    // each association's block, then for a redirected DataNode link its AssetIndex and the aggregate; keys '0',
    // '1', ... in file order, as shipped
    std::vector<oct::Rec> pool;
    pool.reserve(f.blockEnd(ap) - ap + as.size() * 8);
    pool.push_back(f.recs[ap]);
    int key = 0;
    auto keyed = [&](oct::Rec r) {
        if (oct::spec(r.type).keyed) {
            std::string k2;
            oct::File::put(k2, f.addName(std::to_string(key++)), 2);
            r.pay = k2 + r.pay.substr(2);
        }
        return r;
    };
    for (const A& a : as) {
        pool.push_back(keyed(f.recs[a.at]));
        pool.insert(pool.end(), f.recs.begin() + a.at + 1, f.recs.begin() + a.end);
        if (a.type != "CollisionInstanceToDataNode" || a.node.find("|Transform") == std::string::npos) continue;   // redirected only
        auto ai = assetOf.find(a.sn);
        if (ai == assetOf.end()) continue;
        if (!a.hasIndex) pool.push_back(f.intScalar(3, "AssetIndex", ai->second));
        std::string slot = a.node.substr(0, a.node.find("|Transform"));
        auto sc = scene.find({a.sn, slot});
        pool.push_back(keyed(keyedNode(f, 2, "Association", 0)));
        pool.push_back(f.nameScalar(3, f.addName("NodeName"), a.node));
        pool.push_back(f.nameScalar(3, f.addName("Type"), "CollisionAggregate"));
        pool.push_back(f.intScalar(3, "CollisionInstanceRef", a.inst));
        pool.push_back(f.intScalar(3, "SubNetworkRef", a.sn));
        pool.push_back(f.intScalar(3, "SceneTreeNodeRef", sc == scene.end() ? -1 : sc->second));
        pool.push_back(f.intScalar(3, "AssetIndex", ai->second));
        ++perAsset[ai->second];
        ++added;
    }
    for (auto& kv : perAsset)   // AssetPool sits before AssociationPool (addAssetPool), so aggAt indices still hold
        if (kv.first < (int)aggAt.size() && aggAt[kv.first] >= 0) f.setInt(f.recs[aggAt[kv.first]], kv.second);
    size_t poolEnd = f.blockEnd(ap);
    std::vector<oct::Rec> out;
    out.reserve(f.recs.size() - (poolEnd - ap) + pool.size());
    out.insert(out.end(), std::make_move_iterator(f.recs.begin()), std::make_move_iterator(f.recs.begin() + ap));
    out.insert(out.end(), std::make_move_iterator(pool.begin()), std::make_move_iterator(pool.end()));
    out.insert(out.end(), std::make_move_iterator(f.recs.begin() + poolEnd), std::make_move_iterator(f.recs.end()));
    f.recs.swap(out);
    return added;
}

// World lighting in 3.0 form (in-world realms, as interiorlight_test / deathstar_throneroom14_1 ship them; light rule
// from realm30.py, measured on rlm_intro_day which ships in both games). 3.0 ignores the 1.0 RealmDataPool /
// RealmVolumePool and reads 1.0's string-typed lights as garbage, so in_city lit with the engine default (red/dark,
// in-game 2026-10-13). In place, no node index moves:
//   RealmDataPool entry -> its NodeRef node (a Transform) becomes Type RealmData with the entry's fields, NodeRef and
//     SkyNodeRef = itself, AddToZone 0 for a realm some volume names, else 1 (inside 0 / outside 1 in
//     interiorlight_test), RotateToGravity / RotateSkipLighting / BlendWithGravity 0;
//   RealmVolumePool entry -> its NodeRef node (the volume's Geometry) becomes Type RealmVolume: NumRealmNames 1,
//     RealmNames [realm], ZoneName, InsideSize, VolumeExtents (1.0's, mesh-local), VolumeExtentsNS = VolumeExtents x
//     the node's scale (pCube205), TypeName, OverrideType, BlendDistance, InRealmFile 0, BlendWithAvatarPos 0;
//   Light Directional -> LightType 6, LightData [r, g, b, intensity, 0 x6]; Ambient -> 7, [r, g, b, intensity];
//     FeatureCaps 0; the 1.0 named fields go;
//   Light Point -> LightType 0, Spot -> 1, LightData [r, g, b, I, FalloffRadius, FalloffStrength, FalloffClamp,
//     (Spot: ConeAngle, PenumbraAngle, Dropoff,) -1], FeatureCaps = Specular (fitted on the twin files below);
//   LightRig gains a Uuid after LightRefs; the two 1.0 pools go.
// Only worlds (a RealmData NodeRef that is a Transform node); realm files have their own rule (realm30.py).
inline int realm30w(oct::File& f)
{
    long np = f.pool("SceneTreeNodePool");
    if (np < 0) return 0;
    std::vector<size_t> nodes;
    for (size_t i = np + 1, end = f.blockEnd(np); i < end; i = f.blockEnd(i))
        if (f.recs[i].level == 2) nodes.push_back(i);
    auto entries = [&](const char* pool) {
        std::vector<std::vector<oct::Rec>> out;
        long p = f.pool(pool);
        if (p < 0) return out;
        for (size_t i = p + 1, end = f.blockEnd(p); i < end; i = f.blockEnd(i))
            out.emplace_back(f.recs.begin() + i + 1, f.recs.begin() + f.blockEnd(i));
        return out;
    };
    auto field = [&](const std::vector<oct::Rec>& e, const char* n) -> const oct::Rec* {
        for (auto& r : e) if (r.level == 3 && f.name(r) == n) return &r;
        return nullptr;
    };
    auto rds = entries("RealmDataPool"), rvs = entries("RealmVolumePool");
    std::map<size_t, std::vector<oct::Rec>> blocks;   // node index -> its new record block
    std::set<std::string> volumeRealms;
    for (auto& v : rvs) if (auto r = field(v, "RealmName")) volumeRealms.insert(f.nameValues(*r)[0]);
    auto nodeField = [&](size_t n, const char* name) -> const oct::Rec* {
        for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
            if (f.recs[j].level == 3 && f.name(f.recs[j]) == name) return &f.recs[j];
        return nullptr;
    };
    auto head = [&](size_t n, const char* type) {   // Node key, NodeName, Uuid, DisplayLayer, Type
        std::vector<oct::Rec> b = {f.recs[nodes[n]]};
        for (const char* k : {"NodeName", "Uuid", "DisplayLayer"}) if (auto r = nodeField(n, k)) b.push_back(*r);
        b.push_back(f.nameScalar(3, f.addName("Type"), type));
        return b;
    };
    auto tail = [&](size_t n, std::vector<oct::Rec>& b) {
        for (const char* k : {"ParentNodeReferences", "LocalToParentMatrix"}) if (auto r = nodeField(n, k)) b.push_back(*r);
    };
    auto typeOf = [&](size_t n) { auto r = nodeField(n, "Type"); return r ? f.nameValues(*r)[0] : std::string(); };
    int done = 0;
    bool worldRealms = false;
    for (auto& e : rds) {
        auto ref = field(e, "NodeRef");
        int n = ref ? f.ints(*ref)[0] : -1;
        if (n <= 0 || n >= (int)nodes.size() || typeOf(n) != "Transform") continue;
        worldRealms = true;
        auto b = head(n, "RealmData");
        tail(n, b);
        std::string name = field(e, "Name") ? f.nameValues(*field(e, "Name"))[0] : "";
        if (auto r = field(e, "Name")) b.push_back(*r);
        b.push_back(f.intScalar(3, "NodeRef", n));
        b.push_back(f.intScalar(3, "SkyNodeRef", n));
        for (const char* k : {"SunLightRef", "AmbientRigRef", "SunRigRef", "SceneSettingsRef", "SettingsName", "ZoneName", "RigBlendType",
                              "SettingsNonDefault", "SunNoDirection", "SunNoColor", "StartWeight", "FollowsCamera", "ParallaxFactor", "GlobalOverride"})
            if (auto r = field(e, k)) b.push_back(*r);
        b.push_back(f.intScalar(3, "AddToZone", volumeRealms.count(name) ? 0 : 1));
        for (const char* k : {"RotateToGravity", "RotateSkipLighting", "BlendWithGravity"}) b.push_back(f.intScalar(3, k, 0));
        blocks[n] = b;
        ++done;
    }
    if (!worldRealms && rds.size()) return 0;   // a realm file: realm30.py's rule, not this one
    for (auto& e : rvs) {
        auto ref = field(e, "NodeRef");
        int n = ref ? f.ints(*ref)[0] : -1;
        if (n <= 0 || n >= (int)nodes.size()) continue;
        auto b = head(n, "RealmVolume");
        b.push_back(f.intScalar(3, "NumRealmNames", 1));
        if (auto r = field(e, "RealmName")) b.push_back(nameList(f, 3, "RealmNames", {f.nameValues(*r)[0]}));
        for (const char* k : {"ZoneName", "InsideSize", "VolumeExtents"}) if (auto r = field(e, k)) b.push_back(*r);
        if (auto ve = field(e, "VolumeExtents"); ve && ve->pay.size() >= 13) {
            float x[3], m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            memcpy(x, ve->pay.data() + 1, 12);
            if (auto mr = nodeField(n, "LocalToParentMatrix"); mr && mr->pay.size() >= 65) memcpy(m, mr->pay.data() + 1, 64);
            std::vector<float> ns(3);
            for (int k = 0; k < 3; ++k) ns[k] = x[k] * sqrtf(m[4 * k] * m[4 * k] + m[4 * k + 1] * m[4 * k + 1] + m[4 * k + 2] * m[4 * k + 2]);
            b.push_back(floatList(f, 3, "VolumeExtentsNS", ns));
        }
        for (const char* k : {"TypeName", "OverrideType", "BlendDistance"}) if (auto r = field(e, k)) b.push_back(*r);
        b.push_back(f.intScalar(3, "InRealmFile", 0));
        b.push_back(f.intScalar(3, "BlendWithAvatarPos", 0));
        tail(n, b);
        blocks[n] = b;
        ++done;
    }
    for (size_t n = 0; n < nodes.size(); ++n) {   // lights
        if (blocks.count(n) || typeOf(n) != "Light") continue;
        auto lt = nodeField(n, "LightType");
        if (!lt || oct::spec(lt->type).kind != oct::NAME) continue;   // already 3.0 (int)
        std::string kind = f.nameValues(*lt)[0];
        auto flt = [&](const char* name, float d) {   // a float scalar field of the node
            auto r = nodeField(n, name);
            float v = d;
            if (r && r->pay.size() >= 4 && oct::spec(r->type).kind == oct::FLOAT) memcpy(&v, r->pay.data() + r->pay.size() - 4, 4);
            return v;
        };
        auto kv = [&](const char* name, float d) {    // a StaticKeyValuePairs entry ('FalloffRadius' = '60.0')
            for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
                if (f.recs[j].level == 4 && f.name(f.recs[j]) == name && oct::spec(f.recs[j].type).kind == oct::NAME) {
                    auto v = f.nameValues(f.recs[j]);
                    if (!v.empty()) { try { return std::stof(v.back()); } catch (...) {} }
                }
            return d;
        };
        auto kvTrue = [&](const char* name) {
            for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
                if (f.recs[j].level == 4 && f.name(f.recs[j]) == name && oct::spec(f.recs[j].type).kind == oct::NAME) {
                    auto v = f.nameValues(f.recs[j]);
                    return !v.empty() && (v.back() == "True" || v.back() == "true");
                }
            return false;
        };
        std::vector<float> data;
        int type = -1, caps = 0;
        float col[3] = {0, 0, 0};
        if (auto c = nodeField(n, "LightColor"); c && c->pay.size() >= 13) memcpy(col, c->pay.data() + 1, 12);
        float inten = flt("LightIntensity", 0.f);
        if (kind == "Directional" || kind == "Ambient") {
            type = kind == "Directional" ? 6 : 7;
            data.assign(type == 6 ? 10 : 4, 0.f);
            memcpy(data.data(), col, 12);
            data[3] = inten;
        } else if (kind == "Point" || kind == "Spot") {
            // twins in both games (31/31 static Point lights exact, choreographies differ only in animated keys;
            // Spot from lr_generalstore): [r, g, b, I, FalloffRadius (10), FalloffStrength (9), FalloffClamp (1),
            // Spot: ConeAngle, PenumbraAngle, Dropoff, then -1]; FeatureCaps = Specular (232 / 233)
            type = kind == "Point" ? 0 : 1;
            caps = kvTrue("Specular") ? 1 : 0;
            data = {col[0], col[1], col[2], inten, kv("FalloffRadius", 10.f), kv("FalloffStrength", 9.f), kv("FalloffClamp", 1.f)};
            if (type == 1) { data.push_back(flt("ConeAngle", 0.f)); data.push_back(flt("PenumbraAngle", 0.f)); data.push_back(flt("Dropoff", 0.f)); }
            data.push_back(-1.f);
        }
        if (type >= 0) {
            auto b = head(n, "Light");
            b.push_back(f.intScalar(3, "LightType", type));
            b.push_back(f.intScalar(3, "FeatureCaps", caps));
            b.push_back(floatList(f, 3, "LightData", data));
            if (auto x = nodeField(n, "ExclusionType")) b.push_back(*x);
            tail(n, b);
            blocks[n] = b;
        } else {   // a kind 3.0 has no twin rule for: a plain Transform
            auto b = head(n, "Transform");
            tail(n, b);
            blocks[n] = b;
        }
        ++done;
    }
    if (!done) return 0;
    // replace the node blocks in one pass over the records (an erase + insert per node shifted the whole file each time)
    {
        std::vector<oct::Rec> out;
        out.reserve(f.recs.size() + 16 * blocks.size());
        size_t at = 0;
        for (auto& [n, b] : blocks) {
            size_t a = nodes[n], e = f.blockEnd(a);
            out.insert(out.end(), std::make_move_iterator(f.recs.begin() + at), std::make_move_iterator(f.recs.begin() + a));
            out.insert(out.end(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()));
            at = e;
        }
        out.insert(out.end(), std::make_move_iterator(f.recs.begin() + at), std::make_move_iterator(f.recs.end()));
        f.recs.swap(out);
    }
    // rigs gain a Uuid after LightRefs (FNV-1a of the rig's ordinal: unique within the file, stable)
    if (long rp = f.pool("LightRigPool"); rp >= 0) {
        int k = 0;
        for (size_t i = rp + 1; i < f.blockEnd(rp); i = f.blockEnd(i), ++k) {
            bool has = false;
            size_t refs = 0;
            for (size_t j = i + 1; j < f.blockEnd(i); ++j) {
                if (f.name(f.recs[j]) == "Uuid") has = true;
                if (f.name(f.recs[j]) == "LightRefs") refs = j;
            }
            if (has || !refs) continue;
            std::string id(16, '\0');
            uint64_t h1 = 1469598103934665603ull, h2 = 0x9e3779b97f4a7c15ull;
            for (int q = 0; q < 4; ++q) { h1 = (h1 ^ (uint8_t)(k >> (8 * q))) * 1099511628211ull; h2 = (h2 ^ (uint8_t)(k >> (8 * q))) * 0x100000001b3ull + 0x52; }
            memcpy(&id[0], &h1, 8);
            memcpy(&id[8], &h2, 8);
            f.recs.insert(f.recs.begin() + refs + 1, blobRec(f, f.recs[refs].level, "Uuid", id));
        }
    }
    f.dropPool("RealmDataPool");
    f.dropPool("RealmVolumePool");
    return done;
}

// Every SubNetwork of the SubNetworkPool; returns how many got counts.
inline int addAllCounts(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    int done = 0;
    std::vector<size_t> kids, order;
    for (size_t i = pool + 1, end = f.blockEnd(pool); i < end; i = f.blockEnd(i)) {
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") order.push_back(kids.size());
        kids.push_back(i);
    }
    editPoolChildren(f, pool, kids, order, [&](oct::File& g, size_t) {
        clip30(g, 0);
        if (addCounts(g, 0)) ++done;
    });
    return done;
}

}  // namespace octx
