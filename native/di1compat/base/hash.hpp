// Hashes with fixed results: the engines' name hashes, zlib's CRC-32, on-disk file names. base/bytes.hpp; any thread.
#pragma once
#include <algorithm>
#include <cctype>
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

constexpr uint32_t kH131 = 131;
constexpr uint32_t kEhashFold = 0x25;   // FUN_0044a9f0 adds each 4 chars and multiplies by it

// x131 over n chars or to a NUL, chars sign-extended (3.0 FUN_0040bb50, 1.0 FUN_0040bac0); lower-cased: FUN_0040bb80
inline uint32_t h131Signed(const char* s, size_t n)
{
    uint32_t h = 0;
    for (size_t i = 0; i < n && s[i]; ++i) h = h * kH131 + (uint32_t)(int32_t)(signed char)s[i];
    return h;
}

// FNV-1 of the name lower-cased by the CRT: Wwise's GetIDFromString (3.0 FUN_01947970)
inline uint32_t fnv1Lower(const char* s)
{
    constexpr uint32_t kBasis = 0x811c9dc5u;   // the FNV-1 32-bit offset basis
    constexpr uint32_t kPrime = 0x1000193u;    // the FNV-1 32-bit prime
    uint32_t h = kBasis;
    for (; *s; ++s) h = (h * kPrime) ^ (uint8_t)tolower((uint8_t)*s);
    return h;
}
inline uint32_t fnv1Lower(const std::string& s) { return fnv1Lower(s.c_str()); }

// The engine's name hash of shader parameters, slots, techniques: case-sensitive (FUN_0044a9f0, shd.py ehash)
inline uint32_t ehash(const std::string& s)
{
    std::string b = s;
    b.push_back('\0');
    const auto ch = [&](size_t i) { return (uint32_t)(int32_t)(int8_t)b[i]; };
    uint32_t u = ch(0), acc = 0;
    size_t i = 0;
    while (b[i]) {
        ++i;
        u = (u << 8) | ch(i);
        if ((i & 3) == 3) acc = (acc + u) * kEhashFold;
    }
    return (u << ((24 - 8 * (i & 3)) & 31)) + acc;
}

// The vformat and shader GUID hash: ehash of the name ASCII-lowered (FUN_0044aa40)
inline uint32_t ehashLower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return ehash(s);
}

// zlib.crc32: IEEE polynomial, reflected, bit by bit
inline uint32_t crc32Zlib(const std::string& s)
{
    constexpr uint32_t kInit = 0xffffffff, kPolynomial = 0xedb88320;
    uint32_t c = kInit;
    for (unsigned char ch : s) {
        c ^= ch;
        for (int bit = 0; bit < 8; ++bit) c = (c >> 1) ^ (kPolynomial & (0u - (c & 1)));
    }
    return ~c;
}

}  // namespace hash
