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
//   Ids: in scene clips (any SubNetwork but Type 'Clip', base links / LayerBinds or not; clips30) an id is the
//   ClipProcessor output index in both games (1.0 FUN_005444c0 with no
//   table at +0x30, 3.0 FUN_00532050) and is kept. In motion clips it indexes the binding's ConnectionTemplate,
//   named by LayerBind.ConnectionTemplate ('characters/biped/biped.ct'). 3.0 numbers the templates it has itself
//   differently (twins, idrule.py: ids kept 105+55, 2i-2 273, other 4,161, e.g. _townspeople 29->56, 31->60,
//   34->63), and never requested such a template's .bct (in-game 2026-10-13: 23 1.0 biped clips served, no
//   biped.bct); for templates whose owner 3.0 lacks it loads the 1.0 .bct (7/7 requested: kiosk, delivery_boxes,
//   inc_omnisky_spawner, 4 choreography placement.bct). So a motion clip is re-packed with its ids kept only when the
//   template's owner folder is absent from the 3.0 install (di1compat.cpp), or renumbered (clip10ids, below).
//   clip10ids: an id is a port of the template, and a port is a connection name. 3.0 FUN_00552fc0 / FUN_00543f40 ->
//   FUN_00532050 take id -> the model's port table at evaluator +0x1c (the base link's PortToLocalIndices; 0xffff or
//   past its end = skipped) -> local -> node; 1.0 DataNodeLinks is the same map. The template files name nothing
//   (.bct: Filters / Mirrors), but every model link does: port -> local -> ConnectionMapUsedDataNodes -> DataNode,
//   whose header path without the model root is the connection ('cntrl_elbow_right/transform': 1.0 port 131, 3.0
//   port 276). Ports are that name list sorted case-folded (3.0 biped: then a 2nd sorted block from 935).
//   ports10.hpp (DI3Tools portmap.py over the model links of both extracts) maps each 1.0 port to the 3.0 port of
//   its name; a 1.0-only name gets its own 3.0 port no 3.0 model links, ports no 1.0 model links share one. Prediction on the motion-clip twins, the ids never used to build the table (octtest --clip-ids; rule.py):
//   same-size tables 105,708 ids at the twin's position, 1,131 not (41 blocks whose outputs 3.0 re-ordered, 10
//   re-authored); order-free 279,734 of 285,331 ids among the twin block's (ids kept: 1 and 33,553); 403 blocks
//   byte-identical to the twin (ids kept: 0). Those were biped / _townspeople / omnidroid; the table now covers all 25
//   templates 3.0 owns that 1.0 motion clips name but pir_kraken (portmap.py TEMPLATES), each checked on its twins
//   (twin ids per table among ours >= 95.6%, 521 blocks byte-identical over all) or, with no twin clip, on twin models
//   (0 ports of another name). With no spare 3.0 port left a 1.0 port is kDropPort: its outputs are dropped (3.0
//   models skip such an id as they skip a spare; the comps index section data absolutely, FUN_00532050 skips an
//   unmapped output by its comp words alone) and links30 skips it.
//   Only Type 'Clip' SubNetworks whose LayerBinds all name one mapped
//   template: Scene SubNetworks with a LayerBind (inc_helen_swoosh_*) carry ClipProcessor output indices (0..n).
//   links30 renumbers a served 1.0 model's DataNodeLinks with the same table, so 1.0 models and clips agree.
//   A 1.0 output with more than 7 comps (3.0 has 3 bits; 4 such in the
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
// anim30 (choanim10): the time input of ClipProcessor / EventList. 1.0 drives them from a Float
//   DataNode under the root header 'Global.Time'; 3.0 from an AnimState DataNode under the root header 'AnimState'.
//   3.0 binds the input slot by type (EventListProcessor FUN_0054b180: slot 2 'Input' via FUN_00530ad0, AnimState type
//   check DAT_0213e6b0/ac; a Float leaves the slot empty and the evaluate FUN_0052d390 passes NULL to FUN_0048d2b0:
//   in-game 2026-10-13 AV at 0048D2B3 once time30 let ts_watersplashdown evaluate), and its play clock finds the node
//   by path and type (FUN_007822e0 / FUN_007823b0 / FUN_0077d280 -> FUN_0077c530("AnimState"), same type check),
//   connects its TimeProcessor to it (FUN_00780d50 / FUN_00780af0) and takes the asset range from its RangeMin /
//   RangeMax (FUN_007823b0 -> FUN_0076c4d0). AnimState fields (field table 0x02003bb8): Time, RangeMin, RangeMax,
//   DiscontinuityTime, StateData. Twins (choreographies + exploders, twinscan.py): every matched ClipProcessor (680)
//   and EventList (716) Input is 1.0 Float 'Global.Time' -> 3.0 AnimState 'AnimState'; no other processor input
//   changed type. All 606 1.0 'Global.Time' nodes are {Type Float, Data 0.0} used only as those Inputs; all 606 3.0
//   AnimState nodes are {Type AnimState, Data {RangeMin, RangeMax}}. Range rule, exact on all 7,167 AnimState nodes
//   of the 3.0 choreographies + exploders + motions (animrule.py; octtest --cho-anim regenerates them): min / max
//   of the ClipDataBlock f32[0] / f32[1] and the event times of the ClipProcessors / EventLists whose Input is that
//   node; no FLT_MIN floor, so it differs from MinTime/MaxTime when every time is <= 0 (cin_isw_e_insideout: -7.33..
//   -0.5 vs ..FLT_MIN) or a clip has another input (krt_wkrh_kingcandy_*: BLINK clips -10..10 on a Float). Written
//   so: the node's Type and Data replaced (Header kept), the root header string renamed in place. Skipped, node left
//   as it was: the string also names another header, the SubNetwork already has an 'AnimState' string, or the node
//   drives no clip or event (no range).
//
// scene30 (choscene10): SceneTreeNodeRef on PlacementNode Associations. The 3.0 reader (FUN_00781510) fills a Layout
//   placement (0x18 bytes, PlacementNodePool read by FUN_00781320: +0 Name, +0x10 Flags) with +8 NodeName, +0xc the
//   SubNetwork node of that name (FUN_004ae9b0) and +4 the scene tree node of SceneTreeNodeRef (FUN_00452940; absent
//   or -1: none). The Layout dtor (FUN_00787bc0 -> FUN_0077e9a0 -> FUN_0077c6d0) releases +0xc, then +4. In-game
//   2026-10-13, once anim30 let TS_WaterSplashdown end, unloading it faulted in that +0xc release: the DataNode's dtor
//   wrote to its own page after the page stopped being writable (AV write at 004EDDBB, after the dtor's vftable
//   writes to the same page), i.e. the Layout's reference was the last one keeping that node's memory alive. Every 3.0
//   PlacementNode Association carries SceneTreeNodeRef (choreographies + exploders 14,782/14,782, worlds
//   29,850/29,850; every 3.0 file with placements has one with a scene node, 7,536/7,536); no 1.0 one does (4,031, + 9
//   in worlds), so a 1.0 Layout held the network node with no scene node released after it (the scene node is bound to
//   the same network by the 'Transform' Association, FUN_0049c770 -> FUN_00492fa0). That this ref is what keeps the
//   node alive is inferred, not traced. Rule (assoc3.py): the SceneTreeNodeRef of the 'Transform' Association named
//   <NodeName minus its last '|' part>, else -1: 14,779/14,782 3.0 placements (3 name nested scene nodes without a
//   Transform Association); twins: the same scene node (by name) in 879/881. Written after Type, where 3.0 has it.
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
#include <optional>
#include <map>
#include <set>
#include "base/bytes.hpp"
#include "octx.hpp"
#include "bent.hpp"
#include "ports10.hpp"

namespace cho {

inline constexpr size_t kU16Max = 0xffff;   // largest value of the clip format's u16 counts and offsets

// LE value of w bytes at o, 0xffffffff past the end; overflow-safe for any o (GR-06)
inline uint32_t rd(const std::string& s, size_t o, int w)
{
    return bytes::fits(s.size(), o, (size_t)w) ? oct::u((const uint8_t*)s.data() + o, w) : 0xffffffffu;
}
inline void wr(std::string& s, uint32_t v, int w) { oct::File::put(s, v, w); }
// first int of a record, def if it has none (a node or an empty list)
inline int first(const oct::File& f, const oct::Rec& r, int def)
{
    auto v = f.ints(r);
    return v.empty() ? def : v[0];
}

namespace detail {

// A record reader of a scan table: record i of f into out
template <class Out>
struct FieldRule {
    const char* name;
    void (*read)(const oct::File& f, size_t i, Out& out);
};

// Runs the rule named like record i, if any (the names of a table are distinct)
template <class Out, size_t N>
inline void readField(const oct::File& f, size_t i, const FieldRule<Out> (&rules)[N], Out& out)
{
    const std::string& k = f.name(f.recs[i]);
    for (const FieldRule<Out>& rule : rules) {
        if (k != rule.name) continue;
        rule.read(f, i, out);
        return;
    }
}

// The pool's level-2 children named SubNetwork, in file order
inline std::vector<size_t> subNetworks(const oct::File& f, long pool)
{
    std::vector<size_t> sns;
    for (size_t i = pool + 1, e = f.blockEnd(pool); i < e; ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") sns.push_back(i);
    return sns;
}

// The first child of record i (one level down) with this name, -1 if none
inline long child(const oct::File& f, size_t i, const char* nm)
{
    for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j)
        if (f.recs[j].level == f.recs[i].level + 1 && f.name(f.recs[j]) == nm) return (long)j;
    return -1;
}

// Record r's first name ("" for none or r < 0)
inline std::string name1(const oct::File& f, long r)
{
    auto v = r < 0 ? std::vector<std::string>{} : f.nameValues(f.recs[r]);
    return v.empty() ? std::string() : v[0];
}

// A ClipDataBlock's play range: its first two f32 (nullopt when shorter)
inline std::optional<std::pair<float, float>> clipTimes(const std::string& block)
{
    if (block.size() < 8) return std::nullopt;
    float a, z;
    memcpy(&a, &block[0], 4);
    memcpy(&z, &block[4], 4);
    return std::make_pair(a, z);
}

// fn(time) per event of the EventList at off in a NetworkBuffer: u32 n, n x (f32 time, f32 value)
template <class Fn>
inline void eachEventTime(const std::string& nb, uint32_t off, Fn fn)
{
    uint32_t cnt = rd(nb, off, 4);
    for (uint32_t k = 0; cnt != 0xffffffffu && k < cnt && bytes::fits(nb.size(), off + 4 + 8 * (size_t)k, 4); ++k) {
        float t;
        memcpy(&t, &nb[off + 4 + 8 * k], 4);
        fn(t);
    }
}

}  // namespace detail

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
    if (heads.size() > kU16Max) return false;   // GR-33
    wr(t, (uint32_t)heads.size(), 2);
    for (auto& h : heads) wr(t, (uint32_t)(h.second << 13 | h.first), 2);
    for (auto& c : outs)
        for (auto& w : c.comps)
            for (uint16_t x : w) wr(t, x, 2);
    return true;
}

// One 1.0 ClipDataBlock (payload without its count) -> 3.0 layout, ids through pm when given (a 1.0 port past the
// table fails the block). False (out untouched) if it does not parse.
inline bool clipBlock30(const std::string& d, std::string& out, const PortMap* pm = nullptr)
{
    auto renumber = [pm](std::vector<ClipOut>& v) {
        for (auto& c : v) {
            if (c.id >= pm->n10) return false;
            c.id = pm->to30[c.id];
        }
        // kDropPort: an output every model skips (comps carry absolute data indices, so the rest read the same)
        v.erase(std::remove_if(v.begin(), v.end(), [](const ClipOut& c) { return c.id == kDropPort; }), v.end());
        return true;
    };
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
    if (last >= areaEnd) return false;   // a descriptor past the payload: the padding scan below would read past d
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
        if (pm && (!renumber(samp) || !renumber(cnst))) return false;
        std::string st, ct;
        if (!table30(samp, st) || !table30(cnst, ct)) return false;
        std::string t = d.substr(s, 4 + 2 * (size_t)n);
        if (t.size() + 2 + st.size() > kU16Max) return false;   // X, the constant table's u16 offset (GR-33)
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

// The port renumbering of a motion clip (SubNetwork Type 'Clip') whose LayerBinds all name one template in ports10.hpp,
// else nullptr. Scene SubNetworks are left out: their ids are ClipProcessor outputs even beside a LayerBind.
inline const PortMap* clipPorts(const oct::File& f, size_t sn)
{
    const PortMap* pm = nullptr;
    bool clip = false, other = false;
    for (size_t j = sn + 1, e = f.blockEnd(sn), lvl4 = 0; j < e; ++j) {
        const oct::Rec& r = f.recs[j];
        if (r.level == 3 && f.name(r) == "Type") clip = f.nameValues(r) == std::vector<std::string>{"Clip"};
        if (r.level == 4) lvl4 = j;
        if (r.level == 5 && f.name(r) == "ConnectionTemplate" && lvl4 && f.name(f.recs[lvl4]) == "LayerBind") {
            auto v = f.nameValues(r);
            const PortMap* m = v.empty() ? nullptr : portMap(v[0].c_str());
            other |= !m || (pm && m != pm);
            pm = m;
        }
    }
    return clip && !other ? pm : nullptr;
}

// Every ClipDataBlock of the file. A motion clip (SubNetwork Type 'Clip' naming a ConnectionTemplate) is re-packed
// only if motion(ConnectionTemplate) says its ids index a template with 1.0 numbering (null: never), or, with ids, if
// clipPorts gives its renumbering (then its ids go through it). Every other SubNetwork's blocks are scene clips,
// re-packed with ids kept whatever templates its base links or LayerBinds name: there each block is read by a
// ClipProcessor of the same SubNetwork and its ids index that processor's outputs (idscan.py over choreographies +
// exploders, + characters in 1.0: 1.0 2,971/2,971 Scene blocks, 361 beside a BaseDataNodeLinks / LayerBind
// ConnectionTemplate; 3.0 8,014/8,014, 396 beside one; ids exactly 0..outputs-1 in all of them; no 1.0 Scene block
// lacks a local ClipProcessor (3.0: 4, beside LayerBinds); 1.0 motion clips are Type 'Clip' with no processor). Keying the skip on any ConnectionTemplate left inc_dock_explode's three scene
// blocks (debris, the embedded omnidroid rig's Cntrl_* controls, the camera; base link 'characters/omnidroid/
// omnidroid.ct', an owner 3.0 has) in the 1.0 layout: 3.0's FUN_00532050 reads the interleaved {head, id} words as
// heads, gets 0 comps or an id past the outputs (where it returns), and nothing moved for the whole play (in-game
// 2026-10-14). Returns the blocks re-packed; *failed counts blocks left as they are, *renumbered the re-packed ones
// with renumbered ids.
inline int clips30(oct::File& f, const std::function<bool(const std::string&)>& motion, int* failed = nullptr, bool ids = false,
                   int* renumbered = nullptr)
{
    int n = 0;
    bool skip = false;
    const PortMap* pm = nullptr;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        oct::Rec& r = f.recs[i];
        if (r.level == 2 && f.name(r) == "SubNetwork") {
            skip = false;
            pm = ids ? clipPorts(f, i) : nullptr;
            bool clip = false;
            long tmpl = -1;
            for (size_t j = i + 1, e = f.blockEnd(i); j < e && !pm; ++j) {
                const oct::Rec& q = f.recs[j];
                if (q.level == 3 && f.name(q) == "Type") clip = f.nameValues(q) == std::vector<std::string>{"Clip"};
                else if (q.level == 5 && tmpl < 0 && f.name(q) == "ConnectionTemplate") tmpl = (long)j;
            }
            if (clip && tmpl >= 0) {
                auto v = f.nameValues(f.recs[tmpl]);
                skip = !motion || v.empty() || !motion(v[0]);
            }
            continue;
        }
        if (skip || f.name(r) != "ClipDataBlock") continue;
        oct::Spec s = oct::spec(r.type);
        if (s.kind != oct::BYTE) continue;
        std::string out;
        if (!clipBlock30(r.pay.substr(s.cw), out, pm)) { if (failed) ++*failed; continue; }
        r = octx::blobRec(f, r.level, "ClipDataBlock", out);
        ++n;
        if (pm && renumbered) ++*renumbered;
    }
    return n;
}

// Model side of the same renumbering: a 1.0 BaseDataNodeLinks on a template in ports10.hpp gets its DataNodeLinks
// (local per port) re-indexed by 3.0 port, so a served 1.0 model and the renumbered clips agree; octx::convertLink then
// writes the 3.0 blobs from it. Returns the links renumbered.
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
        // a port linked past the table: not this template's numbering. Unlinked (-1) ports past it are the 1.0
        // template's tail no scanned model links (ts_bullseye: every model 116 long, highest linked 111, n10 112)
        size_t used = 0;
        for (size_t p = 0; p < old.size(); ++p) if (old[p] >= 0) used = p + 1;
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
inline oct::Rec floatRec(oct::File& f, const char* name, float v, uint16_t level = 3)
{
    oct::Rec r{level, (uint16_t)(oct::SCALAR | oct::FLOAT << 3), f.addName(name), std::string(4, '\0')};
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

namespace detail {

// What anim30 reads of one SubNetwork: its level-3 header records and pools, and a NetworkBuffer copy
struct AnimScan {
    long hs = -1, hsi = -1, hl = -1;   // HeaderStrings, HeaderStringIndices, HeaderLayout
    long dp = -1, pp = -1;             // DataNodePool, ProcessorNodePool
    std::string nb;
};
inline constexpr FieldRule<AnimScan> kAnimLevel3[] = {
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
        long b = child(f, src, "ClipDataBlock");
        if (b < 0 || oct::spec(f.recs[b].type).kind != oct::BYTE) return;
        const auto ts = clipTimes(f.recs[b].pay.substr(oct::spec(f.recs[b].type).cw));
        if (!ts) return;
        r.add(ts->first);
        r.add(ts->second);
        return;
    }
    long o = child(f, src, "NetworkBufferOffset");
    long at = o < 0 ? -1 : first(f, f.recs[o], -1);
    if (at >= 0) eachEventTime(s.nb, (uint32_t)at, [&](float t) { r.add(t); });   // as time30's off >= 0
}

// The time range of the ClipProcessors / EventLists whose Input is DataNode d (FORMATS.md 4.10)
inline TimeRange timeRange(const oct::File& f, const AnimScan& s, const std::vector<size_t>& dns, int d)
{
    TimeRange r;
    for (size_t i = s.pp + 1, e = f.blockEnd(s.pp); i < e; ++i) {
        if (f.recs[i].level != 4) continue;
        std::string ty = name1(f, child(f, i, "Type"));
        long ar = child(f, i, "DataNodeAttributes"), rr = child(f, i, "DataNodeRefs");
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
    long hd = child(f, dns[d], "Header"), ty = child(f, dns[d], "Type");
    if (hd < 0 || ty < 0) return {};
    if (first(f, f.recs[hd], -1) != h || name1(f, ty) != "Float") return {};
    const TimeRange r = timeRange(f, s, dns, (int)d);
    if (!r.any) return {};
    return {f.recs[hd], f.nameScalar(5, f.recs[ty].name, "AnimState"), f.node(5, "Data"),
            floatRec(f, "RangeMin", r.t0, 6), floatRec(f, "RangeMax", r.t1, 6)};
}

// HeaderStrings element s set to name index a in place (a non-keyed NAME list of u16, GR-34)
inline void setHeaderString(oct::File& f, long hs, int32_t s, uint16_t a)
{
    const oct::Spec ss = oct::spec(f.recs[hs].type);
    memcpy(&f.recs[hs].pay[ss.cw + 2 * (size_t)s], &a, 2);
}

// One SubNetwork's time input in 3.0's form; the DataNodes converted
inline int animSubNetwork(oct::File& f, size_t sn)
{
    AnimScan s;
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i)
        if (f.recs[i].level == 3) readField(f, i, kAnimLevel3, s);
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
    std::vector<std::vector<oct::Rec>> rep(dns.size());   // per DataNode: its children to be, built before any edit
    int here = 0;
    for (size_t d = 0; d < dns.size(); ++d) {
        rep[d] = animNode(f, s, dns, d, th->h);
        here += !rep[d].empty();
    }
    if (!here) return 0;
    setHeaderString(f, s.hs, th->s, f.addName("AnimState"));   // before the edits: they shift a later hs (GR-N1)
    for (size_t d = dns.size(); d-- > 0;) {   // last first: edits keep earlier indices
        if (rep[d].empty()) continue;
        size_t i = dns[d];
        f.recs.erase(f.recs.begin() + i + 1, f.recs.begin() + f.blockEnd(i));
        f.recs.insert(f.recs.begin() + i + 1, rep[d].begin(), rep[d].end());
    }
    return here;
}

}  // namespace detail

// anim30 (choanim10): 'Global.Time' Float inputs become AnimState (FORMATS.md 4.10). Returns the DataNodes converted;
// *subnets = SubNetworks touched.
inline int anim30(oct::File& f, int* subnets = nullptr)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    const std::vector<size_t> sns = detail::subNetworks(f, pool);
    int n = 0, touched = 0;
    for (auto it = sns.rbegin(); it != sns.rend(); ++it) {   // last first: edits keep earlier indices
        const int here = detail::animSubNetwork(f, *it);
        n += here;
        touched += here > 0;
    }
    if (subnets) *subnets = touched;
    return n;
}

// ---- constant section ----
namespace detail {

// A DataNode as const30 reads it
struct ConstNode {
    long hd = -1, st = -1;   // its Header and Static records
    bool stat = false, ext = false;
    std::string type;
};
struct ConstProc {
    std::string type;
    std::vector<std::string> attrs;   // DataNodeAttributes
    std::vector<int32_t> refs;        // DataNodeRefs
};
// What const30 reads of one SubNetwork
struct ConstScan {
    long hs = -1, hsi = -1, hl = -1;   // HeaderStrings, HeaderStringIndices, HeaderLayout
    long after = -1;                   // its last level-3 Type or Name: ConstantHeaderCount goes after it
    bool counted = false;              // it has a VariableHeaderCount or ConstantHeaderCount already
    std::vector<ConstNode> dns;
    std::vector<long> pn;              // ProcessorNode Header records
    std::vector<ConstProc> procs;
    std::set<int> convRefs;            // BasisConversion DataNodeRefs and ConnectionMapUsedDataNodes
};

inline void lastTypeOrName(const oct::File&, size_t i, ConstScan& s) { s.after = (std::max)(s.after, (long)i); }
inline void alreadyCounted(const oct::File&, size_t, ConstScan& s) { s.counted = true; }
inline constexpr FieldRule<ConstScan> kConstLevel3[] = {
    {"HeaderStrings", [](const oct::File&, size_t i, ConstScan& s) { s.hs = (long)i; }},
    {"HeaderStringIndices", [](const oct::File&, size_t i, ConstScan& s) { s.hsi = (long)i; }},
    {"HeaderLayout", [](const oct::File&, size_t i, ConstScan& s) { s.hl = (long)i; }},
    {"VariableHeaderCount", alreadyCounted},
    {"ConstantHeaderCount", alreadyCounted},
    {"Type", lastTypeOrName},
    {"Name", lastTypeOrName},
};
inline constexpr FieldRule<ConstNode> kConstDataField[] = {
    {"Header", [](const oct::File&, size_t i, ConstNode& d) { d.hd = (long)i; }},
    {"Static",
     [](const oct::File& f, size_t i, ConstNode& d) {
         d.st = (long)i;
         d.stat = first(f, f.recs[i], 0) != 0;
     }},
    {"External", [](const oct::File& f, size_t i, ConstNode& d) { d.ext = first(f, f.recs[i], 0) != 0; }},
    {"Type",
     [](const oct::File& f, size_t i, ConstNode& d) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) d.type = v[0];
     }},
};
inline constexpr FieldRule<ConstProc> kConstProcField[] = {
    {"Type",
     [](const oct::File& f, size_t i, ConstProc& p) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) p.type = v[0];
     }},
    {"DataNodeAttributes", [](const oct::File& f, size_t i, ConstProc& p) { p.attrs = f.nameValues(f.recs[i]); }},
    {"DataNodeRefs", [](const oct::File& f, size_t i, ConstProc& p) { p.refs = f.ints(f.recs[i]); }},
};

// A level-5 field of the pool named pool3
inline void scanConstField(const oct::File& f, size_t i, const std::string& pool3, ConstScan& s)
{
    const oct::Rec& r = f.recs[i];
    if (pool3 == "DataNodePool" && !s.dns.empty()) readField(f, i, kConstDataField, s.dns.back());
    else if (pool3 == "ProcessorNodePool" && !s.pn.empty()) {
        if (f.name(r) == "Header") s.pn.back() = (long)i;
        readField(f, i, kConstProcField, s.procs.back());
    } else if (pool3 == "BasisConversionPool" && f.name(r) == "DataNodeRef") s.convRefs.insert(first(f, r, -1));
}

// A level-4 entry of the pool named pool3
inline void scanConstEntry(const std::string& pool3, ConstScan& s)
{
    if (pool3 == "DataNodePool") s.dns.emplace_back();
    else if (pool3 == "ProcessorNodePool") {
        s.pn.push_back(-1);
        s.procs.emplace_back();
    }
}

inline ConstScan scanConst(const oct::File& f, size_t sn)
{
    ConstScan s;
    std::string pool3;   // the level-3 record the scan is under
    for (size_t i = sn + 1, end = f.blockEnd(sn); i < end; ++i) {
        const oct::Rec& r = f.recs[i];
        if (r.level == 3) {
            pool3 = f.name(r);
            readField(f, i, kConstLevel3, s);
            if (pool3 == "ConnectionMapUsedDataNodes")
                for (int32_t d : f.ints(r)) s.convRefs.insert(d);   // same treatment: must stay variable
        } else if (r.level == 4) scanConstEntry(pool3, s);
        else if (r.level == 5) scanConstField(f, i, pool3, s);
    }
    return s;
}

// A SubNetwork's header tree, headers 1..H: parent (0 = the root), name and children each
struct HeaderTree {
    size_t H = 0;
    std::vector<int> parent;
    std::vector<std::string> name;
    std::vector<std::vector<int>> kids;
};

// HeaderLayout's (parent, child count) pairs decoded; nullopt if they name a later parent or a missing string
inline std::optional<HeaderTree> headerTree(const std::vector<std::string>& strs, const std::vector<int32_t>& idx,
                                            const std::vector<int32_t>& lay)
{
    HeaderTree t;
    t.H = idx.size();
    t.parent.assign(t.H + 1, -1);
    t.name.assign(t.H + 1, std::string());
    t.kids.assign(t.H + 1, std::vector<int>());
    size_t k = 0;
    for (size_t p = 0; p + 1 < lay.size(); p += 2)
        for (int32_t c = 0; c < lay[p + 1]; ++c) {
            ++k;
            const bool parentOk = k <= t.H && lay[p] >= 0 && lay[p] < (int)k;
            if (!parentOk || idx[k - 1] < 0 || idx[k - 1] >= (int)strs.size()) return std::nullopt;
            t.parent[k] = lay[p];
            t.name[k] = strs[idx[k - 1]];
            t.kids[lay[p]].push_back((int)k);
        }
    if (k != t.H) return std::nullopt;
    return t;
}

// The header a Header record names, -1 for none or one outside 1..H
inline int headerOf(const oct::File& f, const HeaderTree& t, long rec)
{
    int h = rec < 0 ? -1 : first(f, f.recs[rec], -1);
    return h >= 1 && h <= (int)t.H ? h : -1;
}

// Header h's path from the root, 'a/b/c'
inline std::string pathOf(const HeaderTree& t, int h)
{
    std::string s;
    for (int x = h; x > 0; x = t.parent[x]) s = s.empty() ? t.name[x] : t.name[x] + "/" + s;
    return s;
}

// Every processor attribute 3.0 ships with a Static node (FORMATS.md 4.10, staticrefs.py)
inline constexpr const char* kStaticInputs30[] = {
    "AimConstraintProcessorNode.AlignReference", "AimConstraintProcessorNode.Offset",
    "AimConstraintProcessorNode.UpReference", "ClipProcessor.ClipData", "DilationOrientationConnection.RotateOffset",
    "DilationTranslationConnection.TranslateOffset", "DilationTranslationOrientationConnection.RotateOffset",
    "DilationTranslationOrientationConnection.TranslateOffset", "EventList.EventListData",
    "EventList.ReverseEventListData", "IK2BRPLengthSquashZConstraint.InputDistanceThreshold",
    "IK2BRPLengthSquashZConstraint.InputEndInitialPosition", "IK2BRPLengthSquashZConstraint.InputMidInitialPosition",
    "IK2BRPLengthSquashZConstraint.InputMidLocalToParentInitialOrientation",
    "IK2BRPLengthSquashZConstraint.InputMidLocalToStartParentInitialOrientation",
    "IK2BRPLengthSquashZConstraint.InputStartInitialPosition",
    "IK2BRPLengthSquashZConstraint.InputStartLocalToParentInitialOrientation",
    "IK2BRPLengthSquashZConstraint.InputStartLocalToStartParentInitialOrientation", "IkLean2LinkProcessor.Axis",
    "MotionPathOrientationProcessor.Spline", "MotionPathPositionProcessor.Spline", "SplineInputProcessor.Spline",
    "Position3dOffsetProcessor.Offset", "Position3dWeightedAverageProcessor.TargetWeight",
    "RadialBasisApplyFloat.Neutral", "RadialBasisApplyFloat.Weights", "RadialBasisPosition.ControllerKeys",
    "RadialBasisPosition.StandardDeviationValues", "RelationshipProcessor.RelationshipData",
    "TransformTranslationOrientationConnection.RotateOffset",
    "TransformTranslationOrientationScaleConnection.TranslateOffset", "UnitRotor3dOffsetProcessor.Offset",
    "UnitRotor3dWeightedAverageProcessor.TargetWeight"};

inline bool staticInput30(const std::string& typeDotAttribute)
{
    return std::any_of(std::begin(kStaticInputs30), std::end(kStaticInputs30),
                       [&](const char* s) { return typeDotAttribute == s; });
}

// The DataNodes some processor takes through an input 3.0 never feeds from a Static node
inline std::vector<bool> offListInputs(const ConstScan& s)
{
    std::vector<bool> offList(s.dns.size(), false);
    for (auto& pr : s.procs)
        for (size_t q = 0; q < pr.attrs.size() && q < pr.refs.size(); ++q) {
            std::string a = pr.attrs[q].substr(0, pr.attrs[q].find('['));
            const bool node = pr.refs[q] >= 0 && (size_t)pr.refs[q] < s.dns.size();
            if (node && !staticInput30(pr.type + "." + a)) offList[pr.refs[q]] = true;
        }
    return offList;
}

// Makes a Static node variable, in the file too (it stays so when const30 skips the SubNetwork)
inline void demote(oct::File& f, ConstNode& d)
{
    f.setInt(f.recs[d.st], 0);
    d.stat = false;
}

// Static nodes of a shape 3.0 never keeps Static become variable (FORMATS.md 4.10); named: Association paths
inline void demoteStatics(oct::File& f, ConstScan& s, const HeaderTree& t, const std::set<std::string>* named)
{
    const std::vector<bool> offList = offListInputs(s);
    for (size_t di = 0; di < s.dns.size(); ++di) {
        ConstNode& d = s.dns[di];
        if (!d.stat) continue;
        const int h = headerOf(f, t, d.hd);
        const bool neverStatic = d.type == "Bool" || d.type == "Int" || d.type == "Visibility";
        const bool shape = neverStatic || (d.ext && d.type != "ClipData");
        const bool bound = h > 0 && named && named->count(pathOf(t, h));
        const bool used = bound || s.convRefs.count((int)di) || offList[di];
        if (shape || used) demote(f, d);
    }
}

// True when some path of var is p or lies below it
inline bool onOrBelow(const std::vector<std::string>& var, const std::string& p)
{
    for (const std::string& v : var) {
        const bool below = v.size() > p.size() && v.compare(0, p.size(), p) == 0 && v[p.size()] == '/';
        if (v == p || below) return true;
    }
    return false;
}

// The header paths of the variable nodes and the processors
inline std::vector<std::string> variablePaths(const oct::File& f, const ConstScan& s, const HeaderTree& t)
{
    std::vector<std::string> var;
    for (auto& d : s.dns)
        if (!d.stat && headerOf(f, t, d.hd) > 0) var.push_back(pathOf(t, headerOf(f, t, d.hd)));
    for (long r : s.pn)
        if (headerOf(f, t, r) > 0) var.push_back(pathOf(t, headerOf(f, t, r)));
    return var;
}

// A Static node with a variable node or processor on or below its header becomes variable, until none is left
inline void dropClashes(oct::File& f, ConstScan& s, const HeaderTree& t)
{
    for (bool again = true; again;) {
        again = false;
        const std::vector<std::string> var = variablePaths(f, s, t);
        for (auto& d : s.dns) {
            if (!d.stat || headerOf(f, t, d.hd) <= 0) continue;
            if (!onOrBelow(var, pathOf(t, headerOf(f, t, d.hd)))) continue;
            demote(f, d);
            again = true;
        }
    }
}

// A Static node's variable header goes when nothing else uses it and all its children went
inline std::vector<bool> droppedHeaders(const HeaderTree& t, const std::set<int>& moving, const std::set<int>& used)
{
    std::vector<bool> dropped(t.H + 1, false);
    for (int h = (int)t.H; h >= 1; --h) {   // headers are numbered parents first: children are decided first
        bool d = moving.count(h) && !used.count(h);
        for (int c : t.kids[h]) d = d && dropped[c];
        dropped[h] = d;
    }
    return dropped;
}

// The constant headers: (parent, name) -> id, ids 1.. in creation order
struct ConstTrie {
    std::vector<int> cpar{-1};
    std::vector<std::string> cname{""};
    std::vector<std::vector<int>> ckids = std::vector<std::vector<int>>(1);
    std::map<std::pair<int, std::string>, int> ids;
    std::map<int, int> constOf;   // old header of a Static node -> constant id
};

// Constant id of the path comps, the missing headers created
inline int trieAdd(ConstTrie& c, const std::vector<std::string>& comps)
{
    int at = 0;
    for (auto& name : comps) {
        auto t = c.ids.find({at, name});
        if (t == c.ids.end()) {
            int id = (int)c.cpar.size();
            c.cpar.push_back(at);
            c.cname.push_back(name);
            c.ckids.push_back({});
            c.ckids[at].push_back(id);
            t = c.ids.emplace(std::make_pair(at, name), id).first;
        }
        at = t->second;
    }
    return at;
}

// Each moving header's constant path: its variable path with the root prefixed 'Constant' below the root
inline ConstTrie constantTrie(const HeaderTree& t, const std::set<int>& moving)
{
    ConstTrie c;
    for (int h : moving) {
        std::vector<std::string> comps;
        for (int x = h; x > 0; x = t.parent[x]) comps.insert(comps.begin(), t.name[x]);
        if (comps.size() > 1) comps[0] = "Constant" + comps[0];
        c.constOf[h] = trieAdd(c, comps);
    }
    return c;
}

// The new header numbering: constant headers 1..chc, then the kept variable headers breadth first
struct ConstLayout {
    std::vector<std::string> newName{""};
    std::vector<int32_t> newLay;   // (parent, child count) pairs
    std::vector<int> cnum, vnum;   // per constant id / old variable header: its number in newName
    int chc = 0;
};

inline ConstLayout numberHeaders(const HeaderTree& t, const ConstTrie& c, const std::vector<bool>& dropped)
{
    ConstLayout L;
    L.cnum.assign(c.cpar.size(), 0);
    L.vnum.assign(t.H + 1, 0);
    for (size_t q = 0; q < c.cpar.size(); ++q) {   // ids are created parents-first, so q's number exists
        int me = q == 0 ? 0 : L.cnum[q];
        if (c.ckids[q].empty()) continue;
        L.newLay.insert(L.newLay.end(), {me, (int32_t)c.ckids[q].size()});
        for (int k : c.ckids[q]) {
            L.cnum[k] = (int)L.newName.size();
            L.newName.push_back(c.cname[k]);
        }
    }
    L.chc = (int)L.newName.size() - 1;
    std::vector<int> order{0};   // variable headers breadth first from the root, kept ones only
    for (size_t q = 0; q < order.size(); ++q) {
        std::vector<int> keep;
        for (int k : t.kids[order[q]]) if (!dropped[k]) keep.push_back(k);
        if (keep.empty()) continue;
        L.newLay.insert(L.newLay.end(), {order[q] == 0 ? 0 : L.vnum[order[q]], (int32_t)keep.size()});
        for (int k : keep) {
            L.vnum[k] = (int)L.newName.size();
            L.newName.push_back(t.name[k]);
            order.push_back(k);
        }
    }
    return L;
}

// The header records rewritten to the new numbering, strings appended as needed; then ConstantHeaderCount
inline void rewriteHeaders(oct::File& f, const ConstScan& s, const HeaderTree& t, const ConstTrie& c,
                           const ConstLayout& L)
{
    std::vector<std::string> strs = f.nameValues(f.recs[s.hs]);
    std::vector<int32_t> newIdx;
    for (size_t h = 1; h < L.newName.size(); ++h) {
        auto it = std::find(strs.begin(), strs.end(), L.newName[h]);   // the first match
        if (it == strs.end()) {
            strs.push_back(L.newName[h]);
            it = strs.end() - 1;
        }
        newIdx.push_back((int32_t)(it - strs.begin()));
    }
    f.setInts(f.recs[s.hsi], newIdx);
    f.setInts(f.recs[s.hl], L.newLay);
    octx::setNames(f, f.recs[s.hs], strs);
    for (auto& d : s.dns) {
        int h = headerOf(f, t, d.hd);
        if (h < 0) continue;
        auto id = c.constOf.find(h);
        f.setInt(f.recs[d.hd], d.stat ? L.cnum[id == c.constOf.end() ? 0 : id->second] : L.vnum[h]);
    }
    for (long r : s.pn) {
        int h = headerOf(f, t, r);
        if (h >= 0) f.setInt(f.recs[r], L.vnum[h]);
    }
    f.recs.insert(f.recs.begin() + s.after + 1, f.intScalar(3, "ConstantHeaderCount", L.chc));
}

// The constant header paths, 'ConstantA/b'
inline std::set<std::string> constantPaths(const ConstTrie& c)
{
    std::set<std::string> paths;
    for (size_t q = 1; q < c.cpar.size(); ++q) {
        std::string pth = c.cname[q];
        for (int x = c.cpar[q]; x > 0; x = c.cpar[x]) pth = c.cname[x] + "/" + pth;
        paths.insert(pth);
    }
    return paths;
}

// One SubNetwork's Static nodes under constant headers: its constant paths, nullopt if it stays as it is
inline std::optional<std::set<std::string>> constSubNetwork(oct::File& f, size_t sn, const std::set<std::string>* named)
{
    ConstScan s = scanConst(f, sn);
    const bool headers = s.hs >= 0 && s.hsi >= 0 && s.hl >= 0;
    const bool usable = headers && s.after >= 0 && !s.pn.empty();
    if (s.counted || !usable) return std::nullopt;
    const oct::Spec hsSpec = oct::spec(f.recs[s.hs].type);
    if (hsSpec.kind != oct::NAME || hsSpec.keyed) return std::nullopt;
    const std::optional<HeaderTree> t =
        headerTree(f.nameValues(f.recs[s.hs]), f.ints(f.recs[s.hsi]), f.ints(f.recs[s.hl]));
    if (!t) return std::nullopt;
    demoteStatics(f, s, *t, named);
    dropClashes(f, s, *t);
    if (std::none_of(s.dns.begin(), s.dns.end(), [](const ConstNode& d) { return d.stat; }))
        return std::nullopt;   // addCounts' variable form is then the 3.0 one
    std::set<int> moving, used;
    for (auto& d : s.dns) (d.stat ? moving : used).insert(headerOf(f, *t, d.hd));
    for (long r : s.pn) used.insert(headerOf(f, *t, r));
    moving.erase(-1);
    if (moving.empty()) return std::nullopt;
    const std::vector<bool> dropped = droppedHeaders(*t, moving, used);
    const ConstTrie c = constantTrie(*t, moving);
    rewriteHeaders(f, s, *t, c, numberHeaders(*t, c, dropped));
    return constantPaths(c);
}

// SubNetwork ordinal -> the paths its Associations bind ('a/b'), from NodeName and SubNetworkRef
inline std::map<int, std::set<std::string>> boundPaths(const oct::File& f)
{
    std::map<int, std::set<std::string>> named;
    long ap = f.pool("AssociationPool");
    for (size_t i = ap < 0 ? 0 : ap + 1, e = ap < 0 ? 0 : f.blockEnd(ap); i < e; i = f.blockEnd(i)) {
        std::string node;
        int snRef = -1;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) {
            const std::string& k = f.name(f.recs[j]);
            if (k == "NodeName") {
                auto v = f.nameValues(f.recs[j]);
                if (!v.empty()) node = v[0];
            } else if (k == "SubNetworkRef") snRef = first(f, f.recs[j], -1);
        }
        std::replace(node.begin(), node.end(), '|', '/');
        if (snRef >= 0 && !node.empty()) named[snRef].insert(node);
    }
    return named;
}

// An Association as the AltNodeName pass reads it
struct AltScan {
    std::string type, node;
    int snRef = -1;
    bool alt = false;
};
inline constexpr FieldRule<AltScan> kAltFields[] = {
    {"Type",
     [](const oct::File& f, size_t i, AltScan& a) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) a.type = v[0];
     }},
    {"NodeName",
     [](const oct::File& f, size_t i, AltScan& a) {
         auto v = f.nameValues(f.recs[i]);
         if (!v.empty()) a.node = v[0];
     }},
    {"SubNetworkRef", [](const oct::File& f, size_t i, AltScan& a) { a.snRef = first(f, f.recs[i], -1); }},
    {"AltNodeName", [](const oct::File&, size_t, AltScan& a) { a.alt = true; }},
};

// AltNodeName on each Transform Association whose 'Constant'+NodeName header exists (FORMATS.md 4.10)
inline void addAltNodeNames(oct::File& f, const std::map<int, std::set<std::string>>& constPaths)
{
    long ap = f.pool("AssociationPool");
    if (ap < 0) return;
    std::vector<std::pair<size_t, std::string>> adds;   // insert position, AltNodeName
    for (size_t i = ap + 1, e = f.blockEnd(ap); i < e; i = f.blockEnd(i)) {
        if (f.recs[i].level != 2) continue;
        AltScan a;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) readField(f, j, kAltFields, a);
        std::string want = "Constant" + a.node;
        std::replace(want.begin(), want.end(), '|', '/');
        auto paths = constPaths.find(a.snRef);
        const bool has = paths != constPaths.end() && paths->second.count(want);
        if (a.type == "Transform" && !a.alt && has) adds.push_back({f.blockEnd(i), "Constant" + a.node});
    }
    for (auto it = adds.rbegin(); it != adds.rend(); ++it) {
        const uint16_t key = f.addName("AltNodeName");
        f.recs.insert(f.recs.begin() + it->first, f.nameScalar(3, key, it->second));
    }
}

}  // namespace detail

// const30 (choconst10): processor SubNetworks' Static nodes under constant headers (FORMATS.md 4.10)
inline int const30(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    const std::vector<size_t> sns = detail::subNetworks(f, pool);
    int done = 0;
    std::map<int, std::set<std::string>> constPaths;   // SubNetwork ordinal -> its constant header paths
    const std::map<int, std::set<std::string>> named = detail::boundPaths(f);
    for (size_t q = sns.size(); q-- > 0;) {   // last first: the one inserted record keeps earlier indices
        auto bound = named.find((int)q);
        const std::set<std::string>* paths = bound == named.end() ? nullptr : &bound->second;
        std::optional<std::set<std::string>> made = detail::constSubNetwork(f, sns[q], paths);
        if (!made) continue;
        constPaths[(int)q] = std::move(*made);
        ++done;
    }
    if (done) detail::addAltNodeNames(f, constPaths);
    return done;
}

// ---- placement scene nodes ----
// Returns the PlacementNode Associations given a SceneTreeNodeRef.
inline int scene30(oct::File& f)
{
    long pool = f.pool("AssociationPool");
    if (pool < 0) return 0;
    struct A { std::string name, type; long typeAt = -1; int scene = -2; };   // -2: no SceneTreeNodeRef record
    std::vector<A> as;
    for (size_t i = pool + 1, e = f.blockEnd(pool); i < e; ++i) {
        const oct::Rec& r = f.recs[i];
        if (r.level == 2) { as.push_back({}); continue; }
        if (r.level != 3 || as.empty()) continue;
        const std::string& k = f.name(r);
        if (k == "NodeName" || k == "Type") {
            auto v = f.nameValues(r);
            if (v.empty()) continue;
            if (k == "NodeName") as.back().name = v[0];
            else { as.back().type = v[0]; as.back().typeAt = (long)i; }
        } else if (k == "SceneTreeNodeRef") as.back().scene = first(f, r, -1);
    }
    std::map<std::string, int> tr;   // 'Transform' Association NodeName -> its scene node (first wins)
    for (auto& a : as)
        if (a.type == "Transform" && a.scene != -2) tr.emplace(a.name, a.scene);
    int n = 0;
    for (auto it = as.rbegin(); it != as.rend(); ++it) {   // last first: insertions keep earlier indices
        if (it->type != "PlacementNode" || it->scene != -2) continue;
        size_t bar = it->name.rfind('|');
        auto t = bar == std::string::npos ? tr.end() : tr.find(it->name.substr(0, bar));
        f.recs.insert(f.recs.begin() + it->typeAt + 1, f.intScalar(3, "SceneTreeNodeRef", t == tr.end() ? -1 : t->second));
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
