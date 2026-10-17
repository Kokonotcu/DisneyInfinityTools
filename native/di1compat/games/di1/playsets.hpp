// zones10 (PS-1): 1.0's playsets registered as 3.0's zone list loads; names, starts, zone prefixes. core. Open threads.
#pragma once
#include <windows.h>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../core/log.hpp"
#include "config.hpp"
#include "currency.hpp"
#include "zones.hpp"

namespace di1::playsets {

typedef std::function<bool(const std::string& path, std::string& out)> Read10;

// Keeps the keys the registration follows (zones10, currency10, install10); installs no hook.
inline bool install(const Config& c);
// t: 3.0's gamedb/core/zonelist.lua, 1.0's playsets added to it (unless zones10 is off). The count added.
inline int registerPlaysets(std::string& t, const char* path, const Read10& read10);
// Each registered playset's name and start, for the Play Sets menu.
inline std::vector<std::pair<std::string, std::string>> starts();
// Whether a mission name starts with a registered playset's zone prefix ("IN_", "MU_" ...).
inline bool ownMission(const char* name);

namespace detail {

struct State {
    bool add = false, currencies = false;          // zones10, currency10: set once in install()
    std::string install10;                         // set once in install()
    std::mutex mx;                                 // guards playsets and prefixes
    std::vector<Playset10> playsets;     // the zone list's 1.0 playsets, once it has loaded
    std::vector<std::string> prefixes;             // their zones' name prefixes
};
inline State g_state;

inline bool world10(const std::string& zone) { return worldInstalled(g_state.install10.c_str(), zone); }

// Each playset's line: registered, or already in 3.0's list.
inline int logPlaysets(const std::vector<Playset10>& playsets)
{
    int added = 0;
    for (auto& x : playsets) {
        added += x.added;
        if (x.added)
            core::logf("zones10: %s registered, PlaysetUniqueID %d, %u zones, start %s", x.name.c_str(), x.id,
                       (unsigned)x.zones.size(), x.start.c_str());
        else
            core::logf("zones10: %s already in the 3.0 zone list (start %s)", x.name.c_str(), x.start.c_str());
    }
    return added;
}

}  // namespace detail

inline int registerPlaysets(std::string& t, const char* path, const Read10& read10)
{
    detail::State& s = detail::g_state;
    if (text::norm(path) != "gamedb/core/zonelist.lua") return 0;
    std::string c10;
    if (!read10("gamedb/core/zonelist.lua", c10)) {
        core::logf("zones10: no 1.0 zone list in the 1.0 install");
        return 0;
    }
    Zones10Options o;
    o.add = s.add;
    o.currencies = s.currencies;
    o.world = detail::world10;
    Zones10 z;
    try {
        z = zones10(t, c10, o);
        if (s.currencies) {
            std::string l;
            for (auto& [f, c] : z.currencies) l += " " + f + "=" + c;
            core::logf("currency10: 1.0 playset currencies:%s", l.c_str());
            currency::setCurrencies(std::move(z.currencies));
        }
    } catch (std::exception& e) {
        core::logf("zones10: 1.0 zone list not read (%s)", e.what());
        return 0;
    }
    std::vector<std::string> pre = zonePrefixes(z.playsets);
    int added = detail::logPlaysets(z.playsets);
    std::lock_guard<std::mutex> lock(s.mx);
    s.playsets = z.playsets;
    s.prefixes = pre;
    return added;
}

inline std::vector<std::pair<std::string, std::string>> starts()
{
    detail::State& s = detail::g_state;
    std::vector<std::pair<std::string, std::string>> ps;
    std::lock_guard<std::mutex> lock(s.mx);
    for (auto& x : s.playsets) ps.push_back({x.name, x.start});
    return ps;
}

inline bool ownMission(const char* name)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    for (auto& x : s.prefixes)
        if (!_strnicmp(name, x.c_str(), x.size())) return true;
    return false;
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.add = c.zones10;
    s.currencies = c.currency10;
    s.install10 = c.install10;
    return true;
}

}  // namespace di1::playsets
