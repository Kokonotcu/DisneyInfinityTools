// 1.0 DataMap entries into the form 3.0's per-type handlers take (plan PS-2 / research G3). 3.0's FUN_0070c090 builds
// "choreographies/%s/%s" from a Cho name and appends .dnax/.animtreeproject/.bmx/.sx/.mtb/.ap itself; 1.0 entries carry
// the full path and extension (1.0's FUN_006ec410 passed them through), so 3.0 doubled them (in-game 2026-10-12:
// 'choreographies\choreographies\x\x\choreographies\x\x.bent', past the 0x80-byte path buffer). 3.0's own entries
// never carry these forms, so they are untouched by the same rule.
#pragma once
#include <cstring>
#include <string>

namespace datamap {

// "Kind|path|..." -> normalised in place; true if changed.
inline bool normalise(std::string& s)
{
    size_t a = s.find('|');
    if (a == std::string::npos) return false;
    size_t b = s.find('|', a + 1);
    std::string kind = s.substr(0, a), path = s.substr(a + 1, (b == std::string::npos ? s.size() : b) - a - 1);
    std::string rest = b == std::string::npos ? "" : s.substr(b);
    std::string np = path;
    if (kind == "Cho") {
        size_t slash = np.find_last_of("/\\");
        if (slash != std::string::npos) np = np.substr(slash + 1);
    } else {
        static const char* const K[][2] = {{"DNA", ".dnax"}, {"AnimTreeProject", ".animtreeproject"}, {"BehaviorMap", ".bmx"},
                                           {"Script", ".sx"}, {"MaterialBundle", ".mtb"}, {"APFile", ".ap"}};
        for (auto& k : K) {
            size_t el = strlen(k[1]);
            if (kind == k[0] && np.size() > el && _stricmp(np.c_str() + np.size() - el, k[1]) == 0) np.resize(np.size() - el);
        }
    }
    if (np == path) return false;
    s = kind + "|" + np + rest;
    return true;
}

}  // namespace datamap
