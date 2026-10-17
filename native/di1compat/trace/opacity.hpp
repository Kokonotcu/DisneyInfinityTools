// opacitylog10 probe: the primitives the opacity test drops. Uses core. Render-path threads, one lock.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include "../core/hooks.hpp"
#include "../core/log.hpp"

namespace trace::opacity {

struct Config {
    bool on = false;   // opacitylog10
};

// Installs the probe when c asks for it (H1); false when its hook was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_0044ad80 reads these to decide (opacity parameter * object fade < 1 skips a primitive)
constexpr uint32_t kPrimShader = 0x10;      // primitive +0x10: its .shd; the .shd's first word is its name
constexpr uint32_t kShdOpacityWord = 0x50;  // .shd +0x50: opacity word; its high 16 bits are an instance data offset
constexpr uint32_t kShdSizeA = 0x6a;        // .shd +0x6a: a u16 logged as sizeA
constexpr int kDumpWords = 12;              // instance floats and primitive words dumped per skipped primitive
}  // namespace eng

constexpr uint32_t kMaxPrims = 32;   // the result is a 32-bit mask
constexpr size_t kLinesLogged = 300;   // distinct lines (mixed objects and mask results together)
constexpr int kDumpsLogged = 6;        // skipped primitives dumped in full

typedef uint32_t(__cdecl* OpacityMaskFn)(uint32_t n, float fade, uint32_t prims, uint32_t insts);

struct State {
    OpacityMaskFn g_opacityMask = nullptr;   // set once by hook() in install()
    std::mutex mx;                           // guards seen and dumped; lines are logged under it
    std::set<std::string> seen;              // lines logged so far
    int dumped = 0;                          // skipped primitives dumped so far
};
inline State g_state;

inline const char* shaderName(const uint8_t* shd) { return shd && *(const char**)shd ? *(const char**)shd : "?"; }

inline const uint8_t* primShader(uint32_t prims, uint32_t i, uint32_t* prim)
{
    *prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
    return *(uint8_t**)(uintptr_t)(*prim + eng::kPrimShader);
}

// The primitive's opacity parameter value (1 without one), read where FUN_0044ad80 reads it.
inline float opacityValue(uint32_t w, uint32_t insts, uint32_t i)
{
    return w ? *(float*)(uintptr_t)((w >> 16) + *(uint32_t*)(uintptr_t)(insts + 4 * i)) : 1.0f;
}

// One object, some primitives skipped and some kept: every primitive in one line.
inline std::string mixedLine(uint32_t n, uint32_t r, uint32_t prims, uint32_t insts)
{
    std::string line;
    for (uint32_t i = 0; i < n && i < kMaxPrims; ++i) {
        uint32_t prim;
        const uint8_t* shd = primShader(prims, i, &prim);
        uint32_t w = shd ? *(uint32_t*)(shd + eng::kShdOpacityWord) : 0;
        // never cut: at most 75 characters (a float's %.3f is at most 44)
        char e[96];
        snprintf(e, sizeof(e), " [%u %.8s w%08x v%.3f %s]", i, shaderName(shd), w, opacityValue(w, insts, i),
                 (r >> i) & 1 ? "SKIP" : "keep");
        line += e;
    }
    return line;
}

// A skipped primitive's instance floats and primitive words. Caller holds mx.
inline void dumpSkipped(uint32_t prim, const uint8_t* shd, const float* inst)
{
    std::string f, pr;
    for (int k = 0; k < eng::kDumpWords; ++k) {
        char e[16];
        snprintf(e, sizeof(e), " %.3f", inst[k]);
        f += e;
    }
    for (int k = 0; k < eng::kDumpWords; ++k) {
        char e[16];
        snprintf(e, sizeof(e), " %08x", ((uint32_t*)(uintptr_t)prim)[k]);
        pr += e;
    }
    core::logf("skipped primitive %s sizeA %u: instance floats%s | prim words%s", shaderName(shd),
               shd ? *(uint16_t*)(shd + eng::kShdSizeA) : 0, f.c_str(), pr.c_str());
}

// The mixed line once, then the first skipped primitives in full.
inline void logMixed(uint32_t n, float fade, uint32_t r, uint32_t prims, uint32_t insts)
{
    State& s = g_state;
    std::string line = mixedLine(n, r, prims, insts);
    std::lock_guard<std::mutex> lock(s.mx);
    const bool first = s.seen.size() < kLinesLogged && s.seen.insert(line).second;
    if (first) core::logf("opacity mixed: fade %.3f%s", fade, line.c_str());
    for (uint32_t i = 0; i < n && i < kMaxPrims && s.dumped < kDumpsLogged; ++i) {
        if (!((r >> i) & 1)) continue;
        uint32_t prim;
        const uint8_t* shd = primShader(prims, i, &prim);
        dumpSkipped(prim, shd, *(float**)(uintptr_t)(insts + 4 * i));
        ++s.dumped;
    }
}

// Each skipped or kept primitive's deciding values, once per distinct line.
inline void logMask(uint32_t n, float fade, uint32_t r, uint32_t prims, uint32_t insts)
{
    State& s = g_state;
    for (uint32_t i = 0; r && i < n && i < kMaxPrims; ++i) {
        uint32_t prim;
        const uint8_t* shd = primShader(prims, i, &prim);
        const char* nm = shd ? *(const char**)shd : nullptr;
        uint32_t w = shd ? *(uint32_t*)(shd + eng::kShdOpacityWord) : 0;
        float v = opacityValue(w, insts, i);
        char key[160];
        snprintf(key, sizeof(key), "%s %08x %.3f %.3f %u", nm ? nm : "(null)", w, v, fade, (r >> i) & 1);
        std::lock_guard<std::mutex> lock(s.mx);
        const bool first = s.seen.size() < kLinesLogged && s.seen.insert(key).second;
        if (first)
            core::logf("opacity mask: %s (word %08x: value %.3f, object fade %.3f) skip %u", nm ? nm : "(null)", w, v,
                       fade, (r >> i) & 1);
    }
}

// FUN_0044ad80, cdecl: bit i = primitive i faded (FUN_00461020 skips it). Render path, thread unverified; per object.
inline uint32_t __cdecl hkOpacityMask(uint32_t n, float fade, uint32_t prims, uint32_t insts)
{
    uint32_t r = g_state.g_opacityMask(n, fade, prims, insts);
    uint32_t all = n >= kMaxPrims ? 0xffffffffu : (1u << n) - 1;
    if (r && (r & all) != all) logMixed(n, fade, r, prims, insts);
    logMask(n, fade, r, prims, insts);
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    return core::hook("opacity mask (log)", 0x0044AD80, "83EC088B442418F30F105424", (void*)&detail::hkOpacityMask,
                      (void**)&detail::g_state.g_opacityMask);
}

}  // namespace trace::opacity
