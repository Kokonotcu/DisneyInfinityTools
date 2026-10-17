// 1.0 collision surface types as indices into 3.0's global table (plan OCT-5). convert/records. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "../../convert/records.hpp"
#include "../../formats/oct.hpp"

namespace di1::surfaces {

// Each Shape's SurfaceType remapped by name into names30, the engine's table in index order. -> shapes changed
inline int surfaces30(oct::File& f, const std::vector<std::string>& names30);

namespace detail {

constexpr uint16_t kFieldLevel = 3;   // CollisionShapePool (1) > shape (2) > SurfaceType (3)
constexpr char kWho[] = "gameplay";   // the tag of its damaged-input errors (B1)

// Each 1.0 name's index in names30, -1 for none; names match case-blind, as FUN_007482a0 looks them up.
inline std::vector<int> indexMap(const std::vector<std::string>& names10, const std::vector<std::string>& names30)
{
    std::vector<int> map(names10.size(), -1);
    for (size_t i = 0; i < names10.size(); ++i)
        for (size_t j = 0; j < names30.size() && map[i] < 0; ++j)
            if (_stricmp(names10[i].c_str(), names30[j].c_str()) == 0) map[i] = (int)j;
    return map;
}

}  // namespace detail

inline int surfaces30(oct::File& f, const std::vector<std::string>& names30)
{
    long sp = f.pool("SurfaceTypesPool"), cp = f.pool("CollisionShapePool");
    if (sp < 0 || cp < 0 || names30.empty()) return 0;
    std::vector<std::string> names10;
    for (size_t i = sp + 1, end = f.blockEnd(sp); i < end; ++i) {
        if (f.name(f.recs[i]) != "SurfaceNames") continue;
        names10 = f.nameValues(f.recs[i]);
        break;
    }
    // a 1.0 index names its file's SurfaceTypesPool entry (FUN_0072fcd0); 3.0's table (FUN_00754eb0) inserted snow@24
    const std::vector<int> map = detail::indexMap(names10, names30);
    int n = 0;
    for (size_t i = cp + 1, end = f.blockEnd(cp); i < end; ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level != detail::kFieldLevel || f.name(r) != "SurfaceType") continue;
        int v = octx::firstOrThrow(f.ints(r), f, r, detail::kWho);
        const bool known = v >= 0 && v < (int)map.size();
        if (!known || map[v] < 0 || map[v] == v) continue;
        f.setInt(r, map[v]);
        ++n;
    }
    return n;
}

}  // namespace di1::surfaces
