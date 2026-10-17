// D3D9 shader model 3 programs read (tokens, constant table) and translated to HLSL for D3D11 (sm3.py).
// Base only; any thread.
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "../base/bytes.hpp"

namespace sm3 {

// D3D9 register types (d3d9types.h D3DSHADER_PARAM_REGISTER_TYPE)
enum class Reg : int {
    Temp = 0, Input = 1, Const = 2, AddrTex = 3, Output = 6, ColorOut = 8, DepthOut = 9, Sampler = 10, ConstBool = 14,
    MiscType = 17
};

// D3D9 opcodes (d3d9types.h D3DSHADER_INSTRUCTION_OPCODE_TYPE)
enum class Opcode : int {
    Nop = 0, Mov = 1, Add = 2, Sub = 3, Mad = 4, Mul = 5, Rcp = 6, Rsq = 7, Dp3 = 8, Dp4 = 9, Min = 10, Max = 11,
    Slt = 12, Sge = 13, Exp = 14, Log = 15, Frc = 19, Call = 25, CallNz = 26, Loop = 27, Ret = 28, EndLoop = 29,
    Label = 30, Dcl = 31, Pow = 32, Abs = 35, Nrm = 36, SinCos = 37, Rep = 38, EndRep = 39, If = 40, Ifc = 41,
    Else = 42, EndIf = 43, Break = 44, BreakC = 45, Mova = 46, DefB = 47, DefI = 48, TexKill = 65, Tex = 66, Def = 81,
    Cmp = 88, Dp2Add = 90, TexLdl = 95, BreakP = 96, Comment = 0xfffe, End = 0xffff
};

// dcl usages (D3DDECLUSAGE), in order
inline const char* usage(int u)
{
    static constexpr const char* kUsages[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD",
                                              "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH",
                                              "SAMPLE"};
    constexpr int kCount = (int)(sizeof(kUsages) / sizeof(kUsages[0]));
    return u >= 0 && u < kCount ? kUsages[u] : "UNKNOWN";
}

constexpr uint32_t kShaderModel3 = 0x0300;   // version token, low word
constexpr uint32_t kPixelShader = 0xffff;    // version token, high word (vertex: 0xfffe)
constexpr int kSampler2D = 2, kSamplerCube = 3, kSamplerVolume = 4;   // dcl sampler types (D3DSAMPLER_TEXTURE_TYPE)
constexpr int kModNegate = 1, kModAbs = 11, kModAbsNegate = 12;       // source modifiers (D3DSHADER_PARAM_SRCMOD_TYPE)
constexpr int kModSaturate = 1;                                       // destination modifier (D3DSPDM_SATURATE)
constexpr int kTexLdBias = 2;                                         // texld control (D3DSI_TEXLD_BIAS)

struct Operand {
    Reg type = Reg::Temp;
    int num = 0, mask = 0xf, mod = 0, shift = 0, swz = 0xe4;   // swz 0xe4: .xyzw
    bool rel = false, isDst = false;
    Reg relType = Reg::Temp;
    int relNum = 0, relSwz = 0;
};
struct Op {
    Opcode op = Opcode::Nop;
    int ctl = 0;
    bool pred = false;
    uint32_t decl = 0;
    bool hasDst = false;
    Operand dst;
    std::vector<Operand> src;
    float lit[4] = {0, 0, 0, 0};
};
struct Program { bool ps = false; std::vector<Op> ops; };

// Register token fields (D3DSP_REGNUM_MASK, D3DSP_REGTYPE_MASK and MASK2, D3DSHADER_ADDRMODE_RELATIVE)
inline Reg rtype(uint32_t t) { return (Reg)(((t >> 28) & 7) | ((t >> 8) & 0x18)); }
inline int regNum(uint32_t t) { return t & 0x7ff; }
inline bool regRelative(uint32_t t) { return (t >> 13) & 1; }

// Destination: write mask, result modifier, shift
inline Operand dstOf(uint32_t t)
{
    Operand o;
    o.isDst = true;
    o.type = rtype(t);
    o.num = regNum(t);
    o.mask = (t >> 16) & 0xf;
    o.mod = (t >> 20) & 0xf;
    o.shift = (t >> 24) & 0xf;
    o.rel = regRelative(t);
    return o;
}

// Source: swizzle, source modifier
inline Operand srcOf(uint32_t t)
{
    Operand o;
    o.type = rtype(t);
    o.num = regNum(t);
    o.swz = (t >> 16) & 0xff;
    o.mod = (t >> 24) & 0xf;
    o.rel = regRelative(t);
    return o;
}

namespace detail {

// Instruction token fields: opcode, control, length, predicated (D3DSI_OPCODE_MASK, D3DSI_INSTLENGTH_MASK, ...)
inline Opcode opcode(uint32_t t) { return (Opcode)(t & 0xffff); }
inline int opControl(uint32_t t) { return (t >> 16) & 0xff; }
inline size_t opLength(uint32_t t) { return (t >> 24) & 0xf; }
inline bool opPredicated(uint32_t t) { return (t >> 28) & 1; }
inline size_t commentLength(uint32_t t) { return (t >> 16) & 0x7fff; }   // D3DSI_COMMENTSIZE

// dcl token fields: usage, usage index, sampler type
inline int dclUsage(uint32_t d) { return d & 0x1f; }
inline int dclIndex(uint32_t d) { return (d >> 16) & 0xf; }
inline int dclSampler(uint32_t d) { return (d >> 27) & 0xf; }

// token i of the program (the caller keeps i below b.size() / 4)
inline uint32_t token(const std::string& b, size_t i)
{
    uint32_t v;
    memcpy(&v, b.data() + sizeof v * i, sizeof v);
    return v;
}

// tokens after the opcode that parse reads for dcl, def, defi, defb
inline size_t tokensRead(Opcode op)
{
    switch (op) {
    case Opcode::Dcl: return 2;
    case Opcode::Def: return 5;
    case Opcode::DefB: case Opcode::DefI: return 1;
    default: return 0;
    }
}

// Flow-control instructions: no destination register
inline bool noDestination(Opcode op)
{
    switch (op) {
    case Opcode::Call: case Opcode::CallNz: case Opcode::Loop: case Opcode::Ret: case Opcode::EndLoop:
    case Opcode::Label: case Opcode::Rep: case Opcode::EndRep: case Opcode::If: case Opcode::Ifc: case Opcode::Else:
    case Opcode::EndIf: case Opcode::Break: case Opcode::BreakC: case Opcode::BreakP: return true;
    default: return false;
    }
}

// Source operands at token i from operand token k; a relative-address token must lie inside the instruction (RE-21)
inline void readSources(const std::string& b, size_t i, size_t len, size_t k, Op& o)
{
    while (k < len) {
        Operand s = srcOf(token(b, i + 1 + k));
        ++k;
        if (s.rel) {
            if (k >= len) throw std::runtime_error("truncated instruction");
            Operand a = srcOf(token(b, i + 1 + k));
            ++k;
            s.relType = a.type;
            s.relNum = a.num;
            s.relSwz = a.swz;
        }
        o.src.push_back(s);
    }
}

// The instruction at token i, whose len tokens follow
inline Op readOp(const std::string& b, size_t i, size_t len)
{
    const uint32_t t = token(b, i);
    Op o;
    o.op = opcode(t);
    o.ctl = opControl(t);
    o.pred = opPredicated(t);
    if (o.op == Opcode::Dcl) {
        o.decl = token(b, i + 1);
        o.dst = dstOf(token(b, i + 2));
        o.hasDst = true;
        return o;
    }
    const bool def = o.op == Opcode::Def || o.op == Opcode::DefI || o.op == Opcode::DefB;
    if (def) {
        o.dst = dstOf(token(b, i + 1));
        o.hasDst = true;
        if (o.op == Opcode::Def) memcpy(o.lit, b.data() + 4 * (i + 2), sizeof(o.lit));
        return o;
    }
    size_t k = 0;
    if (!noDestination(o.op) && len) {
        o.dst = dstOf(token(b, i + 1));
        o.hasDst = true;
        k = 1;
    }
    readSources(b, i, len, k, o);
    return o;
}

// source operands toHlsl reads for each opcode it translates (S[0..2]); 0 for the rest
inline size_t sourcesRead(Opcode op)
{
    switch (op) {
    case Opcode::Mad: case Opcode::Cmp: case Opcode::Dp2Add: return 3;
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Dp3: case Opcode::Dp4: case Opcode::Min:
    case Opcode::Max: case Opcode::Slt: case Opcode::Sge: return 2;
    case Opcode::Pow: case Opcode::Ifc: case Opcode::Tex: case Opcode::TexLdl: return 2;
    case Opcode::Mov: case Opcode::Rcp: case Opcode::Rsq: case Opcode::Exp: case Opcode::Log: case Opcode::Frc:
    case Opcode::Abs: case Opcode::Nrm: case Opcode::SinCos: case Opcode::If: case Opcode::Mova: return 1;
    default: return 0;
    }
}

// toHlsl refuses a predicated instruction, or one with fewer source operands than it reads (RE-21)
inline void checkOp(const Op& o)
{
    if (o.pred) throw std::runtime_error("predication");
    if (o.src.size() < sourcesRead(o.op))
        throw std::runtime_error("opcode " + std::to_string((int)o.op) + ": too few operands");
}

}  // namespace detail

inline Program parse(const std::string& b)
{
    size_t n = b.size() / 4;
    if (!n) throw std::runtime_error("empty program");
    uint32_t ver = detail::token(b, 0);
    if ((ver & 0xffff) != kShaderModel3) throw std::runtime_error("not shader model 3");
    Program p;
    p.ps = (ver >> 16) == kPixelShader;
    for (size_t i = 1; i < n;) {
        const uint32_t t = detail::token(b, i);
        const Opcode op = detail::opcode(t);
        if (op == Opcode::End) break;
        if (op == Opcode::Comment) {
            i += 1 + detail::commentLength(t);
            continue;
        }
        const size_t len = detail::opLength(t);
        if (i + 1 + len > n || len < detail::tokensRead(op)) throw std::runtime_error("truncated instruction");
        p.ops.push_back(detail::readOp(b, i, len));
        i += 1 + len;
    }
    return p;
}

// A D3D9 program's constant table: (name, set 'b'/'i'/'f'/'s', register, count).
struct Const { std::string name; char set; int reg, count; };

namespace detail {

constexpr size_t kCtabEntry = 20;   // D3DXSHADER_CONSTANTINFO

// CTAB entry k starts inside b (its fields are read checked); the file's count may be any u32
inline bool entryStarts(const std::string& b, size_t base, uint32_t info, uint32_t k)
{
    return (uint64_t)base + info + (uint64_t)kCtabEntry * k < b.size();
}

}  // namespace detail

// The CTAB block (D3DXSHADER_CONSTANTTABLE: count +12, entries +16); entry: name +0, set +4, register +6, count +8
inline std::vector<Const> ctab(const std::string& b)
{
    std::vector<Const> out;
    size_t i = b.find("CTAB");
    if (i == std::string::npos) return out;
    size_t base = i + 4;
    uint32_t n = bytes::u32OrZero(b, base + 12), info = bytes::u32OrZero(b, base + 16);
    for (uint32_t k = 0; k < n && detail::entryStarts(b, base, info, k); ++k) {
        size_t e = base + info + detail::kCtabEntry * k;
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

// Python's repr() of a double: shortest round trip, exponent form below 1e-4 or from 1e16 (sm3.py's text)
inline std::string pyrepr(double x)
{
    constexpr int kMinPlain = -4, kMaxPlain = 16;
    if (x == 0) return std::signbit(x) ? "-0.0" : "0.0";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), x, std::chars_format::scientific);
    std::string s(buf, r.ptr);                       // d.ddddde+XX
    size_t e = s.find('e');
    std::string mant = s.substr(0, e);
    int ex = std::stoi(s.substr(e + 1));
    bool neg = mant[0] == '-';
    if (neg) mant.erase(0, 1);
    std::string digits;
    for (char c : mant) if (c != '.') digits.push_back(c);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (ex < kMinPlain || ex >= kMaxPlain) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char eb[16];
        snprintf(eb, sizeof eb, "e%c%02d", ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
        out += eb;
    } else if (ex < 0) {
        out = "0." + std::string(-ex - 1, '0') + digits;
    } else if ((size_t)ex + 1 >= digits.size()) {
        out = digits + std::string(ex + 1 - digits.size(), '0') + ".0";
    } else {
        out = digits.substr(0, ex + 1) + "." + digits.substr(ex + 1);
    }
    return neg ? "-" + out : out;
}

// The HLSL texture type of a dcl sampler type
inline const char* textureType(int kind)
{
    return kind == kSamplerCube ? "TextureCube" : kind == kSamplerVolume ? "Texture3D" : "Texture2D";
}

// A sample call on texture tex through sampler smp; extra: the level or bias argument
inline std::string sampleCall(const std::string& tex, const std::string& smp, const std::string& method,
                              const std::string& uv, const std::string& extra)
{
    return tex + "." + method + "(" + smp + ", " + uv + (extra.empty() ? "" : ", " + extra) + ")";
}

// The VS outputs and PS inputs
constexpr const char* kVaryings[] = {"TEXCOORD0", "TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5",
                                     "TEXCOORD6", "TEXCOORD7", "TEXCOORD8", "TEXCOORD9", "COLOR0", "COLOR1"};
// 3.0 renders reversed-Z; 1.0's far-plane idiom z = w is its near plane: the 1.0 sky dome covered the world
constexpr const char* kReversedZ = "    if (vout.pos.z == vout.pos.w) vout.pos.z = 0;";

namespace detail {

constexpr char kSwizzle[] = "xyzw";
constexpr int kVsOutputs = 16;     // o0..o15
constexpr int kColorOutputs = 4;   // oC0..oC3
constexpr int kMiscPosition = 0;   // misc register 0 is vPos, 1 vFace (D3DSMO_POSITION, D3DSMO_FACE)

// The constants a program defines, its samplers by kind, its input and output semantics by register
struct Decls {
    std::map<int, const float*> defs;
    std::map<int, int> samplers;
    std::map<int, std::string> inputs, outputs;
};

inline Decls scanDecls(const Program& P)
{
    Decls d;
    for (auto& o : P.ops) {
        if (o.op == Opcode::Def) {
            d.defs[o.dst.num] = o.lit;
            continue;
        }
        if (o.op != Opcode::Dcl) continue;
        const int n = o.dst.num;
        std::string sem = std::string(usage(dclUsage(o.decl))) + std::to_string(dclIndex(o.decl));
        if (o.dst.type == Reg::Sampler) d.samplers[n] = dclSampler(o.decl);
        else if (o.dst.type == Reg::Input) d.inputs[n] = sem;
        else if (o.dst.type == Reg::Output) d.outputs[n] = sem;
    }
    return d;
}

// dcl, def, defi, defb and nop emit nothing
inline bool isDeclaration(Opcode op)
{
    switch (op) {
    case Opcode::Dcl: case Opcode::Def: case Opcode::DefI: case Opcode::DefB: case Opcode::Nop: return true;
    default: return false;
    }
}

// e under source modifier m: none, negate, abs, negated abs
inline std::string modified(const std::string& e, int m)
{
    if (m == kModNegate) return "(-" + e + ")";
    if (m == kModAbs) return "abs(" + e + ")";
    if (m == kModAbsNegate) return "(-abs(" + e + "))";
    if (m) throw std::runtime_error("source modifier " + std::to_string(m));
    return e;
}

inline std::string maskOf(int m)
{
    std::string x;
    for (int c = 0; c < 4; ++c)
        if (m >> c & 1) x.push_back(kSwizzle[c]);
    return x;
}

inline std::string swizzle(int swz, int n)
{
    std::string sw;
    for (int c = 0; c < n; ++c) sw.push_back(kSwizzle[(swz >> (2 * c)) & 3]);
    return sw;
}

// The program body as HLSL lines; temps and B's used buffers grow as it goes, so shells come after it
template <class Binding>
struct Emitter {
    const Decls& d;
    Binding& B;
    bool vs;
    std::set<int> temps;   // r<n> the body uses, declared before it
    std::vector<std::string> body;
    int depth = 1;

    std::string reg(const Operand& r);
    std::string constant(const Operand& r);
    std::string src(const Operand& r, int n = 4);
    std::string scalar(const Operand& r);
    void emit(const std::string& l) { body.push_back(std::string(4 * depth, ' ') + l); }
    void store(const Op& o, const std::string& expr) { assign(o, "(" + expr + ")." + maskOf(o.dst.mask)); }
    void storeScalar(const Op& o, const std::string& expr)
    {
        assign(o, "((float4)(" + expr + "))." + maskOf(o.dst.mask));
    }
    void assign(const Op& o, const std::string& val);
    void normalize(const Op& o);
    void sinCos(const Op& o);
    void sample(const Op& o);
    void ifCompare(const Op& o);
    void openBlock(const std::string& l);
    void elseBlock();
    void closeBlock();
    void instruction(const Op& o);
};

template <class Binding>
inline std::string Emitter<Binding>::reg(const Operand& r)
{
    const int n = r.num;
    switch (r.type) {
    case Reg::Temp:
        temps.insert(n);
        return "r" + std::to_string(n);
    case Reg::Input: return "v" + std::to_string(n);
    case Reg::Const: return constant(r);
    case Reg::AddrTex:
        if (vs) return "a" + std::to_string(n);
        break;
    case Reg::Output: return "o" + std::to_string(n);
    case Reg::ColorOut: return "o.oC" + std::to_string(n);
    case Reg::DepthOut: return "o.oDepth";
    case Reg::MiscType: return n == kMiscPosition ? "vPos" : "vFace.xxxx";
    case Reg::ConstBool: return B.boolean(n);
    default: break;
    }
    throw std::runtime_error("register type " + std::to_string((int)r.type));
}

// A def'd constant as its literal (sm3.py's float repr), else where the binding puts it
template <class Binding>
inline std::string Emitter<Binding>::constant(const Operand& r)
{
    std::string rel;
    const char* relReg = r.relType == Reg::AddrTex ? "a" : "aL";
    if (r.rel) rel = std::string(relReg) + std::to_string(r.relNum) + "." + kSwizzle[r.relSwz & 3];
    auto def = d.defs.find(r.num);
    if (rel.empty() && def != d.defs.end()) {
        const float* l = def->second;
        return "float4(" + pyrepr(l[0]) + ", " + pyrepr(l[1]) + ", " + pyrepr(l[2]) + ", " + pyrepr(l[3]) + ")";
    }
    return B.constant(r.num, rel);
}

// Source r swizzled to n components
template <class Binding>
inline std::string Emitter<Binding>::src(const Operand& r, int n)
{
    std::string e = reg(r);
    if (r.type == Reg::ConstBool) return e;
    if (r.type == Reg::AddrTex) e = "((float4)" + e + ")";
    std::string sw = swizzle(r.swz, n);
    return modified((sw == "xyzw") ? e : e + "." + sw, r.mod);
}

template <class Binding>
inline std::string Emitter<Binding>::scalar(const Operand& r)
{
    std::string e = reg(r);
    if (r.type == Reg::AddrTex) e = "((float4)" + e + ")";
    return modified(e + "." + kSwizzle[r.swz & 3], r.mod);
}

template <class Binding>
inline void Emitter<Binding>::assign(const Op& o, const std::string& val)
{
    const Operand& dst = o.dst;
    if (dst.shift || (dst.mod & ~kModSaturate)) throw std::runtime_error("dst shift/modifier");
    std::string mask = maskOf(dst.mask);
    std::string v = (dst.mod & kModSaturate) ? "saturate(" + val + ")" : val;
    if (dst.type == Reg::AddrTex && vs)
        emit(reg(dst) + "." + mask + " = (int" + (mask.size() == 1 ? "" : std::to_string(mask.size())) + ")round(" + v +
             ");");
    else
        emit(reg(dst) + "." + mask + " = " + v + ";");
}

template <class Binding>
inline void Emitter<Binding>::normalize(const Op& o)
{
    std::string v = src(o.src[0]);
    store(o, v + " * rsqrt(dot((" + v + ").xyz, (" + v + ").xyz))");
}

template <class Binding>
inline void Emitter<Binding>::sinCos(const Op& o)
{
    std::string a = scalar(o.src[0]);
    store(o, "float4(cos(" + a + "), sin(" + a + "), 0, 0)");
}

// texld (plain or biased) and texldl: the binding names the call; an undeclared sampler is 2D
template <class Binding>
inline void Emitter<Binding>::sample(const Op& o)
{
    const std::vector<Operand>& S = o.src;
    const Operand& sm = S[1];
    auto smp = d.samplers.find(sm.num);
    int k = smp != d.samplers.end() ? smp->second : kSampler2D;
    std::string uv = src(S[0], k == kSampler2D ? 2 : 3), e;
    if (o.op == Opcode::TexLdl) e = B.sample(sm.num, k, "SampleLevel", uv, src(S[0]) + ".w");
    else if (o.ctl == kTexLdBias) e = B.sample(sm.num, k, "SampleBias", uv, src(S[0]) + ".w");
    else if (o.ctl == 0) e = B.sample(sm.num, k, "Sample", uv, "");
    else throw std::runtime_error("texld control " + std::to_string(o.ctl));
    std::string sw = swizzle(sm.swz, 4);
    store(o, sw == "xyzw" ? e : "(" + e + ")." + sw);
}

// ifc: control 1..6 is the comparison (D3DSHADER_COMPARISON)
template <class Binding>
inline void Emitter<Binding>::ifCompare(const Op& o)
{
    static constexpr const char* kCompare[] = {"", ">", "==", ">=", "<", "!=", "<="};
    if (o.ctl < 1 || o.ctl > 6) throw std::runtime_error("ifc");
    openBlock("if (" + scalar(o.src[0]) + " " + kCompare[o.ctl] + " " + scalar(o.src[1]) + ") {");
}

template <class Binding>
inline void Emitter<Binding>::openBlock(const std::string& l)
{
    emit(l);
    ++depth;
}

template <class Binding>
inline void Emitter<Binding>::elseBlock()
{
    --depth;
    emit("} else {");
    ++depth;
}

template <class Binding>
inline void Emitter<Binding>::closeBlock()
{
    --depth;
    emit("}");
}

template <class Binding>
inline void Emitter<Binding>::instruction(const Op& o)
{
    const std::vector<Operand>& S = o.src;
    switch (o.op) {
    case Opcode::Mov: case Opcode::Mova: return store(o, src(S[0]));
    case Opcode::Add: return store(o, src(S[0]) + " + " + src(S[1]));
    case Opcode::Sub: return store(o, src(S[0]) + " - " + src(S[1]));
    case Opcode::Mul: return store(o, src(S[0]) + " * " + src(S[1]));
    case Opcode::Mad: return store(o, src(S[0]) + " * " + src(S[1]) + " + " + src(S[2]));
    case Opcode::Min: return store(o, "min(" + src(S[0]) + ", " + src(S[1]) + ")");
    case Opcode::Max: return store(o, "max(" + src(S[0]) + ", " + src(S[1]) + ")");
    case Opcode::Slt: return store(o, "(float4)(" + src(S[0]) + " < " + src(S[1]) + ")");
    case Opcode::Sge: return store(o, "(float4)(" + src(S[0]) + " >= " + src(S[1]) + ")");
    case Opcode::Frc: return store(o, "frac(" + src(S[0]) + ")");
    case Opcode::Abs: return store(o, "abs(" + src(S[0]) + ")");
    case Opcode::Cmp: return store(o, "(" + src(S[0]) + " >= 0 ? " + src(S[1]) + " : " + src(S[2]) + ")");
    case Opcode::Dp3: return storeScalar(o, "dot(" + src(S[0], 3) + ", " + src(S[1], 3) + ")");
    case Opcode::Dp4: return storeScalar(o, "dot(" + src(S[0]) + ", " + src(S[1]) + ")");
    case Opcode::Dp2Add: return storeScalar(o, "dot(" + src(S[0], 2) + ", " + src(S[1], 2) + ") + " + scalar(S[2]));
    case Opcode::Rcp: return storeScalar(o, "1.0 / " + scalar(S[0]));
    case Opcode::Rsq: return storeScalar(o, "rsqrt(abs(" + scalar(S[0]) + "))");
    case Opcode::Exp: return storeScalar(o, "exp2(" + scalar(S[0]) + ")");
    case Opcode::Log: return storeScalar(o, "log2(abs(" + scalar(S[0]) + "))");
    case Opcode::Pow: return storeScalar(o, "pow(abs(" + scalar(S[0]) + "), " + scalar(S[1]) + ")");
    case Opcode::Nrm: return normalize(o);
    case Opcode::SinCos: return sinCos(o);
    case Opcode::Tex: case Opcode::TexLdl: return sample(o);
    case Opcode::TexKill: return emit("if (any(" + reg(o.dst) + "." + maskOf(o.dst.mask) + " < 0)) discard;");
    case Opcode::If: return openBlock("if (" + src(S[0]) + ") {");
    case Opcode::Ifc: return ifCompare(o);
    case Opcode::Else: return elseBlock();
    case Opcode::EndIf: return closeBlock();
    default: throw std::runtime_error("opcode " + std::to_string((int)o.op));
    }
}

inline std::string varyings()
{
    std::string io = "float4 pos : SV_Position;";
    for (const char* v : kVaryings) io += std::string(" float4 ") + v + " : " + v + ";";
    return io;
}

// VSIn, VSOut and main's opening: the outputs precise, the inputs the binding gives as expressions
template <class Binding>
inline void vsHead(std::map<int, std::string> inputs, Binding& B, std::vector<std::string>& out)
{
    std::string in;
    std::map<int, std::string> given;   // inputs the binding supplies itself
    for (auto& [k, v] : inputs) {
        std::string e;
        if (B.inputConst(v, e)) given[k] = e;
        else in += (in.empty() ? "" : " ") + B.inputType(v) + "4 v" + std::to_string(k) + " : " + v + ";";
    }
    for (auto& [k, e] : given) inputs.erase(k);
    out.push_back("struct VSIn { " + in + " };");
    out.push_back("struct VSOut { " + varyings() + " };");
    out.push_back("VSOut main(VSIn vin) {");
    // precise: 1.0's passes test depth for equality; fused mul+add per program put positions a rounding bit apart
    std::string os = "    precise float4 ";
    for (int k = 0; k < kVsOutputs; ++k) os += (k ? ", o" : "o") + std::to_string(k) + " = 0";
    out.push_back(os + ";");
    out.push_back("    int4 a0 = 0;");
    for (auto& [k, v] : inputs)
        out.push_back("    float4 v" + std::to_string(k) + " = (float4)vin.v" + std::to_string(k) + ";");
    for (auto& [k, e] : given) out.push_back("    float4 v" + std::to_string(k) + " = " + e + ";");
}

inline bool writes(const Program& P, Reg type, int num)
{
    for (auto& o : P.ops)
        if (o.hasDst && o.dst.type == type && o.dst.num == num) return true;
    return false;
}

inline bool writesDepth(const Program& P)
{
    for (auto& o : P.ops)
        if (o.hasDst && o.dst.type == Reg::DepthOut) return true;
    return false;
}

// The PS outputs: the colour targets written and depth, the G-buffer's three, or none for a quiet depth-only pass
template <class Binding>
inline std::string psOutputs(const Program& P, const Binding& B, bool& quiet)
{
    std::string outs;
    for (int k = 0; k < kColorOutputs; ++k)
        if (writes(P, Reg::ColorOut, k))
            outs += (outs.empty() ? "" : " ") + std::string("float4 oC") + std::to_string(k) + " : SV_Target" +
                    std::to_string(k) + ";";
    if (writesDepth(P)) outs += " float oDepth : SV_Depth;";
    if (B.gbuffer) outs = "float4 oC0 : SV_Target0; float4 o1 : SV_Target1; float4 o2 : SV_Target2;";
    quiet = B.depthOnly && outs.find("SV_Depth") == std::string::npos;
    if (quiet) outs = "float4 oC0; float4 oC1; float4 oC2; float4 oC3;";   // written, never output
    return outs;
}

// PSIn, PSOut and main's opening; true: a quiet depth-only PS (void main)
template <class Binding>
inline bool psHead(const Program& P, const Decls& d, Binding& B, std::vector<std::string>& out)
{
    bool quiet = false;
    std::string outs = psOutputs(P, B, quiet);
    out.push_back("struct PSIn { " + varyings() + " bool face : SV_IsFrontFace; };");
    out.push_back("struct PSOut { " + outs + " };");
    out.push_back(quiet ? "void main(PSIn pin) {" : "PSOut main(PSIn pin) {");
    out.push_back("    PSOut o = (PSOut)0;");
    out.push_back("    float4 vPos = float4(pin.pos.xy - 0.5, 0, 0); float vFace = pin.face ? 1.0 : -1.0;");
    for (auto& [k, v] : d.inputs) out.push_back("    float4 v" + std::to_string(k) + " = pin." + v + ";");
    return quiet;
}

inline void declareTemps(const std::set<int>& temps, bool vs, std::vector<std::string>& out)
{
    if (temps.empty()) return;
    std::string t = vs ? "    precise float4 " : "    float4 ";
    bool first = true;
    for (int r : temps) {
        t += (first ? "r" : ", r") + std::to_string(r) + " = 0";
        first = false;
    }
    out.push_back(t + ";");
}

inline void vsTail(const Decls& d, std::vector<std::string>& out)
{
    out.push_back("    VSOut vout = (VSOut)0;");
    for (auto& [k, v] : d.outputs)
        out.push_back("    vout." + (v == "POSITION0" ? std::string("pos") : v) + " = o" + std::to_string(k) + ";");
    out.push_back(kReversedZ);
    out.push_back("    return vout;");
}

inline std::string joinLines(const std::vector<std::string>& out)
{
    std::string all;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i) all.push_back('\n');
        all += out[i];
    }
    return all;
}

}  // namespace detail

// Binding says where resources live (sm3.py Binding). The text is the shader cache key: keep it exact (RE-02)
template <class Binding>
inline std::string toHlsl(const Program& P, Binding& B)
{
    detail::Decls d = detail::scanDecls(P);
    detail::Emitter<Binding> e{d, B, !P.ps};
    for (auto& o : P.ops) {
        if (detail::isDeclaration(o.op)) continue;
        detail::checkOp(o);
        e.instruction(o);
    }
    std::vector<std::string> out = {B.cbuffers()};
    for (auto& [num, k] : d.samplers) {
        std::string t, sm, decl;
        B.sampler(num, k, t, sm, decl);
        out.push_back(decl);
    }
    bool quiet = false;
    if (P.ps) quiet = detail::psHead(P, d, B, out);
    else detail::vsHead(d.inputs, B, out);
    detail::declareTemps(e.temps, !P.ps, out);
    out.insert(out.end(), e.body.begin(), e.body.end());
    if (!P.ps) detail::vsTail(d, out);
    else if (!B.epilogue.empty()) out.push_back(B.epilogue);
    if (P.ps && !quiet) out.push_back("    return o;");
    out.push_back("}");
    return detail::joinLines(out);
}

}  // namespace sm3
