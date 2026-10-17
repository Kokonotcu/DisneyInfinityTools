// Hashes with fixed results that name things on disk. Standard library only. Any thread.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace hash {

// FNV-1a 64 from a shortened start value: it names the shader cache's files, so it stays (CO-19).
inline uint64_t fnv64(const std::string& s)
{
    constexpr uint64_t kStart = 1469598103934665603ull;   // the standard basis is 14695981039346656037
    constexpr uint64_t kPrime = 1099511628211ull;         // the FNV 64-bit prime
    uint64_t h = kStart;
    for (unsigned char c : s) h = (h ^ c) * kPrime;
    return h;
}

// The shader cache's file for one compile: "<16 hex digits>.dxbc" of fnv64(target + "\n" + hlsl) (docs/audit OT-10).
inline std::string shaderCacheName(const char* target, const std::string& hlsl)
{
    char name[24];
    snprintf(name, sizeof(name), "%016llx.dxbc", (unsigned long long)fnv64(std::string(target) + "\n" + hlsl));
    return name;
}

}  // namespace hash
