// Load-time render transcode of a 1.0 world (plan REN-5 + the geometry half of native30): the 1.0 materials are
// redirected onto 3.0 shaders and the 1.0 vertices into vertex formats those shaders take, in memory. C++ port of
// DI3Tools autobundle.resolve + conv.convert_geometry/build_buffers; octtest --render checks it byte for byte against
// them. The 3.0-side knowledge comes from di1compat_render.txt (rentable.py); the 1.0 data from the 1.0 files.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include "base/bytes.hpp"
#include "oct.hpp"
#include "gen30.hpp"

namespace render {

constexpr uint32_t UNSET = 0xffffffff;

// ---- vertex element types (vtxf.TYPES / decode / encode, Python's rounding exactly) ----
struct VType { char code; int n, csize; double norm; };
inline bool vtype(const std::string& t, VType& v)
{
    static const std::map<std::string, VType> T = {
        {"FLOAT32_2", {'f', 2, 4, 0}}, {"FLOAT32_3", {'f', 3, 4, 0}}, {"FLOAT32_4", {'f', 4, 4, 0}},
        {"FLOAT16_2", {'e', 2, 2, 0}}, {"FLOAT16_4", {'e', 4, 2, 0}}, {"U8_4_NORMALIZED", {'B', 4, 1, 255.0}},
        {"S16_4_NORMALIZED", {'h', 4, 2, 32767.0}}, {"U8_4", {'B', 4, 1, 0}}, {"U16_2", {'H', 2, 2, 0}}};
    auto it = T.find(t);
    if (it == T.end()) return false;
    v = it->second;
    return true;
}

// ---- the table ----
struct Shader { int A = 0, B = 0, slots = 0; std::set<uint32_t> vf; std::vector<std::string> roles; };
struct Op {
    int dsi = 0, doff = 0, size = 0;
    char kind = 0;
    int ssi = 0, soff = 0;
    std::string st, dt;
    VType sv{}, dv{};
    std::vector<double> dflt;
    std::string bytes;
};
struct Plan { std::vector<int> ss, ds; std::vector<Op> ops; };
struct Ref { std::string engine, extract; int idx = 0, count = 0; };

struct Table {
    std::unordered_map<std::string, Shader> shaders;
    std::map<std::string, Ref> donors;                 // ordered: the generic tier iterates them
    std::unordered_map<std::string, Ref> matindex;
    std::set<std::string> filled, notReal;
    std::map<std::string, std::vector<int>> genericSlots;
    std::unordered_map<uint32_t, std::vector<uint32_t>> targets;
    std::map<std::pair<uint32_t, uint32_t>, Plan> plans;
    std::map<std::pair<uint32_t, uint32_t>, Plan> skinPlans;   // R lines: into the target's 'skin' layout (bones kept)
    gen30::Tables shaders10;                           // real 1.0 shaders: V / W / Q lines (gen30)

    // false: no shaders or plans, or a corrupt line (later lines unread, earlier ones kept)
    bool load(const std::string& text);
};

namespace detail {

// A count in a table line: never negative, never more than the line has characters (RE-20)
inline bool countOk(int n, size_t limit) { return n >= 0 && (size_t)n <= limit; }

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
        s.vf.insert((uint32_t)std::stoul(h, nullptr, 16));
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
    if (cb < 0 || cb >= gen30::kConstantBuffers) return false;
    T.shaders10.globals[n] = {cb, off, sz};
    return true;
}

inline bool loadWordRule(Table& T, std::istringstream& t)
{
    std::string th, bits;
    int kind = 0, x = 0;
    t >> th >> kind >> x >> bits;
    T.shaders10.wordRules[{(uint32_t)std::stoul(th, nullptr, 16), kind, x}] = (uint32_t)std::stoul(bits, nullptr, 16);
    return true;
}

inline bool loadPassByte(Table& T, std::istringstream& t)
{
    std::string th, w;
    int v = 0;
    t >> th >> w >> v;
    T.shaders10.passByte[{(uint32_t)std::stoul(th, nullptr, 16), (uint32_t)std::stoul(w, nullptr, 16)}] = v;
    return true;
}

// slot indices: transcodeMaterials indexes the material's slots with them
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
    auto& v = T.targets[(uint32_t)std::stoul(a, nullptr, 16)];
    while (t >> b) v.push_back((uint32_t)std::stoul(b, nullptr, 16));
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
    if (o.kind == 'C') {
        t >> o.ssi >> o.soff;
        return true;
    }
    if (o.kind == 'X') {
        int m = 0;
        t >> o.ssi >> o.soff >> o.st >> o.dt >> m;
        if (!countOk(m, limit)) return false;
        o.dflt.resize(m);
        for (auto& d : o.dflt) t >> d;
        const bool types = vtype(o.st, o.sv) && vtype(o.dt, o.dv);
        return types && o.sv.n <= 4 && o.dv.n <= 4;
    }
    std::string hex;
    t >> hex;
    if (hex == "-") return true;
    for (size_t j = 0; j + 1 < hex.size(); j += 2) o.bytes.push_back((char)std::stoi(hex.substr(j, 2), nullptr, 16));
    return true;
}

// convertVertex writes [doff, doff+size) of stream dsi and reads inside source stream ssi (vtxf.plan builds them so)
inline bool opOk(const Plan& p, const Op& o)
{
    const bool stream = o.dsi >= 0 && (size_t)o.dsi < p.ds.size();
    const bool span = o.doff >= 0 && o.size >= 0;
    if (!stream || !span || o.doff + (int64_t)o.size > p.ds[o.dsi]) return false;
    if (o.kind == 'K') return true;
    if (o.kind != 'C' && o.kind != 'X') return false;
    const int64_t read = o.kind == 'C' ? o.size : (int64_t)o.sv.n * o.sv.csize;
    const bool from = o.ssi >= 0 && (size_t)o.ssi < p.ss.size() && o.soff >= 0;
    return from && o.soff + read <= p.ss[o.ssi];
}

inline bool planOk(const Plan& p)
{
    const auto negative = [](int s) { return s < 0; };
    if (std::any_of(p.ss.begin(), p.ss.end(), negative) || std::any_of(p.ds.begin(), p.ds.end(), negative)) return false;
    return std::all_of(p.ops.begin(), p.ops.end(), [&](const Op& o) { return opOk(p, o); });
}

// P and R lines: source strides, destination strides, ops; a plan outside its streams rejects the table
inline bool loadPlan(Table& T, std::istringstream& t, size_t limit, char kind)
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
    auto& into = kind == 'R' ? T.skinPlans : T.plans;
    into[{(uint32_t)std::stoul(a, nullptr, 16), (uint32_t)std::stoul(b, nullptr, 16)}] = p;
    return true;
}

// one line by its tag; any other first word is not a table line
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
    case 'P': case 'R': return loadPlan(T, t, line.size(), k[0]);
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

inline double halfToDouble(uint16_t h)
{
    int s = h >> 15, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    double v = e == 0 ? std::ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(m + 1024, e - 25);
    return s ? -v : v;
}

// CPython PyFloat_Pack2 (struct 'e'): round half to even straight from the double.
inline uint16_t doubleToHalf(double x)
{
    int sign = std::signbit(x) ? 1 : 0;
    if (x == 0.0) return (uint16_t)(sign << 15);
    if (std::isinf(x)) return (uint16_t)((sign << 15) | 0x7c00);
    if (std::isnan(x)) return (uint16_t)((sign << 15) | 0x7e00);
    int e;
    double f = std::frexp(std::fabs(x), &e);
    f *= 2.0; e--;
    if (e >= 16) return (uint16_t)((sign << 15) | 0x7c00);     // overflow (CPython raises; never hit by real data)
    if (e < -25) { f = 0.0; e = 0; }
    else if (e < -14) { f = std::ldexp(f, 14 + e); e = 0; }
    else { e += 15; f -= 1.0; }
    f *= 1024.0;
    unsigned bits = (unsigned)f;
    if ((f - bits > 0.5) || ((f - bits == 0.5) && (bits % 2 == 1))) {
        if (++bits == 1024) { bits = 0; ++e; }
    }
    return (uint16_t)(bits | (e << 10) | (sign << 15));
}

// One element of type v at b -> out[0..v.n).
inline void decode(const VType& v, const uint8_t* b, double* out)
{
    for (int i = 0; i < v.n; ++i) {
        const uint8_t* p = b + i * v.csize;
        double x;
        switch (v.code) {
        case 'f': { float f; memcpy(&f, p, 4); x = f; break; }
        case 'e': x = halfToDouble((uint16_t)(p[0] | p[1] << 8)); break;
        case 'B': x = p[0]; break;
        case 'h': x = (int16_t)(p[0] | p[1] << 8); break;
        default: x = (uint16_t)(p[0] | p[1] << 8); break;
        }
        if (v.norm) { x /= v.norm; if (x < -1.0) x = -1.0; }
        out[i] = x;
    }
}

// vals[0..v.n) as one element of type v at out (v.n * v.csize bytes).
inline void encode(const VType& v, const double* vals, char* out)
{
    for (int i = 0; i < v.n; ++i) {
        double x = vals[i];
        if (v.norm) {
            double lo = v.code == 'B' ? 0.0 : -1.0;
            x = std::nearbyint((x < lo ? lo : x > 1.0 ? 1.0 : x) * v.norm);   // Python round(): half to even
        } else if (v.code != 'f' && v.code != 'e') {
            double hi = v.code == 'B' ? 255.0 : 65535.0;
            x = std::trunc(x);
            x = x < 0 ? 0 : x > hi ? hi : x;
        }
        switch (v.code) {
        case 'f': { float f = (float)x; memcpy(out, &f, 4); out += 4; break; }
        case 'e': { uint16_t h = doubleToHalf(x); memcpy(out, &h, 2); out += 2; break; }
        case 'B': *out++ = (char)(int)x; break;
        default: { int16_t s = (int16_t)(int)x; memcpy(out, &s, 2); out += 2; break; }
        }
    }
}

// One vertex through plan p, appended to the destination streams per[0..p.ds.size()). Per vertex of a 52 MB
// world, so no allocation here (the string/vector form cost in_city ~1.5 s of load).
inline void convertVertex(const std::vector<const uint8_t*>& src, const Plan& p, std::vector<std::string>& per)
{
    for (size_t s = 0; s < p.ds.size(); ++s) per[s].append(p.ds[s], '\0');
    for (const Op& o : p.ops) {
        std::string& ds = per[o.dsi];
        char* d = &ds[ds.size() - p.ds[o.dsi] + o.doff];
        if (o.kind == 'K') memcpy(d, o.bytes.data(), o.size < (int)o.bytes.size() ? o.size : o.bytes.size());
        else if (o.kind == 'C') memcpy(d, src[o.ssi] + o.soff, o.size);
        else {
            double vals[4] = {0, 0, 0, 0};   // source values, then the default's tail, then zeros (to dv.n)
            decode(o.sv, src[o.ssi] + o.soff, vals);
            for (int i = o.sv.n; i < o.dv.n; ++i) vals[i] = o.dflt.empty() ? 0.0 : i < (int)o.dflt.size() ? o.dflt[i] : 0.0;
            char e[16];
            encode(o.dv, vals, e);
            int n = o.dv.n * o.dv.csize;
            memcpy(d, e, o.size < n ? o.size : n);
        }
    }
}

// ---- material bundles (matp.py) ----
struct Material { std::string name, shader, A, B; uint32_t hash = 0; int slots = 0, stride = 8; };
struct Bundle {
    std::vector<std::string> textures;   // 8-byte name hashes
    std::vector<Material> mats;
    std::vector<std::string> ssetNames, ssetBlocks;   // the SSET chunk: scene settings (ssf.py), one block per name
    uint32_t slot(const Material& m, int s) const { return oct::u((const uint8_t*)m.B.data() + m.stride * s, 4); }
};

inline size_t al(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }

// ---- scene settings (ssf.py): a world's / realm's fog, sun, ambient SH, bloom, HDR exposure, DOF, motion blur, SSAO,
// colour-grade LUT and environment map, kept in its bundle's SSET chunk (3.0 FUN_0046b9d0) and laid out by
// shaders/scenesettingsformat.ssf (3.0 FUN_0045d420, 1.0 FUN_004af580): global block A, global block B, then one block B
// per post pass (an embedded .shd each). Block sizes predicted exactly for 156/156 1.0 and 420/420 3.0 SSET chunks.
// 1.0's format is not 3.0's (2544 vs 4256 B; 3.0 adds parameters, passes and 8-byte slots), so a block is re-laid by
// name: Disney's 42 ported realm blocks equal ours in 2304 of 2394 parameters (the rest are retuned colours).
struct SsfPart { int off = 0, size = 0; std::vector<uint32_t> slots; std::map<uint32_t, std::pair<int, int>> params; std::string def; };
struct Ssf {
    int stride = 8, size = 0;
    std::vector<std::pair<uint32_t, SsfPart>> parts;   // key: 1 = global A, 2 = global B, else the pass name hash
    const SsfPart* find(uint32_t k) const { for (auto& p : parts) if (p.first == k) return &p.second; return nullptr; }
};

namespace detail {

// One part's tables in an .ssf: parameter names and words [from, to), slot names
struct SsfTables { const uint8_t *names, *words, *slots; int from, to, nslots; };

// One part: params (name hash, offset) sized up to the next one, slot names, the default block. false: a slot or
// parameter outside the block (RE-23: ssfBlock30 writes and reads by them)
inline bool addSsfPart(const std::string& s, Ssf& f, uint32_t key, int size, const SsfTables& t, size_t def)
{
    SsfPart p;
    p.size = size;
    std::vector<std::pair<int, uint32_t>> ps;
    for (int i = t.from; i < t.to; ++i)
        ps.push_back({(int)(oct::u(t.words + 4 * i, 4) >> 16), (uint32_t)oct::u(t.names + 4 * i, 4)});
    std::sort(ps.begin(), ps.end());
    const bool paramsIn = ps.empty() || ps.back().first <= size;
    if (!paramsIn || (size_t)t.nslots * ((size_t)f.stride + 1) > (size_t)size) return false;
    for (size_t i = 0; i < ps.size(); ++i)
        p.params[ps[i].second] = {ps[i].first, (i + 1 < ps.size() ? ps[i + 1].first : size) - ps[i].first};
    for (int i = 0; i < t.nslots; ++i) p.slots.push_back((uint32_t)oct::u(t.slots + 4 * i, 4));
    p.def = s.substr((std::min)(def, s.size()), size);
    p.off = f.size;
    f.size = (int)al((size_t)f.size + size, 16);
    f.parts.push_back({key, p});
    return true;
}

}  // namespace detail

inline bool parseSsf(const std::string& s, bool is30, Ssf& f)
{
    const uint8_t* b = (const uint8_t*)s.data();
    const size_t n = s.size();
    const int hsize = is30 ? 0xa8 : 0x64, ha = is30 ? 0x38 : 0x1c, hb = is30 ? 0x70 : 0x40, ptr = is30 ? 8 : 4;
    const int counts = is30 ? 0x18 : 0x0c, data = is30 ? 0xe0 : 0xa4, psize = is30 ? 0x50 : 0x30, da = is30 ? 0x68 : 0x44, db = is30 ? 0xa0 : 0x68;
    if (n < (size_t)hsize) return false;
    f.stride = is30 ? 8 : 4;
    int npar = b[0], npass = b[1], nA = b[ha];
    size_t o = hsize;
    auto ok = [&](size_t need) { return need <= n; };
    if (nA > npar || !ok(o + 8 * (size_t)npar + 4 * ((size_t)b[ha + 1] + b[hb + 1]))) return false;
    const uint8_t *names = b + o, *words = b + o + 4 * npar, *snA = words + 4 * npar, *snB = snA + 4 * b[ha + 1];
    int sizeA = (int)oct::u(b + ha + 2, 2), sizeB = (int)oct::u(b + hb + 2, 2);
    o += 8 * (size_t)npar + 4 * ((size_t)b[ha + 1] + b[hb + 1]);
    size_t defA = al(o, 16), defB = al(defA + sizeA, 16);
    if (!detail::addSsfPart(s, f, 1, sizeA, {names, words, snA, 0, nA, b[ha + 1]}, defA)) return false;
    if (!detail::addSsfPart(s, f, 2, sizeB, {names, words, snB, nA, npar, b[hb + 1]}, defB)) return false;
    o = defB + sizeB;
    if (!ok(o + 4 * (size_t)npass + ptr * (size_t)npass)) return false;
    const uint8_t* pnames = b + o;
    o += 4 * (size_t)npass + ptr * (size_t)npass;
    for (int i = 0; i < npass; ++i) {
        o = al(o, 16);
        if (!ok(o + data)) return false;
        const uint8_t* h = b + o;
        int pn = h[counts], pp = h[counts + 1], pt = h[counts + 2], pnA = h[da], psA = h[da + 1], psB = h[db + 1];
        int pSizeA = (int)oct::u(h + da + 2, 2), pSizeB = (int)oct::u(h + db + 2, 2);
        size_t q = al(o + data + (size_t)pp * psize + 2 * (size_t)pt, 4);
        if (!ok(q + 8 * (size_t)pn + 4 * (size_t)pt + 4 * ((size_t)psA + psB))) return false;
        const uint8_t *pnm = b + q, *pw = b + q + 4 * pn + 4 * pt, *pslots = pw + 4 * pn + 4 * psA;   // block B slot names
        o = q + 8 * (size_t)pn + 4 * (size_t)pt + 4 * ((size_t)psA + psB);
        size_t pdA = al(o, 16), pdB = al(pdA + pSizeA, 16);
        if (!detail::addSsfPart(s, f, (uint32_t)oct::u(pnames + 4 * i, 4), pSizeB, {pnm, pw, pslots, pnA, pn, psB}, pdB))
            return false;
        o = pdB + pSizeB;
        if (!is30) { while (o < n && b[o]) ++o; ++o; }   // 1.0 names the pass's shader after its blocks
        if (o > n) return false;
    }
    return true;
}

// One 1.0 settings block -> 3.0: the 3.0 format's defaults with every slot unset, then each 1.0 parameter of the same
// part and name at its 3.0 offset, each texture slot by name (index through tex; the slot's flag byte with it).
inline std::string ssfBlock30(const Ssf& f10, const Ssf& f30, const std::string& b10, const std::function<uint32_t(uint32_t)>& tex)
{
    static const uint32_t LUT10 = gen30::ehash("g_colorgrade_lut"), LUT30 = gen30::ehash("g_colorgrade_lut_sampler");   // renamed
    std::string out((size_t)f30.size, '\0');
    for (auto& [k, p] : f30.parts) {
        out.replace(p.off, (std::min)(p.def.size(), (size_t)p.size), p.def, 0, (std::min)(p.def.size(), (size_t)p.size));
        std::fill(out.begin() + p.off, out.begin() + p.off + 8 * p.slots.size(), '\xff');
    }
    for (auto& [k, p] : f30.parts) {
        const SsfPart* q = f10.find(k);
        if (!q || (size_t)q->off + q->size > b10.size()) continue;
        for (auto& [h, at] : p.params) {
            auto x = q->params.find(h);
            if (x == q->params.end()) continue;
            int len = (std::min)(at.second, x->second.second);
            if (len > 0) out.replace(p.off + at.first, len, b10, q->off + x->second.first, len);
        }
        for (size_t i = 0; i < p.slots.size(); ++i)
            for (size_t j = 0; j < q->slots.size(); ++j) {
                if ((q->slots[j] == LUT10 ? LUT30 : q->slots[j]) != p.slots[i]) continue;
                uint32_t t = oct::u((const uint8_t*)b10.data() + q->off + 4 * j, 4);
                uint32_t w[2] = {t == UNSET ? UNSET : tex(t), 0};
                if (w[0] == UNSET) w[1] = UNSET;
                memcpy(&out[p.off + 8 * i], w, 8);
                out[p.off + 8 * p.slots.size() + i] = b10[q->off + 4 * q->slots.size() + j];
                break;
            }
    }
    return out;
}

using BlockSizes = std::function<bool(const std::string&, int&, int&, int&)>;

namespace detail {

// The NUL-terminated string at p, which must end inside raw (RE-23); p moves past its NUL
inline bool cstrAt(const std::string& raw, size_t& p, std::string& out)
{
    const size_t e = p < raw.size() ? raw.find('\0', p) : std::string::npos;
    if (e == std::string::npos) return false;
    out.assign(raw, p, e - p);
    p = e + 1;
    return true;
}

// count elements of elem bytes at off lie inside size bytes
inline bool fitsArray(size_t size, size_t off, size_t count, size_t elem)
{
    return count <= size / elem && bytes::fits(size, off, count * elem);
}

inline bool fail(std::string& err, const std::string& why)
{
    err = why;
    return false;
}

// TEXB at o: 8-byte texture name hashes, 12 bytes apart; each must lie inside raw
inline bool parseTexb(const std::string& raw, size_t o, Bundle& out, std::string& err)
{
    const uint32_t cnt = oct::u((const uint8_t*)raw.data() + o + 12, 4);
    if (cnt == 0) return true;
    if (cnt - 1 > raw.size() / 12 || !bytes::fits(raw.size(), o + 20, 12 * (size_t)(cnt - 1) + 8))
        return fail(err, "TEXB overrun");
    for (uint32_t i = 0; i < cnt; ++i) out.textures.emplace_back(raw, o + 20 + 12 * (size_t)i, 8);
    return true;
}

// MATP name tables at p: count, then count strings; p moves past them
inline bool matpNames(const std::string& raw, size_t& p, std::vector<std::string>& out)
{
    if (!bytes::fits(raw.size(), p, 4)) return false;
    const uint32_t n = oct::u((const uint8_t*)raw.data() + p, 4);
    p += 4;
    for (uint32_t i = 0; i < n; ++i) {
        out.emplace_back();
        if (!cstrAt(raw, p, out.back())) return false;
    }
    return true;
}

// a material's blocks, sized by its shader; its slots (m.stride bytes each) lie inside block B (Bundle::slot)
inline bool matpBlocks(const std::string& raw, size_t& p, const BlockSizes& sizes, Material& m, std::string& err)
{
    int A = 0, B = 0, slots = 0;
    if (!sizes(m.shader, A, B, slots)) return fail(err, "unknown shader " + m.shader);
    if (A < 0 || B < 0 || slots < 0) return fail(err, "MATP block sizes of shader " + m.shader);
    if (!bytes::fits(raw.size(), p, (size_t)A + (size_t)B)) return fail(err, "MATP overrun");
    if ((size_t)slots * (size_t)m.stride > (size_t)B) return fail(err, "MATP slots past block B of shader " + m.shader);
    m.A = raw.substr(p, A);
    m.B = raw.substr(p + A, B);
    m.slots = slots;
    p += (size_t)A + (size_t)B;
    return true;
}

// MATP at o: shader names, material names, shader indices, hashes, then each material's blocks A and B
inline bool parseMatp(const std::string& raw, size_t o, int stride, const BlockSizes& sizes, Bundle& out, std::string& err)
{
    const uint8_t* b = (const uint8_t*)raw.data();
    const uint32_t nmat = oct::u(b + o + 12, 4);
    size_t p = al(o + 16, 4);
    std::vector<std::string> shaders, names;
    if (!matpNames(raw, p, shaders)) return fail(err, "MATP overrun");
    p = al(p, 4);
    if (!bytes::fits(raw.size(), p, 4)) return fail(err, "MATP overrun");
    if (oct::u(b + p, 4) != nmat) return fail(err, "MATP count");
    if (!matpNames(raw, p, names)) return fail(err, "MATP overrun");
    const size_t pIdx = al(p, 2), pHash = al(pIdx + 2 * (size_t)nmat, 4);
    if (!fitsArray(raw.size(), pIdx, nmat, 2) || !fitsArray(raw.size(), pHash, nmat, 4)) return fail(err, "MATP overrun");
    p = al(pHash + 4 * (size_t)nmat, 16);
    for (uint32_t i = 0; i < nmat; ++i) {
        Material m;
        const uint32_t sidx = oct::u(b + pIdx + 2 * (size_t)i, 2);
        if (sidx >= shaders.size()) return fail(err, "MATP shader index");
        m.name = names[i];
        m.shader = shaders[sidx];
        m.hash = oct::u(b + pHash + 4 * (size_t)i, 4);
        m.stride = stride;
        if (!matpBlocks(raw, p, sizes, m, err)) return false;
        out.mats.push_back(m);
    }
    return true;
}

// SSET at o (csize bytes): u32 count twice, names, 16-aligned blocks
inline void parseSset(const std::string& raw, size_t o, size_t csize, Bundle& out)
{
    const uint32_t cnt = oct::u((const uint8_t*)raw.data() + o + 12, 4);
    size_t p = o + 20;
    const size_t end = csize <= raw.size() - o ? o + csize : raw.size();
    for (uint32_t i = 0; i < cnt && p < end; ++i) {
        const size_t e = raw.find('\0', p);
        out.ssetNames.push_back(raw.substr(p, e == std::string::npos ? std::string::npos : e - p));
        p += out.ssetNames.back().size() + 1;
    }
    p = al(p, 16);
    const size_t sz = cnt && end > p ? (end - p) / cnt : 0;
    for (uint32_t i = 0; i < cnt && sz; ++i) out.ssetBlocks.push_back(raw.substr(p + i * sz, sz));
}

}  // namespace detail

// sizes(shader, A, B, slots) -> false if unknown. stride: 8 (3.0) or 4 (1.0). false: err says why.
inline bool parseBundle(const std::string& raw, Bundle& out, int stride, const BlockSizes& sizes, std::string* err)
{
    const uint8_t* b = (const uint8_t*)raw.data();
    if (raw.size() < 16 || memcmp(b, "BNDL", 4)) { if (err) *err = "not a BNDL"; return false; }
    uint32_t n = oct::u(b + 12, 4);
    size_t o = 16;
    std::string why;
    for (uint32_t c = 0; c < n && bytes::fits(raw.size(), o, 16); ++c) {
        uint32_t csize = oct::u(b + o + 8, 4);
        const bool sset = !memcmp(b + o, "SSET", 4) && bytes::fits(raw.size(), o, 20);
        bool ok = true;
        if (!memcmp(b + o, "TEXB", 4)) ok = detail::parseTexb(raw, o, out, why);
        else if (!memcmp(b + o, "MATP", 4)) ok = detail::parseMatp(raw, o, stride, sizes, out, why);
        else if (sset) detail::parseSset(raw, o, csize, out);
        if (!ok) {
            if (err) *err = why;
            return false;
        }
        // a chunk past the end ends the walk, as the next bounds check would; o never wraps (A-26 form)
        if (!csize || csize > raw.size() - o) break;
        o += csize;
    }
    return true;
}

// matp.build: TEXB + MATP (+ SSET when given), file-relative alignment.
inline std::string buildBundle(const std::vector<std::string>& textures, const std::vector<Material>& mats,
                               const std::vector<std::string>& ssetNames = {}, const std::vector<std::string>& ssetBlocks = {})
{
    auto u32 = [](std::string& s, uint32_t v) { s.append((const char*)&v, 4); };
    std::string texb = "TEXB";
    u32(texb, 3); u32(texb, 0); u32(texb, (uint32_t)textures.size()); u32(texb, 0);
    for (auto& t : textures) { texb += t; u32(texb, 0xffffffff); }
    texb.resize(al(texb.size(), 16), '\0');
    uint32_t ts = (uint32_t)texb.size(); memcpy(&texb[8], &ts, 4);

    std::vector<std::string> shaders;
    for (auto& m : mats) {
        bool seen = false;
        for (auto& s : shaders) seen |= s == m.shader;
        if (!seen) shaders.push_back(m.shader);
    }
    size_t base = 16 + texb.size();
    std::string m = "MATP";
    u32(m, 2); u32(m, 0); u32(m, (uint32_t)mats.size());
    auto pad = [&](size_t a) { m.resize(al(base + m.size(), a) - base, '\0'); };
    pad(4); u32(m, (uint32_t)shaders.size());
    for (auto& s : shaders) { m += s; m.push_back('\0'); }
    pad(4); u32(m, (uint32_t)mats.size());
    for (auto& x : mats) { m += x.name; m.push_back('\0'); }
    pad(2);
    for (auto& x : mats) {
        uint16_t i = 0;
        while (shaders[i] != x.shader) ++i;
        m.append((const char*)&i, 2);
    }
    pad(4);
    for (auto& x : mats) u32(m, x.hash);
    pad(16);
    for (auto& x : mats) { m += x.A; m += x.B; }
    uint32_t ms = (uint32_t)m.size(); memcpy(&m[8], &ms, 4);
    std::string body = texb + m, out = "BNDL";
    if (!ssetBlocks.empty()) {   // 'SSET' ver 0, size, n, n, names, blocks 16-aligned in the file (as 3.0 realms ship it)
        size_t at = 16 + body.size();
        std::string s = "SSET";
        u32(s, 0); u32(s, 0); u32(s, (uint32_t)ssetBlocks.size()); u32(s, (uint32_t)ssetBlocks.size());
        for (auto& nm : ssetNames) { s += nm; s.push_back('\0'); }
        s.resize(al(at + s.size(), 16) - at, '\0');
        for (auto& blk : ssetBlocks) s += blk;
        s.resize(al(at + s.size(), 16) - at, '\0');
        uint32_t ss = (uint32_t)s.size(); memcpy(&s[8], &ss, 4);
        body += s;
    }
    u32(out, 1); u32(out, (uint32_t)(16 + body.size())); u32(out, ssetBlocks.empty() ? 2 : 3);
    return out + body;
}

// ---- the world transcode ----
struct IO {
    std::function<bool(const std::string& path, std::string& out)> read30;   // a 3.0 asset by extract path ('3:...')
    std::function<bool(const std::string& path, std::string& out)> read10;   // a 1.0 asset by path
    std::function<void(const std::string&)> log;
    bool verbose = false;   // log every substituted material's slot assignment
    // real 1.0 shaders (gen30.hpp): a material whose 1.0 shader read10 finds keeps it, as 'x'<shader>, with its own
    // blocks (B re-laid for 8-byte slots) and its primitives keep their 1.0 vertex formats
    bool translate = false;
    // a 1.0 vertex format with bone indices: 3.0 skins on the GPU before the material shader (MemExport, six
    // layouts; a raw 1.0 skinned layout matches none -> its job list spins on purpose, FUN_0047c300, in-game
    // 2026-10-13), so materials drawn with one keep the converted path for now
    std::function<bool(uint32_t vformat)> skinned;
    // the shader translates (and is registered): only then may a material take it
    std::function<bool(const std::string& shader)> translatable;
    // a skinned node's converted primitives go into their target's 'skin' layout with the 1.0 bone indices/weights
    // (R plans), as every shipped skinned primitive is stored (3,396 of 3,396; rigid ones 'main', 75,895 of 75,895).
    // Off: the 'main' layout, bones dropped, which the GPU skinning pass then reads as bones (cfg skinlayout10)
    bool skinLayout = false;
    // both games' scenesettingsformat.ssf: when set, a bundle's 1.0 SSET chunk is re-laid into 3.0's (ssfBlock30) and
    // kept; without it the transcoded bundle had no scene settings at all (no bloom, fog, grading: cfg scenesettings10)
    const Ssf* ssf10 = nullptr;
    const Ssf* ssf30 = nullptr;
    // the served vformat.vtxf's re-laid 1.0 formats (gen30::mergeVtxf): a translated primitive in one has its
    // trailing vertex colour split into a stream of its own, as that layout says
    const std::map<uint32_t, gen30::ColourTail>* tails10 = nullptr;
    // cfg skintr10: a material drawn only with skinned formats may take its translated 1.0 shader too. Its vertices
    // still go to their 3.0 'skin' layout target (skinTarget30: 3.0's GPU skinning reads that and nothing else), and
    // the translated shader takes that target's skinned output (gen30: skin passes, identity bones)
    bool translateSkinned = false;
};

// The 3.0 format a skinned 1.0 format's vertices go to: its first target with an R plan (into the target's 'skin'
// layout) -- the pick transcodeWorld makes for a material whose shader declares no format (0: none).
inline uint32_t skinTarget30(const Table& T, uint32_t crc10)
{
    auto t = T.targets.find(crc10);
    if (t != T.targets.end())
        for (uint32_t c : t->second)
            if (T.skinPlans.count({crc10, c})) return c;
    return 0;
}

struct Result {
    std::string mtb, vbuf, ibuf, ibuf2;
    std::vector<std::string> problems;
    std::map<std::string, int> tiers;
    std::set<std::string> textures10;   // 1.0 texture hashes (hex) the bundle uses
    std::set<int> translated;           // materials on their real (translated) 1.0 shader
    int skinned = 0;                    // primitives written in their target's 'skin' layout (io.skinLayout)
    int sceneSettings = 0;              // SSET blocks re-laid into 3.0's format (io.ssf10/ssf30)
    std::vector<std::pair<std::string, std::string>> shaders;   // per 1.0 material: (its 1.0 shader, the shader served)
};

struct Src { Bundle b; int i; };   // one material of a source bundle (merge input)

namespace detail {

// The table's material index inside the bundle read from the install; another 3.0 build moves it (RE-18)
inline bool hasMaterial(const Bundle& b, const Ref& ref, std::vector<std::string>& problems)
{
    if (ref.idx >= 0 && (size_t)ref.idx < b.mats.size()) return true;
    problems.push_back("3.0 bundle " + ref.extract + ": no material " + std::to_string(ref.idx));
    return false;
}

// Every slot's texture index lies inside its bundle's TEXB; a material block may name any index (RE-18)
inline bool texturesInRange(const std::vector<Src>& srcs, std::vector<std::string>& problems)
{
    for (const Src& s : srcs) {
        const Material& m = s.b.mats[s.i];
        for (int k = 0; k < m.slots; ++k) {
            const uint32_t t = s.b.slot(m, k);
            if (t == UNSET || t < s.b.textures.size()) continue;
            problems.push_back(m.name + ": texture index " + std::to_string(t) + " past the bundle's TEXB");
            return false;
        }
    }
    return true;
}

}  // namespace detail

inline std::string hex8(const std::string& h)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    for (unsigned char c : h) { s.push_back(d[c >> 4]); s.push_back(d[c & 15]); }
    return s;
}

// RenderCaps 1.0 -> 3.0 (conv.rendercaps30).
inline int32_t rendercaps30(int32_t v)
{
    if (v == 0) return 0x200;
    return (v & 0xfff) | ((v & 0x1000) ? 0x200 : 0) | ((v & 0x2000) ? 0x400 : 0);
}

// The material half (autobundle.resolve): the 1.0 bundle's materials onto 3.0 shaders -> r.mtb, r.textures10.
// drawn: material -> the 1.0 vformats it is drawn with (the generic tier needs a shader taking a twin of each);
// donorPrefix: generic donors only from 3.0 bundles whose extract path starts with it ("" = any);
// shaderVf: material -> the vformats its final shader declares.
inline bool transcodeMaterials(const Table& T, const IO& io, const std::string& mtb10, const std::map<int, std::set<uint32_t>>& drawn,
                               const std::string& donorPrefix, Result& r, std::map<int, const std::set<uint32_t>*>& shaderVf)
{
    auto sizes3 = [&](const std::string& sh, int& A, int& B, int& slots) {
        auto it = T.shaders.find(sh);
        if (it == T.shaders.end()) return false;
        A = it->second.A; B = it->second.B; slots = it->second.slots;
        return true;
    };
    std::map<std::string, std::array<int, 3>> shd10;
    auto sizes1 = [&](const std::string& sh, int& A, int& B, int& slots) {
        auto it = shd10.find(sh);
        if (it == shd10.end()) {
            std::string b;
            if (!io.read10("shaders/shaders/" + sh + ".shd", b) || b.size() < 0x6c) return false;
            const uint8_t* p = (const uint8_t*)b.data();
            it = shd10.emplace(sh, std::array<int, 3>{(int)oct::u(p + 0x46, 2), (int)oct::u(p + 0x6a, 2), (int)p[0x69]}).first;
        }
        A = it->second[0]; B = it->second[1]; slots = it->second[2];
        return true;
    };

    std::string err;
    Bundle b10;
    if (!parseBundle(mtb10, b10, 4, sizes1, &err)) { r.problems.push_back("1.0 bundle: " + err); return false; }

    std::map<std::string, Bundle> cache30;
    auto bundle30 = [&](const Ref& ref, Bundle*& out) {
        auto it = cache30.find(ref.extract);
        if (it == cache30.end()) {
            std::string raw;
            Bundle b;
            if (!io.read30(ref.extract, raw) || !parseBundle(raw, b, 8, sizes3, &err)) {
                r.problems.push_back("3.0 bundle " + ref.extract + ": " + (raw.empty() ? std::string("unreadable") : err));
                return false;
            }
            it = cache30.emplace(ref.extract, std::move(b)).first;
        }
        out = &it->second;
        return true;
    };

    std::vector<Src> srcs;
    for (size_t k = 0; k < b10.mats.size(); ++k) {
        const Material& m10 = b10.mats[k];
        Bundle* bnd = nullptr;
        std::string raw10;
        gen30::Shd10 s10;
        bool skin = false, rigid = false;
        if (auto dk = drawn.find((int)k); dk != drawn.end() && io.skinned)
            for (uint32_t c : dk->second) (io.skinned(c) ? skin : rigid) = true;
        // skinned: only when every format it is drawn with is skinned (the translated shader's passes take the 3.0
        // skin target; a rigid primitive would need the 1.0 format) and each has a target, and not when Disney ships
        // the material (the real tier: its 3.0 twin is drawn skinned already) -- this is for the ones that fell to a
        // stand-in shader (same-sh / generic: pupils, translucent FX, screen-space water)
        bool skinOk = io.translateSkinned && !rigid &&
                      !(!m10.name.empty() && T.matindex.count(m10.name) && !T.notReal.count(m10.name));
        if (skin && skinOk)
            for (uint32_t c : drawn.at((int)k)) skinOk &= skinTarget30(T, c) != 0 && T.plans.count({c, skinTarget30(T, c)}) != 0;
        if (io.translate && (!skin || skinOk) && io.read10("shaders/shaders/" + m10.shader + ".shd", raw10) && gen30::parse10(raw10, s10) &&
            (!io.translatable || io.translatable(m10.shader))) {
            gen30::Layout L = gen30::layout(s10);
            Material dm;
            dm.name = m10.name;
            dm.shader = "x" + m10.shader;
            // MATP load (FUN_00472590) reuses any material already registered under the hash (FUN_00468500): the
            // same 1.0 GUID converted onto a 3.0 shader in a choreography/exploder bundle loaded first replaced this
            // one -- 3.0 shader, 1.0 block A, opacity 0 -> skipped, black (in-game 2026-10-13). Own hash space.
            dm.hash = m10.hash ^ 0x9E3779B9u;
            dm.stride = 8;
            dm.slots = L.slots;
            dm.A = m10.A;
            dm.A.resize(L.sizeA, '\0');
            dm.B = gen30::blockB30(m10.B, L);
            Bundle one;
            one.textures = b10.textures;
            one.mats.push_back(dm);
            srcs.push_back({one, 0});
            r.tiers["translated"]++;
            if (skin) r.tiers["translated skinned"]++;
            if (io.verbose) io.log("translated " + m10.name + " [" + m10.shader + "]" + (skin ? " skinned" : ""));
            r.translated.insert((int)k);
            r.shaders.push_back({m10.shader, dm.shader});
            continue;
        }
        auto mi = T.matindex.find(m10.name);
        if (!m10.name.empty() && mi != T.matindex.end() && !T.notReal.count(m10.name)) {
            if (!bundle30(mi->second, bnd) || !detail::hasMaterial(*bnd, mi->second, r.problems)) continue;
            if (io.verbose) {   // the 1.0 material's textures vs the 3.0 material's (a GUID 3.0 reused shows as a mismatch)
                const Material& m3 = bnd->mats[mi->second.idx];
                std::string l = "real " + m10.name + " " + mi->second.extract + " 1.0:";
                for (int s = 0; s < m10.slots; ++s) { uint32_t t = b10.slot(m10, s); l += " " + (t < b10.textures.size() ? hex8(b10.textures[t]) : std::string("-")); }
                l += " 3.0:";
                for (int s = 0; s < m3.slots; ++s) { uint32_t t = bnd->slot(m3, s); l += " " + (t < bnd->textures.size() ? hex8(bnd->textures[t]) : std::string("-")); }
                io.log(l);
            }
            srcs.push_back({*bnd, mi->second.idx});
            r.tiers["real"]++;
        } else {
            const Ref* donor = nullptr;
            const char* tier = "same-sh";
            auto di = T.donors.find(m10.shader);
            if (di != T.donors.end()) donor = &di->second;
            else {
                // most common 3.0 shader accepting a twin of every format this material is drawn with:
                // Python max over (count, shader, extract path, index)
                tier = "generic";
                std::vector<std::set<uint32_t>> need;
                auto dk = drawn.find((int)k);
                if (dk != drawn.end())
                    for (uint32_t c : dk->second) {
                        auto t = T.targets.find(c);
                        need.emplace_back(t == T.targets.end() ? std::set<uint32_t>() : std::set<uint32_t>(t->second.begin(), t->second.end()));
                    }
                // A 1.0 shader that reads the scene colour buffer (refraction, distortion: the 'g_colorBuffer_sampler'
                // slot) keeps a 3.0 shader that reads it, the one sharing most of its slot roles: on the most common
                // (opaque G-buffer) shader the screen-water drops of ScreenFX_WaterA drew nothing (in-game 2026-10-15).
                // Skinned meshes reach this tier because their 1.0 shader is not translated.
                std::set<uint32_t> slots10;
                {
                    std::string raw;
                    gen30::Shd10 s;
                    if (io.read10("shaders/shaders/" + m10.shader + ".shd", raw) && gen30::parse10(raw, s))
                        slots10.insert(s.slotsB.begin(), s.slotsB.end());
                }
                auto reads = [&](const std::string& role) { return slots10.count(gen30::ehash("g_" + role + "_sampler")) != 0; };
                const bool colorBuffer = reads("colorBuffer");
                const std::string* bestSh = nullptr;
                int bestCommon = 0;
                for (int pass = colorBuffer ? 0 : 1; pass < 2 && !donor; ++pass)
                    for (auto& [sh, ref] : T.donors) {
                        auto si = T.shaders.find(sh);
                        if (si == T.shaders.end() || si->second.slots < 1) continue;
                        if (ref.extract.compare(0, donorPrefix.size(), donorPrefix)) continue;
                        bool ok = true;
                        for (auto& c : need) {
                            bool any = false;
                            for (uint32_t x : c) if (si->second.vf.count(x)) { any = true; break; }
                            if (!any) { ok = false; break; }
                        }
                        if (!ok) continue;
                        int common = 0;   // pass 0: colour-buffer readers only, most roles in common first
                        if (pass == 0) {
                            const auto& rs = si->second.roles;
                            if (std::find(rs.begin(), rs.end(), "colorBuffer") == rs.end()) continue;
                            for (auto& role : rs) common += !role.empty() && reads(role);
                        }
                        auto key = [&](int cm, const std::string& s, const Ref& rr) { return std::make_tuple(cm, rr.count, s, rr.extract, rr.idx); };
                        if (!bestSh || key(common, sh, ref) > key(bestCommon, *bestSh, *donor)) { bestSh = &sh; donor = &ref; bestCommon = common; }
                    }
                if (!donor) { r.problems.push_back(m10.name + ": no 3.0 shader accepts its vertex format"); continue; }
            }
            if (!bundle30(*donor, bnd) || !detail::hasMaterial(*bnd, *donor, r.problems)) continue;
            // autobundle._substitute: the donor's blocks + the 1.0 material's own textures
            Material dm = bnd->mats[donor->idx];
            std::vector<uint32_t> used;
            for (int s = 0; s < m10.slots; ++s) used.push_back(b10.slot(m10, s));
            if (dm.shader != m10.shader) {
                auto g = T.genericSlots.find(m10.shader);
                if (g != T.genericSlots.end()) {
                    std::vector<uint32_t> pick;
                    for (int i : g->second) pick.push_back(i < (int)used.size() ? used[i] : UNSET);
                    used = pick;
                }
                std::vector<uint32_t> set;
                for (uint32_t t : used) if (t != UNSET) set.push_back(t);
                std::vector<int> targets;
                const auto& roles = T.shaders.at(dm.shader).roles;
                for (size_t s = 0; s < roles.size(); ++s) if (!roles[s].empty() && T.filled.count(roles[s])) targets.push_back((int)s);
                used.assign(dm.slots, UNSET);
                for (int s = 0; s < dm.slots; ++s)
                    for (size_t ti = 0; ti < targets.size(); ++ti)
                        if (targets[ti] == s) { if (ti < set.size()) used[s] = set[ti]; break; }
            }
            for (int s = 0; s < dm.slots; ++s) {
                uint32_t t = s < (int)used.size() ? used[s] : UNSET, w2 = t == UNSET ? UNSET : 0;
                memcpy(&dm.B[dm.stride * s], &t, 4);
                memcpy(&dm.B[dm.stride * s + 4], &w2, 4);
            }
            const std::string &a10 = m10.A;
            // Block A by parameter name: the 3.0 shader's own defaults, then each parameter the 1.0 material has, at
            // 3.0's offset. Copied raw, 1.0's layout misplaced what 3.0 moved: 3.0 put g_diffuse_hdrModifier at 0x10
            // and the opacity parameter (+0x50 word, read by FUN_0044ad80) at 0x20, which then held the donor's 0 --
            // every such piece was "transparent": depth only, never coloured, black (in-game 2026-10-13).
            std::string raw30, raw10a;
            gen30::ShdA30 a30;
            gen30::Shd10 sa10;
            if (io.read30 && io.read30("3:shaders/shaders/" + dm.shader + ".shd", raw30) && gen30::parseA30(raw30, a30) &&
                (int)dm.A.size() == a30.sizeA) {
                std::string A = a30.defA;
                if (io.read10("shaders/shaders/" + m10.shader + ".shd", raw10a) && gen30::parse10(raw10a, sa10))
                    for (auto& p : sa10.params) {
                        auto q = a30.off.find(p.hash);
                        if (!p.A || q == a30.off.end()) continue;
                        size_t n = (std::min)({(size_t)16, a10.size() - (std::min)(a10.size(), (size_t)p.offset), A.size() - (size_t)q->second});
                        A.replace(q->second, n, a10, p.offset, n);
                    }
                dm.A = A;
            } else if (dm.shader == m10.shader && a10.size() <= dm.A.size()) dm.A = a10 + dm.A.substr(a10.size());
            else if (a10.size() >= 16 && dm.A.size() >= 16) dm.A = a10.substr(0, 16) + dm.A.substr(16);
            if (io.verbose) {   // octtest --render with RENDER_VERBOSE: which 1.0 texture went to which donor slot
                std::string l = std::string(tier) + " " + m10.name + " [" + m10.shader + " -> " + dm.shader + "]";
                const auto& roles = T.shaders.at(dm.shader).roles;
                for (int s = 0; s < dm.slots; ++s) {
                    uint32_t t = s < (int)used.size() ? used[s] : UNSET;
                    l += " " + (s < (int)roles.size() && !roles[s].empty() ? roles[s] : std::string("-")) + "=" +
                         (t == UNSET || t >= b10.textures.size() ? std::string("none") : hex8(b10.textures[t]));
                }
                io.log(l);
            }
            dm.name = m10.name;
            dm.hash = m10.hash;
            Bundle one;
            one.textures = b10.textures;
            one.mats.push_back(dm);
            srcs.push_back({one, 0});
            r.tiers[tier]++;
        }
        const Material& fin = srcs.back().b.mats[srcs.back().i];
        r.shaders.push_back({m10.shader, fin.shader});
        shaderVf[(int)k] = &T.shaders.at(fin.shader).vf;
    }
    if (!r.problems.empty() || !detail::texturesInRange(srcs, r.problems)) return false;

    // matp.merge: union TEXB in first-use order, slot indices rebased (index word only)
    std::vector<std::string> tex;
    std::vector<Material> mats;
    for (auto& s : srcs) {
        Material m = s.b.mats[s.i];
        for (int k = 0; k < m.slots; ++k) {
            uint32_t t = s.b.slot(m, k);
            if (t == UNSET) continue;
            const std::string& h = s.b.textures[t];
            uint32_t at = 0;
            while (at < tex.size() && tex[at] != h) ++at;
            if (at == tex.size()) tex.push_back(h);
            memcpy(&m.B[m.stride * k], &at, 4);
        }
        mats.push_back(m);
    }
    // scene settings: re-laid into 3.0's format; their textures (LUT, environment map) join the merged TEXB
    std::vector<std::string> sset;
    if (io.ssf10 && io.ssf30)
        for (auto& blk : b10.ssetBlocks)
            sset.push_back(ssfBlock30(*io.ssf10, *io.ssf30, blk, [&](uint32_t t) -> uint32_t {
                if (t >= b10.textures.size()) return UNSET;
                uint32_t at = 0;
                while (at < tex.size() && tex[at] != b10.textures[t]) ++at;
                if (at == tex.size()) tex.push_back(b10.textures[t]);
                return at;
            }));
    r.sceneSettings = (int)sset.size();
    for (auto& t : tex) r.textures10.insert(hex8(t));
    r.mtb = buildBundle(tex, mats, sset.empty() ? std::vector<std::string>() : b10.ssetNames, sset);
    return true;
}

// A material bundle without geometry of its own (particles/<x>.mtb): materials only, generic donors from donorPrefix.
inline bool transcodeBundle(const Table& T, const IO& io, const std::string& mtb10, const std::string& donorPrefix, Result& r)
{
    std::map<int, const std::set<uint32_t>*> vf;
    return transcodeMaterials(T, io, mtb10, {}, donorPrefix, r, vf);
}

namespace detail {

// (MaterialReference, vformatCRC) per primitive in record order (autobundle.prim_pairs). -> nullptr or the problem
inline const char* primPairs(const oct::File& f, std::map<uint32_t, std::set<int>>& users,
                             std::map<int, std::set<uint32_t>>& drawn)
{
    int ref = -1;
    for (auto& rec : f.recs) {
        const std::string& n = f.name(rec);
        const bool isRef = n == "MaterialReference";
        if (!isRef && n != "vformatCRC") continue;
        const std::vector<int32_t> v = f.ints(rec);
        if (v.empty()) return isRef ? "MaterialReference without a value" : "vformatCRC without a value";
        if (isRef) {
            ref = v[0];
            continue;
        }
        users[(uint32_t)v[0]].insert(ref);
        drawn[ref].insert((uint32_t)v[0]);
    }
    return nullptr;
}

// A primitive's records by name: the last of each in its subtree; RenderCaps is optional
inline constexpr const char* kPrimFields[] = {"Idata", "Vdata", "vformatCRC", "MaterialReference", "RenderCaps"};
constexpr size_t kIdata = 0, kVdata = 1, kCrc = 2, kMatRef = 3, kCaps = 4, kPrimFieldCount = 5;
struct PrimRecs { size_t at[kPrimFieldCount] = {}; bool has[kPrimFieldCount] = {}; };

// index of nm in kPrimFields, or kPrimFieldCount
inline size_t primField(const std::string& nm)
{
    for (size_t k = 0; k < kPrimFieldCount; ++k)
        if (nm == kPrimFields[k]) return k;
    return kPrimFieldCount;
}

// false: Idata, Vdata, vformatCRC or MaterialReference missing (RE-19: record 0 stood in for it)
inline bool primRecords(const oct::File& f, size_t prim, PrimRecs& p)
{
    for (size_t j = prim + 1, pe = f.blockEnd(prim); j < pe; ++j) {
        const size_t k = primField(f.name(f.recs[j]));
        if (k == kPrimFieldCount) continue;
        p.at[k] = j;
        p.has[k] = true;
    }
    const bool buffers = p.has[kIdata] && p.has[kVdata];
    return buffers && p.has[kCrc] && p.has[kMatRef];
}

// Idata: [width, first index, base, count]; Vdata: [streams, vertices, (0, offset, stride) per stream]
struct PrimData { std::vector<int32_t> ida, vda; uint32_t src = 0; int ref = 0; };

// vda holds the (0, offset, stride) triples of its first `streams` streams
inline bool vdataHolds(const std::vector<int32_t>& vda, size_t streams)
{
    return vda.size() >= 2 && streams <= (vda.size() - 2) / 3;
}

// -> nullptr, or the problem: a value missing, or a list shorter than its counts (RE-19)
inline const char* readPrim(const oct::File& f, const PrimRecs& p, PrimData& d)
{
    const std::vector<int32_t> crc = f.ints(f.recs[p.at[kCrc]]), ref = f.ints(f.recs[p.at[kMatRef]]);
    if (crc.empty() || ref.empty()) return "primitive without a vformatCRC or MaterialReference value";
    d.ida = f.ints(f.recs[p.at[kIdata]]);
    d.vda = f.ints(f.recs[p.at[kVdata]]);
    d.src = (uint32_t)crc[0];
    d.ref = ref[0];
    if (d.ida.size() < 4) return "Idata too short";
    const bool counted = !d.vda.empty() && d.vda[0] >= 0;
    if (!counted || !vdataHolds(d.vda, (size_t)d.vda[0])) return "Vdata too short";
    return nullptr;
}

inline Op copyOp(int dsi, int size, int ssi, int soff)
{
    Op o{};
    o.dsi = dsi;
    o.doff = 0;
    o.size = size;
    o.kind = 'C';
    o.ssi = ssi;
    o.soff = soff;
    return o;
}

// the primitive's last stream ends in the re-laid format's colour tail
inline bool tailFits(const gen30::ColourTail& ct, const std::vector<int32_t>& vda)
{
    return vda[0] == ct.stream + 1 && vda[4 + 3 * ct.stream] == ct.offset + ct.size;
}

// A translated material's primitive: every stream copied as it is; a re-laid format's colour tail to its own stream
inline const char* samePlan(const IO& io, uint32_t src, const std::vector<int32_t>& vda, Plan& same)
{
    const gen30::ColourTail* ct = nullptr;
    if (io.tails10)
        if (auto t = io.tails10->find(src); t != io.tails10->end()) ct = &t->second;
    if (ct && !tailFits(*ct, vda)) return "vertex colour tail: strides differ from the format";
    for (int s = 0; s < vda[0]; ++s) {
        same.ss.push_back(vda[4 + 3 * s]);
        same.ds.push_back(ct && s == ct->stream ? ct->offset : vda[4 + 3 * s]);
        same.ops.push_back(copyOp(s, same.ds.back(), s, 0));
    }
    if (ct) {
        same.ds.push_back(ct->size);
        same.ops.push_back(copyOp(vda[0], ct->size, ct->stream, ct->offset));
    }
    return nullptr;
}

// 16-bit index -> local index, stamped per primitive: a flat table, not a hash map per index (in_city: ~4M indices)
struct Remap { std::vector<uint32_t> stamp, to; uint32_t gen = 0; };

// The primitive's indices made local: order = the vertices it uses, first use first; sub = its local indices
inline const char* remapIndices(const std::vector<int32_t>& ida, const std::string& ibuf10, Remap& m,
                                std::vector<uint32_t>& order, std::vector<uint32_t>& sub)
{
    const int32_t first = ida[1], count = ida[3];
    const bool inside = first >= 0 && bytes::fits(ibuf10.size(), 2 * (size_t)first, 2 * (size_t)count);
    if (count > 0 && !inside) return "index past the ibuf";
    const uint8_t* ib = (const uint8_t*)ibuf10.data();
    sub.reserve(count > 0 ? (size_t)count : 0);
    ++m.gen;
    for (int32_t q = 0; q < count; ++q) {
        const size_t at = 2 * ((size_t)first + q);
        const uint32_t v = (uint32_t)(ib[at] | ib[at + 1] << 8);
        if (m.stamp[v] != m.gen) {
            m.stamp[v] = m.gen;
            m.to[v] = (uint32_t)order.size();
            order.push_back(v);
        }
        sub.push_back(m.to[v]);
    }
    return nullptr;
}

// [off, off + (last + 1) * stride) lies inside the vbuf: every vertex up to index last
inline bool vertexFits(int32_t off, int32_t stride, uint32_t last, size_t size)
{
    if (off < 0 || stride < 0) return false;
    const uint64_t span = ((uint64_t)last + 1) * (uint32_t)stride;
    return span <= size && bytes::fits(size, (size_t)off, (size_t)span);
}

// Every vertex of order through plan into per (one string per destination stream); strides and ranges checked first
inline const char* convertPrim(const Plan& plan, const std::vector<int32_t>& vda, const std::vector<uint32_t>& order,
                               const std::string& vbuf10, std::vector<std::string>& per)
{
    for (int s = 0; s < vda[0]; ++s)
        if (s >= (int)plan.ss.size() || vda[4 + 3 * s] != plan.ss[s]) return "stride mismatch";
    if (!vdataHolds(vda, plan.ss.size())) return "Vdata shorter than the plan";
    const uint32_t last = order.empty() ? 0 : *std::max_element(order.begin(), order.end());
    for (size_t s = 0; s < plan.ss.size() && !order.empty(); ++s)
        if (!vertexFits(vda[3 + 3 * s], plan.ss[s], last, vbuf10.size())) return "vertex past the vbuf";
    per.assign(plan.ds.size(), std::string());
    for (size_t s = 0; s < plan.ds.size(); ++s) per[s].reserve(order.size() * (size_t)plan.ds[s]);
    std::vector<const uint8_t*> srcp(plan.ss.size());
    const uint8_t* vb = (const uint8_t*)vbuf10.data();
    for (uint32_t v : order) {
        for (size_t s = 0; s < plan.ss.size(); ++s) srcp[s] = vb + (size_t)vda[3 + 3 * s] + (size_t)v * plan.ss[s];
        convertVertex(srcp, plan, per);
    }
    return nullptr;
}

// Record edits held until every check passed, so a false return leaves f as it was (RE-05)
struct RecEdits {
    std::map<size_t, oct::Rec> recs;   // record index -> its edited copy
    const oct::Rec& get(const oct::File& f, size_t i) const
    {
        auto it = recs.find(i);
        return it != recs.end() ? it->second : f.recs[i];
    }
    void setInts(oct::File& f, size_t i, const std::vector<int32_t>& v)
    {
        oct::Rec r = get(f, i);
        f.setInts(r, v);
        recs[i] = std::move(r);
    }
    void setInt(oct::File& f, size_t i, int64_t v)
    {
        oct::Rec r = get(f, i);
        f.setInt(r, v);
        recs[i] = std::move(r);
    }
    void commit(oct::File& f)
    {
        for (auto& [i, r] : recs) f.recs[i] = std::move(r);
        recs.clear();
    }
};

// RenderCaps 1.0 -> 3.0; false: the record has no value
inline bool caps30(oct::File& f, RecEdits& e, size_t rec)
{
    const std::vector<int32_t> v = f.ints(e.get(f, rec));
    if (v.empty()) return false;
    e.setInt(f, rec, rendercaps30(v[0]));
    return true;
}

// the first record named nm in block blk's subtree, or -1
inline long fieldOf(const oct::File& f, size_t blk, const char* nm)
{
    for (size_t j = blk + 1, end = f.blockEnd(blk); j < end; ++j)
        if (f.name(f.recs[j]) == nm) return (long)j;
    return -1;
}

// VertexBufferPool: its first non-zero Size becomes the new vbuf size. false: a Size without a value
inline bool vertexPoolSize(oct::File& f, RecEdits& e, size_t size)
{
    const long vbp = f.pool("VertexBufferPool");
    const size_t end = vbp >= 0 ? f.blockEnd(vbp) : 0;
    for (size_t j = vbp + 1; vbp >= 0 && j < end; ++j) {
        if (f.name(f.recs[j]) != "Size") continue;
        const std::vector<int32_t> v = f.ints(e.get(f, j));
        if (v.empty()) return false;
        if (!v[0]) continue;
        e.setInt(f, j, (int64_t)size);
        break;
    }
    return true;
}

// Buffer sizes into the pools; IndexBuffer 2 gets its own file. Commits e once every pool record is found.
inline const char* patchBufferPools(oct::File& f, RecEdits& e, const std::string& stem, const Result& r)
{
    if (!vertexPoolSize(f, e, r.vbuf.size())) return "VertexBufferPool Size without a value";
    const long ibp = f.pool("IndexBufferPool");
    const size_t ibEnd = ibp >= 0 ? f.blockEnd(ibp) : 0;
    std::vector<size_t> ibs;
    for (size_t j = ibp + 1; ibp >= 0 && j < ibEnd; ++j)
        if (f.recs[j].level == 2 && f.name(f.recs[j]) == "IndexBuffer") ibs.push_back(j);
    if (ibs.size() < 3) return "IndexBufferPool shape";
    const long size0 = fieldOf(f, ibs[0], "Size"), size2 = fieldOf(f, ibs[2], "Size");
    const bool ownFile = !r.ibuf2.empty() && fieldOf(f, ibs[2], "FileName") < 0;
    const long name0 = ownFile ? fieldOf(f, ibs[0], "FileName") : 0;
    if (size0 < 0 || size2 < 0 || name0 < 0) return "IndexBufferPool shape";
    e.commit(f);
    f.setInt(f.recs[size0], (int64_t)r.ibuf.size());
    f.setInt(f.recs[size2], (int64_t)r.ibuf2.size());
    if (!ownFile) return nullptr;
    const oct::Rec& fn = f.recs[name0];
    f.recs.insert(f.recs.begin() + f.blockEnd(ibs[2]), f.nameScalar(fn.level, fn.name, stem + "_2.ibuf"));
    return nullptr;
}

}  // namespace detail

// The 1.0 world's files: its stem ('in_city'), .mtb, _0.vbuf and _0.ibuf
struct World10 {
    const std::string& stem;
    const std::string& mtb;
    const std::string& vbuf;
    const std::string& ibuf;
};

namespace detail {

constexpr uint32_t kIndexValues16 = 0x10000;   // every value a 16-bit index takes
constexpr uint64_t kMaxIndex16 = 0xffff;       // a node past it gets 32-bit indices (FORMATS.md 4.4)
constexpr int32_t kIndexBuffer16 = 0, kIndexBuffer32 = 2;   // Idata[0] in 3.0 (FORMATS.md 4.3)
constexpr int kIndexBytes16 = 2, kIndexBytes32 = 4;
constexpr int kNodeLevel = 2;   // a pool's entries (Node, Material); their fields one level deeper (FORMATS.md 4.1)

using FormatMap = std::map<std::pair<int, uint32_t>, uint32_t>;    // (material, 1.0 vformat) -> 3.0 vformat
using ShaderFormats = std::map<int, const std::set<uint32_t>*>;   // material -> vformats its final shader declares

// One transcodeWorld call: its inputs and what the passes before the geometry found
struct WorldRun {
    const Table& T;
    const IO& io;
    Result& r;
    std::map<uint32_t, std::set<int>> users;   // 1.0 vformat -> materials drawn with it
    std::map<int, std::set<uint32_t>> drawn;   // material -> 1.0 vformats it is drawn with
    ShaderFormats shaderVf;
    FormatMap vmap;   // the first 3.0 target the material's shader takes
    FormatMap smap;   // for skinned nodes: the first target with an R plan the shader takes
};

inline bool fail(Result& r, const std::string& why)
{
    r.problems.push_back(why);
    return false;
}

// Material ref's final shader declares vformat c; no entry: any format
inline bool shaderTakes(const ShaderFormats& shaderVf, int ref, uint32_t c)
{
    auto sv = shaderVf.find(ref);
    return sv == shaderVf.end() || sv->second->count(c);
}

inline bool skinnedFormat(const IO& io, uint32_t c) { return io.skinned && io.skinned(c); }

// Translated materials keep the 1.0 format, a skinned one its 3.0 skin target (skintr10). true: all users are
inline bool mapTranslated(WorldRun& w, uint32_t crc, const std::set<int>& refs)
{
    bool all = true;
    const bool skinnedFmt = skinnedFormat(w.io, crc);
    for (int ref : refs) {
        if (w.r.translated.count(ref)) w.vmap[{ref, crc}] = skinnedFmt ? skinTarget30(w.T, crc) : crc;
        else all = false;
    }
    return all;
}

inline void mapConverted(WorldRun& w, int ref, uint32_t crc, const std::vector<uint32_t>& targets)
{
    if (w.r.translated.count(ref)) return;
    const auto takes = [&](uint32_t c) { return shaderTakes(w.shaderVf, ref, c); };
    auto c = std::find_if(targets.begin(), targets.end(), takes);
    if (c != targets.end()) w.vmap[{ref, crc}] = *c;
    else w.r.problems.push_back("vformat: no 3.0 format accepted by material " + std::to_string(ref));
}

inline void mapFormats(WorldRun& w)
{
    for (auto& [crc, refs] : w.users) {
        if (mapTranslated(w, crc, refs)) continue;
        auto t = w.T.targets.find(crc);
        if (t == w.T.targets.end() || t->second.empty()) {
            w.r.problems.push_back("vformat " + std::to_string(crc) + ": no 3.0 target");
            continue;
        }
        for (int ref : refs) mapConverted(w, ref, crc, t->second);
    }
}

inline void mapSkin(WorldRun& w, uint32_t crc, int ref)
{
    auto t = w.T.targets.find(crc);
    if (!w.io.skinLayout || t == w.T.targets.end()) return;
    if (w.r.translated.count(ref) && !skinnedFormat(w.io, crc)) return;   // translated rigid: 1.0 format
    const auto takes = [&](uint32_t c) { return w.T.skinPlans.count({crc, c}) && shaderTakes(w.shaderVf, ref, c); };
    auto c = std::find_if(t->second.begin(), t->second.end(), takes);
    if (c != t->second.end()) w.smap[{ref, crc}] = *c;
}

inline void mapSkinFormats(WorldRun& w)
{
    for (auto& [crc, refs] : w.users)
        for (int ref : refs) mapSkin(w, crc, ref);
}

// One geometry pass: the 1.0 buffers, the index remap, the record edits patchBufferPools commits
struct Geometry {
    const std::string& vbuf10;
    const std::string& ibuf10;
    Remap remap;
    RecEdits edits;
};

// A scene node: whether it is geometry, skinned, and its primitive records
struct NodeScan {
    bool geometry = false, skinned = false;
    std::vector<size_t> prims;
};

inline NodeScan scanNode(const oct::File& f, size_t n)
{
    NodeScan s;
    for (size_t j = n + 1, end = f.blockEnd(n); j < end; ++j) {
        const std::string& nm = f.name(f.recs[j]);
        const bool field = f.recs[j].level == kNodeLevel + 1;
        if (field && nm == "Type") {
            auto v = f.nameValues(f.recs[j]);
            s.geometry = !v.empty() && v[0] == "Geometry";
        }
        if (field && nm == "NumInfluences") s.skinned = true;
        if (nm == "Primitive") s.prims.push_back(j);
    }
    return s;
}

// A primitive's 3.0 vformat and plan; a null plan means same, the 1.0 format kept
struct PrimPlan {
    uint32_t dst = 0;
    Plan same;
    const Plan* plan = nullptr;
    const Plan& get() const { return plan ? *plan : same; }
};

// A skinned node's primitive goes to its 'skin' layout target (R plan); a translated one keeps its format
inline std::optional<PrimPlan> planFor(WorldRun& w, const PrimData& d, const NodeScan& node)
{
    PrimPlan p;
    auto vm = w.vmap.find({d.ref, d.src});
    const bool noTarget = vm == w.vmap.end() && w.T.targets.at(d.src).empty();
    if (noTarget) {
        fail(w.r, "vformat " + std::to_string(d.src) + ": no 3.0 target");
        return std::nullopt;
    }
    p.dst = vm != w.vmap.end() ? vm->second : w.T.targets.at(d.src)[0];
    const Plan* skinPlan = nullptr;
    auto s = w.smap.find({d.ref, d.src});
    if (node.skinned && p.dst != d.src && s != w.smap.end()) {
        p.dst = s->second;
        skinPlan = &w.T.skinPlans.at({d.src, p.dst});
        ++w.r.skinned;
    }
    if (p.dst != d.src) {
        p.plan = skinPlan ? skinPlan : &w.T.plans.at({d.src, p.dst});
        return p;
    }
    if (const char* why = samePlan(w.io, d.src, d.vda, p.same)) {
        fail(w.r, why);
        return std::nullopt;
    }
    return p;
}

// One converted primitive: destination streams and strides, local indices, vertex count, 3.0 vformat
struct PrimOut {
    std::vector<std::string> streams;
    std::vector<int> strides;
    std::vector<uint32_t> sub;
    uint32_t count = 0, dst = 0;
};

// The primitive's vertices through its plan, its indices made local; nullopt: a problem says why
inline std::optional<PrimOut> convertPrimitive(WorldRun& w, Geometry& g, const PrimData& d, const NodeScan& node)
{
    const std::optional<PrimPlan> p = planFor(w, d, node);
    if (!p) return std::nullopt;
    const Plan& plan = p->get();
    PrimOut out;
    std::vector<uint32_t> order;
    const char* why = remapIndices(d.ida, g.ibuf10, g.remap, order, out.sub);
    if (!why) why = convertPrim(plan, d.vda, order, g.vbuf10, out.streams);
    if (why) {
        fail(w.r, why);
        return std::nullopt;
    }
    out.strides = plan.ds;
    out.count = (uint32_t)order.size();
    out.dst = p->dst;
    return out;
}

// The node's vertices stream-major (FORMATS.md 4.4) -> each primitive's stream offsets in nv
inline std::vector<std::vector<uint32_t>> appendStreams(const std::vector<PrimOut>& outs, std::string& nv)
{
    std::vector<std::vector<uint32_t>> off(outs.size());
    size_t maxS = 0;
    for (auto& o : outs) maxS = o.strides.size() > maxS ? o.strides.size() : maxS;
    for (size_t i = 0; i < outs.size(); ++i) off[i].assign(outs[i].strides.size(), 0);
    for (size_t s = 0; s < maxS; ++s)
        for (size_t i = 0; i < outs.size(); ++i) {
            if (s >= outs[i].strides.size()) continue;
            off[i][s] = (uint32_t)nv.size();
            nv += outs[i].streams[s];
        }
    return off;
}

// sub + vbase, indexBytes each, appended to buf -> where they start
inline uint32_t appendIndices(const std::vector<uint32_t>& sub, uint32_t vbase, int indexBytes, std::string& buf)
{
    const uint32_t at = (uint32_t)buf.size();
    for (uint32_t x : sub) {
        uint32_t v = x + vbase;
        buf.append((const char*)&v, indexBytes);
    }
    return at;
}

// Vdata: [streams, vertices, (0, offset, stride) per stream]
inline std::vector<int32_t> vdataOf(const PrimOut& o, const std::vector<uint32_t>& off)
{
    std::vector<int32_t> v = {(int32_t)o.strides.size(), (int32_t)o.count};
    for (size_t s = 0; s < o.strides.size(); ++s) {
        v.push_back(0);
        v.push_back((int32_t)off[s]);
        v.push_back(o.strides[s]);
    }
    return v;
}

// The node's buffers and each primitive's new Idata, Vdata, vformatCRC and RenderCaps, queued in g.edits
inline bool layoutNode(WorldRun& w, oct::File& f, Geometry& g, const std::vector<PrimRecs>& ps,
                       const std::vector<PrimOut>& outs)
{
    const std::vector<std::vector<uint32_t>> off = appendStreams(outs, w.r.vbuf);
    uint64_t total = 0;
    for (auto& o : outs) total += o.count;
    const bool wide = total > kMaxIndex16;
    uint32_t vbase = 0;
    for (size_t i = 0; i < ps.size(); ++i) {
        const PrimOut& o = outs[i];
        const int indexBytes = wide ? kIndexBytes32 : kIndexBytes16;
        const uint32_t at = appendIndices(o.sub, vbase, indexBytes, wide ? w.r.ibuf2 : w.r.ibuf);
        const int32_t width = wide ? kIndexBuffer32 : kIndexBuffer16;
        g.edits.setInts(f, ps[i].at[kIdata], {width, (int32_t)at, (int32_t)vbase, (int32_t)o.sub.size()});
        g.edits.setInts(f, ps[i].at[kVdata], vdataOf(o, off[i]));
        g.edits.setInt(f, ps[i].at[kCrc], o.dst);
        const bool caps = ps[i].has[kCaps];
        if (caps && !caps30(f, g.edits, ps[i].at[kCaps])) return fail(w.r, "RenderCaps without a value");
        vbase += o.count;
    }
    return true;
}

inline bool convertNode(WorldRun& w, oct::File& f, Geometry& g, size_t n)
{
    const NodeScan node = scanNode(f, n);
    if (!node.geometry || node.prims.empty()) return true;
    std::vector<PrimRecs> ps(node.prims.size());
    for (size_t i = 0; i < node.prims.size(); ++i)
        if (!primRecords(f, node.prims[i], ps[i]))
            return fail(w.r, "primitive without Idata, Vdata, vformatCRC or MaterialReference");
    std::vector<PrimOut> outs;
    outs.reserve(ps.size());
    for (auto& p : ps) {
        PrimData d;
        if (const char* why = readPrim(f, p, d)) return fail(w.r, why);
        std::optional<PrimOut> o = convertPrimitive(w, g, d, node);
        if (!o) return false;
        outs.push_back(std::move(*o));
    }
    return layoutNode(w, f, g, ps, outs);
}

inline bool convertNodes(WorldRun& w, oct::File& f, Geometry& g)
{
    const long stp = f.pool("SceneTreeNodePool");
    const size_t stpEnd = stp >= 0 ? f.blockEnd(stp) : 0;   // reads only: the edits wait in g.edits
    for (size_t n = stp + 1; stp >= 0 && n < stpEnd; n = f.blockEnd(n)) {
        const bool node = f.recs[n].level == kNodeLevel && f.name(f.recs[n]) == "Node";
        if (node && !convertNode(w, f, g, n)) return false;
    }
    return true;
}

// conv.convert_geometry + build_buffers; the 32-bit index buffer gets its own file (IndexBuffer 2)
inline bool convertGeometry(WorldRun& w, oct::File& f, const World10& w10)
{
    Geometry g{w10.vbuf, w10.ibuf, {}, {}};
    g.remap.stamp.assign(kIndexValues16, 0);
    g.remap.to.assign(kIndexValues16, 0);
    if (!convertNodes(w, f, g)) return false;
    if (const char* why = patchBufferPools(f, g.edits, w10.stem, w.r)) return fail(w.r, why);
    return true;
}

// Material RenderCaps is 1.0-only (FORMATS.md 4.5, conv.prepare)
inline void dropMaterialRenderCaps(oct::File& f)
{
    const long mp = f.pool("MaterialPool");
    if (mp < 0) return;
    auto b = f.recs.begin() + mp + 1, e = f.recs.begin() + f.blockEnd(mp);
    const auto caps = [&](const oct::Rec& x) { return x.level > kNodeLevel && f.name(x) == "RenderCaps"; };
    f.recs.erase(std::remove_if(b, e, caps), e);
}

}  // namespace detail

// Materials, vertices and .oct primitive records of one 1.0 world, f edited in place. false: r.problems, f unchanged
inline bool transcodeWorld(const Table& T, const IO& io, oct::File& f, const World10& w10, Result& r)
{
    detail::WorldRun w{T, io, r};
    if (const char* why = detail::primPairs(f, w.users, w.drawn)) return detail::fail(r, why);
    if (!transcodeMaterials(T, io, w10.mtb, w.drawn, "", r, w.shaderVf)) return false;
    detail::mapFormats(w);
    if (!r.problems.empty()) return false;
    detail::mapSkinFormats(w);
    // assets without buffers (most mission graphs) have no geometry
    const bool geometry = !w10.vbuf.empty() && !w10.ibuf.empty();
    if (geometry && !detail::convertGeometry(w, f, w10)) return false;
    detail::dropMaterialRenderCaps(f);
    return true;
}

}  // namespace render
