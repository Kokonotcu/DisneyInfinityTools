// Watch: asset opens by outcome: 3.0 stream, di1compat MemFile, or nothing. core, chain. Opening threads.
#pragma once
#include <windows.h>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"
#include "chain.hpp"

namespace dbg::files {

// One path's opens by outcome.
struct Counts {
    uint32_t engine = 0;   // 3.0's own disk or archive stream
    uint32_t compat = 0;   // a Genie::MemFile: di1compat served it (from 1.0, or a 3.0 file it edited)
    uint32_t missing = 0;  // nothing came back
};

struct Row {
    std::string path;
    Counts n;
};

// Hooks the asset open (H1, H2).
inline bool install(const chain::Module& compat);
// Every path opened so far, with its counts.
inline std::vector<Row> rows();
// Whether path (any case, either slash) was ever served by di1compat.
inline bool served(const std::string& path);
// path in lower case with '/' separators: the key rows() and served() use.
inline std::string norm(const char* path);

namespace eng {
// FUN_0041cb10, cdecl (path, flags, a3, a4, a5) -> stream or null
constexpr uint32_t kAssetOpen = 0x0041CB10;
// Genie::MemFile::vftable (dump30 symbols.tsv). FUN_0041cb10 returns only FUN_0040b7a0 / FUN_012e9ed0 streams.
constexpr uint32_t kMemFileVtable = 0x01D91A7C;
}  // namespace eng

namespace detail {

typedef int*(__cdecl* OpenFn)(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);

struct State {
    OpenFn g_open = nullptr;                          // set once by installWatch
    std::mutex mx;                                    // guards paths; a leaf lock
    std::unordered_map<std::string, Counts> paths;   // norm(path) -> counts; grows, a few thousand paths
};
inline State g_state;

inline void count(const char* path, const int* stream)
{
    char p[MAX_PATH];
    core::safeCopy(p, sizeof(p), path);
    std::string key = norm(p);
    const bool memFile = stream && *(const uintptr_t*)stream == core::rt(eng::kMemFileVtable);
    std::lock_guard<std::mutex> lock(g_state.mx);
    Counts& c = g_state.paths[key];
    if (!stream) ++c.missing;
    else if (memFile) ++c.compat;
    else ++c.engine;
}

// FUN_0041cb10, cdecl. File-opening threads, per open. Counting runs after the original.
inline int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    int* s = g_state.g_open(path, flags, a3, a4, a5);
    try {
        count(path, s);
    } catch (...) {   // never unwind into engine code (H6)
    }
    return s;
}

}  // namespace detail

inline std::string norm(const char* path)
{
    std::string k = path ? path : "";
    for (char& c : k) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    return k;
}

inline bool install(const chain::Module& compat)
{
    detail::State& s = detail::g_state;
    return chain::installWatch("files", {{"asset open (files watch)", eng::kAssetOpen, "6AFF68????????64A100000000",
                                          (void*)&detail::hkOpen, (void**)&s.g_open}},
                               compat);
}

inline std::vector<Row> rows()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    std::vector<Row> out;
    out.reserve(s.paths.size());
    for (const auto& kv : s.paths) out.push_back({kv.first, kv.second});
    return out;
}

inline bool served(const std::string& path)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.paths.find(path);
    return it != s.paths.end() && it->second.compat > 0;
}

}  // namespace dbg::files
