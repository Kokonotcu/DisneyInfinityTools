// GameDB loaders: Lua tables through CHD-mode loaders, CHD callback loads, a game's supplements after a table. core.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include "../base/text.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"

namespace engine::gamedb {

// A loader call as the engine makes it (thiscall on the loader: path, a2, a3).
struct LoadCall {
    void* loader;
    const char* path;
    uint32_t a2, a3;
};

typedef std::function<void(const LoadCall& call)> AfterLoad;

struct Config {
    AfterLoad afterLocks;          // the game's supplements after the locks loader read a table; empty: none
    AfterLoad afterCallbackLoad;   // ... after a Lua callback loader read one; empty: that hook is not installed
};

// Installs the loader hooks (H1); false when one was refused.
inline bool install(const Config& c);
// The locks loader on call.path, in its Lua mode for a .lua path (for supplements).
inline uint32_t loadLocks(const LoadCall& call);
// A Lua callback loader (DBFileLoader_Callback vtable[1], FUN_018db270) given path as an engine CString.
inline uint8_t cbLoad(void* loader, const char* path, uint32_t a2, uint32_t a3);
// A 3.0 singleton's DBFileLoader_Callback (eng::kChestInfo, eng::kPerceptionInfo); null if unreadable.
inline void* singletonLoader(uint32_t global);
// Whether p is the .lua a CHD callback loader reads now: its open must answer the table compiled to CHD.
inline bool isChdPath(const char* p);

namespace eng {
constexpr uint32_t kChestInfo = 0x022896FC;       // *: ChestInfo; its loader at +4
constexpr uint32_t kPerceptionInfo = 0x02289700;  // *: PerceptionInfo; its loader at +4
constexpr size_t kChestBlock = 0x1c;              // ChestInfo +0x1c: the chests (count at its +4)
constexpr size_t kChestResources = 0x10;
constexpr size_t kChestChoreographies = 0x30;
constexpr size_t kPerceptionCount = 0x104;
}  // namespace eng

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kCStringCtor = 0x00418310;     // FUN_00418310: thiscall (const char*) on a CString
constexpr uint32_t kCStringRelease = 0x00418230;  // FUN_00418230: thiscall on a CString
constexpr size_t kLoaderMode = 0x2d;              // a loader's byte +0x2d: 1 CHD, 0 Lua (FUN_01612fa0)
constexpr int kVfLoad = 1;                        // DBFileLoader_Callback vtable[1]: load, as FUN_018da710 calls it
constexpr size_t kSingletonLoader = 4;
}  // namespace eng

typedef uint32_t(__fastcall* GameDbLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);
typedef uint8_t(__fastcall* CbLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);

struct State {
    // originals: set once by hook() in install()
    GameDbLoadFn g_loadLocks = nullptr, g_loadInventory = nullptr;
    CbLoadFn g_loadChdCallback = nullptr, g_cbLoad = nullptr;
    AfterLoad afterLocks, afterCallbackLoad;   // set once in install()
    std::mutex chdMx;                          // guards chdPath
    std::string chdPath;                       // process-wide: the engine opens files on other threads (A-18)
};
inline State g_state;

// A CHD-mode loader runs in its Lua mode for one call given a .lua path (a DataMap may name Lua).
inline uint32_t luaModeLoad(GameDbLoadFn orig, const char* tag, const LoadCall& c)
{
    char p[300];
    core::safeCopy(p, sizeof(p), c.path);
    uint8_t* mode = (uint8_t*)c.loader + eng::kLoaderMode;
    if (!*mode || !text::endsWith(p, ".lua")) return orig(c.loader, nullptr, c.path, c.a2, c.a3);
    *mode = 0;
    uint32_t r = orig(c.loader, nullptr, c.path, c.a2, c.a3);
    *mode = 1;
    core::logf("gamedb %s %s: CHD loader run in its Lua mode -> %u", tag, p, r);
    return r;
}

// FUN_01612fa0, thiscall (path, a2, a3): the locks loader; then the game's supplements. Rare (per table).
inline uint32_t __fastcall hkLoadLocks(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    uint32_t r = luaModeLoad(s.g_loadLocks, "locks", {self, path, a2, a3});
    if (s.afterLocks) s.afterLocks({self, path, a2, a3});
    return r;
}

// FUN_014c08d0, thiscall (path, a2, a3): the inventory loader. Rare (per table).
inline uint32_t __fastcall hkLoadInventory(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    return luaModeLoad(g_state.g_loadInventory, "inventory", {self, path, a2, a3});
}

// FUN_018db450, thiscall (ret 0xc): a CHD-only callback loader; a .lua given to it is opened as CHD (hkOpen's stage).
inline uint8_t __fastcall hkLoadChdCallback(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    char p[300];
    core::safeCopy(p, sizeof(p), path);
    if (!text::endsWith(p, ".lua")) return s.g_loadChdCallback(self, nullptr, path, a2, a3);
    {
        std::lock_guard<std::mutex> lock(s.chdMx);
        s.chdPath = text::norm(p);
    }
    uint8_t r = s.g_loadChdCallback(self, nullptr, path, a2, a3);
    {
        std::lock_guard<std::mutex> lock(s.chdMx);
        s.chdPath.clear();
    }
    core::logf("gamedb CHD callback loader given %s -> %u", p, r);
    return r;
}

// FUN_018db270, thiscall (CString path, a2, a3; ret 0xc): every Lua callback loader; then the game's supplements.
inline uint8_t __fastcall hkCbLoad(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    uint8_t r = s.g_cbLoad(self, nullptr, path, a2, a3);
    s.afterCallbackLoad({self, path, a2, a3});
    return r;
}

}  // namespace detail

inline uint32_t loadLocks(const LoadCall& call)
{
    return detail::luaModeLoad(detail::g_state.g_loadLocks, "locks", call);
}

// The path goes in as a CString: the loader add-refs the header at path-0xc (FUN_00caa0d0), a char* would be written.
inline uint8_t cbLoad(void* loader, const char* path, uint32_t a2, uint32_t a3)
{
    typedef void*(__fastcall * CtorFn)(void* str, void* edx, const char* s);
    typedef uint8_t(__fastcall * LoadFn)(void* self, void* edx, char* path, uint32_t a2, uint32_t a3);
    typedef void(__fastcall * ReleaseFn)(void* str, void* edx);
    char* cs = nullptr;
    ((CtorFn)core::rt(detail::eng::kCStringCtor))(&cs, nullptr, path);
    uint8_t ok = ((LoadFn)(*(void***)loader)[detail::eng::kVfLoad])(loader, nullptr, cs, a2, a3);
    ((ReleaseFn)core::rt(detail::eng::kCStringRelease))(&cs, nullptr);
    return ok;
}

inline void* singletonLoader(uint32_t global)
{
    uint8_t* o = *(uint8_t**)core::rt(global);
    const bool held = o && core::readable(o + detail::eng::kSingletonLoader, 4);
    void* loader = held ? *(void**)(o + detail::eng::kSingletonLoader) : nullptr;
    return loader && core::readable(loader, 4) ? loader : nullptr;
}

inline bool isChdPath(const char* p)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.chdMx);
    return !s.chdPath.empty() && text::norm(p) == s.chdPath;
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.afterLocks = c.afterLocks;
    s.afterCallbackLoad = c.afterCallbackLoad;
    bool ok = core::hook("GameDB locks load", 0x01612FA0, "6AFF64A100000000", (void*)&detail::hkLoadLocks,
                         (void**)&s.g_loadLocks);
    ok &= core::hook("GameDB inventory load", 0x014C08D0, "6AFF64A100000000", (void*)&detail::hkLoadInventory,
                     (void**)&s.g_loadInventory);
    ok &= core::hook("GameDB CHD callback load", 0x018DB450, "6AFF64A100000000", (void*)&detail::hkLoadChdCallback,
                     (void**)&s.g_loadChdCallback);
    if (c.afterCallbackLoad)
        ok &= core::hook("GameDB callback load", 0x018DB270, "6AFF64A10000000068????????50", (void*)&detail::hkCbLoad,
                         (void**)&s.g_cbLoad);
    return ok;
}

}  // namespace engine::gamedb
