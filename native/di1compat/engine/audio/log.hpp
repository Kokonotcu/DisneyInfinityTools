// The audio modules' log sink (core::logf, or octtest's stdout). core.
#pragma once

namespace engine::audio {

typedef void (*LogFn)(const char* fmt, ...);

// Sets the sink; before any audio thread runs.
inline void setLog(LogFn sink);
// One line through the sink; nothing when no sink is set.
template <class... Args>
inline void log(const char* fmt, Args... args);

namespace detail {
struct State {
    LogFn sink = nullptr;   // set once by setLog()
};
inline State g_state;
}  // namespace detail

inline void setLog(LogFn sink) { detail::g_state.sink = sink; }

template <class... Args>
inline void log(const char* fmt, Args... args)
{
    if (const LogFn sink = detail::g_state.sink; sink) sink(fmt, args...);
}

}  // namespace engine::audio
