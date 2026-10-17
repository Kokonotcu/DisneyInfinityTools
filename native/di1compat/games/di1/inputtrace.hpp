// input10 and vehicle10 probes (locktrace10): their shared line budget, the vehicle takeover probe. Main thread.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/zones.hpp"
#include "config.hpp"
#include "playsets.hpp"

namespace di1::inputtrace {

// Installs the vehicle takeover probe when c has lockTrace10 (H1); false when it was refused.
inline bool install(const Config& c);
// Whether one more input10 / vehicle10 line may go out: lockTrace10 is on and the budget lasts.
inline bool room();

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kToyBox = 0x0139E6E0;     // FUN_0139e6e0: thiscall (zone) on the zone manager: is it ToyBox
constexpr size_t kControlVehicle = 0xF0;     // a player control's vehicle
}  // namespace eng

constexpr long kBudget = 600;   // lines, never reset

typedef void(__fastcall* VehicleCtlFn)(uint8_t* ctl, void* edx, void* vehicle);
typedef char(__fastcall* ToyBoxFn)(void* zm, void* edx, int zone);

struct State {
    bool on = false;                       // lockTrace10: set once in install()
    std::atomic<long> lines{0};            // budget used; any thread
    VehicleCtlFn g_vehicleCtl = nullptr;   // set once by hook() in install()
};
inline State g_state;

// FUN_00d612a0, thiscall (vehicle; ret 4), main thread, per takeover: the one place 3.0 sets Allow_UIRibbon.
inline void __fastcall hkVehicleCtl(uint8_t* ctl, void* edx, void* vehicle)
{
    State& s = g_state;
    void* old = ctl ? *(void**)(ctl + eng::kControlVehicle) : nullptr;
    s.g_vehicleCtl(ctl, edx, vehicle);
    if (!s.on || !playsets::currentZone10() || !room()) return;
    void* zm = engine::zones::manager();
    char toy = zm ? ((ToyBoxFn)core::rt(eng::kToyBox))(zm, nullptr, 0) : 0;
    core::logf("vehicle10: player control %p takes vehicle %p (was %p); zone PlaysetProperties ToyBox %d -> 3.0 %s "
               "Allow_UIRibbon (Tab = ShowRibbonUI needs it)",
               ctl, vehicle, old, toy, vehicle && toy ? "may set" : "does not set");
}

}  // namespace detail

inline bool room()
{
    detail::State& s = detail::g_state;
    return s.on && ++s.lines <= detail::kBudget;
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.on = c.lockTrace10;
    if (!c.lockTrace10) return true;
    return core::hook("vehicle control takeover (vehicle10 probe)", 0x00D612A0, "6AFF68????????64A100000000",
                      (void*)&detail::hkVehicleCtl, (void**)&s.g_vehicleCtl);
}

}  // namespace di1::inputtrace
