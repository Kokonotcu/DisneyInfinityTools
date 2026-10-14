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
#include <set>
#include <sstream>
#include <unordered_map>
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
struct Op { int dsi, doff, size; char kind; int ssi = 0, soff = 0; std::string st, dt; VType sv{}, dv{}; std::vector<double> dflt; std::string bytes; };
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

    bool load(const std::string& text)
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream t(line);
            std::string k;
            t >> k;
            if (k == "S") {
                std::string name; Shader s; int n;
                t >> name >> s.A >> s.B >> s.slots >> n;
                for (int i = 0; i < n; ++i) { std::string h; t >> h; s.vf.insert((uint32_t)std::stoul(h, nullptr, 16)); }
                t >> n;
                for (int i = 0; i < n; ++i) { std::string r; t >> r; s.roles.push_back(r == "-" ? "" : r); }
                shaders[name] = s;
            } else if (k == "D") {   // a malformed line is dropped, never half-read (extract paths are read with substr(2))
                std::string sh; Ref r; t >> sh >> r.engine >> r.idx >> r.count >> r.extract;
                if (!t.fail() && r.extract.size() > 2) donors[sh] = r;
            } else if (k == "M") {
                std::string g; Ref r; t >> g >> r.engine >> r.idx >> r.extract;
                if (!t.fail() && r.extract.size() > 2) matindex[g] = r;
            } else if (k == "F") { std::string r; t >> r; filled.insert(r); }
            else if (k == "N") { std::string g; t >> g; notReal.insert(g); }
            else if (k == "V") { std::string n; int cb, off, sz; t >> n >> cb >> off >> sz; shaders10.globals[n] = {cb, off, sz}; }
            else if (k == "W") {
                std::string th, bits; int kind, x; t >> th >> kind >> x >> bits;
                shaders10.wordRules[{(uint32_t)std::stoul(th, nullptr, 16), kind, x}] = (uint32_t)std::stoul(bits, nullptr, 16);
            } else if (k == "Q") {
                std::string th, w; int v; t >> th >> w >> v;
                shaders10.passByte[{(uint32_t)std::stoul(th, nullptr, 16), (uint32_t)std::stoul(w, nullptr, 16)}] = v;
            }
            else if (k == "G") { std::string sh; int i; t >> sh; while (t >> i) genericSlots[sh].push_back(i); }
            else if (k == "T") {
                std::string a, b; t >> a;
                auto& v = targets[(uint32_t)std::stoul(a, nullptr, 16)];
                while (t >> b) v.push_back((uint32_t)std::stoul(b, nullptr, 16));
            } else if (k == "P" || k == "R") {
                std::string a, b; Plan p; int n;
                t >> a >> b >> n; p.ss.resize(n); for (auto& x : p.ss) t >> x;
                t >> n; p.ds.resize(n); for (auto& x : p.ds) t >> x;
                t >> n;
                for (int i = 0; i < n; ++i) {
                    Op o; t >> o.dsi >> o.doff >> o.size >> o.kind;
                    if (o.kind == 'C') t >> o.ssi >> o.soff;
                    else if (o.kind == 'X') {
                        int m; t >> o.ssi >> o.soff >> o.st >> o.dt >> m; o.dflt.resize(m); for (auto& d : o.dflt) t >> d;
                        if (!vtype(o.st, o.sv) || !vtype(o.dt, o.dv) || o.sv.n > 4 || o.dv.n > 4) return false;
                    }
                    else { std::string hex; t >> hex; if (hex != "-") for (size_t j = 0; j + 1 < hex.size(); j += 2) o.bytes.push_back((char)std::stoi(hex.substr(j, 2), nullptr, 16)); }
                    p.ops.push_back(o);
                }
                (k == "R" ? skinPlans : plans)[{(uint32_t)std::stoul(a, nullptr, 16), (uint32_t)std::stoul(b, nullptr, 16)}] = p;
            }
        }
        return !shaders.empty() && !plans.empty();
    }
};

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
    if (!ok(o + 8 * (size_t)npar + 4 * ((size_t)b[ha + 1] + b[hb + 1]))) return false;
    // one part: params (name hash, offset) sized up to the next one, slot names, the default block
    auto part = [&](uint32_t key, int size, const uint8_t* names, const uint8_t* words, int from, int to, const uint8_t* slots, int nslots, size_t def) {
        SsfPart p;
        p.size = size;
        std::vector<std::pair<int, uint32_t>> ps;
        for (int i = from; i < to; ++i) ps.push_back({(int)(oct::u(words + 4 * i, 4) >> 16), (uint32_t)oct::u(names + 4 * i, 4)});
        std::sort(ps.begin(), ps.end());
        for (size_t i = 0; i < ps.size(); ++i) p.params[ps[i].second] = {ps[i].first, (i + 1 < ps.size() ? ps[i + 1].first : size) - ps[i].first};
        for (int i = 0; i < nslots; ++i) p.slots.push_back((uint32_t)oct::u(slots + 4 * i, 4));
        p.def = s.substr((std::min)(def, n), size);
        p.off = f.size;
        f.size = (int)al((size_t)f.size + size, 16);
        f.parts.push_back({key, p});
    };
    const uint8_t *names = b + o, *words = b + o + 4 * npar, *snA = words + 4 * npar, *snB = snA + 4 * b[ha + 1];
    int sizeA = (int)oct::u(b + ha + 2, 2), sizeB = (int)oct::u(b + hb + 2, 2);
    o += 8 * (size_t)npar + 4 * ((size_t)b[ha + 1] + b[hb + 1]);
    size_t defA = al(o, 16), defB = al(defA + sizeA, 16);
    part(1, sizeA, names, words, 0, nA, snA, b[ha + 1], defA);
    part(2, sizeB, names, words, nA, npar, snB, b[hb + 1], defB);
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
        part((uint32_t)oct::u(pnames + 4 * i, 4), pSizeB, pnm, pw, pnA, pn, pslots, psB, pdB);
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

// sizes(shader, A, B, slots) -> false if unknown. stride: 8 (3.0) or 4 (1.0).
inline bool parseBundle(const std::string& raw, Bundle& out, int stride,
                        const std::function<bool(const std::string&, int&, int&, int&)>& sizes, std::string* err)
{
    const uint8_t* b = (const uint8_t*)raw.data();
    if (raw.size() < 16 || memcmp(b, "BNDL", 4)) { if (err) *err = "not a BNDL"; return false; }
    uint32_t n = oct::u(b + 12, 4);
    size_t o = 16;
    for (uint32_t c = 0; c < n && o + 16 <= raw.size(); ++c) {
        uint32_t csize = oct::u(b + o + 8, 4);
        if (!memcmp(b + o, "TEXB", 4)) {
            uint32_t cnt = oct::u(b + o + 12, 4);
            for (uint32_t i = 0; i < cnt; ++i) out.textures.emplace_back(raw, o + 20 + 12 * i, 8);
        } else if (!memcmp(b + o, "MATP", 4)) {
            uint32_t nmat = oct::u(b + o + 12, 4);
            size_t p = al(o + 16, 4);
            uint32_t nsh = oct::u(b + p, 4); p += 4;
            std::vector<std::string> shaders;
            for (uint32_t i = 0; i < nsh; ++i) { shaders.emplace_back((const char*)b + p); p += shaders.back().size() + 1; }
            p = al(p, 4);
            if (oct::u(b + p, 4) != nmat) { if (err) *err = "MATP count"; return false; }
            p += 4;
            std::vector<std::string> names;
            for (uint32_t i = 0; i < nmat; ++i) { names.emplace_back((const char*)b + p); p += names.back().size() + 1; }
            p = al(p, 2);
            std::vector<int> sidx;
            for (uint32_t i = 0; i < nmat; ++i) { sidx.push_back((int)oct::u(b + p, 2)); p += 2; }
            p = al(p, 4);
            std::vector<uint32_t> hashes;
            for (uint32_t i = 0; i < nmat; ++i) { hashes.push_back(oct::u(b + p, 4)); p += 4; }
            p = al(p, 16);
            for (uint32_t i = 0; i < nmat; ++i) {
                Material m;
                m.name = names[i];
                m.shader = shaders[sidx[i]];
                m.hash = hashes[i];
                m.stride = stride;
                int A, B, slots;
                if (!sizes(m.shader, A, B, slots)) { if (err) *err = "unknown shader " + m.shader; return false; }
                if (p + A + B > raw.size()) { if (err) *err = "MATP overrun"; return false; }
                m.A = raw.substr(p, A); m.B = raw.substr(p + A, B); m.slots = slots;
                p += A + B;
                out.mats.push_back(m);
            }
        } else if (!memcmp(b + o, "SSET", 4) && o + 20 <= raw.size()) {   // u32 count twice, names, 16-aligned blocks
            uint32_t cnt = oct::u(b + o + 12, 4);
            size_t p = o + 20, end = (std::min)((size_t)o + csize, raw.size());
            for (uint32_t i = 0; i < cnt && p < end; ++i) { out.ssetNames.emplace_back((const char*)b + p); p += out.ssetNames.back().size() + 1; }
            p = al(p, 16);
            size_t sz = cnt && end > p ? (end - p) / cnt : 0;
            for (uint32_t i = 0; i < cnt && sz; ++i) out.ssetBlocks.push_back(raw.substr(p + i * sz, sz));
        }
        if (!csize) break;
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
};

struct Result {
    std::string mtb, vbuf, ibuf, ibuf2;
    std::vector<std::string> problems;
    std::map<std::string, int> tiers;
    std::set<std::string> textures10;   // 1.0 texture hashes (hex) the bundle uses
    std::set<int> translated;           // materials on their real (translated) 1.0 shader
    int skinned = 0;                    // primitives written in their target's 'skin' layout (io.skinLayout)
    int sceneSettings = 0;              // SSET blocks re-laid into 3.0's format (io.ssf10/ssf30)
};

struct Src { Bundle b; int i; };   // one material of a source bundle (merge input)

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
        bool skin = false;
        if (auto dk = drawn.find((int)k); dk != drawn.end() && io.skinned)
            for (uint32_t c : dk->second) skin |= io.skinned(c);
        if (io.translate && !skin && io.read10("shaders/shaders/" + m10.shader + ".shd", raw10) && gen30::parse10(raw10, s10) &&
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
            r.translated.insert((int)k);
            continue;
        }
        auto mi = T.matindex.find(m10.name);
        if (!m10.name.empty() && mi != T.matindex.end() && !T.notReal.count(m10.name)) {
            if (!bundle30(mi->second, bnd)) continue;
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
                const std::string* bestSh = nullptr;
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
                    auto key = [&](const std::string& s, const Ref& rr) { return std::make_tuple(rr.count, s, rr.extract, rr.idx); };
                    if (!bestSh || key(sh, ref) > key(*bestSh, *donor)) { bestSh = &sh; donor = &ref; }
                }
                if (!donor) { r.problems.push_back(m10.name + ": no 3.0 shader accepts its vertex format"); continue; }
            }
            if (!bundle30(*donor, bnd)) continue;
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
        shaderVf[(int)k] = &T.shaders.at(fin.shader).vf;
    }
    if (!r.problems.empty()) return false;

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

// Materials, vertices and the .oct primitive records of one 1.0 world. stem = 'in_city'; f = the 1.0 .oct (edited
// in place); mtb10/vbuf10/ibuf10 = the 1.0 files. Returns false with r.problems if a material cannot be placed.
inline bool transcodeWorld(const Table& T, const IO& io, oct::File& f, const std::string& stem, const std::string& mtb10,
                           const std::string& vbuf10, const std::string& ibuf10, Result& r)
{
    // primitives: (MaterialReference, vformatCRC) per primitive in record order (autobundle.prim_pairs)
    std::map<uint32_t, std::set<int>> users;
    std::map<int, std::set<uint32_t>> drawn;
    {
        int ref = -1;
        for (auto& rec : f.recs) {
            const std::string& n = f.name(rec);
            if (n == "MaterialReference") ref = f.ints(rec)[0];
            else if (n == "vformatCRC") {
                uint32_t crc = (uint32_t)f.ints(rec)[0];
                users[crc].insert(ref);
                drawn[ref].insert(crc);
            }
        }
    }
    std::map<int, const std::set<uint32_t>*> shaderVf;   // material -> vformats its final shader declares
    if (!transcodeMaterials(T, io, mtb10, drawn, "", r, shaderVf)) return false;

    // (material, 1.0 vformat) -> the first 3.0 target that material's shader takes
    std::map<std::pair<int, uint32_t>, uint32_t> vmap;
    for (auto& [crc, refs] : users) {
        bool all = true;
        for (int ref : refs) {
            if (r.translated.count(ref)) vmap[{ref, crc}] = crc;   // its own shader takes the 1.0 format as it is
            else all = false;
        }
        if (all) continue;
        auto t = T.targets.find(crc);
        if (t == T.targets.end() || t->second.empty()) { r.problems.push_back("vformat " + std::to_string(crc) + ": no 3.0 target"); continue; }
        for (int ref : refs) {
            if (r.translated.count(ref)) continue;
            auto sv = shaderVf.find(ref);
            uint32_t pick = 0;
            bool found = false;
            for (uint32_t c : t->second)
                if (sv == shaderVf.end() || sv->second->count(c)) { pick = c; found = true; break; }
            if (!found) r.problems.push_back("vformat: no 3.0 format accepted by material " + std::to_string(ref));
            else vmap[{ref, crc}] = pick;
        }
    }
    if (!r.problems.empty()) return false;
    // (material, 1.0 vformat) -> for a skinned node's primitives: the first target with an R plan the shader takes
    std::map<std::pair<int, uint32_t>, uint32_t> smap;
    for (auto& [crc, refs] : users)
        for (int ref : refs) {
            auto t = T.targets.find(crc);
            if (!io.skinLayout || r.translated.count(ref) || t == T.targets.end()) continue;
            auto sv = shaderVf.find(ref);
            for (uint32_t c : t->second)
                if (T.skinPlans.count({crc, c}) && (sv == shaderVf.end() || sv->second->count(c))) { smap[{ref, crc}] = c; break; }
        }

    // ---- geometry (conv.convert_geometry + build_buffers); assets without buffers (most mission graphs) skip it ----
    const bool geometry = !vbuf10.empty() && !ibuf10.empty();
    const uint8_t* vb = (const uint8_t*)vbuf10.data();
    const uint8_t* ib = (const uint8_t*)ibuf10.data();
    std::string& nv = r.vbuf;
    std::string& ni = r.ibuf;
    std::string& ni2 = r.ibuf2;
    long stp = geometry ? f.pool("SceneTreeNodePool") : -1;
    const size_t stpEnd = stp >= 0 ? f.blockEnd(stp) : 0;   // the loop edits records in place, never adds
    for (size_t n = stp + 1; stp >= 0 && n < stpEnd; n = f.blockEnd(n)) {
        if (f.recs[n].level != 2 || f.name(f.recs[n]) != "Node") continue;
        size_t end = f.blockEnd(n);
        bool geo = false, skinned = false;
        std::vector<size_t> prims;
        for (size_t j = n + 1; j < end; ++j) {
            const std::string& nm = f.name(f.recs[j]);
            if (f.recs[j].level == 3 && nm == "Type") { auto v = f.nameValues(f.recs[j]); geo = !v.empty() && v[0] == "Geometry"; }
            if (f.recs[j].level == 3 && nm == "NumInfluences") skinned = true;
            if (nm == "Primitive") prims.push_back(j);
        }
        if (!geo || prims.empty()) continue;
        struct P { size_t idata, vdata, crc, caps = 0, matref; bool hasCaps = false; };
        std::vector<P> ps;
        for (size_t pi : prims) {
            P p{};
            for (size_t j = pi + 1, pe = f.blockEnd(pi); j < pe; ++j) {
                const std::string& nm = f.name(f.recs[j]);
                if (nm == "Idata") p.idata = j;
                else if (nm == "Vdata") p.vdata = j;
                else if (nm == "vformatCRC") p.crc = j;
                else if (nm == "MaterialReference") p.matref = j;
                else if (nm == "RenderCaps") { p.caps = j; p.hasCaps = true; }
            }
            ps.push_back(p);
        }
        // per primitive: dst streams, strides, local indices, vertex count
        std::vector<std::vector<std::string>> streams;
        std::vector<std::vector<int>> strides;
        std::vector<std::vector<uint32_t>> subs;
        std::vector<uint32_t> counts, dsts;
        for (auto& p : ps) {
            auto ida = f.ints(f.recs[p.idata]);
            auto vda = f.ints(f.recs[p.vdata]);
            uint32_t src = (uint32_t)f.ints(f.recs[p.crc])[0];
            int ref = f.ints(f.recs[p.matref])[0];
            auto vm = vmap.find({ref, src});
            uint32_t dst = vm != vmap.end() ? vm->second : T.targets.at(src)[0];
            const Plan* skinPlan = nullptr;
            if (auto s = smap.find({ref, src}); skinned && dst != src && s != smap.end()) {
                dst = s->second;
                skinPlan = &T.skinPlans.at({src, dst});
                ++r.skinned;
            }
            Plan same;                       // a translated material's primitive: every stream copied as it is
            if (dst == src) {
                for (int s = 0; s < vda[0]; ++s) {
                    same.ss.push_back(vda[4 + 3 * s]);
                    Op o{};
                    o.dsi = s; o.doff = 0; o.size = vda[4 + 3 * s]; o.kind = 'C'; o.ssi = s; o.soff = 0;
                    same.ops.push_back(o);
                }
                same.ds = same.ss;
            }
            const Plan& plan = dst == src ? same : skinPlan ? *skinPlan : T.plans.at({src, dst});
            std::vector<uint32_t> order, sub;
            std::unordered_map<uint32_t, uint32_t> remap;
            for (int32_t q = 0; q < ida[3]; ++q) {
                uint32_t v = (uint32_t)(ib[2 * (ida[1] + q)] | ib[2 * (ida[1] + q) + 1] << 8);
                auto it = remap.find(v);
                if (it == remap.end()) { it = remap.emplace(v, (uint32_t)order.size()).first; order.push_back(v); }
                sub.push_back(it->second);
            }
            for (int s = 0; s < vda[0]; ++s)
                if (s >= (int)plan.ss.size() || vda[4 + 3 * s] != plan.ss[s]) { r.problems.push_back("stride mismatch"); return false; }
            std::vector<std::string> per(plan.ds.size());
            std::vector<const uint8_t*> srcp(plan.ss.size());
            for (uint32_t v : order) {
                for (size_t s = 0; s < plan.ss.size(); ++s) {
                    size_t lo = (size_t)vda[3 + 3 * s] + (size_t)v * plan.ss[s];
                    if (lo + plan.ss[s] > vbuf10.size()) { r.problems.push_back("vertex past the vbuf"); return false; }
                    srcp[s] = vb + lo;
                }
                convertVertex(srcp, plan, per);
            }
            streams.push_back(std::move(per)); strides.push_back(plan.ds); subs.push_back(std::move(sub));
            counts.push_back((uint32_t)order.size()); dsts.push_back(dst);
        }
        // stream-major within the node
        std::vector<std::vector<uint32_t>> nodeOff(ps.size());
        size_t maxS = 0;
        for (auto& s : strides) maxS = s.size() > maxS ? s.size() : maxS;
        for (size_t i = 0; i < ps.size(); ++i) nodeOff[i].assign(strides[i].size(), 0);
        for (size_t s = 0; s < maxS; ++s)
            for (size_t i = 0; i < ps.size(); ++i)
                if (s < strides[i].size()) { nodeOff[i][s] = (uint32_t)nv.size(); nv += streams[i][s]; }
        uint64_t total = 0;
        for (auto c : counts) total += c;
        bool wide = total > 0xffff;
        uint32_t vbase = 0;
        for (size_t i = 0; i < ps.size(); ++i) {
            std::string& buf = wide ? ni2 : ni;
            uint32_t ioff = (uint32_t)buf.size();
            for (uint32_t x : subs[i]) {
                uint32_t v = x + vbase;
                buf.append((const char*)&v, wide ? 4 : 2);
            }
            std::vector<int32_t> vdata = {(int32_t)strides[i].size(), (int32_t)counts[i]};
            for (size_t s = 0; s < strides[i].size(); ++s) { vdata.push_back(0); vdata.push_back((int32_t)nodeOff[i][s]); vdata.push_back(strides[i][s]); }
            f.setInts(f.recs[ps[i].idata], {wide ? 2 : 0, (int32_t)ioff, (int32_t)vbase, (int32_t)subs[i].size()});
            f.setInts(f.recs[ps[i].vdata], vdata);
            f.setInt(f.recs[ps[i].crc], dsts[i]);
            if (ps[i].hasCaps) f.setInt(f.recs[ps[i].caps], rendercaps30(f.ints(f.recs[ps[i].caps])[0]));
            vbase += counts[i];
        }
    }

    // buffer sizes; the 32-bit index buffer gets its own file (IndexBuffer 2)
    if (geometry) {
        long vbp = f.pool("VertexBufferPool");
        for (size_t j = vbp + 1; vbp >= 0 && j < f.blockEnd(vbp); ++j)
            if (f.name(f.recs[j]) == "Size" && f.ints(f.recs[j])[0]) { f.setInt(f.recs[j], (int64_t)nv.size()); break; }
        long ibp = f.pool("IndexBufferPool");
        std::vector<size_t> ibs;
        for (size_t j = ibp + 1; ibp >= 0 && j < f.blockEnd(ibp); ++j)
            if (f.recs[j].level == 2 && f.name(f.recs[j]) == "IndexBuffer") ibs.push_back(j);
        if (ibs.size() < 3) { r.problems.push_back("IndexBufferPool shape"); return false; }
        auto field = [&](size_t blk, const char* nm) -> long {
            for (size_t j = blk + 1; j < f.blockEnd(blk); ++j) if (f.name(f.recs[j]) == nm) return (long)j;
            return -1;
        };
        f.setInt(f.recs[field(ibs[0], "Size")], (int64_t)ni.size());
        f.setInt(f.recs[field(ibs[2], "Size")], (int64_t)ni2.size());
        if (!ni2.empty() && field(ibs[2], "FileName") < 0) {
            const oct::Rec& fn = f.recs[field(ibs[0], "FileName")];
            f.recs.insert(f.recs.begin() + f.blockEnd(ibs[2]), f.nameScalar(fn.level, fn.name, stem + "_2.ibuf"));
        }
    }
    // 1.0-only Material RenderCaps removed (conv.prepare)
    if (long mp = f.pool("MaterialPool"); mp >= 0) {
        auto b = f.recs.begin() + mp + 1, e = f.recs.begin() + f.blockEnd(mp);
        f.recs.erase(std::remove_if(b, e, [&](const oct::Rec& x) { return x.level >= 3 && f.name(x) == "RenderCaps"; }), e);
    }
    return true;
}

}  // namespace render
