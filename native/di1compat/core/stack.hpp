// Exe return addresses on a stack, as static addresses, for the diagnostic stack lines. Never faults and never
// allocates: the vectored exception handlers walk stacks with it (CLAUDE.md H9). core/hooks.hpp, core/memcheck.hpp.
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

// Whether a runtime address lies in the span read as the exe image.
inline bool inExeImage(uintptr_t v) { return v >= g_base && v < g_base + eng::kExeImageSpan; }

// Whether ret is a return address in the exe: the bytes before it are a call (E8 rel32 into the exe, or FF /2).
inline bool afterCall(uintptr_t ret)
{
    if (ret < g_base + eng::kExeHeaderBytes + kLongestCall || ret >= g_base + eng::kExeImageSpan) return false;
    const uint8_t* p = (const uint8_t*)ret;
    if (!vqReadable(p - kLongestCall, kLongestCall)) return false;
    if (p[-5] == 0xE8) { uintptr_t t = ret + *(const int32_t*)(p - 4); return inExeImage(t); }
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;   // call [mem32] / [reg+disp32]
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;   // call [reg+disp8]
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;   // call reg / [reg]
    return false;
}

// Appends the exe return addresses found on a stack (static VAs, call-site validated) to line.
inline void stackOf(const uint32_t* sp, char* line, size_t cap, int len, int max = 24)
{
    MEMORY_BASIC_INFORMATION mbi;   // stay inside the stack's committed region (see vqReadable)
    if (!VirtualQuery(sp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return;
    int words = (int)(std::min)((size_t)2048, (size_t)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)sp) / 4);
    for (int i = 0, found = 0; i < words && found < max; ++i) {
        uint32_t v;
        __try { v = sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        bool ok = false;
        if ((v & 0xff00ff00) == 0) continue;   // two UTF-16 characters of a path buffer, not a return address
        __try { ok = afterCall(v); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) { len += sprintf_s(line + len, cap - len, " %08X", (unsigned)toStaticVa(v)); ++found; }
    }
}

}  // namespace core
