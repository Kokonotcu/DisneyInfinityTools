// realmtrace10: realm requests, zone attaches, animates and a polled realm container view. Log only.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include "../../base/format.hpp"
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../chain.hpp"

namespace trace::realm {

struct Config {
    bool on = false;   // realmtrace10
};

// Installs the three probes, all or none (H1, H2), and starts the poller; false when refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kManager = 0x022035B4;      // the realm manager pointer (FUN_007066b0's ecx)
constexpr size_t kContainer = 0x50;            // manager + 0x50: the realm container (FUN_00644120)
constexpr size_t kMasterStack = 1;             // manager + 1: UseRealmMasterStack (FUN_006f34c0)
constexpr uint32_t kZoneManager = 0x0225F548;  // zone manager pointer; + 0x19c the master zone (FUN_006fe480)
constexpr size_t kMasterZone = 0x19C;
constexpr size_t kEntries = 4;                 // container: entries, count (FUN_00640ee0)
constexpr size_t kEntryCount = 8;
constexpr size_t kEntrySize = 0x6C;
constexpr size_t kZones = 0x54;                // container: zone records, count (FUN_00641180)
constexpr size_t kZoneCount = 0x58;
constexpr size_t kZoneSize = 0x28;             // zone record: hash, zone, realm hash array, count (FUN_0064cca0)
constexpr size_t kZonePtr = 4;
constexpr size_t kZoneRealms = 8;
constexpr size_t kZoneRealmCount = 0xC;
// container + 0x90: the combiner's last output, items {entry, weight, no-consume} of 12 bytes (FUN_00652300)
constexpr size_t kOutItems = 0x94, kOutCount = 0x98, kOutItemSize = 0xC;
constexpr size_t kContainerBytes = 0x9C;
// entry fields: content (FUN_00648b20 skips all-zero), weights per slot (FUN_00640d30), name, parent, kind, flags
constexpr size_t kContent0 = 4, kContent1 = 8, kContent2 = 0xC;
constexpr size_t kWeight = 0x18, kWeightB = 0x20, kWeightC = 0x28;
constexpr size_t kName = 0x30, kParent = 0x38, kKind = 0x3C, kFlags = 0x40;
// a RealmAnimateBasic node (FUN_017c2180): realm name hash, Start, End
constexpr size_t kAnimRealm = 0x94, kAnimStart = 0x48, kAnimEnd = 0x54;
}  // namespace eng

constexpr LONG kLines = 20000;      // log budget of the run
constexpr DWORD kPollMs = 250;      // weight and output changes are looked for this often
constexpr int kDumpPolls = 8;       // the whole state every 8 polls (2 s)
constexpr size_t kLine = 1024;
constexpr uint32_t kMaxListed = 24; // realms listed per line
constexpr uint32_t kTracked = 64;   // entries whose weights the poller follows
constexpr uint32_t kSlots = 2;

// Names the log spells out; any other hash prints as hex.
inline constexpr const char* kKnownNames[] = {"RLM_MU_Campus_Day", "RLM_MU_Campus_Night", "MU_Master", "MU_Campus",
                                              "EngineDefault", "EOW_Default", "RLM_TutorialDesaturate"};

typedef uint32_t(__fastcall* AnimateFn)(uint8_t* node, void*, int pin, int reverse);
typedef uint32_t(__fastcall* RequestFn)(void* m, void*, const char* const* name, void* zone, int a3, float weight,
                                        int a5, int a6, int a7, uint32_t flags, int a9);
typedef uint32_t(__fastcall* AttachFn)(void* m, void*, void* zone, uint32_t nameHash);

// One followed entry: name hash and lighting weight per slot as last logged.
struct Seen {
    uint32_t name = 0;
    float w[kSlots] = {};
};

struct State {
    AnimateFn g_animate = nullptr;   // set once by installFeature in install()
    RequestFn g_request = nullptr;   // set once by installFeature in install()
    AttachFn g_attach = nullptr;     // set once by installFeature in install()
    uint32_t known[std::size(kKnownNames)] = {};   // set once in install()
    std::atomic<LONG> lines{0};
    Seen seen[kTracked];             // poller thread only
    uint32_t outSig = 0;             // poller thread only
};
inline State g_state;

inline bool budget() { return ++g_state.lines <= kLines; }

// FUN_0044aa40, the realm manager's name hash (FUN_006f32f0): case-folded, 4-byte groups times 0x25.
inline uint32_t nameHash(const char* s)
{
    constexpr uint32_t kMul = 0x25, kShiftMax = 0x18;
    auto low = [](char ch) { return (uint32_t)((ch >= 'A' && ch <= 'Z') ? ch + 0x20 : ch); };
    uint32_t a = low(s[0]), h = 0, i = 0;
    while (a & 0xFF) {
        a = a << 8 | low(s[i + 1]);
        ++i;
        if ((i & 3) == 3) h = (h + a) * kMul;
    }
    return (a << (kShiftMax - 8 * (i & 3))) + h;
}

// A hash's known name, else its hex in buf.
inline const char* nameOf(uint32_t h, char* buf, size_t cap)
{
    for (size_t k = 0; k < std::size(kKnownNames); ++k)
        if (g_state.known[k] == h) return kKnownNames[k];
    snprintf(buf, cap, "%08X", h);
    return buf;
}

inline uint32_t u32(const uint8_t* p, size_t off) { return *(const uint32_t*)(p + off); }
inline float f32(const uint8_t* p, size_t off) { return *(const float*)(p + off); }

// The realm container, or null while the manager is not up or not readable.
inline const uint8_t* container()
{
    const uint8_t* slot = (const uint8_t*)core::rt(eng::kManager);
    const uint8_t* m = core::readable(slot, 4) ? *(const uint8_t* const*)slot : nullptr;
    const bool up = m && core::readable(m, eng::kContainer + eng::kContainerBytes);
    return up ? m + eng::kContainer : nullptr;
}

// Entry i of the container, or null when out of range or unreadable.
inline const uint8_t* entry(const uint8_t* c, uint32_t i)
{
    const uint32_t n = u32(c, eng::kEntryCount);
    const uint8_t* arr = (const uint8_t*)(uintptr_t)u32(c, eng::kEntries);
    if (i >= n || !arr || !core::readable(arr + i * eng::kEntrySize, eng::kEntrySize)) return nullptr;
    return arr + i * eng::kEntrySize;
}

// Entry index of a realm name hash, -1 when none (FUN_00640ee0).
inline int entryOf(const uint8_t* c, uint32_t hash)
{
    for (uint32_t i = 0, n = u32(c, eng::kEntryCount); i < n; ++i)
        if (const uint8_t* e = entry(c, i); e && u32(e, eng::kName) == hash) return (int)i;
    return -1;
}

// One entry's state: name, weights per slot (three kinds), content zero or not, parent, kind and flags.
inline size_t appendEntry(char* line, size_t len, const uint8_t* e)
{
    char nb[12], pb[12];
    const bool empty = !u32(e, eng::kContent0) && !u32(e, eng::kContent1) && !u32(e, eng::kContent2);
    return format::appendCut(line, kLine, len, " [%s w0 %.3f/%.3f/%.3f w1 %.3f/%.3f/%.3f content %s parent %s kind %u "
                             "flags %02X]", nameOf(u32(e, eng::kName), nb, sizeof(nb)), f32(e, eng::kWeight),
                             f32(e, eng::kWeightB), f32(e, eng::kWeightC), f32(e, eng::kWeight + 4),
                             f32(e, eng::kWeightB + 4), f32(e, eng::kWeightC + 4), empty ? "EMPTY(skipped)" : "set",
                             u32(e, eng::kParent) ? nameOf(u32(e, eng::kParent), pb, sizeof(pb)) : "-",
                             u32(e, eng::kKind), u32(e, eng::kFlags) & 0xFF);
}

// Zone record z: hash, pointer and realm list in order; the combiner walks it last to first.
inline void logZone(const uint8_t* c, const uint8_t* r, uint32_t z)
{
    char line[kLine], zb[12], rb[12];
    const uint32_t* list = (const uint32_t*)(uintptr_t)u32(r, eng::kZoneRealms);
    const uint32_t cnt = u32(r, eng::kZoneRealmCount);
    size_t len = format::appendCut(line, kLine, 0, "realmtrace10: zone %u %s (zone %08X) realms %u:", z,
                                   nameOf(u32(r, 0), zb, sizeof(zb)), u32(r, eng::kZonePtr), cnt);
    const bool listOk = list && cnt < kMaxListed && core::readable(list, cnt * 4);
    for (uint32_t k = 0; listOk && k < cnt; ++k)
        len = format::appendCut(line, kLine, len, " %s(entry %d)", nameOf(list[k], rb, sizeof(rb)),
                                entryOf(c, list[k]));
    core::logf("%s", line);
}

// The combiner's last output: each pushed realm with the weight it got and whether it consumed weight.
inline void logOutput(const uint8_t* c)
{
    char line[kLine], nb[12];
    const uint32_t n = u32(c, eng::kOutCount);
    const uint8_t* items = (const uint8_t*)(uintptr_t)u32(c, eng::kOutItems);
    size_t len = format::appendCut(line, kLine, 0, "realmtrace10: combiner output %u:", n);
    const bool ok = items && n < kMaxListed && core::readable(items, n * eng::kOutItemSize);
    for (uint32_t k = 0; ok && k < n; ++k) {
        const uint8_t* it = items + k * eng::kOutItemSize;
        const uint8_t* e = entry(c, u32(it, 0));
        len = format::appendCut(line, kLine, len, " %s %.3f%s", e ? nameOf(u32(e, eng::kName), nb, sizeof(nb)) : "?",
                                f32(it, 4), it[8] ? " (no-consume)" : "");
    }
    if (!n) format::appendCut(line, kLine, len, " (empty: the engine default realm)");
    core::logf("%s", line);
}

// Every realm entry, every zone record, the combiner output, the master zone and UseRealmMasterStack.
inline void dumpState(const uint8_t* c)
{
    char line[kLine];
    size_t len = format::appendCut(line, kLine, 0, "realmtrace10: entries %u:", u32(c, eng::kEntryCount));
    for (uint32_t i = 0, n = u32(c, eng::kEntryCount); i < n && i < kMaxListed; ++i)
        if (const uint8_t* e = entry(c, i)) len = appendEntry(line, len, e);
    core::logf("%s", line);
    const uint8_t* zones = (const uint8_t*)(uintptr_t)u32(c, eng::kZones);
    for (uint32_t z = 0, nz = u32(c, eng::kZoneCount); zones && z < nz && z < kMaxListed; ++z) {
        const uint8_t* r = zones + z * eng::kZoneSize;
        if (core::readable(r, eng::kZoneSize)) logZone(c, r, z);
    }
    logOutput(c);
    const uint8_t* zm = (const uint8_t*)core::rt(eng::kZoneManager);
    const uint8_t* zmp = core::readable(zm, 4) ? *(const uint8_t* const*)zm : nullptr;
    const uint32_t master = zmp && core::readable(zmp + eng::kMasterZone, 4) ? u32(zmp, eng::kMasterZone) : 0;
    const uint8_t* manager = c - eng::kContainer;
    core::logf("realmtrace10: master zone %08X, UseRealmMasterStack %d", master, manager[eng::kMasterStack]);
}

// Weight changes since the last poll; polled because FUN_00643f70 keeps ecx/xmm1 across FUN_00640d90.
inline void pollWeights(const uint8_t* c)
{
    char nb[12];
    for (uint32_t i = 0, n = u32(c, eng::kEntryCount); i < n && i < kTracked; ++i) {
        const uint8_t* e = entry(c, i);
        if (!e) continue;
        Seen& s = g_state.seen[i];
        const uint32_t name = u32(e, eng::kName);
        const bool fresh = s.name != name;
        for (uint32_t k = 0; k < kSlots; ++k) {
            const float w = f32(e, eng::kWeight + 4 * k);
            if ((fresh || w != s.w[k]) && budget())
                core::logf("realmtrace10: weight %s slot %u %.3f -> %.3f%s", nameOf(name, nb, sizeof(nb)), k,
                           fresh ? 0.f : s.w[k], w, fresh ? " (new entry)" : "");
            s.w[k] = w;
        }
        s.name = name;
    }
}

// A signature of the combiner's last output, to log it when it changes.
inline uint32_t outputSig(const uint8_t* c)
{
    constexpr uint32_t kBasis = 2166136261u, kPrime = 16777619u;   // FNV-1a
    const uint32_t n = u32(c, eng::kOutCount);
    const uint8_t* items = (const uint8_t*)(uintptr_t)u32(c, eng::kOutItems);
    uint32_t h = (kBasis ^ n) * kPrime;
    const bool ok = items && n < kMaxListed && core::readable(items, n * eng::kOutItemSize);
    for (uint32_t k = 0; ok && k < n * 2; ++k) h = (h ^ u32(items, (k / 2) * eng::kOutItemSize + (k % 2) * 4)) * kPrime;
    return h;
}

// The poller: weight changes and output changes every 250 ms, the whole state every 2 s. Its own thread.
inline DWORD WINAPI poller(void*)
{
    for (int tick = 0;; ++tick) {
        Sleep(kPollMs);
        const uint8_t* c = container();
        if (!c || g_state.lines >= kLines) continue;
        pollWeights(c);
        const uint32_t sig = outputSig(c);
        const bool outChanged = sig != g_state.outSig;
        g_state.outSig = sig;
        if (tick % kDumpPolls == 0) dumpState(c);
        else if (outChanged && budget()) logOutput(c);
    }
}

// FUN_017a35f0, thiscall on a RealmAnimateBasic (pin, reverse; ret 8): its activation. Main thread, per impulse.
inline uint32_t __fastcall hkAnimate(uint8_t* node, void* edx, int pin, int reverse)
{
    const uint8_t* c = container();
    const bool nodeOk = node && core::readable(node, eng::kAnimRealm + 4);
    const uint32_t realm = nodeOk ? u32(node, eng::kAnimRealm) : 0;
    const int at = c && nodeOk ? entryOf(c, realm) : -2;
    if (nodeOk && budget()) {
        char nb[12];
        core::logf("realmtrace10: RealmAnimateBasic %s pin %d reverse %d Start %.3f End %.3f; realm entry %d%s",
                   nameOf(realm, nb, sizeof(nb)), pin, reverse & 0xFF, f32(node, eng::kAnimStart),
                   f32(node, eng::kAnimEnd), at, at < 0 ? " (not loaded: the animate does nothing)" : "");
    }
    return g_state.g_animate(node, edx, pin, reverse);
}

// FUN_007066b0, thiscall (name, zone, ?, weight, ?, ?, ?, flags, ?; ret 0x24): a realm request. Main thread.
inline uint32_t __fastcall hkRequest(void* m, void* edx, const char* const* name, void* zone, int a3, float weight,
                                     int a5, int a6, int a7, uint32_t flags, int a9)
{
    char nb[64] = "";
    core::cstr(name, nb, sizeof(nb));
    const uint8_t* c = container();
    const int at = c ? entryOf(c, nameHash(nb)) : -2;
    const uint32_t ret = core::toStaticVa((uintptr_t)_ReturnAddress());
    if (budget())
        core::logf("realmtrace10: request %s zone %08X weight %.3f flags %02X from %08X; %s", nb, (uint32_t)zone,
                   weight, flags & 0xFF, ret, at >= 0 ? "already an entry: its weight is not set" : "loads");
    return g_state.g_request(m, edx, name, zone, a3, weight, a5, a6, a7, flags, a9);
}

// FUN_006fe480, thiscall on the realm manager (zone, name hash; ret 8) -> attached. Main thread, per realm load.
inline uint32_t __fastcall hkAttach(void* m, void* edx, void* zone, uint32_t hash)
{
    const uint32_t r = g_state.g_attach(m, edx, zone, hash);
    if (budget()) {
        char nb[12];
        core::logf("realmtrace10: attach %s to zone %08X -> %u", nameOf(hash, nb, sizeof(nb)), (uint32_t)zone,
                   r & 0xFF);
    }
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    for (size_t k = 0; k < std::size(detail::kKnownNames); ++k) s.known[k] = detail::nameHash(detail::kKnownNames[k]);
    const bool hooked = dbg::chain::installFeature("realmtrace10", {
        {"RealmAnimateBasic activate (realmtrace10)", 0x017A35F0, "83EC34535556578BF1E8", (void*)&detail::hkAnimate,
         (void**)&s.g_animate},
        {"realm request (realmtrace10)", 0x007066B0, "6AFF64A10000000068", (void*)&detail::hkRequest,
         (void**)&s.g_request},
        {"realm zone attach (realmtrace10)", 0x006FE480, "83EC0C53558B6C2418568BF1", (void*)&detail::hkAttach,
         (void**)&s.g_attach},
    });
    // never waited on (H10): it only reads, with the readable checks, and logs
    const HANDLE t = CreateThread(nullptr, 0, &detail::poller, nullptr, 0, nullptr);
    core::logf("realmtrace10: poller %s", t ? "started" : "not started");
    if (t) CloseHandle(t);
    return hooked;
}

}  // namespace trace::realm
