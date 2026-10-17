// The zone manager: its current zone, a zone's fields and name, zones by name. core. Main and loader threads.
#pragma once
#include <cstdint>
#include "../core/hooks.hpp"

namespace engine::zones {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kZoneManager = 0x0225F548;   // *: the zone manager
constexpr size_t kCurrentZone = 0x19C;          // the manager's current zone id
constexpr uint32_t kZoneString = 0x0139E0B0;    // FUN_0139e0b0: thiscall (key, zone, a3, default) -> a string field
constexpr uint32_t kZoneByName = 0x01391150;    // FUN_01391150: thiscall (uint32_t* out, name) -> the zone's id
constexpr uint32_t kZoneFlag = 0x0139DE80;      // FUN_0139de80: thiscall (key, zone, a3, default) -> a bool field
constexpr uint32_t kZoneName = 0x01391040;      // FUN_01391040: thiscall (zone, a2) -> the zone's name
}  // namespace eng

// The zone manager, null before the engine makes it.
inline void* manager() { return *(void**)core::rt(eng::kZoneManager); }

// zm's current zone id; 0 without one.
inline uint32_t currentZone(const void* zm)
{
    return zm ? *(const uint32_t*)((const uint8_t*)zm + eng::kCurrentZone) : 0;
}

// A zone's string field as the engine reads it; through the zone string hook when one is installed (B-16).
inline const char* zoneString(void* zm, const char* key, uint32_t zone, const char* def)
{
    typedef const char*(__fastcall * StrFn)(void* self, void* edx, const char* key, uint32_t zone, uint32_t a3,
                                            const char* def);
    return ((StrFn)core::rt(eng::kZoneString))(zm, nullptr, key, zone, 0, def);
}

// A zone's bool field as the engine reads it: lua_gettable (FUN_0139be30), so the key's spelling counts.
inline bool zoneFlag(void* zm, const char* key, uint32_t zone)
{
    typedef char(__fastcall * FlagFn)(void* self, void* edx, const char* key, uint32_t zone, uint32_t a3, uint32_t def);
    return ((FlagFn)core::rt(eng::kZoneFlag))(zm, nullptr, key, zone, 0, 0) != 0;
}

// A zone's name. a2 = 1, as tools10 passes it; its meaning is not analysed.
inline const char* zoneName(void* zm, uint32_t zone)
{
    typedef const char*(__fastcall * NameFn)(void* self, void* edx, uint32_t zone, int a2);
    return ((NameFn)core::rt(eng::kZoneName))(zm, nullptr, zone, 1);
}

// Whether zm has registered a zone of that name.
inline bool zoneRegistered(void* zm, const char* name)
{
    typedef void(__fastcall * ByNameFn)(void* self, void* edx, uint32_t* out, const char* name);
    uint32_t id = 0;
    ((ByNameFn)core::rt(eng::kZoneByName))(zm, nullptr, &id, name);
    return id != 0;
}

}  // namespace engine::zones
