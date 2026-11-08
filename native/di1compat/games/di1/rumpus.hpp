// 3.0's RumpusSpawn path: a Toy Box object by name, placed by a matrix. engine/zones. Spawn and Meridian threads.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../engine/zones.hpp"

namespace di1::rumpus {

// A spawn's result: the handle (0: none) and whether an object stands behind it
struct Spawned {
    uint32_t handle;
    bool object;
    uint32_t zone;
};

// 3.0's RumpusSpawn calls (FUN_00aa1350) for object name at matrix m (translation m[12..14]); zone 0 when unknown.
inline Spawned spawn(const char* name, const float* m);
// Whether the calls spawn() makes still have their analysed prologues (H4); false logs nothing.
inline bool callsMatch();

namespace detail {

namespace eng {
constexpr uint32_t kZoneAt = 0x01390E30;     // FUN_01390e30: thiscall (out zone, pos, hint) on the zone manager
constexpr uint32_t kStrCtor = 0x00418310;    // FUN_00418310: a StringT's ctor (thiscall)
constexpr uint32_t kSpawner = 0x00A9DE70;    // FUN_00a9de70: cdecl () -> what RumpusSpawn creates through
constexpr uint32_t kCreate = 0x00F7E3E0;     // FUN_00f7e3e0: create by name (1.0's FUN_00d1da30), ret 0x1c
constexpr uint32_t kObject = 0x00F32070;     // FUN_00f32070: cdecl (handle, 1) -> the object
constexpr uint32_t kHasFlag = 0x00F9E170;    // FUN_00f9e170: thiscall (bit) on an object
constexpr uint32_t kMove = 0x00F9DBD0;       // FUN_00f9dbd0: cdecl (handle, matrix, notify): a live object
constexpr uint32_t kMatrixCopy = 0x00445930; // FUN_00445930: thiscall (src) on a matrix
constexpr uint32_t kLiveFlag = 0xC;          // the object is live: move it, else copy its matrix
constexpr size_t kObjectMatrix = 0x40;
constexpr size_t kActivate = 0x1C / 4;       // the object's vt slot of activate(1), RumpusSpawn's last step
constexpr size_t kTranslation = 12;          // m[12..14]: the matrix's position
}  // namespace eng

struct Call {
    uint32_t va;
    const char* bytes;
};
inline constexpr Call kCalls[] = {
    {eng::kZoneAt, "538BD98B83CC010000"},       {eng::kStrCtor, "8B44240456508BF1E8"},
    {eng::kSpawner, "64A1000000006AFF68"},      {eng::kCreate, "6AFF68????????64A100000000"},
    {eng::kObject, "518B0D????????85C974"},     {eng::kHasFlag, "8B4424045081C194000000"},
    {eng::kMove, "8B4424046A0150E8"},           {eng::kMatrixCopy, "8BC18B4C2404D901D918"},
};

typedef void(__fastcall* ZoneAtFn)(void* self, void* edx, uint32_t* out, const float* pos, uint32_t hint);
typedef void*(__cdecl* SpawnerFn)();
typedef uint32_t*(__fastcall* CreateFn)(void* self, void* edx, uint32_t* out, char* name, uint32_t parent,
                                        uint32_t zone, uint32_t a5, uint32_t a6, uint32_t a7);
typedef uint8_t*(__cdecl* ObjFn)(uint32_t handle, uint32_t a2);
typedef uint8_t(__fastcall* FlagFn)(void* self, void* edx, uint32_t bit);
typedef void(__cdecl* MoveFn)(uint32_t handle, const float* m, uint32_t notify);
typedef void*(__fastcall* MatCopyFn)(void* self, void* edx, const float* src);
typedef void*(__fastcall* StrCtorFn)(void* self, void* edx, const char* s);
typedef void(__fastcall* ActivateFn)(void* self, void* edx, uint32_t on);

}  // namespace detail

inline bool callsMatch()
{
    for (const detail::Call& k : detail::kCalls)
        if (!core::prologueMatches(k.va, k.bytes)) return false;
    return true;
}

inline Spawned spawn(const char* name, const float* m)
{
    namespace eng = detail::eng;
    const float* t = m + eng::kTranslation;
    alignas(16) float pos[4] = {t[0], t[1], t[2], 1.f};
    uint32_t zone = 0;
    void* zones = engine::zones::manager();
    if (zones) ((detail::ZoneAtFn)core::rt(eng::kZoneAt))(zones, nullptr, &zone, pos, 0);
    char* str = nullptr;   // StringT by value: the callee releases it
    ((detail::StrCtorFn)core::rt(eng::kStrCtor))(&str, nullptr, name);
    uint32_t h = 0;
    // parent 0, a5..a7 = 0, 1, 0: as RumpusSpawn passes them
    void* spawner = ((detail::SpawnerFn)core::rt(eng::kSpawner))();
    ((detail::CreateFn)core::rt(eng::kCreate))(spawner, nullptr, &h, str, 0, zone, 0, 1, 0);
    uint8_t* obj = h ? ((detail::ObjFn)core::rt(eng::kObject))(h, 1) : nullptr;
    if (!obj) return {h, false, zone};
    if (((detail::FlagFn)core::rt(eng::kHasFlag))(obj, nullptr, eng::kLiveFlag))
        ((detail::MoveFn)core::rt(eng::kMove))(h, m, 1);
    else ((detail::MatCopyFn)core::rt(eng::kMatrixCopy))(obj + eng::kObjectMatrix, nullptr, m);
    obj = ((detail::ObjFn)core::rt(eng::kObject))(h, 1);
    if (obj) ((detail::ActivateFn)(*(void***)obj)[eng::kActivate])(obj, nullptr, 1);
    return {h, true, zone};
}

}  // namespace di1::rumpus
