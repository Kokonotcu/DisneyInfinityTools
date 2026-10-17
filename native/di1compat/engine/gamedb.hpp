// GameDB loaders: Lua tables through CHD-mode loaders, CHD callback loads, a game's supplements after a table. core.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include "../base/text.hpp"
#include "../core/enginestring.hpp"
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
// The UIDs of every record a set loader's sets hold (FUN_01612fa0 merges by DBName); unreadable parts skipped.
inline std::set<uint32_t> loaderUids(const void* loader);
// PerceptionInfo's perception names, and its Groups records by GroupName (first wins); false if unreadable.
inline bool perceptionNames(const uint8_t* pi, std::set<std::string>& names,
                            std::map<std::string, std::string>& groups);

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
constexpr size_t kLoaderMode = 0x2d;              // a loader's byte +0x2d: 1 CHD, 0 Lua (FUN_01612fa0)
constexpr int kVfLoad = 1;                        // DBFileLoader_Callback vtable[1]: load, as FUN_018da710 calls it
constexpr size_t kSingletonLoader = 4;
constexpr size_t kLoaderSets = 0xc;        // a set loader: sets[] +0xc, count +0x10 (FUN_01612fa0)
constexpr size_t kLoaderSetCount = 0x10;
constexpr size_t kSetPool = 0x30;          // a set's record pool (FUN_01611a30 makes it)
constexpr size_t kPoolUids = 0x30;         // a pool: UID[] +0x30, count +0x34 (FUN_0149f900, FUN_01611cb0)
constexpr size_t kPoolCount = 0x34;
constexpr int kMaxSets = 64;               // plausibility caps of the walks
constexpr int kMaxRecords = 200000;
constexpr int kMaxListRecords = 100000;
constexpr size_t kPerceptionList = 0x100;  // PerceptionInfo: Perceptions +0x100 (count +0x104), record +0 name
constexpr size_t kGroupList = 0xe0;        // Groups +0xe0 (count +0xe4), record +0 name (callback FUN_014afd30)
constexpr size_t kGroupName = 8;           // a Groups record's GroupName
constexpr size_t kRecordHead = 12;         // the record bytes read: name, a word, GroupName
constexpr size_t kNameCap = 256;
constexpr uintptr_t kPage = 0x1000;
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
    typedef uint8_t(__fastcall * LoadFn)(void* self, void* edx, char* path, uint32_t a2, uint32_t a3);
    core::EngineString cs(path);
    return ((LoadFn)(*(void***)loader)[detail::eng::kVfLoad])(loader, nullptr, cs.p, a2, a3);
}

inline void* singletonLoader(uint32_t global)
{
    uint8_t* o = *(uint8_t**)core::rt(global);
    const bool held = o && core::readable(o + detail::eng::kSingletonLoader, 4);
    void* loader = held ? *(void**)(o + detail::eng::kSingletonLoader) : nullptr;
    return loader && core::readable(loader, 4) ? loader : nullptr;
}

namespace detail {

// A name under 256 bytes at p. Each page is checked once: readability is per page (SY-03).
inline bool engineName(const void* p, std::string& s)
{
    if (!core::readable(p, 1)) return false;
    const char* c = (const char*)p;
    size_t k = 0;
    while (k < eng::kNameCap) {
        const bool newPage = k > 0 && ((uintptr_t)(c + k) & (eng::kPage - 1)) == 0;
        if (newPage && !core::readable(c + k, 1)) break;
        if (!c[k]) break;
        ++k;
    }
    s.assign(c, k);
    return k < eng::kNameCap;
}

// A record list of PerceptionInfo at pi + at: its records and count; false if unreadable.
inline bool recordList(const uint8_t* pi, size_t at, void* const*& arr, int& cnt)
{
    if (!core::readable(pi + at, 8)) return false;
    arr = *(void* const**)(pi + at);
    cnt = *(const int*)(pi + at + 4);
    if (cnt < 0 || cnt > eng::kMaxListRecords) return false;
    return cnt == 0 || core::readable(arr, cnt * sizeof(void*));
}

// A set's record UIDs into u (FUN_0149f900 stores each record's UID at its index).
inline void addPoolUids(const void* set, std::set<uint32_t>& u)
{
    const uint8_t* s = (const uint8_t*)set;
    if (!core::readable(s + eng::kSetPool, 4)) return;
    const uint8_t* pool = *(const uint8_t* const*)(s + eng::kSetPool);
    if (!core::readable(pool + eng::kPoolUids, 8)) return;
    const uint32_t* ids = *(const uint32_t* const*)(pool + eng::kPoolUids);
    int n = *(const int*)(pool + eng::kPoolCount);
    if (n <= 0 || n > eng::kMaxRecords || !core::readable(ids, n * sizeof(uint32_t))) return;
    u.insert(ids, ids + n);
}

}  // namespace detail

inline std::set<uint32_t> loaderUids(const void* loader)
{
    namespace eng = detail::eng;
    std::set<uint32_t> u;
    const uint8_t* l = (const uint8_t*)loader;
    if (!core::readable(l + eng::kLoaderSets, 8)) return u;
    void* const* sets = *(void* const**)(l + eng::kLoaderSets);
    int ns = *(const int*)(l + eng::kLoaderSetCount);
    if (ns <= 0 || ns > eng::kMaxSets || !core::readable(sets, ns * sizeof(void*))) return u;
    for (int s = 0; s < ns; ++s) detail::addPoolUids(sets[s], u);
    return u;
}

inline bool perceptionNames(const uint8_t* pi, std::set<std::string>& names, std::map<std::string, std::string>& groups)
{
    namespace eng = detail::eng;
    void* const* arr;
    int cnt;
    if (!detail::recordList(pi, eng::kPerceptionList, arr, cnt)) return false;
    for (int i = 0; i < cnt; ++i) {
        const uint8_t* r = (const uint8_t*)arr[i];
        std::string a;
        if (core::readable(r, eng::kRecordHead) && detail::engineName(*(void* const*)r, a)) names.insert(a);
    }
    if (!detail::recordList(pi, eng::kGroupList, arr, cnt)) return false;
    for (int i = 0; i < cnt; ++i) {
        const uint8_t* r = (const uint8_t*)arr[i];
        std::string a, g;
        if (!core::readable(r, eng::kRecordHead) || !detail::engineName(*(void* const*)r, a)) continue;
        if (detail::engineName(*(void* const*)(r + eng::kGroupName), g)) groups.emplace(g, a);
    }
    return true;
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
