// Real 1.0 shaders in 3.0: a 1.0 shader description (.shd) and its D3D9 programs -> a 3.0 .shd and HLSL for D3D11.
// C++ port of DI3Tools/gen30.py (its docstring has every rule and where it was measured); octtest --gen30 checks
// the output against it. The 3.0-side tables (global constant layout, learned pass-word bits, pass +0x1c bytes)
// come from the render table (rentable.py V / W / Q lines).
#pragma once
#include <algorithm>
#include <array>
#include <tuple>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
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

inline uint32_t u32(const std::string& b, size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) memcpy(&v, b.data() + o, 4); return v; }
inline uint16_t u16(const std::string& b, size_t o) { uint16_t v = 0; if (o + 2 <= b.size()) memcpy(&v, b.data() + o, 2); return v; }

struct Param { uint32_t hash; bool A; int offset, type; };
struct Pass { uint32_t word, vf, vs, ps; };
struct Tech { uint32_t hash; int first, count; };
struct Shd10 { std::vector<Pass> passes; std::vector<Tech> techs; std::vector<Param> params; std::vector<uint32_t> slotsB; int sizeA = 0, sizeB = 0; size_t defaultsAt = 0; };

// shd.parse(b, '1.0')
inline bool parse10(const std::string& b, Shd10& s)
{
    if (b.size() < 0xa4) return false;
    int npar = (uint8_t)b[0x0c], npass = (uint8_t)b[0x0d], ntech = (uint8_t)b[0x0e];
    size_t o = 0xa4;
    for (int i = 0; i < npass; ++i, o += 0x30) s.passes.push_back({u32(b, o), u32(b, o + 0x10), u32(b, o + 0x18), u32(b, o + 0x20)});
    std::vector<uint16_t> tr;
    for (int i = 0; i < ntech; ++i) tr.push_back(u16(b, o + 2 * i));
    o = (o + 2 * ntech + 3) & ~(size_t)3;
    std::vector<uint32_t> pn, tn, pw;
    for (int i = 0; i < npar; ++i, o += 4) pn.push_back(u32(b, o));
    for (int i = 0; i < ntech; ++i, o += 4) tn.push_back(u32(b, o));
    for (int i = 0; i < npar; ++i, o += 4) pw.push_back(u32(b, o));
    int nA = (uint8_t)b[0x44], slotsA = (uint8_t)b[0x45], slotsB = (uint8_t)b[0x69];
    s.sizeA = u16(b, 0x46); s.sizeB = u16(b, 0x6a);
    o += 4 * slotsA;
    for (int i = 0; i < slotsB; ++i, o += 4) s.slotsB.push_back(u32(b, o));
    size_t e = b.find('\0', o);
    if (e == std::string::npos) return false;
    s.defaultsAt = e + 1;
    for (int i = 0; i < ntech; ++i) s.techs.push_back({tn[i], tr[i] & 0xff, tr[i] >> 8});
    for (int i = 0; i < npar; ++i) s.params.push_back({pn[i], i < nA, (int)(pw[i] >> 16), (int)(pw[i] & 0xffff)});
    return o <= b.size();
}

// A D3D9 program's constant table: (name, set 'b'/'i'/'f'/'s', register, count).
struct Const { std::string name; char set; int reg, count; };
inline std::vector<Const> ctab(const std::string& b)
{
    std::vector<Const> out;
    size_t i = b.find("CTAB");
    if (i == std::string::npos) return out;
    size_t base = i + 4;
    uint32_t n = u32(b, base + 12), info = u32(b, base + 16);
    for (uint32_t k = 0; k < n; ++k) {
        size_t e = base + info + 20 * k;
        uint32_t nm = u32(b, e);
        uint16_t rs = u16(b, e + 4), reg = u16(b, e + 6), cnt = u16(b, e + 8);
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
            size_t q = t.find('"', i), e = t.find('"', q + 1);
            guid = t.substr(q + 1, e - q - 1);
            i = t.find(']', e) + 1;
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

inline uint32_t convWord(uint32_t w)
{
    return ((w & 7) << 3) | (((w >> 3) & 0xf) << 6) | (((w >> 7) & 3) << 10) | (((w >> 9) & 0xf) << 12) | (((w >> 13) & 7) << 16);
}
inline uint32_t word30(const Tables& T, uint32_t tech, uint32_t w10)
{
    uint32_t w = convWord(w10);
    auto it = T.wordRules.find({tech, (int)((w >> 3) & 7), (int)((w >> 12) & 0xf)});
    return w | (it != T.wordRules.end() ? it->second : 0);
}

struct Layout { std::map<uint32_t, int> A, B; int first10 = 0, first30 = 0, firstB = 0, slots = 0, sizeA = 0, sizeB = 0; };
inline Layout layout(const Shd10& s)
{
    Layout L;
    L.slots = (int)s.slotsB.size();
    int first10 = (L.slots * 5 + 15) & ~15;
    bool any = false;
    for (auto& p : s.params) if (!p.A) { first10 = any ? std::min(first10, p.offset) : p.offset; any = true; }
    L.first10 = first10;
    L.first30 = (L.slots * 9 + 15) & ~15;
    for (auto& p : s.params) (p.A ? L.A : L.B)[p.hash] = p.A ? p.offset : p.offset - first10 + L.first30;
    L.firstB = L.first30 / 16;
    L.sizeA = s.sizeA;
    L.sizeB = L.first30 + std::max(s.sizeB - first10, 0);
    return L;
}

inline std::string blockB30(const std::string& b10, const Layout& L)
{
    std::string out;
    for (int i = 0; i < L.slots; ++i) {
        uint32_t idx = u32(b10, 4 * i), w[2] = {idx, idx == 0xffffffff ? 0xffffffffu : 0u};
        out.append((const char*)w, 8);
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
    "        o.o2 = float4(1, 1, 1, 0.5);\n"
    "    }";

struct Binding30 : sm3::Binding {
    const Tables& T;
    const Layout& L;
    std::map<std::string, std::string> fmt;
    std::map<int, std::pair<std::string, int>> creg;
    std::map<int, int> used;                              // buffer register -> registers declared
    std::vector<std::string> unresolved;
    Binding30(const Tables& t, const Layout& l, const std::vector<Const>& ct, std::map<std::string, std::string> f, bool gb)
        : T(t), L(l), fmt(std::move(f))
    {
        for (auto& c : ct) if (c.set == 'f') for (int k = 0; k < c.count; ++k) creg[c.reg + k] = {c.name, c.reg};
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
            used[0] = std::max({used[0], pr + 1, cr + 1});
            cb = -1;
            expr = "float4(CB0[" + std::to_string(pr) + "].xyz + CB0[" + std::to_string(cr) + "].xyz, CB0[" + std::to_string(pr) + "].w)";
            return true;
        }
        return false;
    }
    std::string constant(int num, const std::string& rel) override
    {
        auto hit = creg.find(num);
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
            for (auto& [r, h] : creg) if (h == hit->second) span = std::max(span, r - reg0 + 1);
            used[cb] = std::max(used[cb], base + span);
            return "CB" + std::to_string(cb) + "[" + rel + " + " + std::to_string(base + num - reg0) + "]";
        }
        used[cb] = std::max(used[cb], base + num - reg0 + 1);
        return "CB" + std::to_string(cb) + "[" + std::to_string(base + num - reg0) + "]";
    }
    std::string boolean(int) override { return "false"; }
    std::string cbuffers() override
    {
        static const char* names[] = {"perFrame", "perView", "perDraw", "perMaterial", "perInstance"};
        std::string s;
        for (auto& [k, n] : used)
            if (n) s += (s.empty() ? "" : "\n") + std::string("cbuffer ") + names[k] + " : register(b" + std::to_string(k) + ") { float4 CB" +
                        std::to_string(k) + "[" + std::to_string(n) + "]; };";
        return s;
    }
    void sampler(int num, int kind, std::string& tex, std::string& smp, std::string& decl) override
    {
        if (num == 2) {
            tex = "tLight"; smp = "sLight";
            decl = "Texture2D tLight : register(t3); SamplerState sLight : register(s3); Texture2D tSpec : register(t4); SamplerState sSpec : register(s4);";
            return;
        }
        int r = num == 0 ? 0 : num == 1 ? 1 : 5 + num - 3;
        const char* Tn = kind == 3 ? "TextureCube" : kind == 4 ? "Texture3D" : "Texture2D";
        tex = "t" + std::to_string(r); smp = "s" + std::to_string(r);
        decl = std::string(Tn) + " " + tex + " : register(t" + std::to_string(r) + "); SamplerState " + smp + " : register(s" + std::to_string(r) + ");";
    }
    std::string sample(int num, int kind, const std::string& method, const std::string& uv, const std::string& extra) override
    {
        if (num == 2) {
            std::string x = extra.empty() ? "" : ", " + extra;
            return "float4(tLight." + method + "(sLight, " + uv + x + ").rgb, 6.0 * dot(tSpec." + method + "(sSpec, " + uv + x +
                   ").rgb, float3(0.2125, 0.7154, 0.0721)))";
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
struct Generated { std::string shd, name; std::vector<Program> programs; std::vector<std::string> problems; };
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

inline std::string buildShd(const Tables& T, const std::string& raw10, const Shd10& s, const Layout& L,
                            const std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>>& passes)
{
    int nA = 0;
    for (auto& p : s.params) nA += p.A;
    int nB = (int)s.params.size() - nA, slots = L.slots;
    std::string h(0xE0, '\0');
    auto put32 = [](std::string& b, size_t o, uint32_t v) { memcpy(&b[o], &v, 4); };
    auto put16 = [](std::string& b, size_t o, uint16_t v) { memcpy(&b[o], &v, 2); };
    h[0x18] = (char)s.params.size(); h[0x19] = (char)passes.size(); h[0x1a] = (char)s.techs.size(); h[0x1b] = (char)slots;
    int flags = 0;
    for (auto& p : passes) {
        uint32_t w = std::get<0>(p);
        if (((w >> 6) & 0xf) > 1) flags |= 0x4;
        if (((w >> 3) & 7) == 0 && ((w >> 12) & 0xf) == 3) flags |= 0x40;
    }
    h[0x48] = (char)flags;
    if (nA) {
        put32(h, 0x4c, (uint32_t)s.params[0].type | ((uint32_t)s.params[0].offset << 16));
        put32(h, 0x50, (uint32_t)s.params[nA - 1].type | ((uint32_t)s.params[nA - 1].offset << 16));
    }
    memcpy(&h[0x54], raw10.data() + 0x30, 16);
    h[0x68] = (char)nA; h[0x69] = 0;
    put16(h, 0x6a, (uint16_t)L.sizeA);
    h[0x90] = (char)std::max(L.sizeA / 16 - 1, 0);
    memset(&h[0x94], 0x0f, 6);
    h[0xa0] = (char)nB; h[0xa1] = (char)slots;
    put16(h, 0xa2, (uint16_t)L.sizeB);
    h[0xc4] = (char)L.firstB;
    h[0xc8] = (char)std::max(L.sizeB / 16 - L.firstB, 0);
    h[0xca] = (char)slots;
    h[0xcc] = (char)(slots ? (5 | (slots << 4)) : 0x0f);
    memset(&h[0xcd], 0x0f, 5);
    h[0xd9] = 1; h[0xda] = (char)0xff;
    std::string out = h;
    for (auto& [w, vf, tech, vs, ps, vsz, psz] : passes) {
        std::string r(0x50, '\0');
        put32(r, 0, w);
        put32(r, 0x18, vf);
        auto b = T.passByte.find({tech, w});
        r[0x1c] = (char)(b != T.passByte.end() ? b->second : 0);
        put32(r, 0x28, vs);
        put32(r, 0x30, ps);
        put32(r, 0x40, vsz);
        put32(r, 0x44, psz);
        out += r;
    }
    for (auto& t : s.techs) { uint16_t v = (uint16_t)(t.first | (t.count << 8)); out.append((const char*)&v, 2); }
    out.resize((out.size() + 3) & ~(size_t)3, '\0');
    for (auto& p : s.params) out.append((const char*)&p.hash, 4);
    for (auto& t : s.techs) out.append((const char*)&t.hash, 4);
    for (auto& p : s.params) {
        uint32_t w = (uint32_t)p.type | ((uint32_t)(p.A ? p.offset : L.B.at(p.hash)) << 16);
        out.append((const char*)&w, 4);
    }
    for (uint32_t x : s.slotsB) out.append((const char*)&x, 4);
    size_t a = (s.defaultsAt + 15) & ~(size_t)15, b = (a + s.sizeA + 15) & ~(size_t)15;
    std::string dA = raw10.substr(std::min(a, raw10.size()), s.sizeA), dB = raw10.substr(std::min(b, raw10.size()), s.sizeB);
    dA.resize(s.sizeA, '\0');
    dB.resize(s.sizeB, '\0');
    out.resize((out.size() + 15) & ~(size_t)15, '\0');
    out += dA;
    out.resize((out.size() + 15) & ~(size_t)15, '\0');
    out += blockB30(dB, L);
    return out;
}

// gen30.generate: compile may be empty (sizes 0, as gen30.py with compile_=False).
inline bool generate(const Tables& T, const Read& read10, const VFormats& vf, const std::string& shader, const Compile& compile, Generated& g)
{
    std::string raw10;
    if (!read10("shaders/shaders/" + shader + ".shd", raw10)) { g.problems.push_back("no 1.0 shader " + shader); return false; }
    Shd10 s;
    if (!parse10(raw10, s)) { g.problems.push_back("1.0 shader unreadable"); return false; }
    Layout L = layout(s);
    std::map<int, uint32_t> techOf;
    for (auto& t : s.techs) for (int i = t.first; i < t.first + t.count; ++i) techOf[i] = t.hash;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>> passes;
    for (size_t i = 0; i < s.passes.size(); ++i) {
        const Pass& p = s.passes[i];
        uint32_t tech = techOf.count((int)i) ? techOf[(int)i] : 0;
        uint32_t w = word30(T, tech, p.word);
        bool gb = ((w >> 3) & 7) == 2;
        std::map<std::string, std::string> fmt;
        if (auto f = vf.find(p.vf); f != vf.end()) fmt = f->second;
        else g.problems.push_back("pass " + std::to_string(i) + ": vformat unknown");
        uint32_t ids[2] = {0, 0}, sizes[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            uint32_t pid = k ? p.ps : p.vs;
            const char* kind = k ? "fp" : "vp";
            if (!pid) continue;
            std::string b;
            if (!program10(read10, pid, kind, b)) { g.problems.push_back("pass " + std::to_string(i) + ": no program"); continue; }
            Program pr;
            pr.id = progId(shader, (int)i, kind);
            pr.ps = k == 1;
            try {
                sm3::Program prog = sm3::parse(b);
                Binding30 B(T, L, ctab(b), k ? std::map<std::string, std::string>() : fmt, gb && k);
                pr.hlsl = sm3::toHlsl(prog, B);
                if (!B.unresolved.empty()) g.problems.push_back("pass " + std::to_string(i) + ": unresolved " + B.unresolved[0]);
            } catch (std::exception& e) {
                g.problems.push_back("pass " + std::to_string(i) + ": " + e.what());
                continue;
            }
            if (compile) {
                std::string msg;
                if (!compile(pr.hlsl, k ? "ps_4_0" : "vs_4_0", pr.code, msg)) g.problems.push_back("pass " + std::to_string(i) + ": compile: " + msg.substr(0, 200));
            }
            ids[k] = pr.id;
            sizes[k] = (uint32_t)pr.code.size();
            g.programs.push_back(std::move(pr));
        }
        passes.emplace_back(w, p.vf, tech, ids[0], ids[1], sizes[0], sizes[1]);
    }
    g.name = "x" + shader;
    g.shd = buildShd(T, raw10, s, L, passes);
    return true;
}

}  // namespace gen30
