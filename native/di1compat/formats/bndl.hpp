// Material bundles (.mtb, FORMATS.md 6.1): TEXB texture hashes, MATP materials, SSET scene settings; read and written.
// Base only; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../base/bytes.hpp"

namespace bndl {

// A texture slot's index word when the slot is empty (also in scene-settings blocks)
constexpr uint32_t kUnset = 0xffffffff;

struct Material { std::string name, shader, A, B; uint32_t hash = 0; int slots = 0, stride = 8; };
struct Bundle {
    std::vector<std::string> textures;                // 8-byte name hashes
    std::vector<Material> mats;
    std::vector<std::string> ssetNames, ssetBlocks;   // the SSET chunk: scene settings (ssf.py), a block per name
    static uint32_t slot(const Material& m, int s) { return bytes::u32At((const uint8_t*)m.B.data() + m.stride * s); }
};

// sizes(shader, A, B, slots): a shader's block sizes and texture slot count; false if unknown
using BlockSizes = std::function<bool(const std::string&, int&, int&, int&)>;

namespace detail {

// A chunk: tag, u32 version, u32 size (with this header), u32 count, then its data
constexpr size_t kHeader = 16, kSizeAt = 8, kCountAt = 12;
constexpr size_t kTexbFirst = 20, kTexbEntry = 12, kTexbHash = 8;   // hash, then u32 0xffffffff
constexpr size_t kSsetNames = 20;                                   // after the count, given twice
constexpr uint32_t kBndlVersion = 1, kTexbVersion = 3, kMatpVersion = 2, kSsetVersion = 0;

// The NUL-terminated string at p, which must end inside raw (RE-23); p moves past its NUL
inline bool cstrAt(const std::string& raw, size_t& p, std::string& out)
{
    const size_t e = p < raw.size() ? raw.find('\0', p) : std::string::npos;
    if (e == std::string::npos) return false;
    out.assign(raw, p, e - p);
    p = e + 1;
    return true;
}

// count elements of elem bytes at off lie inside size bytes
inline bool fitsArray(size_t size, size_t off, size_t count, size_t elem)
{
    return count <= size / elem && bytes::fits(size, off, count * elem);
}

inline bool fail(std::string& err, const std::string& why)
{
    err = why;
    return false;
}

inline uint32_t u32(const std::string& raw, size_t o) { return bytes::u32At((const uint8_t*)raw.data() + o); }

// TEXB at o: each 8-byte texture name hash must lie inside raw
inline bool parseTexb(const std::string& raw, size_t o, Bundle& out, std::string& err)
{
    const uint32_t cnt = u32(raw, o + kCountAt);
    if (cnt == 0) return true;
    const bool past = cnt - 1 > raw.size() / kTexbEntry;
    if (past || !bytes::fits(raw.size(), o + kTexbFirst, kTexbEntry * (size_t)(cnt - 1) + kTexbHash))
        return fail(err, "TEXB overrun");
    for (uint32_t i = 0; i < cnt; ++i)
        out.textures.emplace_back(raw, o + kTexbFirst + kTexbEntry * (size_t)i, kTexbHash);
    return true;
}

// MATP name tables at p: count, then count strings; p moves past them
inline bool matpNames(const std::string& raw, size_t& p, std::vector<std::string>& out)
{
    if (!bytes::fits(raw.size(), p, 4)) return false;
    const uint32_t n = u32(raw, p);
    p += 4;
    for (uint32_t i = 0; i < n; ++i) {
        out.emplace_back();
        if (!cstrAt(raw, p, out.back())) return false;
    }
    return true;
}

// A material's blocks, sized by its shader; its slots (m.stride bytes each) lie inside block B (Bundle::slot)
inline bool matpBlocks(const std::string& raw, size_t& p, const BlockSizes& sizes, Material& m, std::string& err)
{
    int A = 0, B = 0, slots = 0;
    if (!sizes(m.shader, A, B, slots)) return fail(err, "unknown shader " + m.shader);
    if (A < 0 || B < 0 || slots < 0) return fail(err, "MATP block sizes of shader " + m.shader);
    if (!bytes::fits(raw.size(), p, (size_t)A + (size_t)B)) return fail(err, "MATP overrun");
    if ((size_t)slots * (size_t)m.stride > (size_t)B) return fail(err, "MATP slots past block B of shader " + m.shader);
    m.A = raw.substr(p, A);
    m.B = raw.substr(p + A, B);
    m.slots = slots;
    p += (size_t)A + (size_t)B;
    return true;
}

// MATP at o: shader names, material names, shader indices, hashes, then each material's blocks A and B
inline bool parseMatp(const std::string& raw, size_t o, int stride, const BlockSizes& sizes, Bundle& out,
                      std::string& err)
{
    const uint8_t* b = (const uint8_t*)raw.data();
    const uint32_t nmat = u32(raw, o + kCountAt);
    size_t p = bytes::alignUp(o + kHeader, 4);
    std::vector<std::string> shaders, names;
    if (!matpNames(raw, p, shaders)) return fail(err, "MATP overrun");
    p = bytes::alignUp(p, 4);
    if (!bytes::fits(raw.size(), p, 4)) return fail(err, "MATP overrun");
    if (u32(raw, p) != nmat) return fail(err, "MATP count");
    if (!matpNames(raw, p, names)) return fail(err, "MATP overrun");
    const size_t pIdx = bytes::alignUp(p, 2), pHash = bytes::alignUp(pIdx + 2 * (size_t)nmat, 4);
    const bool tables = fitsArray(raw.size(), pIdx, nmat, 2) && fitsArray(raw.size(), pHash, nmat, 4);
    if (!tables) return fail(err, "MATP overrun");
    p = bytes::alignUp(pHash + 4 * (size_t)nmat, 16);
    for (uint32_t i = 0; i < nmat; ++i) {
        Material m;
        const uint32_t sidx = bytes::u16At(b + pIdx + 2 * (size_t)i);
        if (sidx >= shaders.size()) return fail(err, "MATP shader index");
        m.name = names[i];
        m.shader = shaders[sidx];
        m.hash = u32(raw, pHash + 4 * (size_t)i);
        m.stride = stride;
        if (!matpBlocks(raw, p, sizes, m, err)) return false;
        out.mats.push_back(m);
    }
    return true;
}

// SSET at o (csize bytes): names, then the 16-aligned blocks, equal in size
inline void parseSset(const std::string& raw, size_t o, size_t csize, Bundle& out)
{
    const uint32_t cnt = u32(raw, o + kCountAt);
    size_t p = o + kSsetNames;
    const size_t end = csize <= raw.size() - o ? o + csize : raw.size();
    for (uint32_t i = 0; i < cnt && p < end; ++i) {
        const size_t e = raw.find('\0', p);
        out.ssetNames.push_back(raw.substr(p, e == std::string::npos ? std::string::npos : e - p));
        p += out.ssetNames.back().size() + 1;
    }
    p = bytes::alignUp(p, 16);
    const size_t sz = cnt && end > p ? (end - p) / cnt : 0;
    for (uint32_t i = 0; i < cnt && sz; ++i) out.ssetBlocks.push_back(raw.substr(p + i * sz, sz));
}

inline void append32(std::string& s, uint32_t v) { s.append((const char*)&v, sizeof v); }

// A chunk's header: tag, version, size (0, set by finish), count
inline std::string chunk(const char* tag, uint32_t version, uint32_t count)
{
    std::string c = tag;
    append32(c, version);
    append32(c, 0);
    append32(c, count);
    return c;
}

inline void finish(std::string& c) { bytes::put32(c, kSizeAt, (uint32_t)c.size()); }

// TEXB, 16-aligned: 8-byte hash and u32 0xffffffff per texture
inline std::string texb(const std::vector<std::string>& textures)
{
    std::string c = chunk("TEXB", kTexbVersion, (uint32_t)textures.size());
    append32(c, 0);
    for (auto& t : textures) {
        c += t;
        append32(c, kUnset);
    }
    c.resize(bytes::alignUp(c.size(), 16), '\0');
    finish(c);
    return c;
}

// MATP at file offset base (alignment is file-relative)
inline std::string matp(const std::vector<Material>& mats, size_t base)
{
    std::vector<std::string> shaders;   // first use first
    std::map<std::string, uint16_t> index;
    for (auto& m : mats)
        if (index.emplace(m.shader, (uint16_t)shaders.size()).second) shaders.push_back(m.shader);
    std::string c = chunk("MATP", kMatpVersion, (uint32_t)mats.size());
    auto pad = [&](size_t a) { c.resize(bytes::alignUp(base + c.size(), a) - base, '\0'); };
    pad(4);
    append32(c, (uint32_t)shaders.size());
    for (auto& s : shaders) c.append(s.c_str(), s.size() + 1);
    pad(4);
    append32(c, (uint32_t)mats.size());
    for (auto& x : mats) c.append(x.name.c_str(), x.name.size() + 1);
    pad(2);
    for (auto& x : mats) c.append((const char*)&index.at(x.shader), sizeof(uint16_t));
    pad(4);
    for (auto& x : mats) append32(c, x.hash);
    pad(16);
    for (auto& x : mats) c += x.A + x.B;
    finish(c);
    return c;
}

// SSET at file offset at: count twice, names, blocks 16-aligned in the file (as 3.0 realms ship it)
inline std::string sset(const std::vector<std::string>& names, const std::vector<std::string>& blocks, size_t at)
{
    std::string c = chunk("SSET", kSsetVersion, (uint32_t)blocks.size());
    append32(c, (uint32_t)blocks.size());
    for (auto& nm : names) c.append(nm.c_str(), nm.size() + 1);
    c.resize(bytes::alignUp(at + c.size(), 16) - at, '\0');
    for (auto& blk : blocks) c += blk;
    c.resize(bytes::alignUp(at + c.size(), 16) - at, '\0');
    finish(c);
    return c;
}

}  // namespace detail

// stride: a slot's bytes, 8 (3.0) or 4 (1.0). false: err says why.
inline bool parse(const std::string& raw, Bundle& out, int stride, const BlockSizes& sizes, std::string* err)
{
    const uint8_t* b = (const uint8_t*)raw.data();
    if (raw.size() < detail::kHeader || memcmp(b, "BNDL", 4)) {
        if (err) *err = "not a BNDL";
        return false;
    }
    uint32_t n = detail::u32(raw, detail::kCountAt);
    size_t o = detail::kHeader;
    std::string why;
    for (uint32_t c = 0; c < n && bytes::fits(raw.size(), o, detail::kHeader); ++c) {
        uint32_t csize = detail::u32(raw, o + detail::kSizeAt);
        const bool sset = !memcmp(b + o, "SSET", 4) && bytes::fits(raw.size(), o, detail::kSsetNames);
        bool ok = true;
        if (!memcmp(b + o, "TEXB", 4)) ok = detail::parseTexb(raw, o, out, why);
        else if (!memcmp(b + o, "MATP", 4)) ok = detail::parseMatp(raw, o, stride, sizes, out, why);
        else if (sset) detail::parseSset(raw, o, csize, out);
        if (!ok) {
            if (err) *err = why;
            return false;
        }
        // a chunk past the end ends the walk, as the next bounds check would; o never wraps (A-26 form)
        if (!csize || csize > raw.size() - o) break;
        o += csize;
    }
    return true;
}

// matp.build: TEXB + MATP (+ SSET when given blocks)
inline std::string build(const std::vector<std::string>& textures, const std::vector<Material>& mats,
                         const std::vector<std::string>& ssetNames = {},
                         const std::vector<std::string>& ssetBlocks = {})
{
    const std::string texb = detail::texb(textures);
    std::string body = texb + detail::matp(mats, detail::kHeader + texb.size());
    if (!ssetBlocks.empty()) body += detail::sset(ssetNames, ssetBlocks, detail::kHeader + body.size());
    std::string out = detail::chunk("BNDL", detail::kBndlVersion, ssetBlocks.empty() ? 2 : 3);   // chunks
    bytes::put32(out, detail::kSizeAt, (uint32_t)(detail::kHeader + body.size()));
    return out + body;
}

}  // namespace bndl
