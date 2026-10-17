// The VR's remembered character per playset: one without a 3.0 slot keeps the selected one. core. Thread unverified.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::playsetslot {

struct Config {};

// Installs the slot hook (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_00b7f910 has slots for these only; elsewhere FUN_00b83b90 clears the character every frame ("Missing Figure").
constexpr const char* kSlots[] = {"IGP_PLAYSET_Empire", "IGP_PLAYSET_InsideOut", "IGP_PLAYSET_TheCloneWars",
                                  "IGP_PLAYSET_PlaysetX"};
constexpr size_t kKeyMax = 100;
}  // namespace eng

typedef uint8_t(__fastcall* PlaysetCharFn)(void* self, void* edx, const char* key, int sku);

struct State {
    PlaysetCharFn g_playsetChar = nullptr;   // set once by hook() in install()
};
inline State g_state;

// FUN_00b7f910, thiscall (key "IGP_PLAYSET_<playset>", sku; ret 8): a remembered character. Per frame, FUN_00b83b90.
inline uint8_t __fastcall hkPlaysetChar(void* self, void*, const char* key, int sku)
{
    char k[eng::kKeyMax];
    core::safeCopy(k, sizeof(k), key);
    for (const char* slot : eng::kSlots)
        if (!_stricmp(k, slot)) return g_state.g_playsetChar(self, nullptr, key, sku);
    return 1;
}

}  // namespace detail

inline bool install(const Config&)
{
    return core::hook("VR playset character slot", 0x00B7F910, "6AFF68????????64A100000000",
                      (void*)&detail::hkPlaysetChar, (void**)&detail::g_state.g_playsetChar);
}

}  // namespace engine::playsetslot
