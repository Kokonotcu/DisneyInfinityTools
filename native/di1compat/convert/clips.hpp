// ClipDataBlocks and EventLists of SubNetworks (FORMATS.md 4.10): readers, 3.0's output tables, re-packing. Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"
#include "../formats/oct.hpp"

namespace clips {

inline constexpr size_t kU16Max = 0xffff;   // largest value of the block's u16 counts and offsets
// Block header (3.0 FUN_00543ca0, 1.0 FUN_0053f7c0): f32 MinTime, f32 MaxTime, u16 nDesc, u16 nSec, u32 constOff
inline constexpr size_t kNDescAt = 8;
inline constexpr size_t kNSecAt = 10;
inline constexpr size_t kConstOffAt = 12;
inline constexpr size_t kHeader = 16;          // then u32 descOff[nDesc], u32 secOff[nSec]
inline constexpr char kPad = '\xee';           // fills each descriptor to a multiple of 4
inline constexpr uint32_t kIdMax30 = 0x1fff;   // 3.0 output head: ncomp << kCompShift30 | id
inline constexpr int kCompShift30 = 13;
inline constexpr size_t kMaxComps30 = 7;       // what 3 bits of ncomp hold

// LE value of w bytes at o, 0xffffffff past the end; overflow-safe for any o (GR-06)
inline uint32_t rd(const std::string& s, size_t o, int w)
{
    return bytes::fits(s.size(), o, (size_t)w) ? oct::u((const uint8_t*)s.data() + o, w) : 0xffffffffu;
}

// u16 words of a comp: 2, or 4 when its code & 0xf000 is 0x1000 / 0x2000 (FUN_0051b230, FUN_005329b0)
inline int compWords(uint32_t code)
{
    const uint32_t kind = code & 0xf000;
    return kind == 0x1000 || kind == 0x2000 ? 4 : 2;
}

// An output of a descriptor's table: its id and its comps (their u16 words)
struct Output {
    uint32_t id;
    std::vector<std::vector<uint16_t>> comps;
};

// Reads a source game's output table at o (end, exclusive, into *end); false if it overruns lim
using TableReader = bool (*)(const std::string& d, size_t o, size_t lim, std::vector<Output>& outs, size_t* end);

// 3.0's output table (FUN_005329b0): u16 nOut, nOut u16 heads, then every comp; an output over 7 comps is split
inline bool table30(const std::vector<Output>& outs, std::string& t)
{
    std::vector<std::pair<uint32_t, size_t>> heads;   // (id, comp count) after splitting
    for (auto& c : outs) {
        if (c.id > kIdMax30) return false;
        size_t k = 0;
        do {
            heads.push_back({c.id, (std::min<size_t>)(kMaxComps30, c.comps.size() - k)});
            k += kMaxComps30;
        } while (k < c.comps.size());
    }
    if (heads.size() > kU16Max) return false;   // GR-33
    oct::File::put(t, (uint32_t)heads.size(), 2);
    for (auto& h : heads) oct::File::put(t, (uint32_t)(h.second << kCompShift30 | h.first), 2);
    for (auto& c : outs)
        for (auto& w : c.comps)
            for (uint16_t x : w) oct::File::put(t, x, 2);
    return true;
}

// A ClipDataBlock's play range: its first two f32 (nullopt when shorter)
inline std::optional<std::pair<float, float>> clipTimes(const std::string& block)
{
    if (block.size() < 8) return std::nullopt;
    float a, z;
    memcpy(&a, &block[0], 4);
    memcpy(&z, &block[4], 4);
    return std::make_pair(a, z);
}

// fn(time) per event of the EventList at off in a NetworkBuffer: u32 n, n x (f32 time, f32 value)
template <class Fn>
inline void eachEventTime(const std::string& nb, uint32_t off, Fn fn)
{
    uint32_t cnt = rd(nb, off, 4);
    for (uint32_t k = 0; cnt != 0xffffffffu && k < cnt && bytes::fits(nb.size(), off + 4 + 8 * (size_t)k, 4); ++k) {
        float t;
        memcpy(&t, &nb[off + 4 + 8 * k], 4);
        fn(t);
    }
}

namespace detail {

// A block's header and where its re-packed area (the descriptors) ends
struct Header {
    uint32_t constOff;
    size_t hdrEnd;                  // the first descriptor
    std::vector<uint32_t> desc, sec;
    std::set<uint32_t> starts;      // descriptor offsets, sorted
    uint32_t areaEnd;
};

// The header, or nullopt when an offset points into the area (it would go stale) or past the payload
inline std::optional<Header> header(const std::string& d)
{
    if (d.size() < kHeader + 4) return std::nullopt;
    Header h;
    const uint32_t nDesc = rd(d, kNDescAt, 2), nSec = rd(d, kNSecAt, 2);
    h.constOff = rd(d, kConstOffAt, 4);
    h.hdrEnd = kHeader + 4 * (size_t)(nDesc + nSec);
    if (!nDesc || h.hdrEnd > d.size()) return std::nullopt;
    for (uint32_t i = 0; i < nDesc; ++i) h.desc.push_back(rd(d, kHeader + 4 * i, 4));
    for (uint32_t i = 0; i < nSec; ++i) h.sec.push_back(rd(d, kHeader + 4 * (nDesc + i), 4));
    h.starts.insert(h.desc.begin(), h.desc.end());
    if (*h.starts.begin() != h.hdrEnd) return std::nullopt;
    const uint32_t last = *h.starts.rbegin();
    h.areaEnd = (uint32_t)d.size();
    if (last >= h.areaEnd) return std::nullopt;   // a descriptor past the payload: the padding scan would read past d
    for (uint32_t e : h.sec)
        if (e > last && e < h.areaEnd) h.areaEnd = e;
    if (h.constOff > last && h.constOff < h.areaEnd) h.areaEnd = h.constOff;
    auto inside = [&h](uint32_t o) { return o >= h.hdrEnd && o < h.areaEnd && !h.starts.count(o); };
    if (inside(h.constOff) || std::any_of(h.sec.begin(), h.sec.end(), inside)) return std::nullopt;
    return h;
}

// Descriptor [s, e) with 3.0's output tables: u16, u16 n, u16 secIds[n], u16 X, sampled table, constant one at X
template <class Renumber>
inline std::optional<std::string> descriptor(const std::string& d, uint32_t s, uint32_t e, TableReader read,
                                             Renumber& renumber)
{
    uint32_t n = rd(d, s + 2, 2), x = rd(d, s + 4 + 2 * (size_t)n, 2);
    if (n == 0xffffffffu || x == 0xffffffffu || s + x > e) return std::nullopt;
    std::vector<Output> samp, cnst;
    size_t o1, o2;
    if (!read(d, s + 6 + 2 * (size_t)n, e, samp, &o1) || o1 != s + x) return std::nullopt;
    if (!read(d, s + x, e, cnst, &o2)) return std::nullopt;
    for (size_t p = o2; p < e; ++p)
        if (d[p] != kPad) return std::nullopt;   // only padding may follow
    if (!renumber(samp) || !renumber(cnst)) return std::nullopt;
    std::string st, ct;
    if (!table30(samp, st) || !table30(cnst, ct)) return std::nullopt;
    std::string t = d.substr(s, 4 + 2 * (size_t)n);
    if (t.size() + 2 + st.size() > kU16Max) return std::nullopt;   // X, the constant table's u16 offset (GR-33)
    oct::File::put(t, (uint32_t)(t.size() + 2 + st.size()), 2);
    t += st + ct;
    while (t.size() % 4) t.push_back(kPad);
    return t;
}

// The header with every offset moved: a descriptor to its new place, one past the area by delta
inline std::string movedHeader(const std::string& d, const Header& h, const std::map<uint32_t, uint32_t>& moved,
                               int64_t delta)
{
    auto fix = [&](uint32_t o) {
        auto m = moved.find(o);
        if (m != moved.end()) return m->second;
        return o >= h.areaEnd ? (uint32_t)(o + delta) : o;
    };
    std::string r = d.substr(0, kConstOffAt);
    oct::File::put(r, fix(h.constOff), 4);
    for (uint32_t o : h.desc) oct::File::put(r, fix(o), 4);
    for (uint32_t o : h.sec) oct::File::put(r, fix(o), 4);
    return r;
}

}  // namespace detail

// A ClipDataBlock payload (after its count) with 3.0's tables, read by read, ids through renumber;
// false (out untouched) if unparsed
template <class Renumber>
inline bool repack(const std::string& d, std::string& out, TableReader read, Renumber renumber)
{
    const std::optional<detail::Header> h = detail::header(d);
    if (!h) return false;
    std::string area;
    std::map<uint32_t, uint32_t> moved;
    for (auto it = h->starts.begin(); it != h->starts.end(); ++it) {
        uint32_t s = *it, e = std::next(it) == h->starts.end() ? h->areaEnd : *std::next(it);
        std::optional<std::string> t = detail::descriptor(d, s, e, read, renumber);
        if (!t) return false;
        moved[s] = (uint32_t)(h->hdrEnd + area.size());
        area += *t;
    }
    int64_t delta = (int64_t)(h->hdrEnd + area.size()) - h->areaEnd;
    out = detail::movedHeader(d, *h, moved, delta) + area + d.substr(h->areaEnd);
    return true;
}

}  // namespace clips
