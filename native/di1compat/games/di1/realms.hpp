// 1.0 realm and light data in 3.0's form: RealmData / RealmVolume scene nodes, int-typed lights, rig Uuids. Any thread.
#pragma once
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../convert/records.hpp"
#include "../../formats/oct.hpp"

namespace di1 {

// Whether a realms/ asset may get the realm-file rule (cfg realmfile10): it applies when the file has one realm.
enum class RealmRule { WorldOnly, RealmFile };

namespace detail {

using Block = std::vector<oct::Rec>;

// A RealmDataPool entry's fields a RealmData node takes over, in the order all 367 shipped 3.0 ones have them.
inline constexpr const char* kRealmFields[] = {"SunLightRef", "AmbientRigRef", "SunRigRef", "SceneSettingsRef",
                                               "SettingsName", "ZoneName", "RigBlendType", "SettingsNonDefault",
                                               "SunNoDirection", "SunNoColor", "StartWeight", "FollowsCamera",
                                               "ParallaxFactor", "GlobalOverride"};
inline constexpr const char* kRealmFlags[] = {"RotateToGravity", "RotateSkipLighting", "BlendWithGravity"};   // 0
inline constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// The pool's entries, each a copy of its records below the entry (the splice moves f's records).
inline std::vector<Block> poolEntries(const oct::File& f, const char* pool)
{
    std::vector<Block> out;
    long p = f.pool(pool);
    if (p < 0) return out;
    for (size_t i = p + 1, end = f.blockEnd(p); i < end; i = f.blockEnd(i))
        out.emplace_back(f.recs.begin() + i + 1, f.recs.begin() + f.blockEnd(i));
    return out;
}

// The first level-3 record named n of an entry, or null.
inline const oct::Rec* entryField(const oct::File& f, const Block& e, const char* n)
{
    for (const oct::Rec& r : e)
        if (r.level == 3 && f.name(r) == n) return &r;
    return nullptr;
}

// count floats of a float list (u8 count, then the floats) into out; false when it is shorter.
inline bool floatsOf(const oct::Rec* r, size_t count, float* out)
{
    if (!r || r->pay.size() < 1 + 4 * count) return false;
    memcpy(out, r->pay.data() + 1, 4 * count);
    return true;
}

// realm30w's state: the scene nodes and the blocks that will replace them (CO-03).
struct RealmEdit {
    oct::File& f;
    std::vector<size_t> nodes;           // SceneTreeNodePool entries' record indices
    std::vector<Block> realms, volumes;  // RealmDataPool / RealmVolumePool entries, copied before the splice
    std::map<size_t, Block> blocks;      // node -> its replacement; key order is the splice order

    // the first level-3 field named name of node n, or null
    const oct::Rec* nodeField(size_t n, const char* name) const
    {
        for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
            if (f.recs[j].level == 3 && f.name(f.recs[j]) == name) return &f.recs[j];
        return nullptr;
    }
    std::string typeOf(size_t n) const
    {
        const oct::Rec* r = nodeField(n, "Type");
        return r ? f.name0(*r) : std::string();
    }
    // a node block's start: its Node record, NodeName, Uuid, DisplayLayer, and Type = type
    Block head(size_t n, const char* type)
    {
        Block b = {f.recs[nodes[n]]};
        for (const char* k : {"NodeName", "Uuid", "DisplayLayer"})
            if (const oct::Rec* r = nodeField(n, k)) b.push_back(*r);
        b.push_back(f.nameScalar(3, f.addName("Type"), type));
        return b;
    }
    // a node block's end: ParentNodeReferences and LocalToParentMatrix
    void tail(size_t n, Block& b) const
    {
        for (const char* k : {"ParentNodeReferences", "LocalToParentMatrix"})
            if (const oct::Rec* r = nodeField(n, k)) b.push_back(*r);
    }
    // the node an entry's NodeRef names, or -1 when there is none or it is out of range
    int nodeOf(const Block& e) const
    {
        const oct::Rec* ref = entryField(f, e, "NodeRef");
        const int n = ref ? f.int0(*ref) : -1;
        return n <= 0 || n >= (int)nodes.size() ? -1 : n;
    }
};

// RealmData scene nodes for the world form: an entry whose NodeRef is a Transform node becomes Type RealmData.
inline int realmDataBlocks(RealmEdit& r)
{
    oct::File& f = r.f;
    std::set<std::string> volumeRealms;   // AddToZone 0 for them: inside 0 / outside 1 in interiorlight_test
    for (const Block& v : r.volumes)
        if (const oct::Rec* name = entryField(f, v, "RealmName")) volumeRealms.insert(f.name0(*name));
    int done = 0;
    for (const Block& e : r.realms) {
        const int n = r.nodeOf(e);
        if (n < 0 || r.typeOf(n) != "Transform") continue;
        Block b = r.head(n, "RealmData");
        r.tail(n, b);
        const oct::Rec* name = entryField(f, e, "Name");
        const std::string realm = name ? f.name0(*name) : "";
        if (name) b.push_back(*name);
        b.push_back(f.intScalar(3, "NodeRef", n));
        b.push_back(f.intScalar(3, "SkyNodeRef", n));
        for (const char* k : kRealmFields)
            if (const oct::Rec* x = entryField(f, e, k)) b.push_back(*x);
        b.push_back(f.intScalar(3, "AddToZone", volumeRealms.count(realm) ? 0 : 1));
        for (const char* k : kRealmFlags) b.push_back(f.intScalar(3, k, 0));
        r.blocks[(size_t)n] = b;
        ++done;
    }
    return done;
}

// VolumeExtents x the node's scale (pCube205), as 3.0's VolumeExtentsNS.
inline std::vector<float> scaledExtents(const RealmEdit& r, size_t n, const oct::Rec* extents)
{
    float x[3] = {0, 0, 0}, m[16];
    floatsOf(extents, 3, x);
    memcpy(m, kIdentity, sizeof(m));
    floatsOf(r.nodeField(n, "LocalToParentMatrix"), 16, m);
    std::vector<float> ns(3);
    for (int k = 0; k < 3; ++k) {
        const float* row = m + 4 * k;
        ns[k] = x[k] * sqrtf(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    }
    return ns;
}

// RealmVolume scene nodes: each entry's NodeRef node (the volume's Geometry) becomes Type RealmVolume.
inline int volumeBlocks(RealmEdit& r)
{
    oct::File& f = r.f;
    int done = 0;
    for (const Block& e : r.volumes) {
        const int n = r.nodeOf(e);
        if (n < 0) continue;
        Block b = r.head(n, "RealmVolume");
        b.push_back(f.intScalar(3, "NumRealmNames", 1));
        if (const oct::Rec* x = entryField(f, e, "RealmName"))
            b.push_back(octx::nameList(f, 3, "RealmNames", {f.name0(*x)}));
        for (const char* k : {"ZoneName", "InsideSize", "VolumeExtents"})
            if (const oct::Rec* x = entryField(f, e, k)) b.push_back(*x);
        const oct::Rec* extents = entryField(f, e, "VolumeExtents");
        if (extents && extents->pay.size() >= 13)
            b.push_back(octx::floatList(f, 3, "VolumeExtentsNS", scaledExtents(r, (size_t)n, extents)));
        for (const char* k : {"TypeName", "OverrideType", "BlendDistance"})
            if (const oct::Rec* x = entryField(f, e, k)) b.push_back(*x);
        b.push_back(f.intScalar(3, "InRealmFile", 0));
        b.push_back(f.intScalar(3, "BlendWithAvatarPos", 0));
        r.tail((size_t)n, b);
        r.blocks[(size_t)n] = b;
        ++done;
    }
    return done;
}

// A float scalar field of node n, or d.
inline float nodeFloat(const RealmEdit& r, size_t n, const char* name, float d)
{
    const oct::Rec* x = r.nodeField(n, name);
    float v = d;
    const bool isFloat = x && x->pay.size() >= 4 && oct::spec(x->type).kind == oct::FLOAT;
    if (isFloat) memcpy(&v, x->pay.data() + x->pay.size() - 4, 4);
    return v;
}

// The level-4 name records of node n called name: its StaticKeyValuePairs entries ('FalloffRadius' = '60.0').
inline std::vector<size_t> keyValues(const RealmEdit& r, size_t n, const char* name)
{
    std::vector<size_t> out;
    for (size_t j = r.nodes[n] + 1, e = r.f.blockEnd(r.nodes[n]); j < e; ++j) {
        const oct::Rec& x = r.f.recs[j];
        if (x.level == 4 && r.f.name(x) == name && oct::spec(x.type).kind == oct::NAME) out.push_back(j);
    }
    return out;
}

// The first key-value entry name that reads as a float, or d.
inline float keyFloat(const RealmEdit& r, size_t n, const char* name, float d)
{
    for (size_t j : keyValues(r, n, name)) {
        const std::vector<std::string> v = r.f.nameValues(r.f.recs[j]);
        if (v.empty()) continue;
        try {
            return std::stof(v.back());
        } catch (const std::exception&) {
            // not a number: the next entry, else d
        }
    }
    return d;
}

// Whether the first key-value entry name is True.
inline bool keyTrue(const RealmEdit& r, size_t n, const char* name)
{
    const std::vector<size_t> js = keyValues(r, n, name);
    if (js.empty()) return false;
    const std::vector<std::string> v = r.f.nameValues(r.f.recs[js[0]]);
    if (v.empty()) return false;
    return v.back() == "True" || v.back() == "true";
}

// A 1.0 light's 3.0 LightType, FeatureCaps and LightData; type -1 for a kind 3.0 has no rule for.
struct Light {
    int type = -1, caps = 0;
    std::vector<float> data;
};
inline Light light30(const RealmEdit& r, size_t n, const std::string& kind)
{
    Light l;
    float col[3] = {0, 0, 0};
    floatsOf(r.nodeField(n, "LightColor"), 3, col);
    const float inten = nodeFloat(r, n, "LightIntensity", 0.f);
    if (kind == "Directional" || kind == "Ambient") {   // 6: [r, g, b, I, 0 x6]; 7: [r, g, b, I]
        l.type = kind == "Directional" ? 6 : 7;
        l.data.assign(l.type == 6 ? 10 : 4, 0.f);
        memcpy(l.data.data(), col, 12);
        l.data[3] = inten;
    } else if (kind == "Point" || kind == "Spot") {
        // twins in both games (31/31 static Point lights; Spot from lr_generalstore); FeatureCaps = Specular (232/233)
        l.type = kind == "Point" ? 0 : 1;
        l.caps = keyTrue(r, n, "Specular") ? 1 : 0;
        l.data = {col[0], col[1], col[2], inten, keyFloat(r, n, "FalloffRadius", 10.f),
                  keyFloat(r, n, "FalloffStrength", 9.f), keyFloat(r, n, "FalloffClamp", 1.f)};
        if (l.type == 1) {
            l.data.push_back(nodeFloat(r, n, "ConeAngle", 0.f));
            l.data.push_back(nodeFloat(r, n, "PenumbraAngle", 0.f));
            l.data.push_back(nodeFloat(r, n, "Dropoff", 0.f));
        }
        l.data.push_back(-1.f);
    }
    return l;
}

// Light nodes with a 1.0 string LightType in 3.0's int form; a kind without a rule becomes a plain Transform.
inline int lightBlocks(RealmEdit& r)
{
    oct::File& f = r.f;
    int done = 0;
    for (size_t n = 0; n < r.nodes.size(); ++n) {
        if (r.blocks.count(n) || r.typeOf(n) != "Light") continue;   // realm and volume blocks come first
        const oct::Rec* lt = r.nodeField(n, "LightType");
        if (!lt || oct::spec(lt->type).kind != oct::NAME) continue;   // already 3.0 (int)
        const Light l = light30(r, n, f.name0(*lt));
        Block b = r.head(n, l.type >= 0 ? "Light" : "Transform");
        if (l.type >= 0) {
            b.push_back(f.intScalar(3, "LightType", l.type));
            b.push_back(f.intScalar(3, "FeatureCaps", l.caps));
            b.push_back(octx::floatList(f, 3, "LightData", l.data));
            if (const oct::Rec* x = r.nodeField(n, "ExclusionType")) b.push_back(*x);
        }
        r.tail(n, b);
        r.blocks[n] = b;
        ++done;
    }
    return done;
}

// Replaces each node with its block in one pass (C8: an erase and insert per node shifted the whole file).
inline void spliceNodeBlocks(RealmEdit& r)
{
    std::vector<oct::Rec> out;
    out.reserve(r.f.recs.size() + 16 * r.blocks.size());
    size_t at = 0;
    for (auto& [n, b] : r.blocks) {
        const size_t a = r.nodes[n], e = r.f.blockEnd(a);
        octx::detail::moveAppend(out, r.f.recs, at, a);
        octx::detail::moveAppend(out, b, 0, b.size());
        at = e;
    }
    octx::detail::moveAppend(out, r.f.recs, at, r.f.recs.size());
    r.f.recs.swap(out);
}

// Seeds for the Uuids: FNV-1a's prime, its basis with the last digit dropped (keep it: CO-19), a second basis.
inline constexpr uint64_t kFnvPrime = 1099511628211ull;
inline constexpr uint64_t kUuidBasis = 1469598103934665603ull;
inline constexpr uint64_t kUuidBasis2 = 0x9e3779b97f4a7c15ull;
inline constexpr uint64_t kUuidSalt = 0x52;
inline void uuidStep(uint64_t& h1, uint64_t& h2, uint8_t c)
{
    h1 = (h1 ^ c) * kFnvPrime;
    h2 = (h2 ^ c) * kFnvPrime + kUuidSalt;
}
// A 16-byte Uuid from seed and ordinal k: unique within the file, stable across loads.
inline std::string uuid(const std::string& seed, int k)
{
    uint64_t h1 = kUuidBasis, h2 = kUuidBasis2;
    for (unsigned char c : seed) uuidStep(h1, h2, c);
    for (int q = 0; q < 4; ++q) uuidStep(h1, h2, (uint8_t)(k >> (8 * q)));
    std::string id(16, '\0');
    bytes::setLe64(id, 0, h1);
    bytes::setLe64(id, 8, h2);
    return id;
}

// Each LightRig lacking a Uuid gains one after its LightRefs; seed: a realm file's name, empty for a world.
inline void addRigUuids(oct::File& f, const std::string& seed)
{
    long rp = f.pool("LightRigPool");
    if (rp < 0) return;
    int k = 0;   // every rig's ordinal, the skipped ones too
    for (size_t i = rp + 1; i < f.blockEnd(rp); i = f.blockEnd(i), ++k) {
        bool has = false;
        size_t refs = 0;
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
            has = has || f.name(f.recs[j]) == "Uuid";
            if (f.name(f.recs[j]) == "LightRefs") refs = j;
        }
        if (has || !refs) continue;
        f.recs.insert(f.recs.begin() + refs + 1, octx::blobRec(f, f.recs[refs].level, "Uuid", uuid(seed, k)));
    }
}

// How the realm-file rule moves a node reference: the RealmData node becomes node 1 (CO-22, three rules).
enum class Shift { FromZero, Always, FromOne };
struct ShiftRow {
    const char* name;
    Shift shift;
};
inline constexpr ShiftRow kRefShifts[] = {
    {"ParentNodeReferences", Shift::FromZero},
    {"LinkedNodeRefs", Shift::FromZero},
    {"LightRefs", Shift::Always},   // -1 too; why is unverified (CO-22)
    {"LinkedSceneTreeNodeRefs", Shift::Always},
    {"SceneTreeNodeRef", Shift::FromOne},
};
inline int32_t shifted(int32_t x, Shift s)
{
    switch (s) {
    case Shift::FromZero:
        return x >= 0 ? x + 1 : x;
    case Shift::Always:
        return x + 1;
    default:
        return x >= 1 ? x + 1 : x;
    }
}
// Every int node reference of the file, moved up by one where its rule says so.
inline void shiftNodeRefs(oct::File& f)
{
    for (oct::Rec& r : f.recs) {
        const std::string& n = f.name(r);
        const ShiftRow* rule = nullptr;
        for (const ShiftRow& row : kRefShifts)
            if (n == row.name) rule = &row;
        if (!rule || oct::spec(r.type).kind != oct::INT) continue;
        std::vector<int32_t> v = f.ints(r);
        for (int32_t& x : v) x = shifted(x, rule->shift);
        if (oct::spec(r.type).shape != oct::SCALAR) f.setInts(r, v);
        else if (!v.empty()) f.setInt(r, v[0]);
    }
}

// The realm file's RealmData scene node (node 1), in the field order of all 367 shipped 3.0 ones.
inline Block realmDataNode(oct::File& f, const Block& e, const std::string& realmName)
{
    std::string up = realmName;
    for (char& c : up) c = (char)toupper((unsigned char)c);
    Block b = {octx::keyedNode(f, 2, "Node", 1)};
    b.push_back(f.nameScalar(3, f.addName("NodeName"), up));
    b.push_back(octx::blobRec(f, 3, "Uuid", uuid(realmName + "/RealmData", 0)));
    // DisplayLayer: 0 with a DisplayLayerPool, else -1 (118 / 118 shipped without one)
    b.push_back(f.intScalar(3, "DisplayLayer", f.pool("DisplayLayerPool") >= 0 ? 0 : -1));
    b.push_back(f.nameScalar(3, f.addName("Type"), "RealmData"));
    b.push_back(f.intList(3, "ParentNodeReferences", {0}));
    b.push_back(octx::floatList(f, 3, "LocalToParentMatrix", std::vector<float>(kIdentity, kIdentity + 16)));
    if (const oct::Rec* x = entryField(f, e, "Name")) b.push_back(*x);
    b.push_back(f.intScalar(3, "NodeRef", 1));
    b.push_back(f.intScalar(3, "SkyNodeRef", 0));
    if (const oct::Rec* x = entryField(f, e, kRealmFields[0])) {   // SunLightRef, shifted with the nodes
        oct::Rec s = *x;
        f.setInt(s, f.int0(s) + 1);
        b.push_back(s);
    }
    for (size_t k = 1; k < std::size(kRealmFields); ++k)
        if (const oct::Rec* x = entryField(f, e, kRealmFields[k])) b.push_back(*x);
    b.push_back(f.intScalar(3, "AddToZone", 1));
    for (const char* k : kRealmFlags) b.push_back(f.intScalar(3, k, 0));
    return b;
}

// The PrimaryAsset association dropped (addAssetPool made the 'Root' Asset); the others keyed '0', '1', ...
inline void dropPrimaryAsset(oct::File& f)
{
    long ap = f.pool("AssociationPool");
    if (ap < 0) return;
    std::vector<oct::Rec> out(f.recs.begin(), f.recs.begin() + ap + 1);
    int a = 0;
    const size_t end = f.blockEnd(ap);
    for (size_t i = ap + 1; i < end; i = f.blockEnd(i)) {
        bool primary = false;
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j)
            primary = (f.name(f.recs[j]) == "Type" && f.name0(f.recs[j]) == "PrimaryAsset") || primary;
        if (primary) continue;
        const size_t first = out.size();
        out.insert(out.end(), f.recs.begin() + i, f.recs.begin() + f.blockEnd(i));
        if (!oct::spec(out[first].type).keyed) continue;
        out[first].pay.clear();
        oct::File::put(out[first].pay, f.addName(std::to_string(a++)), 2);
    }
    out.insert(out.end(), f.recs.begin() + end, f.recs.end());
    f.recs.swap(out);
}

// The realm-file rule: a RealmData node 1, references shifted, nodes rekeyed, SurfaceTypesPool, PrimaryAsset dropped.
inline bool realmFile30(oct::File& f, const Block& realm, const std::string& realmName)
{
    // false: no Scene node to put it after; its root children end up under it (ParentNodeReferences shift)
    long np = f.pool("SceneTreeNodePool");
    const bool hasScene = np >= 0 && (size_t)np + 1 < f.recs.size();
    if (!hasScene || f.recs[np + 1].level != 2) return false;
    shiftNodeRefs(f);
    Block b = realmDataNode(f, realm, realmName);
    f.recs.insert(f.recs.begin() + f.blockEnd(np + 1), b.begin(), b.end());   // after the Scene node
    int k = 0;   // node keys '0', '1', ...
    for (size_t i = np + 1, end = f.blockEnd(np); i < end; i = f.blockEnd(i)) {
        if (f.recs[i].level != 2) continue;
        f.recs[i].pay.clear();
        oct::File::put(f.recs[i].pay, f.addName(std::to_string(k++)), 2);
    }
    dropPrimaryAsset(f);
    f.dropPool("SurfaceTypesPool");
    return true;
}
}  // namespace detail

// World lighting in 3.0 form: 3.0 ignores 1.0's realm pools and misreads its string-typed lights. -> nodes changed.
inline int realm30w(oct::File& f, RealmRule rule = RealmRule::WorldOnly)
{
    // a realm with no lighting entry makes FUN_00640f80 write entry[-1] (00640F8E); checked against realmcmp.py
    long np = f.pool("SceneTreeNodePool");
    if (np < 0) return 0;
    detail::RealmEdit r{f, {}, detail::poolEntries(f, "RealmDataPool"), detail::poolEntries(f, "RealmVolumePool"), {}};
    for (size_t i = np + 1, end = f.blockEnd(np); i < end; i = f.blockEnd(i))
        if (f.recs[i].level == 2) r.nodes.push_back(i);
    int done = detail::realmDataBlocks(r);
    const bool worldRealms = done > 0;
    const bool realmFile = rule == RealmRule::RealmFile && !worldRealms && r.realms.size() == 1;
    if (!worldRealms && r.realms.size() && !realmFile) return 0;   // a realm file without the rule: as it is
    done += detail::volumeBlocks(r);
    done += detail::lightBlocks(r);
    if (!done && !realmFile) return 0;
    detail::spliceNodeBlocks(r);
    // a realm file's name seeds its Uuids, so a world and the realms loaded beside it never share one
    const oct::Rec* name = realmFile ? detail::entryField(f, r.realms[0], "Name") : nullptr;
    const std::string realmName = name ? f.name0(*name) : "";
    detail::addRigUuids(f, realmName);
    if (realmFile && detail::realmFile30(f, r.realms[0], realmName)) ++done;
    f.dropPool("RealmDataPool");
    f.dropPool("RealmVolumePool");
    return done;
}

}  // namespace di1
