// A 1.0 DLG v11 dialog script as 3.0 DLG v0x15, merged with 3.0's own script. base. Per script read.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../engine/audio/dlgscript.hpp"

namespace di1::dlg {

using engine::audio::dlgscript::dlgLineKey;
using engine::audio::dlgscript::DlgSeq;
using engine::audio::dlgscript::DlgStats;
using engine::audio::dlgscript::ScriptRequest;

constexpr uint32_t kVersion11 = 11;   // a 1.0 script's first u32

// 1.0 FUN_0071c020 / FUN_00718650 -> 3.0 FUN_0073fb90 / FUN_0073daf0. false: out and st untouched, err says why.
inline bool dlgTranscode(const ScriptRequest& r, std::vector<uint8_t>& out, DlgStats* st = nullptr,
                         std::string* err = nullptr);

namespace detail {

// 1.0: u32 11, u32 n, 8 bytes, n x (u32 case-kept x131, u32 offset); an entry's byte 0: type.
constexpr size_t kEntries10 = 0x10;
constexpr size_t kEntryHead = 0x10;   // a 1.0 entry's fixed part; also 3.0's type 1 / 2 header
constexpr size_t kMaxIndex = 255;     // 3.0 indexes events and classes with a u8
constexpr uint32_t kMaxSeqItems = 64;
constexpr uint8_t kNoClass = 0xff;    // a 3.0 range line without a class
// 3.0: "DLG\0" 0x15 flags 2, u16 events, entries, ranges; then table offsets of 8-byte slots.
constexpr char kMagic30[] = "DLG\0\x15\x02";
constexpr size_t kHeader30 = 0x30;
constexpr size_t kCounts30 = 6;       // u16 events, entries, ranges
constexpr size_t kTables30 = 0x10;    // u32 events, entries, ranges table offsets, 8 bytes apart
constexpr uint32_t kSlot = 8;
// 1.0 entry fields (1.0 FUN_0071c020): priority, flags; type 0 event, count, 4-byte lines
constexpr size_t kPriority = 1, kFlags = 2;
constexpr size_t kClassEvent = 4, kClassCount = 8, kClassLines = 0xc, kLineSize = 4;
constexpr size_t kItemCount = 0xc;   // types 1 to 4: u8 item count
constexpr size_t kVariantSize = 8, kVariantEvent = 4;   // type 1 items from kEntryHead
constexpr size_t kSpeakerSize = 12, kSpeakerName = 4, kSpeakerCount = 6, kSpeakerList = 8;   // type 2
constexpr size_t kSeqCount = 8, kSeqFirst = 0xc, kSeqSize = 12, kSeqEvent = 4, kSeqDelay = 8;   // types 3, 4
constexpr size_t kOnceEnd = 0x14;   // the end of a type 3 / 4 entry's first item
// 3.0 entry fields (3.0 FUN_0073fb90): runtime play counts, item sizes, event bytes
constexpr size_t kPlaysAtOnce = 3, kPlays = 7, kClassSize30 = 8, kClassEvent30 = 4;
constexpr size_t kVariantSize30 = 8, kVariantEvent30 = 6, kSpeakerSize30 = 16, kSpeakerEvent30 = 0xc;
constexpr size_t kSpeakerCount30 = 0xd, kNamesCount30 = 4, kNamesHead30 = 8, kNamesSize30 = 16, kNamesName30 = 8;
constexpr size_t kEntryMin30 = 8;   // read of every 3.0 entry before its type is known
// Entry types: 0 class, 1 variants, 2 speakers; 1.0's 3 / 4 'once', 3.0's 3 named sources
constexpr uint8_t kClass = 0, kVariants = 1, kSpeakers = 2, kOnceAfterEnd = 3, kOnceAfterStart = 4, kNames30 = 3;
constexpr size_t kRangeDefault = 4, kRangeHead = 5;   // u16 min, u16 max, u8 default event; then classes

// One 3.0 entry being built: hash, bytes, and blobs written after it (their offset goes at the pos).
struct Ent {
    uint32_t hash;
    std::vector<uint8_t> bytes;
    std::vector<std::pair<size_t, std::vector<uint8_t>>> lists;
};

// The script being built, in 3.0's order: event names, classes, named entries, ranges.
struct Build {
    const ScriptRequest& req;
    DlgStats s;
    std::vector<std::string> events{"dialog_unassigned"};   // index 0: 3.0's 'use the range default', unused here
    std::map<std::string, uint8_t> evIdx;
    bool evOverflow = false;
    std::vector<Ent> classes, named;
    std::map<uint32_t, std::map<uint16_t, uint8_t>> ranges;   // packed prefix -> number -> class index
    std::map<std::pair<uint32_t, uint8_t>, uint8_t> classOf;  // merge: (3.0 class, default event) -> our class
    const char* why = nullptr;
    bool fail(const char* reason)
    {
        why = reason;
        return false;
    }
};

// 3.0's script of the name: its counts and table offsets.
struct Script30 {
    const std::vector<uint8_t>& t;
    uint16_t ne3, nn3, nr3;
    uint32_t te, tn, tr;
};

inline uint8_t eventIndex(Build& b, const std::string& nm)
{
    auto it = b.evIdx.find(nm);
    if (it != b.evIdx.end()) return it->second;
    if (b.events.size() > kMaxIndex) {
        b.evOverflow = true;
        return 0;
    }
    b.events.push_back(nm);
    return b.evIdx[nm] = (uint8_t)(b.events.size() - 1);
}

// A 1.0 event hash -> its index; one no FMOD project names goes in as its hex.
inline uint8_t event10(Build& b, uint32_t h)
{
    auto nm = b.req.evNames.find(h);
    if (nm != b.req.evNames.end()) return eventIndex(b, nm->second);
    char hex[16];
    snprintf(hex, sizeof hex, "%08x", h);
    if (!b.evIdx.count(hex)) ++b.s.unresolved;
    return eventIndex(b, hex);
}

// A 0-terminated string at o in t, or "".
inline std::string cstrAt(const std::vector<uint8_t>& t, size_t o)
{
    const bool inside = o < t.size() && memchr(t.data() + o, 0, t.size() - o);
    return inside ? std::string((const char*)t.data() + o) : std::string();
}

inline uint8_t event30(Build& b, const Script30& t, uint8_t i)
{
    return i < t.ne3 ? eventIndex(b, cstrAt(t.t, bytes::u32OrZero(t.t, t.te + i * kSlot))) : (uint8_t)0;
}

// Type 0, a line class (+4 event, +8 count, lines from +0xc): a 3.0 class and range bytes.
inline bool classEntry(Build& b, uint32_t hash, size_t off)
{
    const std::vector<uint8_t>& in = b.req.source;
    const uint8_t* e = in.data() + off;
    const uint32_t cnt = bytes::u16At(e + kClassCount);
    if (!bytes::fits(in.size(), off + kClassLines, (size_t)cnt * kLineSize)) return b.fail("class past the end");
    if (b.classes.size() >= kMaxIndex) return b.fail("more than 255 line classes");
    const uint8_t ci = (uint8_t)b.classes.size();
    const uint8_t ev = event10(b, bytes::u32OrZero(in, off + kClassEvent));
    Ent o{hash, {0, (uint8_t)(e[kPriority] >> 1), e[kFlags], 0, ev, 0, 0, 0}, {}};
    for (uint32_t k = 0; k < cnt; ++k) {
        const uint8_t* li = e + kClassLines + k * kLineSize;
        const uint32_t pfx = (uint32_t)(li[0] & 0xdf) | (uint32_t)(li[1] & 0xdf) << 8;   // FUN_0182bc20's key
        std::map<uint16_t, uint8_t>& slot = b.ranges[pfx];
        const uint16_t num = bytes::u16At(li + 2);
        if (b.req.lineEvents) (*b.req.lineEvents)[dlgLineKey(pfx, num)] = b.events[o.bytes[kClassEvent30]];
        if (slot.count(num)) ++b.s.dupLines;
        slot[num] = ci;
        ++b.s.lines;
    }
    b.classes.push_back(std::move(o));
    ++b.s.classes;
    return true;
}

// Type 1, variants: header, n x (char[2] prefix, u16 number, u32 event) -> 3.0 (u32 prefix, u16, u8 event, u8).
inline bool variantsEntry(Build& b, size_t off, Ent& o)
{
    const std::vector<uint8_t>& in = b.req.source;
    const uint8_t* e = in.data() + off;
    const uint8_t cnt = e[kItemCount];
    if (!bytes::fits(in.size(), off, kEntryHead + (size_t)cnt * kVariantSize)) return b.fail("variants past the end");
    for (uint8_t k = 0; k < cnt; ++k) {
        const uint8_t* it = e + kEntryHead + k * kVariantSize;
        const uint8_t ev = event10(b, bytes::u32OrZero(in, off + kEntryHead + k * kVariantSize + kVariantEvent));
        const uint8_t item[8] = {it[0], it[1], 0, 0, it[2], it[3], ev, 0};
        o.bytes.insert(o.bytes.end(), item, item + 8);
    }
    return true;
}

// Type 2, speakers: n x (u32 event, char[2] speaker, u8 count, u8 last, u32 list offset), then u16 lists.
inline bool speakersEntry(Build& b, size_t off, Ent& o)
{
    const std::vector<uint8_t>& in = b.req.source;
    const uint8_t* e = in.data() + off;
    const uint8_t cnt = e[kItemCount];
    if (!bytes::fits(in.size(), off, kEntryHead + (size_t)cnt * kSpeakerSize)) return b.fail("speakers past the end");
    const size_t lists = off + kEntryHead + (size_t)cnt * kSpeakerSize;
    for (uint8_t k = 0; k < cnt; ++k) {
        const uint8_t* sp = e + kEntryHead + k * kSpeakerSize;
        const uint8_t c = sp[kSpeakerCount];
        const uint32_t rel = bytes::u32At(sp + kSpeakerList);
        // the list relative to 'lists' (lists <= in.size() above): no 32-bit wrap of lists + rel
        if (!bytes::fits(in.size() - lists, rel, (size_t)c * 2)) return b.fail("speaker list past the end");
        const size_t lo = lists + rel;
        const uint8_t ev = event10(b, bytes::u32At(sp));
        const uint8_t a = sp[kSpeakerName], z = sp[kSpeakerName + 1];
        const uint8_t sub[kSpeakerSize30] = {0, 0, 0, 0, 0, 0, 0, 0, a, z, 0, 0, ev, c, 0, 0};
        o.lists.emplace_back(o.bytes.size(), std::vector<uint8_t>(in.data() + lo, in.data() + lo + (size_t)c * 2));
        o.bytes.insert(o.bytes.end(), sub, sub + kSpeakerSize30);
    }
    return true;
}

// A type 3 / 4 entry's lines: n x (char[2] prefix, u16 line, u32 event, u32 delay ms) from +0xc.
inline void readSeq(Build& b, uint32_t hash, size_t off, uint32_t items)
{
    const std::vector<uint8_t>& in = b.req.source;
    const uint8_t* e = in.data() + off;
    DlgSeq q{e[0], {}};
    for (uint32_t k = 0; k < items; ++k) {
        const uint8_t* it = e + kSeqFirst + k * kSeqSize;
        auto nm = b.req.evNames.find(bytes::u32OrZero(in, off + kSeqFirst + k * kSeqSize + kSeqEvent));
        const std::string event = nm == b.req.evNames.end() ? std::string() : nm->second;
        q.items.push_back({(uint16_t)(it[0] | it[1] << 8), bytes::u16At(it + 2), event,
                           bytes::u32OrZero(in, off + kSeqFirst + k * kSeqSize + kSeqDelay)});
    }
    (*b.req.seqs)[hash] = std::move(q);
}

// Types 3 / 4, 'once': 3.0's type 1 of one item, one play at once; the sequencer plays later lines.
inline bool onceEntry(Build& b, uint32_t hash, size_t off, Ent& o)
{
    const std::vector<uint8_t>& in = b.req.source;
    const uint8_t* e = in.data() + off;
    if (!bytes::fits(in.size(), off, kOnceEnd)) return b.fail("once past the end");
    const uint32_t items = bytes::u32OrZero(in, off + kSeqCount);
    const bool several = items > 1 && items < kMaxSeqItems;
    const bool fits = bytes::fits(in.size(), off, kSeqFirst + (size_t)items * kSeqSize);
    if (b.req.seqs && several && fits) readSeq(b, hash, off, items);
    // 3.0's type 1 header: [5] chance 0xff, [6] max plays 1, [0xc] one item, [0xe] max at once 1
    const uint8_t pri = (uint8_t)(e[kPriority] >> 1);
    const uint8_t h[kEntryHead] = {kVariants, pri, e[kFlags], 0, 0, 0xff, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0};
    o.bytes.assign(h, h + kEntryHead);
    const uint8_t* it = e + kSeqFirst;
    const uint8_t ev = event10(b, bytes::u32OrZero(in, off + kSeqFirst + kSeqEvent));
    const uint8_t item[kVariantSize30] = {it[0], it[1], 0, 0, it[2], it[3], ev, 0};
    o.bytes.insert(o.bytes.end(), item, item + kVariantSize30);
    return true;
}

// Entry i of the v11 table. Priority: 1.0 0..255 in tenths, 3.0 0..0x7f, same order; 3.0 = 1.0 >> 1.
inline bool parseEntry(Build& b, uint32_t i)
{
    const std::vector<uint8_t>& in = b.req.source;
    const uint32_t hash = bytes::u32OrZero(in, kEntries10 + (size_t)i * 8);
    const uint32_t off = bytes::u32OrZero(in, kEntries10 + 4 + (size_t)i * 8);
    if (!bytes::fits(in.size(), off, kEntryHead)) return b.fail("entry past the end");
    const uint8_t* e = in.data() + off;
    if (e[0] == kClass) return classEntry(b, hash, off);
    Ent o{hash, std::vector<uint8_t>(e, e + kEntryHead), {}};
    bool ok = false;
    switch (e[0]) {
    case kVariants: ok = variantsEntry(b, off, o); break;
    case kSpeakers: ok = speakersEntry(b, off, o); break;
    case kOnceAfterEnd:
    case kOnceAfterStart: ok = onceEntry(b, hash, off, o); break;
    default: return b.fail("unknown entry type");
    }
    if (!ok) return false;
    o.bytes[kPriority] = (uint8_t)(e[kPriority] >> 1);
    o.bytes[kPlaysAtOnce] = 0;   // runtime state
    o.bytes[kPlays] = 0;
    b.named.push_back(std::move(o));
    ++b.s.named;
    return true;
}

// Named entries keep 1.0's case-kept hash: the request hook adds the lower-cased one 3.0 looks up.
inline bool parseV11(Build& b)
{
    const std::vector<uint8_t>& in = b.req.source;
    if (in.size() < kEntries10 || bytes::u32OrZero(in, 0) != kVersion11) return b.fail("not a v11 dlg");
    const uint32_t n = bytes::u32OrZero(in, 4);
    if (n > in.size() / 8 || !bytes::fits(in.size(), kEntries10, (size_t)n * 8))
        return b.fail("entry table past the end");
    for (uint32_t i = 0; i < n; ++i)
        if (!parseEntry(b, i)) return false;
    return true;
}

// Our class for 3.0 class c under its range default (event 0 takes the default, FUN_0073daf0); nullopt: failed.
inline std::optional<uint8_t> classFor30(Build& b, const Script30& t, uint8_t c, uint8_t def)
{
    const uint32_t co = bytes::u32OrZero(t.t, t.tn + c * kSlot + 4);
    if (!bytes::fits(t.t.size(), co, kClassSize30) || t.t[co] != 0) {
        b.fail("3.0 range class is no class");
        return std::nullopt;
    }
    const uint8_t own = t.t[co + kClassEvent30];
    const auto key = std::make_pair((uint32_t)c, own ? (uint8_t)0 : def);
    auto it = b.classOf.find(key);
    if (it != b.classOf.end()) return it->second;
    if (b.classes.size() >= kMaxIndex) {
        b.fail("more than 255 line classes");
        return std::nullopt;
    }
    const uint8_t* cls = t.t.data() + co;
    Ent o{bytes::u32OrZero(t.t, t.tn + c * kSlot), std::vector<uint8_t>(cls, cls + kClassSize30), {}};
    o.bytes[kClassEvent30] = event30(b, t, own ? own : def);
    o.bytes[kPlaysAtOnce] = 0;
    it = b.classOf.emplace(key, (uint8_t)b.classes.size()).first;
    b.classes.push_back(std::move(o));
    return it->second;
}

// 3.0 range r {u16 min, u16 max, u8 default event, a class byte per line}: lines 1.0 does not define.
inline bool mergeRange30(Build& b, const Script30& t, uint16_t r)
{
    const uint32_t pfx = bytes::u32OrZero(t.t, t.tr + r * kSlot);
    const uint32_t ro = bytes::u32OrZero(t.t, t.tr + r * kSlot + 4);
    const uint16_t lo = bytes::u16OrZero(t.t, ro), hi = bytes::u16OrZero(t.t, ro + 2);
    if (hi < lo || !bytes::fits(t.t.size(), ro, kRangeHead + 1u + (hi - lo))) return b.fail("3.0 range past the end");
    std::map<uint16_t, uint8_t>& slot = b.ranges[pfx];
    for (uint32_t num = lo; num <= hi; ++num) {
        const uint8_t c = t.t[ro + kRangeHead + (num - lo)];
        if (c == kNoClass || slot.count((uint16_t)num)) continue;   // none, or 1.0 defines the line
        if (c >= t.nn3) return b.fail("3.0 range class past the entries");
        const std::optional<uint8_t> idx = classFor30(b, t, c, t.t[ro + kRangeDefault]);
        if (!idx) return false;
        slot[(uint16_t)num] = *idx;
        ++b.s.lines30;
    }
    return true;
}

// A 3.0 type 1 / 2 entry: its event indices remapped, a speaker's number list kept as a blob.
inline bool listEntry30(Build& b, const Script30& t, uint32_t o, uint8_t type, Ent& e)
{
    const uint8_t cnt = bytes::fits(t.t.size(), o, kEntryHead) ? t.t[o + kItemCount] : 0;
    const size_t len = kEntryHead + (size_t)cnt * (type == kVariants ? kVariantSize30 : kSpeakerSize30);
    if (!bytes::fits(t.t.size(), o, len)) return b.fail("3.0 entry past the end");
    e.bytes.assign(t.t.data() + o, t.t.data() + o + len);
    for (uint8_t k = 0; k < cnt; ++k) {
        if (type == kVariants) {
            const size_t at = kEntryHead + k * kVariantSize30 + kVariantEvent30;
            e.bytes[at] = event30(b, t, t.t[o + at]);
            continue;
        }
        const size_t so = kEntryHead + (size_t)k * kSpeakerSize30;
        const uint32_t lp = bytes::u32OrZero(t.t, o + so);
        const uint8_t c = t.t[o + so + kSpeakerCount30];
        if (!bytes::fits(t.t.size(), lp, c * 2u)) return b.fail("3.0 speaker list past the end");
        e.bytes[so + kSpeakerEvent30] = event30(b, t, t.t[o + so + kSpeakerEvent30]);
        e.lists.emplace_back(so, std::vector<uint8_t>(t.t.data() + lp, t.t.data() + lp + c * 2u));
    }
    return true;
}

// A 3.0 type 3: [4] n, then n 16-byte items whose +8 names a source; the names are kept.
inline bool namesEntry30(Build& b, const Script30& t, uint32_t o, Ent& e)
{
    const uint8_t cnt = t.t[o + kNamesCount30];
    const size_t len = kNamesHead30 + (size_t)cnt * kNamesSize30;
    if (!bytes::fits(t.t.size(), o, len)) return b.fail("3.0 entry past the end");
    e.bytes.assign(t.t.data() + o, t.t.data() + o + len);
    for (uint8_t k = 0; k < cnt; ++k) {
        const size_t at = kNamesHead30 + (size_t)k * kNamesSize30 + kNamesName30;
        const std::string nm = cstrAt(t.t, bytes::u32OrZero(t.t, o + at));
        e.lists.emplace_back(at, std::vector<uint8_t>(nm.c_str(), nm.c_str() + nm.size() + 1));
    }
    return true;
}

// 3.0 named entry i, kept when 1.0's script has none of that hash.
inline bool keepEntry30(Build& b, const Script30& t, uint16_t i, const std::unordered_set<uint32_t>& named10)
{
    const uint32_t h = bytes::u32OrZero(t.t, t.tn + i * kSlot), o = bytes::u32OrZero(t.t, t.tn + i * kSlot + 4);
    if (!bytes::fits(t.t.size(), o, kEntryMin30)) return b.fail("3.0 entry past the end");
    const uint8_t type = t.t[o];
    if (type == kClass || named10.count(h)) return true;
    Ent e{h, {}, {}};
    bool ok = false;
    switch (type) {
    case kVariants:
    case kSpeakers: ok = listEntry30(b, t, o, type, e); break;
    case kNames30: ok = namesEntry30(b, t, o, e); break;
    default: return b.fail("unknown 3.0 entry type");
    }
    if (!ok) return false;
    e.bytes[kPlaysAtOnce] = 0;
    if (e.bytes.size() > kPlays && type != kNames30) e.bytes[kPlays] = 0;
    b.named.push_back(std::move(e));
    ++b.s.kept30;
    return true;
}

// 3.0 re-authored scripts for its own figures: what 1.0's script lacks is kept, where both define one 1.0's wins.
inline bool merge30(Build& b)
{
    const std::vector<uint8_t>* base = b.req.base;
    const bool is30 = base && base->size() >= kHeader30 && !memcmp(base->data(), kMagic30, 5);
    if (!is30) return true;
    const std::vector<uint8_t>& t = *base;
    const Script30 s{t, bytes::u16OrZero(t, kCounts30), bytes::u16OrZero(t, kCounts30 + 2),
                     bytes::u16OrZero(t, kCounts30 + 4), bytes::u32OrZero(t, kTables30),
                     bytes::u32OrZero(t, kTables30 + kSlot), bytes::u32OrZero(t, kTables30 + 2 * kSlot)};
    const bool tablesIn = bytes::fits(t.size(), s.te, s.ne3 * kSlot) && bytes::fits(t.size(), s.tn, s.nn3 * kSlot) &&
                          bytes::fits(t.size(), s.tr, s.nr3 * kSlot);
    if (!tablesIn) return b.fail("3.0 script tables past the end");
    std::unordered_set<uint32_t> named10;
    for (auto& e : b.named) named10.insert(e.hash);
    for (uint16_t r = 0; r < s.nr3; ++r)
        if (!mergeRange30(b, s, r)) return false;
    for (uint16_t i = 0; i < s.nn3; ++i)
        if (!keepEntry30(b, s, i, named10)) return false;
    return true;
}

inline void padTo4(std::vector<uint8_t>& out)
{
    while (out.size() & 3) out.push_back(0);
}

// Entries in order, classes first (a range byte indexes them); each list goes after its entry, its offset into it.
inline void writeEntries(const Build& b, std::vector<uint8_t>& out, uint32_t tEn)
{
    uint32_t k = 0;
    for (auto* group : {&b.classes, &b.named})
        for (const Ent& e : *group) {
            padTo4(out);
            const uint32_t at = (uint32_t)out.size();
            bytes::put32(out, tEn + k * kSlot, e.hash);
            bytes::put32(out, tEn + k * kSlot + 4, at);
            out.insert(out.end(), e.bytes.begin(), e.bytes.end());
            for (auto& l : e.lists) {   // offsets are absolute; the loader rebases them
                padTo4(out);
                bytes::put32(out, at + l.first, (uint32_t)out.size());
                out.insert(out.end(), l.second.begin(), l.second.end());
            }
            ++k;
        }
}

inline void writeRanges(const Build& b, std::vector<uint8_t>& out, uint32_t tRa)
{
    uint32_t k = 0;
    for (auto& r : b.ranges) {
        padTo4(out);
        const uint16_t lo = r.second.begin()->first, hi = r.second.rbegin()->first;
        bytes::put32(out, tRa + k * kSlot, r.first);
        bytes::put32(out, tRa + k * kSlot + 4, (uint32_t)out.size());
        const size_t at = out.size();
        out.resize(at + kRangeHead + (hi - lo + 1), kNoClass);
        bytes::put16(out, at, lo);
        bytes::put16(out, at + 2, hi);
        out[at + kRangeDefault] = 0;
        for (auto& l : r.second) out[at + kRangeHead + (l.first - lo)] = l.second;
        ++k;
    }
}

// Header, the three tables, event names, entries, ranges.
inline void writeV15(const Build& b, std::vector<uint8_t>& out)
{
    const uint32_t ne = (uint32_t)b.events.size(), nn = (uint32_t)(b.classes.size() + b.named.size());
    const uint32_t nr = (uint32_t)b.ranges.size();
    out.assign(kHeader30 + (size_t)(ne + nn + nr) * kSlot, 0);
    memcpy(out.data(), kMagic30, 6);
    bytes::put16(out, kCounts30, (uint16_t)ne);
    bytes::put16(out, kCounts30 + 2, (uint16_t)nn);
    bytes::put16(out, kCounts30 + 4, (uint16_t)nr);
    const uint32_t tEv = (uint32_t)kHeader30, tEn = tEv + ne * kSlot, tRa = tEn + nn * kSlot;
    bytes::put32(out, kTables30, tEv);
    bytes::put32(out, kTables30 + kSlot, tEn);
    bytes::put32(out, kTables30 + 2 * kSlot, tRa);
    for (uint32_t i = 0; i < ne; ++i) {
        bytes::put32(out, tEv + i * kSlot, (uint32_t)out.size());
        out.insert(out.end(), b.events[i].begin(), b.events[i].end());
        out.push_back(0);
    }
    writeEntries(b, out, tEn);
    writeRanges(b, out, tRa);
}

}  // namespace detail

inline bool dlgTranscode(const ScriptRequest& r, std::vector<uint8_t>& out, DlgStats* st, std::string* err)
{
    detail::Build b{r};
    const bool ok = detail::parseV11(b) && detail::merge30(b);
    if (!ok || b.evOverflow) {
        if (err) *err = ok ? "more than 255 events" : b.why;
        return false;
    }
    detail::writeV15(b, out);
    b.s.ranges = (int)b.ranges.size();
    b.s.events = (int)b.events.size();
    if (st) *st = b.s;
    return true;
}

}  // namespace di1::dlg
