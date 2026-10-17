// Particle curve channels reach the VS as TEXCOORD5/6: their roles by a taint pass, mapped between shaders.
// Any thread.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace chanroles {

// A role by an output-semantic prefix, rules tried in order
struct RoleRule { const char* prefix; char role; };
struct RoleRules { const RoleRule* rules; size_t count; };

// 3.0: TEXCOORD0.w alpha (PS 7ec19b90 multiplies the diffuse sample by it), COLOR0 emissive, TEXCOORD0.xyz colour
inline constexpr RoleRule kRules30Table[] = {{"TEXCOORD0.w", 'A'}, {"COLOR0.", 'E'}, {"TEXCOORD0.", 'C'}};
inline constexpr RoleRules kRules30 = {kRules30Table, std::size(kRules30Table)};

struct Opnd { std::string reg, sw; bool cst = true; };

namespace detail {

inline bool operandMark(char c) { return c == '-' || c == '|' || c == ' '; }
inline bool nameChar(char c) { return isalpha((unsigned char)c) || c == '_'; }

}  // namespace detail

// '-|r1.xyzw|', 'cb1[4].x', 'v7.yyyz', 'l(1.0, ...)' -> register, swizzle / mask; cst: carries no channel
inline Opnd opnd(const std::string& s)
{
    Opnd o;
    size_t i = 0;
    while (i < s.size() && detail::operandMark(s[i])) ++i;
    size_t a = i;
    while (i < s.size() && detail::nameChar(s[i])) ++i;
    std::string letters = s.substr(a, i - a);
    while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
    o.reg = s.substr(a, i - a);
    while (i < s.size() && s[i] == '[') {
        int d = 0;
        do {
            d += s[i] == '[' ? 1 : s[i] == ']' ? -1 : 0;
            ++i;
        } while (i < s.size() && d);
    }
    while (i < s.size() && s[i] == '|') ++i;
    if (i < s.size() && s[i] == '.') {
        ++i;
        size_t e = i;
        while (e < s.size() && strchr("xyzw", s[e])) ++e;
        o.sw = s.substr(i, e - i);
    }
    if (o.sw.empty()) o.sw = "xyzw";
    static constexpr const char* kConstants[] = {"l", "c", "cb", "icb", "i", "b", "s", "t", "vPos", "vFace", "null",
                                                 "aL", "p"};
    o.cst = letters.empty();
    for (const char* c : kConstants) o.cst |= letters == c;
    return o;
}

// An instruction's operands, split at the commas outside brackets, trimmed
inline std::vector<std::string> operands(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    int d = 0;
    for (char c : s) {
        if (c == '(' || c == '[') ++d;
        if (c == ')' || c == ']') --d;
        if (c != ',' || d) {
            cur += c;
            continue;
        }
        out.push_back(cur);
        cur.clear();
    }
    if (cur.find_first_not_of(' ') != std::string::npos) out.push_back(cur);
    for (auto& x : out) {
        x.erase(0, x.find_first_not_of(' '));
        x.erase(x.find_last_not_of(' ') + 1);
    }
    return out;
}

// The disassembly syntax: D3D9 (dcl_ lines) or DXBC (signature comments)
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

// A channel reaching a position output is size; else the first rule whose prefix it reaches; else 'M'
inline char classify(const std::vector<std::string>& hits, const RoleRules& rules)
{
    const auto has = [&](const char* pre) {
        const auto starts = [&](const std::string& h) { return !h.compare(0, strlen(pre), pre); };
        return std::any_of(hits.begin(), hits.end(), starts);
    };
    if (has("POSITION0.") || has("SV_POSITION0.")) return 'S';
    for (size_t i = 0; i < rules.count; ++i)
        if (has(rules.rules[i].prefix)) return rules.rules[i].role;
    return 'M';
}

}  // namespace detail

// Each channel's role (Alpha, Emissive, Colour, Size, M other, 0 unused) to the last one read; empty: no input
inline std::vector<char> roles(const std::string& text, Disasm syntax, const RoleRules& rules)
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
        r[c] = detail::classify(hits[c], rules);
        n = c + 1;
    }
    r.resize(n);
    return r;
}

namespace detail {

// The index of the rank-th channel with role among from's first n, or -1
inline int nthWithRole(const std::vector<char>& from, int n, char role, int rank)
{
    int seen = 0;
    for (int i = 0; i < n && i < (int)from.size(); ++i) {
        if (from[i] != role) continue;
        if (seen++ == rank) return i;
    }
    return -1;
}

}  // namespace detail

// Each channel of to -> from's channel with its role and rank, else -1; from's channels past nFrom unused
inline std::vector<int> mapChannels(const std::vector<char>& from, int nFrom, const std::vector<char>& to)
{
    std::vector<int> m;
    std::map<char, int> rank;
    for (char role : to) m.push_back(role ? detail::nthWithRole(from, nFrom, role, rank[role]++) : -1);
    return m;
}

}  // namespace chanroles
