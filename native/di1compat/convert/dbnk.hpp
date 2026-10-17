// 3.0's dialog bank header (DBNK) for banks known by '.name' indices; media records stay in memory. formats, base.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../base/bytes.hpp"
#include "../formats/fsbname.hpp"

namespace dbnk {

// A bank's index and the token its media records carry (the plugin passes its bank object's address).
struct Source {
    fsbname::Index idx;
    uint32_t token;
};

// The header 3.0 FUN_0182bf80 / FUN_0182c000 / FUN_0182d2f0 read; empty when the banks hold no line.
inline std::vector<uint8_t> fromNames(uint16_t prefix, const std::vector<Source>& banks);

namespace detail {
constexpr char kMagic[] = "DBNK\x08\x02\x07\x00";   // 8 bytes with the version
constexpr size_t kMagicSize = 8;
constexpr size_t kPrefix = 8;           // u32
constexpr size_t kMinLine = 0x10;       // u16
constexpr size_t kMaxLine = 0x12;       // u16
constexpr size_t kLineCount = 0x14;     // u16
constexpr size_t kInMemorySize = 0x18;  // u32: a data offset below it is in memory (FUN_0182c000)
constexpr size_t kLines = 0x20;         // u32 per line: {last variant, variants, u16 first media}, or all 0xff
constexpr size_t kMediaSize = 12;       // {name offset, data offset, data size}
constexpr size_t kRecordSize = 8;       // our media data: {token, subsound}
constexpr uint32_t kNoLine = 0xffffffffu;

// Fills the line entries of one bank whose media start at base; a line already taken keeps its bank.
inline void putLines(std::vector<uint8_t>& h, const fsbname::Index& x, uint32_t lo, uint32_t base)
{
    for (uint32_t l = x.lo(); l <= x.hi(); ++l) {
        const uint16_t v = x.entry(l);
        const size_t e = kLines + (size_t)(l - lo) * 4;
        if (v == fsbname::kNone || bytes::u32At(h.data() + e) != kNoLine) continue;
        h[e] = 0;
        h[e + 1] = (uint8_t)fsbname::variants(v);
        bytes::put16(h, e + 2, (uint16_t)(base + fsbname::firstSubsound(v)));
    }
}

// Appends one bank's media: a name at the end, a record {token, subsound} at recAt.
inline void putMedia(std::vector<uint8_t>& h, const Source& b, uint32_t base, size_t mediaAt, size_t recAt)
{
    for (uint32_t i = 0; i < b.idx.subs(); ++i) {
        const uint32_t m = base + i;
        const char* nm = b.idx.subName(i);
        const size_t me = mediaAt + (size_t)m * kMediaSize;
        bytes::put32(h, me, (uint32_t)h.size());
        bytes::put32(h, me + 4, (uint32_t)(recAt + (size_t)m * kRecordSize));
        bytes::put32(h, me + 8, (uint32_t)kRecordSize);
        h.insert(h.end(), nm, nm + strlen(nm) + 1);
        bytes::put32(h, recAt + (size_t)m * kRecordSize, b.token);
        bytes::put32(h, recAt + (size_t)m * kRecordSize + 4, i);
    }
}
}  // namespace detail

// A line in two banks of one speaker: the first bank keeps it (1.0 tries the slots in order).
inline std::vector<uint8_t> fromNames(uint16_t prefix, const std::vector<Source>& banks)
{
    uint32_t lo = fsbname::kNone, hi = 0, media = 0;
    for (const Source& b : banks) {
        lo = (std::min)(lo, (uint32_t)b.idx.lo());
        hi = (std::max)(hi, (uint32_t)b.idx.hi());
        media += b.idx.subs();
    }
    if (banks.empty() || hi < lo) return {};
    const uint32_t lines = hi - lo + 1;
    const size_t mediaAt = detail::kLines + (size_t)lines * 4, recAt = mediaAt + (size_t)media * detail::kMediaSize;
    std::vector<uint8_t> h(recAt + (size_t)media * detail::kRecordSize, 0);
    memcpy(h.data(), detail::kMagic, detail::kMagicSize);
    bytes::put32(h, detail::kPrefix, prefix);
    bytes::put16(h, detail::kMinLine, (uint16_t)lo);
    bytes::put16(h, detail::kMaxLine, (uint16_t)hi);
    bytes::put16(h, detail::kLineCount, (uint16_t)lines);
    memset(h.data() + detail::kLines, 0xff, (size_t)lines * 4);
    uint32_t base = 0;
    for (const Source& b : banks) {
        detail::putLines(h, b.idx, lo, base);
        detail::putMedia(h, b, base, mediaAt, recAt);
        base += b.idx.subs();
    }
    bytes::put32(h, detail::kInMemorySize, (uint32_t)h.size());
    return h;
}

}  // namespace dbnk
