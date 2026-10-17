// The Wwise API seam mirrored onto the FMOD host, its delivery tick, and the audio feature's install. host, core.
#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../base/hash.hpp"
#include "../../base/text.hpp"
#include "../../core/hooks.hpp"
#include "dialogue.hpp"
#include "fmod.hpp"
#include "host.hpp"
#include "log.hpp"
#include "music.hpp"
#include "positions.hpp"
#include "system.hpp"

namespace engine::audio::wwise {

struct Config {
    bool music = true;    // music10: the MusicStack bridge
    bool vo = true;       // vo10: the dialogue bridge
    bool trace = false;   // audiotrace10: log each posted name once with the backend that took it
    LogFn log = nullptr;
    system::Config systemCfg;
    host::Config hostCfg;
    music::Config musicCfg;
    dialogue::Config voCfg;
    core::InstallFeatureFn installFeature = nullptr;   // core::installFeature; octtest passes a stand-in
};

// The log and every module's config, no hook and no thread (octtest drives the host itself).
inline void configure(const Config& c);
// configure(), the host thread, then audio10's hooks and the features built on them. False: no host thread.
inline bool install(const Config& c);
// One host tick, then the callbacks of what it finished, outside every lock; -> how many ended.
inline int tick();

// FUN_01947150's and FUN_01947a50's detours, for octtest.
inline int __cdecl hkListener(const float* p, uint32_t index);
inline int __cdecl hkSetPos(uint32_t obj, const float* p);
inline int __cdecl hkSrcPos(uint32_t pid, int* pos, uint8_t extrapolate);

typedef int(__cdecl* SetPosFn)(uint32_t obj, const float* p);
typedef int(__cdecl* ListenerFn)(const float* p, uint32_t index);
// octtest replaces the originals: no Wwise behind them there.
inline void setOriginals(ListenerFn listener, SetPosFn setPos);

namespace detail {

// AkCallbackType bits and the callback types Wwise delivers (3.0 LAB_01830120 takes both)
constexpr uint32_t kAkEndOfEvent = 1;
constexpr uint32_t kAkDuration = 8;   // 3.0 FUN_01830370 asks it for dialog lines
constexpr uint8_t kStreaming = 1;     // AkDurationCallbackInfo.bStreaming of our lines
constexpr int kMaxLoadChain = 8;      // ponytail: 1.0 has no guard; no 1.0 'Load' chain is deeper than 3
constexpr int kEndsPerName = 4;       // audiotrace10: ends and placements logged per name
constexpr int kTracedLines = 300;     // audiotrace10: dialog line ends logged
constexpr int kCategoryDepth = 8;     // audiotrace10: categories listed per event

// AkDurationCallbackInfo and AkEventCallbackInfo as 3.0 LAB_01830120 reads them.
struct AkDurationInfo {
    void* cookie;
    uint32_t obj, pid, ev;
    float dur, est;
    uint32_t node, media;
    uint8_t streaming;
};
struct AkEventInfo {
    void* cookie;
    uint32_t obj, pid, ev;
};

typedef uint32_t(__fastcall* BankReqFn)(void* mgr, void* edx, uint32_t* req);
typedef uint32_t(__cdecl* PostFn)(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, void* ext,
                                  uint32_t pid);
typedef int(__cdecl* StopFn)(uint32_t pid, uint32_t ms, uint32_t curve);
typedef int(__cdecl* UnregFn)(uint32_t obj);
typedef int(__cdecl* ActionFn)(const char* name, int action, uint32_t obj, uint32_t ms, uint32_t curve, uint32_t pid);
typedef int(__cdecl* SrcPosFn)(uint32_t pid, int* pos, uint8_t extrapolate);
typedef int(__fastcall* QueuedPostFn)(void* mgr, void* edx, uint32_t* req);
typedef void(__stdcall* FaderFn)(const char* cat, const char* src, float v, uint32_t a4);
typedef void(__cdecl* AkCallback)(uint32_t type, void* info);

struct State {
    bool music = false;   // set once by configure()
    bool vo = false;      // set once by configure()
    uint32_t idPauseAll = 0, idPauseExUi = 0, idResumeAll = 0;   // set once by install()
    // originals: each set once by hook() in install(); octtest sets oListener and oSetPos
    BankReqFn oBankReq = nullptr;     // set once by hook()
    PostFn oPost = nullptr;           // set once by hook()
    SetPosFn oSetPos = nullptr;       // set once by hook()
    ListenerFn oListener = nullptr;   // set once by hook()
    StopFn oStop = nullptr;           // set once by hook()
    SrcPosFn oSrcPos = nullptr;       // set once by hook()
    UnregFn oUnreg = nullptr;         // set once by hook()
    ActionFn oAction = nullptr;       // set once by hook()
    FaderFn oFader = nullptr;         // set once by hook()
    QueuedPostFn oQueuedPost = nullptr;   // set once by hook()
    std::unordered_map<uint32_t, int> endsSeen;      // tick thread only
    int linesEnded = 0;                              // tick thread only
    std::unordered_map<uint32_t, int> placedSeen;    // system's lock
};
inline State g_state;

// A bank request's file as the dispatcher names it: stem, lower-case extension, and a group after ':'.
struct BankFile {
    std::string stem, ext, group;
};

// 'x.fev:Group' is 1.0's request form (a 'Load' item may carry one); the group is lower-cased with ext.
inline void splitGroup(BankFile& bf)
{
    const size_t colon = bf.ext.find(':');
    if (colon == std::string::npos) return;
    bf.group = bf.ext.substr(colon + 1);
    bf.ext.resize(colon);
}

// '<X>_Music.bnk' (3.0 FUN_0073eb30's name per playset) whose music is ours: our project of X instead.
inline uint32_t musicRequest(const BankFile& bf, const char* file, bool load, uint32_t r)
{
    constexpr size_t kSuffix = 6;   // "_music"
    const std::string x = bf.stem.substr(0, bf.stem.size() - kSuffix);
    if (!music::hosted(x)) return r;
    if (load) {
        const bool loaded = music::ensure(x);
        if (loaded) r = 1;
    } else if (music::isPlaysetUnlocked(x))
        music::drop();
    log("audio10: %s %s -> Music_%s.fev", load ? "load" : "unload", file, x.c_str());
    return r;
}

inline uint32_t bankRequest(void* mgr, uint32_t* req, int depth);

// '<stem>.fev' / '<stem>.bnk': our project of that stem, then its dependencies through the dispatcher.
inline uint32_t projectRequest(void* mgr, uint32_t* req, const BankFile& bf, int depth, uint32_t r)
{
    const std::string fev = system::installedName(bf.stem + ".fev");
    if (fev.empty()) return r;
    std::vector<std::string> deps;
    const char* gp = bf.group.empty() ? nullptr : bf.group.c_str();
    if (req[0] == 0) {
        void* p = host::load(fev, gp, &deps);
        if (p && bf.ext == ".fev") r = 1;
    } else
        host::unload(fev, gp, &deps);
    if (depth > kMaxLoadChain) {
        log("audio10: 'Load' chain too deep at %s", fev.c_str());
        return r;
    }
    for (auto& d : deps) {   // 1.0 FUN_01134440: each item with the request's type ('%s -> %s'); AU-28
        const std::string desc = fev + " -> " + d;
        uint32_t dreq[5] = {req[0], req[1], (uint32_t)(uintptr_t)d.c_str(), req[3], (uint32_t)(uintptr_t)desc.c_str()};
        bankRequest(mgr, dreq, depth + 1);
    }
    return r;
}

// One request {0 load / 1 unload, -, file, name, file}: the engine's, then ours (depth: our dependency chain).
inline uint32_t bankRequest(void* mgr, uint32_t* req, int depth)
{
    State& s = g_state;
    uint32_t r = s.oBankReq(mgr, nullptr, req);
    const char* file = req ? (const char*)req[2] : nullptr;
    if (!file || !*file) return r;
    if (const bool up = system::ready(); !up) return r;
    const std::string f = file;
    const size_t dot = f.rfind('.');
    if (dot == std::string::npos) return r;
    BankFile bf{f.substr(0, dot), text::lower(f.substr(dot)), std::string()};
    const bool load = req[0] == 0;
    if (s.vo && bf.ext == ".fsb") {   // 1.0's '.fsb' handler (FUN_01137bb0; 3.0 registers none): a dialog bank
        if (!load) {
            dialogue::unload(f);
            return r;
        }
        const bool loaded = dialogue::load(f);
        return loaded ? 1 : r;
    }
    splitGroup(bf);
    if (bf.ext != ".bnk" && bf.ext != ".fev") return r;
    const bool musicBank = bf.ext == ".bnk" && text::endsWithPastStem(text::lower(bf.stem), "_music");
    if (s.music && musicBank) return musicRequest(bf, file, load, r);
    return projectRequest(mgr, req, bf, depth, r);
}

// FUN_0182db30, thiscall on the sound manager: a bank request (FUN_0182dbb0). Bank dispatcher thread, per bank.
inline uint32_t __fastcall hkBankReq(void* mgr, void*, uint32_t* req) { return bankRequest(mgr, req, 0); }

// 3.0 FUN_0073f900's Pause_All / Pause_All_Except_UI / Resume_All: our plays and the music system follow.
inline void onPausePost(uint32_t ev)
{
    State& s = g_state;
    const host::PauseScope scope = ev == s.idPauseExUi ? host::PauseScope::ExceptUi : host::PauseScope::All;
    const host::PauseCount n = host::pause(ev != s.idResumeAll, scope);
    if (tracing())
        log("audio10: %s: %d 1.0 events %s, %d of them dialog lines", ev == s.idPauseAll ? "Pause_All" :
            ev == s.idPauseExUi ? "Pause_All_Except_UI" : "Resume_All", n.events, ev == s.idResumeAll ? "resumed" :
            "paused", n.lines);
    if (ev != s.idPauseExUi) music::setPaused(ev == s.idPauseAll);
}

// FUN_01948a10, cdecl PostEvent -> playing id, 0 = unknown: then ours by that name. Game threads, every post.
inline uint32_t __cdecl hkPost(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, void* ext,
                               uint32_t pid)
{
    State& s = g_state;
    const uint32_t r = s.oPost(ev, obj, flags, cb, cookie, ext, pid);
    if (const bool up = system::ready(); !up) return r;
    const bool pausePost = ev == s.idPauseAll || ev == s.idPauseExUi || ev == s.idResumeAll;
    if (pausePost) onPausePost(ev);
    if (r || ext) return r;   // a dialog line with external sources is the game's own (vo10 takes its own)
    const host::Post p{ev, obj, flags, cb, cookie};
    std::unique_lock<std::recursive_mutex> l = system::lock();   // the playset changes on the bank dispatcher's thread
    if (s.music) {
        if (std::optional<uint32_t> id = music::onPost(p); id) return *id;
    }
    return host::play(p);
}

}  // namespace detail

// FUN_01947a50, cdecl SetPosition(gameObj, AkSoundPosition). Game threads, per positioned object.
inline int __cdecl hkSetPos(uint32_t obj, const float* p)
{
    if (p) positions::set(obj, p);
    return detail::g_state.oSetPos(obj, p);
}

// FUN_01947150, cdecl SetListenerPosition(AkListenerPosition, index). Game threads, likely per frame.
inline int __cdecl hkListener(const float* p, uint32_t index)
{
    if (p && index == 0) positions::setListener(p);
    return detail::g_state.oListener(p, index);
}

// FUN_019475a0, cdecl GetSourcePlayPosition, the lipsync clock (FUN_014cb180). Game threads, per frame in dialog.
inline int __cdecl hkSrcPos(uint32_t pid, int* pos, uint8_t extrapolate)
{
    constexpr int kAkSuccess = 1;
    const std::optional<int> ms = pos ? host::position(pid) : std::nullopt;
    if (ms) {
        *pos = *ms;
        return kAkSuccess;
    }
    return detail::g_state.oSrcPos(pid, pos, extrapolate);
}

namespace detail {

// FUN_019478d0, cdecl StopPlayingID(id, fade ms, curve). Game threads, per stop.
inline int __cdecl hkStop(uint32_t pid, uint32_t ms, uint32_t curve)
{
    host::act(pid, host::kActionStop);
    return g_state.oStop(pid, ms, curve);
}

// FUN_01947640, cdecl UnregisterGameObj: Wwise stops what plays on it. Game threads, per unregister.
inline int __cdecl hkUnreg(uint32_t obj)
{
    host::stopObject(obj);
    return g_state.oUnreg(obj);
}

// FUN_01948c20, cdecl ExecuteActionOnEvent; 3.0 FUN_0182c3b0 pauses and resumes. Game threads, per action.
inline int __cdecl hkAction(const char* name, int action, uint32_t obj, uint32_t ms, uint32_t curve, uint32_t pid)
{
    const bool known = action >= host::kActionStop && action <= host::kActionResume;
    if (known && pid) host::act(pid, action);
    if (known && !pid && name) {
        for (uint32_t id : host::idsOf(hash::fnv1Lower(name), obj)) host::act(id, action);
    }
    return g_state.oAction(name, action, obj, ms, curve, pid);
}

// FUN_0182c460, stdcall (ret 0x10): SetRTPCValue('<src>_Volume_Fader_<cat>', v). Game threads, per fader set.
inline void __stdcall hkFader(const char* cat, const char* src, float v, uint32_t a4)
{
    if (cat && src) {
        if (const bool up = system::ready(); up) host::setFader(cat, src, v);
    }
    g_state.oFader(cat, src, v, a4);
}

// The categories an event plays in, with their volumes, from its own up.
inline std::string categoryChain(void* ev)
{
    const fmod::Api& f = system::api();
    std::string chain;
    void* c = nullptr;
    f.Event_GetCategory(ev, &c);
    fmod::walkCategories(f, c, kCategoryDepth, [&](void* cat) {
        char* cn = nullptr;
        float cv = -1;
        f.EventCategory_GetInfo(cat, nullptr, &cn);
        f.EventCategory_GetVolume(cat, &cv);
        char one[80];
        snprintf(one, sizeof one, "%s%s %.3f", chain.empty() ? "" : " < ", cn ? cn : "?", cv);
        chain += one;
    });
    return chain;
}

// audiotrace10: where one of our sounds plays against the listener, and the mix it plays in (posMx held).
inline void tracePlacement(const char* name, const host::Play& pl, const float* at, const positions::Listener& ls)
{
    const float* lpos = ls.pos;
    const float* v = at ? at : lpos;
    const float dx = v[0] - lpos[0], dy = v[1] - lpos[1], dz = v[2] - lpos[2];
    log("audio10: %s plays on obj %u at (%.1f %.1f %.1f)%s, %.1f from the listener (%.1f %.1f %.1f)", name, pl.obj,
        v[0], v[1], v[2], at ? "" : " (no position: the listener's)", sqrtf(dx * dx + dy * dy + dz * dz), lpos[0],
        lpos[1], lpos[2]);
    // FMOD mode (0x10 3D, 0x8 2D, 0x40000 head-relative); fwd and up must be unit and perpendicular
    unsigned mode = 0;
    std::string chain;
    if (void* ev = pl.ev; ev) {
        system::api().Event_GetPropertyByIndex(ev, fmod::kPropMode, &mode, 0);
        chain = categoryChain(ev);
    }
    const float *lf = ls.fwd, *lu = ls.up;
    log("audio10: %s mode %x, categories %s; listener result %d, fwd (%.3f %.3f %.3f) up (%.3f %.3f %.3f) "
        "|fwd| %.3f |up| %.3f fwd.up %.3f", name, mode, chain.c_str(), (int)host::listenerResult(), lf[0], lf[1],
        lf[2], lu[0], lu[1], lu[2], sqrtf(lf[0] * lf[0] + lf[1] * lf[1] + lf[2] * lf[2]),
        sqrtf(lu[0] * lu[0] + lu[1] * lu[1] + lu[2] * lu[2]), lf[0] * lu[0] + lf[1] * lu[1] + lf[2] * lu[2]);
}

// FUN_01830270, thiscall on the sound manager: a queued post {name, gameObj, 'bank:event'}. Game threads.
inline int __fastcall hkQueuedPost(void* mgr, void*, uint32_t* req)
{
    State& s = g_state;
    const int r = s.oQueuedPost(mgr, nullptr, req);
    const char* name = req ? (const char*)req[0] : nullptr;
    if (!name) return r;
    const uint32_t id = hash::fnv1Lower(name);
    bool ours;
    {
        std::unique_lock<std::recursive_mutex> l = system::lock();
        ours = host::playOf((uint32_t)r) != nullptr;
    }
    traceBackend(id, name, !r ? "nothing (no Wwise or 1.0 event)" : ours ? "1.0 FMOD" : "Wwise");
    if (!ours) return r;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    const host::Play* pl = host::playOf((uint32_t)r);
    if (!pl) return r;
    const int seen = s.placedSeen[id]++;
    if (seen >= kEndsPerName) return r;
    positions::inspect(pl->obj,
                       [&](const float* at, const positions::Listener& ls) { tracePlacement(name, *pl, at, ls); });
    return r;
}

// audiotrace10: how long each sound played and what ended it (dialog lines up to a cap, others per name).
inline void traceEnds(const std::vector<std::pair<uint32_t, host::Play>>& fin)
{
    State& s = g_state;
    for (auto& d : fin) {
        const host::Play& p = d.second;
        const char* why = p.stopReq ? "stopped by 3.0" : p.ev ? "finished or stolen" : "retired";
        if (p.fsb) {
            const int line = s.linesEnded++;
            if (line >= kTracedLines) continue;
            const std::string n = dialogue::lineName(p.fsb, p.sub);
            log("vo10: line %s (id %u) ended after %lu ms (%s)", n.c_str(), d.first, GetTickCount() - p.t0,
                p.stopReq ? why : p.endBy ? p.endBy : why);
            continue;
        }
        const int seen = s.endsSeen[p.eventId]++;
        if (seen >= kEndsPerName) continue;
        const std::string n = host::eventName(p.eventId).value_or(std::string());
        log("audio10: %s ended after %lu ms (%s)", n.c_str(), GetTickCount() - p.t0, why);
    }
}

}  // namespace detail

inline int tick()
{
    const host::TickResult t = host::tick();
    for (auto& d : t.lengths) {
        if (!(d.p.flags & detail::kAkDuration) || !d.p.cb) continue;
        detail::AkDurationInfo info = {d.p.cookie, d.p.obj, d.id, d.p.eventId, d.ms, d.ms, 0, 0, detail::kStreaming};
        ((detail::AkCallback)d.p.cb)(detail::kAkDuration, &info);
    }
    if (tracing()) detail::traceEnds(t.ended);
    for (auto& d : t.ended) {
        if (!(d.second.flags & detail::kAkEndOfEvent) || !d.second.cb) continue;
        detail::AkEventInfo info = {d.second.cookie, d.second.obj, d.first, d.second.eventId};
        ((detail::AkCallback)d.second.cb)(detail::kAkEndOfEvent, &info);
    }
    dialogue::seqTick();
    return (int)t.ended.size();
}

inline void setOriginals(ListenerFn listener, SetPosFn setPos)
{
    detail::g_state.oListener = listener;
    detail::g_state.oSetPos = setPos;
}

inline void configure(const Config& c)
{
    detail::State& s = detail::g_state;
    setLog(c.log, c.trace);
    s.music = c.music;
    s.vo = c.vo;
    system::configure(c.systemCfg);
    host::configure(c.hostCfg);
    music::configure(c.musicCfg);
    dialogue::configure(c.voCfg);
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    const core::InstallFeatureFn installFeature = c.installFeature;
    configure(c);
    if (const bool started = system::install(c.systemCfg); !started) return false;
    s.idPauseAll = hash::fnv1Lower("Pause_All");
    s.idPauseExUi = hash::fnv1Lower("Pause_All_Except_UI");
    s.idResumeAll = hash::fnv1Lower("Resume_All");
    // music10, vo10 and audiotrace10 build on these hooks, so they install only with them
    const bool base = installFeature("audio10", {
            {"sound bank request (audio10)", 0x0182DB30, "568B711C5785F6", (void*)&detail::hkBankReq,
             (void**)&s.oBankReq},
            {"Wwise PostEvent (audio10)", 0x01948A10, "558BEC8B45088B0D????????", (void*)&detail::hkPost,
             (void**)&s.oPost},
            {"Wwise SetPosition (audio10)", 0x01947A50, "558BEC8B450885C07507", (void*)&hkSetPos, (void**)&s.oSetPos},
            {"Wwise SetListenerPosition (audio10)", 0x01947150, "558BEC535657E8", (void*)&hkListener,
             (void**)&s.oListener},
            {"Wwise StopPlayingID (audio10)", 0x019478D0, "558BEC568B750885F6", (void*)&detail::hkStop,
             (void**)&s.oStop},
            {"Wwise GetSourcePlayPosition (audio10)", 0x019475A0, "558BEC8B450C85C07507", (void*)&hkSrcPos,
             (void**)&s.oSrcPos},
            {"Wwise UnregisterGameObj (audio10)", 0x01947640, "558BEC568B750885F67506", (void*)&detail::hkUnreg,
             (void**)&s.oUnreg},
            {"Wwise ExecuteActionOnEvent (audio10)", 0x01948C20, "558BEC8B450850E8", (void*)&detail::hkAction,
             (void**)&s.oAction},
            {"volume fader (audio10)", 0x0182C460, "8B4424048B4C240883EC30", (void*)&detail::hkFader,
             (void**)&s.oFader},
        });
    if (!base) return true;
    if (c.music) music::install(c.musicCfg);
    if (c.vo) dialogue::install(c.voCfg);
    if (c.trace)
        installFeature("audiotrace10", {
            {"queued sound post (audiotrace10)", 0x01830270, "83EC44538B5C244C", (void*)&detail::hkQueuedPost,
             (void**)&s.oQueuedPost},
        });
    return true;
}

}  // namespace engine::audio::wwise
