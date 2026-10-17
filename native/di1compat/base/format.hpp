// Bounded text formatting that keeps sprintf_s's contract. Standard library and the MSVC CRT only. Any thread.
#pragma once
#include <corecrt.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace format {

// sprintf_s without its name (N21): the length, and text that does not fit ends the process (A-37 decides).
inline int fitOrFailFast(char* out, size_t cap, const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int n = vsnprintf(out, cap, fmt, a);
    va_end(a);
    if (n < 0 || (size_t)n >= cap) _invalid_parameter_noinfo_noreturn();
    return n;
}

}  // namespace format
