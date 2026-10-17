// breaktrace probe: choreography clocks and plays, layout attachments, exploder breaks. core, base. Threads unverified.
#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../base/format.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"

namespace trace::breaktrace {

struct Config {
    bool on = false;   // breaktrace
};

// Installs the probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

// The line for a choreography's start (engine::choreography's starting callback, set only under breaktrace).
inline void choreographyStart(const uint8_t* cho, const char* name, uint32_t flags)
{
    core::logf("breaktrace cho start %p '%s' flags %08X", cho, name, flags);
}

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kClipClock = 0x00528A70;   // FUN_00528a70: float __fastcall(processor), the processor's input clock
// ClipProcessor fields (FUN_00543f40)
constexpr size_t kProcClip = 0x3c;
constexpr size_t kProcBlock = 0x40;
constexpr size_t kProcTable = 0x48;
// FUN_01442ee0's sync object {dialog request id, active byte, seconds left}
constexpr size_t kSyncActive = 4;
constexpr size_t kSyncLeft = 8;
// choreography instance (FUN_01449390): +0x1b4 flags, 0x20 audio sync; play handle {instance, ?, network, sub}
constexpr size_t kInstFlags = 0x1b4;
constexpr uint32_t kFlagAudioSync = 0x20;
constexpr size_t kNetReady = 0x63;            // network +0x63: bit 0 = plays when started (FUN_01445e70)
// Zoo::Layout attachments (FUN_00788280): +0x30 array, +0x34 count, +0x24 instances; 0xc each
constexpr size_t kLayoutInstances = 0x24;
constexpr size_t kLayoutAttachments = 0x30;
constexpr size_t kLayoutCount = 0x34;
constexpr size_t kLayoutRead = 0x38;
constexpr size_t kAttachBytes = 0xc;          // {base mesh name, layer mesh name, base u16 +8, layer u16 +10}
constexpr size_t kAttachLayerName = 4;
constexpr size_t kAttachBase = 8;
constexpr size_t kAttachLayer = 10;
constexpr size_t kInstObjects = 4;            // instance list +4: objects, 0xc each, object pointer first
constexpr size_t kInstRead = 8;
constexpr size_t kObjBytes = 0xc;
// clip evaluator (FUN_00552fc0, FUN_00532050): port table +0x1c, locals +0x34, connection table +0x44
constexpr size_t kEvalPorts = 0x1c;
constexpr size_t kEvalLocals = 0x34;
constexpr size_t kEvalTable = 0x44;
constexpr size_t kEvalTableRead = 0x48;
constexpr size_t kEvalRead = 0x4c;
constexpr size_t kPortArray = 0x10;           // port table +0x10 u16 per port, +0x14 count, +0x18 mask
constexpr size_t kPortCount = 0x14;
constexpr size_t kPortMask = 0x18;
constexpr size_t kPortRead = 0x1c;
constexpr uint32_t kIdBits = 0x1fff;          // an output id's low 13 bits
// block descriptor u16s (FUN_00532050): [1] outputs n, [2 + n] the constant table's byte offset, [3 + n] sampled
constexpr size_t kDescOutputs = 1;
constexpr size_t kDescRead = 8;
constexpr uint16_t kNoPort = 0xffff;
// exploder munition (FUN_0151e420) and ExploderData (FUN_0151e130)
constexpr size_t kMunEnergy = 0x60;
constexpr size_t kMunWeapon = 0x68;           // FourCC
constexpr size_t kMunRead = 0x6c;
constexpr size_t kFourCc = 4;
constexpr size_t kPieceFile = 0x1c;
constexpr size_t kPieceName = 0x20;
constexpr size_t kPieceCount = 0x60;
constexpr size_t kExplodeFlags = 0xf1;        // 0x40 = exploded
}  // namespace eng

constexpr size_t kProcessorsLogged = 40;
constexpr LONG kEvalSummary = 20000;          // a summary line every this many evaluations
constexpr ULONGLONG kReportMs = 2000;
constexpr ULONGLONG kFreshMs = 60000;         // the report lists processors born this recently (a cinematic's)
constexpr size_t kYoungestListed = 8;
constexpr LONG kAppliesLogged = 80;
constexpr uint32_t kAttachmentsListed = 24;
constexpr size_t kLineReserve = 80;           // an attachment entry stops the list when less is left
constexpr LONG kGatesLogged = 80;
constexpr LONG kClipSetsLogged = 400;
constexpr LONG kBindsLogged = 300;
constexpr LONG kHitsLogged = 40;
constexpr LONG kBoomsLogged = 40;
constexpr uint32_t kNoLocal = 0xffffffffu;

typedef void(__fastcall* ThisFn)(void* self, void* edx);
typedef char(__fastcall* ReadyFn)(int32_t* sync, void* edx);
typedef char(__fastcall* PlayFn)(uint32_t* h, void* edx);
typedef int(__fastcall* ApplyFn)(uint8_t* self, void* edx, uint32_t a1, uint32_t a2, uint8_t* inst, uint32_t a4);
typedef int(__fastcall* GateFn)(void* self, void* edx, uint32_t a1, uint32_t a2);
typedef void(__fastcall* ClipSetFn)(uint8_t* self, void* edx, uint32_t clip, uint16_t* desc, uint32_t a3);
typedef uint32_t(__fastcall* MusicFn)(void* self, void* edx, const char* name, int prio, float f, const char* s);
typedef int(__fastcall* BindFn)(void* self, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
typedef void(__cdecl* HitFn)(uint32_t actor, uint32_t a2, uint8_t* mun);
typedef void(__fastcall* OneFn)(void* self, void* edx, uint32_t a1);

struct Clock {
    ULONGLONG born, changed;
    float first, last;
};

struct State {
    // originals: set once by hook() in install()
    ThisFn g_clipEval = nullptr;
    ReadyFn g_choReady = nullptr;
    PlayFn g_btPlay = nullptr;
    ApplyFn g_layApply = nullptr;
    GateFn g_layGate = nullptr;
    ClipSetFn g_clipSet = nullptr;
    MusicFn g_musicPush = nullptr;
    BindFn g_layBind = nullptr;
    HitFn g_munHit = nullptr;
    OneFn g_explode = nullptr;
    std::mutex mx;                                    // guards first, moving, clocks, report and ready
    std::unordered_map<void*, float> first;           // processor -> its first clock
    std::unordered_set<void*> moving;                 // processors whose clock changed
    std::unordered_map<void*, Clock> clocks;          // every processor's clock, for the report
    ULONGLONG report = 0;                             // tick of the last report
    std::unordered_map<int32_t*, char> ready;         // sync object -> its last answer
    std::atomic<LONG> evals{0}, applies{0}, gates{0}, clipSets{0}, binds{0}, hits{0}, booms{0};   // budgets
};
inline State g_state;

// Every 2 s: the processors born in the last 60 s, youngest first, each one's clock. Caller holds mx.
inline void reportClocks(ULONGLONG now)
{
    State& s = g_state;
    s.report = now;
    std::vector<std::pair<ULONGLONG, void*>> fresh;
    int moving = 0;
    for (auto& e : s.clocks) {
        if (now - e.second.born > kFreshMs) continue;
        fresh.emplace_back(e.second.born, e.first);
        moving += e.second.first != e.second.last;
    }
    std::sort(fresh.begin(), fresh.end(), [](auto& a, auto& b) { return a.first > b.first; });
    char line[900] = "";
    size_t at = 0;
    for (size_t i = 0; i < fresh.size() && i < kYoungestListed; ++i) {
        const Clock& clock = s.clocks[fresh[i].second];
        // floats of any size: text that does not fit keeps sprintf_s's fail-fast (A-37)
        at += format::fitOrFailFast(line + at, sizeof(line) - at, " %p born -%llu ms %.2f->%.2f (still %llu ms)",
                                    fresh[i].second, now - clock.born, clock.first, clock.last, now - clock.changed);
    }
    if (!fresh.empty())
        core::logf("breaktrace clocks: %zu born in the last 60 s, %d moved; youngest:%s", fresh.size(), moving, line);
}

// One evaluation's clock t: first sight, movement, the summary and the report. Caller holds mx.
inline void noteClock(void* self, float t, LONG n)
{
    State& s = g_state;
    ULONGLONG now = GetTickCount64();
    auto it = s.first.find(self);
    if (it == s.first.end()) {
        s.first[self] = t;
        const uint8_t* p = (const uint8_t*)self;
        if (s.first.size() <= kProcessorsLogged)
            core::logf("breaktrace clip eval: processor %p clip %08X table %08X block %08X t=%.3f", self,
                       *(const uint32_t*)(p + eng::kProcClip), *(const uint32_t*)(p + eng::kProcTable),
                       *(const uint32_t*)(p + eng::kProcBlock), t);
    } else if (it->second != t) {
        s.moving.insert(self);
    }
    auto c = s.clocks.emplace(self, Clock{now, now, t, t}).first;
    if (c->second.last != t) {
        c->second.last = t;
        c->second.changed = now;
    }
    if (n % kEvalSummary == 0)
        core::logf("breaktrace clip evals %ld: %zu processors, %zu with a moving clock", n, s.first.size(),
                   s.moving.size());
    if (now - s.report >= kReportMs) reportClocks(now);
}

// FUN_00543f40, thiscall: ClipProcessor evaluate. Threads unverified; per processor per frame.
inline void __fastcall hkClipEval(void* self, void*)
{
    State& s = g_state;
    float t = ((float(__fastcall*)(void*, void*))core::rt(eng::kClipClock))(self, nullptr);
    const LONG n = ++s.evals;
    {
        std::lock_guard<std::mutex> lock(s.mx);
        noteClock(self, t, n);
    }
    s.g_clipEval(self, nullptr);
}

// FUN_01442ee0, thiscall: a choreography's audio-sync wait, true = go. Threads unverified; per tick until go.
inline char __fastcall hkChoReady(int32_t* sync, void*)
{
    State& s = g_state;
    int32_t id = sync ? sync[0] : -1;
    int act = sync ? ((uint8_t*)sync)[eng::kSyncActive] : 0;
    float left = sync ? *(float*)((uint8_t*)sync + eng::kSyncLeft) : 0.f;
    char r = s.g_choReady(sync, nullptr);
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.ready.find(sync);
    if (it != s.ready.end() && it->second == r) return r;
    s.ready[sync] = r;
    core::logf("breaktrace cho audio-sync %p: request %d active %d, %.2f s left -> %s", sync, id, act, left,
               r ? "go" : "waiting");
    return r;
}

// FUN_01445e70, thiscall: choreography play, true = handled; audio-synced instances logged. Threads unverified.
inline char __fastcall hkBtPlay(uint32_t* h, void*)
{
    State& s = g_state;
    uint8_t* inst = h ? (uint8_t*)(uintptr_t)h[0] : nullptr;
    uint32_t before = inst ? *(uint32_t*)(inst + eng::kInstFlags) : 0;
    char r = s.g_btPlay(h, nullptr);
    if (inst && (before & eng::kFlagAudioSync)) {
        uint8_t* net = (uint8_t*)(uintptr_t)h[2];
        core::logf("breaktrace cho play %p flags %08X -> %08X, network %p ready byte %d, sub %08X -> %d", inst, before,
                   *(uint32_t*)(inst + eng::kInstFlags), net, net ? net[eng::kNetReady] : -1, h[3], r);
    }
    return r;
}

// "(?)" for an unreadable object slot, "(null)" for an empty one, "" for a bound one.
inline const char* objectMark(const uint8_t* objs, uint16_t i)
{
    if (!objs) return "(?)";
    const uint8_t* slot = objs + eng::kObjBytes * i;
    if (!core::readable(slot, 4)) return "(?)";
    return *(const uint32_t*)slot ? "" : "(null)";
}

// Each attachment as the applier sees it: index, base and layer instances, their objects and mesh names.
inline void attachmentList(const uint8_t* self, uint32_t n, const uint8_t* objs, char* line, size_t cap)
{
    const uint8_t* att = *(uint8_t* const*)(self + eng::kLayoutAttachments);
    size_t at = 0;
    for (uint32_t k = 0; k < n && k < kAttachmentsListed && att; ++k) {
        const uint8_t* a = att + eng::kAttachBytes * k;
        if (!core::readable(a, eng::kAttachBytes)) break;
        uint16_t b = *(const uint16_t*)(a + eng::kAttachBase), l = *(const uint16_t*)(a + eng::kAttachLayer);
        char m0[40] = "", m1[40] = "";
        core::safeCopy(m0, sizeof(m0), *(const char* const*)a);
        core::safeCopy(m1, sizeof(m1), *(const char* const*)(a + eng::kAttachLayerName));
        // up to 109 characters an entry: text that does not fit keeps sprintf_s's fail-fast (A-37)
        at += format::fitOrFailFast(line + at, cap - at, " %u:%u%s'%s'/%u%s'%s'", k, b, objectMark(objs, b), m0, l,
                                    objectMark(objs, l), m1);
        if (at > cap - kLineReserve) break;
    }
}

// FUN_00788280, thiscall: a choreography layout's attachments apply. Threads unverified; per apply.
inline int __fastcall hkLayApply(uint8_t* self, void*, uint32_t a1, uint32_t a2, uint8_t* inst, uint32_t a4)
{
    State& s = g_state;
    const LONG nth = ++s.applies;
    if (nth > kAppliesLogged || !core::readable(self, eng::kLayoutRead))
        return s.g_layApply(self, nullptr, a1, a2, inst, a4);
    uint32_t n = *(uint32_t*)(self + eng::kLayoutCount);
    const bool listed = inst && core::readable(inst, eng::kInstRead);
    const uint8_t* objs = listed ? *(uint8_t**)(inst + eng::kInstObjects) : nullptr;
    char line[900] = "";
    attachmentList(self, n, objs, line, sizeof(line));
    core::logf("breaktrace layout apply %p: %u attachments, %u instances, args %08X %08X %p %08X:%s", self, n,
               *(uint32_t*)(self + eng::kLayoutInstances), a1, a2, inst, a4, line);
    return s.g_layApply(self, nullptr, a1, a2, inst, a4);
}

// FUN_00785b40, thiscall: the layout apply's gate, 0 = nothing applied. Threads unverified; per apply.
inline int __fastcall hkLayGate(void* self, void*, uint32_t a1, uint32_t a2)
{
    State& s = g_state;
    int r = s.g_layGate(self, nullptr, a1, a2);
    const LONG nth = ++s.gates;
    if (nth <= kGatesLogged) core::logf("breaktrace layout gate (%08X, %08X) -> %08X", a1, a2, r);
    return r;
}

// A clip evaluator's port table (FUN_00532050): no table maps each id to itself.
struct PortTable {
    uint32_t map = 0, arr = 0, ports = 0, mask = eng::kNoPort, locals = 0;
};

// The local an output id reaches through the port table, kNoLocal if none.
inline uint32_t portLocal(const PortTable& pt, uint32_t id)
{
    if (!pt.map) return id;
    if (id >= pt.ports || !core::readable((const void*)(uintptr_t)(pt.arr + 2 * id), 2)) return kNoLocal;
    uint16_t p = *(const uint16_t*)(uintptr_t)(pt.arr + 2 * id);
    return p != eng::kNoPort ? p & pt.mask : kNoLocal;
}

// How many of a table's output ids reach a node; false when the table is unreadable.
inline bool tableHits(const uint16_t* tab, const PortTable& pt, uint32_t* tot, uint32_t* hit)
{
    if (!core::readable(tab, 2) || !core::readable(tab, 2 + 2 * (size_t)tab[0])) return false;
    *tot = tab[0];
    for (uint32_t k = 0; k < *tot; ++k) *hit += portLocal(pt, tab[1 + k] & eng::kIdBits) < pt.locals;
    return true;
}

// After the set-up: how many sampled and constant output ids reach a node ("" if unreadable).
inline void clipBound(const uint8_t* self, const uint16_t* desc, char* out, size_t cap)
{
    out[0] = 0;
    if (!core::readable(self, eng::kEvalRead) || !desc || !core::readable(desc, eng::kDescRead)) return;
    PortTable pt;
    pt.map = *(const uint32_t*)(self + eng::kEvalPorts);
    pt.locals = *(const uint32_t*)(self + eng::kEvalLocals);
    if (pt.map && core::readable((const void*)(uintptr_t)pt.map, eng::kPortRead)) {
        pt.arr = *(const uint32_t*)(uintptr_t)(pt.map + eng::kPortArray);
        pt.ports = *(const uint32_t*)(uintptr_t)(pt.map + eng::kPortCount);
        pt.mask = *(const uint16_t*)(uintptr_t)(pt.map + eng::kPortMask);
    }
    uint16_t n = desc[eng::kDescOutputs];
    if (!core::readable(desc + 3 + n, 2)) return;
    const uint16_t* tabs[2] = {desc + 3 + n, (const uint16_t*)((const uint8_t*)desc + desc[2 + n])};
    uint32_t tot[2] = {}, hit[2] = {};
    for (int t = 0; t < 2; ++t) tableHits(tabs[t], pt, &tot[t], &hit[t]);
    // never cut: at most 123 characters
    snprintf(out, cap, "; ids reaching a node: sampled %u/%u, constant %u/%u (port table %08X: %u ports, %u locals)",
             hit[0], tot[0], hit[1], tot[1], pt.map, pt.ports, pt.locals);
}

// FUN_00552fc0, thiscall: clip setup (clip, block descriptor). Threads unverified; per clip set.
inline void __fastcall hkClipSet(uint8_t* self, void*, uint32_t clip, uint16_t* desc, uint32_t a3)
{
    State& s = g_state;
    uint32_t table = core::readable(self, eng::kEvalTableRead) ? *(uint32_t*)(self + eng::kEvalTable) : 0;
    s.g_clipSet(self, nullptr, clip, desc, a3);
    const LONG nth = ++s.clipSets;
    if (nth > kClipSetsLogged) return;
    char bound[160];
    clipBound(self, desc, bound, sizeof(bound));
    core::logf("breaktrace clip set %p: clip %08X desc %p outputs %d table %08X a3 %08X%s", self, clip, desc,
               desc && core::readable(desc, 4) ? desc[eng::kDescOutputs] : -1, table, a3, bound);
}

// FUN_0073c5c0, thiscall: music cue push (name, priority, float, string). Threads unverified; per push.
inline uint32_t __fastcall hkMusicPush(void* self, void*, const char* name, int prio, float f, const char* str)
{
    State& s = g_state;
    char n[80] = "", t[80] = "";
    core::safeCopy(n, sizeof(n), name);
    core::safeCopy(t, sizeof(t), str);
    uint32_t r = s.g_musicPush(self, nullptr, name, prio, f, str);
    core::logf("breaktrace music push '%s' priority %d %.2f '%s' -> %08X", n, prio, f, t, r);
    return r;
}

// FUN_00787830, thiscall (4 args, ret 0x10): a layout attachment's bind. Threads unverified; per bind.
inline int __fastcall hkLayBind(void* self, void*, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    State& s = g_state;
    const LONG nth = ++s.binds;
    if (nth <= kBindsLogged)
        core::logf("breaktrace layout bind %p (%08X %08X %08X %08X)", self, a1, a2, a3, a4);
    return s.g_layBind(self, nullptr, a1, a2, a3, a4);
}

// FUN_0151e420, cdecl: exploder munition impact (FUN_01521560 registers it). Threads unverified; per hit.
inline void __cdecl hkMunHit(uint32_t actor, uint32_t a2, uint8_t* mun)
{
    State& s = g_state;
    const LONG nth = ++s.hits;
    if (nth <= kHitsLogged && core::readable(mun, eng::kMunRead)) {
        char cc[5] = {};
        memcpy(cc, mun + eng::kMunWeapon, eng::kFourCc);
        core::logf("breaktrace exploder hit: actor %08X weapon '%s' energy %.2f", actor, cc,
                   *(float*)(mun + eng::kMunEnergy));
    }
    s.g_munHit(actor, a2, mun);
}

// FUN_0151e130, thiscall on ExploderData: explode; piece spawners fill PieceCount. Threads unverified; per explode.
inline void __fastcall hkExplode(void* self, void*, uint32_t a1)
{
    State& s = g_state;
    uint8_t* d = (uint8_t*)self;
    int before = *(int*)(d + eng::kPieceCount);
    s.g_explode(self, nullptr, a1);
    const LONG nth = ++s.booms;
    if (nth > kBoomsLogged) return;
    char file[100] = "", piece[40] = "";
    core::safeCopy(file, sizeof(file), *(const char**)(d + eng::kPieceFile));
    core::safeCopy(piece, sizeof(piece), *(const char**)(d + eng::kPieceName));
    core::logf("breaktrace explode: data %p actor %08X PieceFile '%s' PieceName '%s' pieces %d -> %d flags %02X", self,
               *(uint32_t*)d, file, piece, before, *(int*)(d + eng::kPieceCount), d[eng::kExplodeFlags]);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("breaktrace", {
        {"ClipProcessor evaluate (breaktrace)", 0x00543F40, "B8E42E0000E8", (void*)&detail::hkClipEval,
         (void**)&s.g_clipEval},
        {"exploder munition impact (breaktrace)", 0x0151E420, "6AFF68????????64A100000000", (void*)&detail::hkMunHit,
         (void**)&s.g_munHit},
        {"exploder explode (breaktrace)", 0x0151E130, "6AFF64A100000000", (void*)&detail::hkExplode,
         (void**)&s.g_explode},
        {"choreography audio-sync wait (breaktrace)", 0x01442EE0, "568BF185F6744D8B06", (void*)&detail::hkChoReady,
         (void**)&s.g_choReady},
        {"choreography play (breaktrace)", 0x01445E70, "5183790C00753B8B01", (void*)&detail::hkBtPlay,
         (void**)&s.g_btPlay},
        {"layout attachments apply (breaktrace)", 0x00788280, "6AFF68????????64A100000000", (void*)&detail::hkLayApply,
         (void**)&s.g_layApply},
        {"layout attachments gate (breaktrace)", 0x00785B40, "8B4C240885C97407", (void*)&detail::hkLayGate,
         (void**)&s.g_layGate},
        {"layout attachment bind (breaktrace)", 0x00787830, "6AFF68????????64A100000000", (void*)&detail::hkLayBind,
         (void**)&s.g_layBind},
        {"clip setup (breaktrace)", 0x00552FC0, "8B44240456578BF1", (void*)&detail::hkClipSet, (void**)&s.g_clipSet},
        {"music cue push (breaktrace)", 0x0073C5C0, "81EC1C010000538BD9807B1000", (void*)&detail::hkMusicPush,
         (void**)&s.g_musicPush},
    });
}

}  // namespace trace::breaktrace
