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
    // every DataNode static (clips: 2,945/2,945 shipped 3.0 ones so): all headers constant, as 3.0 writes them
    bool allStatic = !exact && !pn && dn && statics == dn;
    // Otherwise the static DataNodes become variable ones. 3.0 keeps constant headers first and gives them the
    // constant nodes; 1.0 interleaves them, and with all headers variable an instanced SubNetwork relocates every
    // header's node pointer by the instance offset (FUN_004b4470), a constant (or heap) node included -> wild pointer
    // (in-game 2026-10-12, once the 1.0 mission graphs' .mer loaded and instanced them). Static data is never written,
    // so a per-instance copy reads the same.
    if (!allStatic && statics) {
        for (size_t i = sn + 1; i < f.blockEnd(sn); ++i)
            if (f.recs[i].level == 5 && f.name(f.recs[i]) == "Static" && f.ints(f.recs[i])[0]) f.setInt(f.recs[i], 0);
        statics = 0;
    }
    if (!exact) {   // ponytail: generous bounds (128 B/node + payload), fit per-type sizes if memory ever matters
        s32 = 128 * (dn - statics + pn) + dnBytes + 4096;
        s64 = 2 * s32;
    }

    std::vector<oct::Rec> a;
    int vals[] = {allStatic ? hsi : 0, allStatic ? 0 : hsi, statics, dn - statics, pn};
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
    for (size_t i = ap + 1; i < f.blockEnd(ap); i = f.blockEnd(i)) {
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

// SkinInfluence in 3.0 form. A 1.0 SkinInfluence association names a Transform/Dilation DataNode
// ('Anchor|Center|Transform|Center to WorldBasis'); the 3.0 exporter adds beside it a Matrix4x4 DataNode
// '<leaf>.Matrix4x4' (External, same basis, identity data) fed by a ConvertTransformToMatrix4x4 /
// ConvertDilationToMatrix4x4 processor (TransformToMatrix4x4VariantProcessor / DilationToMatrix4x4VariantProcessor,
// Input/Output, its header under the new node's) and names that node (tb_tron_disc, _proxyavatar). Without it
// 3.0 binds the influence through an 'AsXfrmTransMat' adapter header made at load outside the SubNetwork's header
// arena (FUN_004a5ee0 -> FUN_005077e0 -> FUN_0056afc0 'As%s'), which instancing cannot map: the clone reads an empty
// temp slot at 0x4cdabe (in-game 2026-10-13, 1.0 inc_delivery_box). New headers go at the end of the layout, the
// node and processor at the end of their pools, so no existing index moves. Returns the influences redirected.
inline int skin30(oct::File& f)
{
    long ap = f.pool("AssociationPool"), sp = f.pool("SubNetworkPool");
    if (ap < 0 || sp < 0) return 0;
    std::vector<size_t> sns;
    for (size_t i = sp + 1; i < f.blockEnd(sp); i = f.blockEnd(i))
        if (f.recs[i].level == 2) sns.push_back(i);
    // requested paths per SubNetwork
    std::map<int, std::vector<std::string>> want;
    auto each = [&](auto fn) {   // fn(NodeName record, SubNetworkRef) for every SkinInfluence
        long a = f.pool("AssociationPool");
        for (size_t i = a + 1; i < f.blockEnd(a); i = f.blockEnd(i)) {
            std::string type;
            long nn = -1;
            int sn = -1;
            for (size_t j = i + 1; j < f.blockEnd(i); ++j) {
                const std::string& n = f.name(f.recs[j]);
                if (n == "Type") type = f.nameValues(f.recs[j])[0];
                else if (n == "NodeName") nn = (long)j;
                else if (n == "SubNetworkRef") sn = f.ints(f.recs[j])[0];
            }
            if (type == "SkinInfluence" && nn >= 0 && sn >= 0 && sn < (int)sns.size()) fn(f.recs[nn], sn);
        }
    };
    each([&](oct::Rec& r, int sn) { want[sn].push_back(f.nameValues(r)[0]); });
    if (want.empty()) return 0;
    std::map<std::pair<int, std::string>, std::string> renamed;
    for (auto it = want.rbegin(); it != want.rend(); ++it) {   // later SubNetworks first: inserts keep earlier indices
        size_t sn = sns[it->first], end = f.blockEnd(sn);
        long hsAt = -1, hsiAt = -1, hlAt = -1, dnPool = -1, pnPool = -1;
        struct DN { int header, basis; std::string type; };
        std::vector<DN> dns;
        int npn = 0;
        std::string pool;
        for (size_t i = sn + 1; i < end; ++i) {
            const oct::Rec& r = f.recs[i];
            const std::string& n = f.name(r);
            if (r.level == 3) {
                pool = n;
                if (n == "HeaderStrings") hsAt = (long)i;
                else if (n == "HeaderStringIndices") hsiAt = (long)i;
                else if (n == "HeaderLayout") hlAt = (long)i;
                else if (n == "DataNodePool") dnPool = (long)i;
                else if (n == "ProcessorNodePool") pnPool = (long)i;
            } else if (r.level == 4 && pool == "DataNodePool" && n == "DataNode") dns.push_back({-1, 0, ""});
            else if (r.level == 4 && pool == "ProcessorNodePool" && n == "ProcessorNode") ++npn;
            else if (r.level == 5 && pool == "DataNodePool" && !dns.empty()) {
                if (n == "Header") dns.back().header = f.ints(r)[0];
                else if (n == "BasisRef") dns.back().basis = f.ints(r)[0];
                else if (n == "Type") dns.back().type = f.nameValues(r)[0];
            }
        }
        if (hsAt < 0 || hsiAt < 0 || hlAt < 0 || dnPool < 0) continue;
        std::vector<std::string> hs = f.nameValues(f.recs[hsAt]);
        std::vector<int32_t> hsi = f.ints(f.recs[hsiAt]), hl = f.ints(f.recs[hlAt]);
        std::vector<int32_t> parent(1, -1);   // header k (1-based; 0 = the SubNetwork root) -> parent
        for (size_t i = 0; i + 1 < hl.size(); i += 2)
            for (int c = 0; c < hl[i + 1]; ++c) parent.push_back(hl[i]);
        auto hname = [&](int h) -> std::string { return h >= 1 && h <= (int)hsi.size() && hsi[h - 1] < (int)hs.size() ? hs[hsi[h - 1]] : ""; };
        auto strIdx = [&](const std::string& s) {
            for (size_t i = 0; i < hs.size(); ++i) if (hs[i] == s) return (int32_t)i;
            hs.push_back(s);
            return (int32_t)(hs.size() - 1);
        };
        std::vector<oct::Rec> addDn, addPn;
        std::map<int, std::string> done;   // source header -> new leaf name
        for (const std::string& path : it->second) {
            int h = 0;
            size_t p = 0;
            while (h >= 0 && p <= path.size()) {   // walk the path's components down the header tree
                size_t q = path.find('|', p);
                std::string comp = path.substr(p, q == std::string::npos ? std::string::npos : q - p);
                int found = -1;
                for (int k = 1; k < (int)parent.size() && found < 0; ++k)
                    if (parent[k] == h && hname(k) == comp) found = k;
                h = found;
                if (q == std::string::npos) break;
                p = q + 1;
            }
            if (h <= 0) continue;
            int src = -1;
            for (size_t d = 0; d < dns.size(); ++d) if (dns[d].header == h) src = (int)d;
            if (src < 0) continue;
            const std::string& t = dns[src].type;
            if (t != "Transform" && t != "Dilation") continue;   // Matrix4x4 already, or a type 3.0 never converts
            std::string leaf = hname(h) + ".Matrix4x4";
            if (!done.count(h)) {
                int h1 = (int)parent.size(), h2 = h1 + 1;
                parent.push_back(parent[h]);
                parent.push_back(h1);
                hsi.push_back(strIdx(leaf));
                hsi.push_back(strIdx("Convert" + t + "ToMatrix4x4"));
                hl.insert(hl.end(), {parent[h], 1, h1, 1});
                int nd = (int)dns.size();
                dns.push_back({h1, dns[src].basis, "Matrix4x4"});
                addDn.push_back(keyedNode(f, 4, "DataNode", nd));
                addDn.push_back(f.intScalar(5, "Header", h1));
                addDn.push_back(f.nameScalar(5, f.addName("Type"), "Matrix4x4"));
                addDn.push_back(f.intScalar(5, "External", 1));
                addDn.push_back(f.intScalar(5, "BasisRef", dns[src].basis));
                addDn.push_back(f.node(5, "Data"));
                addDn.push_back(floatList(f, 6, "Matrix", {1, 0, 0, 0, 1, 0, 0, 0, 1}));
                addDn.push_back(floatList(f, 6, "Translation", {0, 0, 0}));
                addPn.push_back(keyedNode(f, 4, "ProcessorNode", npn++));
                addPn.push_back(f.intScalar(5, "Header", h2));
                addPn.push_back(f.nameScalar(5, f.addName("Type"), t + "ToMatrix4x4VariantProcessor"));
                addPn.push_back(f.intList(5, "DataNodeRefs", {src, nd}));
                addPn.push_back(nameList(f, 5, "DataNodeAttributes", {"Input", "Output"}));
                done[h] = leaf;
            }
            size_t cut = path.rfind('|');
            renamed[{it->first, path}] = (cut == std::string::npos ? std::string() : path.substr(0, cut + 1)) + done[h];
        }
        if (addDn.empty()) continue;
        f.setInts(f.recs[hlAt], hl);   // in place, before the inserts move anything
        f.setInts(f.recs[hsiAt], hsi);
        setNames(f, f.recs[hsAt], hs);
        // later records first
        if (pnPool > dnPool) f.recs.insert(f.recs.begin() + f.blockEnd(pnPool), addPn.begin(), addPn.end());
        size_t dnEnd = f.blockEnd(dnPool);
        if (pnPool < 0) addPn.insert(addPn.begin(), f.node(3, "ProcessorNodePool"));
        if (pnPool < 0) f.recs.insert(f.recs.begin() + dnEnd, addPn.begin(), addPn.end());
        f.recs.insert(f.recs.begin() + dnEnd, addDn.begin(), addDn.end());
        if (pnPool >= 0 && pnPool < dnPool) f.recs.insert(f.recs.begin() + f.blockEnd(pnPool), addPn.begin(), addPn.end());
    }
    int n = 0;
    each([&](oct::Rec& r, int sn) {
        auto k = renamed.find({sn, f.nameValues(r)[0]});
        if (k == renamed.end()) return;
        r.pay.clear();
        oct::File::put(r.pay, f.addName(k->second), 2);
        ++n;
    });
    return n;
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
