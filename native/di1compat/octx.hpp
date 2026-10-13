// .oct transforms that turn a 1.0 world/mission/exploder tuple file into the form the 3.0 reader takes, in memory.
// Each one is checked against the shipped 3.0 corpus by octtest.exe (strip the 3.0 records, regenerate, compare).
#pragma once
#include <map>
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
//   PortToLocalIndices = DataNodeLinks; LocalUsedDataNodeIndices = the locals used, in port order;
//   LocalToPortIndices = the port of each of those. (inc_phonebooth [-1,0,1,-1,-1] -> used [0,1], ports [1,2].)
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
    std::vector<int32_t> ports = f.ints(f.recs[dnl]), used, toPort;
    for (size_t p = 0; p < ports.size(); ++p) {
        bool seen = false;
        for (int32_t u : used) seen |= u == ports[p];
        if (ports[p] >= 0 && !seen) { used.push_back(ports[p]); toPort.push_back((int32_t)p); }
    }
    auto u16s = [](const std::vector<int32_t>& v) { std::string s; for (int32_t x : v) { uint16_t u = (uint16_t)x; s.append((const char*)&u, 2); } return s; };
    uint16_t lvl = f.recs[dnl].level;
    std::vector<oct::Rec> blobs = {blobRec(f, lvl, "LocalUsedDataNodeIndices", u16s(used)),
                                   blobRec(f, lvl, "LocalToPortIndices", u16s(toPort)),
                                   blobRec(f, lvl, "PortToLocalIndices", u16s(ports))};
    f.recs.erase(f.recs.begin() + dnl);
    f.recs.insert(f.recs.begin() + dnl, blobs.begin(), blobs.end());
    return 4 + 2 * (int)(used.size() + toPort.size() + ports.size());
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
// ConstantDataNodeCount = #Static (3.0 differs on 7 static ClothData). Headers stay all variable (ConstantHeaderCount
// and ConstantLayoutCount 0): 3.0 puts constant headers first (FUN_004fe4d0 builds headers 1..CHC from the first
// ConstantLayoutCount layout entries), 1.0 interleaves them, and the header tables do not depend on Static.
// Arena sizes are upper bounds, not fits: FUN_00500ac0 sums [align, size, count] triples per capability key
// (FUN_004b4970: count*(align-1)+size) into the variable (+0x88) / constant (+0x84) arenas, which grow on demand
// (FUN_00412220) and DataNodes fall back to the heap (FUN_0050a720); only basis / conversion slots trap, and those
// counts are exact. 3.0 processor graphs need at most 64 B per node in total over all keys (vasfit.py).
inline bool addCounts(oct::File& f, size_t sn, std::string* why = nullptr)
{
    size_t end = f.blockEnd(sn);
    for (size_t i = sn + 1; i < end; ++i)
        if (f.recs[i].level == 3 && f.name(f.recs[i]) == "VariableHeaderCount") { if (why) *why = "has counts"; return false; }
    // links first: their additional size is measured on the 3.0 form
    int links = 0, linksExtra = 0;
    bool hasLinkPool = false;
    for (size_t i = sn + 1; i < f.blockEnd(sn); ++i) {
        if (f.recs[i].level == 3 && f.name(f.recs[i]) == "BaseDataNodeLinksPool") hasLinkPool = true;
        if (f.recs[i].level == 4 && f.name(f.recs[i]) == "BaseDataNodeLinks") { ++links; linksExtra += convertLink(f, i); }
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
    if (!exact) {   // ponytail: generous bounds (128 B/node + payload), fit per-type sizes if memory ever matters
        statics = statics < dn ? statics : dn;
        s32 = 128 * (dn - statics + pn) + dnBytes + 4096;
        s64 = 2 * s32;
    } else statics = 0;   // the exact rule counts static DataNodes as variable (in_city root, tested in-game)
    // every DataNode static (clips: 2,945/2,945 shipped 3.0 ones so): all headers constant, as 3.0 writes them
    bool allStatic = !exact && !pn && dn && statics == dn;

    std::vector<oct::Rec> a;
    int vals[] = {allStatic ? hsi : 0, allStatic ? 0 : hsi, statics, dn - statics, pn};
    for (int k = 0; k < 5; ++k) a.push_back(f.intScalar(3, COUNTS_A[k], vals[k]));
    if (statics) {
        int c32 = 128 * statics + dnBytes;
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
    if (defaultData >= 0) a.push_back(f.intScalar(3, "DefaultDataSize", (defaultData + defaultIdx) & ~3));
    a.push_back(f.intScalar(3, COUNTS_B[6], lbinds));                     // LayerBindCount
    if (lbinds) a.push_back(f.intScalar(3, "LayerBindAdditionalSize", lbBytes));
    a.push_back(f.intScalar(3, COUNTS_B[7], links));                      // BaseDataNodeLinksCount
    if (hasLinkPool) a.push_back(f.intScalar(3, "BaseDataNodeLinksAdditionalSize", linksExtra));
    oct::Rec cl = f.intScalar(3, "ConstantLayoutCount", allStatic ? hl : 0);

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

// Every SubNetwork of the SubNetworkPool; returns how many got counts.
inline int addAllCounts(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    int done = 0;
    for (size_t i = pool + 1; i < f.blockEnd(pool);) {
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") {
            clip30(f, i);
            if (addCounts(f, i)) ++done;
        }
        i = f.blockEnd(i);   // the next sibling (recomputed after the insert)
    }
    return done;
}

}  // namespace octx
