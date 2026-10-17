// Slang (.sx) script containers: their layout, and a 0x100c container rewritten as 0x100d. base. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "../base/bytes.hpp"

namespace slang {

// Header words, from 3.0's fix-up FUN_007366f0, linker FUN_00736da0 and CALL handler 0x738345.
constexpr uint32_t kMagic = 0x474e4c53;         // "SLNG"
constexpr uint32_t kVersionOld = 0x100c;        // 16-byte import entries: all 699 1.0 scripts
constexpr uint32_t kVersion = 0x100d;           // 24-byte import entries: the only version 3.0's fix-up takes
constexpr uint32_t kNativeOrder = 0x12345678;   // the byte-order word of a little-endian file
constexpr size_t kMinHeader = 0x68;
constexpr size_t kVersionAt = 4;
constexpr size_t kOrderAt = 8;
constexpr size_t kSizeAt = 0xc;                 // the file size; upgrade() writes it last
constexpr size_t kUnreadSectionAt = 0x28;       // 0 in all 699 1.0 scripts; 3.0 has no reader for it
constexpr size_t kImportCountAt = 0x2c;
constexpr size_t kCodeAt = 0x40;                // once loaded: the code, in u16 words
constexpr size_t kImportTableAt = 0x54;         // in a file self-relative minus 1; once loaded a pointer

// Import entries. 0x100c: {name, hash, slot, signature}; 0x100d: {name, hash, slot, 0, signature, 0}.
constexpr size_t kOldEntry = 16;
constexpr size_t kOldSignatureAt = 12;
constexpr size_t kEntry = 24;
constexpr size_t kHashAt = 4;
constexpr size_t kSlotAt = 8;          // the native the linker bound: void __cdecl fn(ctx)
constexpr size_t kSpareAt = 12;        // never read by 3.0: the script's path offset, then the binder's row
constexpr size_t kSignatureAt = 16;
constexpr size_t kMarkAt = 20;         // never read by 3.0 (0 in 53,742 of 53,742 shipped entries)
constexpr uint32_t kServedMark = 0x00314944;   // "DI1": upgrade() wrote this entry

// Whether d holds a 0x100c container.
inline bool isV100c(const void* d, size_t n)
{
    const uint8_t* b = (const uint8_t*)d;
    return n >= kMinHeader && bytes::u32At(b) == kMagic && bytes::u32At(b + kVersionAt) == kVersionOld;
}

namespace detail {

// The offset a self-relative-minus-1 word at offset at names; a negative word wraps and fails the bounds checks.
inline size_t target(size_t at, uint32_t rel) { return at + (size_t)(int32_t)rel + 1; }

inline int fail(std::string* why, const char* w)
{
    if (why) *why = w;
    return -1;
}

// The k old entries at table lie inside a file of n bytes.
inline bool tableInFile(size_t n, uint32_t rel, size_t table, uint32_t k)
{
    const uint64_t tableSize = (uint64_t)kOldEntry * k;
    return rel != 0 && tableSize <= n && bytes::fits(n, table, (size_t)tableSize);
}

// The old entry at e as the 24-byte entry at f of out; false when its name lies outside.
inline bool upgradeEntry(const uint8_t* b, size_t n, size_t e, size_t f, std::string& out)
{
    uint32_t nameRel = bytes::u32At(b + e);
    if (nameRel) {
        size_t name = target(e, nameRel);
        if (name >= n) return false;
        bytes::put32(out, f, (uint32_t)(name - f - 1));
    }
    bytes::put32(out, f + kHashAt, bytes::u32At(b + e + kHashAt));
    bytes::put32(out, f + kSignatureAt, bytes::u32At(b + e + kOldSignatureAt));
    bytes::put32(out, f + kMarkAt, kServedMark);
    return true;
}

inline void padTo4(std::string& out)
{
    while (out.size() % 4) out.push_back(0);
}

}  // namespace detail

// 0x100c as 0x100d: a 24-byte import table and the path appended, nothing moves (sxconv.py, 699/699). -1: why.
inline int upgrade(const void* data, size_t n, const std::string& path, std::string& out, std::string* why)
{
    const uint8_t* b = (const uint8_t*)data;
    if (!isV100c(data, n)) return detail::fail(why, "not a 1.0 SLNG");
    if (bytes::u32At(b + kOrderAt) != kNativeOrder) return detail::fail(why, "byte order");
    if (bytes::u32At(b + kUnreadSectionAt)) return detail::fail(why, "1.0 idx10 section used");
    uint32_t k = bytes::u32At(b + kImportCountAt), rel = bytes::u32At(b + kImportTableAt);
    size_t table = detail::target(kImportTableAt, rel);
    if (k && !detail::tableInFile(n, rel, table, k)) return detail::fail(why, "import table outside the file");
    out.assign((const char*)data, n);
    bytes::put32(out, kVersionAt, kVersion);
    detail::padTo4(out);
    size_t at = out.size();
    out.append(kEntry * k, '\0');
    for (uint32_t i = 0; i < k; ++i)
        if (!detail::upgradeEntry(b, n, table + kOldEntry * i, at + kEntry * i, out))
            return detail::fail(why, "import name outside the file");
    if (k) {
        bytes::put32(out, kImportTableAt, (uint32_t)(at - kImportTableAt - 1));
        bytes::put32(out, at + kSpareAt, (uint32_t)out.size());   // entry 0: the path's offset, for the binder's log
        out.append(path.c_str(), path.size() + 1);
        detail::padTo4(out);
    }
    bytes::put32(out, kSizeAt, (uint32_t)out.size());
    return (int)k;
}

}  // namespace slang
