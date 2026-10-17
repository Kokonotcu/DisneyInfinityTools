// 1.0 choreographies and clips in 3.0's form (FORMATS.md 4.10). convert/clips, convert/records, ports10; any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../../convert/clips.hpp"
#include "../../convert/records.hpp"
#include "../../formats/oct.hpp"
#include "bent.hpp"
#include "ports10.hpp"

namespace di1::cho {

// ---- clips ----
namespace detail {

inline constexpr uint32_t kCompMask10 = 0xf;   // 1.0 output head: its comp count (FUN_005444c0)

// 1.0's output table at o (FUN_005444c0): u16 nOut, nOut x {u16 head, u16 id, comps}; false if it overruns lim
inline bool table10(const std::string& d, size_t o, size_t lim, std::vector<clips::Output>& outs, size_t* end)
{
    uint32_t n = clips::rd(d, o, 2);
    if (n == 0xffffffffu) return false;
    o += 2;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t head = clips::rd(d, o, 2), id = clips::rd(d, o + 2, 2);
        if (!bytes::fits(lim, o, 4)) return false;
        o += 4;
        clips::Output c{id, {}};
        for (uint32_t k = 0; k < (head & kCompMask10); ++k) {
            const int len = clips::compWords(clips::rd(d, o, 2));
            if (!bytes::fits(lim, o, 2 * (size_t)len)) return false;
            std::vector<uint16_t> w;
            for (int j = 0; j < len; ++j) w.push_back((uint16_t)clips::rd(d, o + 2 * j, 2));
            c.comps.push_back(w);
            o += 2 * len;
        }
        outs.push_back(std::move(c));
    }
    *end = o;
    return true;
}

// The ids through pm (null: kept); false when a 1.0 port lies past the table. kDropPort outputs go.
inline bool renumber(const PortMap* pm, std::vector<clips::Output>& v)
{
    if (!pm) return true;
    for (auto& c : v) {
        if (c.id >= pm->n10) return false;
        c.id = pm->to30[c.id];
    }
    // kDropPort: an output every model skips (comps carry absolute data indices, so the rest read the same)
    v.erase(std::remove_if(v.begin(), v.end(), [](const clips::Output& c) { return c.id == kDropPort; }), v.end());
    return true;
}

}  // namespace detail

// A 1.0 ClipDataBlock payload in 3.0's layout, ids through pm if given; false (out untouched) if it does not parse
inline bool clipBlock30(const std::string& d, std::string& out, const PortMap* pm = nullptr)
{
    auto ids = [pm](std::vector<clips::Output>& v) { return detail::renumber(pm, v); };
    return clips::repack(d, out, detail::table10, ids);
}

// The port map of a motion clip (Type 'Clip') whose LayerBinds all name one ports10.hpp template, else nullptr
inline const PortMap* clipPorts(const oct::File& f, size_t sn)
{
    const PortMap* pm = nullptr;
    bool clip = false, other = false;
    for (size_t j = sn + 1, e = f.blockEnd(sn), lvl4 = 0; j < e; ++j) {
        const oct::Rec& r = f.recs[j];
        if (r.level == 3 && f.name(r) == "Type") clip = f.nameValues(r) == std::vector<std::string>{"Clip"};
        if (r.level == 4) lvl4 = j;
        const bool layerBind = lvl4 && f.name(f.recs[lvl4]) == "LayerBind";
        if (r.level != 5 || f.name(r) != "ConnectionTemplate" || !layerBind) continue;
        auto v = f.nameValues(r);
        const PortMap* m = v.empty() ? nullptr : portMap(v[0].c_str());
        const bool another = pm && m != pm;
        other |= !m || another;
        pm = m;
    }
    return clip && !other ? pm : nullptr;
}

using Motion = std::function<bool(const std::string& connectionTemplate)>;

namespace detail {

// True when SubNetwork i is a motion clip (Type 'Clip' naming a ConnectionTemplate) motion does not take
inline bool skippedMotionClip(const oct::File& f, size_t i, const Motion& motion)
{
    bool clip = false;
    long tmpl = -1;
    for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
        const oct::Rec& q = f.recs[j];
        if (q.level == 3 && f.name(q) == "Type") clip = f.nameValues(q) == std::vector<std::string>{"Clip"};
        else if (q.level == 5 && tmpl < 0 && f.name(q) == "ConnectionTemplate") tmpl = (long)j;
    }
    if (!clip || tmpl < 0) return false;
    auto v = f.nameValues(f.recs[tmpl]);
    return !motion || v.empty() || !motion(v[0]);   // the first ConnectionTemplate, whatever holds it (GR-31)
}

}  // namespace detail

// What clips30 re-packs: motion clips whose template motion() takes (empty: none) and, with portIds, mapped ones
struct ClipSettings {
    Motion motion;
    bool portIds = false;   // clip10ids: motion clips on a ports10.hpp template get 3.0 port ids
};
struct ClipCounts {
    int repacked = 0;
    int failed = 0;       // blocks left as they are
    int renumbered = 0;   // re-packed with 3.0 port ids
};

// clips30 (clip10): every ClipDataBlock in 3.0's layout; scene clips keep their ids (FORMATS.md 4.10)
inline ClipCounts clips30(oct::File& f, const ClipSettings& s)
{
    ClipCounts n;
    bool skip = false;
    const PortMap* pm = nullptr;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level == 2 && f.name(r) == "SubNetwork") {
            pm = s.portIds ? clipPorts(f, i) : nullptr;
            skip = !pm && detail::skippedMotionClip(f, i, s.motion);
            continue;
        }
        if (skip || f.name(r) != "ClipDataBlock") continue;
        oct::Spec sp = oct::spec(r.type);
        if (sp.kind != oct::BYTE) continue;
        std::string out;
        if (!clipBlock30(r.pay.substr(sp.cw), out, pm)) {
            ++n.failed;
            continue;
        }
        r = octx::blobRec(f, r.level, "ClipDataBlock", out);
        ++n.repacked;
        if (pm) ++n.renumbered;
    }
    return n;
}

// links30 (clip10ids): a 1.0 model's DataNodeLinks on a ports10.hpp template in 3.0 port order, for octx::convertLink.
// Returns the links renumbered.
inline int links30(oct::File& f)
{
    int n = 0;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        if (f.recs[i].level != 4 || f.name(f.recs[i]) != "BaseDataNodeLinks") continue;
        const PortMap* pm = nullptr;
        long dnl = -1;
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
            const std::string& nm = f.name(f.recs[j]);
            if (nm == "ConnectionTemplate") {
                auto v = f.nameValues(f.recs[j]);
                pm = v.empty() ? nullptr : portMap(v[0].c_str());
            } else if (nm == "DataNodeLinks") dnl = (long)j;
        }
        if (!pm || dnl < 0) continue;
        std::vector<int32_t> old = f.ints(f.recs[dnl]), by30;
        // a port linked past the table is not this template's numbering; unlinked (-1) ones past it may be
        size_t used = 0;
        for (size_t p = 0; p < old.size(); ++p)
            if (old[p] >= 0) used = p + 1;
        if (used > pm->n10) continue;
        for (size_t p = 0; p < used; ++p) {
            if (old[p] < 0) continue;
            uint16_t q = pm->to30[p];
            if (q == kDropPort) continue;
            if (by30.size() <= q) by30.resize(q + 1, -1);
            by30[q] = old[p];
        }
        f.setInts(f.recs[dnl], by30);
        ++n;
    }
    return n;
}

// ---- time range ----
namespace detail {

// What time30 reads of one SubNetwork, and the play range of its clips and events
struct TimeScan {
    bool has = false;      // it has MinTime / MaxTime already
    bool isClip = false;   // Type 'Clip': a motion clip
    long vgEnd = -1;       // end of its VariantGroupPool
    long lbAt = -1;        // its LayerBindPool
    std::string nb;        // its NetworkBuffer
    std::vector<uint32_t> evOffs;   // NetworkBufferOffset of each EventList DataNode
    float mn = FLT_MAX, mx = FLT_MIN;
    bool any = false;
};
inline void hasTimes(const oct::File&, size_t, TimeScan& s) { s.has = true; }
inline constexpr octx::FieldRule<TimeScan> kTimeLevel3[] = {
    {"MinTime", hasTimes},
    {"MaxTime", hasTimes},
    {"Type",
     [](const oct::File& f, size_t i, TimeScan& s) {
         s.isClip = f.nameValues(f.recs[i]) == std::vector<std::string>{"Clip"};
     }},
    {"VariantGroupPool", [](const oct::File& f, size_t i, TimeScan& s) { s.vgEnd = (long)f.blockEnd(i); }},
    {"LayerBindPool", [](const oct::File&, size_t i, TimeScan& s) { s.lbAt = (long)i; }},
    {"NetworkBuffer",
     [](const oct::File& f, size_t i, TimeScan& s) { s.nb = f.recs[i].pay.substr(oct::spec(f.recs[i].type).cw); }},
};

// DataNode i's NetworkBufferOffset if it is an EventList, else -1
inline long eventListOffset(const oct::File& f, size_t i)
{
    bool ev = false;
    long off = -1;
    for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
        const std::string& k = f.name(f.recs[j]);
        if (k == "Type") ev = f.nameValues(f.recs[j]) == std::vector<std::string>{"EventList"};
        else if (k == "NetworkBufferOffset") off = octx::firstInt(f, f.recs[j], -1);
    }
    return ev ? off : -1;
}

inline TimeScan scanTime(const oct::File& f, size_t sn)
{
    TimeScan s;
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& nm = f.name(r);
        if (r.level == 3) octx::readField(f, i, kTimeLevel3, s);
        else if (nm == "ClipDataBlock") {   // any block of the SubNetwork, BYTE or not
            const auto ts = clips::clipTimes(r.pay.substr(oct::spec(r.type).cw));
            if (!ts) continue;
            s.mn = (std::min)(s.mn, ts->first);
            s.mx = (std::max)(s.mx, ts->second);
            s.any = true;
        } else if (r.level == 4 && nm == "DataNode") {
            const long off = eventListOffset(f, i);
            if (off >= 0) s.evOffs.push_back((uint32_t)off);
        }
    }
    return s;
}

// The MinTime / MaxTime insert of one SubNetwork with clips or events; false if it gets none
inline bool timeSubNetwork(oct::File& f, size_t sn, std::vector<octx::RangeEdit>& edits)
{
    const size_t end = f.blockEnd(sn);
    TimeScan s = scanTime(f, sn);
    if (s.has || s.isClip) return false;   // motion clips: octx::clip30 (addAllCounts)
    auto widen = [&s](float t) {
        s.mn = (std::min)(s.mn, t);
        s.mx = (std::max)(s.mx, t);
        s.any = true;
    };
    for (uint32_t off : s.evOffs) clips::eachEventTime(s.nb, off, widen);
    if (!s.any) return false;
    size_t at = (size_t)(s.vgEnd >= 0 ? s.vgEnd : s.lbAt >= 0 ? s.lbAt : (long)end);   // where 3.0 writes them
    edits.push_back({at, 0, {octx::floatRec(f, 3, "MinTime", s.mn), octx::floatRec(f, 3, "MaxTime", s.mx)}});
    return true;
}

}  // namespace detail

// time30 (chotime10): MinTime / MaxTime on every SubNetwork with clips or events (FORMATS.md 4.10)
inline int time30(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    const std::vector<size_t> sns = octx::subNetworks(f, pool);
    std::vector<octx::RangeEdit> edits;
    int n = 0;
    for (auto it = sns.rbegin(); it != sns.rend(); ++it)   // last first: the order names were added in
        n += detail::timeSubNetwork(f, *it, edits);
    octx::applyEdits(f, edits);
    return n;
}

namespace detail {

// What anim30 reads of one SubNetwork: its level-3 header records and pools, and a NetworkBuffer copy
struct AnimScan {
    long hs = -1, hsi = -1, hl = -1;   // HeaderStrings, HeaderStringIndices, HeaderLayout
    long dp = -1, pp = -1;             // DataNodePool, ProcessorNodePool
    std::string nb;
};
inline constexpr octx::FieldRule<AnimScan> kAnimLevel3[] = {
    {"HeaderStrings", [](const oct::File&, size_t i, AnimScan& s) { s.hs = (long)i; }},
    {"HeaderStringIndices", [](const oct::File&, size_t i, AnimScan& s) { s.hsi = (long)i; }},
    {"HeaderLayout", [](const oct::File&, size_t i, AnimScan& s) { s.hl = (long)i; }},
    {"DataNodePool", [](const oct::File&, size_t i, AnimScan& s) { s.dp = (long)i; }},
    {"ProcessorNodePool", [](const oct::File&, size_t i, AnimScan& s) { s.pp = (long)i; }},
    {"NetworkBuffer",
     [](const oct::File& f, size_t i, AnimScan& s) { s.nb = f.recs[i].pay.substr(oct::spec(f.recs[i].type).cw); }},
};

// The 'Global.Time' string s and its root header h (1-based), when exactly one header names s
struct TimeHeader {
    int32_t s;
    int h;
};
inline std::optional<TimeHeader> timeHeader(const std::vector<std::string>& strs, const std::vector<int32_t>& idx,
                                            const std::vector<int32_t>& lay)
{
    auto at = std::find(strs.begin(), strs.end(), "Global.Time");
    if (at == strs.end() || std::count(strs.begin(), strs.end(), "AnimState")) return std::nullopt;
    const int32_t s = (int32_t)(at - strs.begin());
    if (std::count(idx.begin(), idx.end(), s) != 1) return std::nullopt;   // the string names another header too
    // HeaderLayout: (parent, child count) pairs; the children take the next indices, numbered from 1 (0 = root)
    int h = -1;
    size_t k = 0;
    for (size_t p = 0; p + 1 < lay.size(); p += 2)
        for (int32_t c = 0; c < lay[p + 1] && k < idx.size(); ++c, ++k)
            if (lay[p] == 0 && idx[k] == s) h = (int)k + 1;
    if (h < 0) return std::nullopt;
    return TimeHeader{s, h};
}

// The play range of the clips and events one DataNode drives
struct TimeRange {
    bool any = false;
    float t0 = 0, t1 = 0;
    void add(float t)
    {
        t0 = any ? (std::min)(t0, t) : t;
        t1 = any ? (std::max)(t1, t) : t;
        any = true;
    }
};

// Value of the processor attribute a in DataNodeRefs, -1 if it has none
inline int attributeRef(const std::vector<std::string>& attrs, const std::vector<int32_t>& refs, const char* a)
{
    for (size_t q = 0; q < attrs.size() && q < refs.size(); ++q)
        if (attrs[q] == a) return refs[q];
    return -1;
}

// The times in DataNode src that a processor of type ty reads (ClipDataBlock or EventList), into r
inline void processorTimes(const oct::File& f, const AnimScan& s, size_t src, const std::string& ty, TimeRange& r)
{
    if (ty == "ClipProcessor") {
        long b = octx::child(f, src, "ClipDataBlock");
        if (b < 0 || oct::spec(f.recs[b].type).kind != oct::BYTE) return;
        const auto ts = clips::clipTimes(f.recs[b].pay.substr(oct::spec(f.recs[b].type).cw));
        if (!ts) return;
        r.add(ts->first);
        r.add(ts->second);
        return;
    }
    long o = octx::child(f, src, "NetworkBufferOffset");
    long at = o < 0 ? -1 : octx::firstInt(f, f.recs[o], -1);
    if (at >= 0) clips::eachEventTime(s.nb, (uint32_t)at, [&](float t) { r.add(t); });   // as time30's off >= 0
}

// The time range of the ClipProcessors / EventLists whose Input is DataNode d (FORMATS.md 4.10)
inline TimeRange timeRange(const oct::File& f, const AnimScan& s, const std::vector<size_t>& dns, int d)
{
    TimeRange r;
    for (size_t i = s.pp + 1, e = f.blockEnd(s.pp); i < e; ++i) {
        if (f.recs[i].level != 4) continue;
        std::string ty = octx::name1(f, octx::child(f, i, "Type"));
        long ar = octx::child(f, i, "DataNodeAttributes"), rr = octx::child(f, i, "DataNodeRefs");
        const bool reader = ty == "ClipProcessor" || ty == "EventList";
        if (!reader || ar < 0 || rr < 0) continue;
        auto attrs = f.nameValues(f.recs[ar]);
        auto refs = f.ints(f.recs[rr]);
        if (attributeRef(attrs, refs, "Input") != d) continue;
        int src = attributeRef(attrs, refs, ty == "ClipProcessor" ? "ClipData" : "EventListData");
        if (src < 0 || (size_t)src >= dns.size()) continue;
        processorTimes(f, s, dns[src], ty, r);
    }
    return r;
}

// DataNode d's new children if it is the Float 'Global.Time' node: AnimState with its range; else empty
inline std::vector<oct::Rec> animNode(oct::File& f, const AnimScan& s, const std::vector<size_t>& dns, size_t d, int h)
{
    long hd = octx::child(f, dns[d], "Header"), ty = octx::child(f, dns[d], "Type");
    if (hd < 0 || ty < 0) return {};
    if (octx::firstInt(f, f.recs[hd], -1) != h || octx::name1(f, ty) != "Float") return {};
    const TimeRange r = timeRange(f, s, dns, (int)d);
    if (!r.any) return {};
    return {f.recs[hd], f.nameScalar(5, f.recs[ty].name, "AnimState"), f.node(5, "Data"),
            octx::floatRec(f, 6, "RangeMin", r.t0), octx::floatRec(f, 6, "RangeMax", r.t1)};
}

// HeaderStrings element s set to name index a in place (a non-keyed NAME list of u16, GR-34)
inline void setHeaderString(oct::File& f, long hs, int32_t s, uint16_t a)
{
    const oct::Spec ss = oct::spec(f.recs[hs].type);
    memcpy(&f.recs[hs].pay[ss.cw + 2 * (size_t)s], &a, 2);
}

// One SubNetwork's time input in 3.0's form, its DataNode edits into edits; the DataNodes converted
inline int animSubNetwork(oct::File& f, size_t sn, std::vector<octx::RangeEdit>& edits)
{
    AnimScan s;
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i)
        if (f.recs[i].level == 3) octx::readField(f, i, kAnimLevel3, s);
    const bool headers = s.hs >= 0 && s.hsi >= 0 && s.hl >= 0;
    if (!headers || s.dp < 0 || s.pp < 0) return 0;
    const oct::Spec ss = oct::spec(f.recs[s.hs].type);
    if (ss.kind != oct::NAME || ss.keyed) return 0;
    const std::optional<TimeHeader> th =
        timeHeader(f.nameValues(f.recs[s.hs]), f.ints(f.recs[s.hsi]), f.ints(f.recs[s.hl]));
    if (!th) return 0;
    std::vector<size_t> dns;
    for (size_t i = s.dp + 1, e = f.blockEnd(s.dp); i < e; ++i)
        if (f.recs[i].level == 4) dns.push_back(i);
    std::vector<octx::RangeEdit> mine;   // a DataNode's children replaced; all built before any edit
    for (size_t d = 0; d < dns.size(); ++d) {
        std::vector<oct::Rec> rep = animNode(f, s, dns, d, th->h);
        if (!rep.empty()) mine.push_back({dns[d] + 1, f.blockEnd(dns[d]) - dns[d] - 1, std::move(rep)});
    }
    if (mine.empty()) return 0;
    setHeaderString(f, s.hs, th->s, f.addName("AnimState"));   // in place: no record moves (GR-N1)
    for (octx::RangeEdit& e : mine) edits.push_back(std::move(e));
    return (int)mine.size();
}

}  // namespace detail

// anim30 (choanim10): 'Global.Time' Float inputs become AnimState (FORMATS.md 4.10). Returns the DataNodes converted;
// *subnets = SubNetworks touched.
inline int anim30(oct::File& f, int* subnets = nullptr)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    const std::vector<size_t> sns = octx::subNetworks(f, pool);
    std::vector<octx::RangeEdit> edits;
    int n = 0, touched = 0;
    for (auto it = sns.rbegin(); it != sns.rend(); ++it) {   // last first: the order names were added in
        const int here = detail::animSubNetwork(f, *it, edits);
        n += here;
        touched += here > 0;
    }
    octx::applyEdits(f, edits);
    if (subnets) *subnets = touched;
    return n;
}

// ---- placement scene nodes ----
namespace detail {

// An Association as scene30 reads it
struct SceneScan {
    std::string name, type;
    long typeAt = -1;
    int scene = -2;   // -2: no SceneTreeNodeRef record
};

// One level-3 field of the Association being read into a
inline void sceneField(const oct::File& f, size_t i, SceneScan& a)
{
    const oct::Rec& r = f.recs[i];
    const std::string& k = f.name(r);
    if (k == "SceneTreeNodeRef") {
        a.scene = octx::firstInt(f, r, -1);
        return;
    }
    if (k != "NodeName" && k != "Type") return;
    auto v = f.nameValues(r);
    if (v.empty()) return;
    if (k == "NodeName") {
        a.name = v[0];
        return;
    }
    a.type = v[0];
    a.typeAt = (long)i;
}

}  // namespace detail

// scene30 (choscene10): SceneTreeNodeRef on PlacementNode Associations (FORMATS.md 4.10). Returns those changed.
inline int scene30(oct::File& f)
{
    long pool = f.pool("AssociationPool");
    if (pool < 0) return 0;
    std::vector<detail::SceneScan> as;
    for (size_t i = pool + 1, e = f.blockEnd(pool); i < e; ++i) {
        if (f.recs[i].level == 2) as.push_back({});
        else if (f.recs[i].level == 3 && !as.empty()) detail::sceneField(f, i, as.back());
    }
    std::map<std::string, int> tr;   // 'Transform' Association NodeName -> its scene node (first wins)
    for (auto& a : as)
        if (a.type == "Transform" && a.scene != -2) tr.emplace(a.name, a.scene);
    std::vector<octx::RangeEdit> edits;
    int n = 0;
    for (auto it = as.rbegin(); it != as.rend(); ++it) {   // last first: the order names were added in
        if (it->type != "PlacementNode" || it->scene != -2) continue;
        size_t bar = it->name.rfind('|');
        auto t = bar == std::string::npos ? tr.end() : tr.find(it->name.substr(0, bar));
        const int scene = t == tr.end() ? -1 : t->second;
        edits.push_back({(size_t)it->typeAt + 1, 0, {f.intScalar(3, "SceneTreeNodeRef", scene)}});
        ++n;
    }
    octx::applyEdits(f, edits);
    return n;
}

// banm30 (banm10): 1.0f .banm as 1.0g with full track paths; path: its engine path. False (f unchanged) if not 1.0f
inline bool banm30(oct::File& f, const std::string& path)
{
    long ver = -1;
    for (size_t i = 0; i < f.recs.size(); ++i)
        if (f.recs[i].level == 1 && f.name(f.recs[i]) == "Version") ver = (long)i;
    if (ver < 0 || f.nameValues(f.recs[ver]) != std::vector<std::string>{"1.0f"}) return false;
    f.recs[ver] = f.nameScalar(1, f.recs[ver].name, "1.0g");
    size_t cut = path.find_last_of('/');
    std::string dir = cut == std::string::npos ? "" : path.substr(0, cut);
    for (size_t i = 1; i < f.recs.size(); ++i) {
        oct::Rec& r = f.recs[i];
        if (f.name(r) != "Filename" || oct::spec(r.type).kind != oct::NAME) continue;
        auto v = f.nameValues(r);
        if (!v.empty()) r = f.nameScalar(r.level, r.name, bent::resolve(dir, v[0]));
    }
    return true;
}

// choalias10: Cho entries the 1.0 DataMap keeps outside choreographies/<name>/<name> (FORMATS.md 4.10). Any thread.
struct Aliases {
    std::mutex m;
    std::map<std::string, std::string> map;   // 'choreographies/<n>/<n>' -> the 1.0 path; guarded by m

    // entry: "Cho|<path>[|...]", as the 1.0 DataMap has it
    void add(const std::string& entry)
    {
        if (entry.compare(0, 4, "Cho|")) return;
        size_t b = entry.find('|', 4);
        std::string p = entry.substr(4, b == std::string::npos ? std::string::npos : b - 4);
        for (char& c : p) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
        size_t s = p.rfind('/');
        if (s == std::string::npos) return;
        std::string n = p.substr(s + 1), key = "choreographies/" + n + "/" + n;
        if (key == p) return;
        std::lock_guard<std::mutex> l(m);
        map.emplace(key, p);
    }
    // 'choreographies/<n>/<n><rest>' (normalised) -> '<1.0 path><rest>', "" if none
    std::string resolve(const std::string& p)
    {
        size_t a = p.find('/'), b = a == std::string::npos ? a : p.find('/', a + 1);
        if (b == std::string::npos) return "";
        std::string key = p.substr(0, b) + "/" + p.substr(a + 1, b - a - 1);
        const bool prefixed = p.compare(0, key.size(), key) == 0;
        const bool longer = p.size() > key.size();
        const bool joined = longer && p[key.size()] != '.' && p[key.size()] != '_';   // <n>.ext and <n>_x count
        if (!prefixed || joined) return "";
        std::lock_guard<std::mutex> l(m);
        auto it = map.find(key);
        return it == map.end() ? "" : it->second + p.substr(key.size());
    }
};

}  // namespace di1::cho
