// .oct transforms that turn a 1.0 world/mission/exploder tuple file into the form the 3.0 reader takes, in memory.
// Each one is checked against the shipped 3.0 corpus by octtest.exe (strip the 3.0 records, regenerate, compare).
#pragma once
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include "formats/oct.hpp"
#include "convert/records.hpp"
#include "convert/counts.hpp"
#include "convert/snedit.hpp"
#include "convert/assets.hpp"
#include "games/di1/associations.hpp"

namespace octx {

// World lighting in 3.0 form (in-world realms, as interiorlight_test / deathstar_throneroom14_1 ship them; light rule
// from realm30.py, measured on rlm_intro_day which ships in both games). 3.0 ignores the 1.0 RealmDataPool /
// RealmVolumePool and reads 1.0's string-typed lights as garbage, so in_city lit with the engine default (red/dark,
// in-game 2026-10-13). In place, no node index moves:
//   RealmDataPool entry -> its NodeRef node (a Transform) becomes Type RealmData with the entry's fields, NodeRef and
//     SkyNodeRef = itself, AddToZone 0 for a realm some volume names, else 1 (inside 0 / outside 1 in
//     interiorlight_test), RotateToGravity / RotateSkipLighting / BlendWithGravity 0;
//   RealmVolumePool entry -> its NodeRef node (the volume's Geometry) becomes Type RealmVolume: NumRealmNames 1,
//     RealmNames [realm], ZoneName, InsideSize, VolumeExtents (1.0's, mesh-local), VolumeExtentsNS = VolumeExtents x
//     the node's scale (pCube205), TypeName, OverrideType, BlendDistance, InRealmFile 0, BlendWithAvatarPos 0;
//   Light Directional -> LightType 6, LightData [r, g, b, intensity, 0 x6]; Ambient -> 7, [r, g, b, intensity];
//     FeatureCaps 0; the 1.0 named fields go;
//   Light Point -> LightType 0, Spot -> 1, LightData [r, g, b, I, FalloffRadius, FalloffStrength, FalloffClamp,
//     (Spot: ConeAngle, PenumbraAngle, Dropoff,) -1], FeatureCaps = Specular (fitted on the twin files below);
//   LightRig gains a Uuid after LightRefs; the two 1.0 pools go.
// Only worlds (a RealmData NodeRef that is a Transform node); realm files have their own rule (realm30.py),
// applied when realmFile is set (a realms/ asset, cfg realmfile10):
//   RealmDataPool entry -> a RealmData scene node at index 1, after the Scene node, in the field order all 367
//     shipped 3.0 RealmData nodes have: NodeName (Name upper-cased), Uuid, DisplayLayer (0 with a DisplayLayerPool,
//     else -1: 118 / 118 shipped without one hold -1), Type, ParentNodeReferences [0], identity matrix, Name,
//     NodeRef 1 (itself, as every shipped one), SkyNodeRef 0, the entry's fields (SunLightRef + 1), AddToZone 1,
//     RotateToGravity / RotateSkipLighting / BlendWithGravity 0;
//   node references >= 1 shift by one; root children hang under the RealmData node; lights and rigs as above;
//   SurfaceTypesPool and the PrimaryAsset association go (addAssetPool already made the 'Root' Asset).
// Why: 3.0 registers a realm's lighting entry from its RealmData node (realm manager +0x50: 0x6c-byte entries keyed
// by the realm name hash at +0x34). Applying a realm (FUN_0070ed30 -> FUN_00708d70) that has no entry reads an unset
// list slot, FUN_00640ee0 returns -1 and FUN_00640f80 writes entry[-1]: with an empty container that is the write
// to FFFFFFC4 at 00640F8E (crash dumps 2026-10-14: MonstersU as rlm_mu_campus_night loaded, Pirates as
// rlm_pc_calypsobayou_day did; Lone Ranger died right after rlm_lr_colby_midday was served).
// Checked: octtest --oct on 1.0 rlm_intro_day against its shipped 3.0 twin (realmcmp.py, realm30.py's selftest).
inline int realm30w(oct::File& f, bool realmFile = false)
{
    long np = f.pool("SceneTreeNodePool");
    if (np < 0) return 0;
    std::vector<size_t> nodes;
    for (size_t i = np + 1, end = f.blockEnd(np); i < end; i = f.blockEnd(i))
        if (f.recs[i].level == 2) nodes.push_back(i);
    auto entries = [&](const char* pool) {
        std::vector<std::vector<oct::Rec>> out;
        long p = f.pool(pool);
        if (p < 0) return out;
        for (size_t i = p + 1, end = f.blockEnd(p); i < end; i = f.blockEnd(i))
            out.emplace_back(f.recs.begin() + i + 1, f.recs.begin() + f.blockEnd(i));
        return out;
    };
    auto field = [&](const std::vector<oct::Rec>& e, const char* n) -> const oct::Rec* {
        for (auto& r : e) if (r.level == 3 && f.name(r) == n) return &r;
        return nullptr;
    };
    auto rds = entries("RealmDataPool"), rvs = entries("RealmVolumePool");
    std::map<size_t, std::vector<oct::Rec>> blocks;   // node index -> its new record block
    std::set<std::string> volumeRealms;
    for (auto& v : rvs) if (auto r = field(v, "RealmName")) volumeRealms.insert(f.name0(*r));
    auto nodeField = [&](size_t n, const char* name) -> const oct::Rec* {
        for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
            if (f.recs[j].level == 3 && f.name(f.recs[j]) == name) return &f.recs[j];
        return nullptr;
    };
    auto head = [&](size_t n, const char* type) {   // Node key, NodeName, Uuid, DisplayLayer, Type
        std::vector<oct::Rec> b = {f.recs[nodes[n]]};
        for (const char* k : {"NodeName", "Uuid", "DisplayLayer"}) if (auto r = nodeField(n, k)) b.push_back(*r);
        b.push_back(f.nameScalar(3, f.addName("Type"), type));
        return b;
    };
    auto tail = [&](size_t n, std::vector<oct::Rec>& b) {
        for (const char* k : {"ParentNodeReferences", "LocalToParentMatrix"}) if (auto r = nodeField(n, k)) b.push_back(*r);
    };
    auto typeOf = [&](size_t n) { auto r = nodeField(n, "Type"); return r ? f.name0(*r) : std::string(); };
    int done = 0;
    bool worldRealms = false;
    for (auto& e : rds) {
        auto ref = field(e, "NodeRef");
        int n = ref ? f.int0(*ref) : -1;
        if (n <= 0 || n >= (int)nodes.size() || typeOf(n) != "Transform") continue;
        worldRealms = true;
        auto b = head(n, "RealmData");
        tail(n, b);
        std::string name = field(e, "Name") ? f.name0(*field(e, "Name")) : "";
        if (auto r = field(e, "Name")) b.push_back(*r);
        b.push_back(f.intScalar(3, "NodeRef", n));
        b.push_back(f.intScalar(3, "SkyNodeRef", n));
        for (const char* k : {"SunLightRef", "AmbientRigRef", "SunRigRef", "SceneSettingsRef", "SettingsName", "ZoneName", "RigBlendType",
                              "SettingsNonDefault", "SunNoDirection", "SunNoColor", "StartWeight", "FollowsCamera", "ParallaxFactor", "GlobalOverride"})
            if (auto r = field(e, k)) b.push_back(*r);
        b.push_back(f.intScalar(3, "AddToZone", volumeRealms.count(name) ? 0 : 1));
        for (const char* k : {"RotateToGravity", "RotateSkipLighting", "BlendWithGravity"}) b.push_back(f.intScalar(3, k, 0));
        blocks[n] = b;
        ++done;
    }
    realmFile = realmFile && !worldRealms && rds.size() == 1;
    if (!worldRealms && rds.size() && !realmFile) return 0;   // a realm file without realmFile: left as it is
    for (auto& e : rvs) {
        auto ref = field(e, "NodeRef");
        int n = ref ? f.int0(*ref) : -1;
        if (n <= 0 || n >= (int)nodes.size()) continue;
        auto b = head(n, "RealmVolume");
        b.push_back(f.intScalar(3, "NumRealmNames", 1));
        if (auto r = field(e, "RealmName")) b.push_back(nameList(f, 3, "RealmNames", {f.name0(*r)}));
        for (const char* k : {"ZoneName", "InsideSize", "VolumeExtents"}) if (auto r = field(e, k)) b.push_back(*r);
        if (auto ve = field(e, "VolumeExtents"); ve && ve->pay.size() >= 13) {
            float x[3], m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            memcpy(x, ve->pay.data() + 1, 12);
            if (auto mr = nodeField(n, "LocalToParentMatrix"); mr && mr->pay.size() >= 65) memcpy(m, mr->pay.data() + 1, 64);
            std::vector<float> ns(3);
            for (int k = 0; k < 3; ++k) ns[k] = x[k] * sqrtf(m[4 * k] * m[4 * k] + m[4 * k + 1] * m[4 * k + 1] + m[4 * k + 2] * m[4 * k + 2]);
            b.push_back(floatList(f, 3, "VolumeExtentsNS", ns));
        }
        for (const char* k : {"TypeName", "OverrideType", "BlendDistance"}) if (auto r = field(e, k)) b.push_back(*r);
        b.push_back(f.intScalar(3, "InRealmFile", 0));
        b.push_back(f.intScalar(3, "BlendWithAvatarPos", 0));
        tail(n, b);
        blocks[n] = b;
        ++done;
    }
    for (size_t n = 0; n < nodes.size(); ++n) {   // lights
        if (blocks.count(n) || typeOf(n) != "Light") continue;
        auto lt = nodeField(n, "LightType");
        if (!lt || oct::spec(lt->type).kind != oct::NAME) continue;   // already 3.0 (int)
        std::string kind = f.name0(*lt);
        auto flt = [&](const char* name, float d) {   // a float scalar field of the node
            auto r = nodeField(n, name);
            float v = d;
            if (r && r->pay.size() >= 4 && oct::spec(r->type).kind == oct::FLOAT) memcpy(&v, r->pay.data() + r->pay.size() - 4, 4);
            return v;
        };
        auto kv = [&](const char* name, float d) {    // a StaticKeyValuePairs entry ('FalloffRadius' = '60.0')
            for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
                if (f.recs[j].level == 4 && f.name(f.recs[j]) == name && oct::spec(f.recs[j].type).kind == oct::NAME) {
                    auto v = f.nameValues(f.recs[j]);
                    if (!v.empty()) { try { return std::stof(v.back()); } catch (...) {} }
                }
            return d;
        };
        auto kvTrue = [&](const char* name) {
            for (size_t j = nodes[n] + 1, e = f.blockEnd(nodes[n]); j < e; ++j)
                if (f.recs[j].level == 4 && f.name(f.recs[j]) == name && oct::spec(f.recs[j].type).kind == oct::NAME) {
                    auto v = f.nameValues(f.recs[j]);
                    return !v.empty() && (v.back() == "True" || v.back() == "true");
                }
            return false;
        };
        std::vector<float> data;
        int type = -1, caps = 0;
        float col[3] = {0, 0, 0};
        if (auto c = nodeField(n, "LightColor"); c && c->pay.size() >= 13) memcpy(col, c->pay.data() + 1, 12);
        float inten = flt("LightIntensity", 0.f);
        if (kind == "Directional" || kind == "Ambient") {
            type = kind == "Directional" ? 6 : 7;
            data.assign(type == 6 ? 10 : 4, 0.f);
            memcpy(data.data(), col, 12);
            data[3] = inten;
        } else if (kind == "Point" || kind == "Spot") {
            // twins in both games (31/31 static Point lights exact, choreographies differ only in animated keys;
            // Spot from lr_generalstore): [r, g, b, I, FalloffRadius (10), FalloffStrength (9), FalloffClamp (1),
            // Spot: ConeAngle, PenumbraAngle, Dropoff, then -1]; FeatureCaps = Specular (232 / 233)
            type = kind == "Point" ? 0 : 1;
            caps = kvTrue("Specular") ? 1 : 0;
            data = {col[0], col[1], col[2], inten, kv("FalloffRadius", 10.f), kv("FalloffStrength", 9.f), kv("FalloffClamp", 1.f)};
            if (type == 1) { data.push_back(flt("ConeAngle", 0.f)); data.push_back(flt("PenumbraAngle", 0.f)); data.push_back(flt("Dropoff", 0.f)); }
            data.push_back(-1.f);
        }
        if (type >= 0) {
            auto b = head(n, "Light");
            b.push_back(f.intScalar(3, "LightType", type));
            b.push_back(f.intScalar(3, "FeatureCaps", caps));
            b.push_back(floatList(f, 3, "LightData", data));
            if (auto x = nodeField(n, "ExclusionType")) b.push_back(*x);
            tail(n, b);
            blocks[n] = b;
        } else {   // a kind 3.0 has no twin rule for: a plain Transform
            auto b = head(n, "Transform");
            tail(n, b);
            blocks[n] = b;
        }
        ++done;
    }
    if (!done && !realmFile) return 0;
    // replace the node blocks in one pass over the records (an erase + insert per node shifted the whole file each time)
    {
        std::vector<oct::Rec> out;
        out.reserve(f.recs.size() + 16 * blocks.size());
        size_t at = 0;
        for (auto& [n, b] : blocks) {
            size_t a = nodes[n], e = f.blockEnd(a);
            out.insert(out.end(), std::make_move_iterator(f.recs.begin() + at), std::make_move_iterator(f.recs.begin() + a));
            out.insert(out.end(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()));
            at = e;
        }
        out.insert(out.end(), std::make_move_iterator(f.recs.begin() + at), std::make_move_iterator(f.recs.end()));
        f.recs.swap(out);
    }
    // a realm file's name seeds its Uuids, so a world and the realms loaded beside it never share one
    std::string realmName;
    if (realmFile)
        if (auto r = field(rds[0], "Name")) realmName = f.name0(*r);
    auto uuid = [&](const std::string& seed, int k) {   // FNV-1a of seed + ordinal (empty seed: the world form)
        std::string id(16, '\0');
        uint64_t h1 = 1469598103934665603ull, h2 = 0x9e3779b97f4a7c15ull;
        for (unsigned char c : seed) { h1 = (h1 ^ c) * 1099511628211ull; h2 = (h2 ^ c) * 0x100000001b3ull + 0x52; }
        for (int q = 0; q < 4; ++q) { h1 = (h1 ^ (uint8_t)(k >> (8 * q))) * 1099511628211ull; h2 = (h2 ^ (uint8_t)(k >> (8 * q))) * 0x100000001b3ull + 0x52; }
        memcpy(&id[0], &h1, 8);
        memcpy(&id[8], &h2, 8);
        return id;
    };
    // rigs gain a Uuid after LightRefs (FNV-1a of the rig's ordinal: unique within the file, stable)
    if (long rp = f.pool("LightRigPool"); rp >= 0) {
        int k = 0;
        for (size_t i = rp + 1; i < f.blockEnd(rp); i = f.blockEnd(i), ++k) {
            bool has = false;
            size_t refs = 0;
            for (size_t j = i + 1; j < f.blockEnd(i); ++j) {
                if (f.name(f.recs[j]) == "Uuid") has = true;
                if (f.name(f.recs[j]) == "LightRefs") refs = j;
            }
            if (has || !refs) continue;
            f.recs.insert(f.recs.begin() + refs + 1, blobRec(f, f.recs[refs].level, "Uuid", uuid(realmName, k)));
        }
    }
    long np2 = f.pool("SceneTreeNodePool");
    if (realmFile && np2 >= 0 && (size_t)np2 + 1 < f.recs.size() && f.recs[np2 + 1].level == 2) {
        const auto& e = rds[0];
        // node references >= 1 shift by one (the new node is 1); root children hang under it
        for (auto& r : f.recs) {
            const std::string& n = f.name(r);
            int mode = n == "ParentNodeReferences" ? 1 : (n == "LightRefs" || n == "LinkedSceneTreeNodeRefs") ? 2
                     : n == "LinkedNodeRefs" ? 3 : n == "SceneTreeNodeRef" ? 4 : 0;
            if (!mode || oct::spec(r.type).kind != oct::INT) continue;
            auto v = f.ints(r);
            for (auto& x : v)
                x = mode == 1 ? (x >= 1 ? x + 1 : x == 0 ? 1 : x) : mode == 2 ? x + 1 : mode == 3 ? (x >= 0 ? x + 1 : x) : (x >= 1 ? x + 1 : x);
            if (oct::spec(r.type).shape == oct::SCALAR) { if (!v.empty()) f.setInt(r, v[0]); }
            else f.setInts(r, v);
        }
        std::string up = realmName;
        for (char& c : up) c = (char)toupper((unsigned char)c);
        std::vector<oct::Rec> b = {keyedNode(f, 2, "Node", 1)};
        b.push_back(f.nameScalar(3, f.addName("NodeName"), up));
        b.push_back(blobRec(f, 3, "Uuid", uuid(realmName + "/RealmData", 0)));
        b.push_back(f.intScalar(3, "DisplayLayer", f.pool("DisplayLayerPool") >= 0 ? 0 : -1));
        b.push_back(f.nameScalar(3, f.addName("Type"), "RealmData"));
        b.push_back(f.intList(3, "ParentNodeReferences", {0}));
        b.push_back(floatList(f, 3, "LocalToParentMatrix", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}));
        if (auto r = field(e, "Name")) b.push_back(*r);
        b.push_back(f.intScalar(3, "NodeRef", 1));
        b.push_back(f.intScalar(3, "SkyNodeRef", 0));
        if (auto r = field(e, "SunLightRef")) { oct::Rec s = *r; f.setInt(s, f.int0(s) + 1); b.push_back(s); }
        for (const char* k : {"AmbientRigRef", "SunRigRef", "SceneSettingsRef", "SettingsName", "ZoneName", "RigBlendType", "SettingsNonDefault",
                              "SunNoDirection", "SunNoColor", "StartWeight", "FollowsCamera", "ParallaxFactor", "GlobalOverride"})
            if (auto r = field(e, k)) b.push_back(*r);
        b.push_back(f.intScalar(3, "AddToZone", 1));
        for (const char* k : {"RotateToGravity", "RotateSkipLighting", "BlendWithGravity"}) b.push_back(f.intScalar(3, k, 0));
        f.recs.insert(f.recs.begin() + f.blockEnd(np2 + 1), b.begin(), b.end());   // after the Scene node
        int k = 0;   // node keys '0', '1', ...
        for (size_t i = np2 + 1, end = f.blockEnd(np2); i < end; i = f.blockEnd(i))
            if (f.recs[i].level == 2) { f.recs[i].pay.clear(); oct::File::put(f.recs[i].pay, f.addName(std::to_string(k++)), 2); }
        // the PrimaryAsset association goes (3.0 has the AssetPool 'Root' instead); entries renumbered
        if (long ap = f.pool("AssociationPool"); ap >= 0) {
            std::vector<oct::Rec> out(f.recs.begin(), f.recs.begin() + ap + 1);
            int a = 0;
            size_t end = f.blockEnd(ap);
            for (size_t i = ap + 1; i < end; i = f.blockEnd(i)) {
                bool primary = false;
                for (size_t j = i + 1; j < f.blockEnd(i); ++j)
                    if (f.name(f.recs[j]) == "Type" && f.name0(f.recs[j]) == "PrimaryAsset") primary = true;
                if (primary) continue;
                size_t first = out.size();
                out.insert(out.end(), f.recs.begin() + i, f.recs.begin() + f.blockEnd(i));
                if (oct::spec(out[first].type).keyed) { out[first].pay.clear(); oct::File::put(out[first].pay, f.addName(std::to_string(a++)), 2); }
            }
            out.insert(out.end(), f.recs.begin() + end, f.recs.end());
            f.recs.swap(out);
        }
        f.dropPool("SurfaceTypesPool");
        ++done;
    }
    f.dropPool("RealmDataPool");
    f.dropPool("RealmVolumePool");
    return done;
}

}  // namespace octx
