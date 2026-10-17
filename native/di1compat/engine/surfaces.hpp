// The engine's surface table: surface names by index. core memory checks. Loader threads, after startup2 loads.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::surfaces {

// The table's names in index order; empty until startup2 loads.
inline std::vector<std::string> table();

namespace detail {

namespace eng {
// FUN_00754eb0 fills the table at *(0x2204830): +0xc {char*, bool}[], +0x10 count
constexpr uint32_t kTable = 0x02204830;
constexpr size_t kEntries = 0xc;
constexpr size_t kCount = 0x10;
constexpr size_t kHeaderBytes = 0x14;   // up to and with the count
constexpr size_t kEntryBytes = 8;       // {char*, bool}
}  // namespace eng

constexpr uint32_t kMaxEntries = 1024;   // a larger count is not read as a table
constexpr size_t kNameCap = 64;          // a name's copy; longer names are cut

}  // namespace detail

inline std::vector<std::string> table()
{
    namespace eng = detail::eng;
    std::vector<std::string> t;
    uintptr_t obj = *(const uintptr_t*)core::rt(eng::kTable);
    if (!core::readable((const void*)obj, eng::kHeaderBytes)) return t;
    uintptr_t arr = *(const uintptr_t*)(obj + eng::kEntries);
    uint32_t n = *(const uint32_t*)(obj + eng::kCount);
    if (n > detail::kMaxEntries || !core::readable((const void*)arr, eng::kEntryBytes * (size_t)n)) return t;
    for (uint32_t i = 0; i < n; ++i) {
        char s[detail::kNameCap];
        core::safeCopy(s, sizeof(s), *(const char* const*)(arr + eng::kEntryBytes * i));
        t.push_back(s);
    }
    return t;
}

}  // namespace engine::surfaces
