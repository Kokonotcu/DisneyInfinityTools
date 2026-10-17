// DataMap entries in the form 3.0's per-type handlers take: those add the folder and extension. Any thread.
#pragma once
#include <cstring>
#include <string>

namespace datamap {

namespace detail {
// The Kinds whose 3.0 handler appends the extension itself (FUN_0070c090): a path keeping it gets it twice.
struct KindExt {
    const char* kind;
    const char* ext;
};
inline constexpr KindExt kExtensions[] = {{"DNA", ".dnax"},           {"AnimTreeProject", ".animtreeproject"},
                                          {"BehaviorMap", ".bmx"},     {"Script", ".sx"},
                                          {"MaterialBundle", ".mtb"},  {"APFile", ".ap"}};

// path without the extension its Kind's handler adds, ignoring case.
inline std::string withoutExtension(const std::string& kind, std::string path)
{
    for (const KindExt& k : kExtensions) {
        const size_t el = strlen(k.ext);
        const bool hasExt = path.size() > el && _stricmp(path.c_str() + path.size() - el, k.ext) == 0;
        if (kind == k.kind && hasExt) path.resize(path.size() - el);
    }
    return path;
}
}  // namespace detail

// "Kind|path|..." in 3.0's form, in place; true if changed. 3.0's own entries never carry these forms.
inline bool normalise(std::string& s)
{
    const size_t a = s.find('|');
    if (a == std::string::npos) return false;
    const size_t b = s.find('|', a + 1);
    const std::string kind = s.substr(0, a), path = s.substr(a + 1, (b == std::string::npos ? s.size() : b) - a - 1);
    const std::string rest = b == std::string::npos ? "" : s.substr(b);
    std::string np = path;
    // 3.0 builds "choreographies/%s/%s" from a Cho name: only the last path part stays
    const size_t slash = np.find_last_of("/\\");
    if (kind == "Cho" && slash != std::string::npos) np = np.substr(slash + 1);
    else if (kind != "Cho") np = detail::withoutExtension(kind, np);
    if (np == path) return false;
    s = kind + "|" + np + rest;
    return true;
}

}  // namespace datamap
