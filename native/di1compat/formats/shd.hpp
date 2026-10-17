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
constexpr uint32_t kBlendOpaqueLast = 1;   // blend values 0 and 1 draw unblended (gen30.py build_shd)

// Fields within Layout::counts and within a block descriptor
constexpr size_t kCountParams = 0, kCountPasses = 1, kCountTechs = 2;
constexpr size_t kBlockParams = 0, kBlockSlots = 1, kBlockSize = 2;
constexpr size_t kNameAlign = 4, kBlockAlign = 16;   // the name tables and the default blocks start aligned

// Both games' packing (shd.py): a technique's u16 pass range, a parameter's u32 word
inline int rangeFirst(uint16_t r) { return r & 0xff; }
inline int rangeCount(uint16_t r) { return r >> 8; }
inline uint16_t techRange(int first, int count) { return (uint16_t)(first | (count << 8)); }
inline int wordType(uint32_t w) { return (int)(w & 0xffff); }
inline int wordOffset(uint32_t w) { return (int)(w >> 16); }
inline uint32_t paramWord(uint32_t type, uint32_t offset) { return type | (offset << 16); }

// A 3.0 .shd's block A: parameter offsets by name hash, size, and the shader's default block (shd.parse(b, '3.0')).
struct BlockA { std::map<uint32_t, int> off; int sizeA = 0; std::string defA; };
// false: s may be partly written
inline bool parseBlockA(const std::string& b, BlockA& s)
{
    const Layout& L = kLayout30;
    if (b.size() < L.data) return false;
    const int npar = (uint8_t)b[L.counts + kCountParams], npass = (uint8_t)b[L.counts + kCountPasses];
    const int ntech = (uint8_t)b[L.counts + kCountTechs], nA = (uint8_t)b[L.blockA + kBlockParams];
    const size_t slots = (size_t)(uint8_t)b[L.blockA + kBlockSlots] + (uint8_t)b[L.blockB + kBlockSlots];
    const size_t names = bytes::alignUp(L.data + L.pass * npass + sizeof(uint16_t) * ntech, kNameAlign);
    const size_t words = names + sizeof(uint32_t) * ((size_t)npar + ntech);
    const size_t o = bytes::alignUp(words + sizeof(uint32_t) * ((size_t)npar + slots), kBlockAlign);
    s.sizeA = bytes::u16OrZero(b, L.blockA + kBlockSize);
    if (!bytes::fits(b.size(), o, (size_t)s.sizeA)) return false;
    for (int i = 0; i < npar && i < nA; ++i) {
        const uint32_t word = bytes::u32OrZero(b, words + sizeof(uint32_t) * i);
        s.off[bytes::u32OrZero(b, names + sizeof(uint32_t) * i)] = wordOffset(word);
    }
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
        if (blendField(p.word) > kBlendOpaqueLast) flags |= kFlagBlended;
        if (depthField(p.word) == kDepthFinal && xField(p.word) == kXLightBuffer) flags |= kFlagLightFinal;
    }
    return flags;
}

inline std::string header(const Shader& s)
{
    const Layout& L = kLayout30;
    std::string h(L.data, '\0');
    const int slots = (int)s.slots.size(), paramsB = (int)s.params.size() - s.paramsA;
    h[L.counts + kCountParams] = (char)s.params.size();
    h[L.counts + kCountPasses] = (char)s.passes.size();
    h[L.counts + kCountTechs] = (char)s.techs.size();
    h[kSlotCount] = (char)slots;
    h[kFlags] = (char)passFlags(s.passes);
    if (s.paramsA) {
        bytes::put32(h, kFirstParamA, s.params[0].word);
        bytes::put32(h, kLastParamA, s.params[s.paramsA - 1].word);
    }
    memcpy(&h[kTechSlots], s.techSlots.data(), s.techSlots.size());
    h[L.blockA + kBlockParams] = (char)s.paramsA;
    bytes::put16(h, L.blockA + kBlockSize, (uint16_t)s.sizeA);
    h[kLastRegisterA] = (char)(std::max)(s.sizeA / kRegisterBytes - 1, 0);
    memset(&h[kStagesA], kNoStage, kStagesACount);
    h[L.blockB + kBlockParams] = (char)paramsB;
    h[L.blockB + kBlockSlots] = (char)slots;
    bytes::put16(h, L.blockB + kBlockSize, (uint16_t)s.sizeB);
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
        const uint16_t v = techRange(t.first, t.count);
        out.append((const char*)&v, sizeof v);
    }
    out.resize(bytes::alignUp(out.size(), kNameAlign), '\0');
    for (auto& p : s.params) detail::append32(out, p.hash);
    for (auto& t : s.techs) detail::append32(out, t.hash);
    for (auto& p : s.params) detail::append32(out, p.word);
    for (uint32_t x : s.slots) detail::append32(out, x);
    out.resize(bytes::alignUp(out.size(), kBlockAlign), '\0');
    out += s.defaultsA;
    out.resize(bytes::alignUp(out.size(), kBlockAlign), '\0');
    out += s.blockB;
    return out;
}

}  // namespace shd
