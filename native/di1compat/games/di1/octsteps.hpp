// The 1.0 .oct transcode: which steps run on a 1.0 .oct, in which order (CLAUDE.md M7, M8). The plugin and octtest
// both call it. Engine-bound steps come in as function objects (M2). No engine, log or config. Any thread.
#pragma once
#include <cctype>
#include <functional>
#include <string>
#include <vector>
#include "../../base/text.hpp"
#include "../../cho.hpp"
#include "../../gameplay.hpp"
#include "../../npc.hpp"
#include "../../octx.hpp"

namespace di1 {

// Which steps run (their cfg keys) and the engine-bound ones; an empty function object skips its step.
struct OctSteps {
    // the world render (transcodeAssetRender): may edit f and appends its note
    std::function<bool(const std::string& path, oct::File& f, std::string& note)> render;
    bool navmesh = false;       // navmesh10; off, or a mesh that does not convert: NavMeshPool dropped
    bool cameras = false;       // camworld10
    bool realmFile = false;     // realmfile10, for paths under realms/
    bool clips = false;         // clip10
    // clip10motions: is a clip template's folder in the 3.0 install? Empty: motion clips stay as they are
    std::function<bool(const std::string& dir)> installed30;
    bool clipIds = false;       // clip10ids: 3.0 port ids on motion clips, model links in 3.0 port order
    bool layout = false;        // cholayout10
    bool time = false;          // chotime10
    bool anim = false;          // choanim10
    bool scene = false;         // choscene10
    bool curves = false;        // curves10
    std::function<std::vector<std::string>()> surfaceTable;   // surfaces10: the engine's surface table, by index
    bool constSections = false; // choconst10, for paths under choreographies/ or exploders/
};

namespace detail {

// Choreographies, exploders and clips in 3.0 form (cho.hpp): clip tables, ContextNodes, play ranges, anim inputs.
inline bool choSteps(oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = false;
    if (o.clips) {
        std::function<bool(const std::string&)> motion;
        if (o.installed30)
            motion = [&o](const std::string& t) {
                size_t s = t.find_last_of("/\\");
                return s != std::string::npos && !o.installed30(t.substr(0, s));
            };
        int failed = 0, ren = 0;
        if (int n = cho::clips30(f, motion, &failed, o.clipIds, &ren)) {
            std::string ids = ren ? ", " + std::to_string(ren) + " with 3.0 port ids" : std::string();
            note += "+clips(" + std::to_string(n) + ids + ") ";
            changed = true;
        }
        if (failed) note += "clips not in the 1.0 layout(" + std::to_string(failed) + ") ";
    }
    if (o.clipIds)
        if (int n = cho::links30(f)) {
            note += "+ports(" + std::to_string(n) + " model links in 3.0 port order) ";
            changed = true;
        }
    if (o.layout) {
        int inst = 0;
        if (int n = cho::layout30(f, &inst)) {
            note += "+layout(" + std::to_string(n) + ", " + std::to_string(inst) + " instances) ";
            changed = true;
        }
    }
    if (o.time)
        if (int n = cho::time30(f)) { note += "+time(" + std::to_string(n) + " subnetworks) "; changed = true; }
    if (o.anim) {
        int sns = 0;
        if (int n = cho::anim30(f, &sns)) {
            note += "+anim(" + std::to_string(n) + " in " + std::to_string(sns) + " subnetworks) ";
            changed = true;
        }
    }
    if (o.scene)
        if (int n = cho::scene30(f)) { note += "+scene(" + std::to_string(n) + " placements) "; changed = true; }
    return changed;
}

}  // namespace detail

// Every step before the constant sections, in this order: render, navmesh, asset pool, skin, locators, cameras,
// collision, realm, choreography, curves, surfaces. path: the normalised engine path. True if f changed.
// Evidence: a raw 1.0 .mtb names 1.0 shaders (int3 in Shader_LookupCrc); 3.0's NavMesh parser FUN_01909fb0 trusts
// the layout (raw in_city.oct faulted at 0x190A4A7); without counts the basis arenas are sized 0 (FUN_004d0160).
inline bool octSteps(const std::string& path, oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = o.render && o.render(path, f, note);
    std::string navWhy = "navmesh10 off";
    int navs = o.navmesh ? npc::navmesh30(f, &navWhy) : -1;
    if (navs > 0) { note += "+navmesh(" + std::to_string(navs) + ") "; changed = true; }
    else if (navs < 0 && f.dropPool("NavMeshPool")) { note += "-NavMeshPool (" + navWhy + ") "; changed = true; }
    if (octx::addAssetPool(f)) { note += "+AssetPool "; changed = true; }
    if (int n = octx::skin30(f)) { note += "+skin(" + std::to_string(n) + ") "; changed = true; }
    if (int n = octx::locator30(f)) { note += "+locators(" + std::to_string(n) + ") "; changed = true; }
    if (int n = o.cameras ? octx::camera30(f) : 0) { note += "+cameras(" + std::to_string(n) + ") "; changed = true; }
    if (int n = octx::collision30(f)) { note += "+collision(" + std::to_string(n) + ") "; changed = true; }
    if (int n = octx::realm30w(f, o.realmFile && path.rfind("realms/", 0) == 0)) {
        note += "+lighting(" + std::to_string(n) + " nodes) ";
        changed = true;
    }
    if (detail::choSteps(f, o, note)) changed = true;
    int nfl = 0, ncv = o.curves ? gameplay::curves30(f, &nfl) : 0;
    if (ncv || nfl) {
        note += "+curves(" + std::to_string(ncv) + " bound, " + std::to_string(nfl) + " flagged) ";
        changed = true;
    }
    if (int n = o.surfaceTable ? gameplay::surfaces30(f, o.surfaceTable()) : 0) {
        note += "+surfaces(" + std::to_string(n) + ") ";
        changed = true;
    }
    return changed;
}

// The last two steps: constant sections, then the counts. const30 renumbers headers, so it is the last SubNetwork
// edit before the counts (cho.hpp); mission graphs keep octx's variable form. True if f changed.
inline bool octFinish(const std::string& path, oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = false;
    // norm again: path is normalised already, and norm strips one more "assets/" (docs/audit A-22)
    std::string np = text::norm(path.c_str());
    if (o.constSections && (np.rfind("choreographies/", 0) == 0 || np.rfind("exploders/", 0) == 0))
        if (int n = cho::const30(f)) { note += "+constant(" + std::to_string(n) + " subnetworks) "; changed = true; }
    if (int n = octx::addAllCounts(f)) { note += "+counts(" + std::to_string(n) + " subnetworks) "; changed = true; }
    return changed;
}

// A 1.0 .oct (world, mission, choreography, exploder ...) in 3.0 form: octSteps, then octFinish. True if f changed.
inline bool transcodeOct(const std::string& path, oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = octSteps(path, f, o, note);
    if (octFinish(path, f, o, note)) changed = true;
    return changed;
}

}  // namespace di1
