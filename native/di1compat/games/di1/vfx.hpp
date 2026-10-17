// 1.0 particle packages and meshes in 3.0's form, at load (_research\impl-vfx.md; oracle particles.py).
// Base and formats/particledata only; any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../base/hash.hpp"
#include "../../formats/particledata.hpp"

namespace vfx {

// Little-endian reads; the caller has checked the bytes are there
inline uint32_t rd32(const std::string& s, size_t o)
{
    uint32_t v;
    memcpy(&v, s.data() + o, sizeof v);
    return v;
}
inline uint16_t rd16(const std::string& s, size_t o)
{
    uint16_t v;
    memcpy(&v, s.data() + o, sizeof v);
    return v;
}

// 1.0's trailer (FUN_006c81e0), last 0x14 bytes: u16 0x123, u16 12, u32 size, u32 keys offset, then u16 counts
constexpr size_t kTrailer10 = 0x14;
constexpr uint16_t kVersion10 = 12;
constexpr size_t kTrailerKeys = 8, kTrailerCounts = 0xc;   // keys offset; key, emitter, effect, param counts
// 1.0's records: emitter 0x108 + 2 bytes per curve channel, param 0x24, effect its emitter list (both games)
constexpr size_t kEmitter10 = 0x108, kChannelCount10 = 0x105, kMeshName10 = 0xf8, kParam10 = 0x24, kEffectMin = 4;
// 1.0 emitter index entry: offset, hash, u16 pool, u16 material (+10), u32 type x (+12) (FUN_006ef350)
constexpr size_t kEntryHash = 4, kEntryMaterial = 10, kEntryType = 12;

inline bool isPd10(const std::string& d)
{
    if (d.size() < kTrailer10) return false;
    const size_t t = d.size() - kTrailer10;
    return rd16(d, t) == particledata::kMagic && rd16(d, t + 2) == kVersion10;
}

// Emitter type 1.0 x -> 3.0 c as in the twins, else the lower neighbour's (FUN_006dbfe0 +0x28, FUN_00666c80 +0x14)
inline uint32_t xc(uint32_t x)
{
    static constexpr uint8_t kTwins[][2] = {{1, 63}, {2, 63}, {3, 0}, {5, 0}, {6, 0}, {9, 1}, {10, 1}, {18, 3}, {20, 4},
        {22, 4}, {23, 5}, {26, 5}, {30, 6}, {31, 7}, {33, 7}, {34, 7}, {35, 8}, {39, 9}, {40, 9}, {41, 9}, {44, 10},
        {46, 10}, {47, 11}, {48, 11}, {49, 11}, {50, 11}, {51, 12}, {52, 12}, {55, 13}, {58, 13}, {59, 14}, {60, 14},
        {61, 14}, {62, 14}, {63, 0}};
    uint32_t c = 0;
    for (auto& e : kTwins)
        if (e[0] <= x) c = e[1];
    return c;
}

namespace detail {

// A span of an emitter copied as it is, 3.0 offset <- 1.0 offset (spawn FUN_006dce20 / FUN_00673870, 1,431 twins)
struct Span { size_t to, from, bytes; };
constexpr Span kEmitterSpans[] = {
    {0x00, 0x00, 0xa4},    // life, box, direction + cone, speed, accel, rotation ranges
    {0xb0, 0xa4, 8},       // inherited-velocity range
    {0xb8, 0xa4, 8},       // 3.0's second velocity range: Disney fills it the same (1427/1431)
    {0xc0, 0xac, 8},       // gravity range
    {0xd8, 0xb4, 0x28},    // five ranges with modifier slots (1.0 [10..14], 3.0 [13..17])
    {0x108, 0xe4, 8},      // spawn's last argument and the child emitter hash
};
constexpr size_t kOneRange30 = 0xc8;   // a 3.0-only range: 1.0f, 1.0f in every twin
constexpr size_t kFlags10 = 0xdc, kRangeFlags10 = 0xe0, kFlags30 = 0x100, kMasks30 = 0x104, kRangeMasks30 = 0x106;
constexpr size_t kColourRandoms10 = 0xec, kColourRandoms30 = 0x110, kColourRandoms = 6;
constexpr size_t kMeshHash30 = 0x11c;   // x131 hash of the mesh name ('particles/%08x', FUN_00670760)
constexpr size_t kMeshNameBytes = 8;
constexpr uint32_t kColourRandomBias = 0x8000;   // Disney's 3.0 value: 0x8000 + 1.0's / 2
// 3.0 flag bits: velocity in emitter space (1.0 always rotates it); one every twin has
constexpr uint32_t kVelocityInEmitter = 0x10000u, kTwinFlag = 0x40000000u;

// 1.0 dc: 0-2 rotation range signs, 3 type flag, 28-29 colour randoms lerped, 30 (desc +0x24); e0: 0-3 range flags
inline uint32_t flags30(uint32_t dc, uint32_t e0)
{
    return (dc & 7) | (dc >> 3 & 1) << 4 | (e0 & 0xf) << 8 | kVelocityInEmitter | (dc >> 28 & 1) << 18 |
           (dc >> 29 & 1) << 20 | (dc >> 30 & 1) << 21 | kTwinFlag;
}

// The modifier masks: 1.0 dc 4-11, 12-19, 20-27; e0 4-8, 9-13, 14-18 for the five ranges
inline void masks30(uint32_t dc, uint32_t e0, std::string& o)
{
    bytes::put16(o, kMasks30, (uint16_t)((dc >> 4 & 0xff) | (e0 >> 4 & 0x1f) << 9));
    const uint32_t ranges = (e0 >> 9 & 0x1f) | (e0 >> 14 & 0x1f);
    bytes::put16(o, kRangeMasks30, (uint16_t)((dc >> 12 & 0xff) | (dc >> 20 & 0xff) | ranges << 9));
}

// The curve channel keys: 1.0's as they are, or re-laid as chans says (-1: the constant key at constKeyIndex)
inline void channels30(const std::string& a, const std::vector<int>* chans, uint16_t constKeyIndex, std::string& o)
{
    const uint8_t n = (uint8_t)a[kChannelCount10];
    if (!chans) {
        o[particledata::kChannelCount] = (char)n;
        o.append(a, kEmitter10, 2 * (size_t)n);
        return;
    }
    o[particledata::kChannelCount] = (char)chans->size();
    // mapChannels gives -1 .. n-1: transcode appends the constant key exactly when some c < 0 (RE-40)
    for (int c : *chans) {
        uint16_t k = c >= 0 && c < n ? rd16(a, kEmitter10 + 2 * (size_t)c) : constKeyIndex;
        o.append((const char*)&k, sizeof k);
    }
}

}  // namespace detail

// Emitter record 1.0 -> 3.0, 4-aligned; chans: the 3.0 channels as 1.0 channel indices (chan.hpp), null: as they are
inline std::string emitter30(const std::string& a, const std::vector<int>* chans = nullptr, uint16_t constKeyIndex = 0)
{
    std::string o(particledata::kEmitterRecord, '\0');
    for (const detail::Span& s : detail::kEmitterSpans) memcpy(&o[s.to], a.data() + s.from, s.bytes);
    const float one = 1.0f;
    memcpy(&o[detail::kOneRange30], &one, sizeof one);
    memcpy(&o[detail::kOneRange30 + sizeof one], &one, sizeof one);
    const uint32_t dc = rd32(a, detail::kFlags10), e0 = rd32(a, detail::kRangeFlags10);
    bytes::put32(o, detail::kFlags30, detail::flags30(dc, e0));
    detail::masks30(dc, e0, o);
    for (size_t i = 0; i < detail::kColourRandoms; ++i) {   // 1.0 v/1023, 3.0 v/1023 - 32.03
        const uint16_t v = rd16(a, detail::kColourRandoms10 + 2 * i);
        bytes::put16(o, detail::kColourRandoms30 + 2 * i, (uint16_t)(detail::kColourRandomBias + v / 2));
    }
    // case kept, up to 8 chars; p3dAlias hashes a lower-cased name under 8: capitals would miss (RE-34)
    const uint32_t mesh = a[kMeshName10] ? hash::h131Signed(a.data() + kMeshName10, detail::kMeshNameBytes) : 0;
    bytes::put32(o, detail::kMeshHash30, mesh);
    detail::channels30(a, chans, constKeyIndex, o);
    o.resize(bytes::alignUp(o.size(), 4), '\0');
    return o;
}

// chanMap(material, 1.0 channel count, out): the 3.0 channels of an emitter drawn with that material; false: keep
using ChanMap = std::function<bool(uint16_t material, int n10, std::vector<int>& chans)>;

namespace detail {

// A param's spans copied as they are, 3.0 offset <- 1.0 offset (373 twin params)
constexpr Span kParamSpans[] = {{0, 0, 0xe}, {0x10, 0xe, 2}, {0x18, 0x10, 0x10}};   // six key indices first
constexpr size_t kParamOne30 = 0xf, kParamNine30 = 0x31;   // 1 in every twin; 9 in 362 of 373
constexpr size_t kParamMode10 = 0x20, kParamMode30 = 0x30;

}  // namespace detail

// Param record 1.0 (0x24) -> 3.0 (0x34)
inline std::string param30(const std::string& a)
{
    std::string o(particledata::kParamRecord, '\0');
    for (const detail::Span& s : detail::kParamSpans) memcpy(&o[s.to], a.data() + s.from, s.bytes);
    o[detail::kParamOne30] = 1;
    const uint32_t v = rd32(a, detail::kParamMode10);
    o[detail::kParamMode30] = (char)((v & 1) ? 1 : (v & 6) ? 2 : 0);   // twins: 0->0 332, 1->1, 2->2, 4->2
    o[detail::kParamNine30] = 9;
    return o;
}

struct Stats { int emitters = 0, effects = 0, params = 0, keys = 0, meshes = 0, rechanneled = 0, constChans = 0; };

namespace detail {

enum class Kind { Emitter, Effect, Param };
struct Rec { uint32_t off; Kind kind; size_t entry; };   // entry: its index entry's offset in the package

// The 1.0 package's trailer and index: its records in file order, each one's span ending before the keys
struct Index {
    uint32_t keys = 0;
    uint16_t nkeys = 0, ne = 0, nf = 0, np = 0;
    size_t pe = 0, pf = 0, pp = 0;   // the emitter, effect, param tables
    std::vector<Rec> recs;
};

inline std::optional<Index> readIndex(const std::string& in)
{
    Index x;
    const size_t t = in.size() - kTrailer10, c = t + kTrailerCounts;
    x.keys = rd32(in, t + kTrailerKeys);
    x.nkeys = rd16(in, c);
    x.ne = rd16(in, c + 2);
    x.nf = rd16(in, c + 4);
    x.np = rd16(in, c + 6);
    const size_t emitE = particledata::kEmitterEntry, e = particledata::kEntry;   // 1.0's entries are 3.0's sizes
    const size_t tabs = t - (size_t)x.np * e - (size_t)x.nf * e - (size_t)x.ne * emitE;
    if (tabs > t || x.keys > tabs || tabs - x.keys != particledata::kKeyBytes * x.nkeys) return std::nullopt;
    x.pe = tabs;
    x.pf = x.pe + (size_t)x.ne * emitE;
    x.pp = x.pf + (size_t)x.nf * e;
    for (size_t i = 0; i < x.ne; ++i) x.recs.push_back({rd32(in, x.pe + emitE * i), Kind::Emitter, x.pe + emitE * i});
    for (size_t i = 0; i < x.nf; ++i) x.recs.push_back({rd32(in, x.pf + e * i), Kind::Effect, x.pf + e * i});
    for (size_t i = 0; i < x.np; ++i) x.recs.push_back({rd32(in, x.pp + e * i), Kind::Param, x.pp + e * i});
    std::stable_sort(x.recs.begin(), x.recs.end(), [](const Rec& a, const Rec& b) { return a.off < b.off; });
    if (!x.recs.empty() && x.recs.back().off > x.keys) return std::nullopt;   // every span ends inside in (RE-22)
    return x;
}

// The package being converted: its bytes, its index, the channel re-layout (null: none)
struct Source { const std::string& in; const Index& x; const ChanMap* chanMap; };

// Record i, up to the next one, in 3.0's form appended to body; false: too short for its kind
inline bool convertRecord(const Source& pkg, size_t i, Stats& s, std::string& body)
{
    const std::string& in = pkg.in;
    const Index& x = pkg.x;
    const ChanMap* chanMap = pkg.chanMap;
    const Rec& r = x.recs[i];
    const uint32_t end = i + 1 < x.recs.size() ? x.recs[i + 1].off : x.keys;
    const uint32_t need = r.kind == Kind::Emitter ? kEmitter10 : r.kind == Kind::Param ? kParam10 : kEffectMin;
    if (end < r.off || end - r.off < need) return false;
    const std::string src = in.substr(r.off, end - r.off);
    if (r.kind == Kind::Param) {
        body += param30(src);
        ++s.params;
        return true;
    }
    if (r.kind == Kind::Effect) {
        body += src;
        ++s.effects;
        return true;
    }
    const uint8_t channels = (uint8_t)src[kChannelCount10];
    if (kEmitter10 + 2 * (size_t)channels > src.size()) return false;
    std::vector<int> chans;
    const bool re = chanMap && *chanMap && (*chanMap)(rd16(in, r.entry + kEntryMaterial), channels, chans);
    if (re) ++s.rechanneled;
    if (re) s.constChans += (int)std::count_if(chans.begin(), chans.end(), [](int c) { return c < 0; });
    body += emitter30(src, re ? &chans : nullptr, x.nkeys);
    ++s.emitters;
    s.meshes += src[kMeshName10] != 0;
    return true;
}

// The 3.0 index tables, entries in their 1.0 order pointing at the records' new offsets
inline void indexTables(const std::string& in, const Index& x, const std::vector<uint32_t>& newOff,
                        particledata::Package& p)
{
    p.emitters.assign(particledata::kEmitterEntry * x.ne, '\0');
    p.effects.assign(particledata::kEntry * x.nf, '\0');
    p.params.assign(particledata::kEntry * x.np, '\0');
    const size_t e = particledata::kEntry;
    for (size_t i = 0; i < x.recs.size(); ++i) {
        const Rec& r = x.recs[i];
        const uint32_t hash = rd32(in, r.entry + kEntryHash);
        if (r.kind == Kind::Effect) particledata::putEntry(p.effects, (r.entry - x.pf) / e, newOff[i], hash);
        if (r.kind == Kind::Param) particledata::putEntry(p.params, (r.entry - x.pp) / e, newOff[i], hash);
        if (r.kind != Kind::Emitter) continue;
        // class 1 in 1428/1431 twins (3 = no LOD thinning, FUN_00675980); the material indexes the .mtb, order kept
        const uint16_t material = rd16(in, r.entry + kEntryMaterial);
        const particledata::EmitterEntry entry{newOff[i], hash, 1, material, xc(rd32(in, r.entry + kEntryType))};
        particledata::putEmitterEntry(p.emitters, (r.entry - x.pe) / particledata::kEmitterEntry, entry);
    }
}

}  // namespace detail

// 1.0 package -> 3.0; false: not a well-formed 1.0 package, out and st untouched. chanMap: null keeps the channels
inline bool transcode(const std::string& in, std::string& out, Stats* st = nullptr, const ChanMap* chanMap = nullptr)
{
    if (!isPd10(in)) return false;
    const std::optional<detail::Index> x = detail::readIndex(in);
    if (!x) return false;
    particledata::Package p;
    std::vector<uint32_t> newOff(x->recs.size());
    Stats s;
    const detail::Source pkg{in, *x, chanMap};
    for (size_t i = 0; i < x->recs.size(); ++i) {
        newOff[i] = (uint32_t)p.body.size();
        if (!detail::convertRecord(pkg, i, s, p.body)) return false;
        p.body.resize(bytes::alignUp(p.body.size(), 4), '\0');
    }
    p.body.resize(bytes::alignUp(p.body.size(), particledata::kKeyBytes), '\0');
    p.keys = (uint32_t)p.body.size();
    p.body.append(in, x->keys, particledata::kKeyBytes * (size_t)x->nkeys);
    p.nkeys = x->nkeys;
    if (s.constChans) {   // index nkeys: emitter30's constKeyIndex
        p.body.append(particledata::kConstantKey, particledata::kKeyBytes);
        ++p.nkeys;
    }
    detail::indexTables(in, *x, newOff, p);
    out = particledata::write(p);
    s.keys = p.nkeys;
    if (st) *st = s;
    return true;
}

// .p3d: header 0x20 (u32 vertices +0xc, u8 indices +0x10, vertex bytes +0x1c), vertices, then the u8 indices
constexpr size_t kP3dHeader = 0x20, kP3dVertices = 0xc, kP3dIndices = 0x10, kP3dVertexBytes = 0x1c;
constexpr uint32_t kP3dVertex10 = 8, kP3dVertex30 = 12;   // 3.0 adds a colour (FUN_006634b0 reads count x 0xc)

// .p3d mesh 1.0 -> 3.0 with white vertex colour (no tint); false: not a 1.0 mesh (FUN_006587e0 reads the indices)
inline bool p3d30(const std::string& in, std::string& out)
{
    if (in.size() < kP3dHeader || in.compare(0, 4, "P3D ") || rd32(in, kP3dVertexBytes) != kP3dVertex10) return false;
    const uint32_t nv = rd32(in, kP3dVertices);
    if (kP3dHeader + (uint64_t)kP3dVertex10 * nv + rd32(in, kP3dIndices) > in.size()) return false;
    out = in.substr(0, kP3dHeader);
    bytes::put32(out, kP3dVertexBytes, kP3dVertex30);
    for (uint32_t i = 0; i < nv; ++i) {
        out.append(in, kP3dHeader + kP3dVertex10 * (size_t)i, kP3dVertex10);
        out.append(kP3dVertex30 - kP3dVertex10, '\xff');
    }
    out.append(in, kP3dHeader + kP3dVertex10 * (size_t)nv, std::string::npos);
    return true;
}

// 1.0 member 'particles/<name>.p3d' (lower-cased by the caller) -> 3.0's 'particles/%08x.p3d' for it, else ""
inline std::string p3dAlias(const std::string& member)
{
    const std::string dir = "particles/", ext = ".p3d";
    const bool shape = member.size() > dir.size() + ext.size() && !member.compare(0, dir.size(), dir);
    if (!shape || member.compare(member.size() - ext.size(), ext.size(), ext)) return "";
    std::string name = member.substr(dir.size(), member.size() - dir.size() - ext.size());
    const bool fits = name.size() < detail::kMeshNameBytes;   // char[8] in the emitter, NUL-terminated
    if (name.find('/') != std::string::npos || !fits) return "";
    char buf[32];
    snprintf(buf, sizeof(buf), "particles/%08x.p3d", hash::h131Signed(name.data(), name.size()));
    return buf;
}

}  // namespace vfx
