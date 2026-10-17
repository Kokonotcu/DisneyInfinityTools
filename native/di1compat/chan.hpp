// chan.hpp: what a particle emitter's curve channels mean, and moving them onto another shader (cfg vfxchan10).
//
// A particle's curve channels (emitter record: 1.0 count +0x105, key indices +0x108; 3.0 +0x121 / +0x124; one 8 B
// key per channel, evaluated over the particle's life: 3.0 FUN_00662d20) reach the material's vertex shader as
// TEXCOORD5.xyzw (channels 0-3) and TEXCOORD6.xyzw (4-7): the particle stream of both games' particle vformat
// (1.0 07fc8bc6 stream 2, 3.0 a2bb30bd). The channel count must equal the emitter type's (3.0 FUN_00673870 skips
// the spawn otherwise), so nothing else names a channel: its meaning is whatever that shader does with the
// component. 1.0 particle VS e2efadb6 / 3.0 425cbc66 (shader 35bf9850 in both): .x alpha, .y emissive, .z size;
// 1.0 cdb3140f (ed5fa7f7): .x alpha, .y size; 1.0 bcc5b417 (4e9b393b): alpha, colour lerp, emissive, other, size.
// Disney re-laid the channels whenever a material moved to another shader (twin emitters, curvetwins2.py:
// 4e9b393b -> 35bf9850 keeps 1.0 channels 0, 2, 4; ed5fa7f7 -> ba0c9df8 keeps 0 and 1 as 0 and 2 and puts a
// constant key in 1). The transcode used to copy the 1.0 channels as they were, so on a changed shader a size
// curve drove emission and a colour or emissive curve drove size.
//
// Roles, from a taint pass over the VS disassembly: a channel that reaches the position output is size; 1.0
// COLOR0.w alpha, COLOR0.xyz colour, COLOR1 emissive; 3.0 TEXCOORD0.w alpha (the colour interpolant: 3.0 PS
// 7ec19b90 multiplies the diffuse sample by it), COLOR0 emissive (the PS's emissive scale), TEXCOORD0.xyz colour;
// anything else 'M'. map() gives each 3.0 channel the 1.0 channel of the same role and rank, or -1 (a constant
// 1.0 key: ff 1f 1f ff 55 aa 00 ff, what Disney put in 313 of 341 new emissive channels). Checked against Disney's
// port of the twin emitters whose material shader changed (chanscore.py, 1,174 emitters): 3.0 channel count
// 1,174/1,174, keys per channel 2,958/3,167, whole emitters 1,032 (the 1.0 slots as they were: 371).
#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "formats/shd.hpp"
#include "gen30.hpp"

namespace chan {

// D3DDisassemble of a D3D9 (raw tokens) or DXBC program, "" if it fails
inline std::string disasm(const std::string& prog)
{
    struct Blob : IUnknown {
        virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
        virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
    };
    typedef HRESULT(WINAPI * Fn)(LPCVOID, SIZE_T, UINT, LPCSTR, Blob**);
    static Fn fn = [] {
        HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
        return m ? (Fn)GetProcAddress(m, "D3DDisassemble") : (Fn) nullptr;
    }();
    size_t o = prog.find("DXBC");
    std::string b = o != std::string::npos && o < 64 ? prog.substr(o) : prog;
    Blob* out = nullptr;
    if (!fn || b.empty() || FAILED(fn(b.data(), b.size(), 0, nullptr, &out)) || !out) return "";
    std::string t((const char*)out->GetBufferPointer(), out->GetBufferSize());
    out->Release();
    t.erase(std::remove(t.begin(), t.end(), '\0'), t.end());
    return t;
}

struct Opnd { std::string reg, sw; bool cst = true; };

// '-|r1.xyzw|', 'cb1[4].x', 'v7.yyyz', 'l(1.0, ...)' -> register, swizzle / mask; cst: carries no channel
inline Opnd opnd(const std::string& s)
{
    Opnd o;
    size_t i = 0;
    while (i < s.size() && (s[i] == '-' || s[i] == '|' || s[i] == ' ')) ++i;
    size_t a = i;
    while (i < s.size() && (isalpha((unsigned char)s[i]) || s[i] == '_')) ++i;
    std::string letters = s.substr(a, i - a);
    while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
    o.reg = s.substr(a, i - a);
    while (i < s.size() && s[i] == '[') {
        int d = 0;
        do { d += s[i] == '[' ? 1 : s[i] == ']' ? -1 : 0; ++i; } while (i < s.size() && d);
    }
    while (i < s.size() && s[i] == '|') ++i;
    if (i < s.size() && s[i] == '.') {
        size_t e = ++i;
        while (e < s.size() && strchr("xyzw", s[e])) ++e;
        o.sw = s.substr(i, e - i);
    }
    if (o.sw.empty()) o.sw = "xyzw";
    static const char* consts[] = {"l", "c", "cb", "icb", "i", "b", "s", "t", "vPos", "vFace", "null", "aL", "p"};
    o.cst = letters.empty();
    for (const char* c : consts) o.cst |= letters == c;
    return o;
}

inline std::vector<std::string> operands(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    int d = 0;
    for (char c : s) {
        if (c == '(' || c == '[') ++d;
        if (c == ')' || c == ']') --d;
        if (c == ',' && !d) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (cur.find_first_not_of(' ') != std::string::npos) out.push_back(cur);
    for (auto& x : out) { x.erase(0, x.find_first_not_of(' ')); x.erase(x.find_last_not_of(' ') + 1); }
    return out;
}

// The disassembly syntax: D3D9 (dcl_ lines, 1.0's programs) or DXBC (signature comments, 3.0's)
enum class Disasm { Sm3, Sm4 };

namespace detail {

constexpr char kComponents[] = "xyzw";
constexpr int kChannels = 8;                   // TEXCOORD5.xyzw, TEXCOORD6.xyzw
using Taint4 = std::array<uint8_t, 4>;         // per component: bit c = curve channel c

// Which curve channels reach each register component, and the output registers' semantics
struct Taint {
    std::map<std::string, Taint4> regs;
    std::map<std::string, std::string> outSem;   // output register -> 'COLOR0', 'SV_POSITION0', ...
    bool anyInput = false;                       // a TEXCOORD5/6 input was declared
};

inline std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    for (size_t pos = 0, e; pos < text.size(); pos = e + 1) {
        e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        lines.push_back(text.substr(pos, e - pos));
    }
    return lines;
}

inline std::string upper(std::string s)
{
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

// The particle stream input: TEXCOORD5 carries channels 0-3, TEXCOORD6 channels 4-7
inline bool channelInput(Taint& t, const std::string& sem, const std::string& reg)
{
    if (sem != "TEXCOORD5" && sem != "TEXCOORD6") return false;
    const int b = sem == "TEXCOORD5" ? 0 : 4;
    t.regs[reg] = {(uint8_t)(1 << b), (uint8_t)(1 << (b + 1)), (uint8_t)(1 << (b + 2)), (uint8_t)(1 << (b + 3))};
    t.anyInput = true;
    return true;
}

// DXBC: '// NAME index mask register ...' rows under the input and output signature headings
inline void sm4Signatures(const std::vector<std::string>& lines, Taint& t)
{
    bool inSig = false, outSig = false;
    for (auto& l : lines) {
        if (l.find("// Input signature") == 0) {
            inSig = true;
            outSig = false;
            continue;
        }
        if (l.find("// Output signature") == 0) {
            outSig = true;
            inSig = false;
            continue;
        }
        const bool table = inSig || outSig;
        const bool comment = l.size() > 3 && l[0] == '/' && l[1] == '/';
        if (!table || !comment) {
            if (l.find("//") != 0) inSig = outSig = false;
            continue;
        }
        char name[64], mask[8];
        int idx, reg;
        const int got = sscanf_s(l.c_str(), "// %63s %d %7s %d", name, (unsigned)sizeof(name), &idx, mask,
                                 (unsigned)sizeof(mask), &reg);
        if (got != 4) continue;
        const std::string sem = upper(name) + std::to_string(idx);
        if (inSig) channelInput(t, sem, "v" + std::to_string(reg));
        if (outSig) t.outSem["o" + std::to_string(reg)] = sem;
    }
}

// D3D9: 'dcl_texcoord5 v3' and 'dcl_color o1' lines
inline void sm3Declarations(const std::vector<std::string>& lines, Taint& t)
{
    for (auto& l : lines) {
        constexpr char kDcl[] = "dcl_";
        size_t d = l.find(kDcl);
        if (d == std::string::npos || l.find("//") != std::string::npos) continue;
        size_t sp = l.find(' ', d);
        if (sp == std::string::npos) continue;
        d += sizeof kDcl - 1;
        std::string kind = l.substr(d, sp - d), reg = l.substr(sp + 1);
        reg = reg.substr(0, reg.find_first_of(". \r"));
        size_t k = kind.find_first_of("0123456789");
        const std::string sem = upper(kind.substr(0, k)) + (k == std::string::npos ? "0" : kind.substr(k));
        if (reg.size() < 2) continue;
        if (reg[0] == 'v') channelInput(t, sem, reg);
        if (reg[0] == 'o') t.outSem[reg] = sem;
    }
}

inline bool listed(const std::string& s, const char* const* list, size_t n)
{
    return std::find_if(list, list + n, [&](const char* x) { return s == x; }) != list + n;
}

// Instructions that move no channel: flow control, declarations, constants
constexpr const char* kSkipped[] = {"ret", "if", "else", "endif", "loop", "endloop", "rep", "endrep", "break", "breakc",
                                    "discard", "texkill", "label", "call", "callnz", "vs", "ps", "customdata", "def",
                                    "defi", "defb"};
// Sampling instructions: only their coordinates carry channels
constexpr const char* kSampling[] = {"sample", "texld", "texldl", "texldd", "texldb", "ld", "resinfo", "gather4"};
// Matrix and cross-component instructions: every source component reaches every written one
constexpr const char* kAllComponents[] = {"m4x4", "m4x3", "m3x4", "m3x3", "m3x2", "crs", "nrm"};

// One instruction's destinations and sources; SM4 sincos writes two destinations
struct Instruction { std::string base; std::vector<std::string> dsts, srcs; };

// The instruction on line l, or base empty for a line that moves no channel
inline Instruction instruction(const std::string& raw, Disasm syntax)
{
    Instruction in;
    std::string l = raw;
    l.erase(0, l.find_first_not_of(" \t"));
    if (l.empty() || l[0] == '/') return in;
    if (isdigit((unsigned char)l[0]) || !l.compare(0, 3, "dcl")) return in;
    size_t sp = l.find(' ');
    if (sp == std::string::npos) return in;
    std::string op = l.substr(0, sp), base = op.substr(0, op.find('_'));
    if (listed(base, kSkipped, std::size(kSkipped))) return in;
    auto ops = operands(l.substr(sp + 1));
    if (ops.empty()) return in;
    in.base = base;
    in.dsts = {ops[0]};
    in.srcs.assign(ops.begin() + 1, ops.end());
    if (syntax == Disasm::Sm4 && base == "sincos" && ops.size() == 3) {   // sincos sin_dst, cos_dst, src
        in.dsts.clear();
        for (int k = 0; k < 2; ++k)
            if (ops[k] != "null") in.dsts.push_back(ops[k]);
        in.srcs = {ops[2]};
    }
    if (listed(base, kSampling, std::size(kSampling))) in.srcs.resize((std::min<size_t>)(in.srcs.size(), 1));
    return in;
}

// The channels in component k of source o
inline uint8_t sourceTaint(const Taint& t, const Opnd& o, size_t k)
{
    if (o.cst) return 0;
    auto it = t.regs.find(o.reg);
    if (it == t.regs.end()) return 0;
    char c = k < o.sw.size() ? o.sw[k] : o.sw.back();
    const char* p = strchr(kComponents, c);
    return p ? it->second[p - kComponents] : 0;
}

// The channels written component k of a destination reads: dpN its first N, cross-component ones all four
inline uint8_t writtenTaint(const Taint& t, const std::vector<Opnd>& so, int reads, size_t k)
{
    uint8_t acc = 0;
    for (auto& s : so) {
        if (!reads) acc |= sourceTaint(t, s, k);
        for (int j = 0; j < reads; ++j) acc |= sourceTaint(t, s, j);
    }
    return acc;
}

// Taint flows from sources to destinations; an output register accumulates, a temp is overwritten
inline void propagate(const Instruction& in, Disasm syntax, Taint& t)
{
    std::vector<Opnd> so;
    for (auto& s : in.srcs) so.push_back(opnd(s));
    const int dot = in.base == "dp2" || in.base == "dp2add" ? 2 : in.base == "dp3" ? 3 : in.base == "dp4" ? 4 : 0;
    const bool sm3SinCos = syntax == Disasm::Sm3 && in.base == "sincos";
    const bool all = listed(in.base, kAllComponents, std::size(kAllComponents)) || sm3SinCos;
    std::vector<std::pair<std::string, std::array<int, 4>>> writes;
    for (auto& d : in.dsts) {
        Opnd dd = opnd(d);
        if (dd.reg.empty()) continue;
        std::array<int, 4> nw{-1, -1, -1, -1};
        for (size_t k = 0; k < 4; ++k)
            if (dd.sw.find(kComponents[k]) != std::string::npos) nw[k] = writtenTaint(t, so, all ? 4 : dot, k);
        writes.push_back({dd.reg, nw});
    }
    for (auto& [reg, nw] : writes) {
        Taint4& cur = t.regs[reg];
        const bool out = reg[0] == 'o';
        for (int k = 0; k < 4; ++k)
            if (nw[k] >= 0) cur[k] = out ? (uint8_t)(cur[k] | nw[k]) : (uint8_t)nw[k];
    }
}

// A role by the first output-semantic prefix a channel reaches (after size: a position output)
struct RoleRule { const char* prefix; char role; };
// 3.0: TEXCOORD0.w alpha (PS 7ec19b90 multiplies the diffuse sample by it), COLOR0 emissive, TEXCOORD0.xyz colour
constexpr RoleRule kRules30[] = {{"TEXCOORD0.w", 'A'}, {"COLOR0.", 'E'}, {"TEXCOORD0.", 'C'}};
// 1.0: COLOR0.w alpha, COLOR0.xyz colour, COLOR1 emissive
constexpr RoleRule kRules10[] = {{"COLOR0.w", 'A'}, {"COLOR0.", 'C'}, {"COLOR1.", 'E'}};

// Per channel, the output components (semantic.component) it reaches
inline void addHits(const std::string& sem, const Taint4& taint, std::vector<std::vector<std::string>>& hits)
{
    for (int k = 0; k < 4; ++k)
        for (int c = 0; c < kChannels; ++c)
            if (taint[k] >> c & 1) hits[c].push_back(sem + "." + kComponents[k]);
}

inline std::vector<std::vector<std::string>> channelHits(const Taint& t)
{
    std::vector<std::vector<std::string>> hits(kChannels);
    for (auto& [reg, taint] : t.regs) {
        if (reg[0] != 'o') continue;
        auto s = t.outSem.find(reg);
        addHits(s != t.outSem.end() ? s->second : reg, taint, hits);
    }
    return hits;
}

inline char classify(const std::vector<std::string>& hits, Disasm syntax)
{
    const auto has = [&](const char* pre) {
        const auto starts = [&](const std::string& h) { return !h.compare(0, strlen(pre), pre); };
        return std::any_of(hits.begin(), hits.end(), starts);
    };
    if (has("POSITION0.") || has("SV_POSITION0.")) return 'S';
    const bool sm4 = syntax == Disasm::Sm4;
    for (const RoleRule& rule : sm4 ? kRules30 : kRules10)
        if (has(rule.prefix)) return rule.role;
    return 'M';
}

}  // namespace detail

// Each channel's role (Alpha, Emissive, Colour, Size, M other, 0 unused) to the last one read; empty: no input
inline std::vector<char> roles(const std::string& text, Disasm syntax)
{
    const std::vector<std::string> lines = detail::splitLines(text);
    detail::Taint t;
    if (syntax == Disasm::Sm4) detail::sm4Signatures(lines, t);
    else detail::sm3Declarations(lines, t);
    if (!t.anyInput) return {};
    for (auto& l : lines) {
        const detail::Instruction in = detail::instruction(l, syntax);
        if (!in.base.empty()) detail::propagate(in, syntax, t);
    }
    const std::vector<std::vector<std::string>> hits = detail::channelHits(t);
    std::vector<char> r(detail::kChannels, 0);
    int n = 0;
    for (int c = 0; c < detail::kChannels; ++c) {
        if (hits[c].empty()) continue;
        r[c] = detail::classify(hits[c], syntax);
        n = c + 1;
    }
    r.resize(n);
    return r;
}

// each 3.0 channel's 1.0 channel (same role, same rank among that role's channels), -1 = none; n10 = the 1.0
// record's channel count (1.0 channels past the shader's are unused)
inline std::vector<int> map(const std::vector<char>& r10, int n10, const std::vector<char>& r30)
{
    std::vector<int> m;
    std::map<char, int> rank30;
    for (char rb : r30) {
        int want = rb ? rank30[rb]++ : -1, src = -1;
        if (rb) {
            int seen = 0;
            for (int i = 0; i < n10; ++i)
                if (i < (int)r10.size() && r10[i] == rb && seen++ == want) { src = i; break; }
        }
        m.push_back(src);
    }
    return m;
}

using Read = std::function<bool(const std::string& path, std::string& out)>;

// Roles of a shader's first-pass VS, cached per game and shader. Programs: 1.0 '%x.vp', 3.0 '3:...%08x.vp'
inline std::vector<char> shaderRoles(const Read& read, const std::string& guid, bool is30)
{
    static std::mutex mx;
    static std::map<std::string, std::vector<char>> cache;
    std::string key = (is30 ? "3:" : "1:") + guid;
    {
        std::lock_guard<std::mutex> l(mx);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    std::vector<char> r;
    std::string shd, prog;
    std::string pre = is30 ? "3:shaders/shaders/" : "shaders/shaders/";
    const shd::Layout& L = is30 ? shd::kLayout30 : gen30::kShd10;
    const size_t at = L.data + L.passVs, cnt = L.counts + 1;
    if (read(pre + guid + ".shd", shd) && shd.size() >= at + 4 && (uint8_t)shd[cnt] > 0) {
        uint32_t vs;
        memcpy(&vs, shd.data() + at, 4);
        char a[24], b[24];
        sprintf_s(a, "%x.vp", vs);
        sprintf_s(b, "%08x.vp", vs);
        const Disasm syntax = is30 ? Disasm::Sm4 : Disasm::Sm3;
        const bool unpadded = !is30 && read(pre + a, prog);   // 1.0's '%x' name first
        if (unpadded || read(pre + b, prog)) r = roles(disasm(prog), syntax);
    }
    std::lock_guard<std::mutex> l(mx);
    return cache[key] = r;
}

// The channel layout for an emitter drawn with a 1.0 material on shader s10 that is served on shader s30: false
// (keep the 1.0 channels) when either program has no channel input, the material went onto its translated 1.0
// shader ('x' + GUID), or the layout comes out the same.
inline bool layout(const Read& read10, const Read& read30, const std::string& s10, const std::string& s30, int n10,
                   std::vector<int>& chans)
{
    if (s10.empty() || s30.empty() || s30[0] == 'x') return false;
    std::vector<char> r10 = shaderRoles(read10, s10, false), r30 = shaderRoles(read30, s30, true);
    if (r10.empty() || r30.empty()) return false;
    chans = map(r10, n10, r30);
    bool same = (int)chans.size() == n10;
    for (int i = 0; same && i < n10; ++i) same = chans[i] == i;
    return !same;
}

}  // namespace chan
