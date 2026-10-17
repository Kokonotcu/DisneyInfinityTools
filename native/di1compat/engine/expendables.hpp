// The expendables package loader; the game hears of each load and of packages the engine lacks. core. Loader threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::expendables {

struct Config {
    std::function<void()> beforeLoad;                              // before every package load; empty: none
    std::function<bool(const char* path)> shipped;                 // the engine's own archives have path; empty: none
    std::function<void(const char* path, bool shipped)> loading;   // with shipped: right before the load
    std::function<void(const char* path)> loaded;                  // with shipped: right after it
};

// Installs the package hook when c gives it something to do (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

typedef void(__fastcall* ExpPkgFn)(void* ecx, void* edx, const char* path, uint32_t a2);

struct State {
    ExpPkgFn g_expPkg = nullptr;   // set once by hook() in install()
    Config cfg;                    // set once in install()
};
inline State g_state;

// FUN_0160aa30, thiscall (path, a2; ret 8): an expendables package load. Per package; threads unverified.
inline void __fastcall hkExpPkg(void* ecx, void*, const char* path, uint32_t a2)
{
    State& s = g_state;
    if (s.cfg.beforeLoad) s.cfg.beforeLoad();
    if (!s.cfg.shipped) return s.g_expPkg(ecx, nullptr, path, a2);
    char p[300];
    core::safeCopy(p, sizeof(p), path);
    s.cfg.loading(p, s.cfg.shipped(p));
    s.g_expPkg(ecx, nullptr, path, a2);
    s.cfg.loaded(p);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.beforeLoad && !c.shipped) return true;
    detail::State& s = detail::g_state;
    s.cfg = c;
    return core::hook("expendables package load", 0x0160AA30, "558BEC83E4F86AFF68????????64A1",
                      (void*)&detail::hkExpPkg, (void**)&s.g_expPkg);
}

}  // namespace engine::expendables
