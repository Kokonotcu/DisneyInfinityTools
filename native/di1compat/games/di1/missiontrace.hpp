// missiontrace on 1.0's playset missions (zones10's prefixes); renderdoc10 arms on their tick. trace, playsets.
// Boot thread installs.
#pragma once
#include "../../trace/mission.hpp"
#include "../../trace/renderdoc.hpp"
#include "config.hpp"
#include "playsets.hpp"

namespace di1::missiontrace {

// Installs trace/mission for 1.0's missions; renderdoc10 arms on the first one's tick, missiontrace or not.
inline bool install(const Config& c)
{
    trace::mission::Config m;
    m.on = c.missionTrace;
    m.ownMission = playsets::ownMission;
    if (trace::renderdoc::loaded()) m.ownMissionTick = trace::renderdoc::arm;
    return trace::mission::install(m);
}

}  // namespace di1::missiontrace
