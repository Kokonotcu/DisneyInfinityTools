// Asset defs fallback: DefID misses answered from gamedb/incredibles/assetdefs.lua. core, engine/assets. Any thread.
#pragma once
#include <windows.h>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/assets.hpp"
#include "config.hpp"

namespace di1::assetdefs {

// Installs the asset defs hook, always (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr LONG kLogLimit = 20;   // fallback answers logged
constexpr size_t kKeyLog = 64;   // the logged key's copy
}  // namespace eng

typedef const char*(__fastcall* AssetDefFn)(void* self, void* edx, const char* key);

struct State {
    AssetDefFn g_assetDef = nullptr;   // set once by hook() in install()
    std::mutex mx;                     // guards defs; never held across the engine read (L3, A-38)
    std::map<std::string, std::string> defs;   // guarded by mx; lowercase DefID -> FileName; nodes never move (S5)
    std::atomic<bool> loaded{false};           // set under mx once defs is published, never cleared; read without it
    std::atomic<LONG> logged{0};               // fallback answers so far
};
inline State g_state;

// The quoted value after name in a Lua table block; "" when there is none.
inline std::string fieldOf(const std::string& blk, const char* name)
{
    const size_t i = blk.find(name);
    if (i == std::string::npos) return std::string();
    const size_t q = blk.find('"', i);
    const size_t e = q == std::string::npos ? q : blk.find('"', q + 1);
    return e == std::string::npos ? std::string() : blk.substr(q + 1, e - q - 1);
}

// The Cho entries of assetdefs.lua text t into defs (lowercase DefID -> FileName).
inline void addChoDefs(const std::string& t, std::map<std::string, std::string>& defs)
{
    for (size_t a = t.find('{', t.find("AssetDefs")); a != std::string::npos; a = t.find('{', a + 1)) {
        const size_t b = t.find('}', a);
        if (b == std::string::npos) break;
        const std::string blk = t.substr(a, b - a);
        std::string id = fieldOf(blk, "DefID");
        const std::string file = fieldOf(blk, "FileName");
        if (fieldOf(blk, "AssetType") != "Cho" || id.empty()) continue;
        for (char& c : id) c = (char)tolower((unsigned char)c);
        defs[id] = file;
    }
}

// assetdefs.lua's Cho entries, read at the first miss (the engine runs by then) with no lock held (L3).
inline std::map<std::string, std::string> readDefs()
{
    std::map<std::string, std::string> defs;
    std::string t;
    if (engine::assets::read30("gamedb/incredibles/assetdefs.lua", t)) addChoDefs(t, defs);
    return defs;
}

// Publishes defs once: the first build to finish wins, a later one is dropped. Its count is logged once.
inline void publishDefs(std::map<std::string, std::string> defs)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    if (s.loaded.load()) return;
    s.defs.swap(defs);
    s.loaded.store(true);
    core::logf("asset defs fallback: %u Cho entries from gamedb/incredibles/assetdefs.lua", (unsigned)s.defs.size());
}

// The Incredibles package loads by the fixed-list path, which never opens its assetdefs.lua: key's Cho from here.
inline const char* fallbackDef(const char* key)
{
    State& s = g_state;
    std::string k = key ? key : "";
    for (char& c : k) c = (char)tolower((unsigned char)c);
    if (!s.loaded.load()) publishDefs(readDefs());
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.defs.find(k);
    return it == s.defs.end() ? nullptr : it->second.c_str();
}

// fallbackDef under __try, nullptr when it faulted; no objects with destructors here (H7).
inline const char* guardedFallback(const char* key)
{
    __try {
        return fallbackDef(key);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// FUN_00703340, thiscall on the AssetDefsManager (key; ret 4): a DefID's Cho FileName, "" when none. Per lookup.
inline const char* __fastcall hkAssetDef(void* self, void*, const char* key)
{
    State& s = g_state;
    const char* r = s.g_assetDef(self, nullptr, key);
    if (!r || *r) return r;
    const char* f = guardedFallback(key);
    if (!f) return r;
    const LONG nth = ++s.logged;
    if (nth > eng::kLogLimit) return f;
    char k[eng::kKeyLog] = "";
    core::safeCopy(k, sizeof(k), key);
    core::logf("assetdef '%s' -> '%s' (fallback)", k, f);
    return f;
}

}  // namespace detail

inline bool install(const Config&)
{
    return core::hook("asset defs lookup", 0x00703340, "8B44240456", (void*)&detail::hkAssetDef,
                      (void**)&detail::g_state.g_assetDef);
}

}  // namespace di1::assetdefs
