// Case and path helpers for engine paths. Standard library and the MSVC CRT (_stricmp) only. Any thread.
#pragma once
#include <cctype>
#include <cstring>
#include <string>

namespace text {

// True when s ends with suffix, ignoring ASCII case.
inline bool endsWith(const char* s, const char* suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && _stricmp(s + a - b, suffix) == 0;
}

// An engine path as a lookup key: '\' -> '/', lower case, one leading "assets/" removed. Not idempotent:
// "assets/assets/x" keeps one "assets/" (docs/audit A-22).
inline std::string norm(const char* path)
{
    std::string p = path;
    for (char& c : p) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    if (p.rfind("assets/", 0) == 0) p.erase(0, 7);
    return p;
}

}  // namespace text
