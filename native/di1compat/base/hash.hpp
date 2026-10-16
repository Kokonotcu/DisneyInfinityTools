// Hashes with fixed results that name things on disk. Standard library only. Any thread.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace hash {

// FNV-1a 64 from a shortened start value (1469598103934665603; the standard one is 14695981039346656037). It names
// the shader cache's files, so the start value stays (docs/audit CO-19).
inline uint64_t fnv64(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
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
