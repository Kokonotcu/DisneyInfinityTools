// 1.0 collision blocks in 3.0's layout (FORMATS.md 8, btdconv.py): without it a world has no collision. Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "../../base/bytes.hpp"
#include "../../formats/btd.hpp"

namespace di1::btd {

inline constexpr ::btd::BlockLayout kLayout10{0x28, 0x2c};

namespace detail {
// The 120-byte constant every 3.0 section A carries at +64 (btd-a-const.bin, identical in all 322 pairs).
inline constexpr uint8_t kSectionAConst[120] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
inline constexpr uint8_t kHeaderTail[8] = {0x0c, 0, 6, 0, 2, 0, 0, 0};   // +56..63 of all 6,866 shipped blocks
inline constexpr uint32_t kVertexBytes = 12;      // f32 x3
inline constexpr uint32_t kTriangleBytes = 6;     // u16 x3
inline constexpr uint32_t kSectionGrowth = 16;    // sections A and B each grow by 16 bytes
inline constexpr uint64_t kSectionBSize = 72;     // section B's size field in 3.0
inline constexpr uint32_t kGapWord = 0xffffffff;  // the 8-byte gap after the indices: 0, ffffffff

// The 1.0 block header (FORMATS.md 8): section A / B offset and length, vertices, indices.
struct Header10 { uint32_t aoff, alen, boff, blen, voff, vcnt, ioff, tcnt; };
inline Header10 header10(const std::string& src)
{
    return {bytes::le32(src, 0), bytes::le32(src, 4), bytes::le32(src, 8), bytes::le32(src, 12),
            bytes::le32(src, 16), bytes::le32(src, 20), bytes::le32(src, 24), bytes::le32(src, 28)};
}
// True when the sections tile the block and fit it. 64-bit sums: in 32 bits a wrapped sum passes (CO-07).
inline bool layoutFits(const Header10& h, size_t size)
{
    const bool vertices = h.voff + 12ull * h.vcnt == h.ioff;
    const bool sections = (uint64_t)h.aoff + h.alen == h.boff && (uint64_t)h.boff + h.blen == h.voff;
    if (!vertices || !sections || h.ioff + 6ull * h.tcnt > size) return false;
    return h.aoff + 188ull <= size && size >= 108;
}
}  // namespace detail

// One 1.0 block -> one 3.0 block, with btdconv.convert's writes in its order (a later one wins on overlap).
inline bool convertBlock(const std::string& src, std::string& out)
{
    if (src.size() < ::btd::kBlockHeader) return false;
    const detail::Header10 h = detail::header10(src);
    if (!detail::layoutFits(h, src.size())) return false;
    const uint32_t aNew = h.alen + detail::kSectionGrowth, bNew = h.blen + detail::kSectionGrowth;
    const uint32_t A = (uint32_t)::btd::kBlockHeader, B = A + aNew, V = B + bNew, I = V + h.vcnt * detail::kVertexBytes;
    const uint32_t IE = (I + h.tcnt * detail::kTriangleBytes + 3) & ~3u, P = IE + 8;   // index end to 4, then the gap
    out.assign((std::max)((size_t)(P + 4ull * h.tcnt), (size_t)(A + 204)), '\0');
    memcpy(&out[56], detail::kHeaderTail, 8);
    // section A (FORMATS.md 8): [0, 64) carried, [64, 184) the constant, then 1.0's [64, 80), [180, 184), [188, ...)
    memcpy(&out[A], src.data() + h.aoff, 64);
    memcpy(&out[A + 64], detail::kSectionAConst, 120);
    memcpy(&out[A + 184], src.data() + h.aoff + 64, 16);
    memcpy(&out[A + 200], src.data() + h.aoff + 180, 4);
    if (h.alen > 188) memcpy(&out[A + 204], src.data() + h.aoff + 188, h.alen - 188);
    // section B: u32 pointers widen to u64, so 32 zero bytes, then 1.0's from +16; size field 72
    if (h.blen > 16) memcpy(&out[B + 32], src.data() + h.boff + 16, h.blen - 16);
    bytes::setLe64(out, B + 16, detail::kSectionBSize);
    memcpy(&out[V], src.data() + h.voff, (size_t)(detail::kVertexBytes * (uint64_t)h.vcnt));
    memcpy(&out[I], src.data() + h.ioff, (size_t)(detail::kTriangleBytes * (uint64_t)h.tcnt));
    bytes::setLe32(out, IE + 4, detail::kGapWord);
    // AABB min / max at absolute +80 / +96, as btdconv writes them (the copied section A header)
    memcpy(&out[80], src.data() + 80, 12);
    memcpy(&out[96], src.data() + 96, 12);
    bytes::setLe64(out, 0, V);
    bytes::setLe64(out, 8, I);
    const uint32_t fields[10] = {h.vcnt, h.tcnt, A, aNew, B, bNew, IE, 1, P, h.tcnt};   // the 3.0 header, +16..55
    for (size_t k = 0; k < 10; ++k) bytes::setLe32(out, 16 + 4 * k, fields[k]);
    return true;
}

// A 1.0 .btd in 3.0's layout (323/330 shipped single-block twins rebuilt); false, out untouched, if a block fails.
inline bool convert(const std::string& src, std::string& out)
{
    const auto bl = ::btd::blocks(src, kLayout10);
    if (bl.empty()) return false;
    std::string r, blk;
    for (const auto& se : bl) {
        r.append((::btd::kBlockAlign - r.size() % ::btd::kBlockAlign) % ::btd::kBlockAlign, '\0');
        if (!convertBlock(src.substr(se.first, se.second - se.first), blk)) return false;
        r += blk;
    }
    out.swap(r);
    return true;
}

}  // namespace di1::btd
