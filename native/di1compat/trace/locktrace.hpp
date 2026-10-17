// locktrace10 probe: control locks, contexts, choreography, messages, mission gates, compass. core. Threads unverified.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"

namespace trace::locktrace {

struct Config {
    bool on = false;   // locktrace10
};

// Installs the probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kScreenByHandle = 0x0154FED0;   // FUN_0154fed0: thiscall (handle) on the screen manager
constexpr uint32_t kScreenManager = 0x02289E88;    // *: the screen manager
constexpr uint32_t kPauseFlag = 0x0225F478;        // DAT_0225f478[0]: Pause_UnPauseFromPausedScreenIfPaused's test
// node fields: ContextName CString (FUN_01224880), resolved cho path (FUN_017e8680), MessageSender's CStrings
constexpr size_t kContextName = 0xa8;
constexpr size_t kChoPath = 0x8c;
constexpr size_t kChoHandler = 0x30;               // the cho node's event handler sits at node+0x30
constexpr size_t kMsgName = 0x1c;
constexpr size_t kMsgTarget = 0x28;
constexpr size_t kMsgData = 0x34;
constexpr size_t kListenerNode = 0x20;             // MessageHandler listener +0x20: its node
constexpr size_t kHandlerMsgName = 0x4c;           // the handler node's MessageName (FUN_017fd6b0)
constexpr size_t kMonitorMission = 0x64;           // MissionMonitor's MissionName (FUN_01743f60)
// LoadScreenActive (FUN_0145b9a0 on node+0x14): IsOn +0x24, pending FireInit +0xc
constexpr size_t kLoadNodeBase = 0x14;
constexpr size_t kLoadIsOn = 0x24;
constexpr size_t kLoadFireInit = 0xc;
// MasterNetSync (FUN_006ec4f0): node+4 the vbtable pointer, its +4 the ContainedBase offset
constexpr size_t kNetVbase = 4;
constexpr int kVfGraph = 2;                        // ContainedBase vf[2]: its GraphInstance
constexpr int kVfNetworked = 4;                    // GraphInstance vf+0x10 (FUN_006ab280)
constexpr int kVfHost = 5;                         // GraphInstance vf+0x14
// ColVisToggle (0x01808FE0 / 0x01803550): IsOn +0x30, mode bits +0x38; actor list {?, items, count}
constexpr size_t kColVisOn = 0x30;
constexpr size_t kColVisMode = 0x38;
constexpr size_t kListCount = 8;
// BeaconMap entry (FUN_01591570) {name, cho, placement, float}
constexpr size_t kBeaconCho = 4;
constexpr size_t kBeaconPlacement = 8;
constexpr size_t kBeaconOffset = 0xc;
// mission manager slots +0x10 + 4 * player (FUN_015ba4d0); mission +0x140 its context, name at +0x10
constexpr size_t kTrackSlots = 0x10;
constexpr int kTrackPlayers = 4;
constexpr size_t kMissionContext = 0x140;
constexpr size_t kContextNameField = 0x10;
// MissionNavData (FUN_00c13c50); its marker 0: +0x24 visible, +8 x, +0xc y, +0x20 alpha 0..100
constexpr size_t kNavRead = 0xa8;
constexpr size_t kNavMarker0 = 0x10;
constexpr size_t kNavOn = 0x68;
constexpr size_t kNavHidden = 0x69;
constexpr size_t kNavEnabled = 0x75;               // CheckCompass
constexpr size_t kNavTarget = 0x78;                // the target FUN_00be3c20 last drew
constexpr size_t kNavScreen = 0xa4;                // hud_Marker screen handle
constexpr size_t kMarkerRead = 0x28;
constexpr size_t kMarkerX = 8;
constexpr size_t kMarkerY = 0xc;
constexpr size_t kMarkerAlpha = 0x20;
constexpr size_t kMarkerVisible = 0x24;
constexpr size_t kScreenLoaded = 0x6d;
// mission progress entries (FUN_0157cd10): pointers +0x2ac, count +0x2b0
constexpr size_t kProgressEntries = 0x2ac;
constexpr size_t kProgressCount = 0x2b0;
constexpr size_t kEntryRead = 0x24;                // [0] state, [1] string, [2] key, [3] suffix, [4]/[5] floats
constexpr size_t kEntryIsKey = 0x1c;
constexpr size_t kEntryOptional = 0x1d;
}  // namespace eng

constexpr LONG kRoom = 4000;           // lines per budget
constexpr LONG kNavLines = 300;
constexpr LONG kProgressLines = 400;
constexpr int kNavFields = 6;
constexpr float kNavMovedM = 2.f;      // a target moved this far counts as a change
constexpr int kProgressListed = 12;
constexpr int kChoEventLast = 3;       // events 0 and 2 turn 'Playing' on, 1 and 3 off (FUN_017deef0)

typedef void(__fastcall* CtlLockFn)(void* self, void* edx, int player, const void* reason);
typedef void(__fastcall* CtxFn)(uint8_t* node, void* edx, uint32_t agent);
typedef void(__fastcall* ChoPlayFn)(uint8_t* node, void* edx);
typedef void(__fastcall* ChoEventFn)(uint8_t* handler, void* edx, const int* ev);
typedef void(__fastcall* MsgSendFn)(uint8_t* node, void* edx, void* payload);
typedef void(__fastcall* MsgRecvFn)(uint8_t* listener, void* edx, const char* data, uint32_t a2, uint32_t a3);
typedef uint32_t(__fastcall* MonPinFn)(uint8_t* node, void* edx, int pin);
typedef void(__fastcall* TickFn)(uint8_t* self, void* edx, uint32_t a1);
typedef void(__fastcall* NetSyncFn)(uint8_t* node, void* edx, void* payload);
typedef void(__fastcall* ColVisFn)(uint8_t* node, void* edx, void* arg);
typedef uint8_t*(__fastcall* BeaconLookupFn)(void* self, void* edx, const char* name, const char* fallback);
typedef void(__fastcall* TrackFn)(uint8_t* mgr, void* edx, uint8_t* mission, int player, int flag);
typedef void(__cdecl* NavUpdateFn)(uint8_t* nav);
typedef void(__fastcall* MisProgressFn)(uint8_t* mission, void* edx, void* table);

struct State {
    // originals: set once by hook() in install()
    CtlLockFn g_ctlLock = nullptr, g_ctlUnlock = nullptr;
    CtxFn g_ctxOn = nullptr, g_ctxOff = nullptr;
    ChoPlayFn g_choPlay = nullptr;
    ChoEventFn g_choEvent = nullptr;
    MsgSendFn g_msgSend = nullptr;
    MsgRecvFn g_msgRecv = nullptr;
    MonPinFn g_monPin = nullptr;
    TickFn g_loadTick = nullptr;
    NetSyncFn g_netSync = nullptr;
    ColVisFn g_colVisLoad = nullptr, g_colVisApply = nullptr;
    BeaconLookupFn g_beaconLookup = nullptr;
    TrackFn g_track = nullptr;
    NavUpdateFn g_navUpdate = nullptr;
    MisProgressFn g_misProgress = nullptr;
    // budgets, any thread: lock lines have their own, so cho and message lines cannot use it up first
    std::atomic<LONG> lockLines{0}, traceLines{0}, gateLines{0};
    std::atomic<LONG> monLines{0}, colLoadLines{0}, colApplyLines{0}, beaconLines{0}, trackLines{0}, navLines{0};
    std::atomic<LONG> progressLines{0};
    core::LogOnce<uint8_t*> loadSeen{64};          // load-screen nodes given a first-tick line
    core::LogOnce<std::string> beaconSeen{300};    // beacon names logged
    core::LogOnce<std::string> progressSeen{201};  // (mission, entry count) pairs logged
    // the compass draw's last state, shared by every MissionNavData (B-29); unguarded, its thread unverified (B-48)
    int navLast[kNavFields] = {-9, -9, -9, -9, -9, -9};
    float navLastTarget[3] = {};
};
inline State g_state;

inline bool room(std::atomic<LONG>& n) { return ++n <= kRoom; }

inline uint32_t callerVa(void* ret) { return core::toStaticVa((uintptr_t)ret); }

// GamePlayers lock FUN_0139b8d0 / unlock FUN_0139b960 (vtable +0x14 / +0x18), thiscall (player, CString reason). Rare.
inline void __fastcall hkCtlLock(void* self, void*, int player, const void* reason)
{
    State& s = g_state;
    char r[128];
    if (room(s.lockLines))
        core::logf("control LOCK player %d reason '%s' (from %08X)", player, core::cstr(reason, r, sizeof(r)),
                   callerVa(_ReturnAddress()));
    s.g_ctlLock(self, nullptr, player, reason);
}

inline void __fastcall hkCtlUnlock(void* self, void*, int player, const void* reason)
{
    State& s = g_state;
    char r[128];
    if (room(s.lockLines))
        core::logf("control unlock player %d reason '%s' (from %08X)", player, core::cstr(reason, r, sizeof(r)),
                   callerVa(_ReturnAddress()));
    s.g_ctlUnlock(self, nullptr, player, reason);
}

// ControllerContextNode apply FUN_0121d5c0 / remove FUN_0121d8c0 per agent, thiscall (agent). Per graph event.
inline void __fastcall hkCtxOn(uint8_t* node, void*, uint32_t agent)
{
    State& s = g_state;
    char n[200];
    if (room(s.lockLines))
        core::logf("controller context ON (inputs disabled) '%s' agent %08X (from %08X)",
                   core::cstr(node + eng::kContextName, n, sizeof(n)), agent, callerVa(_ReturnAddress()));
    s.g_ctxOn(node, nullptr, agent);
}

inline void __fastcall hkCtxOff(uint8_t* node, void*, uint32_t agent)
{
    State& s = g_state;
    char n[200];
    if (room(s.lockLines))
        core::logf("controller context off '%s' agent %08X (from %08X)",
                   core::cstr(node + eng::kContextName, n, sizeof(n)), agent, callerVa(_ReturnAddress()));
    s.g_ctxOff(node, nullptr, agent);
}

// FUN_017e91b0, thiscall: Choreography node play (Link_In). Per graph event.
inline void __fastcall hkChoPlay(uint8_t* node, void*)
{
    State& s = g_state;
    s.g_choPlay(node, nullptr);
    char n[200];
    if (room(s.traceLines)) core::logf("cho node %p play '%s'", node, core::cstr(node + eng::kChoPath, n, sizeof(n)));
}

// FUN_017e5890, thiscall on node+0x30: the cho node's event handler. Per event.
inline void __fastcall hkChoEvent(uint8_t* handler, void*, const int* ev)
{
    State& s = g_state;
    int type = ev ? ev[0] : -1;
    const bool playing = type >= 0 && type <= kChoEventLast;
    if (playing && room(s.traceLines)) {
        uint8_t* node = handler - eng::kChoHandler;
        char n[200];
        core::logf("cho node %p event %d (Playing %s) '%s'", node, type, type == 0 || type == 2 ? "on" : "off",
                   core::cstr(node + eng::kChoPath, n, sizeof(n)));
    }
    s.g_choEvent(handler, nullptr, ev);
}

// FUN_018120b0, thiscall: MessageSender send (Link_In). Per message.
inline void __fastcall hkMsgSend(uint8_t* node, void*, void* payload)
{
    State& s = g_state;
    char n[128], t[128], d[128];
    if (room(s.traceLines))
        core::logf("message send '%s' target '%s' data '%s'", core::cstr(node + eng::kMsgName, n, sizeof(n)),
                   core::cstr(node + eng::kMsgTarget, t, sizeof(t)), core::cstr(node + eng::kMsgData, d, sizeof(d)));
    s.g_msgSend(node, nullptr, payload);
}

// FUN_018049f0, thiscall on the listener: MessageHandler delivery; a sent name without one reached no graph.
inline void __fastcall hkMsgRecv(uint8_t* listener, void*, const char* data, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    char n[128], d[128];
    if (room(s.traceLines)) {
        const void* node = nullptr;
        if (core::readable(listener + eng::kListenerNode, sizeof(node)))
            node = *(void* const*)(listener + eng::kListenerNode);
        core::safeCopy(d, sizeof(d), data);
        core::logf("message recv '%s' data '%s'",
                   node ? core::cstr((const uint8_t*)node + eng::kHandlerMsgName, n, sizeof(n)) : "?", d);
    }
    s.g_msgRecv(listener, nullptr, data, a2, a3);
}

// FUN_01730490, thiscall (pin): MissionMonitor fires an output, once per output. Its own budget (load fires many).
inline uint32_t __fastcall hkMonPin(uint8_t* node, void*, int pin)
{
    static const char* const kPin[] = {"Locked",  "Now Available", "Talking",  "Actived",   "Completed",
                                       "Failed",  "Aborted",       "Selected", "Unselected"};
    State& s = g_state;
    char n[128];
    const bool known = pin >= 0 && pin < (int)(sizeof(kPin) / sizeof(kPin[0]));
    if (room(s.monLines))
        core::logf("mission monitor '%s' fires %s (pin %d)", core::cstr(node + eng::kMonitorMission, n, sizeof(n)),
                   known ? kPin[pin] : "?", pin);
    return s.g_monPin(node, nullptr, pin);
}

// FUN_0145b9a0, thiscall on node+0x14: LoadScreenActive update. Per node update, likely per frame.
inline void __fastcall hkLoadTick(uint8_t* self, void*, uint32_t a1)
{
    State& s = g_state;
    uint8_t on = self[eng::kLoadIsOn], init = self[eng::kLoadFireInit];
    s.g_loadTick(self, nullptr, a1);
    const bool first = s.loadSeen.first(self);
    const bool flipped = self[eng::kLoadIsOn] != on;
    const bool initFired = init && !self[eng::kLoadFireInit];
    const bool fired = flipped || initFired;
    const bool news = first || fired;
    if (!news || !room(s.gateLines)) return;
    const char* what = self[eng::kLoadIsOn] ? "fires Started" : "fires Stopped";
    core::logf("load screen node %p: %s (IsOn %d -> %d%s)", self - eng::kLoadNodeBase,
               fired ? what : "first tick, no fire", on, self[eng::kLoadIsOn], init ? ", FireInit" : "");
}

// FUN_006ec4f0, thiscall (payload): MasterNetSync Link_In. Per Link_In; the virtual calls are the original's own.
inline void __fastcall hkNetSync(uint8_t* node, void*, void* payload)
{
    State& s = g_state;
    if (room(s.gateLines)) {
        typedef void*(__fastcall * GetFn)(void* self, void* edx);
        typedef bool(__fastcall * FlagFn)(void* self, void* edx);
        uint8_t* vb = node + eng::kNetVbase + *(int*)(*(uint8_t**)(node + eng::kNetVbase) + 4);   // as FUN_006ec4f0
        void* g = ((GetFn)(*(void***)vb)[eng::kVfGraph])(vb, nullptr);
        int net = g ? ((FlagFn)(*(void***)g)[eng::kVfNetworked])(g, nullptr) : -1;
        int host = g ? ((FlagFn)(*(void***)g)[eng::kVfHost])(g, nullptr) : -1;
        const bool linkOut = net == 1 && host == 1;
        core::logf("net sync node %p Link_In: graph networked %d host %d -> 3.0 fires Private%s", node, net, host,
                   linkOut ? " and Link_Out" : " only");
    }
    s.g_netSync(node, nullptr, payload);
}

// ColVisToggle mode bits (1 collision, 2 visibility) as text.
inline const char* colVisMode(uint8_t bits)
{
    static const char* const kMode[] = {"none", "collision only", "visibility only", "collision+visibility"};
    return kMode[bits & 3];
}

// 0x01808FE0, thiscall (record): ColVisToggle LoadParameters sets mode bits and IsOn. Per load.
inline void __fastcall hkColVisLoad(uint8_t* node, void*, void* rec)
{
    State& s = g_state;
    s.g_colVisLoad(node, nullptr, rec);
    if (room(s.colLoadLines))
        core::logf("colvis toggle %p loaded: %s, IsOn %d", node, colVisMode(node[eng::kColVisMode]),
                   node[eng::kColVisOn]);
}

// 0x01803550, thiscall (actor list): ColVisToggle applies IsOn to its actors. Per apply.
inline void __fastcall hkColVisApply(uint8_t* node, void*, void* list)
{
    State& s = g_state;
    uint32_t n = list ? *(const uint32_t*)((const uint8_t*)list + eng::kListCount) : 0;
    if (n && room(s.colApplyLines))
        core::logf("colvis toggle %p applies %s (%s) to %u actors", node, node[eng::kColVisOn] ? "ON" : "off",
                   colVisMode(node[eng::kColVisMode]), n);
    s.g_colVisApply(node, nullptr, list);
}

// FUN_01591570, thiscall (CString name, fallback by value; the callee releases both): BeaconMap lookup.
inline uint8_t* __fastcall hkBeaconLookup(void* self, void*, const char* name, const char* fallback)
{
    State& s = g_state;
    char n[96], fb[96];
    core::safeCopy(n, sizeof(n), name);   // copies first: the callee releases its by-value CStrings
    core::safeCopy(fb, sizeof(fb), fallback);
    uint8_t* e = s.g_beaconLookup(self, nullptr, name, fallback);
    const bool first = s.beaconSeen.first(n);
    if (!first || !room(s.beaconLines)) return e;
    char c[96], pl[96];
    if (e)
        core::logf("beacon map '%s' -> cho '%s' placement '%s' offset %.2f", n,
                   core::cstr(e + eng::kBeaconCho, c, sizeof(c)), core::cstr(e + eng::kBeaconPlacement, pl, sizeof(pl)),
                   *(float*)(e + eng::kBeaconOffset));
    else
        core::logf("beacon map '%s' (fallback '%s') -> no entry", n, fb);
    return e;
}

// A mission's context name into buf (96 bytes), "nothing" without a mission.
inline const char* missionName(uint8_t* m, char* buf)
{
    uint8_t* ctx = m ? *(uint8_t**)(m + eng::kMissionContext) : nullptr;
    core::safeCopy(buf, 96, ctx ? *(const char**)(ctx + eng::kContextNameField) : "nothing");
    return buf;
}

// FUN_015ba4d0, thiscall (mission, player, flag): the compass follows a tracked mission. Rare.
inline void __fastcall hkTrack(uint8_t* mgr, void*, uint8_t* mission, int player, int flag)
{
    State& s = g_state;
    uint8_t* before[eng::kTrackPlayers];
    memcpy(before, mgr + eng::kTrackSlots, sizeof(before));
    s.g_track(mgr, nullptr, mission, player, flag);
    for (int slot = 0; slot < eng::kTrackPlayers; ++slot) {
        uint8_t* now = *(uint8_t**)(mgr + eng::kTrackSlots + 4 * slot);
        char a[96], b[96];
        if (now != before[slot] && room(s.trackLines))
            core::logf("compass: player slot %d tracks '%s' (asked for '%s', player %d)", slot, missionName(now, a),
                       missionName(mission, b), player);
    }
}

// Marker alpha in steps: -1 none, 0 under 1, 1 under 99, 2 full.
inline int alphaStep(float alpha)
{
    if (alpha < 0) return -1;
    if (alpha < 1) return 0;
    return alpha < 99 ? 1 : 2;
}

// The hud_Marker screen's loaded byte: -1 no manager or handle, -2 no screen.
inline int markerScreenLoaded(int h)
{
    void* mgr = *(void**)core::rt(eng::kScreenManager);
    if (!mgr || h == -1) return -1;
    uint8_t* scr = ((uint8_t*(__fastcall*)(void*, void*, int))core::rt(eng::kScreenByHandle))(mgr, nullptr, h);
    return scr ? scr[eng::kScreenLoaded] : -2;
}

// FUN_00c13c50, cdecl: MissionNavData's update (the compass draw). Likely per frame per avatar.
inline void __cdecl hkNavUpdate(uint8_t* nav)
{
    State& s = g_state;
    s.g_navUpdate(nav);
    if (!nav || !core::readable(nav, eng::kNavRead)) return;
    uint8_t* m0 = *(uint8_t**)(nav + eng::kNavMarker0);
    const bool mOk = m0 && core::readable(m0, eng::kMarkerRead);
    int vis = mOk ? m0[eng::kMarkerVisible] : -1;
    float alpha = mOk ? *(float*)(m0 + eng::kMarkerAlpha) : -1.f;
    const float* tg = (float*)(nav + eng::kNavTarget);
    int h = *(int*)(nav + eng::kNavScreen), loaded = markerScreenLoaded(h);
    int paused = *(uint8_t*)core::rt(eng::kPauseFlag);
    int now[kNavFields] = {nav[eng::kNavEnabled], nav[eng::kNavOn] | nav[eng::kNavHidden] << 1, loaded, vis,
                           alphaStep(alpha), paused};
    float* lastT = s.navLastTarget;
    const bool moved = fabsf(tg[0] - lastT[0]) + fabsf(tg[1] - lastT[1]) + fabsf(tg[2] - lastT[2]) > kNavMovedM;
    if (!memcmp(now, s.navLast, sizeof(now)) && !moved) return;
    memcpy(s.navLast, now, sizeof(now));
    memcpy(lastT, tg, sizeof(s.navLastTarget));
    if (!room(s.navLines) || s.navLines > kNavLines) return;
    core::logf("compass draw: enabled %d on %d hidden %d, hud_Marker screen %d loaded %d, marker0 visible %d at "
               "(%.0f %.0f) alpha %.0f, target (%.2f %.2f %.2f), paused %d", nav[eng::kNavEnabled], nav[eng::kNavOn],
               nav[eng::kNavHidden], h, loaded, vis, mOk ? *(float*)(m0 + eng::kMarkerX) : 0.f,
               mOk ? *(float*)(m0 + eng::kMarkerY) : 0.f, alpha, tg[0], tg[1], tg[2], paused);
}

// One progress entry's line, if it is readable.
inline void logProgressEntry(int i, const uint32_t* e)
{
    if (!e || !core::readable(e, eng::kEntryRead)) return;
    char a[96], k[96], sfx[96];
    core::safeCopy(a, sizeof(a), (const char*)(uintptr_t)e[1]);
    core::safeCopy(k, sizeof(k), (const char*)(uintptr_t)e[2]);
    core::safeCopy(sfx, sizeof(sfx), (const char*)(uintptr_t)e[3]);
    core::logf("  [%d] state %u progress '%s' key '%s' suffix '%s' %.0f/%.0f isKey %d optional %d", i, e[0], a, k, sfx,
               *(const float*)(e + 4), *(const float*)(e + 5), ((const uint8_t*)e)[eng::kEntryIsKey],
               ((const uint8_t*)e)[eng::kEntryOptional]);
}

// FUN_0157cd10, thiscall (Lua table): writes an instantiated mission's progress entries. One line per mission/count.
inline void __fastcall hkMisProgress(uint8_t* mission, void*, void* table)
{
    State& s = g_state;
    s.g_misProgress(mission, nullptr, table);
    const bool readableMission = mission && core::readable(mission + eng::kProgressEntries, 8);
    if (!readableMission || !core::readable(mission + eng::kMissionContext, 4)) return;
    char name[96];
    uint8_t* ctx = *(uint8_t**)(mission + eng::kMissionContext);
    const bool named = ctx && core::readable(ctx + eng::kContextNameField, 4);
    core::safeCopy(name, sizeof(name), named ? *(const char**)(ctx + eng::kContextNameField) : "?");
    uint8_t** arr = *(uint8_t***)(mission + eng::kProgressEntries);
    int n = *(int*)(mission + eng::kProgressCount);
    if (!s.progressSeen.first(std::string(name) + "#" + std::to_string(n))) return;
    if (!room(s.progressLines) || s.progressLines > kProgressLines) return;
    core::logf("mission progress '%s': %d entries", name, n);
    for (int i = 0; i < n && i < kProgressListed && arr; ++i) {
        if (!core::readable(arr + i, 4)) break;
        logProgressEntry(i, (const uint32_t*)arr[i]);
    }
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("locktrace10", {
        {"GamePlayers lock controls (locktrace10)", 0x0139B8D0, "833D????????00558BE9", (void*)&detail::hkCtlLock,
         (void**)&s.g_ctlLock},
        {"GamePlayers unlock controls (locktrace10)", 0x0139B960, "55578B7C240C8BE985FF", (void*)&detail::hkCtlUnlock,
         (void**)&s.g_ctlUnlock},
        {"controller context apply (locktrace10)", 0x0121D5C0, "8B44240483EC0C53568BD9", (void*)&detail::hkCtxOn,
         (void**)&s.g_ctxOn},
        {"controller context remove (locktrace10)", 0x0121D8C0, "8B44240483EC0C56578BF9", (void*)&detail::hkCtxOff,
         (void**)&s.g_ctxOff},
        {"choreography node play (locktrace10)", 0x017E91B0, "83EC1C5556578BF1E8", (void*)&detail::hkChoPlay,
         (void**)&s.g_choPlay},
        {"choreography node event (locktrace10)", 0x017E5890, "538BD9568D73D057", (void*)&detail::hkChoEvent,
         (void**)&s.g_choEvent},
        {"message sender send (locktrace10)", 0x018120B0, "6AFF68????????64A100000000", (void*)&detail::hkMsgSend,
         (void**)&s.g_msgSend},
        {"message handler delivery (locktrace10)", 0x018049F0, "83EC08568BF18B4E20", (void*)&detail::hkMsgRecv,
         (void**)&s.g_msgRecv},
        {"mission monitor fire (locktrace10)", 0x01730490, "5156578BF18B068B504833FF57", (void*)&detail::hkMonPin,
         (void**)&s.g_monPin},
        {"load screen active update (locktrace10)", 0x0145B9A0, "5356578BF98D77ECBB01000000",
         (void*)&detail::hkLoadTick, (void**)&s.g_loadTick},
        {"master net sync Link_In (locktrace10)", 0x006EC4F0,
         "6AFF68????????64A100000000506489250000000083EC30535556578BF1", (void*)&detail::hkNetSync,
         (void**)&s.g_netSync},
        {"colvis toggle parameters (locktrace10)", 0x01808FE0,
         "6AFF68????????64A1000000005064892500000000518B442414535657508BF1", (void*)&detail::hkColVisLoad,
         (void**)&s.g_colVisLoad},
        {"colvis toggle apply (locktrace10)", 0x01803550, "558B6C2408565733FF8BF1", (void*)&detail::hkColVisApply,
         (void**)&s.g_colVisApply},
        {"beacon map lookup (locktrace10)", 0x01591570, "83EC088B81740100005355", (void*)&detail::hkBeaconLookup,
         (void**)&s.g_beaconLookup},
        {"compass mission track (locktrace10)", 0x015BA4D0, "83EC0C538B5C2414568BF1", (void*)&detail::hkTrack,
         (void**)&s.g_track},
        {"compass draw (locktrace10)", 0x00C13C50, "558BEC83E4F081ECB4000000", (void*)&detail::hkNavUpdate,
         (void**)&s.g_navUpdate},
        {"mission progress entries (locktrace10)", 0x0157CD10, "558BEC83E4F86AFF68????????64A100000000",
         (void*)&detail::hkMisProgress, (void**)&s.g_misProgress},
    });
}

}  // namespace trace::locktrace
