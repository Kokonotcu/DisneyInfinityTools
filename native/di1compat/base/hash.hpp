// Hashes with fixed results: the engine's name hashes and the names of files on disk. base/bytes.hpp; any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include "bytes.hpp"

namespace hash {

namespace detail {
// lookup2's mix(a, b, c), one row per line of Jenkins' macro.
inline void mix(uint32_t& a, uint32_t& b, uint32_t& c)
{
    a = (a - b - c) ^ (c >> 13);
    b = (b - c - a) ^ (a << 8);
    c = (c - a - b) ^ (b >> 13);
    a = (a - b - c) ^ (c >> 12);
    b = (b - c - a) ^ (a << 16);
    c = (c - a - b) ^ (b >> 5);
    a = (a - b - c) ^ (c >> 3);
    b = (b - c - a) ^ (a << 10);
    c = (c - a - b) ^ (b >> 15);
}
// The little-endian word of the first min(n, 4) bytes at p.
inline uint32_t word(const uint8_t* p, size_t n) { return bytes::le(p, (int)(std::min)(n, (size_t)4)); }
}  // namespace detail

// Bob Jenkins' lookup2 hash() of the n bytes at k, initval c: the engine's FUN_0040b900.
inline uint32_t lookup2(const uint8_t* k, size_t n, uint32_t c)
{
    constexpr uint32_t kGoldenRatio = 0x9e3779b9;   // lookup2's initial a and b
    uint32_t a = kGoldenRatio;
    uint32_t b = kGoldenRatio;
    size_t i = 0;
    for (; bytes::fits(n, i, 12); i += 12) {
        a += detail::word(k + i, 4);
        b += detail::word(k + i + 4, 4);
        c += detail::word(k + i + 8, 4);
        detail::mix(a, b, c);
    }
    const size_t rest = n - i;
    a += detail::word(k + i, rest);
    b += rest > 4 ? detail::word(k + i + 4, rest - 4) : 0;
    c += (uint32_t)n + ((rest > 8 ? detail::word(k + i + 8, rest - 8) : 0) << 8);
    detail::mix(a, b, c);
    return c;
}

// FNV-1a 64 from a shortened start value: it names the shader cache's files, so it stays (CO-19).
inline uint64_t fnv64(const std::string& s)
{
    constexpr uint64_t kStart = 1469598103934665603ull;   // the standard basis is 14695981039346656037
    constexpr uint64_t kPrime = 1099511628211ull;         // the FNV 64-bit prime
    uint64_t h = kStart;
    for (unsigned char c : s) h = (h ^ c) * kPrime;
    return h;
}

// The shader cache's file for one compile: "<16 hex digits>.dxbc" of fnv64(target + "\n" + hlsl) (docs/audit OT-10).
inline std::string shaderCacheName(const char* target, const std::string& hlsl)
{
    char name[24];
    snprintf(name, sizeof(name), "%016llx.dxbc", (unsigned long long)fnv64(std::string(target) + "\n" + hlsl));
    return name;
}

}  // namespace hash
