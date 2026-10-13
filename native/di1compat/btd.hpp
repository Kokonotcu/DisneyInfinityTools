// Collision .btd/.btm, 1.0 -> 3.0 at load time. C++ port of DI3Tools/btdconv.py + btdblocks.py (FORMATS.md 8):
// one block per TriangleList shape, packed back to back, 16-byte aligned; each ends at its per-triangle u32 array
// (1.0: offset/count at +0x28/+0x2c, 3.0: +48/+52). btdconv rebuilds 323/330 shipped single-block 3.0 .btd from
// their 1.0 twins and every shipped .btm from its .btd (317/317); without the 3.0 layout the world has no collision.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace btd {

inline uint32_t u32(const std::string& b, size_t o) { uint32_t v; memcpy(&v, b.data() + o, 4); return v; }
inline void put32(std::string& b, size_t o, uint32_t v) { memcpy(&b[o], &v, 4); }
inline void put64(std::string& b, size_t o, uint64_t v) { memcpy(&b[o], &v, 8); }

// [(start, end)] tiling b for a 1.0 (game 1) or 3.0 (game 3) file; empty if the rule does not tile it.
inline std::vector<std::pair<size_t, size_t>> blocks(const std::string& b, int game)
{
    size_t oa = game == 1 ? 0x28 : 48, oc = game == 1 ? 0x2c : 52, o = 0;
    std::vector<std::pair<size_t, size_t>> out;
    while (o + 64 <= b.size()) {   // a block header is 64 bytes; anything shorter is tail padding
        size_t end = o + u32(b, o + oa) + 4ull * u32(b, o + oc);
        if (!(o < end && end <= b.size())) return {};
        out.push_back({o, end});
        o = (end + 15) & ~(size_t)15;
    }
    return out;
}

// the 120-byte constant every 3.0 section A carries at +64 (btd-a-const.bin, identical in all 322 pairs)
static const uint8_t A_CONST[120] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// One 1.0 block -> one 3.0 block; false if the block does not have the 1.0 layout.
inline bool convertBlock(const std::string& src, std::string& out)
{
    if (src.size() < 64) return false;
    uint32_t aoff = u32(src, 0), alen = u32(src, 4), boff = u32(src, 8), blen = u32(src, 12);
    uint32_t voff = u32(src, 16), vcnt = u32(src, 20), ioff = u32(src, 24), tcnt = u32(src, 28);
    if (voff + 12ull * vcnt != ioff || aoff + alen != boff || boff + blen != voff || ioff + 6ull * tcnt > src.size()) return false;
    if (aoff + 188ull > src.size() || src.size() < 108) return false;
    uint32_t aNew = alen + 16, bNew = blen + 16, A = 64, B = A + aNew, V = B + bNew, I = V + vcnt * 12;
    uint32_t IE = (I + tcnt * 6 + 3) & ~3u, P = IE + 8;   // index end rounded up to 4, then an 8-byte gap
    out.assign(std::max<size_t>(P + 4ull * tcnt, A + 204), '\0');
    static const uint8_t tail[8] = {0x0c, 0, 6, 0, 2, 0, 0, 0};
    memcpy(&out[56], tail, 8);
    // the same writes, in the same order, as btdconv.convert (later ones win where a short section overlaps)
    memcpy(&out[A], src.data() + aoff, 64);
    memcpy(&out[A + 64], A_CONST, 120);
    memcpy(&out[A + 184], src.data() + aoff + 64, 16);
    memcpy(&out[A + 200], src.data() + aoff + 180, 4);
    if (alen > 188) memcpy(&out[A + 204], src.data() + aoff + 188, alen - 188);
    if (blen > 16) memcpy(&out[B + 32], src.data() + boff + 16, blen - 16);
    put64(out, B + 16, 72);
    memcpy(&out[V], src.data() + voff, 12ull * vcnt);
    memcpy(&out[I], src.data() + ioff, 6ull * tcnt);
    put32(out, IE + 4, 0xffffffff);   // the gap after the indices: 0, ffffffff
    memcpy(&out[80], src.data() + 80, 12);   // AABB min / max as btdconv writes them (absolute +80/+96;
    memcpy(&out[96], src.data() + 96, 12);   // = the copied section A header, since 1.0 blocks have A at 64)
    put64(out, 0, V);
    put64(out, 8, I);
    uint32_t h[10] = {vcnt, tcnt, A, aNew, B, bNew, IE, 1, P, tcnt};
    memcpy(&out[16], h, 40);
    return true;
}

// A 1.0 .btd -> 3.0; false (out untouched) if any block does not convert.
inline bool convert(const std::string& src, std::string& out)
{
    auto bl = blocks(src, 1);
    if (bl.empty()) return false;
    std::string r, blk;
    for (auto& se : bl) {
        r.append((16 - r.size() % 16) % 16, '\0');
        if (!convertBlock(src.substr(se.first, se.second - se.first), blk)) return false;
        r += blk;
    }
    out.swap(r);
    return true;
}

// The 3.0 .btd's block table: magic, block count, (offset, 0x00010010) per block.
inline std::string btm(const std::string& btd3)
{
    auto bl = blocks(btd3, 3);
    std::string r(8 + 8 * bl.size(), '\0');
    put32(r, 0, 0xc007d00d);
    put32(r, 4, (uint32_t)bl.size());
    for (size_t k = 0; k < bl.size(); ++k) { put32(r, 8 + 8 * k, (uint32_t)bl[k].first); put32(r, 12 + 8 * k, 0x10010); }
    return r;
}

}  // namespace btd
