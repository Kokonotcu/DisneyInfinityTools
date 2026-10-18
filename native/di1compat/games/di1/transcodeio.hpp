// The plugin's readers, tables and session for the 1.0 transcode dispatch. games/di1, engine. Open threads.
#pragma once
#include <map>
#include <mutex>
#include <string>
#include "../../core/log.hpp"
#include "../../engine/assets.hpp"
#include "../../engine/langkeys.hpp"
#include "../../engine/surfaces.hpp"
#include "chan.hpp"
#include "choalias.hpp"
#include "config.hpp"
#include "gamedb.hpp"
#include "groups.hpp"
#include "octsteps.hpp"
#include "options.hpp"
#include "render10.hpp"
#include "session.hpp"
#include "tables.hpp"
#include "transcode.hpp"
#include "vfs.hpp"

namespace di1::transcodeio {

// Keeps c; installs no hook.
inline bool install(const Config& c);
// The dispatch's own cfg keys.
inline TranscodeOptions options();
// What the dispatch reads through and keeps into, with the engine-bound steps c switches on.
inline TranscodeIO io();

namespace detail {

struct State {
    Config cfg;                          // set once in install()
    std::mutex coinMx;                             // guards coin; not held across the read
    std::map<std::string, groups::CoinDna> coin;   // currency10: DNA path -> its FastCoin; never cleared
    chan::Roles roles;                   // vfxchan10: shader roles, guarded by its own mx; never cleared
};
inline State g_state;

// The .oct steps: the cfg's, the world render, the 3.0 install and the engine's surface table.
inline OctSteps octSteps10()
{
    const Config& c = g_state.cfg;
    OctSteps o = octStepsOf(c);
    o.render = render10::transcodeAssetRender;
    if (c.clip10Motions) o.installed30 = engine::assets::installed;
    if (c.surfaces10) o.surfaceTable = engine::surfaces::table;
    return o;
}

// currency10: a 1.0 expendable's DNA ('FastCoin_Spark', 'X.dnax', a path) as its FastCoin. Memoised.
inline groups::CoinDna coinDna10(const std::string& dnaFile)
{
    State& s = g_state;
    std::string p = dnaPath(dnaFile);
    if (p.empty()) return {};
    {
        std::lock_guard<std::mutex> lock(s.coinMx);
        if (auto it = s.coin.find(p); it != s.coin.end()) return it->second;
    }
    groups::CoinDna coin;
    std::string b;
    if (vfs::read10s(p, b)) coin = parseCoinDna(b);
    std::lock_guard<std::mutex> lock(s.coinMx);
    return s.coin[p] = coin;
}

inline void keepBundle(const std::string& path, const std::string& bytes) { session::keep({{path, bytes}}); }

// A DataMap entry, before it is normalised: Cho aliases (choalias10) and the playset's GameDB tables (gamedb.hpp)
inline void dataMapEntry10(const std::string& s)
{
    choalias::add(s);
    gamedb::dbTrigger10Add(s);   // the playset's GameDB tables: the locks / bitMask supplements follow them
}

}  // namespace detail

inline TranscodeOptions options() { return transcodeOptionsOf(detail::g_state.cfg); }

inline TranscodeIO io()
{
    const Config& c = detail::g_state.cfg;
    TranscodeIO t;
    t.read10 = vfs::read10s;
    t.read30x = engine::assets::read30x;
    t.read30 = engine::assets::read30;
    t.oct = detail::octSteps10();
    t.mer = tables::mer();
    t.render = tables::renderOk() ? &tables::render() : nullptr;
    t.lang30 = engine::langkeys::keys;
    t.keep = detail::keepBundle;
    t.renderProblem = [](const std::string& p, const std::string& pr) {
        core::logf("render problem %s: %s", p.c_str(), pr.c_str());
    };
    if (c.vfxChan10) {
        t.particleShaders = render10::particleShaders;
        t.roles = &detail::g_state.roles;
    }
    if (c.currency10) t.coinDna = detail::coinDna10;
    t.dataMapEntry = detail::dataMapEntry10;
    return t;
}

inline bool install(const Config& c)
{
    detail::g_state.cfg = c;
    return true;
}

}  // namespace di1::transcodeio
