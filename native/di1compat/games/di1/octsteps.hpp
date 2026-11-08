// The 1.0 .oct steps and their order (M7, M8); engine-bound steps are function objects (M2). Any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <set>
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
    bool zoneVolumes = false;   // zonevolume10
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
    bool lodLevels = false;     // lod10: a LodGroup draws its first (nearest) level only
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

// The record of field n inside the block opened at i; nullptr when it has none.
inline oct::Rec* fieldOf(oct::File& f, size_t i, const char* n)
{
    for (size_t j = i + 1, be = f.blockEnd(i); j < be; ++j)
        if (f.name(f.recs[j]) == n) return &f.recs[j];
    return nullptr;
}

// Scene nodes a CollisionInstanceToSceneTreeNode association binds.
inline std::set<int> colliderNodes(oct::File& f)
{
    std::set<int> out;
    const long ap = f.pool("AssociationPool");
    for (size_t i = ap + 1, end = ap < 0 ? 0 : f.blockEnd(ap); i < end; i = f.blockEnd(i)) {
        const oct::Rec* type = fieldOf(f, i, "Type");
        const oct::Rec* ref = fieldOf(f, i, "SceneTreeNodeRef");
        if (type && ref && f.name0(*type) == "CollisionInstanceToSceneTreeNode") out.insert(f.int0(*ref));
    }
    return out;
}

// 3.0 hits a hidden exploder collider only as node 1 (INC_rescueBus: node 6, no hits): it moves there. -> 1
inline int hiddenColliderFirst(const std::string& path, oct::File& f)
{
    const long sp = f.pool("SceneTreeNodePool");
    if (path.rfind("exploders/", 0) != 0 || sp < 0) return 0;
    std::vector<size_t> nodes;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; i = f.blockEnd(i)) nodes.push_back(i);
    const std::set<int> colliders = colliderNodes(f);
    int k = -1;
    for (int c : colliders) {
        const oct::Rec* visible = c > 1 && c < (int)nodes.size() ? fieldOf(f, nodes[c], "Visible") : nullptr;
        if (visible && !f.int0(*visible) && k < 0) k = c;
    }
    if (k < 0 || colliders.count(1)) return 0;
    // new order: 0, k, 1 .. k-1, k+1 ..; each slot keeps its own key ('0', '1', ...)
    std::vector<int> order{0, k};
    for (int i = 1; i < (int)nodes.size(); ++i)
        if (i != k) order.push_back(i);
    std::vector<int> moved(nodes.size());
    for (int p = 0; p < (int)order.size(); ++p) moved[order[p]] = p;
    std::vector<oct::Rec> pool;
    pool.reserve(f.blockEnd(sp) - sp - 1);
    for (int p = 0; p < (int)order.size(); ++p) {
        const size_t from = nodes[order[p]], at = pool.size();
        pool.insert(pool.end(), f.recs.begin() + from, f.recs.begin() + f.blockEnd(from));
        pool[at].pay.replace(0, 2, f.recs[nodes[p]].pay.substr(0, 2));
    }
    std::copy(pool.begin(), pool.end(), f.recs.begin() + sp + 1);
    for (oct::Rec& r : f.recs) {
        const std::string& n = f.name(r);
        const bool ref = n == "SceneTreeNodeRef", parents = n == "ParentNodeReferences";
        if (!ref && !parents) continue;
        std::vector<int32_t> vs = f.ints(r);
        for (int32_t& v : vs)
            if (v >= 0 && v < (int32_t)moved.size()) v = moved[v];
        if (ref && !vs.empty()) f.setInt(r, vs[0]);
        if (parents) f.setInts(r, vs);
    }
    return 1;
}

// Each LodGroup node's children, in node order (ParentNodeReferences), by node index.
inline std::map<int, std::vector<int>> lodChildren(oct::File& f, const std::vector<size_t>& nodes)
{
    std::map<int, std::vector<int>> kids;
    std::vector<bool> lod(nodes.size());
    for (size_t k = 0; k < nodes.size(); ++k) {
        const oct::Rec* type = fieldOf(f, nodes[k], "Type");
        lod[k] = type && f.name0(*type) == "LodGroup";
    }
    for (size_t k = 0; k < nodes.size(); ++k) {
        const oct::Rec* parent = fieldOf(f, nodes[k], "ParentNodeReferences");
        const int p = parent ? f.int0(*parent) : -1;
        if (p >= 0 && p < (int)nodes.size() && lod[p]) kids[p].push_back((int)k);
    }
    return kids;
}

// 3.0 draws every LodGroup child its cells link; 1.0 drew one. -> lower levels hidden
inline int lodLevels30(oct::File& f)
{
    const long sp = f.pool("SceneTreeNodePool");
    if (sp < 0) return 0;
    std::vector<size_t> nodes;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; i = f.blockEnd(i)) nodes.push_back(i);
    int hidden = 0;
    // ponytail: far buildings draw at full detail; switch levels if 3.0's cell path honours LodGroup
    std::map<int, std::vector<int>> groups = lodChildren(f, nodes);
    // the first child is the nearest level (Distances[0], 120 of 120 in_city groups: building, then its _Lod)
    for (auto& [group, kids] : groups) {
        for (size_t c = 1; c < kids.size(); ++c) {
            oct::Rec* visible = fieldOf(f, nodes[kids[c]], "Visible");
            if (!visible || !f.int0(*visible)) continue;
            f.setInt(*visible, 0);
            ++hidden;
        }
    }
    return hidden;
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
    changed |= detail::noted(note, detail::hiddenColliderFirst(path, f), "+colliderfirst(", ") ");
    changed |= detail::noted(note, detail::rootParents(path, f), "+parents(", " nodes) ");
    changed |= detail::noted(note, o.lodLevels ? detail::lodLevels30(f) : 0, "+lod(", " lower levels hidden) ");
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
    changed |= detail::noted(note, o.zoneVolumes ? zoneVolumeRealms(f) : 0, "+zonevolumes(", " volumes) ");
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
