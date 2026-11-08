// pranktask10: 1.0 MissionTask_Pranked as 3.0's MissionTask_Generic, a PrankTaskCounter fires its Success. formats.
// Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "../../formats/mer.hpp"
#include "../../formats/oct.hpp"

namespace di1::pranktasks {

// A one-namespace 1.0 mission graph: MissionTask_Pranked as MissionTask_Generic, a counter beside each. -> tasks
inline int graph30(oct::File& f);
// A 1.0 mission table (.lua): Eval_Pranked evals become Eval_Generic (3.0's eval factory knows no Eval_Pranked).
inline int lua30(std::string& lua);

namespace detail {

inline constexpr char kTask10[] = "MissionTask_Pranked";
inline constexpr char kTask30[] = "MissionTask_Generic";
inline constexpr char kEval10[] = "Eval_Pranked";
inline constexpr char kEval30[] = "Eval_Generic";
inline constexpr char kCounter[] = "PrankTaskCounter";   // prankcombo.hpp's native class
inline constexpr char kTaskId[] = "TaskID";              // the counter's copy of its task's MissionID
// what the counter carries over from its task (Eval_Pranked's fields, 1.0 FUN_00b547d0)
inline constexpr const char* kCounterFields[] = {"MissionID", "PrankedCharacter", "Ender", "PrankCount", "PrankZone"};
// MissionTask_Pranked's pins as its graphs number them; MissionTask_Generic has none of them
inline constexpr const char* kTaskPins[] = {"Trigger", "Who", "Prank", "LaunchCount", "Pranked"};
constexpr int kSuccessPin = 0, kFailPin = 1;   // the Generic pins we list; 3.0 binds them by name
constexpr int kDonePin = 0;
constexpr uint16_t kImpulseKind = 1;           // link code kind (FORMATS.md 5.3)
constexpr char kUuidFlip = 0x5b;               // a counter's Uuid: its task's, last byte flipped

struct TNode {
    size_t at, end;
    std::string type;
};

// The rewrite's view: pools, field level, nodes, the tasks, the counts each node's links change by
struct TGraph {
    oct::File& f;
    long pool = -1, lpool = -1;
    uint16_t fl = 0;
    std::vector<TNode> nodes;
    std::vector<size_t> tasks;
    std::map<size_t, int> dIn, dOut;   // links removed (negative: added), per old node index
};

inline void setNameValue(oct::File& f, oct::Rec& r, const std::string& v)
{
    r.pay.clear();
    oct::File::put(r.pay, f.addName(v), 2);
}

inline bool isTaskPin(const std::string& nm)
{
    for (const char* p : kTaskPins)
        if (nm == p) return true;
    return false;
}

// Nodes with their first Type; the task indices
inline void collect(TGraph& g)
{
    oct::File& f = g.f;
    const uint16_t nl = (uint16_t)(f.recs[g.pool].level + 1);
    g.fl = (uint16_t)(nl + 1);
    for (size_t i = g.pool + 1, e = f.blockEnd(g.pool); i < e; i = f.blockEnd(i)) {
        if (f.recs[i].level != nl) continue;
        TNode n{i, f.blockEnd(i), ""};
        for (size_t j = i + 1; j < n.end && n.type.empty(); ++j) {
            const bool type = f.recs[j].level == g.fl && f.name(f.recs[j]) == "Type";
            const std::optional<std::string> v = type ? ::mer::firstName(f, f.recs[j]) : std::nullopt;
            if (v) n.type = *v;
        }
        if (n.type == kTask10) g.tasks.push_back(g.nodes.size());
        g.nodes.push_back(n);
    }
}

// One namespace, NameSpaceEnd last, at least one task
inline bool fits(const TGraph& g)
{
    int begins = 0;
    for (const TNode& n : g.nodes) begins += n.type == "NameSpaceBegin";
    const bool endLast = !g.nodes.empty() && g.nodes.back().type == "NameSpaceEnd";
    return begins == 1 && endLast && !g.tasks.empty();
}

inline bool isTask(const TGraph& g, size_t k) { return k < g.nodes.size() && g.nodes[k].type == kTask10; }

// The Data record of the link pool's LinksBuffer in the index form, -1 none
inline long linksData(const TGraph& g)
{
    const oct::File& f = g.f;
    for (size_t i = g.lpool + 1, e = f.blockEnd(g.lpool); i < e; i = f.blockEnd(i)) {
        long data = -1;
        bool buffer = false;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) {
            const std::string& nm = f.name(f.recs[j]);
            if (nm == "Data") data = (long)j;
            if (nm == "Type") buffer = ::mer::firstName(f, f.recs[j]).value_or("") == "LinksBuffer";
        }
        const std::optional<::mer::IndexLinks> b = buffer && data >= 0 ? ::mer::indexLinks(f.recs[data]) : std::nullopt;
        if (b && !b->uu && ::mer::sizeMatches(*b)) return data;
    }
    return -1;
}

// The links without those touching a task, NameSpaceEnd moved up by the counters, each counter's Done -> Success
inline ::mer::LinkRows relink(TGraph& g, long data)
{
    const ::mer::IndexLinks b = *::mer::indexLinks(g.f.recs[data]);
    const std::map<std::string, size_t> noUuids;
    const size_t end = g.nodes.size() - 1, n = g.tasks.size();
    ::mer::LinkRows rows;
    for (uint32_t k = 0; k < b.n; ++k) {
        const ::mer::Link L = *::mer::linkAt(b, k, noUuids);
        if (isTask(g, L.from) || isTask(g, L.to)) {
            ++g.dOut[L.from];
            ++g.dIn[L.to];
            continue;
        }
        rows.code.push_back(L.code);
        rows.from.push_back((uint16_t)(L.from == end ? end + n : L.from));
        rows.to.push_back((uint16_t)(L.to == end ? end + n : L.to));
    }
    const uint16_t code =
        (uint16_t)(kDonePin << ::mer::kFromPinShift | kSuccessPin << ::mer::kToPinShift | kImpulseKind);
    for (size_t i = 0; i < n; ++i) {
        rows.code.push_back(code);
        rows.from.push_back((uint16_t)(end + i));
        rows.to.push_back((uint16_t)g.tasks[i]);
        --g.dIn[g.tasks[i]];
    }
    return rows;
}

// Node k with its link counts changed; a task in Generic form: old pins out, Success / Fail in
inline void copyNode(TGraph& g, size_t k, std::vector<oct::Rec>& out)
{
    oct::File& f = g.f;
    const TNode& n = g.nodes[k];
    const bool task = isTask(g, k);
    bool firstType = true, counted = false;
    out.push_back(f.recs[n.at]);
    for (size_t j = n.at + 1; j < n.end; ++j) {
        oct::Rec r = f.recs[j];
        const bool field = r.level == g.fl;
        const std::string nm = field ? f.name(r) : std::string();
        if (field && nm == "NumInLinks") {
            f.setInt(r, f.int0(r) - g.dIn[k]);
            counted = true;
        }
        if (field && nm == "NumOutLinks" && g.dOut.count(k)) f.setInt(r, f.int0(r) - g.dOut[k]);
        if (task && field && isTaskPin(nm)) continue;
        if (task && field && nm == "Type") {
            setNameValue(f, r, firstType ? kTask30 : kEval30);
            firstType = false;
        }
        out.push_back(r);
    }
    if (!task) return;
    out.push_back(f.intScalar(g.fl, "Success", kSuccessPin));
    out.push_back(f.intScalar(g.fl, "Fail", kFailPin));
    if (!counted) out.push_back(f.intScalar(g.fl, "NumInLinks", -g.dIn[k]));
}

// Task t's counter as node k: its Done pin, the task's prank fields, one out-link
inline void emitCounter(TGraph& g, size_t t, size_t k, std::vector<oct::Rec>& out)
{
    oct::File& f = g.f;
    const TNode& n = g.nodes[t];
    oct::Rec hdr = f.recs[n.at];
    setNameValue(f, hdr, std::to_string(k));
    out.push_back(hdr);
    out.push_back(f.nameScalar(g.fl, f.addName("Type"), kCounter));
    for (size_t j = n.at + 1; j < n.end; ++j) {
        oct::Rec r = f.recs[j];
        if (r.level != g.fl) continue;
        const std::string nm = f.name(r);
        if (nm == "Uuid" && !r.pay.empty()) r.pay.back() = (char)(r.pay.back() ^ kUuidFlip);
        bool keep = nm == "Uuid";
        for (const char* c : kCounterFields) keep = keep || nm == c;
        if (nm == "MissionID") r.name = f.addName(kTaskId);   // the mission system binds MissionID: the task keeps it
        if (keep) out.push_back(r);
    }
    out.push_back(f.intScalar(g.fl, "Done", kDonePin));
    out.push_back(f.intScalar(g.fl, "IsRootObject", 1));
    out.push_back(f.nameScalar(g.fl, f.addName("AuthoredName"), "Prank task counter"));
    out.push_back(f.intScalar(g.fl, "NumOutLinks", 1));
}

}  // namespace detail

inline int graph30(oct::File& f)
{
    detail::TGraph g{f};
    g.pool = f.pool("MeridianNodePool");
    g.lpool = f.pool("MeridianLinkPool");
    if (g.pool < 0 || g.lpool < 0) return 0;
    detail::collect(g);
    const long data = detail::fits(g) ? detail::linksData(g) : -1;
    if (data < 0) return 0;
    const ::mer::LinkRows rows = detail::relink(g, data);
    ::mer::writeLinks(f.recs[data], rows);   // the payload only: the pool ends stay put
    const size_t end = g.nodes.size() - 1;
    std::vector<oct::Rec> out(f.recs.begin(), f.recs.begin() + g.pool + 1);
    for (size_t k = 0; k < end; ++k) detail::copyNode(g, k, out);
    for (size_t i = 0; i < g.tasks.size(); ++i) detail::emitCounter(g, g.tasks[i], end + i, out);
    oct::Rec endHdr = f.recs[g.nodes[end].at];
    detail::setNameValue(f, endHdr, std::to_string(end + g.tasks.size()));
    out.push_back(endHdr);
    for (size_t j = g.nodes[end].at + 1; j < g.nodes[end].end; ++j) out.push_back(f.recs[j]);
    for (size_t i = f.blockEnd(g.pool); i < f.recs.size(); ++i) out.push_back(f.recs[i]);
    f.recs = std::move(out);
    return (int)g.tasks.size();
}

inline int lua30(std::string& lua)
{
    const std::string from = std::string("Type = \"") + detail::kEval10 + "\"";
    const std::string to = std::string("Type = \"") + detail::kEval30 + "\"";
    int n = 0;
    for (size_t p = lua.find(from); p != std::string::npos; p = lua.find(from, p + to.size())) {
        lua.replace(p, from.size(), to);
        ++n;
    }
    return n;
}

}  // namespace di1::pranktasks
