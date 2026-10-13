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

// Adds the count records to the SubNetwork whose level-2 record is at index sn. Returns false (file unchanged)
// if it already has counts or does not fit the rule (processors, constant data, an unsized DataNode type).
inline bool addCounts(oct::File& f, size_t sn, std::string* why = nullptr)
{
    size_t end = f.blockEnd(sn);
    long typeAt = -1, nameAt = -1, layoutAt = -1;
    int hsi = 0, bases = 0, roots = 0, bconv = 0, dn = 0, pn = 0, s32 = 0, s64 = 0;
    std::string pool;
    for (size_t i = sn + 1; i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            pool = n;
            if (n == "VariableHeaderCount") { if (why) *why = "has counts"; return false; }
            // 3.0-only form (6 shipped exploders): default data and base DataNode links, with their own size fields
            // between the counts. 1.0 never writes it.
            if (n == "DefaultData" || n == "BaseDataNodeLinksPool") { if (why) *why = "default data / base links"; return false; }
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
    int valsB[] = {bases - roots, bconv, 0, 0, 0, 0, 0, 0};
    for (int k = 0; k < 8; ++k) a.push_back(f.intScalar(3, COUNTS_B[k], valsB[k]));
    oct::Rec cl = f.intScalar(3, "ConstantLayoutCount", 0);

    // insert the later one first so the earlier index stays valid
    f.recs.insert(f.recs.begin() + layoutAt + 1, cl);
    size_t after = (size_t)(typeAt > nameAt ? typeAt : nameAt) + 1;
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
