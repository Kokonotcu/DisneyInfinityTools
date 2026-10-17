// 1.0 association bindings in 3.0's form: skin influences, locators, cameras, collision (convert/ octx). Any thread.
#pragma once
#include <string>
#include <vector>
#include "../../convert/assets.hpp"
#include "../../convert/snedit.hpp"
#include "../../formats/oct.hpp"

namespace di1 {

namespace detail {
inline std::string parentPath(const std::string& path)
{
    const size_t c = path.rfind('|');
    return c == std::string::npos ? "" : path.substr(0, c + 1);
}

// A SkinInfluence path naming a Transform / Dilation node -> its Matrix4x4 twin '<leaf>.Matrix4x4', made if missing.
inline std::string skinTwin(octx::SnEdit& e, const std::string& path)
{
    int h = e.find(path), src = h > 0 ? e.dnOf(h) : -1;
    if (src < 0) return "";
    std::string t = e.dns[src].type, leaf = e.name(h) + ".Matrix4x4";
    if (t != "Transform" && t != "Dilation") return "";   // Matrix4x4 already, or a type 3.0 never converts
    // External, same basis, fed by a <type>ToMatrix4x4VariantProcessor under its header (tb_tron_disc, _proxyavatar)
    if (e.child(e.parent[h], leaf) < 0) {
        int h1 = e.header(leaf, e.parent[h]), h2 = e.header("Convert" + t + "ToMatrix4x4", h1);
        int nd = e.node(h1, "Matrix4x4", e.dns[src].basis, true);
        e.processor(h2, t + "ToMatrix4x4VariantProcessor", {src, nd}, {"Input", "Output"});
    }
    return parentPath(path) + leaf;
}
}  // namespace detail

// SkinInfluence in 3.0 form. Without the twin 3.0 binds a load-time 'As' adapter that instancing cannot map.
inline int skin30(oct::File& f, const octx::Options& opt)
{
    // FUN_004a5ee0 -> FUN_005077e0 makes the adapter outside the header arena; the clone faults at 0x4cdabe
    return octx::redirectAssociations(f, "SkinInfluence", detail::skinTwin, opt);
}

// A 1.0 binding path in 3.0 form: a header whose 'Transform' child is a Dilation names its root-basis twin.
inline std::string worldTransform30(octx::SnEdit& e, const std::string& path)
{
    int loc = e.find(path);
    if (loc <= 0 || e.dnOf(loc) >= 0) return "";   // unresolved, or already names a node (3.0 form)
    int xf = e.child(loc, "Transform"), src = xf > 0 ? e.dnOf(xf) : -1;
    const bool rootTransform = src >= 0 && e.dns[src].type == "Transform" && e.dns[src].basis == 0;
    if (rootTransform) return path + "|Transform";
    if (src < 0 || e.dns[src].type != "Dilation") return "";
    // '<path>|Transform|In WorldBasis.Transform': the exporter's chain, then DilationToTransform (emp_tatooine_details)
    std::string to = path + "|Transform|In WorldBasis.Transform";
    if (e.child(xf, "In WorldBasis.Transform") >= 0) return to;
    int in = octx::worldChain(e, xf, src, false);
    if (in < 0) return "";
    int ht = e.header("In WorldBasis.Transform", xf), hc = e.header("ConvertDilationToTransform", ht);
    int nt = e.node(ht, "Transform", 0, true);
    e.processor(hc, "DilationToTransformVariantProcessor", {in, nt}, {"Input", "Output"});
    return to;
}

// MeridianLocator in 3.0 form: 3.0 binds a root-basis Transform node (FUN_006c1000 -> FUN_006b6830).
inline int locator30(oct::File& f, const octx::Options& opt)
{
    return octx::redirectAssociations(f, "MeridianLocator", worldTransform30, opt);
}

// Camera in 3.0 form: a non-root camera Transform gets its world twin, 'Transform|In WorldBasis' (camworld.py).
inline int camera30(oct::File& f, const octx::Options& opt)
{
    // without it the binder builds the chain at load with 'As' adapters (FUN_00507c20, FUN_005077e0): instancing faults
    int n = 0;
    octx::redirectAssociations(f, "Camera", [&](octx::SnEdit& e, const std::string& path) -> std::string {
        int cam = e.find(path), xf = cam > 0 ? e.child(cam, "Transform") : -1, src = xf > 0 ? e.dnOf(xf) : -1;
        if (src < 0 || e.dns[src].basis == 0 || e.child(xf, "In WorldBasis") >= 0) return "";
        if (e.dns[src].type != "Transform" && e.dns[src].type != "Dilation") return "";
        if (octx::worldChain(e, xf, src, true) >= 0) ++n;
        return "";   // the association keeps naming the camera
    }, opt);
    return n;
}

// Collision in 3.0 form: instances name '<slot>|Transform|In WorldBasis.Transform', then the aggregates. Runs after
// addAssetPool; returns the aggregates added.
inline int collision30(oct::File& f, const octx::Options& opt)
{
    octx::redirectAssociations(f, "CollisionInstanceToDataNode", worldTransform30, opt);
    return octx::addCollisionAggregates(f);
}

}  // namespace di1
