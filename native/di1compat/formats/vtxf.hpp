// vformat.vtxf (FORMATS.md 7.2): entries, element types, 3.0's file merged with a source's.
// Scans assume the shipped formatting (RE-50).
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../base/hash.hpp"

namespace vtxf {

// A vertex element type (vtxf.TYPES): struct code, components, bytes each, normalisation divisor (0: none)
struct ElementType { char code; int n, csize; double norm; };

inline bool elementType(const std::string& t, ElementType& v)
{
    struct Named { const char* name; ElementType type; };
    static constexpr Named kTypes[] = {
        {"FLOAT16_2", {'e', 2, 2, 0}}, {"FLOAT16_4", {'e', 4, 2, 0}}, {"FLOAT32_2", {'f', 2, 4, 0}},
        {"FLOAT32_3", {'f', 3, 4, 0}}, {"FLOAT32_4", {'f', 4, 4, 0}}, {"S16_4_NORMALIZED", {'h', 4, 2, 32767.0}},
        {"U16_2", {'H', 2, 2, 0}}, {"U8_4", {'B', 4, 1, 0}}, {"U8_4_NORMALIZED", {'B', 4, 1, 255.0}}};
    for (const Named& x : kTypes) {
        if (t != x.name) continue;
        v = x.type;
        return true;
    }
    return false;
}

// Vertex formats: GUID hash -> semantic with index ('NORMAL' -> 'NORMAL0') -> element type
using VFormats = std::unordered_map<uint32_t, std::map<std::string, std::string>>;

// A formats table entry: its GUID, start, and end past its trailing comma
struct Entry { std::string guid; size_t start, end; };

namespace detail {

inline bool identChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// The matching '}' of the '{' at or after i, or t.size()
inline size_t closeBrace(const std::string& t, size_t i)
{
    int d = 0;
    for (; i < t.size(); ++i) {
        if (t[i] == '{') ++d;
        if (t[i] != '}') continue;
        --d;
        if (d == 0) break;
    }
    return i;
}

// A key ["<guid>"], or a bare identifier when the GUID starts with a letter (15 of 1.0's 44); npos: truncated
inline size_t entryKey(const std::string& t, size_t i, std::string& guid)
{
    if (t[i] != '[') {
        size_t e = i;
        while (e < t.size() && identChar(t[e])) ++e;
        guid = t.substr(i, e - i);
        return e;
    }
    // a truncated key ends the scan; npos + 1 would restart it at the first entry forever (RE-48)
    size_t q = t.find('"', i), e = q == std::string::npos ? q : t.find('"', q + 1);
    size_t close = e == std::string::npos ? e : t.find(']', e);
    if (close == std::string::npos) return close;
    guid = t.substr(q + 1, e - q - 1);
    return close + 1;
}

inline std::string lowered(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

}  // namespace detail

// The formats table's entries; tableEnd: where its closing '}' is
inline std::vector<Entry> entries(const std::string& t, size_t* tableEnd = nullptr)
{
    std::vector<Entry> out;
    size_t f = t.find("formats");
    if (f == std::string::npos) return out;
    size_t i = t.find('{', f);
    if (i == std::string::npos) return out;
    ++i;
    auto ws = [&]() {
        while (i < t.size() && isspace((unsigned char)t[i])) ++i;
    };
    while (true) {
        ws();
        if (i >= t.size() || t[i] == '}') {
            if (tableEnd) *tableEnd = i;
            break;
        }
        const size_t start = i;
        std::string guid;
        i = detail::entryKey(t, i, guid);
        if (i == std::string::npos) break;
        i = t.find('{', i);
        if (i == std::string::npos) break;
        i = detail::closeBrace(t, i) + 1;
        ws();
        if (i < t.size() && t[i] == ',') ++i;
        out.push_back({guid, start, i});
    }
    return out;
}

// Each format's 'main' layout: semantic -> element type, by the GUID's ehashLower
inline VFormats formats(const std::string& t)
{
    constexpr char kSemantic[] = "semantic = \"", kType[] = "type = \"";
    constexpr size_t kSemanticLen = sizeof kSemantic - 1, kTypeLen = sizeof kType - 1;
    VFormats out;
    for (auto& v : entries(t)) {
        size_t m = t.find("main", v.start);
        if (m == std::string::npos || m > v.end) continue;
        size_t ob = t.find('{', m);
        const size_t end = detail::closeBrace(t, ob);
        auto& f = out[hash::ehashLower(v.guid)];
        for (size_t s = t.find(kSemantic, ob); s != std::string::npos && s < end; s = t.find(kSemantic, s + 1)) {
            size_t se = t.find('"', s + kSemanticLen);
            std::string sem = t.substr(s + kSemanticLen, se - s - kSemanticLen);
            size_t eb = t.rfind('{', s), ee = t.find('}', s);   // the element's braces; its type is inside them
            size_t ty = t.find(kType, eb);
            if (ty == std::string::npos || ty > ee) continue;
            size_t te = t.find('"', ty + kTypeLen);
            if (!sem.empty() && !isdigit((unsigned char)sem.back())) sem += "0";
            f[sem] = t.substr(ty + kTypeLen, te - ty - kTypeLen);
        }
    }
    return out;
}

// The direct child tables of the table opening at t[open]: {open, close} pairs
inline std::vector<std::pair<size_t, size_t>> children(const std::string& t, size_t open)
{
    std::vector<std::pair<size_t, size_t>> out;
    int d = 0;
    size_t start = 0;
    for (size_t i = open; i < t.size(); ++i) {
        if (t[i] == '{') {
            ++d;
            if (d == 2) start = i;
            continue;
        }
        if (t[i] != '}') continue;
        if (d == 2) out.push_back({start, i});
        --d;
        if (d == 0) break;
    }
    return out;
}

// key = "..." within [a, b), or ""
inline std::string field(const std::string& t, size_t a, size_t b, const char* key)
{
    std::string k = std::string(key) + " = \"";
    size_t p = t.find(k, a);
    if (p == std::string::npos || p >= b) return "";
    size_t q = t.find('"', p + k.size());
    return t.substr(p + k.size(), q - p - k.size());
}

// The 'types' table: name -> sizeInBytes
inline std::map<std::string, int> typeSizes(const std::string& t)
{
    constexpr char kSize[] = "sizeInBytes = ";
    std::map<std::string, int> out;
    size_t ty = t.find("\ttypes");
    if (ty == std::string::npos) return out;
    for (auto [a, b] : children(t, t.find('{', ty))) {
        size_t s = t.rfind('=', a);
        if (s == std::string::npos) continue;   // t[npos - 1] below
        while (s > 0 && isspace((unsigned char)t[s - 1])) --s;
        size_t n0 = s;
        while (n0 > 0 && detail::identChar(t[n0 - 1])) --n0;
        size_t z = t.find(kSize, a);
        if (z != std::string::npos && z < b) out[t.substr(n0, s - n0)] = atoi(t.c_str() + z + sizeof kSize - 1);
    }
    return out;
}

// The engine gives each element name 8 descriptor slots (FUN_0044d790): a 9th becomes another element (FORMATS.md 7.2)
struct ColourTail { int stream = 0, offset = 0, size = 0; };   // bytes [offset, offset+size) of stream -> stream+1

namespace detail {

// A format entry's text with its trailing vertex colour moved to a stream of its own, if it has one
inline std::string relayColour(const std::string& f, const std::map<std::string, int>& sizes, ColourTail& tail)
{
    size_t m = f.find("main"), st = m == std::string::npos ? m : f.find("streams", m);
    if (st == std::string::npos) return f;
    auto streams = children(f, f.find('{', st));
    if (streams.size() < 2) return f;
    auto [so, sc] = streams.back();
    auto el = children(f, so);
    std::vector<int> off;
    int o = 0;
    for (auto [a, b] : el) {
        auto ty = sizes.find(field(f, a, b, "type"));
        if (ty == sizes.end()) return f;
        off.push_back(o);
        o += ty->second;
    }
    const std::string colour = "COLOR";
    size_t k = el.size();
    while (k > 0 && field(f, el[k - 1].first, el[k - 1].second, "semantic").compare(0, colour.size(), colour) == 0) --k;
    if (k == el.size() || k == 0) return f;   // no trailing colour, or the stream is colour only
    std::string moved;
    for (size_t i = k; i < el.size(); ++i) moved += "\n" + f.substr(el[i].first, el[i].second - el[i].first + 1) + ",";
    tail = {(int)streams.size() - 1, off[k], o - off[k]};
    size_t cut = f.find(',', el[k - 1].second) + 1;   // after the last element kept
    return f.substr(0, cut) + "\n}," + "\n{" + moved + "\n}" + f.substr(sc + 1);
}

}  // namespace detail

// t30 with each format of src it lacks appended, a trailing colour moved out; tails: GUID ehashLower -> what moved
inline std::string merge(const std::string& t30, const std::string& src, int& added,
                         std::map<uint32_t, ColourTail>& tails)
{
    added = 0;
    size_t end30 = 0;
    auto e30 = entries(t30, &end30);
    if (!end30) return t30;
    std::set<std::string> have;
    for (auto& e : e30) have.insert(detail::lowered(e.guid));
    auto sizes = typeSizes(src);
    std::string add;
    for (auto& e : entries(src)) {
        const std::string g = detail::lowered(e.guid);
        if (have.count(g)) continue;
        ColourTail ct;
        std::string block = detail::relayColour(src.substr(e.start, e.end - e.start), sizes, ct);
        if (ct.size) tails[hash::ehashLower(g)] = ct;
        if (block.back() != ',') block += ",";
        add += "\t\t" + block + "\n";
        ++added;
    }
    return t30.substr(0, end30) + add + t30.substr(end30);
}

}  // namespace vtxf
