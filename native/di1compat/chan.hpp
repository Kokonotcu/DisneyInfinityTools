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

// role per channel ('A' alpha, 'E' emissive, 'C' colour, 'S' size, 'M' other, 0 unused), up to the last channel
// the shader reads; empty if the program has no TEXCOORD5/6 input or does not disassemble
inline std::vector<char> roles(const std::string& text, bool sm4)
{
    using T4 = std::array<uint8_t, 4>;   // per component: bit c = channel c
    std::map<std::string, T4> T;
    std::map<std::string, std::string> outSem;   // output register -> 'COLOR0', 'SV_POSITION0', ...
    const char* C = "xyzw";
    size_t pos = 0;
    bool anyInput = false, inSig = false, outSig = false;
    std::vector<std::string> lines;
    for (size_t e; pos < text.size(); pos = e + 1) {
        e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        lines.push_back(text.substr(pos, e - pos));
    }
    auto upper = [](std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; };
    for (auto& l : lines) {   // declarations / signatures
        if (sm4) {
            if (l.find("// Input signature") == 0) { inSig = true; outSig = false; continue; }
            if (l.find("// Output signature") == 0) { outSig = true; inSig = false; continue; }
            if ((inSig || outSig) && l.size() > 3 && l[0] == '/' && l[1] == '/') {
                char name[64], mask[8];
                int idx, reg;
                if (sscanf_s(l.c_str(), "// %63s %d %7s %d", name, (unsigned)sizeof(name), &idx, mask, (unsigned)sizeof(mask), &reg) == 4) {
                    std::string sem = upper(name) + std::to_string(idx);
                    if (inSig && (sem == "TEXCOORD5" || sem == "TEXCOORD6")) {
                        int b = sem == "TEXCOORD5" ? 0 : 4;
                        T["v" + std::to_string(reg)] = {(uint8_t)(1 << b), (uint8_t)(1 << (b + 1)), (uint8_t)(1 << (b + 2)), (uint8_t)(1 << (b + 3))};
                        anyInput = true;
                    }
                    if (outSig) outSem["o" + std::to_string(reg)] = sem;
                }
            } else if (l.find("//") != 0) inSig = outSig = false;
        } else {
            size_t d = l.find("dcl_");
            if (d == std::string::npos || l.find("//") != std::string::npos) continue;
            size_t sp = l.find(' ', d);
            if (sp == std::string::npos) continue;
            std::string kind = l.substr(d + 4, sp - d - 4), reg = l.substr(sp + 1);
            reg = reg.substr(0, reg.find_first_of(". \r"));
            size_t k = kind.find_first_of("0123456789");
            std::string sem = upper(kind.substr(0, k)) + (k == std::string::npos ? "0" : kind.substr(k));
            if (reg.size() > 1 && reg[0] == 'v' && (sem == "TEXCOORD5" || sem == "TEXCOORD6")) {
                int b = sem == "TEXCOORD5" ? 0 : 4;
                T[reg] = {(uint8_t)(1 << b), (uint8_t)(1 << (b + 1)), (uint8_t)(1 << (b + 2)), (uint8_t)(1 << (b + 3))};
                anyInput = true;
            }
            if (reg.size() > 1 && reg[0] == 'o') outSem[reg] = sem;
        }
    }
    if (!anyInput) return {};
    static const char* skip[] = {"ret", "if", "else", "endif", "loop", "endloop", "rep", "endrep", "break", "breakc",
                                 "discard", "texkill", "label", "call", "callnz", "vs", "ps", "customdata", "def", "defi", "defb"};
    for (auto& raw : lines) {
        std::string l = raw;
        l.erase(0, l.find_first_not_of(" \t"));
        if (l.empty() || l[0] == '/' || isdigit((unsigned char)l[0]) || !l.compare(0, 3, "dcl")) continue;
        size_t sp = l.find(' ');
        if (sp == std::string::npos) continue;
        std::string op = l.substr(0, sp), base = op.substr(0, op.find('_'));
        bool skipped = false;
        for (const char* s : skip) skipped |= base == s;
        if (skipped) continue;
        auto ops = operands(l.substr(sp + 1));
        if (ops.empty()) continue;
        std::vector<std::string> dsts{ops[0]}, srcs(ops.begin() + 1, ops.end());
        if (sm4 && base == "sincos" && ops.size() == 3) {   // sincos sin_dst, cos_dst, src
            dsts.clear();
            for (int k = 0; k < 2; ++k) if (ops[k] != "null") dsts.push_back(ops[k]);
            srcs = {ops[2]};
        }
        if (base == "sample" || base == "texld" || base == "texldl" || base == "texldd" || base == "texldb" || base == "ld" ||
            base == "resinfo" || base == "gather4")
            srcs.resize((std::min<size_t>)(srcs.size(), 1));   // coordinates only
        int dot = base == "dp2" || base == "dp2add" ? 2 : base == "dp3" ? 3 : base == "dp4" ? 4 : 0;
        bool all = base == "m4x4" || base == "m4x3" || base == "m3x4" || base == "m3x3" || base == "m3x2" || base == "crs" ||
                   base == "nrm" || (!sm4 && base == "sincos");
        std::vector<Opnd> so;
        for (auto& s : srcs) so.push_back(opnd(s));
        auto comp = [&](const Opnd& o, size_t k) -> uint8_t {
            if (o.cst) return 0;
            auto it = T.find(o.reg);
            if (it == T.end()) return 0;
            char c = k < o.sw.size() ? o.sw[k] : o.sw.back();
            const char* p = strchr(C, c);
            return p ? it->second[p - C] : 0;
        };
        std::vector<std::pair<std::string, std::array<int, 4>>> writes;
        for (auto& d : dsts) {
            Opnd dd = opnd(d);
            if (dd.reg.empty()) continue;
            std::array<int, 4> nw{-1, -1, -1, -1};
            for (int k = 0; k < 4; ++k) {
                if (dd.sw.find(C[k]) == std::string::npos) continue;
                uint8_t acc = 0;
                for (auto& s : so) {
                    if (dot) for (int j = 0; j < dot; ++j) acc |= comp(s, j);
                    else if (all) for (int j = 0; j < 4; ++j) acc |= comp(s, j);
                    else acc |= comp(s, k);
                }
                nw[k] = acc;
            }
            writes.push_back({dd.reg, nw});
        }
        for (auto& [reg, nw] : writes) {
            T4& cur = T[reg];
            bool out = reg[0] == 'o';
            for (int k = 0; k < 4; ++k)
                if (nw[k] >= 0) cur[k] = out ? (uint8_t)(cur[k] | nw[k]) : (uint8_t)nw[k];
        }
    }
    std::vector<std::vector<std::string>> hits(8);
    for (auto& [reg, t] : T) {
        if (reg[0] != 'o') continue;
        auto s = outSem.find(reg);
        std::string sem = s != outSem.end() ? s->second : reg;
        for (int k = 0; k < 4; ++k)
            for (int c = 0; c < 8; ++c)
                if (t[k] >> c & 1) hits[c].push_back(sem + "." + C[k]);
    }
    std::vector<char> r(8, 0);
    int n = 0;
    for (int c = 0; c < 8; ++c) {
        if (hits[c].empty()) continue;
        auto has = [&](const std::string& pre) { for (auto& h : hits[c]) if (!h.compare(0, pre.size(), pre)) return true; return false; };
        if (has("POSITION0.") || has("SV_POSITION0.")) r[c] = 'S';
        else if (sm4) r[c] = has("TEXCOORD0.w") ? 'A' : has("COLOR0.") ? 'E' : has("TEXCOORD0.") ? 'C' : 'M';
        else r[c] = has("COLOR0.w") ? 'A' : has("COLOR0.") ? 'C' : has("COLOR1.") ? 'E' : 'M';
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
        if ((!is30 && read(pre + a, prog)) || read(pre + b, prog)) r = roles(disasm(prog), is30);
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
