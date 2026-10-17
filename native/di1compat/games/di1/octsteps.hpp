// The 1.0 .oct steps and their order (M7, M8); engine-bound steps are function objects (M2). Any thread.
#pragma once
#include <cctype>
#include <functional>
#include <string>
#include <vector>
#include "../../base/text.hpp"
#include "../../cho.hpp"
#include "../../gameplay.hpp"
#include "../../convert/assets.hpp"
#include "../../convert/counts.hpp"
#include "../../npc.hpp"
#include "associations.hpp"
#include "realms.hpp"

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
    octx::Options transforms;   // arena slack and variant groups (CO-01): the plugin's are octStepsOf's
};

namespace detail {

// A step's note "<head><n><tail>", appended when the step changed something (n != 0). Returns n != 0.
inline bool noted(std::string& note, int n, const char* head, const char* tail)
{
    if (!n) return false;
    note += head + std::to_string(n) + tail;
    return true;
}

// Clip tables re-packed (cho.hpp); a motion clip only when its template's folder is not in the 3.0 install.
inline bool clipSteps(oct::File& f, const OctSteps& o, std::string& note)
{
    std::function<bool(const std::string&)> motion;
    if (o.installed30)
        motion = [&o](const std::string& t) {
            size_t s = t.find_last_of("/\\");
            return s != std::string::npos && !o.installed30(t.substr(0, s));
        };
    int failed = 0, ren = 0;
    bool changed = false;
    if (int n = cho::clips30(f, motion, &failed, o.clipIds, &ren)) {
        std::string ids = ren ? ", " + std::to_string(ren) + " with 3.0 port ids" : std::string();
        note += "+clips(" + std::to_string(n) + ids + ") ";
        changed = true;
    }
    if (failed) note += "clips not in the 1.0 layout(" + std::to_string(failed) + ") ";
    return changed;
}

// Choreographies, exploders and clips in 3.0 form (cho.hpp): clip tables, ContextNodes, play ranges, anim inputs.
inline bool choSteps(oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = false;
    if (o.clips) changed |= clipSteps(f, o, note);
    if (o.clipIds) changed |= noted(note, cho::links30(f), "+ports(", " model links in 3.0 port order) ");
    if (o.layout) {
        int inst = 0;
        if (int n = cho::layout30(f, &inst)) {
            note += "+layout(" + std::to_string(n) + ", " + std::to_string(inst) + " instances) ";
            changed = true;
        }
    }
    if (o.time) changed |= noted(note, cho::time30(f), "+time(", " subnetworks) ");
    if (o.anim) {
        int sns = 0;
        if (int n = cho::anim30(f, &sns)) {
            note += "+anim(" + std::to_string(n) + " in " + std::to_string(sns) + " subnetworks) ";
            changed = true;
        }
    }
    if (o.scene) changed |= noted(note, cho::scene30(f), "+scene(", " placements) ");
    return changed;
}

// NavMeshPool into 3.0's layout: 3.0's parser FUN_01909fb0 trusts it (raw in_city.oct faulted at 0x190A4A7).
inline bool navSteps(oct::File& f, const OctSteps& o, std::string& note)
{
    std::string navWhy = "navmesh10 off";
    int navs = o.navmesh ? npc::navmesh30(f, &navWhy) : -1;
    if (navs > 0) {
        note += "+navmesh(" + std::to_string(navs) + ") ";
        return true;
    }
    if (navs < 0 && f.dropPool("NavMeshPool")) {
        note += "-NavMeshPool (" + navWhy + ") ";
        return true;
    }
    return false;
}

}  // namespace detail

// The steps before the constant sections, in this order. path: the normalised engine path. True if f changed.
inline bool octSteps(const std::string& path, oct::File& f, const OctSteps& o, std::string& note)
{
    // a raw 1.0 .mtb names 1.0 shaders: int3 in Shader_LookupCrc
    bool changed = o.render && o.render(path, f, note);
    changed |= detail::navSteps(f, o, note);
    if (octx::addAssetPool(f)) {
        note += "+AssetPool ";
        changed = true;
    }
    changed |= detail::noted(note, skin30(f, o.transforms), "+skin(", ") ");
    changed |= detail::noted(note, locator30(f, o.transforms), "+locators(", ") ");
    changed |= detail::noted(note, o.cameras ? camera30(f, o.transforms) : 0, "+cameras(", ") ");
    changed |= detail::noted(note, collision30(f, o.transforms), "+collision(", ") ");
    const bool realmFile = o.realmFile && path.rfind("realms/", 0) == 0;
    const RealmRule rule = realmFile ? RealmRule::RealmFile : RealmRule::WorldOnly;
    changed |= detail::noted(note, realm30w(f, rule), "+lighting(", " nodes) ");
    changed |= detail::choSteps(f, o, note);
    int nfl = 0, ncv = o.curves ? gameplay::curves30(f, &nfl) : 0;
    if (ncv || nfl) {
        note += "+curves(" + std::to_string(ncv) + " bound, " + std::to_string(nfl) + " flagged) ";
        changed = true;
    }
    changed |= detail::noted(note, o.surfaceTable ? gameplay::surfaces30(f, o.surfaceTable()) : 0, "+surfaces(", ") ");
    return changed;
}

// Constant sections, then the counts: const30 renumbers headers, so it is the last SubNetwork edit (cho.hpp).
inline bool octFinish(const std::string& path, oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = false;
    // norm again: path is normalised already, and norm strips one more "assets/" (A-22)
    std::string np = text::norm(path.c_str());
    const bool choOrExploder = np.rfind("choreographies/", 0) == 0 || np.rfind("exploders/", 0) == 0;
    if (o.constSections && choOrExploder)
        changed |= detail::noted(note, cho::const30(f), "+constant(", " subnetworks) ");
    // without counts the engine sizes the basis arenas 0 (FUN_004d0160)
    changed |= detail::noted(note, octx::addAllCounts(f, o.transforms), "+counts(", " subnetworks) ");
    return changed;
}

// A 1.0 .oct (world, mission, choreography, exploder ...) in 3.0 form: octSteps, then octFinish. True if f changed.
inline bool transcodeOct(const std::string& path, oct::File& f, const OctSteps& o, std::string& note)
{
    bool changed = octSteps(path, f, o, note);
    changed |= octFinish(path, f, o, note);
    return changed;
}

}  // namespace di1
