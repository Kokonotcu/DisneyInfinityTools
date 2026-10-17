// Meridian graphs (FORMATS.md 5.3, 5.4): class table, graph view, links, parameters, .mtc. Uses oct, hash; any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "../base/hash.hpp"
#include "oct.hpp"

namespace mer {

// Meridian's name hash: lookup2 with seed 0 (FORMATS.md 5.3), the same function as dct's (GR-11)
inline uint32_t lookup2(const std::string& s, uint32_t seed = 0)
{
    return hash::lookup2((const uint8_t*)s.data(), s.size(), seed);
}

// Parameter description types (FORMATS.md 5.3)
enum { T_BOOL = 0, T_INT = 1, T_FLOAT = 2, T_STRING = 3, T_VEC3 = 4 };

// A class as 3.0 graphs use it: its pins and its parameters' types
struct Class {
    std::set<std::string> pins;
    std::map<std::string, int> params;
};

// The class table (di1compat_mer.txt, mertable.py): 3.0's registered classes, and pins and parameters per class
struct Table {
    std::set<std::string> registered;
    std::map<std::string, Class> classes;

    // Lines 'R <class>', 'C <class>', 'P <pin>', 'A <parameter> <type>'; false if no class is registered
    bool load(const std::string& text)
    {
        std::istringstream in(text);
        std::string line;
        Class* cur = nullptr;
        while (std::getline(in, line)) {
            if (line.size() < 3 || line[1] != ' ') continue;
            const std::string rest = line.substr(2);
            switch (line[0]) {
            case 'R': registered.insert(rest); break;
            case 'C': cur = &classes[rest]; break;
            case 'P':
                if (cur) cur->pins.insert(rest);
                break;
            case 'A':
                if (cur) addParameter(*cur, rest);
                break;
            }
        }
        return !registered.empty();
    }

private:
    // '<parameter> <type>' into c; an unknown type adds nothing
    static void addParameter(Class& c, const std::string& rest)
    {
        static constexpr std::pair<const char*, int> kTypes[] = {
            {"bool", T_BOOL}, {"int", T_INT}, {"float", T_FLOAT}, {"string", T_STRING}, {"vec3", T_VEC3}};
        size_t sp = rest.find(' ');
        if (sp == std::string::npos) return;
        const std::string type = rest.substr(sp + 1);
        for (const auto& [name, t] : kTypes)
            if (type == name) c.params[rest.substr(0, sp)] = t;
    }
};

namespace detail {
inline bool fail(std::string* why, const char* reason)
{
    if (why) *why = reason;
    return false;
}
}  // namespace detail

// ---- graph view ----

// A node of the node pool: its records [at, end), its class, its namespace (-1: none)
struct Node {
    size_t at, end;
    std::string type;
    int ns = -1;
    bool stub = false;   // a stand-in for a class 3.0 does not register (the transcoder's)
};
// A namespace: the node indices of its Begin and End, and its name
struct NS {
    size_t begin, end;
    std::string name;
};

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
    for (size_t j = n.at + 1; j < n.end; ++j)
        if (f.recs[j].level == fl && f.name(f.recs[j]) == name) return (long)j;
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
        if (!name) return detail::fail(why, "node Type holds no name");
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

// ---- links ----

// The older LinksBuffer Data, after its count: u32 n, u8 uuid form, n u16 codes, then n endpoint pairs
struct IndexLinks {
    const uint8_t* d;
    uint32_t n;
    bool uu;       // endpoints as 16-byte Uuid pairs, else as u16 node indices
    size_t len;
};
struct Link {
    uint16_t code;   // from pin << kFromPinShift | to pin << 4 | kind (FORMATS.md 5.3)
    size_t from, to;
};
inline constexpr int kFromPinShift = 10;
inline constexpr uint16_t kBelowFromPin = 0x3ff;

// A LinksBuffer Data record's payload in the older form; nullopt when shorter than its 5-byte head
inline std::optional<IndexLinks> indexLinks(const oct::Rec& r)
{
    oct::Spec sp = oct::spec(r.type);
    const uint8_t* d = (const uint8_t*)r.pay.data() + sp.cw;
    size_t len = r.pay.size() - sp.cw;
    if (len < 5) return std::nullopt;
    return IndexLinks{d, oct::u(d, 4), d[4] != 0, len};
}

// True when the payload holds exactly its n rows: 6 bytes each, 34 with Uuids. 64-bit: cannot wrap (GR-17)
inline bool sizeMatches(const IndexLinks& b)
{
    return (uint64_t)b.len == 5 + (uint64_t)b.n * (b.uu ? 34u : 6u);
}

// Row k: the uuid form's endpoints through byUuid (nullopt: unresolved), else the index form's
inline std::optional<Link> linkAt(const IndexLinks& b, uint32_t k, const std::map<std::string, size_t>& byUuid)
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

// One 3.0 row: u16 code, u16 from, u16 to, positions in the namespace (node index - Begin - 1)
inline void appendRow(std::string& rows, uint16_t code, size_t from, size_t to)
{
    uint16_t row[3] = {code, (uint16_t)from, (uint16_t)to};
    rows.append((const char*)row, 6);
}

// 3.0's LinksBuffer Data (version 3): u32 n, u8 0, u16 3, then the n rows
inline std::string version3Links(uint32_t n, const std::string& rows)
{
    std::string hdr((const char*)&n, 4);
    hdr += std::string("\x00\x03\x00", 3);
    return hdr + rows;
}

// ---- parameters ----

// A node's parameter block (val) and Mer_ParameterDescription entries (desc), in parameter order
struct ParamBlock {
    std::string desc, val;
    uint32_t count = 0;
};

// One description entry: u8 type, u32 lookup2(name), i32 offset in the block (FORMATS.md 5.3)
inline void describe(ParamBlock& p, int type, const std::string& name, int32_t offset)
{
    const uint8_t tb = (uint8_t)type;
    const uint32_t h = lookup2(name);
    p.desc.append((const char*)&tb, 1);
    p.desc.append((const char*)&h, 4);
    p.desc.append((const char*)&offset, 4);
    ++p.count;
}

// Mer_ParameterDescription's bytes: u32 count, then the entries
inline std::string descriptionBytes(const ParamBlock& p) { return std::string((const char*)&p.count, 4) + p.desc; }

// A string parameter's value: u32 offset into the namespace's strings, 8 x FF; the string appended there
inline void appendString(std::string& val, std::string& strings, const std::string& s)
{
    uint32_t so = (uint32_t)strings.size();
    val.append((const char*)&so, 4);
    val.append(8, '\xff');
    strings += s;
    strings.push_back('\0');
}

// One parameter value from a record as the given type; false if the record cannot be read so
inline bool encode(const oct::File& f, const oct::Rec& r, int type, std::string& val, std::string& strings)
{
    oct::Spec s = oct::spec(r.type);
    std::vector<double> nums;
    if (s.kind == oct::INT) for (int32_t v : f.ints(r)) nums.push_back(v);
    else if (s.kind == oct::FLOAT) {
        size_t n = s.shape == oct::LIST ? oct::u((const uint8_t*)r.pay.data() + 2 * s.keyed, s.cw) : 1;
        const uint8_t* p = (const uint8_t*)r.pay.data() + 2 * s.keyed + s.cw;
        for (size_t i = 0; i < n; ++i) {
            float x;
            memcpy(&x, p + 4 * i, 4);
            nums.push_back(x);
        }
    }
    auto put4 = [&](uint32_t v) { val.append((const char*)&v, 4); };
    auto putFloat = [&](double d) {
        float x = (float)d;
        val.append((const char*)&x, 4);
    };
    const size_t want = type == T_VEC3 ? 3 : 1;
    const bool number = type == T_BOOL || type == T_INT || type == T_FLOAT || type == T_VEC3;
    if (number && nums.size() != want) return false;
    switch (type) {
    case T_BOOL: put4(nums[0] != 0); return true;
    case T_INT: put4((uint32_t)(int32_t)nums[0]); return true;
    case T_FLOAT: putFloat(nums[0]); return true;
    case T_VEC3:
        for (double d : nums) putFloat(d);
        return true;
    case T_STRING:
        if (s.kind != oct::NAME || s.shape != oct::SCALAR) return false;
        appendString(val, strings, f.nameValues(r).back());
        return true;
    }
    return false;
}

// The parameter type a record has of its own (-1: none)
inline int typeOf(const oct::Rec& r)
{
    oct::Spec s = oct::spec(r.type);
    if (s.keyed) return -1;
    if (s.shape == oct::SCALAR && s.kind == oct::INT) return T_INT;
    if (s.shape == oct::SCALAR && s.kind == oct::FLOAT) return T_FLOAT;
    if (s.shape == oct::SCALAR && s.kind == oct::NAME) return T_STRING;
    if (s.shape == oct::LIST && s.kind == oct::FLOAT && oct::u((const uint8_t*)r.pay.data(), s.cw) == 3) return T_VEC3;
    return -1;
}

// ---- the type manifest ----

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

// The .mtc of a graph (FORMATS.md 5.4): node classes then link classes, each sorted, without NameSpaceBegin / End
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
