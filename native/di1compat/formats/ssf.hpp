// Scene settings (ssf.py): a game's scenesettingsformat.ssf read; an SSET block re-laid between formats by name.
// Any thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"
#include "bndl.hpp"
#include "shd.hpp"

namespace ssf {

// Global blocks A and B, then a block B per post pass (3.0 FUN_0045d420, 1.0 FUN_004af580; 576 chunks checked)

// Where a game's .ssf keeps its tables, after u8 parameter and pass counts
struct Layout {
    int header;                 // bytes before the parameter tables
    int blockA, blockB;         // global block descriptors: u8 params in A, u8 slots, u16 size
    int passPointer;            // bytes after each pass name hash
    int slotStride;             // bytes per texture slot in a block
    shd::Layout pass;           // each post pass is an embedded .shd
    bool shaderNameAfterPass;   // a NUL-terminated shader name follows each pass's blocks
};

inline constexpr Layout kLayout30 = {0xa8, 0x38, 0x70, 8, 8, shd::kLayout30, false};

// A block's part: its offset and size in the block, slot names, parameters (name hash -> offset, size), defaults
struct Part {
    int off = 0, size = 0;
    std::vector<uint32_t> slots;
    std::map<uint32_t, std::pair<int, int>> params;
    std::string def;
};
struct Format {
    int stride = 8, size = 0;
    std::vector<std::pair<uint32_t, Part>> parts;   // key: 1 = global A, 2 = global B, else the pass name hash
    const Part* find(uint32_t k) const
    {
        for (auto& p : parts)
            if (p.first == k) return &p.second;
        return nullptr;
    }
};

namespace detail {

constexpr uint32_t kGlobalA = 1, kGlobalB = 2;

// One part's tables in an .ssf: parameter names and words [from, to), slot names
struct Tables { const uint8_t *names, *words, *slots; int from, to, nslots; };

// Params sized up to the next one, slot names, defaults. false: a slot or param past the block (RE-23)
inline bool addPart(const std::string& s, Format& f, uint32_t key, int size, const Tables& t, size_t def)
{
    Part p;
    p.size = size;
    std::vector<std::pair<int, uint32_t>> ps;
    for (int i = t.from; i < t.to; ++i)
        ps.push_back({(int)(bytes::u32At(t.words + 4 * i) >> 16), bytes::u32At(t.names + 4 * i)});
    std::sort(ps.begin(), ps.end());
    const bool paramsIn = ps.empty() || ps.back().first <= size;
    if (!paramsIn || (size_t)t.nslots * ((size_t)f.stride + 1) > (size_t)size) return false;
    for (size_t i = 0; i < ps.size(); ++i)
        p.params[ps[i].second] = {ps[i].first, (i + 1 < ps.size() ? ps[i + 1].first : size) - ps[i].first};
    for (int i = 0; i < t.nslots; ++i) p.slots.push_back(bytes::u32At(t.slots + 4 * i));
    p.def = s.substr((std::min)(def, s.size()), size);
    p.off = f.size;
    f.size = (int)bytes::alignUp((size_t)f.size + size, 16);
    f.parts.push_back({key, p});
    return true;
}

// One post pass at o (an embedded .shd); o moves past it. false: it runs past the file
inline bool addPass(const std::string& s, const Layout& L, Format& f, uint32_t key, size_t& o)
{
    const uint8_t* b = (const uint8_t*)s.data();
    const shd::Layout& P = L.pass;
    o = bytes::alignUp(o, 16);
    if (o + P.data > s.size()) return false;
    const uint8_t* h = b + o;
    const int pn = h[P.counts], pp = h[P.counts + 1], pt = h[P.counts + 2];
    const int pnA = h[P.blockA], psA = h[P.blockA + 1], psB = h[P.blockB + 1];
    const int pSizeA = (int)bytes::u16At(h + P.blockA + 2), pSizeB = (int)bytes::u16At(h + P.blockB + 2);
    size_t q = bytes::alignUp(o + P.data + (size_t)pp * P.pass + 2 * (size_t)pt, 4);
    const size_t tables = 8 * (size_t)pn + 4 * (size_t)pt + 4 * ((size_t)psA + psB);
    if (q + tables > s.size()) return false;
    const uint8_t *pnm = b + q, *pw = b + q + 4 * pn + 4 * pt, *pslots = pw + 4 * pn + 4 * psA;   // block B slots
    o = q + tables;
    size_t pdA = bytes::alignUp(o, 16), pdB = bytes::alignUp(pdA + pSizeA, 16);
    if (!addPart(s, f, key, pSizeB, {pnm, pw, pslots, pnA, pn, psB}, pdB)) return false;
    o = pdB + pSizeB;
    if (L.shaderNameAfterPass) {
        while (o < s.size() && b[o]) ++o;
        ++o;
    }
    return o <= s.size();
}

}  // namespace detail

inline bool parse(const std::string& s, const Layout& L, Format& f)
{
    const uint8_t* b = (const uint8_t*)s.data();
    const size_t n = s.size();
    const int ha = L.blockA, hb = L.blockB;
    if (n < (size_t)L.header) return false;
    f.stride = L.slotStride;
    int npar = b[0], npass = b[1], nA = b[ha];
    size_t o = L.header;
    const size_t tables = 8 * (size_t)npar + 4 * ((size_t)b[ha + 1] + b[hb + 1]);
    if (nA > npar || o + tables > n) return false;
    const uint8_t *names = b + o, *words = b + o + 4 * npar, *snA = words + 4 * npar, *snB = snA + 4 * b[ha + 1];
    int sizeA = (int)bytes::u16At(b + ha + 2), sizeB = (int)bytes::u16At(b + hb + 2);
    o += tables;
    size_t defA = bytes::alignUp(o, 16), defB = bytes::alignUp(defA + sizeA, 16);
    if (!detail::addPart(s, f, detail::kGlobalA, sizeA, {names, words, snA, 0, nA, b[ha + 1]}, defA)) return false;
    if (!detail::addPart(s, f, detail::kGlobalB, sizeB, {names, words, snB, nA, npar, b[hb + 1]}, defB)) return false;
    o = defB + sizeB;
    const size_t passNames = 4 * (size_t)npass + L.passPointer * (size_t)npass;
    if (o + passNames > n) return false;
    const uint8_t* pnames = b + o;
    o += passNames;
    for (int i = 0; i < npass; ++i)
        if (!detail::addPass(s, L, f, bytes::u32At(pnames + 4 * i), o)) return false;
    return true;
}

namespace detail {

// One block being re-laid from format from into out, in format to
struct Relay {
    const Format& from;
    const Format& to;
    const std::string& block;
    const std::function<uint32_t(uint32_t)>& tex;   // a texture index in to's bundle
    const std::map<uint32_t, uint32_t>& renamed;    // from's slot names that to spells differently
    std::string& out;
};

// Part p's texture slots filled from part q's of the same name; the slot's flag byte with it
inline void relaySlots(const Relay& r, const Part& p, const Part& q)
{
    for (size_t i = 0; i < p.slots.size(); ++i)
        for (size_t j = 0; j < q.slots.size(); ++j) {
            auto rn = r.renamed.find(q.slots[j]);
            if ((rn != r.renamed.end() ? rn->second : q.slots[j]) != p.slots[i]) continue;
            uint32_t t = bytes::u32At((const uint8_t*)r.block.data() + q.off + r.from.stride * j);
            uint32_t w[2] = {t == bndl::kUnset ? bndl::kUnset : r.tex(t), 0};
            if (w[0] == bndl::kUnset) w[1] = bndl::kUnset;
            memcpy(&r.out[p.off + r.to.stride * i], w, r.to.stride);
            r.out[p.off + r.to.stride * p.slots.size() + i] = r.block[q.off + r.from.stride * q.slots.size() + j];
            break;
        }
}

}  // namespace detail

// A block of format from in format to: to's defaults, slots unset, then from's parameters and texture slots by name
inline std::string relayBlock(const Format& from, const Format& to, const std::string& block,
                              const std::function<uint32_t(uint32_t)>& tex, const std::map<uint32_t, uint32_t>& renamed)
{
    std::string out((size_t)to.size, '\0');
    const detail::Relay r{from, to, block, tex, renamed, out};
    for (auto& [k, p] : to.parts) {
        const size_t n = (std::min)(p.def.size(), (size_t)p.size);
        out.replace(p.off, n, p.def, 0, n);
        std::fill(out.begin() + p.off, out.begin() + p.off + to.stride * p.slots.size(), '\xff');
    }
    for (auto& [k, p] : to.parts) {
        const Part* q = from.find(k);
        if (!q || (size_t)q->off + q->size > block.size()) continue;
        for (auto& [h, at] : p.params) {
            auto x = q->params.find(h);
            if (x == q->params.end()) continue;
            int len = (std::min)(at.second, x->second.second);
            if (len > 0) out.replace(p.off + at.first, len, block, q->off + x->second.first, len);
        }
        detail::relaySlots(r, p, *q);
    }
    return out;
}

}  // namespace ssf
