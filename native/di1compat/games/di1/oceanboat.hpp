// oceanboat10: a 3.0 BoatController floats on the zone's OceanSystem sea level, as 1.0's ships did. core. Game thread.
#pragma once
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::oceanboat {

// Installs the boat update hook when c.oceanBoat10 is on; false when its hook was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_00d47900, BoatController vtable 0x1d0dbf4 slot 20 (0x1d0dc44): thiscall(controller, float dt; ret 4)
constexpr uint32_t kBoatUpdate = 0x00D47900;
// FUN_006f3a80 returns it; created by FUN_006fc350 for a zone with OceanSystemQuarterback, cleared at 0x70ef1f
constexpr uint32_t kOcean = 0x022033C0;
// FUN_006fc760, thiscall(surface, float y; ret 4) -> sea level; 1.0 GetOceanSeaLevel's 0x6d4f10, byte for byte
constexpr uint32_t kSeaLevel = 0x006FC760;
constexpr size_t kLayers = 0x58;       // surface: layer list, low bit a flag; FUN_006fc760 answers 0 without one
constexpr uintptr_t kLayerMask = ~(uintptr_t)1;
// FUN_00d06750 caches the controller's water parameters: slot 0 == 1 at +0x58, slot 1 (float3) at +0x5c
constexpr size_t kInWater = 0x58;      // FUN_00d47900 skips the whole boat step while it is 0
constexpr size_t kWaterPoint = 0x5c;   // FUN_00d46fc0: the water plane's point; only y counts (normal is gravity)
// Toy Box sets both through a water node (FUN_011917f0, net tag 'WATP'); a 1.0 zone has no such node
}  // namespace eng

typedef void(__fastcall* BoatUpdateFn)(uint8_t* controller, void* edx, float dt);
typedef float(__fastcall* SeaLevelFn)(void* surface, void* edx, float y);

struct State {
    BoatUpdateFn boatUpdate = nullptr;   // the original; set once in install()
    std::atomic<bool> logged{false};
};
inline State g_state;

// The zone's ocean surface with at least one layer, or null (Toy Box zones have no OceanSystem).
inline void* oceanSurface()
{
    // engine globals the 3.0 callers read unchecked (0x6fe813); the ocean is freed only after the global is cleared
    uint8_t* ocean = *(uint8_t**)core::rt(eng::kOcean);
    uint8_t* surface = ocean ? *(uint8_t**)ocean : nullptr;
    const bool layers = surface && (*(uintptr_t*)(surface + eng::kLayers) & eng::kLayerMask);
    return layers ? surface : nullptr;
}

// FUN_00d47900: before the boat step, the water plane at the sea level under the current one, as 1.0 floated ships.
inline void __fastcall hkBoatUpdate(uint8_t* controller, void* edx, float dt)
{
    State& s = g_state;
    if (void* surface = oceanSurface()) {
        float* point = (float*)(controller + eng::kWaterPoint);
        // ponytail: queried at the plane's own y, so it settles on one layer; per-hull y if layers stack
        point[1] = ((SeaLevelFn)core::rt(eng::kSeaLevel))(surface, nullptr, point[1]);
        controller[eng::kInWater] = 1;
        if (!s.logged.exchange(true)) core::logf("oceanboat10: a boat floats on the ocean at sea level %.2f", point[1]);
    }
    s.boatUpdate(controller, edx, dt);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.oceanBoat10) return true;
    detail::State& s = detail::g_state;
    // the sea level query the detour calls (H4)
    if (!core::prologueMatches(detail::eng::kSeaLevel, "8B4158A9FEFFFFFF7505D9EEC20400")) {
        core::logf("oceanboat10 off: sea level @ %08X prologue differs", detail::eng::kSeaLevel);
        return false;
    }
    return core::installFeature("oceanboat10", {{"boat update (oceanboat10)", detail::eng::kBoatUpdate,
                                                 "568BF18B460480B80E01000002", (void*)&detail::hkBoatUpdate,
                                                 (void**)&s.boatUpdate}});
}

}  // namespace di1::oceanboat
