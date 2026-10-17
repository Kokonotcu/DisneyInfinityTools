// GLB-1: 1.0's var/chests/ChestInfo.bin as the Lua 3.0's chestInfo loader reads. luaconst. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../../formats/luaconst.hpp"

namespace di1::chestinfo {

// 1.0's .bin (ctor FUN_00faac30) as 3.0's ChestInfo = {Resource, Choreography, Chests} (callback FUN_014adef0).
inline bool chestInfoLua(const std::string& b, std::string& out, std::string* why = nullptr);

namespace detail {

constexpr uint32_t kNone = 0xffffffff;
constexpr size_t kStrings = 4;           // the string pool, after its size
constexpr size_t kBlockHead = 8;         // [size, n, records]
constexpr size_t kChoreography = 0x20;   // record sizes
constexpr size_t kResource = 0xc;
constexpr size_t kChest = 0x48;

inline bool fail(std::string* why, const char* w)
{
    if (why) *why = w;
    return false;
}

inline bool truthy(const std::string& s) { return s[0] == 't' || s[0] == 'T' || s[0] == '1'; }

// [u32 size, string pool] [u32 size, lists] then 3 record blocks; ok turns false on a read out of range.
struct Bin {
    const std::string& b;
    uint32_t s1 = 0, s2 = 0, s3 = 0, s4 = 0, nc = 0, nr = 0, nk = 0;
    size_t lists = 0, cho = 0, res = 0, ch = 0;
    bool ok = true;

    bool u32(size_t o, uint32_t& v) const
    {
        if (o >= b.size() || b.size() - o < 4) return false;
        memcpy(&v, b.data() + o, 4);
        return true;
    }
    int32_t field(size_t rec, int k)
    {
        uint32_t v = kNone;
        ok &= u32(rec + 4 * k, v);
        return (int32_t)v;
    }
    // A pool string may end in the next block's size word, 4 bytes looser than perceptions (SY-06).
    bool str(int32_t off, std::string& s)
    {
        if (off < 0) return false;
        size_t at = kStrings + off, e = at < s1 + 4 ? b.find('\0', at) : std::string::npos;
        if (e == std::string::npos || e > s1 + 4) {
            ok = false;
            return false;
        }
        s.assign(b, at, e - at);
        return true;
    }
    // A list: a flag word, then string offsets up to -1, joined with ','.
    bool list(int32_t idx, std::string& s)
    {
        if (idx < 0) return false;
        s.clear();
        for (size_t p = lists + 4 * (size_t)idx + 4;; p += 4) {
            uint32_t v;
            if (p >= s1 + 4 + s2 || !u32(p, v)) {
                ok = false;
                return false;
            }
            if (v == kNone) break;
            std::string t;
            if (!str((int32_t)v, t)) return false;
            s += (s.empty() ? "" : ",") + t;
        }
        return true;
    }
};

// The block headers and sizes; nullptr, or why they do not fit. 64-bit sums: no count wraps.
inline const char* parseBlocks(Bin& f)
{
    if (!f.u32(0, f.s1) || !f.u32(f.s1, f.s2) || !f.u32(f.s1 + f.s2, f.s3)) return "block headers";
    if (!f.u32(f.s1 + f.s2 + 4, f.nc) || !f.u32(f.s1 + f.s2 + f.s3, f.s4)) return "block headers";
    if (!f.u32(f.s1 + f.s2 + f.s3 + 4, f.nr) || !f.u32(f.s1 + f.s2 + f.s3 + f.s4 + 4, f.nk)) return "block headers";
    const uint64_t end = (uint64_t)f.s1 + f.s2 + f.s3 + f.s4 + kBlockHead + (uint64_t)f.nk * kChest;
    const bool choSized = f.s3 == kBlockHead + (uint64_t)f.nc * kChoreography;
    const bool resSized = f.s4 == kBlockHead + (uint64_t)f.nr * kResource;
    if (!choSized || !resSized || end > f.b.size()) return "record blocks";
    f.lists = f.s1 + 4;
    f.cho = f.s1 + f.s2 + kBlockHead;
    f.res = f.s1 + f.s2 + f.s3 + kBlockHead;
    f.ch = f.s1 + f.s2 + f.s3 + f.s4 + kBlockHead;
    return nullptr;
}

// One key per line under a record; kv quotes the value, kn writes it as it is.
struct Emit {
    Bin& f;
    std::string& out;

    void kv(const char* key, const std::string& v) { kn(key, luaconst::quoteAscii(v)); }
    void kn(const char* key, const std::string& v) { out += std::string("\t\t\t") + key + " = " + v + ",\n"; }
};

// Resource: [0] Currency, [1] list Locks, [2] a list empty in all 41 records that 3.0 does not read.
inline void resources(Emit& e)
{
    Bin& f = e.f;
    std::string s;
    for (uint32_t i = 0; i < f.nr; ++i) {
        size_t r = f.res + kResource * i;
        e.out += "\t\t[\"R" + std::to_string(i) + "\"] =\n\t\t{\n";
        if (f.str(f.field(r, 0), s)) e.kv("Currency", s);
        if (f.list(f.field(r, 1), s)) e.kv("Locks", s);
        e.out += "\t\t},\n";
    }
}

inline void choreographies(Emit& e)
{
    Bin& f = e.f;
    std::string s;
    for (uint32_t i = 0; i < f.nc; ++i) {
        size_t r = f.cho + kChoreography * i;
        e.out += "\t\t[\"C" + std::to_string(i) + "\"] =\n\t\t{\n";
        if (f.str(f.field(r, 0), s)) e.kn("PauseAtLoadTime", truthy(s) ? "1" : "0");
        if (f.str(f.field(r, 1), s)) e.kv("ChoName", s);
        if (f.str(f.field(r, 2), s)) e.kv("AnchorName", s);
        if (f.list(f.field(r, 3), s)) e.kv("LockLayer", s);
        if (f.list(f.field(r, 4), s)) e.kv("LockClips", s);
        if (f.str(f.field(r, 5), s)) e.kv("DoorLayer", s);
        if (f.str(f.field(r, 6), s)) e.kv("DoorOpen", s);
        if (f.str(f.field(r, 7), s)) {
            char n[32];
            snprintf(n, sizeof n, "%.9g", atof(s.c_str()));
            e.kn("SpawnAward", n);
        }
        e.out += "\t\t},\n";
    }
}

// Chest slots 10..17 after the record references were checked.
inline void chestTail(Emit& e, size_t r, int32_t open, int32_t reward)
{
    Bin& f = e.f;
    std::string s;
    if (f.str(f.field(r, 10), s)) e.kn("BitOffset", std::to_string(atoi(s.c_str())));
    if (f.str(f.field(r, 11), s)) e.kv("UnlockedStat", s);
    if (f.list(f.field(r, 12), s)) e.kv("UnlockSfx", s);
    if (f.str(f.field(r, 13), s)) e.kv("OpenSfx", s);
    if (f.str(f.field(r, 14), s)) e.kv("AwardSfx", s);
    if (f.list(f.field(r, 15), s)) e.kv("LockEntityName", s);
    if (open >= 0) e.kv("OpenCho", "C" + std::to_string(open));
    if (reward >= 0) e.kv("RewardCho", "C" + std::to_string(reward));
}

// A chest: resources and choreographies by record index, which 3.0 resolves by name (FUN_0148d4d0).
inline const char* chest(Emit& e, size_t r)
{
    Bin& f = e.f;
    std::string name, s;
    if (!f.str(f.field(r, 0), name)) return f.ok ? "chest without a name" : "string offset";
    e.out += "\t\t[" + luaconst::quoteAscii(name) + "] =\n\t\t{\n";
    if (f.str(f.field(r, 2), s)) e.kv("Group", s);
    if (f.list(f.field(r, 4), s)) e.kv("ActorType", s);
    if (f.str(f.field(r, 5), s)) e.kn("Spawnable", truthy(s) ? "1" : "0");
    if (f.str(f.field(r, 6), s)) e.kn("Awards", std::to_string(atoi(s.c_str())));
    if (f.str(f.field(r, 7), s)) e.kv("CommercialName", s);
    int32_t g = f.field(r, 8), h = f.field(r, 9), o = f.field(r, 16), w = f.field(r, 17);
    const bool resOk = g < (int32_t)f.nr && h < (int32_t)f.nr;
    const bool choOk = o < (int32_t)f.nc && w < (int32_t)f.nc;
    if (!resOk || !choOk) return "record index";
    if (g >= 0) e.kv("GuaranteedRes", "R" + std::to_string(g));
    if (h >= 0) e.kv("SharedRes", "R" + std::to_string(h));
    chestTail(e, r, o, w);
    e.out += "\t\t},\n";
    return nullptr;
}

}  // namespace detail

inline bool chestInfoLua(const std::string& b, std::string& out, std::string* why)
{
    detail::Bin f{b};
    if (const char* bad = detail::parseBlocks(f)) return detail::fail(why, bad);
    out = "-- 1.0 var/chests/ChestInfo.bin in the form of 3.0's chestInfo loader (di1compat sys.hpp)\nChestInfo =\n{\n"
          "\tResource =\n\t{\n";
    detail::Emit e{f, out};
    detail::resources(e);
    out += "\t},\n\tChoreography =\n\t{\n";
    detail::choreographies(e);
    out += "\t},\n\tChests =\n\t{\n";
    for (uint32_t i = 0; i < f.nk; ++i)
        if (const char* bad = detail::chest(e, f.ch + detail::kChest * i)) return detail::fail(why, bad);
    out += "\t},\n}\n";
    return f.ok || detail::fail(why, "offset out of range");
}

}  // namespace di1::chestinfo
