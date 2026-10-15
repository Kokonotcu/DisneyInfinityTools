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
#include <map>
#include <set>
#include "octx.hpp"
#include "bent.hpp"
#include "ports10.hpp"

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

// Returns the DataNodes converted; *subnets = SubNetworks touched.
inline int anim30(oct::File& f, int* subnets = nullptr)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    std::vector<size_t> sns;
    for (size_t i = pool + 1, e = f.blockEnd(pool); i < e; ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") sns.push_back(i);
    int n = 0, touched = 0;
    for (auto it = sns.rbegin(); it != sns.rend(); ++it) {   // last first: edits keep earlier indices
        size_t sn = *it, end = f.blockEnd(sn);
        long hs = -1, hsi = -1, hl = -1, dp = -1, pp = -1;
        std::string nb;
        for (size_t i = sn + 1; i < end; ++i) {
            if (f.recs[i].level != 3) continue;
            const std::string& k = f.name(f.recs[i]);
            if (k == "HeaderStrings") hs = (long)i;
            else if (k == "HeaderStringIndices") hsi = (long)i;
            else if (k == "HeaderLayout") hl = (long)i;
            else if (k == "DataNodePool") dp = (long)i;
            else if (k == "ProcessorNodePool") pp = (long)i;
            else if (k == "NetworkBuffer") nb = f.recs[i].pay.substr(oct::spec(f.recs[i].type).cw);
        }
        if (hs < 0 || hsi < 0 || hl < 0 || dp < 0 || pp < 0) continue;
        oct::Spec ss = oct::spec(f.recs[hs].type);
        if (ss.kind != oct::NAME || ss.keyed) continue;
        std::vector<std::string> strs = f.nameValues(f.recs[hs]);
        std::vector<int32_t> idx = f.ints(f.recs[hsi]), lay = f.ints(f.recs[hl]);
        auto at = std::find(strs.begin(), strs.end(), "Global.Time");
        if (at == strs.end() || std::count(strs.begin(), strs.end(), "AnimState")) continue;
        int32_t s = (int32_t)(at - strs.begin());
        if (std::count(idx.begin(), idx.end(), s) != 1) continue;   // the string names another header too
        // HeaderLayout: (parent header, child count) pairs; the children take the next HeaderStringIndices and are
        // numbered from 1 (0 = root)
        int h = -1;
        size_t k = 0;
        for (size_t p = 0; p + 1 < lay.size(); p += 2)
            for (int32_t c = 0; c < lay[p + 1] && k < idx.size(); ++c, ++k)
                if (lay[p] == 0 && idx[k] == s) h = (int)k + 1;
        if (h < 0) continue;
        // child of a node (level + 1) by name, -1 if none
        auto kid = [&](size_t i, const char* nm) -> long {
            for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j)
                if (f.recs[j].level == f.recs[i].level + 1 && f.name(f.recs[j]) == nm) return (long)j;
            return -1;
        };
        auto str1 = [&](long r) { auto v = r < 0 ? std::vector<std::string>{} : f.nameValues(f.recs[r]); return v.empty() ? std::string() : v[0]; };
        std::vector<size_t> dns;
        for (size_t i = dp + 1, e = f.blockEnd(dp); i < e; ++i)
            if (f.recs[i].level == 4) dns.push_back(i);
        // the time range of the clips and events this DataNode drives
        auto range = [&](int d, float& t0, float& t1) {
            bool any = false;
            auto add = [&](float t) { t0 = any ? (std::min)(t0, t) : t; t1 = any ? (std::max)(t1, t) : t; any = true; };
            for (size_t i = pp + 1, e = f.blockEnd(pp); i < e; ++i) {
                if (f.recs[i].level != 4) continue;
                std::string ty = str1(kid(i, "Type"));
                long ar = kid(i, "DataNodeAttributes"), rr = kid(i, "DataNodeRefs");
                if ((ty != "ClipProcessor" && ty != "EventList") || ar < 0 || rr < 0) continue;
                auto attrs = f.nameValues(f.recs[ar]);
                auto rf = f.ints(f.recs[rr]);
                auto ref = [&](const char* a) { for (size_t q = 0; q < attrs.size() && q < rf.size(); ++q) if (attrs[q] == a) return rf[q]; return -1; };
                if (ref("Input") != d) continue;
                int src = ref(ty == "ClipProcessor" ? "ClipData" : "EventListData");
                if (src < 0 || (size_t)src >= dns.size()) continue;
                if (ty == "ClipProcessor") {
                    long b = kid(dns[src], "ClipDataBlock");
                    if (b < 0 || oct::spec(f.recs[b].type).kind != oct::BYTE) continue;
                    std::string blk = f.recs[b].pay.substr(oct::spec(f.recs[b].type).cw);
                    if (blk.size() < 8) continue;
                    float a, z;
                    memcpy(&a, &blk[0], 4);
                    memcpy(&z, &blk[4], 4);
                    add(a);
                    add(z);
                } else {
                    long o = kid(dns[src], "NetworkBufferOffset");
                    uint32_t off = (uint32_t)(o < 0 ? -1 : first(f, f.recs[o], -1)), cnt = rd(nb, off, 4);
                    for (uint32_t k2 = 0; cnt != 0xffffffffu && k2 < cnt && off + 8 + 8 * (size_t)k2 <= nb.size(); ++k2) {
                        float t;
                        memcpy(&t, &nb[off + 4 + 8 * k2], 4);
                        add(t);
                    }
                }
            }
            return any;
        };
        std::vector<std::vector<oct::Rec>> rep(dns.size());   // new children per DataNode, built before any edit
        int here = 0;
        for (size_t d = 0; d < dns.size(); ++d) {
            long hd = kid(dns[d], "Header"), ty = kid(dns[d], "Type");
            float t0 = 0, t1 = 0;
            if (hd < 0 || ty < 0 || first(f, f.recs[hd], -1) != h || str1(ty) != "Float" || !range((int)d, t0, t1)) continue;
            rep[d] = {f.recs[hd], f.nameScalar(5, f.recs[ty].name, "AnimState"), f.node(5, "Data"),
                      floatRec(f, "RangeMin", t0, 6), floatRec(f, "RangeMax", t1, 6)};
            ++here;
        }
        for (size_t d = dns.size(); d-- > 0;) {   // last first: edits keep earlier indices
            if (rep[d].empty()) continue;
            size_t i = dns[d];
            f.recs.erase(f.recs.begin() + i + 1, f.recs.begin() + f.blockEnd(i));
            f.recs.insert(f.recs.begin() + i + 1, rep[d].begin(), rep[d].end());
        }
        if (!here) continue;
        uint16_t a = f.addName("AnimState");
        memcpy(&f.recs[hs].pay[ss.cw + 2 * (size_t)s], &a, 2);
        n += here;
        ++touched;
    }
    if (subnets) *subnets = touched;
    return n;
}

// ---- constant section ----
// const30 (choconst10): the Static DataNodes of a processor SubNetwork under constant headers, as 3.0 ships them.
//   3.0 never has a variable ClipData or EventList node (processor graphs: 8,389 + 7,811 Static, 0 not; statics.py),
//   and its reader keeps them in the constant arena (FUN_0050d8d0: Static -> +0x84) under constant headers 1..CHC
//   (FUN_004fe4d0 builds them from the first ConstantLayoutCount layout ints), which instances share. octx::addCounts
//   instead made 1.0 Static nodes variable (constant nodes under variable headers were relocated into wild pointers
//   when instanced). A variable ClipData breaks instancing another way: its block buffer is a separate counted
//   allocation in the arena (FUN_00516c00 -> FUN_00412150, pointer at node+0x3c), the instance clone relocates nodes
//   only through vtable+0x28 = FUN_004cf8d0, which re-registers the node and leaves +0x3c on the template's buffer,
//   and the clone's dtor (FUN_005080f0) then deletes the template's buffer against the template arena: one count too
//   few per destroyed instance, and the arena (tracker at *(0x021B73A0), FUN_00416840) is freed while its last object
//   is still in its dtor (in-game 2026-10-14, TS_WaterSplashdown: placement node at arena+0x324, arena count 2 = its
//   two header entries; the second entry's delete freed the arena under the node -> AV 004EDDBB).
//   Rule, on every 3.0 processor SubNetwork with Static nodes (6,798/6,798, construle.py): Static nodes' headers are
//   1..CHC and no other node's / processor's; the constant headers are exactly the prefixes of the Static nodes' paths;
//   ConstantLayoutCount = the layout ints of headers 1..CHC; a Static node's path is the variable path with its root
//   prefixed 'Constant' ('ConstantANC_X/ClipData'; a root-level node keeps its name, 111 SubNetworks), and the
//   variable tree no longer has it. Pool order is not part of it (111 shipped SubNetworks interleave). Written so:
//   constant headers first (breadth first), the remaining headers after, Header fields renumbered, and a
//   ConstantHeaderCount record that octx::addCounts turns into the constant counts (it then keeps Static). A Static
//   node's variable header is dropped unless another node / processor uses it or a kept header lies below it. A
//   Transform Association whose 'Constant'+NodeName header now exists gets AltNodeName = that name, as 3.0 writes.
inline int const30(oct::File& f)
{
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return 0;
    std::vector<size_t> sns;
    for (size_t i = pool + 1, e = f.blockEnd(pool); i < e; ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") sns.push_back(i);
    int done = 0;
    std::map<int, std::set<std::string>> constPaths;   // SubNetwork ordinal -> its constant header paths
    std::map<int, std::set<std::string>> named;        // SubNetwork ordinal -> paths Associations bind ('a/b')
    long ap0 = f.pool("AssociationPool");
    for (size_t i = ap0 < 0 ? 0 : ap0 + 1, e = ap0 < 0 ? 0 : f.blockEnd(ap0); i < e; i = f.blockEnd(i)) {
        std::string node;
        int snRef = -1;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) {
            const std::string& k = f.name(f.recs[j]);
            if (k == "NodeName") { auto v = f.nameValues(f.recs[j]); if (!v.empty()) node = v[0]; }
            else if (k == "SubNetworkRef") snRef = first(f, f.recs[j], -1);
        }
        std::replace(node.begin(), node.end(), '|', '/');
        if (snRef >= 0 && !node.empty()) named[snRef].insert(node);
    }
    for (auto it = sns.rbegin(); it != sns.rend(); ++it) {   // last first: the one inserted record keeps earlier indices
        size_t sn = *it, end = f.blockEnd(sn);
        int ordinal = (int)(sns.rend() - it) - 1;
        long hs = -1, hsi = -1, hl = -1, after = -1;
        bool counted = false;
        struct D { long hd = -1, st = -1; bool stat = false, ext = false; std::string type; };
        std::vector<D> dns;                      // DataNode Header / Static records, Static, Type
        std::vector<long> pn;                    // ProcessorNode Header records
        struct P { std::string type; std::vector<std::string> attrs; std::vector<int32_t> refs; };
        std::vector<P> procs;
        std::set<int> convRefs;                  // BasisConversion DataNodeRefs
        std::string pool3;
        for (size_t i = sn + 1; i < end; ++i) {
            const oct::Rec& r = f.recs[i];
            const std::string& k = f.name(r);
            if (r.level == 3) {
                pool3 = k;
                if (k == "HeaderStrings") hs = (long)i;
                else if (k == "HeaderStringIndices") hsi = (long)i;
                else if (k == "HeaderLayout") hl = (long)i;
                else if (k == "VariableHeaderCount" || k == "ConstantHeaderCount") counted = true;
                else if (k == "Type" || k == "Name") after = (std::max)(after, (long)i);
            } else if (r.level == 4 && pool3 == "DataNodePool") dns.emplace_back();
            else if (r.level == 4 && pool3 == "ProcessorNodePool") { pn.push_back(-1); procs.emplace_back(); }
            else if (r.level == 5 && pool3 == "DataNodePool" && !dns.empty()) {
                if (k == "Header") dns.back().hd = (long)i;
                else if (k == "Static") { dns.back().st = (long)i; dns.back().stat = first(f, r, 0) != 0; }
                else if (k == "External") dns.back().ext = first(f, r, 0) != 0;
                else if (k == "Type") { auto v = f.nameValues(r); if (!v.empty()) dns.back().type = v[0]; }
            } else if (r.level == 5 && pool3 == "ProcessorNodePool" && !pn.empty()) {
                if (k == "Header") pn.back() = (long)i;
                else if (k == "Type") { auto v = f.nameValues(r); if (!v.empty()) procs.back().type = v[0]; }
                else if (k == "DataNodeAttributes") procs.back().attrs = f.nameValues(r);
                else if (k == "DataNodeRefs") procs.back().refs = f.ints(r);
            } else if (r.level == 5 && pool3 == "BasisConversionPool" && k == "DataNodeRef") convRefs.insert(first(f, r, -1));
            if (r.level == 3 && k == "ConnectionMapUsedDataNodes")
                for (int32_t d : f.ints(r)) convRefs.insert(d);   // same treatment: must stay variable
        }
        if (counted || hs < 0 || hsi < 0 || hl < 0 || after < 0 || pn.empty()) continue;
        if (oct::spec(f.recs[hs].type).kind != oct::NAME || oct::spec(f.recs[hs].type).keyed) continue;
        std::vector<std::string> strs = f.nameValues(f.recs[hs]);
        std::vector<int32_t> idx = f.ints(f.recs[hsi]), lay = f.ints(f.recs[hl]);
        size_t H = idx.size();
        std::vector<int> parent(H + 1, -1);
        std::vector<std::string> name(H + 1);
        std::vector<std::vector<int>> kids(H + 1);
        size_t k = 0;
        bool ok = true;
        for (size_t p = 0; p + 1 < lay.size() && ok; p += 2)
            for (int32_t c = 0; c < lay[p + 1]; ++c) {
                if (++k > H || lay[p] < 0 || lay[p] >= (int)k || idx[k - 1] < 0 || idx[k - 1] >= (int)strs.size()) { ok = false; break; }
                parent[k] = lay[p];
                name[k] = strs[idx[k - 1]];
                kids[lay[p]].push_back((int)k);
            }
        if (!ok || k != H) continue;
        auto hdr = [&](long rec) { int h = rec < 0 ? -1 : first(f, f.recs[rec], -1); return h >= 1 && h <= (int)H ? h : -1; };
        auto pathOf = [&](int h) {
            std::string s;
            for (int x = h; x > 0; x = parent[x]) s = s.empty() ? name[x] : name[x] + "/" + s;
            return s;
        };
        // Only shapes 3.0 ships stay Static (staticflags.py, processor graphs); the other Static nodes become variable:
        //  - types 3.0 never keeps Static: Bool (3,743), Visibility (37,917), Int (4) all variable;
        //  - External ones: 3.0 has no Static External node but ClipData (1.0: 1,451 Static External Floats);
        //  - nodes listed in ConnectionMapUsedDataNodes (3.0: 0 Static; 1.0: 439, all Static External Floats). The
        //    reader puts those nodes in a NodeHandleArray in the variable arena (FUN_00500ac0 reads the list, hash
        //    0x2cb3247b at 00500ecb, -> FUN_004e25d0), which an instance clones and relocates by the instance offset
        //    (FUN_004ff0e0 -> NodeHandleArray vtable+0xc = FUN_004cf950 -> FUN_0049c5e0): a constant node there
        //    becomes a wild pointer (in-game 2026-10-14 02:03, inc_dock_explode instanced at IN_City load: AV writing
        //    00240028 under FUN_004cf950; its list holds the Static External Floats ScaleMultiplier / GrowShrink);
        //  - nodes an Association binds (it writes them: the Surge ticker's 'InputSurge' Bool, 1.0 Static, 3.0
        //    variable + Peeked);
        //  - BasisConversion DataNodeRefs (3.0: never Static; 1.0: 11; same container relocation risk);
        //  - nodes a processor takes through an input 3.0 never feeds from a Static node (FloatConnection / FloatNegate
        //    / FloatMultiply Input, RelationshipProcessor Input, ClipProcessor Input, RadialBasisOrientation ...;
        //    staticrefs.py: the 3.0 list below is every processor attribute it ships with a Static node);
        //  - nodes whose header has a variable node / processor on or below it (3.0: 0 of 17,981 Static nodes;
        //    1.0: 11), repeated until none is left.
        static const std::set<std::string> staticInputs30 = {
            "AimConstraintProcessorNode.AlignReference", "AimConstraintProcessorNode.Offset", "AimConstraintProcessorNode.UpReference",
            "ClipProcessor.ClipData", "DilationOrientationConnection.RotateOffset", "DilationTranslationConnection.TranslateOffset",
            "DilationTranslationOrientationConnection.RotateOffset", "DilationTranslationOrientationConnection.TranslateOffset",
            "EventList.EventListData", "EventList.ReverseEventListData",
            "IK2BRPLengthSquashZConstraint.InputDistanceThreshold", "IK2BRPLengthSquashZConstraint.InputEndInitialPosition",
            "IK2BRPLengthSquashZConstraint.InputMidInitialPosition", "IK2BRPLengthSquashZConstraint.InputMidLocalToParentInitialOrientation",
            "IK2BRPLengthSquashZConstraint.InputMidLocalToStartParentInitialOrientation",
            "IK2BRPLengthSquashZConstraint.InputStartInitialPosition", "IK2BRPLengthSquashZConstraint.InputStartLocalToParentInitialOrientation",
            "IK2BRPLengthSquashZConstraint.InputStartLocalToStartParentInitialOrientation", "IkLean2LinkProcessor.Axis",
            "MotionPathOrientationProcessor.Spline", "MotionPathPositionProcessor.Spline", "SplineInputProcessor.Spline",
            "Position3dOffsetProcessor.Offset", "Position3dWeightedAverageProcessor.TargetWeight", "RadialBasisApplyFloat.Neutral",
            "RadialBasisApplyFloat.Weights", "RadialBasisPosition.ControllerKeys", "RadialBasisPosition.StandardDeviationValues",
            "RelationshipProcessor.RelationshipData", "TransformTranslationOrientationConnection.RotateOffset",
            "TransformTranslationOrientationScaleConnection.TranslateOffset", "UnitRotor3dOffsetProcessor.Offset",
            "UnitRotor3dWeightedAverageProcessor.TargetWeight"};
        std::vector<bool> offList(dns.size(), false);
        for (auto& pr : procs)
            for (size_t q = 0; q < pr.attrs.size() && q < pr.refs.size(); ++q) {
                std::string a = pr.attrs[q].substr(0, pr.attrs[q].find('['));
                if (pr.refs[q] >= 0 && (size_t)pr.refs[q] < dns.size() && !staticInputs30.count(pr.type + "." + a)) offList[pr.refs[q]] = true;
            }
        bool keepAny = false;
        for (size_t di = 0; di < dns.size(); ++di) {
            auto& d = dns[di];
            if (!d.stat) continue;
            int h = hdr(d.hd);
            if (d.type == "Bool" || d.type == "Int" || d.type == "Visibility" || (d.ext && d.type != "ClipData") ||
                (h > 0 && named[ordinal].count(pathOf(h))) || convRefs.count((int)di) || offList[di]) {
                f.setInt(f.recs[d.st], 0);
                d.stat = false;
            }
        }
        for (bool again = true; again;) {
            again = false;
            std::vector<std::string> var;
            for (auto& d : dns) if (!d.stat && hdr(d.hd) > 0) var.push_back(pathOf(hdr(d.hd)));
            for (long r : pn) if (hdr(r) > 0) var.push_back(pathOf(hdr(r)));
            for (auto& d : dns) {
                if (!d.stat || hdr(d.hd) <= 0) continue;
                std::string p = pathOf(hdr(d.hd));
                bool clash = std::any_of(var.begin(), var.end(), [&](const std::string& v) {
                    return v == p || (v.size() > p.size() && v.compare(0, p.size(), p) == 0 && v[p.size()] == '/');
                });
                if (clash) { f.setInt(f.recs[d.st], 0); d.stat = false; again = true; }
            }
        }
        for (auto& d : dns) keepAny |= d.stat;
        if (!keepAny) continue;   // addCounts' variable form is then the 3.0 one
        std::set<int> moving, used;
        for (auto& d : dns) (d.stat ? moving : used).insert(hdr(d.hd));
        for (long r : pn) used.insert(hdr(r));
        moving.erase(-1);
        if (moving.empty()) continue;
        // a Static node's variable header goes when nothing else uses it and all its children went (headers are
        // numbered parents first, so children are decided first); otherwise it stays as a plain variable header
        std::vector<bool> dropped(H + 1, false);
        for (int h = (int)H; h >= 1; --h) {
            bool d = moving.count(h) && !used.count(h);
            for (int c : kids[h]) d = d && dropped[c];
            dropped[h] = d;
        }
        // constant trie: (parent, name) -> id, ids 1.. in creation order; cpar/cname per id
        std::vector<int> cpar{-1};
        std::vector<std::string> cname{""};
        std::vector<std::vector<int>> ckids(1);
        std::map<std::pair<int, std::string>, int> trie;
        std::map<int, int> constOf;   // old header of a Static node -> constant id
        for (int h : moving) {
            std::vector<std::string> comps;
            for (int x = h; x > 0; x = parent[x]) comps.insert(comps.begin(), name[x]);
            if (comps.size() > 1) comps[0] = "Constant" + comps[0];
            int at = 0;
            for (auto& c : comps) {
                auto t = trie.find({at, c});
                if (t == trie.end()) {
                    int id = (int)cpar.size();
                    cpar.push_back(at);
                    cname.push_back(c);
                    ckids.push_back({});
                    ckids[at].push_back(id);
                    t = trie.emplace(std::make_pair(at, c), id).first;
                }
                at = t->second;
            }
            constOf[h] = at;
        }
        // numbering, breadth first: a parent's children take consecutive numbers, one (parent, count) pair each
        std::vector<std::string> newName{""};
        std::vector<int32_t> newLay;
        std::vector<int> cnum(cpar.size(), 0), vnum(H + 1, 0);
        for (size_t q = 0; q < cpar.size(); ++q) {   // ids are created parents-first, so q's number exists
            int me = q == 0 ? 0 : cnum[q];
            if (ckids[q].empty()) continue;
            newLay.insert(newLay.end(), {me, (int32_t)ckids[q].size()});
            for (int c : ckids[q]) { cnum[c] = (int)newName.size(); newName.push_back(cname[c]); }
        }
        int chc = (int)newName.size() - 1;
        std::vector<int> order{0};   // variable headers breadth first from the root, kept ones only
        for (size_t q = 0; q < order.size(); ++q) {
            std::vector<int> keep;
            for (int c : kids[order[q]]) if (!dropped[c]) keep.push_back(c);
            if (keep.empty()) continue;
            newLay.insert(newLay.end(), {order[q] == 0 ? 0 : vnum[order[q]], (int32_t)keep.size()});
            for (int c : keep) { vnum[c] = (int)newName.size(); newName.push_back(name[c]); order.push_back(c); }
        }
        std::vector<int32_t> newIdx;
        for (size_t h = 1; h < newName.size(); ++h) {
            auto s = std::find(strs.begin(), strs.end(), newName[h]);
            if (s == strs.end()) { strs.push_back(newName[h]); s = strs.end() - 1; }
            newIdx.push_back((int32_t)(s - strs.begin()));
        }
        f.setInts(f.recs[hsi], newIdx);
        f.setInts(f.recs[hl], newLay);
        octx::setNames(f, f.recs[hs], strs);
        for (auto& d : dns) {
            int h = hdr(d.hd);
            if (h < 0) continue;
            f.setInt(f.recs[d.hd], d.stat ? cnum[constOf[h]] : vnum[h]);
        }
        for (long r : pn) { int h = hdr(r); if (h >= 0) f.setInt(f.recs[r], vnum[h]); }
        f.recs.insert(f.recs.begin() + after + 1, f.intScalar(3, "ConstantHeaderCount", chc));
        for (size_t q = 1; q < cpar.size(); ++q) {
            std::string pth = cname[q];
            for (int x = cpar[q]; x > 0; x = cpar[x]) pth = cname[x] + "/" + pth;
            constPaths[ordinal].insert(pth);
        }
        ++done;
    }
    // 3.0's Transform Association names the constant twin of its node as AltNodeName exactly when the SubNetwork has a
    // 'Constant'+NodeName header (9,203 of 9,204 Transform Associations with one, 0 of 24,496 without; altname.py)
    long ap = done ? f.pool("AssociationPool") : -1;
    if (ap >= 0) {
        std::vector<std::pair<size_t, std::string>> adds;   // insert position, AltNodeName
        for (size_t i = ap + 1, e = f.blockEnd(ap); i < e; i = f.blockEnd(i)) {
            if (f.recs[i].level != 2) continue;
            std::string type, node;
            int snRef = -1;
            bool alt = false;
            for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) {
                const std::string& k = f.name(f.recs[j]);
                auto v = f.nameValues(f.recs[j]);
                if (k == "Type" && !v.empty()) type = v[0];
                else if (k == "NodeName" && !v.empty()) node = v[0];
                else if (k == "SubNetworkRef") snRef = first(f, f.recs[j], -1);
                else if (k == "AltNodeName") alt = true;
            }
            std::string want = "Constant" + node;
            std::replace(want.begin(), want.end(), '|', '/');
            if (type == "Transform" && !alt && constPaths.count(snRef) && constPaths[snRef].count(want))
                adds.push_back({f.blockEnd(i), "Constant" + node});
        }
        for (auto a = adds.rbegin(); a != adds.rend(); ++a)
            f.recs.insert(f.recs.begin() + a->first, f.nameScalar(3, f.addName("AltNodeName"), a->second));
    }
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
