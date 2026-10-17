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

// s lower-cased by the CRT (ASCII only in the "C" locale).
inline std::string lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// True when s is a non-empty stem followed by suffix, ignoring ASCII case.
inline bool endsWithPastStem(const std::string& s, const char* suffix)
{
    const size_t n = strlen(suffix);
    return s.size() > n && _stricmp(s.c_str() + s.size() - n, suffix) == 0;
}

// An engine path as a lookup key: '\' -> '/', lower case, one leading "assets/" removed (not idempotent, A-22).
inline std::string norm(const char* path)
{
    std::string p = path;
    for (char& c : p) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    if (p.rfind("assets/", 0) == 0) p.erase(0, 7);
    return p;
}

}  // namespace text
