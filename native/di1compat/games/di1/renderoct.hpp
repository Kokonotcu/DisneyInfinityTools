// A 1.0 world's .oct, vertices and indices in 3.0's form (conv.convert_geometry, build_buffers).
// Base and formats only; any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../convert/vertex.hpp"
#include "../../formats/oct.hpp"
#include "../../formats/rentable.hpp"
#include "../../formats/vtxf.hpp"
#include "render.hpp"
#include "rendermtb.hpp"

namespace render {

// The 1.0 world's files: its stem ('in_city'), .mtb, _0.vbuf and _0.ibuf
struct World10 {
    const std::string& stem;
    const std::string& mtb;
    const std::string& vbuf;
    const std::string& ibuf;
};

namespace detail {

constexpr uint64_t kMaxIndex16 = 0xffff;       // a node past it gets 32-bit indices (FORMATS.md 4.4)
constexpr int32_t kIndexBuffer16 = 0, kIndexBuffer32 = 2;   // Idata[0] in 3.0 (FORMATS.md 4.3)
constexpr size_t kIndexBuffers = 3;   // IndexBufferPool holds IndexBuffer 0 .. 2
constexpr int kIndexBytes16 = 2, kIndexBytes32 = 4;
constexpr int kNodeLevel = 2;   // a pool's entries (Node, Material); their fields one level deeper (FORMATS.md 4.1)

using FormatMap = std::map<std::pair<int, uint32_t>, uint32_t>;    // (material, 1.0 vformat) -> 3.0 vformat
using ShaderFormats = std::map<int, const std::set<uint32_t>*>;   // material -> vformats its final shader declares

// Primitive RenderCaps (FORMATS.md 4.5): 1.0's drawn bit is 3.0's 0x200; 0x2000 -> 0x400 is a guess
constexpr int32_t kCapsLow = 0xfff, kDrawn10 = 0x1000, kDrawn30 = 0x200, kGuess10 = 0x2000, kGuess30 = 0x400;

// conv.rendercaps30; 1.0 draws a primitive with 0, 3.0 does not
inline int32_t rendercaps30(int32_t v)
{
    if (v == 0) return kDrawn30;
    return (v & kCapsLow) | ((v & kDrawn10) ? kDrawn30 : 0) | ((v & kGuess10) ? kGuess30 : 0);
}

// (MaterialReference, vformatCRC) per primitive in record order (autobundle.prim_pairs). -> nullptr or the problem
inline const char* primPairs(const oct::File& f, std::map<uint32_t, std::set<int>>& users,
                             std::map<int, std::set<uint32_t>>& drawn)
{
    int ref = -1;
    for (auto& rec : f.recs) {
        const std::string& n = f.name(rec);
        const bool isRef = n == "MaterialReference";
        if (!isRef && n != "vformatCRC") continue;
        const std::vector<int32_t> v = f.ints(rec);
        if (v.empty()) return isRef ? "MaterialReference without a value" : "vformatCRC without a value";
        if (isRef) {
            ref = v[0];
            continue;
        }
        users[(uint32_t)v[0]].insert(ref);
        drawn[ref].insert((uint32_t)v[0]);
    }
    return nullptr;
}

// A primitive's records by name: the last of each in its subtree; RenderCaps is optional
inline constexpr const char* kPrimFields[] = {"Idata", "Vdata", "vformatCRC", "MaterialReference", "RenderCaps"};
constexpr size_t kIdata = 0, kVdata = 1, kCrc = 2, kMatRef = 3, kCaps = 4, kPrimFieldCount = 5;
struct PrimRecs { size_t at[kPrimFieldCount] = {}; bool has[kPrimFieldCount] = {}; };

// index of nm in kPrimFields, or kPrimFieldCount
inline size_t primField(const std::string& nm)
{
    for (size_t k = 0; k < kPrimFieldCount; ++k)
        if (nm == kPrimFields[k]) return k;
    return kPrimFieldCount;
}

// false: Idata, Vdata, vformatCRC or MaterialReference missing (RE-19: record 0 stood in for it)
inline bool primRecords(const oct::File& f, size_t prim, PrimRecs& p)
{
    for (size_t j = prim + 1, pe = f.blockEnd(prim); j < pe; ++j) {
        const size_t k = primField(f.name(f.recs[j]));
        if (k == kPrimFieldCount) continue;
        p.at[k] = j;
        p.has[k] = true;
    }
    const bool buffers = p.has[kIdata] && p.has[kVdata];
    return buffers && p.has[kCrc] && p.has[kMatRef];
}

// Idata: [width, first index, base, count]; Vdata: [streams, vertices, (0, offset, stride) per stream]
struct PrimData { std::vector<int32_t> ida, vda; uint32_t src = 0; int ref = 0; };

// -> nullptr, or the problem: a value missing, or a list shorter than its counts (RE-19)
inline const char* readPrim(const oct::File& f, const PrimRecs& p, PrimData& d)
{
    const std::vector<int32_t> crc = f.ints(f.recs[p.at[kCrc]]), ref = f.ints(f.recs[p.at[kMatRef]]);
    if (crc.empty() || ref.empty()) return "primitive without a vformatCRC or MaterialReference value";
    d.ida = f.ints(f.recs[p.at[kIdata]]);
    d.vda = f.ints(f.recs[p.at[kVdata]]);
    d.src = (uint32_t)crc[0];
    d.ref = ref[0];
    if (d.ida.size() <= vertex::kIdataCount) return "Idata too short";
    const bool counted = !d.vda.empty() && d.vda[0] >= 0;
    if (!counted || !vertex::vdataHolds(d.vda, (size_t)d.vda[0])) return "Vdata too short";
    return nullptr;
}

inline rentable::Op copyOp(int dsi, int size, int ssi, int soff)
{
    rentable::Op o{};
    o.dsi = dsi;
    o.doff = 0;
    o.size = size;
    o.kind = rentable::kCopy;
    o.ssi = ssi;
    o.soff = soff;
    return o;
}

// the primitive's last stream ends in the re-laid format's colour tail
inline bool tailFits(const vtxf::ColourTail& ct, const std::vector<int32_t>& vda)
{
    return vda[0] == ct.stream + 1 && vertex::vdataStride(vda, ct.stream) == ct.offset + ct.size;
}

// A translated material's primitive: every stream copied as it is; a re-laid format's colour tail to its own stream
inline const char* samePlan(const IO& io, uint32_t src, const std::vector<int32_t>& vda, rentable::Plan& same)
{
    const vtxf::ColourTail* ct = nullptr;
    if (io.tails10)
        if (auto t = io.tails10->find(src); t != io.tails10->end()) ct = &t->second;
    if (ct && !tailFits(*ct, vda)) return "vertex colour tail: strides differ from the format";
    for (int s = 0; s < vda[0]; ++s) {
        same.ss.push_back(vertex::vdataStride(vda, s));
        same.ds.push_back(ct && s == ct->stream ? ct->offset : vertex::vdataStride(vda, s));
        same.ops.push_back(copyOp(s, same.ds.back(), s, 0));
    }
    if (ct) {
        same.ds.push_back(ct->size);
        same.ops.push_back(copyOp(vda[0], ct->size, ct->stream, ct->offset));
    }
    return nullptr;
}

// Record edits held until every check passed, so a false return leaves f as it was (RE-05)
struct RecEdits {
    std::map<size_t, oct::Rec> recs;   // record index -> its edited copy
    const oct::Rec& get(const oct::File& f, size_t i) const
    {
        auto it = recs.find(i);
        return it != recs.end() ? it->second : f.recs[i];
    }
    void setInts(oct::File& f, size_t i, const std::vector<int32_t>& v)
    {
        oct::Rec r = get(f, i);
        f.setInts(r, v);
        recs[i] = std::move(r);
    }
    void setInt(oct::File& f, size_t i, int64_t v)
    {
        oct::Rec r = get(f, i);
        f.setInt(r, v);
        recs[i] = std::move(r);
    }
    void commit(oct::File& f)
    {
        for (auto& [i, r] : recs) f.recs[i] = std::move(r);
        recs.clear();
    }
};

// RenderCaps 1.0 -> 3.0; false: the record has no value
inline bool caps30(oct::File& f, RecEdits& e, size_t rec)
{
    const std::vector<int32_t> v = f.ints(e.get(f, rec));
    if (v.empty()) return false;
    e.setInt(f, rec, rendercaps30(v[0]));
    return true;
}

// the first record named nm in block blk's subtree, or -1
inline long fieldOf(const oct::File& f, size_t blk, const char* nm)
{
    for (size_t j = blk + 1, end = f.blockEnd(blk); j < end; ++j)
        if (f.name(f.recs[j]) == nm) return (long)j;
    return -1;
}

// VertexBufferPool: its first non-zero Size becomes the new vbuf size. false: a Size without a value
inline bool vertexPoolSize(oct::File& f, RecEdits& e, size_t size)
{
    const long vbp = f.pool("VertexBufferPool");
    if (vbp < 0) return true;
    for (size_t j = vbp + 1, end = f.blockEnd(vbp); j < end; ++j) {
        if (f.name(f.recs[j]) != "Size") continue;
        const std::vector<int32_t> v = f.ints(e.get(f, j));
        if (v.empty()) return false;
        if (!v[0]) continue;
        e.setInt(f, j, (int64_t)size);
        break;
    }
    return true;
}

// Buffer sizes into the pools; IndexBuffer 2 gets its own file. Commits e once every pool record is found.
inline const char* patchBufferPools(oct::File& f, RecEdits& e, const std::string& stem, const Result& r)
{
    if (!vertexPoolSize(f, e, r.vbuf.size())) return "VertexBufferPool Size without a value";
    const long ibp = f.pool("IndexBufferPool");
    if (ibp < 0) return "IndexBufferPool shape";
    std::vector<size_t> ibs;
    for (size_t j = ibp + 1, end = f.blockEnd(ibp); j < end; ++j)
        if (f.recs[j].level == kNodeLevel && f.name(f.recs[j]) == "IndexBuffer") ibs.push_back(j);
    if (ibs.size() < kIndexBuffers) return "IndexBufferPool shape";
    const size_t ib16 = ibs[kIndexBuffer16], ib32 = ibs[kIndexBuffer32];
    const long size0 = fieldOf(f, ib16, "Size"), size2 = fieldOf(f, ib32, "Size");
    const bool ownFile = !r.ibuf2.empty() && fieldOf(f, ib32, "FileName") < 0;
    const long name0 = ownFile ? fieldOf(f, ib16, "FileName") : 0;
    if (size0 < 0 || size2 < 0 || name0 < 0) return "IndexBufferPool shape";
    e.commit(f);
    f.setInt(f.recs[size0], (int64_t)r.ibuf.size());
    f.setInt(f.recs[size2], (int64_t)r.ibuf2.size());
    if (!ownFile) return nullptr;
    const oct::Rec& fn = f.recs[name0];
    f.recs.insert(f.recs.begin() + f.blockEnd(ib32), f.nameScalar(fn.level, fn.name, stem + "_2.ibuf"));
    return nullptr;
}

// One transcodeWorld call: its inputs and what the passes before the geometry found
struct WorldRun {
    const rentable::Table& T;
    const IO& io;
    Result& r;
    std::map<uint32_t, std::set<int>> users;   // 1.0 vformat -> materials drawn with it
    std::map<int, std::set<uint32_t>> drawn;   // material -> 1.0 vformats it is drawn with
    ShaderFormats shaderVf;
    FormatMap vmap;   // the first 3.0 target the material's shader takes
    FormatMap smap;   // for skinned nodes: the first target with an R plan the shader takes
};

inline bool fail(Result& r, const std::string& why)
{
    r.problems.push_back(why);
    return false;
}

// Material ref's final shader declares vformat c; no entry: any format
inline bool shaderTakes(const ShaderFormats& shaderVf, int ref, uint32_t c)
{
    auto sv = shaderVf.find(ref);
    return sv == shaderVf.end() || sv->second->count(c);
}

inline bool skinnedFormat(const IO& io, uint32_t c) { return io.skinned && io.skinned(c); }

// Translated materials keep the 1.0 format, a skinned one its 3.0 skin target (skintr10). true: all users are
inline bool mapTranslated(WorldRun& w, uint32_t crc, const std::set<int>& refs)
{
    bool all = true;
    const bool skinnedFmt = skinnedFormat(w.io, crc);
    for (int ref : refs) {
        if (w.r.translated.count(ref)) w.vmap[{ref, crc}] = skinnedFmt ? skinTarget30(w.T, crc) : crc;
        else all = false;
    }
    return all;
}

inline void mapConverted(WorldRun& w, int ref, uint32_t crc, const std::vector<uint32_t>& targets)
{
    if (w.r.translated.count(ref)) return;
    const auto takes = [&](uint32_t c) { return shaderTakes(w.shaderVf, ref, c); };
    auto c = std::find_if(targets.begin(), targets.end(), takes);
    if (c != targets.end()) w.vmap[{ref, crc}] = *c;
    else w.r.problems.push_back("vformat: no 3.0 format accepted by material " + std::to_string(ref));
}

inline void mapFormats(WorldRun& w)
{
    for (auto& [crc, refs] : w.users) {
        if (mapTranslated(w, crc, refs)) continue;
        auto t = w.T.targets.find(crc);
        if (t == w.T.targets.end() || t->second.empty()) {
            w.r.problems.push_back("vformat " + std::to_string(crc) + ": no 3.0 target");
            continue;
        }
        for (int ref : refs) mapConverted(w, ref, crc, t->second);
    }
}

inline void mapSkin(WorldRun& w, uint32_t crc, int ref)
{
    auto t = w.T.targets.find(crc);
    if (!w.io.skinLayout || t == w.T.targets.end()) return;
    if (w.r.translated.count(ref) && !skinnedFormat(w.io, crc)) return;   // translated rigid: 1.0 format
    const auto takes = [&](uint32_t c) { return w.T.skinPlans.count({crc, c}) && shaderTakes(w.shaderVf, ref, c); };
    auto c = std::find_if(t->second.begin(), t->second.end(), takes);
    if (c != t->second.end()) w.smap[{ref, crc}] = *c;
}

inline void mapSkinFormats(WorldRun& w)
{
    for (auto& [crc, refs] : w.users)
        for (int ref : refs) mapSkin(w, crc, ref);
}

// One geometry pass: the 1.0 buffers, the index remap, the record edits patchBufferPools commits
struct Geometry {
    const std::string& vbuf10;
    const std::string& ibuf10;
    vertex::Remap remap;
    RecEdits edits;
};

// A scene node: whether it is geometry, skinned, and its primitive records
struct NodeScan {
    bool geometry = false, skinned = false;
    std::vector<size_t> prims;
};

// Node n's records up to end, its blockEnd
inline NodeScan scanNode(const oct::File& f, size_t n, size_t end)
{
    NodeScan s;
    for (size_t j = n + 1; j < end; ++j) {
        const std::string& nm = f.name(f.recs[j]);
        const bool field = f.recs[j].level == kNodeLevel + 1;
        if (field && nm == "Type") {
            auto v = f.nameValues(f.recs[j]);
            s.geometry = !v.empty() && v[0] == "Geometry";
        }
        if (field && nm == "NumInfluences") s.skinned = true;
        if (nm == "Primitive") s.prims.push_back(j);
    }
    return s;
}

// A primitive's 3.0 vformat and plan; a null plan means same, the 1.0 format kept
struct PrimPlan {
    uint32_t dst = 0;
    rentable::Plan same;
    const rentable::Plan* plan = nullptr;
    const rentable::Plan& get() const { return plan ? *plan : same; }
};

// A skinned node's primitive goes to its 'skin' layout target (R plan); a translated one keeps its format
inline std::optional<PrimPlan> planFor(WorldRun& w, const PrimData& d, const NodeScan& node)
{
    PrimPlan p;
    auto vm = w.vmap.find({d.ref, d.src});
    const bool noTarget = vm == w.vmap.end() && w.T.targets.at(d.src).empty();
    if (noTarget) {
        fail(w.r, "vformat " + std::to_string(d.src) + ": no 3.0 target");
        return std::nullopt;
    }
    p.dst = vm != w.vmap.end() ? vm->second : w.T.targets.at(d.src)[0];
    const rentable::Plan* skinPlan = nullptr;
    auto s = w.smap.find({d.ref, d.src});
    if (node.skinned && p.dst != d.src && s != w.smap.end()) {
        p.dst = s->second;
        skinPlan = &w.T.skinPlans.at({d.src, p.dst});
        ++w.r.skinned;
    }
    if (p.dst != d.src) {
        p.plan = skinPlan ? skinPlan : &w.T.plans.at({d.src, p.dst});
        return p;
    }
    if (const char* why = samePlan(w.io, d.src, d.vda, p.same)) {
        fail(w.r, why);
        return std::nullopt;
    }
    return p;
}

// One converted primitive: destination streams and strides, local indices, vertex count, 3.0 vformat
struct PrimOut {
    std::vector<std::string> streams;
    std::vector<int> strides;
    std::vector<uint32_t> sub;
    uint32_t count = 0, dst = 0;
};

// The primitive's vertices through its plan, its indices made local; nullopt: a problem says why
inline std::optional<PrimOut> convertPrimitive(WorldRun& w, Geometry& g, const PrimData& d, const NodeScan& node)
{
    const std::optional<PrimPlan> p = planFor(w, d, node);
    if (!p) return std::nullopt;
    const rentable::Plan& plan = p->get();
    PrimOut out;
    std::vector<uint32_t> order;
    const char* why = vertex::remapIndices(d.ida, g.ibuf10, g.remap, order, out.sub);
    if (!why) why = vertex::convertPrim(plan, d.vda, order, g.vbuf10, out.streams);
    if (why) {
        fail(w.r, why);
        return std::nullopt;
    }
    out.strides = plan.ds;
    out.count = (uint32_t)order.size();
    out.dst = p->dst;
    return out;
}

// The node's vertices stream-major (FORMATS.md 4.4) -> each primitive's stream offsets in nv
inline std::vector<std::vector<uint32_t>> appendStreams(const std::vector<PrimOut>& outs, std::string& nv)
{
    std::vector<std::vector<uint32_t>> off(outs.size());
    size_t maxS = 0;
    for (auto& o : outs) maxS = o.strides.size() > maxS ? o.strides.size() : maxS;
    for (size_t i = 0; i < outs.size(); ++i) off[i].assign(outs[i].strides.size(), 0);
    for (size_t s = 0; s < maxS; ++s)
        for (size_t i = 0; i < outs.size(); ++i) {
            if (s >= outs[i].strides.size()) continue;
            off[i][s] = (uint32_t)nv.size();
            nv += outs[i].streams[s];
        }
    return off;
}

// sub + vbase, indexBytes each, appended to buf -> where they start
inline uint32_t appendIndices(const std::vector<uint32_t>& sub, uint32_t vbase, int indexBytes, std::string& buf)
{
    const uint32_t at = (uint32_t)buf.size();
    for (uint32_t x : sub) bytes::appendLe(buf, x + vbase, indexBytes);
    return at;
}

// Vdata: [streams, vertices, (0, offset, stride) per stream]
inline std::vector<int32_t> vdataOf(const PrimOut& o, const std::vector<uint32_t>& off)
{
    std::vector<int32_t> v = {(int32_t)o.strides.size(), (int32_t)o.count};
    for (size_t s = 0; s < o.strides.size(); ++s) {
        v.push_back(0);
        v.push_back((int32_t)off[s]);
        v.push_back(o.strides[s]);
    }
    return v;
}

// The node's buffers and each primitive's new Idata, Vdata, vformatCRC and RenderCaps, queued in g.edits
inline bool layoutNode(WorldRun& w, oct::File& f, Geometry& g, const std::vector<PrimRecs>& ps,
                       const std::vector<PrimOut>& outs)
{
    const std::vector<std::vector<uint32_t>> off = appendStreams(outs, w.r.vbuf);
    uint64_t total = 0;
    for (auto& o : outs) total += o.count;
    const bool wide = total > kMaxIndex16;
    uint32_t vbase = 0;
    for (size_t i = 0; i < ps.size(); ++i) {
        const PrimOut& o = outs[i];
        const int indexBytes = wide ? kIndexBytes32 : kIndexBytes16;
        const uint32_t at = appendIndices(o.sub, vbase, indexBytes, wide ? w.r.ibuf2 : w.r.ibuf);
        const int32_t width = wide ? kIndexBuffer32 : kIndexBuffer16;
        g.edits.setInts(f, ps[i].at[kIdata], {width, (int32_t)at, (int32_t)vbase, (int32_t)o.sub.size()});
        g.edits.setInts(f, ps[i].at[kVdata], vdataOf(o, off[i]));
        g.edits.setInt(f, ps[i].at[kCrc], o.dst);
        const bool caps = ps[i].has[kCaps];
        if (caps && !caps30(f, g.edits, ps[i].at[kCaps])) return fail(w.r, "RenderCaps without a value");
        vbase += o.count;
    }
    return true;
}

inline bool convertNode(WorldRun& w, oct::File& f, Geometry& g, size_t n, size_t end)
{
    const NodeScan node = scanNode(f, n, end);
    if (!node.geometry || node.prims.empty()) return true;
    std::vector<PrimRecs> ps(node.prims.size());
    for (size_t i = 0; i < node.prims.size(); ++i)
        if (!primRecords(f, node.prims[i], ps[i]))
            return fail(w.r, "primitive without Idata, Vdata, vformatCRC or MaterialReference");
    std::vector<PrimOut> outs;
    outs.reserve(ps.size());
    for (auto& p : ps) {
        PrimData d;
        if (const char* why = readPrim(f, p, d)) return fail(w.r, why);
        std::optional<PrimOut> o = convertPrimitive(w, g, d, node);
        if (!o) return false;
        outs.push_back(std::move(*o));
    }
    return layoutNode(w, f, g, ps, outs);
}

inline bool convertNodes(WorldRun& w, oct::File& f, Geometry& g)
{
    const long stp = f.pool("SceneTreeNodePool");
    if (stp < 0) return true;
    // reads only, the edits wait in g.edits: the pool's end holds
    for (size_t n = stp + 1, end = f.blockEnd(stp), next = 0; n < end; n = next) {
        next = f.blockEnd(n);
        const bool node = f.recs[n].level == kNodeLevel && f.name(f.recs[n]) == "Node";
        if (node && !convertNode(w, f, g, n, next)) return false;
    }
    return true;
}

// conv.convert_geometry + build_buffers; the 32-bit index buffer gets its own file (IndexBuffer 2)
inline bool convertGeometry(WorldRun& w, oct::File& f, const World10& w10)
{
    Geometry g{w10.vbuf, w10.ibuf, {}, {}};
    g.remap.stamp.assign(vertex::kIndexValues16, 0);
    g.remap.to.assign(vertex::kIndexValues16, 0);
    if (!convertNodes(w, f, g)) return false;
    if (const char* why = patchBufferPools(f, g.edits, w10.stem, w.r)) return fail(w.r, why);
    return true;
}

// Material RenderCaps is 1.0-only (FORMATS.md 4.5, conv.prepare)
inline void dropMaterialRenderCaps(oct::File& f)
{
    const long mp = f.pool("MaterialPool");
    if (mp < 0) return;
    auto b = f.recs.begin() + mp + 1, e = f.recs.begin() + f.blockEnd(mp);
    const auto caps = [&](const oct::Rec& x) { return x.level > kNodeLevel && f.name(x) == "RenderCaps"; };
    f.recs.erase(std::remove_if(b, e, caps), e);
}

}  // namespace detail

// Materials, vertices and .oct primitive records of one 1.0 world, f edited in place. false: r.problems, f unchanged
inline bool transcodeWorld(const rentable::Table& T, const IO& io, oct::File& f, const World10& w10, Result& r)
{
    detail::WorldRun w{T, io, r};
    if (const char* why = detail::primPairs(f, w.users, w.drawn)) return detail::fail(r, why);
    const std::string anyDonor;   // generic donors from any 3.0 bundle
    if (!transcodeMaterials(T, io, {w10.mtb, w.drawn, anyDonor}, r, w.shaderVf)) return false;
    detail::mapFormats(w);
    if (!r.problems.empty()) return false;
    detail::mapSkinFormats(w);
    // assets without buffers (most mission graphs) have no geometry
    const bool geometry = !w10.vbuf.empty() && !w10.ibuf.empty();
    if (geometry && !detail::convertGeometry(w, f, w10)) return false;
    detail::dropMaterialRenderCaps(f);
    return true;
}

}  // namespace render
