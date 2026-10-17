// Game data lookups several octtest modes share: a .dct key, a chd table field, 3.0's surface names. Main thread.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../base/bytes.hpp"
#include "../base/hash.hpp"
#include "../formats/chd.hpp"
#include "../formats/dct.hpp"
#include "../base/text.hpp"

namespace octtest {

// The named child k of a chd table, or null.
inline const chd::V* field(const chd::V& t, const char* k)
{
    for (auto& e : t.named)
        if (e.first == k) return &e.second;
    return nullptr;
}

// The engine's .dct probe (FORMATS.md 9), bounded: false for a missing key or a too-short table (OT-24).
inline bool dctLookup(const std::string& t, const std::string& key, std::string& value)
{
    namespace d = dct::detail;
    if (t.size() < d::kHeader) return false;
    const uint32_t ns = bytes::le32(t, d::kSlotsAt);
    if (!ns || (uint64_t)ns * d::kSlotBytes > t.size() - d::kHeader) return false;
    const std::string k = text::lower(key);
    const uint32_t h = hash::lookup2((const uint8_t*)k.data(), k.size(), bytes::le32(t, d::kSeedAt));
    uint32_t i = h % ns;
    for (uint32_t n = 0; n < ns; ++n, i = (i + 1) % ns) {   // lower-cased key, linear from hash % slots
        const size_t slot = d::kHeader + d::kSlotBytes * i;
        const int32_t rel = (int32_t)bytes::le32(t, slot + d::kSlotValueAt);
        if (!rel) return false;   // an empty slot ends the probe
        if (bytes::le32(t, slot) != h) continue;
        const int64_t at = (int64_t)(slot + d::kSlotValueAt + 1) + rel;   // a rel counts from its field + 1
        if (at < 0 || at >= (int64_t)t.size()) return false;
        value = t.c_str() + at;
        return true;
    }
    return false;
}

// The names of a 3.0 surfacetypes.lua in order: entries '{ name = "dynamic" | "static" }'.
inline std::vector<std::string> surfaceNames30(const std::string& lua)
{
    std::vector<std::string> names;
    for (size_t p = lua.find('{', lua.find('{') + 1); p != std::string::npos; p = lua.find('{', p + 1)) {
        size_t a = lua.find_first_not_of(" \t\r\n", p + 1), b = lua.find_first_of(" \t=", a);
        if (a != std::string::npos && b != std::string::npos) names.push_back(lua.substr(a, b - a));
    }
    return names;
}

}  // namespace octtest
