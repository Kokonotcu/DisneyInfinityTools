// The 1.0 .oct steps and their order (M7, M8); engine-bound steps are function objects (M2). Any thread.
#pragma once
#include <cctype>
#include <functional>
#include <string>
#include <vector>
#include "../../base/text.hpp"
#include "cho.hpp"
#include "choconst.hpp"
#include "cholayout.hpp"
#include "curves.hpp"
#include "surfaces.hpp"
#include "../../convert/assets.hpp"
#include "../../convert/counts.hpp"
#include "associations.hpp"
#include "navmesh.hpp"
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
    cho::ClipSettings s;
    s.portIds = o.clipIds;
    if (o.installed30)
        s.motion = [&o](const std::string& t) {
            size_t sep = t.find_last_of("/\\");
            return sep != std::string::npos && !o.installed30(t.substr(0, sep));
        };
    const cho::ClipCounts c = cho::clips30(f, s);
    bool changed = false;
    if (c.repacked) {
        std::string ids = c.renumbered ? ", " + std::to_string(c.renumbered) + " with 3.0 port ids" : std::string();
        note += "+clips(" + std::to_string(c.repacked) + ids + ") ";
        changed = true;
    }
    if (c.failed) note += "clips not in the 1.0 layout(" + std::to_string(c.failed) + ") ";
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

// Where each scene node lacking ParentNodeReferences gets it: before its LocalToParentMatrix, else at its block end.
inline std::vector<size_t> orphanSlots(const oct::File& f, long sp)
{
    std::vector<size_t> at;
    bool root = true;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; i = f.blockEnd(i)) {
        if (root) {   // node 0, the Scene, has no parent
            root = false;
            continue;
        }
        const size_t be = f.blockEnd(i);
        size_t slot = be;
        bool has = false;
        for (size_t j = i + 1; j < be; ++j) {
            const std::string& n = f.name(f.recs[j]);
            has |= n == "ParentNodeReferences";
            if (n == "LocalToParentMatrix" && slot == be) slot = j;
        }
        if (!has) at.push_back(slot);
    }
    return at;
}

// 1.0 omits a root child's ParentNodeReferences; 3.0 exploders carry it (7948 of 7971 nodes). -> nodes given parent 0
inline int rootParents(const std::string& path, oct::File& f)
{
    const long sp = f.pool("SceneTreeNodePool");
    if (path.rfind("exploders/", 0) != 0 || sp < 0) return 0;
    const std::vector<size_t> at = orphanSlots(f, sp);
    if (at.empty()) return 0;
    std::vector<oct::Rec> recs;
    recs.reserve(f.recs.size() + at.size());
    size_t k = 0;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        for (; k < at.size() && at[k] == i; ++k) recs.push_back(f.intList(3, "ParentNodeReferences", {0}));
        recs.push_back(std::move(f.recs[i]));
    }
    for (; k < at.size(); ++k) recs.push_back(f.intList(3, "ParentNodeReferences", {0}));
    f.recs = std::move(recs);
    return (int)at.size();
}

// Whether a SubNetwork of f is named n.
inline bool hasGraphNamed(const oct::File& f, const std::string& n)
{
    for (size_t i = 0; i + 1 < f.recs.size(); ++i) {
        const bool graph = f.name(f.recs[i]) == "SubNetwork" && f.name(f.recs[i + 1]) == "Name";
        if (graph && f.nameValues(f.recs[i + 1]) == std::vector<std::string>{n}) return true;
    }
    return false;
}

// 1.0 capsule graphs are named 'Exploder' (their entries' AssetName); they take the model's name, as crates have.
inline int exploderGraphName(const std::string& path, oct::File& f)
{
    constexpr char kGraph[] = "Exploder";
    constexpr size_t kGraphLen = sizeof(kGraph) - 1;
    const size_t slash = path.rfind('/'), dot = path.rfind('.');
    const bool stemmed = slash != std::string::npos && dot != std::string::npos && dot > slash + 1;
    if (!stemmed || !hasGraphNamed(f, kGraph)) return 0;
    const std::string stem = path.substr(slash + 1, dot - slash - 1);
    int n = 0;
    for (std::string& s : f.names) {
        if (s.compare(0, kGraphLen, kGraph) != 0) continue;
        const std::string rest = s.substr(kGraphLen);
        if (!rest.empty() && rest[0] != '|' && rest != "Shape") continue;
        s = stem + rest;
        ++n;
    }
    f.nameIndexed = 0;   // names changed outside addName: its index is rebuilt
    return n;
}

// NavMeshPool into 3.0's layout: 3.0's parser FUN_01909fb0 trusts it (raw in_city.oct faulted at 0x190A4A7).
inline bool navSteps(oct::File& f, const OctSteps& o, std::string& note)
{
    std::string navWhy = "navmesh10 off";
    int navs = o.navmesh ? navmesh::navmesh30(f, &navWhy) : -1;
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
    changed |= detail::noted(note, detail::exploderGraphName(path, f), "+graphname(", " names) ");
    changed |= detail::noted(note, detail::rootParents(path, f), "+parents(", " nodes) ");
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
    int nfl = 0, ncv = o.curves ? curves::curves30(f, &nfl) : 0;
    if (ncv || nfl) {
        note += "+curves(" + std::to_string(ncv) + " bound, " + std::to_string(nfl) + " flagged) ";
        changed = true;
    }
    changed |= detail::noted(note, o.surfaceTable ? surfaces::surfaces30(f, o.surfaceTable()) : 0, "+surfaces(", ") ");
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
