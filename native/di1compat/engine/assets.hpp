// 3.0 asset reads: AssetOpen's original (the engine's own open) and 3.0 archive members. core, arc30. Any open thread.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include "../formats/arc30.hpp"
#include "../core/engine_file.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/readonly_file.hpp"

namespace engine::assets {

struct Config {
    void* hkOpen = nullptr;   // the game's AssetOpen detour, FUN_0041cb10's signature (cdecl path, flags, a3, a4, a5)
    std::string gameDir;      // the 3.0 install (read30x)
};

// Installs AssetOpen with the game's detour (H1). False: refused, and nothing else may install (H2).
inline bool install(const Config& c);
// The engine's own open, unhooked.
inline int* open(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);
// The 3.0 install's folder, as install() was given it.
inline const std::string& gameDir();
// Whether the engine's own archives have path (opened and destroyed).
inline bool shipped(const char* path);
// A 3.0 asset through the engine's own open, read whole and destroyed; false on a short read.
inline bool read30(const std::string& path, std::string& out);
// A 3.0 archive member by extract path, '3:X/name/member' = a member of <game>/assets/X/name.zip, read in place.
inline bool read30x(const std::string& extract, std::string& out);

namespace detail {

typedef int*(__cdecl* OpenFn)(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);

struct State {
    OpenFn g_open = nullptr;   // set once by hook() in install()
    std::string gameDir;       // set once in install()
};
inline State g_state;

inline void destroy(int* s)
{
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    ((DtorFn)(*(void***)s)[core::eng::kFileDestroy])(s, nullptr, core::eng::kDestroyFree);
}

}  // namespace detail

inline int* open(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    return detail::g_state.g_open(path, flags, a3, a4, a5);
}

inline const std::string& gameDir() { return detail::g_state.gameDir; }

inline bool shipped(const char* path)
{
    int* s = open(path, 0, 0, 0, nullptr);
    if (s) detail::destroy(s);
    return s != nullptr;
}

// No Read for an empty file; out keeps the full size on a short read.
inline bool read30(const std::string& path, std::string& out)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    int* s = open(path.c_str(), 0, 0, 0, nullptr);
    if (!s) return false;
    void** vt = *(void***)s;
    uint32_t n = ((SizeFn)vt[core::eng::kFileSize])(s, nullptr);
    out.assign(n, '\0');
    uint32_t got = n ? ((ReadFn)vt[core::eng::kFileRead])(s, nullptr, &out[0], n) : 0;
    detail::destroy(s);
    return got == n;
}

// The engine's own path conventions vary by asset kind, so these are read straight from the archive.
inline bool read30x(const std::string& extract, std::string& out)
{
    std::string rel = extract.substr(2);
    size_t cut = rel.rfind('/');
    if (cut == std::string::npos) return false;
    std::string arc = detail::g_state.gameDir + "/assets/" + rel.substr(0, cut) + ".zip";
    for (char& c : arc)
        if (c == '/') c = '\\';
    core::ReadOnlyFile file(arc);
    if (!file.open()) return false;
    const arc30::Source src{file.size(), [&file](uint64_t at, void* dst, size_t n) { return file.readAt(at, dst, n); }};
    return arc30::readMember(src, rel.substr(cut + 1), out);
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.gameDir = c.gameDir;
    void* hkOpen = c.hkOpen;
    if (core::hook("AssetOpen", 0x0041CB10, "6AFF68????????", hkOpen, (void**)&s.g_open)) return true;
    core::logf("feature AssetOpen off: no other hook installed, the game runs as plain 3.0");
    return false;
}

}  // namespace engine::assets
