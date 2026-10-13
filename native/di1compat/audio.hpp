// 1.0 audio in 3.0 (plan M9, AUD-1..5). 3.0 plays sound through a statically linked Wwise 2014.1; 1.0's data is FMOD
// Ex/Event 4.44.14 (.fev projects, FSB5 MPEG banks) that Wwise cannot play, so the unmodified 1.0 FMOD DLLs are loaded
// from the 1.0 install and host only that data. 3.0's sound manager stays in charge of everything else: its bank
// requests, game objects, positions, posts, stops, pauses and end-of-event callbacks are mirrored onto FMOD at the Wwise
// API seam, so every 3.0 system that posts a sound (graph nodes, emitters, animation events, music stack) drives the
// 1.0 events unchanged.
//   - bank request dispatcher FUN_0182db30: '<stem>.fev'/'<stem>.bnk' -> the 1.0 '<stem>.fev' too (3.0 registers no
//     '.fev' handler, FUN_01831e30, so every 1.0 bank name was dead); '<X>_Music.bnk' that 3.0 lacks -> 1.0 'Music_<X>.fev'
//   - PostEvent FUN_01948a10 (every Wwise post funnels here; 0 = event unknown, FUN_0195d770): fallback to the 1.0 event
//     of the same name, registered as 1.0 0x011384a0 registers it
//   - SetPosition FUN_01947a50, listener FUN_01947150, StopPlayingID FUN_019478d0, UnregisterGameObj FUN_01947640,
//     ExecuteActionOnEvent FUN_01948c20 (stop/pause/resume), the Pause_All/Resume_All posts (3.0 FUN_0073f900)
//   - music: MusicStates check FUN_007397b0, SetState FUN_01947bd0 (3.0 music cues are Wwise states, FUN_0073ede0),
//     stack tick FUN_0073ede0: 1.0 MusicSystem prompts as 1.0 FUN_00717bd0 drives them
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace audio {

typedef int FR;   // FMOD_RESULT, 0 = FMOD_OK
typedef FR(__stdcall* EventCb)(void* ev, int type, void* p1, void* p2, void* ud);

inline void (*logfn)(const char* fmt, ...) = nullptr;
#define AUDLOG(...) do { if (::audio::logfn) ::audio::logfn(__VA_ARGS__); } while (0)

// 3.0 FUN_01947970 (Wwise GetIDFromString): FNV-1 over the lower-cased name (1,938 of 1,938 bank ids = fnv1(stem))
inline uint32_t fnv1(const char* s)
{
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h * 0x1000193u) ^ (uint8_t)tolower((uint8_t)*s);
    return h;
}
inline uint32_t fnv1(const std::string& s) { return fnv1(s.c_str()); }

inline std::string lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// The C API of fmodex.dll / fmod_event.dll 4.44.14 (all __stdcall; argument counts checked against each export's ret N)
struct Api {
    FR(__stdcall* EventSystem_Create)(void** es);
    FR(__stdcall* EventSystem_GetSystemObject)(void* es, void** sys);
    FR(__stdcall* EventSystem_GetMusicSystem)(void* es, void** ms);
    FR(__stdcall* EventSystem_Init)(void* es, int maxch, unsigned flags, void* extra, unsigned evflags);
    FR(__stdcall* EventSystem_SetMediaPath)(void* es, const char* path);
    FR(__stdcall* EventSystem_Load)(void* es, const char* name, void* info, void** proj);
    FR(__stdcall* EventSystem_Update)(void* es);
    FR(__stdcall* EventSystem_GetEventBySystemID)(void* es, unsigned id, unsigned mode, void** ev);
    FR(__stdcall* EventSystem_Set3DListenerAttributes)(void* es, int l, const float* pos, const float* vel, const float* fwd, const float* up);
    FR(__stdcall* EventSystem_GetVersion)(void* es, unsigned* v);
    FR(__stdcall* EventProject_Release)(void* p);
    FR(__stdcall* EventProject_GetNumGroups)(void* p, int* n);
    FR(__stdcall* EventProject_GetGroupByIndex)(void* p, int i, int cache, void** g);
    FR(__stdcall* EventProject_GetGroup)(void* p, const char* name, int cache, void** g);
    FR(__stdcall* EventProject_LoadSampleData)(void* p, int* ids, int nids, char** groups, int ngroups, unsigned mode);
    FR(__stdcall* EventGroup_GetNumGroups)(void* g, int* n);
    FR(__stdcall* EventGroup_GetGroupByIndex)(void* g, int i, int cache, void** sub);
    FR(__stdcall* EventGroup_GetNumEvents)(void* g, int* n);
    FR(__stdcall* EventGroup_GetEventByIndex)(void* g, int i, unsigned mode, void** ev);
    FR(__stdcall* EventGroup_GetProperty)(void* g, const char* name, void* value);
    FR(__stdcall* Event_GetInfo)(void* ev, int* index, char** name, void* info);
    FR(__stdcall* Event_GetPropertyByIndex)(void* ev, int idx, void* value, int thisInstance);
    FR(__stdcall* Event_GetCategory)(void* ev, void** cat);
    FR(__stdcall* EventCategory_GetInfo)(void* cat, int* index, char** name);
    FR(__stdcall* EventCategory_GetParentCategory)(void* cat, void** parent);
    FR(__stdcall* Event_Start)(void* ev);
    FR(__stdcall* Event_Stop)(void* ev, int immediate);
    FR(__stdcall* Event_SetPaused)(void* ev, int paused);
    FR(__stdcall* Event_Set3DAttributes)(void* ev, const float* pos, const float* vel, const float* orient);
    FR(__stdcall* Event_SetCallback)(void* ev, EventCb cb, void* ud);
    FR(__stdcall* EventSystem_GetCategory)(void* es, const char* name, void** cat);
    FR(__stdcall* EventCategory_SetVolume)(void* cat, float v);
    FR(__stdcall* MusicSystem_GetCues)(void* ms, void* it, const char* filter);
    FR(__stdcall* MusicSystem_GetNextCue)(void* ms, void* it);
    FR(__stdcall* MusicSystem_PrepareCue)(void* ms, unsigned id, void** prompt);
    FR(__stdcall* MusicSystem_Reset)(void* ms);
    FR(__stdcall* MusicSystem_SetPaused)(void* ms, int paused);
    FR(__stdcall* MusicPrompt_Begin)(void* p);
    FR(__stdcall* MusicPrompt_End)(void* p);
    FR(__stdcall* MusicPrompt_IsActive)(void* p, int* active);
    FR(__stdcall* MusicPrompt_Release)(void* p);
    FR(__stdcall* System_Set3DSettings)(void* s, float doppler, float dist, float rolloff);
    FR(__stdcall* System_GetNumDrivers)(void* s, int* n);
    FR(__stdcall* System_GetDriverCaps)(void* s, int id, unsigned* caps, int* rate, int* speakermode);
    FR(__stdcall* System_SetSpeakerMode)(void* s, int mode);
    FR(__stdcall* System_SetOutput)(void* s, int type);
    FR(__stdcall* System_GetSoftwareFormat)(void* s, int* rate, int* fmt, int* nout, int* maxin, int* resampler, int* bits);
    FR(__stdcall* System_SetSoftwareFormat)(void* s, int rate, int fmt, int nout, int maxin, int resampler);
    FR(__stdcall* System_SetStreamBufferSize)(void* s, unsigned size, unsigned unit);
    FR(__stdcall* System_SetAdvancedSettings)(void* s, void* adv);
    FR(__stdcall* System_GetVersion)(void* s, unsigned* v);

    bool load(const std::string& dir10)
    {
        HMODULE ex = LoadLibraryExA((dir10 + "\\fmodex.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        HMODULE ev = ex ? LoadLibraryExA((dir10 + "\\fmod_event.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
        if (!ev) return false;
        bool ok = true;
        auto get = [&](HMODULE m, const char* n) {
            void* p = (void*)GetProcAddress(m, n);
            if (!p) { AUDLOG("audio10: %s missing", n); ok = false; }
            return p;
        };
#define AUD_E(n) *(void**)&n = get(ev, "FMOD_" #n)
#define AUD_X(n) *(void**)&n = get(ex, "FMOD_" #n)
        AUD_E(EventSystem_Create); AUD_E(EventSystem_GetSystemObject); AUD_E(EventSystem_GetMusicSystem); AUD_E(EventSystem_Init);
        AUD_E(EventSystem_SetMediaPath); AUD_E(EventSystem_Load); AUD_E(EventSystem_Update); AUD_E(EventSystem_GetEventBySystemID);
        AUD_E(EventSystem_Set3DListenerAttributes); AUD_E(EventSystem_GetVersion); AUD_E(EventProject_Release);
        AUD_E(EventProject_GetNumGroups); AUD_E(EventProject_GetGroupByIndex); AUD_E(EventProject_GetGroup);
        AUD_E(EventProject_LoadSampleData); AUD_E(EventGroup_GetNumGroups); AUD_E(EventGroup_GetGroupByIndex);
        AUD_E(EventGroup_GetNumEvents); AUD_E(EventGroup_GetEventByIndex); AUD_E(EventGroup_GetProperty); AUD_E(Event_GetInfo);
        AUD_E(Event_GetPropertyByIndex); AUD_E(Event_GetCategory); AUD_E(EventCategory_GetInfo); AUD_E(EventCategory_GetParentCategory);
        AUD_E(Event_Start); AUD_E(Event_Stop); AUD_E(Event_SetPaused); AUD_E(Event_Set3DAttributes); AUD_E(Event_SetCallback);
        AUD_E(EventSystem_GetCategory); AUD_E(EventCategory_SetVolume); AUD_E(MusicSystem_GetCues); AUD_E(MusicSystem_GetNextCue); AUD_E(MusicSystem_PrepareCue); AUD_E(MusicSystem_Reset);
        AUD_E(MusicSystem_SetPaused); AUD_E(MusicPrompt_Begin); AUD_E(MusicPrompt_End); AUD_E(MusicPrompt_IsActive);
        AUD_E(MusicPrompt_Release);
        AUD_X(System_Set3DSettings); AUD_X(System_GetNumDrivers); AUD_X(System_GetDriverCaps); AUD_X(System_SetSpeakerMode);
        AUD_X(System_SetOutput); AUD_X(System_GetSoftwareFormat); AUD_X(System_SetSoftwareFormat); AUD_X(System_SetStreamBufferSize);
        AUD_X(System_SetAdvancedSettings); AUD_X(System_GetVersion);
#undef AUD_E
#undef AUD_X
        return ok;
    }
};

// One registered 1.0 event: the entry 1.0 FUN_01138430 keeps (+0xc system id, +0x10 flags), plus its owner project
struct Entry {
    void* proj;
    unsigned sysId;
    uint8_t flags;   // 1 'virtual' category, 2 looping (ONESHOT property 0), 4 its group has the 'Release' property
    bool ui;         // a 'ui' category is in its chain (Pause_All_Except_UI keeps it running)
    std::string name;
};

struct Project {
    void* p = nullptr;
    int refs = 0;
    int events = 0;
    bool registered = false;   // 1.0's load handler 0x01138830 registers only when its group lookup succeeds
};

// One FMOD event standing in for a Wwise playing id
struct Play {
    void* ev;          // nullptr: finished, or a stand-in with no FMOD event (StartMusic_X)
    void* proj;
    uint32_t obj, eventId, flags;
    void* cb;          // the 3.0 AkCallbackFunc (cdecl: type, info*) and its cookie
    void* cookie;
    bool ui;
    uint32_t born;     // tick number at start: its end is delivered one tick later at the earliest (3.0 FUN_01830270
                       // stores the playing id in the cookie record after the post returns; Wwise ends events no sooner)
};
struct V3 { float v[3]; };

struct Host {
    Api f{};
    void* es = nullptr;
    void* sys = nullptr;
    void* ms = nullptr;
    std::string sound10;          // <1.0 install>\assets\sound
    std::recursive_mutex mx;      // every FMOD call and the maps below
    std::map<std::string, Project> projects;          // lower-case fev name
    std::unordered_map<uint32_t, Entry> byId;         // fnv1(event name): the id 3.0 posts
    std::unordered_map<uint32_t, Play> plays;         // Wwise-space playing id
    std::unordered_map<uint32_t, V3> pos;             // game object -> engine-space position (SetPosition)
    float lpos[3] = {}, lfwd[3] = {0, 0, 1}, lup[3] = {0, 1, 0};
    bool lset = false;
    volatile LONG* idCounter = nullptr;               // Wwise's own playing-id counter (FUN_01948a10), so ids never clash
    volatile LONG ownCounter = 0x40000000;            // octtest: no engine
    std::mutex doneMx;
    std::vector<uint32_t> done;                       // finished playing ids, delivered outside mx
    uint32_t tickNo = 0;
    std::map<std::string, std::map<std::string, float>> faders;   // 3.0 volume faders: category -> source -> value
    std::map<std::string, std::string>* dropped = nullptr;   // octtest: events the 1.0 rule leaves out, with the reason

    // 3.0's faders ('<src>_Volume_Fader_<cat>' RTPCs, FUN_0182c460) onto the 1.0 categories of the same use
    // (1.0's table at 0x0148d240: sfx, music, dialog, ui/sfx, ui/songs, ui/dialog)
    void applyVolumes()
    {
        static const char* const map[][3] = {{"sfx", "sfx", "ui/sfx"}, {"music", "music", "ui/songs"}, {"dialog", "dialog", "ui/dialog"}};
        for (auto& m : map) {
            auto it = faders.find(m[0]);
            if (it == faders.end()) continue;
            float v = 1.0f;
            for (auto& s : it->second) v *= s.second;
            for (int i = 1; i < 3; ++i) {
                void* cat = nullptr;
                if (!f.EventSystem_GetCategory(es, m[i], &cat) && cat) f.EventCategory_SetVolume(cat, v);
            }
        }
    }

    // 1.0 FUN_011372c0 (Octane::Audio::Internal init), call for call. Left out: FMOD_Memory_Initialize with the engine
    // allocators (FMOD's own heap instead), FMOD_Debug_SetLevel (no-op in release DLLs) and setFileSystem with the
    // engine's file callbacks (the 1.0 sound folder is loose files: the media path points FMOD at it).
    bool init(const std::string& install10, bool nosound)
    {
        sound10 = install10 + "\\assets\\sound";
        if (!f.load(install10)) { AUDLOG("audio10: FMOD DLLs not loaded from %s (%lu)", install10.c_str(), GetLastError()); return false; }
        std::lock_guard<std::recursive_mutex> l(mx);
        FR r = f.EventSystem_Create(&es);
        if (!r) r = f.EventSystem_GetSystemObject(es, &sys);
        if (!r) r = f.EventSystem_GetMusicSystem(es, &ms);
        if (!r) r = f.System_Set3DSettings(sys, 1.0f, 1.0f, 1.0f);
        int drivers = 0;
        if (!r) r = f.System_GetNumDrivers(sys, &drivers);
        if (r) { AUDLOG("audio10: FMOD setup failed (%d)", r); return false; }
        if (nosound || drivers == 0) f.System_SetOutput(sys, 2);   // FMOD_OUTPUTTYPE_NOSOUND, as 1.0 does with no driver
        else {
            unsigned caps = 0;
            int mode = 0;
            if (!f.System_GetDriverCaps(sys, 0, &caps, nullptr, &mode)) f.System_SetSpeakerMode(sys, mode);
        }
        int rate = 0, fmt = 0, nout = 0, maxin = 0, resampler = 0, bits = 1;
        r = f.System_GetSoftwareFormat(sys, &rate, &fmt, &nout, &maxin, &resampler, &bits);
        if (!r) r = f.System_SetSoftwareFormat(sys, rate, fmt, 0, maxin, resampler);
        if (!r) r = f.System_SetStreamBufferSize(sys, 1000, 1);   // 1000, FMOD_TIMEUNIT_MS
        if (r) { AUDLOG("audio10: FMOD format setup failed (%d)", r); return false; }
        uint8_t adv[0x68] = {};
        *(uint32_t*)adv = 0x68;            // cbsize, as 1.0 passes it
        *(uint32_t*)(adv + 0x40) = 750;    // the one field 1.0 sets
        f.System_SetAdvancedSettings(sys, adv);
        r = f.EventSystem_Init(es, 64, 2, nullptr, 10);   // 64 channels, FMOD_INIT_3D_RIGHTHANDED, event flags 0xa (1.0's)
        if (r) { AUDLOG("audio10: EventSystem::init failed (%d)", r); return false; }
        f.EventSystem_SetMediaPath(es, (sound10 + "\\").c_str());
        unsigned v1 = 0, v2 = 0;
        f.System_GetVersion(sys, &v1);
        f.EventSystem_GetVersion(es, &v2);
        AUDLOG("audio10: FMOD Ex %08X / Event %08X up (%d drivers%s)", v1, v2, drivers, nosound ? ", no sound" : "");
        return true;
    }

    // the file's name as the 1.0 install spells it ('' if absent): 3.0 requests arrive lower-cased, FMOD group names are
    // matched as spelled
    std::string name10(const std::string& file) const
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((sound10 + "\\" + file).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return "";
        FindClose(h);
        return fd.cFileName;
    }

    // 1.0 0x011384a0, the delegate 1.0's load handler runs over every group (FUN_01133040 / FUN_01132fc0): an event is
    // registered when the category under 'master' is sfx/music/songs/dialog, or is 'ui' with one of those under it;
    // a 'virtual' category also needs a looping event with MAX_PLAYBACKS_BEHAVIOR 5. Same name: the last load wins.
    int registerGroup(void* proj, void* g)
    {
        int count = 0;
        uint8_t prop[16] = {};
        bool release = f.EventGroup_GetProperty(g, "Release", prop) == 0;
        int n = 0;
        f.EventGroup_GetNumEvents(g, &n);
        for (int i = 0; i < n; ++i) {
            void* ev = nullptr;
            if (f.EventGroup_GetEventByIndex(g, i, 4, &ev) || !ev) continue;   // FMOD_EVENT_INFOONLY
            char* name = nullptr;
            uint8_t info[0x80] = {};
            if (f.Event_GetInfo(ev, nullptr, &name, info) || !name) continue;
            int oneshot = 0;
            f.Event_GetPropertyByIndex(ev, 35, &oneshot, 0);   // FMOD_EVENTPROPERTY_ONESHOT
            bool virt = false, ui = false;
            void* cat = nullptr;
            if (!f.Event_GetCategory(ev, &cat) && cat) {
                int depth = 0, known = 0, uiDepth = 0;
                for (void* c = cat; c;) {
                    ++depth;
                    char* cn = nullptr;
                    if (!f.EventCategory_GetInfo(c, nullptr, &cn) && cn) {
                        if (!strcmp(cn, "sfx") || !strcmp(cn, "music") || !strcmp(cn, "songs") || !strcmp(cn, "dialog")) known = depth;
                        else if (!strcmp(cn, "ui")) uiDepth = depth;
                        else if (!strcmp(cn, "virtual")) virt = true;
                    }
                    void* parent = nullptr;
                    if (f.EventCategory_GetParentCategory(c, &parent)) parent = nullptr;
                    c = parent;
                }
                int top = depth - 1;   // the category directly under 'master'
                if (known != top && !(known == top - 1 && uiDepth == top)) {
                    if (dropped) (*dropped)[name] = "category";
                    continue;
                }
                if (virt) {
                    int behavior = 0;
                    f.Event_GetPropertyByIndex(ev, 13, &behavior, 0);   // FMOD_EVENTPROPERTY_MAX_PLAYBACKS_BEHAVIOR
                    if (oneshot || behavior != 5) {
                        if (dropped) (*dropped)[name] = "virtual";
                        continue;
                    }
                }
                ui = uiDepth > 0;
            }
            Entry& e = byId[fnv1(name)];
            e.proj = proj;
            e.sysId = *(unsigned*)(info + 0x20);   // FMOD_EVENT_INFO.systemid
            e.flags = (uint8_t)((virt ? 1 : 0) | (oneshot == 0 ? 2 : 0) | (release ? 4 : 0));
            e.ui = ui;
            e.name = name;
            ++count;
        }
        int ng = 0;
        f.EventGroup_GetNumGroups(g, &ng);
        for (int i = 0; i < ng; ++i) {
            void* sub = nullptr;
            if (!f.EventGroup_GetGroupByIndex(g, i, 0, &sub) && sub) count += registerGroup(proj, sub);
        }
        return count;
    }

    // 1.0's '.fev' handler 0x01138830 for a request 'X.fev[:Group]': EventSystem::load, then the request's group (the
    // file's stem when none is named; FUN_011342e0/FUN_011330a0: getGroup(name, cacheevents) + loadSampleData of it);
    // only if that group exists (or the project has none) are the project's events registered. Refcounted per file.
    void* load(const std::string& file, const char* group = nullptr)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        Project& pr = projects[lower(file)];
        if (pr.p) { ++pr.refs; return pr.p; }
        FR r = f.EventSystem_Load(es, file.c_str(), nullptr, &pr.p);
        if (r || !pr.p) { AUDLOG("audio10: load %s failed (%d)", file.c_str(), r); projects.erase(lower(file)); return nullptr; }
        pr.refs = 1;
        std::string gname = group ? group : file.substr(0, file.rfind('.'));
        int ng = 0;
        f.EventProject_GetNumGroups(pr.p, &ng);
        void* g = nullptr;
        bool found = ng == 0 || (!f.EventProject_GetGroup(pr.p, gname.c_str(), 1, &g) && g);
        if (found) {
            if (g) {
                char* names[1] = {(char*)gname.c_str()};
                f.EventProject_LoadSampleData(pr.p, nullptr, 0, names, 1, 0);
            }
            for (int i = 0; i < ng; ++i) {
                void* top = nullptr;
                if (!f.EventProject_GetGroupByIndex(pr.p, i, 0, &top) && top) pr.events += registerGroup(pr.p, top);
            }
            pr.registered = true;
        }
        applyVolumes();   // the categories a project defines exist once it is loaded
        AUDLOG("audio10: loaded %s: %d events%s", file.c_str(), pr.events, found ? "" : " (no group named after it: 1.0 leaves it unregistered)");
        return pr.p;
    }

    void unload(const std::string& file)
    {
        std::vector<uint32_t> finished;
        {
            std::lock_guard<std::recursive_mutex> l(mx);
            auto it = projects.find(lower(file));
            if (it == projects.end() || !it->second.p || --it->second.refs > 0) return;
            void* p = it->second.p;
            for (auto& pl : plays)
                if (pl.second.proj == p && pl.second.ev) { f.Event_Stop(pl.second.ev, 1); pl.second.ev = nullptr; finished.push_back(pl.first); }
            for (auto e = byId.begin(); e != byId.end();) e = e->second.proj == p ? byId.erase(e) : std::next(e);
            f.EventProject_Release(p);
            projects.erase(it);
            AUDLOG("audio10: unloaded %s", file.c_str());
        }
        std::lock_guard<std::mutex> d(doneMx);
        done.insert(done.end(), finished.begin(), finished.end());
    }

    static FR __stdcall onEvent(void* ev, int type, void* p1, void* p2, void* ud);

    uint32_t newId() { return (uint32_t)InterlockedIncrement(idCounter ? idCounter : &ownCounter); }

    void place(void* ev, uint32_t obj)
    {
        auto it = pos.find(obj);
        const float* p = it != pos.end() ? it->second.v : lpos;   // no position known: at the listener
        f.Event_Set3DAttributes(ev, p, nullptr, nullptr);
    }

    // 1.0 FUN_01136520 without the per-call volume / min-max overrides (3.0's post carries none): getEventBySystemID
    // (FMOD_EVENT_DEFAULT), callback, 3D attributes, start.
    uint32_t play(uint32_t eventId, uint32_t obj, uint32_t flags, void* cb, void* cookie)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        auto it = byId.find(eventId);
        if (it == byId.end()) return 0;
        void* ev = nullptr;
        FR r = f.EventSystem_GetEventBySystemID(es, it->second.sysId, 0, &ev);
        if (r || !ev) return 0;
        uint32_t id = newId();
        f.Event_SetCallback(ev, &Host::onEvent, (void*)(uintptr_t)id);
        place(ev, obj);
        if (f.Event_Start(ev)) { f.Event_SetCallback(ev, nullptr, nullptr); return 0; }
        plays[id] = Play{ev, it->second.proj, obj, eventId, flags, cb, cookie, it->second.ui, tickNo};
        return id;
    }

    // a playing id 3.0 holds for something with no FMOD event (StartMusic_X); finished like any other
    uint32_t standIn(uint32_t eventId, uint32_t obj, uint32_t flags, void* cb, void* cookie)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        uint32_t id = newId();
        plays[id] = Play{nullptr, nullptr, obj, eventId, flags, cb, cookie, false, tickNo};
        return id;
    }

    // 0 stop, 1 pause, 2 resume (AkActionOnEventType); true if the id is one of ours
    bool act(uint32_t id, int action)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        auto it = plays.find(id);
        if (it == plays.end()) return false;
        if (action == 1 || action == 2) { if (it->second.ev) f.Event_SetPaused(it->second.ev, action == 1); }
        else if (it->second.ev) f.Event_Stop(it->second.ev, 0);   // EVENTFINISHED arrives through the callback
        else { std::lock_guard<std::mutex> d(doneMx); done.push_back(id); }
        return true;
    }

    void stopObject(uint32_t obj)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        for (auto& pl : plays)
            if (pl.second.obj == obj && pl.second.ev) f.Event_Stop(pl.second.ev, 1);
        pos.erase(obj);
    }

    void pause(bool on, bool keepUi)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        for (auto& pl : plays)
            if (pl.second.ev && !(keepUi && pl.second.ui)) f.Event_SetPaused(pl.second.ev, on ? 1 : 0);
    }

    // one tick: listener and positions of live events, FMOD's update (fires the event callbacks), then the finished ids
    void tick(std::vector<std::pair<uint32_t, Play>>& out)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        ++tickNo;
        if (lset) f.EventSystem_Set3DListenerAttributes(es, 0, lpos, nullptr, lfwd, lup);
        for (auto& pl : plays)
            if (pl.second.ev) place(pl.second.ev, pl.second.obj);
        f.EventSystem_Update(es);
        std::vector<uint32_t> ids, later;
        {
            std::lock_guard<std::mutex> d(doneMx);
            ids.swap(done);
        }
        for (uint32_t id : ids) {
            auto it = plays.find(id);
            if (it == plays.end()) continue;
            if (it->second.born + 1 >= tickNo) { later.push_back(id); continue; }
            out.emplace_back(id, it->second);
            plays.erase(it);
        }
        if (!later.empty()) {
            std::lock_guard<std::mutex> d(doneMx);
            done.insert(done.end(), later.begin(), later.end());
        }
    }
};

inline Host g;

inline FR __stdcall Host::onEvent(void*, int type, void*, void*, void* ud)
{
    if (type == 3 || type == 4) {   // FMOD_EVENT_CALLBACKTYPE_STOLEN / EVENTFINISHED (1.0's callback 0x01136440 ends the sound on both)
        std::lock_guard<std::mutex> d(g.doneMx);
        g.done.push_back((uint32_t)(uintptr_t)ud);
    }
    return 0;
}

// ---- music: 1.0 Music_<X>.fev through the MusicSystem, driven by 3.0's MusicStack ----
struct Music {
    std::string playset;        // the 1.0-hosted playset whose Music_<X>.fev is loaded
    std::string file;
    void* prompt = nullptr;     // 1.0 FUN_00717bd0 keeps one MusicPrompt for the top entry
    bool seenActive = false;
    DWORD begunAt = 0;
    uint32_t startId = 0;       // the stand-in id for 'StartMusic_<X>'

    // 1.0 FUN_011335f0 -> FUN_01132af0 / FUN_01132b60: getCues(filter = name), first match
    static bool cue(const char* name, unsigned* id)
    {
        struct Entity { const char* name; unsigned id; };
        struct Iter { const Entity* value; const char* filter; } it{};
        if (g.f.MusicSystem_GetCues(g.ms, &it, name) || !it.value) return false;
        *id = it.value->id;
        return true;
    }

    void endPrompt()
    {
        if (!prompt) return;
        g.f.MusicPrompt_End(prompt);
        g.f.MusicPrompt_Release(prompt);
        prompt = nullptr;
    }

    // 1.0 FUN_00717bd0: prepareCue + begin for the new top entry
    bool begin(const char* name)
    {
        endPrompt();
        unsigned id = 0;
        if (!cue(name, &id) || g.f.MusicSystem_PrepareCue(g.ms, id, &prompt) || !prompt) { prompt = nullptr; return false; }
        g.f.MusicPrompt_Begin(prompt);
        seenActive = false;
        begunAt = GetTickCount();
        return true;
    }

    // 1.0 FUN_00715650: load Music_<X>.fev, unloading the previous playset's (MusicSystem::reset first, FUN_01133630)
    bool ensure(const std::string& x)
    {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        std::string want = g.name10("Music_" + x + ".fev");
        if (want.empty()) return false;
        if (lower(want) == lower(file)) return true;
        drop();
        if (!g.load(want)) return false;
        file = want;
        playset = x;
        return true;
    }

    void drop()
    {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        if (file.empty()) return;
        endPrompt();
        g.f.MusicSystem_Reset(g.ms);
        g.unload(file);
        file.clear();
        playset.clear();
        if (startId) { g.act(startId, 0); startId = 0; }
    }
};
inline Music music;

// ---- engine glue ----
struct Cfg {
    bool on = true;       // audio10: the FMOD host and the Wwise-seam mirroring
    bool music = true;    // music10: 1.0 music through the MusicStack
    bool trace = false;   // audiotrace10: log each posted name once with the backend that took it
};
inline Cfg cfg;
inline std::string sound30;   // <3.0 install>\assets\sound

// a playset X whose music only 1.0 has: 3.0 ships no '<x>_music.bnk', 1.0 has 'Music_<X>.fev'
inline bool hostedMusic(const std::string& x)
{
    if (x.empty() || GetFileAttributesA((sound30 + "\\" + lower(x) + "_music.bnk").c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    return !g.name10("Music_" + x + ".fev").empty();
}

typedef uint32_t(__fastcall* BankReqFn)(void* mgr, void* edx, uint32_t* req);
typedef uint32_t(__cdecl* PostFn)(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, void* ext, uint32_t pid);
typedef int(__cdecl* SetPosFn)(uint32_t obj, const float* p);
typedef int(__cdecl* ListenerFn)(const float* p, uint32_t index);
typedef int(__cdecl* StopFn)(uint32_t pid, uint32_t ms, uint32_t curve);
typedef int(__cdecl* UnregFn)(uint32_t obj);
typedef int(__cdecl* ActionFn)(const char* name, int action, uint32_t obj, uint32_t ms, uint32_t curve, uint32_t pid);
typedef uint32_t(__fastcall* MusicStateFn)(void* self, void* edx, const char* group, const char* state);
typedef int(__cdecl* SetStateFn)(const char* group, const char* state);
typedef void(__fastcall* StackTickFn)(void* self, void* edx);
typedef void(__fastcall* StackPopFn)(void* self, void* edx, void* entry);
typedef void(__cdecl* AkCallback)(uint32_t type, void* info);
typedef int(__fastcall* QueuedPostFn)(void* mgr, void* edx, uint32_t* req);
typedef void(__stdcall* FaderFn)(const char* cat, const char* src, float v, uint32_t a4);
inline FaderFn oFader;

// FUN_0182c460 (stdcall, ret 0x10): SetRTPCValue('<src>_Volume_Fader_<cat>', v); 3.0 FUN_0073e690 sets sfx/dialog/music
// from 'Options'
inline void __stdcall hkFader(const char* cat, const char* src, float v, uint32_t a4)
{
    if (cat && src) {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        g.faders[cat][src] = v;
        g.applyVolumes();
    }
    oFader(cat, src, v, a4);
}

inline BankReqFn oBankReq;
inline PostFn oPost;
inline SetPosFn oSetPos;
inline ListenerFn oListener;
inline StopFn oStop;
inline UnregFn oUnreg;
inline ActionFn oAction;
inline MusicStateFn oMusicState;
inline SetStateFn oSetState;
inline StackTickFn oStackTick;
inline StackPopFn stackPop;
inline QueuedPostFn oQueuedPost;
inline uint32_t idPauseAll, idPauseExUi, idResumeAll;

inline std::mutex traceMx;
inline std::unordered_set<uint32_t> traced;
inline void trace(uint32_t id, const char* name, const char* how)
{
    if (!cfg.trace) return;
    std::lock_guard<std::mutex> l(traceMx);
    if (traced.insert(id ^ fnv1(how)).second) AUDLOG("audio10: %s -> %s", name ? name : "?", how);
}

// FUN_0182db30 (thiscall on the sound manager): the request FUN_0182dbb0 built, {0 load / 1 unload, -, file, name, file}
inline uint32_t __fastcall hkBankReq(void* mgr, void*, uint32_t* req)
{
    uint32_t r = oBankReq(mgr, nullptr, req);
    const char* file = req ? (const char*)req[2] : nullptr;
    if (!file || !*file) return r;
    std::string f = file;
    size_t dot = f.rfind('.');
    if (dot == std::string::npos) return r;
    std::string stem = f.substr(0, dot), ext = lower(f.substr(dot));
    if (ext != ".bnk" && ext != ".fev") return r;
    bool load = req[0] == 0;
    std::string ls = lower(stem);
    if (cfg.music && ext == ".bnk" && ls.size() > 6 && ls.compare(ls.size() - 6, 6, "_music") == 0) {
        std::string x = stem.substr(0, stem.size() - 6);   // 3.0 FUN_0073eb30: '%s_Music.bnk' per playset
        if (!hostedMusic(x)) return r;
        if (load) { if (music.ensure(x)) r = 1; }
        else if (lower(music.playset) == lower(x)) music.drop();
        AUDLOG("audio10: %s %s -> Music_%s.fev", load ? "load" : "unload", file, x.c_str());
        return r;
    }
    std::string fev = g.name10(stem + ".fev");
    if (fev.empty()) return r;
    if (load) { if (g.load(fev) && ext == ".fev") r = 1; }
    else g.unload(fev);
    return r;
}

// FUN_01948a10: Wwise PostEvent(id, gameObj, flags, callback, cookie, externals, playingId) -> playing id, 0 = unknown
inline uint32_t __cdecl hkPost(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, void* ext, uint32_t pid)
{
    uint32_t r = oPost(ev, obj, flags, cb, cookie, ext, pid);
    if (ev == idPauseAll || ev == idPauseExUi || ev == idResumeAll) {   // 3.0 FUN_0073f900
        g.pause(ev != idResumeAll, ev == idPauseExUi);
        if (ev != idPauseExUi) { std::lock_guard<std::recursive_mutex> l(g.mx); g.f.MusicSystem_SetPaused(g.ms, ev == idPauseAll); }
    }
    if (r || ext) return r;   // a dialog line with external sources is 3.0's own (VO: see vo10)
    if (cfg.music && !music.playset.empty()) {
        if (ev == fnv1("StartMusic_" + music.playset)) {   // 3.0 FUN_0073eb30 starts its playset's music
            if (!music.startId) music.startId = g.standIn(ev, obj, flags, cb, cookie);
            return music.startId;
        }
        if (ev == fnv1("StopMusic_" + music.playset)) {   // 1.0 resets the MusicSystem before switching (FUN_00715650)
            std::lock_guard<std::recursive_mutex> l(g.mx);
            music.endPrompt();
            g.f.MusicSystem_Reset(g.ms);
            if (music.startId) { g.act(music.startId, 0); music.startId = 0; }
            return 0;
        }
    }
    return g.play(ev, obj, flags, cb, cookie);
}

// FUN_01947a50: SetPosition(gameObj, AkSoundPosition {position, orientation}); 3.0 FUN_0182f170 negated z for Wwise
inline int __cdecl hkSetPos(uint32_t obj, const float* p)
{
    if (p) {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        V3& v = g.pos[obj];
        v.v[0] = p[0]; v.v[1] = p[1]; v.v[2] = -p[2];
    }
    return oSetPos(obj, p);
}

// FUN_01947150: SetListenerPosition(AkListenerPosition {front, top, position}, index); z negated by FUN_0182d340
inline int __cdecl hkListener(const float* p, uint32_t index)
{
    if (p && index == 0) {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        for (int i = 0; i < 3; ++i) {
            float s = i == 2 ? -1.0f : 1.0f;
            g.lfwd[i] = p[i] * s; g.lup[i] = p[3 + i] * s; g.lpos[i] = p[6 + i] * s;
        }
        g.lset = true;
    }
    return oListener(p, index);
}

// FUN_019478d0: StopPlayingID(id, fade ms, curve)
inline int __cdecl hkStop(uint32_t pid, uint32_t ms, uint32_t curve)
{
    g.act(pid, 0);
    return oStop(pid, ms, curve);
}

// FUN_01947640: UnregisterGameObj (Wwise stops what plays on it)
inline int __cdecl hkUnreg(uint32_t obj)
{
    g.stopObject(obj);
    return oUnreg(obj);
}

// FUN_01948c20: ExecuteActionOnEvent(name, action, gameObj, ms, curve, playingId); 3.0 FUN_0182c3b0 pauses/resumes
inline int __cdecl hkAction(const char* name, int action, uint32_t obj, uint32_t ms, uint32_t curve, uint32_t pid)
{
    if (action >= 0 && action <= 2) {
        if (pid) g.act(pid, action);
        else if (name) {
            uint32_t ev = fnv1(name);
            std::vector<uint32_t> ids;
            {
                std::lock_guard<std::recursive_mutex> l(g.mx);
                for (auto& pl : g.plays)
                    if (pl.second.eventId == ev && (obj == 0xffffffffu || pl.second.obj == obj)) ids.push_back(pl.first);
            }
            for (uint32_t id : ids) g.act(id, action);
        }
    }
    return oAction(name, action, obj, ms, curve, pid);
}

// FUN_007397b0 (thiscall, MusicStates.bin): 3.0 FUN_0073c5c0 ignores a cue not listed there. For a 1.0-hosted playset
// the list is Music_<X>.fev's cues, loaded at push time as 1.0 FUN_00716ac0 loads it.
inline uint32_t __fastcall hkMusicState(void* self, void*, const char* group, const char* state)
{
    uint32_t r = oMusicState(self, nullptr, group, state);
    if (r || !group || !state || _strnicmp(group, "Music_", 6)) return r;
    std::string x = group + 6;
    if (!hostedMusic(x) || !music.ensure(x)) return r;
    std::lock_guard<std::recursive_mutex> l(g.mx);
    unsigned id = 0;
    r = Music::cue(state, &id) ? 1 : 0;
    AUDLOG("audio10: music cue %s:%s %s", group, state, r ? "is a 1.0 cue" : "is not in the 1.0 project");
    return r;
}

// FUN_01947bd0: SetState(group, state); 3.0 FUN_0073ede0 sets 'Music_<X>' to the top cue ('None' for '<X>_None')
inline int __cdecl hkSetState(const char* group, const char* state)
{
    if (group && state && !_strnicmp(group, "Music_", 6) && !music.playset.empty() && !_stricmp(group + 6, music.playset.c_str())) {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        if (!_stricmp(state, "None")) music.endPrompt();
        else AUDLOG("audio10: music cue %s %s", state, music.begin(state) ? "prompted" : "failed");
    }
    return oSetState(group, state);
}

// FUN_0073ede0 (thiscall, MusicStack tick). 1.0 FUN_00717bd0 also pops the top entry once its prompt stops being
// active (a cue that ends by itself); 3.0's stack never learns that from Wwise states, so it is done here, with 3.0's
// own unlink FUN_0073b2c0. An entry is taken as ended only after it was seen active (the FMOD update runs on its own
// thread) or 2 s without ever starting.
inline void __fastcall hkStackTick(void* self, void*)
{
    oStackTick(self, nullptr);
    if (!music.prompt) return;
    void* entry = nullptr;
    {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        if (!music.prompt) return;
        int active = 0;
        g.f.MusicPrompt_IsActive(music.prompt, &active);
        if (active) { music.seenActive = true; return; }
        if (!music.seenActive && GetTickCount() - music.begunAt < 2000) return;
        g.f.MusicPrompt_Release(music.prompt);
        music.prompt = nullptr;
        entry = *(void**)((uint8_t*)self + 0xc);
    }
    if (entry) {
        *(void**)((uint8_t*)self + 0xc) = nullptr;
        stackPop(self, nullptr, entry);
    }
}

// FUN_01830270 (trace only; thiscall on the sound manager, FUN_018309e0): the queued post {name, gameObj, 'bank:event',
// ...}; logs which backend took each name
inline int __fastcall hkQueuedPost(void* mgr, void*, uint32_t* req)
{
    int r = oQueuedPost(mgr, nullptr, req);
    const char* name = req ? (const char*)req[0] : nullptr;
    if (name) {
        uint32_t id = fnv1(name);
        bool ours;
        {
            std::lock_guard<std::recursive_mutex> l(g.mx);
            ours = g.plays.count((uint32_t)r) != 0;
        }
        trace(id, name, !r ? "nothing (no Wwise or 1.0 event)" : ours ? "1.0 FMOD" : "Wwise");
    }
    return r;
}

// one tick plus the end-of-event callbacks of what finished, outside every lock
inline int tickAndDeliver()
{
    std::vector<std::pair<uint32_t, Play>> fin;
    g.tick(fin);
    for (auto& d : fin) {
        if (!(d.second.flags & 1) || !d.second.cb) continue;   // AK_EndOfEvent requested
        struct { void* cookie; uint32_t obj, pid, ev; } info = {d.second.cookie, d.second.obj, d.first, d.second.eventId};
        ((AkCallback)d.second.cb)(1, &info);   // AkEventCallbackInfo, as Wwise delivers it (3.0 LAB_01830120)
    }
    return (int)fin.size();
}

inline DWORD WINAPI tickThread(void*)
{
    for (;;) {
        Sleep(16);
        tickAndDeliver();
    }
}

typedef bool (*HookFn)(const char* name, uint32_t staticVa, const char* prologue, void* detour, void** original);

inline bool install(HookFn hook, uintptr_t (*rt)(uint32_t), const std::string& install10, const std::string& game30)
{
    if (!g.init(install10, false)) return false;
    sound30 = game30 + "\\assets\\sound";
    g.idCounter = (volatile LONG*)rt(0x022AFBE8);
    g.load("Initialize.fev", "Permanent");   // 1.0 FUN_01138210 at audio init: 'Initialize.fev:Permanent'
    idPauseAll = fnv1("Pause_All");
    idPauseExUi = fnv1("Pause_All_Except_UI");
    idResumeAll = fnv1("Resume_All");
    hook("sound bank request (audio10)", 0x0182DB30, "568B711C5785F6", (void*)&hkBankReq, (void**)&oBankReq);
    hook("Wwise PostEvent (audio10)", 0x01948A10, "558BEC8B45088B0D????????", (void*)&hkPost, (void**)&oPost);
    hook("Wwise SetPosition (audio10)", 0x01947A50, "558BEC8B450885C07507", (void*)&hkSetPos, (void**)&oSetPos);
    hook("Wwise SetListenerPosition (audio10)", 0x01947150, "558BEC535657E8", (void*)&hkListener, (void**)&oListener);
    hook("Wwise StopPlayingID (audio10)", 0x019478D0, "558BEC568B750885F6", (void*)&hkStop, (void**)&oStop);
    hook("Wwise UnregisterGameObj (audio10)", 0x01947640, "558BEC568B750885F67506", (void*)&hkUnreg, (void**)&oUnreg);
    hook("Wwise ExecuteActionOnEvent (audio10)", 0x01948C20, "558BEC8B450850E8", (void*)&hkAction, (void**)&oAction);
    hook("volume fader (audio10)", 0x0182C460, "8B4424048B4C240883EC30", (void*)&hkFader, (void**)&oFader);
    if (cfg.music) {
        stackPop = (StackPopFn)rt(0x0073B2C0);
        hook("MusicStates cue check (music10)", 0x007397B0, "578BF9837F1400", (void*)&hkMusicState, (void**)&oMusicState);
        hook("Wwise SetState (music10)", 0x01947BD0, "558BEC8B450856", (void*)&hkSetState, (void**)&oSetState);
        hook("MusicStack tick (music10)", 0x0073EDE0, "A1????????DB05????????81EC04010000", (void*)&hkStackTick, (void**)&oStackTick);
    }
    if (cfg.trace) hook("queued sound post (audiotrace10)", 0x01830270, "83EC44538B5C244C", (void*)&hkQueuedPost, (void**)&oQueuedPost);
    CreateThread(nullptr, 0, tickThread, nullptr, 0, nullptr);
    return true;
}

}  // namespace audio
