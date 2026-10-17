// The FMOD host: loaded projects, registered events, what plays, volumes, the tick. system, positions, finished.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../../base/hash.hpp"
#include "../../base/text.hpp"
#include "finished.hpp"
#include "fmod.hpp"
#include "log.hpp"
#include "positions.hpp"
#include "system.hpp"

namespace engine::audio::host {

// One registered event: system id and flags (as 1.0 FUN_01138430's entry keeps them), and its project.
struct Entry {
    void* proj;
    unsigned sysId;
    uint8_t flags;   // kVirtual | kLooping | kRelease
    bool ui;         // a ui category is in its chain (Pause_All_Except_UI keeps it running)
    std::string name;
};
constexpr uint8_t kVirtual = 1;   // a virtual category is in its chain
constexpr uint8_t kLooping = 2;   // ONESHOT property 0
constexpr uint8_t kRelease = 4;   // its group has the rule's release property

struct Project {
    void* p = nullptr;
    int refs = 0;
    int events = 0;
    std::map<std::string, std::vector<std::string>> deps;   // lower-case group -> its dependency list
};

// One FMOD event standing in for a Wwise playing id.
struct Play {
    void* ev;     // nullptr: finished, or a stand-in with no FMOD event (StartMusic_X)
    void* proj;
    uint32_t obj, eventId, flags;
    void* cb;     // the game's AkCallbackFunc (cdecl type, info*) and its cookie
    void* cookie;
    bool ui;
    uint32_t born;   // tick at start: its end goes out a tick later at the earliest (3.0 FUN_01830270)
    void* fsb;       // a dialog line: the bank whose subsound it plays
    DWORD t0 = GetTickCount();     // audiotrace10: started at
    bool stopReq = false;          // audiotrace10: the game asked for its stop
    int sub = -1;                  // audiotrace10: a dialog line's subsound
    const char* endBy = nullptr;   // audiotrace10: what else ended it
};

// A Wwise post: event id, game object, AkCallbackType flags, callback and cookie.
struct Post {
    uint32_t eventId, obj, flags;
    void* cb;
    void* cookie;
};

// Which events a project load registers, by each event's category chain (the game's rule).
struct RegisterRule {
    std::vector<std::string> top;    // the category under the root is one of these,
    std::string ui;                  // or this one with one of them under it
    std::string virtualCategory;     // in the chain: the event must loop and have
    int virtualBehavior = 0;         // this MAX_PLAYBACKS_BEHAVIOR
    std::string releaseProperty;     // a group property that sets kRelease
};

// A game fader category and the FMOD categories it scales.
struct FaderTarget {
    std::string fader;
    std::vector<std::string> categories;
};

struct Config {
    RegisterRule rule;
    std::string depsProperty;   // the group property naming what a project load also requests
    std::function<void(const char* value, std::vector<std::string>& out)> splitDeps;
    std::vector<FaderTarget> faders;
    uintptr_t (*rt)(uint32_t) = nullptr;   // core::rt; null: no engine (octtest)
    std::map<std::string, std::string>* dropped = nullptr;   // octtest: events the rule leaves out, why
};

// Which plays a pause or resume reaches.
enum class PauseScope { All, ExceptUi };

struct PauseCount {
    int events, lines;
};

// A dialog line started: its id, its play and its length (for AK_Duration).
struct LineLength {
    uint32_t id;
    Play p;
    float ms;
};

// One tick's output: plays that ended (removed), and lines started since the last tick.
struct TickResult {
    std::vector<std::pair<uint32_t, Play>> ended;
    std::vector<LineLength> lengths;
};

// AkActionOnEventType
constexpr int kActionStop = 0;
constexpr int kActionPause = 1;
constexpr int kActionResume = 2;
constexpr uint32_t kAnyObject = 0xffffffffu;   // ExecuteActionOnEvent's game object for all of them

// Keeps c and binds the engine's playing-id counter; no FMOD call.
inline void configure(const Config& c);
inline void* load(const std::string& file, const char* group = nullptr, std::vector<std::string>* deps = nullptr);
inline void loadTree(const std::string& file, const char* group, int depth = 0);
inline void unload(const std::string& file, const char* group = nullptr, std::vector<std::string>* deps = nullptr);
inline const std::vector<std::string>& depsOf(Project& pr, const std::string& gname);
inline uint32_t play(const Post& p);
inline uint32_t playLine(const Post& p, void* fsb, int sub);
inline uint32_t standIn(const Post& p);
inline bool act(uint32_t id, int action);
inline std::optional<int> position(uint32_t id);
inline void stopObject(uint32_t obj);
inline PauseCount pause(bool on, PauseScope scope);
inline TickResult tick();
inline bool knowsEvent(uint32_t eventId);
inline std::optional<std::string> eventName(uint32_t eventId);
inline std::vector<uint32_t> idsOf(uint32_t eventId, uint32_t obj);
inline void setMixer(const std::map<std::string, float>& mixer);
inline void setFader(const char* cat, const char* src, float v);
// system::lock() held: the categories' volumes from the faders and the mixer.
inline void applyVolumes();
// system::lock() held: the play of an id, or null.
inline Play* playOf(uint32_t id);
// system::lock() held: stops every play of a dialog bank's sound.
inline void stopBank(void* snd);
// system::lock() held: audiotrace10's view.
inline fmod::Result listenerResult();
inline bool paused();
// 1.0 SafeAudioSync (FUN_00ecabc0): an audio-synced choreography also starts the event named after it.
inline void choreographyStarted(const char* path, uint32_t flags);

namespace detail {

constexpr size_t kProgSlots = 4096;        // playing id & 4095 -> programmer sound
constexpr LONG kOwnIdStart = 0x40000000;   // octtest's ids, clear of the engine's
constexpr int kMaxTreeDepth = 8;
constexpr size_t kPropertyBuffer = 16;     // room for any FMOD event property value

namespace eng {
constexpr uint32_t kPlayingIdCounter = 0x022AFBE8;   // Wwise's playing-id counter (FUN_01948a10 increments it)
}  // namespace eng

// Every member but cfg is guarded by system's lock.
struct State {
    Config cfg;                                 // set once by configure(), before the host thread starts
    std::map<std::string, Project> projects;    // lower-case project file
    std::unordered_map<uint32_t, Entry> byId;   // fnv1Lower(event name), the id the game posts
    std::unordered_map<uint32_t, Play> plays;   // Wwise-space playing id
    fmod::Result lres = 0;                      // the last listener update's result
    bool pausedNow = false;                     // the last pause or resume posted
    uint32_t tickNo = 0;
    std::map<std::string, std::map<std::string, float>> faderValues;   // category -> source -> value
    std::map<std::string, float> mixer;         // the game's 'Mixer' source per category
    // written under the lock; onEvent reads it unlocked (aligned pointers; AU-12, AU-26)
    void* progSound[kProgSlots] = {};
    LONG* idCounter = nullptr;      // set once by configure(); InterlockedIncrement only
    LONG ownCounter = kOwnIdStart;  // InterlockedIncrement only
};
inline State g_state;

inline bool isOneOf(const char* s, const std::vector<std::string>& list)
{
    for (const std::string& x : list)
        if (x == s) return true;
    return false;
}

// fn(subgroup) for each group directly under g.
template <class Fn>
inline void forEachSubgroup(void* g, Fn fn)
{
    const fmod::Api& f = system::api();
    int n = 0;
    f.EventGroup_GetNumGroups(g, &n);
    for (int i = 0; i < n; ++i) {
        void* sub = nullptr;
        if (const fmod::Result r = f.EventGroup_GetGroupByIndex(g, i, 0, &sub); !r && sub) fn(sub);
    }
}

// FMOD's callback, on whatever thread FMOD runs it: never takes system's lock.
inline fmod::Result __stdcall onEvent(void*, int type, void*, void* p2, void* ud)
{
    const uint32_t id = (uint32_t)(uintptr_t)ud;
    // 1.0's callback 0x01136440 ends the sound on both
    if (type == fmod::kCallbackStolen || type == fmod::kCallbackFinished) {
        finished::push(id);
        return 0;
    }
    if (type != fmod::kCallbackSoundDefCreate || !p2) return 0;
    // 1.0 0x011364cb hands back the line's subsound
    void* s = g_state.progSound[id & (kProgSlots - 1)];
    if (s) *(void**)p2 = s;
    return 0;
}

inline uint32_t newId()
{
    State& s = g_state;
    return (uint32_t)InterlockedIncrement(s.idCounter ? s.idCounter : &s.ownCounter);
}

inline void place(void* ev, uint32_t obj)
{
    const positions::V3 p = positions::at(obj);
    system::api().Event_Set3DAttributes(ev, p.v, nullptr, nullptr);
}

// A virtual event's info-only instance at volume 1 and the new sound's position (MAX_PLAYBACKS_BEHAVIOR 5).
inline void placeInfoOnly(unsigned sysId, uint32_t obj)
{
    const fmod::Api& f = system::api();
    void* info = nullptr;
    const fmod::Result r = f.EventSystem_GetEventBySystemID(system::eventSystem(), sysId, fmod::kEventInfoOnly, &info);
    if (r || !info) return;
    f.Event_SetVolume(info, 1.0f);
    place(info, obj);
}

// Starts ev on obj with our callback and programmer sound snd; id is new; false: it did not start.
inline bool start(void* ev, uint32_t obj, void* snd, uint32_t& id)
{
    const fmod::Api& f = system::api();
    id = newId();
    g_state.progSound[id & (kProgSlots - 1)] = snd;   // a play without one clears a stale subsound
    f.Event_SetCallback(ev, &onEvent, (void*)(uintptr_t)id);
    place(ev, obj);
    if (const fmod::Result failed = f.Event_Start(ev); !failed) return true;
    f.Event_SetCallback(ev, nullptr, nullptr);
    return false;
}

// The category walk of one event against the rule: the reason it is dropped, or null.
inline const char* categoryVerdict(void* ev, int oneshot, bool& virt, bool& ui)
{
    const fmod::Api& f = system::api();
    const RegisterRule& rule = g_state.cfg.rule;
    void* cat = nullptr;
    if (const fmod::Result r = f.Event_GetCategory(ev, &cat); r || !cat) return nullptr;
    int depth = 0, known = 0, uiDepth = 0;
    fmod::walkCategories(f, cat, fmod::kUncapped, [&](void* c) {
        ++depth;
        char* cn = nullptr;
        if (const fmod::Result r = f.EventCategory_GetInfo(c, nullptr, &cn); r || !cn) return;
        if (isOneOf(cn, rule.top)) known = depth;
        else if (rule.ui == cn) uiDepth = depth;
        else if (rule.virtualCategory == cn) virt = true;
    });
    const int top = depth - 1;   // the category directly under the root
    const bool direct = known == top;
    const bool underUi = known == top - 1 && uiDepth == top;
    if (!direct && !underUi) return "category";
    if (virt) {
        int behavior = 0;
        f.Event_GetPropertyByIndex(ev, fmod::kPropMaxPlaybacksBehavior, &behavior, 0);
        if (oneshot || behavior != rule.virtualBehavior) return "virtual";
    }
    ui = uiDepth > 0;
    return nullptr;
}

// Registers one event of a group if the rule takes it (1.0 0x011384a0); same name: the last load wins.
inline bool registerEvent(void* proj, void* ev, bool release)
{
    State& s = g_state;
    const fmod::Api& f = system::api();
    char* name = nullptr;
    uint8_t info[fmod::kEventInfoSize] = {};
    if (const fmod::Result r = f.Event_GetInfo(ev, nullptr, &name, info); r || !name) return false;
    int oneshot = 0;
    f.Event_GetPropertyByIndex(ev, fmod::kPropOneshot, &oneshot, 0);
    bool virt = false, ui = false;
    if (const char* why = categoryVerdict(ev, oneshot, virt, ui); why) {
        if (s.cfg.dropped) (*s.cfg.dropped)[name] = why;
        return false;
    }
    Entry& e = s.byId[hash::fnv1Lower(name)];
    e.proj = proj;
    memcpy(&e.sysId, info + fmod::kInfoSystemId, sizeof e.sysId);
    e.flags = (uint8_t)((virt ? kVirtual : 0) | (oneshot == 0 ? kLooping : 0) | (release ? kRelease : 0));
    e.ui = ui;
    e.name = name;
    return true;
}

// The events of a group and of every group under it that the rule registers -> how many.
inline int registerGroup(void* proj, void* g)
{
    const fmod::Api& f = system::api();
    int count = 0;
    uint8_t prop[kPropertyBuffer] = {};
    const bool release = f.EventGroup_GetProperty(g, g_state.cfg.rule.releaseProperty.c_str(), prop) == 0;
    int n = 0;
    f.EventGroup_GetNumEvents(g, &n);
    for (int i = 0; i < n; ++i) {
        void* ev = nullptr;
        if (const fmod::Result r = f.EventGroup_GetEventByIndex(g, i, fmod::kEventInfoOnly, &ev); r || !ev) continue;
        count += registerEvent(proj, ev, release);
    }
    forEachSubgroup(g, [&](void* sub) { count += registerGroup(proj, sub); });
    return count;
}

// The dependency property of a group and of every group under it (1.0 FUN_011348e0).
inline void collectDeps(void* g, std::vector<std::string>& out)
{
    const State& s = g_state;
    const char* v = nullptr;
    if (const fmod::Result r = system::api().EventGroup_GetProperty(g, s.cfg.depsProperty.c_str(), &v); !r && v)
        s.cfg.splitDeps(v, out);
    forEachSubgroup(g, [&](void* sub) { collectDeps(sub, out); });
}

// A fresh project: its request's group's sample data, then its events if that group exists or it has none.
inline bool registerProject(Project& pr, const std::string& gname)
{
    const fmod::Api& f = system::api();
    int ng = 0;
    f.EventProject_GetNumGroups(pr.p, &ng);
    void* g = nullptr;
    if (ng != 0) {
        const fmod::Result r = f.EventProject_GetGroup(pr.p, gname.c_str(), fmod::kCacheEvents, &g);
        if (r || !g) return false;
        char* names[1] = {(char*)gname.c_str()};
        f.EventProject_LoadSampleData(pr.p, nullptr, 0, names, 1, fmod::kEventDefault);
    }
    for (int i = 0; i < ng; ++i) {
        void* top = nullptr;
        if (const fmod::Result r = f.EventProject_GetGroupByIndex(pr.p, i, 0, &top); !r && top)
            pr.events += registerGroup(pr.p, top);
    }
    return true;
}

inline std::string stemOf(const std::string& file) { return file.substr(0, file.rfind('.')); }

}  // namespace detail

inline void configure(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    if (c.rt) s.idCounter = (LONG*)c.rt(detail::eng::kPlayingIdCounter);
}

// The request's group's dependency list, read once per group (an item may name another group of the file).
inline const std::vector<std::string>& depsOf(Project& pr, const std::string& gname)
{
    auto it = pr.deps.find(text::lower(gname));
    if (it != pr.deps.end()) return it->second;
    std::vector<std::string>& v = pr.deps[text::lower(gname)];
    void* g = nullptr;
    if (const fmod::Result r = system::api().EventProject_GetGroup(pr.p, gname.c_str(), 0, &g); !r && g)
        detail::collectDeps(g, v);
    return v;
}

// 1.0's '.fev' handler 0x01138830 for 'X.fev[:Group]' (group: the stem when none is named); refcounted per file.
inline void* load(const std::string& file, const char* group, std::vector<std::string>* deps)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    Project& pr = s.projects[text::lower(file)];
    const std::string gname = group ? group : detail::stemOf(file);
    if (pr.p) {
        ++pr.refs;
        if (deps) *deps = depsOf(pr, gname);
        return pr.p;
    }
    const fmod::Result r = system::api().EventSystem_Load(system::eventSystem(), file.c_str(), nullptr, &pr.p);
    if (r || !pr.p) {
        log("audio10: load %s failed (%d)", file.c_str(), r);
        s.projects.erase(text::lower(file));
        return nullptr;
    }
    pr.refs = 1;
    const bool found = detail::registerProject(pr, gname);
    const std::vector<std::string>& dv = depsOf(pr, gname);
    if (deps) *deps = dv;
    applyVolumes();   // the categories a project defines exist once it is loaded
    std::string dl;
    for (auto& d : dv) dl += (dl.empty() ? "" : ", ") + d;
    log("audio10: loaded %s: %d events%s%s%s", file.c_str(), pr.events,
        found ? "" : " (no group named after it: 1.0 leaves it unregistered)", dl.empty() ? "" : "; Load: ",
        dl.c_str());
    return pr.p;
}

// A load and its project dependencies, before the game's dispatcher exists; other items are left to the game.
inline void loadTree(const std::string& file, const char* group, int depth)
{
    std::vector<std::string> deps;
    void* p = load(file, group, &deps);
    if (!p || depth > detail::kMaxTreeDepth) return;
    for (auto& d : deps) {
        const size_t c = d.find(':');
        const std::string fn = d.substr(0, c);
        const std::string name = text::endsWithPastStem(fn, ".fev") ? system::installedName(fn) : "";
        if (name.empty()) log("audio10: %s -> %s left to 3.0", file.c_str(), d.c_str());
        else loadTree(name, c == std::string::npos ? nullptr : d.c_str() + c + 1, depth + 1);
    }
}

inline void unload(const std::string& file, const char* group, std::vector<std::string>* deps)
{
    detail::State& s = detail::g_state;
    const fmod::Api& f = system::api();
    std::vector<uint32_t> ended;
    {
        std::unique_lock<std::recursive_mutex> l = system::lock();
        auto it = s.projects.find(text::lower(file));
        if (it == s.projects.end() || !it->second.p) return;
        if (deps) *deps = depsOf(it->second, group ? group : detail::stemOf(file));
        --it->second.refs;
        if (it->second.refs > 0) return;
        void* p = it->second.p;
        for (auto& pl : s.plays) {
            if (pl.second.proj != p || !pl.second.ev) continue;
            f.Event_Stop(pl.second.ev, fmod::kStopImmediate);
            pl.second.ev = nullptr;
            ended.push_back(pl.first);
        }
        for (auto e = s.byId.begin(); e != s.byId.end();) e = e->second.proj == p ? s.byId.erase(e) : std::next(e);
        f.EventProject_Release(p);
        s.projects.erase(it);
        log("audio10: unloaded %s", file.c_str());
    }
    finished::push(ended);
}

// 1.0 FUN_01136520: a virtual event's info-only instance is placed first, so FMOD's playback limit judges it there.
inline uint32_t play(const Post& p)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    auto it = s.byId.find(p.eventId);
    if (it == s.byId.end()) return 0;
    if (it->second.flags & kVirtual) detail::placeInfoOnly(it->second.sysId, p.obj);
    void* ev = nullptr;
    const fmod::Result r =
        system::api().EventSystem_GetEventBySystemID(system::eventSystem(), it->second.sysId, fmod::kEventDefault, &ev);
    if (r || !ev) return 0;
    uint32_t id = 0;
    if (const bool started = detail::start(ev, p.obj, nullptr, id); !started) return 0;
    s.plays[id] = Play{ev, it->second.proj, p.obj, p.eventId, p.flags, p.cb, p.cookie, it->second.ui, s.tickNo};
    return id;
}

// 1.0 FUN_01136850: the bank's subsound as the event's programmer sound. One stream per bank: the old line retires.
inline uint32_t playLine(const Post& p, void* fsb, int sub)
{
    detail::State& s = detail::g_state;
    const fmod::Api& f = system::api();
    std::unique_lock<std::recursive_mutex> l = system::lock();
    auto it = s.byId.find(p.eventId);
    if (it == s.byId.end() || !fsb) return 0;
    for (auto& pl : s.plays) {   // 3.0 FUN_01830370 posts the new line before it stops the old one
        if (pl.second.fsb != fsb || !pl.second.ev) continue;
        f.Event_SetCallback(pl.second.ev, nullptr, nullptr);
        f.Event_Stop(pl.second.ev, fmod::kStopImmediate);
        pl.second.ev = nullptr;
        pl.second.endBy = "retired by the next line of its bank";
        finished::push(pl.first);
    }
    void* snd = nullptr;
    if (const fmod::Result r = f.Sound_GetSubSound(fsb, sub, &snd); r || !snd) return 0;
    void* ev = nullptr;
    void* es = system::eventSystem();
    const fmod::Result r = f.EventSystem_GetEventBySystemID(es, it->second.sysId, fmod::kEventDefault, &ev);
    if (r || !ev) return 0;
    uint32_t id = 0;
    if (const bool started = detail::start(ev, p.obj, snd, id); !started) return 0;
    s.plays[id] = Play{ev, it->second.proj, p.obj, p.eventId, p.flags, p.cb, p.cookie, it->second.ui, s.tickNo, fsb};
    s.plays[id].sub = sub;
    unsigned ms = 0;
    if (const fmod::Result failed = f.Sound_GetLength(snd, &ms, fmod::kTimeUnitMs); !failed)
        finished::length(id, (float)ms);
    return id;
}

// A playing id for something with no FMOD event (StartMusic_X); it ends like any other.
inline uint32_t standIn(const Post& p)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    const uint32_t id = detail::newId();
    s.plays[id] = Play{nullptr, nullptr, p.obj, p.eventId, p.flags, p.cb, p.cookie, false, s.tickNo};
    return id;
}

// kActionStop / kActionPause / kActionResume on one of our ids; false if the id is not ours.
inline bool act(uint32_t id, int action)
{
    detail::State& s = detail::g_state;
    const fmod::Api& f = system::api();
    std::unique_lock<std::recursive_mutex> l = system::lock();
    auto it = s.plays.find(id);
    if (it == s.plays.end()) return false;
    if (action == kActionPause || action == kActionResume) {
        if (it->second.ev) f.Event_SetPaused(it->second.ev, action == kActionPause);
        return true;
    }
    it->second.stopReq = true;
    if (it->second.ev) f.Event_Stop(it->second.ev, fmod::kStopFadeOut);   // EVENTFINISHED comes through the callback
    else finished::push(id);
    return true;
}

// The played time of one of our ids (FMOD_EVENT_INFO.positionms), for Wwise's GetSourcePlayPosition.
inline std::optional<int> position(uint32_t id)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    auto it = s.plays.find(id);
    if (it == s.plays.end() || !it->second.ev) return std::nullopt;
    uint8_t info[fmod::kEventInfoSize] = {};
    if (const fmod::Result r = system::api().Event_GetInfo(it->second.ev, nullptr, nullptr, info); r)
        return std::nullopt;
    int ms;
    memcpy(&ms, info + fmod::kInfoPositionMs, sizeof ms);
    return ms;
}

inline void stopObject(uint32_t obj)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    for (auto& pl : s.plays) {
        if (pl.second.obj != obj || !pl.second.ev) continue;
        system::api().Event_Stop(pl.second.ev, fmod::kStopImmediate);
        pl.second.endBy = "its game object unregistered";
    }
    positions::erase(obj);
}

// Pauses or resumes the live plays the scope reaches -> how many, and how many were dialog lines.
inline PauseCount pause(bool on, PauseScope scope)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    s.pausedNow = on;
    PauseCount n{0, 0};
    for (auto& pl : s.plays) {
        const bool kept = scope == PauseScope::ExceptUi && pl.second.ui;
        if (!pl.second.ev || kept) continue;
        system::api().Event_SetPaused(pl.second.ev, on ? 1 : 0);
        ++n.events;
        n.lines += pl.second.fsb != nullptr;
    }
    return n;
}

// Listener and positions of live events, FMOD's update (it fires the callbacks), then what ended.
inline TickResult tick()
{
    detail::State& s = detail::g_state;
    const fmod::Api& f = system::api();
    TickResult out;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    void* es = system::eventSystem();
    ++s.tickNo;
    const positions::Listener ls = positions::listener();
    if (ls.set) s.lres = f.EventSystem_Set3DListenerAttributes(es, fmod::kListener, ls.pos, nullptr, ls.fwd, ls.up);
    for (auto& pl : s.plays)
        if (pl.second.ev) detail::place(pl.second.ev, pl.second.obj);
    f.EventSystem_Update(es);
    std::vector<uint32_t> ids, later;
    finished::Lengths ds;
    finished::take(ids, ds);
    for (auto& d : ds) {
        auto it = s.plays.find(d.first);
        if (it != s.plays.end()) out.lengths.push_back(LineLength{d.first, it->second, d.second});
    }
    for (uint32_t id : ids) {
        auto it = s.plays.find(id);
        if (it == s.plays.end()) continue;
        if (it->second.born + 1 >= s.tickNo) {
            later.push_back(id);
            continue;
        }
        out.ended.emplace_back(id, it->second);
        s.plays.erase(it);
    }
    if (!later.empty()) finished::push(later);
    return out;
}

inline bool knowsEvent(uint32_t eventId)
{
    std::unique_lock<std::recursive_mutex> l = system::lock();
    return detail::g_state.byId.count(eventId) != 0;
}

inline std::optional<std::string> eventName(uint32_t eventId)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    auto e = s.byId.find(eventId);
    if (e == s.byId.end()) return std::nullopt;
    return e->second.name;
}

// Our ids playing that event, on that object (kAnyObject: any).
inline std::vector<uint32_t> idsOf(uint32_t eventId, uint32_t obj)
{
    detail::State& s = detail::g_state;
    std::vector<uint32_t> ids;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    for (auto& pl : s.plays) {
        const bool onObj = obj == kAnyObject || pl.second.obj == obj;
        if (pl.second.eventId == eventId && onObj) ids.push_back(pl.first);
    }
    return ids;
}

inline void setMixer(const std::map<std::string, float>& mixer)
{
    std::unique_lock<std::recursive_mutex> l = system::lock();
    detail::g_state.mixer = mixer;
}

inline void setFader(const char* cat, const char* src, float v)
{
    std::unique_lock<std::recursive_mutex> l = system::lock();
    detail::g_state.faderValues[cat][src] = v;
    applyVolumes();
}

// A category's volume is the product of its fader sources and the game's mixer source.
inline void applyVolumes()
{
    detail::State& s = detail::g_state;
    std::map<std::string, float> vol = s.mixer;
    for (const FaderTarget& t : s.cfg.faders) {
        auto it = s.faderValues.find(t.fader);
        if (it == s.faderValues.end()) continue;
        float v = 1.0f;
        for (auto& src : it->second) v *= src.second;
        for (const std::string& c : t.categories) {
            auto have = vol.find(c);
            const float base = have != vol.end() ? have->second : 1.0f;
            vol[c] = base * v;
        }
    }
    const fmod::Api& f = system::api();
    for (auto& c : vol) {
        void* cat = nullptr;
        if (const fmod::Result r = f.EventSystem_GetCategory(system::eventSystem(), c.first.c_str(), &cat); !r && cat)
            f.EventCategory_SetVolume(cat, c.second);
    }
}

inline Play* playOf(uint32_t id)
{
    detail::State& s = detail::g_state;
    auto it = s.plays.find(id);
    return it != s.plays.end() ? &it->second : nullptr;
}

// Its subsounds die with the bank (EVENTFINISHED ends them for the game).
inline void stopBank(void* snd)
{
    for (auto& pl : detail::g_state.plays)
        if (pl.second.ev && pl.second.fsb == snd) system::api().Event_Stop(pl.second.ev, fmod::kStopImmediate);
}

inline fmod::Result listenerResult() { return detail::g_state.lres; }
inline bool paused() { return detail::g_state.pausedNow; }

// 3.0's audio sync (flag 0x20) requests only the dialog; the event plays at the listener, as long as it is.
inline void choreographyStarted(const char* path, uint32_t flags)
{
    constexpr uint32_t kAudioSync = 0x20;   // the instance flag enableAudioSync sets
    const bool named = path && *path;
    if (!(flags & kAudioSync) || !named) return;
    if (const bool up = system::ready(); !up) return;
    const char* leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    const uint32_t ev = hash::fnv1Lower(leaf);
    if (!knowsEvent(ev)) return;   // no event of that name (every 3.0 choreography, most 1.0 ones)
    const uint32_t id = play({ev, 0, 0, nullptr, nullptr});
    log("audio10: %s (audio sync) starts the 1.0 sound event of its name -> %s", leaf, id ? "1.0 FMOD" : "not played");
}

}  // namespace engine::audio::host
