// deliv10 and popin10 on 3.0's expendables package loads. engine/expendables, engine/assets, popin. Loader threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <mutex>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/assets.hpp"
#include "../../engine/expendables.hpp"
#include "config.hpp"
#include "popin.hpp"

namespace di1::expendables {

// Installs the package hook with deliv10's registration and popin10's package marks, as c switches them on.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kManager = 0x0228AA40;        // *: the expendables manager (DAT_0228aa40)
constexpr uint32_t kRegisterType = 0x0160A9C0;   // FUN_0160a9c0: thiscall (name, factory) on the manager
constexpr uint32_t kMultiFactory = 0x016049F0;   // FUN_016049f0: MultiExpendableReward's factory
constexpr size_t kManagerRead = 0x70;
}  // namespace eng

struct State {
    std::mutex mx;           // guards registered; held across the one registry call (packages load on several threads)
    bool registered = false;
};
inline State g_state;

// deliv10: 1.0's RewardType "Delivery" is the one 1.0 type 3.0's registry lacks; it runs as 3.0's Multi. Once.
inline void registerDelivery()
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    if (s.registered) return;
    void* mgr = *(void**)core::rt(eng::kManager);
    if (!mgr || !core::readable(mgr, eng::kManagerRead)) return;
    typedef void(__fastcall * RegisterFn)(void* mgr, void* edx, const char* name, void* factory);
    ((RegisterFn)core::rt(eng::kRegisterType))(mgr, nullptr, "Delivery", (void*)core::rt(eng::kMultiFactory));
    s.registered = true;
    core::logf("deliv10: RewardType Delivery registered with 3.0's MultiExpendableReward factory (FUN_016049f0)");
}

}  // namespace detail

inline bool install(const Config& c)
{
    engine::expendables::Config e;
    if (c.deliv10) e.beforeLoad = detail::registerDelivery;
    if (c.popin10) {
        e.shipped = engine::assets::shipped;
        e.loading = popin::packageLoading;
        e.loaded = popin::packageLoaded;
    }
    return engine::expendables::install(e);
}

}  // namespace di1::expendables
