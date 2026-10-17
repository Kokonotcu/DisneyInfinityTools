// Exe return addresses on a stack, for diagnostic lines. Never faults or allocates: VEHs use it (H9).
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "hooks.hpp"
#include "memcheck.hpp"

namespace core {

namespace eng {
constexpr uint32_t kExeImageSpan = 0x2100000u;   // bytes from the base read as the exe; tdStatic uses 0x209B000 (A-47)
constexpr uint32_t kExeHeaderBytes = 0x1000u;    // the PE headers' page: no code before it
}  // namespace eng

constexpr uint32_t kLongestCall = 6;   // FF /2 with a 32-bit displacement: the longest call instruction

namespace detail {
constexpr uint8_t kCallRel32 = 0xE8;       // E8 rel32: call
constexpr uint8_t kCallRm = 0xFF;          // FF /2: call r/m32
constexpr uint8_t kModRmReg = 0x38;        // the ModRM byte's reg field
constexpr uint8_t kRegCall = 0x10;         // reg field 2: FF's call
constexpr size_t kStackWalkWords = 2048;   // stackOf reads at most 8 KiB of stack
constexpr uint32_t kUtf16Pair = 0xff00ff00u;   // these bits are zero in two UTF-16 characters of a path buffer

// Whether op starts an FF /2 call.
inline bool callRm(const uint8_t* op) { return op[0] == kCallRm && (op[1] & kModRmReg) == kRegCall; }
}  // namespace detail

// Whether a runtime address lies in the span read as the exe image.
inline bool inExeImage(uintptr_t v) { return v >= g_base && v < g_base + eng::kExeImageSpan; }

// Whether ret is an exe return address: a call precedes it (E8 rel32 into the exe, or FF /2).
inline bool afterCall(uintptr_t ret)
{
    const bool inside = ret >= g_base + eng::kExeHeaderBytes + kLongestCall && ret < g_base + eng::kExeImageSpan;
    if (!inside) return false;
    const uint8_t* p = (const uint8_t*)ret;
    if (!vqReadable(p - kLongestCall, kLongestCall)) return false;
    if (p[-5] == detail::kCallRel32) return inExeImage(ret + *(const int32_t*)(p - 4));
    // call [mem32] / [reg+disp32], call [reg+disp8], call reg / [reg]
    return detail::callRm(p - 6) || detail::callRm(p - 3) || detail::callRm(p - 2);
}

// Appends the exe return addresses found on a stack (static VAs, call-site validated) to line.
inline void stackOf(const uint32_t* sp, char* line, size_t cap, int len, int max = 24)
{
    MEMORY_BASIC_INFORMATION mbi;   // stay inside the stack's committed region (see vqReadable)
    if (!VirtualQuery(sp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return;
    size_t region = (size_t)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)sp) / 4;
    int words = (int)(std::min)(detail::kStackWalkWords, region);
    for (int i = 0, found = 0; i < words && found < max; ++i) {
        uint32_t v;
        __try { v = sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        bool ok = false;
        if ((v & detail::kUtf16Pair) == 0) continue;   // path text, not a return address
        __try { ok = afterCall(v); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (!ok) continue;
        // sprintf_s kept (N21): an overflow fail-fasts, where snprintf would cut and push len past cap (A-37)
        len += sprintf_s(line + len, cap - len, " %08X", (unsigned)toStaticVa(v));
        ++found;
    }
}

}  // namespace core
