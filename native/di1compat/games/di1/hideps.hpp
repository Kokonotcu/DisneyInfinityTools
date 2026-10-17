// hide_ps (experiment): the actor registry denies AV_<name>_PS, so avatars fall back. core, base. Thread unverified.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "../../base/text.hpp"
#include "../../core/hooks.hpp"
#include "../../core/memcheck.hpp"
#include "config.hpp"

namespace di1::hideps {

// Installs the actor registry hook for hide_ps without trace, whose own probe denies them otherwise (H1).
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr size_t kNameMax = 200;   // the name's copy, as trace's probe takes it
}  // namespace eng

typedef uint8_t(__fastcall* ActorExistsFn)(void* self, void* edx, const char** name);

struct State {
    ActorExistsFn g_actorExists = nullptr;   // set once by hook() in install()
};
inline State g_state;

// FUN_00751750, thiscall (ret 4): the actor registry's "does <name> exist" (the playset avatar choice asks it).
inline uint8_t __fastcall hkActorExists(void* self, void*, const char** name)
{
    const uint8_t r = g_state.g_actorExists(self, nullptr, name);
    char n[eng::kNameMax];
    core::safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    const bool hidden = text::endsWith(n, "_PS") && !strncmp(n, "AV_", 3);
    return hidden ? 0 : r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.hidePs || c.trace) return true;
    return core::hook("actor exists (hide_ps)", 0x00751750, "518B4424088B00", (void*)&detail::hkActorExists,
                      (void**)&detail::g_state.g_actorExists);
}

}  // namespace di1::hideps
