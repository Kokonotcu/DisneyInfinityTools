// choalias10: Cho DataMap paths outside choreographies/<n>/<n>, as 1.0 DataMaps list them. cho. Any thread.
#pragma once
#include <string>
#include "../../cho.hpp"
#include "config.hpp"

namespace di1::choalias {

// Keeps choalias10; installs no hook.
inline bool install(const Config& c);
// A DataMap entry ("Cho|<path>[|...]"): its alias is kept when choalias10 is on.
inline void add(const std::string& entry);
// 'choreographies/<n>/<n><rest>' (normalised) -> '<1.0 path><rest>'; "" if none or choalias10 is off.
inline std::string resolve(const std::string& p);

namespace detail {

struct State {
    bool on = false;        // choalias10: set once in install()
    cho::Aliases aliases;   // guarded by its own lock (cho::Aliases::m), a leaf
};
inline State g_state;

}  // namespace detail

inline void add(const std::string& entry)
{
    detail::State& s = detail::g_state;
    if (s.on) s.aliases.add(entry);
}

inline std::string resolve(const std::string& p)
{
    detail::State& s = detail::g_state;
    return s.on ? s.aliases.resolve(p) : "";
}

inline bool install(const Config& c)
{
    detail::g_state.on = c.choAlias10;
    return true;
}

}  // namespace di1::choalias
