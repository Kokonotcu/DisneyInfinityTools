// 1.0 activity missions in 3.0's Challenge classes: graphs, mission tables, givers. lua_lex, formats/mer, luachunk.
// Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/lua_lex.hpp"
#include "../../convert/records.hpp"
#include "../../formats/luachunk.hpp"
#include "../../formats/mer.hpp"
#include "../../formats/oct.hpp"

namespace di1::activities {

// A 1.0 task class with a 3.0 counterpart. Pins bind by name in both engines (3.0 0x6c7dd0, 1.0 0x6bd670).
struct ActClass {
    const char *cls10, *cls30, *eval10, *eval30;
    std::vector<std::pair<std::string, std::string>> pins;     // "": no 3.0 pin (links dropped); "*": Participants
    std::vector<std::pair<std::string, std::string>> params;   // 3.0 keeps task parameters in the node (011b4db0)
};

// The class whose 1.0 task type is t, or null.
inline const ActClass* taskClass(const std::string& t);
// The class whose 1.0 eval type is t, or null.
inline const ActClass* evalClass(const std::string& t);
// A one-namespace 1.0 activity graph (.mer, before mer::transcode) in 3.0 Challenge classes. -> tasks, 0: unchanged
inline int activityGraph30(oct::File& f, int* rerouted = nullptr, int* dropped = nullptr);
// A 1.0 mission table (.lua) whose tasks all map: Type and parameters renamed, ChallengeZone added. -> tables, 0: none
inline int activityTasks30(std::string& lua);
// The MissionGiver tags of a missiongraph whose mission table converts (readMission(name) reads it).
inline std::set<std::string> activityGivers(const std::string& graphLua,
                                            const std::function<bool(const std::string&, std::string&)>& readMission);
// .ap giver sections (with ActivityIcon; tag in `only`, if given) get ChallengeType and BeaconType. -> sections
inline int apGivers30(std::string& ap, const std::set<std::string>* only = nullptr);
// A 1.0 DNA chunk's MissionGiverActivity gadget as 3.0's MissionGiverChallenge. -> constants renamed
inline int dnaGadgets30(const std::string& in, std::string& out);

namespace detail {
// The classes (pins30.py: 3.0 pin tables identical in all shipped graphs). CombatTimeTrial and Collector have none.
inline const std::vector<ActClass>& actClasses()
{
    static const std::vector<ActClass> m = {
        // CompleteIntro has no 3.0 input; Participants (37 links) feed an AvatarList, ParticipantsOnly (pinuse.py)
        {"MissionTask_ActivityStateMachine", "MissionTask_ChallengeStateMachine", "Eval_ActivityStateMachine",
         "Eval_ChallengeStateMachine",
         {{"StartPosition", "StartPosition"}, {"EndPosition", "EndPosition"}, {"SetupStart", "Preload"},
          {"IntroStart", "PreIntro"}, {"InProgStart", "PreInProgress"}, {"ActivityExit", "PostInProgress"},
          {"CompleteIntro", ""}, {"Participants", "*"}},
         {{"LineUpSpacing", "Spacing"}, {"EndLineUpSpacing", "EndSpacing"}}},
        // 3.0 shows its own lap and timer HUD; times go lenient to strict in both (1.0 60/50/45)
        {"MissionTask_ActivityCheckpointRace", "MissionTask_Challenge_CheckpointRace", "Eval_Activity_CheckpointRace",
         "Eval_Challenge_CheckpointRace",
         {{"CheckpointPositions", "CheckpointPositions"}, {"RacePath", "RacePath"}, {"ShowLapHUD", ""},
          {"ShowTimerHUD", ""}},
         {{"EasyTime", "BronzeTime"}, {"MediumTime", "SilverTime"}, {"HardTime", "GoldTime"}}},
    };
    return m;
}
}  // namespace detail

inline const ActClass* taskClass(const std::string& t)
{
    for (auto& c : detail::actClasses()) if (t == c.cls10) return &c;
    return nullptr;
}

inline const ActClass* evalClass(const std::string& t)
{
    for (auto& c : detail::actClasses()) if (t == c.eval10) return &c;
    return nullptr;
}

namespace detail {

constexpr char kWho[] = "gameplay";   // the tag of their damaged-input errors (B1)
inline std::string firstName(const oct::File& f, const oct::Rec& r)
{
    return octx::firstOrThrow(f.nameValues(r), f, r, kWho);
}
inline int32_t firstInt(const oct::File& f, const oct::Rec& r) { return octx::firstOrThrow(f.ints(r), f, r, kWho); }

using Pin = std::pair<std::string, std::string>;   // 1.0 pin -> 3.0 pin ("" none, "*" the participant list)

// One graph node: its record range and the name of its first Type field.
struct ActNode {
    size_t at, end;
    std::string type;
};

// activityGraph30's work: the graph's pools and nodes, its pin tables, the link edits and their counts.
struct ActGraph {
    oct::File& f;
    long pool = -1, lpool = -1;                                 // MeridianNodePool, MeridianLinkPool
    uint16_t nl = 0, fl = 0;                                    // node and field record levels
    std::vector<ActNode> nodes;
    std::map<std::string, std::map<std::string, int>> pinIdx;   // task class -> pin name -> index
    size_t newIdx = 0;                                          // the AvatarList's index: NameSpaceEnd's
    std::map<size_t, int> dIn, dOut;                            // links dropped, per node
    int nRe = 0, nDrop = 0;                                     // links rerouted to the AvatarList; dropped
    std::string smUuid;                                         // the state machine's Uuid payload
};

inline void collectNodes(ActGraph& g)
{
    oct::File& f = g.f;
    for (size_t i = g.pool + 1, e = f.blockEnd(g.pool); i < e; i = f.blockEnd(i)) {
        if (f.recs[i].level != g.nl) continue;
        ActNode n{i, f.blockEnd(i), ""};
        for (size_t j = i + 1; j < n.end; ++j) {
            if (f.recs[j].level != g.fl || f.name(f.recs[j]) != "Type") continue;
            n.type = firstName(f, f.recs[j]);
            break;
        }
        g.nodes.push_back(n);
    }
}

// The activity task nodes; 0 when one has no 3.0 class or the graph is not exactly one namespace.
inline int mappedNodes(const ActGraph& g)
{
    int begins = 0, mapped = 0;
    for (auto& n : g.nodes) {
        begins += n.type == "NameSpaceBegin";
        if (n.type.rfind("MissionTask_Activity", 0) != 0) continue;
        if (!taskClass(n.type)) return 0;
        ++mapped;
    }
    if (!mapped || begins != 1 || g.nodes.back().type != "NameSpaceEnd") return 0;
    return mapped;
}

inline bool isIntScalar(const oct::Rec& r)
{
    oct::Spec s = oct::spec(r.type);
    return s.shape == oct::SCALAR && s.kind == oct::INT && !s.keyed;
}

// The pin indices node n of class c lists; a class's first instance wins (1.0 writes one table per layout).
inline void notePins(ActGraph& g, const ActNode& n, const ActClass& c)
{
    for (size_t j = n.at + 1; j < n.end; ++j) {
        const oct::Rec& r = g.f.recs[j];
        if (r.level != g.fl || !isIntScalar(r)) continue;
        for (auto& p : c.pins)
            if (g.f.name(r) == p.first) g.pinIdx[n.type].emplace(p.first, g.f.ints(r)[0]);
    }
}

inline const Pin* pinOf(ActGraph& g, const std::string& cls, int idx)
{
    const ActClass* c = taskClass(cls);
    for (auto& p : c->pins) {
        auto it = g.pinIdx[cls].find(p.first);
        if (it != g.pinIdx[cls].end() && it->second == idx) return &p;
    }
    return nullptr;
}

// The pin at index idx of a link end's node; null unless that node is an activity task.
inline const Pin* endPin(ActGraph& g, size_t node, int idx)
{
    if (node >= g.nodes.size() || !taskClass(g.nodes[node].type)) return nullptr;
    return pinOf(g, g.nodes[node].type, idx);
}

// Whether link pool record i is a LinksBuffer's Data: the nearest Type above it at its level says so.
inline bool inLinksBuffer(const ActGraph& g, size_t i)
{
    const oct::File& f = g.f;
    const uint16_t level = f.recs[i].level;
    for (size_t j = i; j-- > (size_t)g.lpool + 1 && f.recs[j].level >= level;) {
        if (f.recs[j].level != level || f.name(f.recs[j]) != "Type") continue;
        return firstName(f, f.recs[j]) == "LinksBuffer";
    }
    return false;
}

// Links in the index form: u32 count, u8 0, the u16 codes, then u16 from / to pairs (FORMATS.md 5.3).
struct LinkRows {
    std::vector<uint16_t> code, from, to;
};

// Record r's Data as the index form: count and rows, the count width by the byte size.
inline void writeLinks(oct::Rec& r, const LinkRows& rows)
{
    const uint32_t count = (uint32_t)rows.code.size();
    std::string o((const char*)&count, 4);
    o.push_back('\0');
    for (uint16_t v : rows.code) o.append((const char*)&v, 2);
    for (size_t k = 0; k < count; ++k) {
        o.append((const char*)&rows.from[k], 2);
        o.append((const char*)&rows.to[k], 2);
    }
    r.pay.clear();
    const int cw = oct::countWidth(o.size());
    r.type = oct::makeType(oct::SCALAR, 0, oct::BYTE, cw);
    oct::File::put(r.pay, (uint32_t)o.size(), cw);
    r.pay += o;
}

// A LinksBuffer without the links on pins 3.0 lacks; Participants links leave the new AvatarList's List.
inline void rewriteLinkBuffer(ActGraph& g, oct::Rec& r)
{
    const std::optional<::mer::IndexLinks> b = ::mer::indexLinks(r);
    if (!b || b->uu || !::mer::sizeMatches(*b)) return;   // the index form: all 1.0 graphs (linkcodec.py)
    const std::map<std::string, size_t> noUuids;   // the index form names nodes by index
    LinkRows kept;
    for (uint32_t k = 0; k < b->n; ++k) {
        const ::mer::Link L = *::mer::linkAt(*b, k, noUuids);
        const uint16_t from = (uint16_t)L.from, to = (uint16_t)L.to;
        const Pin* pa = endPin(g, from, L.code >> ::mer::kFromPinShift);
        const Pin* pz = endPin(g, to, (L.code >> ::mer::kToPinShift) & ::mer::kPinMask);
        const bool fromNone = pa && pa->second.empty();
        const bool toNone = pz && pz->second.empty();
        const bool toList = pz && pz->second == "*";
        if (fromNone || toNone || toList) {
            ++g.dOut[from];
            ++g.dIn[to];
            ++g.nDrop;
            continue;
        }
        const bool fromList = pa && pa->second == "*";
        if (fromList) ++g.dOut[from];
        if (fromList) ++g.nRe;
        kept.code.push_back(fromList ? (uint16_t)(L.code & ::mer::kBelowFromPin) : L.code);   // from pin 0: List
        kept.from.push_back(fromList ? (uint16_t)g.newIdx : from);
        kept.to.push_back(to);
    }
    // nRe counts every buffer so far: after the first reroute each later buffer is rewritten (GP-24)
    if (kept.code.size() == b->n && !g.nRe) return;
    writeLinks(r, kept);
}

inline void rewriteLinks(ActGraph& g)
{
    oct::File& f = g.f;
    // the rewrite changes payloads only, so the pool's end holds (GP-23)
    for (size_t i = g.lpool + 1, end = f.blockEnd(g.lpool); i < end; ++i) {
        if (f.name(f.recs[i]) != "Data" || !inLinksBuffer(g, i)) continue;
        rewriteLinkBuffer(g, f.recs[i]);
    }
}

// A scalar name record's (or an MNode key's) value set to v.
inline void setNameValue(oct::File& f, oct::Rec& r, const std::string& v)
{
    r.pay.clear();
    oct::File::put(r.pay, f.addName(v), 2);
}

// The Participants list: an AvatarList (ParticipantsOnly) at NameSpaceEnd's index, then NameSpaceEnd one up.
inline void emitAvatarList(ActGraph& g, size_t k, std::vector<oct::Rec>& out)
{
    oct::File& f = g.f;
    const uint16_t fl = g.fl;
    const ActNode& n = g.nodes[k];
    oct::Rec hdr = f.recs[n.at];
    setNameValue(f, hdr, std::to_string(k));
    out.push_back(hdr);
    out.push_back(f.nameScalar(fl, f.addName("Type"), "AvatarList"));
    if (!g.smUuid.empty()) {
        oct::Rec u{fl, oct::makeType(oct::SCALAR, 0, oct::BYTE), f.addName("Uuid"), g.smUuid};
        u.pay.back() = (char)(u.pay.back() ^ 0x5a);   // unique beside the state machine it stands for
        out.push_back(u);
    }
    out.push_back(f.intScalar(fl, "List", 0));
    out.push_back(f.intScalar(fl, "NetSyncType", 2));
    out.push_back(f.intScalar(fl, "LocalsOnly", 0));
    out.push_back(f.intScalar(fl, "SpectatorsOnly", 0));
    out.push_back(f.intScalar(fl, "ParticipantsOnly", 1));
    out.push_back(f.intScalar(fl, "IsRootObject", 1));
    out.push_back(f.nameScalar(fl, f.addName("AuthoredName"), "Activity: Participants"));
    out.push_back(f.intScalar(fl, "NumOutLinks", g.nRe));
    oct::Rec end = f.recs[n.at];
    setNameValue(f, end, std::to_string(k + 1));
    out.push_back(end);
    for (size_t j = n.at + 1; j < n.end; ++j) out.push_back(f.recs[j]);
}

// Field r of a class-c node in 3.0 form: Type / eval type, pins, parameters renamed. false: drop it.
inline bool renameField(ActGraph& g, const ActClass& c, oct::Rec& r, const std::string& nm, bool& firstType)
{
    oct::File& f = g.f;
    if (nm == "Type") {
        setNameValue(f, r, firstType ? c.cls30 : c.eval30);
        firstType = false;
    } else if (nm == "Uuid" && g.smUuid.empty() && !strcmp(c.cls10, "MissionTask_ActivityStateMachine")) {
        g.smUuid = r.pay;
    }
    bool drop = false;
    for (auto& p : c.pins) {
        if (nm != p.first || !isIntScalar(r)) continue;
        if (p.second.empty() || p.second == "*") drop = true;
        else r.name = f.addName(p.second);
    }
    for (auto& p : c.params)
        if (nm == p.first) r.name = f.addName(p.second);
    return !drop;
}

// Node k's records: NumIn/OutLinks less the dropped links; an activity task's fields in 3.0 form.
inline void rewriteNode(ActGraph& g, size_t k, std::vector<oct::Rec>& out)
{
    oct::File& f = g.f;
    const ActNode& n = g.nodes[k];
    const ActClass* c = taskClass(n.type);
    out.push_back(f.recs[n.at]);
    bool firstType = true;
    for (size_t j = n.at + 1; j < n.end; ++j) {
        oct::Rec r = f.recs[j];
        const bool field = r.level == g.fl;
        const std::string nm = field ? f.name(r) : std::string();
        if (field && nm == "NumInLinks" && g.dIn.count(k)) f.setInt(r, firstInt(f, r) - g.dIn[k]);
        if (field && nm == "NumOutLinks" && g.dOut.count(k)) f.setInt(r, firstInt(f, r) - g.dOut[k]);
        if (c && field && !renameField(g, *c, r, nm, firstType)) continue;
        out.push_back(r);
    }
    if (c && !strcmp(c->cls30, "MissionTask_ChallengeStateMachine"))
        out.push_back(f.nameScalar(g.fl, f.addName("ChallengeZone"), "NONE"));   // 53/61 shipped graphs
}


// ---- mission tables ----

struct Edit {
    size_t at, len;
    std::string text;
};

// The table holding a key token: its '{' and '}' tokens (close = toks.size() if unclosed), its keys' depth.
struct KeyTable {
    size_t open, close;
    int depth;
};

inline bool opensAbove(const lualex::Tok& t, int depth) { return t.kind == '{' && t.depth < depth; }
inline bool closesAbove(const lualex::Tok& t, int depth) { return t.kind == '}' && t.depth < depth; }

inline KeyTable tableAround(const std::vector<lualex::Tok>& toks, size_t k)
{
    KeyTable tb{k, k, toks[k].depth};
    while (tb.open > 0 && !opensAbove(toks[tb.open], tb.depth)) --tb.open;
    while (tb.close < toks.size() && !closesAbove(toks[tb.close], tb.depth)) ++tb.close;
    return tb;
}

// Edits renaming class c's parameters among the keys of table tb.
inline void renameParams(const std::string& lua, const std::vector<lualex::Tok>& toks, const KeyTable& tb,
                         const ActClass& c, std::vector<Edit>& edits)
{
    for (size_t j = tb.open + 1; j < tb.close; ++j) {
        const bool key = toks[j].kind == 'i' && toks[j].depth == tb.depth;
        if (!key || !lualex::assignedAt(lua, toks[j].at + toks[j].len)) continue;
        for (auto& p : c.params)
            if (!lua.compare(toks[j].at, toks[j].len, p.first)) edits.push_back({toks[j].at, toks[j].len, p.second});
    }
}

// ---- givers (.ap sections) ----

constexpr char kIconKey[] = "\nActivityIcon=";
constexpr char kTagKey[] = "\nActorTagName=";
constexpr char kTypeKey[] = "\nChallengeType=";
constexpr size_t kIconLen = sizeof(kIconKey) - 1, kTagLen = sizeof(kTagKey) - 1;

// The rest of the line from `at` (to its '\r' or '\n').
inline std::string lineFrom(const std::string& ap, size_t at)
{
    size_t e = ap.find_first_of("\r\n", at);
    return ap.substr(at, (e == std::string::npos ? ap.size() : e) - at);
}

// The giver section at icon gains ChallengeType (its ActivityIcon) and BeaconType after that line. -> bytes added
inline size_t addGiverKeys(std::string& ap, size_t icon)
{
    size_t vs = icon + kIconLen, ve = ap.find_first_of("\r\n", vs);
    if (ve == std::string::npos) ve = ap.size();
    std::string eol = ve < ap.size() && ap[ve] == '\r' ? "\r\n" : "\n";
    // 1.0: the type's ActivityBeaconName, else DefaultActivityBeacon (FUN_00f938a0); no Incredibles type sets one
    std::string add = "ChallengeType=" + ap.substr(vs, ve - vs) + eol + "BeaconType=DefaultActivityBeacon" + eol;
    size_t ins = ap.find('\n', vs);
    ins = ins == std::string::npos ? ap.size() : ins + 1;
    const bool noEol = ap.empty() || ap.back() != '\n';
    if (ins == ap.size() && noEol) {
        ap += eol;
        ins = ap.size();
    }
    ap.insert(ins, add);
    return add.size();
}

}  // namespace detail

inline int activityGraph30(oct::File& f, int* rerouted, int* dropped)
{
    detail::ActGraph g{f};
    g.pool = f.pool("MeridianNodePool");
    g.lpool = f.pool("MeridianLinkPool");
    if (g.pool < 0 || g.lpool < 0) return 0;
    g.nl = (uint16_t)(f.recs[g.pool].level + 1);
    g.fl = (uint16_t)(g.nl + 1);
    detail::collectNodes(g);
    const int mapped = detail::mappedNodes(g);
    if (!mapped) return 0;
    for (auto& n : g.nodes)
        if (const ActClass* c = taskClass(n.type)) detail::notePins(g, n, *c);
    g.newIdx = g.nodes.size() - 1;   // NameSpaceEnd moves up one
    detail::rewriteLinks(g);
    std::vector<oct::Rec> out(f.recs.begin(), f.recs.begin() + g.pool + 1);
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        if (k == g.newIdx && g.nRe) detail::emitAvatarList(g, k, out);
        else detail::rewriteNode(g, k, out);
    }
    for (size_t i = f.blockEnd(g.pool); i < f.recs.size(); ++i) out.push_back(f.recs[i]);
    f.recs = std::move(out);
    if (rerouted) *rerouted = g.nRe;
    if (dropped) *dropped = g.nDrop;
    return mapped;
}

inline int activityTasks30(std::string& lua)
{
    constexpr size_t kTypeLen = sizeof("Type") - 1;
    auto toks = lualex::tokens(lua);
    std::vector<detail::Edit> edits;
    int n = 0;
    for (size_t k = 0; k < toks.size(); ++k) {
        const bool type = toks[k].kind == 'i' && !lua.compare(toks[k].at, toks[k].len, "Type");
        if (!type || !lualex::assignedAt(lua, toks[k].at + kTypeLen)) continue;
        size_t q = lua.find('"', toks[k].at), e = q == std::string::npos ? q : lua.find('"', q + 1);
        if (e == std::string::npos || lua.find_first_not_of(" \t=", toks[k].at + kTypeLen) != q) continue;
        std::string t = lua.substr(q + 1, e - q - 1);
        if (t.rfind("Eval_Activity", 0)) continue;
        const ActClass* c = evalClass(t);
        if (!c) return 0;
        edits.push_back({q + 1, t.size(), c->eval30});   // 3.0's factory FUN_00df42a0 builds the eval from Type
        const detail::KeyTable tb = detail::tableAround(toks, k);
        detail::renameParams(lua, toks, tb, *c, edits);
        // 61 of 61 shipped 3.0 state machine tables carry ChallengeZone
        if (!strcmp(c->cls30, "MissionTask_ChallengeStateMachine") && tb.close < toks.size())
            edits.push_back({toks[tb.close].at, 0, "\tChallengeZone = \"NONE\",\n"});
        ++n;
    }
    std::sort(edits.begin(), edits.end(), [](const detail::Edit& a, const detail::Edit& b) { return a.at > b.at; });
    for (auto& e : edits) lua.replace(e.at, e.len, e.text);
    return n;
}

inline std::set<std::string> activityGivers(const std::string& graphLua,
                                            const std::function<bool(const std::string&, std::string&)>& readMission)
{
    std::set<std::string> ok;
    static const std::string g = "MissionGiver = \"", m = "MissionName = \"";
    for (size_t p = graphLua.find(g); p != std::string::npos; p = graphLua.find(g, p + 1)) {
        size_t ge = graphLua.find('"', p + g.size()), q = graphLua.find(m, p);
        size_t qe = q == std::string::npos ? q : graphLua.find('"', q + m.size());
        size_t nextG = graphLua.find(g, p + 1);
        const bool pastNext = nextG != std::string::npos && q > nextG;   // the name belongs to the next giver
        if (ge == std::string::npos || qe == std::string::npos || pastNext) continue;
        std::string tag = graphLua.substr(p + g.size(), ge - p - g.size());
        std::string name = graphLua.substr(q + m.size(), qe - q - m.size()), t;
        const bool named = !tag.empty() && !name.empty();
        if (named && readMission(name, t) && activityTasks30(t)) ok.insert(tag);
    }
    return ok;
}

inline int apGivers30(std::string& ap, const std::set<std::string>* only)
{
    // a giver without ChallengeType stays inert in 3.0 (its init 0x1507380 returns at once): only convertible ones
    int n = 0;
    for (size_t sec = ap.find('['); sec != std::string::npos;) {
        size_t next = ap.find("\n[", sec + 1);
        size_t end = next == std::string::npos ? ap.size() : next + 1;
        size_t icon = ap.find(detail::kIconKey, sec), tag = ap.find(detail::kTagKey, sec);
        bool want = !only;
        if (only && tag < end) want = only->count(detail::lineFrom(ap, tag + detail::kTagLen)) > 0;
        const bool hasIcon = want && icon != std::string::npos && icon < end;
        if (hasIcon && ap.find(detail::kTypeKey, sec) >= end) {
            end += detail::addGiverKeys(ap, icon);
            ++n;
        }
        sec = end < ap.size() ? end : std::string::npos;
    }
    return n;
}

inline int dnaGadgets30(const std::string& in, std::string& out)
{
    // 3.0's missiongiver_challenge.dnax is 1.0's missiongiver_activity.dnax with this gadget renamed
    return luachunk::rewrite(in, out, [](std::string& s) {
        if (s != "MissionGiverActivity") return false;
        s = "MissionGiverChallenge";
        return true;
    });
}

}  // namespace di1::activities
