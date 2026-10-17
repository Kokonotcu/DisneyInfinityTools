// Particle packages (.particledata) in 3.0's form: index entries, trailer, the package assembled (particles.py).
// Base only; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "../base/bytes.hpp"

namespace particledata {

// [records][curve keys][emitter, effect, param index tables][trailer]; the engine uses the records in place
constexpr size_t kKeyBytes = 8;            // one curve key, one decode in both games (FUN_006c7210, FUN_00662d20)
constexpr size_t kEmitterEntry = 16;       // emitter index entry (FUN_00672120)
constexpr size_t kEntry = 8;               // effect and param index entries: record offset, name hash
constexpr size_t kEmitterRecord = 0x124;   // an emitter record before its channel keys, u16 each
constexpr size_t kChannelCount = 0x121;    // u8 in an emitter record
constexpr size_t kParamRecord = 0x34;
constexpr size_t kTrailer = 0x18;          // FUN_00656bd0 rejects any other trailer
constexpr uint16_t kMagic = 0x123, kVersion = 0x2f;

// The constant curve key (cubic 1, 1, 1, 1, linear time, clamp 0..1): Disney's key for channels 1.0 lacked
inline constexpr char kConstantKey[kKeyBytes + 1] = "\xff\x1f\x1f\xff\x55\xaa\x00\xff";

// An emitter's index entry: record offset, name hash, u8 class, u16 material, u32 emitter type
struct EmitterEntry { uint32_t off, hash; uint8_t cls; uint16_t material; uint32_t type; };

namespace detail {

inline void append16(std::string& s, uint16_t v) { s.append((const char*)&v, sizeof v); }
inline void append32(std::string& s, uint32_t v) { s.append((const char*)&v, sizeof v); }

}  // namespace detail

// Entry j of an emitter index table
inline void putEmitterEntry(std::string& table, size_t j, const EmitterEntry& e)
{
    std::string x;
    detail::append32(x, e.off);
    detail::append32(x, e.hash);
    x.push_back((char)e.cls);
    x.push_back('\0');
    detail::append16(x, e.material);
    detail::append32(x, e.type);
    table.replace(kEmitterEntry * j, kEmitterEntry, x);
}

// Entry j of an effect or param index table
inline void putEntry(std::string& table, size_t j, uint32_t off, uint32_t hash)
{
    bytes::put32(table, kEntry * j, off);
    bytes::put32(table, kEntry * j + sizeof off, hash);
}

// A package: records then curve keys in body (keys at offset keys), and the three index tables
struct Package {
    std::string body, emitters, effects, params;
    uint32_t keys = 0;
    uint16_t nkeys = 0;
};

// magic, version, the keys' offset twice, the key, emitter, effect and param counts; 3.0's two other kinds none
inline std::string trailer(const Package& p)
{
    std::string tr;
    detail::append16(tr, kMagic);
    detail::append16(tr, kVersion);
    detail::append32(tr, p.keys);
    detail::append32(tr, p.keys);
    detail::append16(tr, p.nkeys);
    detail::append16(tr, (uint16_t)(p.emitters.size() / kEmitterEntry));
    detail::append16(tr, (uint16_t)(p.effects.size() / kEntry));
    detail::append16(tr, (uint16_t)(p.params.size() / kEntry));
    tr.resize(kTrailer, '\0');
    return tr;
}

inline std::string write(const Package& p) { return p.body + p.emitters + p.effects + p.params + trailer(p); }

}  // namespace particledata
