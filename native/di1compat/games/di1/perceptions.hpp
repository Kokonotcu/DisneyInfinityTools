// GLB-1: 1.0's var/perceptions/PerceptionInfo.bin, the perceptions 3.0 lacks, as 3.0's perceptions Lua. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../formats/luaconst.hpp"

namespace di1::perceptions {

// What 3.0's PerceptionInfo already holds: perception names, and Groups records by GroupName.
struct Perc30 {
    std::function<bool(const std::string&)> has;                // 3.0 holds a perception of this name
    std::function<std::string(const std::string&)> group;       // 3.0 Groups record with this GroupName, "" if none
};

// The perceptions 3.0 lacks (37 of 250) and the records they reach, under DI1_ names (perccheck.py: 213 twins).
inline bool perceptionInfoLua(const std::string& b, const Perc30& p30, std::string& out, int& added,
                              std::string* why = nullptr);

namespace detail {

// The 9 blocks after the two pools and their record sizes (1.0 ctor FUN_00f7ac10).
enum Block { kDetect, kTargetMode, kReticule, kRicochet, kPartnerCheck, kInteractionCheck, kGroups, kUiText,
             kPerceptions, kBlocks };
inline constexpr uint32_t kRecordSize[kBlocks] = {0x4c, 0x10, 0x14, 0x0c, 0x18, 0x10, 0x10, 0x14, 0x60};
constexpr uint32_t kNone = 0xffffffff;   // no string, record or list
constexpr size_t kBlockHead = 8;         // u32 size, u32 count

// Detect slots 0..17 as 3.0's keys; 3.0 inserted targetPersistence before slot 15 (210 of 210 twins agree then).
inline constexpr const char* kDetectKeys[] = {
    "AggroRadius", "PriorityOverrideThreshhold", "FocusWeight", "Distance", "AwareDistance", "nearHeight",
    "farHeight", "useCenter", "focusMethod", "FOV", "FOVNotMoving", "ellipsePercent", "ellipsePercentNotMoving",
    "lineOfSight", "targetFacing", "maxHeightAboveGround", "foundTargetIgnoresPriority", "allowRumpusTerrain"};
constexpr int kDetectFields = 18;
constexpr int kFocusMethod = 8;        // the one string slot
constexpr int kTypePriorityField = 18;


inline bool fail(std::string* why, const char* w)
{
    if (why) *why = w;
    return false;
}

inline bool truthy(const std::string& s) { return s[0] == 't' || s[0] == 'T' || s[0] == '1'; }

// The .bin: [u32 size, string pool] [u32 size, list words], then the blocks. ok: no read went out of range.
struct Bin {
    const std::string& b;
    uint32_t s1 = 0, s2 = 0;
    size_t lw = 0, lwEnd = 0;
    size_t base[kBlocks] = {};
    uint32_t n[kBlocks] = {};
    bool ok = true;

    bool u32(size_t o, uint32_t& v) const
    {
        if (o >= b.size() || b.size() - o < 4) return false;
        memcpy(&v, b.data() + o, 4);
        return true;
    }
    uint32_t field(int blk, uint32_t i, int f)
    {
        uint32_t v = kNone;
        ok &= u32(base[blk] + (size_t)i * kRecordSize[blk] + 4 * f, v);
        return v;
    }
    bool str(uint32_t off, std::string& s)
    {
        if (off == kNone) return false;
        size_t at = 4 + (size_t)off, e = at < s1 ? b.find('\0', at) : std::string::npos;
        if (e == std::string::npos || e >= s1) {
            ok = false;
            return false;
        }
        s.assign(b, at, e - at);
        return true;
    }
    // list word i, inside the list pool
    bool word(size_t i, uint32_t& v)
    {
        if (!bytes::fits((lwEnd - lw) / 4, i, 1)) {
            ok = false;
            return false;
        }
        return u32(lw + 4 * i, v);
    }
    // a plain list (FUN_00f6bf10): words from i up to -1
    void words(uint32_t i, std::vector<uint32_t>& w)
    {
        w.clear();
        uint32_t v;
        if (i != kNone)
            for (size_t k = i; word(k, v) && v != kNone; ++k) w.push_back(v);
    }
    std::string num(uint32_t off)
    {
        std::string s;
        char f[32];
        snprintf(f, sizeof f, "%.9g", str(off, s) ? atof(s.c_str()) : 0.0);
        return f;
    }
    std::string flag(uint32_t off)
    {
        std::string s;
        return str(off, s) && truthy(s) ? "1" : "0";
    }
    std::string inum(uint32_t off)
    {
        std::string s;
        return std::to_string(str(off, s) ? atoi(s.c_str()) : 0);
    }
};

// The two pool headers and the 9 block headers; nullptr, or why they do not fit the file.
inline const char* parseBlocks(Bin& f)
{
    if (!f.u32(0, f.s1) || f.s1 < 4 || !f.u32(f.s1, f.s2)) return "pool headers";
    if (f.s2 < 4 || (uint64_t)f.s1 + f.s2 > f.b.size()) return "pool headers";
    f.lw = f.s1 + 4;
    f.lwEnd = (size_t)f.s1 + f.s2;
    size_t o = f.lwEnd;
    for (int k = 0; k < kBlocks; ++k) {
        uint32_t size;
        if (!f.u32(o, size) || !f.u32(o + 4, f.n[k])) return "record blocks";
        if (size != kBlockHead + (uint64_t)f.n[k] * kRecordSize[k] || (uint64_t)o + size > f.b.size())
            return "record blocks";
        f.base[k] = o + kBlockHead;
        o += size;
    }
    return o != f.b.size() ? "trailing bytes" : nullptr;
}

// The perceptions 3.0 lacks and every record they reach: type and state priority lists too.
struct Reach {
    std::vector<uint32_t> per;
    std::set<uint32_t> use[kBlocks], typePriority, statePriority;
};

// Perception fields that name a record, and fields that name a list of records.
struct Ref {
    int field, block;
};
inline constexpr Ref kRecordRefs[] = {{2, kGroups}, {18, kDetect}, {19, kTargetMode}, {21, kReticule}, {22, kRicochet}};
inline constexpr Ref kListRefs[] = {{9, kPartnerCheck}, {14, kInteractionCheck}, {20, kUiText}};

// Perception i's references into r; nullptr, or why one points past its block.
inline const char* reachPerception(Bin& f, uint32_t i, Reach& r)
{
    for (const Ref& x : kRecordRefs) {
        uint32_t v = f.field(kPerceptions, i, x.field);
        if (v == kNone) continue;
        if (v >= f.n[x.block]) return "record index";
        r.use[x.block].insert(v);
    }
    std::vector<uint32_t> w;
    for (const Ref& x : kListRefs) {
        f.words(f.field(kPerceptions, i, x.field), w);
        for (uint32_t v : w) {
            if (v >= f.n[x.block]) return "list record index";
            r.use[x.block].insert(v);
        }
    }
    return nullptr;
}

// A Detect record's TypePriority list (FUN_00f76830: flag, then type, value, isRef); refs name state lists.
inline void reachDetect(Bin& f, uint32_t d, Reach& r)
{
    uint32_t t = f.field(kDetect, d, kTypePriorityField), v;
    if (t == kNone) return;
    r.typePriority.insert(t);
    for (size_t k = (size_t)t + 1; f.word(k, v) && v != kNone; k += 3) {
        uint32_t val, isRef;
        if (!f.word(k + 1, val) || !f.word(k + 2, isRef)) break;
        if (f.flag(isRef) == "1") r.statePriority.insert(val);
    }
}

inline const char* reachRecords(Bin& f, const Perc30& p30, Reach& r)
{
    for (uint32_t i = 0; i < f.n[kPerceptions]; ++i) {
        std::string s;
        if (!f.str(f.field(kPerceptions, i, 0), s)) return f.ok ? "perception without a name" : "string offset";
        if (p30.has(s)) continue;
        r.per.push_back(i);
        if (const char* bad = reachPerception(f, i, r)) return bad;
    }
    for (uint32_t d : r.use[kDetect]) reachDetect(f, d, r);
    return f.ok ? nullptr : "offset out of range";
}

inline std::string name(const char* pre, uint32_t i) { return std::string(pre) + std::to_string(i); }

// The Lua writer: one key per line, records under their DI1_ names.
struct Emit {
    Bin& f;
    std::string& out;

    void kraw(const char* key, const std::string& v) { out += std::string("\t\t\t") + key + " = " + v + ",\n"; }
    void kv(const char* key, uint32_t off)
    {
        std::string s;
        if (f.str(off, s)) kraw(key, luaconst::quoteAscii(s));
    }
    void kn(const char* key, uint32_t off)
    {
        if (off != kNone) kraw(key, f.num(off));
    }
    void kb(const char* key, uint32_t off)
    {
        if (off != kNone) kraw(key, f.flag(off));
    }
    void ki(const char* key, uint32_t off)
    {
        if (off != kNone) kraw(key, f.inum(off));
    }
    void refs(const char* key, uint32_t list, const char* pre)
    {
        std::vector<uint32_t> w;
        f.words(list, w);
        std::string v;
        for (uint32_t x : w) v += (v.empty() ? "" : ",") + name(pre, x);
        if (!v.empty()) kraw(key, luaconst::quoteAscii(v));
    }
    void section(const char* sec) { out += std::string("\t") + sec + " =\n\t{\n"; }
    void rec(const std::string& key) { out += "\t\t[" + luaconst::quoteAscii(key) + "] =\n\t\t{\n"; }
    void endRec() { out += "\t\t},\n"; }
    void endSection() { out += "\t},\n"; }
};

// State priority lists (FUN_00f70300: flag, then state, value pairs).
inline void statePriorities(Emit& e, const Reach& r)
{
    e.section("StatePriority");
    for (uint32_t l : r.statePriority) {
        e.rec(name("DI1_SP", l));
        uint32_t st, v;
        for (size_t k = (size_t)l + 1; e.f.word(k, st) && st != kNone && e.f.word(k + 1, v); k += 2) {
            std::string s;
            if (!e.f.str(st, s)) continue;
            const std::string key = "[" + luaconst::quoteAscii(s) + "]";
            e.kraw(key.c_str(), e.f.num(v));
        }
        e.endRec();
    }
    e.endSection();
}

// Type priority lists: a value is a number, or a state list's name when isRef.
inline void typePriorities(Emit& e, const Reach& r)
{
    e.section("TypePriority");
    for (uint32_t l : r.typePriority) {
        e.rec(name("DI1_TP", l));
        for (size_t k = (size_t)l + 1;; k += 3) {
            uint32_t ty, v, isRef;
            if (!e.f.word(k, ty) || ty == kNone) break;
            if (!e.f.word(k + 1, v) || !e.f.word(k + 2, isRef)) break;
            std::string s;
            if (!e.f.str(ty, s)) continue;
            const std::string key = "[" + luaconst::quoteAscii(s) + "]";
            e.kraw(key.c_str(), e.f.flag(isRef) == "1" ? luaconst::quoteAscii(name("DI1_SP", v)) : e.f.num(v));
        }
        e.endRec();
    }
    e.endSection();
}

inline void detects(Emit& e, const Reach& r)
{
    e.section("Detect");
    for (uint32_t d : r.use[kDetect]) {
        e.rec(name("DI1_D", d));
        for (int k = 0; k < kDetectFields; ++k) {
            uint32_t v = e.f.field(kDetect, d, k);
            const bool boolean = k == 7 || k == 13 || k >= 16;
            if (k == kFocusMethod) e.kv(kDetectKeys[k], v);
            else if (boolean) e.kb(kDetectKeys[k], v);
            else e.kn(kDetectKeys[k], v);
        }
        if (e.f.field(kDetect, d, kTypePriorityField) != kNone)
            e.kraw("TypePriority", luaconst::quoteAscii(name("DI1_TP", e.f.field(kDetect, d, kTypePriorityField))));
        e.endRec();
    }
    e.endSection();
}

// TargetMode, Reticule, Ricochet, InteractionCheck and UIText: one writer call per slot, in slot order.
inline void targetModes(Emit& e, const Reach& r)
{
    e.section("TargetMode");
    for (uint32_t t : r.use[kTargetMode]) {
        e.rec(name("DI1_TM", t));
        e.kn("Distance", e.f.field(kTargetMode, t, 0));
        e.kn("Distance2Radius", e.f.field(kTargetMode, t, 1));
        e.kb("lineOfSight", e.f.field(kTargetMode, t, 2));
        e.kb("allowRumpusTerrain", e.f.field(kTargetMode, t, 3));
        e.endRec();
    }
    e.endSection();
}

inline void reticules(Emit& e, const Reach& r)
{
    e.section("Reticule");
    for (uint32_t t : r.use[kReticule]) {   // ponytail: floats 2..4 unmapped (no proven 3.0 key)
        e.rec(name("DI1_R", t));
        e.kv("ChoName", e.f.field(kReticule, t, 0));
        e.kv("Anchor", e.f.field(kReticule, t, 1));
        e.endRec();
    }
    e.endSection();
}

inline void ricochets(Emit& e, const Reach& r)
{
    e.section("Ricochet");
    for (uint32_t t : r.use[kRicochet]) {
        e.rec(name("DI1_RC", t));
        e.ki("MaxRicochets", e.f.field(kRicochet, t, 0));
        e.ki("NumRequiredNetworkedHandles", e.f.field(kRicochet, t, 1));
        e.kv("Perception", e.f.field(kRicochet, t, 2));
        e.endRec();
    }
    e.endSection();
}

inline constexpr const char* kPartnerKeys[] = {"Interaction", "Initiator", "PartnerMustBeOfThisType",
                                               "PartnerMustHaveAllTheseStates", "PartnerMustHaveAnyOfTheseStates",
                                               "PartnerCannotHaveAnyOfTheseStates"};
constexpr int kInitiator = 1;   // the one boolean slot

inline void partnerChecks(Emit& e, const Reach& r)
{
    e.section("PartnerCheck");
    for (uint32_t t : r.use[kPartnerCheck]) {
        e.rec(name("DI1_PC", t));
        for (int k = 0; k < (int)std::size(kPartnerKeys); ++k) {
            if (k == kInitiator) e.kb(kPartnerKeys[k], e.f.field(kPartnerCheck, t, k));
            else e.kv(kPartnerKeys[k], e.f.field(kPartnerCheck, t, k));
        }
        e.endRec();
    }
    e.endSection();
}

inline void interactionChecks(Emit& e, const Reach& r)
{
    e.section("InteractionCheck");
    for (uint32_t t : r.use[kInteractionCheck]) {
        e.rec(name("DI1_IC", t));
        e.kv("TargetActorType", e.f.field(kInteractionCheck, t, 0));
        e.kv("Interaction", e.f.field(kInteractionCheck, t, 1));
        e.kv("InteractionActorType", e.f.field(kInteractionCheck, t, 2));
        e.kb("InteractionCentric", e.f.field(kInteractionCheck, t, 3));
        e.endRec();
    }
    e.endSection();
}

// A 1.0 group is the 3.0 group of its GroupName (all 31 exist); grp gets each group's record name.
inline void groups(Emit& e, const Reach& r, const Perc30& p30, std::vector<std::string>& grp)
{
    e.section("Groups");
    grp.assign(e.f.n[kGroups], std::string());
    for (uint32_t g : r.use[kGroups]) {
        std::string s;
        if (e.f.str(e.f.field(kGroups, g, 0), s)) grp[g] = p30.group(s);
        if (!grp[g].empty()) continue;
        grp[g] = name("DI1_G", g);
        e.rec(grp[g]);
        e.kv("GroupName", e.f.field(kGroups, g, 0));
        e.ki("MaxTargets", e.f.field(kGroups, g, 2));
        e.kb("OnePerceptionAllowed", e.f.field(kGroups, g, 3));
        e.endRec();
    }
    e.endSection();
}

inline void uiTexts(Emit& e, const Reach& r)
{
    e.section("UIText");
    for (uint32_t t : r.use[kUiText]) {
        e.rec(name("DI1_U", t));
        e.kv("Text", e.f.field(kUiText, t, 0));
        e.kv("Button", e.f.field(kUiText, t, 1));
        e.ki("Priority", e.f.field(kUiText, t, 2));
        e.kn("Timer", e.f.field(kUiText, t, 3));
        e.kv("WiiMotebutton", e.f.field(kUiText, t, 4));
        e.endRec();
    }
    e.endSection();
}

// Perception slots written as values ('b' 0/1, 'f' number, 's' string) and as record names.
struct ValueSlot {
    int field;
    const char* key;
    char kind;
};
inline constexpr ValueSlot kPerceptionValues[] = {
    {3, "FromTrigger", 'b'}, {4, "AutoEliminateTargets", 'b'}, {5, "PerceiverMustBeOfThisType", 's'},
    {6, "PerceiverMustHaveAllTheseStates", 's'}, {7, "PerceiverMustHaveAnyOfTheseStates", 's'},
    {8, "PerceiverCannotHaveAnyOfTheseStates", 's'}, {10, "TargetMustBeOfThisType", 's'},
    {11, "TargetMustHaveAllTheseStates", 's'}, {12, "TargetMustHaveAnyOfTheseStates", 's'},
    {13, "TargetCannotHaveAnyOfTheseStates", 's'}, {15, "PerceiveRider", 'b'}, {16, "BasePriority", 'f'},
    {17, "AllowInTargetMode", 'b'}};
struct RecordSlot {
    int field;
    const char *key, *pre;
};
inline constexpr RecordSlot kPerceptionRecords[] = {
    {18, "Detect", "DI1_D"}, {19, "TargetMode", "DI1_TM"}, {21, "Reticule", "DI1_R"}, {22, "Ricochet", "DI1_RC"}};
constexpr int kControlContext = 23;   // a list of strings, written comma-joined

// One perception 3.0 lacks; reachRecords proved its name and record indices.
inline void perception(Emit& e, uint32_t i, const std::vector<std::string>& grp)
{
    std::string s;
    e.f.str(e.f.field(kPerceptions, i, 0), s);
    e.rec(s);
    uint32_t g = e.f.field(kPerceptions, i, 2);
    if (g != kNone) e.kraw("Groups", luaconst::quoteAscii(grp[g]));
    for (const ValueSlot& x : kPerceptionValues) {
        uint32_t v = e.f.field(kPerceptions, i, x.field);
        if (x.kind == 'b') e.kb(x.key, v);
        else if (x.kind == 'f') e.kn(x.key, v);
        else e.kv(x.key, v);
    }
    for (const RecordSlot& x : kPerceptionRecords)
        if (e.f.field(kPerceptions, i, x.field) != kNone)
            e.kraw(x.key, luaconst::quoteAscii(name(x.pre, e.f.field(kPerceptions, i, x.field))));
    e.refs("PartnerCheck", e.f.field(kPerceptions, i, 9), "DI1_PC");
    e.refs("InteractionCheck", e.f.field(kPerceptions, i, 14), "DI1_IC");
    e.refs("UIText", e.f.field(kPerceptions, i, 20), "DI1_U");
    std::vector<uint32_t> w;
    e.f.words(e.f.field(kPerceptions, i, kControlContext), w);
    std::string cc, c;
    for (uint32_t x : w)
        if (e.f.str(x, c)) cc += (cc.empty() ? "" : ",") + c;
    if (!cc.empty()) e.kraw("ControlContext", luaconst::quoteAscii(cc));
    e.endRec();
}

}  // namespace detail

inline bool perceptionInfoLua(const std::string& b, const Perc30& p30, std::string& out, int& added, std::string* why)
{
    added = 0;
    detail::Bin f{b};
    if (const char* bad = detail::parseBlocks(f)) return detail::fail(why, bad);
    detail::Reach r;
    if (const char* bad = detail::reachRecords(f, p30, r)) return detail::fail(why, bad);
    out = "-- 1.0 var/perceptions/PerceptionInfo.bin: the perceptions 3.0 lacks, in the form of 3.0's perceptions"
          " loader (di1compat sys.hpp)\nPerceptionInfo =\n{\n";
    detail::Emit e{f, out};
    detail::statePriorities(e, r);
    detail::typePriorities(e, r);
    detail::detects(e, r);
    detail::targetModes(e, r);
    detail::reticules(e, r);
    detail::ricochets(e, r);
    detail::partnerChecks(e, r);
    detail::interactionChecks(e, r);
    std::vector<std::string> grp;
    detail::groups(e, r, p30, grp);
    detail::uiTexts(e, r);
    e.section("Perceptions");
    for (uint32_t i : r.per) {
        detail::perception(e, i, grp);
        ++added;
    }
    out += "\t},\n}\n";
    return f.ok || detail::fail(why, "offset out of range");
}

}  // namespace di1::perceptions
