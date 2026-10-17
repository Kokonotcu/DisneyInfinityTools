// Localisation dictionaries (.dct, FORMATS.md 9): load, dump, and keys re-hashed under another seed. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"
#include "../base/hash.hpp"

namespace dct {

// 3.0's Localize (FUN_006952d0 -> FUN_006932a0) probes every table with the main table's seed: all 3.0 tables use it.
constexpr uint32_t SEED30 = 0x99fc0d82;   // all 3.0 tables but atvremote's

struct Dict {
    uint32_t initval = 0;
    std::vector<std::pair<uint32_t, std::string>> keys;   // table order
};

namespace detail {
// Header fields (FORMATS.md 9); a "rel" points at field offset + 1 + its value.
inline constexpr size_t kHeader = 0x20, kVersionAt = 0x04, kSeedAt = 0x08, kSlotsRelAt = 0x0c, kSlotsAt = 0x10;
inline constexpr size_t kOneAt = 0x14, kContainersAt = 0x1c, kSlotBytes = 12;
inline constexpr uint32_t kVersion = 0x2000, kSlotsRel = 0x13;
inline constexpr size_t kSlotValueAt = 4;   // within a slot: u32 hash, rel -> value, u32 0

inline bool headerOk(const std::string& raw)
{
    if (raw.size() < kHeader || raw.compare(0, 4, "DICT")) return false;
    const bool fixed = bytes::le32(raw, kVersionAt) == kVersion && bytes::le32(raw, kSlotsRelAt) == kSlotsRel;
    const bool noContainers = bytes::le32(raw, kOneAt) == 1 && !bytes::le32(raw, kContainersAt);
    return fixed && noContainers;
}

// Each key's slot: hash % slots, then a linear probe forward with wrap. -1 marks an empty slot.
inline std::vector<int> slotTable(const Dict& d, uint32_t slots)
{
    std::vector<int> tab(slots, -1);
    for (size_t k = 0; k < d.keys.size(); ++k) {
        uint32_t i = d.keys[k].first % slots;
        while (tab[i] >= 0) i = (i + 1) % slots;
        tab[i] = (int)k;
    }
    return tab;
}

// The value pool: '' first, then each distinct value once in slot order; at gets each one's offset.
inline std::string valuePool(const Dict& d, const std::vector<int>& tab, std::unordered_map<std::string, uint32_t>& at)
{
    std::string pool(1, '\0');
    at = {{"", 0}};
    for (int k : tab) {
        if (k < 0 || at.count(d.keys[k].second)) continue;
        at[d.keys[k].second] = (uint32_t)pool.size();
        pool += d.keys[k].second;
        pool.push_back('\0');
    }
    return pool;
}

// A stringids table's names by key hash.
inline std::unordered_map<uint32_t, const std::string*> namesByHash(const Dict& ids)
{
    std::unordered_map<uint32_t, const std::string*> names;
    for (const auto& [h, v] : ids.keys) names.emplace(h, &v);
    return names;
}
}  // namespace detail

// A stringids name as the bytes the engine hashes: Latin-1 (UTF-8 decoded), U+2026 as '&' like the shipped hashes.
inline bool latin1(const std::string& s, std::string& out)
{
    out.clear();
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = (uint8_t)s[i];
        if (c < 0x80) {
            out.push_back((char)c);
            ++i;
            continue;
        }
        if (s.compare(i, 3, "\xe2\x80\xa6") == 0) {
            out.push_back('&');
            i += 3;
            continue;
        }
        if ((c & 0xe0) != 0xc0 || i + 1 >= s.size()) return false;
        const uint32_t cp = (c & 0x1f) << 6 | (s[i + 1] & 0x3f);
        if (cp > 0xff) return false;   // a code point above U+00FF has no Latin-1 form
        out.push_back((char)cp);
        i += 2;
    }
    return true;
}

namespace detail {
// Key h's name in names, hashed under seed into out; false when it has no name or no Latin-1 form.
inline bool rehash(const std::unordered_map<uint32_t, const std::string*>& names, uint32_t h, uint32_t seed,
                   uint32_t& out)
{
    std::string b;
    auto it = names.find(h);
    if (it == names.end() || !latin1(*it->second, b)) return false;
    out = hash::lookup2((const uint8_t*)b.data(), b.size(), seed);
    return true;
}
}  // namespace detail

// False when raw is not a table. Keys are listed from the first empty slot on: re-inserting them rebuilds it.
inline bool load(const std::string& raw, Dict& d)
{
    if (!detail::headerOk(raw)) return false;
    const uint32_t ns = bytes::le32(raw, detail::kSlotsAt);
    if (!ns || detail::kHeader + (uint64_t)detail::kSlotBytes * ns > raw.size()) return false;
    d.initval = bytes::le32(raw, detail::kSeedAt);
    d.keys.clear();
    auto rel = [&](uint32_t slot) { return bytes::le32(raw, detail::kHeader + detail::kSlotBytes * slot + 4); };
    uint32_t empty = 0;   // slots > keys, so there is one
    while (empty < ns && rel(empty)) ++empty;
    if (empty == ns) return false;
    for (uint32_t j = empty + 1; j < empty + ns; ++j) {
        const uint32_t i = j % ns;
        const int32_t r = (int32_t)rel(i);
        if (!r) continue;
        const size_t p = detail::kHeader + detail::kSlotBytes * (size_t)i + detail::kSlotValueAt + 1 + r;
        const size_t end = raw.find('\0', p);
        if (p >= raw.size() || end == std::string::npos) return false;
        d.keys.emplace_back(bytes::le32(raw, detail::kHeader + detail::kSlotBytes * i), raw.substr(p, end - p));
    }
    return true;
}

// dct.py's dump: (keys + 1) * 6 / 5 slots (all 27 shipped counts), values pooled in slot order.
inline std::string dump(const Dict& d)
{
    const uint32_t ns = (uint32_t)((d.keys.size() + 1) * 6 / 5);
    const std::vector<int> tab = detail::slotTable(d, ns);
    std::unordered_map<std::string, uint32_t> at;
    const std::string pool = detail::valuePool(d, tab, at);
    const uint32_t end = (uint32_t)(detail::kHeader + detail::kSlotBytes * ns);
    // the containers rel: end - 0x18 - 1, so it points at the end of the slot table
    const uint32_t h[8] = {0, detail::kVersion, d.initval, detail::kSlotsRel, ns, 1, end - 0x19, 0};
    std::string out;
    for (uint32_t v : h) bytes::appendLe(out, v, 4);
    out.replace(0, 4, "DICT");
    for (uint32_t i = 0; i < ns; ++i) {
        const bool used = tab[i] >= 0;
        const uint32_t slotAt = (uint32_t)(detail::kHeader + detail::kSlotBytes * i + detail::kSlotValueAt);
        bytes::appendLe(out, used ? d.keys[tab[i]].first : 0, 4);
        bytes::appendLe(out, used ? end + at[d.keys[tab[i]].second] - slotAt - 1 : 0, 4);
        bytes::appendLe(out, 0, 4);
    }
    return out + pool;
}

// raw re-keyed under seed, unnamed keys dropped -> keys kept; -1: an input is no table, or raw has seed.
inline int reseed(std::string& raw, const std::string& stringids, uint32_t seed, int* dropped = nullptr)
{
    Dict d, ids;
    if (!load(raw, d) || d.initval == seed || !load(stringids, ids)) return -1;
    const auto names = detail::namesByHash(ids);
    Dict o;
    o.initval = seed;
    for (const auto& [h, v] : d.keys) {
        uint32_t h2 = 0;
        if (detail::rehash(names, h, seed, h2)) o.keys.emplace_back(h2, v);
    }
    if (dropped) *dropped = (int)(d.keys.size() - o.keys.size());
    raw = dump(o);
    return (int)o.keys.size();
}

// Hashes of a table's keys (all 3.0 tables share SEED30, so hashes compare across them).
inline bool hashes(const std::string& raw, std::unordered_set<uint32_t>& out)
{
    Dict d;
    if (!load(raw, d)) return false;
    for (const auto& k : d.keys) out.insert(k.first);
    return true;
}

// raw plus raw10's keys (named by ids10) that neither raw nor have answers -> keys added; -1: no table.
inline int supplement(std::string& raw, const std::string& raw10, const std::string& ids10,
                      const std::unordered_set<uint32_t>& have)
{
    // 1.0 loaded lang/main for every playset; 3,154 of 9,467 named english keys are in no 3.0 table
    Dict d, s, ids;
    if (!load(raw, d) || d.initval != SEED30) return -1;
    if (!load(raw10, s) || !load(ids10, ids)) return -1;
    std::unordered_set<uint32_t> own;
    for (const auto& k : d.keys) own.insert(k.first);
    const auto names = detail::namesByHash(ids);
    int added = 0;
    for (const auto& [h, v] : s.keys) {
        uint32_t h30 = 0;
        if (!detail::rehash(names, h, SEED30, h30)) continue;
        if (have.count(h30) || !own.insert(h30).second) continue;
        d.keys.emplace_back(h30, v);
        ++added;
    }
    if (added) raw = dump(d);
    return added;
}

}  // namespace dct
