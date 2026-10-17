// Bounded text formatting: text that does not fit is cut, never a fail-fast (N21, A-37). Standard library only.
#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace format {

// Appends printf text at out[len], cut to fit; returns the new length, below cap. Integer formats never allocate (H9).
inline size_t appendCut(char* out, size_t cap, size_t len, const char* fmt, ...)
{
    if (cap == 0 || len >= cap - 1) return len;
    va_list a;
    va_start(a, fmt);
    const int n = vsnprintf(out + len, cap - len, fmt, a);
    va_end(a);
    if (n < 0) {
        out[len] = 0;
        return len;
    }
    return (size_t)n < cap - len ? len + (size_t)n : cap - 1;
}

}  // namespace format
