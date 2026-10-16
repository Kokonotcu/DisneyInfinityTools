// The plugin log, <game dir>\di1compat.log: logf writes one timestamped, flushed line under one lock; LogOnce
// keeps "log this once per key" sets. Windows and the standard library only. Any thread, the vectored exception
// handlers too: after the file's first line logf never allocates. The log lock is the last lock (core/locks.hpp).
#pragma once
#include <windows.h>
#include <share.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>

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

// One line: "[hh:mm:ss.mmm] " and the text, flushed at once (the log is the crash evidence).
inline void logf(const char* fmt, ...)
{
    detail::LogState& s = detail::g_logState;
    if (!s.file) return;
    EnterCriticalSection(&s.lock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(s.file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(s.file, fmt, a);
    va_end(a);
    fputc('\n', s.file);
    fflush(s.file);
    LeaveCriticalSection(&s.lock);
}

// The keys already logged (CLAUDE.md L6): first(key) is true once per key, for at most cap keys. Its lock is a leaf.
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

}  // namespace core
