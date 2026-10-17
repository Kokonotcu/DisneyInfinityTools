// The AssetPool 3.0 loads a world's or graph's .mer by, and the collision aggregates per asset. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../formats/oct.hpp"
#include "records.hpp"

namespace octx {

namespace detail {

// An association as addAssetPool reads it.
struct AssetFields {
    std::string type, node;
    int32_t sn = -1, stn = -1;   // SubNetworkRef, SceneTreeNodeRef
};
inline constexpr EntryField<AssetFields> kAssetFields[] = {
    {"Type", &AssetFields::type, nullptr},
    {"NodeName", &AssetFields::node, nullptr},
    {"SubNetworkRef", nullptr, &AssetFields::sn},
    {"SceneTreeNodeRef", nullptr, &AssetFields::stn},
};

struct AssetRef {
    std::string name;
    int32_t sn, stn;
};

// The assets of the associations: the PrimaryAsset first as 'Root', then each Asset with a SubNetwork.
inline std::vector<AssetRef> assetRefs(const oct::File& f, long ap)
{
    std::vector<AssetRef> assets;
    for (size_t i = ap + 1, end = f.blockEnd(ap); i < end; i = f.blockEnd(i)) {
        if (f.recs[i].level != f.recs[ap].level + 1) continue;
        AssetFields a;
        readEntryFields(f, i, kAssetFields, a);
        // pieces without a SubNetwork are no assets: inc_phonebooth's piece_1..7 are absent from its 3.0 twin
        if (a.type == "PrimaryAsset") assets.insert(assets.begin(), AssetRef{"Root", a.sn, a.stn});
        else if (a.type == "Asset" && a.sn >= 0) assets.push_back(AssetRef{a.node, a.sn, a.stn});
    }
    return assets;
}

// A graph-less Root beside one Asset with a graph: Root takes it, as in 74 of 75 3.0 twins (assetpair.py).
inline void rootTakesLoneGraph(std::vector<AssetRef>& assets)
{
    const bool lone = assets.size() == 2 && assets[1].sn >= 0;
    if (assets[0].name == "Root" && assets[0].sn < 0 && lone) assets = {AssetRef{"Root", assets[1].sn, assets[0].stn}};
}
}  // namespace detail

// AssetPool from the associations, placed before AssociationPool as shipped. False when there is nothing to add.
inline bool addAssetPool(oct::File& f)
{
    // 3.0 builds a graph's '<name>.mer' path only when its AssetPool is non-empty (FUN_0077cb60, FUN_0076c100)
    if (f.pool("AssetPool") >= 0) return false;
    long ap = f.pool("AssociationPool");
    if (ap < 0) return false;
    std::vector<detail::AssetRef> assets = detail::assetRefs(f, ap);
    if (assets.empty()) return false;
    detail::rootTakesLoneGraph(assets);
    std::vector<oct::Rec> add = {f.node(1, "AssetPool")};
    for (size_t k = 0; k < assets.size(); ++k) {
        add.push_back(keyedNode(f, 2, "Asset", (int)k));
        add.push_back(f.nameScalar(3, f.addName("Name"), assets[k].name));
        add.push_back(f.intScalar(3, "SubNetworkRef", assets[k].sn));
        add.push_back(f.intScalar(3, "SceneTreeNodeRef", assets[k].stn));
        add.push_back(f.intScalar(3, "AggregateCount", 0));   // never read by 3.0
    }
    f.recs.insert(f.recs.begin() + ap, add.begin(), add.end());
    return true;
}

namespace detail {

// An association as addCollisionAggregates reads it: its block [at, end) and fields.
struct Assoc {
    size_t at = 0, end = 0;
    std::string type, node;
    int32_t sn = -1, inst = -1, stn = -1;   // SubNetworkRef, CollisionInstanceRef, SceneTreeNodeRef
    bool hasIndex = false;                  // has an AssetIndex
};
inline constexpr EntryField<Assoc> kAssocFields[] = {
    {"Type", &Assoc::type, nullptr},
    {"NodeName", &Assoc::node, nullptr},
    {"SubNetworkRef", nullptr, &Assoc::sn},
    {"CollisionInstanceRef", nullptr, &Assoc::inst},
    {"SceneTreeNodeRef", nullptr, &Assoc::stn},
};

// The associations in file order; none when they have CollisionAggregates already (3.0 form).
inline std::optional<std::vector<Assoc>> associations(const oct::File& f, long ap)
{
    std::vector<Assoc> as;
    for (size_t i = ap + 1, end = f.blockEnd(ap); i < end; i = f.blockEnd(i)) {
        Assoc a;
        a.at = i;
        a.end = f.blockEnd(i);
        readEntryFields(f, i, kAssocFields, a);
        a.hasIndex = hasField(f, i, "AssetIndex");
        if (a.type == "CollisionAggregate") return std::nullopt;
        as.push_back(a);
    }
    return as;
}

// The AssetPool's asset index per SubNetworkRef (first wins), and each asset's AggregateCount record (-1 none).
struct AssetIndex {
    std::map<int, int> ofSubNetwork;
    std::vector<long> aggregateAt;
};
inline AssetIndex assetIndex(const oct::File& f, long asp)
{
    AssetIndex x;
    for (size_t i = asp + 1, k = 0, end = f.blockEnd(asp); i < end; i = f.blockEnd(i), ++k) {
        x.aggregateAt.push_back(-1);
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
            const std::string& n = f.name(f.recs[j]);
            if (n == "SubNetworkRef") x.ofSubNetwork.emplace(f.int0(f.recs[j]), (int)k);
            else if (n == "AggregateCount") x.aggregateAt.back() = (long)j;
        }
    }
    return x;
}

// SceneTreeNodeRef per (SubNetworkRef, NodeName) of the Transform / Geometry associations; the first wins.
inline std::map<std::pair<int, std::string>, int> sceneRefs(const std::vector<Assoc>& as)
{
    std::map<std::pair<int, std::string>, int> scene;
    for (const Assoc& a : as) {
        const bool placed = a.type == "Transform" || a.type == "Geometry";
        if (placed && a.stn != -1) scene.emplace(std::make_pair(a.sn, a.node), a.stn);
    }
    return scene;
}

// The rebuilt AssociationPool. Its entries get keys '0', '1', ... in file order, as shipped.
struct AggregatePool {
    oct::File& f;
    std::vector<oct::Rec> recs;
    int key = 0;

    // r with its key replaced by the next number; the rest of its payload kept
    oct::Rec rekeyed(oct::Rec r)
    {
        if (!oct::spec(r.type).keyed) return r;
        std::string k2;
        oct::File::put(k2, f.addName(std::to_string(key++)), 2);
        r.pay = k2 + r.pay.substr(2);
        return r;
    }
    // a redirected instance's AssetIndex, then its CollisionAggregate (names in this order: CO-02)
    void addAggregate(const Assoc& a, int asset, int sceneRef)
    {
        if (!a.hasIndex) recs.push_back(f.intScalar(3, "AssetIndex", asset));
        recs.push_back(rekeyed(keyedNode(f, 2, "Association", 0)));
        recs.push_back(f.nameScalar(3, f.addName("NodeName"), a.node));
        recs.push_back(f.nameScalar(3, f.addName("Type"), "CollisionAggregate"));
        recs.push_back(f.intScalar(3, "CollisionInstanceRef", a.inst));
        recs.push_back(f.intScalar(3, "SubNetworkRef", a.sn));
        recs.push_back(f.intScalar(3, "SceneTreeNodeRef", sceneRef));
        recs.push_back(f.intScalar(3, "AssetIndex", asset));
    }
};
}  // namespace detail

// Collision aggregates, as on all 24,287 shipped 3.0 ones: one after each instance naming '...|Transform...'.
inline int addCollisionAggregates(oct::File& f)
{
    long ap = f.pool("AssociationPool"), asp = f.pool("AssetPool");
    if (ap < 0 || asp < 0) return 0;
    const detail::AssetIndex assets = detail::assetIndex(f, asp);
    const std::optional<std::vector<detail::Assoc>> as = detail::associations(f, ap);
    if (!as) return 0;
    const auto scene = detail::sceneRefs(*as);
    std::map<int, int> perAsset;
    int added = 0;
    // one forward rebuild: inserting per association into a ~200k-record file took over a second
    detail::AggregatePool pool{f, {}, 0};
    pool.recs.reserve(f.blockEnd(ap) - ap + as->size() * 8);
    pool.recs.push_back(f.recs[ap]);
    for (const detail::Assoc& a : *as) {
        pool.recs.push_back(pool.rekeyed(f.recs[a.at]));
        pool.recs.insert(pool.recs.end(), f.recs.begin() + a.at + 1, f.recs.begin() + a.end);
        const bool instance = a.type == "CollisionInstanceToDataNode";
        const bool redirected = instance && a.node.find("|Transform") != std::string::npos;
        auto asset = assets.ofSubNetwork.find(a.sn);
        if (!redirected || asset == assets.ofSubNetwork.end()) continue;
        // SceneTreeNodeRef: that of the Transform / Geometry association named by the path before '|Transform'
        auto sc = scene.find({a.sn, a.node.substr(0, a.node.find("|Transform"))});
        pool.addAggregate(a, asset->second, sc == scene.end() ? -1 : sc->second);
        ++perAsset[asset->second];
        ++added;
    }
    // the AssetPool sits before the AssociationPool (addAssetPool), so aggregateAt indices still hold
    for (auto& [asset, count] : perAsset)
        if (asset < (int)assets.aggregateAt.size() && assets.aggregateAt[asset] >= 0)
            f.setInt(f.recs[assets.aggregateAt[asset]], count);
    const size_t poolEnd = f.blockEnd(ap);
    std::vector<oct::Rec> out;
    out.reserve(f.recs.size() - (poolEnd - ap) + pool.recs.size());
    detail::moveAppend(out, f.recs, 0, ap);
    detail::moveAppend(out, pool.recs, 0, pool.recs.size());
    detail::moveAppend(out, f.recs, poolEnd, f.recs.size());
    f.recs.swap(out);
    return added;
}

}  // namespace octx
