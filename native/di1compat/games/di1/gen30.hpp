// 1.0 shaders (.shd, D3D9 programs) to a 3.0 .shd and D3D11 HLSL, as gen30.py. Base and formats only; any thread.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../base/hash.hpp"
#include "../../formats/rentable.hpp"
#include "../../formats/shd.hpp"
#include "../../formats/sm3.hpp"
#include "../../formats/vtxf.hpp"

namespace gen30 {

// A 1.0 .shd's records: a parameter (A: in block A; offset within its block), a pass, a technique
struct Param { uint32_t hash; bool A; int offset, type; };
struct Pass { uint32_t word, vf, vs, ps; };   // 1.0 pass word, vformatCRC, VS and PS ids
struct Tech { uint32_t hash; int first, count; };
struct Shd10 {
    std::vector<Pass> passes;
    std::vector<Tech> techs;
    std::vector<Param> params;
    std::vector<uint32_t> slotsB;   // block B's texture slot names
    int sizeA = 0, sizeB = 0;
    size_t defaultsAt = 0;          // past the name that ends the slot names
};

// 1.0's .shd (DisneyInfinity.exe FUN_004af380 'Shaders/%s.shd', finalize FUN_004af200); a name ends its slot names
inline constexpr shd::Layout kShd10 = {0x0C, 0xA4, 0x30, 0x10, 0x18, 0x20, 0x44, 0x68};
constexpr size_t kShd10TechSlots = 0x30;   // 16 bytes, 3.0's +0x54 in the same slot order (gen30.py)

// shd.parse(b, '1.0'); false: s may be partly written
inline bool parse10(const std::string& b, Shd10& s)
{
    const shd::Layout& L = kShd10;
    if (b.size() < L.data) return false;
    const int npar = (uint8_t)b[L.counts + shd::kCountParams], npass = (uint8_t)b[L.counts + shd::kCountPasses];
    const int ntech = (uint8_t)b[L.counts + shd::kCountTechs];
    size_t o = L.data;
    for (int i = 0; i < npass; ++i, o += L.pass) {
        const uint32_t word = bytes::u32OrZero(b, o), vf = bytes::u32OrZero(b, o + L.passVformat);
        s.passes.push_back({word, vf, bytes::u32OrZero(b, o + L.passVs), bytes::u32OrZero(b, o + L.passPs)});
    }
    std::vector<uint16_t> ranges;
    for (int i = 0; i < ntech; ++i) ranges.push_back(bytes::u16OrZero(b, o + sizeof(uint16_t) * i));
    o = bytes::alignUp(o + sizeof(uint16_t) * ntech, shd::kNameAlign);
    std::vector<uint32_t> pn, tn, pw;
    for (int i = 0; i < npar; ++i, o += sizeof(uint32_t)) pn.push_back(bytes::u32OrZero(b, o));
    for (int i = 0; i < ntech; ++i, o += sizeof(uint32_t)) tn.push_back(bytes::u32OrZero(b, o));
    for (int i = 0; i < npar; ++i, o += sizeof(uint32_t)) pw.push_back(bytes::u32OrZero(b, o));
    const int nA = (uint8_t)b[L.blockA + shd::kBlockParams], slotsA = (uint8_t)b[L.blockA + shd::kBlockSlots];
    const int slotsB = (uint8_t)b[L.blockB + shd::kBlockSlots];
    s.sizeA = bytes::u16OrZero(b, L.blockA + shd::kBlockSize);
    s.sizeB = bytes::u16OrZero(b, L.blockB + shd::kBlockSize);
    o += sizeof(uint32_t) * slotsA;
    for (int i = 0; i < slotsB; ++i, o += sizeof(uint32_t)) s.slotsB.push_back(bytes::u32OrZero(b, o));
    const size_t e = b.find('\0', o);
    if (e == std::string::npos) return false;
    s.defaultsAt = e + 1;
    for (int i = 0; i < ntech; ++i) s.techs.push_back({tn[i], shd::rangeFirst(ranges[i]), shd::rangeCount(ranges[i])});
    for (int i = 0; i < npar; ++i) s.params.push_back({pn[i], i < nA, shd::wordOffset(pw[i]), shd::wordType(pw[i])});
    return o <= b.size();
}

// Parameter offsets in the 3.0 blocks by name hash: A as in 1.0, B moved past the wider slot table
struct Layout {
    std::map<uint32_t, int> A, B;
    int first10 = 0, first30 = 0, firstB = 0, slots = 0, sizeA = 0, sizeB = 0;
};

constexpr uint32_t kEmptySlot = 0xffffffff;   // an empty slot's texture index
constexpr size_t kSlot10Bytes = 4;            // a 1.0 slot: texture index (gen30.py)
constexpr size_t kSlot30Bytes = 8;            // a 3.0 slot: texture index, then 0 (FORMATS.md 6.2)
constexpr size_t kSlotTailBytes = 1;          // a byte per slot after the slot table, copied as it is (gen30.py)

inline Layout layout(const Shd10& s)
{
    Layout L;
    L.slots = (int)s.slotsB.size();
    int first10 = (int)bytes::alignUp(L.slots * (kSlot10Bytes + kSlotTailBytes), shd::kRegisterBytes);
    bool any = false;
    for (auto& p : s.params) {
        if (p.A) continue;
        first10 = any ? (std::min)(first10, p.offset) : p.offset;
        any = true;
    }
    L.first10 = first10;
    L.first30 = (int)bytes::alignUp(L.slots * (kSlot30Bytes + kSlotTailBytes), shd::kRegisterBytes);
    for (auto& p : s.params) (p.A ? L.A : L.B)[p.hash] = p.A ? p.offset : p.offset - first10 + L.first30;
    L.firstB = L.first30 / shd::kRegisterBytes;
    L.sizeA = s.sizeA;
    L.sizeB = L.first30 + (std::max)(s.sizeB - first10, 0);
    return L;
}

// A 1.0 block B in 3.0's form: slots widened to 8 bytes, tail bytes kept, parameters moved to first30
inline std::string blockB30(const std::string& b10, const Layout& L)
{
    std::string out;
    for (int i = 0; i < L.slots; ++i) {
        const uint32_t idx = bytes::u32OrZero(b10, kSlot10Bytes * i);
        const uint32_t w[2] = {idx, idx == kEmptySlot ? kEmptySlot : 0u};
        out.append((const char*)w, sizeof w);
    }
    out += b10.substr((std::min)(kSlot10Bytes * L.slots, b10.size()), kSlotTailBytes * L.slots);
    out.resize(L.first30, '\0');
    if ((size_t)L.first10 < b10.size()) out += b10.substr(L.first10);
    out.resize(L.sizeB, '\0');
    return out;
}

struct Program { uint32_t id; bool ps; std::string hlsl, code; };
struct Generated { std::string shd; std::vector<Program> programs; std::vector<std::string> problems; };
using Read = std::function<bool(const std::string& path, std::string& out)>;
using Compile = std::function<bool(const std::string& hlsl, const char* target, std::string& code, std::string& msg)>;

// What generate reads
struct Sources {
    Read read10;
    const vtxf::VFormats& vf;                       // 1.0's vertex formats
    Compile compile;                                // empty: no code, sizes 0 (gen30.py compile_=False)
    std::function<uint32_t(uint32_t)> skinTarget;   // skintr10: a skinned 1.0 format's 3.0 target
    const vtxf::VFormats* vf30 = nullptr;           // skintr10: 3.0's formats; without them a skinned pass fails
};

namespace detail {

// 1.0 pass word fields (depth, blend, cull, X, Y) and their 3.0 bits (gen30.py; FUN_0049fb20, FUN_0045cf40)
struct WordField { int from, bits, to; };
inline constexpr WordField kWordFields[] = {{0, 3, 3}, {3, 4, 6}, {7, 2, 10}, {9, 4, 12}, {13, 3, 16}};

inline uint32_t convWord(uint32_t w)
{
    uint32_t out = 0;
    for (const WordField& f : kWordFields) out |= ((w >> f.from) & ((1u << f.bits) - 1)) << f.to;
    return out;
}

// The 1.0 word in 3.0's fields, plus the bits 3.0 adds for (technique, depth kind, X): W lines
inline uint32_t word30(const rentable::ShaderRules& rules, uint32_t tech, uint32_t w10)
{
    const uint32_t w = convWord(w10);
    auto it = rules.wordRules.find({tech, (int)shd::depthField(w), (int)shd::xField(w)});
    return w | (it != rules.wordRules.end() ? it->second : 0);
}

// A G-buffer pass's PS ending: 3.0's three targets, the 1.0 normal and gloss re-encoded (gen30.py GBUFFER_EPILOGUE)
inline constexpr const char* kGBufferEpilogue =
    "    {   // 3.0 G-buffer encoding of the 1.0 world normal (.xyz) and gloss (.w)\n"
    "        float3 n = o.oC0.xyz / (abs(o.oC0.x) + abs(o.oC0.y) + abs(o.oC0.z));\n"
    "        float2 f = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0);\n"
    "        o.oC0 = float4((n.z >= 0 ? n.xy : f) * 0.5 + 0.5, o.oC0.w, 1.0);\n"
    "        o.o1 = float4(0, 0, 0, 0);\n"
    "        o.o2 = float4(1, 1, 1, 0);\n"
    "    }";

// 1.0's normal-gloss sampler (s1) reads world normal and gloss; ng10 decodes 3.0's octahedral t1 into that
inline constexpr const char* kNormalGlossDecl =
    "#ifndef NG10\n#define NG10\nTexture2D tNG : register(t1); SamplerState sNG : register(s1);\n"
    "float4 ng10(float4 t) { float2 e = t.xy * 2.0 - 1.0; float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));\n"
    "    if (n.z < 0) n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0); "
    "return float4(normalize(n), t.z); }\n#endif";

// The light sampler's 3.0 sources: diffuse light at t3, specular light at t4 (gen30.py LIGHT10)
inline constexpr const char* kLightDecl =
    "\nTexture2D tLight : register(t3); SamplerState sLight : register(s3); "
    "Texture2D tSpec : register(t4); SamplerState sSpec : register(s4);";

// 1.0 sampler registers (gen30.py ENGINE_TEX, LIGHT10, SLOT10): engine-bound depth, normal-gloss, light; then slots
constexpr int kDepthSampler = 0, kNormalGlossSampler = 1, kLightSampler = 2, kFirstSlotSampler = 3;
constexpr int kDepthTexture = 0;   // 3.0 binds depth at t0 as well

// A skinned pass's bones are the identity: row_major float4x4, one register per row
inline constexpr const char* kIdentityRows[] = {"float4(1, 0, 0, 0)", "float4(0, 1, 0, 0)", "float4(0, 0, 1, 0)",
                                               "float4(0, 0, 0, 1)"};
constexpr int kBoneRows = 4;

// sm3::toHlsl's binding for a 1.0 program in 3.0
struct Binding30 {
    const rentable::ShaderRules& rules;
    const Layout& blocks;
    std::map<std::string, std::string> fmt;            // VS input semantic -> element type
    std::map<int, std::pair<std::string, int>> creg;   // float register -> its constant's name and first register
    std::map<int, int> used;                           // buffer register -> registers declared
    std::vector<std::string> unresolved;
    // Skinned pass (cfg skintr10): 3.0 skins before the VS, so the 1.0 VS's bones become identity
    bool skin = false;
    std::set<std::string> needed;   // the vertex inputs the VS still reads
    bool gbuffer = false;
    bool depthOnly = false;   // a depth-only pass: keep the discards, drop the outputs
    std::string epilogue;

    Binding30(const rentable::ShaderRules& shaderRules, const Layout& layout10, const std::vector<sm3::Const>& table,
              std::map<std::string, std::string> formats)
        : rules(shaderRules), blocks(layout10), fmt(std::move(formats))
    {
        for (auto& c : table) {
            if (c.set != 'f') continue;
            for (int k = 0; k < c.count; ++k) creg[c.reg + k] = {c.name, c.reg};
        }
    }

    // A skinned VS reads bone index 0 at weight 1; every other input is recorded as needed
    bool inputConst(const std::string& sem, std::string& expr)
    {
        if (skin && sem.rfind("BLENDINDICES", 0) == 0) {
            expr = "float4(0, 0, 0, 0)";
            return true;
        }
        if (skin && sem.rfind("BLENDWEIGHT", 0) == 0) {
            expr = "float4(1, 0, 0, 0)";
            return true;
        }
        needed.insert(sem);
        return false;
    }

    // A G-buffer pass's PS: 3.0's three targets, the 1.0 normal and gloss re-encoded
    void gbufferOutput()
    {
        gbuffer = true;
        epilogue = kGBufferEpilogue;
    }

    // 1.0's g_fog_sunLight_color_primary is g_fog_sunLight_params + g_fog_color, read from perFrame as gen30.py does
    bool fogPrimary(int& cb, std::string& expr)
    {
        auto p = rules.globals.find("g_fog_sunLight_params"), c = rules.globals.find("g_fog_color");
        if (p == rules.globals.end() || c == rules.globals.end()) return false;
        const int pr = p->second[1] / shd::kRegisterBytes, cr = c->second[1] / shd::kRegisterBytes;
        used[shd::kPerFrame] = (std::max)({used[shd::kPerFrame], pr + 1, cr + 1});
        cb = -1;
        const std::string buffer = "CB" + std::to_string(shd::kPerFrame) + "[";
        const std::string params = buffer + std::to_string(pr) + "]", colour = buffer + std::to_string(cr) + "]";
        expr = "float4(" + params + ".xyz + " + colour + ".xyz, " + params + ".w)";
        return true;
    }

    // -> buffer register (0..4) and first register, or buffer -1 with an expression, or false
    bool locate(const std::string& name, int& cb, int& base, std::string& expr)
    {
        const uint32_t h = hash::ehash(name);
        if (auto a = blocks.A.find(h); a != blocks.A.end()) {
            cb = shd::kPerInstance;
            base = a->second / shd::kRegisterBytes;
            return true;
        }
        if (auto b = blocks.B.find(h); b != blocks.B.end()) {
            cb = shd::kPerMaterial;
            base = b->second / shd::kRegisterBytes - blocks.firstB;
            return true;
        }
        if (auto g = rules.globals.find(name); g != rules.globals.end()) {
            cb = g->second[0];
            base = g->second[1] / shd::kRegisterBytes;
            return true;
        }
        if (name == "g_fog_sunLight_color_primary") return fogPrimary(cb, expr);
        return false;
    }

    std::string constant(int num, const std::string& rel)
    {
        auto hit = creg.find(num);
        const bool bones = skin && hit != creg.end() && hit->second.first == "g_skinning_boneTransforms";
        if (bones) return kIdentityRows[(num - hit->second.second) & (kBoneRows - 1)];
        int cb = 0, base = 0;
        std::string expr;
        if (hit == creg.end() || !locate(hit->second.first, cb, base, expr)) {
            unresolved.push_back(hit != creg.end() ? hit->second.first : "c" + std::to_string(num));
            return "float4(0, 0, 0, 0)";
        }
        if (cb < 0) return expr;
        const int reg0 = hit->second.second;
        if (rel.empty()) {
            used[cb] = (std::max)(used[cb], base + num - reg0 + 1);
            return "CB" + std::to_string(cb) + "[" + std::to_string(base + num - reg0) + "]";
        }
        int span = 0;
        for (auto& [r, named] : creg)
            if (named == hit->second) span = (std::max)(span, r - reg0 + 1);
        used[cb] = (std::max)(used[cb], base + span);
        return "CB" + std::to_string(cb) + "[" + rel + " + " + std::to_string(base + num - reg0) + "]";
    }

    std::string boolean(int) { return "false"; }

    std::string cbuffers()
    {
        std::string s;
        for (auto& [k, n] : used) {
            if (!n) continue;
            if (!s.empty()) s += "\n";
            s += std::string("cbuffer ") + shd::kConstantBufferNames[k] + " : register(b" + std::to_string(k) +
                 ") { float4 CB" + std::to_string(k) + "[" + std::to_string(n) + "]; };";
        }
        return s;
    }

    void sampler(int num, int kind, std::string& tex, std::string& smp, std::string& decl)
    {
        if (num == kNormalGlossSampler) {
            tex = "tNG";
            smp = "sNG";
            decl = kNormalGlossDecl;
            return;
        }
        if (num == kLightSampler) {
            tex = "tLight";
            smp = "sLight";
            decl = std::string(kNormalGlossDecl) + kLightDecl;
            return;
        }
        const int r = num == kDepthSampler ? kDepthTexture : shd::kFirstSlotRegister + num - kFirstSlotSampler;
        tex = "t" + std::to_string(r);
        smp = "s" + std::to_string(r);
        decl = std::string(sm3::textureType(kind)) + " " + tex + " : register(t" + std::to_string(r) +
               "); SamplerState " + smp + " : register(s" + std::to_string(r) + ");";
    }

    // 3.0's specular is normalized: dividing by 1.0's Blinn factor (exp2(10 gloss + 1) + 2) / 8 restores it
    static std::string lightSample(const std::string& method, const std::string& args)
    {
        return "float4(tLight." + method + "(sLight, " + args + ").rgb, 6.0 * dot(tSpec." + method + "(sSpec, " +
               args + ").rgb, float3(0.2125, 0.7154, 0.0721)) * 8.0 / (exp2(10.0 * ng10(tNG." + method + "(sNG, " +
               args + ")).w + 1.0) + 2.0))";
    }

    // The light sampler is 1.0's light buffer: diffuse .rgb, specular luminance x6 in .a
    std::string sample(int num, int kind, const std::string& method, const std::string& uv, const std::string& extra)
    {
        const std::string x = extra.empty() ? "" : ", " + extra;
        if (num == kNormalGlossSampler) return "ng10(tNG." + method + "(sNG, " + uv + x + "))";
        if (num == kLightSampler) return lightSample(method, uv + x);
        std::string t, s, d;
        sampler(num, kind, t, s, d);
        return sm3::sampleCall(t, s, method, uv, extra);
    }

    // Integer vertex elements arrive as uint or int (gen30.py INT_TYPES); every other element as float
    std::string inputType(const std::string& sem)
    {
        struct Typed { const char* element; const char* hlsl; };
        static constexpr Typed kIntegers[] = {{"U8_4", "uint"}, {"U16_2", "uint"}, {"U16_4", "uint"},
                                              {"U8_1", "uint"}, {"U16_1", "uint"}, {"S16_2", "int"},
                                              {"S16_4", "int"}, {"S8_4", "int"}};
        auto f = fmt.find(sem);
        if (f == fmt.end()) return "float";
        for (const Typed& t : kIntegers)
            if (f->second == t.element) return t.hlsl;
        return "float";
    }
};

constexpr uint32_t kGeneratedIdBit = 0x80000000;   // set in every generated program id (gen30.py prog_id)

// A generated program's id: zlib CRC-32 of "di1compat:<shader>:<pass>:<vp|fp>"
inline uint32_t progId(const std::string& shader, int i, const char* kind)
{
    return hash::crc32Zlib("di1compat:" + shader + ":" + std::to_string(i) + ":" + kind) | kGeneratedIdBit;
}

// A 1.0 program by id: '%x' first (1.0's 'Shaders/%x.vp', no zero padding), then '%08x'
inline bool program10(const Read& read10, uint32_t id, const char* kind, std::string& out)
{
    char a[32], b[32];
    snprintf(a, sizeof a, "shaders/shaders/%x.%s", id, kind);
    snprintf(b, sizeof b, "shaders/shaders/%08x.%s", id, kind);
    return read10(a, out) || read10(b, out);
}

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
    auto f30 = in.vf30 && t ? in.vf30->find(t) : vtxf::VFormats::const_iterator();
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
    const rentable::ShaderRules& rules;
    const Sources& in;
    const std::string& shader;
    const Shd10& s10;
    const Layout& blocks;
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
constexpr size_t kCompileMessageChars = 200;   // a compile problem keeps the message's start

// Every input a skinned VS still reads must be in the 3.0 format, or no input layout can be made
inline void checkSkinInputs(const Binding30& binding, const PassFormat& f, const std::string& at, Generated& g)
{
    for (auto& sem : binding.needed)
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
        Binding30 binding(job.sj.rules, job.sj.blocks, sm3::ctab(b), ps ? none : job.format.elements);
        if (ps && depth == shd::kDepthGBuffer) binding.gbufferOutput();
        // depth-only: no colour output in 1549 of 3.0's 1550; 1.0's linear-depth colour painted near-black
        binding.depthOnly = ps && depth == shd::kDepthOnly;
        binding.skin = job.format.skinned && !ps;
        hlsl = sm3::toHlsl(prog, binding);
        if (!binding.unresolved.empty()) g.problems.push_back(at + ": unresolved " + binding.unresolved[0]);
        if (binding.skin) checkSkinInputs(binding, job.format, at, g);
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
inline uint16_t passByte(const rentable::ShaderRules& rules, uint32_t tech, uint32_t w)
{
    auto b = rules.passByte.find({tech, w});
    return (uint8_t)(b != rules.passByte.end() ? b->second : 0);
}

inline shd::Pass generatePass(const ShaderJob& sj, size_t i, Generated& g)
{
    const Pass& p = sj.s10.passes[i];
    auto t = sj.techOf.find((int)i);
    const uint32_t tech = t != sj.techOf.end() ? t->second : 0;
    const uint32_t w = word30(sj.rules, tech, p.word);
    const PassFormat f = passFormat(sj.in, p, "pass " + std::to_string(i), g);
    const PassJob job{sj, i, w, f};
    const ProgramRef vs = addProgram(job, Stage::Vertex, p.vs, g);
    const ProgramRef ps = addProgram(job, Stage::Pixel, p.ps, g);
    return {w, f.vformat, vs.id, ps.id, vs.size, ps.size, passByte(sj.rules, tech, w)};
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
inline void setTextureMasks(const Shd10& s10, const std::vector<Program>& programs, std::vector<shd::Pass>& passes)
{
    std::vector<uint32_t> mask(passes.size(), 0);
    for (auto& pr : programs) {
        const int i = passOf(passes, pr.id);
        if (i >= 0) mask[i] |= textureRegisters(pr.hlsl);
    }
    for (auto& t : s10.techs) {
        uint32_t all = 0;
        const int end = (std::min)(t.first + t.count, (int)passes.size());
        for (int i = t.first; i < end; ++i) all |= mask[i];
        for (int i = t.first; i < end; ++i) passes[i].textures = (uint16_t)(i == t.first ? all : mask[i]);
    }
}

// The 1.0 shader's 3.0 description: parameters at 3.0 offsets, its defaults, block B re-laid for 8-byte slots
inline shd::Shader shader30(const std::string& raw10, const Shd10& s10, const Layout& blocks,
                            std::vector<shd::Pass> passes)
{
    shd::Shader d;
    d.passes = std::move(passes);
    for (auto& t : s10.techs) d.techs.push_back({t.hash, t.first, t.count});
    for (auto& p : s10.params) {
        const int offset = p.A ? p.offset : blocks.B.at(p.hash);
        d.params.push_back({p.hash, shd::paramWord((uint32_t)p.type, (uint32_t)offset)});
        d.paramsA += p.A;
    }
    d.slots = s10.slotsB;
    memcpy(d.techSlots.data(), raw10.data() + kShd10TechSlots, d.techSlots.size());
    d.sizeA = blocks.sizeA;
    d.sizeB = blocks.sizeB;
    d.firstB = blocks.firstB;
    const size_t a = bytes::alignUp(s10.defaultsAt, shd::kBlockAlign);
    const size_t b = bytes::alignUp(a + s10.sizeA, shd::kBlockAlign);
    d.defaultsA = raw10.substr((std::min)(a, raw10.size()), s10.sizeA);
    d.defaultsA.resize(s10.sizeA, '\0');
    std::string dB = raw10.substr((std::min)(b, raw10.size()), s10.sizeB);
    dB.resize(s10.sizeB, '\0');
    d.blockB = blockB30(dB, blocks);
    // its slots hold texture pointers the engine fills (FUN_0045ce20): zero in all 836 3.0 shaders
    std::fill(d.blockB.begin(), d.blockB.begin() + (std::min)(d.blockB.size(), kSlot30Bytes * blocks.slots), '\0');
    return d;
}

}  // namespace detail

// gen30.generate: a 1.0 shader -> its 3.0 .shd and the programs' HLSL (compiled when in.compile is set)
inline bool generate(const rentable::ShaderRules& rules, const Sources& in, const std::string& shader, Generated& g)
{
    std::string raw10;
    if (!in.read10("shaders/shaders/" + shader + ".shd", raw10)) {
        g.problems.push_back("no 1.0 shader " + shader);
        return false;
    }
    Shd10 s10;
    if (!parse10(raw10, s10)) {
        g.problems.push_back("1.0 shader unreadable");
        return false;
    }
    const Layout blocks = layout(s10);
    detail::ShaderJob sj{rules, in, shader, s10, blocks, {}};
    for (auto& t : s10.techs)
        for (int i = t.first; i < t.first + t.count; ++i) sj.techOf[i] = t.hash;
    std::vector<shd::Pass> passes;
    for (size_t i = 0; i < s10.passes.size(); ++i) passes.push_back(detail::generatePass(sj, i, g));
    detail::setTextureMasks(s10, g.programs, passes);
    g.shd = shd::write(detail::shader30(raw10, s10, blocks, std::move(passes)));
    return true;
}

}  // namespace gen30
