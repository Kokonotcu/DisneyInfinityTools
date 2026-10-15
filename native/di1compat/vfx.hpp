// vfx.hpp: 1.0 particle data in 3.0 form, at load time (research: _research\impl-vfx.md; oracle: particles.py).
//
// Particle package <zone>.particledata. Both engines keep the file and use its records in place (1.0 FUN_006ef500,
// 3.0 FUN_00672410): [records][keys: n x 8 B][index tables, backwards from the trailer][trailer].
//   trailer 1.0 FUN_006c81e0: end-0x14, u16 0x123, u16 12, u32 size, u32 key offset, u16 nkeys, u16 emitters,
//           u16 effects, u16 params;   3.0 FUN_00656bd0: end-0x18, version 0x2f, + u16 n14, u16 n16 (3.0-only kinds)
//   3.0 rejects any other trailer, so the raw 1.0 IN_City package registered nothing (log 2026-10-13: served raw).
//   index 1.0 FUN_006ef350 / 3.0 FUN_00672120: emitters 16 B (off, hash, 1.0 u16 pool + u16 material + u32 x |
//           3.0 u8 class + u8 0 + u16 material + u32 c), effects 8 B, params 8 B (off, hash).
//   records: effect (emitter list; tick 1.0 FUN_006de3d0 = 3.0 FUN_00675980) unchanged; emitter 0x108+2n -> 0x124+2n;
//           param 0x24 -> 0x34. Keys: 8 B curve keys, one decode in both (FUN_006c7210 / FUN_00662d20), indices
//           package-local (the loader adds the base) -> the table is copied as it is.
// Field map: emitter spawn 1.0 FUN_006dce20 vs 3.0 FUN_00673870, registration FUN_006ee910 vs FUN_00671830; checked
// on the 1,431 emitters / 373 params whose hash is in a 1.0 and a 3.0 package (Disney's own port of those effects).
//
// Mesh particles: 1.0 names the mesh in the emitter (char[8] at +0xf8, FUN_006e9270 'particles/<name>'), 3.0 by the
// x131 hash of it (+0x11c, FUN_00670760 'particles/%08x'). .p3d: 3.0 vertices are 12 B (FUN_006634b0 reads
// count x 0xc), 1.0's 8 B + 4 B colour (27 twin meshes: first 8 B and indices identical in 25).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

namespace vfx {

inline uint32_t rd32(const std::string& s, size_t o) { uint32_t v; memcpy(&v, s.data() + o, 4); return v; }
inline uint16_t rd16(const std::string& s, size_t o) { uint16_t v; memcpy(&v, s.data() + o, 2); return v; }
inline void wr32(std::string& s, size_t o, uint32_t v) { memcpy(&s[o], &v, 4); }
inline void wr16(std::string& s, size_t o, uint16_t v) { memcpy(&s[o], &v, 2); }

// the engine's name hash (3.0 FUN_0040bb50 = 1.0 FUN_0040bac0): h = h*131 + signed char
inline uint32_t h131(const char* s, size_t n)
{
    uint32_t h = 0;
    for (size_t i = 0; i < n && s[i]; ++i) h = h * 131u + (uint32_t)(int32_t)(signed char)s[i];
    return h;
}

inline bool isPd10(const std::string& d)
{
    return d.size() >= 0x14 && rd16(d, d.size() - 0x14) == 0x123 && rd16(d, d.size() - 0x12) == 12;
}

// 1.0 entry x (0..63) -> 3.0 entry c, as Disney converted it (twin pairs x:c; absent x take the lower neighbour's c).
// Meaning unknown: 1.0 copies x into the emitter type (FUN_006dbfe0 +0x28), 3.0 c likewise (FUN_00666c80 +0x14).
inline uint32_t xc(uint32_t x)
{
    static const uint8_t k[][2] = {{1, 63}, {2, 63}, {3, 0}, {5, 0}, {6, 0}, {9, 1}, {10, 1}, {18, 3}, {20, 4}, {22, 4},
        {23, 5}, {26, 5}, {30, 6}, {31, 7}, {33, 7}, {34, 7}, {35, 8}, {39, 9}, {40, 9}, {41, 9}, {44, 10}, {46, 10},
        {47, 11}, {48, 11}, {49, 11}, {50, 11}, {51, 12}, {52, 12}, {55, 13}, {58, 13}, {59, 14}, {60, 14}, {61, 14},
        {62, 14}, {63, 0}};
    uint32_t c = 0;
    for (auto& e : k) if (e[0] <= x) c = e[1];
    return c;
}

// emitter record 1.0 (0x108 + 2n) -> 3.0 (0x124 + 2n, 4-aligned); chans: the 3.0 curve channels as 1.0 channel
// indices, -1 = the key constKey (chan.hpp; null: the 1.0 channels as they are)
inline std::string emitter30(const std::string& a, const std::vector<int>* chans = nullptr, uint16_t constKey = 0)
{
    std::string o(0x124, '\0');
    memcpy(&o[0], a.data(), 0xa4);              // life, box, direction + cone, speed, accel, rotation ranges
    memcpy(&o[0xb0], a.data() + 0xa4, 8);       // inherited-velocity range (x param_4[0x10..0x12] in both spawns)
    memcpy(&o[0xb8], a.data() + 0xa4, 8);       // 3.0's second velocity range: Disney fills it the same (1427/1431)
    memcpy(&o[0xc0], a.data() + 0xac, 8);       // gravity range
    const float one = 1.0f;
    memcpy(&o[0xc8], &one, 4); memcpy(&o[0xcc], &one, 4);   // 3.0-only range, 1.0 in every twin
    memcpy(&o[0xd8], a.data() + 0xb4, 0x28);    // five ranges (x the modifier slots 1.0 [10..14] / 3.0 [13..17])
    uint32_t dc = rd32(a, 0xdc), e0 = rd32(a, 0xe0);
    // 1.0 dc: 0-2 random sign of the rotation ranges, 3 type flag (desc +0xc), 4-11/12-19/20-27 modifier masks,
    // 28, 29 colour randoms lerped, 30 (desc +0x24); e0: 0-1 random sign of ranges 3/4, 2-3 type flags (desc +0xd/e),
    // 4-8/9-13/14-18 masks of the five ranges. 3.0 0x10000 = velocity in emitter space (1.0 always rotates it),
    // 0x20000 clear = acceleration in world space (1.0 never rotates it), 0x40000000 as in every twin.
    uint32_t f = (dc & 7) | (dc >> 3 & 1) << 4 | (e0 & 0xf) << 8 | 0x10000u | (dc >> 28 & 1) << 18 | (dc >> 29 & 1) << 20 |
                 (dc >> 30 & 1) << 21 | 0x40000000u;
    wr32(o, 0x100, f);
    wr16(o, 0x104, (uint16_t)((dc >> 4 & 0xff) | (e0 >> 4 & 0x1f) << 9));
    wr16(o, 0x106, (uint16_t)((dc >> 12 & 0xff) | (dc >> 20 & 0xff) | ((e0 >> 9 & 0x1f) | (e0 >> 14 & 0x1f)) << 9));
    memcpy(&o[0x108], a.data() + 0xe4, 8);      // +0xe4 (spawn's last argument) and +0xe8 child emitter hash
    for (int i = 0; i < 6; ++i)                 // colour randoms: 1.0 v/1023, 3.0 v/1023 - 32.03 (Disney: 0x8000 + v/2)
        wr16(o, 0x110 + 2 * i, (uint16_t)(0x8000 + rd16(a, 0xec + 2 * i) / 2));
    wr32(o, 0x11c, a[0xf8] ? h131(a.data() + 0xf8, 8) : 0);
    uint8_t n = (uint8_t)a[0x105];
    if (!chans) {
        o[0x121] = (char)n;
        o.append(a, 0x108, 2 * (size_t)n);
    } else {
        o[0x121] = (char)chans->size();
        for (int c : *chans) {
            uint16_t k = c >= 0 && c < n ? rd16(a, 0x108 + 2 * (size_t)c) : constKey;
            o.append((const char*)&k, 2);
        }
    }
    o.resize((o.size() + 3) & ~(size_t)3, '\0');
    return o;
}

// The constant 1.0 curve key (cubic 1, 1, 1, 1, linear time, clamp 0..1): Disney's key in channels a 1.0 emitter
// had no curve for (chan.hpp)
inline const char* constKey() { return "\xff\x1f\x1f\xff\x55\xaa\x00\xff"; }

// chanMap(material, 1.0 channel count, out): the 3.0 channels of an emitter drawn with that 1.0 material; false =
// keep the 1.0 channels
using ChanMap = std::function<bool(uint16_t material, int n10, std::vector<int>& chans)>;

// param record 1.0 (0x24) -> 3.0 (0x34)
inline std::string param30(const std::string& a)
{
    std::string o(0x34, '\0');
    memcpy(&o[0], a.data(), 0xe);               // 6 key indices, 2 bytes
    o[0xf] = 1;                                 // every twin
    memcpy(&o[0x10], a.data() + 0xe, 2);
    memcpy(&o[0x18], a.data() + 0x10, 0x10);
    uint32_t v = rd32(a, 0x20);
    o[0x30] = (char)((v & 1) ? 1 : (v & 6) ? 2 : 0);   // twins: 0->0 332, 1->1, 2->2, 4->2
    o[0x31] = 9;                                // 362/373 twins
    return o;
}

struct Stats { int emitters = 0, effects = 0, params = 0, keys = 0, meshes = 0, rechanneled = 0, constChans = 0; };

// 1.0 package -> 3.0 package; false (out untouched) if in is not a well-formed 1.0 package. chanMap: channel
// re-layout per emitter material (null: the 1.0 channels as they are)
inline bool transcode(const std::string& in, std::string& out, Stats* st = nullptr, const ChanMap* chanMap = nullptr)
{
    if (!isPd10(in)) return false;
    size_t t = in.size() - 0x14;
    uint32_t keys = rd32(in, t + 8);
    uint16_t nkeys = rd16(in, t + 0xc), ne = rd16(in, t + 0xe), nf = rd16(in, t + 0x10), np = rd16(in, t + 0x12);
    size_t tabs = t - (size_t)np * 8 - (size_t)nf * 8 - (size_t)ne * 16;
    if (tabs > t || (size_t)keys + 8u * nkeys != tabs) return false;
    size_t pe = tabs, pf = pe + (size_t)ne * 16, pp = pf + (size_t)nf * 8;
    struct Rec { uint32_t off; int kind; size_t entry; };   // kind 0 emitter, 1 effect, 2 param
    std::vector<Rec> recs;
    for (uint16_t i = 0; i < ne; ++i) recs.push_back({rd32(in, pe + 16 * i), 0, pe + 16 * (size_t)i});
    for (uint16_t i = 0; i < nf; ++i) recs.push_back({rd32(in, pf + 8 * i), 1, pf + 8 * (size_t)i});
    for (uint16_t i = 0; i < np; ++i) recs.push_back({rd32(in, pp + 8 * i), 2, pp + 8 * (size_t)i});
    std::stable_sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) { return x.off < y.off; });
    std::string body;
    std::vector<uint32_t> newOff(recs.size());
    Stats s;
    for (size_t i = 0; i < recs.size(); ++i) {
        uint32_t end = i + 1 < recs.size() ? recs[i + 1].off : keys;
        uint32_t need = recs[i].kind == 0 ? 0x108 : recs[i].kind == 2 ? 0x24 : 4;
        if (end < recs[i].off || end - recs[i].off < need) return false;
        std::string src = in.substr(recs[i].off, end - recs[i].off);
        if (recs[i].kind == 0 && 0x108 + 2 * (size_t)(uint8_t)src[0x105] > src.size()) return false;
        newOff[i] = (uint32_t)body.size();
        std::vector<int> chans;
        bool re = recs[i].kind == 0 && chanMap && *chanMap && (*chanMap)(rd16(in, recs[i].entry + 10), (uint8_t)src[0x105], chans);
        if (re) {
            ++s.rechanneled;
            for (int c : chans) s.constChans += c < 0;
        }
        if (recs[i].kind == 0) { body += emitter30(src, re ? &chans : nullptr, nkeys); ++s.emitters; s.meshes += src[0xf8] != 0; }
        else if (recs[i].kind == 2) { body += param30(src); ++s.params; }
        else { body += src; ++s.effects; }
        body.resize((body.size() + 3) & ~(size_t)3, '\0');
    }
    body.resize((body.size() + 7) & ~(size_t)7, '\0');
    uint32_t keys30 = (uint32_t)body.size();
    body.append(in, keys, 8 * (size_t)nkeys);
    uint16_t nkeys30 = nkeys;
    if (s.constChans) { body.append(constKey(), 8); ++nkeys30; }   // index nkeys (emitter30's constKey)
    std::string emit(16 * (size_t)ne, '\0'), eff(8 * (size_t)nf, '\0'), par(8 * (size_t)np, '\0');
    for (size_t i = 0; i < recs.size(); ++i) {
        size_t e = recs[i].entry;
        if (recs[i].kind == 0) {
            size_t j = (e - pe) / 16;
            wr32(emit, 16 * j, newOff[i]);
            wr32(emit, 16 * j + 4, rd32(in, e + 4));
            emit[16 * j + 8] = 1;                          // class: 1 in 1428/1431 twins (3 = no LOD thinning, FUN_00675980)
            wr16(emit, 16 * j + 10, rd16(in, e + 10));     // material index in the package's .mtb (order kept, render.hpp)
            wr32(emit, 16 * j + 12, xc(rd32(in, e + 12)));
        } else {
            std::string& tab = recs[i].kind == 1 ? eff : par;
            size_t j = (e - (recs[i].kind == 1 ? pf : pp)) / 8;
            wr32(tab, 8 * j, newOff[i]);
            wr32(tab, 8 * j + 4, rd32(in, e + 4));
        }
    }
    std::string tr(0x18, '\0');
    wr16(tr, 0, 0x123); wr16(tr, 2, 0x2f);
    wr32(tr, 4, keys30); wr32(tr, 8, keys30);
    wr16(tr, 0xc, nkeys30); wr16(tr, 0xe, ne); wr16(tr, 0x10, nf); wr16(tr, 0x12, np);
    out = body + emit + eff + par + tr;
    s.keys = nkeys30;
    if (st) *st = s;
    return true;
}

// .p3d mesh 1.0 (8 B vertices) -> 3.0 (12 B: + colour, white = no tint); false if not a 1.0 mesh. 3.0 reads u8
// indices (count +0x10) right after the vertices (FUN_006587e0 = +0x20 + 12 nv): all 65 1.0 meshes hold them.
inline bool p3d30(const std::string& in, std::string& out)
{
    if (in.size() < 0x20 || in.compare(0, 4, "P3D ") || rd32(in, 0x1c) != 8) return false;
    uint32_t nv = rd32(in, 0xc);
    if (0x20 + 8ull * nv + rd32(in, 0x10) > in.size()) return false;
    out = in.substr(0, 0x20);
    wr32(out, 0x1c, 12);
    for (uint32_t i = 0; i < nv; ++i) { out.append(in, 0x20 + 8 * (size_t)i, 8); out.append(4, '\xff'); }
    out.append(in, 0x20 + 8 * (size_t)nv, std::string::npos);
    return true;
}

// a 1.0 package member 'particles/<name>.p3d' -> the path 3.0 opens for it ('particles/%08x.p3d', lower case as
// the engine's paths are normalised), or "" for any other member
inline std::string p3dAlias(const std::string& member)
{
    if (member.compare(0, 10, "particles/") || member.size() < 15 || member.compare(member.size() - 4, 4, ".p3d")) return "";
    std::string name = member.substr(10, member.size() - 14);
    if (name.empty() || name.find('/') != std::string::npos || name.size() > 7) return "";   // char[8] in the emitter
    char buf[32];
    snprintf(buf, sizeof(buf), "particles/%08x.p3d", h131(name.data(), name.size()));
    return buf;
}

}  // namespace vfx
