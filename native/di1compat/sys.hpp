// sys track: 1.0 global tables and progression records routed into 3.0's GameDB (research PLAN.md PS-5, PS-8,
// GLB-1). Pure text/data helpers here; the engine glue (hooks, opens) stays in di1compat.cpp.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

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
// A 1.0 playset DataMap loads its playset locks from GameDB/<folder>/locks__lua.lua (Incredibles, MonstersU, Pirates,
// Cars, LoneRanger; the folder is not always the playset's name: MonstersU's bitmasks are GameDB/Monsters/). 3.0
// ships a file at GameDB/Incredibles/locks__lua.lua (18 records), so the engine reads 3.0's and the 1.0 table (66)
// never loads: its 55 INCREDIBLES_STAR records (31 named by the 1.0 missions' GoldStar contexts / Eval_LockState) are
// absent. 1.0 also loaded its core table at boot (startup2.zip gamedb/core/locks__lua.lua, 2,325 records, 1,757 of
// them in no 3.0 locks table by UID, 0 by name; CUST_BH_Frozone_B, the lock reward of a 1.0 mission, is one). After a
// playset file a 1.0 DataMap named (dbTrigger10), the same 3.0 loader is run on that path's 1.0 table and on the core
// table, both served through kLocks10Prefix: only the records whose UID the Locks DB does not hold yet (merge by
// DBName, FUN_01612fa0; a duplicate UID re-points the record index, FUN_0149f900, so duplicates are never sent).
static const char* const kLocks10Prefix = "di1sys/locks10/";
static const char* const kLocks10Core = "di1sys/locks10/gamedb/core/locks__lua.lua";

// The GameDB tables 1.0 playset DataMaps name ("GameDB|GameDB/<folder>/<file>.lua" entries, seen as the DataMap is
// served: dbTrigger10Add), lower case. A load of one of them is where a 1.0 playset's tables come in, whatever the
// playset: the locks and bitMask loaders run the 1.0 supplements after it. 3.0's own DataMaps name compiled .chd.
inline std::set<std::string>& dbTriggers10()
{
    static std::set<std::string> s;
    return s;
}
inline std::mutex& dbTriggers10Lock()
{
    static std::mutex m;
    return m;
}
inline void dbTrigger10Add(const std::string& entry)
{
    static const char kind[] = "GameDB|";
    const size_t k = sizeof(kind) - 1;
    if (entry.compare(0, k, kind)) return;
    size_t bar = entry.find('|', k);
    std::string p = entry.substr(k, bar == std::string::npos ? std::string::npos : bar - k);
    for (char& c : p) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    std::lock_guard<std::mutex> l(dbTriggers10Lock());
    dbTriggers10().insert(p);
}
// path (as the loader got it) is a GameDB table a 1.0 DataMap named, and its file name is `file` ("locks__lua.lua")
inline bool dbTrigger10(const char* path, const char* file)
{
    std::string p = path;
    for (char& c : p) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    size_t n = strlen(file);
    if (p.size() <= n || p.compare(p.size() - n, n, file) || p[p.size() - n - 1] != '/') return false;
    std::lock_guard<std::mutex> l(dbTriggers10Lock());
    return dbTriggers10().count(p) != 0;
}

// p .. p + n can be read now: every page committed, not PAGE_NOACCESS / PAGE_GUARD. The one pointer check of the
// plugin (safeCopy, cstr, the probes). Each page is asked of the working set first (QueryWorkingSetEx, constant time;
// a resident page with a protection is readable: no-access and guard pages are never resident); only a page that is
// not resident goes to VirtualQuery. VirtualQuery alone walks every touched page from p to the end of p's region to
// size it (offline vqbench.cpp: 0.19 ms at the start of a touched 64 MB region, 5.1 ms at 768 MB; 0.4 us either way
// for QueryWorkingSetEx), and the engine's strings sit in its big arena: in-game 2026-10-14 (run 15:02, loadprof10)
// these checks were 85% of the main thread in play (7 fps) and ~100 ms of each served .oct's transcode.
inline bool readable(const void* p, size_t n)
{
    uintptr_t a = (uintptr_t)p, end = a + (n ? n : 1);
    if (a < 0x10000 || end < a) return false;
    for (uintptr_t pg = a & ~(uintptr_t)0xfff; pg < end;) {
        PSAPI_WORKING_SET_EX_INFORMATION ws{};
        ws.VirtualAddress = (void*)pg;
        DWORD prot = K32QueryWorkingSetEx(GetCurrentProcess(), &ws, sizeof(ws)) && ws.VirtualAttributes.Valid
                         ? (DWORD)ws.VirtualAttributes.Win32Protection : 0;
        if (prot && !(prot & (PAGE_NOACCESS | PAGE_GUARD))) { pg += 0x1000; continue; }
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void*)pg, &m, sizeof(m)) || m.State != MEM_COMMIT || !m.Protect || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        pg = (uintptr_t)m.BaseAddress + m.RegionSize;   // the rest of that region is readable too
    }
    return true;
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
    auto u32 = [&](size_t o, uint32_t& v) { if (o >= b.size() || b.size() - o < 4) return false; memcpy(&v, b.data() + o, 4); return true; };
    uint32_t s1, s2, s3, s4, nc, nr, nk;
    if (!u32(0, s1) || !u32(s1, s2) || !u32(s1 + s2, s3) || !u32(s1 + s2 + 4, nc) || !u32(s1 + s2 + s3, s4) ||
        !u32(s1 + s2 + s3 + 4, nr) || !u32(s1 + s2 + s3 + s4 + 4, nk))
        return fail("block headers");
    // sizes in 64 bits: a corrupt count must not wrap past the file size (the record loops run to these counts)
    if (s3 != 8 + (uint64_t)nc * 0x20 || s4 != 8 + (uint64_t)nr * 0xc || (uint64_t)s1 + s2 + s3 + s4 + 8 + (uint64_t)nk * 0x48 > b.size())
        return fail("record blocks");
    size_t strings = 4, lists = s1 + 4, cho = s1 + s2 + 8, res = s1 + s2 + s3 + 8, ch = s1 + s2 + s3 + s4 + 8;
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

// ---- GLB-1: var/perceptions/PerceptionInfo.bin -> 3.0's perceptions loader ----
// 1.0 PerceptionInfo ctor FUN_00f7ac10 maps the .bin: [u32 size, string pool] [u32 size, list words] then 9 blocks
// [size, n, n records]: Detect 0x4c, TargetMode 0x10, Reticule 0x14, Ricochet 0xc, PartnerCheck 0x18,
// InteractionCheck 0x10, Groups 0x10, UIText 0x14, Perceptions 0x60. Fields are string offsets (-1 none), block indices,
// or list indices; a plain list (FUN_00f6bf10) is read in place up to -1, a TypePriority list (FUN_00f76830) and a
// state-priority list (FUN_00f70300) start with a 0 flag word. 3.0 keeps the system (PerceptionInfo DAT_02289700, ctor
// FUN_014b6560, DBFileLoader_Callback "perceptions") and reads the Lua global PerceptionInfo in callback FUN_014afd30,
// which appends every block to the arrays it already holds and resolves references by record name; Perceptions go in a
// name map that a later load overwrites (FUN_0149a090), so only the perceptions 3.0 lacks are sent (37 of 250: AvatarChest,
// Activity*, Summon*, Ricochet_* ...), their Detect/UIText/.. records under new DI1_ names. Slot -> key: the 3.0 struct
// slot the callback stores each key in; tested over the 213 perceptions both games have (perccheck.py): every Detect key
// equal in >= 196/210 records once 1.0 slot 15 is read as maxHeightAboveGround (210/210; 3.0 inserted
// targetPersistence before it), TargetMode 37-40/40, UIText 49-50/50, Groups by GroupName 211/213. A 1.0 group is the 3.0
// group with its GroupName (all 31 exist); 1.0 Reticule's three floats have no proven 3.0 key and are not sent.
// Booleans go as 0/1: 3.0 reads them as numbers (type 3).
struct Perc30 {
    std::function<bool(const std::string&)> has;                // 3.0 holds a perception of this name
    std::function<std::string(const std::string&)> group;       // 3.0 Groups record with this GroupName, "" if none
};

inline bool perceptionInfoLua(const std::string& b, const Perc30& p30, std::string& out, int& added, std::string* why = nullptr)
{
    enum { DET, TM, RET, RIC, PC, IC, GRP, UI, PER, NB };
    static const uint32_t rs[NB] = {0x4c, 0x10, 0x14, 0x0c, 0x18, 0x10, 0x10, 0x14, 0x60};
    const uint32_t NONE = 0xffffffff;
    added = 0;
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    auto u32 = [&](size_t o, uint32_t& v) { if (o >= b.size() || b.size() - o < 4) return false; memcpy(&v, b.data() + o, 4); return true; };
    uint32_t s1, s2;
    if (!u32(0, s1) || s1 < 4 || !u32(s1, s2) || s2 < 4 || (uint64_t)s1 + s2 > b.size()) return fail("pool headers");
    const size_t lw = s1 + 4, lwEnd = (size_t)s1 + s2;
    size_t base[NB], o = lwEnd;
    uint32_t n[NB];
    for (int k = 0; k < NB; ++k) {
        uint32_t size;
        if (!u32(o, size) || !u32(o + 4, n[k]) || size != 8 + (uint64_t)n[k] * rs[k] || (uint64_t)o + size > b.size()) return fail("record blocks");
        base[k] = o + 8;
        o += size;
    }
    if (o != b.size()) return fail("trailing bytes");
    bool ok = true;
    auto field = [&](int blk, uint32_t i, int f) { uint32_t v = NONE; ok &= u32(base[blk] + (size_t)i * rs[blk] + 4 * f, v); return v; };
    auto str = [&](uint32_t off, std::string& s) {
        if (off == NONE) return false;
        size_t at = 4 + (size_t)off, e = at < s1 ? b.find('\0', at) : npos;
        if (e == npos || e >= s1) { ok = false; return false; }
        s.assign(b, at, e - at);
        return true;
    };
    auto word = [&](size_t i, uint32_t& v) {
        if (lw + 4 * i + 4 > lwEnd) { ok = false; return false; }
        return u32(lw + 4 * i, v);
    };
    auto words = [&](uint32_t i, std::vector<uint32_t>& w) {
        w.clear();
        uint32_t v;
        if (i != NONE)
            for (size_t k = i; word(k, v) && v != NONE; ++k) w.push_back(v);
    };
    std::string s;
    auto num = [&](uint32_t off) { char f[32]; sprintf_s(f, "%.9g", str(off, s) ? atof(s.c_str()) : 0.0); return std::string(f); };
    auto flag = [&](uint32_t off) { return std::string(str(off, s) && (s[0] == 't' || s[0] == 'T' || s[0] == '1') ? "1" : "0"); };
    auto inum = [&](uint32_t off) { return std::to_string(str(off, s) ? atoi(s.c_str()) : 0); };
    auto name = [](const char* pre, uint32_t i) { return std::string(pre) + std::to_string(i); };

    // the perceptions 3.0 lacks and every record they reach
    std::vector<uint32_t> per, w;
    std::set<uint32_t> use[NB], tpl, spl;
    for (uint32_t i = 0; i < n[PER]; ++i) {
        if (!str(field(PER, i, 0), s)) return fail(ok ? "perception without a name" : "string offset");
        if (p30.has(s)) continue;
        per.push_back(i);
        static const struct { int f, blk; } ix[] = {{2, GRP}, {18, DET}, {19, TM}, {21, RET}, {22, RIC}};
        for (auto& x : ix) {
            uint32_t v = field(PER, i, x.f);
            if (v == NONE) continue;
            if (v >= n[x.blk]) return fail("record index");
            use[x.blk].insert(v);
        }
        static const struct { int f, blk; } il[] = {{9, PC}, {14, IC}, {20, UI}};
        for (auto& x : il) {
            words(field(PER, i, x.f), w);
            for (uint32_t v : w) { if (v >= n[x.blk]) return fail("list record index"); use[x.blk].insert(v); }
        }
    }
    for (uint32_t d : use[DET]) {
        uint32_t t = field(DET, d, 18), v;
        if (t == NONE) continue;
        tpl.insert(t);
        for (size_t k = (size_t)t + 1; word(k, v) && v != NONE; k += 3) {
            uint32_t val, isRef;
            if (!word(k + 1, val) || !word(k + 2, isRef)) break;
            if (flag(isRef) == "1") spl.insert(val);
        }
    }
    if (!ok) return fail("offset out of range");

    auto kv = [&](const char* key, uint32_t off) { if (str(off, s)) out += std::string("\t\t\t") + key + " = " + luaQuote(s) + ",\n"; };
    auto kraw = [&](const char* key, const std::string& v) { out += std::string("\t\t\t") + key + " = " + v + ",\n"; };
    auto kn = [&](const char* key, uint32_t off) { if (off != NONE) kraw(key, num(off)); };
    auto kb = [&](const char* key, uint32_t off) { if (off != NONE) kraw(key, flag(off)); };
    auto ki = [&](const char* key, uint32_t off) { if (off != NONE) kraw(key, inum(off)); };
    auto refs = [&](const char* key, uint32_t list, const char* pre) {
        words(list, w);
        std::string v;
        for (uint32_t x : w) v += (v.empty() ? "" : ",") + name(pre, x);
        if (!v.empty()) kraw(key, luaQuote(v));
    };
    auto section = [&](const char* sec) { out += std::string("\t") + sec + " =\n\t{\n"; };
    auto rec = [&](const std::string& key) { out += "\t\t[" + luaQuote(key) + "] =\n\t\t{\n"; };
    const char* endRec = "\t\t},\n";

    out = "-- 1.0 var/perceptions/PerceptionInfo.bin: the perceptions 3.0 lacks, in the form of 3.0's perceptions loader"
          " (di1compat sys.hpp)\nPerceptionInfo =\n{\n";
    section("StatePriority");
    for (uint32_t l : spl) {
        rec(name("DI1_SP", l));
        uint32_t st, v;
        for (size_t k = (size_t)l + 1; word(k, st) && st != NONE && word(k + 1, v); k += 2)
            if (str(st, s)) {
                std::string key = "[" + luaQuote(s) + "]";
                kraw(key.c_str(), num(v));
            }
        out += endRec;
    }
    out += "\t},\n";
    section("TypePriority");
    for (uint32_t l : tpl) {
        rec(name("DI1_TP", l));
        uint32_t ty, v, isRef;
        for (size_t k = (size_t)l + 1; word(k, ty) && ty != NONE && word(k + 1, v) && word(k + 2, isRef); k += 3)
            if (str(ty, s)) {
                std::string key = "[" + luaQuote(s) + "]";
                kraw(key.c_str(), flag(isRef) == "1" ? luaQuote(name("DI1_SP", v)) : num(v));
            }
        out += endRec;
    }
    out += "\t},\n";
    section("Detect");
    static const char* const detF[] = {"AggroRadius", "PriorityOverrideThreshhold", "FocusWeight", "Distance", "AwareDistance",
                                       "nearHeight", "farHeight", "useCenter", "focusMethod", "FOV", "FOVNotMoving",
                                       "ellipsePercent", "ellipsePercentNotMoving", "lineOfSight", "targetFacing",
                                       "maxHeightAboveGround", "foundTargetIgnoresPriority", "allowRumpusTerrain"};
    for (uint32_t d : use[DET]) {
        rec(name("DI1_D", d));
        for (int f = 0; f < 18; ++f) {
            uint32_t v = field(DET, d, f);
            if (f == 8) kv(detF[f], v);
            else if (f == 7 || f == 13 || f == 16 || f == 17) kb(detF[f], v);
            else kn(detF[f], v);
        }
        if (field(DET, d, 18) != NONE) kraw("TypePriority", luaQuote(name("DI1_TP", field(DET, d, 18))));
        out += endRec;
    }
    out += "\t},\n";
    section("TargetMode");
    for (uint32_t t : use[TM]) {
        rec(name("DI1_TM", t));
        kn("Distance", field(TM, t, 0)); kn("Distance2Radius", field(TM, t, 1));
        kb("lineOfSight", field(TM, t, 2)); kb("allowRumpusTerrain", field(TM, t, 3));
        out += endRec;
    }
    out += "\t},\n";
    section("Reticule");
    for (uint32_t r : use[RET]) {   // ponytail: floats 2..4 unmapped (no proven 3.0 key); map them if a 1.0-only reticle matters
        rec(name("DI1_R", r));
        kv("ChoName", field(RET, r, 0)); kv("Anchor", field(RET, r, 1));
        out += endRec;
    }
    out += "\t},\n";
    section("Ricochet");
    for (uint32_t r : use[RIC]) {
        rec(name("DI1_RC", r));
        ki("MaxRicochets", field(RIC, r, 0)); ki("NumRequiredNetworkedHandles", field(RIC, r, 1)); kv("Perception", field(RIC, r, 2));
        out += endRec;
    }
    out += "\t},\n";
    section("PartnerCheck");
    static const char* const pcF[] = {"Interaction", "Initiator", "PartnerMustBeOfThisType", "PartnerMustHaveAllTheseStates",
                                      "PartnerMustHaveAnyOfTheseStates", "PartnerCannotHaveAnyOfTheseStates"};
    for (uint32_t r : use[PC]) {
        rec(name("DI1_PC", r));
        for (int f = 0; f < 6; ++f) f == 1 ? kb(pcF[f], field(PC, r, f)) : kv(pcF[f], field(PC, r, f));
        out += endRec;
    }
    out += "\t},\n";
    section("InteractionCheck");
    for (uint32_t r : use[IC]) {
        rec(name("DI1_IC", r));
        kv("TargetActorType", field(IC, r, 0)); kv("Interaction", field(IC, r, 1));
        kv("InteractionActorType", field(IC, r, 2)); kb("InteractionCentric", field(IC, r, 3));
        out += endRec;
    }
    out += "\t},\n";
    section("Groups");
    std::vector<std::string> grp(n[GRP]);
    for (uint32_t g : use[GRP]) {
        if (str(field(GRP, g, 0), s) && !(grp[g] = p30.group(s)).empty()) continue;
        grp[g] = name("DI1_G", g);
        rec(grp[g]);
        kv("GroupName", field(GRP, g, 0)); ki("MaxTargets", field(GRP, g, 2)); kb("OnePerceptionAllowed", field(GRP, g, 3));
        out += endRec;
    }
    out += "\t},\n";
    section("UIText");
    for (uint32_t u : use[UI]) {
        rec(name("DI1_U", u));
        kv("Text", field(UI, u, 0)); kv("Button", field(UI, u, 1)); ki("Priority", field(UI, u, 2));
        kn("Timer", field(UI, u, 3)); kv("WiiMotebutton", field(UI, u, 4));
        out += endRec;
    }
    out += "\t},\n";
    section("Perceptions");
    static const struct { int f; const char* key; char kind; } perF[] = {
        {3, "FromTrigger", 'b'}, {4, "AutoEliminateTargets", 'b'}, {5, "PerceiverMustBeOfThisType", 's'},
        {6, "PerceiverMustHaveAllTheseStates", 's'}, {7, "PerceiverMustHaveAnyOfTheseStates", 's'},
        {8, "PerceiverCannotHaveAnyOfTheseStates", 's'}, {10, "TargetMustBeOfThisType", 's'},
        {11, "TargetMustHaveAllTheseStates", 's'}, {12, "TargetMustHaveAnyOfTheseStates", 's'},
        {13, "TargetCannotHaveAnyOfTheseStates", 's'}, {15, "PerceiveRider", 'b'}, {16, "BasePriority", 'f'},
        {17, "AllowInTargetMode", 'b'}};
    for (uint32_t i : per) {
        str(field(PER, i, 0), s);
        rec(s);
        uint32_t g = field(PER, i, 2);
        if (g != NONE) kraw("Groups", luaQuote(grp[g]));
        for (auto& x : perF) {
            uint32_t v = field(PER, i, x.f);
            x.kind == 'b' ? kb(x.key, v) : x.kind == 'f' ? kn(x.key, v) : kv(x.key, v);
        }
        static const struct { int f; const char *key, *pre; } ref[] = {{18, "Detect", "DI1_D"}, {19, "TargetMode", "DI1_TM"},
                                                                       {21, "Reticule", "DI1_R"}, {22, "Ricochet", "DI1_RC"}};
        for (auto& x : ref)
            if (field(PER, i, x.f) != NONE) kraw(x.key, luaQuote(name(x.pre, field(PER, i, x.f))));
        refs("PartnerCheck", field(PER, i, 9), "DI1_PC");
        refs("InteractionCheck", field(PER, i, 14), "DI1_IC");
        refs("UIText", field(PER, i, 20), "DI1_U");
        words(field(PER, i, 23), w);
        std::string cc, c;
        for (uint32_t x : w) if (str(x, c)) cc += (cc.empty() ? "" : ",") + c;
        if (!cc.empty()) kraw("ControlContext", luaQuote(cc));
        out += endRec;
        ++added;
    }
    out += "\t},\n}\n";
    return ok || fail("offset out of range");
}

// the perceptions supplement is sent once the playset's bitmasks (and kBits10) are in, so its 1.0 actor types resolve;
// the 3.0 names / groups are read from PerceptionInfo before the load (the open that serves it may run on another thread)
static const char* const kPerc10Prefix = "di1sys/perc10/";   // + the 1.0 path with .lua for .bin
static const char* const kPerc10 = "di1sys/perc10/var/perceptions/perceptioninfo.lua";
inline std::mutex g_percMx;
inline std::set<std::string> g_perc30;
inline std::map<std::string, std::string> g_group30;   // GroupName -> 3.0 Groups record name (first, as 3.0's lookup)
inline Perc30 perc30()
{
    std::lock_guard<std::mutex> l(g_percMx);
    auto names = g_perc30;
    auto groups = g_group30;
    return {[names](const std::string& s) { return names.count(s) != 0; },
            [groups](const std::string& s) { auto it = groups.find(s); return it == groups.end() ? std::string() : it->second; }};
}
// PerceptionInfo (DAT_02289700): Perceptions +0x100 records / +0x104 count, record +0 name; Groups +0xe0 / +0xe4,
// record +0 name, +8 GroupName (callback FUN_014afd30). False if the object is not readable.
inline bool snapshotPerc30(const uint8_t* pi)
{
    std::set<std::string> names;
    std::map<std::string, std::string> groups;
    auto cstr = [](const void* p, std::string& s) {
        if (!readable(p, 1)) return false;
        const char* c = (const char*)p;
        size_t k = 0;
        while (k < 256 && readable(c + k, 1) && c[k]) ++k;
        s.assign(c, k);
        return k < 256;
    };
    for (int sec = 0; sec < 2; ++sec) {
        size_t at = sec ? 0xe0 : 0x100;
        if (!readable(pi + at, 8)) return false;
        void* const* arr = *(void* const**)(pi + at);
        int cnt = *(const int*)(pi + at + 4);
        if (cnt < 0 || cnt > 100000 || (cnt && !readable(arr, cnt * 4))) return false;
        for (int i = 0; i < cnt; ++i) {
            const uint8_t* r = (const uint8_t*)arr[i];
            std::string a, g;
            if (!readable(r, 12) || !cstr(*(void* const*)r, a)) continue;
            if (!sec) names.insert(a);
            else if (cstr(*(void* const*)(r + 8), g)) groups.emplace(g, a);
        }
    }
    std::lock_guard<std::mutex> l(g_percMx);
    g_perc30.swap(names);
    g_group30.swap(groups);
    return true;
}

// the chestInfo loader runs once per process, after the playset locks (its callback appends; the loader skips a
// path it has seen, FUN_018daf30)
static const char* const kChest10Prefix = "di1sys/chest10/";   // + the 1.0 path with .lua for .bin
static const char* const kChest10 = "di1sys/chest10/var/chests/chestinfo.lua";

// ---- PS-5: the 1.0 core bitmasks, as one more bitMask package ----
// 1.0's core bitmasks.lua has 201 masks no 3.0 package has (by set + name; 0 UID collisions within a set): AvatarAction
// 102 (the blockhead BH* actions), ActorType 50, ActorState 27, ActorTag 16 (MissionGivers_Active/_Available ...),
// QBGroup 2 (ScourgeQuarterback, IntroQuarterback), BehaviorMod 2, Interaction 1, Interrupt 1
// (GrabbedByZeroPointEnergy), and 5 ActorType + 1 ActorState filters. 3.0 merges every bitMask package into the same
// sets (bitmask callback FUN_00711020: a mask already in its set is flagged and skipped, FUN_0070e7a0; an existing
// filter is OR-ed with the package's, FUN_00710d60 -> FUN_006f99c0; 3.0's own packages grow sets past their declared
// count, e.g. ActorType 167 declared, 918 masks), so the 1.0 file goes in verbatim after the playset's.
static const char* const kRaw10Prefix = "di1sys/raw10/";   // + a 1.0 path 3.0 also has: the 1.0 bytes
static const char* const kBits10 = "di1sys/raw10/gamedb/core/bitmasks.lua";   // after a bitmasks.lua a 1.0 DataMap named

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
