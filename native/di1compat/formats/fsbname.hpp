// A dialog bank's '.name' index: line table, subsounds, subsound names (1.0 FUN_01132360). base/bytes.hpp; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../base/bytes.hpp"

namespace fsbname {

// Layout: u16 version, char[2] prefix, u16 table length, u16 min, u16 max, u16 subsounds; table at 0x10.
constexpr uint16_t kVersion = 4;
constexpr size_t kPrefix = 2;
constexpr size_t kTableLength = 4;
constexpr size_t kMinLine = 6;
constexpr size_t kMaxLine = 8;
constexpr size_t kSubsounds = 0xa;
constexpr size_t kTable = 0x10;   // u16 per line, then u16 name offset per subsound
constexpr uint16_t kNone = 0xffff;   // a line without subsounds; also u16() past the end
constexpr unsigned kVariantShift = 12;   // an entry is variants << 12 | first subsound
constexpr uint16_t kFirstMask = 0xfff;

// A table entry's first subsound.
inline int firstSubsound(uint16_t entry) { return entry & kFirstMask; }
// A table entry's subsound count: its variants, at least 1.
inline int variants(uint16_t entry) { return entry >> kVariantShift > 1 ? entry >> kVariantShift : 1; }

// A view of '.name' bytes; valid() before the other readers.
struct Index {
    const uint8_t* b = nullptr;
    size_t n = 0;
    uint16_t u16(size_t o) const { return bytes::fits(n, o, 2) ? bytes::u16At(b + o) : kNone; }
    // Every name ends inside the file (all 916 1.0 .name files, every language) and every entry fits.
    bool valid() const
    {
        if (n < kTable || u16(0) != kVersion) return false;
        const uint16_t len = u16(kTableLength), lo = u16(kMinLine), hi = u16(kMaxLine), subs = u16(kSubsounds);
        if (hi < lo || hi - lo + 1 > len || !bytes::fits(n, kTable, (size_t)(len + subs) * 2)) return false;
        for (uint16_t i = 0; i < subs; ++i) {
            const uint16_t o = u16(kTable + (size_t)(len + i) * 2);
            if (o >= n || !memchr(b + o, 0, n - o)) return false;
        }
        for (uint32_t l = lo; l <= hi; ++l) {
            const uint16_t v = u16(kTable + (size_t)(l - lo) * 2);
            if (v != kNone && firstSubsound(v) + variants(v) > subs) return false;
        }
        return true;
    }
    uint16_t prefix() const { return u16(kPrefix); }
    uint16_t lo() const { return u16(kMinLine); }
    uint16_t hi() const { return u16(kMaxLine); }
    uint16_t subs() const { return u16(kSubsounds); }
    uint16_t entry(uint32_t line) const
    {
        return line < lo() || line > hi() ? kNone : u16(kTable + (size_t)(line - lo()) * 2);
    }
    const char* subName(uint32_t i) const { return (const char*)b + u16(kTable + (size_t)(u16(kTableLength) + i) * 2); }
};

}  // namespace fsbname
