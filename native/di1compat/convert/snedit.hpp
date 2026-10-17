// One SubNetwork's header tree, DataNodes and processors: lookups, and appends in the exporter's shapes. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../formats/oct.hpp"
#include "records.hpp"

namespace octx {

namespace detail {
enum class SnPool { Other, DataNodes, Processors, Bases, Conversions };   // the pools SnEdit reads entries of
}  // namespace detail

// The SubNetwork at record sn. Appends keep existing indices: headers, nodes and processors go last.
struct SnEdit {
    oct::File& f;
    size_t sn;
    long hsAt = -1, hsiAt = -1, hlAt = -1, dnPool = -1, pnPool = -1;   // HeaderStrings, -Indices, -Layout, pools
    std::vector<oct::Rec> addVg;
    struct DN {
        int header = -1, basis = 0;
        std::string type;
    };
    std::vector<DN> dns;
    int npn = 0;   // ProcessorNodes
    std::vector<std::string> hs;
    std::vector<int32_t> hsi, hl, parent{-1};   // parent[k] of header k (1-based; 0 = the SubNetwork root)
    std::map<int, int> basisParent;
    std::map<std::pair<int, int>, int> conv;   // (from basis, to basis) -> converter DataNode
    std::vector<oct::Rec> addDn, addPn;
    std::map<int, std::vector<int>> kids;      // header -> its child headers, in creation order
    std::map<int, int> dnAt;                   // header -> its DataNode, last wins; key -1: header-less
    // header string -> its first index (a linear scan per new header cost ~0.1 s on in_city's 1,251 locators)
    std::unordered_map<std::string, int> hsIndex;
    const bool variantGroups;   // Options::variantGroups

    SnEdit(oct::File& file, size_t snRec, const Options& opt);
    bool ok() const
    {
        const bool headers = hsAt >= 0 && hsiAt >= 0 && hlAt >= 0;
        return headers && dnPool >= 0;
    }
    // header h's name; a reference, since child() compares it for every sibling
    const std::string& name(int h) const
    {
        static const std::string none;
        const bool known = h >= 1 && h <= (int)hsi.size() && hsi[h - 1] >= 0;
        return known && hsi[h - 1] < (int)hs.size() ? hs[hsi[h - 1]] : none;
    }
    int child(int h, const std::string& n) const
    {
        auto it = kids.find(h);
        if (it == kids.end()) return -1;
        for (int k : it->second)
            if (name(k) == n) return k;
        return -1;
    }
    int find(const std::string& path) const;
    int dnOf(int h) const
    {
        auto it = dnAt.find(h);
        return it == dnAt.end() ? -1 : it->second;
    }
    int header(const std::string& n, int par);
    int node(int h, const std::string& type, int basis, bool external);
    void processor(int h, const std::string& type, const std::vector<int32_t>& refs,
                   const std::vector<std::string>& attrs);
    std::vector<std::pair<int, int>> toWorld(int b, bool& found) const;
    void apply();

private:
    using Fields = std::map<std::string, int>;
    void readEntry(detail::SnPool pool, const std::string& n, const oct::Rec& r, int& basis, Fields& c);
    void readField(detail::SnPool pool, const std::string& n, const oct::Rec& r, int basis, Fields& c);
    void readHeaders();
};

namespace detail {
// The level-3 records SnEdit reads: their positions, and the pools whose entries it reads.
struct SnRecord {
    const char* name;
    long SnEdit::*at;
    SnPool pool;
};
inline constexpr SnRecord kSnRecords[] = {
    {"HeaderStrings", &SnEdit::hsAt, SnPool::Other},
    {"HeaderStringIndices", &SnEdit::hsiAt, SnPool::Other},
    {"HeaderLayout", &SnEdit::hlAt, SnPool::Other},
    {"DataNodePool", &SnEdit::dnPool, SnPool::DataNodes},
    {"ProcessorNodePool", &SnEdit::pnPool, SnPool::Processors},
    {"BasisPool", nullptr, SnPool::Bases},
    {"BasisConversionPool", nullptr, SnPool::Conversions},
};
inline const SnRecord* snRecord(const std::string& n)
{
    for (const SnRecord& r : kSnRecords)
        if (n == r.name) return &r;
    return nullptr;
}
}  // namespace detail

inline SnEdit::SnEdit(oct::File& file, size_t snRec, const Options& opt)
    : f(file), sn(snRec), variantGroups(opt.variantGroups)
{
    detail::SnPool pool = detail::SnPool::Other;
    int basis = -1;
    Fields c;   // the current BasisConversion's fields
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            const detail::SnRecord* known = detail::snRecord(n);
            pool = known ? known->pool : detail::SnPool::Other;
            if (known && known->at) this->*(known->at) = (long)i;
        } else if (r.level == 4) readEntry(pool, n, r, basis, c);
        else if (r.level == 5) readField(pool, n, r, basis, c);
    }
    if (ok()) readHeaders();
}

// A level-4 pool entry: a DataNode, a ProcessorNode, a Basis (its key is its number) or a BasisConversion.
inline void SnEdit::readEntry(detail::SnPool pool, const std::string& n, const oct::Rec& r, int& basis, Fields& c)
{
    switch (pool) {
    case detail::SnPool::DataNodes:
        if (n == "DataNode") dns.emplace_back();
        break;
    case detail::SnPool::Processors:
        if (n == "ProcessorNode") ++npn;
        break;
    case detail::SnPool::Bases:
        basis = std::stoi(f.name0(r));
        break;
    case detail::SnPool::Conversions:
        c.clear();
        break;
    default:
        break;
    }
}

// A level-5 field of the current pool entry.
inline void SnEdit::readField(detail::SnPool pool, const std::string& n, const oct::Rec& r, int basis, Fields& c)
{
    if (pool == detail::SnPool::DataNodes && !dns.empty()) {
        if (n == "Header") dns.back().header = f.int0(r);
        else if (n == "BasisRef") dns.back().basis = f.int0(r);
        else if (n == "Type") dns.back().type = f.name0(r);
        return;
    }
    if (pool == detail::SnPool::Bases && n == "ParentRef") basisParent[basis] = f.int0(r);
    if (pool != detail::SnPool::Conversions) return;
    c[n] = f.int0(r);
    const bool complete = c.count("FromBasisRef") && c.count("ToBasisRef") && c.count("DataNodeRef");
    if (complete) conv[{c["FromBasisRef"], c["ToBasisRef"]}] = c["DataNodeRef"];
}

// The header strings, parents and children, and each header's DataNode.
inline void SnEdit::readHeaders()
{
    hs = f.nameValues(f.recs[hsAt]);
    hsi = f.ints(f.recs[hsiAt]);
    hl = f.ints(f.recs[hlAt]);
    for (size_t i = 0; i + 1 < hl.size(); i += 2)
        for (int k = 0; k < hl[i + 1]; ++k) parent.push_back(hl[i]);
    for (size_t k = 1; k < parent.size(); ++k) kids[parent[k]].push_back((int)k);
    for (size_t d = 0; d < dns.size(); ++d) dnAt[dns[d].header] = (int)d;
}

// 'A|B|C' from the root -> its header, or -1.
inline int SnEdit::find(const std::string& path) const
{
    int h = 0;
    for (size_t p = 0;;) {
        size_t q = path.find('|', p);
        h = child(h, path.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (h < 0 || q == std::string::npos) return h;
        p = q + 1;
    }
}

// A header named n under par, appended -> its number.
inline int SnEdit::header(const std::string& n, int par)
{
    if (hsIndex.empty())
        for (size_t i = 0; i < hs.size(); ++i) hsIndex.emplace(hs[i], (int)i);
    auto it = hsIndex.find(n);
    int s = it != hsIndex.end() ? it->second : -1;
    if (s < 0) {
        hs.push_back(n);
        s = (int)hs.size() - 1;
        hsIndex.emplace(n, s);
    }
    hsi.push_back(s);
    hl.insert(hl.end(), {par, 1});
    parent.push_back(par);
    int h = (int)parent.size() - 1;
    kids[par].push_back(h);
    return h;
}

// A DataNode with identity data of its type (Dilation / Transform / Matrix4x4) -> its index.
inline int SnEdit::node(int h, const std::string& type, int basis, bool external)
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
        return nd;
    }
    addDn.push_back(floatList(f, 6, "Translation", {0, 0, 0}));
    addDn.push_back(floatList(f, 6, "Orientation", {0, 0, 0, 1}));
    if (type != "Dilation") {
        addDn.push_back(floatList(f, 6, "Scale", {1, 1, 1, 1}));
        addDn.push_back(floatList(f, 6, "Unscale", {1, 1, 1, 1}));
        return nd;
    }
    const float one = 1;
    const std::string scale((const char*)&one, 4);
    addDn.push_back(oct::Rec{6, oct::makeType(oct::SCALAR, 0, oct::FLOAT), f.addName("Scale"), scale});
    return nd;
}

// A processor node of this type; a two-ref *VariantProcessor also gets its VariantGroup [Input, Output].
inline void SnEdit::processor(int h, const std::string& type, const std::vector<int32_t>& refs,
                              const std::vector<std::string>& attrs)
{
    addPn.push_back(keyedNode(f, 4, "ProcessorNode", npn++));
    addPn.push_back(f.intScalar(5, "Header", h));
    addPn.push_back(f.nameScalar(5, f.addName("Type"), type));
    addPn.push_back(f.intList(5, "DataNodeRefs", refs));
    addPn.push_back(nameList(f, 5, "DataNodeAttributes", attrs));
    const size_t tl = type.size(), vl = strlen("VariantProcessor");
    const bool variant = tl > vl && type.compare(tl - vl, vl, "VariantProcessor") == 0;
    if (variantGroups && refs.size() == 2 && variant) addVg.push_back(f.intList(4, "VariantGroup", refs));
}

// Converter steps (DataNode, target basis) from basis b to root 0: direct, else via parents. found: a chain exists.
inline std::vector<std::pair<int, int>> SnEdit::toWorld(int b, bool& found) const
{
    constexpr int kMaxHops = 64;   // a longer parent chain is a cycle in a damaged file
    std::vector<std::pair<int, int>> steps;
    for (int guard = 0; b != 0 && guard < kMaxHops; ++guard) {
        auto d = conv.find({b, 0});
        if (d != conv.end()) {
            steps.push_back({d->second, 0});
            b = 0;
            break;
        }
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

// Writes the appends into f: layout and header lists in place first, then the records, later positions first.
inline void SnEdit::apply()
{
    if (addDn.empty() && addPn.empty()) return;
    f.setInts(f.recs[hlAt], hl);
    f.setInts(f.recs[hsiAt], hsi);
    setNames(f, f.recs[hsAt], hs);
    if (pnPool > dnPool) f.recs.insert(f.recs.begin() + f.blockEnd(pnPool), addPn.begin(), addPn.end());
    const size_t dnEnd = f.blockEnd(dnPool);
    if (pnPool < 0 && !addPn.empty()) {
        addPn.insert(addPn.begin(), f.node(3, "ProcessorNodePool"));
        f.recs.insert(f.recs.begin() + dnEnd, addPn.begin(), addPn.end());
    }
    f.recs.insert(f.recs.begin() + dnEnd, addDn.begin(), addDn.end());
    const bool pnFirst = pnPool >= 0 && pnPool < dnPool;
    if (pnFirst) f.recs.insert(f.recs.begin() + f.blockEnd(pnPool), addPn.begin(), addPn.end());
    if (addVg.empty()) return;
    // after the other inserts: find the pool again
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        if (f.recs[i].level != 3 || f.name(f.recs[i]) != "VariantGroupPool") continue;
        f.recs.insert(f.recs.begin() + f.blockEnd(i), addVg.begin(), addVg.end());
        return;
    }
    // ponytail: a SubNetwork without a VariantGroupPool keeps its processors ungrouped (none seen in 1.0 files)
}

// Renames each association of type whose path edit(SnEdit&, path) redirects; edit returns the new path or "".
template <class Edit>
inline int redirectAssociations(oct::File& f, const std::string& type, Edit edit, const Options& opt = {})
{
    // di1::curves::curves30 passes no options: it makes no variant processor, so they change nothing there
    long sp = f.pool("SubNetworkPool");
    if (sp < 0) return 0;
    std::vector<size_t> sns;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; i = f.blockEnd(i))
        if (f.recs[i].level == 2) sns.push_back(i);
    std::map<int, std::vector<std::string>> want;
    eachAssociation(f, type, [&](oct::Rec& r, int sn) {
        if (sn < (int)sns.size()) want[sn].push_back(f.name0(r));
    });
    std::map<std::pair<int, std::string>, std::string> renamed;
    // last to first: names are interned in this order, so the name table's bytes follow it (CO-02)
    std::vector<size_t> order;
    for (auto it = want.rbegin(); it != want.rend(); ++it) order.push_back((size_t)it->first);
    editPoolChildren(f, sp, sns, order, [&](oct::File& g, size_t k) {
        SnEdit e(g, 0, opt);
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
        auto k = renamed.find({sn, f.name0(r)});
        if (k == renamed.end()) return;
        r.pay.clear();
        oct::File::put(r.pay, f.addName(k->second), 2);
        ++n;
    });
    return n;
}

// The 3.0 exporter's chain from node src (header xf) to the root basis: per conversion an 'In <basis>' node.
inline int worldChain(SnEdit& e, int xf, int src, bool lastExternal)
{
    bool found = false;
    auto steps = e.toWorld(e.dns[src].basis, found);
    if (!found) return -1;
    for (auto& s : steps) {
        // the converter is a BasisConversion's DataNodeRef from the file: a damaged one can name no DataNode (CO-16)
        if (s.first < 0 || s.first >= (int)e.dns.size())
            throw std::runtime_error("octx: a BasisConversion names DataNode " + std::to_string(s.first) +
                                     ", which is not there");
        // 3.0 never feeds a converter of another type into a chain (concat.py): no chain then
        const std::string& ct = e.dns[s.first].type;
        if (ct != "Transform" && ct != "Dilation" && ct != "Matrix4x4") return -1;
    }
    std::string t = e.dns[src].type;
    int in = src;
    for (auto& s : steps) {
        // each written by 'Convert To <basis>' = Transform<src type>With<converter type> [Input, Converter, Output]
        std::string bn = s.second == 0 ? "WorldBasis" : "Basis" + std::to_string(s.second);
        int ho = e.header("In " + bn, xf), hp = e.header("Convert To " + bn, xf);
        int out = e.node(ho, t, s.second, lastExternal && s.second == 0);
        const std::string type = "Transform" + t + "With" + e.dns[s.first].type;
        e.processor(hp, type, {in, s.first, out}, {"Input", "Converter", "Output"});
        in = out;
    }
    return in;   // src itself when it is in the root basis
}

}  // namespace octx
