// The 1.0 GameDB supplements (PS-5 locks and bitmasks, GLB-1 chests and perceptions). engine/gamedb. Loader threads.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include "../../base/text.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/gamedb.hpp"
#include "config.hpp"
#include "perceptions.hpp"

namespace di1::gamedb {

// The paths the supplements are served under (serve.hpp's sysServe); a .bin is asked for as .lua.
inline constexpr const char* kLocks10Prefix = "di1sys/locks10/";
inline constexpr const char* kLocks10Core = "di1sys/locks10/gamedb/core/locks__lua.lua";
inline constexpr const char* kChest10Prefix = "di1sys/chest10/";
inline constexpr const char* kChest10 = "di1sys/chest10/var/chests/chestinfo.lua";
inline constexpr const char* kPerc10Prefix = "di1sys/perc10/";
inline constexpr const char* kPerc10 = "di1sys/perc10/var/perceptions/perceptioninfo.lua";
inline constexpr const char* kRaw10Prefix = "di1sys/raw10/";   // + a 1.0 path 3.0 also has: the 1.0 bytes
inline constexpr const char* kBits10 = "di1sys/raw10/gamedb/core/bitmasks.lua";

// Installs the GameDB loader hooks with the 1.0 supplements c switches on (locks10, chest10, bits10, perc10).
inline bool install(const Config& c);
// A served DataMap's entry: "GameDB|<path>|.." names a table whose load brings the 1.0 supplements.
inline void dbTrigger10Add(const std::string& entry);
// path, as its loader got it, is such a table, and its file name is file ("locks__lua.lua").
inline bool dbTrigger10(const char* path, const char* file);
// Every table dbTrigger10Add has seen, lower case.
inline std::set<std::string> dbTriggers10();
// The Locks DB's UIDs when the last lock supplement started; its open may run on another thread.
inline std::set<uint32_t> lockUids();
// What 3.0's PerceptionInfo held when the last perceptions supplement started.
inline perceptions::Perc30 perc30();

namespace detail {

struct State {
    bool locks10 = false, chest10 = false, bits10 = false, perc10 = false;   // set once in install()
    std::mutex mx;                              // guards the four below; a leaf lock on cold paths
    std::set<std::string> triggers;             // never cleared: DataMaps are served once per name
    std::set<uint32_t> lockUids;
    std::set<std::string> perc30;
    std::map<std::string, std::string> group30;   // GroupName -> its first 3.0 Groups record, as 3.0's lookup
};
inline State g_state;

inline void setLockUids(std::set<uint32_t> u)
{
    std::lock_guard<std::mutex> l(g_state.mx);
    g_state.lockUids.swap(u);
}

// PS-5: the records the Locks DB lacks from that table's 1.0 version and from 1.0's core table.
inline void lockSupplements(const engine::gamedb::LoadCall& call, const char* p)
{
    for (std::string s : {std::string(kLocks10Prefix) + p, std::string(kLocks10Core)}) {
        setLockUids(engine::gamedb::loaderUids(call.loader));
        engine::gamedb::loadLocks({call.loader, s.c_str(), call.a2, call.a3});
    }
}

// GLB-1: 1.0's ChestInfo.bin through 3.0's chestInfo loader.
inline void chestSupplement(const engine::gamedb::LoadCall& call)
{
    namespace eng = engine::gamedb::eng;
    void* loader = engine::gamedb::singletonLoader(eng::kChestInfo);
    if (!loader) return;
    uint8_t ok = engine::gamedb::cbLoad(loader, kChest10, call.a2, call.a3);
    uint8_t *ci = *(uint8_t**)core::rt(eng::kChestInfo), *chests = ci + eng::kChestBlock;
    core::logf("chestInfo loader given %s -> %u; ChestInfo holds %d chests, %d resources, %d choreographies",
               kChest10, ok, *(int*)(chests + 4), *(int*)(ci + eng::kChestResources),
               *(int*)(ci + eng::kChestChoreographies));
}

// After the locks loader: a 1.0 playset DataMap's locks table brings the locks and chest supplements.
inline void afterLocks(const engine::gamedb::LoadCall& call)
{
    const State& s = g_state;
    char p[300];
    core::safeCopy(p, sizeof(p), call.path);
    const bool playset10 = dbTrigger10(p, "locks__lua.lua");
    if (s.locks10 && playset10) lockSupplements(call, p);
    if (s.chest10 && playset10) chestSupplement(call);
}

// PerceptionInfo's names, kept for the open that serves the supplement; false (nothing kept) if unreadable.
inline bool snapshotPerc30(const uint8_t* pi)
{
    std::set<std::string> names;
    std::map<std::string, std::string> groups;
    if (!engine::gamedb::perceptionNames(pi, names, groups)) return false;
    std::lock_guard<std::mutex> l(g_state.mx);
    g_state.perc30.swap(names);
    g_state.group30.swap(groups);
    return true;
}

// GLB-1: the 1.0 perceptions 3.0 lacks, sent once the bitmasks registered their actor types.
inline void perceptionSupplement(const engine::gamedb::LoadCall& call)
{
    namespace eng = engine::gamedb::eng;
    void* pl = engine::gamedb::singletonLoader(eng::kPerceptionInfo);
    const uint8_t* pi = pl ? *(uint8_t**)core::rt(eng::kPerceptionInfo) : nullptr;
    if (!pi || !snapshotPerc30(pi)) return;
    int before = *(const int*)(pi + eng::kPerceptionCount);
    uint8_t ok = engine::gamedb::cbLoad(pl, kPerc10, call.a2, call.a3);
    core::logf("perceptions loader given %s -> %u; PerceptionInfo holds %d perceptions (%d before)", kPerc10, ok,
               *(const int*)(pi + eng::kPerceptionCount), before);
}

// After a Lua callback loader: a 1.0 playset DataMap's bitmasks bring 1.0's core masks and perceptions.
inline void afterCallbackLoad(const engine::gamedb::LoadCall& call)
{
    const State& s = g_state;
    char p[300];
    core::safeCopy(p, sizeof(p), call.path);
    if (!dbTrigger10(p, "bitmasks.lua")) return;
    if (s.bits10) {
        uint8_t ok = engine::gamedb::cbLoad(call.loader, kBits10, call.a2, call.a3);
        core::logf("bitMask loader given %s after %s -> %u", kBits10, p, ok);
    }
    if (s.perc10) perceptionSupplement(call);
}

}  // namespace detail

inline void dbTrigger10Add(const std::string& entry)
{
    constexpr char kKind[] = "GameDB|";
    constexpr size_t k = sizeof(kKind) - 1;
    if (entry.compare(0, k, kKind)) return;
    size_t bar = entry.find('|', k);
    std::string p = text::slashLower(entry.substr(k, bar == std::string::npos ? std::string::npos : bar - k));
    std::lock_guard<std::mutex> l(detail::g_state.mx);
    detail::g_state.triggers.insert(p);
}

inline bool dbTrigger10(const char* path, const char* file)
{
    std::string p = text::slashLower(path);
    size_t n = strlen(file);
    if (p.size() <= n || p.compare(p.size() - n, n, file) || p[p.size() - n - 1] != '/') return false;
    std::lock_guard<std::mutex> l(detail::g_state.mx);
    return detail::g_state.triggers.count(p) != 0;
}

inline std::set<std::string> dbTriggers10()
{
    std::lock_guard<std::mutex> l(detail::g_state.mx);
    return detail::g_state.triggers;
}

inline std::set<uint32_t> lockUids()
{
    std::lock_guard<std::mutex> l(detail::g_state.mx);
    return detail::g_state.lockUids;
}

inline perceptions::Perc30 perc30()
{
    std::set<std::string> names;
    std::map<std::string, std::string> groups;
    {
        std::lock_guard<std::mutex> l(detail::g_state.mx);
        names = detail::g_state.perc30;
        groups = detail::g_state.group30;
    }
    return {[names](const std::string& s) { return names.count(s) != 0; },
            [groups](const std::string& s) {
                auto it = groups.find(s);
                return it == groups.end() ? std::string() : it->second;
            }};
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.locks10 = c.locks10;
    s.chest10 = c.chest10;
    s.bits10 = c.bits10;
    s.perc10 = c.perc10;
    engine::gamedb::Config g;
    g.afterLocks = detail::afterLocks;
    if (c.bits10 || c.perc10) g.afterCallbackLoad = detail::afterCallbackLoad;
    return engine::gamedb::install(g);
}

}  // namespace di1::gamedb
