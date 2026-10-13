// vfxrt.hpp: the vfx track's engine hooks. Included by di1compat.cpp after its hook()/logf()/rt()/safeCopy() helpers;
// the data side (packages, meshes) is vfx.hpp.
#pragma once
#include <intrin.h>
#include <map>
#include <mutex>

namespace vfxrt {

inline uint32_t retVa(void* ra) { return (uint32_t)((uintptr_t)ra - g_base + 0x400000u); }

// ---- 1.0's universal particle package beside 3.0's (cfg vfx_universal10) ----
// 1.0 loads 'particles/universal' for every zone but the frontend (FUN_00cc16a0) before the zone's own package
// (FUN_00f1ae90 'particles/%s'). 3.0 ships a different package under that name, so the 1.0 one was never loaded:
// 8 effects 1.0 data names exist only in it, 2 of them named by files the IN_City run of 2026-10-13 opened
// (beacon_sparkle_blue / _green: the mission beacon choreographies). It loads under the alias
// 'particles/universal10' (served from 1.0 particles/universal.zip, di1compat.cpp g_over10) right after 3.0's
// universal. 3.0's package loader FUN_00672620 keys packages by the name's hash; registration replaces the entry of an
// already known hash (effects FUN_0066ef20, emitters FUN_0066f7c0 -> FUN_00671830, params FUN_0066ef60), so the
// later package wins: 3.0 universal < 1.0 universal < the zone package, 1.0's order.
typedef uint32_t*(__cdecl* PkgFn)(const char* name, uint32_t flags);
static PkgFn g_pkg;
static volatile LONG g_uni10;

static void loadUniversal10(uint32_t flags)
{
    if (InterlockedExchange(&g_uni10, 1)) return;
    uint32_t* p = g_pkg("particles/universal10", flags);
    logf("vfx: 1.0 universal package (particles/universal10) %s", p ? "registered" : "NOT registered");
}

static uint32_t* __cdecl hkPkg(const char* name, uint32_t flags)
{
    char n[128];
    safeCopy(n, sizeof(n), name);
    bool uni = !_stricmp(n, "particles/universal"), skip = uni || !_stricmp(n, "particles/universal10") ||
                                                          !_stricmp(n, "particles/frontend");
    if (!skip) loadUniversal10(flags);   // a zone package before any universal: 1.0's order still holds
    uint32_t* r = g_pkg(name, flags);
    logf("vfx: particle package %s flags=%X caller %08X -> %s", n, flags, retVa(_ReturnAddress()), r ? "ok" : "none");
    if (uni) loadUniversal10(flags);
    return r;
}

// ---- spawn trace (cfg vfx_trace, diagnostic) ----
// Every effect and emitter the engine creates goes through three functions; each logs the creator's call site and
// the placement it gets, the first 3 times per (name, call site). Placement = 3.0 FUN_00658960 / 1.0 FUN_006d81a0
// layout: quaternion x y z w, position. 'up' is local +Y as both spawns rotate the emitter direction (1.0
// FUN_006dce20, 3.0 FUN_00673870: v' = (w2 - |q|2) v + 2 (q.v) q - 2 w (q x v)); flames and smoke are authored
// along +Y (in_city: inc_dockflamessmall direction (0.0995, 0.995, 0)), so 'up' far from (0, 1, 0) means the
// transform fed to the effect is rotated, not the effect data.
static std::mutex g_traceLock;
static std::map<uint64_t, int> g_traceSeen;
static LONG g_traceLines;

static bool readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if ((uintptr_t)p < 0x10000 || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect)
        return false;
    return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static void trace(const char* what, uint32_t key, const char* name, const float* pl, uint32_t site)
{
    {
        std::lock_guard<std::mutex> lock(g_traceLock);
        if (++g_traceSeen[(uint64_t)key << 32 | site] > 3 || g_traceLines >= 3000) return;
        ++g_traceLines;
    }
    if (!readable(pl, 28)) { logf("vfx spawn %s %08X %s caller %08X placement unreadable", what, key, name, site); return; }
    float x = pl[0], y = pl[1], z = pl[2], w = pl[3];
    logf("vfx spawn %s %08X %s caller %08X q (%.3f %.3f %.3f %.3f) pos (%.2f %.2f %.2f) up (%.3f %.3f %.3f)", what, key,
         name, site, x, y, z, w, pl[4], pl[5], pl[6], 2 * (x * y + w * z), w * w - x * x + y * y - z * z, 2 * (y * z - w * x));
}

// FUN_0065e3a0(hash, placement, info, 0): every effect (FUN_0065e5a0 / FUN_0065e6d0, the name hashed by FUN_0040bb80)
typedef void*(__cdecl* EffectFn)(uint32_t hash, const float* pl, void* info, uint32_t a4);
static EffectFn g_effect;
static void* __cdecl hkEffect(uint32_t hash, const float* pl, void* info, uint32_t a4)
{
    trace("effect", hash, "", pl, retVa(_ReturnAddress()));
    return g_effect(hash, pl, info, a4);
}

// FUN_0066a1c0(name, placement, ...): a free emitter (Meridian Particle2Emitter with Slave 0 FUN_01806080, natives)
typedef void*(__cdecl* EmitterFn)(const char* name, const float* pl, void* a3, void* a4, void* a5);
static EmitterFn g_emitter;
static void* __cdecl hkEmitter(const char* name, const float* pl, void* a3, void* a4, void* a5)
{
    char n[64];
    safeCopy(n, sizeof(n), name);
    trace("emitter", vfx::h131(n, sizeof(n)), n, pl, retVa(_ReturnAddress()));
    return g_emitter(name, pl, a3, a4, a5);
}

// FUN_0066e7f0(name, transform, secondary, ...): an emitter slaved to a transform DataNode (Particle2Emitter's
// default, Slave = 1 in 5,749 of 5,757 3.0 nodes); the node's Transform value is at +0x34 (FUN_0048d2b0 returns it)
typedef void*(__cdecl* AttachedFn)(const char* name, uint8_t* xf, uint8_t* xf2, void* a4, void* a5, void* a6);
static AttachedFn g_attached;
static void* __cdecl hkAttached(const char* name, uint8_t* xf, uint8_t* xf2, void* a4, void* a5, void* a6)
{
    char n[64];
    safeCopy(n, sizeof(n), name);
    trace("attached", vfx::h131(n, sizeof(n)), n, xf ? (const float*)(xf + 0x34) : nullptr, retVa(_ReturnAddress()));
    return g_attached(name, xf, xf2, a4, a5, a6);
}

}  // namespace vfxrt
