// DataMap records: the include and playset-unload hooks with a game's steps around them, the GameDB router. core.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "../core/enginestring.hpp"
#include "../core/hooks.hpp"

namespace engine::datamaps {

// Steps around FUN_0070d730 (a record's include, kind as FUN_006fd430 numbers it) and after a playset's unload.
struct Config {
    std::function<std::string(void* manager, const char* name, int kind)> beforeInclude;   // -> passed to after
    std::function<void(void* manager, const char* name, const std::string& before)> afterInclude;
    std::function<void(const char* name)> afterUnload;
};

// Installs the include and unload hooks as one feature (H2); false when refused.
inline bool install(const Config& c);
// The GameDB file router ([0x22a6e68], DBFileRouter); null before the engine made it.
inline void* router();
// Whether a loader named type already holds path; the router keeps no count (FUN_018da710, FUN_018d9730).
inline bool loaded(void* router, const std::string& type, const std::string& path);
// Router vtable[1](type, path, a3, a4): loads a GameDB entry; a path already loaded is skipped (FUN_018db270).
inline uint8_t load(void* router, const core::EngineString& type, const core::EngineString& path, int a3, int a4);
// Router vtable[2](type, path): releases one (FUN_018da820).
inline uint8_t release(void* router, const core::EngineString& type, const core::EngineString& path);
// FUN_00705860, thiscall (name, kind; ret 8): the manager's record, or null.
inline void* findRecord(void* manager, const char* name, int kind);

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kRouter = 0x022A6E68;       // *: the DBFileRouter
constexpr uint32_t kFindRecord = 0x00705860;
constexpr uint32_t kLoaderFind = 0x018D9730;   // thiscall (path) on a loader: its loaded-file id, or -1
constexpr int kRouteLoad = 1;                  // router vtable slots (FUN_0070c090 case 0/1, FUN_0143acc0)
constexpr int kRouteRelease = 2;
constexpr size_t kLoaders = 8;                 // router +8 loaders[], +0xc count; a loader's name at +4
constexpr size_t kLoaderCount = 0xc;
constexpr int kLoaderName = 1;
}  // namespace eng

namespace detail {

typedef uint8_t(__fastcall* IncludeFn)(void* self, void* edx, const char* name, int kind, int flag, int prog);
typedef void(__fastcall* PlaysetUnloadFn)(void* self, void* edx, const char* name);
typedef uint8_t(__fastcall* RouteFn)(void* self, void* edx, const char* type, const char* path, int a3, int a4);
typedef uint8_t(__fastcall* UnrouteFn)(void* self, void* edx, const char* type, const char* path);

struct State {
    Config steps;                               // set once in install()
    IncludeFn g_include = nullptr;              // set once by installFeature
    PlaysetUnloadFn g_playsetUnload = nullptr;
};
inline State g_state;

// FUN_0070d730, thiscall (name, kind, flag, progress; ret 0x10). Load threads, per DataMap record include.
inline uint8_t __fastcall hkInclude(void* self, void*, const char* name, int kind, int flag, int prog)
{
    State& s = g_state;
    std::string before = s.steps.beforeInclude(self, name, kind);
    uint8_t r = s.g_include(self, nullptr, name, kind, flag, prog);
    s.steps.afterInclude(self, name, before);
    return r;
}

// FUN_0070df70, thiscall (name; ret 4); its one caller (0x01440F1C) ignores EAX. Load threads, per unload.
inline void __fastcall hkPlaysetUnload(void* self, void*, const char* name)
{
    g_state.g_playsetUnload(self, nullptr, name);
    g_state.steps.afterUnload(name);
}

}  // namespace detail

inline void* router() { return *(void**)core::rt(eng::kRouter); }

inline bool loaded(void* router, const std::string& type, const std::string& path)
{
    typedef int(__fastcall * FindFn)(void* self, void* edx, const char* path);
    FindFn find = (FindFn)core::rt(eng::kLoaderFind);
    void** ls = *(void***)((uint8_t*)router + eng::kLoaders);
    int n = *(int*)((uint8_t*)router + eng::kLoaderCount);
    for (int i = 0; ls && i < n; ++i) {
        const char* nm = ls[i] ? ((const char**)ls[i])[eng::kLoaderName] : nullptr;
        if (nm && type == nm && find(ls[i], nullptr, path.c_str()) >= 0) return true;
    }
    return false;
}

inline uint8_t load(void* router, const core::EngineString& type, const core::EngineString& path, int a3, int a4)
{
    return ((detail::RouteFn)(*(void***)router)[eng::kRouteLoad])(router, nullptr, type.p, path.p, a3, a4);
}

inline uint8_t release(void* router, const core::EngineString& type, const core::EngineString& path)
{
    return ((detail::UnrouteFn)(*(void***)router)[eng::kRouteRelease])(router, nullptr, type.p, path.p);
}

inline void* findRecord(void* manager, const char* name, int kind)
{
    typedef void*(__fastcall * LookupFn)(void* self, void* edx, const char* name, int kind);
    return ((LookupFn)core::rt(eng::kFindRecord))(manager, nullptr, name, kind);
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.steps = c;
    return core::installFeature("gamedb30", {
        {"DataMap record include (gamedb30)", 0x0070D730, "6AFF68????????64A100000000", (void*)&detail::hkInclude,
         (void**)&s.g_include},
        {"DataMap playset unload (gamedb30)", 0x0070DF70, "566A0068????????8BF1", (void*)&detail::hkPlaysetUnload,
         (void**)&s.g_playsetUnload},
    });
}

}  // namespace engine::datamaps
