// File data helpers: overflow-safe bounds checks, little-endian reads and writes. Standard library only; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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
inline uint32_t u32OrZero(const std::vector<uint8_t>& b, size_t off)
{
    uint32_t v = 0;
    if (fits(b.size(), off, sizeof v)) memcpy(&v, b.data() + off, sizeof v);
    return v;
}
inline uint16_t u16OrZero(const std::vector<uint8_t>& b, size_t off)
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
inline void put32(std::vector<uint8_t>& b, size_t off, uint32_t v) { memcpy(&b[off], &v, sizeof v); }
inline void put16(std::vector<uint8_t>& b, size_t off, uint16_t v) { memcpy(&b[off], &v, sizeof v); }

// x rounded up to a multiple of a, a power of two
inline size_t alignUp(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }

// The unsigned little-endian value of width (0-4) bytes at p. Unchecked: the caller proved the bytes are there.
inline uint32_t le(const void* p, int width)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint32_t v = 0;
    for (int i = 0; i < width; ++i) v |= (uint32_t)b[i] << (8 * i);
    return v;
}

// le() of the 4 bytes at offset at of s. Unchecked.
inline uint32_t le32(const std::string& s, size_t at) { return le(s.data() + at, 4); }

// Appends the low width bytes of v, little-endian.
inline void appendLe(std::string& s, uint32_t v, int width)
{
    for (int i = 0; i < width; ++i) s.push_back((char)(v >> (8 * i)));
}

// Overwrites the 4 or 8 bytes at offset at of s with v, little-endian. Unchecked.
inline void setLe32(std::string& s, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) s[at + i] = (char)(v >> (8 * i));
}
inline void setLe64(std::string& s, size_t at, uint64_t v)
{
    for (int i = 0; i < 8; ++i) s[at + i] = (char)(v >> (8 * i));
}

}  // namespace bytes
