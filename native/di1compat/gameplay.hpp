// 1.0 gameplay data (track gp: traversal, vehicles, traffic, tools, activities, store) in the 3.0 systems that
// replaced it, at load time. The 1.0 files are never rewritten; each transform is checked offline (octtest, scratch
// oracles) against the shipped 3.0 corpus.
#pragma once
#include "octx.hpp"

namespace gameplay {

// Ledges and rails (plan OCT-4). in_city.oct's CurvesPool holds 3,164 LH_* ledge-hang and 42 rail curves; every one
// of its 3,207 curves is bound by a Curve association {NodeName, SubNetworkRef, CurveIndices} to a scene node, and
// its points are local to that node (658/665 resolved root-basis curves sit near the origin, not at the node).
// 1.0's handler (FUN_006fc6e0) binds NodeName + '|Transform' (the node's Dilation, any basis); 3.0's (FUN_0071c2d0)
// resolves NodeName itself as a DataNode path (FUN_004ae9b0) and, when nothing resolves, never reads CurveIndices:
// the curve keeps its node-local points, so every Metroville ledge/rail lay near the world origin. Every shipped 3.0
// Curve association names a ROOT-basis Dilation (2,239 in 131 worlds, curvebind30.py): 'P|Transform' when that
// Dilation is root (679), 'P|Transform|In WorldBasis' -- the exporter's TransformDilationWithDilation chain output --
// when it is nested (743), or an existing 'X to WorldBasis' conversion node (800). This names the first two shapes
// (octx::SnEdit builds the chain exactly as locator30 does, minus its DilationToTransform step).
inline std::string worldDilation30(octx::SnEdit& e, const std::string& path)
{
    int loc = e.find(path);
    if (loc <= 0 || e.dnOf(loc) >= 0) return "";   // unresolved, or already names a node (3.0 form)
    int xf = e.child(loc, "Transform"), src = xf > 0 ? e.dnOf(xf) : -1;
    if (src < 0 || e.dns[src].type != "Dilation") return "";
    if (e.dns[src].basis == 0) return path + "|Transform";
    std::string to = path + "|Transform|In WorldBasis";
    if (e.child(xf, "In WorldBasis") >= 0) return to;   // a locator / collision chain on the same node made it
    bool found = false;
    auto steps = e.toWorld(e.dns[src].basis, found);
    if (!found) return "";
    int in = src;
    for (auto& s : steps) {
        std::string bn = s.second == 0 ? "WorldBasis" : "Basis" + std::to_string(s.second);
        int ho = e.header("In " + bn, xf), hp = e.header("Convert To " + bn, xf);
        int out = e.node(ho, "Dilation", s.second, false);
        e.processor(hp, "TransformDilationWithDilation", {in, s.first, out}, {"Input", "Converter", "Output"});
        in = out;
    }
    return to;
}

// Curves in 3.0 form: the Curve associations above, and CurvesPool Flags |= 0x1E0000. 3.0's CurveBSpline
// (FUN_0071bfe0) packs Flags bits 16..22 into a property byte (bits 4-6 = the capture distance of the nearest-curve
// query FUN_0071aef0, forced to >= 1; bits 0-3 copied into its result); 1.0 Flags are 3 / 0x23 (bits 0-5 only, read
// as NURBS flags by FUN_006fbfe0) and the 3.0 exporter re-exported the same curves with 0x1E0000 added (twins 6/6 in 4
// worlds; 23,931/23,932 shipped 3.0 curves carry 0x1E in bits 16..22). The ledge query itself (FUN_0071aa10) filters
// by the name-prefix category ('lh', 'rail': FUN_0071c860 registers the prefix before '_') and geometry only.
// Runs after locator30 / collision30 (reuses their world chains) and before the counts. Returns the curves bound.
inline int curves30(oct::File& f, int* flagged = nullptr)
{
    long cp = f.pool("CurvesPool");
    if (cp < 0) return 0;
    int nf = 0;
    for (size_t i = cp + 1, end = f.blockEnd(cp); i < end; ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level != 3 || f.name(r) != "Flags") continue;
        std::vector<int32_t> v = f.ints(r);
        bool any = false;
        for (int32_t& x : v)
            if (((uint32_t)x >> 16) == 0) { x |= 0x1E0000; any = true; ++nf; }
        if (any) f.setInts(r, v);
    }
    if (flagged) *flagged = nf;
    return octx::redirectAssociations(f, "Curve", worldDilation30);
}

// Surface types (plan OCT-5). A 1.0 Shape's SurfaceType indexes its file's own SurfaceTypesPool name list (1.0
// FUN_0072fcd0; the same 63 names in all 1,608 1.0 .oct); a 3.0 Shape's indexes the engine's global table (built by
// FUN_00754eb0 from startup2 surfacetypes.lua, 109 names; FUN_007482a0 looks names up with stricmp) and 3.0 ignores the
// 1.0 pool. 3.0 inserted snow@24 and noeffect@39, so 1.0 indices >= 24 named the wrong surface: in_city's 6 'metal'
// shapes (26) read as 'leaves'. Each value is mapped by name, as 1.0 resolved it (every 1.0 name is in the 3.0 table;
// twin: 'wood' 54 -> 56 as the 3.0 exporter wrote it, 8/8 shapes). names30 = the engine's table in index order.
// Returns the shapes changed (in_city 6, in_city_combat 5, the 194 mission .oct 0: surf.py).
inline int surfaces30(oct::File& f, const std::vector<std::string>& names30)
{
    long sp = f.pool("SurfaceTypesPool"), cp = f.pool("CollisionShapePool");
    if (sp < 0 || cp < 0 || names30.empty()) return 0;
    std::vector<std::string> names10;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; ++i)
        if (f.name(f.recs[i]) == "SurfaceNames") { names10 = f.nameValues(f.recs[i]); break; }
    std::vector<int> map(names10.size(), -1);
    for (size_t i = 0; i < names10.size(); ++i)
        for (size_t j = 0; j < names30.size() && map[i] < 0; ++j)
            if (_stricmp(names10[i].c_str(), names30[j].c_str()) == 0) map[i] = (int)j;
    int n = 0;
    for (size_t i = cp + 1, end = f.blockEnd(cp); i < end; ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level != 3 || f.name(r) != "SurfaceType") continue;
        int v = f.ints(r)[0];
        if (v >= 0 && v < (int)map.size() && map[v] >= 0 && map[v] != v) { f.setInt(r, map[v]); ++n; }
    }
    return n;
}

}  // namespace gameplay
