// Real 1.0 shaders in 3.0: a 1.0 shader description (.shd) and its D3D9 programs -> a 3.0 .shd and HLSL for D3D11.
// C++ port of DI3Tools/gen30.py (its docstring has every rule and where it was measured); octtest --gen30 checks
// the output against it. The 3.0-side tables (global constant layout, learned pass-word bits, pass +0x1c bytes)
// come from the render table (rentable.py V / W / Q lines).
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "base/bytes.hpp"
#include "formats/shd.hpp"
#include "sm3.hpp"

namespace gen30 {

// The engine's name hash (FUN_0044a9f0): case-sensitive; lower = the vformat/shader GUID hash (FUN_0044aa40).
inline uint32_t ehash(const std::string& s, bool lower = false)
{
    std::string b = s;
    if (lower) for (char& c : b) if (c >= 'A' && c <= 'Z') c += 0x20;
    b.push_back('\0');
    auto ch = [&](size_t i) { return (uint32_t)(int32_t)(int8_t)b[i]; };
    uint32_t u = ch(0), acc = 0;
    size_t i = 0;
    while (b[i]) {
        ++i;
        u = (u << 8) | ch(i);
        if ((i & 3) == 3) acc = (acc + u) * 0x25;
    }
    return (u << ((24 - 8 * (i & 3)) & 31)) + acc;
}

struct Tables {
    std::unordered_map<std::string, std::array<int, 3>> globals;           // name -> buffer, offset, size
    std::map<std::tuple<uint32_t, int, int>, uint32_t> wordRules;         // (technique, depth kind, X) -> bits
    std::map<std::pair<uint32_t, uint32_t>, int> passByte;                // (technique, 3.0 word) -> +0x1c
};

struct Param { uint32_t hash; bool A; int offset, type; };
struct Pass { uint32_t word, vf, vs, ps; };
struct Tech { uint32_t hash; int first, count; };
struct Shd10 { std::vector<Pass> passes; std::vector<Tech> techs; std::vector<Param> params; std::vector<uint32_t> slotsB; int sizeA = 0, sizeB = 0; size_t defaultsAt = 0; };

// 1.0's .shd (DisneyInfinity.exe FUN_004af380 'Shaders/%s.shd', finalize FUN_004af200); a name ends its slot names
inline constexpr shd::Layout kShd10 = {0x0C, 0xA4, 0x30, 0x10, 0x18, 0x20, 0x44, 0x68};
constexpr size_t kShd10TechSlots = 0x30;   // 16 bytes, 3.0's +0x54 in the same slot order (gen30.py)

// shd.parse(b, '1.0')
inline bool parse10(const std::string& b, Shd10& s)
{
    const shd::Layout& L = kShd10;
    if (b.size() < L.data) return false;
    int npar = (uint8_t)b[L.counts], npass = (uint8_t)b[L.counts + 1], ntech = (uint8_t)b[L.counts + 2];
    size_t o = L.data;
    for (int i = 0; i < npass; ++i, o += L.pass) {
        const uint32_t word = bytes::u32OrZero(b, o), vf = bytes::u32OrZero(b, o + L.passVformat);
        s.passes.push_back({word, vf, bytes::u32OrZero(b, o + L.passVs), bytes::u32OrZero(b, o + L.passPs)});
    }
    std::vector<uint16_t> tr;
    for (int i = 0; i < ntech; ++i) tr.push_back(bytes::u16OrZero(b, o + 2 * i));
    o = bytes::alignUp(o + 2 * ntech, 4);
    std::vector<uint32_t> pn, tn, pw;
    for (int i = 0; i < npar; ++i, o += 4) pn.push_back(bytes::u32OrZero(b, o));
    for (int i = 0; i < ntech; ++i, o += 4) tn.push_back(bytes::u32OrZero(b, o));
    for (int i = 0; i < npar; ++i, o += 4) pw.push_back(bytes::u32OrZero(b, o));
    int nA = (uint8_t)b[L.blockA], slotsA = (uint8_t)b[L.blockA + 1], slotsB = (uint8_t)b[L.blockB + 1];
    s.sizeA = bytes::u16OrZero(b, L.blockA + 2);
    s.sizeB = bytes::u16OrZero(b, L.blockB + 2);
    o += 4 * slotsA;
    for (int i = 0; i < slotsB; ++i, o += 4) s.slotsB.push_back(bytes::u32OrZero(b, o));
    size_t e = b.find('\0', o);
    if (e == std::string::npos) return false;
    s.defaultsAt = e + 1;
    for (int i = 0; i < ntech; ++i) s.techs.push_back({tn[i], tr[i] & 0xff, tr[i] >> 8});
    for (int i = 0; i < npar; ++i) s.params.push_back({pn[i], i < nA, (int)(pw[i] >> 16), (int)(pw[i] & 0xffff)});
    return o <= b.size();
}

// A D3D9 program's constant table: (name, set 'b'/'i'/'f'/'s', register, count).
struct Const { std::string name; char set; int reg, count; };

// CTAB entry k starts inside b (its fields are read checked); the file's count may be any u32
inline bool entryStarts(const std::string& b, size_t base, uint32_t info, uint32_t k)
{
    return (uint64_t)base + info + 20ull * k < b.size();
}
inline std::vector<Const> ctab(const std::string& b)
{
    std::vector<Const> out;
    size_t i = b.find("CTAB");
    if (i == std::string::npos) return out;
    size_t base = i + 4;
    uint32_t n = bytes::u32OrZero(b, base + 12), info = bytes::u32OrZero(b, base + 16);
    for (uint32_t k = 0; k < n && entryStarts(b, base, info, k); ++k) {
        size_t e = base + info + 20 * k;
        uint32_t nm = bytes::u32OrZero(b, e);
        uint16_t rs = bytes::u16OrZero(b, e + 4);
        uint16_t reg = bytes::u16OrZero(b, e + 6);
        uint16_t cnt = bytes::u16OrZero(b, e + 8);
        size_t z = b.find('\0', base + nm);
        if (z == std::string::npos || rs > 3) continue;
        out.push_back({b.substr(base + nm, z - base - nm), "bifs"[rs], reg, cnt});
    }
    return out;
}

// 1.0 vertex formats: GUID hash -> semantic (with index, 'NORMAL' -> 'NORMAL0') -> element type, from vformat.vtxf.
using VFormats = std::unordered_map<uint32_t, std::map<std::string, std::string>>;

// The entries of vformat.vtxf's formats table: (GUID, entry start, entry end incl. its trailing comma). Keys come as
// ["<guid>"] or, when the GUID starts with a letter, as a bare identifier (15 of 1.0's 44).
struct VEntry { std::string guid; size_t start, end; };
inline std::vector<VEntry> ventries(const std::string& t, size_t* tableEnd = nullptr)
{
    std::vector<VEntry> out;
    size_t f = t.find("formats");
    if (f == std::string::npos) return out;
    size_t i = t.find('{', f);
    if (i == std::string::npos) return out;
    ++i;
    auto ws = [&]() { while (i < t.size() && isspace((unsigned char)t[i])) ++i; };
    while (true) {
        ws();
        if (i >= t.size() || t[i] == '}') { if (tableEnd) *tableEnd = i; break; }
        size_t start = i;
        std::string guid;
        if (t[i] == '[') {
            // a truncated key ends the scan; npos + 1 would restart it at the first entry forever (RE-48)
            size_t q = t.find('"', i), e = q == std::string::npos ? q : t.find('"', q + 1);
            size_t close = e == std::string::npos ? e : t.find(']', e);
            if (close == std::string::npos) break;
            guid = t.substr(q + 1, e - q - 1);
            i = close + 1;
        } else {
            size_t e = i;
            while (e < t.size() && (isalnum((unsigned char)t[e]) || t[e] == '_')) ++e;
            guid = t.substr(i, e - i);
            i = e;
        }
        i = t.find('{', i);
        if (i == std::string::npos) break;
        int d = 0;
        for (; i < t.size(); ++i) { if (t[i] == '{') ++d; else if (t[i] == '}' && --d == 0) break; }
        ++i;
        ws();
        if (i < t.size() && t[i] == ',') ++i;
        out.push_back({guid, start, i});
    }
    return out;
}

inline VFormats vformats(const std::string& t)
{
    VFormats out;
    for (auto& v : ventries(t)) {
        size_t m = t.find("main", v.start);
        if (m == std::string::npos || m > v.end) continue;
        size_t ob = t.find('{', m);
        int d = 0;
        size_t end = ob;
        for (; end < t.size(); ++end) { if (t[end] == '{') ++d; else if (t[end] == '}' && --d == 0) break; }
        auto& f = out[ehash(v.guid, true)];
        for (size_t s = t.find("semantic = \"", ob); s != std::string::npos && s < end; s = t.find("semantic = \"", s + 1)) {
            size_t se = t.find('"', s + 12);
            std::string sem = t.substr(s + 12, se - s - 12);
            // this element's type: the element's braces are { ... }; type follows within it
            size_t eb = t.rfind('{', s), ee = t.find('}', s);
            size_t ty = t.find("type = \"", eb);
            if (ty == std::string::npos || ty > ee) continue;
            size_t te = t.find('"', ty + 8);
            if (!sem.empty() && !isdigit((unsigned char)sem.back())) sem += "0";
            f[sem] = t.substr(ty + 8, te - ty - 8);
        }
    }
    return out;
}

// ---- the served vformat.vtxf: 3.0's formats plus every 1.0 format 3.0 lacks ----
// The engine's parser (FUN_004642f0, 'main' layouts) numbers element names first come (FUN_0044d730, 32 of them) and
// gives each name 8 descriptor slots (FUN_0044d790 at 0x0216f260; descriptor = size, type, semantic, offset, stream).
// A 9th distinct descriptor returns name*8|8: the NEXT name's slot 0, so that element turns into another one. 3.0
// uses 7 for color0; 1.0's formats add five more (color0 ending stream 1 at offsets 8..28), 12 in all: in the
// parser's Lua table order 3.0's 1a9a10d9 got COLOR1@stream 1+32 for its COLOR0, so the env shader 88c83eaf's
// G-buffer VS had no COLOR0 and the 3.0-native expendables on that format drew black (dock crates) or in flat
// per-face colours (dumpster) (in-game 2026-10-14). So a 1.0 format whose last stream ends in vertex colour at a
// non-zero offset gets that colour as a stream of its own (color0 at 0: a descriptor 3.0 has), and translated
// primitives in it are split the same way (render::transcodeWorld). Names and semantics are untouched.
struct ColourTail { int stream = 0, offset = 0, size = 0; };   // bytes [offset, offset+size) of stream -> stream+1

// direct child tables of the table opening at t[open]: {open, close} pairs
inline std::vector<std::pair<size_t, size_t>> children(const std::string& t, size_t open)
{
    std::vector<std::pair<size_t, size_t>> out;
    int d = 0;
    size_t start = 0;
    for (size_t i = open; i < t.size(); ++i) {
        if (t[i] == '{') { if (++d == 2) start = i; }
        else if (t[i] == '}') { if (d == 2) out.push_back({start, i}); if (--d == 0) break; }
    }
    return out;
}

inline std::string field(const std::string& t, size_t a, size_t b, const char* key)   // key = "..." within [a, b)
{
    std::string k = std::string(key) + " = \"";
    size_t p = t.find(k, a);
    if (p == std::string::npos || p >= b) return "";
    size_t q = t.find('"', p + k.size());
    return t.substr(p + k.size(), q - p - k.size());
}

inline std::map<std::string, int> typeSizes(const std::string& t)   // 'types' table: name -> sizeInBytes
{
    std::map<std::string, int> out;
    size_t ty = t.find("\ttypes");
    if (ty == std::string::npos) return out;
    for (auto [a, b] : children(t, t.find('{', ty))) {
        size_t s = t.rfind('=', a);
        if (s == std::string::npos) continue;   // t[npos - 1] below
        while (s > 0 && isspace((unsigned char)t[s - 1])) --s;
        size_t n0 = s;
        while (n0 > 0 && (isalnum((unsigned char)t[n0 - 1]) || t[n0 - 1] == '_')) --n0;
        size_t z = t.find("sizeInBytes = ", a);
        if (z != std::string::npos && z < b) out[t.substr(n0, s - n0)] = atoi(t.c_str() + z + 14);
    }
    return out;
}

// one format entry's text with a trailing vertex colour moved to its own stream (unchanged if it has none)
inline std::string relayColour(const std::string& f, const std::map<std::string, int>& sizes, ColourTail& tail)
{
    size_t m = f.find("main"), st = m == std::string::npos ? m : f.find("streams", m);
    if (st == std::string::npos) return f;
    auto streams = children(f, f.find('{', st));
    if (streams.size() < 2) return f;
    auto [so, sc] = streams.back();
    auto el = children(f, so);
    std::vector<int> off;
    int o = 0;
    for (auto [a, b] : el) {
        auto ty = sizes.find(field(f, a, b, "type"));
        if (ty == sizes.end()) return f;
        off.push_back(o);
        o += ty->second;
    }
    size_t k = el.size();
    while (k > 0 && field(f, el[k - 1].first, el[k - 1].second, "semantic").compare(0, 5, "COLOR") == 0) --k;
    if (k == el.size() || k == 0) return f;            // no trailing colour, or the stream is colour only
    std::string moved;
    for (size_t i = k; i < el.size(); ++i) moved += "\n" + f.substr(el[i].first, el[i].second - el[i].first + 1) + ",";
    tail = {(int)streams.size() - 1, off[k], o - off[k]};
    size_t cut = f.find(',', el[k - 1].second) + 1;    // after the last element kept
    return f.substr(0, cut) + "\n}," + "\n{" + moved + "\n}" + f.substr(sc + 1);
}

// added: 1.0 formats appended; tails: vformat GUID hash (ehash lower) -> the colour moved out of a re-laid one
inline std::string mergeVtxf(const std::string& t30, const std::string& t10, int& added, std::map<uint32_t, ColourTail>& tails)
{
    added = 0;
    size_t end30 = 0;
    auto e30 = ventries(t30, &end30);
    if (!end30) return t30;
    std::set<std::string> have;
    for (auto& e : e30) { std::string g = e.guid; for (char& c : g) c = (char)tolower((unsigned char)c); have.insert(g); }
    auto sizes = typeSizes(t10);
    std::string add;
    for (auto& e : ventries(t10)) {
        std::string g = e.guid;
        for (char& c : g) c = (char)tolower((unsigned char)c);
        if (have.count(g)) continue;
        ColourTail ct;
        std::string block = relayColour(t10.substr(e.start, e.end - e.start), sizes, ct);
        if (ct.size) tails[ehash(g, true)] = ct;
        if (block.back() != ',') block += ",";
        add += "\t\t" + block + "\n";
        ++added;
    }
    return t30.substr(0, end30) + add + t30.substr(end30);
}

inline uint32_t convWord(uint32_t w)
{
    return ((w & 7) << 3) | (((w >> 3) & 0xf) << 6) | (((w >> 7) & 3) << 10) | (((w >> 9) & 0xf) << 12) | (((w >> 13) & 7) << 16);
}
inline uint32_t word30(const Tables& T, uint32_t tech, uint32_t w10)
{
    uint32_t w = convWord(w10);
    auto it = T.wordRules.find({tech, (int)shd::depthField(w), (int)shd::xField(w)});
    return w | (it != T.wordRules.end() ? it->second : 0);
}

struct Layout { std::map<uint32_t, int> A, B; int first10 = 0, first30 = 0, firstB = 0, slots = 0, sizeA = 0, sizeB = 0; };
inline Layout layout(const Shd10& s)
{
    Layout L;
    L.slots = (int)s.slotsB.size();
    int first10 = (L.slots * 5 + 15) & ~15;
    bool any = false;
    for (auto& p : s.params) if (!p.A) { first10 = any ? (std::min)(first10, p.offset) : p.offset; any = true; }
    L.first10 = first10;
    L.first30 = (L.slots * 9 + 15) & ~15;
    for (auto& p : s.params) (p.A ? L.A : L.B)[p.hash] = p.A ? p.offset : p.offset - first10 + L.first30;
    L.firstB = L.first30 / 16;
    L.sizeA = s.sizeA;
    L.sizeB = L.first30 + (std::max)(s.sizeB - first10, 0);
    return L;
}

constexpr uint32_t kEmptySlot = 0xffffffff;   // an empty slot's texture index
constexpr size_t kSlot30Bytes = 8;            // a 3.0 slot: texture index, then 0 (FORMATS.md 6.2)

inline std::string blockB30(const std::string& b10, const Layout& L)
{
    std::string out;
    for (int i = 0; i < L.slots; ++i) {
        uint32_t idx = bytes::u32OrZero(b10, 4 * i), w[2] = {idx, idx == kEmptySlot ? kEmptySlot : 0u};
        out.append((const char*)w, sizeof w);
    }
    out += b10.substr(std::min<size_t>(4 * L.slots, b10.size()), L.slots);
    out.resize(L.first30, '\0');
    if ((size_t)L.first10 < b10.size()) out += b10.substr(L.first10);
    out.resize(L.sizeB, '\0');
    return out;
}

static const char* GBUFFER_EPILOGUE =
    "    {   // 3.0 G-buffer encoding of the 1.0 world normal (.xyz) and gloss (.w)\n"
    "        float3 n = o.oC0.xyz / (abs(o.oC0.x) + abs(o.oC0.y) + abs(o.oC0.z));\n"
    "        float2 f = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0);\n"
    "        o.oC0 = float4((n.z >= 0 ? n.xy : f) * 0.5 + 0.5, o.oC0.w, 1.0);\n"
    "        o.o1 = float4(0, 0, 0, 0);\n"
    "        o.o2 = float4(1, 1, 1, 0);\n"
    "    }";

// cbuffer registers b0..b4 Binding30 declares, one name each (perFrame .. perInstance); the table's V lines name one
constexpr int kConstantBuffers = 5;

struct Binding30 : sm3::Binding {
    const Tables& T;
    const Layout& L;
    std::map<std::string, std::string> fmt;
    std::map<int, std::pair<std::string, int>> creg;
    std::map<int, int> used;                              // buffer register -> registers declared
    std::map<int, std::string> sname;                     // sampler register -> 1.0 sampler name
    bool envOff(int) { return false; }   // test 2026-10-13: dropping environment maps did not fix the black objects
    std::vector<std::string> unresolved;
    // skinned pass (cfg skintr10): 3.0 skins on the GPU before the material shader, so the VS reads vertices skinned
    // already, in world space (3.0's own skinned-material VSs: worldToProjected only, no bone input). The 1.0 VS's
    // own skinning becomes identity: bone indices 0, weights (1, 0, 0, 0), every bone the identity matrix.
    bool skin = false;
    std::set<std::string> needed;   // the vertex inputs the VS still reads
    bool inputConst(const std::string& sem, std::string& expr) override
    {
        if (skin && sem.compare(0, 12, "BLENDINDICES") == 0) { expr = "float4(0, 0, 0, 0)"; return true; }
        if (skin && sem.compare(0, 11, "BLENDWEIGHT") == 0) { expr = "float4(1, 0, 0, 0)"; return true; }
        needed.insert(sem);
        return false;
    }
    Binding30(const Tables& t, const Layout& l, const std::vector<Const>& ct, std::map<std::string, std::string> f, bool gb)
        : T(t), L(l), fmt(std::move(f))
    {
        for (auto& c : ct) {
            if (c.set == 'f') for (int k = 0; k < c.count; ++k) creg[c.reg + k] = {c.name, c.reg};
            if (c.set == 's') sname[c.reg] = c.name;
        }
        gbuffer = gb;
        if (gb) epilogue = GBUFFER_EPILOGUE;
    }
    // -> buffer register (0..4) and first register, or buffer -1 with an expression, or false
    bool locate(const std::string& name, int& cb, int& base, std::string& expr)
    {
        uint32_t h = ehash(name);
        if (auto a = L.A.find(h); a != L.A.end()) { cb = 4; base = a->second / 16; return true; }
        if (auto b = L.B.find(h); b != L.B.end()) { cb = 3; base = b->second / 16 - L.firstB; return true; }
        if (auto g = T.globals.find(name); g != T.globals.end()) { cb = g->second[0]; base = g->second[1] / 16; return true; }
        if (name == "g_fog_sunLight_color_primary") {
            auto p = T.globals.find("g_fog_sunLight_params"), c = T.globals.find("g_fog_color");
            if (p == T.globals.end() || c == T.globals.end()) return false;
            int pr = p->second[1] / 16, cr = c->second[1] / 16;
            used[0] = (std::max)({used[0], pr + 1, cr + 1});
            cb = -1;
            expr = "float4(CB0[" + std::to_string(pr) + "].xyz + CB0[" + std::to_string(cr) + "].xyz, CB0[" + std::to_string(pr) + "].w)";
            return true;
        }
        return false;
    }
    std::string constant(int num, const std::string& rel) override
    {
        auto hit = creg.find(num);
        if (skin && hit != creg.end() && hit->second.first == "g_skinning_boneTransforms") {   // row_major float4x4 per bone
            static const char* const I[] = {"float4(1, 0, 0, 0)", "float4(0, 1, 0, 0)", "float4(0, 0, 1, 0)", "float4(0, 0, 0, 1)"};
            return I[(num - hit->second.second) & 3];
        }
        int cb = 0, base = 0;
        std::string expr;
        if (hit == creg.end() || !locate(hit->second.first, cb, base, expr)) {
            unresolved.push_back(hit != creg.end() ? hit->second.first : "c" + std::to_string(num));
            return "float4(0, 0, 0, 0)";
        }
        if (cb < 0) return expr;
        int reg0 = hit->second.second;
        if (!rel.empty()) {
            int span = 0;
            for (auto& [r, h] : creg) if (h == hit->second) span = (std::max)(span, r - reg0 + 1);
            used[cb] = (std::max)(used[cb], base + span);
            return "CB" + std::to_string(cb) + "[" + rel + " + " + std::to_string(base + num - reg0) + "]";
        }
        used[cb] = (std::max)(used[cb], base + num - reg0 + 1);
        return "CB" + std::to_string(cb) + "[" + std::to_string(base + num - reg0) + "]";
    }
    std::string boolean(int) override { return "false"; }
    std::string cbuffers() override
    {
        static const char* names[kConstantBuffers] = {"perFrame", "perView", "perDraw", "perMaterial", "perInstance"};
        std::string s;
        for (auto& [k, n] : used)
            if (n) s += (s.empty() ? "" : "\n") + std::string("cbuffer ") + names[k] + " : register(b" + std::to_string(k) + ") { float4 CB" +
                        std::to_string(k) + "[" + std::to_string(n) + "]; };";
        return s;
    }
    // 1.0's normal-gloss target (s1) holds the world normal and gloss (.w); 3.0's (t1) the octahedral normal (.xy) and,
    // through GBUFFER_EPILOGUE, the same gloss in .z: NG10 hands a 1.0 program its own layout. One declaration for
    // both users (the light bridge reads gloss too).
    static constexpr const char* NG10 =
        "#ifndef NG10\n#define NG10\nTexture2D tNG : register(t1); SamplerState sNG : register(s1);\n"
        "float4 ng10(float4 t) { float2 e = t.xy * 2.0 - 1.0; float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));\n"
        "    if (n.z < 0) n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0); return float4(normalize(n), t.z); }\n#endif";
    void sampler(int num, int kind, std::string& tex, std::string& smp, std::string& decl) override
    {
        if (num == 1) { tex = "tNG"; smp = "sNG"; decl = NG10; return; }
        if (envOff(num)) { tex = smp = decl = ""; return; }
        if (num == 2) {
            tex = "tLight"; smp = "sLight";
            decl = std::string(NG10) + "\nTexture2D tLight : register(t3); SamplerState sLight : register(s3); Texture2D tSpec : register(t4); SamplerState sSpec : register(s4);";
            return;
        }
        int r = num == 0 ? 0 : num == 1 ? 1 : 5 + num - 3;
        const char* Tn = kind == 3 ? "TextureCube" : kind == 4 ? "Texture3D" : "Texture2D";
        tex = "t" + std::to_string(r); smp = "s" + std::to_string(r);
        decl = std::string(Tn) + " " + tex + " : register(t" + std::to_string(r) + "); SamplerState " + smp + " : register(s" + std::to_string(r) + ");";
    }
    std::string sample(int num, int kind, const std::string& method, const std::string& uv, const std::string& extra) override
    {
        std::string x = extra.empty() ? "" : ", " + extra;
        if (envOff(num)) return "float4(0, 0, 0, 0)";
        if (num == 1) return "ng10(tNG." + method + "(sNG, " + uv + x + "))";
        if (num == 2) {
            // 1.0's light buffer: diffuse light .rgb, raw specular luminance x6 in .a, which its final pass scales by
            // the Blinn normalization (exp2(10 gloss + 1) + 2) / 8. 3.0's specular buffer is normalized already:
            // divided here, so 1.0's multiply restores exactly 3.0's lighting (it multiplied it ~256x: blown out).
            return "float4(tLight." + method + "(sLight, " + uv + x + ").rgb, 6.0 * dot(tSpec." + method + "(sSpec, " + uv + x +
                   ").rgb, float3(0.2125, 0.7154, 0.0721)) * 8.0 / (exp2(10.0 * ng10(tNG." + method + "(sNG, " + uv + x +
                   ")).w + 1.0) + 2.0))";
        }
        return sm3::Binding::sample(num, kind, method, uv, extra);
    }
    std::string inputType(const std::string& sem) override
    {
        static const std::map<std::string, std::string> I = {{"U8_4", "uint"}, {"U16_2", "uint"}, {"U16_4", "uint"}, {"U8_1", "uint"},
                                                           {"U16_1", "uint"}, {"S16_2", "int"}, {"S16_4", "int"}, {"S8_4", "int"}};
        auto f = fmt.find(sem);
        if (f == fmt.end()) return "float";
        auto i = I.find(f->second);
        return i != I.end() ? i->second : "float";
    }
};

inline uint32_t crc32(const std::string& s)
{
    uint32_t c = 0xffffffff;
    for (unsigned char ch : s) { c ^= ch; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320 & (0u - (c & 1))); }
    return ~c;
}
inline uint32_t progId(const std::string& shader, int i, const char* kind)
{
    return crc32("di1compat:" + shader + ":" + std::to_string(i) + ":" + kind) | 0x80000000;
}

struct Program { uint32_t id; bool ps; std::string hlsl, code; };
struct Generated { std::string shd; std::vector<Program> programs; std::vector<std::string> problems; };
using Read = std::function<bool(const std::string& path, std::string& out)>;
using Compile = std::function<bool(const std::string& hlsl, const char* target, std::string& code, std::string& msg)>;

// A 1.0 program by id: file names are '%x' (1.0's 'Shaders/%x.vp', no zero padding).
inline bool program10(const Read& read10, uint32_t id, const char* kind, std::string& out)
{
    char a[32], b[32];
    sprintf_s(a, "shaders/shaders/%x.%s", id, kind);
    sprintf_s(b, "shaders/shaders/%08x.%s", id, kind);
    return read10(a, out) || read10(b, out);
}

// What generate reads
struct Sources {
    Read read10;
    const VFormats& vf;                             // 1.0's vertex formats
    Compile compile;                                // empty: no code, sizes 0 (gen30.py compile_=False)
    std::function<uint32_t(uint32_t)> skinTarget;   // skintr10: a skinned 1.0 format's 3.0 target
    const VFormats* vf30 = nullptr;                 // skintr10: 3.0's formats; without them a skinned pass fails
};

// A pass's vertex format; a skinned pass draws its 1.0 format's 3.0 skin target, the GPU skinning's output
struct PassFormat {
    uint32_t vformat = 0;
    std::map<std::string, std::string> elements;   // semantic -> element type
    bool skinned = false;
};

inline PassFormat passFormat(const Sources& in, const Pass& p, const std::string& at, Generated& g)
{
    PassFormat f;
    f.vformat = p.vf;
    if (auto e = in.vf.find(p.vf); e != in.vf.end()) f.elements = e->second;
    else g.problems.push_back(at + ": vformat unknown");
    if (!in.skinTarget || !f.elements.count("BLENDINDICES0")) return f;
    const uint32_t t = in.skinTarget(p.vf);
    auto f30 = in.vf30 && t ? in.vf30->find(t) : VFormats::const_iterator();
    if (!t || !in.vf30 || f30 == in.vf30->end()) {
        g.problems.push_back(at + ": skinned, no 3.0 format to draw it in");
        return f;
    }
    f.vformat = t;
    f.elements = f30->second;
    f.skinned = true;
    return f;
}

// One 1.0 shader being generated
struct ShaderJob {
    const Tables& T;
    const Sources& in;
    const std::string& shader;
    const Shd10& s;
    const Layout& L;
    std::map<int, uint32_t> techOf;   // pass -> its technique's hash
};

// One pass of it: its index, 3.0 word and vertex format
struct PassJob {
    const ShaderJob& sj;
    size_t index;
    uint32_t word;
    const PassFormat& format;
};

enum class Stage { Vertex, Pixel };
struct ProgramRef { uint32_t id = 0, size = 0; };
constexpr size_t kCompileMessageChars = 200;

// Every input a skinned VS still reads must be in the 3.0 format, or no input layout can be made
inline void checkSkinInputs(const Binding30& B, const PassFormat& f, const std::string& at, Generated& g)
{
    for (auto& sem : B.needed)
        if (!f.elements.count(sem)) g.problems.push_back(at + ": skinned, 3.0 format lacks " + sem);
}

// The program as HLSL through Binding30; false: sm3 refused it, a problem says why
inline bool translateProgram(const PassJob& job, Stage stage, const std::string& b, std::string& hlsl, Generated& g)
{
    const bool ps = stage == Stage::Pixel;
    const uint32_t depth = shd::depthField(job.word);
    const std::string at = "pass " + std::to_string(job.index);
    try {
        sm3::Program prog = sm3::parse(b);
        const std::map<std::string, std::string> none;
        Binding30 B(job.sj.T, job.sj.L, ctab(b), ps ? none : job.format.elements, depth == shd::kDepthGBuffer && ps);
        // depth-only: no colour output in 1549 of 3.0's 1550; 1.0's linear-depth colour painted near-black
        B.depthOnly = ps && depth == shd::kDepthOnly;
        B.skin = job.format.skinned && !ps;
        hlsl = sm3::toHlsl(prog, B);
        if (!B.unresolved.empty()) g.problems.push_back(at + ": unresolved " + B.unresolved[0]);
        if (B.skin) checkSkinInputs(B, job.format, at, g);
    } catch (std::exception& e) {
        g.problems.push_back(at + ": " + e.what());
        return false;
    }
    return true;
}

// The pass's program id10 translated (and compiled) into g.programs -> its generated id and code size
inline ProgramRef addProgram(const PassJob& job, Stage stage, uint32_t id10, Generated& g)
{
    const bool ps = stage == Stage::Pixel;
    const char* kind = ps ? "fp" : "vp";
    const std::string at = "pass " + std::to_string(job.index);
    std::string b;
    if (!id10) return {};
    if (!program10(job.sj.in.read10, id10, kind, b)) {
        g.problems.push_back(at + ": no program");
        return {};
    }
    Program pr;
    pr.id = progId(job.sj.shader, (int)job.index, kind);
    pr.ps = ps;
    if (!translateProgram(job, stage, b, pr.hlsl, g)) return {};
    std::string msg;
    const bool compiled = !job.sj.in.compile || job.sj.in.compile(pr.hlsl, ps ? "ps_4_0" : "vs_4_0", pr.code, msg);
    if (!compiled) g.problems.push_back(at + ": compile: " + msg.substr(0, kCompileMessageChars));
    const ProgramRef ref{pr.id, (uint32_t)pr.code.size()};
    g.programs.push_back(std::move(pr));
    return ref;
}

// The render table's +0x1c byte for (technique, word); a technique's passes take texture masks instead (RE-24)
inline uint16_t passByte(const Tables& T, uint32_t tech, uint32_t w)
{
    auto b = T.passByte.find({tech, w});
    return (uint8_t)(b != T.passByte.end() ? b->second : 0);
}

inline shd::Pass generatePass(const ShaderJob& sj, size_t i, Generated& g)
{
    const Pass& p = sj.s.passes[i];
    auto t = sj.techOf.find((int)i);
    const uint32_t tech = t != sj.techOf.end() ? t->second : 0;
    const uint32_t w = word30(sj.T, tech, p.word);
    const PassFormat f = passFormat(sj.in, p, "pass " + std::to_string(i), g);
    const PassJob job{sj, i, w, f};
    const ProgramRef vs = addProgram(job, Stage::Vertex, p.vs, g);
    const ProgramRef ps = addProgram(job, Stage::Pixel, p.ps, g);
    return {w, f.vformat, vs.id, ps.id, vs.size, ps.size, passByte(sj.T, tech, w)};
}

// The texture registers an HLSL program declares, bit n = tn (t0..t15 fit the pass's u16)
inline uint32_t textureRegisters(const std::string& hlsl)
{
    constexpr const char kDecl[] = "register(t";
    constexpr int kTextureRegisters = 16;
    uint32_t mask = 0;
    for (size_t at = 0; (at = hlsl.find(kDecl, at)) != std::string::npos;) {
        at += sizeof kDecl - 1;
        const int r = atoi(hlsl.c_str() + at);
        if (r >= 0 && r < kTextureRegisters) mask |= 1u << r;
    }
    return mask;
}

// The first pass running program id, or -1
inline int passOf(const std::vector<shd::Pass>& passes, uint32_t id)
{
    for (size_t k = 0; k < passes.size(); ++k)
        if (passes[k].vs == id || passes[k].ps == id) return (int)k;
    return -1;
}

// A technique's first pass binds the union of its passes' textures: 3794 of 3.0's 3802 passes (shdiff.py)
inline void setTextureMasks(const Shd10& s, const std::vector<Program>& programs, std::vector<shd::Pass>& passes)
{
    std::vector<uint32_t> mask(passes.size(), 0);
    for (auto& pr : programs) {
        const int i = passOf(passes, pr.id);
        if (i >= 0) mask[i] |= textureRegisters(pr.hlsl);
    }
    for (auto& t : s.techs) {
        uint32_t all = 0;
        const int end = (std::min)(t.first + t.count, (int)passes.size());
        for (int i = t.first; i < end; ++i) all |= mask[i];
        for (int i = t.first; i < end; ++i) passes[i].textures = (uint16_t)(i == t.first ? all : mask[i]);
    }
}

// The 1.0 shader's 3.0 description: parameters at 3.0 offsets, its defaults, block B re-laid for 8-byte slots
inline shd::Shader shader30(const std::string& raw10, const Shd10& s, const Layout& L, std::vector<shd::Pass> passes)
{
    shd::Shader d;
    d.passes = std::move(passes);
    for (auto& t : s.techs) d.techs.push_back({t.hash, t.first, t.count});
    for (auto& p : s.params) {
        d.params.push_back({p.hash, (uint32_t)p.type | ((uint32_t)(p.A ? p.offset : L.B.at(p.hash)) << 16)});
        d.paramsA += p.A;
    }
    d.slots = s.slotsB;
    memcpy(d.techSlots.data(), raw10.data() + kShd10TechSlots, d.techSlots.size());
    d.sizeA = L.sizeA;
    d.sizeB = L.sizeB;
    d.firstB = L.firstB;
    const size_t a = bytes::alignUp(s.defaultsAt, 16), b = bytes::alignUp(a + s.sizeA, 16);
    d.defaultsA = raw10.substr((std::min)(a, raw10.size()), s.sizeA);
    d.defaultsA.resize(s.sizeA, '\0');
    std::string dB = raw10.substr((std::min)(b, raw10.size()), s.sizeB);
    dB.resize(s.sizeB, '\0');
    d.blockB = blockB30(dB, L);
    // its slots hold texture pointers the engine fills (FUN_0045ce20): zero in all 836 3.0 shaders
    std::fill(d.blockB.begin(), d.blockB.begin() + (std::min)(d.blockB.size(), kSlot30Bytes * L.slots), '\0');
    return d;
}

// gen30.generate: a 1.0 shader -> its 3.0 .shd and the programs' HLSL (compiled when in.compile is set)
inline bool generate(const Tables& T, const Sources& in, const std::string& shader, Generated& g)
{
    std::string raw10;
    if (!in.read10("shaders/shaders/" + shader + ".shd", raw10)) {
        g.problems.push_back("no 1.0 shader " + shader);
        return false;
    }
    Shd10 s;
    if (!parse10(raw10, s)) {
        g.problems.push_back("1.0 shader unreadable");
        return false;
    }
    const Layout L = layout(s);
    ShaderJob sj{T, in, shader, s, L, {}};
    for (auto& t : s.techs)
        for (int i = t.first; i < t.first + t.count; ++i) sj.techOf[i] = t.hash;
    std::vector<shd::Pass> passes;
    for (size_t i = 0; i < s.passes.size(); ++i) passes.push_back(generatePass(sj, i, g));
    setTextureMasks(s, g.programs, passes);
    g.shd = shd::write(shader30(raw10, s, L, std::move(passes)));
    return true;
}

}  // namespace gen30
