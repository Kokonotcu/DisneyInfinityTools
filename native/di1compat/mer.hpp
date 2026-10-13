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
//   LeafName    + LeafNameHash = lookup2(LeafName) (3.0 port sub-nodes carry the hash)
//   links       u32 n, u8 0, u16 3, n x (u16 code, u16 from, u16 to), from/to = node index - Begin - 1
//   classes 3.0 does not register (DeliveryNode, Activate/SpawnDecorationSockets + 8 mission classes) would leave
//               no node slot and shift every later index (verify-meridian claim 6: out-of-bounds link writes), so
//               until they are ported (MER-3/6) such a node becomes a pinless NodeGroup and its links are dropped.
#pragma once
#include <map>
#include <set>
#include <sstream>
#include "oct.hpp"

namespace mer {

inline uint32_t lookup2(const std::string& s, uint32_t seed = 0)
{
    auto mix = [](uint32_t& a, uint32_t& b, uint32_t& c) {
        a -= b; a -= c; a ^= c >> 13;  b -= c; b -= a; b ^= a << 8;  c -= a; c -= b; c ^= b >> 13;
        a -= b; a -= c; a ^= c >> 12;  b -= c; b -= a; b ^= a << 16; c -= a; c -= b; c ^= b >> 5;
        a -= b; a -= c; a ^= c >> 3;   b -= c; b -= a; b ^= a << 10; c -= a; c -= b; c ^= b >> 15;
    };
    const uint8_t* k = (const uint8_t*)s.data();
    uint32_t n = (uint32_t)s.size(), a = 0x9e3779b9, b = a, c = seed, len = n;
    auto w = [&](const uint8_t* p) { return oct::u(p, 4); };
    for (; len >= 12; k += 12, len -= 12) { a += w(k); b += w(k + 4); c += w(k + 8); mix(a, b, c); }
    uint8_t t[12] = {};
    memcpy(t, k, len);
    a += w(t); b += w(t + 4); c += n + (w(t + 8) << 8);
    mix(a, b, c);
    return c;
}

enum { T_BOOL = 0, T_INT = 1, T_FLOAT = 2, T_STRING = 3, T_VEC3 = 4 };
struct Class { std::set<std::string> pins; std::map<std::string, int> params; };
struct Table {
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

inline bool isGeneric(const std::string& n)
{
    static const std::set<std::string> g = {"Type", "Uuid", "IsProxy", "MerSpawnGroup", "NumInLinks", "NumOutLinks", "IsRootObject",
                                            "LeafName", "Parent", "AuthoredName", "Mer_NoConnections", "LeafNameHash"};
    return g.count(n) > 0;
}

inline oct::Rec blob(oct::File& f, uint16_t level, const std::string& n, const std::string& data)
{
    int cw = data.size() < 0x100 ? 1 : data.size() < 0x10000 ? 2 : 3;
    oct::Rec r{level, (uint16_t)(oct::SCALAR | oct::BYTE << 3 | (cw - 1) << 6), f.addName(n), {}};
    oct::File::put(r.pay, (uint32_t)data.size(), cw);
    r.pay += data;
    return r;
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

struct Stats { int nodes = 0, params = 0, pinsCopied = 0, stubs = 0, links = 0, linksDropped = 0, namespaces = 0; };

// False (f unchanged) if the file is not a 1.0 graph (already has descriptions, or no node pool).
inline bool transcode(const Table& T, oct::File& f, Stats* st = nullptr, std::string* why = nullptr)
{
    Stats s;
    long pool = f.pool("MeridianNodePool");
    if (pool < 0) { if (why) *why = "no MeridianNodePool"; return false; }
    for (auto& r : f.recs) if (f.name(r) == "Mer_ParameterDescription") { if (why) *why = "already 3.0"; return false; }
    size_t poolEnd = f.blockEnd(pool);
    uint16_t nodeLevel = (uint16_t)(f.recs[pool].level + 1), fl = (uint16_t)(nodeLevel + 1);

    struct Node { size_t at, end; std::string type; int ns = -1; bool stub = false; };
    std::vector<Node> nodes;
    for (size_t i = pool + 1; i < poolEnd; i = f.blockEnd(i)) {
        if (f.recs[i].level != nodeLevel) continue;
        Node n{i, f.blockEnd(i), ""};
        for (size_t j = i + 1; j < n.end; ++j)
            if (f.recs[j].level == fl && f.name(f.recs[j]) == "Type") { n.type = f.nameValues(f.recs[j])[0]; break; }
        nodes.push_back(n);
    }
    struct NS { size_t begin, end; std::string name; };
    std::vector<NS> nss;
    for (size_t k = 0; k < nodes.size(); ++k) {
        if (nodes[k].type == "NameSpaceBegin") {
            std::string name;
            for (size_t j = nodes[k].at + 1; j < nodes[k].end; ++j)
                if (f.name(f.recs[j]) == "Name") name = f.nameValues(f.recs[j])[0];
            nss.push_back({k, k, name});
        } else if (nodes[k].type == "NameSpaceEnd" && !nss.empty()) nss.back().end = k;
        if (!nss.empty()) nodes[k].ns = (int)nss.size() - 1;
    }
    for (auto& n : nodes)
        n.stub = n.type != "NameSpaceBegin" && n.type != "NameSpaceEnd" && !T.registered.count(n.type);

    auto field = [&](const Node& n, const char* name) -> long {
        for (size_t j = n.at + 1; j < n.end; ++j) if (f.recs[j].level == fl && f.name(f.recs[j]) == name) return (long)j;
        return -1;
    };
    auto isInt = [&](const oct::Rec& r) { oct::Spec sp = oct::spec(r.type); return sp.shape == oct::SCALAR && sp.kind == oct::INT && !sp.keyed; };
    // the class's pin records: from its first instance listing any (1.0 writes the table once per file)
    std::map<std::string, std::vector<oct::Rec>> pinTables;
    for (auto& n : nodes) {
        auto c = T.classes.find(n.type);
        if (c == T.classes.end() || pinTables.count(n.type)) continue;
        std::vector<oct::Rec> pins;
        for (size_t j = n.at + 1; j < n.end; ++j)
            if (f.recs[j].level == fl && isInt(f.recs[j]) && c->second.pins.count(f.name(f.recs[j]))) pins.push_back(f.recs[j]);
        if (!pins.empty()) pinTables[n.type] = pins;
    }

    // links first: dropped ones change NumInLinks / NumOutLinks
    std::map<size_t, int> dIn, dOut;   // node index -> links dropped
    struct Out { size_t at; std::string data; };
    std::vector<Out> newLinks;
    long lpool = f.pool("MeridianLinkPool");
    if (lpool >= 0) {
        std::map<std::string, int> seen;   // namespace name -> times matched
        int curNs = -1;
        for (size_t i = lpool + 1; i < f.blockEnd(lpool); i = f.blockEnd(i)) {
            if (f.recs[i].level != f.recs[lpool].level + 1) continue;
            std::string type, name;
            long data = -1;
            for (size_t j = i + 1; j < f.blockEnd(i); ++j) {
                const std::string& n = f.name(f.recs[j]);
                if (n == "Type") type = f.nameValues(f.recs[j])[0];
                else if (n == "Name") name = f.nameValues(f.recs[j])[0];
                else if (n == "Data") data = (long)j;
            }
            if (type == "NameSpaceBegin") {
                int want = seen[name]++, k = 0;
                curNs = -1;
                for (size_t q = 0; q < nss.size(); ++q) if (nss[q].name == name && k++ == want) { curNs = (int)q; break; }
            } else if (type == "LinksBuffer" && data >= 0 && curNs >= 0) {
                const oct::Rec& r = f.recs[data];
                oct::Spec sp = oct::spec(r.type);
                const uint8_t* d = (const uint8_t*)r.pay.data() + sp.cw;
                size_t len = r.pay.size() - sp.cw;
                if (len < 5) continue;
                uint32_t n = oct::u(d, 4);
                bool uu = d[4] != 0;
                if (len != 5 + n * (uu ? 34u : 6u)) { if (why) *why = "LinksBuffer size"; return false; }
                std::map<std::string, size_t> byUuid;
                if (uu)
                    for (size_t k = 0; k < nodes.size(); ++k) {
                        long q = field(nodes[k], "Uuid");
                        if (q >= 0) byUuid[f.recs[q].pay.substr(oct::spec(f.recs[q].type).cw, 16)] = k;
                    }
                const NS& ns = nss[curNs];
                std::string out;
                uint32_t kept = 0;
                for (uint32_t k = 0; k < n; ++k) {
                    uint16_t code = (uint16_t)oct::u(d + 5 + 2 * k, 2);
                    size_t a, z;
                    if (uu) {
                        auto ia = byUuid.find(std::string((const char*)d + 5 + 2 * n + 32 * k, 16));
                        auto iz = byUuid.find(std::string((const char*)d + 5 + 2 * n + 32 * k + 16, 16));
                        if (ia == byUuid.end() || iz == byUuid.end()) { ++s.linksDropped; continue; }
                        a = ia->second; z = iz->second;
                    } else {
                        a = oct::u(d + 5 + 2 * n + 4 * k, 2);
                        z = oct::u(d + 5 + 2 * n + 4 * k + 2, 2);
                    }
                    bool inside = a > ns.begin && a < ns.end && z > ns.begin && z < ns.end && a < nodes.size() && z < nodes.size();
                    if (!inside || nodes[a].stub || nodes[z].stub) { ++dOut[a]; ++dIn[z]; ++s.linksDropped; continue; }
                    uint16_t row[3] = {code, (uint16_t)(a - ns.begin - 1), (uint16_t)(z - ns.begin - 1)};
                    out.append((const char*)row, 6);
                    ++kept;
                }
                std::string hdr((const char*)&kept, 4);
                hdr += std::string("\x00\x03\x00", 3);
                newLinks.push_back({(size_t)data, hdr + out});
                s.links += (int)kept;
            }
        }
    }

    // rebuild the node pool
    std::vector<oct::Rec> outRecs(f.recs.begin(), f.recs.begin() + pool + 1);
    std::vector<std::string> values(nss.size()), strs(nss.size());
    std::vector<size_t> beginAt(nss.size());   // position of each Begin's last record in outRecs
    for (size_t k = 0; k < nodes.size(); ++k) {
        Node& n = nodes[k];
        outRecs.push_back(f.recs[n.at]);
        if (n.type == "NameSpaceBegin" || n.type == "NameSpaceEnd") {
            for (size_t j = n.at + 1; j < n.end; ++j) {
                oct::Rec r = f.recs[j];
                if (n.type == "NameSpaceBegin" && f.name(r) == "RootNodeCount") f.setInt(r, (int64_t)(nss[n.ns].end - nss[n.ns].begin - 1));
                outRecs.push_back(r);
            }
            if (n.type == "NameSpaceBegin") { beginAt[n.ns] = outRecs.size(); ++s.namespaces; }
            continue;
        }
        ++s.nodes;
        int ns = n.ns < 0 ? 0 : n.ns;
        auto cls = T.classes.find(n.type);
        const Class* c = cls == T.classes.end() ? nullptr : &cls->second;
        std::set<std::string> names, valued;
        for (size_t j = n.at + 1; j < n.end; ++j) {
            if (f.recs[j].level != fl) continue;
            const std::string& nm = f.name(f.recs[j]);
            names.insert(nm);
            if (nm.size() > 6 && !nm.compare(nm.size() - 6, 6, "_value")) valued.insert(nm.substr(0, nm.size() - 6));
        }
        bool hasPins = false;
        for (size_t j = n.at + 1; j < n.end; ++j)
            if (f.recs[j].level == fl && c && c->pins.count(f.name(f.recs[j]))) hasPins = true;

        std::string desc, val;
        uint32_t count = 0;
        std::set<std::string> done;
        bool firstType = true;
        for (size_t j = n.at + 1; j < n.end; ++j) {
            oct::Rec r = f.recs[j];
            std::string nm = f.name(r);
            if (n.stub && r.level == fl && nm == "Type" && firstType) {   // pinless stand-in, links dropped
                r.pay.clear();
                oct::File::put(r.pay, f.addName("NodeGroup"), 2);
                ++s.stubs;
            }
            if (n.stub && r.level == fl && (nm == "NumInLinks" || nm == "NumOutLinks")) f.setInt(r, 0);
            else if (r.level == fl && nm == "NumInLinks" && dIn.count(k)) f.setInt(r, f.ints(r)[0] - dIn[k]);
            else if (r.level == fl && nm == "NumOutLinks" && dOut.count(k)) f.setInt(r, f.ints(r)[0] - dOut[k]);
            if (!(n.stub && r.level == fl && c && c->pins.count(nm))) outRecs.push_back(r);
            if (r.level != fl || n.stub) continue;
            if (nm == "Type") {
                if (firstType) { firstType = false; continue; }
                // a second Type: the parameter 'Type'
            } else if (isGeneric(nm)) continue;
            bool pin = isInt(r) && ((c && c->pins.count(nm)) || valued.count(nm));
            if (pin) continue;
            std::string pn = nm;
            if (pn.size() > 6 && !pn.compare(pn.size() - 6, 6, "_value")) pn = pn.substr(0, pn.size() - 6);
            if (done.count(pn)) continue;
            int type = -1;
            if (c) { auto t = c->params.find(pn); if (t != c->params.end()) type = t->second; }
            if (type < 0) type = typeOf(f, r);
            if (type < 0) continue;
            size_t off = val.size();
            if (!encode(f, r, type, val, strs[ns])) continue;
            done.insert(pn);
            uint8_t tb = (uint8_t)type;
            uint32_t h = lookup2(pn);
            int32_t o = (int32_t)off;
            desc.append((const char*)&tb, 1);
            desc.append((const char*)&h, 4);
            desc.append((const char*)&o, 4);
            ++count;
        }
        if (!n.stub && !hasPins && c) {   // the class's pin table on this instance too
            auto pt = pinTables.find(n.type);
            if (pt != pinTables.end()) { outRecs.insert(outRecs.end(), pt->second.begin(), pt->second.end()); ++s.pinsCopied; }
        }
        long leaf = field(n, "LeafName");
        if (leaf >= 0 && !names.count("LeafNameHash"))
            outRecs.push_back(f.intScalar(fl, "LeafNameHash", (int32_t)lookup2(f.nameValues(f.recs[leaf])[0])));
        outRecs.push_back(f.intScalar(fl, "Mer_ParametersOffset", (int32_t)values[ns].size()));
        std::string d((const char*)&count, 4);
        outRecs.push_back(blob(f, fl, "Mer_ParameterDescription", d + desc));
        values[ns] += val;
        s.params += (int)count;
    }
    // namespace blobs, inserted after each Begin's records (back to front keeps earlier positions valid)
    for (size_t q = nss.size(); q-- > 0;) {
        std::vector<oct::Rec> add = {f.intScalar(fl, "EnableNetworking", 1), blob(f, fl, "ParameterValues", values[q]),
                                     blob(f, fl, "ParameterStrings", strs[q])};
        const Node& b = nodes[nss[q].begin];
        long ref = field(b, "ReferenceEmbeddedNamespace");
        if (ref >= 0 && field(b, "MergedLayoutPath") < 0)
            add.push_back(f.nameScalar(fl, f.addName("MergedLayoutPath"), f.nameValues(f.recs[ref])[0]));
        if (field(b, "EnableNetworking") >= 0) add.erase(add.begin());
        outRecs.insert(outRecs.begin() + beginAt[q], add.begin(), add.end());
    }
    // the rest of the file, links rewritten
    for (size_t i = poolEnd; i < f.recs.size(); ++i) {
        oct::Rec r = f.recs[i];
        for (auto& L : newLinks)
            if (L.at == i) { oct::Rec b = blob(f, r.level, "Data", L.data); b.name = r.name; r = b; }
        outRecs.push_back(r);
    }
    f.recs = std::move(outRecs);
    if (st) *st = s;
    return true;
}

// The .mtc type manifest of a graph (DI3Tools mer.mtc, first Type record): u32 0xffff, u32 node types, u32 link
// types, per type (u32 count, u32 file offset of its name), then the names; node classes then link classes, each
// sorted, without NameSpaceBegin / NameSpaceEnd.
inline std::string mtc(const oct::File& f)
{
    std::vector<std::vector<std::pair<std::string, uint32_t>>> sections;
    for (const char* poolName : {"MeridianNodePool", "MeridianLinkPool"}) {
        std::map<std::string, uint32_t> c;
        long p = f.pool(poolName);
        for (size_t i = p + 1; p >= 0 && i < f.blockEnd(p); i = f.blockEnd(i)) {
            if (f.recs[i].level != f.recs[p].level + 1) continue;
            for (size_t j = i + 1; j < f.blockEnd(i); ++j)
                if (f.name(f.recs[j]) == "Type") { c[f.nameValues(f.recs[j])[0]]++; break; }
        }
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
