// 1.0 choreography layouts: ContextNodes as 3.0 InstanceNodes (FORMATS.md 4.10). convert/records; any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../convert/records.hpp"
#include "../../formats/oct.hpp"

namespace di1::cho {

// ---- layout ----
namespace detail {

// A pool entry's fields by name: record index, the last one of a repeated name
using Entry = std::map<std::string, long>;

inline Entry entryFields(const oct::File& f, size_t e)
{
    Entry m;
    for (size_t j = e + 1, x = f.blockEnd(e); j < x; ++j)
        if (f.recs[j].level == f.recs[e].level + 1) m[f.name(f.recs[j])] = (long)j;
    return m;
}

// The level-4 entries of a pool, none for pool < 0
inline std::vector<Entry> poolEntries(const oct::File& f, long pool)
{
    std::vector<Entry> v;
    if (pool < 0) return v;
    for (size_t j = pool + 1, x = f.blockEnd(pool); j < x; ++j)
        if (f.recs[j].level == 4) v.push_back(entryFields(f, j));
    return v;
}

// An entry's int field k, def when absent or holding no int
inline int entryInt(const oct::File& f, const Entry& m, const char* k, int def)
{
    auto it = m.find(k);
    return it == m.end() ? def : octx::firstInt(f, f.recs[it->second], def);
}

inline oct::Rec atLevel(const oct::File& f, long rec, uint16_t level)
{
    oct::Rec r = f.recs[rec];
    r.level = level;
    return r;
}

// Layout flag bits (FORMATS.md 4.10): 1.0 FUN_0075d630 ContextNode, 3.0 FUN_00787e30 InstanceNode
inline constexpr int kCtxPlaced = 0x1;         // has a PlacementNodeRef
inline constexpr int kCtxFlag4 = 0x4;          // -> kInstFlag4
inline constexpr int kCtxAttachable = 0x8;     // instanced when an Attachment uses it
inline constexpr int kCtxBounded = 0x10;       // has a BoundingSceneNodeName
inline constexpr int kInstFilePath = 0x8;
inline constexpr int kInstFlag4 = 0x10;
inline constexpr int kInstBounded = 0x20;
inline constexpr int kInstAttachable = 0x40;
// PlacementNode flag bits, 1.0 -> 3.0 (FORMATS.md 4.10)
inline constexpr int kPlaceKept = 0xf;         // kept as they are
inline constexpr int kPlaceFlag10 = 0x10;      // -> kPlaceFlag30
inline constexpr int kPlaceFlag30 = 0x200;
inline constexpr int kPlaceAxes = 0x1e0;       // per-axis bits, one bit lower in 3.0
inline constexpr int kPlaceLayout = 0x100;     // named 'Layout'
inline constexpr int kPlaceCtxPlaced = 0x400;  // its context is kCtxPlaced

// The contexts that become InstanceNodes, numbered from 0: a FilePath name, or attachable and attached
inline std::map<int, int> instanceNumbers(const oct::File& f, std::vector<Entry>& ctx, const std::vector<Entry>& atts)
{
    std::set<int> used;
    for (auto& a : atts) {
        used.insert(entryInt(f, a, "BaseContextRef", -1));
        used.insert(entryInt(f, a, "LayerContextRef", -1));
    }
    std::map<int, int> remap;
    for (size_t i = 0; i < ctx.size(); ++i) {
        int fl = entryInt(f, ctx[i], "Flags", 0);
        auto fp = ctx[i].find("FilePath");
        if (fp != ctx[i].end() && f.nameValues(f.recs[fp->second]).empty()) ctx[i].erase(fp);   // not a name
        const bool attached = (fl & kCtxAttachable) && used.count((int)i);
        if (!ctx[i].count("FilePath") && !attached) continue;
        const int next = (int)remap.size();   // GR-23: no reliance on the order C++17 gives '='
        remap[(int)i] = next;
    }
    return remap;
}

// The InstanceNode of a 1.0 context ref field k10, -1 for none
inline int instanceRef(const oct::File& f, const Entry& a, const std::map<int, int>& remap, const char* k10)
{
    auto m = remap.find(entryInt(f, a, k10, -1));
    return m == remap.end() ? -1 : m->second;
}

// The AttachmentPool rebuilt: Base/LayerContextRef become Base/LayerInstanceRef
inline std::vector<oct::Rec> attachments(oct::File& f, long ap, const std::vector<Entry>& atts,
                                         const std::map<int, int>& remap)
{
    std::vector<oct::Rec> att;
    if (ap < 0) return att;
    att.push_back(f.recs[ap]);
    size_t k = 0;
    for (size_t j = ap + 1, x = f.blockEnd(ap); j < x; ++j) {
        if (f.recs[j].level != 4) continue;
        const Entry& a = atts[k++];
        att.push_back(f.recs[j]);
        att.push_back(f.intScalar(5, "BaseInstanceRef", instanceRef(f, a, remap, "BaseContextRef")));
        if (a.count("BaseConnectionMap")) att.push_back(atLevel(f, a.at("BaseConnectionMap"), 5));
        att.push_back(f.intScalar(5, "LayerInstanceRef", instanceRef(f, a, remap, "LayerContextRef")));
        if (a.count("LayerConnectionMap")) att.push_back(atLevel(f, a.at("LayerConnectionMap"), 5));
        if (a.count("AnimLayerName")) att.push_back(atLevel(f, a.at("AnimLayerName"), 5));
    }
    return att;
}

// A FilePath record with its path in lower case
inline oct::Rec lowerFilePath(oct::File& f, long rec)
{
    std::string p = f.nameValues(f.recs[rec])[0];
    for (char& ch : p) ch = (char)tolower((unsigned char)ch);
    return f.nameScalar(5, f.recs[rec].name, p);
}

// The InstanceNodePool: an InstanceNode per numbered context, in number order
inline std::vector<oct::Rec> instanceNodes(oct::File& f, const std::vector<Entry>& ctx, const std::map<int, int>& remap)
{
    std::vector<oct::Rec> inst = {f.node(3, "InstanceNodePool")};
    for (auto& [old, k] : remap) {
        const Entry& c = ctx[old];
        int fl = entryInt(f, c, "Flags", 0), nf = 0;
        inst.push_back(octx::keyedNode(f, 4, "InstanceNode", k));
        if (c.count("Name")) inst.push_back(atLevel(f, c.at("Name"), 5));
        if (c.count("PlacementNodeRef"))
            inst.push_back(f.intScalar(5, "PlacementNodeRef", entryInt(f, c, "PlacementNodeRef", -1)));
        if (c.count("BoundingSceneNodeName")) inst.push_back(atLevel(f, c.at("BoundingSceneNodeName"), 5));
        if (c.count("FilePath")) {
            inst.push_back(lowerFilePath(f, c.at("FilePath")));
            nf |= kInstFilePath | (fl & kCtxFlag4 ? kInstFlag4 : 0);
        }
        if (fl & kCtxBounded) nf |= kInstBounded;
        if (fl & kCtxAttachable) nf |= kInstAttachable;
        inst.push_back(f.intScalar(5, "Flags", nf));
    }
    return inst;
}

// The PlacementNodePool rebuilt: Name = its context's, ContextNodeRef dropped, Flags in 3.0 bits
inline std::vector<oct::Rec> placementNodes(oct::File& f, long pp, const std::vector<Entry>& pls,
                                            const std::vector<Entry>& ctx)
{
    std::vector<oct::Rec> pl;
    if (pp < 0) return pl;
    pl.push_back(f.recs[pp]);
    size_t k = 0;
    for (size_t j = pp + 1, x = f.blockEnd(pp); j < x; ++j) {
        if (f.recs[j].level != 4) continue;
        const Entry& p = pls[k++];
        int fl = entryInt(f, p, "Flags", 0);
        int nf = (fl & kPlaceKept) | (fl & kPlaceFlag10 ? kPlaceFlag30 : 0) | (fl & kPlaceAxes) >> 1;
        long nameRec = p.count("Name") ? p.at("Name") : -1;
        int cref = entryInt(f, p, "ContextNodeRef", -1);
        if (cref >= 0 && cref < (int)ctx.size()) {
            if (ctx[cref].count("Name")) nameRec = ctx[cref].at("Name");
            if (entryInt(f, ctx[cref], "Flags", 0) & kCtxPlaced) nf |= kPlaceCtxPlaced;
        }
        pl.push_back(f.recs[j]);
        if (nameRec >= 0) {
            pl.push_back(atLevel(f, nameRec, 5));
            if (f.nameValues(f.recs[nameRec]) == std::vector<std::string>{"Layout"}) nf |= kPlaceLayout;
        }
        pl.push_back(f.intScalar(5, "Flags", nf));
    }
    return pl;
}

// The pool at 'at' (if any) replaced by recs, as an edit
inline void replacePool(const oct::File& f, long at, std::vector<oct::Rec>& recs, std::vector<octx::RangeEdit>& edits)
{
    if (at < 0) return;
    edits.push_back({(size_t)at, f.blockEnd(at) - (size_t)at, std::move(recs)});
}

// One Layout's ContextNodes as InstanceNodes, its pools' edits into edits: the instances, -1 without ContextNodePool
inline int layoutOne(oct::File& f, size_t L, std::vector<octx::RangeEdit>& edits)
{
    long pp = -1, cp = -1, ap = -1;
    for (size_t i = L + 1, e = f.blockEnd(L); i < e; ++i) {
        if (f.recs[i].level != 3) continue;
        const std::string& n = f.name(f.recs[i]);
        if (n == "PlacementNodePool") pp = (long)i;
        else if (n == "ContextNodePool") cp = (long)i;
        else if (n == "AttachmentPool") ap = (long)i;
    }
    if (cp < 0) return -1;
    std::vector<Entry> ctx = poolEntries(f, cp);
    const std::vector<Entry> pls = poolEntries(f, pp), atts = poolEntries(f, ap);
    const std::map<int, int> remap = instanceNumbers(f, ctx, atts);
    // built in this order: it is the order their names enter the name table (rule 1)
    std::vector<oct::Rec> att = attachments(f, ap, atts, remap);
    std::vector<oct::Rec> inst = instanceNodes(f, ctx, remap);
    std::vector<oct::Rec> pl = placementNodes(f, pp, pls, ctx);
    replacePool(f, pp, pl, edits);
    replacePool(f, cp, inst, edits);
    replacePool(f, ap, att, edits);
    return (int)remap.size();
}

}  // namespace detail

// layout30 (cholayout10): ContextNodes become InstanceNodes (FORMATS.md 4.10). Returns the Layouts converted
// (0: no ContextNodePool); *instances = InstanceNodes written.
inline int layout30(oct::File& f, int* instances = nullptr)
{
    long lp = f.pool("LayoutPool");
    if (lp < 0) return 0;
    std::vector<size_t> layouts;
    for (size_t i = lp + 1, e = f.blockEnd(lp); i < e; ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "Layout") layouts.push_back(i);
    std::vector<octx::RangeEdit> edits;
    int converted = 0, written = 0;
    for (auto lt = layouts.rbegin(); lt != layouts.rend(); ++lt) {   // last first: the order names were added in
        const int n = detail::layoutOne(f, *lt, edits);
        if (n < 0) continue;
        written += n;
        ++converted;
    }
    octx::applyEdits(f, edits);
    if (instances) *instances = written;
    return converted;
}

}  // namespace di1::cho
