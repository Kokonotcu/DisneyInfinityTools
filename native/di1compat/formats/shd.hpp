// Shader descriptions (.shd, shd.py): each game's field layout; 3.0's block A read and description written.
// Base only; any thread.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "../base/bytes.hpp"

namespace shd {

// Where one game's .shd keeps its fields (shd.py LAYOUT)
struct Layout {
    size_t counts;                        // u8 parameters, passes, techniques
    size_t data;                          // the pass records' start
    size_t pass;                          // a pass record's bytes
    size_t passVformat, passVs, passPs;   // in a pass record: vformatCRC, VS id, PS id
    size_t blockA, blockB;                // block descriptors: u8 parameters in A, u8 slots, u16 size
};

// 3.0: list loader FUN_00476180, finalize FUN_0045cf40
inline constexpr Layout kLayout30 = {0x18, 0xE0, 0x50, 0x18, 0x28, 0x30, 0x68, 0xA0};

constexpr int kRegisterBytes = 16;      // one float4 constant register
constexpr int kFirstSlotRegister = 5;   // material slot i is t(5 + i) in 821 of 836 shaders (FORMATS.md 7.1)
constexpr uint32_t kConstantBuffers = 5;   // the engine binds b0..b4 (gen30.py globals30, RDEF)
inline constexpr const char* kConstantBufferNames[kConstantBuffers] = {"perFrame", "perView", "perDraw",
                                                                       "perMaterial", "perInstance"};

// 3.0 pass word fields (gen30.py: depth 3..5, blend 6..9, cull 10..11, X 12..15, Y 16..19)
inline uint32_t depthField(uint32_t w) { return (w >> 3) & 7; }
inline uint32_t blendField(uint32_t w) { return (w >> 6) & 0xf; }
inline uint32_t xField(uint32_t w) { return (w >> 12) & 0xf; }
constexpr uint32_t kDepthFinal = 0, kDepthOnly = 1, kDepthGBuffer = 2;
constexpr uint32_t kXLightBuffer = 3;   // with kDepthFinal: a light-buffer final pass

// A 3.0 .shd's block A: parameter offsets by name hash, size, and the shader's default block (shd.parse(b, '3.0')).
struct BlockA { std::map<uint32_t, int> off; int sizeA = 0; std::string defA; };
inline bool parseBlockA(const std::string& b, BlockA& s)
{
    const Layout& L = kLayout30;
    if (b.size() < L.data) return false;
    const int npar = (uint8_t)b[L.counts], npass = (uint8_t)b[L.counts + 1], ntech = (uint8_t)b[L.counts + 2];
    const int nA = (uint8_t)b[L.blockA];
    size_t o = bytes::alignUp(L.data + L.pass * (size_t)npass + 2 * (size_t)ntech, 4);
    const size_t names = o, words = o + 4 * ((size_t)npar + ntech);
    o = words + 4 * (size_t)npar + 4 * ((size_t)(uint8_t)b[L.blockA + 1] + (uint8_t)b[L.blockB + 1]);
    o = bytes::alignUp(o, 16);
    s.sizeA = bytes::u16OrZero(b, L.blockA + 2);
    if (o + s.sizeA > b.size()) return false;
    for (int i = 0; i < npar && i < nA; ++i)
        s.off[bytes::u32OrZero(b, names + 4 * i)] = (int)(bytes::u32OrZero(b, words + 4 * i) >> 16);
    s.defA = b.substr(o, s.sizeA);
    return true;
}

// ---- writing a 3.0 description ----
struct Pass {
    uint32_t word, vformat, vs, ps, vsSize, psSize;
    uint16_t textures;   // texture registers the pass binds, bit n = tn
};
struct Tech { uint32_t hash; int first, count; };
struct Param { uint32_t hash, word; };   // word: type | offset in its block << 16
struct Shader {
    std::vector<Pass> passes;
    std::vector<Tech> techs;
    std::vector<Param> params;            // block A's first
    int paramsA = 0;
    std::vector<uint32_t> slots;          // block B's texture slot names; block A has none
    std::array<char, 16> techSlots{};     // the technique slot table
    int sizeA = 0, sizeB = 0, firstB = 0;   // block sizes; block B's first parameter register
    std::string defaultsA, blockB;        // the template blocks, written as they are
};

namespace detail {

// Header fields past the counts and block descriptors (gen30.py build_shd)
constexpr size_t kSlotCount = 0x1b;                          // u8, after the three counts
constexpr size_t kFlags = 0x48;                              // u8
constexpr uint8_t kFlagBlended = 0x4, kFlagLightFinal = 0x40;
constexpr size_t kFirstParamA = 0x4c, kLastParamA = 0x50;    // u32 words of block A's first and last parameter
constexpr size_t kTechSlots = 0x54;
constexpr size_t kLastRegisterA = 0x90;                      // u8
constexpr size_t kStagesA = 0x94, kStagesACount = 6;         // stage bytes, kNoStage in every generated shader
constexpr size_t kFirstRegisterB = 0xc4, kRegistersB = 0xc8, kSlotsB = 0xca;   // u8 each
constexpr size_t kPsStage = 0xcc, kOtherStages = 5;          // PS stage: first t-register | slots read << 4
constexpr uint8_t kNoStage = 0x0f;
constexpr size_t kFixedOne = 0xd9, kFixedFf = 0xda;          // 1 and 0xff as gen30.py writes them; not decoded
// Pass record fields past the layout's
constexpr size_t kPassWord = 0, kPassTextures = 0x1c, kPassVsSize = 0x40, kPassPsSize = 0x44;

inline uint8_t passFlags(const std::vector<Pass>& passes)
{
    uint8_t flags = 0;
    for (auto& p : passes) {
        if (blendField(p.word) > 1) flags |= kFlagBlended;
        if (depthField(p.word) == kDepthFinal && xField(p.word) == kXLightBuffer) flags |= kFlagLightFinal;
    }
    return flags;
}

inline std::string header(const Shader& s)
{
    const Layout& L = kLayout30;
    std::string h(L.data, '\0');
    const int slots = (int)s.slots.size(), paramsB = (int)s.params.size() - s.paramsA;
    h[L.counts] = (char)s.params.size();
    h[L.counts + 1] = (char)s.passes.size();
    h[L.counts + 2] = (char)s.techs.size();
    h[kSlotCount] = (char)slots;
    h[kFlags] = (char)passFlags(s.passes);
    if (s.paramsA) {
        bytes::put32(h, kFirstParamA, s.params[0].word);
        bytes::put32(h, kLastParamA, s.params[s.paramsA - 1].word);
    }
    memcpy(&h[kTechSlots], s.techSlots.data(), s.techSlots.size());
    h[L.blockA] = (char)s.paramsA;
    bytes::put16(h, L.blockA + 2, (uint16_t)s.sizeA);
    h[kLastRegisterA] = (char)(std::max)(s.sizeA / kRegisterBytes - 1, 0);
    memset(&h[kStagesA], kNoStage, kStagesACount);
    h[L.blockB] = (char)paramsB;
    h[L.blockB + 1] = (char)slots;
    bytes::put16(h, L.blockB + 2, (uint16_t)s.sizeB);
    h[kFirstRegisterB] = (char)s.firstB;
    h[kRegistersB] = (char)(std::max)(s.sizeB / kRegisterBytes - s.firstB, 0);
    h[kSlotsB] = (char)slots;
    h[kPsStage] = (char)(slots ? (kFirstSlotRegister | (slots << 4)) : kNoStage);
    memset(&h[kPsStage + 1], kNoStage, kOtherStages);
    h[kFixedOne] = 1;
    h[kFixedFf] = '\xff';
    return h;
}

inline std::string passRecord(const Pass& p)
{
    const Layout& L = kLayout30;
    std::string r(L.pass, '\0');
    bytes::put32(r, kPassWord, p.word);
    bytes::put32(r, L.passVformat, p.vformat);
    bytes::put16(r, kPassTextures, p.textures);
    bytes::put32(r, L.passVs, p.vs);
    bytes::put32(r, L.passPs, p.ps);
    bytes::put32(r, kPassVsSize, p.vsSize);
    bytes::put32(r, kPassPsSize, p.psSize);
    return r;
}

inline void append32(std::string& out, uint32_t v) { out.append((const char*)&v, sizeof v); }

}  // namespace detail

// Header, pass records, techniques, parameter and technique names, parameter words, slot names, then the blocks
inline std::string write(const Shader& s)
{
    std::string out = detail::header(s);
    for (const Pass& p : s.passes) out += detail::passRecord(p);
    for (auto& t : s.techs) {
        const uint16_t v = (uint16_t)(t.first | (t.count << 8));
        out.append((const char*)&v, sizeof v);
    }
    out.resize(bytes::alignUp(out.size(), 4), '\0');
    for (auto& p : s.params) detail::append32(out, p.hash);
    for (auto& t : s.techs) detail::append32(out, t.hash);
    for (auto& p : s.params) detail::append32(out, p.word);
    for (uint32_t x : s.slots) detail::append32(out, x);
    out.resize(bytes::alignUp(out.size(), 16), '\0');
    out += s.defaultsA;
    out.resize(bytes::alignUp(out.size(), 16), '\0');
    out += s.blockB;
    return out;
}

}  // namespace shd
