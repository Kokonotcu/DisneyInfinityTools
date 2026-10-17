// Name hashes the engines use, bit-exact. Standard library only; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace hash {

constexpr uint32_t kH131 = 131;
constexpr uint32_t kEhashFold = 0x25;   // FUN_0044a9f0 adds each 4 chars and multiplies by it

// x131 over n chars or to a NUL, chars sign-extended (3.0 FUN_0040bb50, 1.0 FUN_0040bac0); audio.hpp's is unsigned
inline uint32_t h131Signed(const char* s, size_t n)
{
    uint32_t h = 0;
    for (size_t i = 0; i < n && s[i]; ++i) h = h * kH131 + (uint32_t)(int32_t)(signed char)s[i];
    return h;
}

// The engine's name hash of shader parameters, slots, techniques: case-sensitive (FUN_0044a9f0, shd.py ehash)
inline uint32_t ehash(const std::string& s)
{
    std::string b = s;
    b.push_back('\0');
    const auto ch = [&](size_t i) { return (uint32_t)(int32_t)(int8_t)b[i]; };
    uint32_t u = ch(0), acc = 0;
    size_t i = 0;
    while (b[i]) {
        ++i;
        u = (u << 8) | ch(i);
        if ((i & 3) == 3) acc = (acc + u) * kEhashFold;
    }
    return (u << ((24 - 8 * (i & 3)) & 31)) + acc;
}

// The vformat and shader GUID hash: ehash of the name ASCII-lowered (FUN_0044aa40)
inline uint32_t ehashLower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return ehash(s);
}

}  // namespace hash
