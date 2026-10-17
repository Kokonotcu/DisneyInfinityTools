// The audio modules' log sink (core::logf, or octtest's stdout) and audiotrace10's once-per-name lines. core.
#pragma once
#include <cstdint>
#include "../../base/hash.hpp"
#include "../../core/log.hpp"

namespace engine::audio {

typedef void (*LogFn)(const char* fmt, ...);

// Sets the sink, and whether audiotrace10's lines are wanted; before any audio thread runs.
inline void setLog(LogFn sink, bool trace = false);
// One line through the sink; nothing when no sink is set.
template <class... Args>
inline void log(const char* fmt, Args... args);
// Whether audiotrace10 is on.
inline bool tracing();
// audiotrace10: "<name> -> <how>" the first time this id meets this backend.
inline void traceBackend(uint32_t id, const char* name, const char* how);

namespace detail {
struct State {
    LogFn sink = nullptr;             // set once by setLog()
    bool trace = false;               // set once by setLog()
    core::LogOnce<uint32_t> traced;   // id ^ fnv1Lower(how) of the lines logged
};
inline State g_state;
}  // namespace detail

inline void setLog(LogFn sink, bool trace)
{
    detail::g_state.sink = sink;
    detail::g_state.trace = trace;
}

template <class... Args>
inline void log(const char* fmt, Args... args)
{
    if (const LogFn sink = detail::g_state.sink; sink) sink(fmt, args...);
}

inline bool tracing() { return detail::g_state.trace; }

inline void traceBackend(uint32_t id, const char* name, const char* how)
{
    detail::State& s = detail::g_state;
    if (!s.trace) return;
    const bool first = s.traced.first(id ^ hash::fnv1Lower(how));
    if (first) log("audio10: %s -> %s", name ? name : "?", how);
}

}  // namespace engine::audio
