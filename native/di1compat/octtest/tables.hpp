// Game data lookups several octtest modes share: a .dct key, a chd table field, 3.0's surface names. Main thread.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../base/bytes.hpp"
#include "../base/hash.hpp"
#include "../formats/chd.hpp"
#include "../formats/dct.hpp"
#include "../npc.hpp"

namespace octtest {

// The named child k of a chd table, or null.
inline const chd::V* field(const chd::V& t, const char* k)
{
    for (auto& e : t.named)
        if (e.first == k) return &e.second;
    return nullptr;
}

// The engine's probe of a .dct table (FORMATS.md 9): lower-cased key, seeded lookup2, linear from hash % slots.
inline bool dctLookup(const std::string& t, const std::string& key, std::string& value)
{
    namespace d = dct::detail;
    const std::string k = npc::lower(key);
    uint32_t h = hash::lookup2((const uint8_t*)k.data(), k.size(), bytes::le32(t, d::kSeedAt));
    const uint32_t ns = bytes::le32(t, d::kSlotsAt);
    for (uint32_t i = h % ns; bytes::le32(t, d::kHeader + d::kSlotBytes * i + d::kSlotValueAt); i = (i + 1) % ns) {
        const size_t slot = d::kHeader + d::kSlotBytes * i;
        if (bytes::le32(t, slot) != h) continue;
        value = t.c_str() + slot + d::kSlotValueAt + 1 + (int32_t)bytes::le32(t, slot + d::kSlotValueAt);   // rel: +1
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
