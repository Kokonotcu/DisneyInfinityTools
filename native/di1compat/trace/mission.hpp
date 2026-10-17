// missiontrace probe: playset mission context states and actor focus points. core. Threads unverified; per tick.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"

namespace trace::mission {

struct Config {
    bool on = false;                                    // missiontrace
    std::function<bool(const char* name)> ownMission;   // a mission of the source game's playsets
    std::function<void()> ownMissionTick;               // run on each such mission's tick, before its line
};

// Installs the probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kMissionStates = 0x0228A2C0;   // 14 MissionContext state singletons, 4 bytes apart
constexpr int kMissionStateCount = 14;
// MissionContext (FUN_016748d0 parses it, FUN_01662730 dumps it): +0x10 name; states carry their name at +8
constexpr size_t kCtxName = 0x10;
constexpr size_t kCtxState = 0x44;                // m_state
constexpr size_t kCtxPresentation = 0x54;         // m_presentationState
constexpr size_t kCtxMission = 0x198;
constexpr size_t kStateName = 8;
constexpr size_t kMissionFlags = 0x3d9;
constexpr size_t kCtxFaft = 0x112;
constexpr size_t kCtxTrig = 0x113;
constexpr size_t kCtxInZone = 0x114;
constexpr size_t kCtxIgnZone = 0x135;
constexpr size_t kCtxAvReady = 0x137;
constexpr size_t kCtxB159 = 0x159;
constexpr size_t kCtxB15f = 0x15f;
constexpr size_t kCtxReason = 0x290;
constexpr size_t kCtxF3f = 0xfc;
constexpr size_t kCtxBehaviour = 0xc8;
constexpr size_t kCtxInit = 0x224;
// FocusPoint_Actor record (vtable 01DCA744, update FUN_015df390)
constexpr uint32_t kFpRecordVt = 0x01DCA744;
constexpr size_t kFpContext = 0xd4;               // its mission context (+0x10 name)
constexpr size_t kFpCho = 0xe8;                   // beacon cho / placement (FUN_015c30a0)
constexpr size_t kFpPlacement = 0xec;
constexpr size_t kFpBeacons = 0x118;              // the beacon item list (head; next at [0])
constexpr size_t kFpFlags = 0x140;                // 1 active, 4 turned off, 0x10 update
constexpr size_t kFpMissionId = 0x142;
constexpr size_t kFpParentId = 0x144;
constexpr size_t kFpParent = 0x148;               // parent task, its +0xc flags (bit 0 active)
constexpr size_t kFpCompass = 0x150;
constexpr size_t kFpTarget = 0x160;
constexpr size_t kFpCheck = 0x168;                // FUN_015930d0's last active check
constexpr size_t kFpNoHud = 0x170;
constexpr size_t kFpTag = 0x17c;
constexpr size_t kFpTagged = 0x180;
constexpr size_t kFpNode = 0x190;
constexpr size_t kParentFlags = 0xc;
// MissionFocusPoint_Actor graph node (Meridian::FocusPointActor, vtable 01DE726C; FUN_01747160 binds it)
constexpr uint32_t kFpNodeVt = 0x01DE726C;
constexpr size_t kNodeElement = 0x28;
constexpr size_t kNodeId = 0x30;
constexpr size_t kNodeMissionId = 0x40;           // 'name:id'
constexpr size_t kNodeActorTag = 0x5c;
constexpr size_t kNodeTag = 0x60;
constexpr size_t kNodeRecord = 0x64;
constexpr size_t kNodeRead = 0x68;
constexpr size_t kPinNode = 4;                    // the Update pin context: [ctx+4] = node
constexpr size_t kActorState = 0x10e;             // an actor's +0x10e (2: the scan skips it)
}  // namespace eng

constexpr LONG kRoom = 4000;            // lines per budget (locktrace10's)
constexpr LONG kRecordLines = 300;
constexpr LONG kNodeLines = 200;
constexpr size_t kMaxTracked = 400;     // records and nodes whose last line is kept
constexpr int kBeaconsCounted = 1000;
constexpr int kActorsNoted = 4;

typedef void(__fastcall* MissionTickFn)(uint8_t* self, void* edx, float dt);
typedef void(__fastcall* FpUpdateFn)(uint8_t* fp, void* edx, int a);
typedef void(__cdecl* FpPinFn)(uint8_t* pinCtx);
typedef void(__fastcall* FpThisFn)(uint8_t* self, void* edx);
typedef uint8_t*(__cdecl* ActorOfFn)(uint32_t handle);

// The last line per record or node, at most kMaxTracked of them, under its own lock.
class LastLine {
public:
    // Whether sig is new for key (it is then kept); false once kMaxTracked keys are held.
    bool changed(uint8_t* key, const std::string& sig)
    {
        std::lock_guard<std::mutex> lock(mx);
        if (last.size() > kMaxTracked) return false;
        std::string& l = last[key];
        if (l == sig) return false;
        l = sig;
        return true;
    }

private:
    std::mutex mx;                                    // guards last
    std::unordered_map<uint8_t*, std::string> last;
};

struct State {
    // originals: set once by hook() in install()
    MissionTickFn g_missionTick = nullptr;
    FpUpdateFn g_fpUpdate = nullptr;
    FpPinFn g_fpNodeUpdate = nullptr;
    FpThisFn g_fpApply = nullptr, g_fpScan = nullptr;
    ActorOfFn g_actorOf = nullptr;
    std::function<bool(const char*)> ownMission;          // set once in install()
    std::function<void()> ownMissionTick;                 // set once in install()
    std::unordered_map<uint8_t*, std::string> tickLast;   // unguarded as before; the tick's thread is unverified (B-48)
    LastLine updateLast, applyLast, scanLast;
    std::atomic<LONG> updateLines{0}, nodeLines{0}, applyLines{0}, scanLines{0};   // budgets; any thread
};
inline State g_state;

// handle -> actor calls counted inside one tag apply (1) or scan (2); per thread, fibers share it (B-10).
struct Scope {
    int mode, calls, actors;
    uint8_t states[kActorsNoted];
};
inline thread_local Scope g_scope;

inline bool room(std::atomic<LONG>& n) { return ++n <= kRoom; }

inline int stateIndex(uint32_t v)
{
    for (int i = 0; i < eng::kMissionStateCount; ++i)
        if (*(uint32_t*)core::rt(eng::kMissionStates + 4 * i) == v) return i;
    return -1;
}

inline const char* stateName(uint32_t st, char* buf)
{
    core::safeCopy(buf, 48, st ? *(const char**)(uintptr_t)(st + eng::kStateName) : "<null>");
    return buf;
}

// FUN_0166ffb0, thiscall: MissionContext update, per frame per context; a playset mission's state when it changes.
inline void __fastcall hkMissionTick(uint8_t* self, void*, float dt)
{
    State& s = g_state;
    s.g_missionTick(self, nullptr, dt);
    const char* name = *(const char**)(self + eng::kCtxName);
    if (!name || !s.ownMission(name)) return;
    if (s.ownMissionTick) s.ownMissionTick();
    uint32_t state = *(uint32_t*)(self + eng::kCtxState), m = *(uint32_t*)(self + eng::kCtxMission);
    char sn[48], pn[48], sig[320];
    // never cut: at most 266 characters
    snprintf(sig, sizeof(sig), "state=%d(%s) pres=%s mission=%08X mflags=%02X faft=%d trig=%d b159=%d b15f=%d "
             "inzone=%d ignzone=%d avready=%d reason=%d f3f=%08X beh=%d init=%d", stateIndex(state),
             stateName(state, sn), stateName(*(uint32_t*)(self + eng::kCtxPresentation), pn), m,
             m ? *(uint8_t*)(uintptr_t)(m + eng::kMissionFlags) : 0xff, self[eng::kCtxFaft], self[eng::kCtxTrig],
             self[eng::kCtxB159], self[eng::kCtxB15f], self[eng::kCtxInZone], self[eng::kCtxIgnZone],
             self[eng::kCtxAvReady], *(int*)(self + eng::kCtxReason), *(uint32_t*)(self + eng::kCtxF3f),
             *(int*)(self + eng::kCtxBehaviour), self[eng::kCtxInit]);
    std::string& l = s.tickLast[self];
    if (l == sig) return;
    l = sig;
    core::logf("mission %.60s: %s", name, sig);
}

// A record's or context's mission name into name (64 bytes); "" when unreadable.
inline void contextName(const uint8_t* ctx, char* name)
{
    const bool named = ctx && core::readable(ctx + eng::kCtxName, 4);
    core::safeCopy(name, 64, named ? *(const char**)(ctx + eng::kCtxName) : nullptr);
}

// The record's state as one line; sig holds 512 bytes (at most 460 are written).
inline void recordLine(uint8_t* fp, char* sig, size_t cap)
{
    uint8_t *parent = *(uint8_t**)(fp + eng::kFpParent), *node = *(uint8_t**)(fp + eng::kFpNode);
    char tag[64], cho[64], pl[64], ntag[64] = "";
    void *nRec = nullptr, *nAct = nullptr;
    if (node && core::readable(node + eng::kNodeActorTag, 12)) {
        core::cstr(node + eng::kNodeTag, ntag, sizeof(ntag));
        nAct = *(void**)(node + eng::kNodeActorTag);
        nRec = *(void**)(node + eng::kNodeRecord);
    }
    uint8_t f = fp[eng::kFpFlags];
    const bool parentRead = parent && core::readable(parent + eng::kParentFlags, 1);
    snprintf(sig, cap, "flags %02X (active %d, off %d, update %d) check %d, parent %d %p flags %d, node %p (record %p, "
             "tag '%s', ActorTag %p), tagged %d tag '%s', beacon '%s' '%s', compass %d target %d nohud %d", f, f & 1,
             f >> 2 & 1, f >> 4 & 1, fp[eng::kFpCheck], *(int16_t*)(fp + eng::kFpParentId), parent,
             parentRead ? parent[eng::kParentFlags] : -1, node, nRec, ntag, nAct, fp[eng::kFpTagged],
             core::cstr(fp + eng::kFpTag, tag, sizeof(tag)), core::cstr(fp + eng::kFpCho, cho, sizeof(cho)),
             core::cstr(fp + eng::kFpPlacement, pl, sizeof(pl)), fp[eng::kFpCompass], fp[eng::kFpTarget],
             fp[eng::kFpNoHud]);
}

// FUN_015df390, thiscall (int): FocusPoint_Actor record update. One line per record whenever its state changes.
inline void __fastcall hkFpUpdate(uint8_t* fp, void*, int a)
{
    State& s = g_state;
    s.g_fpUpdate(fp, nullptr, a);
    if (!fp || *(uint32_t*)fp != (uint32_t)core::rt(eng::kFpRecordVt)) return;   // the engine's own this: no probe
    uint8_t* ctx = *(uint8_t**)(fp + eng::kFpContext);
    char name[64];
    contextName(ctx, name);
    if (!ctx || !s.ownMission(name)) return;
    char sig[512];
    recordLine(fp, sig, sizeof(sig));
    if (!s.updateLast.changed(fp, sig)) return;
    if (room(s.updateLines) && s.updateLines <= kRecordLines)
        core::logf("focus point %.60s:%d %s", name, *(int16_t*)(fp + eng::kFpMissionId), sig);
}

// FUN_01730e80, cdecl (pin context): FocusPointActor's Update pin; it only marks the record.
inline void __cdecl hkFpNodeUpdate(uint8_t* pinCtx)
{
    State& s = g_state;
    const bool pinned = pinCtx && core::readable(pinCtx + eng::kPinNode, 4);
    uint8_t* node = pinned ? *(uint8_t**)(pinCtx + eng::kPinNode) : nullptr;
    const bool readableNode = node && core::readable(node, eng::kNodeRead);
    const bool mine = readableNode && *(uint32_t*)node == (uint32_t)core::rt(eng::kFpNodeVt);
    if (mine) {
        char id[96];
        core::cstr(node + eng::kNodeMissionId, id, sizeof(id));
        if (s.ownMission(id) && room(s.nodeLines) && s.nodeLines <= kNodeLines)
            core::logf("focus point node %p '%s' Update: record %p, mission element %p, id %d", node, id,
                       *(void**)(node + eng::kNodeRecord), *(void**)(node + eng::kNodeElement),
                       *(int*)(node + eng::kNodeId));
    }
    s.g_fpNodeUpdate(pinCtx);
}

// FUN_0074b200, cdecl: handle -> actor; counted only inside a traced tag apply or scan. Very frequent.
inline uint8_t* __cdecl hkActorOf(uint32_t handle)
{
    uint8_t* a = g_state.g_actorOf(handle);
    Scope& sc = g_scope;
    if (!sc.mode) return a;
    ++sc.calls;
    if (a && sc.actors < kActorsNoted) sc.states[sc.actors] = a[eng::kActorState];
    if (a) ++sc.actors;
    return a;
}

// The scope's counts into buf (96 bytes; at most 65 are written).
inline const char* scopeText(char* buf, size_t cap)
{
    const Scope& sc = g_scope;
    snprintf(buf, cap, "%d handles -> %d actors (+0x10e %d %d %d %d)", sc.calls, sc.actors,
             sc.actors > 0 ? sc.states[0] : -1, sc.actors > 1 ? sc.states[1] : -1, sc.actors > 2 ? sc.states[2] : -1,
             sc.actors > 3 ? sc.states[3] : -1);
    return buf;
}

// FUN_0173a3f0, thiscall: the node tags its ActorList's actors (each handle through FUN_0074b200).
inline void __fastcall hkFpApply(uint8_t* node, void*)
{
    State& s = g_state;
    char id[96] = "";
    const bool ours = node && *(uint32_t*)node == (uint32_t)core::rt(eng::kFpNodeVt);
    if (!ours || !s.ownMission(core::cstr(node + eng::kNodeMissionId, id, sizeof(id))))
        return s.g_fpApply(node, nullptr);
    g_scope = Scope{1};
    s.g_fpApply(node, nullptr);
    char t[96], sig[160];
    snprintf(sig, sizeof(sig), "tag apply: ActorList %s, ActorTag %p", scopeText(t, sizeof(t)),
             *(void**)(node + eng::kNodeActorTag));
    g_scope.mode = 0;
    if (!s.applyLast.changed(node, sig)) return;
    if (room(s.applyLines) && s.applyLines <= kNodeLines) core::logf("focus point node %p '%s' %s", node, id, sig);
}

// FUN_015d5f80, thiscall (slot 12): the record scans its tag's actors into beacon items. Per scan.
inline void __fastcall hkFpScan(uint8_t* fp, void*)
{
    State& s = g_state;
    const bool record = fp && *(uint32_t*)fp == (uint32_t)core::rt(eng::kFpRecordVt);
    uint8_t* ctx = record ? *(uint8_t**)(fp + eng::kFpContext) : nullptr;
    char name[64];
    contextName(ctx, name);
    if (!ctx || !s.ownMission(name)) return s.g_fpScan(fp, nullptr);
    g_scope = Scope{2};
    s.g_fpScan(fp, nullptr);
    int items = 0;
    for (uint8_t** e = *(uint8_t***)(fp + eng::kFpBeacons); e && items < kBeaconsCounted; e = (uint8_t**)e[0]) ++items;
    char t[96], sig[200];
    snprintf(sig, sizeof(sig), "scan: tagged %s, active %d, beacon items %d", scopeText(t, sizeof(t)),
             fp[eng::kFpFlags] & 1, items);
    g_scope.mode = 0;
    if (!s.scanLast.changed(fp, sig)) return;
    if (room(s.scanLines) && s.scanLines <= kRecordLines)
        core::logf("focus point %.60s:%d %s", name, *(int16_t*)(fp + eng::kFpMissionId), sig);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    s.ownMission = c.ownMission;
    s.ownMissionTick = c.ownMissionTick;
    return core::installFeature("missiontrace", {
        {"MissionContext update (trace)", 0x0166FFB0, "6AFF68????????64A100000000", (void*)&detail::hkMissionTick,
         (void**)&s.g_missionTick},
        {"FocusPoint_Actor update (missiontrace)", 0x015DF390, "6AFF64A10000000068????????50",
         (void*)&detail::hkFpUpdate, (void**)&s.g_fpUpdate},
        {"FocusPointActor Update pin (missiontrace)", 0x01730E80, "8B4424048B40048B486485C9",
         (void*)&detail::hkFpNodeUpdate, (void**)&s.g_fpNodeUpdate},
        {"FocusPointActor tag apply (missiontrace)", 0x0173A3F0, "6AFF68????????64A100000000",
         (void*)&detail::hkFpApply, (void**)&s.g_fpApply},
        {"FocusPoint_Actor tag scan (missiontrace)", 0x015D5F80, "6AFF68????????64A100000000",
         (void*)&detail::hkFpScan, (void**)&s.g_fpScan},
        {"handle to actor (missiontrace)", 0x0074B200, "83EC088B44240C8D0C2451", (void*)&detail::hkActorOf,
         (void**)&s.g_actorOf},
    });
}

}  // namespace trace::mission
