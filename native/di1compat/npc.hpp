// NPC track: actors, navigation, AI. Load-time adaptations of 1.0 data into the 3.0 systems that replaced them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "oct.hpp"
#include "base/hash.hpp"
#include "formats/vtxf.hpp"
#include "luachunk.hpp"
#include "base/bytes.hpp"

namespace npc {

namespace detail {
// A primitive's vertex format CRC; an empty record is damaged input: hkOpen's catch logs it, serves the raw .oct.
inline int32_t vformatCrc(const oct::File& f, const oct::Rec& r)
{
    std::vector<int32_t> v = f.ints(r);
    if (v.empty()) throw std::runtime_error("skin10: vformatCRC record without a value");
    return v[0];
}
}  // namespace detail

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

// ---- skinned vertices: 1.0 InfluenceRemap palettes applied (REN-3) ----
// A 1.0 skinned geometry node carries NumInfluenceRemaps / InfluenceRemapInfluenceRefs / InfluenceRemapPrimitiveData
// (per primitive: start, count into the refs): a vertex's bone index k means influence refs[start + k]. 1.0 reads
// them (FUN_004c3350, dump10: NumInfluenceRemaps << 16 | NumInfluences into the node); 3.0 has none of the names and
// takes the vertex index as the influence index. Over the 243 1.0 models with remaps that 3.0 also ships (vertices
// the primitive's indices reach, matched by position, weight > 0; skincheck2.py): 3.0's index == refs[start + 1.0
// index] for 830,242 bone slots, the raw 1.0 index for 139,739; most of the 101,205 misses sit in the *_monster
// vehicles and pir_dutchman_crew_ranged, which 3.0 re-rigged.

// 1.0 vformat CRC -> (stream, byte offset) of its BLENDINDICES (U8_4 in all 18 1.0 formats that have one).
using SkinLayouts = std::unordered_map<uint32_t, std::pair<int, int>>;
inline SkinLayouts skinLayouts(const std::string& t)
{
    static const std::map<std::string, int> size = {{"FLOAT32_2", 8}, {"FLOAT32_3", 12}, {"FLOAT32_4", 16}, {"FLOAT16_2", 4},
        {"FLOAT16_4", 8}, {"U8_4_NORMALIZED", 4}, {"S16_4_NORMALIZED", 8}, {"U8_4", 4}, {"U16_2", 4}};
    SkinLayouts out;
    for (auto& v : vtxf::entries(t)) {
        size_t m = t.find("main", v.start), s = m == std::string::npos ? m : t.find("streams", m);
        if (s == std::string::npos || s > v.end) continue;
        size_t p = t.find('{', s);
        int depth = 0, stream = -1, off = 0;
        std::string sem, type;
        for (; p < v.end; ++p) {
            if (t[p] == '{') {
                ++depth;
                if (depth == 2) { ++stream; off = 0; }
                if (depth == 3) { sem.clear(); type.clear(); }
            } else if (t[p] == '}') {
                if (depth == 3) {
                    auto z = size.find(type);
                    if (z == size.end()) break;                         // an unknown type: no layout for this format
                    if (sem == "BLENDINDICES" && type == "U8_4") out[hash::ehashLower(v.guid)] = {stream, off};
                    off += z->second;
                }
                if (--depth == 0) break;
            } else if (depth == 3 && t.compare(p, 12, "semantic = \"") == 0) {
                size_t e = t.find('"', p + 12);
                sem = t.substr(p + 12, e - p - 12);
            } else if (depth == 3 && t.compare(p, 8, "type = \"") == 0) {
                size_t e = t.find('"', p + 8);
                type = t.substr(p + 8, e - p - 8);
            }
        }
    }
    return out;
}

// Rewrites the bone indices of every remapped skinned primitive in the 1.0 vertex buffer: each vertex the primitive's
// indices reach (Idata [0] buffer 0, [1] first index, [2] base vertex 0, [3] count; u16 _0.ibuf) gets its palette.
// The primitives of a node often share one vertex range with their own palettes (tb_mickeymouse: 8 primitives, one
// 6,153-vertex range); a vertex another palette already claimed sends the primitive to its own copy of the stream
// (appended to vbuf, its Vdata offset re-pointed; the render transcode copies each primitive's vertices apart anyway).
// Returns the primitives remapped; copies and indices outside their palette go to *why.
inline int skinRemap(oct::File& f, std::string& vbuf, const std::string& ibuf, const SkinLayouts& L, std::string* why = nullptr)
{
    long pool = f.pool("SceneTreeNodePool");
    if (pool < 0 || std::find(f.names.begin(), f.names.end(), "InfluenceRemapInfluenceRefs") == f.names.end()) return 0;
    size_t end = f.blockEnd(pool);
    std::vector<std::vector<int32_t>> pals;                   // palettes seen; claim value = index + 1
    std::unordered_map<uint32_t, int> claim;                   // byte offset of a vertex's bone indices -> palette
    int n = 0, outside = 0, copies = 0, skipped = 0;
    for (size_t i = pool + 1; i < end; ++i) {
        if (f.recs[i].level != 2) continue;
        size_t ne = f.blockEnd(i);
        std::vector<int32_t> refs, pd;
        struct P { int32_t crc; size_t vdata = 0, idata = 0; };
        std::vector<P> prims;
        for (size_t j = i + 1; j < ne; ++j) {
            const std::string& nm = f.name(f.recs[j]);
            if (nm == "InfluenceRemapInfluenceRefs") refs = f.ints(f.recs[j]);
            else if (nm == "InfluenceRemapPrimitiveData") pd = f.ints(f.recs[j]);
            else if (nm == "vformatCRC") prims.push_back({detail::vformatCrc(f, f.recs[j])});
            else if (nm == "Vdata" && !prims.empty()) prims.back().vdata = j;
            else if (nm == "Idata" && !prims.empty()) prims.back().idata = j;
        }
        if (refs.empty() || pd.size() < 2 * prims.size()) continue;
        for (size_t k = 0; k < prims.size(); ++k) {
            auto lay = L.find((uint32_t)prims[k].crc);
            if (lay == L.end() || !prims[k].vdata || !prims[k].idata) continue;
            std::vector<int32_t> vd = f.ints(f.recs[prims[k].vdata]), id = f.ints(f.recs[prims[k].idata]);
            int s = lay->second.first, off = lay->second.second;
            if (vd.size() < 2 || vd[0] <= s || vd.size() < 2 + 3 * (uint64_t)vd[0] || vd[2 + 3 * s] != 0 ||   // [2+3s]: buffer file
                id.size() < 4 || id[0] != 0 || id[2] != 0) { ++skipped; continue; }
            int32_t start = pd[2 * k], cnt = pd[2 * k + 1];
            if (start < 0 || cnt < 0 || (size_t)start + cnt > refs.size()) { ++skipped; continue; }
            std::vector<int32_t> pal(refs.begin() + start, refs.begin() + start + cnt);
            int pid = 0;
            while (pid < (int)pals.size() && pals[pid] != pal) ++pid;
            if (pid == (int)pals.size()) pals.push_back(pal);
            if (id[1] < 0 || id[3] < 0) { ++skipped; continue; }   // a negative first index or count
            uint32_t base = (uint32_t)vd[3 + 3 * s], stride = (uint32_t)vd[4 + 3 * s], count = (uint32_t)vd[1];
            if ((uint64_t)base + (uint64_t)stride * count > vbuf.size() || stride < (uint32_t)off + 4 ||
                (uint64_t)2 * ((uint64_t)id[1] + id[3]) > ibuf.size()) { ++skipped; continue; }
            std::vector<uint32_t> verts;
            bool clash = false;
            for (int32_t q = 0; q < id[3]; ++q) {
                uint32_t v = (uint8_t)ibuf[2 * (id[1] + q)] | (uint32_t)(uint8_t)ibuf[2 * (id[1] + q) + 1] << 8;
                if (v >= count) { verts.clear(); break; }
                verts.push_back(v);
                auto c = claim.find(base + v * stride + off);
                if (c != claim.end() && c->second != pid + 1) clash = true;
            }
            if (verts.empty()) { ++skipped; continue; }
            if (clash) {                                       // its own copy of the stream
                uint32_t nb = (uint32_t)((vbuf.size() + 15) & ~(size_t)15);
                vbuf.resize(nb, '\0');
                vbuf.append(vbuf, base, (size_t)stride * count);
                vd[3 + 3 * s] = (int32_t)nb;
                f.setInts(f.recs[prims[k].vdata], vd);
                base = nb;
                ++copies;
            }
            for (uint32_t v : verts) {
                uint32_t at = base + v * stride + off;
                if (!claim.emplace(at, pid + 1).second) continue;   // done by this palette already
                uint8_t* b = (uint8_t*)&vbuf[at];
                for (int c = 0; c < 4; ++c) {
                    if (b[c] < pal.size()) b[c] = (uint8_t)pal[b[c]];
                    else ++outside;
                }
            }
            ++n;
        }
    }
    if (why && copies) *why += std::to_string(copies) + " primitives on their own stream copy; ";
    if (why && outside) *why += std::to_string(outside) + " bone indices outside their palette; ";
    if (why && skipped) *why += std::to_string(skipped) + " remapped primitives skipped (layout); ";
    return n;
}

// ---- DataMaps: the 3.0 GameDB tables a 1.0 DataMap cannot name ----
// A DataMap chunk is one Lua 5.1 function of NEWTABLE / LOADK / SETTABLE / SETLIST / SETGLOBAL / RETURN only (all 16
// shipped DataMaps of both games, luaops.py), so it is evaluated here without a Lua state. Both avatar.btx add only
// LOADBOOL.
struct LVal { char t = 0; std::string s; double n = 0; int tab = -1; };   // t: 0 nil, 's', 'n', 'b', 't'
struct LTab { std::vector<std::pair<LVal, LVal>> kv; };

inline bool luaEval(const std::string& b, std::vector<LTab>& tabs, std::vector<std::pair<std::string, LVal>>& globals,
                    std::string* why = nullptr)
{
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    static const uint8_t HDR[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
    if (b.size() < 12 || memcmp(b.data(), HDR, 12)) return fail("not a Lua 5.1 x86 chunk");
    size_t at = 12;
    auto u32 = [&](uint32_t& v) { if (at + 4 > b.size()) return false; memcpy(&v, b.data() + at, 4); at += 4; return true; };
    uint32_t n, code0, ncode;
    if (!u32(n) || !bytes::fits(b.size(), at, n) || !bytes::fits(b.size(), at + n, 12)) return fail("header");
    at += n + 8;
    uint8_t maxstack = (uint8_t)b[at + 3];
    at += 4;
    if (!u32(ncode) || ncode > b.size() / 4 || !bytes::fits(b.size(), at, (size_t)ncode * 4)) return fail("code");
    code0 = (uint32_t)at;
    at += (size_t)ncode * 4;
    std::vector<LVal> K;
    uint32_t nk;
    if (!u32(nk)) return fail("constants");
    for (uint32_t i = 0; i < nk; ++i) {
        if (at >= b.size()) return fail("constants");
        LVal v;
        uint8_t t = (uint8_t)b[at++];
        if (t == 1) { v.t = 'b'; v.n = b[at++] != 0; }
        else if (t == 3) { v.t = 'n'; if (at + 8 > b.size()) return fail("number"); memcpy(&v.n, b.data() + at, 8); at += 8; }
        else if (t == 4) {
            uint32_t len;
            if (!u32(len) || !bytes::fits(b.size(), at, len)) return fail("string");
            v.t = 's';
            v.s.assign(b, at, len ? len - 1 : 0);
            at += len;
        } else if (t != 0) return fail("constant type");
        K.push_back(std::move(v));
    }
    std::vector<LVal> R(maxstack + 1);
    auto set = [&](int tab, const LVal& k, const LVal& v) {
        for (auto& e : tabs[tab].kv)
            if (e.first.t == k.t && e.first.s == k.s && e.first.n == k.n) { e.second = v; return; }
        tabs[tab].kv.emplace_back(k, v);
    };
    for (uint32_t pc = 0; pc < ncode; ++pc) {
        uint32_t i;
        memcpy(&i, b.data() + code0 + 4 * pc, 4);
        uint32_t op = i & 0x3f, A = (i >> 6) & 0xff, C = (i >> 14) & 0x1ff, B = i >> 23, Bx = i >> 14;
        auto rk = [&](uint32_t x) -> const LVal& { return x & 0x100 ? K.at(x & 0xff) : R.at(x); };
        if (A >= R.size()) return fail("register");
        if (op == 1) R[A] = K.at(Bx);                                                   // LOADK
        else if (op == 2) { R[A] = LVal{'b', "", (double)(B != 0)}; if (C) ++pc; }      // LOADBOOL (.btx)
        else if (op == 10) { R[A] = LVal{'t', "", 0, (int)tabs.size()}; tabs.emplace_back(); }   // NEWTABLE
        else if (op == 9) {                                                              // SETTABLE
            if (R[A].t != 't') return fail("SETTABLE on a non-table");
            set(R[A].tab, rk(B), rk(C));
        } else if (op == 34) {                                                           // SETLIST (50 per flush)
            if (R[A].t != 't' || B == 0) return fail("SETLIST form");
            if (C == 0 && ++pc < ncode) memcpy(&C, b.data() + code0 + 4 * pc, 4);
            for (uint32_t j = 1; j <= B; ++j) set(R[A].tab, LVal{'n', "", (double)((C - 1) * 50 + j)}, R.at(A + j));
        } else if (op == 7) globals.emplace_back(K.at(Bx).s, R[A]);                     // SETGLOBAL
        else if (op == 30) return true;                                                  // RETURN
        else return fail("opcode outside the constructor subset");
    }
    return fail("no RETURN");
}

// DataMap.<Group>.<Name> = { Files = {...}, Includes = {"<name>|<Group>", ...} }; group and name lower-case (the engine
// compares them case-insensitively, FUN_006fd430 / FUN_00705860). A repeated key keeps its last value, as in Lua.
struct DmRec { std::vector<std::string> files, includes; };
using DataMap = std::map<std::pair<std::string, std::string>, DmRec>;

inline std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

inline bool dataMap(const std::string& chunk, DataMap& out, std::string* why = nullptr)
{
    std::vector<LTab> tabs;
    std::vector<std::pair<std::string, LVal>> g;
    if (!luaEval(chunk, tabs, g, why)) return false;
    const LVal* dm = nullptr;
    for (auto& [k, v] : g) if (k == "DataMap" && v.t == 't') dm = &v;
    if (!dm) { if (why) *why = "no DataMap table"; return false; }
    auto strings = [&](const LVal& t, std::vector<std::string>& to) {
        if (t.t != 't') return;
        for (auto& [k, v] : tabs[t.tab].kv) if (v.t == 's') to.push_back(v.s);
    };
    for (auto& [gk, gv] : tabs[dm->tab].kv) {
        if (gk.t != 's' || gv.t != 't') continue;
        for (auto& [nk, nv] : tabs[gv.tab].kv) {
            if (nk.t != 's' || nv.t != 't') continue;
            DmRec r;
            for (auto& [fk, fv] : tabs[nv.tab].kv) {
                if (fk.s == "Files") strings(fv, r.files);
                else if (fk.s == "Includes") strings(fv, r.includes);
            }
            out[{lower(gk.s), lower(nk.s)}] = std::move(r);
        }
    }
    return true;
}

// 'GameDB|<path>|1|<type>' -> (type, path)
inline bool gamedbEntry(const std::string& e, std::pair<std::string, std::string>& tp)
{
    if (e.compare(0, 7, "GameDB|")) return false;
    size_t a = e.find('|', 7), b = a == std::string::npos ? a : e.find('|', a + 1);
    if (b == std::string::npos) return false;
    tp = {e.substr(b + 1), e.substr(7, a - 7)};
    return true;
}

inline std::string dnaOf(const DmRec& r)
{
    for (auto& f : r.files)
        if (!f.compare(0, 4, "DNA|")) {
            std::string p = lower(f.substr(4));
            for (char& c : p) if (c == '\\') c = '/';
            if (p.size() > 5 && !p.compare(p.size() - 5, 5, ".dnax")) p.resize(p.size() - 5);
            return p;
        }
    return "";
}

// Records reachable from (group, name) through Includes ("<name>|<Group>"), the engine's own expansion (FUN_007058c0).
inline void reach(const DataMap& m, const std::pair<std::string, std::string>& k, std::set<std::pair<std::string, std::string>>& seen)
{
    auto it = m.find(k);
    if (it == m.end() || !seen.insert(k).second) return;
    for (auto& inc : it->second.includes) {
        size_t bar = inc.find('|');
        reach(m, {bar == std::string::npos ? "include" : lower(inc.substr(bar + 1)), lower(inc.substr(0, bar))}, seen);
    }
}

// What the engine cannot load for a 1.0 DataMap:
// - a 1.0 DataMap has no Playset group (7/7; all 9 3.0 DataMaps have one), and 3.0 includes DataMap.Playset.<playset>
//   for every playset (FUN_0070d9b0 -> FUN_0070d730(name, 0)): the 3.0 playset tables -- CarryStyles, SizeClassifications,
//   DecisionsInfo, DifficultyInfo, Munitions, SurfaceImpacts -- never load. Taken from ToyBox's Playset.toybox (the 3.0
//   playset that hosts the 1.0 characters) for every GameDB type the 1.0 DataMap never names, except assetDefs (served
//   by the asset defs fallback from gamedb/incredibles/assetdefs.lua) and RumpusSpawnData (the Toy Box spawn map: 1 of
//   the 9 3.0 Playset closures; sizeclassifications/decisionsInfo/difficultyInfo 9, carrystyles/munitions 8,
//   surfaceImpacts 6; pstypes.py).
// - a 1.0 Actor record carries no GameDB entries, while the 3.0 DNA served for it reads 3.0 tables: emuInfo by its
//   Progression Hierarchy, toolset by its ToolSet (68 of 70 3.0 *_ps DNA: every Hierarchy name has its
//   emuinfo_<name>.lua among the GameDB entries of the ToyBox record with the same DNA minus _ps; psdna.py). Per 1.0
//   Actor: the GameDB entries reachable from the 3.0 ToyBox Actor record whose DNA is the 1.0 record's DNA (or it
//   without a trailing _ps; several records: the smallest name), types the 1.0 DataMap names excluded.
struct GameDb {
    std::vector<std::pair<std::string, std::string>> playset;                          // (type, path)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> actor;     // 1.0 actor -> (type, path)
    DataMap m10;
};

inline GameDb gamedb30(const DataMap& m10, const DataMap& m30)
{
    GameDb g;
    g.m10 = m10;
    std::set<std::string> types10;
    std::pair<std::string, std::string> tp;
    for (auto& [k, r] : m10) for (auto& f : r.files) if (gamedbEntry(f, tp)) types10.insert(tp.first);
    if (auto pt = m30.find({"playset", "toybox"}); pt != m30.end())
        for (auto& f : pt->second.files)
            if (gamedbEntry(f, tp) && !types10.count(tp.first) && tp.first != "assetDefs" && tp.first != "RumpusSpawnData")
                g.playset.push_back(tp);
    std::map<std::string, std::string> byDna;   // DataMap is ordered: the first record per DNA has the smallest name
    for (auto& [k, r] : m30) {
        std::string d = k.first == "actor" ? dnaOf(r) : "";
        if (!d.empty()) byDna.emplace(d, k.second);
    }
    for (auto& [k, r] : m10) {
        std::string d = k.first == "actor" ? dnaOf(r) : "";
        if (d.empty()) continue;
        auto hit = byDna.find(d);
        if (hit == byDna.end() && d.size() > 3 && !d.compare(d.size() - 3, 3, "_ps")) hit = byDna.find(d.substr(0, d.size() - 3));
        if (hit == byDna.end()) continue;
        std::set<std::pair<std::string, std::string>> seen;
        reach(m30, {"actor", hit->second}, seen);
        std::set<std::pair<std::string, std::string>> got;
        for (auto& rk : seen)
            for (auto& f : m30.at(rk).files)
                if (gamedbEntry(f, tp) && !types10.count(tp.first) && got.insert(tp).second) g.actor[k.second].push_back(tp);
    }
    return g;
}

// ---- playset30: the Playset record a 1.0 DataMap lacks, as one more DataMap (Lua text) ----
// Every 3.0 playset's Playset record includes avatar.auto and avatar.btx.auto (9 of 9 3.0 DataMaps), and outside Toy Box
// includeforallnontoyboxplaysets (8 of 8), which includes currencycache (psreach.py): the avatar behaviour scripts and
// combat Chos (com_blockimpact, mba_default_impact, default_punch_big ...), its AnimTreeProject / BehaviorMap, the
// 'Common' mission glob, MissionLog screen, mission alert UI, the overhead health Cho, the spark/health currency caches.
// avatar.btx.auto, currencycache and includeforallnontoyboxplaysets are identical in every map that defines them;
// avatar.auto differs per playset (8 variants, autocmp.py) and comes from Toy Box, the 3.0 playset hosting the 1.0
// characters. The record also lists the gamedb30 playset tables. 3.0 reads GameDB Lua text as well as bytecode: it
// ships its own stats.lua, bitmasks.lua, zonelist.lua as text, and the DataMap loader is the same
// GameDB::DBFileLoader_Callback (0008.c:28225).
// hudmarker10: the mission compass. 1.0 drew it as the Cho UI_Player_compass, loaded for every playset by its
// CoreDataMap (_loadforallplaysets_), which a 3.0 session never reads. 3.0's compass is the hud_Marker screen: the
// avatar's MissionNavData opens it on CheckCompass (FUN_00be40d0 -> FUN_00be3700, event HudMarker_Event ->
// hud_Marker in 3.0's eventtoscreenmap.lua) and its update FUN_00c13c50 draws the focus-point arrow + distance only
// once that screen exists. Screen|hud_Marker is in Include.hudmarker_inc (30 files, identical in the 6 3.0 maps that
// define it), listed by every mission playset's own include record (Empire, InsideOut, PlaysetX, PlaysetZ,
// TheCloneWars <playset>playsetincludes; ToyBox inc_tbgamemode_all), which playset30 never copies: no compass in IN.
// Taken from Toy Box like avatar.auto (Dory, the 'other' map, has none: Dory / Moana / BattleArena don't list it).
inline std::string luaQuote(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += (char)c; }
        else if (c < 0x20 || c > 0x7e) { char b[8]; sprintf_s(b, "\\%03u", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

// rewards10: 1.0 loads CommonExpendables / ToyBuildingsExpendables / Tools / Enemy_Expendables for every playset from
// its CoreDataMap's Zone._core_ record, which a 3.0 session never reads (3.0's own Zone._core_ loads its tools.lua, each
// 3.0 playset its package from its include record, e.g. 'Expendables|expendables_data/dory.lua'). CommonExpendables
// holds SPARK, the spark every 1.0 economy group spawns. The record's 'Expendables|' entries; the caller keeps those
// 3.0 does not ship and puts them in the Playset record (playsetText's files), where 3.0 lists a playset's packages.
inline std::vector<std::string> coreExpendables10(const DataMap& core10)
{
    auto r = core10.find({"zone", "_core_"});
    std::vector<std::string> out;
    if (r != core10.end())
        for (auto& f : r->second.files)
            if (!f.compare(0, 12, "Expendables|")) out.push_back(f);
    return out;
}

// files: more entries for the record as they are (rewards10: the 1.0 core expendables packages, coreExpendables10)
inline std::string playsetText(const std::string& playset, const std::vector<std::pair<std::string, std::string>>& tables,
                               const DataMap& toybox, const DataMap& other, bool hudMarker = true,
                               const std::vector<std::string>& files = {})
{
    static const char* const GROUPS[] = {"Playset", "GameMode", "Zone", "Actor", "Toy", "Activity", "ZoneBackgroundLoad",
                                         "Realm", "Munition", "SurfaceImpact", "Costume", "PowerDisc", "Include"};
    DataMap out;
    DmRec& ps = out[{"playset", lower(playset)}];
    for (auto& [t, p] : tables) ps.files.push_back("GameDB|" + p + "|1|" + t);
    ps.files.insert(ps.files.end(), files.begin(), files.end());
    std::pair<const DataMap*, const char*> std30[] = {{&toybox, "avatar.auto"}, {&toybox, "avatar.btx.auto"},
                                                       {&other, "includeforallnontoyboxplaysets"}, {&toybox, "hudmarker_inc"}};
    for (auto& [m, name] : std30) {
        if (!hudMarker && !strcmp(name, "hudmarker_inc")) continue;
        std::set<std::pair<std::string, std::string>> seen;
        reach(*m, {"include", name}, seen);
        if (!seen.empty()) ps.includes.push_back(std::string(name) + "|Include");
        for (auto& k : seen) out.emplace(k, m->at(k));
    }
    std::string t = "DataMap =\n{\n";
    for (const char* g : GROUPS) {
        std::string body;
        for (auto& [k, r] : out) {
            if (k.first != lower(g)) continue;
            body += "\t\t[" + luaQuote(k.second) + "] =\n\t\t{\n\t\t\tFiles =\n\t\t\t{\n";
            for (auto& f : r.files) body += "\t\t\t\t" + luaQuote(f) + ",\n";
            body += "\t\t\t},\n\t\t\tIncludes =\n\t\t\t{\n";
            for (auto& i : r.includes) body += "\t\t\t\t" + luaQuote(i) + ",\n";
            body += "\t\t\t},\n\t\t},\n";
        }
        if (!body.empty()) t += std::string("\t") + g + " =\n\t{\n" + body + "\t},\n";
    }
    return t + "}\n";
}

// ---- psdna30: a 1.0 playset avatar DNA (<x>_ps.dnax) that 3.0 ships only as <x>.dnax ----
// 3.0's own convention for a playset DNA is its base DNA naming the _PS model: of the 72 3.0 *_ps.dnax with a base,
// 63 differ from it only in EntityInstance.ModelName, 54 of them by '_PS' appended (psrule.py); inc_bob_ps vs inc_bob:
// 1 of 635 leaves. All 33 1.0 *_ps.dnax have a 3.0 base, one (inc_bob_ps) a 3.0 _ps. The 1.0 DNA itself cannot run on
// 3.0 (1.0 Combat schema: 237 of 511 fields absent; 1.0-only gadgets, research npcs-ai 3.2). Served: the 3.0 base with
// its one string constant c for which c + '_ps' is a constant of the 1.0 DNA (case-insensitive) replaced by that 1.0
// constant -- only when exactly one such constant exists and the code references it once (one leaf changes; 31 of 32
// such 1.0 DNA, the ModelName every time; psmodel.py).
inline bool luaStrings(const std::string& b, std::vector<std::string>& ks, std::vector<int>& refs)
{
    static const uint8_t HDR[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
    if (b.size() < 24 || memcmp(b.data(), HDR, 12)) return false;
    size_t at = 12;
    auto u32 = [&](uint32_t& v) { if (at + 4 > b.size()) return false; memcpy(&v, b.data() + at, 4); at += 4; return true; };
    uint32_t n, ncode, nk;
    if (!u32(n) || !bytes::fits(b.size(), at, n) || !bytes::fits(b.size(), at + n, 12)) return false;
    at += n + 12;
    if (!u32(ncode) || ncode > b.size() / 4 || !bytes::fits(b.size(), at, (size_t)ncode * 4)) return false;
    size_t code = at;
    at += (size_t)ncode * 4;
    if (!u32(nk)) return false;
    std::vector<int> kidx;                                     // constant index -> string index, -1 if not a string
    for (uint32_t i = 0; i < nk; ++i) {
        if (at >= b.size()) return false;
        uint8_t t = (uint8_t)b[at++];
        kidx.push_back(-1);
        if (t == 1) ++at;
        else if (t == 3) at += 8;
        else if (t == 4) {
            uint32_t len;
            if (!u32(len) || !bytes::fits(b.size(), at, len)) return false;
            kidx.back() = (int)ks.size();
            ks.emplace_back(b, at, len ? len - 1 : 0);
            at += len;
        } else if (t != 0) return false;
    }
    refs.assign(ks.size(), 0);
    auto ref = [&](uint32_t k) { if (k < kidx.size() && kidx[k] >= 0) ++refs[kidx[k]]; };
    for (uint32_t pc = 0; pc < ncode; ++pc) {
        uint32_t i;
        memcpy(&i, b.data() + code + 4 * pc, 4);
        uint32_t op = i & 0x3f, C = (i >> 14) & 0x1ff, B = i >> 23;
        if (op == 1 || op == 5 || op == 7) ref(i >> 14);                     // LOADK, GETGLOBAL, SETGLOBAL: Bx
        else if (op == 6 || op == 9 || (op >= 12 && op <= 17) || (op >= 23 && op <= 25) || op == 11) {
            if (B & 0x100) ref(B & 0xff);                                    // RK operands
            if (C & 0x100) ref(C & 0xff);
        }
    }
    return true;
}

inline bool psDna30(const std::string& dna10, const std::string& base30, std::string& out, std::string* why = nullptr)
{
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    std::vector<std::string> k10, k30;
    std::vector<int> r10, r30;
    if (!luaStrings(dna10, k10, r10) || !luaStrings(base30, k30, r30)) return fail("not a one-function Lua chunk");
    std::map<std::string, std::string> ps;                      // lower(1.0 constant) -> 1.0 constant
    for (auto& s : k10) ps.emplace(lower(s), s);
    int hit = -1, hits = 0;
    for (size_t i = 0; i < k30.size(); ++i)
        if (k30[i].find('/') != std::string::npos && ps.count(lower(k30[i]) + "_ps")) { hit = (int)i; ++hits; }
    if (hits != 1) return fail(hits ? "several model constants" : "no model constant");
    if (r30[hit] != 1) return fail("the model constant has several uses");
    const std::string from = k30[hit], to = ps[lower(from) + "_ps"];
    int n = luachunk::rewrite(base30, out, [&](std::string& s) { if (s != from) return false; s = to; return true; });
    if (n != 1) return fail("rewrite");
    if (why) *why = from + " -> " + to;
    return true;
}

// ---- avperc10: the 1.0 avatar's perception policy in a 1.0 playset ----
// A BehaviorTree modifier Apply_PerceptionMode lists perceptions as "Name" (always), {prefixes, ifMatch, ifNot
// [, "hostonly"]} (3.0 parser FUN_0154a470: the zone name, lower-case, against each ','/' '-separated prefix by strstr
// == 0; ifMatch when one matches, else ifNot; 0 = none) or {layer, "FullBody"}. 3.0 rewrote the avatar's standard list:
// 1.0 enables building lots, delivery boxes, decorations everywhere but adv_ zones (["adv_", 0, "BuildingCustomize"]), 3.0
// only in rumpus/tbx_ (["rumpus,tbx_", "BuildingCustomize", 0]); 1.0's in_-only RecruitHero / DismissHero /
// SummonHelicopter / Inc_Driving and AvatarChest are gone from it. In IN_City the 3.0 avatar (served as 3.0 DNA, psdna30)
// therefore runs none of them.
// A 3.0 list descends from the 1.0 list sharing the most zone-entry perception names (case-insensitive; unique, > 0): the
// 70-entry standard list from 1.0's 50-entry one (11 shared, next 7); the 25-entry list ties (8 / 8) and keeps 1.0's
// zone entries verbatim anyway. Additions = what the 1.0 list enables in the zone and the 3.0 list does not; 1.0's
// ActivityInitiate / OffAdvert / LockedAdvert are 3.0's Challenge* (giver handlers 1.0 FUN_00c0ee50, 3.0 FUN_00d3f0a0),
// already in the 3.0 list.
struct PercEntry { char kind = 0; std::string on, off, pre; bool host = false; };   // kind 'p' plain, 'z' zone entry
using PercList = std::vector<PercEntry>;
using PercAdds = std::vector<std::pair<std::string, bool>>;                           // name, hostonly

// Every distinct Perceptions list of a .btx chunk, in walk order (tables in insertion order, shared tables once).
inline bool percLists(const std::string& chunk, std::vector<PercList>& out, std::string* why = nullptr)
{
    std::vector<LTab> tabs;
    std::vector<std::pair<std::string, LVal>> g;
    if (!luaEval(chunk, tabs, g, why)) return false;
    std::set<int> seen;
    std::set<std::string> have;
    auto list = [&](const LTab& t) {
        PercList l;
        std::string key;
        for (auto& [k, v] : t.kv) {
            PercEntry e;
            if (v.t == 's') { e.kind = 'p'; e.on = v.s; }
            else if (v.t == 't') {
                const auto& kv = tabs[v.tab].kv;
                auto at = [&](double i) -> const LVal* { for (auto& [a, b] : kv) if (a.t == 'n' && a.n == i) return &b; return nullptr; };
                const LVal *p = at(1), *m = at(2), *n = at(3), *f = at(4);
                if (!p || !m || !n || p->t != 's') continue;                          // a layer {name, "FullBody"}
                e.kind = 'z';
                e.pre = p->s;
                if (m->t == 's') e.on = m->s;
                if (n->t == 's') e.off = n->s;
                e.host = f && f->t == 's' && lower(f->s) == "hostonly";
            } else continue;
            key += std::string(1, e.kind) + e.pre + '\x1f' + e.on + '\x1f' + e.off + (e.host ? "!" : "") + '\x1e';
            l.push_back(e);
        }
        if (have.insert(key).second) out.push_back(l);
    };
    std::function<void(int)> walk = [&](int t) {
        if (!seen.insert(t).second) return;
        for (auto& [k, v] : tabs[t].kv) {
            if (v.t != 't') continue;
            if (k.t == 's' && k.s == "Perceptions") list(tabs[v.tab]);
            walk(v.tab);
        }
    };
    for (auto& [k, v] : g) if (v.t == 't') walk(v.tab);
    return true;
}

// lower-case name -> (name, hostonly) a list enables in a zone (zone lower-case)
inline std::map<std::string, std::pair<std::string, bool>> percActive(const PercList& l, const std::string& zone)
{
    std::map<std::string, std::pair<std::string, bool>> out;
    for (auto& e : l) {
        if (e.kind == 'p') { out.emplace(lower(e.on), std::make_pair(e.on, false)); continue; }
        bool hit = false;
        for (size_t a = 0, b; a < e.pre.size() && !hit; a = b + 1) {
            b = e.pre.find_first_of(", ", a);
            if (b == std::string::npos) b = e.pre.size();
            if (b > a && !zone.compare(0, b - a, e.pre, a, b - a)) hit = true;   // the token as written
        }
        const std::string& n = hit ? e.on : e.off;
        if (!n.empty()) out.emplace(lower(n), std::make_pair(n, e.host));
    }
    return out;
}

// per 3.0 list: the 1.0 list it descends from (-1: none) and the additions for the zone
inline std::vector<std::pair<int, PercAdds>> percAdds(const std::vector<PercList>& l10, const std::vector<PercList>& l30, const std::string& zone)
{
    auto zset = [](const PercList& l) {
        std::set<std::string> s;
        for (auto& e : l) if (e.kind == 'z') { if (!e.on.empty()) s.insert(lower(e.on)); if (!e.off.empty()) s.insert(lower(e.off)); }
        return s;
    };
    std::vector<std::set<std::string>> z10;
    for (auto& l : l10) z10.push_back(zset(l));
    std::vector<std::pair<int, PercAdds>> out;
    std::string zl = lower(zone);
    for (auto& l : l30) {
        std::set<std::string> z = zset(l);
        int best = -1;
        size_t bs = 0, second = 0;
        for (size_t i = 0; i < l10.size(); ++i) {
            size_t s = 0;
            for (auto& n : z10[i]) s += z.count(n);
            if (s > bs) { second = bs; bs = s; best = (int)i; }
            else if (s > second) second = s;
        }
        if (bs == 0 || second == bs) { out.push_back({-1, {}}); continue; }
        auto a10 = percActive(l10[best], zl), a30 = percActive(l, zl);
        PercAdds add;
        for (auto& [k, v] : a10)   // 3.0 renamed Activity to Challenge (the giver handlers: gp challenge10)
            if (!a30.count(k) && !(k.compare(0, 8, "activity") == 0 && a30.count("challenge" + k.substr(8)))) add.push_back(v);
        out.push_back({best, add});
    }
    return out;
}

}  // namespace npc
