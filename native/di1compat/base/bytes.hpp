// Bounds checks that cannot wrap, and little-endian reads and writes on file buffers. Standard library only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace bytes {

// True when [off, off + n) lies inside a buffer of size bytes. Cannot overflow.
inline bool fits(size_t size, size_t off, size_t n) { return n <= size && off <= size - n; }

// The little-endian u32 / u16 at off in b; 0 when it does not fit (readers rely on that)
inline uint32_t u32OrZero(const std::string& b, size_t off)
{
    uint32_t v = 0;
    if (fits(b.size(), off, sizeof v)) memcpy(&v, b.data() + off, sizeof v);
    return v;
}
inline uint16_t u16OrZero(const std::string& b, size_t off)
{
    uint16_t v = 0;
    if (fits(b.size(), off, sizeof v)) memcpy(&v, b.data() + off, sizeof v);
    return v;
}

// The little-endian u32 / u16 at p; the caller has checked that it fits
inline uint32_t u32At(const uint8_t* p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}
inline uint16_t u16At(const uint8_t* p)
{
    uint16_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

// v little-endian at off in s, which already holds those bytes
inline void put32(std::string& s, size_t off, uint32_t v) { memcpy(&s[off], &v, sizeof v); }
inline void put16(std::string& s, size_t off, uint16_t v) { memcpy(&s[off], &v, sizeof v); }

// x rounded up to a multiple of a, a power of two
inline size_t alignUp(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }

}  // namespace bytes
