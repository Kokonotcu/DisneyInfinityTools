// Ledge and rail curves of 1.0 .oct files in 3.0's form (plan OCT-4). convert/snedit, formats/oct. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "../../convert/snedit.hpp"
#include "../../formats/oct.hpp"

namespace di1::curves {

// Flags gain kFlags30; Curve associations name root-basis Dilations. Run after locator30 / collision30. -> curves bound
inline int curves30(oct::File& f, int* flagged = nullptr);

namespace detail {

// 3.0's CurveBSpline (FUN_0071bfe0) reads bits 16-22: 23,931 of 23,932 shipped 3.0 curves carry 0x1E there.
constexpr uint32_t kFlags30 = 0x1E0000;
constexpr int kFlags30Shift = 16;     // 1.0 flags (3, 0x23) use bits 0-5 only
constexpr uint16_t kFieldLevel = 3;   // CurvesPool (1) > curve (2) > Flags (3)

// A 1.0 Curve path, bound with node-local points (FUN_006fc6e0) -> the root-basis Dilation 3.0 binds (FUN_0071c2d0).
inline std::string worldDilation30(octx::SnEdit& e, const std::string& path)
{
    int loc = e.find(path);
    if (loc <= 0 || e.dnOf(loc) >= 0) return "";   // unresolved, or already names a node (3.0 form)
    int xf = e.child(loc, "Transform"), src = xf > 0 ? e.dnOf(xf) : -1;
    if (src < 0 || e.dns[src].type != "Dilation") return "";
    if (e.dns[src].basis == 0) return path + "|Transform";
    // the exporter's TransformDilationWithDilation chain: 743 of the 2,239 shipped 3.0 Curve bindings (curvebind30.py)
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

}  // namespace detail

inline int curves30(oct::File& f, int* flagged)
{
    long cp = f.pool("CurvesPool");
    if (cp < 0) return 0;
    int nf = 0;
    for (size_t i = cp + 1, end = f.blockEnd(cp); i < end; ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level != detail::kFieldLevel || f.name(r) != "Flags") continue;
        std::vector<int32_t> v = f.ints(r);
        bool any = false;
        for (int32_t& x : v) {
            if (((uint32_t)x >> detail::kFlags30Shift) != 0) continue;
            x |= detail::kFlags30;
            any = true;
            ++nf;
        }
        if (any) f.setInts(r, v);
    }
    if (flagged) *flagged = nf;
    return octx::redirectAssociations(f, "Curve", detail::worldDilation30);
}

}  // namespace di1::curves
