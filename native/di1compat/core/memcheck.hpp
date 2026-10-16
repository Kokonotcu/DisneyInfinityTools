// Checked reads of memory the plugin does not own (CLAUDE.md H8): readability checks and guarded string copies.
// sys::readable (working set first, fast) is the default check; vqReadable gives different answers and keeps its name.
// Windows and sys.hpp (for sys::readable). Any thread; vqReadable and readableWords also inside the VEHs (H9).
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../sys.hpp"

namespace core {

// Whether [p, p+n) is readable, by VirtualQuery alone. Unlike sys::readable it demands a read permission (an
// execute-only page fails), is true for n == 0 and queries low addresses; it is slow on big regions (sys.hpp), so
// only the diagnostic stack walks use it. Pre-check, don't fault: every fault reaches Pentane's and CrabeLoader's
// handlers.
// ponytail: query-then-read can race a protection change; never seen.
inline bool vqReadable(const void* p, size_t n)
{
    const uint8_t* a = (const uint8_t*)p;
    MEMORY_BASIC_INFORMATION mbi;
    while (n) {
        if (!VirtualQuery(a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            || !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        size_t room = (const uint8_t*)mbi.BaseAddress + mbi.RegionSize - a;
        if (room >= n) return true;
        a += room; n -= room;
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
    // trace hooks pass values that may not be pointers; check first, so no first-chance AV reaches the vectored
    // handlers (CrabeLoader writes a dump per AV even when the __try below handles it)
    if (!sys::readable(src, 1)) {
        if (cap) out[0] = 0;
        return;
    }
    __try {
        size_t i = 0;
        for (; src && i + 1 < cap && src[i]; ++i) out[i] = src[i];
        out[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
