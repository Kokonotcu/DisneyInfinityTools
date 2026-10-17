// Octane tuple files (.oct .mer .bent .banm ...): parse, edit, write byte-exact (FORMATS.md 3). Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"

namespace oct {

constexpr uint32_t MAGIC = 0x45017629;
constexpr size_t HDR = 0x3c;
enum Shape { NODE = 1, LIST = 2, SCALAR = 3 };
enum Kind { NAME = 1, FLOAT = 2, INT = 3, BYTE = 4 };

// Header fields (FORMATS.md 3): name table size, record data size, record count.
inline constexpr size_t kNamesSizeAt = 0x0c;
inline constexpr size_t kDataSizeAt = 0x10;
inline constexpr size_t kRecordCountAt = 0x14;
// A record starts with u16 flag = level << 10 | type, then its u16 name index (FORMATS.md 3).
inline constexpr size_t kRecordHead = 4;
inline constexpr int kLevelShift = 10;
inline constexpr uint16_t kTypeMask = 0x3ff;

// A type from its bit fields (FORMATS.md 3): shape 0-1, keyed 2, kind 3-5, count width 6-7, int width 8-9.
constexpr uint16_t makeType(int shape, int keyed, int kind, int countWidth = 1, int intWidth = 1)
{
    return (uint16_t)(shape | keyed << 2 | kind << 3 | (countWidth - 1) << 6 | (intWidth - 1) << 8);
}

// The narrowest element-count width, 1-3 bytes. Callers stay below 2^24 elements (CO-31).
inline int countWidth(size_t n) { return n < 0x100 ? 1 : n < 0x10000 ? 2 : 3; }

struct Spec { int shape, keyed, kind, cw, ew; bool ok; };

// type -> its bit fields (shape, keyed, kind, count width, element width); ok=false if they do not form a type.
inline Spec spec(int t)
{
    int shape = t & 3, keyed = (t >> 2) & 1, kind = (t >> 3) & 7, cw = ((t >> 6) & 3) + 1, iw = ((t >> 8) & 3) + 1;
    const bool plainWidths = cw == 1 && iw == 1;
    const bool node = shape == NODE && kind == 0 && plainWidths;
    const bool valueShape = shape == LIST || shape == SCALAR;
    const bool valueKind = kind >= NAME && kind <= BYTE;
    const bool intWidthOk = kind == INT || iw == 1;                  // only ints have an int width
    const bool countOk = shape == LIST || kind == BYTE || cw == 1;   // only lists and blobs have a count
    const bool kindOk = valueKind && intWidthOk && countOk;
    const bool value = valueShape && kindOk;
    int el = kind == NAME ? 2 : kind == FLOAT ? 4 : kind == BYTE ? 1 : 0;
    return {shape, keyed, kind, (shape == LIST || kind == BYTE) ? cw : 0, kind == INT ? iw : el, node || value};
}

// The unsigned little-endian value of w bytes at p, unchecked (bytes::le).
inline uint32_t u(const uint8_t* p, int w) { return bytes::le(p, w); }

// Payload length of record at p, or -1 if bad or past end. 64-bit, so huge counts cannot wrap (CO-15).
inline long paylen(int t, const uint8_t* p, const uint8_t* end)
{
    Spec s = spec(t);
    if (!s.ok) return -1;
    long pre = 2 * s.keyed;
    if (s.shape == NODE) return pre;
    if (!s.cw) return pre + s.ew;
    const size_t room = (size_t)(end - p);
    if (!bytes::fits(room, (size_t)pre, (size_t)s.cw)) return -1;
    const uint64_t len = (uint64_t)pre + (uint64_t)s.cw + (uint64_t)u(p + pre, s.cw) * (uint64_t)s.ew;
    return len <= room ? (long)len : -1;
}

struct Rec {
    uint16_t level = 0, type = 0, name = 0;
    std::string pay;
};

struct File {
    std::string hdr;
    std::vector<std::string> names;
    std::vector<Rec> recs;
    uint32_t recordCount = 0;   // the header's record count as read; 0 (unwritten) stays 0 on write
    std::string tail;
    // addName's index of names; rebuilt when names changed outside addName (nameIndexed != names.size())
    std::unordered_map<std::string, uint16_t> nameIndex;
    size_t nameIndexed = 0;

    static File parse(const void* data, size_t n)
    {
        const uint8_t* b = (const uint8_t*)data;
        if (n < HDR || u(b, 4) != MAGIC) throw std::runtime_error("bad magic");
        File f;
        f.hdr.assign((const char*)b, HDR);
        const uint32_t nb = u(b + kNamesSizeAt, 4), db = u(b + kDataSizeAt, 4);
        f.recordCount = u(b + kRecordCountAt, 4);
        // fits() before summing: in 32 bits HDR + nb + db can wrap below n (CO-15)
        if (!bytes::fits(n, HDR, nb) || !bytes::fits(n, HDR + nb, db))
            throw std::runtime_error("size invariant broken");
        f.readNames(b, HDR + nb);
        const size_t end = f.readRecords(b, HDR + nb, HDR + nb + db);
        f.tail.assign((const char*)b + end, HDR + nb + db - end);
        return f;
    }

    std::string write() const
    {
        size_t namesSize = 0, dataSize = tail.size();
        for (const std::string& s : names) namesSize += s.size() + 1;
        for (const Rec& r : recs) dataSize += kRecordHead + r.pay.size();
        std::string out = hdr;
        bytes::setLe32(out, kNamesSizeAt, (uint32_t)namesSize);
        bytes::setLe32(out, kDataSizeAt, (uint32_t)dataSize);
        // the engine allocates from the record count (FORMATS.md 3)
        bytes::setLe32(out, kRecordCountAt, recordCount == 0 ? 0u : (uint32_t)recs.size());
        out.reserve(out.size() + namesSize + dataSize);
        for (const std::string& s : names) {
            out += s;
            out.push_back('\0');
        }
        for (const Rec& r : recs) {
            const uint16_t flag = (uint16_t)(r.level << kLevelShift | r.type);
            const char head[kRecordHead] = {(char)flag, (char)(flag >> 8), (char)r.name, (char)(r.name >> 8)};
            out.append(head, kRecordHead);
            out += r.pay;
        }
        out += tail;
        return out;
    }

    // The name at index i, or "?" past the table.
    const std::string& name(size_t i) const
    {
        static const std::string none = "?";
        return i < names.size() ? names[i] : none;
    }
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

    // s's index; an unseen name is appended. Call order is part of the bytes: the name table follows it (CO-02).
    uint16_t addName(const std::string& s)
    {
        // indexed: in_city has ~15k names and gains thousands of records; a linear scan made locator30 take 1 s
        if (nameIndexed != names.size()) {
            nameIndex.clear();
            for (size_t i = 0; i < names.size(); ++i) nameIndex.emplace(names[i], (uint16_t)i);   // first wins
            nameIndexed = names.size();
        }
        auto it = nameIndex.find(s);
        if (it != nameIndex.end()) return it->second;
        // grows names: a name() reference taken before this call may dangle (CO-35)
        names.push_back(s);
        nameIndex.emplace(s, (uint16_t)(names.size() - 1));
        nameIndexed = names.size();
        return (uint16_t)(names.size() - 1);
    }

    // ---- value access ----
    // Int elements of an int/name/byte record (signed for ints), key and count stripped.
    std::vector<int32_t> ints(const Rec& r) const
    {
        Spec s = spec(r.type);
        const uint8_t* p = (const uint8_t*)r.pay.data() + 2 * s.keyed;
        size_t n = s.cw ? u(p, s.cw) : (s.shape == NODE ? 0 : 1);
        p += s.cw;
        const bool extend = s.kind == INT && s.ew < 4;   // ints are signed (FORMATS.md 3)
        std::vector<int32_t> out;
        for (size_t i = 0; i < n; ++i) {
            uint32_t v = u(p + i * s.ew, s.ew);
            if (extend && (v >> (8 * s.ew - 1)) & 1) v |= ~0u << (8 * s.ew);
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
    // ints(r)[0] / nameValues(r)[0], but throw when a damaged record has none: [0] of empty is an AV (CO-05).
    int32_t int0(const Rec& r) const
    {
        std::vector<int32_t> v = ints(r);
        if (v.empty()) throw std::runtime_error("oct: record '" + name(r) + "' has no int value");
        return v[0];
    }
    std::string name0(const Rec& r) const
    {
        std::vector<std::string> v = nameValues(r);
        if (v.empty()) throw std::runtime_error("oct: record '" + name(r) + "' has no name value");
        return std::move(v[0]);
    }

    // ---- record construction (the exporter's encodings: narrowest signed int width) ----

    // True when v fits a signed int of w (1-3) bytes.
    static bool fitsSigned(int32_t v, int w)
    {
        const int32_t limit = 1 << (8 * w - 1);
        return v >= -limit && v < limit;
    }
    static int width(const std::vector<int32_t>& vals)
    {
        int w = 1;
        for (int32_t v : vals)
            while (w < 4 && !fitsSigned(v, w)) ++w;
        return w;
    }
    static void put(std::string& s, uint32_t v, int w) { bytes::appendLe(s, v, w); }

    // In place, as oct.py: widths only grow, never narrow; values >= 2^31 become their signed 32-bit twin.
    void setInt(Rec& r, int64_t v)
    {
        Spec s = spec(r.type);
        if (v >= (int64_t)1 << 31) v -= (int64_t)1 << 32;
        int w = width({(int32_t)v});
        if (s.ew > w) w = s.ew;
        r.type = makeType(SCALAR, s.keyed, INT, 1, w);
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
        while (vals.size() >= ((size_t)1 << (8 * cw))) ++cw;   // lists stay below 2^24 elements (CO-31)
        r.type = makeType(LIST, s.keyed, INT, cw, w);
        std::string pay = r.pay.substr(0, 2 * s.keyed);
        put(pay, (uint32_t)vals.size(), cw);
        for (int32_t v : vals) put(pay, (uint32_t)v, w);
        r.pay = pay;
    }
    // A scalar name record (0x0b) with this string.
    Rec nameScalar(uint16_t level, uint16_t nameIdx, const std::string& value)
    {
        Rec r{level, makeType(SCALAR, 0, NAME), nameIdx, {}};
        put(r.pay, addName(value), 2);
        return r;
    }

    Rec node(uint16_t level, const std::string& n) { return Rec{level, NODE, addName(n), {}}; }
    Rec intScalar(uint16_t level, const std::string& n, int32_t v)
    {
        int w = width({v});
        Rec r{level, makeType(SCALAR, 0, INT, 1, w), addName(n), {}};
        put(r.pay, (uint32_t)v, w);
        return r;
    }
    Rec intList(uint16_t level, const std::string& n, const std::vector<int32_t>& vals)
    {
        int w = width(vals), cw = countWidth(vals.size());
        Rec r{level, makeType(LIST, 0, INT, cw, w), addName(n), {}};
        put(r.pay, (uint32_t)vals.size(), cw);
        for (int32_t v : vals) put(r.pay, (uint32_t)v, w);
        return r;
    }

private:
    // The NUL-terminated names of b[HDR, end).
    void readNames(const uint8_t* b, size_t end)
    {
        for (size_t p = HDR, q = HDR; q < end; ++q) {
            if (b[q] != 0) continue;
            names.emplace_back((const char*)b + p, q - p);
            p = q + 1;
        }
    }
    // The records of b[at, end) -> where they stop (the tail starts there).
    size_t readRecords(const uint8_t* b, size_t at, size_t end)
    {
        size_t p = at;
        while (bytes::fits(end, p, kRecordHead)) {
            const uint16_t flag = (uint16_t)u(b + p, 2);
            Rec r;
            r.type = flag & kTypeMask;
            r.level = (uint16_t)(flag >> kLevelShift);
            r.name = (uint16_t)u(b + p + 2, 2);
            long len = paylen(r.type, b + p + kRecordHead, b + end);
            if (len < 0 || !bytes::fits(end, p + kRecordHead, (size_t)len))
                throw std::runtime_error("unknown type at +" + std::to_string(p - at));
            r.pay.assign((const char*)b + p + kRecordHead, len);
            recs.push_back(std::move(r));
            p += kRecordHead + len;
        }
        return p;
    }
};

}  // namespace oct
