// Collision .btd blocks and the .btm block table (FORMATS.md 8): tiling and the table, byte-exact. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"

namespace btd {

// Where a block header keeps its per-triangle u32 array's offset and count; the array ends the block.
struct BlockLayout { size_t arrayAt, countAt; };
inline constexpr BlockLayout kLayout30{48, 52};
inline constexpr size_t kBlockHeader = 64;   // anything shorter at the end is tail padding
inline constexpr size_t kBlockAlign = 16;    // each block starts 16-byte aligned
inline constexpr uint32_t kBtmMagic = 0xc007d00d;
inline constexpr uint32_t kBtmBlockWord = 0x10010;   // after each block offset in a .btm

// [(start, end)] tiling b for this layout; empty if the rule does not tile it.
inline std::vector<std::pair<size_t, size_t>> blocks(const std::string& b, const BlockLayout& layout)
{
    std::vector<std::pair<size_t, size_t>> out;
    for (size_t o = 0; bytes::fits(b.size(), o, kBlockHeader);) {
        // 64-bit: as size_t, a damaged offset or count wraps to an end that passes the check (CO-07)
        const uint64_t arrayAt = (uint64_t)o + bytes::u32At(b, o + layout.arrayAt);
        const uint64_t end = arrayAt + 4ull * bytes::u32At(b, o + layout.countAt);
        if (!(o < end && end <= b.size())) return {};
        out.push_back({o, (size_t)end});
        o = ((size_t)end + kBlockAlign - 1) & ~(kBlockAlign - 1);
    }
    return out;
}

// A 3.0 .btd's block table: magic, block count, then (offset, kBtmBlockWord) per block.
inline std::string btm(const std::string& btd3)
{
    const auto bl = blocks(btd3, kLayout30);
    std::string r(8 + 8 * bl.size(), '\0');
    bytes::put32(r, 0, kBtmMagic);
    bytes::put32(r, 4, (uint32_t)bl.size());
    for (size_t k = 0; k < bl.size(); ++k) {
        bytes::put32(r, 8 + 8 * k, (uint32_t)bl[k].first);
        bytes::put32(r, 12 + 8 * k, kBtmBlockWord);
    }
    return r;
}

}  // namespace btd
