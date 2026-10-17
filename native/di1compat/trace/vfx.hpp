// vfx_trace probe: each effect and emitter spawn's call site and placement, logged. core, base/hash. Spawn threads.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../base/hash.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"

namespace trace::vfx {

struct Config {
    bool on = false;   // vfx_trace
};

// Installs the three spawn probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr size_t kPlacement = 28;           // quaternion x y z w, then position (3.0 FUN_00658960, 1.0 FUN_006d81a0)
constexpr size_t kNodeStamp = 0x20;         // a transform node's u16 stamp; its owner's at (+0x1c)->+0x20
constexpr size_t kNodeDirty = 0x18, kNodeOwner = 0x1C;
constexpr size_t kNodeTransform = 0x34;     // the node's Transform value
}  // namespace eng

constexpr int kPerSite = 3;      // lines per (name, call site)
constexpr long kLines = 3000;    // lines in all

typedef void*(__cdecl* EffectFn)(uint32_t hash, const float* pl, void* info, uint32_t a4);
typedef void*(__cdecl* EmitterFn)(const char* name, const float* pl, void* a3, void* a4, void* a5);
typedef void*(__cdecl* AttachedFn)(const char* name, uint8_t* xf, uint8_t* xf2, void* a4, void* a5, void* a6);

struct State {
    // originals: set once by installFeature in install()
    EffectFn g_effect = nullptr;
    EmitterFn g_emitter = nullptr;
    AttachedFn g_attached = nullptr;
    core::LogCount<uint64_t> seen;   // lines per (name hash, call site)
    std::atomic<long> lines{0};      // lines so far; any thread
};
inline State g_state;

// One placement line. 'up' is local +Y, as both games rotate an emitter (3.0 FUN_00673870): flames point up.
inline void logPlacement(const char* what, uint32_t key, const char* name, const float* pl, uint32_t site,
                         const char* note = "")
{
    State& s = g_state;
    if (s.lines.load() >= kLines) return;   // RE-37: nothing is counted once the budget is spent
    if (!s.seen.first((uint64_t)key << 32 | site, kPerSite)) return;
    if (++s.lines > kLines) return;
    if (!core::readable(pl, eng::kPlacement)) {
        core::logf("vfx spawn %s %08X %s caller %08X placement unreadable", what, key, name, site);
        return;
    }
    float x = pl[0], y = pl[1], z = pl[2], w = pl[3];
    core::logf("vfx spawn %s %08X %s caller %08X q (%.3f %.3f %.3f %.3f) pos (%.2f %.2f %.2f) up (%.3f %.3f %.3f)%s",
               what, key, name, site, x, y, z, w, pl[4], pl[5], pl[6], 2 * (x * y + w * z),
               w * w - x * x + y * y - z * z, 2 * (y * z - w * x), note);
}

// Effect names hash lower-cased (3.0 FUN_0040bb80 = 1.0 FUN_0040baf0): n is lowered in place, so it logs lower case.
inline uint32_t nameHash(char* n)
{
    for (char* p = n; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return hash::h131Signed(n, strlen(n));
}

// FUN_0065e3a0, cdecl (hash, placement, info, 0), per effect spawn (FUN_0065e5a0 / FUN_0065e6d0).
inline void* __cdecl hkEffect(uint32_t hash, const float* pl, void* info, uint32_t a4)
{
    logPlacement("effect", hash, "", pl, core::toStaticVa((uintptr_t)_ReturnAddress()));
    return g_state.g_effect(hash, pl, info, a4);
}

// FUN_0066a1c0, cdecl (name, placement, ...), per free emitter spawn (Particle2Emitter with Slave 0, natives).
inline void* __cdecl hkEmitter(const char* name, const float* pl, void* a3, void* a4, void* a5)
{
    char n[64];
    core::safeCopy(n, sizeof(n), name);
    logPlacement("emitter", nameHash(n), n, pl, core::toStaticVa((uintptr_t)_ReturnAddress()));
    return g_state.g_emitter(name, pl, a3, a4, a5);
}

// FUN_0066e7f0, cdecl (name, transform node, ...), per emitter spawn slaved to a node (5,749 of 5,757 3.0 nodes).
inline void* __cdecl hkAttached(const char* name, uint8_t* xf, uint8_t* xf2, void* a4, void* a5, void* a6)
{
    char n[64];
    core::safeCopy(n, sizeof(n), name);
    const char* note = "";
    // FUN_0048d2b0 refreshes a value whose stamp differs from its owner's first; this only reads: logged as stale
    if (xf && core::readable(xf + eng::kNodeDirty, 10)) {
        const uint8_t* o = *(const uint8_t* const*)(xf + eng::kNodeOwner);
        const bool dirty = *(const void* const*)(xf + eng::kNodeDirty) && core::readable(o + eng::kNodeStamp, 2);
        const bool stale = dirty && *(const uint16_t*)(xf + eng::kNodeStamp) != *(const uint16_t*)(o + eng::kNodeStamp);
        if (stale) note = " stale";
    }
    const float* pl = xf ? (const float*)(xf + eng::kNodeTransform) : nullptr;
    logPlacement("attached", nameHash(n), n, pl, core::toStaticVa((uintptr_t)_ReturnAddress()), note);
    return g_state.g_attached(name, xf, xf2, a4, a5, a6);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("vfx_trace", {
        {"effect create (vfx trace)", 0x0065E3A0, "8B54240C8B4A18", (void*)&detail::hkEffect, (void**)&s.g_effect},
        {"emitter create (vfx trace)", 0x0066A1C0, "6AFF68????????64A100000000", (void*)&detail::hkEmitter,
         (void**)&s.g_emitter},
        {"slaved emitter create (vfx trace)", 0x0066E7F0, "6AFF68????????64A100000000", (void*)&detail::hkAttached,
         (void**)&s.g_attached},
    });
}

}  // namespace trace::vfx
