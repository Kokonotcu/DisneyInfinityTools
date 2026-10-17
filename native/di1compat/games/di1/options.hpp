// The cfg keys of the 1.0 transcode as its options structs (S4); engine-bound function objects stay empty. Any thread.
#pragma once
#include "config.hpp"
#include "octsteps.hpp"
#include "renderio.hpp"
#include "transcode.hpp"

namespace di1 {

// The .oct steps c switches on; render, installed30 and surfaceTable are the caller's.
inline OctSteps octStepsOf(const Config& c)
{
    OctSteps o;
    o.navmesh = c.navmesh10;
    o.cameras = c.camWorld10;
    o.realmFile = c.realmFile10;
    o.clips = c.clip10;
    o.clipIds = c.clip10Ids;
    o.layout = c.choLayout10;
    o.time = c.choTime10;
    o.anim = c.choAnim10;
    o.scene = c.choScene10;
    o.curves = c.curves10;
    o.constSections = c.choConst10;
    return o;
}

// The dispatch's own keys; the function objects of TranscodeIO are the caller's.
inline TranscodeOptions transcodeOptionsOf(const Config& c)
{
    TranscodeOptions o;
    o.challenge = c.challenge10;
    o.behaviour = c.behav10;
    o.events = c.events10;
    o.progress = c.progress10;
    o.text = c.text10;
    o.banm = c.banm10;
    o.psdna = c.psdna30;
    o.vfx = c.vfx10;
    o.rewards = c.rewards10;
    o.sx = c.sx10;
    o.bmx = c.bmx10;
    o.btx = c.btx10;
    return o;
}

// A world render's switches; the vertex formats and tails are the caller's.
inline WorldRender worldRenderOf(const Config& c)
{
    WorldRender w;
    w.translate = c.shaders10 && c.translate10;
    w.skinLayout = c.skinLayout10;
    w.skinTr = c.skinTr10;
    return w;
}

}  // namespace di1
