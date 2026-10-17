// Checked reads of memory the plugin does not own (H8). Windows, sys.hpp. Any thread; vq* and readableWords in VEHs.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../sys.hpp"

namespace core {

namespace detail {
// The protections vqReadable reads through: an execute-only page has none of them.
constexpr DWORD kReadProtect = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

inline bool readableRegion(const MEMORY_BASIC_INFORMATION& mbi)
{
    const bool committed = mbi.State == MEM_COMMIT;
    const bool blocked = (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;
    const bool read = (mbi.Protect & kReadProtect) != 0;
    return committed && !blocked && read;
}
}  // namespace detail

// Whether [p, p+n) is readable, by VirtualQuery alone: slow on big regions, so only diagnostic stack walks use it.
inline bool vqReadable(const void* p, size_t n)
{
    const uint8_t* a = (const uint8_t*)p;
    MEMORY_BASIC_INFORMATION mbi;
    // n == 0 is readable and low addresses are queried: sys::readable answers both differently
    while (n) {
        // ponytail: query-then-read can race a protection change; never seen.
        if (!VirtualQuery(a, &mbi, sizeof(mbi)) || !detail::readableRegion(mbi)) return false;
        size_t room = (const uint8_t*)mbi.BaseAddress + mbi.RegionSize - a;
        if (room >= n) return true;
        a += room;
        n -= room;
    }
    return true;
}

// Readable words from sp, at most max. Stops exactly where an unchecked walk would fault; a VEH must never fault.
inline int readableWords(const uint32_t* sp, int max)
{
    int words = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (words < max && vqReadable(sp + words, 4) && VirtualQuery(sp + words, &mbi, sizeof(mbi))) {
        size_t room = (size_t)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)(sp + words)) / 4;
        if (!room) break;   // an unaligned sp in the last 3 bytes of a region
        words += (int)(std::min)(room, (size_t)(max - words));
    }
    return words;
}

// Copies a C string the engine handed us without trusting the pointer.
inline void safeCopy(char* out, size_t cap, const char* src)
{
    // trace hooks may pass non-pointers: check first, as CrabeLoader dumps every AV, even one the __try handles
    if (!sys::readable(src, 1)) {
        if (cap) out[0] = 0;
        return;
    }
    __try {
        size_t i = 0;
        for (; src && i + 1 < cap && src[i]; ++i) out[i] = src[i];
        out[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // strcpy_s kept (N21): a cap under 13 fail-fasts, where snprintf would cut; callers' caps are not all checked
        strcpy_s(out, cap, "<unreadable>");
    }
}

// The char* a CString member at 'at' holds, copied into buf by safeCopy.
inline const char* cstr(const void* at, char* buf, size_t cap)
{
    const char* p = nullptr;
    if (sys::readable(at, sizeof(p))) p = *(const char* const*)at;
    safeCopy(buf, cap, p);
    return buf;
}

}  // namespace core
