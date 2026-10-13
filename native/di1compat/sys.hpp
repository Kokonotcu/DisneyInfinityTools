// sys track: 1.0 global tables and progression records routed into 3.0's GameDB (research PLAN.md PS-5, PS-8,
// GLB-1). Pure text/data helpers here; the engine glue (hooks, opens) stays in di1compat.cpp.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <set>
#include <string>

namespace sys {

static const size_t npos = std::string::npos;

// t[i] is a quote: index after the closing quote (backslash escapes skipped), npos if unterminated
inline size_t skipString(const std::string& t, size_t i)
{
    char q = t[i++];
    while (i < t.size() && t[i] != q) i += t[i] == '\\' ? 2 : 1;
    return i < t.size() ? i + 1 : npos;
}

// t[i] == '{': index of its matching '}', strings and -- comments skipped (stateData strings hold any byte)
inline size_t matchBrace(const std::string& t, size_t i)
{
    int depth = 0;
    while (i < t.size()) {
        char c = t[i];
        if (c == '\'' || c == '"') {
            if ((i = skipString(t, i)) == npos) return npos;
            continue;
        }
        if (c == '-' && i + 1 < t.size() && t[i + 1] == '-') {
            if ((i = t.find('\n', i)) == npos) return npos;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i;
        ++i;
    }
    return npos;
}

// first `key =` in t[from, to) at brace depth 0 relative to from: index after the '=', npos if absent
inline size_t findKey(const std::string& t, const char* key, size_t from = 0, size_t to = npos)
{
    if (to > t.size()) to = t.size();
    size_t n = strlen(key);
    int depth = 0;
    for (size_t i = from; i < to;) {
        char c = t[i];
        if (c == '\'' || c == '"') {
            if ((i = skipString(t, i)) == npos) return npos;
            continue;
        }
        if (c == '-' && i + 1 < to && t[i + 1] == '-') {
            if ((i = t.find('\n', i)) == npos) return npos;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}') --depth;
        else if (depth == 0 && !t.compare(i, n, key) && (i == 0 || !(isalnum((unsigned char)t[i - 1]) || t[i - 1] == '_'))) {
            size_t j = i + n;
            while (j < to && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) ++j;
            if (j < to && t[j] == '=') return j + 1;
        }
        ++i;
    }
    return npos;
}

// A generated GameDB table (customization_csv2game.lua layout: Data = { DBName = ..., Records = { {UID = n, ...}, ... } })
// -> the same text with only the records keep(UID) accepts, each record's text verbatim (PS-5: the 1.0 records
// verbatim, adapted only in which of them load). Records without a UID are dropped. False if the layout is not found.
inline bool keepRecords(const std::string& t, const std::function<bool(uint32_t)>& keep, std::string& out, int& kept, int& total)
{
    kept = total = 0;
    size_t data = findKey(t, "Data");
    if (data == npos) return false;
    size_t dOpen = t.find('{', data), dClose = dOpen == npos ? npos : matchBrace(t, dOpen);
    if (dClose == npos) return false;
    size_t rec = findKey(t, "Records", dOpen + 1, dClose);
    if (rec == npos) return false;
    size_t open = t.find('{', rec), close = open == npos ? npos : matchBrace(t, open);
    if (close == npos || close > dClose) return false;
    out.assign(t, 0, open + 1);
    out += "\n";
    for (size_t i = open + 1; i < close;) {
        char c = t[i];
        if (c == '{') {
            size_t e = matchBrace(t, i);
            if (e == npos || e > close) return false;
            ++total;
            size_t u = findKey(t, "UID", i + 1, e);
            if (u != npos) {
                while (u < e && (t[u] == ' ' || t[u] == '\t')) ++u;
                uint32_t uid = (uint32_t)strtoul(t.c_str() + u, nullptr, 10);
                if (keep(uid)) {
                    out += "\t\t";
                    out.append(t, i, e + 1 - i);
                    out += ",\n";
                    ++kept;
                }
            }
            i = e + 1;
        } else if (c == '-' && i + 1 < close && t[i + 1] == '-') {
            if ((i = t.find('\n', i)) == npos) return false;
        } else ++i;
    }
    out += "\t";
    out.append(t, close, npos);
    return true;
}

// ---- PS-5: supplemental 1.0 lock records ----
// The 1.0 DataMap loads its playset locks from GameDB/Incredibles/locks__lua.lua; 3.0 ships a file at that path (18
// records), so the engine reads 3.0's and the 1.0 table (66) never loads: its 55 INCREDIBLES_STAR records (31 named by
// the 1.0 missions' GoldStar contexts / Eval_LockState) are absent. 1.0 also loaded its core table at boot
// (startup2.zip gamedb/core/locks__lua.lua, 2,325 records, 1,757 of them in no 3.0 locks table by UID, 0 by name;
// CUST_BH_Frozone_B, the lock reward of a 1.0 mission, is one). After the playset file, the same 3.0 loader is run on
// each 1.0 table served through kLocks10Prefix: only the records whose UID the Locks DB does not hold yet (merge by
// DBName, FUN_01612fa0; a duplicate UID re-points the record index, FUN_0149f900, so duplicates are never sent).
static const char* const kLocks10Prefix = "di1sys/locks10/";
static const char* const kLocksTrigger = "GameDB/Incredibles/locks__lua.lua";
static const char* const kLocks10[] = {"di1sys/locks10/GameDB/Incredibles/locks__lua.lua", "di1sys/locks10/gamedb/core/locks__lua.lua"};

inline bool readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION m;
    if ((uintptr_t)p < 0x10000 || !VirtualQuery(p, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return (uintptr_t)p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}

// UIDs of every record a GameDB set loader's sets hold: loader +0xc sets[], +0x10 count (FUN_01612fa0 walks them to
// merge a file by DBName); set +0x30 its record pool (FUN_01611a30 creates it); pool +0x30 UID[], +0x34 count
// (FUN_0149f900 stores each record's UID at its index; FUN_01611cb0 sizes the array). Unreadable parts are skipped.
inline std::set<uint32_t> loaderUids(const void* loader)
{
    std::set<uint32_t> u;
    const uint8_t* L = (const uint8_t*)loader;
    if (!readable(L + 0xc, 8)) return u;
    void* const* sets = *(void* const**)(L + 0xc);
    int ns = *(const int*)(L + 0x10);
    if (ns <= 0 || ns > 64 || !readable(sets, ns * 4)) return u;
    for (int s = 0; s < ns; ++s) {
        const uint8_t* set = (const uint8_t*)sets[s];
        if (!readable(set + 0x30, 4)) continue;
        const uint8_t* pool = *(const uint8_t* const*)(set + 0x30);
        if (!readable(pool + 0x30, 8)) continue;
        const uint32_t* ids = *(const uint32_t* const*)(pool + 0x30);
        int n = *(const int*)(pool + 0x34);
        if (n <= 0 || n > 200000 || !readable(ids, n * 4)) continue;
        u.insert(ids, ids + n);
    }
    return u;
}

// the Locks DB's UIDs when a supplement load starts; the open that serves it may run on another thread
inline std::mutex g_lockMx;
inline std::set<uint32_t> g_lockUids;
inline void setLockUids(std::set<uint32_t> u) { std::lock_guard<std::mutex> l(g_lockMx); g_lockUids.swap(u); }
inline std::set<uint32_t> lockUids() { std::lock_guard<std::mutex> l(g_lockMx); return g_lockUids; }

// ---- GLB-1: var/chests/ChestInfo.bin -> 3.0's chestInfo GameDB loader ----
// 1.0 ChestInfo ctor FUN_00faac30 maps the .bin (little-endian): [u32 size, string pool] [u32 size, list pool: per
// list a u32 flag then string offsets up to -1] [size, n, n x 0x20 choreographies] [size, n, n x 0xc resources]
// [size, n, n x 0x48 chests]; every field is a string offset or a list / record index, -1 = none.
// 3.0 dropped the .bin for a DBFileLoader_Callback "chestInfo" (built in ChestInfo ctor FUN_014b6420; ships no data):
// its callback FUN_014adef0 reads the Lua global ChestInfo = {Resource = {[name] = {...}}, Choreography = {...},
// Chests = {...}}, lists as " ,"-separated strings (tokenizer FUN_004727b0), and resolves chests' resources and
// choreographies by name (FUN_0148d4d0 stricmp), where 1.0 used record indices: those get the names R<i> / C<i>.
// Field correspondence (1.0 converter per field -> 3.0 key of the same type and role):
//   choreography [0] bool PauseAtLoadTime, [1] ChoName, [2] AnchorName, [3] list LockLayer, [4] list LockClips,
//     [5] DoorLayer, [6] DoorOpen, [7] float SpawnAward
//   resource [0] Currency, [1] list Locks, [2] list (empty in all 41 records; 3.0 reads no third field)
//   chest [0] name, [1]/[3] hashes of name/group (3.0 compares names), [2] Group, [4] bitmask list ActorType,
//     [5] bool Spawnable, [6] int Awards, [7] CommercialName, [8]/[9] resource GuaranteedRes/SharedRes,
//     [10] int BitOffset, [11] UnlockedStat (1.0 looks it up as a stat, FUN_00fa6e70; 3.0 FUN_014bb710 reads its
//     bits at BitOffset), [12] list UnlockSfx, [13] OpenSfx, [14] AwardSfx, [15] list LockEntityName,
//     [16]/[17] choreography OpenCho/RewardCho
inline std::string luaQuote(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 32 || c >= 127) { char b[8]; sprintf_s(b, "\\%03u", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

inline bool chestInfoLua(const std::string& b, std::string& out, std::string* why = nullptr)
{
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    auto u32 = [&](size_t o, uint32_t& v) { if (o + 4 > b.size()) return false; memcpy(&v, b.data() + o, 4); return true; };
    uint32_t s1, s2, s3, s4, nc, nr, nk;
    if (!u32(0, s1) || !u32(s1, s2) || !u32(s1 + s2, s3) || !u32(s1 + s2 + 4, nc) || !u32(s1 + s2 + s3, s4) ||
        !u32(s1 + s2 + s3 + 4, nr) || !u32(s1 + s2 + s3 + s4 + 4, nk))
        return fail("block headers");
    size_t strings = 4, lists = s1 + 4, cho = s1 + s2 + 8, res = s1 + s2 + s3 + 8, ch = s1 + s2 + s3 + s4 + 8;
    if (s3 != 8 + nc * 0x20 || s4 != 8 + nr * 0xc || ch + (size_t)nk * 0x48 > b.size()) return fail("record blocks");
    bool ok = true;
    auto field = [&](size_t rec, int k) { uint32_t v = 0xffffffff; ok &= u32(rec + 4 * k, v); return (int32_t)v; };
    auto str = [&](int32_t off, std::string& s) {
        if (off < 0) return false;
        size_t at = strings + off, e = at < s1 + 4 ? b.find('\0', at) : npos;
        if (e == npos || e > s1 + 4) { ok = false; return false; }
        s.assign(b, at, e - at);
        return true;
    };
    auto list = [&](int32_t idx, std::string& s) {
        if (idx < 0) return false;
        s.clear();
        for (size_t p = lists + 4 * (size_t)idx + 4;; p += 4) {
            uint32_t v;
            if (p >= s1 + 4 + s2 || !u32(p, v)) { ok = false; return false; }
            if (v == 0xffffffff) break;
            std::string t;
            if (!str((int32_t)v, t)) return false;
            s += (s.empty() ? "" : ",") + t;
        }
        return true;
    };
    auto kv = [&](const char* key, const std::string& v) { out += std::string("\t\t\t") + key + " = " + luaQuote(v) + ",\n"; };
    auto kn = [&](const char* key, const std::string& v) { out += std::string("\t\t\t") + key + " = " + v + ",\n"; };
    std::string s;
    out = "-- 1.0 var/chests/ChestInfo.bin in the form of 3.0's chestInfo loader (di1compat sys.hpp)\nChestInfo =\n{\n\tResource =\n\t{\n";
    for (uint32_t i = 0; i < nr; ++i) {
        size_t r = res + 0xc * i;
        out += "\t\t[\"R" + std::to_string(i) + "\"] =\n\t\t{\n";
        if (str(field(r, 0), s)) kv("Currency", s);
        if (list(field(r, 1), s)) kv("Locks", s);
        out += "\t\t},\n";
    }
    out += "\t},\n\tChoreography =\n\t{\n";
    for (uint32_t i = 0; i < nc; ++i) {
        size_t r = cho + 0x20 * i;
        out += "\t\t[\"C" + std::to_string(i) + "\"] =\n\t\t{\n";
        if (str(field(r, 0), s)) kn("PauseAtLoadTime", s[0] == 't' || s[0] == 'T' || s[0] == '1' ? "1" : "0");
        if (str(field(r, 1), s)) kv("ChoName", s);
        if (str(field(r, 2), s)) kv("AnchorName", s);
        if (list(field(r, 3), s)) kv("LockLayer", s);
        if (list(field(r, 4), s)) kv("LockClips", s);
        if (str(field(r, 5), s)) kv("DoorLayer", s);
        if (str(field(r, 6), s)) kv("DoorOpen", s);
        if (str(field(r, 7), s)) { char f[32]; sprintf_s(f, "%.9g", atof(s.c_str())); kn("SpawnAward", f); }
        out += "\t\t},\n";
    }
    out += "\t},\n\tChests =\n\t{\n";
    for (uint32_t i = 0; i < nk; ++i) {
        size_t r = ch + 0x48 * i;
        std::string name;
        if (!str(field(r, 0), name)) return fail(ok ? "chest without a name" : "string offset");
        out += "\t\t[" + luaQuote(name) + "] =\n\t\t{\n";
        if (str(field(r, 2), s)) kv("Group", s);
        if (list(field(r, 4), s)) kv("ActorType", s);
        if (str(field(r, 5), s)) kn("Spawnable", s[0] == 't' || s[0] == 'T' || s[0] == '1' ? "1" : "0");
        if (str(field(r, 6), s)) kn("Awards", std::to_string(atoi(s.c_str())));
        if (str(field(r, 7), s)) kv("CommercialName", s);
        int32_t g = field(r, 8), h = field(r, 9), o = field(r, 16), w = field(r, 17);
        if (g >= (int32_t)nr || h >= (int32_t)nr || o >= (int32_t)nc || w >= (int32_t)nc) return fail("record index");
        if (g >= 0) kv("GuaranteedRes", "R" + std::to_string(g));
        if (h >= 0) kv("SharedRes", "R" + std::to_string(h));
        if (str(field(r, 10), s)) kn("BitOffset", std::to_string(atoi(s.c_str())));
        if (str(field(r, 11), s)) kv("UnlockedStat", s);
        if (list(field(r, 12), s)) kv("UnlockSfx", s);
        if (str(field(r, 13), s)) kv("OpenSfx", s);
        if (str(field(r, 14), s)) kv("AwardSfx", s);
        if (list(field(r, 15), s)) kv("LockEntityName", s);
        if (o >= 0) kv("OpenCho", "C" + std::to_string(o));
        if (w >= 0) kv("RewardCho", "C" + std::to_string(w));
        out += "\t\t},\n";
    }
    out += "\t},\n}\n";
    return ok || fail("offset out of range");
}

// the chestInfo loader runs once per process, after the playset locks (its callback appends; the loader skips a
// path it has seen, FUN_018daf30)
static const char* const kChest10Prefix = "di1sys/chest10/";   // + the 1.0 path with .lua for .bin
static const char* const kChest10 = "di1sys/chest10/var/chests/chestinfo.lua";

// ---- PS-8: pop-in of 1.0 expendable definitions ----
// ExpendablesDefBase (ctor FUN_016022c0, one Lua entry) keeps the pop-in range squared at +0x9c and (range+14)^2 at
// +0xa0: both stay 0 unless the entry has PopinDistance (an explicit 0 gives 0/196). With 0 a 3.0 expendable never
// pops in and its model is never requested (FORMATS 11, in-game). 1.0 has no pop-in at all: no PopinDistance string
// in the 1.0 exe, and its socket handler FUN_00f8b930 binds each world expendable at load. 3.0's own packages leave
// the key out on purpose (2,910 of their 4,404 entries, e.g. all 205 of tools.lua; 1.0: 0 of 2,371), so only entries
// of the packages 3.0 does not ship get the 1.0 behaviour, always in range.
static const float kPopin10 = 100000.0f;   // 100 km: every distance inside a zone
inline bool applyPopin10(void* def)
{
    float* f = (float*)((uint8_t*)def + 0x9c);
    if (f[0] != 0 || f[1] != 0) return false;
    f[0] = kPopin10 * kPopin10;
    f[1] = (kPopin10 + 14) * (kPopin10 + 14);
    return true;
}
inline thread_local int g_pkg10 = -1;   // definitions given the 1.0 pop-in in the 1.0 package loading now; -1: none

}  // namespace sys
