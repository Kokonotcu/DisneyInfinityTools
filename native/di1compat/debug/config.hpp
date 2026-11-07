// di1debug.cfg: the log-only trace switches, all off by default (E5). core config and log. Read once in Pentane_Main.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <string>
#include "../core/config.hpp"
#include "../core/log.hpp"

namespace dbg {

// The trace switches, under the key names di1compat.cfg used for them.
struct Config {
    bool trace = false;            // figure flow, avatar spawn, SubNetwork instancing, every open (trace/spawn)
    bool missionTrace = false;     // mission context state changes, focus points (trace/mission)
    bool teardownTrace = false;    // Layout dtor placements and frees (trace/teardown)
    bool breakTrace = false;       // clip clocks, choreography starts and plays, exploder breaks (trace/breaktrace)
    bool lockTrace10 = false;      // control locks, contexts, choreography, messages (trace/locktrace)
    bool currencyTrace10 = false;  // currency payouts, pickups, balances read (trace/currency)
    bool vfxTrace = false;         // effect and emitter spawns (trace/vfx)
    bool spawnTrace10 = false;     // building lots, SpawnBuilding, player starts (trace/buildings, spawntrace)
    bool uiTrace10 = false;        // pauses on and off, tutorial MissionAccepted (trace/pause)
    bool fpsLog10 = false;         // each second, frame time and the main thread's wait on rendering (trace/loadprof)
    bool loadProf10 = false;       // oct load timings and a sampling profile (trace/loadprof)
    bool opacityLog10 = false;     // primitives the opacity test drops (trace/opacity)
    int fadeProbe10 = 0;           // non-zero: faded objects' fade inputs (trace/fade)
    int renderdoc10 = 0;           // > 0: RenderDoc captures one frame this many seconds after the first mission tick
};

inline const core::ConfigKey kConfigKeys[] = {
    {"trace", core::ConfigKind::Flag, offsetof(Config, trace)},
    {"missiontrace", core::ConfigKind::Flag, offsetof(Config, missionTrace)},
    {"teardowntrace", core::ConfigKind::Flag, offsetof(Config, teardownTrace)},
    {"breaktrace", core::ConfigKind::Flag, offsetof(Config, breakTrace)},
    {"locktrace10", core::ConfigKind::Flag, offsetof(Config, lockTrace10)},
    {"currencytrace10", core::ConfigKind::Flag, offsetof(Config, currencyTrace10)},
    {"vfx_trace", core::ConfigKind::Flag, offsetof(Config, vfxTrace)},
    {"spawntrace10", core::ConfigKind::Flag, offsetof(Config, spawnTrace10)},
    {"uitrace10", core::ConfigKind::Flag, offsetof(Config, uiTrace10)},
    {"fpslog10", core::ConfigKind::Flag, offsetof(Config, fpsLog10)},
    {"loadprof10", core::ConfigKind::Flag, offsetof(Config, loadProf10)},
    {"opacitylog10", core::ConfigKind::Flag, offsetof(Config, opacityLog10)},
    {"fadeprobe10", core::ConfigKind::Int, offsetof(Config, fadeProbe10)},
    {"renderdoc10", core::ConfigKind::Int, offsetof(Config, renderdoc10)},
};

// Reads <dir>\di1debug.cfg into cfg and logs the switches; without the file every trace stays off.
inline void loadConfig(const std::string& dir, Config& cfg)
{
    const std::string path = dir + "\\di1debug.cfg";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") || !f) {
        core::logf("no %s: every trace off, the watches run", path.c_str());
        return;
    }
    core::readConfig(f, cfg, kConfigKeys);
    fclose(f);
    core::logf("config: trace=%d missiontrace=%d teardowntrace=%d breaktrace=%d locktrace10=%d currencytrace10=%d "
               "vfx_trace=%d spawntrace10=%d uitrace10=%d fpslog10=%d loadprof10=%d opacitylog10=%d fadeprobe10=%d "
               "renderdoc10=%d", cfg.trace, cfg.missionTrace, cfg.teardownTrace, cfg.breakTrace, cfg.lockTrace10,
               cfg.currencyTrace10, cfg.vfxTrace, cfg.spawnTrace10, cfg.uiTrace10, cfg.fpsLog10, cfg.loadProf10,
               cfg.opacityLog10, cfg.fadeProbe10, cfg.renderdoc10);
}

}  // namespace dbg
