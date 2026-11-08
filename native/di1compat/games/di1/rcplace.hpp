// rcplace10: the Toy Store card list skips a catalog item lacking a place record.
// core. Main thread.
#pragma once
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::rcplace {

// Installs the catalog item guard when c.rcplace10 is on; false when its hook was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_0149f580, thiscall(index, a2; ret 8): the RC catalog's item at index, or null
constexpr uint32_t kItemAt = 0x0149F580;
// return address of its call in Place_GetRCCardItems (FUN_00fd45a0), which then reads [[item+0x34]+8] unchecked
constexpr uint32_t kCardItemsCall = 0x00FD472E;
constexpr size_t kPlace = 0x34;   // the item's place record (FUN_0165f390); its +8 is the place name
constexpr size_t kNameSlot = 0x28 / sizeof(void*);   // vtable slot: -> char** name (0xfd4738)
constexpr int kLogCap = 24;       // skipped items named in the log
}  // namespace eng

typedef uint8_t*(__fastcall* ItemAtFn)(void* catalog, void* edx, int index, int a2);
typedef const char**(__fastcall* ItemNameFn)(uint8_t* item, void* edx);

struct State {
    ItemAtFn itemAt = nullptr;   // the original; set once in install()
    std::atomic<int> logged{0};
};
inline State g_state;

// Cars' Toy Store: a placeless item faulted at 0xfd481f (read 8). The loop skips null items itself.
inline uint8_t* __fastcall hkItemAt(void* catalog, void* edx, int index, int a2)
{
    State& s = g_state;
    uint8_t* item = s.itemAt(catalog, edx, index, a2);
    if (!item || _ReturnAddress() != (void*)core::rt(eng::kCardItemsCall)) return item;
    if (*(void**)(item + eng::kPlace)) return item;
    if (s.logged.fetch_add(1) < eng::kLogCap) {
        const char** name = ((ItemNameFn)(*(void***)item)[eng::kNameSlot])(item, nullptr);
        core::logf("rcplace10: Toy Store card list skips item %d '%s': no place record", index,
                   name && *name ? *name : "?");
    }
    return nullptr;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.rcplace10) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("rcplace10", {{"RC catalog item at (rcplace10)", detail::eng::kItemAt,
                                               "8B49088B493085C9740D", (void*)&detail::hkItemAt,
                                               (void**)&s.itemAt}});
}

}  // namespace di1::rcplace
