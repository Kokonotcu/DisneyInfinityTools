// Hooks as data, their installer (H1, H2) and rt(). MinHook, core/log.hpp. Installing runs under the loader lock.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../minhook/MinHook.h"
#include "log.hpp"

namespace core {

static_assert(sizeof(void*) == 4, "the game is 32-bit x86: addresses and offsets assume it");

namespace eng {
constexpr uint32_t kImageBase = 0x400000u;   // DisneyInfinity3.exe's preferred base: every static address assumes it
}

struct HookSpec {
    const char* name;          // stable: it appears in the log
    uint32_t staticVa;         // address in the exe at image base 0x400000
    const char* prologueHex;   // the bytes the target must start with; "??" skips one byte
    void* detour;
    void** original;
};

// Installs every hook of the feature, or none of them. Returns whether the feature is on.
typedef bool (*InstallFeatureFn)(const char* feature, const std::vector<HookSpec>& hooks);

inline uintptr_t g_base;   // runtime base of DisneyInfinity3.exe (ASLR); set once in Pentane_Main, before any hook
inline uintptr_t rt(uint32_t staticVa) { return g_base + (staticVa - eng::kImageBase); }
// rt's inverse: the static address of a runtime exe address.
inline uint32_t toStaticVa(uintptr_t v) { return (uint32_t)(v - g_base + eng::kImageBase); }

// Whether the target still starts with the analysed bytes. '??' skips a byte that ASLR relocates.
inline bool prologueMatches(uint32_t staticVa, const char* prologueHex)
{
    const uint8_t* target = (const uint8_t*)rt(staticVa);
    size_t i = 0;
    for (const char* p = prologueHex; p[0] && p[1]; p += 2, ++i) {
        if (p[0] == '?') continue;
        unsigned v;
        sscanf_s(p, "%2x", &v);
        if (target[i] != (uint8_t)v) break;
    }
    return i * 2 == strlen(prologueHex);
}

// Installs a detour only if the target's prologue matches. A refused hook leaves *original null.
inline bool hook(const char* name, uint32_t staticVa, const char* prologueHex, void* detour, void** original)
{
    if (!prologueMatches(staticVa, prologueHex)) {
        logf("hook %s @ %08X refused: prologue differs", name, staticVa);
        return false;
    }
    const uint8_t* target = (const uint8_t*)rt(staticVa);
    MH_STATUS s = MH_CreateHook((LPVOID)target, detour, original);
    if (s == MH_OK) s = MH_EnableHook((LPVOID)target);
    logf("hook %s @ %08X %s (%d)", name, staticVa, s == MH_OK ? "ok" : "refused", (int)s);
    return s == MH_OK;
}

// Takes one hook back out (a no-op for a hook MinHook never created) and clears its original.
inline void unhook(const HookSpec& h)
{
    LPVOID target = (LPVOID)rt(h.staticVa);
    MH_DisableHook(target);
    MH_RemoveHook(target);
    *h.original = nullptr;
}

// Installs all of a feature's hooks or none (CLAUDE.md H2). Success logs exactly what one hook() call per row would.
inline bool installFeature(const char* feature, const std::vector<HookSpec>& hooks)
{
    for (const HookSpec& h : hooks) {
        if (prologueMatches(h.staticVa, h.prologueHex)) continue;
        logf("hook %s @ %08X refused: prologue differs", h.name, h.staticVa);
        logf("feature %s off: hook %s refused, none of its hooks installed", feature, h.name);
        return false;
    }
    for (size_t i = 0; i < hooks.size(); ++i) {
        const HookSpec& h = hooks[i];
        if (hook(h.name, h.staticVa, h.prologueHex, h.detour, h.original)) continue;
        // MinHook failed after the prologue check: take this feature's hooks out again
        for (size_t k = 0; k <= i; ++k) unhook(hooks[k]);
        logf("feature %s off: hook %s refused, none of its hooks installed", feature, h.name);
        return false;
    }
    return true;
}

}  // namespace core
