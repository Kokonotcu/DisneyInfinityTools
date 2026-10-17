// 1.0 .mer graphs in 3.0's form (FORMATS.md 5.8): parameters, pins, port names, links. Uses formats/mer; any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "../../convert/records.hpp"
#include "../../formats/mer.hpp"
#include "../../formats/oct.hpp"
#include "mission.hpp"

namespace di1::mer {

using ::mer::Class;
using ::mer::IndexLinks;
using ::mer::Link;
using ::mer::Node;
using ::mer::NS;
using ::mer::ParamBlock;
using ::mer::Table;

// The 1.0 rules' switches, one cfg key each, all on by default (FORMATS.md 5.8)
struct Rules {
    bool events10 = true;       // events10: MissionPresentationList Event ints in the 3.0 enum
    bool portRefs = true;       // portrefs10: owner fields naming port sub-nodes are neither parameters nor pins
    bool ports10 = true;        // ports10: port names from owner fields; a DataFetch's Value under its port
    bool pins10 = true;         // pins10: every plain non-parameter int is a pin; copies add the names missing
    bool gate10 = true;         // gate10: MasterNetSync Link_Out links leave from Private
    bool toggleType10 = true;   // toggletype10: the ColVisToggle family describes Type = its class
};

// What transcode changed, for the log line
struct Stats {
    int nodes = 0;
    int params = 0;
    int pinsCopied = 0;
    int stubs = 0;
    int links = 0;
    int linksDropped = 0;
    int namespaces = 0;
    int events = 0;
    int fetches = 0;
    int netSync = 0;
    int typed = 0;
};

// mission.hpp's kAliases as registered classes, each with its 3.0 twin's pins and parameter types added
inline void addAliases(Table& T)
{
    for (auto& a : mission::kAliases) {
        T.registered.insert(a.name);
        Class &c = T.classes[a.name], &t = T.classes[a.twin];
        c.pins.insert(t.pins.begin(), t.pins.end());
        c.params.insert(t.params.begin(), t.params.end());
    }
}

namespace detail {

// Node fields that are neither parameters nor pins
inline constexpr const char* kGenericFields[] = {
    "Type",     "Uuid",   "IsProxy",      "MerSpawnGroup",     "NumInLinks",  "NumOutLinks",
    "IsRootObject", "LeafName", "Parent", "AuthoredName", "Mer_NoConnections", "LeafNameHash"};
// The classes of port sub-nodes, which sit right before their owner (FORMATS.md 5.3)
inline constexpr const char* kSubNodeTypes[] = {"Event",         "DataFetch", "DataSource", "DataListSource",
                                                "DataListFetch", "Handler",   "EventSurge", "DataWritable",
                                                "DataWrite"};

inline bool listed(const char* const* first, const char* const* last, const std::string& s)
{
    return std::any_of(first, last, [&](const char* x) { return s == x; });
}
inline bool isGeneric(const std::string& n) { return listed(std::begin(kGenericFields), std::end(kGenericFields), n); }
inline bool isSubNode(const std::string& t) { return listed(std::begin(kSubNodeTypes), std::end(kSubNodeTypes), t); }

struct Out {   // a LinksBuffer Data record (index in f) and its 3.0 bytes
    size_t at;
    std::string data;
};

inline bool fail(std::string* why, const char* reason)
{
    if (why) *why = reason;
    return false;
}

// False when transcode would copy a ReferenceEmbeddedNamespace that holds no name to MergedLayoutPath
inline bool mergedPathReadable(const oct::File& f, const Node& b, uint16_t fl)
{
    const long ref = ::mer::field(f, b, fl, "ReferenceEmbeddedNamespace");
    const bool copied = ref >= 0 && ::mer::field(f, b, fl, "MergedLayoutPath") < 0;
    return !copied || ::mer::firstName(f, f.recs[ref]).has_value();
}

// Namespaces by node index, each node's one (-1 before any Begin); false (nss partial) if none (GR-07) or unreadable
inline bool collectNamespaces(const oct::File& f, uint16_t fl, std::vector<Node>& nodes, std::vector<NS>& nss,
                              std::string* why)
{
    for (size_t k = 0; k < nodes.size(); ++k) {
        if (nodes[k].type == "NameSpaceBegin") {
            const std::optional<std::string> name = ::mer::beginName(f, nodes[k]);
            if (!name) return fail(why, "NameSpaceBegin Name holds no name");
            if (!mergedPathReadable(f, nodes[k], fl)) return fail(why, "ReferenceEmbeddedNamespace holds no name");
            nss.push_back({k, k, *name});
        } else if (nodes[k].type == "NameSpaceEnd" && !nss.empty()) nss.back().end = k;
        if (!nss.empty()) nodes[k].ns = (int)nss.size() - 1;
    }
    return nss.empty() ? fail(why, "no NameSpaceBegin") : true;
}

// False if a name field (NumInLinks / NumOutLinks) the rebuild lowers by drops holds no int (GR-16)
inline bool dropsReadable(const oct::File& f, const std::vector<Node>& nodes, uint16_t fl,
                          const std::map<size_t, int>& drops, const char* name)
{
    for (const auto& d : drops) {
        if (d.first >= nodes.size()) continue;
        const Node& n = nodes[d.first];
        const bool rebuilt = !n.stub && n.type != "NameSpaceBegin" && n.type != "NameSpaceEnd";
        for (size_t j = n.at + 1; rebuilt && j < n.end; ++j) {
            const bool lowered = f.recs[j].level == fl && f.name(f.recs[j]) == name;
            if (lowered && f.ints(f.recs[j]).empty()) return false;
        }
    }
    return true;
}

// Record i of f; a LinksBuffer's Data as its 3.0 blob, wherever the link pool lies (GR-39)
inline oct::Rec relinked(oct::File& f, size_t i, const std::vector<Out>& newLinks)
{
    oct::Rec r = f.recs[i];
    for (auto& L : newLinks) {
        if (L.at != i) continue;
        oct::Rec b = octx::blobRec(f, r.level, "Data", L.data);
        b.name = r.name;
        r = b;
    }
    return r;
}

// A plain int scalar: not keyed (pins, port references and counts are)
inline bool isPlainInt(const oct::Rec& r)
{
    const oct::Spec sp = oct::spec(r.type);
    return sp.shape == oct::SCALAR && sp.kind == oct::INT && !sp.keyed;
}

// '<pin>_value' holds a data pin's constant in 1.0 (FORMATS.md 5.8); "_value" alone does not count
inline constexpr size_t kValueSuffixLen = 6;   // strlen("_value")
inline bool hasValueSuffix(const std::string& nm)
{
    return nm.size() > kValueSuffixLen && !nm.compare(nm.size() - kValueSuffixLen, kValueSuffixLen, "_value");
}
inline std::string withoutValueSuffix(const std::string& nm) { return nm.substr(0, nm.size() - kValueSuffixLen); }

inline bool isNamespaceNode(const Node& n) { return n.type == "NameSpaceBegin" || n.type == "NameSpaceEnd"; }

// What transcode's phases share: the parsed graph, then what each phase derives from it, in call order.
struct Graph {
    const Table& T;
    const Rules& R;
    oct::File& f;
    uint16_t fl;                                              // the level of node fields
    std::vector<Node> nodes;
    std::vector<NS> nss;
    std::vector<std::string> port;                            // per node: its port name, "" if none
    std::vector<std::set<std::string>> portRefs;              // per node: its fields naming port sub-nodes
    std::map<std::string, std::vector<oct::Rec>> pinTables;   // class -> its pin records
    std::vector<int> nsOut, nsPriv;                           // per node: MasterNetSync Link_Out / Private pin
    std::map<size_t, int> dIn, dOut;                          // node index -> links dropped
    std::vector<Out> newLinks;
    std::vector<oct::Rec> out;                                // the file's records as rebuilt
    std::vector<std::string> values, strs;                    // per namespace: ParameterValues, ParameterStrings
    std::vector<size_t> beginAt;                              // per namespace: end of its Begin's records in out
    Stats s;
};

// Nodes of a class 3.0 does not register become pinless stand-ins (FORMATS.md 5.8)
inline void markStubs(Graph& g)
{
    for (auto& n : g.nodes) n.stub = !isNamespaceNode(n) && !g.T.registered.count(n.type);
}

// Owner field j of node k naming a sub-node before k: that sub-node's port name and k's port reference
inline void ownerField(Graph& g, size_t k, size_t j)
{
    constexpr size_t kMaxPortDistance = 64;   // how far before its owner a port sub-node may sit
    const oct::Rec& r = g.f.recs[j];
    if (r.level != g.fl || !isPlainInt(r) || isGeneric(g.f.name(r))) return;
    const int64_t v = g.f.ints(r)[0];
    if (v < 0 || (size_t)v >= k) return;
    const size_t sub = (size_t)v;
    if (k - sub > kMaxPortDistance || !g.port[sub].empty() || g.nodes[sub].ns != g.nodes[k].ns) return;
    bool owned = true;
    for (size_t q = sub; q < k && owned; ++q) owned = isSubNode(g.nodes[q].type);
    if (!owned) return;
    g.port[sub] = g.f.name(r);
    if (g.R.portRefs) g.portRefs[k].insert(g.port[sub]);
}

// Port names: a sub-node's LeafName, else the owner field naming it (FORMATS.md 5.8, ports10)
inline void portNames(Graph& g)
{
    g.port.assign(g.nodes.size(), std::string());
    g.portRefs.assign(g.nodes.size(), std::set<std::string>());
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        const long leaf = ::mer::field(g.f, g.nodes[k], g.fl, "LeafName");
        std::vector<std::string> v = leaf >= 0 ? g.f.nameValues(g.f.recs[leaf]) : std::vector<std::string>();
        if (!v.empty()) g.port[k] = v[0];
    }
    for (size_t k = 0; g.R.ports10 && k < g.nodes.size(); ++k)
        for (size_t j = g.nodes[k].at + 1; j < g.nodes[k].end; ++j) ownerField(g, k, j);
}

// pins10's pin candidate: a plain int field that is no generic field, constant, port reference or '__' name
inline bool pinCandidate(const Graph& g, size_t k, const oct::Rec& r)
{
    const std::string& nm = g.f.name(r);
    if (r.level != g.fl || !isPlainInt(r) || isGeneric(nm)) return false;
    if (nm.find("__") != std::string::npos || hasValueSuffix(nm)) return false;
    return !g.portRefs[k].count(nm);
}

// The pin records node k lists: pins10's rule, or with pins10 off only those the class table names
inline std::vector<oct::Rec> listedPins(const Graph& g, size_t k, const Class* c)
{
    const Node& n = g.nodes[k];
    std::vector<oct::Rec> pins;
    for (size_t j = n.at + 1; j < n.end; ++j) {
        const oct::Rec& r = g.f.recs[j];
        const std::string& nm = g.f.name(r);
        if (!g.R.pins10) {
            if (r.level == g.fl && isPlainInt(r) && c->pins.count(nm)) pins.push_back(r);
            continue;
        }
        if (!pinCandidate(g, k, r)) continue;
        if (!c || c->pins.count(nm) || !c->params.count(nm)) pins.push_back(r);
    }
    return pins;
}

// Each class's pin table: from its first instance listing any pin (FORMATS.md 5.8, pins10)
inline void pinTables(Graph& g)
{
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        const Node& n = g.nodes[k];
        if (g.pinTables.count(n.type) || isNamespaceNode(n)) continue;
        auto c = g.T.classes.find(n.type);
        if (!g.R.pins10 && c == g.T.classes.end()) continue;
        std::vector<oct::Rec> pins = listedPins(g, k, c == g.T.classes.end() ? nullptr : &c->second);
        if (!pins.empty()) g.pinTables[n.type] = pins;
    }
}

// Node k's pin index by name: its own field, else its class's pin table (-1: none)
inline int pinOf(const Graph& g, size_t k, const char* name)
{
    const long j = ::mer::field(g.f, g.nodes[k], g.fl, name);
    if (j >= 0 && isPlainInt(g.f.recs[j])) return (int)g.f.ints(g.f.recs[j])[0];
    auto pt = g.pinTables.find(g.nodes[k].type);
    if (pt != g.pinTables.end())
        for (auto& r : pt->second) if (g.f.name(r) == name) return (int)g.f.ints(r)[0];
    return -1;
}

// gate10: each MasterNetSync's Link_Out and Private pins (FORMATS.md 5.8)
inline void netSyncPins(Graph& g)
{
    g.nsOut.assign(g.nodes.size(), -1);
    g.nsPriv.assign(g.nodes.size(), -1);
    for (size_t k = 0; g.R.gate10 && k < g.nodes.size(); ++k) {
        if (g.nodes[k].type != "MasterNetSync") continue;
        g.nsOut[k] = pinOf(g, k, "Link_Out");
        g.nsPriv[k] = pinOf(g, k, "Private");
    }
}

// A link pool record's Type, Name and Data; nullopt if a Type or Name holds no name
struct LinkRecord {
    std::string type, name;
    long data = -1;
};
inline std::optional<LinkRecord> linkRecord(const oct::File& f, size_t i)
{
    LinkRecord L;
    for (size_t j = i + 1, end = f.blockEnd(i); j < end; ++j) {
        const std::string& n = f.name(f.recs[j]);
        if (n == "Data") L.data = (long)j;
        if (n != "Type" && n != "Name") continue;
        const std::optional<std::string> v = ::mer::firstName(f, f.recs[j]);
        if (!v) return std::nullopt;
        (n == "Type" ? L.type : L.name) = *v;
    }
    return L;
}

// The namespace of a link pool NameSpaceBegin: the node pool's next one of that name (-1: none)
inline int linkNamespace(const Graph& g, std::map<std::string, int>& seen, const std::string& name)
{
    int want = seen[name]++, k = 0;
    for (size_t q = 0; q < g.nss.size(); ++q)
        if (g.nss[q].name == name && k++ == want) return (int)q;
    return -1;
}

// Node index by Uuid, for the uuid link form
inline std::map<std::string, size_t> nodesByUuid(const Graph& g)
{
    std::map<std::string, size_t> byUuid;
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        const long q = ::mer::field(g.f, g.nodes[k], g.fl, "Uuid");
        if (q >= 0) byUuid[g.f.recs[q].pay.substr(oct::spec(g.f.recs[q].type).cw, 16)] = k;
    }
    return byUuid;
}

// A kept link as a 3.0 row; gate10 sends a Link_Out from Private (FORMATS.md 5.8)
inline void keepLink(Graph& g, const NS& ns, Link L, std::string& rows)
{
    const size_t a = L.from;
    if (g.nsOut[a] >= 0 && g.nsPriv[a] >= 0 && (L.code >> ::mer::kFromPinShift) == g.nsOut[a]) {
        L.code = (uint16_t)((L.code & ::mer::kBelowFromPin) | g.nsPriv[a] << ::mer::kFromPinShift);
        ++g.s.netSync;
    }
    ::mer::appendRow(rows, L.code, a - ns.begin - 1, L.to - ns.begin - 1);
}

// A LinksBuffer of namespace ns in 3.0's form into newLinks; links leaving ns or touching a stand-in dropped
inline bool relinkBuffer(Graph& g, size_t data, const NS& ns, std::string* why)
{
    const std::optional<IndexLinks> b = ::mer::indexLinks(g.f.recs[data]);
    if (!b) return true;
    if (!::mer::sizeMatches(*b)) return fail(why, "LinksBuffer size");
    std::map<std::string, size_t> byUuid;
    if (b->uu) byUuid = nodesByUuid(g);
    std::string rows;
    uint32_t kept = 0;
    for (uint32_t k = 0; k < b->n; ++k) {
        const std::optional<Link> L = ::mer::linkAt(*b, k, byUuid);
        if (!L) {
            ++g.s.linksDropped;
            continue;
        }
        const size_t a = L->from, z = L->to;
        const bool fromInside = a > ns.begin && a < ns.end && a < g.nodes.size();
        const bool toInside = z > ns.begin && z < ns.end && z < g.nodes.size();
        const bool inside = fromInside && toInside;
        if (!inside || g.nodes[a].stub || g.nodes[z].stub) {
            ++g.dOut[a];
            ++g.dIn[z];
            ++g.s.linksDropped;
            continue;
        }
        keepLink(g, ns, *L, rows);
        ++kept;
    }
    g.newLinks.push_back({data, ::mer::version3Links(kept, rows)});
    g.s.links += (int)kept;
    return true;
}

// Every LinksBuffer in 3.0's form (newLinks); dropped links counted per node for NumInLinks / NumOutLinks
inline bool rewriteLinks(Graph& g, std::string* why)
{
    const oct::File& f = g.f;
    long lpool = f.pool("MeridianLinkPool");
    if (lpool < 0) return true;
    std::map<std::string, int> seen;   // namespace name -> times matched
    int curNs = -1;
    for (size_t i = lpool + 1, end = f.blockEnd(lpool); i < end; i = f.blockEnd(i)) {   // f is not edited here
        if (f.recs[i].level != f.recs[lpool].level + 1) continue;
        const std::optional<LinkRecord> L = linkRecord(f, i);
        if (!L) return fail(why, "link record holds no name");
        if (L->type == "NameSpaceBegin") curNs = linkNamespace(g, seen, L->name);
        else if (L->type != "LinksBuffer" || L->data < 0 || curNs < 0) continue;
        else if (!relinkBuffer(g, (size_t)L->data, g.nss[curNs], why)) return false;
    }
    return true;
}

// A Begin or End node's records; a Begin's RootNodeCount = its namespace's node count
inline void copyNamespaceNode(Graph& g, const Node& n)
{
    const bool begin = n.type == "NameSpaceBegin";
    for (size_t j = n.at + 1; j < n.end; ++j) {
        oct::Rec r = g.f.recs[j];
        if (begin && g.f.name(r) == "RootNodeCount") g.f.setInt(r, (int64_t)(g.nss[n.ns].end - g.nss[n.ns].begin - 1));
        g.out.push_back(r);
    }
    if (!begin) return;
    g.beginAt[n.ns] = g.out.size();
    ++g.s.namespaces;
}

// A node being rebuilt: its class (null if the table has none) and level-fl field names
struct NodeView {
    size_t k;
    const Node& n;
    const Class* c;
    int ns;                              // its namespace, 0 before any Begin
    std::set<std::string> names;
    std::set<std::string> valued;        // pins holding a constant ('<pin>_value')
};

// One field record and its name, copied: a stand-in's record may be rewritten
struct Field {
    oct::Rec r;
    std::string nm;
};

// A node's parameters as described so far
struct NodeParams {
    ParamBlock block;
    std::set<std::string> done;   // parameter names described
};

// Node k as rebuildNode reads it
inline NodeView viewOf(const Graph& g, size_t k)
{
    const Node& n = g.nodes[k];
    auto cls = g.T.classes.find(n.type);
    NodeView v{k, n, cls == g.T.classes.end() ? nullptr : &cls->second, n.ns < 0 ? 0 : n.ns, {}, {}};
    for (size_t j = n.at + 1; j < n.end; ++j) {
        if (g.f.recs[j].level != g.fl) continue;
        const std::string& nm = g.f.name(g.f.recs[j]);
        v.names.insert(nm);
        if (hasValueSuffix(nm)) v.valued.insert(withoutValueSuffix(nm));
    }
    return v;
}

// A field as copied: a stand-in's Type and link counts rewritten, its pins left out; dropped links subtracted
inline void copyField(Graph& g, const NodeView& v, bool firstType, Field& fd)
{
    oct::Rec& r = fd.r;
    const std::string& nm = fd.nm;
    const bool field = r.level == g.fl;
    const bool stubField = v.n.stub && field;
    if (stubField && nm == "Type" && firstType) {
        r = g.f.nameScalar(r.level, r.name, "NodeGroup");   // a plain name scalar whatever the 1.0 Type's form
        ++g.s.stubs;
    }
    const bool linkCount = nm == "NumInLinks" || nm == "NumOutLinks";
    if (stubField && linkCount) g.f.setInt(r, 0);
    else if (field && nm == "NumInLinks" && g.dIn.count(v.k)) g.f.setInt(r, g.f.ints(r)[0] - g.dIn[v.k]);
    else if (field && nm == "NumOutLinks" && g.dOut.count(v.k)) g.f.setInt(r, g.f.ints(r)[0] - g.dOut[v.k]);
    if (!(stubField && v.c && v.c->pins.count(nm))) g.out.push_back(r);
}

// A field as a parameter, unless a pin, a port reference or described already
inline void describeField(Graph& g, const NodeView& v, const Field& fd, NodeParams& p)
{
    const oct::Rec& r = fd.r;
    const bool classPin = v.c && v.c->pins.count(fd.nm);
    const bool pinName = classPin || v.valued.count(fd.nm);
    const bool pin = isPlainInt(r) && pinName;
    if (pin || g.portRefs[v.k].count(fd.nm)) return;
    std::string pn = hasValueSuffix(fd.nm) ? withoutValueSuffix(fd.nm) : fd.nm;
    const bool fetchValue = v.n.type == "DataFetch" && pn == "Value";
    if (g.R.ports10 && fetchValue && !g.port[v.k].empty()) {
        pn = g.port[v.k];
        ++g.s.fetches;
    }
    if (p.done.count(pn)) return;
    int type = -1;
    if (v.c) {
        auto t = v.c->params.find(pn);
        if (t != v.c->params.end()) type = t->second;
    }
    if (type < 0) type = ::mer::typeOf(r);
    if (type < 0) return;
    std::string& val = p.block.val;
    size_t off = val.size();
    if (!::mer::encode(g.f, r, type, val, g.strs[v.ns])) return;
    const bool presentation = !v.n.type.compare(0, 23, "MissionPresentationList");   // 23 = its length
    const bool presentEvent = pn == "Event" && presentation;
    if (g.R.events10 && type == ::mer::T_INT && presentEvent) {
        int32_t e = mission::presentEvent30((int32_t)oct::u((const uint8_t*)val.data() + off, 4));
        memcpy(&val[off], &e, 4);
        ++g.s.events;
    }
    p.done.insert(pn);
    ::mer::describe(p.block, type, pn, (int32_t)off);
}

// toggleType10: a ColVisToggle-family node describes Type = its class (FORMATS.md 5.8)
inline void toggleType(Graph& g, const NodeView& v, NodeParams& p)
{
    const std::string& type = v.n.type;
    const bool family = type == "VisibilityToggle" || type == "CollisionToggle" || type == "ColVisToggle";
    const bool wanted = g.R.toggleType10 && !v.n.stub && family;
    if (!wanted || p.done.count("Type")) return;
    const int32_t o = (int32_t)p.block.val.size();
    ::mer::appendString(p.block.val, g.strs[v.ns], type);
    ::mer::describe(p.block, ::mer::T_STRING, "Type", o);
    ++g.s.typed;
}

// The class's pin table on this instance too: the names it lacks (pins10), else all when it lists none
inline void copyPinTable(Graph& g, const NodeView& v)
{
    auto pt = g.pinTables.find(v.n.type);
    bool copied = false, has = false;
    if (pt != g.pinTables.end() && !g.R.pins10) {
        for (size_t j = v.n.at + 1; j < v.n.end; ++j)
            if (g.f.recs[j].level == g.fl && v.c && v.c->pins.count(g.f.name(g.f.recs[j]))) has = true;
        if (!has) {
            g.out.insert(g.out.end(), pt->second.begin(), pt->second.end());
            copied = true;
        }
    } else if (pt != g.pinTables.end()) {
        for (auto& pr : pt->second)
            if (!v.names.count(g.f.name(pr))) {
                g.out.push_back(pr);
                copied = true;
            }
    }
    g.s.pinsCopied += copied;
}

// Node k rebuilt into out: its fields, pin table, LeafNameHash and parameter description
inline void rebuildNode(Graph& g, size_t k)
{
    ++g.s.nodes;
    const NodeView v = viewOf(g, k);
    NodeParams p;
    bool firstType = true;
    for (size_t j = v.n.at + 1; j < v.n.end; ++j) {
        Field fd{g.f.recs[j], g.f.name(g.f.recs[j])};
        copyField(g, v, firstType, fd);
        if (fd.r.level != g.fl || v.n.stub) continue;
        if (fd.nm == "Type" && firstType) {
            firstType = false;
            continue;
        }
        if (fd.nm != "Type" && isGeneric(fd.nm)) continue;   // a second Type is the parameter 'Type'
        describeField(g, v, fd, p);
    }
    toggleType(g, v, p);
    if (!v.n.stub) copyPinTable(g, v);
    if (!g.port[k].empty() && !v.names.count("LeafNameHash"))
        g.out.push_back(g.f.intScalar(g.fl, "LeafNameHash", (int32_t)::mer::lookup2(g.port[k])));
    g.out.push_back(g.f.intScalar(g.fl, "Mer_ParametersOffset", (int32_t)g.values[v.ns].size()));
    g.out.push_back(octx::blobRec(g.f, g.fl, "Mer_ParameterDescription", ::mer::descriptionBytes(p.block)));
    g.values[v.ns] += p.block.val;
    g.s.params += (int)p.block.count;
}

// Each namespace's blobs after its Begin's records, back to front so earlier positions stay valid
inline void namespaceBlobs(Graph& g)
{
    oct::File& f = g.f;
    for (size_t q = g.nss.size(); q-- > 0;) {
        std::vector<oct::Rec> add = {f.intScalar(g.fl, "EnableNetworking", 1),
                                     octx::blobRec(f, g.fl, "ParameterValues", g.values[q]),
                                     octx::blobRec(f, g.fl, "ParameterStrings", g.strs[q])};
        const Node& b = g.nodes[g.nss[q].begin];
        long ref = ::mer::field(f, b, g.fl, "ReferenceEmbeddedNamespace");
        if (ref >= 0 && ::mer::field(f, b, g.fl, "MergedLayoutPath") < 0) {
            const uint16_t key = f.addName("MergedLayoutPath");
            add.push_back(f.nameScalar(g.fl, key, f.nameValues(f.recs[ref])[0]));
        }
        if (::mer::field(f, b, g.fl, "EnableNetworking") >= 0) add.erase(add.begin());
        g.out.insert(g.out.begin() + g.beginAt[q], add.begin(), add.end());
    }
}

// The file's records into out: the node pool rebuilt, every LinksBuffer in 3.0's form
inline void rebuild(Graph& g, size_t pool, size_t poolEnd)
{
    for (size_t i = 0; i <= pool; ++i) g.out.push_back(relinked(g.f, i, g.newLinks));
    g.values.assign(g.nss.size(), std::string());
    g.strs.assign(g.nss.size(), std::string());
    g.beginAt.assign(g.nss.size(), 0);
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        g.out.push_back(g.f.recs[g.nodes[k].at]);
        if (isNamespaceNode(g.nodes[k])) copyNamespaceNode(g, g.nodes[k]);
        else rebuildNode(g, k);
    }
    namespaceBlobs(g);
    for (size_t i = poolEnd; i < g.f.recs.size(); ++i) g.out.push_back(relinked(g.f, i, g.newLinks));
}

}  // namespace detail

// A 1.0 graph in 3.0's form under rules R; false (f unchanged) when not one or damaged, checked before editing
inline bool transcode(const Table& T, const Rules& R, oct::File& f, Stats* st = nullptr, std::string* why = nullptr)
{
    long pool = f.pool("MeridianNodePool");
    if (pool < 0) return detail::fail(why, "no MeridianNodePool");
    for (auto& r : f.recs)
        if (f.name(r) == "Mer_ParameterDescription") return detail::fail(why, "already 3.0");
    const size_t poolEnd = f.blockEnd(pool);
    detail::Graph g{T, R, f, (uint16_t)(f.recs[pool].level + 2)};
    if (!::mer::collectNodes(f, pool, g.nodes, why) || !detail::collectNamespaces(f, g.fl, g.nodes, g.nss, why))
        return false;
    detail::markStubs(g);
    detail::portNames(g);
    detail::pinTables(g);
    detail::netSyncPins(g);
    if (!detail::rewriteLinks(g, why)) return false;
    const bool countsReadable = detail::dropsReadable(f, g.nodes, g.fl, g.dIn, "NumInLinks") &&
                                detail::dropsReadable(f, g.nodes, g.fl, g.dOut, "NumOutLinks");
    if (!countsReadable) return detail::fail(why, "NumInLinks / NumOutLinks holds no int");
    detail::rebuild(g, (size_t)pool, poolEnd);
    f.recs = std::move(g.out);
    if (st) *st = g.s;
    return true;
}

}  // namespace di1::mer
