// REN-3: 1.0 InfluenceRemap palettes applied to skinned vertex bone indices before the render transcode. Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../base/hash.hpp"
#include "../../formats/oct.hpp"
#include "../../formats/vtxf.hpp"

namespace di1::skin {

// 1.0 vformat CRC -> (stream, byte offset) of its BLENDINDICES, U8_4 in all 18 1.0 formats that have one.
using SkinLayouts = std::unordered_map<uint32_t, std::pair<int, int>>;

// The layouts of vformat.vtxf's formats; a format with an unknown element type has none.
inline SkinLayouts skinLayouts(const std::string& t);
// Bone index k means influence refs[start + k] (FUN_004c3350), as 3.0 reads k. -> primitives remapped; notes: *why.
inline int skinRemap(oct::File& f, std::string& vbuf, const std::string& ibuf, const SkinLayouts& L,
                     std::string* why = nullptr);

namespace detail {

struct TypeSize {
    const char* type;
    int bytes;
};
inline constexpr TypeSize kTypeSizes[] = {{"FLOAT32_2", 8}, {"FLOAT32_3", 12}, {"FLOAT32_4", 16}, {"FLOAT16_2", 4},
                                          {"FLOAT16_4", 8}, {"U8_4_NORMALIZED", 4}, {"S16_4_NORMALIZED", 8},
                                          {"U8_4", 4}, {"U16_2", 4}};
constexpr int kStreamDepth = 2;    // main.streams { stream { element { .. } } }
constexpr int kElementDepth = 3;

// An element type's size, 0 if unknown.
inline int typeSize(const std::string& type)
{
    for (const TypeSize& z : kTypeSizes)
        if (type == z.type) return z.bytes;
    return 0;
}

// The brace walk of one format's main streams: depth, stream index, offset in it, the open element's fields.
struct Scan {
    int depth = 0, stream = -1, off = 0;
    std::string sem, type;

    void open()
    {
        ++depth;
        if (depth == kStreamDepth) {
            ++stream;
            off = 0;
        }
        if (depth == kElementDepth) {
            sem.clear();
            type.clear();
        }
    }
    // false: stop this format (an unknown type, or the streams' end)
    bool close(uint32_t crc, SkinLayouts& out)
    {
        if (depth == kElementDepth) {
            int size = typeSize(type);
            if (!size) return false;
            if (sem == "BLENDINDICES" && type == "U8_4") out[crc] = {stream, off};
            off += size;
        }
        --depth;
        return depth != 0;
    }
    // semantic = "X" or type = "X" at p
    void field(const std::string& t, size_t p)
    {
        constexpr size_t kSem = sizeof("semantic = \"") - 1, kType = sizeof("type = \"") - 1;
        if (t.compare(p, kSem, "semantic = \"") == 0) sem = t.substr(p + kSem, t.find('"', p + kSem) - p - kSem);
        else if (t.compare(p, kType, "type = \"") == 0) type = t.substr(p + kType, t.find('"', p + kType) - p - kType);
    }
};

inline void formatLayout(const std::string& t, const vtxf::Entry& v, SkinLayouts& out)
{
    size_t m = t.find("main", v.start), s = m == std::string::npos ? m : t.find("streams", m);
    if (s == std::string::npos || s > v.end) return;
    const uint32_t crc = hash::ehashLower(v.guid);
    Scan sc;
    for (size_t p = t.find('{', s); p < v.end; ++p) {
        const char c = t[p];
        if (c == '{') {
            sc.open();
            continue;
        }
        if (c != '}') {
            if (sc.depth == kElementDepth) sc.field(t, p);
            continue;
        }
        if (!sc.close(crc, out)) return;
    }
}

// A primitive: its vformatCRC and its Vdata / Idata records.
struct Prim {
    int32_t crc;
    size_t vdata = 0, idata = 0;
};

// The remap of one .oct: the palettes seen (claim value = index + 1), the bytes each claimed, the counts.
struct Run {
    oct::File& f;
    std::string& vbuf;
    const std::string& ibuf;
    const SkinLayouts& L;
    std::vector<std::vector<int32_t>> pals;
    std::unordered_map<uint32_t, int> claim;   // byte offset of a vertex's bone indices -> palette
    int n = 0, outside = 0, copies = 0, skipped = 0;
};

// A primitive's vformat CRC; an empty record is damaged input: hkOpen's catch logs it and serves the raw .oct.
inline int32_t vformatCrc(const oct::File& f, const oct::Rec& r)
{
    std::vector<int32_t> v = f.ints(r);
    if (v.empty()) throw std::runtime_error("skin10: vformatCRC record without a value");
    return v[0];
}

// One geometry node's remap records and primitives.
inline void nodeRecords(const oct::File& f, size_t i, std::vector<int32_t>& refs, std::vector<int32_t>& pd,
                        std::vector<Prim>& prims)
{
    size_t ne = f.blockEnd(i);
    for (size_t j = i + 1; j < ne; ++j) {
        const std::string& nm = f.name(f.recs[j]);
        if (nm == "InfluenceRemapInfluenceRefs") refs = f.ints(f.recs[j]);
        else if (nm == "InfluenceRemapPrimitiveData") pd = f.ints(f.recs[j]);
        else if (nm == "vformatCRC") prims.push_back({vformatCrc(f, f.recs[j])});
        else if (nm == "Vdata" && !prims.empty()) prims.back().vdata = j;
        else if (nm == "Idata" && !prims.empty()) prims.back().idata = j;
    }
}

// Vdata: streams, count, per stream (buffer file, base, stride). Idata: buffer 0, first, base 0, count.
inline bool layoutUsable(const std::vector<int32_t>& vd, const std::vector<int32_t>& id, int s)
{
    const bool streams = vd.size() >= 2 && vd[0] > s && vd.size() >= 2 + 3 * (uint64_t)vd[0];
    const bool indices = id.size() >= 4 && id[0] == 0 && id[2] == 0;
    return streams && indices && vd[2 + 3 * s] == 0;   // [2 + 3s]: the stream's buffer file
}

// The palette's id in r.pals, added if new.
inline int paletteId(Run& r, const std::vector<int32_t>& pal)
{
    int pid = 0;
    while (pid < (int)r.pals.size() && r.pals[pid] != pal) ++pid;
    if (pid == (int)r.pals.size()) r.pals.push_back(pal);
    return pid;
}

// A primitive's stream: its base and stride in vbuf, its vertex count, its bone indices' offset in a vertex.
struct Stream {
    uint32_t base, stride, count;
    int off;
};
constexpr uint32_t kBoneBytes = 4;    // BLENDINDICES U8_4
constexpr uint64_t kIndexBytes = 2;   // u16 indices in _0.ibuf
constexpr size_t kCopyAlign = 16;
constexpr int kNodeLevel = 2;         // a geometry node under SceneTreeNodePool

// The vertices the primitive's u16 indices reach (empty if one is past count); clash: another palette claimed one.
inline std::vector<uint32_t> primVertices(const Run& r, const std::vector<int32_t>& id, const Stream& st, int pid,
                                          bool& clash)
{
    std::vector<uint32_t> verts;
    for (int32_t q = 0; q < id[3]; ++q) {
        uint32_t v = (uint8_t)r.ibuf[2 * (id[1] + q)] | (uint32_t)(uint8_t)r.ibuf[2 * (id[1] + q) + 1] << 8;
        if (v >= st.count) return {};
        verts.push_back(v);
        auto c = r.claim.find(st.base + v * st.stride + st.off);
        if (c != r.claim.end() && c->second != pid + 1) clash = true;
    }
    return verts;
}

// Primitive k's palette, refs[start, start + count); false when it lies outside refs.
inline bool palette(const std::vector<int32_t>& refs, const std::vector<int32_t>& pd, size_t k,
                    std::vector<int32_t>& pal)
{
    int32_t start = pd[2 * k], cnt = pd[2 * k + 1];
    if (start < 0 || cnt < 0 || !bytes::fits(refs.size(), (size_t)start, (size_t)cnt)) return false;
    pal.assign(refs.begin() + start, refs.begin() + start + cnt);
    return true;
}

// The stream and the indices lie inside vbuf and ibuf, and a vertex holds the bone indices.
inline bool streamFits(const Run& r, const std::vector<int32_t>& id, const Stream& st)
{
    const uint64_t streamEnd = (uint64_t)st.base + (uint64_t)st.stride * st.count;
    const uint64_t indexEnd = kIndexBytes * ((uint64_t)id[1] + id[3]);
    return streamEnd <= r.vbuf.size() && st.stride >= (uint32_t)st.off + kBoneBytes && indexEnd <= r.ibuf.size();
}

// Primitives of a node often share one vertex range; a clash gets its own copy of the stream.
inline void ownCopy(Run& r, std::vector<int32_t>& vd, int s, size_t vdata, Stream& st)
{
    uint32_t nb = (uint32_t)bytes::alignUp(r.vbuf.size(), kCopyAlign);
    r.vbuf.resize(nb, '\0');
    r.vbuf.append(r.vbuf, st.base, (size_t)st.stride * st.count);
    vd[3 + 3 * s] = (int32_t)nb;
    r.f.setInts(r.f.recs[vdata], vd);
    st.base = nb;
    ++r.copies;
}

// Each vertex's 4 bone indices through the palette, once per byte offset; an index past it is counted.
inline void applyPalette(Run& r, const std::vector<uint32_t>& verts, const Stream& st, int pid,
                         const std::vector<int32_t>& pal)
{
    for (uint32_t v : verts) {
        uint32_t at = st.base + v * st.stride + st.off;
        if (auto [it, fresh] = r.claim.emplace(at, pid + 1); !fresh) continue;   // done by this palette already
        uint8_t* b = (uint8_t*)&r.vbuf[at];
        for (uint32_t c = 0; c < kBoneBytes; ++c) {
            if (b[c] < pal.size()) b[c] = (uint8_t)pal[b[c]];
            else ++r.outside;
        }
    }
}

// Primitive k of a node: its layout, palette, indices and stream checked, then its vertices remapped.
inline void remapPrim(Run& r, const std::vector<int32_t>& refs, const std::vector<int32_t>& pd, size_t k,
                      const Prim& p)
{
    auto lay = r.L.find((uint32_t)p.crc);
    if (lay == r.L.end() || !p.vdata || !p.idata) return;
    std::vector<int32_t> vd = r.f.ints(r.f.recs[p.vdata]), id = r.f.ints(r.f.recs[p.idata]);
    int s = lay->second.first;
    std::vector<int32_t> pal;
    if (!layoutUsable(vd, id, s) || !palette(refs, pd, k, pal)) {
        ++r.skipped;
        return;
    }
    int pid = paletteId(r, pal);
    Stream st{0, 0, 0, lay->second.second};
    bool clash = false;
    std::vector<uint32_t> verts;
    if (id[1] >= 0 && id[3] >= 0) {   // a negative first index or count is skipped
        st = {(uint32_t)vd[3 + 3 * s], (uint32_t)vd[4 + 3 * s], (uint32_t)vd[1], lay->second.second};
        if (streamFits(r, id, st)) verts = primVertices(r, id, st, pid, clash);
    }
    if (verts.empty()) {
        ++r.skipped;
        return;
    }
    if (clash) ownCopy(r, vd, s, p.vdata, st);
    applyPalette(r, verts, st, pid, pal);
    ++r.n;
}

}  // namespace detail

inline SkinLayouts skinLayouts(const std::string& t)
{
    SkinLayouts out;
    for (auto& v : vtxf::entries(t)) detail::formatLayout(t, v, out);
    return out;
}

// Over the 243 1.0 models 3.0 also ships: 830,242 bone slots equal 3.0's after the remap (skincheck2.py).
inline int skinRemap(oct::File& f, std::string& vbuf, const std::string& ibuf, const SkinLayouts& L, std::string* why)
{
    long pool = f.pool("SceneTreeNodePool");
    if (pool < 0 || std::find(f.names.begin(), f.names.end(), "InfluenceRemapInfluenceRefs") == f.names.end()) return 0;
    size_t end = f.blockEnd(pool);
    detail::Run r{f, vbuf, ibuf, L};
    for (size_t i = pool + 1; i < end; ++i) {
        if (f.recs[i].level != detail::kNodeLevel) continue;
        std::vector<int32_t> refs, pd;
        std::vector<detail::Prim> prims;
        detail::nodeRecords(f, i, refs, pd, prims);
        if (refs.empty() || pd.size() < 2 * prims.size()) continue;
        for (size_t k = 0; k < prims.size(); ++k) detail::remapPrim(r, refs, pd, k, prims[k]);
    }
    if (why && r.copies) *why += std::to_string(r.copies) + " primitives on their own stream copy; ";
    if (why && r.outside) *why += std::to_string(r.outside) + " bone indices outside their palette; ";
    if (why && r.skipped) *why += std::to_string(r.skipped) + " remapped primitives skipped (layout); ";
    return r.n;
}

}  // namespace di1::skin
