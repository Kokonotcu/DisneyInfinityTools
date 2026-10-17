// psdna30: a 1.0 playset avatar DNA (<x>_ps.dnax) 3.0 ships only as <x>.dnax, as that base. Any thread.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../../base/text.hpp"
#include "../../formats/luachunk.hpp"
#include "../../formats/luaconst.hpp"

namespace di1::psdna {

// 3.0's base, its one model constant c replaced by the 1.0 constant c + "_ps" (psmodel.py: 31 of 32).
inline bool psDna30(const std::string& dna10, const std::string& base30, std::string& out, std::string* why = nullptr)
{
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    std::vector<std::string> k10, k30;
    std::vector<int> r10, r30;
    if (!luaconst::strings(dna10, k10, r10) || !luaconst::strings(base30, k30, r30))
        return fail("not a one-function Lua chunk");
    std::map<std::string, std::string> ps;   // lower(1.0 constant) -> 1.0 constant
    for (auto& s : k10) ps.emplace(text::lower(s), s);
    int hit = -1, hits = 0;
    for (size_t i = 0; i < k30.size(); ++i) {
        if (k30[i].find('/') == std::string::npos || !ps.count(text::lower(k30[i]) + "_ps")) continue;
        hit = (int)i;
        ++hits;
    }
    if (hits != 1) return fail(hits ? "several model constants" : "no model constant");
    if (r30[hit] != 1) return fail("the model constant has several uses");
    const std::string from = k30[hit], to = ps[text::lower(from) + "_ps"];
    int n = luachunk::rewrite(base30, out, [&](std::string& s) {
        if (s != from) return false;
        s = to;
        return true;
    });
    if (n != 1) return fail("rewrite");
    if (why) *why = from + " -> " + to;
    return true;
}

}  // namespace di1::psdna
