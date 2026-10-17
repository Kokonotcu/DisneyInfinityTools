// The UI screen manager: screens by name or handle, a screen's fields, a screen added by name. core. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::screens {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kManager = 0x02289E88;        // *: the screen manager
constexpr uint32_t kByName = 0x01550050;         // FUN_01550050: thiscall (name) -> handle, -1 none; reads only
constexpr uint32_t kByHandle = 0x0154FED0;       // FUN_0154fed0: thiscall (handle) -> screen, 0 none; reads only
constexpr uint32_t kRequestCtor = 0x0155A630;    // FUN_0155a630: a screen request's ctor (thiscall)
constexpr uint32_t kRequestDtor = 0x0155C270;    // FUN_0155c270: its dtor
constexpr uint32_t kCStringAssign = 0x0041A250;  // FUN_0041a250: thiscall (const char*) on a CString
constexpr uint32_t kAddScreen = 0x015699B0;      // FUN_015699b0: thiscall (request) on the manager, ret 4; -1 refused
constexpr size_t kRequestBytes = 0x100;          // the ctor sets 0x00..0x93
constexpr size_t kRequestName = 0x40;            // the request's screen name (CString)
constexpr size_t kRequestPlayer = 0x6C;
// a screen: its script name, name, movie path, player slot and its action gates (delivery FUN_01565330)
constexpr size_t kScript = 0xC, kName = 0x10, kMovie = 0x14, kPlayer = 0x50;
constexpr size_t kFocus = 0x60, kHiding = 0x6B, kHidden = 0x6C, kLoaded = 0x6D;
}  // namespace eng

// The screen manager, null before the engine makes it.
inline void* manager() { return *(void**)core::rt(eng::kManager); }

// The screen named so on mgr: its handle, -1 for none.
inline int byName(void* mgr, const char* name)
{
    typedef int(__fastcall * ByNameFn)(void* mgr, void* edx, const char* name);
    return ((ByNameFn)core::rt(eng::kByName))(mgr, nullptr, name);
}

// The screen of a handle, null for none.
inline uint8_t* byHandle(void* mgr, int handle)
{
    typedef uint8_t*(__fastcall * ByHandleFn)(void* mgr, void* edx, int handle);
    return ((ByHandleFn)core::rt(eng::kByHandle))(mgr, nullptr, handle);
}

// A string field of a live screen (kScript, kName, kMovie), copied by safeCopy.
inline void copyField(const uint8_t* screen, size_t field, char* out, size_t cap)
{
    core::safeCopy(out, cap, *(const char* const*)(screen + field));
}

// Queues screen `name` for a player as FUN_00b670a0 does, with no parent: -> the request's handle, -1 refused.
inline int addByName(const char* name, int player)
{
    typedef void(__fastcall * ThisFn)(void* self, void* edx);
    typedef void*(__fastcall * StrAssignFn)(void* str, void* edx, const char* v);
    typedef int(__fastcall * AddScreenFn)(void* ui, void* edx, void* req);
    alignas(8) uint8_t req[eng::kRequestBytes] = {};
    ((ThisFn)core::rt(eng::kRequestCtor))(req, nullptr);
    ((StrAssignFn)core::rt(eng::kCStringAssign))(req + eng::kRequestName, nullptr, name);
    *(int*)(req + eng::kRequestPlayer) = player;
    int h = ((AddScreenFn)core::rt(eng::kAddScreen))(manager(), nullptr, req);
    ((ThisFn)core::rt(eng::kRequestDtor))(req, nullptr);
    return h;
}

}  // namespace engine::screens
