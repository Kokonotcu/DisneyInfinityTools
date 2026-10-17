// OCT-8: 1.0 NavigationMesh 'Data' blobs in 3.0's layout, so 3.0's reader FUN_01909fb0 takes them. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../formats/oct.hpp"

namespace di1::navmesh {

// One blob: both games deserialise the same graph; 1.0 links by self-relative pointer, 3.0 by u16 index.
inline bool nav30(const uint8_t* b, size_t n, std::string& out, std::string* why = nullptr);
// Every NavMeshPool > NavigationMesh > Data blob -> meshes converted, or -1 (nothing changed) if one does not walk.
inline int navmesh30(oct::File& f, std::string* why = nullptr);

namespace detail {

// 1.0 (reader FUN_011b9190, pointers relocated from the blob start by FUN_011b5a70): records and fields.
constexpr size_t kRegion = 0x48;        // type, index, mask, 0, 0, edges, nEdges, nVerts, verts, centre, radius, ..
constexpr size_t kVertex = 0x40;        // 0, kind, w, f, pos, v3, v3, f, nLinks, links
constexpr size_t kEdge = 0x50;          // u32, f, neighbour region, a kind-4 vertex body at +0xc, u32 at +0x4c
constexpr size_t kLink = 0x2c;          // u32, 3 vertex pointers (the first its own vertex), n, v3, v3
constexpr size_t kRegIndex = 4, kRegMask = 8, kRegEdges = 0x14, kRegEdgeCount = 0x18, kRegVertCount = 0x1c;
constexpr size_t kRegVerts = 0x20, kRegCentre = 0x24, kRegNormal = 0x34;   // centre + radius, f + normal
constexpr size_t kRegVectors = 0x10;   // each of those two runs
constexpr size_t kVtxKind = 4, kVtxW = 8, kVtxBody = 0xc, kVtxBodySize = 0x2c, kVtxLinkCount = 0x38, kVtxLinks = 0x3c;
constexpr size_t kEdgeVertex = 0xc, kEdgeNeighbour = 8, kEdgeHead = 8, kEdgeTail = 0x4c;
constexpr size_t kLinkVerts = 4, kLinkCount = 0x10, kLinkVectors = 0x14, kLinkVectorsSize = 0x18;
constexpr uint32_t kMaxCount = 0xffff;        // 3.0 indexes regions and vertices by u16
constexpr uint32_t kMaxLinks = 0x10000;
constexpr uint32_t kVertexFlag = 0x80000000u;
// 3.0 sets bits 3 and 7 in none of 16,870 regions; 1.0's -1 is 0xffffff77 in 453/453 twins.
constexpr uint32_t NAV_MASK30 = 0xffffff77;

inline bool fail(std::string* why, const char* w)
{
    if (why) *why = w;
    return false;
}

// The 1.0 walk: region headers, every vertex body in output order, and the index of each by its offset.
struct Walk {
    const uint8_t* b;
    size_t n;
    std::vector<size_t> regOff, order;
    std::unordered_map<size_t, uint32_t> vidx, ridx;

    uint32_t u32(size_t o) const { return bytes::u32At(b + o); }
    // A vertex body and its links lie inside the blob.
    bool body(size_t v) const
    {
        if (!bytes::fits(n, v, kVertex)) return false;
        uint32_t nl = u32(v + kVtxLinkCount), lp = u32(v + kVtxLinks);
        return nl == 0 || (nl < kMaxLinks && bytes::fits(n, lp, (size_t)nl * kLink));
    }
};

// Region r at offset o: its vertices, then its edges' bodies, into the walk; o moves past it.
inline const char* walkRegion(Walk& w, uint32_t r, size_t& o)
{
    if (!bytes::fits(w.n, o, kRegion)) return "region header";
    if (w.u32(o) != 1) return "region type (1.0 ships type 1 only)";
    uint32_t ep = w.u32(o + kRegEdges), ne = w.u32(o + kRegEdgeCount), nv = w.u32(o + kRegVertCount);
    uint32_t vp = w.u32(o + kRegVerts);
    if (nv & kVertexFlag) return "vertex count flag";
    const bool edgesIn = !ne || bytes::fits(w.n, ep, (size_t)ne * kEdge);
    const bool vertsIn = !nv || bytes::fits(w.n, vp, (size_t)nv * kVertex);
    if (!edgesIn || !vertsIn) return "region arrays";
    w.regOff.push_back(o);
    w.ridx[o] = r;
    size_t size = kRegion + (size_t)nv * kVertex + (size_t)ne * kEdge;
    for (uint32_t i = 0; i < nv; ++i) {
        size_t v = vp + kVertex * i;
        if (!w.body(v)) return "vertex";
        w.vidx[v] = (uint32_t)w.order.size();
        w.order.push_back(v);
        size += (size_t)w.u32(v + kVtxLinkCount) * kLink;
    }
    for (uint32_t i = 0; i < ne; ++i) {
        size_t v = ep + kEdge * i + kEdgeVertex;
        if (!w.body(v)) return "edge";
        if (w.u32(v + kVtxLinkCount)) return "edge with links";
        w.order.push_back(v);
    }
    o += size;
    return nullptr;
}

inline void put(std::string& out, uint32_t v, int width) { out.append((const char*)&v, width); }

// A 3.0 link: u16 v0 v1 v2 n, u32, v3, v3; false when a vertex pointer names no vertex.
inline bool writeLink(const Walk& w, size_t l, std::string& out)
{
    for (size_t j = 0; j < 3; ++j) {
        auto it = w.vidx.find(w.u32(l + kLinkVerts + 4 * j));
        if (it == w.vidx.end()) return false;
        put(out, it->second, 2);
    }
    put(out, w.u32(l + kLinkCount), 2);
    put(out, w.u32(l), 4);
    out.append((const char*)w.b + l + kLinkVectors, kLinkVectorsSize);
    return true;
}

// 3.0 vertices: u16 kind, u16 nLinks, u32 w, the 0x2c body, then the links.
inline const char* writeVertices(const Walk& w, std::string& out)
{
    for (size_t v : w.order) {
        uint32_t nl = w.u32(v + kVtxLinkCount), lp = w.u32(v + kVtxLinks);
        put(out, w.u32(v + kVtxKind), 2);
        put(out, nl, 2);
        put(out, w.u32(v + kVtxW), 4);
        out.append((const char*)w.b + v + kVtxBody, kVtxBodySize);
        for (uint32_t k = 0; k < nl; ++k)
            if (!writeLink(w, lp + kLink * k, out)) return "link target";
    }
    return nullptr;
}

// 3.0 regions: i16 type 1, nEdges, u32 mask, index, f + normal, nIdx, centre + radius; edges; u16 indices padded.
inline const char* writeRegions(const Walk& w, std::string& out)
{
    uint32_t vi = 0;   // edge bodies follow each region's vertices in 'order'
    for (size_t h : w.regOff) {
        uint32_t ep = w.u32(h + kRegEdges), ne = w.u32(h + kRegEdgeCount), nv = w.u32(h + kRegVertCount);
        uint32_t vp = w.u32(h + kRegVerts);
        put(out, 1, 2);
        put(out, ne, 2);
        put(out, w.u32(h + kRegMask) & NAV_MASK30, 4);
        put(out, w.u32(h + kRegIndex), 4);
        out.append((const char*)w.b + h + kRegNormal, kRegVectors);
        put(out, nv, 4);
        out.append((const char*)w.b + h + kRegCentre, kRegVectors);
        vi += nv;
        for (uint32_t i = 0; i < ne; ++i) {
            size_t e = ep + kEdge * i;
            auto it = w.ridx.find(w.u32(e + kEdgeNeighbour));
            if (it == w.ridx.end()) return "edge neighbour";
            out.append((const char*)w.b + e, kEdgeHead);
            put(out, w.u32(e + kEdgeTail), 4);
            put(out, it->second, 2);
            put(out, vi++, 2);
        }
        for (uint32_t i = 0; i < nv; ++i) put(out, w.vidx.at(vp + kVertex * i), 2);
        if (nv & 1) put(out, 0, 2);
    }
    return nullptr;
}

}  // namespace detail

// Corpus (nav.py): 750/750 1.0 blobs walk; twins convert geometry- and topology-identical to 3.0's.
inline bool nav30(const uint8_t* b, size_t n, std::string& out, std::string* why)
{
    if (!bytes::fits(n, 0, 4)) return detail::fail(why, "short");
    detail::Walk w{b, n};
    uint32_t nr = w.u32(0);
    if (nr > detail::kMaxCount) return detail::fail(why, "region count");
    size_t o = 4;
    for (uint32_t r = 0; r < nr; ++r)
        if (const char* bad = detail::walkRegion(w, r, o)) return detail::fail(why, bad);
    if (w.order.size() > detail::kMaxCount) return detail::fail(why, "vertex count");
    if (o >= n || b[n - 1] != 0) return detail::fail(why, "name");   // the rest: the NUL-terminated mesh name
    out.clear();
    detail::put(out, (uint32_t)w.order.size(), 4);
    detail::put(out, nr, 4);
    detail::put(out, 0, 4);   // no portals; w3 0
    detail::put(out, 0, 4);
    if (const char* bad = detail::writeVertices(w, out)) return detail::fail(why, bad);
    if (const char* bad = detail::writeRegions(w, out)) return detail::fail(why, bad);
    out.append((const char*)b + o, n - o);
    return true;
}

inline int navmesh30(oct::File& f, std::string* why)
{
    long p = f.pool("NavMeshPool");
    if (p < 0) return 0;
    size_t end = f.blockEnd(p);
    std::vector<std::pair<size_t, std::string>> done;
    for (size_t i = p + 1; i < end; ++i) {
        oct::Rec& r = f.recs[i];
        oct::Spec s = oct::spec(r.type);
        const bool blob = s.kind == oct::BYTE && s.shape == oct::SCALAR && !s.keyed;
        if (f.name(r) != "Data" || !blob) continue;
        const uint8_t* q = (const uint8_t*)r.pay.data();
        size_t len = oct::u(q, s.cw);
        if (s.cw + len != r.pay.size()) {
            if (why) *why = "blob size";
            return -1;
        }
        std::string conv, w;
        if (!nav30(q + s.cw, len, conv, &w)) {
            if (why) *why = "mesh " + std::to_string(done.size()) + ": " + w;
            return -1;
        }
        done.emplace_back(i, std::move(conv));
    }
    for (auto& [i, conv] : done) {
        oct::Rec& r = f.recs[i];
        int cw = oct::countWidth(conv.size());   // the exporter's narrowest count width
        r.type = oct::makeType(oct::SCALAR, 0, oct::BYTE, cw);
        r.pay.clear();
        oct::File::put(r.pay, (uint32_t)conv.size(), cw);
        r.pay += conv;
    }
    return (int)done.size();
}

}  // namespace di1::navmesh
