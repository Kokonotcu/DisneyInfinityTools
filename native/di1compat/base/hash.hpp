// Name hashes the engines use, bit-exact. Standard library only; any thread.
#pragma once
#include <cstddef>
#include <cstdint>

namespace hash {

constexpr uint32_t kH131 = 131;

// x131 over n chars or to a NUL, chars sign-extended (3.0 FUN_0040bb50, 1.0 FUN_0040bac0); audio.hpp's is unsigned
inline uint32_t h131Signed(const char* s, size_t n)
{
    uint32_t h = 0;
    for (size_t i = 0; i < n && s[i]; ++i) h = h * kH131 + (uint32_t)(int32_t)(signed char)s[i];
    return h;
}

}  // namespace hash
