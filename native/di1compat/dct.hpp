// Localisation dictionaries (.dct; layout in DI3Tools/dct.py and FORMATS.md section 9).
// 3.0's Localize (FUN_006952d0 -> FUN_006932a0) hashes a key once, with its main table's seed, and probes the merged
// table with that hash, so every 3.0 table shares initval 0x99fc0d82 and a 1.0 table (0x98cd7176) never answers
// (in-game 2026-10-13: IN_City_DockFire's dialog showed its key 'IN_City_DockFire_Act'). reseed() re-hashes a 1.0
// table's keys under the 3.0 seed; the names come from the stringids table beside it (its values are the names).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace dct {

constexpr uint32_t SEED30 = 0x99fc0d82;   // all 3.0 tables but atvremote's

// Bob Jenkins' lookup2 hash() (the engine's FUN_0040b900).
inline uint32_t lookup2(const uint8_t* k, size_t n, uint32_t c)
{
    uint32_t a = 0x9e3779b9, b = 0x9e3779b9;
    auto mix = [&]() {
        a -= b; a -= c; a ^= c >> 13;  b -= c; b -= a; b ^= a << 8;   c -= a; c -= b; c ^= b >> 13;
        a -= b; a -= c; a ^= c >> 12;  b -= c; b -= a; b ^= a << 16;  c -= a; c -= b; c ^= b >> 5;
        a -= b; a -= c; a ^= c >> 3;   b -= c; b -= a; b ^= a << 10;  c -= a; c -= b; c ^= b >> 15;
    };
    auto word = [](const uint8_t* p, size_t m) { uint32_t v = 0; for (size_t i = 0; i < m && i < 4; ++i) v |= (uint32_t)p[i] << (8 * i); return v; };
    size_t i = 0;
    for (; i + 12 <= n; i += 12) { a += word(k + i, 4); b += word(k + i + 4, 4); c += word(k + i + 8, 4); mix(); }
    size_t r = n - i;
    a += word(k + i, r);
    b += r > 4 ? word(k + i + 4, r - 4) : 0;
    c += (uint32_t)n + ((r > 8 ? word(k + i + 8, r - 8) : 0) << 8);
    mix();
    return c;
}

struct Dict { uint32_t initval = 0; std::vector<std::pair<uint32_t, std::string>> keys; };   // table order

inline uint32_t u32(const std::string& s, size_t o) { uint32_t v; memcpy(&v, s.data() + o, 4); return v; }

inline bool load(const std::string& raw, Dict& d)
{
    if (raw.size() < 0x20 || raw.compare(0, 4, "DICT") || u32(raw, 4) != 0x2000 || u32(raw, 0xc) != 0x13 || u32(raw, 0x14) != 1 ||
        u32(raw, 0x1c))
        return false;
    uint32_t ns = u32(raw, 0x10);
    if (!ns || 0x20 + 12ull * ns > raw.size()) return false;
    d.initval = u32(raw, 8);
    d.keys.clear();
    // from the first empty slot on (slots > keys, so there is one): re-inserting in that order rebuilds the table
    uint32_t e = 0;
    while (e < ns && u32(raw, 0x24 + 12 * e)) ++e;
    if (e == ns) return false;
    for (uint32_t j = e + 1; j < e + ns; ++j) {
        uint32_t i = j % ns;
        int32_t rel = (int32_t)u32(raw, 0x24 + 12 * i);
        if (!rel) continue;
        size_t p = 0x25 + 12 * (size_t)i + rel, e = raw.find('\0', p);
        if (p >= raw.size() || e == std::string::npos) return false;
        d.keys.emplace_back(u32(raw, 0x20 + 12 * i), raw.substr(p, e - p));
    }
    return true;
}

// dct.dump: slots (keys + 1) * 6 / 5, linear probe from hash % slots, values pooled once in slot order after ''.
inline std::string dump(const Dict& d)
{
    uint32_t ns = (uint32_t)((d.keys.size() + 1) * 6 / 5);
    std::vector<int> tab(ns, -1);
    for (size_t k = 0; k < d.keys.size(); ++k) {
        uint32_t i = d.keys[k].first % ns;
        while (tab[i] >= 0) i = (i + 1) % ns;
        tab[i] = (int)k;
    }
    std::string pool(1, '\0');
    std::unordered_map<std::string, uint32_t> at = {{"", 0}};
    for (int k : tab)
        if (k >= 0 && !at.count(d.keys[k].second)) { at[d.keys[k].second] = (uint32_t)pool.size(); pool += d.keys[k].second; pool.push_back('\0'); }
    uint32_t end = 0x20 + 12 * ns;
    uint32_t h[8] = {0, 0x2000, d.initval, 0x13, ns, 1, end - 0x19, 0};
    std::string out((const char*)h, 32);
    memcpy(&out[0], "DICT", 4);
    for (uint32_t i = 0; i < ns; ++i) {
        uint32_t e[3] = {0, 0, 0};
        if (tab[i] >= 0) { e[0] = d.keys[tab[i]].first; e[1] = end + at[d.keys[tab[i]].second] - 0x25 - 12 * i; }
        out.append((const char*)e, 12);
    }
    return out + pool;
}

// A stringids name as the bytes the engine hashes: Latin-1 (UTF-8 decoded), '...' (U+2026) as '&' like the shipped
// hashes; false if a code point is above U+00FF.
inline bool latin1(const std::string& s, std::string& out)
{
    out.clear();
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) { out.push_back((char)c); ++i; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) { uint32_t cp = (c & 0x1f) << 6 | (s[i + 1] & 0x3f); if (cp > 0xff) return false; out.push_back((char)cp); i += 2; }
        else if (!s.compare(i, 3, "\xe2\x80\xa6")) { out.push_back('&'); i += 3; }
        else return false;
    }
    return true;
}

// raw (a 1.0 table) re-keyed under `seed`; keys with no name in `stringids` (or no Latin-1 form) are dropped, since
// they cannot be re-hashed. -> keys kept, or -1 if either input is not a table or raw already has the seed.
inline int reseed(std::string& raw, const std::string& stringids, uint32_t seed, int* dropped = nullptr)
{
    Dict d, ids;
    if (!load(raw, d) || d.initval == seed || !load(stringids, ids)) return -1;
    std::unordered_map<uint32_t, const std::string*> names;
    for (auto& [h, v] : ids.keys) names.emplace(h, &v);
    Dict o;
    o.initval = seed;
    std::string b;
    for (auto& [h, v] : d.keys) {
        auto it = names.find(h);
        if (it == names.end() || !latin1(*it->second, b)) continue;
        o.keys.emplace_back(lookup2((const uint8_t*)b.data(), b.size(), seed), v);
    }
    if (dropped) *dropped = (int)(d.keys.size() - o.keys.size());
    raw = dump(o);
    return (int)o.keys.size();
}

}  // namespace dct
