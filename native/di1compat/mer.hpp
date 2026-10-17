// Meridian graph (.mer) 1.0 -> 3.0, in memory (plan MER-2/MER-4; research verify-meridian + DI3Tools mer.py).
// 3.0 reads node parameters only through Mer_ParameterDescription (FUN_006b2700 -> FUN_006a9740, default on a miss)
// and links only in version 3 (FUN_006ab3d0: namespace-relative 6-byte rows); 1.0 stores parameters as named fields
// per node (a data pin's constant as '<pin>_value') and links with global node indices. Additive: every 1.0 record
// stays (3.0 reads fields by name hash and ignores the rest), and the 3.0 form is added:
//   node        Mer_ParametersOffset + Mer_ParameterDescription (u32 n, n x (u8 type, u32 lookup2(name), i32 offset))
//               over its block in the namespace's ParameterValues: 0 bool / 1 int / 2 float 4 B, 3 string = u32
//               offset into ParameterStrings + 8 x FF, 4 vec3 = 3 floats; strings appended in parameter order
//   namespace   ParameterValues, ParameterStrings, EnableNetworking 1 (104/104 shipped), RootNodeCount = its node
//               count (520/520 shipped; 1.0 counts IsRootObject nodes), MergedLayoutPath = ReferenceEmbeddedNamespace
//               on merged namespaces that lack it (FUN_006c88f0 copies it; 1.0 never writes it)
//   pins        names from 3.0 graphs (di1compat_mer.txt, mertable.py); 1.0 lists a class's pin table once per
//               file, 3.0 shares a definition only within a namespace and parameter layout (FUN_006c6b80), so every
//               instance gets the pins; a name with a '<name>_value' sibling is a data pin
//   types       a parameter's type is the one 3.0 reads it as (the table), else the record's own
//   second Type the parameter 'Type' (2,127 1.0 mission nodes; 3.0 data stores it so)
//   LeafName    + LeafNameHash = lookup2(port name: LeafName, else the owner field naming the sub-node); a DataFetch's
//               'Value' becomes the parameter of that port name (3.0 form); the owner field stays an int record only
//               (no parameter, no pin-table copy: Table::portRefs)
//   links       u32 n, u8 0, u16 3, n x (u16 code, u16 from, u16 to), from/to = node index - Begin - 1
//   classes 3.0 does not register (DeliveryNode, Activate/SpawnDecorationSockets + 8 mission classes) would leave
//               no node slot and shift every later index (verify-meridian claim 6: out-of-bounds link writes), so
//               until they are ported (MER-3/6) such a node becomes a pinless NodeGroup and its links are dropped.
//   Event       MissionPresentationList* 'Event' in the 3.0 mission event enum (mission.hpp; Table::events10)
//   NetSync     MasterNetSync links from Link_Out leave from Private, the output 3.0 fires locally (Table::gate10)
//   ToggleType  Collision/Visibility/ColVisToggle describe 'Type' = their class, 3.0's mode switch (Table::toggleType10)
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "base/hash.hpp"
#include "convert/records.hpp"
#include "formats/oct.hpp"
#include "mission.hpp"

namespace mer {

// Meridian's name hash: lookup2 with seed 0 (FORMATS.md 5.3), the same function as dct's (GR-11)
inline uint32_t lookup2(const std::string& s, uint32_t seed = 0)
{
    return hash::lookup2((const uint8_t*)s.data(), s.size(), seed);
}

enum { T_BOOL = 0, T_INT = 1, T_FLOAT = 2, T_STRING = 3, T_VEC3 = 4 };
struct Class { std::set<std::string> pins; std::map<std::string, int> params; };
struct Table {
    bool events10 = true;   // presentation Event ints 1.0 -> 3.0 enum (cfg events10)
    bool portRefs = true;   // owner fields naming port sub-nodes: not described, not copied as pins (cfg portrefs10)
    bool ports10 = true;    // port names from owner fields (LeafNameHash), DataFetch 'Value' under its port (cfg ports10)
    bool pins10 = true;     // pin table = every plain non-parameter int; copies add the names an instance lacks (cfg pins10)
    bool gate10 = true;     // MasterNetSync Link_Out links leave from Private, the pin 3.0 fires locally (cfg gate10)
    bool toggleType10 = true;   // Collision/VisibilityToggle get the 'Type' parameter 3.0 reads their mode from (cfg toggletype10)
    std::set<std::string> registered;
    std::map<std::string, Class> classes;
    bool load(const std::string& text)
    {
        std::istringstream in(text);
        std::string line;
        Class* cur = nullptr;
        static const std::map<std::string, int> types = {{"bool", T_BOOL}, {"int", T_INT}, {"float", T_FLOAT}, {"string", T_STRING}, {"vec3", T_VEC3}};
        while (std::getline(in, line)) {
            if (line.size() < 3 || line[1] != ' ') continue;
            std::string rest = line.substr(2);
            if (line[0] == 'R') registered.insert(rest);
            else if (line[0] == 'C') cur = &classes[rest];
            else if (line[0] == 'P' && cur) cur->pins.insert(rest);
            else if (line[0] == 'A' && cur) {
                size_t sp = rest.find(' ');
                auto t = sp == std::string::npos ? types.end() : types.find(rest.substr(sp + 1));
                if (t != types.end()) cur->params[rest.substr(0, sp)] = t->second;
            }
        }
        return !registered.empty();
    }
};

// mission.hpp kAliases as registered classes, each with its 3.0 twin's pins and parameter types added
inline void addAliases(Table& T)
{
    for (auto& a : mission::kAliases) {
        T.registered.insert(a.name);
        Class &c = T.classes[a.name], &t = T.classes[a.twin];
        c.pins.insert(t.pins.begin(), t.pins.end());
        c.params.insert(t.params.begin(), t.params.end());
    }
}

inline bool isGeneric(const std::string& n)
{
    static const std::set<std::string> g = {"Type", "Uuid", "IsProxy", "MerSpawnGroup", "NumInLinks", "NumOutLinks", "IsRootObject",
                                            "LeafName", "Parent", "AuthoredName", "Mer_NoConnections", "LeafNameHash"};
    return g.count(n) > 0;
}


// One parameter value from a 1.0 record as the given 3.0 type; false if the record cannot be read so.
inline bool encode(const oct::File& f, const oct::Rec& r, int type, std::string& val, std::string& strings)
{
    oct::Spec s = oct::spec(r.type);
    auto put4 = [&](uint32_t v) { val.append((const char*)&v, 4); };
    std::vector<double> nums;
    if (s.kind == oct::INT) for (int32_t v : f.ints(r)) nums.push_back(v);
    else if (s.kind == oct::FLOAT) {
        size_t n = s.shape == oct::LIST ? oct::u((const uint8_t*)r.pay.data() + 2 * s.keyed, s.cw) : 1;
        const uint8_t* p = (const uint8_t*)r.pay.data() + 2 * s.keyed + s.cw;
        for (size_t i = 0; i < n; ++i) { float x; memcpy(&x, p + 4 * i, 4); nums.push_back(x); }
    }
    switch (type) {
    case T_BOOL: if (nums.size() != 1) return false; put4(nums[0] != 0); return true;
    case T_INT: if (nums.size() != 1) return false; put4((uint32_t)(int32_t)nums[0]); return true;
    case T_FLOAT: { if (nums.size() != 1) return false; float x = (float)nums[0]; val.append((const char*)&x, 4); return true; }
    case T_VEC3:
        if (nums.size() != 3) return false;
        for (double d : nums) { float x = (float)d; val.append((const char*)&x, 4); }
        return true;
    case T_STRING: {
        if (s.kind != oct::NAME || s.shape != oct::SCALAR) return false;
        put4((uint32_t)strings.size());
        val.append(8, '\xff');
        strings += f.nameValues(r).back();
        strings.push_back('\0');
        return true;
    }
    }
    return false;
}

inline int typeOf(const oct::File& f, const oct::Rec& r)
{
    oct::Spec s = oct::spec(r.type);
    if (s.keyed) return -1;
    if (s.shape == oct::SCALAR && s.kind == oct::INT) return T_INT;
    if (s.shape == oct::SCALAR && s.kind == oct::FLOAT) return T_FLOAT;
    if (s.shape == oct::SCALAR && s.kind == oct::NAME) return T_STRING;
    if (s.shape == oct::LIST && s.kind == oct::FLOAT && oct::u((const uint8_t*)r.pay.data(), s.cw) == 3) return T_VEC3;
    return -1;
}

struct Stats { int nodes = 0, params = 0, pinsCopied = 0, stubs = 0, links = 0, linksDropped = 0, namespaces = 0, events = 0, fetches = 0, netSync = 0, typed = 0; };

// transcode's graph parsing. Every check here runs before transcode changes f (its "false means f unchanged").
namespace detail {

struct Node { size_t at, end; std::string type; int ns = -1; bool stub = false; };
struct NS { size_t begin, end; std::string name; };
struct Out { size_t at; std::string data; };   // a LinksBuffer Data record (index in f) and its 3.0 bytes

inline bool fail(std::string* why, const char* reason)
{
    if (why) *why = reason;
    return false;
}

// nameValues(r)[0], a keyed record's key; nullopt when the record holds no name (GR-16)
inline std::optional<std::string> firstName(const oct::File& f, const oct::Rec& r)
{
    std::vector<std::string> v = f.nameValues(r);
    if (v.empty()) return std::nullopt;
    return std::move(v[0]);
}

// A node's first field (record at level fl) of this name, -1 if none
inline long field(const oct::File& f, const Node& n, uint16_t fl, const char* name)
{
    for (size_t j = n.at + 1; j < n.end; ++j) if (f.recs[j].level == fl && f.name(f.recs[j]) == name) return (long)j;
    return -1;
}

// The pool's child nodes with their first Type field; false (nodes partly filled) if that Type holds no name
inline bool collectNodes(const oct::File& f, size_t pool, std::vector<Node>& nodes, std::string* why)
{
    const size_t poolEnd = f.blockEnd(pool);
    const uint16_t nodeLevel = (uint16_t)(f.recs[pool].level + 1), fl = (uint16_t)(nodeLevel + 1);
    for (size_t i = pool + 1; i < poolEnd; i = f.blockEnd(i)) {
        if (f.recs[i].level != nodeLevel) continue;
        Node n{i, f.blockEnd(i), ""};
        const long type = field(f, n, fl, "Type");
        const std::optional<std::string> name = type < 0 ? std::optional<std::string>("") : firstName(f, f.recs[type]);
        if (!name) return fail(why, "node Type holds no name");
        n.type = *name;
        nodes.push_back(n);
    }
    return true;
}

// A NameSpaceBegin's name: its last Name record at any level ("" if none); nullopt if one holds no name
inline std::optional<std::string> beginName(const oct::File& f, const Node& b)
{
    std::string name;
    for (size_t j = b.at + 1; j < b.end; ++j) {
        if (f.name(f.recs[j]) != "Name") continue;
        const std::optional<std::string> v = firstName(f, f.recs[j]);
        if (!v) return std::nullopt;
        name = *v;
    }
    return name;
}

// False when transcode would copy a ReferenceEmbeddedNamespace that holds no name to MergedLayoutPath
inline bool mergedPathReadable(const oct::File& f, const Node& b, uint16_t fl)
{
    const long ref = field(f, b, fl, "ReferenceEmbeddedNamespace");
    const bool copied = ref >= 0 && field(f, b, fl, "MergedLayoutPath") < 0;
    return !copied || firstName(f, f.recs[ref]).has_value();
}

// Namespaces by node index, each node's one (-1 before any Begin); false (nss partial) if none (GR-07) or unreadable
inline bool collectNamespaces(const oct::File& f, uint16_t fl, std::vector<Node>& nodes, std::vector<NS>& nss,
                              std::string* why)
{
    for (size_t k = 0; k < nodes.size(); ++k) {
        if (nodes[k].type == "NameSpaceBegin") {
            const std::optional<std::string> name = beginName(f, nodes[k]);
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
    static const std::set<std::string> subTypes = {"Event", "DataFetch", "DataSource", "DataListSource", "DataListFetch",
                                                   "Handler", "EventSurge", "DataWritable", "DataWrite"};
    constexpr size_t kMaxPortDistance = 64;   // how far before its owner a port sub-node may sit
    const oct::Rec& r = g.f.recs[j];
    if (r.level != g.fl || !isPlainInt(r) || isGeneric(g.f.name(r))) return;
    const int64_t v = g.f.ints(r)[0];
    if (v < 0 || (size_t)v >= k) return;
    const size_t sub = (size_t)v;
    if (k - sub > kMaxPortDistance || !g.port[sub].empty() || g.nodes[sub].ns != g.nodes[k].ns) return;
    bool owned = true;
    for (size_t q = sub; q < k && owned; ++q) owned = subTypes.count(g.nodes[q].type) > 0;
    if (!owned) return;
    g.port[sub] = g.f.name(r);
    if (g.T.portRefs) g.portRefs[k].insert(g.port[sub]);
}

// Port names: a sub-node's LeafName, else the owner field naming it (FORMATS.md 5.8, ports10)
inline void portNames(Graph& g)
{
    g.port.assign(g.nodes.size(), std::string());
    g.portRefs.assign(g.nodes.size(), std::set<std::string>());
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        const long leaf = field(g.f, g.nodes[k], g.fl, "LeafName");
        std::vector<std::string> v = leaf >= 0 ? g.f.nameValues(g.f.recs[leaf]) : std::vector<std::string>();
        if (!v.empty()) g.port[k] = v[0];
    }
    for (size_t k = 0; g.T.ports10 && k < g.nodes.size(); ++k)
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
        if (!g.T.pins10) {
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
        if (!g.T.pins10 && c == g.T.classes.end()) continue;
        std::vector<oct::Rec> pins = listedPins(g, k, c == g.T.classes.end() ? nullptr : &c->second);
        if (!pins.empty()) g.pinTables[n.type] = pins;
    }
}

// Node k's pin index by name: its own field, else its class's pin table (-1: none)
inline int pinOf(const Graph& g, size_t k, const char* name)
{
    const long j = field(g.f, g.nodes[k], g.fl, name);
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
    for (size_t k = 0; g.T.gate10 && k < g.nodes.size(); ++k) {
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
        const std::optional<std::string> v = firstName(f, f.recs[j]);
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

// A 1.0 LinksBuffer's Data after its count: u32 n, u8 uuid form, n u16 codes, then n endpoint pairs
struct LinksData {
    const uint8_t* d;
    uint32_t n;
    bool uu;
};
struct Link {
    uint16_t code;
    size_t from, to;
};

// Row k: the uuid form's endpoints through byUuid (nullopt: unresolved), else the global index form's
inline std::optional<Link> linkAt(const LinksData& b, uint32_t k, const std::map<std::string, size_t>& byUuid)
{
    const uint8_t* d = b.d;
    const uint32_t n = b.n;
    Link L{(uint16_t)oct::u(d + 5 + 2 * k, 2), 0, 0};
    if (!b.uu) {
        L.from = oct::u(d + 5 + 2 * n + 4 * k, 2);
        L.to = oct::u(d + 5 + 2 * n + 4 * k + 2, 2);
        return L;
    }
    auto ia = byUuid.find(std::string((const char*)d + 5 + 2 * n + 32 * k, 16));
    auto iz = byUuid.find(std::string((const char*)d + 5 + 2 * n + 32 * k + 16, 16));
    if (ia == byUuid.end() || iz == byUuid.end()) return std::nullopt;
    L.from = ia->second;
    L.to = iz->second;
    return L;
}

// Node index by Uuid, for the uuid link form
inline std::map<std::string, size_t> nodesByUuid(const Graph& g)
{
    std::map<std::string, size_t> byUuid;
    for (size_t k = 0; k < g.nodes.size(); ++k) {
        const long q = field(g.f, g.nodes[k], g.fl, "Uuid");
        if (q >= 0) byUuid[g.f.recs[q].pay.substr(oct::spec(g.f.recs[q].type).cw, 16)] = k;
    }
    return byUuid;
}

// A kept link as a 3.0 row; gate10 sends a Link_Out from Private (FORMATS.md 5.3: from pin << 10)
inline void keepLink(Graph& g, const NS& ns, Link L, std::string& out)
{
    constexpr int kFromPinShift = 10;
    constexpr uint16_t kBelowFromPin = 0x3ff;
    const size_t a = L.from;
    if (g.nsOut[a] >= 0 && g.nsPriv[a] >= 0 && (L.code >> kFromPinShift) == g.nsOut[a]) {
        L.code = (uint16_t)((L.code & kBelowFromPin) | g.nsPriv[a] << kFromPinShift);
        ++g.s.netSync;
    }
    uint16_t row[3] = {L.code, (uint16_t)(a - ns.begin - 1), (uint16_t)(L.to - ns.begin - 1)};
    out.append((const char*)row, 6);
}

// A LinksBuffer of namespace ns in 3.0's form into newLinks; links leaving ns or touching a stand-in dropped
inline bool relinkBuffer(Graph& g, size_t data, const NS& ns, std::string* why)
{
    const oct::Rec& r = g.f.recs[data];
    oct::Spec sp = oct::spec(r.type);
    const uint8_t* d = (const uint8_t*)r.pay.data() + sp.cw;
    size_t len = r.pay.size() - sp.cw;
    if (len < 5) return true;
    const LinksData b{d, oct::u(d, 4), d[4] != 0};
    if ((uint64_t)len != 5 + (uint64_t)b.n * (b.uu ? 34u : 6u)) return fail(why, "LinksBuffer size");
    std::map<std::string, size_t> byUuid;
    if (b.uu) byUuid = nodesByUuid(g);
    std::string out;
    uint32_t kept = 0;
    for (uint32_t k = 0; k < b.n; ++k) {
        const std::optional<Link> L = linkAt(b, k, byUuid);
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
        keepLink(g, ns, *L, out);
        ++kept;
    }
    std::string hdr((const char*)&kept, 4);
    hdr += std::string("\x00\x03\x00", 3);   // u8 0 (index form), u16 version 3 (FORMATS.md 5.3)
    g.newLinks.push_back({data, hdr + out});
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

// A node's 3.0 parameter block (val) and Mer_ParameterDescription entries (desc), in parameter order
struct NodeParams {
    std::string desc, val;
    uint32_t count = 0;
    std::set<std::string> done;   // parameter names described
};

// One description entry: u8 type, u32 lookup2(name), i32 offset in the block (FORMATS.md 5.3)
inline void describe(NodeParams& p, int type, const std::string& name, int32_t offset)
{
    const uint8_t tb = (uint8_t)type;
    const uint32_t h = lookup2(name);
    p.desc.append((const char*)&tb, 1);
    p.desc.append((const char*)&h, 4);
    p.desc.append((const char*)&offset, 4);
    ++p.count;
}

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
    if (g.T.ports10 && fetchValue && !g.port[v.k].empty()) {
        pn = g.port[v.k];
        ++g.s.fetches;
    }
    if (p.done.count(pn)) return;
    int type = -1;
    if (v.c) {
        auto t = v.c->params.find(pn);
        if (t != v.c->params.end()) type = t->second;
    }
    if (type < 0) type = typeOf(g.f, r);
    if (type < 0) return;
    size_t off = p.val.size();
    if (!encode(g.f, r, type, p.val, g.strs[v.ns])) return;
    const bool presentation = !v.n.type.compare(0, 23, "MissionPresentationList");   // 23 = its length
    const bool presentEvent = pn == "Event" && presentation;
    if (g.T.events10 && type == T_INT && presentEvent) {
        int32_t e = mission::presentEvent30((int32_t)oct::u((const uint8_t*)p.val.data() + off, 4));
        memcpy(&p.val[off], &e, 4);
        ++g.s.events;
    }
    p.done.insert(pn);
    describe(p, type, pn, (int32_t)off);
}

// toggleType10: a ColVisToggle-family node describes Type = its class (FORMATS.md 5.8)
inline void toggleType(Graph& g, const NodeView& v, NodeParams& p)
{
    const std::string& type = v.n.type;
    const bool family = type == "VisibilityToggle" || type == "CollisionToggle" || type == "ColVisToggle";
    const bool wanted = g.T.toggleType10 && !v.n.stub && family;
    if (!wanted || p.done.count("Type")) return;
    std::string& strs = g.strs[v.ns];
    uint32_t so = (uint32_t)strs.size();
    int32_t o = (int32_t)p.val.size();
    p.val.append((const char*)&so, 4);
    p.val.append(8, '\xff');
    strs += type;
    strs.push_back('\0');
    describe(p, T_STRING, "Type", o);
    ++g.s.typed;
}

// The class's pin table on this instance too: the names it lacks (pins10), else all when it lists none
inline void copyPinTable(Graph& g, const NodeView& v)
{
    auto pt = g.pinTables.find(v.n.type);
    bool copied = false, has = false;
    if (pt != g.pinTables.end() && !g.T.pins10) {
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
        g.out.push_back(g.f.intScalar(g.fl, "LeafNameHash", (int32_t)lookup2(g.port[k])));
    g.out.push_back(g.f.intScalar(g.fl, "Mer_ParametersOffset", (int32_t)g.values[v.ns].size()));
    std::string d((const char*)&p.count, 4);
    g.out.push_back(octx::blobRec(g.f, g.fl, "Mer_ParameterDescription", d + p.desc));
    g.values[v.ns] += p.val;
    g.s.params += (int)p.count;
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
        long ref = field(f, b, g.fl, "ReferenceEmbeddedNamespace");
        if (ref >= 0 && field(f, b, g.fl, "MergedLayoutPath") < 0) {
            const uint16_t key = f.addName("MergedLayoutPath");
            add.push_back(f.nameScalar(g.fl, key, f.nameValues(f.recs[ref])[0]));
        }
        if (field(f, b, g.fl, "EnableNetworking") >= 0) add.erase(add.begin());
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

// False (f unchanged) if the file is not a 1.0 graph (already 3.0, no node pool) or is damaged.
inline bool transcode(const Table& T, oct::File& f, Stats* st = nullptr, std::string* why = nullptr)
{
    long pool = f.pool("MeridianNodePool");
    if (pool < 0) return detail::fail(why, "no MeridianNodePool");
    for (auto& r : f.recs)
        if (f.name(r) == "Mer_ParameterDescription") return detail::fail(why, "already 3.0");
    const size_t poolEnd = f.blockEnd(pool);
    detail::Graph g{T, f, (uint16_t)(f.recs[pool].level + 2)};
    if (!detail::collectNodes(f, pool, g.nodes, why) || !detail::collectNamespaces(f, g.fl, g.nodes, g.nss, why))
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

namespace detail {

// Each entry of pool p counted under its first Type; ends hoisted, a rescan per entry cost 132 ms (GR-04)
inline void countTypes(const oct::File& f, size_t p, std::map<std::string, uint32_t>& c)
{
    for (size_t i = p + 1, pe = f.blockEnd(p); i < pe; i = f.blockEnd(i)) {
        if (f.recs[i].level != f.recs[p].level + 1) continue;
        for (size_t j = i + 1, je = f.blockEnd(i); j < je; ++j) {
            if (f.name(f.recs[j]) != "Type") continue;
            const std::optional<std::string> type = firstName(f, f.recs[j]);
            if (type) c[*type]++;   // one holding no name counts nothing (GR-16)
            break;
        }
    }
}

}  // namespace detail

// The .mtc type manifest of a graph (DI3Tools mer.mtc, first Type record): u32 0xffff, u32 node types, u32 link
// types, per type (u32 count, u32 file offset of its name), then the names; node classes then link classes, each
// sorted, without NameSpaceBegin / NameSpaceEnd.
inline std::string mtc(const oct::File& f)
{
    std::vector<std::vector<std::pair<std::string, uint32_t>>> sections;
    for (const char* poolName : {"MeridianNodePool", "MeridianLinkPool"}) {
        std::map<std::string, uint32_t> c;
        long p = f.pool(poolName);
        if (p >= 0) detail::countTypes(f, (size_t)p, c);
        c.erase("NameSpaceBegin");
        c.erase("NameSpaceEnd");
        sections.emplace_back(c.begin(), c.end());
    }
    size_t n = sections[0].size() + sections[1].size();
    uint32_t at = (uint32_t)(12 + 8 * n);
    std::string table, names;
    for (auto& sec : sections)
        for (auto& [t, k] : sec) {
            uint32_t v[2] = {k, at + (uint32_t)names.size()};
            table.append((const char*)v, 8);
            names += t;
            names.push_back('\0');
        }
    uint32_t h[3] = {0xffff, (uint32_t)sections[0].size(), (uint32_t)sections[1].size()};
    return std::string((const char*)h, 12) + table + names;
}

}  // namespace mer
