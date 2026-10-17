// The render table (di1compat_render.txt, rentable.py): 3.0 shaders, donors, vertex plans, shader rules.
// Loaded once at boot, read-only after.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "shd.hpp"
#include "vtxf.hpp"

namespace rentable {

// A 3.0 shader: block sizes, texture slots, the vformats it declares, each slot's role ("" when not a material's)
struct Shader { int A = 0, B = 0, slots = 0; std::set<uint32_t> vf; std::vector<std::string> roles; };

// One step of a vertex plan: copy ('C'), convert between element types ('X'), or constant bytes ('K')
constexpr char kCopy = 'C', kConvert = 'X', kConstant = 'K';
constexpr int kMaxComponents = 4;
struct Op {
    int dsi = 0, doff = 0, size = 0;   // destination stream, offset, bytes
    char kind = 0;
    int ssi = 0, soff = 0;             // source stream and offset
    std::string st, dt;                // kConvert: element type names
    vtxf::ElementType sv{}, dv{};
    std::vector<double> dflt;          // kConvert: defaults for components the source lacks
    std::string bytes;                 // kConstant
};
// A vertex plan: source and destination stream strides, ops per vertex
struct Plan { std::vector<int> ss, ds; std::vector<Op> ops; };
// A shipped 3.0 material: its engine path, extract path, index in its bundle, how many materials use the shader
struct Ref { std::string engine, extract; int idx = 0, count = 0; };

// The translated shaders' 3.0-side rules (V, W, Q lines)
struct ShaderRules {
    std::unordered_map<std::string, std::array<int, 3>> globals;    // name -> buffer, offset, size
    std::map<std::tuple<uint32_t, int, int>, uint32_t> wordRules;  // (technique, depth kind, X) -> bits
    std::map<std::pair<uint32_t, uint32_t>, int> passByte;         // (technique, 3.0 word) -> +0x1c (RE-24)
};

// One line per record, tagged S D M F N V W Q G T P R (rentable.py)
struct Table {
    std::unordered_map<std::string, Shader> shaders;                // S
    std::map<std::string, Ref> donors;                              // D, by source shader; ordered: generic tier order
    std::unordered_map<std::string, Ref> matindex;                  // M: source material name -> the shipped one
    std::set<std::string> filled, notReal;                          // F: roles materials fill; N: not really shipped
    std::map<std::string, std::vector<int>> genericSlots;           // G: source shader -> slots in order
    std::unordered_map<uint32_t, std::vector<uint32_t>> targets;    // T: source vformat -> 3.0 vformats
    std::map<std::pair<uint32_t, uint32_t>, Plan> plans;            // P: (source, 3.0 vformat) -> plan
    std::map<std::pair<uint32_t, uint32_t>, Plan> skinPlans;        // R: into the target's 'skin' layout, bones kept
    ShaderRules shaderRules;                                        // V W Q

    // false: no shaders or plans, or a corrupt line (later lines unread, earlier ones kept)
    bool load(const std::string& text);
};

namespace detail {

// A count in a table line: never negative, never more than the line has characters (RE-20)
inline bool countOk(int n, size_t limit) { return n >= 0 && (size_t)n <= limit; }

inline uint32_t hex32(const std::string& s) { return (uint32_t)std::stoul(s, nullptr, 16); }

inline bool loadShader(Table& T, std::istringstream& t, size_t limit)
{
    std::string name;
    Shader s;
    int n = 0;
    t >> name >> s.A >> s.B >> s.slots >> n;
    if (!countOk(n, limit)) return false;
    for (int i = 0; i < n; ++i) {
        std::string h;
        t >> h;
        s.vf.insert(hex32(h));
    }
    t >> n;
    if (!countOk(n, limit)) return false;
    for (int i = 0; i < n; ++i) {
        std::string r;
        t >> r;
        s.roles.push_back(r == "-" ? "" : r);
    }
    T.shaders[name] = s;
    return true;
}

// D and M lines: a malformed line is dropped, never half-read (extract paths are read with substr(2))
inline bool loadDonor(Table& T, std::istringstream& t)
{
    std::string sh;
    Ref r;
    t >> sh >> r.engine >> r.idx >> r.count >> r.extract;
    if (!t.fail() && r.extract.size() > 2) T.donors[sh] = r;
    return true;
}

inline bool loadMaterialRef(Table& T, std::istringstream& t)
{
    std::string g;
    Ref r;
    t >> g >> r.engine >> r.idx >> r.extract;
    if (!t.fail() && r.extract.size() > 2) T.matindex[g] = r;
    return true;
}

inline bool loadName(std::istringstream& t, std::set<std::string>& into)
{
    std::string s;
    t >> s;
    into.insert(s);
    return true;
}

inline bool loadGlobal(Table& T, std::istringstream& t)
{
    std::string n;
    int cb = 0, off = 0, sz = 0;
    t >> n >> cb >> off >> sz;
    if (cb < 0 || cb >= (int)shd::kConstantBuffers) return false;
    T.shaderRules.globals[n] = {cb, off, sz};
    return true;
}

inline bool loadWordRule(Table& T, std::istringstream& t)
{
    std::string th, bits;
    int kind = 0, x = 0;
    t >> th >> kind >> x >> bits;
    T.shaderRules.wordRules[{hex32(th), kind, x}] = hex32(bits);
    return true;
}

inline bool loadPassByte(Table& T, std::istringstream& t)
{
    std::string th, w;
    int v = 0;
    t >> th >> w >> v;
    T.shaderRules.passByte[{hex32(th), hex32(w)}] = v;
    return true;
}

// slot indices: the material transcode indexes the material's slots with them
inline bool loadGenericSlots(Table& T, std::istringstream& t)
{
    std::string sh;
    int i = 0;
    t >> sh;
    while (t >> i) {
        if (i < 0) return false;
        T.genericSlots[sh].push_back(i);
    }
    return true;
}

inline bool loadTargets(Table& T, std::istringstream& t)
{
    std::string a, b;
    t >> a;
    auto& v = T.targets[hex32(a)];
    while (t >> b) v.push_back(hex32(b));
    return true;
}

inline bool loadStrides(std::istringstream& t, size_t limit, std::vector<int>& v)
{
    int n = 0;
    t >> n;
    if (!countOk(n, limit)) return false;
    v.resize(n);
    for (auto& x : v) t >> x;
    return true;
}

inline bool loadOp(std::istringstream& t, size_t limit, Op& o)
{
    t >> o.dsi >> o.doff >> o.size >> o.kind;
    if (o.kind == kCopy) {
        t >> o.ssi >> o.soff;
        return true;
    }
    if (o.kind == kConvert) {
        int m = 0;
        t >> o.ssi >> o.soff >> o.st >> o.dt >> m;
        if (!countOk(m, limit)) return false;
        o.dflt.resize(m);
        for (auto& d : o.dflt) t >> d;
        const bool types = vtxf::elementType(o.st, o.sv) && vtxf::elementType(o.dt, o.dv);
        return types && o.sv.n <= kMaxComponents && o.dv.n <= kMaxComponents;
    }
    std::string hex;
    t >> hex;
    if (hex == "-") return true;
    for (size_t j = 0; j + 1 < hex.size(); j += 2) o.bytes.push_back((char)std::stoi(hex.substr(j, 2), nullptr, 16));
    return true;
}

// The vertex conversion writes [doff, doff+size) of stream dsi and reads inside source stream ssi (vtxf.plan)
inline bool opOk(const Plan& p, const Op& o)
{
    const bool stream = o.dsi >= 0 && (size_t)o.dsi < p.ds.size();
    const bool span = o.doff >= 0 && o.size >= 0;
    if (!stream || !span || o.doff + (int64_t)o.size > p.ds[o.dsi]) return false;
    if (o.kind == kConstant) return true;
    if (o.kind != kCopy && o.kind != kConvert) return false;
    const int64_t read = o.kind == kCopy ? o.size : (int64_t)o.sv.n * o.sv.csize;
    const bool from = o.ssi >= 0 && (size_t)o.ssi < p.ss.size() && o.soff >= 0;
    return from && o.soff + read <= p.ss[o.ssi];
}

inline bool planOk(const Plan& p)
{
    const auto negative = [](int s) { return s < 0; };
    const bool sources = std::none_of(p.ss.begin(), p.ss.end(), negative);
    if (!sources || std::any_of(p.ds.begin(), p.ds.end(), negative)) return false;
    return std::all_of(p.ops.begin(), p.ops.end(), [&](const Op& o) { return opOk(p, o); });
}

// P and R lines: source strides, destination strides, ops; a plan outside its streams rejects the table
inline bool loadPlan(std::istringstream& t, size_t limit, std::map<std::pair<uint32_t, uint32_t>, Plan>& into)
{
    std::string a, b;
    Plan p;
    int n = 0;
    t >> a >> b;
    if (!loadStrides(t, limit, p.ss) || !loadStrides(t, limit, p.ds)) return false;
    t >> n;
    if (!countOk(n, limit)) return false;
    for (int i = 0; i < n; ++i) {
        Op o;
        if (!loadOp(t, limit, o)) return false;
        p.ops.push_back(o);
    }
    if (!planOk(p)) return false;
    into[{hex32(a), hex32(b)}] = p;
    return true;
}

// One line by its tag; any other first word is not a table line
inline bool loadLine(Table& T, const std::string& line)
{
    std::istringstream t(line);
    std::string k;
    t >> k;
    if (k.size() != 1) return true;
    switch (k[0]) {
    case 'S': return loadShader(T, t, line.size());
    case 'D': return loadDonor(T, t);
    case 'M': return loadMaterialRef(T, t);
    case 'F': return loadName(t, T.filled);
    case 'N': return loadName(t, T.notReal);
    case 'V': return loadGlobal(T, t);
    case 'W': return loadWordRule(T, t);
    case 'Q': return loadPassByte(T, t);
    case 'G': return loadGenericSlots(T, t);
    case 'T': return loadTargets(T, t);
    case 'P': return loadPlan(t, line.size(), T.plans);
    case 'R': return loadPlan(t, line.size(), T.skinPlans);
    default: return true;
    }
}

}  // namespace detail

// Runs at boot outside any try: a corrupt number (std::stoul) or count is a load failure, never a throw.
inline bool Table::load(const std::string& text)
{
    try {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
            if (!detail::loadLine(*this, line)) return false;
    } catch (const std::exception&) {
        return false;
    }
    return !shaders.empty() && !plans.empty();
}

}  // namespace rentable
