// Choreographies, scene clips and motion clips of 1.0 .oct files in the form the 3.0 runtime reads (plan CHO-1, NPC-6).
// Three load-time transforms and a path alias (Aliases, below), each gated by its own cfg key in di1compat.cpp:
//
// clips30 (clip10): ClipDataBlock output tables re-packed. Header and data sections are one format in both games:
//   f32 MinTime, f32 MaxTime, u16 nDesc, u16 nSec, u32 constOff, u32 descOff[nDesc], u32 secOff[nSec] (3.0 FUN_00543ca0
//   and the sampler FUN_0052b720 = 1.0 FUN_0053f7c0, line for line). A descriptor is u16, u16 n, u16 secIds[n], u16 X,
//   the sampled output table, and at desc + X the constant one. Only those two tables changed:
//     1.0 FUN_005444c0 (via FUN_00554610 / FUN_00562e40): u16 nOut, nOut x {u16 head (ncomp = head & 0xf), u16 id, comps}
//     3.0 FUN_005329b0 / FUN_00532050 (via FUN_00543f40 / FUN_00552fc0): u16 nOut, u16 heads[nOut] = ncomp << 13 | id,
//       then the comps of every output in order
//   A comp is a u16 code plus 1 u16, or 3 when code & 0xf000 is 0x1000 / 0x2000 (FUN_0051b230, FUN_005329b0); twins
//   carry the same codes. The area is padded with 0xee to 4 and every offset past it moves by the size change.
//   Measured (clipinv.py): 4,314/4,314 1.0 choreography + exploder blocks parse with the sampled table ending exactly
//   at X and only 0xee after the constant table; on the 7,621 motion-clip twins the result has the 3.0 twin's length
//   in 5,850 and equals it byte for byte in 70 (+136 when ids are renumbered, below).
//   Ids: in scene clips (no LayerBind) an id is the ClipProcessor output index in both games (1.0 FUN_005444c0 with no
//   table at +0x30, 3.0 FUN_00532050) and is kept. In motion clips it indexes the binding's ConnectionTemplate,
//   named by LayerBind.ConnectionTemplate ('characters/biped/biped.ct'). 3.0 numbers the templates it has itself
//   differently (twins, idrule.py: ids kept 105+55, 2i-2 273, other 4,161, e.g. _townspeople 29->56, 31->60,
//   34->63), and never requested such a template's .bct (in-game 2026-10-13: 23 1.0 biped clips served, no
//   biped.bct); for templates whose owner 3.0 lacks it loads the 1.0 .bct (7/7 requested: kiosk, delivery_boxes,
//   inc_omnisky_spawner, 4 choreography placement.bct). So a motion clip is re-packed with its ids kept only when the
//   template's owner folder is absent from the 3.0 install (di1compat.cpp); the others stay raw until an id map
//   exists (NPC-6). A 1.0 output with more than 7 comps (3.0 has 3 bits; 4 such in the
//   motion corpus, none in choreographies) is split into outputs with the same id: FUN_005329b0 reads, applies and
//   writes back per output, so the split applies the same comps.
//
// time30 (chotime10): MinTime / MaxTime of every SubNetwork with clips or events. 3.0 takes a SubNetwork's time range
//   only from these records (FUN_00500ac0 -> FUN_004b0f70, absent = FLT_MAX..-FLT_MAX = invalid); it reaches the asset
//   (FUN_0076d4e0 / FUN_00788280 -> FUN_0076c090) and the playback clock's play range (FUN_00774360 -> FUN_00525830,
//   which leaves the range unset while it is invalid). 1.0 has no such records and computes the range at runtime
//   (FUN_004ebc90). Rule, exact on every SubNetwork of every shipped 3.0 choreography (8,612/8,612) and exploder
//   (2,947/2,947; timefit2.py): present iff the SubNetwork has a ClipDataBlock or a static EventList event;
//   MinTime = min(block f32[0], event times), MaxTime = max(FLT_MIN, block f32[1], event times). EventList data sits
//   in the SubNetwork's NetworkBuffer at the node's NetworkBufferOffset: u32 n, n x (f32 time, f32 value), the same
//   bytes in both games (bld_damage_explosion twin). Placed after VariantGroupPool, as 3.0 writes them.
//
// layout30 (cholayout10): 3.0's layout reader (FUN_00789e80) has no ContextNodePool; actors and clips of a
//   choreography are InstanceNodes (FUN_00787e30: Flags, Name, PlacementNodeRef, if 8 FilePath, if 0x20
//   BoundingSceneNodeName) bound by Attachments with Base/LayerInstanceRef (FUN_00781410, applied in FUN_00788280).
//   1.0's ContextNodes (1.0 FUN_0075d630: flag 1 PlacementNodeRef, 2 FilePath, 0x10 BoundingSceneNodeName) are
//   ignored, so no 1.0 choreography actor or clip was ever instanced (in-game log 2026-10-13: _gtut_inc_ledgehang's
//   motions/inc_bob_clip.oct never requested). Rules, on the 611 choreography twins with a LayoutPool (layout30.py):
//   attachments 611/611, instances 610/611, placements 594/611 (the rest: 3.0 also pruned unreferenced placements,
//   kept here so the files' PlacementNode associations keep their indices). Corpus-wide (1.0 choreographies +
//   exploders, 2,589 layouts) the entries hold only the fields mapped here, all name (0x0b) or int (0x1b) scalars.
//   An empty ContextNodePool (in_city.oct, buildingrepair*) becomes an empty InstanceNodePool: the 3.0 pool
//   resize (FUN_00786c30, like the AttachmentPool's FUN_0077e810 1.0 files already pass through) takes 0.
//     ContextNode -> InstanceNode if it has a FilePath, or flag 8 and an Attachment uses it; others (anchors, flags
//       4 / 5) only name placements. Fields Name, PlacementNodeRef, [BoundingSceneNodeName], FilePath (lowercase),
//       Flags: FilePath 8 (| 0x10 if 1.0 flag 4), 1.0 0x10 -> 0x20, 1.0 8 -> 0x40.
//     PlacementNode: ContextNodeRef dropped, Name = that context's Name; Flags: low nibble kept, 0x10 -> 0x200,
//       0x20..0x100 -> >> 1 (both readers: per-axis bits), context flag 1 -> 0x400, name 'Layout' -> 0x100.
//     Attachment: Base/LayerContextRef -> Base/LayerInstanceRef through the kept-context index map.
#pragma once
#include <algorithm>
#include <cfloat>
#include <functional>
#include <iterator>
#include <mutex>
#include <map>
#include <set>
#include "octx.hpp"
#include "bent.hpp"

namespace cho {

inline uint32_t rd(const std::string& s, size_t o, int w)
{
    return o + w <= s.size() ? oct::u((const uint8_t*)s.data() + o, w) : 0xffffffffu;
}
inline void wr(std::string& s, uint32_t v, int w) { oct::File::put(s, v, w); }
// first int of a record, def if it has none (a node or an empty list)
inline int first(const oct::File& f, const oct::Rec& r, int def)
{
    auto v = f.ints(r);
    return v.empty() ? def : v[0];
}

// ---- clips ----
struct ClipOut { uint32_t id; std::vector<std::vector<uint16_t>> comps; };

// A 1.0 output table at o; end (exclusive) in *end. False if it overruns lim.
inline bool table10(const std::string& d, size_t o, size_t lim, std::vector<ClipOut>& outs, size_t* end)
{
    uint32_t n = rd(d, o, 2);
    if (n == 0xffffffffu) return false;
    o += 2;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t head = rd(d, o, 2), id = rd(d, o + 2, 2);
        if (o + 4 > lim) return false;
        o += 4;
        ClipOut c{id, {}};
        for (uint32_t k = 0; k < (head & 0xf); ++k) {
            uint32_t code = rd(d, o, 2);
            int len = (code & 0xf000) == 0x1000 || (code & 0xf000) == 0x2000 ? 4 : 2;
            if (o + 2 * len > lim) return false;
            std::vector<uint16_t> w;
            for (int j = 0; j < len; ++j) w.push_back((uint16_t)rd(d, o + 2 * j, 2));
            c.comps.push_back(w);
            o += 2 * len;
        }
        outs.push_back(std::move(c));
    }
    *end = o;
    return true;
}

inline bool table30(const std::vector<ClipOut>& outs, std::string& t)
{
    std::vector<std::pair<uint32_t, size_t>> heads;   // (id, comp count) after splitting at 7
    for (auto& c : outs) {
        if (c.id > 0x1fff) return false;
        size_t k = 0;
        do { heads.push_back({c.id, (std::min<size_t>)(7, c.comps.size() - k)}); k += 7; } while (k < c.comps.size());
    }
    wr(t, (uint32_t)heads.size(), 2);
    for (auto& h : heads) wr(t, (uint32_t)(h.second << 13 | h.first), 2);
    for (auto& c : outs)
        for (auto& w : c.comps)
            for (uint16_t x : w) wr(t, x, 2);
    return true;
}

// One 1.0 ClipDataBlock (payload without its count) -> 3.0 layout. False (out untouched) if it does not parse.
inline bool clipBlock30(const std::string& d, std::string& out)
{
    if (d.size() < 0x14) return false;
    uint32_t nDesc = rd(d, 8, 2), nSec = rd(d, 10, 2), constOff = rd(d, 12, 4);
    size_t hdrEnd = 0x10 + 4 * (size_t)(nDesc + nSec);
    if (!nDesc || hdrEnd > d.size()) return false;
    std::vector<uint32_t> desc, sec;
    for (uint32_t i = 0; i < nDesc; ++i) desc.push_back(rd(d, 0x10 + 4 * i, 4));
    for (uint32_t i = 0; i < nSec; ++i) sec.push_back(rd(d, 0x10 + 4 * (nDesc + i), 4));
    std::set<uint32_t> starts(desc.begin(), desc.end());
    if (*starts.begin() != hdrEnd) return false;
    uint32_t last = *starts.rbegin(), areaEnd = (uint32_t)d.size();
    for (uint32_t e : sec) if (e > last && e < areaEnd) areaEnd = e;
    if (constOff > last && constOff < areaEnd) areaEnd = constOff;
    // an offset into the middle of the re-packed area would go stale
    auto inside = [&](uint32_t o) { return o >= hdrEnd && o < areaEnd && !starts.count(o); };
    if (inside(constOff) || std::any_of(sec.begin(), sec.end(), inside)) return false;
    std::string area;
    std::map<uint32_t, uint32_t> moved;
    for (auto it = starts.begin(); it != starts.end(); ++it) {
        uint32_t s = *it, e = std::next(it) == starts.end() ? areaEnd : *std::next(it);
        uint32_t n = rd(d, s + 2, 2), x = rd(d, s + 4 + 2 * (size_t)n, 2);
        if (n == 0xffffffffu || x == 0xffffffffu || s + x > e) return false;
        std::vector<ClipOut> samp, cnst;
        size_t o1, o2;
        if (!table10(d, s + 6 + 2 * (size_t)n, e, samp, &o1) || o1 != s + x) return false;
        if (!table10(d, s + x, e, cnst, &o2)) return false;
        for (size_t p = o2; p < e; ++p) if ((uint8_t)d[p] != 0xee) return false;   // only padding may follow
        std::string st, ct;
        if (!table30(samp, st) || !table30(cnst, ct)) return false;
        std::string t = d.substr(s, 4 + 2 * (size_t)n);
        wr(t, (uint32_t)(t.size() + 2 + st.size()), 2);
        t += st + ct;
        while (t.size() % 4) t.push_back((char)0xee);
        moved[s] = (uint32_t)(hdrEnd + area.size());
        area += t;
    }
    int64_t delta = (int64_t)(hdrEnd + area.size()) - areaEnd;
    auto fix = [&](uint32_t o) { auto m = moved.find(o); return m != moved.end() ? m->second : o >= areaEnd ? (uint32_t)(o + delta) : o; };
    std::string r = d.substr(0, 12);
    wr(r, fix(constOff), 4);
    for (uint32_t o : desc) wr(r, fix(o), 4);
    for (uint32_t o : sec) wr(r, fix(o), 4);
    out = r + area + d.substr(areaEnd);
    return true;
}

// Every ClipDataBlock of the file. A SubNetwork with a LayerBind (a motion clip) is re-packed only if
// motion(ConnectionTemplate) says its ids index a template with 1.0 numbering (null: never). Returns the blocks
// re-packed; *failed counts blocks left as they are.
inline int clips30(oct::File& f, const std::function<bool(const std::string&)>& motion, int* failed = nullptr)
{
    int n = 0;
    bool skip = false;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level == 2 && f.name(r) == "SubNetwork") {
            skip = false;
            for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j)
                if (f.recs[j].level == 5 && f.name(f.recs[j]) == "ConnectionTemplate") {
                    auto v = f.nameValues(f.recs[j]);
                    skip = !motion || v.empty() || !motion(v[0]);
                    break;
                }
            continue;
        }
        if (skip || f.name(r) != "ClipDataBlock") continue;
        oct::Spec s = oct::spec(r.type);
        if (s.kind != oct::BYTE) continue;
        std::string out;
        if (!clipBlock30(r.pay.substr(s.cw), out)) { if (failed) ++*failed; continue; }
        r = octx::blobRec(f, r.level, "ClipDataBlock", out);
        ++n;
    }
    return n;
}

// ---- time range ----
inline oct::Rec floatRec(oct::File& f, const char* name, float v)
{
    oct::Rec r{3, (uint16_t)(oct::SCALAR | oct::FLOAT << 3), f.addName(name), std::string(4, '\0')};
    memcpy(&r.pay[0], &v, 4);
    return r;
}

inline int time30(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    std::vector<size_t> sns;
    for (size_t i = pool + 1, e = f.blockEnd(pool); i < e; ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") sns.push_back(i);
    int n = 0;
    for (auto it = sns.rbegin(); it != sns.rend(); ++it) {   // last first: insertions keep earlier indices
        size_t sn = *it, end = f.blockEnd(sn);
        bool has = false, any = false, isClip = false;
        long vgEnd = -1, lbAt = -1;
        std::string nb;
        std::vector<uint32_t> evOffs;
        float mn = FLT_MAX, mx = FLT_MIN;
        for (size_t i = sn + 1; i < end; ++i) {
            const oct::Rec& r = f.recs[i];
            const std::string& nm = f.name(r);
            if (r.level == 3) {
                if (nm == "MinTime" || nm == "MaxTime") has = true;
                else if (nm == "Type") isClip = f.nameValues(r) == std::vector<std::string>{"Clip"};
                else if (nm == "VariantGroupPool") vgEnd = (long)f.blockEnd(i);
                else if (nm == "LayerBindPool") lbAt = (long)i;
                else if (nm == "NetworkBuffer") nb = r.pay.substr(oct::spec(r.type).cw);
            } else if (nm == "ClipDataBlock") {
                std::string b = r.pay.substr(oct::spec(r.type).cw);
                if (b.size() < 8) continue;
                float a, z;
                memcpy(&a, &b[0], 4);
                memcpy(&z, &b[4], 4);
                mn = (std::min)(mn, a);
                mx = (std::max)(mx, z);
                any = true;
            } else if (r.level == 4 && nm == "DataNode") {
                bool ev = false;
                long off = -1;
                for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
                    const std::string& k = f.name(f.recs[j]);
                    if (k == "Type") ev = f.nameValues(f.recs[j]) == std::vector<std::string>{"EventList"};
                    else if (k == "NetworkBufferOffset") off = first(f, f.recs[j], -1);
                }
                if (ev && off >= 0) evOffs.push_back((uint32_t)off);
            }
        }
        if (has || isClip) continue;   // motion clips: octx::clip30 (addAllCounts)
        for (uint32_t off : evOffs) {
            uint32_t cnt = rd(nb, off, 4);
            for (uint32_t k = 0; cnt != 0xffffffffu && k < cnt && off + 8 + 8 * (size_t)k <= nb.size(); ++k) {
                float t;
                memcpy(&t, &nb[off + 4 + 8 * k], 4);
                mn = (std::min)(mn, t);
                mx = (std::max)(mx, t);
                any = true;
            }
        }
        if (!any) continue;
        size_t at = (size_t)(vgEnd >= 0 ? vgEnd : lbAt >= 0 ? lbAt : (long)end);
        std::vector<oct::Rec> t = {floatRec(f, "MinTime", mn), floatRec(f, "MaxTime", mx)};
        f.recs.insert(f.recs.begin() + at, t.begin(), t.end());
        ++n;
    }
    return n;
}

// ---- blended animations (.banm, plan NPC-5) ----
// 3.0's loader FUN_0076f810 parses the tuple (FUN_0182b760, binary or text) and drops it unless Version is "1.0g"
// (FUN_0076f480 -> FUN_015e89d0 = 0), so every 1.0 .banm ('1.0f', binary) was discarded. On the 1,249 twins only two
// things differ: the Version, and ReferencedTrack Filenames, relative to the .banm's folder in 1.0
// ('Blk_BM_HeadTurns_R.oct') and full lowercase paths in 3.0 ('characters/_townspeople/motions/blk_bm_headturns_r.oct').
// path: the engine path of the .banm. False (f unchanged) if it is not 1.0f.
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

// ---- Cho paths ----
// 3.0 builds a Cho entry's path as choreographies/<name>/<name> (FUN_0070c090) and datamap.hpp keeps only <name> of
// the 1.0 entry's full path, which 1.0 opened as given (FUN_006ec410). 8 of IncrediblesDataMap's 486 Cho entries
// live elsewhere: choreographies/vaulta/bolt_inc{bob,dash,helen,syndrome,violet}, vaulta/bolt_spare, vaultb/door,
// characters/avatar_tools/pnf_baseball_physics (chomap.py; in-game 2026-10-13 bolt_inc*.oct and door.oct MISSING).
// add() records 'choreographies/<name>/<name>' -> that 1.0 path for them; resolve() maps a request under it.
struct Aliases {
    std::mutex m;
    std::map<std::string, std::string> map;

    void add(const std::string& entry)   // "Cho|<path>[|...]", as the 1.0 DataMap has it
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
        if (p.compare(0, key.size(), key) || (p.size() > key.size() && p[key.size()] != '.' && p[key.size()] != '_')) return "";
        std::lock_guard<std::mutex> l(m);
        auto it = map.find(key);
        return it == map.end() ? "" : it->second + p.substr(key.size());
    }
};

// ---- layout ----
inline oct::Rec keyed(oct::File& f, uint16_t level, const std::string& n, int key)
{
    oct::Rec r{level, (uint16_t)(oct::NODE | 4), f.addName(n), {}};
    wr(r.pay, f.addName(std::to_string(key)), 2);
    return r;
}

// Returns the Layouts converted (0: no ContextNodePool); *instances = InstanceNodes written.
inline int layout30(oct::File& f, int* instances = nullptr)
{
    long lp = f.pool("LayoutPool");
    if (lp < 0) return 0;
    int converted = 0;
    std::vector<size_t> layouts;
    for (size_t i = lp + 1, e = f.blockEnd(lp); i < e; ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "Layout") layouts.push_back(i);
    int written = 0;
    for (auto lt = layouts.rbegin(); lt != layouts.rend(); ++lt) {
        size_t L = *lt;
        long pp = -1, cp = -1, ap = -1;
        for (size_t i = L + 1, e = f.blockEnd(L); i < e; ++i) {
            if (f.recs[i].level != 3) continue;
            const std::string& n = f.name(f.recs[i]);
            if (n == "PlacementNodePool") pp = (long)i;
            else if (n == "ContextNodePool") cp = (long)i;
            else if (n == "AttachmentPool") ap = (long)i;
        }
        if (cp < 0) continue;
        // children of an entry by name (level 5 record index), -1 if absent
        auto kids = [&](size_t e) {
            std::map<std::string, long> m;
            for (size_t j = e + 1, x = f.blockEnd(e); j < x; ++j)
                if (f.recs[j].level == f.recs[e].level + 1) m[f.name(f.recs[j])] = (long)j;
            return m;
        };
        auto entries = [&](long pool) {
            std::vector<std::map<std::string, long>> v;
            if (pool < 0) return v;
            for (size_t j = pool + 1, x = f.blockEnd(pool); j < x; ++j)
                if (f.recs[j].level == 4) v.push_back(kids(j));
            return v;
        };
        auto ival = [&](const std::map<std::string, long>& m, const char* k, int def) {
            auto it = m.find(k);
            return it == m.end() ? def : first(f, f.recs[it->second], def);
        };
        auto ctx = entries(cp), pls = entries(pp), atts = entries(ap);
        std::set<int> used;
        for (auto& a : atts) { used.insert(ival(a, "BaseContextRef", -1)); used.insert(ival(a, "LayerContextRef", -1)); }
        std::map<int, int> remap;
        for (size_t i = 0; i < ctx.size(); ++i) {
            int fl = ival(ctx[i], "Flags", 0);
            if (ctx[i].count("FilePath") && f.nameValues(f.recs[ctx[i]["FilePath"]]).empty()) ctx[i].erase("FilePath");   // not a name
            if (ctx[i].count("FilePath") || ((fl & 8) && used.count((int)i))) remap[(int)i] = (int)remap.size();
        }
        auto copyAt = [&](long rec, uint16_t level) { oct::Rec r = f.recs[rec]; r.level = level; return r; };

        std::vector<oct::Rec> att;   // AttachmentPool
        if (ap >= 0) {
            att.push_back(f.recs[ap]);
            size_t k = 0;
            for (size_t j = ap + 1, x = f.blockEnd(ap); j < x; ++j) {
                if (f.recs[j].level != 4) continue;
                auto& a = atts[k++];
                att.push_back(f.recs[j]);
                auto ref = [&](const char* k10) { auto m = remap.find(ival(a, k10, -1)); return m == remap.end() ? -1 : m->second; };
                att.push_back(f.intScalar(5, "BaseInstanceRef", ref("BaseContextRef")));
                if (a.count("BaseConnectionMap")) att.push_back(copyAt(a["BaseConnectionMap"], 5));
                att.push_back(f.intScalar(5, "LayerInstanceRef", ref("LayerContextRef")));
                if (a.count("LayerConnectionMap")) att.push_back(copyAt(a["LayerConnectionMap"], 5));
                if (a.count("AnimLayerName")) att.push_back(copyAt(a["AnimLayerName"], 5));
            }
        }
        std::vector<oct::Rec> inst = {f.node(3, "InstanceNodePool")};
        for (auto& [old, k] : remap) {
            auto& c = ctx[old];
            int fl = ival(c, "Flags", 0), nf = 0;
            inst.push_back(keyed(f, 4, "InstanceNode", k));
            if (c.count("Name")) inst.push_back(copyAt(c["Name"], 5));
            if (c.count("PlacementNodeRef")) inst.push_back(f.intScalar(5, "PlacementNodeRef", ival(c, "PlacementNodeRef", -1)));
            if (c.count("BoundingSceneNodeName")) inst.push_back(copyAt(c["BoundingSceneNodeName"], 5));
            if (c.count("FilePath")) {
                std::string p = f.nameValues(f.recs[c["FilePath"]])[0];
                for (char& ch : p) ch = (char)tolower((unsigned char)ch);
                inst.push_back(f.nameScalar(5, f.recs[c["FilePath"]].name, p));
                nf |= 8 | (fl & 4 ? 0x10 : 0);
            }
            if (fl & 0x10) nf |= 0x20;
            if (fl & 8) nf |= 0x40;
            inst.push_back(f.intScalar(5, "Flags", nf));
        }
        std::vector<oct::Rec> pl;
        if (pp >= 0) {
            pl.push_back(f.recs[pp]);
            size_t k = 0;
            for (size_t j = pp + 1, x = f.blockEnd(pp); j < x; ++j) {
                if (f.recs[j].level != 4) continue;
                auto& p = pls[k++];
                int fl = ival(p, "Flags", 0), nf = (fl & 0xf) | (fl & 0x10 ? 0x200 : 0) | (fl & 0x1e0) >> 1;
                long nameRec = p.count("Name") ? p["Name"] : -1;
                int cref = ival(p, "ContextNodeRef", -1);
                if (cref >= 0 && cref < (int)ctx.size()) {
                    if (ctx[cref].count("Name")) nameRec = ctx[cref]["Name"];
                    if (ival(ctx[cref], "Flags", 0) & 1) nf |= 0x400;
                }
                pl.push_back(f.recs[j]);
                if (nameRec >= 0) {
                    pl.push_back(copyAt(nameRec, 5));
                    if (f.nameValues(f.recs[nameRec]) == std::vector<std::string>{"Layout"}) nf |= 0x100;
                }
                pl.push_back(f.intScalar(5, "Flags", nf));
            }
        }
        // replace the pools, later ones first
        std::vector<std::pair<long, std::vector<oct::Rec>*>> rep = {{pp, &pl}, {cp, &inst}, {ap, &att}};
        std::sort(rep.begin(), rep.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& [at, recs] : rep) {
            if (at < 0) continue;
            size_t e = f.blockEnd(at);
            f.recs.erase(f.recs.begin() + at, f.recs.begin() + e);
            f.recs.insert(f.recs.begin() + at, recs->begin(), recs->end());
        }
        written += (int)remap.size();
        ++converted;
    }
    if (instances) *instances = written;
    return converted;
}

}  // namespace cho
