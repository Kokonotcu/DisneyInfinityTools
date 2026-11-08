// realmstart10: a 1.0 RealmCache starts at the weight a Catalyst sets at once. formats/mer; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "../../base/text.hpp"
#include "../../formats/mer.hpp"
#include "../../formats/oct.hpp"

namespace di1::realmstart {

// One RealmCache given a new StartWeight
struct Change {
    std::string realm;
    float from = 0, to = 0;
};

namespace detail {

using ::mer::Link;
using ::mer::Node;

inline constexpr uint16_t kImpulse = 1;   // link code kinds (FORMATS.md 5.3)
inline constexpr uint16_t kData = 3;
inline constexpr uint16_t kKindMask = 0xf;
inline constexpr int kMaxDepth = 8;       // If nodes followed from one Catalyst

// The graph as the start walk reads it: nodes, fields at level fl, index-form links
struct View {
    oct::File& f;
    uint16_t fl;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::map<std::string, std::optional<float>> weights;   // lower-cased realm -> start weight; nullopt: conflict
};

inline int fromPin(const Link& L) { return L.code >> ::mer::kFromPinShift; }
inline int toPin(const Link& L) { return L.code >> ::mer::kToPinShift & ::mer::kPinMask; }

// A scalar int field of node k, or nullopt
inline std::optional<int32_t> intField(const View& v, size_t k, const char* name)
{
    const long j = ::mer::field(v.f, v.nodes[k], v.fl, name);
    if (j < 0 || oct::spec(v.f.recs[j].type).kind != oct::INT) return std::nullopt;
    const std::vector<int32_t> x = v.f.ints(v.f.recs[j]);
    if (x.empty()) return std::nullopt;
    return x[0];
}

// The record index of a scalar float field of node k, or -1
inline long floatField(const View& v, size_t k, const char* name)
{
    const long j = ::mer::field(v.f, v.nodes[k], v.fl, name);
    if (j < 0) return -1;
    const oct::Spec s = oct::spec(v.f.recs[j].type);
    const bool scalarFloat = s.shape == oct::SCALAR && s.kind == oct::FLOAT;
    return scalarFloat && v.f.recs[j].pay.size() >= 4 ? j : -1;
}

// A float scalar's value: its last 4 payload bytes (after a key, when keyed)
inline float floatOf(const oct::Rec& r)
{
    float x = 0;
    memcpy(&x, r.pay.data() + r.pay.size() - 4, 4);
    return x;
}

// A pin's index: node k's own field, else the first same-class node listing it (-1: none)
inline int pinOf(const View& v, size_t k, const char* name)
{
    if (const std::optional<int32_t> own = intField(v, k, name)) return *own;
    for (size_t q = 0; q < v.nodes.size(); ++q) {
        if (v.nodes[q].type != v.nodes[k].type) continue;
        if (const std::optional<int32_t> p = intField(v, q, name)) return *p;
    }
    return -1;
}

// Every index-form LinksBuffer row of the file; the Uuid form is left out (no 1.0 world graph uses it)
inline void collectLinks(View& v)
{
    const long lp = v.f.pool("MeridianLinkPool");
    if (lp < 0) return;
    const std::map<std::string, size_t> noUuids;
    for (size_t i = (size_t)lp + 1, end = v.f.blockEnd((size_t)lp); i < end; ++i) {
        if (v.f.name(v.f.recs[i]) != "Data") continue;
        const std::optional<::mer::IndexLinks> b = ::mer::indexLinks(v.f.recs[i]);
        if (!b || b->uu || !::mer::sizeMatches(*b)) continue;
        for (uint32_t q = 0; q < b->n; ++q)
            if (const std::optional<Link> L = ::mer::linkAt(*b, q, noUuids)) v.links.push_back(*L);
    }
}

// An If's condition at start: its Condition data link from an unsaved Gate's IsGateOpen, else Condition_value
inline std::optional<bool> startCondition(const View& v, size_t k)
{
    const int cond = pinOf(v, k, "Condition");
    for (const Link& L : v.links) {
        const bool intoCondition = L.to == k && toPin(L) == cond && (L.code & kKindMask) == kData;
        if (!intoCondition || L.from >= v.nodes.size()) continue;
        const size_t g = L.from;
        const bool gateOpen = v.nodes[g].type == "Gate" && fromPin(L) == pinOf(v, g, "IsGateOpen");
        const bool unsaved = intField(v, g, "Serialize").value_or(1) == 0;   // a saved gate may load open
        if (!gateOpen || !unsaved) return std::nullopt;
        return intField(v, g, "IsOpen").value_or(0) != 0;
    }
    const std::optional<int32_t> c = intField(v, k, "Condition_value");
    if (!c) return std::nullopt;
    return *c != 0;
}

// A zero-length RealmAnimateBasic reached at its Activate: its realm's start weight is End
inline void noteAnimate(View& v, size_t k, int pin)
{
    const long dur = floatField(v, k, "Duration"), end = floatField(v, k, "End");
    const long name = ::mer::field(v.f, v.nodes[k], v.fl, "Name");
    const bool fields = dur >= 0 && end >= 0 && name >= 0;
    const bool activated = pin == pinOf(v, k, "Activate");
    if (!fields || !activated || floatOf(v.f.recs[dur]) != 0.f) return;
    const std::optional<std::string> realm = ::mer::firstName(v.f, v.f.recs[name]);
    if (!realm) return;
    const float w = floatOf(v.f.recs[end]);
    auto [it, fresh] = v.weights.emplace(text::lower(*realm), w);
    if (!fresh && it->second != w) it->second = std::nullopt;
}

// The start walk from node k entered at pin: through If nodes to RealmAnimateBasic nodes, nothing else
inline void walk(View& v, size_t k, int pin, int depth)
{
    if (v.nodes[k].type == "RealmAnimateBasic") {
        noteAnimate(v, k, pin);
        return;
    }
    const bool follow = v.nodes[k].type == "If" && depth < kMaxDepth;
    if (!follow) return;
    const std::optional<bool> c = startCondition(v, k);
    if (!c) return;
    const int out = pinOf(v, k, *c ? "Then" : "Else");
    for (const Link& L : v.links) {
        const bool next = L.from == k && fromPin(L) == out && (L.code & kKindMask) == kImpulse;
        if (next && L.to < v.nodes.size()) walk(v, L.to, toPin(L), depth + 1);
    }
}

// Each RealmCache whose realm the start walk sets: its StartWeight record rewritten in place (same size)
inline std::vector<Change> applyWeights(View& v)
{
    std::vector<Change> out;
    for (size_t k = 0; k < v.nodes.size(); ++k) {
        if (v.nodes[k].type != "RealmCache") continue;
        const long name = ::mer::field(v.f, v.nodes[k], v.fl, "Name"), sw = floatField(v, k, "StartWeight");
        const std::optional<std::string> realm = name >= 0 ? ::mer::firstName(v.f, v.f.recs[name]) : std::nullopt;
        if (!realm || sw < 0) continue;
        auto it = v.weights.find(text::lower(*realm));
        if (it == v.weights.end() || !it->second) continue;
        oct::Rec& r = v.f.recs[sw];
        const float from = floatOf(r), to = *it->second;
        if (from == to) continue;
        memcpy(&r.pay[r.pay.size() - 4], &to, 4);
        out.push_back({*realm, from, to});
    }
    return out;
}

}  // namespace detail

// Each RealmCache of 1.0 graph f at the weight a Catalyst sets at once; f untouched without changes.
inline std::vector<Change> apply(oct::File& f)
{
    const long pool = f.pool("MeridianNodePool");
    if (pool < 0) return {};
    detail::View v{f, (uint16_t)(f.recs[pool].level + 2), {}, {}, {}};
    if (!::mer::collectNodes(f, (size_t)pool, v.nodes, nullptr)) return {};
    detail::collectLinks(v);
    for (const ::mer::Link& L : v.links) {
        const bool fromCatalyst = L.from < v.nodes.size() && v.nodes[L.from].type == "Catalyst";
        const bool impulse = (L.code & detail::kKindMask) == detail::kImpulse;
        if (fromCatalyst && impulse && L.to < v.nodes.size()) detail::walk(v, L.to, detail::toPin(L), 0);
    }
    // 3.0 FUN_017a35f0 skips an animate whose realm is not loaded yet: start animates can be lost
    return detail::applyWeights(v);
}

}  // namespace di1::realmstart
