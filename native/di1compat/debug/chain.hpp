// di1debug hooks over targets di1compat may already hook. MinHook, core. Installed in Pentane_Main.
#pragma once
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../minhook/MinHook.h"

namespace dbg::chain {

// A loaded module's address range; empty when the module is not loaded.
struct Module {
    uintptr_t lo = 0, hi = 0;
    bool contains(uintptr_t a) const { return a >= lo && a < hi; }
};

// The range of a loaded DLL by file name.
inline Module module(const char* name);
// Whether staticVa starts with a jmp rel32 into m: MinHook's x86 patch writes the detour address directly.
inline bool jumpsInto(uint32_t staticVa, const Module& m);
// All of a watch's hooks or none (H2). A row may match its prologue or jump into compat.
inline bool installWatch(const char* watch, const std::vector<core::HookSpec>& hooks, const Module& compat);

namespace eng {
constexpr uint8_t kJmpRel32 = 0xE9;   // MinHook's x86 patch: E9 rel32 straight to the detour (hook.c, no relay)
constexpr size_t kJmpBytes = 5;
}  // namespace eng

inline Module module(const char* name)
{
    Module m;
    HMODULE h = GetModuleHandleA(name);
    MODULEINFO mi{};
    if (!h || !K32GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) return m;
    m.lo = (uintptr_t)mi.lpBaseOfDll;
    m.hi = m.lo + mi.SizeOfImage;
    return m;
}

inline bool jumpsInto(uint32_t staticVa, const Module& m)
{
    const uint8_t* p = (const uint8_t*)core::rt(staticVa);
    if (p[0] != eng::kJmpRel32) return false;
    const uintptr_t dest = (uintptr_t)p + eng::kJmpBytes + *(const int32_t*)(p + 1);
    return m.contains(dest);
}

namespace detail {

// MinHook direct: core::hook refuses a target di1compat already patched (CLAUDE.md H1 exception for di1debug).
inline bool enable(const core::HookSpec& h)
{
    LPVOID target = (LPVOID)core::rt(h.staticVa);
    MH_STATUS s = MH_CreateHook(target, h.detour, h.original);
    if (s == MH_OK) s = MH_EnableHook(target);
    core::logf("hook %s @ %08X %s (%d)", h.name, h.staticVa, s == MH_OK ? "ok" : "refused", (int)s);
    return s == MH_OK;
}

inline bool fits(const core::HookSpec& h, const Module& compat)
{
    if (jumpsInto(h.staticVa, compat)) return true;
    if (core::prologueMatches(h.staticVa, h.prologueHex)) return true;
    core::logf("hook %s @ %08X refused: prologue differs and no di1compat jump", h.name, h.staticVa);
    return false;
}

}  // namespace detail

inline bool installWatch(const char* watch, const std::vector<core::HookSpec>& hooks, const Module& compat)
{
    for (const core::HookSpec& h : hooks) {
        if (detail::fits(h, compat)) continue;
        core::logf("watch %s off: none of its hooks installed", watch);
        return false;
    }
    for (size_t i = 0; i < hooks.size(); ++i) {
        if (detail::enable(hooks[i])) continue;
        for (size_t k = 0; k < i; ++k) core::unhook(hooks[k]);
        core::logf("watch %s off: hook %s refused, none of its hooks installed", watch, hooks[i].name);
        return false;
    }
    return true;
}

}  // namespace dbg::chain
