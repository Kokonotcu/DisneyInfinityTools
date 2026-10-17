// The key hashes of the game's language tables, per language, read once. formats/dct. Loader threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include "../formats/dct.hpp"

namespace engine::langkeys {

using Read = std::function<bool(const std::string& extract, std::string& out)>;

struct Config {
    std::string gameDir;   // the install whose assets\lang packages are read
    Read read;             // an archive member by extract path ('3:lang/<pkg>/<pkg>/<file>')
};

// Keeps c for keys(); installs no hook.
inline bool install(const Config& c);
// Every table's key hashes for lang (both .dct kinds of each assets\lang package), read at its first ask.
inline const std::unordered_set<uint32_t>& keys(const std::string& lang);

namespace detail {

struct State {
    Config cfg;     // set once in install()
    std::mutex mx;  // guards cache; held across a language's reads, so each language is read once (L3)
    std::map<std::string, std::unordered_set<uint32_t>> cache;   // language -> hashes; never cleared
};
inline State g_state;

}  // namespace detail

inline const std::unordered_set<uint32_t>& keys(const std::string& lang)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.cache.find(lang);
    if (it != s.cache.end()) return it->second;
    auto& hashes = s.cache[lang];
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((s.cfg.gameDir + "\\assets\\lang\\*").c_str(), &fd);
    for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileA(h, &fd)) {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        std::string pkg = fd.cFileName, t;
        for (const char* v : {"_win32.dct", "_pc_con.dct"})
            if (s.cfg.read("3:lang/" + pkg + "/" + pkg + "/" + lang + v, t)) dct::hashes(t, hashes);
    }
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    return hashes;
}

inline bool install(const Config& c)
{
    detail::g_state.cfg = c;
    return true;
}

}  // namespace engine::langkeys
