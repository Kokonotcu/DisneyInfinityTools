// 1.0's UI rules on 3.0's UI hooks: loading screens, FMVs, tutorial element 61, context lists. engine/ui, ui.hpp.
#pragma once
#include <string>
#include "../../core/log.hpp"
#include "../../engine/ui.hpp"
#include "ui.hpp"
#include "config.hpp"

namespace di1::screens {

// Logs the ui keys and, with serve10, installs the UI hooks they switch on.
inline bool install(const Config& c);

namespace detail {

struct State {
    std::string install10;   // set once in install()
};
inline State g_state;

// fmv10: a clip 3.0 lacks plays from the 1.0 install's loose assets\fmv\PC1\<clip>.bik (ui::fmvClip).
inline engine::ui::Fmv fmv(const char* name)
{
    const State& s = g_state;
    if (s.install10.empty()) return {};
    std::string clip = ui::fmvClip(name);
    if (clip.empty()) return {};
    return {clip, s.install10 + "\\assets\\fmv\\PC1\\" + clip + ".bik"};
}

}  // namespace detail

inline bool install(const Config& c)
{
    core::logf("ui: loadscreen10=%d uiscope10=%d fmv10=%d text10=%d tips10=%d tutorial10=%d context10=%d",
               c.loadScreen10, c.uiScope10, c.fmv10, c.text10, c.tips10, c.tutorial10, c.context10);
    if (!c.serve10) return true;
    detail::g_state.install10 = c.install10;
    engine::ui::Config u;
    if (c.loadScreen10) {
        u.screenKey = "LoadingScreen";   // 1.0's key: 11 1.0 zone records carry it, no 3.0 one
        u.loadingScreen = ui::loadingScreen10;
    }
    if (c.fmv10) u.fmv = detail::fmv;
    u.selectMenu = c.tutorial10;
    if (c.context10) u.contexts = ui::contextList10;
    return engine::ui::install(u);
}

}  // namespace di1::screens
