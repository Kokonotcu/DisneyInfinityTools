// audio10: the FMOD host (audio.hpp) set up from the cfg's audio keys and both installs. core, engine/assets.
// Boot thread.
#pragma once
#include "../../audio.hpp"
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/assets.hpp"
#include "config.hpp"

namespace di1::audio10 {

// audio::install with c's keys, after engine::assets; needs the 1.0 install, and its log goes in first.
inline bool install(const Config& c)
{
    if (!c.audio10 || !c.install10[0]) return true;
    audio::logfn = core::logf;
    audio::Cfg a;
    a.music = c.music10;
    a.vo = c.vo10;
    a.trace = c.audioTrace10;
    a.mixer = c.mixer10;
    a.install10 = c.install10;
    a.game30 = engine::assets::gameDir();
    a.installFeature = core::installFeature;
    a.rt = core::rt;
    if (audio::install(a)) return true;
    core::logf("audio10: off (FMOD host failed)");
    return false;
}

}  // namespace di1::audio10
