// Octane tuple files (.oct/.mer/.merdebug/.bent/.banm/.bct): parse, edit, write. C++ port of DI3Tools/oct.py
// (that docstring has the format); records keep their payload bytes, so write() is byte-exact unless edited.
// Checked by octtest.exe: every tuple file of both extracts round-trips byte-exactly (as oct.py --selftest).
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace oct {

constexpr uint32_t MAGIC = 0x45017629;
constexpr size_t HDR = 0x3c;
enum Shape { NODE = 1, LIST = 2, SCALAR = 3 };
enum Kind { NAME = 1, FLOAT = 2, INT = 3, BYTE = 4 };

struct Spec { int shape, keyed, kind, cw, ew; bool ok; };

// type -> its bit fields (shape, keyed, kind, count width, element width); ok=false if they do not form a type.
inline Spec spec(int t)
{
    int shape = t & 3, keyed = (t >> 2) & 1, kind = (t >> 3) & 7, cw = ((t >> 6) & 3) + 1, iw = ((t >> 8) & 3) + 1;
    bool ok = (shape == NODE && kind == 0 && cw == 1 && iw == 1) ||
              ((shape == LIST || shape == SCALAR) && kind >= NAME && kind <= BYTE && (kind == INT || iw == 1) &&
               (shape == LIST || kind == BYTE || cw == 1));
    int el = kind == NAME ? 2 : kind == FLOAT ? 4 : kind == BYTE ? 1 : 0;
    return {shape, keyed, kind, (shape == LIST || kind == BYTE) ? cw : 0, kind == INT ? iw : el, ok};
}

inline uint32_t u(const uint8_t* p, int w) { uint32_t v = 0; for (int i = 0; i < w; ++i) v |= (uint32_t)p[i] << (8 * i); return v; }

// Payload length of a record whose payload starts at p (end = data end), or -1.
inline long paylen(int t, const uint8_t* p, const uint8_t* end)
{
    Spec s = spec(t);
    if (!s.ok) return -1;
    long pre = 2 * s.keyed;
    if (s.shape == NODE) return pre;
    if (!s.cw) return pre + s.ew;
    if (p + pre + s.cw > end) return -1;
    return pre + s.cw + (long)u(p + pre, s.cw) * s.ew;
}

struct Rec {
    uint16_t level = 0, type = 0, name = 0;
    std::string pay;
};

struct File {
    std::string hdr;
    std::vector<std::string> names;
    std::vector<Rec> recs;
    uint32_t f14 = 0;
    std::string tail;

    static File parse(const void* data, size_t n)
    {
        const uint8_t* b = (const uint8_t*)data;
        if (n < HDR || u(b, 4) != MAGIC) throw std::runtime_error("bad magic");
        File f;
        f.hdr.assign((const char*)b, HDR);
        uint32_t nb = u(b + 12, 4), db = u(b + 16, 4);
        f.f14 = u(b + 20, 4);
        size_t ds = HDR + nb, de = ds + db;
        if (de > n) throw std::runtime_error("size invariant broken");
        for (size_t p = HDR, q = HDR; q < ds; ++q)
            if (b[q] == 0) { f.names.emplace_back((const char*)b + p, q - p); p = q + 1; }
        size_t p = ds;
        while (p + 4 <= de) {
            uint16_t flag = (uint16_t)u(b + p, 2);
            Rec r;
            r.type = flag & 0x3ff;
            r.level = flag >> 10;
            r.name = (uint16_t)u(b + p + 2, 2);
            long len = paylen(r.type, b + p + 4, b + de);
            if (len < 0 || p + 4 + len > de) throw std::runtime_error("unknown type at +" + std::to_string(p - ds));
            r.pay.assign((const char*)b + p + 4, len);
            f.recs.push_back(std::move(r));
            p += 4 + len;
        }
        f.tail.assign((const char*)b + p, de - p);
        return f;
    }

    std::string write() const
    {
        std::string nt, data;
        for (auto& s : names) { nt += s; nt.push_back('\0'); }
        for (auto& r : recs) {
            uint16_t flag = (uint16_t)((r.level << 10) | r.type);
            data.append((const char*)&flag, 2);
            data.append((const char*)&r.name, 2);
            data += r.pay;
        }
        data += tail;
        std::string h = hdr;
        // 0x14 is the record count and the engine allocates from it (0 when the source left it unwritten).
        uint32_t v[3] = {(uint32_t)nt.size(), (uint32_t)data.size(), f14 == 0 ? 0u : (uint32_t)recs.size()};
        memcpy(&h[12], v, 12);
        return h + nt + data;
    }

    const std::string& name(size_t i) const { static const std::string none = "?"; return i < names.size() ? names[i] : none; }
    const std::string& name(const Rec& r) const { return name(r.name); }

    // End (exclusive) of record i's subtree.
    size_t blockEnd(size_t i) const
    {
        size_t j = i + 1;
        while (j < recs.size() && recs[j].level > recs[i].level) ++j;
        return j;
    }

    // Index of the first top-level (level 1) record with this name, or -1.
    long pool(const char* poolName) const
    {
        for (size_t i = 0; i < recs.size(); ++i)
            if (recs[i].level == 1 && name(recs[i]) == poolName) return (long)i;
        return -1;
    }

    // Removes a top-level pool and its subtree; true if it was there.
    bool dropPool(const char* poolName)
    {
        long i = pool(poolName);
        if (i < 0) return false;
        recs.erase(recs.begin() + i, recs.begin() + blockEnd(i));
        return true;
    }

    uint16_t addName(const std::string& s)
    {
        // indexed: the load-time transforms add thousands of records to files with ~15k names (in_city), and a
        // linear scan per record made locator30 take a second
        if (nameIndexed != names.size()) {   // names changed outside addName (or first use): reindex
            nameIndex.clear();
            for (size_t i = 0; i < names.size(); ++i) nameIndex.emplace(names[i], (uint16_t)i);   // first wins
            nameIndexed = names.size();
        }
        auto it = nameIndex.find(s);
        if (it != nameIndex.end()) return it->second;
        names.push_back(s);
        nameIndex.emplace(s, (uint16_t)(names.size() - 1));
        nameIndexed = names.size();
        return (uint16_t)(names.size() - 1);
    }
    std::unordered_map<std::string, uint16_t> nameIndex;
    size_t nameIndexed = 0;

    // ---- value access ----
    // Int elements of an int/name/byte record (signed for ints), key and count stripped.
    std::vector<int32_t> ints(const Rec& r) const
    {
        Spec s = spec(r.type);
        const uint8_t* p = (const uint8_t*)r.pay.data() + 2 * s.keyed;
        size_t n = s.cw ? u(p, s.cw) : (s.shape == NODE ? 0 : 1);
        p += s.cw;
        std::vector<int32_t> out;
        for (size_t i = 0; i < n; ++i) {
            uint32_t v = u(p + i * s.ew, s.ew);
            if (s.kind == INT && s.ew < 4 && (v >> (8 * s.ew - 1)) & 1) v |= ~0u << (8 * s.ew);   // sign-extend
            out.push_back((int32_t)v);
        }
        return out;
    }
    // Name values (the key first for keyed records).
    std::vector<std::string> nameValues(const Rec& r) const
    {
        Spec s = spec(r.type);
        std::vector<std::string> out;
        if (s.keyed) out.push_back(name(u((const uint8_t*)r.pay.data(), 2)));
        if (s.kind == NAME)
            for (int32_t i : ints(r)) out.push_back(name((uint16_t)i));
        return out;
    }

    // ---- record construction (the exporter's encodings: narrowest signed int width) ----
    static int width(const std::vector<int32_t>& vals)
    {
        int w = 1;
        for (int32_t v : vals)
            while (w < 4 && !(v >= -(1 << (8 * w - 1)) && v < (1 << (8 * w - 1)))) ++w;
        return w;
    }
    static void put(std::string& s, uint32_t v, int w) { for (int i = 0; i < w; ++i) s.push_back((char)(v >> (8 * i))); }

    // In-place writes with oct.py's rules: an int scalar/list keeps its widths unless the values outgrow them
    // (never narrows); values >= 2^31 are written as their signed 32-bit twin.
    void setInt(Rec& r, int64_t v)
    {
        Spec s = spec(r.type);
        if (v >= (int64_t)1 << 31) v -= (int64_t)1 << 32;
        int w = width({(int32_t)v});
        if (s.ew > w) w = s.ew;
        r.type = (uint16_t)(SCALAR | (s.keyed << 2) | INT << 3 | (w - 1) << 8);
        std::string pay = r.pay.substr(0, 2 * s.keyed);
        put(pay, (uint32_t)(int32_t)v, w);
        r.pay = pay;
    }
    void setInts(Rec& r, const std::vector<int32_t>& vals)
    {
        Spec s = spec(r.type);
        int w = width(vals);
        if (s.ew > w) w = s.ew;
        int cw = s.cw < 1 ? 1 : s.cw;
        while (vals.size() >= ((size_t)1 << (8 * cw))) ++cw;
        r.type = (uint16_t)(LIST | (s.keyed << 2) | INT << 3 | (cw - 1) << 6 | (w - 1) << 8);
        std::string pay = r.pay.substr(0, 2 * s.keyed);
        put(pay, (uint32_t)vals.size(), cw);
        for (int32_t v : vals) put(pay, (uint32_t)v, w);
        r.pay = pay;
    }
    // A scalar name record (0x0b) with this string.
    Rec nameScalar(uint16_t level, uint16_t nameIdx, const std::string& value)
    {
        Rec r{level, 0x0b, nameIdx, {}};
        put(r.pay, addName(value), 2);
        return r;
    }

    Rec node(uint16_t level, const std::string& n) { return Rec{level, NODE, addName(n), {}}; }
    Rec intScalar(uint16_t level, const std::string& n, int32_t v)
    {
        int w = width({v});
        Rec r{level, (uint16_t)(SCALAR | INT << 3 | (w - 1) << 8), addName(n), {}};
        put(r.pay, (uint32_t)v, w);
        return r;
    }
    Rec intList(uint16_t level, const std::string& n, const std::vector<int32_t>& vals)
    {
        int w = width(vals), cw = vals.size() < 0x100 ? 1 : vals.size() < 0x10000 ? 2 : 3;
        Rec r{level, (uint16_t)(LIST | INT << 3 | (cw - 1) << 6 | (w - 1) << 8), addName(n), {}};
        put(r.pay, (uint32_t)vals.size(), cw);
        for (int32_t v : vals) put(r.pay, (uint32_t)v, w);
        return r;
    }
};

}  // namespace oct
