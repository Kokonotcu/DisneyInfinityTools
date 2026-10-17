// The exe CRT's fail-fasts: who called __invoke_watson; resident paths too long to copy. core. Any thread; rare.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/stack.hpp"

namespace engine::crt {

struct Config {};   // both hooks are always on

// Installs both hooks (H1); false when one was refused.
inline bool install(const Config& c);

namespace detail {

constexpr USHORT kFramesLogged = 24;
constexpr size_t kPathScanned = 4096;   // a longer path is measured as this long

typedef void(__cdecl* WatsonFn)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef void(__cdecl* NormPathFn)(char* dst, unsigned size, const char* src);

struct State {
    // originals: set once by hook() in install()
    WatsonFn g_watson = nullptr;
    NormPathFn g_normPath = nullptr;
};
inline State g_state;

// 0x012CA1D1, cdecl: the exe CRT's __invoke_watson; its fail-fast reaches no exception handler. Logs, then dies.
inline void __cdecl hkWatson(const wchar_t* a, const wchar_t* b, const wchar_t* c, unsigned d, uintptr_t e)
{
    void* frames[kFramesLogged];
    USHORT n = CaptureStackBackTrace(0, kFramesLogged, frames, nullptr);
    char line[600] = "CRT invalid parameter (__invoke_watson); stack (static):";
    size_t len = strlen(line);
    for (USHORT i = 0; i < n; ++i) {
        uintptr_t f = (uintptr_t)frames[i];
        // never cut: 56 characters and at most 24 frames of 13
        if (core::inExeImage(f))
            len += snprintf(line + len, sizeof(line) - len, " %08X", (unsigned)core::toStaticVa(f));
        else
            len += snprintf(line + len, sizeof(line) - len, " ext:%08X", (unsigned)f);
    }
    core::logf("%s", line);
    g_state.g_watson(a, b, c, d, e);
}

// FUN_0041fef0, cdecl: resident path normaliser (strcpy_s into 0x80 bytes); a longer path is logged and cut.
inline void __cdecl hkNormPath(char* dst, unsigned size, const char* src)
{
    size_t n = src ? strnlen(src, kPathScanned) : 0;
    if (src && n >= size) {
        core::logf("path too long for the engine's %u-byte buffer (%u): %s", size, (unsigned)n, src);
        std::string cut(src, size - 1);
        g_state.g_normPath(dst, size, cut.c_str());
        return;
    }
    g_state.g_normPath(dst, size, src);
}

}  // namespace detail

inline bool install(const Config&)
{
    detail::State& s = detail::g_state;
    bool ok =
        core::hook("CRT __invoke_watson", 0x012CA1D1, "8BFF566A01", (void*)&detail::hkWatson, (void**)&s.g_watson);
    ok &= core::hook("resident path normaliser", 0x0041FEF0, "8B44240C56", (void*)&detail::hkNormPath,
                     (void**)&s.g_normPath);
    return ok;
}

}  // namespace engine::crt
