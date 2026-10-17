// vfx runtime: the universal10 package hook, vfx_trace probes; data side games/di1/vfx.hpp. core, games/di1.
// Engine threads.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "base/hash.hpp"
#include "core/hooks.hpp"
#include "core/log.hpp"
#include "core/memcheck.hpp"
#include "engine/assets.hpp"
#include "games/di1/config.hpp"
#include "games/di1/session.hpp"
#include "sys.hpp"

namespace vfxrt {

// core's services this file calls by their short names (declarations: N26 forbids only using namespace)
using core::hook;
using core::installFeature;
using core::logf;
using core::safeCopy;

struct State {
    di1::Config cfg;   // the keys its hooks read: set once in install(), before them
};
inline State g_state;

inline uint32_t retVa(void* ra) { return (uint32_t)((uintptr_t)ra - core::g_base + 0x400000u); }

// ---- 1.0's universal particle package beside 3.0's (cfg vfx_universal10) ----
// 1.0 loads 'particles/universal' for every zone but the frontend (FUN_00cc16a0) before the zone's own package
// (FUN_00f1ae90 'particles/%s'). 3.0 ships a different package under that name, so the 1.0 one was never loaded:
// 8 effects 1.0 data names exist only in it, 2 of them named by files the IN_City run of 2026-10-13 opened
// (beacon_sparkle_blue / _green: the mission beacon choreographies). It loads under the alias
// 'particles/universal10' (served from 1.0 particles/universal.zip, di1compat.cpp g_over10) right before the first
// 1.0 zone package (one 1.0 ships and 3.0 does not: particles/IN_City), so 3.0 levels never see it. 3.0's package
// loader FUN_00672620 keys packages by the name's hash; registration replaces the entry of an already known hash
// (effects FUN_0066ef20, emitters FUN_0066f7c0 -> FUN_00671830, params FUN_0066ef60), so the later package wins:
// 3.0 universal (loaded first, log 2026-10-13: its mesh 0f876eca 10 s before IN_City's package) < 1.0 universal <
// the zone package, 1.0's order. Key headroom: 3.0's global key table holds 30000 (FUN_00658440; over it every key
// index of the package becomes 0, FUN_00656c00); 3.0 universal 3398 + 1.0 universal 401 + in_city 1655 keys.
// Asked again before every 1.0 zone package: FUN_00672620 returns a package already listed under the name's hash,
// and loads it anew after the non-permanent sweep (flags bit 0 clear: FUN_00661c30 <- FUN_00670090 <- playset
// teardown FUN_006fd240) dropped it; a load-once latch lost it for the rest of the session.
typedef uint32_t*(__cdecl* PkgFn)(const char* name, uint32_t flags);
static PkgFn g_pkg;
static std::mutex g_uniLock;   // the engine's list lookup + insert is not atomic

static bool only10(const char* leaf)   // particles/<leaf>.zip in the 1.0 install and not in 3.0's
{
    std::string a = std::string(g_state.cfg.install10) + "\\assets\\particles\\" + leaf + ".zip";
    std::string b = engine::assets::gameDir() + "\\assets\\particles\\" + leaf + ".zip";
    return GetFileAttributesA(a.c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesA(b.c_str()) == INVALID_FILE_ATTRIBUTES;
}

static uint32_t* __cdecl hkPkg(const char* name, uint32_t flags)
{
    char n[128];
    safeCopy(n, sizeof(n), name);
    if (!_strnicmp(n, "particles/", 10) && only10(n + 10)) {
        std::lock_guard<std::mutex> lock(g_uniLock);
        uint32_t* u = g_pkg("particles/universal10", flags);
        logf("vfx: 1.0 universal package (particles/universal10) %s before %s", u ? "registered" : "NOT registered", n);
    }
    uint32_t* r = g_pkg(name, flags);
    logf("vfx: particle package %s flags=%X caller %08X -> %s", n, flags, retVa(_ReturnAddress()), r ? "ok" : "none");
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

using sys::readable;

static void trace(const char* what, uint32_t key, const char* name, const float* pl, uint32_t site, const char* note = "")
{
    {
        std::lock_guard<std::mutex> lock(g_traceLock);
        if (++g_traceSeen[(uint64_t)key << 32 | site] > 3 || g_traceLines >= 3000) return;
        ++g_traceLines;
    }
    if (!readable(pl, 28)) { logf("vfx spawn %s %08X %s caller %08X placement unreadable", what, key, name, site); return; }
    float x = pl[0], y = pl[1], z = pl[2], w = pl[3];
    logf("vfx spawn %s %08X %s caller %08X q (%.3f %.3f %.3f %.3f) pos (%.2f %.2f %.2f) up (%.3f %.3f %.3f)%s", what, key,
         name, site, x, y, z, w, pl[4], pl[5], pl[6], 2 * (x * y + w * z), w * w - x * x + y * y - z * z, 2 * (y * z - w * x),
         note);
}

// effect names hash lower-cased (3.0 FUN_0040bb80 = 1.0 FUN_0040baf0), so a traced name matches the package entry
inline uint32_t nameHash(char* n)
{
    for (char* p = n; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return hash::h131Signed(n, strlen(n));
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
    trace("emitter", nameHash(n), n, pl, retVa(_ReturnAddress()));
    return g_emitter(name, pl, a3, a4, a5);
}

// FUN_0066e7f0(name, transform, secondary, ...): an emitter slaved to a transform DataNode (Particle2Emitter's
// default, Slave = 1 in 5,749 of 5,757 3.0 nodes); the node's Transform value is at +0x34. FUN_0048d2b0 refreshes
// it first when the node's u16 +0x20 differs from its owner's (+0x1c)->+0x20 (and +0x18 is set); the trace only
// reads, so such a value is logged as 'stale' (the engine's next FUN_0048d2b0 read refreshes it).
typedef void*(__cdecl* AttachedFn)(const char* name, uint8_t* xf, uint8_t* xf2, void* a4, void* a5, void* a6);
static AttachedFn g_attached;
static void* __cdecl hkAttached(const char* name, uint8_t* xf, uint8_t* xf2, void* a4, void* a5, void* a6)
{
    char n[64];
    safeCopy(n, sizeof(n), name);
    const char* note = "";
    if (xf && readable(xf + 0x18, 10)) {
        const uint8_t* o = *(const uint8_t* const*)(xf + 0x1c);
        if (*(const void* const*)(xf + 0x18) && readable(o + 0x20, 2) && *(const uint16_t*)(xf + 0x20) != *(const uint16_t*)(o + 0x20))
            note = " stale";
    }
    trace("attached", nameHash(n), n, xf ? (const float*)(xf + 0x34) : nullptr, retVa(_ReturnAddress()), note);
    return g_attached(name, xf, xf2, a4, a5, a6);
}

// The vfx runtime's hooks (H1); the universal10 alias is in the overlay before the package hook can ask for it.
inline bool install(const di1::Config& c)
{
    g_state.cfg = c;
    bool ok = true;
    if (c.vfx10 && c.vfxUniversal10) {
        di1::session::Files alias;
        for (const char* e : {".particledata", ".mtb"})   // the alias hkPkg loads -> 1.0 particles/universal.zip
            alias.push_back({std::string("particles/universal10") + e,
                             std::string("particles/universal/universal") + e});
        di1::session::addOverlay(alias);   // AssetOpen is live already
        ok &= hook("particle package load (vfx)", 0x00672620, "6AFF68????????64A100000000", (void*)&hkPkg,
                   (void**)&g_pkg);
    }
    if (c.vfxTrace)
        ok &= installFeature("vfx_trace", {
            {"effect create (vfx trace)", 0x0065E3A0, "8B54240C8B4A18", (void*)&hkEffect, (void**)&g_effect},
            {"emitter create (vfx trace)", 0x0066A1C0, "6AFF68????????64A100000000", (void*)&hkEmitter,
             (void**)&g_emitter},
            {"slaved emitter create (vfx trace)", 0x0066E7F0, "6AFF68????????64A100000000", (void*)&hkAttached,
             (void**)&g_attached},
        });
    return ok;
}

}  // namespace vfxrt
