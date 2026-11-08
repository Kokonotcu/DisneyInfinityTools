// An actor's world position by handle, as ActorGetPosition (0x74b790) reads it. core. Game thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::actorpos {

namespace eng {
constexpr uint32_t kHandles = 0x02202A38;   // the handle table FUN_00492e20 reads (deliveries.hpp)
// FUN_00492e20: thiscall (uint32_t* out, handle*) -> entry; callers check this prologue once (H4)
constexpr uint32_t kResolve = 0x00492E20;
constexpr const char* kResolvePrologue = "8B4424088B0085C07503C20800";
constexpr size_t kEntryActor = 8;           // a resolved entry: the actor
constexpr size_t kActorType = 6;            // u16 & 0x3ff: the actor type (0x74b7cd)
constexpr uint16_t kTypeMask = 0x3ff;
constexpr uint32_t kAvatarType = 0x0228E4AC;   // u16: the avatar actor type (deliveries.hpp)
constexpr size_t kAvatarPos = 0x268;        // an avatar's position (0x74b7ef)
constexpr size_t kActorPos = 0x2c;          // any other actor's (0x74b7f7)
}  // namespace eng

constexpr int kVec = 3;

typedef uint8_t*(__fastcall* ResolveFn)(void* handles, void* edx, uint32_t* out, const uint32_t* handle);

// ponytail: skips the FUN_01887040 refresh (actor +0x51 bit 0); an avatar may read a frame old

// The position into out; false when a step is null or unreadable (H8).
inline bool of(uint32_t handle, float* out)
{
    void* handles = *(void**)core::rt(eng::kHandles);
    uint32_t slot = 0;
    const uint8_t* entry = handles ? ((ResolveFn)core::rt(eng::kResolve))(handles, nullptr, &slot, &handle) : nullptr;
    if (!entry || !core::readable(entry + eng::kEntryActor, sizeof(void*))) return false;
    const uint8_t* actor = *(uint8_t* const*)(entry + eng::kEntryActor);
    if (!actor || !core::readable(actor + eng::kActorType, sizeof(uint16_t))) return false;
    const uint16_t type = *(const uint16_t*)(actor + eng::kActorType) & eng::kTypeMask;
    const size_t at = type == *(const uint16_t*)core::rt(eng::kAvatarType) ? eng::kAvatarPos : eng::kActorPos;
    if (!core::readable(actor + at, kVec * sizeof(float))) return false;
    for (int i = 0; i < kVec; ++i) out[i] = ((const float*)(actor + at))[i];
    return true;
}

}  // namespace engine::actorpos
