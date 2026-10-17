// teardowntrace probe: who frees a Layout-held DataNode inside its own dtor. core. Engine threads; per dtor, per free.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"
#include "../core/stack.hpp"

namespace trace::teardown {

struct Config {
    bool on = false;   // teardowntrace
};

// Installs the probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kEntryTable = 0x021B739C;   // *: the hierarchy entry table (FUN_004b46d0 / FUN_004b2800)
constexpr uint32_t kBlockTracker = 0x021B73A0; // *: the allocation-block tracker (FUN_00416840 / FUN_00412050)
constexpr uint32_t kTdImageSpan = 0x0209B000;  // the exe image as these lines have read it (A-47: core's is 0x2100000)
// MSVC RTTI: vtable[-1] = complete object locator, +12 its type descriptor, whose name is at +8
constexpr uint32_t kVtLocator = 4;
constexpr uint32_t kLocatorType = 12;
constexpr uint32_t kTypeName = 8;
constexpr uint32_t kObjRef = 4;                // object +4: the refcount word
// Zoo::Layout (FUN_00787bc0): placements +0x40 array, +0x44 count, 0x18 each
constexpr uint32_t kLayoutPlacements = 0x40;
constexpr uint32_t kLayoutCount = 0x44;
constexpr uint32_t kPlacementBytes = 0x18;
constexpr uint32_t kPlacementScene = 4;        // +4 scene node, +8 name, +0xc DataNode, +0x10 flags
constexpr uint32_t kPlacementName = 8;
constexpr uint32_t kPlacementNode = 0xc;
constexpr uint32_t kPlacementFlags = 0x10;
constexpr uint32_t kNodeHandle = 0xc;          // Samurai::Node +0xc: its hierarchy handle
// a node handle as FUN_004b46d0 decodes it into its 0x14-byte hierarchy entry
constexpr uint32_t kHandleBits = 0x7ffff;      // all set: no entry
constexpr uint32_t kHandlePaged = 0x40000;
constexpr uint32_t kDirectPages = 0x18;        // table +0x18: direct pages of 0x1000 entries
constexpr uint32_t kDirectShift = 12;
constexpr uint32_t kDirectIndex = 0xfff;
constexpr uint32_t kPagedPages = 0x130;        // table +0x130: paged directory
constexpr uint32_t kPagedDir = 0x1fff7fff;
constexpr uint32_t kPagedDirShift = 9;
constexpr uint32_t kPagedPage = 0x1ff;
constexpr uint32_t kPagedIndex = 7;
constexpr uint32_t kEntryBytes = 0x14;
constexpr uint32_t kEntryTarget = 0x10;        // entry +0x10: its tagged target (bit 0 a tag)
// the tracker: table at +0x10, u16 count at table+2, entries 0x10 each at table+4: {count | flags, ?, size, base}
constexpr uint32_t kTrackerTable = 0x10;
constexpr uint32_t kTrackerEntries = 4;
constexpr uint32_t kTrackerEntryBytes = 0x10;
constexpr uint32_t kTrackCount = 0x0fffffff;
constexpr uint32_t kTrackFlagsShift = 28;
}  // namespace eng

constexpr uint32_t kUnread = 0xDEADBEEF;        // rd()'s answer for an unreadable word
constexpr uintptr_t kLowestMapped = 0x10000;    // nothing is mapped below 64 KiB
constexpr uint32_t kNotCommitted = 0x10000000;  // prot(): this bit + the region state when not committed
constexpr LONG kLayoutsLogged = 3000;
constexpr uint32_t kPlacementsListed = 8;
constexpr uint32_t kPlacementsBound = 64;
constexpr LONG kLinesLogged = 30000;
constexpr LONG kEarlyLogged = 200;
constexpr int kFreesPerLayout = 80;
constexpr int kEarlyStackWords = 24;

typedef uint32_t(__fastcall* DtorFn)(void* self, void* edx);
typedef void(__fastcall* EntryFn)(void* self, void* edx, uint32_t a1);
typedef uint32_t(__fastcall* PlBindFn)(void* self, void* edx, uint32_t a1, uint32_t a2, uint32_t a3);
typedef char(__fastcall* TrackFreeFn)(void* self, void* edx, uint32_t obj, uint32_t* base, int* remaining);
typedef void(__cdecl* HeapFreeFn)(uint32_t p);

struct State {
    // originals: set once by hook() in install()
    DtorFn g_layoutDtor = nullptr;
    DtorFn g_nodeDtor = nullptr;
    EntryFn g_entryRel = nullptr;
    EntryFn g_entryFree = nullptr;
    PlBindFn g_plBind = nullptr;
    TrackFreeFn g_trackFree = nullptr;
    HeapFreeFn g_heapFree = nullptr;
    std::atomic<LONG> layouts{0}, lines{0}, early{0};   // budgets; any thread
    std::mutex watchMx;                                 // guards watch; bind lines are logged under it
    std::map<uintptr_t, uintptr_t> watch;               // placement DataNode -> its Layout (bound, alive)
};
inline State g_state;

// The Layout dtor the calling thread is in: per thread, not per fiber (A-44).
struct Tls {
    int depth;         // Layout dtors running
    uintptr_t node;    // the node whose dtor runs inside one
    uintptr_t outer;   // placement 0's node of the outermost: frees are logged against it
    int frees;         // tracker and heap free lines in this Layout dtor
};
inline thread_local Tls g_tls;

inline bool readable(uintptr_t p, size_t n) { return core::readable((const void*)p, n); }
inline uint32_t rd(uintptr_t p) { return readable(p, 4) ? *(const uint32_t*)p : kUnread; }

// The page protection at p, or kNotCommitted | its state (0: below 64 KiB or not queried).
inline uint32_t prot(uintptr_t p)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (p < kLowestMapped || !VirtualQuery((void*)p, &mbi, sizeof(mbi))) return 0;
    return mbi.State == MEM_COMMIT ? mbi.Protect : kNotCommitted | mbi.State;
}

inline uint32_t staticVa(uint32_t v)
{
    const bool exe = v >= core::g_base && v < core::g_base + eng::kTdImageSpan;
    return exe ? core::toStaticVa(v) : v;
}

// "ptr vt=<static> <rtti> ref=<word> prot=<protect> alloc=<base>"; every caller passes 200 bytes.
inline void obj(char* out, size_t cap, uintptr_t p)
{
    if (!readable(p, 8)) {
        snprintf(out, cap, "%08X (unreadable, prot %X)", (uint32_t)p, prot(p));
        return;
    }
    uint32_t vt = rd(p), col = rd(vt - eng::kVtLocator), td = rd(col + eng::kLocatorType);
    char name[96] = "";
    if (td != kUnread) core::safeCopy(name, sizeof(name), (const char*)(uintptr_t)(td + eng::kTypeName));
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery((void*)p, &mbi, sizeof(mbi));
    // never cut: at most 158 characters
    snprintf(out, cap, "%08X vt=%08X %s ref=%08X prot=%X alloc=%08X", (uint32_t)p, staticVa(vt), name,
             rd(p + eng::kObjRef), prot(p), (uint32_t)(uintptr_t)mbi.AllocationBase);
}

// A node handle's hierarchy entry, 0 if none or unreadable.
inline uintptr_t entryOf(uint32_t handle)
{
    uint32_t h = handle & eng::kHandleBits, t = rd(core::rt(eng::kEntryTable)), e;
    if (h == eng::kHandleBits || t == kUnread) return 0;
    if (!(h & eng::kHandlePaged)) {
        e = rd(t + eng::kDirectPages + (h >> eng::kDirectShift) * 4) + (h & eng::kDirectIndex) * eng::kEntryBytes;
    } else {
        uint32_t dir = rd(t + eng::kPagedPages + (((h >> 3) & eng::kPagedDir) >> eng::kPagedDirShift) * 4);
        e = rd(dir + ((h >> 3) & eng::kPagedPage) * 4) + (h & eng::kPagedIndex) * eng::kEntryBytes;
    }
    return readable(e, eng::kEntryBytes) ? e : 0;
}

inline void entryLog(const char* what, uintptr_t e)
{
    if (!e) {
        core::logf("td   %s entry: none", what);
        return;
    }
    char a[200], b[200];
    obj(a, sizeof(a), rd(e) & ~1u);
    obj(b, sizeof(b), rd(e + eng::kEntryTarget) & ~1u);
    core::logf("td   %s entry %08X: [0]=%08X [4]=%08X [8]=%08X [c]=%08X [10]=%08X; [0]-> %s; [10]-> %s", what,
               (uint32_t)e, rd(e), rd(e + 4), rd(e + 8), rd(e + 12), rd(e + eng::kEntryTarget), a, b);
}

// The tracked block holding p ("untracked" if none); callers pass 160 bytes, a line is at most 58.
inline void block(char* out, size_t cap, uintptr_t p)
{
    uint32_t tr = rd(core::rt(eng::kBlockTracker)), tbl = tr == kUnread ? kUnread : rd(tr + eng::kTrackerTable);
    if (tbl == kUnread || !tbl || !readable(tbl, 4)) {
        snprintf(out, cap, "blk(%08X)=no tracker", (uint32_t)p);
        return;
    }
    uint32_t n = rd(tbl) >> 16;
    if (!readable(tbl + eng::kTrackerEntries, (size_t)n * eng::kTrackerEntryBytes)) {
        snprintf(out, cap, "blk(%08X)=tracker unreadable", (uint32_t)p);
        return;
    }
    const uint32_t* e = (const uint32_t*)(uintptr_t)(tbl + eng::kTrackerEntries);
    for (uint32_t i = 0; i < n; ++i, e += 4) {
        if (e[3] > p || p >= e[3] + e[2]) continue;
        snprintf(out, cap, "blk(%08X)=[%08X +%X count %u flags %X]", (uint32_t)p, e[3], e[2], e[0] & eng::kTrackCount,
                 e[0] >> eng::kTrackFlagsShift);
        return;
    }
    snprintf(out, cap, "blk(%08X)=untracked (%u blocks)", (uint32_t)p, n);
}

// One placement of a Layout being destroyed: its scene node, DataNode, entry and blocks.
inline void logPlacement(uint32_t k, uintptr_t el)
{
    State& s = g_state;
    char sc[200], d[200], nm[64] = "";
    obj(sc, sizeof(sc), rd(el + eng::kPlacementScene));
    obj(d, sizeof(d), rd(el + eng::kPlacementNode));
    core::safeCopy(nm, sizeof(nm), (const char*)(uintptr_t)rd(el + eng::kPlacementName));
    uint32_t node = rd(el + eng::kPlacementNode);
    bool watched;
    {
        std::lock_guard<std::mutex> lock(s.watchMx);
        watched = s.watch.count(node) != 0;
    }
    core::logf("td  placement %u '%s' flags %X: scene %s; node %s handle %08X (bound and alive: %s)", k, nm,
               rd(el + eng::kPlacementFlags), sc, d, rd(node + eng::kNodeHandle), watched ? "yes" : "no/never bound");
    if (!node || node == kUnread) return;
    entryLog("node", entryOf(rd(node + eng::kNodeHandle)));
    char b[160], eb[160];
    block(b, sizeof(b), node);
    block(eb, sizeof(eb), entryOf(rd(node + eng::kNodeHandle)));
    core::logf("td   node %s; its entry %s", b, eb);
}

inline void logPlacements(uintptr_t layout)
{
    uint32_t arr = rd(layout + eng::kLayoutPlacements), n = rd(layout + eng::kLayoutCount);
    core::logf("td layout %08X dtor: %u placements (array %08X)", (uint32_t)layout, n, arr);
    for (uint32_t k = 0; k < n && k < kPlacementsListed && arr != kUnread; ++k)
        logPlacement(k, arr + eng::kPlacementBytes * k);
}

// FUN_00787bc0, thiscall: Zoo::Layout dtor. Threads unverified; per Layout destroyed.
inline uint32_t __fastcall hkLayoutDtor(void* self, void*)
{
    State& s = g_state;
    Tls& t = g_tls;
    const bool on = ++s.layouts <= kLayoutsLogged;
    if (on) logPlacements((uintptr_t)self);
    uintptr_t outerPrev = t.outer;
    t.frees = 0;
    if (on) {
        ++t.depth;
        uint32_t arr = rd((uintptr_t)self + eng::kLayoutPlacements);
        uint32_t first = arr == kUnread ? 0 : rd(arr + eng::kPlacementNode);
        t.outer = first == kUnread ? 0 : first;
    }
    uint32_t r = s.g_layoutDtor(self, nullptr);
    if (on) {
        --t.depth;
        t.outer = outerPrev;
        core::logf("td layout %08X dtor done", (uint32_t)(uintptr_t)self);
    }
    return r;
}

// FUN_0077d9c0, thiscall (ret 0xc): a Layout binds its placements. Threads unverified; per instance bind.
inline uint32_t __fastcall hkPlBind(void* self, void*, uint32_t a1, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    uintptr_t layout = (uintptr_t)self;
    uint32_t arr = rd(layout + eng::kLayoutPlacements), n = rd(layout + eng::kLayoutCount);
    for (uint32_t k = 0; k < n && k < kPlacementsBound && arr != kUnread; ++k) {
        uint32_t node = rd(arr + eng::kPlacementBytes * k + eng::kPlacementNode);
        if (!node || node == kUnread) continue;
        std::lock_guard<std::mutex> lock(s.watchMx);
        const bool fresh = s.watch.emplace(node, layout).second;
        if (!fresh) continue;
        const LONG nth = ++s.lines;
        if (nth > kLinesLogged) continue;
        char o[200];
        obj(o, sizeof(o), node);
        core::logf("td bind layout %08X placement %u node %s", (uint32_t)layout, k, o);
    }
    return s.g_plBind(self, nullptr, a1, a2, a3);
}

// A placement node destroyed outside its Layout's dtor was released once too often: its stack names who.
inline void logEarly(uintptr_t node, uintptr_t layout, const uint32_t* sp)
{
    char o[200], line[600];
    obj(o, sizeof(o), node);
    // never cut: at most 277 characters before the stack's 216
    int len = snprintf(line, sizeof(line), "td PLACEMENT NODE DESTROYED OUTSIDE ITS LAYOUT DTOR (layout %08X): "
                       "%s, stack:", (uint32_t)layout, o);
    core::stackOf(sp, line, sizeof(line), len, kEarlyStackWords);
    core::logf("%s", line);
}

// Whether a line about a node dtor may be logged: inside a Layout dtor, within the budget.
inline bool lineInDtor()
{
    if (g_tls.depth <= 0 || !g_tls.node) return false;
    const LONG nth = ++g_state.lines;
    return nth <= kLinesLogged;
}

// ... about a free: inside a Layout dtor with a placement node, within this dtor's and the global budget.
inline bool freeInDtor()
{
    Tls& t = g_tls;
    if (t.depth <= 0 || !t.outer) return false;
    ++t.frees;
    if (t.frees > kFreesPerLayout) return false;
    const LONG nth = ++g_state.lines;
    return nth <= kLinesLogged;
}

// FUN_004ee5f0, thiscall: Samurai::Node dtor. Threads unverified; every node destroyed.
inline uint32_t __fastcall hkNodeDtor(void* self, void*)
{
    State& s = g_state;
    Tls& t = g_tls;
    uintptr_t prev = t.node, layout = 0;
    {
        std::lock_guard<std::mutex> lock(s.watchMx);
        auto it = s.watch.find((uintptr_t)self);
        if (it != s.watch.end()) {
            layout = it->second;
            s.watch.erase(it);
        }
    }
    if (layout && t.depth <= 0) {
        const LONG nth = ++s.early;
        if (nth <= kEarlyLogged) logEarly((uintptr_t)self, layout, (const uint32_t*)_AddressOfReturnAddress());
    }
    const LONG nth = t.depth > 0 ? ++s.lines : kLinesLogged + 1;
    if (nth <= kLinesLogged) {
        char o[200], b[160];
        obj(o, sizeof(o), (uintptr_t)self);
        block(b, sizeof(b), (uintptr_t)self);
        core::logf("td  node dtor %s handle %08X %s", o, rd((uintptr_t)self + eng::kNodeHandle), b);
        entryLog("own", entryOf(rd((uintptr_t)self + eng::kNodeHandle)));
        t.node = (uintptr_t)self;
    }
    uint32_t r = s.g_nodeDtor(self, nullptr);
    t.node = prev;
    return r;
}

// FUN_004b2780, thiscall: hierarchy entry release (arg 0 in the node dtor). Threads unverified; per release.
inline void __fastcall hkEntryRel(void* self, void*, uint32_t a1)
{
    State& s = g_state;
    if (!lineInDtor()) {
        s.g_entryRel(self, nullptr, a1);
        return;
    }
    uintptr_t e = (uintptr_t)self, node = g_tls.node;
    char o[200];
    obj(o, sizeof(o), rd(e + eng::kEntryTarget) & ~1u);
    core::logf("td   entry release %08X (arg %X) [10]=%08X -> %s; node %08X page prot %X", (uint32_t)e, a1,
               rd(e + eng::kEntryTarget), o, (uint32_t)node, prot(node));
    s.g_entryRel(self, nullptr, a1);
    core::logf("td   entry release done; node %08X page prot %X", (uint32_t)g_tls.node, prot(g_tls.node));
}

// FUN_004e1a40, thiscall on the entry table: frees a hierarchy entry (the node dtor reaches it). Threads unverified.
inline void __fastcall hkEntryFree(void* self, void*, uint32_t entry)
{
    State& s = g_state;
    if (!lineInDtor()) {
        s.g_entryFree(self, nullptr, entry);
        return;
    }
    Tls& t = g_tls;
    char b[160];
    block(b, sizeof(b), t.outer);
    entryLog("freeing", readable(entry, eng::kEntryBytes) ? entry : 0);
    core::logf("td   entry free %08X; node %08X page prot %X; placement node %s", entry, (uint32_t)t.node,
               prot(t.node), b);
    s.g_entryFree(self, nullptr, entry);
    block(b, sizeof(b), t.outer);
    core::logf("td   entry free done; node %08X page prot %X; placement node %s page prot %X", (uint32_t)t.node,
               prot(t.node), b, prot(t.outer));
}

// FUN_00416840, thiscall on the tracker (ret 0xc): an object's delete counted against its block. Threads unverified.
inline char __fastcall hkTrackFree(void* self, void*, uint32_t o, uint32_t* base, int* remaining)
{
    State& s = g_state;
    if (!freeInDtor()) return s.g_trackFree(self, nullptr, o, base, remaining);
    Tls& t = g_tls;
    char os[200], b[160], pb[160];
    obj(os, sizeof(os), o);
    block(b, sizeof(b), o);
    char r = s.g_trackFree(self, nullptr, o, base, remaining);
    block(pb, sizeof(pb), t.outer);
    core::logf("td    tracker delete %s %s -> %d (block %08X, %d left); placement node %08X %s page prot %X", os, b, r,
               base ? *base : 0, remaining ? *remaining : -1, (uint32_t)t.outer, pb, prot(t.outer));
    return r;
}

// FUN_00409e10, cdecl: a heap or block free. Every engine free; logs only inside a logged Layout dtor.
inline void __cdecl hkHeapFree(uint32_t p)
{
    State& s = g_state;
    if (!freeInDtor()) {
        s.g_heapFree(p);
        return;
    }
    Tls& t = g_tls;
    char b[160];
    block(b, sizeof(b), p);
    s.g_heapFree(p);
    core::logf("td    heap free %08X (was %s); placement node %08X (node - p = %X) page prot %X", p, b,
               (uint32_t)t.outer, (uint32_t)(t.outer - p), prot(t.outer));
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("teardowntrace", {
        {"Zoo::Layout dtor (teardowntrace)", 0x00787BC0, "6AFF68????????64A100000000", (void*)&detail::hkLayoutDtor,
         (void**)&s.g_layoutDtor},
        {"Samurai::Node dtor (teardowntrace)", 0x004EE5F0, "6AFF68????????64A100000000", (void*)&detail::hkNodeDtor,
         (void**)&s.g_nodeDtor},
        {"hierarchy entry release (teardowntrace)", 0x004B2780, "8B442404568D7110", (void*)&detail::hkEntryRel,
         (void**)&s.g_entryRel},
        {"hierarchy entry free (teardowntrace)", 0x004E1A40, "83EC0853578B7C2414", (void*)&detail::hkEntryFree,
         (void**)&s.g_entryFree},
        {"Layout placement bind (teardowntrace)", 0x0077D9C0, "6AFF68????????64A100000000", (void*)&detail::hkPlBind,
         (void**)&s.g_plBind},
        {"block tracker delete" " (teardowntrace)", 0x00416840, "535556578BF9", (void*)&detail::hkTrackFree,
         (void**)&s.g_trackFree},
        {"heap free (teardowntrace)", 0x00409E10, "8B4424046A0350E8", (void*)&detail::hkHeapFree,
         (void**)&s.g_heapFree},
    });
}

}  // namespace trace::teardown
