// The world session: transcode products the engine reopens, 1.0 archives' full-path members, 1.0-served UI. vfs.
// Any open or loader thread.
#pragma once
#include <windows.h>
#include <cctype>
#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../core/log.hpp"
#include "../../third_party/miniz/miniz.h"
#include "config.hpp"
#include "vfs.hpp"
#include "vfx.hpp"

namespace di1::session {

using Files = std::vector<std::pair<std::string, std::string>>;   // (path, bytes), or (path, archive member)

// Keeps vfx10 and (resident10) indexes the 1.0 startup archives, before any hook.
inline bool install(const Config& c);
// Keeps each file for the engine's next opens of its path; a path kept again is replaced. One lock for all.
inline void keep(Files files);
// Keeps a file unless its path is kept already (first wins).
inline void keepFirst(const std::string& path, std::string bytes);
// A kept file's bytes into hit; else the archive member a full path names into over; else neither is written.
inline void lookup(const std::string& path, std::string& hit, std::string& over);
// The archive member a full path names, "" if none.
inline std::string overlayMember(const std::string& path);
// Full-path members (path, "<archive>/<member>"), all under one lock; a known path keeps its member.
inline void addOverlay(const Files& members);
// Every full-path member of a 1.0 archive (path under assets), once per archive.
inline void indexArchive(const std::string& zipRel);
// A flash/<dir>/<movie> served from 1.0 (ui: its images may be too), and whether one was.
inline void noteMovie(const std::string& movie);
inline bool movieServed(const std::string& movie);
// A presentation/<screen>.lua served from 1.0 by its lower-case name (gprt), and whether one was.
inline void noteScreen(const std::string& screen);
inline bool screenServed(const std::string& screen);

namespace detail {

constexpr size_t kZipSuffix = sizeof(".zip") - 1;
constexpr size_t kNameCap = 300;   // a member name's buffer; miniz cuts longer names

struct State {
    bool vfx = false;                            // vfx10: set once in install()
    std::mutex mx;                               // guards served, over, indexed, movies and screens
    std::map<std::string, std::string> served;   // path -> bytes
    std::map<std::string, std::string> over;     // full path -> "<archive>/<member>"; first wins
    std::set<std::string> indexed;               // archives whose members are in over
    std::set<std::string> movies;                // flash/<dir>/<movie> served from 1.0
    std::set<std::string> screens;               // presentation screens served from 1.0, lower case
};
inline State g_state;

// The members of z under their full paths (and vfx10's mesh aliases). Caller holds vfs's lock (L2: it, then mx).
inline void indexMembers(const std::string& zipRel, mz_zip_archive* z)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    if (!s.indexed.insert(zipRel).second) return;
    std::string base = zipRel.substr(0, zipRel.size() - kZipSuffix) + "/";
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z); ++i) {
        char name[kNameCap];
        mz_zip_reader_get_filename(z, i, name, sizeof(name));
        std::string n = name;
        for (char& c : n) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
        if (n.find('/') != std::string::npos && n.back() != '/') s.over.emplace(n, base + name);
        if (!s.vfx) continue;
        // 3.0 opens meshes by hash
        if (std::string a = vfx::p3dAlias(n); !a.empty()) s.over.emplace(a, base + name);
    }
}

// resident10: 1.0 loads assets\startup*.zip resident, so their members are found by full path.
inline void indexResident(const Config& c)
{
    if (!c.serve10 || !c.resident10 || !c.install10[0]) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(c.install10) + "\\assets\\startup*.zip").c_str(), &fd);
    for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileA(h, &fd)) indexArchive(fd.cFileName);
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    std::unique_lock<std::mutex> lock(g_state.mx);
    const unsigned members = (unsigned)g_state.over.size();
    lock.unlock();
    core::logf("resident10: %u full-path members of the 1.0 startup archives indexed", members);
}

}  // namespace detail

inline void keep(Files files)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    for (auto& f : files) s.served[f.first] = std::move(f.second);
}

inline void keepFirst(const std::string& path, std::string bytes)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    s.served.emplace(path, std::move(bytes));
}

inline void lookup(const std::string& path, std::string& hit, std::string& over)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.served.find(path);
    if (it != s.served.end()) {
        hit = it->second;
        return;
    }
    auto o = s.over.find(path);
    if (o != s.over.end()) over = o->second;
}

inline std::string overlayMember(const std::string& path)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    auto o = s.over.find(path);
    return o != s.over.end() ? o->second : std::string();
}

inline void addOverlay(const Files& members)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    for (auto& m : members) s.over.emplace(m.first, m.second);
}

inline void indexArchive(const std::string& zipRel)
{
    vfs::withArchive(zipRel, [&zipRel](mz_zip_archive* z) {
        if (z) detail::indexMembers(zipRel, z);
    });
}

inline void noteMovie(const std::string& movie)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    s.movies.insert(movie);
}

inline bool movieServed(const std::string& movie)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    return s.movies.count(movie) != 0;
}

inline void noteScreen(const std::string& screen)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    s.screens.insert(screen);
}

inline bool screenServed(const std::string& screen)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    return s.screens.count(screen) != 0;
}

inline bool install(const Config& c)
{
    detail::g_state.vfx = c.vfx10;
    detail::indexResident(c);
    return true;
}

}  // namespace di1::session
