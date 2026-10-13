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
// if it already has counts or does not fit the rule (processors, constant data, an unsized DataNode type).
// SubNetworks with default data and base links (characters, props) also get DefaultDataSize (data + 4),
// BaseDataNodeLinksCount and BaseDataNodeLinksAdditionalSize, their links in 3.0 form, and
// ConnectionMapUsedDataNodes in its 3.0 place (after VariantMemberCount), as the 15 shipped ones.
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
    int hsi = 0, bases = 0, roots = 0, bconv = 0, dn = 0, pn = 0, s32 = 0, s64 = 0, defaultData = -1;
    std::string pool;
    for (size_t i = sn + 1; i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            pool = n;
            if (n == "DefaultData") defaultData = (int)r.pay.size() - oct::spec(r.type).cw;
            else if (n == "ConnectionMapUsedDataNodes") cmudnAt = (long)i;
            if (n == "Type") typeAt = (long)i;
            else if (n == "Name") nameAt = (long)i;
            else if (n == "HeaderLayout") layoutAt = (long)i;
            else if (n == "HeaderStringIndices") hsi = (int)f.ints(r).size();
        } else if (r.level == 4) {
            if (pool == "BasisPool" && n == "Basis") ++bases;
            else if (pool == "BasisConversionPool" && n == "BasisConversion") ++bconv;
            else if (pool == "DataNodePool" && n == "DataNode") ++dn;
            else if (pool == "ProcessorNodePool" && n == "ProcessorNode") ++pn;
        } else if (r.level == 5) {
            if (pool == "BasisPool" && n == "Behavior") {
                auto v = f.nameValues(r);
                if (!v.empty() && v[0] == "Root") ++roots;
            } else if (pool == "DataNodePool" && n == "Type") {
                auto v = f.nameValues(r);
                int a = v.empty() ? -1 : typeSize(v[0], false), b = v.empty() ? -1 : typeSize(v[0], true);
                if (a < 0) { if (why) *why = "unsized DataNode type " + (v.empty() ? std::string("?") : v[0]); return false; }
                s32 += a;
                s64 += b;
            }
        }
    }
    if (pn) { if (why) *why = "has processors"; return false; }
    if (typeAt < 0 || nameAt < 0 || layoutAt < 0) { if (why) *why = "no Type/Name/HeaderLayout"; return false; }

    std::vector<oct::Rec> a;
    int vals[] = {0, hsi, 0, dn, 0};
    for (int k = 0; k < 5; ++k) a.push_back(f.intScalar(3, COUNTS_A[k], vals[k]));
    a.push_back(f.node(3, "VariableAccumulatedSize32"));
    a.push_back(f.intList(4, "1", {4, s32, dn}));
    a.push_back(f.node(3, "VariableAccumulatedSize64"));
    a.push_back(f.intList(4, "1", {8, s64, dn}));
    int valsB[] = {bases - roots, bconv, 0, 0, 0, 0};
    for (int k = 0; k < 6; ++k) a.push_back(f.intScalar(3, COUNTS_B[k], valsB[k]));
    if (cmudnAt >= 0) a.push_back(f.recs[cmudnAt]);                     // moved into its 3.0 place
    if (defaultData >= 0) a.push_back(f.intScalar(3, "DefaultDataSize", defaultData + 4));
    a.push_back(f.intScalar(3, COUNTS_B[6], 0));                          // LayerBindCount
    a.push_back(f.intScalar(3, COUNTS_B[7], links));                      // BaseDataNodeLinksCount
    if (hasLinkPool) a.push_back(f.intScalar(3, "BaseDataNodeLinksAdditionalSize", linksExtra));
    oct::Rec cl = f.intScalar(3, "ConstantLayoutCount", 0);

    // later positions first so the earlier indices stay valid
    f.recs.insert(f.recs.begin() + layoutAt + 1, cl);
    if (cmudnAt > layoutAt) f.recs.erase(f.recs.begin() + cmudnAt + 1);
    size_t after = (size_t)(typeAt > nameAt ? typeAt : nameAt) + 1;
    if (cmudnAt >= 0 && cmudnAt < layoutAt) {
        f.recs.erase(f.recs.begin() + cmudnAt);
        if (cmudnAt < (long)after) --after;
    }
    f.recs.insert(f.recs.begin() + after, a.begin(), a.end());
    return true;
}

// Every SubNetwork of the SubNetworkPool; returns how many got counts.
inline int addAllCounts(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    int done = 0;
    for (size_t i = pool + 1; i < f.blockEnd(pool);) {
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork" && addCounts(f, i)) ++done;
        i = f.blockEnd(i);   // the next sibling (recomputed after the insert)
    }
    return done;
}

}  // namespace octx
