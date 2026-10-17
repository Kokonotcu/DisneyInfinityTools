// The plugin log (di1compat.log) and LogOnce. Windows and std only. Any thread, VEHs too; its lock is last (L2).
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

// Holds a CRITICAL_SECTION for one scope (CLAUDE.md L5).
class CsLock {
public:
    explicit CsLock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~CsLock() { LeaveCriticalSection(&cs_); }
    CsLock(const CsLock&) = delete;
    CsLock& operator=(const CsLock&) = delete;

private:
    CRITICAL_SECTION& cs_;
};
}  // namespace detail

// Opens the log for writing, readable while the game runs. Call once, before anything logs.
inline void openLog(const char* path)
{
    InitializeCriticalSection(&detail::g_logState.lock);
    detail::g_logState.file = _fsopen(path, "w", _SH_DENYNO);
}

// One line: "[hh:mm:ss.mmm] " and the text, flushed (crash evidence). Allocates nothing after the first line (VEHs).
inline void logf(const char* fmt, ...)
{
    detail::LogState& s = detail::g_logState;
    if (!s.file) return;
    detail::CsLock lock(s.lock);
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

}  // namespace core
