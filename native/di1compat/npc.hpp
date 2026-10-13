// NPC track: actors, navigation, AI. Load-time adaptations of 1.0 data into the 3.0 systems that replaced them.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "oct.hpp"

namespace npc {

// ---- NavigationMesh 'Data' blob, 1.0 -> 3.0 layout (OCT-8) ----
// Both readers are deserializers of the same runtime graph (regions, their vertices with corner links, region edges
// to neighbours); 1.0 nests and links by self-relative pointer, 3.0 flattens and links by u16 index.
// 1.0 reader FUN_011b9190 (dump10; size FUN_011b5550, relocation FUN_011b5a70: a non-zero pointer is an offset
// from the blob start): u32 nRegions, regions back to back, each
//   header 0x48: [0] type, [1] index, [2] agent mask, [3] [4] 0, [5] edges ptr, [6] nEdges, [7] nVerts, [8] verts
//     ptr, [9..b] centre, [c] radius, [d] f, [e..10] normal, [11] 0
//   nVerts x vertex 0x40: u32 0, u32 kind, u32 w, f, pos, v3, v3, f, nLinks, links ptr
//   nEdges x edge 0x50: u32, f, neighbour region ptr, then a vertex body (kind 4) at +0xc, u32 0 at +0x4c
//   link 0x2c: u32, 3 vertex ptrs (the first = its own vertex), u32 n, v3, v3
// 3.0 reader FUN_01909fb0: u32 nVerts, nRegions, nPortals, w3; nVerts x (vertex 0x34: u16 kind, u16 nLinks, u32 w,
//   f, pos, v3, v3, f; nLinks x link 0x24: u16 v0, v1, v2, n, u32, v3, v3); nRegions x (region 0x30: i16 type,
//   i16 nEdges, u32 mask, u32 index, f, normal, u32 nIdx, centre, radius; nEdges x edge 0x10: u32, f, u16 0, u16 0,
//   u16 neighbour region, u16 vertex; type 1: nIdx u16 vertex indices padded to 4 B); portals.
// Corpus (Python oracle _scratch/npc/nav.py): both walks end at the NUL-terminated mesh name in 750/750 1.0 and
// 5,358/5,358 3.0 blobs; 1.0 has only type-1 regions and no portals; every 1.0 link pointer is a vertex (51,776,
// the first is the link's own vertex), every edge pointer another region (22,906); edge bodies are kind 4 without
// links (22,906). Twins (worlds in both games): the converted mesh is geometry- and topology-identical to the
// shipped 3.0 one (actormovertest, meridiandatavartest x2; modelviewer except 32 corner links 3.0's exporter dropped).
// Mask: 3.0 sets bits 3 and 7 in 0 of 16,870 shipped regions; 1.0 -1 is 3.0 0xffffff77 in 453/453 twin regions
// (FUN_018f6a30 / FUN_018fd480 AND the mask with the agent's).
constexpr uint32_t NAV_MASK30 = 0xffffff77;

inline bool nav30(const uint8_t* b, size_t n, std::string& out, std::string* why = nullptr)
{
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; };
    auto in = [&](size_t o, size_t len) { return o <= n && len <= n - o; };
    if (!in(0, 4)) return fail("short");
    uint32_t nr = u32(0);
    if (nr > 0xffff) return fail("region count");
    std::vector<size_t> regOff, order;         // region headers; output vertex order (1.0 vertex bodies, 0x40 B)
    std::unordered_map<size_t, uint32_t> vidx, ridx;
    size_t o = 4;
    auto body = [&](size_t v) -> bool {        // a vertex body and its links lie inside the blob
        if (!in(v, 0x40)) return false;
        uint32_t nl = u32(v + 0x38), lp = u32(v + 0x3c);
        return nl == 0 || (nl < 0x10000 && in(lp, (size_t)nl * 0x2c));
    };
    for (uint32_t r = 0; r < nr; ++r) {
        if (!in(o, 0x48)) return fail("region header");
        if (u32(o) != 1) return fail("region type (1.0 ships type 1 only)");
        uint32_t ep = u32(o + 0x14), ne = u32(o + 0x18), nv = u32(o + 0x1c), vp = u32(o + 0x20);
        if (nv & 0x80000000u) return fail("vertex count flag");
        if ((ne && !in(ep, (size_t)ne * 0x50)) || (nv && !in(vp, (size_t)nv * 0x40))) return fail("region arrays");
        regOff.push_back(o);
        ridx[o] = r;
        size_t size = 0x48 + (size_t)nv * 0x40 + (size_t)ne * 0x50;
        for (uint32_t i = 0; i < nv; ++i) {
            size_t v = vp + 0x40 * i;
            if (!body(v)) return fail("vertex");
            vidx[v] = (uint32_t)order.size();
            order.push_back(v);
            size += (size_t)u32(v + 0x38) * 0x2c;
        }
        for (uint32_t i = 0; i < ne; ++i) {
            size_t v = ep + 0x50 * i + 0xc;
            if (!body(v)) return fail("edge");
            if (u32(v + 0x38)) return fail("edge with links");
            order.push_back(v);
        }
        o += size;
    }
    if (order.size() > 0xffff) return fail("vertex count");
    // the mesh name: the rest of the blob, NUL-terminated
    if (o >= n || b[n - 1] != 0) return fail("name");
    out.clear();
    auto put = [&](uint32_t v, int w) { out.append((const char*)&v, w); };
    auto copy = [&](size_t src, size_t len) { out.append((const char*)b + src, len); };
    put((uint32_t)order.size(), 4); put(nr, 4); put(0, 4); put(0, 4);
    for (size_t v : order) {
        uint32_t nl = u32(v + 0x38), lp = u32(v + 0x3c);
        put(u32(v + 4), 2); put(nl, 2); put(u32(v + 8), 4);
        copy(v + 0xc, 0x2c);                                   // f, pos, v3, v3, f
        for (uint32_t k = 0; k < nl; ++k) {
            size_t l = lp + 0x2c * k;
            for (int j = 0; j < 3; ++j) {
                auto it = vidx.find(u32(l + 4 + 4 * j));
                if (it == vidx.end()) return fail("link target");
                put(it->second, 2);
            }
            put(u32(l + 0x10), 2); put(u32(l), 4);
            copy(l + 0x14, 0x18);
        }
    }
    uint32_t vi = 0;   // edge bodies follow each region's vertices in 'order'
    for (uint32_t r = 0; r < nr; ++r) {
        size_t h = regOff[r];
        uint32_t ep = u32(h + 0x14), ne = u32(h + 0x18), nv = u32(h + 0x1c), vp = u32(h + 0x20);
        put(1, 2); put(ne, 2); put(u32(h + 8) & NAV_MASK30, 4); put(u32(h + 4), 4);
        copy(h + 0x34, 0x10);                                  // f, normal
        put(nv, 4);
        copy(h + 0x24, 0x10);                                  // centre, radius
        vi += nv;
        for (uint32_t i = 0; i < ne; ++i) {
            size_t e = ep + 0x50 * i;
            auto it = ridx.find(u32(e + 8));
            if (it == ridx.end()) return fail("edge neighbour");
            copy(e, 8);
            put(u32(e + 0x4c), 4);
            put(it->second, 2); put(vi++, 2);
        }
        for (uint32_t i = 0; i < nv; ++i) put(vidx[vp + 0x40 * i], 2);
        if (nv & 1) put(0, 2);
    }
    copy(o, n - o);
    return true;
}

// Every NavMeshPool > NavigationMesh > Data blob of an .oct into the 3.0 layout. Returns the meshes converted, or -1
// (nothing changed) if one of them does not walk as a 1.0 mesh.
inline int navmesh30(oct::File& f, std::string* why = nullptr)
{
    long p = f.pool("NavMeshPool");
    if (p < 0) return 0;
    size_t end = f.blockEnd(p);
    std::vector<std::pair<size_t, std::string>> done;
    for (size_t i = p + 1; i < end; ++i) {
        oct::Rec& r = f.recs[i];
        oct::Spec s = oct::spec(r.type);
        if (f.name(r) != "Data" || s.kind != oct::BYTE || s.shape != oct::SCALAR || s.keyed) continue;
        const uint8_t* q = (const uint8_t*)r.pay.data();
        size_t len = oct::u(q, s.cw);
        if (s.cw + len != r.pay.size()) { if (why) *why = "blob size"; return -1; }
        std::string conv, w;
        if (!nav30(q + s.cw, len, conv, &w)) { if (why) *why = "mesh " + std::to_string(done.size()) + ": " + w; return -1; }
        done.emplace_back(i, std::move(conv));
    }
    for (auto& [i, conv] : done) {
        oct::Rec& r = f.recs[i];
        int cw = conv.size() < 0x100 ? 1 : conv.size() < 0x10000 ? 2 : 3;   // the exporter's narrowest count width
        r.type = (uint16_t)(oct::SCALAR | oct::BYTE << 3 | (cw - 1) << 6);
        r.pay.clear();
        oct::File::put(r.pay, (uint32_t)conv.size(), cw);
        r.pay += conv;
    }
    return (int)done.size();
}

}  // namespace npc
