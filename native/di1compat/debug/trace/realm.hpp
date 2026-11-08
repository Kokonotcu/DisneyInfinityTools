// realmtrace10: realm requests, zone attaches, weight changes and the per-zone realm combiner, logged. Log only.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "../../base/format.hpp"
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../core/stack.hpp"
#include "../chain.hpp"

namespace trace::realm {

struct Config {
    bool on = false;   // realmtrace10
};

// Installs the seven probes when c asks for them, all or none (H1, H2); false when they were refused.
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
// entry fields: content (FUN_00648b20 skips all-zero), weights per slot (FUN_00640d30), name, parent, kind, flags
constexpr size_t kContent0 = 4, kContent1 = 8, kContent2 = 0xC;
constexpr size_t kWeight = 0x18, kWeightB = 0x20, kWeightC = 0x28;
constexpr size_t kName = 0x30, kParent = 0x38, kKind = 0x3C, kFlags = 0x40;
constexpr int kSlots = 2;
// a RealmAnimateBasic node (FUN_017c2180): realm name hash, Start, End
constexpr size_t kAnimRealm = 0x94, kAnimStart = 0x48, kAnimEnd = 0x54;
// the combiner's output stack (FUN_00644ba0): items {entry, weight, no-consume} of 12 bytes at +4, count +8
constexpr size_t kOutItems = 4, kOutCount = 8, kOutItemSize = 0xC;
// FUN_00648b20's return addresses in FUN_00652300: player zone lists, then the master zone
constexpr uint32_t kRetZoneList = 0x00652414;
constexpr uint32_t kRetMasterZone = 0x00652531;
}  // namespace eng

constexpr LONG kLines = 20000;      // log budget of the run
constexpr DWORD kDumpMs = 2000;     // the whole realm state at most this often
constexpr int kStackRets = 4;
constexpr int kKeys = 16;           // combiner call sites followed for change detection
constexpr size_t kLine = 1024;
constexpr int kMaxListed = 24;      // realms listed per line

// Names the log spells out; any other hash prints as hex.
inline constexpr const char* kKnownNames[] = {"RLM_MU_Campus_Day", "RLM_MU_Campus_Night", "MU_Master", "MU_Campus",
                                              "EngineDefault", "EOW_Default", "RLM_TutorialDesaturate"};

typedef float(__fastcall* CombineFn)(void* c, void*, void* list, float zoneWeight, int slot, int a4, void* out);
typedef uint32_t(__fastcall* SetWeightFn)(void* c, void*, int entry, int slot, float w);
typedef uint32_t(__fastcall* SetWeights3Fn)(void* c, void*, int entry, int slot, float a, float b, float d);
typedef uint32_t(__fastcall* ClampWeightFn)(void* c, void*, int entry, int slot, float max);
typedef uint32_t(__fastcall* AnimateFn)(uint8_t* node, void*, int pin, int reverse);
typedef uint32_t(__fastcall* RequestFn)(void* m, void*, const char* const* name, void* zone, int a3, float weight,
                                        int a5, int a6, int a7, uint32_t flags, int a9);
typedef uint32_t(__fastcall* AttachFn)(void* m, void*, void* zone, uint32_t nameHash);

struct Key {
    uint32_t ret = 0, slot = 0, first = 0, sig = 0;   // combiner thread only
};

struct State {
    CombineFn g_combine = nullptr;          // set once by installFeature in install()
    SetWeightFn g_setWeight = nullptr;      // set once by installFeature in install()
    SetWeights3Fn g_setWeights3 = nullptr;  // set once by installFeature in install()
    ClampWeightFn g_clamp = nullptr;        // set once by installFeature in install()
    AnimateFn g_animate = nullptr;          // set once by installFeature in install()
    RequestFn g_request = nullptr;          // set once by installFeature in install()
    AttachFn g_attach = nullptr;            // set once by installFeature in install()
    uint32_t known[std::size(kKnownNames)] = {};   // set once in install()
    std::atomic<LONG> lines{0};
    DWORD lastDump = 0;                     // combiner thread only
    Key keys[kKeys];                        // combiner thread only
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

// The realm container, or null while the manager is not up.
inline uint8_t* container()
{
    const uint8_t* slot = (const uint8_t*)core::rt(eng::kManager);
    uint8_t* m = core::readable(slot, 4) ? *(uint8_t* const*)slot : nullptr;
    const bool up = m && core::readable(m, eng::kContainer + eng::kZoneCount + 4);
    return up ? m + eng::kContainer : nullptr;
}

inline uint32_t u32(const uint8_t* p, size_t off) { return *(const uint32_t*)(p + off); }
inline float f32(const uint8_t* p, size_t off) { return *(const float*)(p + off); }

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
    const bool listOk = list && cnt < (uint32_t)kMaxListed && core::readable(list, cnt * 4);
    for (uint32_t k = 0; listOk && k < cnt; ++k)
        len = format::appendCut(line, kLine, len, " %s(entry %d)", nameOf(list[k], rb, sizeof(rb)),
                                entryOf(c, list[k]));
    core::logf("%s", line);
}

// Every realm entry, every zone record, then the master stack flag and the master zone.
inline void dumpState(const uint8_t* c)
{
    char line[kLine];
    size_t len = format::appendCut(line, kLine, 0, "realmtrace10: entries %u:", u32(c, eng::kEntryCount));
    for (uint32_t i = 0, n = u32(c, eng::kEntryCount); i < n && (int)i < kMaxListed; ++i)
        if (const uint8_t* e = entry(c, i)) len = appendEntry(line, len, e);
    core::logf("%s", line);
    const uint8_t* zones = (const uint8_t*)(uintptr_t)u32(c, eng::kZones);
    for (uint32_t z = 0, nz = u32(c, eng::kZoneCount); zones && z < nz; ++z) {
        const uint8_t* r = zones + z * eng::kZoneSize;
        if (core::readable(r, eng::kZoneSize)) logZone(c, r, z);
    }
    const uint8_t* zm = (const uint8_t*)core::rt(eng::kZoneManager);
    const uint8_t* zmp = core::readable(zm, 4) ? *(const uint8_t* const*)zm : nullptr;
    const uint32_t master = zmp && core::readable(zmp + eng::kMasterZone, 4) ? u32(zmp, eng::kMasterZone) : 0;
    const uint8_t* manager = c - eng::kContainer;
    core::logf("realmtrace10: master zone %08X, UseRealmMasterStack %d", master, manager[eng::kMasterStack]);
}

// FNV-1a steps for a combiner call's signature: its inputs and what it appended to the output stack.
constexpr uint32_t kFnvBasis = 2166136261u, kFnvPrime = 16777619u;
inline uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * kFnvPrime; }
inline uint32_t bitsOf(float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof(v));
    return v;
}

// The combiner call's line: zone weight, its realm list as weighed, the items it pushed and the weight left.
inline void logCombine(const uint8_t* c, const uint32_t* const* list, float zw, int slot, const uint8_t* out,
                       uint32_t before, float left, const char* where)
{
    char line[kLine], nb[12];
    const uint32_t* data = list ? list[0] : nullptr;
    const uint32_t n = list ? (uint32_t)(uintptr_t)list[1] : 0;
    size_t len = format::appendCut(line, kLine, 0, "realmtrace10: combine %s slot %d zone weight %.3f, realms %u "
                                   "(walked last to first):", where, slot, zw, n);
    const bool listOk = data && n < (uint32_t)kMaxListed && core::readable(data, n * 4);
    for (uint32_t k = 0; listOk && k < n; ++k)
        if (const uint8_t* e = entry(c, data[k])) len = appendEntry(line, len, e);
    const uint8_t* items = (const uint8_t*)(uintptr_t)u32(out, eng::kOutItems);
    const uint32_t after = u32(out, eng::kOutCount);
    len = format::appendCut(line, kLine, len, "; pushed:");
    for (uint32_t k = before; items && k < after && core::readable(items + k * eng::kOutItemSize, eng::kOutItemSize);
         ++k) {
        const uint8_t* it = items + k * eng::kOutItemSize;
        const uint8_t* e = entry(c, u32(it, 0));
        len = format::appendCut(line, kLine, len, " %s %.3f%s", e ? nameOf(u32(e, eng::kName), nb, sizeof(nb)) : "?",
                                f32(it, 4), it[8] ? " (no-consume)" : "");
    }
    format::appendCut(line, kLine, len, "; weight left %.3f", left);
    core::logf("%s", line);
}

// Whether call site key (ret, slot, first realm) changed its signature; a full table always reports a change.
inline bool changed(uint32_t ret, int slot, uint32_t first, uint32_t sig)
{
    for (Key& k : g_state.keys) {
        if (k.ret == ret && k.slot == (uint32_t)slot && k.first == first) {
            const bool diff = k.sig != sig;
            k.sig = sig;
            return diff;
        }
        if (k.ret) continue;
        k = {ret, (uint32_t)slot, first, sig};
        return true;
    }
    return true;
}

// FUN_00648b20, thiscall (list, zone weight, slot, ?, out; ret 0x14) -> weight left. Per zone, slot, frame.
inline float __fastcall hkCombine(void* cv, void* edx, void* list, float zw, int slot, int a4, void* outv)
{
    uint8_t* out = (uint8_t*)outv;
    const bool outOk = out && core::readable(out, eng::kOutCount + 4);
    const uint32_t before = outOk ? u32(out, eng::kOutCount) : 0;
    const uint32_t ret = core::toStaticVa((uintptr_t)_ReturnAddress());
    const float left = g_state.g_combine(cv, edx, list, zw, slot, a4, outv);
    const uint8_t* c = (const uint8_t*)cv;
    const uint32_t* const* l = (const uint32_t* const*)list;
    if (!outOk || !l || !core::readable(l, 8)) return left;
    const uint32_t after = u32(out, eng::kOutCount);
    uint32_t sig = mix(mix(mix(kFnvBasis, bitsOf(zw)), bitsOf(left)), after);
    const uint8_t* items = (const uint8_t*)(uintptr_t)u32(out, eng::kOutItems);
    const bool itemsOk = items && after >= before && core::readable(items, after * eng::kOutItemSize);
    for (uint32_t k = before; itemsOk && k < after; ++k)
        sig = mix(mix(sig, u32(items, k * eng::kOutItemSize)), u32(items, k * eng::kOutItemSize + 4));
    const uint32_t first = l[0] && core::readable(l[0], 4) ? l[0][0] : 0;
    const DWORD now = GetTickCount();
    const bool dump = now - g_state.lastDump >= kDumpMs;
    if (dump) g_state.lastDump = now;
    const bool report = changed(ret, slot, first, sig) || dump;
    if (!report || !budget()) return left;
    const char* where = ret == eng::kRetZoneList ? "player zone" : ret == eng::kRetMasterZone ? "master zone" : "other";
    logCombine(c, l, zw, slot, out, before, left, where);
    if (dump) dumpState(c);
    return left;
}

// A weight change of entry i, slot s: old and new lighting weight, and the exe stack that made it.
inline void logWeight(const char* how, const uint8_t* c, int i, int slot, float was)
{
    const uint8_t* e = entry(c, (uint32_t)i);
    const bool slotOk = slot >= 0 && slot < eng::kSlots;
    if (!e || !slotOk) return;
    const float now = f32(e, eng::kWeight + 4 * slot);
    if (now == was || !budget()) return;
    char nb[12], stack[kStackRets * 9 + 1] = "";
    core::stackOf((const uint32_t*)_AddressOfReturnAddress(), stack, sizeof(stack), 0, kStackRets);
    core::logf("realmtrace10: weight %s %s slot %d %.3f -> %.3f; stack%s", how, nameOf(u32(e, eng::kName), nb,
               sizeof(nb)), slot, was, now, stack);
}

// The lighting weight of entry i, slot s before a setter runs; -1 when it cannot be read.
inline float weightOf(const uint8_t* c, int i, int slot)
{
    const uint8_t* e = entry(c, (uint32_t)i);
    const bool slotOk = slot >= 0 && slot < eng::kSlots;
    return e && slotOk ? f32(e, eng::kWeight + 4 * slot) : -1.f;
}

// FUN_00640d90, thiscall on the container (entry, slot, weight; ret 0xc): all three weights. Game threads.
inline uint32_t __fastcall hkSetWeight(void* cv, void* edx, int i, int slot, float w)
{
    const float was = weightOf((const uint8_t*)cv, i, slot);
    const uint32_t r = g_state.g_setWeight(cv, edx, i, slot, w);
    logWeight("set", (const uint8_t*)cv, i, slot, was);
    return r;
}

// FUN_00640dd0, thiscall on the container (entry, slot, three weights; ret 0x14). Game threads.
inline uint32_t __fastcall hkSetWeights3(void* cv, void* edx, int i, int slot, float a, float b, float d)
{
    const float was = weightOf((const uint8_t*)cv, i, slot);
    const uint32_t r = g_state.g_setWeights3(cv, edx, i, slot, a, b, d);
    logWeight("set3", (const uint8_t*)cv, i, slot, was);
    return r;
}

// FUN_00640e10, thiscall on the container (entry, slot, max; ret 0xc): clamps the weights. Game threads.
inline uint32_t __fastcall hkClamp(void* cv, void* edx, int i, int slot, float max)
{
    const float was = weightOf((const uint8_t*)cv, i, slot);
    const uint32_t r = g_state.g_clamp(cv, edx, i, slot, max);
    logWeight("clamp", (const uint8_t*)cv, i, slot, was);
    return r;
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
    return dbg::chain::installFeature("realmtrace10", {
        {"realm combiner (realmtrace10)", 0x00648B20, "83EC0C8B4424108B4004", (void*)&detail::hkCombine,
         (void**)&s.g_combine},
        {"realm weight set (realmtrace10)", 0x00640D90, "8B4424048B51046BC01BF30F", (void*)&detail::hkSetWeight,
         (void**)&s.g_setWeight},
        {"realm weights set (realmtrace10)", 0x00640DD0, "8B4424048B51046BC01BF30F", (void*)&detail::hkSetWeights3,
         (void**)&s.g_setWeights3},
        {"realm weight clamp (realmtrace10)", 0x00640E10, "8B4424048B51046BC01B0F57", (void*)&detail::hkClamp,
         (void**)&s.g_clamp},
        {"RealmAnimateBasic activate (realmtrace10)", 0x017A35F0, "83EC34535556578BF1E8", (void*)&detail::hkAnimate,
         (void**)&s.g_animate},
        {"realm request (realmtrace10)", 0x007066B0, "6AFF64A10000000068", (void*)&detail::hkRequest,
         (void**)&s.g_request},
        {"realm zone attach (realmtrace10)", 0x006FE480, "83EC0C53558B6C2418568BF1", (void*)&detail::hkAttach,
         (void**)&s.g_attach},
    });
}

}  // namespace trace::realm
