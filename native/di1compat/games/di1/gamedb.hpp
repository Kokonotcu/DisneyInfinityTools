// The 1.0 GameDB supplements (PS-5 locks and bitmasks, GLB-1 chests and perceptions). engine/gamedb, sys.hpp.
#pragma once
#include <cstdint>
#include <string>
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/gamedb.hpp"
#include "../../sys.hpp"
#include "config.hpp"

namespace di1::gamedb {

// Installs the GameDB loader hooks with the 1.0 supplements c switches on (locks10, chest10, bits10, perc10).
inline bool install(const Config& c);

namespace detail {

struct State {
    bool locks10 = false, chest10 = false, bits10 = false, perc10 = false;   // set once in install()
};
inline State g_state;

// PS-5: the records the Locks DB lacks from that table's 1.0 version and from 1.0's core table.
inline void lockSupplements(const engine::gamedb::LoadCall& call, const char* p)
{
    for (std::string s : {std::string(sys::kLocks10Prefix) + p, std::string(sys::kLocks10Core)}) {
        sys::setLockUids(sys::loaderUids(call.loader));
        engine::gamedb::loadLocks({call.loader, s.c_str(), call.a2, call.a3});
    }
}

// GLB-1: 1.0's ChestInfo.bin through 3.0's chestInfo loader.
inline void chestSupplement(const engine::gamedb::LoadCall& call)
{
    namespace eng = engine::gamedb::eng;
    void* loader = engine::gamedb::singletonLoader(eng::kChestInfo);
    if (!loader) return;
    uint8_t ok = engine::gamedb::cbLoad(loader, sys::kChest10, call.a2, call.a3);
    uint8_t *ci = *(uint8_t**)core::rt(eng::kChestInfo), *chests = ci + eng::kChestBlock;
    core::logf("chestInfo loader given %s -> %u; ChestInfo holds %d chests, %d resources, %d choreographies",
               sys::kChest10, ok, *(int*)(chests + 4), *(int*)(ci + eng::kChestResources),
               *(int*)(ci + eng::kChestChoreographies));
}

// After the locks loader: a 1.0 playset DataMap's locks table brings the locks and chest supplements.
inline void afterLocks(const engine::gamedb::LoadCall& call)
{
    const State& s = g_state;
    char p[300];
    core::safeCopy(p, sizeof(p), call.path);
    const bool playset10 = sys::dbTrigger10(p, "locks__lua.lua");
    if (s.locks10 && playset10) lockSupplements(call, p);
    if (s.chest10 && playset10) chestSupplement(call);
}

// GLB-1: the 1.0 perceptions 3.0 lacks, whose actor types the 1.0 bitmasks register.
inline void perceptionSupplement(const engine::gamedb::LoadCall& call)
{
    namespace eng = engine::gamedb::eng;
    void* pl = engine::gamedb::singletonLoader(eng::kPerceptionInfo);
    const uint8_t* pi = pl ? *(uint8_t**)core::rt(eng::kPerceptionInfo) : nullptr;
    if (!pi || !sys::snapshotPerc30(pi)) return;
    int before = *(const int*)(pi + eng::kPerceptionCount);
    uint8_t ok = engine::gamedb::cbLoad(pl, sys::kPerc10, call.a2, call.a3);
    core::logf("perceptions loader given %s -> %u; PerceptionInfo holds %d perceptions (%d before)", sys::kPerc10, ok,
               *(const int*)(pi + eng::kPerceptionCount), before);
}

// After a Lua callback loader: a 1.0 playset DataMap's bitmasks bring 1.0's core masks and perceptions.
inline void afterCallbackLoad(const engine::gamedb::LoadCall& call)
{
    const State& s = g_state;
    char p[300];
    core::safeCopy(p, sizeof(p), call.path);
    if (!sys::dbTrigger10(p, "bitmasks.lua")) return;
    if (s.bits10) {
        uint8_t ok = engine::gamedb::cbLoad(call.loader, sys::kBits10, call.a2, call.a3);
        core::logf("bitMask loader given %s after %s -> %u", sys::kBits10, p, ok);
    }
    if (s.perc10) perceptionSupplement(call);
}

}  // namespace detail

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
