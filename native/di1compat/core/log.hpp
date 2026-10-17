// The plugin log (di1compat.log) and LogOnce. Windows, std. Any thread, VEHs too; its lock is last (L2).
#pragma once
#include <windows.h>
#include <share.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include "locks.hpp"

namespace core {

namespace detail {
struct LogState {
    CRITICAL_SECTION lock;   // initialised by openLog; guards file
    FILE* file;              // set once by openLog, before any other thread runs; null: logging is off
};
inline LogState g_logState;   // zero-initialised: logf does nothing until openLog
}  // namespace detail

// Opens the log for writing, readable while the game runs. Call once, before anything logs.
inline void openLog(const char* path)
{
    InitializeCriticalSection(&detail::g_logState.lock);
    detail::g_logState.file = _fsopen(path, "w", _SH_DENYNO);
}

// Opens <exe folder>\name (a .log), keeping the last 5 runs' logs as name.1.log (newest) .. name.5.log.
// -> the exe's folder.
inline std::string openExeLog(const char* name)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) snprintf(slash + 1, path + MAX_PATH - (slash + 1), "%s", name);
    std::string base(path, strlen(path) - (sizeof(".log") - 1));
    for (int i = 4; i >= 0; --i)
        MoveFileExA(i ? (base + "." + std::to_string(i) + ".log").c_str() : path,
                    (base + "." + std::to_string(i + 1) + ".log").c_str(), MOVEFILE_REPLACE_EXISTING);
    openLog(path);
    return std::string(path, slash ? slash - path : 0);
}

// One line: "[hh:mm:ss.mmm] " and the text, flushed (crash evidence). Allocates nothing after the first line (VEHs).
inline void logf(const char* fmt, ...)
{
    detail::LogState& s = detail::g_logState;
    if (!s.file) return;
    CsLock lock(s.lock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(s.file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(s.file, fmt, a);
    va_end(a);
    fputc('\n', s.file);
    fflush(s.file);
}

// The keys already logged (L6): first(key) is true once per key, for at most cap keys. A leaf lock.
template <class Key>
class LogOnce {
public:
    explicit LogOnce(size_t maxKeys = SIZE_MAX) : cap(maxKeys) {}
    bool first(const Key& key)
    {
        std::lock_guard<std::mutex> lock(mx);
        return seen.size() < cap && seen.insert(key).second;
    }

private:
    std::mutex mx;         // guards seen
    std::set<Key> seen;
    const size_t cap;      // set once
};

// Calls counted per key (L6): first(key, n) holds for a key's first n calls, while under cap keys are known.
template <class Key>
class LogCount {
public:
    explicit LogCount(size_t maxKeys = SIZE_MAX) : cap(maxKeys) {}
    bool first(const Key& key, int n)
    {
        std::lock_guard<std::mutex> lock(mx);
        return seen.size() < cap && ++seen[key] <= n;
    }

private:
    std::mutex mx;            // guards seen; a leaf lock
    std::map<Key, int> seen;
    const size_t cap;         // set once
};

}  // namespace core
