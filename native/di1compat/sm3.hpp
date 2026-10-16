// D3D9 shader model 3 bytecode (the 1.0 .vp / .fp programs) -> HLSL for the D3D11 compiler. C++ port of DI3Tools/sm3.py
// (its docstring has the token format and the translation rules); octtest --sm3 checks the text against it.
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace sm3 {

enum Reg { R_TEMP = 0, R_INPUT = 1, R_CONST = 2, R_ADDR_TEX = 3, R_OUTPUT = 6, R_COLOROUT = 8, R_DEPTHOUT = 9, R_SAMPLER = 10,
           R_CONSTBOOL = 14, R_LOOP = 15, R_MISCTYPE = 17, R_PREDICATE = 19 };
inline const char* usage(int u)
{
    static const char* U[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL",
                              "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE"};
    return u >= 0 && u < 14 ? U[u] : "UNKNOWN";
}

struct Operand { int type = 0, num = 0, mask = 0xf, mod = 0, shift = 0, swz = 0xe4; bool rel = false, isDst = false; int relType = 0, relNum = 0, relSwz = 0; };
struct Op { int op = 0, ctl = 0; bool pred = false; uint32_t decl = 0; bool hasDst = false; Operand dst; std::vector<Operand> src; float lit[4] = {0, 0, 0, 0}; };
struct Program { bool ps = false; std::vector<Op> ops; };

inline int rtype(uint32_t t) { return (int)(((t >> 28) & 7) | ((t >> 8) & 0x18)); }
inline Operand dstOf(uint32_t t)
{
    Operand o; o.isDst = true; o.type = rtype(t); o.num = t & 0x7ff; o.mask = (t >> 16) & 0xf; o.mod = (t >> 20) & 0xf;
    o.shift = (t >> 24) & 0xf; o.rel = (t >> 13) & 1; return o;
}
inline Operand srcOf(uint32_t t)
{
    Operand o; o.type = rtype(t); o.num = t & 0x7ff; o.swz = (t >> 16) & 0xff; o.mod = (t >> 24) & 0xf; o.rel = (t >> 13) & 1; return o;
}

namespace detail {

// token i of the program (the caller keeps i below b.size() / 4)
inline uint32_t token(const std::string& b, size_t i)
{
    uint32_t v;
    memcpy(&v, b.data() + 4 * i, 4);
    return v;
}

// tokens after the opcode that parse reads for dcl (31), def (81), defi (48), defb (47)
inline size_t tokensRead(int op)
{
    switch (op) {
    case 31: return 2;
    case 81: return 5;
    case 47: case 48: return 1;
    default: return 0;
    }
}

// Source operands of the instruction at token i (len tokens follow it), from its token k on. A relative-address
// token follows its operand and must lie inside the instruction (RE-21).
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

// source operands toHlsl reads for each opcode it translates (S[0..2]); 0 for the rest
inline size_t sourcesRead(int op)
{
    switch (op) {
    case 4: case 88: case 90: return 3;
    case 2: case 3: case 5: case 8: case 9: case 10: case 11: case 12: case 13: return 2;
    case 32: case 41: case 66: case 95: return 2;
    case 1: case 6: case 7: case 14: case 15: case 19: case 35: case 36: case 37: case 40: case 46: return 1;
    default: return 0;
    }
}

// toHlsl refuses a predicated instruction, or one with fewer source operands than it reads (RE-21)
inline void checkOp(const Op& o)
{
    if (o.pred) throw std::runtime_error("predication");
    if (o.src.size() < sourcesRead(o.op))
        throw std::runtime_error("opcode " + std::to_string(o.op) + ": too few operands");
}

}  // namespace detail

inline Program parse(const std::string& b)
{
    size_t n = b.size() / 4;
    auto tok = [&](size_t i) { return detail::token(b, i); };
    if (!n) throw std::runtime_error("empty program");
    uint32_t ver = tok(0);
    if ((ver & 0xffff) != 0x0300) throw std::runtime_error("not shader model 3");
    Program p;
    p.ps = (ver >> 16) == 0xffff;
    for (size_t i = 1; i < n;) {
        uint32_t t = tok(i);
        int op = t & 0xffff;
        if (op == 0xffff) break;
        if (op == 0xfffe) { i += 1 + ((t >> 16) & 0x7fff); continue; }
        size_t len = (t >> 24) & 0xf;
        if (i + 1 + len > n || len < detail::tokensRead(op)) throw std::runtime_error("truncated instruction");
        Op o; o.op = op; o.ctl = (t >> 16) & 0xff; o.pred = (t >> 28) & 1;
        size_t k = 0;
        if (op == 31) { o.decl = tok(i + 1); o.dst = dstOf(tok(i + 2)); o.hasDst = true; }
        else if (op == 81 || op == 48 || op == 47) {
            o.dst = dstOf(tok(i + 1)); o.hasDst = true;
            if (op == 81) for (int c = 0; c < 4; ++c) { uint32_t v = tok(i + 2 + c); memcpy(&o.lit[c], &v, 4); }
        } else {
            static const std::set<int> noDst = {25, 26, 27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 96};
            if (!noDst.count(op) && len) { o.dst = dstOf(tok(i + 1)); o.hasDst = true; k = 1; }
            detail::readSources(b, i, len, k, o);
        }
        p.ops.push_back(o);
        i += 1 + len;
    }
    return p;
}

// Python repr() of a double (shortest round trip; exponent form below 1e-4 or from 1e16), for literal text identical
// to sm3.py's.
inline std::string pyrepr(double x)
{
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
    if (ex < -4 || ex >= 16) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char eb[16];
        sprintf_s(eb, "e%c%02d", ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
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

// Where a program's resources live (sm3.Binding).
struct Binding {
    virtual ~Binding() = default;
    virtual std::string cbuffers() { return "cbuffer CB0 : register(b0) { float4 C[256]; uint4 B[16]; };"; }
    virtual std::string constant(int num, const std::string& rel) { return "C[" + (rel.empty() ? "" : rel + " + ") + std::to_string(num) + "]"; }
    virtual std::string boolean(int num) { return "(B[" + std::to_string(num) + "].x != 0)"; }
    // -> texture name, sampler name, declarations
    virtual void sampler(int num, int kind, std::string& tex, std::string& smp, std::string& decl)
    {
        const char* T = kind == 3 ? "TextureCube" : kind == 4 ? "Texture3D" : "Texture2D";
        tex = "t" + std::to_string(num); smp = "s" + std::to_string(num);
        decl = std::string(T) + " " + tex + " : register(t" + std::to_string(num) + "); SamplerState " + smp + " : register(s" + std::to_string(num) + ");";
    }
    virtual std::string sample(int num, int kind, const std::string& method, const std::string& uv, const std::string& extra)
    {
        std::string t, s, d;
        sampler(num, kind, t, s, d);
        return t + "." + method + "(" + s + ", " + uv + (extra.empty() ? "" : ", " + extra) + ")";
    }
    virtual std::string inputType(const std::string&) { return "float"; }
    // a VS input given by an expression instead of the vertex (true: not declared in VSIn)
    virtual bool inputConst(const std::string&, std::string&) { return false; }
    bool gbuffer = false;
    bool depthOnly = false;   // a depth-only pass (3.0 writes no colour there): keep the discards, drop the outputs
    std::string epilogue;
};

inline const char* VARYINGS[] = {"TEXCOORD0", "TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5", "TEXCOORD6",
                                 "TEXCOORD7", "TEXCOORD8", "TEXCOORD9", "COLOR0", "COLOR1"};
// 3.0 renders reversed-Z (its sky VSs write z = 0, the far plane); 1.0's far-plane idiom is z = w (a .xyww matrix row,
// bit-equal components), which under 3.0's test is the near plane: the 1.0 sky dome covered the world (2026-10-13).
// Geometry through the shared worldToProjected matrix is reversed already. (sm3.py REVERSED_Z)
inline const char* REVERSED_Z = "    if (vout.pos.z == vout.pos.w) vout.pos.z = 0;";

inline std::string toHlsl(const Program& P, Binding& B)
{
    const bool vs = !P.ps;
    std::map<int, const float*> defs;
    std::map<int, int> samplers;
    std::map<int, std::string> inputs, outputs;
    std::set<int> temps;
    std::vector<std::string> body;
    int depth = 1;
    for (auto& o : P.ops) {
        if (o.op == 81) defs[o.dst.num] = o.lit;
        else if (o.op == 31) {
            int t = o.dst.type, n = o.dst.num;
            std::string sem = std::string(usage(o.decl & 0x1f)) + std::to_string((o.decl >> 16) & 0xf);
            if (t == R_SAMPLER) samplers[n] = (o.decl >> 27) & 0xf;
            else if (t == R_INPUT) inputs[n] = sem;
            else if (t == R_OUTPUT) outputs[n] = sem;
        }
    }
    static const char SW[] = "xyzw";
    auto reg = [&](const Operand& r) -> std::string {
        int t = r.type, n = r.num;
        switch (t) {
        case R_TEMP: temps.insert(n); return "r" + std::to_string(n);
        case R_INPUT: return "v" + std::to_string(n);
        case R_CONST: {
            std::string rel;
            if (r.rel) rel = std::string(r.relType == R_ADDR_TEX ? "a" : "aL") + std::to_string(r.relNum) + "." + SW[r.relSwz & 3];
            if (rel.empty() && defs.count(n)) {
                const float* l = defs[n];
                return "float4(" + pyrepr(l[0]) + ", " + pyrepr(l[1]) + ", " + pyrepr(l[2]) + ", " + pyrepr(l[3]) + ")";
            }
            return B.constant(n, rel);
        }
        case R_ADDR_TEX: if (vs) return "a" + std::to_string(n); break;
        case R_OUTPUT: return "o" + std::to_string(n);
        case R_COLOROUT: return "o.oC" + std::to_string(n);
        case R_DEPTHOUT: return "o.oDepth";
        case R_MISCTYPE: return n == 0 ? "vPos" : "vFace.xxxx";
        case R_CONSTBOOL: return B.boolean(n);
        }
        throw std::runtime_error("register type " + std::to_string(t));
    };
    auto mod = [](const std::string& e, int m) -> std::string {
        if (m == 1) return "(-" + e + ")";
        if (m == 11) return "abs(" + e + ")";
        if (m == 12) return "(-abs(" + e + "))";
        if (m) throw std::runtime_error("source modifier " + std::to_string(m));
        return e;
    };
    auto s = [&](const Operand& r, int n = 4) -> std::string {
        std::string e = reg(r);
        if (r.type == R_CONSTBOOL) return e;
        if (r.type == R_ADDR_TEX) e = "((float4)" + e + ")";
        std::string sw;
        for (int c = 0; c < n; ++c) sw.push_back(SW[(r.swz >> (2 * c)) & 3]);
        return mod((sw == "xyzw") ? e : e + "." + sw, r.mod);
    };
    auto scal = [&](const Operand& r) -> std::string {
        std::string e = reg(r);
        if (r.type == R_ADDR_TEX) e = "((float4)" + e + ")";
        return mod(e + "." + SW[r.swz & 3], r.mod);
    };
    auto emit = [&](const std::string& l) { body.push_back(std::string(4 * depth, ' ') + l); };
    auto maskOf = [&](int m) { std::string x; for (int c = 0; c < 4; ++c) if (m >> c & 1) x.push_back(SW[c]); return x; };
    auto store = [&](const Op& o, const std::string& expr, bool scalar = false) {
        const Operand& d = o.dst;
        if (d.shift || (d.mod & ~1)) throw std::runtime_error("dst shift/modifier");
        std::string mask = maskOf(d.mask);
        std::string val = scalar ? "((float4)(" + expr + "))." + mask : "(" + expr + ")." + mask;
        if (d.mod & 1) val = "saturate(" + val + ")";
        if (d.type == R_ADDR_TEX && vs)
            emit(reg(d) + "." + mask + " = (int" + (mask.size() == 1 ? "" : std::to_string(mask.size())) + ")round(" + val + ");");
        else
            emit(reg(d) + "." + mask + " = " + val + ";");
    };
    for (auto& o : P.ops) {
        auto& S = o.src;
        switch (o.op) {
        case 31: case 81: case 48: case 47: case 0: continue;
        }
        detail::checkOp(o);
        switch (o.op) {
        case 1: case 46: store(o, s(S[0])); break;
        case 2: store(o, s(S[0]) + " + " + s(S[1])); break;
        case 3: store(o, s(S[0]) + " - " + s(S[1])); break;
        case 5: store(o, s(S[0]) + " * " + s(S[1])); break;
        case 4: store(o, s(S[0]) + " * " + s(S[1]) + " + " + s(S[2])); break;
        case 10: store(o, "min(" + s(S[0]) + ", " + s(S[1]) + ")"); break;
        case 11: store(o, "max(" + s(S[0]) + ", " + s(S[1]) + ")"); break;
        case 12: store(o, "(float4)(" + s(S[0]) + " < " + s(S[1]) + ")"); break;
        case 13: store(o, "(float4)(" + s(S[0]) + " >= " + s(S[1]) + ")"); break;
        case 19: store(o, "frac(" + s(S[0]) + ")"); break;
        case 35: store(o, "abs(" + s(S[0]) + ")"); break;
        case 88: store(o, "(" + s(S[0]) + " >= 0 ? " + s(S[1]) + " : " + s(S[2]) + ")"); break;
        case 8: store(o, "dot(" + s(S[0], 3) + ", " + s(S[1], 3) + ")", true); break;
        case 9: store(o, "dot(" + s(S[0]) + ", " + s(S[1]) + ")", true); break;
        case 90: store(o, "dot(" + s(S[0], 2) + ", " + s(S[1], 2) + ") + " + scal(S[2]), true); break;
        case 6: store(o, "1.0 / " + scal(S[0]), true); break;
        case 7: store(o, "rsqrt(abs(" + scal(S[0]) + "))", true); break;
        case 14: store(o, "exp2(" + scal(S[0]) + ")", true); break;
        case 15: store(o, "log2(abs(" + scal(S[0]) + "))", true); break;
        case 32: store(o, "pow(abs(" + scal(S[0]) + "), " + scal(S[1]) + ")", true); break;
        case 36: { std::string v = s(S[0]); store(o, v + " * rsqrt(dot((" + v + ").xyz, (" + v + ").xyz))"); break; }
        case 37: { std::string a = scal(S[0]); store(o, "float4(cos(" + a + "), sin(" + a + "), 0, 0)"); break; }
        case 66: case 95: {
            const Operand& sm = S[1];
            int k = samplers.count(sm.num) ? samplers[sm.num] : 2;
            std::string uv = s(S[0], k == 2 ? 2 : 3), e;
            if (o.op == 95) e = B.sample(sm.num, k, "SampleLevel", uv, s(S[0]) + ".w");
            else if (o.ctl == 2) e = B.sample(sm.num, k, "SampleBias", uv, s(S[0]) + ".w");
            else if (o.ctl == 0) e = B.sample(sm.num, k, "Sample", uv, "");
            else throw std::runtime_error("texld control " + std::to_string(o.ctl));
            std::string sw;
            for (int c = 0; c < 4; ++c) sw.push_back(SW[(sm.swz >> (2 * c)) & 3]);
            store(o, sw == "xyzw" ? e : "(" + e + ")." + sw);
            break;
        }
        case 65: emit("if (any(" + reg(o.dst) + "." + maskOf(o.dst.mask) + " < 0)) discard;"); break;
        case 40: emit("if (" + s(S[0]) + ") {"); ++depth; break;
        case 41: {
            static const char* cmp[] = {"", ">", "==", ">=", "<", "!=", "<="};
            if (o.ctl < 1 || o.ctl > 6) throw std::runtime_error("ifc");
            emit("if (" + scal(S[0]) + " " + cmp[o.ctl] + " " + scal(S[1]) + ") {"); ++depth; break;
        }
        case 42: --depth; emit("} else {"); ++depth; break;
        case 43: --depth; emit("}"); break;
        default: throw std::runtime_error("opcode " + std::to_string(o.op));
        }
    }
    std::vector<std::string> out = {B.cbuffers()};
    for (auto& [num, k] : samplers) { std::string t, sm, d; B.sampler(num, k, t, sm, d); out.push_back(d); }
    bool quiet = false;   // a depth-only PS: no outputs
    std::string io = "float4 pos : SV_Position;";
    for (const char* v : VARYINGS) io += std::string(" float4 ") + v + " : " + v + ";";
    if (vs) {
        std::string in;
        std::map<int, std::string> given;   // inputs the binding supplies itself
        for (auto& [k, v] : inputs) {
            std::string e;
            if (B.inputConst(v, e)) given[k] = e;
            else in += (in.empty() ? "" : " ") + B.inputType(v) + "4 v" + std::to_string(k) + " : " + v + ";";
        }
        for (auto& [k, e] : given) inputs.erase(k);
        out.push_back("struct VSIn { " + in + " };");
        out.push_back("struct VSOut { " + io + " };");
        out.push_back("VSOut main(VSIn vin) {");
        // precise: 1.0 draws a surface in passes that test depth for equality against its prepass (G-buffer, final);
        // each pass's VS is compiled on its own, and unconstrained the compiler fuses mul+add differently per program:
        // positions a rounding bit apart z-fought -- dotted black surfaces, flicker, worst far away (2026-10-13)
        std::string os = "    precise float4 ";
        for (int k = 0; k < 16; ++k) os += (k ? ", o" : "o") + std::to_string(k) + " = 0";
        out.push_back(os + ";");
        out.push_back("    int4 a0 = 0;");
        for (auto& [k, v] : inputs) out.push_back("    float4 v" + std::to_string(k) + " = (float4)vin.v" + std::to_string(k) + ";");
        for (auto& [k, e] : given) out.push_back("    float4 v" + std::to_string(k) + " = " + e + ";");
    } else {
        std::string outs;
        for (int k = 0; k < 4; ++k) {
            bool w = false;
            for (auto& o : P.ops) if (o.hasDst && o.dst.type == R_COLOROUT && o.dst.num == k) w = true;
            if (w) outs += (outs.empty() ? "" : " ") + std::string("float4 oC") + std::to_string(k) + " : SV_Target" + std::to_string(k) + ";";
        }
        for (auto& o : P.ops) if (o.hasDst && o.dst.type == R_DEPTHOUT) { outs += " float oDepth : SV_Depth;"; break; }
        if (B.gbuffer) outs = "float4 oC0 : SV_Target0; float4 o1 : SV_Target1; float4 o2 : SV_Target2;";
        quiet = B.depthOnly && outs.find("SV_Depth") == std::string::npos;
        if (quiet) outs = "float4 oC0; float4 oC1; float4 oC2; float4 oC3;";   // written, never output
        out.push_back("struct PSIn { " + io + " bool face : SV_IsFrontFace; };");
        out.push_back("struct PSOut { " + outs + " };");
        out.push_back(quiet ? "void main(PSIn pin) {" : "PSOut main(PSIn pin) {");
        out.push_back("    PSOut o = (PSOut)0;");
        out.push_back("    float4 vPos = float4(pin.pos.xy - 0.5, 0, 0); float vFace = pin.face ? 1.0 : -1.0;");
        for (auto& [k, v] : inputs) out.push_back("    float4 v" + std::to_string(k) + " = pin." + v + ";");
    }
    if (!temps.empty()) {
        std::string t = vs ? "    precise float4 " : "    float4 ";
        bool first = true;
        for (int r : temps) { t += (first ? "r" : ", r") + std::to_string(r) + " = 0"; first = false; }
        out.push_back(t + ";");
    }
    out.insert(out.end(), body.begin(), body.end());
    if (vs) {
        out.push_back("    VSOut vout = (VSOut)0;");
        for (auto& [k, v] : outputs) out.push_back("    vout." + (v == "POSITION0" ? std::string("pos") : v) + " = o" + std::to_string(k) + ";");
        out.push_back(REVERSED_Z);
        out.push_back("    return vout;");
    } else {
        if (!B.epilogue.empty()) out.push_back(B.epilogue);
        if (!quiet) out.push_back("    return o;");
    }
    out.push_back("}");
    std::string all;
    for (size_t i = 0; i < out.size(); ++i) { if (i) all.push_back('\n'); all += out[i]; }
    return all;
}

}  // namespace sm3
