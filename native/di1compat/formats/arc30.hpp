// 3.0 archive members (FORMATS.md 2): hash index, AES-128-CTR, deflate, read from a Source. Any thread.
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../base/bytes.hpp"
#include "../third_party/miniz/miniz.h"
#pragma comment(lib, "bcrypt.lib")

namespace arc30 {

// An archive's bytes: its size, and readAt(offset, dst, n) -> true when all n bytes were read.
struct Source {
    uint64_t size;
    std::function<bool(uint64_t, void*, size_t)> readAt;
};

namespace detail {
inline uint32_t rotl(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }
inline uint32_t mm3Block(uint32_t k)
{
    k *= 0xcc9e2d51;
    k = rotl(k, 15);
    return k * 0x1b873593;
}
}  // namespace detail

// MurmurHash3_x86_32 of a member name, seed 0: the index key.
inline uint32_t mm3(const std::string& s)
{
    const uint8_t* d = (const uint8_t*)s.data();
    const size_t n = s.size();
    uint32_t h = 0;
    size_t i = 0;
    for (; bytes::fits(n, i, 4); i += 4) {
        h ^= detail::mm3Block(bytes::le(d + i, 4));
        h = detail::rotl(h, 13) * 5 + 0xe6546b64;
    }
    if (n & 3) h ^= detail::mm3Block(bytes::le(d + i, (int)(n - i)));
    h ^= (uint32_t)n;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

namespace detail {

// AES-128 ECB block encryptor (BCrypt) that makes the CTR keystream. BCrypt results are unchecked (CO-24).
struct Aes {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    explicit Aes(const uint8_t k[16])
    {
        BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
        BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, (PUCHAR)k, 16, 0);
    }
    ~Aes()
    {
        if (key) BCryptDestroyKey(key);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    Aes(const Aes&) = delete;
    Aes& operator=(const Aes&) = delete;
    // XOR b with the keystream from position at: a big-endian block counter from 0, all blocks in one ECB call.
    void x(uint8_t* b, size_t n, size_t at) const
    {
        if (!n) return;
        const uint64_t first = at / 16;
        const size_t blocks = (size_t)((at + n - 1) / 16 - first + 1);
        std::vector<uint8_t> ctr(16 * blocks), ks(16 * blocks);
        for (size_t k = 0; k < blocks; ++k)
            for (int j = 0; j < 8; ++j) ctr[16 * k + 15 - j] = (uint8_t)((first + k) >> (8 * j));
        ULONG got;
        BCryptEncrypt(key, ctr.data(), (ULONG)ctr.size(), nullptr, nullptr, 0, ks.data(), (ULONG)ks.size(), &got, 0);
        for (size_t i = 0; i < n; ++i) b[i] ^= ks[at % 16 + i];
    }
};

inline constexpr uint8_t kDefaultKey[16] = {0x68, 0x1B, 0xBE, 0xEA, 0x63, 0x16, 0x01, 0x88,
                                            0xF9, 0xB7, 0x94, 0x51, 0x04, 0xA5, 0x14, 0x99};
inline constexpr uint8_t kPsxKey[16] = {0x7D, 0xDD, 0x6D, 0x92, 0xF3, 0xA4, 0x6A, 0xBA,
                                        0xF0, 0x61, 0xEB, 0xC3, 0xC0, 0x1D, 0x7D, 0x88};

// The two keyed ciphers, made on first use (never under the loader lock); per-read keys crashed after ~10,000 reads.
struct State {
    std::once_flag made;
    std::unique_ptr<Aes> def, psx;   // set once by call_once in cipher()
    std::mutex lock;                 // guards every use of def and psx, and every member read (readMember)
};
inline State g_state;

inline const Aes& cipher(int which)
{
    std::call_once(g_state.made, [] {
        g_state.def = std::make_unique<Aes>(kDefaultKey);
        g_state.psx = std::make_unique<Aes>(kPsxKey);
    });
    return which == 1 ? *g_state.def : *g_state.psx;
}

// Decrypts in place with key which (0: the archive is plain).
struct Keystream {
    int which;
    void operator()(uint8_t* b, size_t n, size_t at) const
    {
        if (which) cipher(which).x(b, n, at);
    }
};

// The key turning the first 4 bytes into the index magic 'PK\xff\xff': 0 plain, 1 default, 2 PSX, -1 none.
inline int keyOf(const uint8_t hdr[8])
{
    for (int k = 0; k < 3; ++k) {
        uint8_t t[4];
        memcpy(t, hdr, 4);
        if (k) cipher(k).x(t, 4, 0);
        if (!memcmp(t, "PK\xff\xff", 4)) return k;
    }
    return -1;
}

inline constexpr size_t kLocalHeader = 30;     // a zip local file header
inline constexpr size_t kClearSpan = 0x200;    // encrypted data bytes per member; a .dct is encrypted whole

enum class Entry { Next, Failed, Read };
// The index entry at offset off for member: Next on a hash collision or an entry past the end.
inline Entry readEntry(const Source& src, const Keystream& dec, uint32_t off, const std::string& member,
                       std::string& out)
{
    if ((uint64_t)off + kLocalHeader > src.size) return Entry::Next;
    uint8_t lh[kLocalHeader];
    if (!src.readAt(off, lh, kLocalHeader)) return Entry::Failed;
    dec(lh, kLocalHeader, 0);
    const uint16_t method = (uint16_t)bytes::le(lh + 8, 2), nl = (uint16_t)bytes::le(lh + 26, 2);
    const uint16_t el = (uint16_t)bytes::le(lh + 28, 2);
    const uint32_t comp = bytes::le(lh + 18, 4), ucomp = bytes::le(lh + 22, 4);
    const size_t body = kLocalHeader + (size_t)nl + el;
    if ((uint64_t)off + body + comp > src.size) return Entry::Failed;
    std::vector<uint8_t> blk(body + comp);
    if (!src.readAt(off, blk.data(), blk.size())) return Entry::Failed;
    const bool dct = member.size() >= 4 && _stricmp(member.c_str() + member.size() - 4, ".dct") == 0;
    dec(blk.data(), body + (dct ? comp : (comp < kClearSpan ? comp : kClearSpan)), 0);
    if (std::string((const char*)blk.data() + kLocalHeader, nl) != member) return Entry::Next;   // a hash collision
    const uint8_t* data = blk.data() + body;
    if (method == 0) {
        out.assign((const char*)data, comp);
        return Entry::Read;
    }
    out.assign(ucomp, '\0');
    return tinfl_decompress_mem_to_mem(&out[0], ucomp, data, comp, 0) == ucomp ? Entry::Read : Entry::Failed;
}
}  // namespace detail

// One member by name; false if absent or unreadable. Reads the index and that member only (CO-13 left open).
inline bool readMember(const Source& src, const std::string& member, std::string& out)
{
    uint8_t hdr[8];
    if (src.size < 8 || !src.readAt(0, hdr, 8)) return false;
    std::lock_guard<std::mutex> lock(detail::g_state.lock);
    const detail::Keystream dec{detail::keyOf(hdr)};
    if (dec.which < 0) return false;
    dec(hdr, 8, 0);
    const uint32_t count = bytes::le(hdr + 4, 4);   // 'PK\xff\xff', count, count x (mm3(name), offset)
    if (8 + 8ull * count > src.size) return false;
    std::vector<uint8_t> idx(8 * (size_t)count);
    if (!src.readAt(8, idx.data(), idx.size())) return false;
    dec(idx.data(), idx.size(), 8);
    const uint32_t want = mm3(member);
    for (uint32_t i = 0; i < count; ++i) {
        if (bytes::le(&idx[8 * i], 4) != want) continue;
        const detail::Entry e = detail::readEntry(src, dec, bytes::le(&idx[8 * i + 4], 4), member, out);
        if (e != detail::Entry::Next) return e == detail::Entry::Read;
    }
    return false;
}

}  // namespace arc30
