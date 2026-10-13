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

}  // namespace sys
