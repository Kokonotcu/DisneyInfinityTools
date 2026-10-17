// The SubNetwork count records 3.0's reader sizes its arenas from (FUN_00500ac0), and clip time ranges. Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "../base/bytes.hpp"
#include "../formats/oct.hpp"
#include "records.hpp"

namespace octx {

// The scalar counts, in 3.0's order: A after Type/Name, then the arena sizes, then B.
inline constexpr const char* kCountsA[] = {"ConstantHeaderCount", "VariableHeaderCount", "ConstantDataNodeCount",
                                           "VariableDataNodeCount", "ProcessorNodeCount"};
inline constexpr const char* kCountsB[] = {"BasisCount", "BasisConversionCount", "ComponentFamilyCount",
                                           "ComponentMemberCount", "VariantGroupCount", "VariantMemberCount",
                                           "LayerBindCount", "BaseDataNodeLinksCount"};

// Arena slack (CO-01): an instance copies only the sized block (FUN_004f5900), so a spill becomes a wild pointer.
inline int g_slack = 0;

namespace detail {

inline constexpr int kLinkOverhead = 4;   // a base link's additional size: its blobs' data + 4 (15 shipped files)
inline constexpr int kNodeBound = 128;    // bytes per node of an inexact arena; 3.0 graphs need <= 64 (vasfit.py)
inline constexpr int kInexactPad = 4096;  // added to an inexact variable arena
inline constexpr int kSlackShare = 16;    // DefaultDataSize and the links' size get slack / 16

// A DataNode type's arena bytes, 32- and 64-bit; -1 when unknown.
struct ArenaSize { int size32, size64; };
inline ArenaSize typeSize(const std::string& t)
{
    if (t == "Dilation") return {32, 32};
    if (t == "Transform") return {64, 64};
    if (t == "Visibility" || t == "Bool" || t == "Float") return {4, 8};
    return {-1, -1};
}

// A link's local DataNode per port (-1 none) as 3.0's three index lists.
struct LinkMaps { std::vector<int32_t> used, toPort, toUsed; };
inline LinkMaps linkMaps(const std::vector<int32_t>& ports)
{
    LinkMaps m;
    for (size_t p = 0; p < ports.size(); ++p) {
        auto it = std::find(m.used.begin(), m.used.end(), ports[p]);
        const int32_t at = (int32_t)(it - m.used.begin());
        if (ports[p] >= 0 && it == m.used.end()) {
            m.used.push_back(ports[p]);
            m.toPort.push_back((int32_t)p);
        }
        m.toUsed.push_back(ports[p] >= 0 ? at : -1);
    }
    return m;
}

inline std::string u16Blob(const std::vector<int32_t>& v)
{
    std::string s;
    for (int32_t x : v) bytes::appendLe(s, (uint16_t)x, 2);
    return s;
}

// A link Name '<node>-<link>' becomes '<link>' (3.0 names: no prefix, 6/6).
inline void dropLinkPrefix(oct::File& f, oct::Rec& r)
{
    std::vector<std::string> v = f.nameValues(r);
    const size_t dash = v.empty() ? std::string::npos : v[0].find('-');
    if (dash == std::string::npos) return;
    r.pay.clear();
    oct::File::put(r.pay, f.addName(v[0].substr(dash + 1)), 2);
}

inline bool isLinkBlob(const std::string& n)
{
    return n == "LocalUsedDataNodeIndices" || n == "LocalToPortIndices" || n == "PortToLocalIndices";
}
}  // namespace detail

// The base link at record link in 3.0 form: bare name, DataNodeLinks as three u16 blobs. -> its additional size.
inline int convertLink(oct::File& f, size_t link)
{
    const size_t end = f.blockEnd(link);
    long dnl = -1;
    int extra = detail::kLinkOverhead;
    for (size_t j = link + 1; j < end; ++j) {
        const std::string& n = f.name(f.recs[j]);
        if (n == "Name") detail::dropLinkPrefix(f, f.recs[j]);
        else if (n == "DataNodeLinks") dnl = (long)j;
        else if (detail::isLinkBlob(n)) extra += (int)f.recs[j].pay.size() - oct::spec(f.recs[j].type).cw;
    }
    if (dnl < 0) return extra;   // already 3.0 form
    // PortToLocalIndices[p]: the position of p's local in the used list (3,706/3,706 shipped links, linkrule.py)
    const detail::LinkMaps m = detail::linkMaps(f.ints(f.recs[dnl]));
    const uint16_t lvl = f.recs[dnl].level;
    std::vector<oct::Rec> blobs = {blobRec(f, lvl, "LocalUsedDataNodeIndices", detail::u16Blob(m.used)),
                                   blobRec(f, lvl, "LocalToPortIndices", detail::u16Blob(m.toPort)),
                                   blobRec(f, lvl, "PortToLocalIndices", detail::u16Blob(m.toUsed))};
    f.recs.erase(f.recs.begin() + dnl);
    f.recs.insert(f.recs.begin() + dnl, blobs.begin(), blobs.end());
    return detail::kLinkOverhead + 2 * (int)(m.used.size() + m.toPort.size() + m.toUsed.size());
}

namespace detail {

// What addCounts reads from one SubNetwork (CO-04).
struct SnStats {
    long typeAt = -1, nameAt = -1, layoutAt = -1;   // level-3 Type, Name, HeaderLayout
    long connectionMapAt = -1;                      // ConnectionMapUsedDataNodes
    int headerStrings = 0, layoutInts = 0;          // HeaderStringIndices and HeaderLayout element counts
    int bases = 0, roots = 0, conversions = 0;      // Basis entries, their Root behaviours, BasisConversions
    int dataNodes = 0, processors = 0, statics = 0;
    int size32 = 0, size64 = 0;                     // the DataNodes' typeSize sums
    bool sized = true;                              // every DataNode type has a typeSize
    int defaultData = -1, defaultIndices = 0;       // payload bytes, count included
    int dataNodeBytes = 0, layerBindBytes = 0;      // payloads below level 4 of those pools
    int families = 0, familyMembers = 0, variantGroups = 0, variantMembers = 0, layerBinds = 0;
};

// The records scan() tallies.
enum class Tallied {
    DefaultData, DefaultIndices, ConnectionMap, Type, Name, Layout, HeaderStrings,
    Basis, Conversion, DataNode, Processor, Family, VariantGroup, LayerBind,
    Behaviour, NodeType, Static, FamilyNames
};
// By level, enclosing level-3 record and name (nullptr: any). A level-3 record encloses itself. Rows never overlap.
struct TallyRow { int level; const char* pool; const char* name; Tallied what; };
inline constexpr TallyRow kTallyRows[] = {
    {3, "DefaultData", nullptr, Tallied::DefaultData},
    {3, "DefaultDataIndices", nullptr, Tallied::DefaultIndices},
    {3, "ConnectionMapUsedDataNodes", nullptr, Tallied::ConnectionMap},
    {3, "Type", nullptr, Tallied::Type},
    {3, "Name", nullptr, Tallied::Name},
    {3, "HeaderLayout", nullptr, Tallied::Layout},
    {3, "HeaderStringIndices", nullptr, Tallied::HeaderStrings},
    {4, "BasisPool", "Basis", Tallied::Basis},
    {4, "BasisConversionPool", "BasisConversion", Tallied::Conversion},
    {4, "DataNodePool", "DataNode", Tallied::DataNode},
    {4, "ProcessorNodePool", "ProcessorNode", Tallied::Processor},
    {4, "ComponentFamilyPool", nullptr, Tallied::Family},
    {4, "VariantGroupPool", nullptr, Tallied::VariantGroup},
    {4, "LayerBindPool", nullptr, Tallied::LayerBind},
    {5, "BasisPool", "Behavior", Tallied::Behaviour},
    {5, "DataNodePool", "Type", Tallied::NodeType},
    {5, "DataNodePool", "Static", Tallied::Static},
    {5, "ComponentFamilyPool", "ComponentNameIndices", Tallied::FamilyNames},
};

// The level-3 row for a record named n, or null.
inline const TallyRow* fieldRow(const std::string& n)
{
    for (const TallyRow& row : kTallyRows)
        if (row.level == 3 && n == row.pool) return &row;
    return nullptr;
}

// The rows of pool at level 4 and deeper, looked up once per level-3 record.
inline std::vector<const TallyRow*> poolRows(const std::string& pool)
{
    std::vector<const TallyRow*> rows;
    for (const TallyRow& row : kTallyRows)
        if (row.level > 3 && pool == row.pool) rows.push_back(&row);
    return rows;
}

// The row among rows for a record at this level with this name, or null.
inline const TallyRow* tallyRow(const std::vector<const TallyRow*>& rows, int level, const std::string& name)
{
    for (const TallyRow* row : rows) {
        if (row->level != level) continue;
        if (!row->name || name == row->name) return row;
    }
    return nullptr;
}

// A level-3 record's tally; at is its index.
inline void tallyField(SnStats& s, Tallied what, const oct::File& f, const oct::Rec& r, long at)
{
    switch (what) {
    case Tallied::DefaultData:
        s.defaultData = (int)r.pay.size();
        break;
    case Tallied::DefaultIndices:
        s.defaultIndices = (int)r.pay.size();
        break;
    case Tallied::ConnectionMap:
        s.connectionMapAt = at;
        break;
    case Tallied::Type:
        s.typeAt = at;
        break;
    case Tallied::Name:
        s.nameAt = at;
        break;
    case Tallied::Layout:
        s.layoutAt = at;
        s.layoutInts = (int)f.ints(r).size();
        break;
    case Tallied::HeaderStrings:
        s.headerStrings = (int)f.ints(r).size();
        break;
    default:
        break;
    }
}

// A Behavior record: a Root basis is no BasisCount entry.
inline void tallyBehaviour(SnStats& s, const oct::File& f, const oct::Rec& r)
{
    const std::vector<std::string> v = f.nameValues(r);
    if (!v.empty() && v[0] == "Root") ++s.roots;
}

// A DataNode Type's sizes; an unknown type makes the graph unsized.
inline void tallyNodeType(SnStats& s, const oct::File& f, const oct::Rec& r)
{
    const std::vector<std::string> v = f.nameValues(r);
    const ArenaSize z = v.empty() ? ArenaSize{-1, -1} : typeSize(v[0]);
    if (z.size32 < 0) s.sized = false;
    s.size32 += z.size32;
    s.size64 += z.size64;
}

// A level-4 or level-5 record's tally.
inline void tallyMember(SnStats& s, Tallied what, const oct::File& f, const oct::Rec& r)
{
    switch (what) {
    case Tallied::Basis:
        ++s.bases;
        break;
    case Tallied::Conversion:
        ++s.conversions;
        break;
    case Tallied::DataNode:
        ++s.dataNodes;
        break;
    case Tallied::Processor:
        ++s.processors;
        break;
    case Tallied::Family:
        ++s.families;
        break;
    case Tallied::VariantGroup:
        ++s.variantGroups;
        s.variantMembers += (int)f.ints(r).size();
        break;
    case Tallied::LayerBind:
        ++s.layerBinds;
        break;
    case Tallied::Static:
        s.statics += f.int0(r) ? 1 : 0;
        break;
    case Tallied::FamilyNames:
        s.familyMembers += (int)f.ints(r).size();
        break;
    case Tallied::Behaviour:
        tallyBehaviour(s, f, r);
        break;
    case Tallied::NodeType:
        tallyNodeType(s, f, r);
        break;
    default:
        break;
    }
}

inline SnStats scan(const oct::File& f, size_t sn)
{
    SnStats s;
    std::string pool;
    std::vector<const TallyRow*> rows;   // the current pool's
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            pool = n;
            rows = poolRows(pool);
            if (const TallyRow* row = fieldRow(n)) tallyField(s, row->what, f, r, (long)i);
            continue;
        }
        if (r.level != 4 && pool == "DataNodePool") s.dataNodeBytes += (int)r.pay.size();
        else if (r.level != 4 && pool == "LayerBindPool") s.layerBindBytes += (int)r.pay.size();
        if (const TallyRow* row = tallyRow(rows, r.level, n)) tallyMember(s, row->what, f, r);
    }
    return s;
}

// The links' tally: count, additional size, and whether the SubNetwork has a link pool.
struct LinkStats { int count = 0, extra = 0; bool pool = false; };
// Converts every base link of the SubNetwork to 3.0 form, measuring each after (convertLink).
inline LinkStats convertLinks(oct::File& f, size_t sn)
{
    LinkStats links;
    for (size_t i = sn + 1, e = f.blockEnd(sn); i < e; ++i) {
        const bool level3 = f.recs[i].level == 3;
        if (level3 && f.name(f.recs[i]) == "BaseDataNodeLinksPool") links.pool = true;
        if (f.recs[i].level != 4 || f.name(f.recs[i]) != "BaseDataNodeLinks") continue;
        const size_t before = f.recs.size();
        ++links.count;
        links.extra += convertLink(f, i);
        e += f.recs.size() - before;   // convertLink replaces one record by three
    }
    return links;
}

inline bool hasCounts(const oct::File& f, size_t sn)
{
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i)
        if (f.recs[i].level == 3 && f.name(f.recs[i]) == "VariableHeaderCount") return true;
    return false;
}

// cho::const30's ConstantHeaderCount: its value, the record removed; -1 without one.
inline int takeConstantHeaderCount(oct::File& f, size_t sn)
{
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        if (f.recs[i].level != 3 || f.name(f.recs[i]) != "ConstantHeaderCount") continue;
        const int chc = (int)f.int0(f.recs[i]);
        f.recs.erase(f.recs.begin() + i);
        return chc;
    }
    return -1;
}

// How the counts are made; exact: no processors and every type sized.
struct Mode {
    bool exact = false, allStatic = false, constSection = false;
    int constantHeaders = -1, constantLayout = 0;   // ConstantHeaderCount from cho::const30; ConstantLayoutCount
};
inline Mode chooseMode(const SnStats& s, int constantHeaders)
{
    Mode m;
    m.constantHeaders = constantHeaders;
    // exact on every shipped 3.0 SubNetwork of that kind (octtest --counts); the rest get bounds
    m.exact = !s.processors && s.sized;
    // every DataNode static (2,945/2,945 shipped 3.0 clips): all headers constant
    const bool everyNodeStatic = s.dataNodes && s.statics == s.dataNodes;
    m.allStatic = !m.exact && !s.processors && everyNodeStatic;
    m.constSection = constantHeaders > 0 && !m.allStatic && s.statics;
    return m;
}

// Static DataNodes made variable: an instance relocates every header's node pointer (FUN_004b4470).
inline void makeStaticsVariable(oct::File& f, size_t sn)
{
    for (size_t i = sn + 1, e = f.blockEnd(sn); i < e; ++i)
        if (f.recs[i].level == 5 && f.name(f.recs[i]) == "Static" && f.int0(f.recs[i])) f.setInt(f.recs[i], 0);
}

// The HeaderLayout ints that number headers 1..constantHeaders (FUN_004fe4d0 builds constant headers from them).
inline int constantLayoutCount(const oct::File& f, long layoutAt, int constantHeaders)
{
    const std::vector<int32_t> lay = f.ints(f.recs[layoutAt]);
    int clc = 0;
    for (int got = 0; clc + 1 < (int)lay.size() && got < constantHeaders; clc += 2) got += lay[clc + 1];
    return clc;
}

// An arena size pair as 3.0 writes it: <name> { '1' = [align, size, count] } (FUN_004b4970 sums them).
inline void addArenaSize(oct::File& f, std::vector<oct::Rec>& a, const std::string& name,
                         const std::vector<int32_t>& triple)
{
    a.push_back(f.node(3, name));
    a.push_back(f.intList(4, "1", triple));
}

// The count records in 3.0's order. Interning order is byte output (CO-02): do not reorder.
inline std::vector<oct::Rec> countRecords(oct::File& f, const SnStats& s, const Mode& m, const LinkStats& links)
{
    std::vector<oct::Rec> a;
    const int chc = m.constantHeaders;
    const int constHeaders = m.allStatic ? s.headerStrings : m.constSection ? chc : 0;
    const int varHeaders = m.allStatic ? 0 : m.constSection ? s.headerStrings - chc : s.headerStrings;
    const int valsA[] = {constHeaders, varHeaders, s.statics, s.dataNodes - s.statics, s.processors};
    for (size_t k = 0; k < std::size(valsA); ++k) a.push_back(f.intScalar(3, kCountsA[k], valsA[k]));
    if (s.statics || g_slack) {
        const int c32 = kNodeBound * s.statics + (s.statics ? s.dataNodeBytes : 0) + g_slack;
        addArenaSize(f, a, "ConstantAccumulatedSize32", {4, c32, s.statics});
        addArenaSize(f, a, "ConstantAccumulatedSize64", {8, 2 * c32, s.statics});
    }
    const int variable = m.exact ? s.dataNodes : s.dataNodes - s.statics + s.processors;
    if (!m.allStatic) {   // 3.0 writes none when nothing is variable
        addArenaSize(f, a, "VariableAccumulatedSize32", {4, s.size32 + g_slack, variable});
        addArenaSize(f, a, "VariableAccumulatedSize64", {8, s.size64 + 2 * g_slack, variable});
    }
    const int valsB[] = {s.bases - s.roots, s.conversions, s.families, s.familyMembers, s.variantGroups,
                         s.variantMembers};
    for (size_t k = 0; k < std::size(valsB); ++k) a.push_back(f.intScalar(3, kCountsB[k], valsB[k]));
    if (s.connectionMapAt >= 0) a.push_back(f.recs[s.connectionMapAt]);   // moved into its 3.0 place
    // DefaultDataSize: floor4 of the DefaultData + DefaultDataIndices payloads (1,516/1,518 shipped, ddfit.py)
    const int defaultSize = ((s.defaultData + s.defaultIndices) & ~3) + g_slack / kSlackShare;
    if (s.defaultData >= 0) a.push_back(f.intScalar(3, "DefaultDataSize", defaultSize));
    a.push_back(f.intScalar(3, kCountsB[6], s.layerBinds));
    if (s.layerBinds) a.push_back(f.intScalar(3, "LayerBindAdditionalSize", s.layerBindBytes));
    a.push_back(f.intScalar(3, kCountsB[7], links.count));
    const int linksSize = links.extra + g_slack / kSlackShare;
    if (links.pool) a.push_back(f.intScalar(3, "BaseDataNodeLinksAdditionalSize", linksSize));
    return a;
}

// Inserts the records where 3.0 writes them: counts after Type/Name, ConstantLayoutCount after HeaderLayout.
inline void placeCounts(oct::File& f, const SnStats& s, const std::vector<oct::Rec>& a, const oct::Rec& cl)
{
    // later positions first so the earlier indices stay valid
    f.recs.insert(f.recs.begin() + s.layoutAt + 1, cl);
    if (s.connectionMapAt > s.layoutAt) f.recs.erase(f.recs.begin() + s.connectionMapAt + 1);
    size_t after = (size_t)(std::max)(s.typeAt, s.nameAt) + 1;   // no Name (clips): after Type
    if (s.connectionMapAt >= 0 && s.connectionMapAt < s.layoutAt) {
        f.recs.erase(f.recs.begin() + s.connectionMapAt);
        if (s.connectionMapAt < (long)after) --after;
    }
    f.recs.insert(f.recs.begin() + after, a.begin(), a.end());
}

inline bool fail(std::string* why, const char* reason)
{
    if (why) *why = reason;
    return false;
}
}  // namespace detail

// Adds 3.0's count records to the SubNetwork at record sn (1.0 has none: its arenas would be sized 0).
inline bool addCounts(oct::File& f, size_t sn, std::string* why = nullptr)
{
    // false with f unchanged when it has counts; without Type/HeaderLayout, after links and ConstantHeaderCount
    if (detail::hasCounts(f, sn)) return detail::fail(why, "has counts");
    const int chc = detail::takeConstantHeaderCount(f, sn);
    const detail::LinkStats links = detail::convertLinks(f, sn);   // first: their size is measured on the 3.0 form
    detail::SnStats s = detail::scan(f, sn);
    if (s.typeAt < 0 || s.layoutAt < 0) return detail::fail(why, "no Type/HeaderLayout");   // clips have no Name
    detail::Mode m = detail::chooseMode(s, chc);
    // a constant section keeps cho::const30's Static nodes under constant headers 1..chc, as 3.0 writes them
    if (!m.allStatic && s.statics && !m.constSection) {
        detail::makeStaticsVariable(f, sn);
        s.statics = 0;
    }
    if (m.constSection) m.constantLayout = detail::constantLayoutCount(f, s.layoutAt, chc);
    if (!m.exact) {   // ponytail: generous bounds (128 B/node + payload), fit per-type sizes if memory ever matters
        s.size32 = detail::kNodeBound * (s.dataNodes - s.statics + s.processors) + s.dataNodeBytes;
        s.size32 += detail::kInexactPad;
        s.size64 = 2 * s.size32;
    }
    const std::vector<oct::Rec> a = detail::countRecords(f, s, m, links);
    const int clc = m.allStatic ? s.layoutInts : m.constSection ? m.constantLayout : 0;
    detail::placeCounts(f, s, a, f.intScalar(3, "ConstantLayoutCount", clc));
    return true;
}

namespace detail {

// What clip30 reads from a SubNetwork: level-3 fields, and the first level-5 ClipDataBlock data (count stripped).
struct ClipFields {
    bool isClip = false, hasTime = false;
    long variantGroupsEnd = -1, layerBindAt = -1;
    std::string block;
};
inline ClipFields clipFields(const oct::File& f, size_t sn)
{
    ClipFields c;
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        const bool firstBlock = r.level == 5 && n == "ClipDataBlock" && c.block.empty();
        if (firstBlock) c.block = r.pay.substr(oct::spec(r.type).cw);
        if (r.level != 3) continue;
        c.hasTime = c.hasTime || n == "MinTime" || n == "MaxTime";
        if (n == "Type") c.isClip = f.nameValues(r) == std::vector<std::string>{"Clip"};
        else if (n == "VariantGroupPool") c.variantGroupsEnd = (long)f.blockEnd(i);
        else if (n == "LayerBindPool") c.layerBindAt = (long)i;
    }
    return c;
}


// A float scalar holding these 4 bytes.
inline oct::Rec floatBytes(oct::File& f, const char* name, const std::string& bytes4)
{
    return oct::Rec{3, oct::makeType(oct::SCALAR, 0, oct::FLOAT), f.addName(name), bytes4};
}

// Where a ClipData DataNode without External takes it (after Type / Static); 0 for any other node.
inline size_t externalSlot(const oct::File& f, size_t node)
{
    size_t put = 0;
    bool clipData = false, external = false;
    for (size_t j = node + 1, e = f.blockEnd(node); j < e; ++j) {
        const std::string& n = f.name(f.recs[j]);
        if (n == "Type") {
            clipData = f.nameValues(f.recs[j]) == std::vector<std::string>{"ClipData"};
            put = j + 1;
        } else if (n == "Static") put = j + 1;
        else if (n == "External") external = true;
    }
    return clipData && !external ? put : 0;
}
}  // namespace detail

// A clip in 3.0 form: MinTime/MaxTime from its ClipDataBlock, External 1 on ClipData nodes. True if changed.
inline bool clip30(oct::File& f, size_t sn)
{
    const detail::ClipFields c = detail::clipFields(f, sn);
    const std::string& block = c.block;
    if (!c.isClip) return false;
    bool changed = false;
    // the reader takes the time range from them (FUN_00500ac0 -> FUN_004b0f70); 2,995/2,995 shipped 3.0 clips
    const bool hasSlot = c.variantGroupsEnd >= 0 || c.layerBindAt >= 0;
    if (!c.hasTime && block.size() >= 8 && hasSlot) {
        std::vector<oct::Rec> t = {detail::floatBytes(f, "MinTime", block.substr(0, 4)),
                                   detail::floatBytes(f, "MaxTime", block.substr(4, 4))};
        const size_t at = (size_t)(c.variantGroupsEnd >= 0 ? c.variantGroupsEnd : c.layerBindAt);
        f.recs.insert(f.recs.begin() + at, t.begin(), t.end());
        changed = true;
    }
    for (size_t i = sn + 1; i < f.blockEnd(sn); ++i) {
        if (f.recs[i].level != 4 || f.name(f.recs[i]) != "DataNode") continue;
        const size_t put = detail::externalSlot(f, i);
        if (!put) continue;
        f.recs.insert(f.recs.begin() + put, f.intScalar(5, "External", 1));
        changed = true;
    }
    return changed;
}

// clip30 and addCounts on every SubNetwork of the pool, spliced once. Returns how many got counts.
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
