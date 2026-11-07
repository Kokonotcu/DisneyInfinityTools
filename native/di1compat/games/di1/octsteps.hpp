// The 1.0 .oct steps and their order (M7, M8); engine-bound steps are function objects (M2). Any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstring>
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

// Whether model f's Asset named asset sits on a hidden Geometry node (1.0 rescue props' stand-ins).
inline bool hiddenAsset(oct::File& f, const std::string& asset);

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

// Each scene node's opening record index, node 0 (the Scene) first.
inline std::vector<size_t> sceneNodes(const oct::File& f, long sp)
{
    std::vector<size_t> at;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; i = f.blockEnd(i)) at.push_back(i);
    return at;
}

// The scene node of the Asset association named name; -1 when there is none.
inline int modelAssetNode(oct::File& f, long ap, const std::string& name)
{
    for (size_t i = ap + 1, end = f.blockEnd(ap); i < end; i = f.blockEnd(i)) {
        const oct::Rec* named = fieldOf(f, i, "NodeName");
        const oct::Rec* type = fieldOf(f, i, "Type");
        const oct::Rec* ref = fieldOf(f, i, "SceneTreeNodeRef");
        const bool asset = type && ref && f.name0(*type) == "Asset";
        if (asset && named && !_stricmp(f.name0(*named).c_str(), name.c_str())) return f.int0(*ref);
    }
    return -1;
}

}  // namespace detail

inline bool hiddenAsset(oct::File& f, const std::string& asset)
{
    const long sp = f.pool("SceneTreeNodePool"), ap = f.pool("AssociationPool");
    if (sp < 0 || ap < 0) return false;
    const std::vector<size_t> nodes = detail::sceneNodes(f, sp);
    const int node = detail::modelAssetNode(f, ap, asset);
    if (node <= 0 || node >= (int)nodes.size()) return false;
    const oct::Rec* type = detail::fieldOf(f, nodes[node], "Type");
    const oct::Rec* visible = detail::fieldOf(f, nodes[node], "Visible");
    return type && visible && f.name0(*type) == "Geometry" && f.int0(*visible) == 0;
}

namespace detail {

constexpr size_t kMatrixFloats = 16;
constexpr size_t kTranslation = 12;   // LocalToParentMatrix row 3: x, y, z
constexpr float kIdentity3[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

// Node i's matrix translation when its rotation and scale are the identity (rescue stand-ins); false otherwise.
inline bool pureTranslation(oct::File& f, size_t i, float (&t)[3])
{
    float m[kMatrixFloats];
    if (!floatsOf(fieldOf(f, i, "LocalToParentMatrix"), kMatrixFloats, m)) return false;
    if (memcmp(m, kIdentity3, sizeof(kIdentity3)) != 0) return false;
    memcpy(t, m + kTranslation, sizeof(t));
    return true;
}

// The root's other children hang under node `anchor` at translation t. -> how many moved
inline int hangUnder(oct::File& f, const std::vector<size_t>& nodes, int anchor, const float (&t)[3])
{
    int moved = 0;
    for (size_t k = 1; k < nodes.size(); ++k) {
        oct::Rec* parent = fieldOf(f, nodes[k], "ParentNodeReferences");
        oct::Rec* matrix = fieldOf(f, nodes[k], "LocalToParentMatrix");
        float m[kMatrixFloats];
        const bool rootChild = parent && f.ints(*parent) == std::vector<int32_t>{0};
        if ((int)k == anchor || !rootChild || !floatsOf(matrix, kMatrixFloats, m)) continue;
        for (int c = 0; c < 3; ++c) m[kTranslation + c] -= t[c];
        memcpy(&matrix->pay[1], m, sizeof(m));   // the float list: u8 count, then the floats (floatsOf)
        f.setInts(*parent, {anchor});
        ++moved;
    }
    return moved;
}

// The fields of 3.0's Anchor, a Transform scene node (exploders/in/inc_electricbox node 1).
inline constexpr const char* kTransformFields[] = {"NodeName", "Uuid", "DisplayLayer", "Type",
                                                  "ParentNodeReferences", "LocalToParentMatrix"};

inline bool transformField(const std::string& n)
{
    for (const char* k : kTransformFields)
        if (n == k) return true;
    return false;
}

// Node i in 3.0's Anchor form (Transform: name, uuid, layer, parent, matrix); its mesh and visibility records go.
inline void toTransformNode(oct::File& f, size_t i)
{
    oct::Rec* type = fieldOf(f, i, "Type");
    *type = f.nameScalar(type->level, type->name, "Transform");
    std::vector<oct::Rec> recs;
    recs.reserve(f.recs.size());
    const size_t be = f.blockEnd(i);
    // a dropped field's whole block goes with it (Primitives)
    for (size_t j = 0; j < f.recs.size(); j = (j > i && j < be) ? f.blockEnd(j) : j + 1) {
        const bool inNode = j > i && j < be;
        if (!inNode || transformField(f.name(f.recs[j]))) recs.push_back(std::move(f.recs[j]));
    }
    f.recs = std::move(recs);
}

// Node `anchor`'s Geometry links become Transform; Anchor and PrimaryAnchor are added (inc_electricbox's Anchor).
inline int anchorLinks(oct::File& f, long ap, int anchor)
{
    int n = 0, keys = 0;
    std::string name;
    for (size_t i = ap + 1, end = f.blockEnd(ap); i < end; i = f.blockEnd(i), ++keys) {
        oct::Rec* type = fieldOf(f, i, "Type");
        const oct::Rec* ref = fieldOf(f, i, "SceneTreeNodeRef");
        const oct::Rec* node = fieldOf(f, i, "NodeName");
        if (!type || !ref || f.int0(*ref) != anchor || f.name0(*type) != "Geometry") continue;
        *type = f.nameScalar(type->level, type->name, "Transform");
        if (node) name = f.name0(*node);
        ++n;
    }
    if (name.empty()) return n;
    std::vector<oct::Rec> add;
    for (const char* kind : {"Anchor", "PrimaryAnchor"}) {
        add.push_back(octx::keyedNode(f, 2, "Association", keys++));
        add.push_back(f.nameScalar(3, f.addName("NodeName"), name));
        add.push_back(f.nameScalar(3, f.addName("Type"), kind));
        add.push_back(f.intScalar(3, "SceneTreeNodeRef", anchor));
        add.push_back(f.intScalar(3, "SubNetworkRef", 0));
    }
    const size_t at = f.blockEnd(ap);
    f.recs.insert(f.recs.begin() + at, add.begin(), add.end());
    return n + 2;
}

// 1.0 rescue props hide their model-named node and draw the pieces beside it: 3.0's Anchor form. -> pieces moved
inline int standInAnchor(const std::string& path, oct::File& f)
{
    const long sp = f.pool("SceneTreeNodePool"), ap = f.pool("AssociationPool");
    const size_t slash = path.rfind('/'), dot = path.rfind('.');
    const bool stemmed = slash != std::string::npos && dot != std::string::npos && dot > slash + 1;
    if (path.rfind("exploders/", 0) != 0 || sp < 0 || ap < 0 || !stemmed) return 0;
    const std::vector<size_t> nodes = sceneNodes(f, sp);
    const int anchor = modelAssetNode(f, ap, path.substr(slash + 1, dot - slash - 1));
    if (anchor <= 0 || anchor >= (int)nodes.size()) return 0;
    const oct::Rec* type = fieldOf(f, nodes[anchor], "Type");
    const oct::Rec* visible = fieldOf(f, nodes[anchor], "Visible");
    float t[3];
    const bool standIn = type && visible && f.name0(*type) == "Geometry" && f.int0(*visible) == 0;
    if (!standIn || !pureTranslation(f, nodes[anchor], t)) return 0;
    const int moved = hangUnder(f, nodes, anchor, t);
    if (!moved) return 0;
    anchorLinks(f, ap, anchor);
    toTransformNode(f, nodes[anchor]);
    return moved;
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
    changed |= detail::noted(note, detail::standInAnchor(path, f), "+anchor(", " pieces) ");
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
