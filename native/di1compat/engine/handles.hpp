// Actor handles to actors through the engine's handle table, every pointer checked (H8). core. Any engine thread.
#pragma once
#include <cstdint>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::handles {

// The actor behind a handle, null when the handle does not resolve or a step is unreadable.
inline uint8_t* actorOf(uint32_t handle);

namespace eng {
constexpr uint32_t kHandles = 0x02202A38;   // the handle table FUN_00492e20 reads (FUN_0074b200, FUN_01876690)
constexpr uint32_t kResolve = 0x00492E20;   // FUN_00492e20: thiscall (uint32_t* out, handle*) -> entry
constexpr size_t kEntryActor = 8;           // a resolved entry: the actor
}  // namespace eng

namespace detail {
typedef uint8_t*(__fastcall* ResolveFn)(void* handles, void* edx, uint32_t* out, const uint32_t* handle);
}  // namespace detail

// H4: callers check FUN_00492e20's prologue in their install() ("8B4424088B0085C07503C20800")
inline uint8_t* actorOf(uint32_t handle)
{
    void* handles = *(void**)core::rt(eng::kHandles);
    if (!handle || !handles) return nullptr;
    uint32_t slot = 0;
    uint8_t* entry = ((detail::ResolveFn)core::rt(eng::kResolve))(handles, nullptr, &slot, &handle);
    if (!entry || !core::readable(entry + eng::kEntryActor, sizeof(void*))) return nullptr;
    return *(uint8_t**)(entry + eng::kEntryActor);
}

}  // namespace engine::handles
