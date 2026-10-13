// Read-only 3.0 archive member reader (port of DI3Tools di3arc.py read path; FORMATS.md section 2): the hash index
// 'PK\xff\xff' + count + count x (MurmurHash3_x86_32(name), offset), AES-128-CTR (big-endian counter from 0; the
// keystream restarts at each member's offset and covers its local header, name, extra and the first 0x200 data bytes,
// all of a .dct), deflate. Keys: none / default / PSX, detected from the header like the engine.
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "third_party/miniz/miniz.h"
#pragma comment(lib, "bcrypt.lib")

namespace arc30 {

inline uint32_t mm3(const std::string& s)
{
    const uint8_t* d = (const uint8_t*)s.data();
    size_t n = s.size();
    uint32_t h = 0, c1 = 0xcc9e2d51, c2 = 0x1b873593;
    auto rotl = [](uint32_t x, int r) { return (x << r) | (x >> (32 - r)); };
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t k = d[i] | d[i + 1] << 8 | d[i + 2] << 16 | (uint32_t)d[i + 3] << 24;
        k *= c1; k = rotl(k, 15); k *= c2;
        h ^= k; h = rotl(h, 13) * 5 + 0xe6546b64;
    }
    uint32_t k = 0;
    for (size_t j = 0; i + j < n; ++j) k |= (uint32_t)d[i + j] << (8 * j);
    if (n & 3) { k *= c1; k = rotl(k, 15); k *= c2; h ^= k; }
    h ^= (uint32_t)n;
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    return h;
}

// AES-128 ECB block encryptor (BCrypt) used to make the CTR keystream.
struct Aes {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    explicit Aes(const uint8_t k[16])
    {
        BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
        BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, (PUCHAR)k, 16, 0);
    }
    ~Aes() { if (key) BCryptDestroyKey(key); if (alg) BCryptCloseAlgorithmProvider(alg, 0); }
    // XOR b with the keystream starting at position `at`.
    void x(uint8_t* b, size_t n, size_t at) const
    {
        uint8_t ctr[16], ks[16];
        ULONG got;
        for (size_t i = 0; i < n;) {
            uint64_t blk = (at + i) / 16;
            size_t off = (at + i) % 16;
            memset(ctr, 0, 8);
            for (int j = 0; j < 8; ++j) ctr[15 - j] = (uint8_t)(blk >> (8 * j));
            BCryptEncrypt(key, ctr, 16, nullptr, nullptr, 0, ks, 16, &got, 0);
            for (; off < 16 && i < n; ++off, ++i) b[i] ^= ks[off];
        }
    }
};

inline const uint8_t* keyBytes(int which)
{
    static const uint8_t def[16] = {0x68, 0x1B, 0xBE, 0xEA, 0x63, 0x16, 0x01, 0x88, 0xF9, 0xB7, 0x94, 0x51, 0x04, 0xA5, 0x14, 0x99};
    static const uint8_t psx[16] = {0x7D, 0xDD, 0x6D, 0x92, 0xF3, 0xA4, 0x6A, 0xBA, 0xF0, 0x61, 0xEB, 0xC3, 0xC0, 0x1D, 0x7D, 0x88};
    return which == 1 ? def : psx;
}

// The two keyed ciphers, made once (opening a BCrypt provider and key per read crashed after ~10,000 reads) and
// used under one lock.
inline const Aes& cipher(int which)
{
    static Aes def(keyBytes(1)), psx(keyBytes(2));
    return which == 1 ? def : psx;
}
inline std::mutex& cipherLock() { static std::mutex m; return m; }

// n bytes at file offset `at` (positional; the handle's file pointer is not used).
inline bool readAt(HANDLE h, uint64_t at, void* dst, size_t n)
{
    OVERLAPPED o{};
    o.Offset = (DWORD)at;
    o.OffsetHigh = (DWORD)(at >> 32);
    DWORD got = 0;
    return ReadFile(h, dst, (DWORD)n, &got, &o) && got == n;
}

// One member of the archive at `archivePath`, by name; false if absent or unreadable. Reads the index and that
// member only (reading whole archives per member cost in_city's render ~0.3 s over 92 donor reads).
inline bool readMember(const std::string& archivePath, const std::string& member, std::string& out)
{
    HANDLE h = CreateFileA(archivePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{h};
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    const uint64_t size = (uint64_t)sz.QuadPart;
    uint8_t hdr[8];
    if (size < 8 || !readAt(h, 0, hdr, 8)) return false;
    std::lock_guard<std::mutex> lock(cipherLock());
    // key: the one that turns the first 4 bytes into the index magic
    int which = -1;
    for (int k = 0; k < 3 && which < 0; ++k) {
        uint8_t t[4];
        memcpy(t, hdr, 4);
        if (k) cipher(k).x(t, 4, 0);
        if (!memcmp(t, "PK\xff\xff", 4)) which = k;
    }
    if (which < 0) return false;
    auto dec = [&](uint8_t* b, size_t n, size_t at) { if (which) cipher(which).x(b, n, at); };
    dec(hdr, 8, 0);
    uint32_t count = hdr[4] | hdr[5] << 8 | hdr[6] << 16 | (uint32_t)hdr[7] << 24;
    if (8 + 8ull * count > size) return false;
    std::vector<uint8_t> idx(8 * (size_t)count);
    if (!readAt(h, 8, idx.data(), idx.size())) return false;
    dec(idx.data(), idx.size(), 8);
    uint32_t want = mm3(member);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t hh, off;
        memcpy(&hh, &idx[8 * i], 4);
        memcpy(&off, &idx[8 * i + 4], 4);
        if (hh != want || (uint64_t)off + 30 > size) continue;
        uint8_t lh[30];
        if (!readAt(h, off, lh, 30)) return false;
        dec(lh, 30, 0);
        uint16_t method = lh[8] | lh[9] << 8, nl = lh[26] | lh[27] << 8, el = lh[28] | lh[29] << 8;
        uint32_t comp, ucomp;
        memcpy(&comp, lh + 18, 4);
        memcpy(&ucomp, lh + 22, 4);
        size_t body = 30 + (size_t)nl + el;
        if ((uint64_t)off + body + comp > size) return false;
        std::vector<uint8_t> blk(body + comp);
        if (!readAt(h, off, blk.data(), blk.size())) return false;
        bool dct = member.size() >= 4 && _stricmp(member.c_str() + member.size() - 4, ".dct") == 0;
        size_t span = body + (dct ? comp : (comp < 0x200 ? comp : 0x200));
        dec(blk.data(), span, 0);
        if (std::string((const char*)blk.data() + 30, nl) != member) continue;   // hash collision
        const uint8_t* data = blk.data() + body;
        if (method == 0) { out.assign((const char*)data, comp); return true; }
        out.assign(ucomp, '\0');
        size_t n = tinfl_decompress_mem_to_mem(&out[0], ucomp, data, comp, 0);
        return n == ucomp;
    }
    return false;
}

}  // namespace arc30
