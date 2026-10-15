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
//   - mixer: 1.0's Mixer presets (a datamap kind 3.0 dropped) as the 'Mixer' source of the 1.0 category volumes
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "third_party/miniz/miniz.h"

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
    FR(__stdcall* Event_SetVolume)(void* ev, float v);
    FR(__stdcall* Event_SetCallback)(void* ev, EventCb cb, void* ud);
    FR(__stdcall* EventSystem_GetCategory)(void* es, const char* name, void** cat);
    FR(__stdcall* EventSystem_GetMusicCategory)(void* es, void** cat);
    FR(__stdcall* EventCategory_SetVolume)(void* cat, float v);
    FR(__stdcall* EventCategory_GetVolume)(void* cat, float* v);
    FR(__stdcall* MusicSystem_GetCues)(void* ms, void* it, const char* filter);
    FR(__stdcall* MusicSystem_GetNextCue)(void* ms, void* it);
    FR(__stdcall* MusicSystem_PrepareCue)(void* ms, unsigned id, void** prompt);
    FR(__stdcall* MusicSystem_PromptCue)(void* ms, unsigned id);
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
    FR(__stdcall* System_CreateSound)(void* s, const char* name, unsigned mode, void* exinfo, void** snd);
    FR(__stdcall* Sound_GetSubSound)(void* snd, int i, void** sub);
    FR(__stdcall* Sound_GetNumSubSounds)(void* snd, int* n);
    FR(__stdcall* Sound_GetLength)(void* snd, unsigned* len, unsigned unit);
    FR(__stdcall* Sound_Release)(void* snd);

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
        AUD_E(Event_Start); AUD_E(Event_Stop); AUD_E(Event_SetPaused); AUD_E(Event_Set3DAttributes); AUD_E(Event_SetVolume);
        AUD_E(Event_SetCallback);
        AUD_E(EventSystem_GetCategory); AUD_E(EventSystem_GetMusicCategory); AUD_E(EventCategory_SetVolume);
        AUD_E(EventCategory_GetVolume); AUD_E(MusicSystem_GetCues); AUD_E(MusicSystem_GetNextCue); AUD_E(MusicSystem_PrepareCue); AUD_E(MusicSystem_PromptCue); AUD_E(MusicSystem_Reset);
        AUD_E(MusicSystem_SetPaused); AUD_E(MusicPrompt_Begin); AUD_E(MusicPrompt_End); AUD_E(MusicPrompt_IsActive);
        AUD_E(MusicPrompt_Release);
        AUD_X(System_Set3DSettings); AUD_X(System_GetNumDrivers); AUD_X(System_GetDriverCaps); AUD_X(System_SetSpeakerMode);
        AUD_X(System_SetOutput); AUD_X(System_GetSoftwareFormat); AUD_X(System_SetSoftwareFormat); AUD_X(System_SetStreamBufferSize);
        AUD_X(System_SetAdvancedSettings); AUD_X(System_GetVersion); AUD_X(System_CreateSound); AUD_X(Sound_GetSubSound);
        AUD_X(Sound_GetNumSubSounds); AUD_X(Sound_GetLength); AUD_X(Sound_Release);
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
    std::map<std::string, std::vector<std::string>> deps;   // lower-case group -> its 'Load' list (1.0 FUN_011348e0)
};

// 1.0 FUN_01134440: a 'Load' property value split at ' ', ',' and ';'; an item without extension is a '.fev'; an item
// may name a group ('mu_campus.fev:mu_exploders'), kept in the item
inline void splitLoad(const char* v, std::vector<std::string>& out)
{
    std::string cur;
    for (const char* c = v;; ++c) {
        if (*c && *c != ' ' && *c != ',' && *c != ';') { cur += *c; continue; }
        if (!cur.empty()) {
            size_t colon = cur.find(':');
            std::string file = cur.substr(0, colon);
            if (file.find('.') == std::string::npos) file += ".fev";
            out.push_back(colon == std::string::npos ? file : file + cur.substr(colon));
            cur.clear();
        }
        if (!*c) break;
    }
}

// ---- 1.0 Mixer: the datamap kind 'Mixer' (1.0 FUN_006efd10 case 7 -> FUN_00714f50) that 3.0 dropped (3.0's kind
// list has none, FUN_0070c090); 1.0's core datamap names it 'Mixer|GameDB/Core/Mixer.lua', a plain Lua table ----

// A Lua table constructor as data: numbers, strings, booleans, nested tables, 'name = value' and positional fields
struct LuaV {
    int t = 0;   // 0 nil, 1 number, 2 string, 3 boolean, 4 table
    double n = 0;
    std::string s;
    std::map<std::string, LuaV> kv;
    std::vector<LuaV> arr;
    const LuaV* get(const char* k) const { auto it = kv.find(k); return it == kv.end() ? nullptr : &it->second; }
    double num(const char* k, double d) const { const LuaV* v = get(k); return v && (v->t == 1 || v->t == 3) ? v->n : d; }
    std::string str(const char* k) const { const LuaV* v = get(k); return v && v->t == 2 ? v->s : ""; }
};
struct LuaParse {
    const char* p;
    const char* e;
    void ws()
    {
        for (;;) {
            while (p < e && isspace((uint8_t)*p)) ++p;
            if (e - p < 2 || p[0] != '-' || p[1] != '-') return;
            while (p < e && *p != '\n') ++p;
        }
    }
    bool word(const char* w)
    {
        size_t n = strlen(w);
        if ((size_t)(e - p) < n || memcmp(p, w, n) || (p + n < e && (isalnum((uint8_t)p[n]) || p[n] == '_'))) return false;
        p += n;
        return true;
    }
    bool value(LuaV& v, int depth = 0)
    {
        ws();
        if (p >= e || depth > 32) return false;
        if (*p == '{') {
            ++p;
            v.t = 4;
            for (;;) {
                ws();
                if (p >= e) return false;
                if (*p == '}') { ++p; return true; }
                const char* at = p;
                std::string name;
                while (p < e && (isalnum((uint8_t)*p) || *p == '_')) name += *p++;
                ws();
                if (!name.empty() && !isdigit((uint8_t)name[0]) && p < e && *p == '=' && (p + 1 >= e || p[1] != '=')) {
                    ++p;
                    if (!value(v.kv[name], depth + 1)) return false;
                } else {
                    p = at;
                    v.arr.emplace_back();
                    if (!value(v.arr.back(), depth + 1)) return false;
                }
                ws();
                if (p < e && (*p == ',' || *p == ';')) ++p;
            }
        }
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            v.t = 2;
            while (p < e && *p != q) {
                if (*p == '\\' && p + 1 < e) ++p;
                v.s += *p++;
            }
            if (p >= e) return false;
            ++p;
            return true;
        }
        if (word("true")) { v.t = 3; v.n = 1; return true; }
        if (word("false")) { v.t = 3; v.n = 0; return true; }
        if (word("nil")) return true;
        std::string num;
        while (p < e && (isalnum((uint8_t)*p) || *p == '.' || *p == '-' || *p == '+')) num += *p++;
        char* end = nullptr;
        v.n = strtod(num.c_str(), &end);
        v.t = 1;
        return !num.empty() && end && !*end;
    }
};

// A file of the 1.0 install's root archives (assets\*.zip: data.zip, startup*.zip, mounted at the root by 1.0, so the
// member path is the engine path; startup2.zip holds gamedb/core/coredatamap.lua, startup3.zip gamedb/core/mixer.lua)
inline bool readRoot10(const std::string& install10, const std::string& path, std::string& out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((install10 + "\\assets\\*.zip").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        mz_zip_archive z{};
        if (!mz_zip_reader_init_file(&z, (install10 + "\\assets\\" + fd.cFileName).c_str(), 0)) continue;
        int i = mz_zip_reader_locate_file(&z, path.c_str(), nullptr, 0);   // case-insensitive
        size_t n = 0;
        void* d = i >= 0 ? mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &n, 0) : nullptr;
        if (d) { out.assign((const char*)d, n); mz_free(d); found = true; }
        mz_zip_reader_end(&z);
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

// The 'Mixer' entries of 1.0's core datamap (GameDB/Core/CoreDataMap.lua, 1.0 string 0x01350a24; Lua bytecode: the
// entry is the string constant 'Mixer|<path>')
inline std::vector<std::string> mixerPaths(const std::string& coreDataMap)
{
    std::vector<std::string> out;
    for (size_t at = coreDataMap.find("Mixer|"); at != std::string::npos; at = coreDataMap.find("Mixer|", at + 6)) {
        size_t b = at + 6, e = b;
        while (e < coreDataMap.size() && isprint((uint8_t)coreDataMap[e]) && coreDataMap[e] != '|') ++e;
        if (e > b) out.push_back(coreDataMap.substr(b, e - b));
    }
    return out;
}

// 1.0's mixer at rest, per group (= FMOD category name): the 'Mixer' fader source (1.0 FUN_00713360: fader(group,
// 'Mixer', base * v), base 1). Presets active from the start (ActivateOnStart, FUN_00714a00 -> FUN_00713f50) with their
// Gain; tick FUN_007141d0: v = clamp(10^(clamp(sum, -60, 60) / 20) * MasterVolume, 0, 1), MasterVolume = 10^(dB / 20)
// (FUN_00713290); a Volume entry contributes (FUN_00713490, gain clamped to 0..1)
// 20 * log10(clamp(1 - (1 - 10^(clamp(Value, -60, 60) / 20)) * gain, 1e-5, 1e4)), clamped to -60..60.
// ponytail: the fade to that value (FadeInTime, curve) is not run -- the 1.0 presets only switch at start (no
// SoundMixerPreset node in the Metroville graphs); run FUN_00713490's fade if a node or the world edge drives them.
struct MixerRest { std::map<std::string, float> vol; std::vector<std::string> active; double pitchSum = 0; };
inline bool mixerRest(const std::string& lua, MixerRest& out, std::string* err = nullptr)
{
    auto fail = [&](const char* why) { if (err) *err = why; return false; };
    size_t b = lua.find('{');
    if (b == std::string::npos) return fail("no table");
    LuaV m;
    LuaParse ps{lua.data() + b, lua.data() + lua.size()};
    if (!ps.value(m) || m.t != 4) return fail("not a Lua table");
    const LuaV* gs = m.get("GlobalSettings");
    const LuaV* groups = m.get("Groups");
    const LuaV* presets = m.get("Presets");
    if (!groups || groups->t != 4 || !presets || presets->t != 4) return fail("no Groups or Presets");
    auto clampd = [](double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; };
    double master = pow(10.0, clampd(gs ? gs->num("MasterVolume", 0) : 0, -60, 60) / 20);
    for (auto& p : presets->arr)
        if (p.num("ActivateOnStart", 0)) out.active.push_back(p.str("Name"));
    for (auto& g : groups->arr) {
        std::string name = g.str("Name");
        if (name.empty()) continue;
        double sum = 0;
        for (auto& p : presets->arr) {
            if (!p.num("ActivateOnStart", 0)) continue;
            double gain = clampd(p.num("Gain", 1), 0, 1);
            for (auto& entry : p.arr) {
                if (entry.str("Name") != name) continue;
                for (auto& par : entry.arr) {
                    std::string kind = par.str("Parameter");
                    double v = par.num("Value", 0);
                    if (kind == "Volume") {
                        double x = clampd(1 - (1 - pow(10.0, clampd(v, -60, 60) / 20)) * gain, 1e-5, 1e4);
                        sum += clampd(20 * log10(x), -60, 60);
                    } else if (kind == "Pitch")
                        out.pitchSum += fabs(v * gain);
                }
            }
        }
        out.vol[name] = (float)clampd(pow(10.0, clampd(sum, -60, 60) / 20) * master, 0, 1);
    }
    if (gs) out.pitchSum += fabs(gs->num("MasterPitch", 0));
    return true;
}

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
    void* fsb;         // a dialog line: the 1.0 bank whose subsound it plays
    DWORD t0 = GetTickCount();   // audiotrace10: started at
    bool stopReq = false;        // audiotrace10: 3.0 asked for its stop (StopPlayingID / action)
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
    FR lres = 0;                                      // audiotrace10: the last set3DListenerAttributes result
    std::mutex posMx;                                // pos and the listener: the game thread's SetPosition calls never
                                                      // wait for FMOD's update, which runs under mx
    volatile LONG* idCounter = nullptr;               // Wwise's own playing-id counter (FUN_01948a10), so ids never clash
    volatile LONG ownCounter = 0x40000000;            // octtest: no engine
    std::mutex doneMx;
    std::vector<uint32_t> done;                       // finished playing ids, delivered outside mx
    std::vector<std::pair<uint32_t, float>> durs;     // dialog lines started: (playing id, length ms) for AK_Duration
    void* progSound[4096] = {};                       // playing id & 4095 -> the programmer sound its event asks for
    uint32_t tickNo = 0;
    std::map<std::string, std::map<std::string, float>> faders;   // 3.0 volume faders: category -> source -> value
    std::map<std::string, float> mixer;                           // 1.0 'Mixer' source per 1.0 category (mixer10)
    std::map<std::string, std::string>* dropped = nullptr;   // octtest: events the 1.0 rule leaves out, with the reason
    int testSpeakerMode = -1;                                 // octtest: the speaker mode a no-sound system mixes for

    // A 1.0 category's volume is the product of its fader sources (1.0 FUN_01136b10 drops a source that reaches 1.0:
    // neutral). 3.0's faders ('<src>_Volume_Fader_<cat>' RTPCs, FUN_0182c460) go onto the 1.0 categories of the same
    // use (1.0's table at 0x0148d240: sfx, music, dialog, ui/sfx, ui/songs, ui/dialog), times 1.0's own 'Mixer' source.
    void applyVolumes()
    {
        static const char* const map[][3] = {{"sfx", "sfx", "ui/sfx"}, {"music", "music", "ui/songs"}, {"dialog", "dialog", "ui/dialog"}};
        std::map<std::string, float> vol = mixer;
        for (auto& m : map) {
            auto it = faders.find(m[0]);
            if (it == faders.end()) continue;
            float v = 1.0f;
            for (auto& s : it->second) v *= s.second;
            for (int i = 1; i < 3; ++i) vol[m[i]] = (vol.count(m[i]) ? vol[m[i]] : 1.0f) * v;
        }
        for (auto& c : vol) {
            void* cat = nullptr;
            if (!f.EventSystem_GetCategory(es, c.first.c_str(), &cat) && cat) f.EventCategory_SetVolume(cat, c.second);
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
        if (nosound || drivers == 0) {
            f.System_SetOutput(sys, 2);   // FMOD_OUTPUTTYPE_NOSOUND, as 1.0 does with no driver
            if (testSpeakerMode >= 0) f.System_SetSpeakerMode(sys, testSpeakerMode);
        } else {
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

    // 1.0 FUN_011348e0: the 'Load' property of a group and of every group under it
    void collectDeps(void* g, std::vector<std::string>& out)
    {
        const char* v = nullptr;
        if (!f.EventGroup_GetProperty(g, "Load", &v) && v) splitLoad(v, out);
        int n = 0;
        f.EventGroup_GetNumGroups(g, &n);
        for (int i = 0; i < n; ++i) {
            void* sub = nullptr;
            if (!f.EventGroup_GetGroupByIndex(g, i, 0, &sub) && sub) collectDeps(sub, out);
        }
    }

    // 1.0's '.fev' handler 0x01138830 for a request 'X.fev[:Group]': EventSystem::load, then the request's group (the
    // file's stem when none is named; FUN_011342e0/FUN_011330a0: getGroup(name, cacheevents) + loadSampleData of it);
    // only if that group exists (or the project has none) are the project's events registered. Refcounted per file.
    // deps: the group's 'Load' list, which 1.0 requests through its dispatcher on every load of the file (0x0113887b,
    // 0x011389cc) and every unload (FUN_01136e80): IN_City.fev -> Inc_OmniSky_Spawner.fev, Inc_Buildings.fev;
    // Inc_Townspeople.fev -> the townspeople's dialog banks; Edna.fev -> Edna_str.fsb (143 of the 316 FEVs have one).
    // the 'Load' list of the request's group, read once per group (an item may name another group of the same file:
    // mu_campus.fev lists mu_campus.fev:mu_exploders, whose own list is what that request walks)
    const std::vector<std::string>& depsOf(Project& pr, const std::string& gname)
    {
        auto it = pr.deps.find(lower(gname));
        if (it != pr.deps.end()) return it->second;
        std::vector<std::string>& v = pr.deps[lower(gname)];
        void* g = nullptr;
        if (!f.EventProject_GetGroup(pr.p, gname.c_str(), 0, &g) && g) collectDeps(g, v);
        return v;
    }

    void* load(const std::string& file, const char* group = nullptr, std::vector<std::string>* deps = nullptr)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        Project& pr = projects[lower(file)];
        std::string gname = group ? group : file.substr(0, file.rfind('.'));
        if (pr.p) {
            ++pr.refs;
            if (deps) *deps = depsOf(pr, gname);
            return pr.p;
        }
        FR r = f.EventSystem_Load(es, file.c_str(), nullptr, &pr.p);
        if (r || !pr.p) { AUDLOG("audio10: load %s failed (%d)", file.c_str(), r); projects.erase(lower(file)); return nullptr; }
        pr.refs = 1;
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
        const std::vector<std::string>& dv = depsOf(pr, gname);
        if (deps) *deps = dv;
        applyVolumes();   // the categories a project defines exist once it is loaded
        std::string dl;
        for (auto& d : dv) dl += (dl.empty() ? "" : ", ") + d;
        AUDLOG("audio10: loaded %s: %d events%s%s%s", file.c_str(), pr.events, found ? "" : " (no group named after it: 1.0 leaves it unregistered)",
               dl.empty() ? "" : "; Load: ", dl.c_str());
        return pr.p;
    }

    // a load before 3.0's dispatcher exists (plugin start: Initialize.fev:Permanent lists Dialog.fev, Reverbs.fev,
    // UI.fev, LoadScreen.fev, TOYBOX_TBScript.dlg): its '.fev' items load here, the rest is left to 3.0
    void loadTree(const std::string& file, const char* group, int depth = 0)
    {
        std::vector<std::string> deps;
        if (!load(file, group, &deps) || depth > 8) return;
        for (auto& d : deps) {
            size_t c = d.find(':');
            std::string fn = d.substr(0, c), n10 = fn.size() > 4 && !_stricmp(fn.c_str() + fn.size() - 4, ".fev") ? name10(fn) : "";
            if (n10.empty()) AUDLOG("audio10: %s -> %s left to 3.0", file.c_str(), d.c_str());
            else loadTree(n10, c == std::string::npos ? nullptr : d.c_str() + c + 1, depth + 1);
        }
    }

    void unload(const std::string& file, const char* group = nullptr, std::vector<std::string>* deps = nullptr)
    {
        std::vector<uint32_t> finished;
        {
            std::lock_guard<std::recursive_mutex> l(mx);
            auto it = projects.find(lower(file));
            if (it == projects.end() || !it->second.p) return;
            if (deps) *deps = depsOf(it->second, group ? group : file.substr(0, file.rfind('.')));
            if (--it->second.refs > 0) return;
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
        V3 p;
        {
            std::lock_guard<std::mutex> l(posMx);
            auto it = pos.find(obj);
            memcpy(p.v, it != pos.end() ? it->second.v : lpos, sizeof p.v);   // no position known: at the listener
        }
        f.Event_Set3DAttributes(ev, p.v, nullptr, nullptr);
    }

    // 1.0 FUN_01136520 without the per-call volume / min-max overrides (3.0's post carries none, so 1.0's defaults:
    // volume 1, distances left as authored): a 'virtual' event (entry flag 1) first gets its info-only instance
    // (FMOD_EVENT_INFOONLY) set to volume 1 and the sound's position, so FMOD's playback limit (MAX_PLAYBACKS_BEHAVIOR 5,
    // 'just fail if quietest', 0x011384a0's condition for these) judges the new sound where it will play (fire_loop,
    // fire_small_loop and 7 more of the 1,565 Metroville events); then getEventBySystemID (FMOD_EVENT_DEFAULT),
    // callback, 3D attributes, start.
    uint32_t play(uint32_t eventId, uint32_t obj, uint32_t flags, void* cb, void* cookie)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        auto it = byId.find(eventId);
        if (it == byId.end()) return 0;
        void* info = nullptr;
        if ((it->second.flags & 1) && !f.EventSystem_GetEventBySystemID(es, it->second.sysId, 4, &info) && info) {
            f.Event_SetVolume(info, 1.0f);
            place(info, obj);
        }
        void* ev = nullptr;
        FR r = f.EventSystem_GetEventBySystemID(es, it->second.sysId, 0, &ev);
        if (r || !ev) return 0;
        uint32_t id = newId();
        progSound[id & 4095] = nullptr;   // not a dialog line: no stale subsound for a SOUNDDEF_CREATE
        f.Event_SetCallback(ev, &Host::onEvent, (void*)(uintptr_t)id);
        place(ev, obj);
        if (f.Event_Start(ev)) { f.Event_SetCallback(ev, nullptr, nullptr); return 0; }
        plays[id] = Play{ev, it->second.proj, obj, eventId, flags, cb, cookie, it->second.ui, tickNo};
        return id;
    }

    // 1.0 FUN_01136850 (dialog line): the line's event with the bank's subsound as its programmer sound (1.0's
    // callback 0x01136440 answers SOUNDDEF_CREATE with it), 3D attributes, start. Its length goes out as AK_Duration.
    // A bank is one FSB (one stream for '_str'): 1.0 stops the speaker's current line before it seeks the bank to the
    // next (FUN_01135d60, then getSubSound); 3.0 FUN_01830370 posts the new line first and stops the old one after, so
    // the old line of the same bank is retired here (ended for 3.0, its later StopPlayingID finds no event).
    uint32_t playLine(uint32_t eventId, uint32_t obj, uint32_t flags, void* cb, void* cookie, void* fsb, int sub)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        auto it = byId.find(eventId);
        if (it == byId.end() || !fsb) return 0;
        for (auto& pl : plays)
            if (pl.second.fsb == fsb && pl.second.ev) {
                f.Event_SetCallback(pl.second.ev, nullptr, nullptr);
                f.Event_Stop(pl.second.ev, 1);
                pl.second.ev = nullptr;
                std::lock_guard<std::mutex> d(doneMx);
                done.push_back(pl.first);
            }
        void* s = nullptr;
        if (f.Sound_GetSubSound(fsb, sub, &s) || !s) return 0;
        void* ev = nullptr;
        if (f.EventSystem_GetEventBySystemID(es, it->second.sysId, 0, &ev) || !ev) return 0;
        uint32_t id = newId();
        progSound[id & 4095] = s;
        f.Event_SetCallback(ev, &Host::onEvent, (void*)(uintptr_t)id);
        place(ev, obj);
        if (f.Event_Start(ev)) { f.Event_SetCallback(ev, nullptr, nullptr); return 0; }
        plays[id] = Play{ev, it->second.proj, obj, eventId, flags, cb, cookie, it->second.ui, tickNo, fsb};
        unsigned ms = 0;
        if (!f.Sound_GetLength(s, &ms, 1)) {   // FMOD_TIMEUNIT_MS
            std::lock_guard<std::mutex> d(doneMx);
            durs.emplace_back(id, (float)ms);
        }
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
        if (action == 1 || action == 2) { if (it->second.ev) f.Event_SetPaused(it->second.ev, action == 1); return true; }
        it->second.stopReq = true;
        if (it->second.ev) f.Event_Stop(it->second.ev, 0);   // EVENTFINISHED arrives through the callback
        else { std::lock_guard<std::mutex> d(doneMx); done.push_back(id); }
        return true;
    }

    // the played time of one of our ids (FMOD_EVENT_INFO.positionms, info + 4), for Wwise's GetSourcePlayPosition
    bool position(uint32_t id, int* ms)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        auto it = plays.find(id);
        if (it == plays.end() || !it->second.ev) return false;
        uint8_t info[0x80] = {};
        if (f.Event_GetInfo(it->second.ev, nullptr, nullptr, info)) return false;
        *ms = *(int*)(info + 4);
        return true;
    }

    void stopObject(uint32_t obj)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        for (auto& pl : plays)
            if (pl.second.obj == obj && pl.second.ev) f.Event_Stop(pl.second.ev, 1);
        std::lock_guard<std::mutex> p(posMx);
        pos.erase(obj);
    }

    void pause(bool on, bool keepUi)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        for (auto& pl : plays)
            if (pl.second.ev && !(keepUi && pl.second.ui)) f.Event_SetPaused(pl.second.ev, on ? 1 : 0);
    }

    // one tick: listener and positions of live events, FMOD's update (fires the event callbacks), then the finished ids
    struct Len { uint32_t id; Play p; float ms; };
    void tick(std::vector<std::pair<uint32_t, Play>>& out, std::vector<Len>* lengths = nullptr)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        ++tickNo;
        float lp[3], lf[3], lu[3];
        bool ls;
        {
            std::lock_guard<std::mutex> p(posMx);
            memcpy(lp, lpos, sizeof lp); memcpy(lf, lfwd, sizeof lf); memcpy(lu, lup, sizeof lu);
            ls = lset;
        }
        if (ls) lres = f.EventSystem_Set3DListenerAttributes(es, 0, lp, nullptr, lf, lu);
        for (auto& pl : plays)
            if (pl.second.ev) place(pl.second.ev, pl.second.obj);
        f.EventSystem_Update(es);
        std::vector<uint32_t> ids, later;
        std::vector<std::pair<uint32_t, float>> ds;
        {
            std::lock_guard<std::mutex> d(doneMx);
            ids.swap(done);
            ds.swap(durs);
        }
        if (lengths)
            for (auto& d : ds) {
                auto it = plays.find(d.first);
                if (it != plays.end()) lengths->push_back(Len{d.first, it->second, d.second});
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

inline FR __stdcall Host::onEvent(void*, int type, void*, void* p2, void* ud)
{
    if (type == 3 || type == 4) {   // FMOD_EVENT_CALLBACKTYPE_STOLEN / EVENTFINISHED (1.0's callback 0x01136440 ends the sound on both)
        std::lock_guard<std::mutex> d(g.doneMx);
        g.done.push_back((uint32_t)(uintptr_t)ud);
    } else if (type == 6 && p2) {   // SOUNDDEF_CREATE: 1.0 0x011364cb hands back the line's subsound
        void* s = g.progSound[(uint32_t)(uintptr_t)ud & 4095];
        if (s) *(void**)p2 = s;
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

// ---- VO: 1.0 dialog banks and dialog scripts in 3.0's dialog system ----
// 3.0 kept 1.0's dialog design: a speaker registry (sound manager +0xd4, keyed by the line prefix, 3 bank slots,
// 1.0 FUN_00f75df0 / FUN_01137bb0 -> 3.0 FUN_013cc1f0 / FUN_018312b0) and a DialogueManager that resolves a line or
// named event through loaded .dlg scripts (1.0 FUN_00718650 -> 3.0 FUN_0073daf0). Only the containers changed:
// 1.0 '<Char>_str.fsb' + '.name' (MPEG, played as an FMOD programmer sound) became 3.0 '.dbnk' (Vorbis WEMs posted as
// Wwise external sources), 1.0 DLG v11 became 3.0 DLG v0x15. 3.0 dropped the story lines (inc_tbscript.dlg 'ED'
// covers 93..126; the Metroville missions use ED019..ED092), so the 1.0 banks join the registry in 3.0's layout and
// the 1.0 scripts are read through the 3.0 loader; the post of a 1.0 line is played by FMOD instead of Wwise.

inline uint32_t h131(const char* s, bool low)   // 1.0 FUN_0040bac0 (case kept), 3.0 FUN_0040bb80 (lower-cased)
{
    uint32_t h = 0;
    for (; *s; ++s) h = h * 131 + (uint32_t)(low ? tolower((uint8_t)*s) : (uint8_t)*s);
    return h;
}

inline bool slurp(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") || !fp) return false;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = n > 0 && fread(out.data(), 1, out.size(), fp) == out.size();
    fclose(fp);
    return ok;
}

// A 1.0 '.name' (read by 1.0 FUN_01132360): u16 4, char[2] prefix, u16 table length, u16 min, u16 max, u16 subsounds,
// u16 last line, u8 last variant, u8; u16 table[length] (variants << 12 | first subsound, 0xffff none) at 0x10, then
// u16 name offsets[subsounds], then the subsound names.
struct Name10 {
    const uint8_t* b = nullptr;
    size_t n = 0;
    uint16_t u16(size_t o) const { return o + 2 <= n ? *(const uint16_t*)(b + o) : 0xffff; }
    bool valid() const
    {
        if (n < 0x10 || u16(0) != 4) return false;
        uint16_t len = u16(4), lo = u16(6), hi = u16(8), subs = u16(0xa);
        if (hi < lo || hi - lo + 1 > len || 0x10 + (size_t)(len + subs) * 2 > n) return false;
        for (uint16_t i = 0; i < subs; ++i) {   // every name ends inside the file (all 916 1.0 .name files, every language)
            uint16_t o = u16(0x10 + (size_t)(len + i) * 2);
            if (o >= n || !memchr(b + o, 0, n - o)) return false;
        }
        for (uint32_t l = lo; l <= hi; ++l) {
            uint16_t v = u16(0x10 + (size_t)(l - lo) * 2);
            if (v != 0xffff && (v & 0xfff) + (v >> 12 > 1 ? (v >> 12) : 1) > subs) return false;
        }
        return true;
    }
    uint16_t prefix() const { return u16(2); }
    uint16_t lo() const { return u16(6); }
    uint16_t hi() const { return u16(8); }
    uint16_t subs() const { return u16(0xa); }
    uint16_t entry(uint32_t line) const { return line < lo() || line > hi() ? 0xffff : u16(0x10 + (size_t)(line - lo()) * 2); }
    const char* subName(uint32_t i) const { return (const char*)b + u16(0x10 + (size_t)(u16(4) + i) * 2); }
};

struct DlgBank {
    std::string file;                // as the 1.0 install spells it ('Edna_str.fsb')
    std::vector<uint8_t> name;       // its '.name'
    void* snd = nullptr;             // the FSB
    int refs = 0;
    Name10 idx() const { return Name10{name.data(), name.size()}; }
};

// The 3.0 bank layout FUN_0182bf80 / FUN_0182c000 / FUN_0182d2f0 read (DBNK header, 0x10 min, 0x12 max, 0x14 line
// count, 0x18 in-memory size; at 0x20 one u32 per line {last variant, variants, u16 first media} or -1, then 3 u32 per
// media {name offset, data offset, data size}). A 1.0 media's 'data' is {DlgBank*, subsound}: in memory, so the line
// is posted with that pointer as its external source, which the post hook recognises.
inline std::vector<uint8_t> dbnkFromName10(uint16_t prefix, const std::vector<DlgBank*>& banks)
{
    uint32_t lo = 0xffff, hi = 0, media = 0;
    for (DlgBank* b : banks) {
        Name10 x = b->idx();
        lo = (std::min)(lo, (uint32_t)x.lo());
        hi = (std::max)(hi, (uint32_t)x.hi());
        media += x.subs();
    }
    if (banks.empty() || hi < lo) return {};
    uint32_t lines = hi - lo + 1;
    size_t mediaAt = 0x20 + (size_t)lines * 4, recAt = mediaAt + (size_t)media * 12, namesAt = recAt + (size_t)media * 8;
    std::vector<uint8_t> h(namesAt, 0);
    memcpy(h.data(), "DBNK\x08\x02\x07\x00", 8);
    *(uint32_t*)(h.data() + 8) = prefix;
    *(uint16_t*)(h.data() + 0x10) = (uint16_t)lo;
    *(uint16_t*)(h.data() + 0x12) = (uint16_t)hi;
    *(uint16_t*)(h.data() + 0x14) = (uint16_t)lines;
    memset(h.data() + 0x20, 0xff, (size_t)lines * 4);
    uint32_t base = 0;
    for (DlgBank* b : banks) {   // a line in two banks of one speaker: the first loaded keeps it (1.0 tries slots in order)
        Name10 x = b->idx();
        for (uint32_t l = x.lo(); l <= x.hi(); ++l) {
            uint16_t v = x.entry(l);
            uint8_t* e = h.data() + 0x20 + (size_t)(l - lo) * 4;
            if (v == 0xffff || *(uint32_t*)e != 0xffffffffu) continue;
            e[0] = 0;
            e[1] = (uint8_t)(v >> 12 > 1 ? v >> 12 : 1);
            *(uint16_t*)(e + 2) = (uint16_t)(base + (v & 0xfff));
        }
        for (uint32_t i = 0; i < x.subs(); ++i) {
            uint32_t m = base + i;
            const char* nm = x.subName(i);
            uint32_t* me = (uint32_t*)(h.data() + mediaAt + (size_t)m * 12);
            me[0] = (uint32_t)h.size();
            me[1] = (uint32_t)(recAt + (size_t)m * 8);
            me[2] = 8;
            h.insert(h.end(), nm, nm + strlen(nm) + 1);
            uint32_t* rec = (uint32_t*)(h.data() + recAt + (size_t)m * 8);
            rec[0] = (uint32_t)(uintptr_t)b;
            rec[1] = i;
        }
        base += x.subs();
    }
    *(uint32_t*)(h.data() + 0x18) = (uint32_t)h.size();   // every data offset is below it: in memory (FUN_0182c000)
    return h;
}

// 1.0 DLG v11 (1.0 FUN_0071c020 / FUN_00718650) -> 3.0 DLG v0x15 (3.0 FUN_0073fb90 / FUN_0073daf0 / FUN_0073e080).
//  1.0: u32 11, u32 n, 8 bytes, n x (h131 case kept, offset); entry byte 0 = type:
//   0 line class {u8 0, u8 priority, u8 flags, u8, u32 event h131, u16 n, u16, n x (char[2] prefix, u16 number)}
//   1 variants   {16-byte header ([5] chance, [6] max plays, u16 [8] interval, [0xc] n, [0xe] max at once),
//                 n x (char[2] prefix, u16 number, u32 event)}
//   2 speaker    {16-byte header, n x (u32 event, char[2] speaker, u8 count, u8 last, u32 list offset), u16 lists}
//   3/4 once     {u8 type, u8 priority, u8 flags, ..., +0xc char[2] prefix, u16 number, +0x10 u32 event}
//  3.0: "DLG\0" 0x15 flags 2, u16 events, u16 entries, u16 ranges; +0x10/+0x18/+0x20 table offsets (8-byte slots);
//   events: (name offset, 0); entries: (h131 lower-cased, offset); ranges: (packed upper prefix, offset)
//   class {u8 0, u8 priority, u8 flags, u8, u8 event index, 3 x u8}; range {u16 min, u16 max, u8 default event,
//   u8 class index per line (0xff none)}; type 1 {16-byte header, n x (u32 prefix, u16 number, u8 event, u8)};
//   type 2 {16-byte header, n x (u32 list offset, u32, u32 speaker, u8 event, u8 count, u8 last, u8), u16 lists}.
//  A 1.0 line class becomes a class entry plus range bytes (3.0's form of the same per-line lookup); 'once' entries
//  (3.0 has no such type) become type 1 with one item, max plays 1 and max at once 1 (1.0 plays them while [3] == 0).
//  Named entries keep 1.0's case-kept hash (the request hook adds the lower-cased alias 3.0 looks up).
//  'priority': 1.0 writes 0..255 in tenths (0x1a, 0x33 .. 0x80 .. 0xff over the 22 1.0 scripts), 3.0 0..0x7f
//  (FUN_01830370 'pri_%u' = 0x40 - p; presentations pass the cap 0x7f to FUN_0073daf0). Same order, lower = more
//  important in both (1.0 FUN_01136850, 3.0 FUN_01830370 block a line while the speaker's current one is <= it); 18
//  of 28 twin entries of Inc_TBScript.dlg go 0x80 -> 0x40, so 3.0 = 1.0 >> 1.
//  base30: 3.0's own script of the same name (3.0 re-authored inc_tbscript.dlg for its Incredibles figures: 115 of its
//  named entries are not in 1.0's). Its entries and range lines that 1.0's script does not define are kept, so 3.0's
//  figures keep their lines; where both define one, 1.0's wins (1.0's playset loads 1.0's script). 3.0 type 3
//  {8-byte header ([4] n), n x (u32 prefix, u16 number, u16, u32 source name offset, u32)} keeps its names.
struct DlgStats { int classes = 0, lines = 0, named = 0, ranges = 0, events = 0, unresolved = 0, dupLines = 0, kept30 = 0, lines30 = 0; };
// A 1.0 type 3 / 4 entry of more than one line: {type, n x (char[2] prefix, u16 line, u32 event h131, u32 delay ms)}
// from +0xc, 12 bytes each, count u32 at +8 (Inc_TBScript INC_CIN_OpeningA: NC001+0 NC002+8150 MR021+5600 SY010+3800
// SY011+16390 SY012+8860 SY013+8400; 41 such entries over the 1.0 scripts, dlgtypes.py). 1.0 plays them in order
// (FUN_00718650 starts item 0; FUN_00715fc0 / FUN_00715d80 advance): type 4 counts each delay from the previous line's
// start, type 3 adds the previous line's length first (a gap after it ends). 3.0 has no such entry (its type 3 posts one
// Wwise event whose timing is authored in Wwise), so the script keeps item 0 for 3.0 and vo's sequencer plays the rest.
struct DlgSeqItem { uint16_t prefix, line; std::string event; uint32_t delay; };
struct DlgSeq { uint8_t type; std::vector<DlgSeqItem> items; };
inline bool dlgTranscode(const std::vector<uint8_t>& in, const std::unordered_map<uint32_t, std::string>& evNames,
                         std::vector<uint8_t>& out, DlgStats* st = nullptr, std::string* err = nullptr,
                         const std::vector<uint8_t>* base30 = nullptr, std::unordered_map<uint32_t, DlgSeq>* seqs = nullptr)
{
    auto fail = [&](const char* why) { if (err) *err = why; return false; };
    auto rd32 = [&](size_t o) { return o + 4 <= in.size() ? *(const uint32_t*)(in.data() + o) : 0u; };
    if (in.size() < 0x10 || rd32(0) != 11) return fail("not a v11 dlg");
    uint32_t n = rd32(4);
    if (0x10 + (size_t)n * 8 > in.size()) return fail("entry table past the end");
    DlgStats s;
    std::vector<std::string> events{"dialog_unassigned"};   // index 0: 3.0 'use the range default'; never used here
    std::map<std::string, uint8_t> evIdx;
    bool evOverflow = false;
    auto evName = [&](const std::string& nm) -> uint8_t {
        auto it = evIdx.find(nm);
        if (it != evIdx.end()) return it->second;
        if (events.size() > 255) { evOverflow = true; return 0; }
        events.push_back(nm);
        return evIdx[nm] = (uint8_t)(events.size() - 1);
    };
    auto ev = [&](uint32_t h) -> uint8_t {
        auto nm = evNames.find(h);
        char hex[16];
        if (nm == evNames.end()) { sprintf_s(hex, "%08x", h); if (!evIdx.count(hex)) ++s.unresolved; }
        return evName(nm != evNames.end() ? nm->second : hex);
    };
    // lists: blobs written after the entry, their absolute offset stored at the given position (rebased at load)
    struct Ent { uint32_t hash; std::vector<uint8_t> bytes; std::vector<std::pair<size_t, std::vector<uint8_t>>> lists; };
    std::vector<Ent> classes, named;
    std::map<uint32_t, std::map<uint16_t, uint8_t>> ranges;   // packed prefix -> number -> class index
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t hash = rd32(0x10 + (size_t)i * 8), off = rd32(0x14 + (size_t)i * 8);
        if (off + 0x10 > in.size()) return fail("entry past the end");
        const uint8_t* e = in.data() + off;
        Ent o{hash, {}, {}};
        if (e[0] == 0) {
            uint32_t cnt = *(const uint16_t*)(e + 8);
            if (off + 0xc + (size_t)cnt * 4 > in.size()) return fail("class past the end");
            if (classes.size() >= 255) return fail("more than 255 line classes");
            uint8_t ci = (uint8_t)classes.size();
            o.bytes = {0, (uint8_t)(e[1] >> 1), e[2], 0, ev(rd32(off + 4)), 0, 0, 0};
            for (uint32_t k = 0; k < cnt; ++k) {
                const uint8_t* li = e + 0xc + k * 4;
                uint32_t pfx = (uint32_t)(li[0] & 0xdf) | (uint32_t)(li[1] & 0xdf) << 8;   // FUN_0182bc20's key
                auto& slot = ranges[pfx];
                uint16_t num = *(const uint16_t*)(li + 2);
                if (slot.count(num)) ++s.dupLines;
                slot[num] = ci;
                ++s.lines;
            }
            classes.push_back(std::move(o));
            ++s.classes;
            continue;
        }
        o.bytes.assign(e, e + 0x10);
        uint8_t cnt = e[0xc];
        if (e[0] == 1) {
            if (off + 0x10 + (size_t)cnt * 8 > in.size()) return fail("variants past the end");
            for (uint8_t k = 0; k < cnt; ++k) {
                const uint8_t* it = e + 0x10 + k * 8;
                uint8_t item[8] = {it[0], it[1], 0, 0, it[2], it[3], ev(rd32(off + 0x14 + k * 8)), 0};
                o.bytes.insert(o.bytes.end(), item, item + 8);
            }
        } else if (e[0] == 2) {
            size_t lists = off + 0x10 + (size_t)cnt * 12;
            if (lists > in.size()) return fail("speakers past the end");
            for (uint8_t k = 0; k < cnt; ++k) {
                const uint8_t* sp = e + 0x10 + k * 12;
                uint8_t c = sp[6];
                size_t lo = lists + *(const uint32_t*)(sp + 8);
                if (lo + (size_t)c * 2 > in.size()) return fail("speaker list past the end");
                uint8_t sub[16] = {0, 0, 0, 0, 0, 0, 0, 0, sp[4], sp[5], 0, 0, ev(*(const uint32_t*)sp), c, 0, 0};
                o.lists.emplace_back(o.bytes.size(), std::vector<uint8_t>(in.data() + lo, in.data() + lo + (size_t)c * 2));
                o.bytes.insert(o.bytes.end(), sub, sub + 16);
            }
        } else if (e[0] == 3 || e[0] == 4) {
            if (off + 0x14 > in.size()) return fail("once past the end");
            uint32_t items = rd32(off + 8);
            if (seqs && items > 1 && items < 64 && off + 0xc + (size_t)items * 12 <= in.size()) {
                DlgSeq q{e[0], {}};
                for (uint32_t k = 0; k < items; ++k) {
                    const uint8_t* it = e + 0xc + k * 12;
                    auto nm = evNames.find(rd32(off + 0x10 + k * 12));
                    q.items.push_back({(uint16_t)(it[0] | it[1] << 8), *(const uint16_t*)(it + 2),
                                       nm == evNames.end() ? std::string() : nm->second, rd32(off + 0x14 + k * 12)});
                }
                (*seqs)[hash] = std::move(q);
            }
            uint8_t h[16] = {1, (uint8_t)(e[1] >> 1), e[2], 0, 0, 0xff, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0};
            o.bytes.assign(h, h + 16);
            uint8_t item[8] = {e[0xc], e[0xd], 0, 0, e[0xe], e[0xf], ev(rd32(off + 0x10)), 0};
            o.bytes.insert(o.bytes.end(), item, item + 8);
        } else
            return fail("unknown entry type");
        o.bytes[1] = (uint8_t)(e[1] >> 1);   // see 'priority' above
        o.bytes[3] = 0;   // plays at once: runtime state
        o.bytes[7] = 0;   // plays: runtime state
        named.push_back(std::move(o));
        ++s.named;
    }
    if (base30 && base30->size() >= 0x30 && !memcmp(base30->data(), "DLG\0\x15", 5)) {
        const std::vector<uint8_t>& t = *base30;
        auto t32 = [&](size_t o) { return o + 4 <= t.size() ? *(const uint32_t*)(t.data() + o) : 0u; };
        auto t16 = [&](size_t o) { return o + 2 <= t.size() ? *(const uint16_t*)(t.data() + o) : (uint16_t)0; };
        auto tstr = [&](size_t o) { return o < t.size() && memchr(t.data() + o, 0, t.size() - o) ? std::string((const char*)t.data() + o) : std::string(); };
        uint16_t ne3 = t16(6), nn3 = t16(8), nr3 = t16(10);
        uint32_t te = t32(0x10), tn = t32(0x18), tr = t32(0x20);
        if (te + ne3 * 8u > t.size() || tn + nn3 * 8u > t.size() || tr + nr3 * 8u > t.size()) return fail("3.0 script tables past the end");
        auto ev3 = [&](uint8_t i) { return i < ne3 ? evName(tstr(t32(te + i * 8u))) : (uint8_t)0; };
        std::unordered_set<uint32_t> named10;
        for (auto& e : named) named10.insert(e.hash);
        std::map<std::pair<uint32_t, uint8_t>, uint8_t> classOf;   // (3.0 class index, default event) -> merged class
        for (uint16_t r = 0; r < nr3; ++r) {
            uint32_t pfx = t32(tr + r * 8u), ro = t32(tr + r * 8u + 4);
            uint16_t lo = t16(ro), hi = t16(ro + 2);
            if (hi < lo || ro + 5u + (hi - lo) >= t.size()) return fail("3.0 range past the end");
            auto& slot = ranges[pfx];
            for (uint32_t num = lo; num <= hi; ++num) {
                uint8_t c = t[ro + 5 + (num - lo)];
                if (c == 0xff || slot.count((uint16_t)num)) continue;   // none, or 1.0 defines the line
                if (c >= nn3) return fail("3.0 range class past the entries");
                uint32_t co = t32(tn + c * 8u + 4);
                if (co + 8 > t.size() || t[co] != 0) return fail("3.0 range class is no class");
                uint8_t def = t[ro + 4];
                auto key = std::make_pair((uint32_t)c, t[co + 4] ? (uint8_t)0 : def);
                auto it = classOf.find(key);
                if (it == classOf.end()) {   // a class with event 0 takes its range's default (FUN_0073daf0): made explicit
                    if (classes.size() >= 255) return fail("more than 255 line classes");
                    Ent o{t32(tn + c * 8u), std::vector<uint8_t>(t.data() + co, t.data() + co + 8), {}};
                    o.bytes[4] = ev3(t[co + 4] ? t[co + 4] : def);
                    o.bytes[3] = 0;
                    it = classOf.emplace(key, (uint8_t)classes.size()).first;
                    classes.push_back(std::move(o));
                }
                slot[(uint16_t)num] = it->second;
                ++s.lines30;
            }
        }
        for (uint16_t i = 0; i < nn3; ++i) {
            uint32_t h = t32(tn + i * 8u), o = t32(tn + i * 8u + 4);
            if (o + 8 > t.size()) return fail("3.0 entry past the end");
            uint8_t type = t[o];
            if (type == 0 || named10.count(h)) continue;
            Ent e{h, {}, {}};
            if (type == 1 || type == 2) {
                uint8_t cnt = o + 0x10 <= t.size() ? t[o + 0xc] : 0;
                size_t len = 0x10 + (size_t)cnt * (type == 1 ? 8 : 16);
                if (o + len > t.size()) return fail("3.0 entry past the end");
                e.bytes.assign(t.data() + o, t.data() + o + len);
                for (uint8_t k = 0; k < cnt; ++k) {
                    if (type == 1) { e.bytes[0x10 + k * 8 + 6] = ev3(t[o + 0x10 + k * 8 + 6]); continue; }
                    size_t so = 0x10 + (size_t)k * 16;
                    uint32_t lp = t32(o + so);
                    uint8_t c = t[o + so + 0xd];
                    if (lp + c * 2u > t.size()) return fail("3.0 speaker list past the end");
                    e.bytes[so + 0xc] = ev3(t[o + so + 0xc]);
                    e.lists.emplace_back(so, std::vector<uint8_t>(t.data() + lp, t.data() + lp + c * 2u));
                }
            } else if (type == 3) {
                uint8_t cnt = t[o + 4];
                size_t len = 8 + (size_t)cnt * 16;
                if (o + len > t.size()) return fail("3.0 entry past the end");
                e.bytes.assign(t.data() + o, t.data() + o + len);
                for (uint8_t k = 0; k < cnt; ++k) {
                    std::string nm = tstr(t32(o + 8 + k * 16u + 8));
                    e.lists.emplace_back(8 + (size_t)k * 16 + 8, std::vector<uint8_t>(nm.c_str(), nm.c_str() + nm.size() + 1));
                }
            } else
                return fail("unknown 3.0 entry type");
            e.bytes[3] = 0;
            if (e.bytes.size() > 7 && type != 3) e.bytes[7] = 0;
            named.push_back(std::move(e));
            ++s.kept30;
        }
    }
    if (evOverflow) return fail("more than 255 events");
    // layout: header, the three tables, event names, entries (classes first: a range byte indexes them), ranges
    uint32_t ne = (uint32_t)events.size(), nn = (uint32_t)(classes.size() + named.size()), nr = (uint32_t)ranges.size();
    out.assign(0x30 + (size_t)(ne + nn + nr) * 8, 0);
    memcpy(out.data(), "DLG\0\x15\x02", 6);
    *(uint16_t*)(out.data() + 6) = (uint16_t)ne;
    *(uint16_t*)(out.data() + 8) = (uint16_t)nn;
    *(uint16_t*)(out.data() + 10) = (uint16_t)nr;
    uint32_t tEv = 0x30, tEn = tEv + ne * 8, tRa = tEn + nn * 8;
    *(uint32_t*)(out.data() + 0x10) = tEv;
    *(uint32_t*)(out.data() + 0x18) = tEn;
    *(uint32_t*)(out.data() + 0x20) = tRa;
    auto align = [&]() { while (out.size() & 3) out.push_back(0); };
    for (uint32_t i = 0; i < ne; ++i) {
        *(uint32_t*)(out.data() + tEv + i * 8) = (uint32_t)out.size();
        out.insert(out.end(), events[i].begin(), events[i].end());
        out.push_back(0);
    }
    uint32_t k = 0;
    for (auto* group : {&classes, &named})
        for (Ent& e : *group) {
            align();
            uint32_t at = (uint32_t)out.size();
            *(uint32_t*)(out.data() + tEn + k * 8) = e.hash;
            *(uint32_t*)(out.data() + tEn + k * 8 + 4) = at;
            out.insert(out.end(), e.bytes.begin(), e.bytes.end());
            for (auto& l : e.lists) {   // a speaker's number list, its absolute offset in the sub-entry (rebased at load)
                align();
                *(uint32_t*)(out.data() + at + l.first) = (uint32_t)out.size();
                out.insert(out.end(), l.second.begin(), l.second.end());
            }
            ++k;
        }
    k = 0;
    for (auto& r : ranges) {
        align();
        uint16_t lo = r.second.begin()->first, hi = r.second.rbegin()->first;
        *(uint32_t*)(out.data() + tRa + k * 8) = r.first;
        *(uint32_t*)(out.data() + tRa + k * 8 + 4) = (uint32_t)out.size();
        size_t at = out.size();
        out.resize(at + 5 + (hi - lo + 1), 0xff);
        *(uint16_t*)(out.data() + at) = lo;
        *(uint16_t*)(out.data() + at + 2) = hi;
        out[at + 4] = 0;
        for (auto& l : r.second) out[at + 5 + (l.first - lo)] = l.second;
        ++k;
    }
    s.ranges = (int)nr;
    s.events = (int)ne;
    if (st) *st = s;
    return true;
}

struct Vo {
    std::string root10;                                 // <1.0 install>\assets\sound\dialog
    std::string dir10;                                  // its language folder in use (ENU until the first bank)
    const char*(__cdecl* lang)() = nullptr;             // 3.0 FUN_00680c00: the 'LangHogFile' setting
    std::recursive_mutex mx;
    std::map<std::string, std::unique_ptr<DlgBank>> banks;   // lower-case file name; kept (posts hold their pointer)
    struct Speaker {
        std::vector<DlgBank*> banks;
        uint8_t* slot = nullptr;                        // the 3.0 bank object we put in the registry (0x50 bytes)
        std::vector<std::unique_ptr<std::vector<uint8_t>>> hdrs;   // current last; older ones stay valid
    };
    std::map<uint16_t, Speaker> speakers;
    std::unordered_map<uint32_t, std::string> evNames;  // h131 of every 1.0 FEV string (316 STRR tables)
    bool evNamesBuilt = false;
    std::unordered_set<const uint8_t*> scripts;         // transcoded scripts handed to 3.0's loader
    void* (__fastcall* mapFind)(void* map, void* edx, uint32_t key) = nullptr;               // FUN_013cc1f0
    void(__fastcall* mapAdd)(void* map, void* edx, uint32_t key, const uint32_t* rec) = nullptr;   // FUN_018308e0
    uint8_t* mgr = nullptr;                             // the sound manager (0x0228D668)

    // the language folder: 1.0 FUN_01137bb0 builds sound/dialog/<lang>/ from its 'LangHogFile' setting
    // (FUN_007185d0); 3.0 keeps the setting and the folder names (DED enu ESL FRF; 1.0 adds PTB, 3.0 NLN PL)
    void pickLanguage()
    {
        const char* l = lang ? lang() : nullptr;
        DWORD a = l && *l ? GetFileAttributesA((root10 + "\\" + l).c_str()) : INVALID_FILE_ATTRIBUTES;
        std::string want = root10 + "\\" + (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? l : "ENU");
        if (want != dir10) AUDLOG("vo10: 1.0 dialog language folder %s", want.c_str());
        dir10 = want;
    }

    // the bank's '<file>' and its '.name' from the 1.0 language folder; 1.0 FUN_01137bb0: '_str' streams (mode
    // 0x20002c1), else 0x2000241; the FSB must hold as many subsounds as the '.name' lists
    DlgBank* open(const std::string& file)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        if (dir10.empty()) pickLanguage();
        auto& slot = banks[lower(file)];
        if (!slot) { slot.reset(new DlgBank); slot->file = file; }
        DlgBank* b = slot.get();
        if (b->refs++ > 0 && b->snd) return b;
        std::string stem = file.substr(0, file.rfind('.'));
        if (!slurp(dir10 + "\\" + stem + ".name", b->name) || !b->idx().valid()) {
            AUDLOG("vo10: %s: no valid .name", file.c_str());
            --b->refs;
            return nullptr;
        }
        bool str = stem.size() > 4 && !_stricmp(stem.c_str() + stem.size() - 4, "_str");
        std::lock_guard<std::recursive_mutex> gl(g.mx);
        int subs = -1;
        if (g.f.System_CreateSound(g.sys, (dir10 + "\\" + file).c_str(), str ? 0x20002c1 : 0x2000241, nullptr, &b->snd) || !b->snd ||
            g.f.Sound_GetNumSubSounds(b->snd, &subs) || subs != b->idx().subs()) {
            AUDLOG("vo10: %s: FSB not opened or %d subsounds for %u names", file.c_str(), subs, b->idx().subs());
            if (b->snd) g.f.Sound_Release(b->snd);
            b->snd = nullptr;
            --b->refs;
            return nullptr;
        }
        AUDLOG("vo10: %s: %u subsounds, lines %c%c%03u..%03u", file.c_str(), subs, b->idx().prefix() & 0xff,
               b->idx().prefix() >> 8, b->idx().lo(), b->idx().hi());
        return b;
    }

    // 1.0 FUN_01137bb0's registration: the speaker of the bank's prefix gets the bank in a free slot (3.0's form:
    // FUN_018312b0, new speaker through FUN_018308e0). 3.0's own banks keep the lower slots (searched from the top),
    // so a line both games have stays Wwise's.
    void publish(uint16_t prefix)
    {
        Speaker& sp = speakers[prefix];
        void* map = mgr ? mgr + 0xd4 : nullptr;
        if (sp.banks.empty()) {
            if (map && sp.slot) {
                uint8_t* node = (uint8_t*)mapFind(map, nullptr, prefix);
                for (int i = 0; node && i < 3; ++i)
                    if (*(uint8_t**)(node + 0x14 + i * 4) == sp.slot) *(uint8_t**)(node + 0x14 + i * 4) = nullptr;
            }
            return;
        }
        sp.hdrs.emplace_back(new std::vector<uint8_t>(dbnkFromName10(prefix, sp.banks)));
        if (!map) return;   // octtest: no engine
        if (!sp.slot) {
            sp.slot = (uint8_t*)calloc(1, 0x50);
            *(uint32_t*)(sp.slot + 0x20) = 1;
            sprintf_s((char*)sp.slot + 0x2c, 0x20, "%c%c.fsb10", prefix & 0xff, prefix >> 8);
        }
        *(uint8_t**)(sp.slot + 0x28) = sp.hdrs.back()->data();
        uint8_t* node = (uint8_t*)mapFind(map, nullptr, prefix);
        if (!node) {
            uint32_t rec[5] = {0, 0, (uint32_t)(uintptr_t)sp.slot, 0, 0};
            mapAdd(map, nullptr, prefix, rec);
            AUDLOG("vo10: speaker %c%c registered with the 1.0 banks", prefix & 0xff, prefix >> 8);
            return;
        }
        for (int i = 0; i < 3; ++i)
            if (*(uint8_t**)(node + 0x14 + i * 4) == sp.slot) return;
        for (int i = 2; i >= 0; --i)
            if (!*(uint8_t**)(node + 0x14 + i * 4)) {
                *(uint8_t**)(node + 0x14 + i * 4) = sp.slot;
                AUDLOG("vo10: speaker %c%c: 1.0 banks in slot %d", prefix & 0xff, prefix >> 8, i);
                return;
            }
        AUDLOG("vo10: speaker %c%c has no free slot for the 1.0 banks", prefix & 0xff, prefix >> 8);
    }

    bool load(const std::string& file)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        DlgBank* b = open(file);
        if (!b) return false;
        uint16_t p = b->idx().prefix();
        auto& v = speakers[p].banks;
        if (std::find(v.begin(), v.end(), b) == v.end()) { v.push_back(b); publish(p); }
        return true;
    }

    void unload(const std::string& file)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        auto it = banks.find(lower(file));
        if (it == banks.end() || it->second->refs <= 0 || --it->second->refs > 0) return;
        DlgBank* b = it->second.get();
        uint16_t p = b->idx().prefix();
        auto& v = speakers[p].banks;
        v.erase(std::remove(v.begin(), v.end(), b), v.end());
        publish(p);
        std::lock_guard<std::recursive_mutex> gl(g.mx);
        for (auto& pl : g.plays)   // its subsounds die with it (EVENTFINISHED ends them for 3.0)
            if (pl.second.ev && pl.second.fsb == b->snd) g.f.Event_Stop(pl.second.ev, 1);
        g.f.Sound_Release(b->snd);
        b->snd = nullptr;
        AUDLOG("vo10: unloaded %s", file.c_str());
    }

    // 1.0 dialog sequences (DlgSeq, by the entry's case-kept h131): armed by the request hook, started when 3.0 posts
    // their first line (the item the script kept), then advanced by the host tick as 1.0's FUN_00715fc0 does
    std::unordered_map<uint32_t, DlgSeq> seqs;
    struct Armed { DlgSeq seq; DWORD at = 0; bool on = false; } armed;
    struct Run { DlgSeq seq; size_t next; DWORD prevStart, prevEnd; uint32_t prevPid, obj; };
    std::vector<Run> runs;

    // the line a {bank, subsound} plays: the .name line whose subsounds (first .. first + variants) hold it
    static int lineOf(DlgBank* b, int sub)
    {
        Name10 x = b->idx();
        for (uint32_t l = x.lo(); l <= x.hi(); ++l) {
            uint16_t v = x.entry(l);
            int first = v & 0xfff, nv = v >> 12 > 1 ? v >> 12 : 1;
            if (v != 0xffff && sub >= first && sub < first + nv) return (int)l;
        }
        return -1;
    }
    void onLinePosted(DlgBank* b, int sub, uint32_t obj, uint32_t pid)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        if (!armed.on || GetTickCount() - armed.at > 5000 || armed.seq.items.empty()) return;
        const DlgSeqItem& first = armed.seq.items[0];
        if (b->idx().prefix() != first.prefix || lineOf(b, sub) != first.line) return;
        DWORD now = GetTickCount();
        runs.push_back({armed.seq, 1, now, 0, pid, obj});
        armed.on = false;
        AUDLOG("vo10: sequence of %u lines started at %c%c%03u (1.0 type %u)", (unsigned)runs.back().seq.items.size(),
               first.prefix & 0xff, first.prefix >> 8, first.line, armed.seq.type);
    }
    uint32_t playSeqLine(const DlgSeqItem& it, uint32_t obj)
    {
        auto sp = speakers.find(it.prefix);
        for (DlgBank* b : sp == speakers.end() ? std::vector<DlgBank*>() : sp->second.banks) {
            uint16_t v = b->idx().entry(it.line);
            if (v == 0xffff || !b->snd || it.event.empty()) continue;
            int nv = v >> 12 > 1 ? v >> 12 : 1;
            uint32_t id = g.playLine(fnv1(it.event), obj, 0, nullptr, nullptr, b->snd, (v & 0xfff) + (nv > 1 ? rand() % nv : 0));
            AUDLOG("vo10: sequence line %c%c%03u -> %s", it.prefix & 0xff, it.prefix >> 8, it.line, id ? "1.0 FMOD dialog" : "not played");
            return id;
        }
        AUDLOG("vo10: sequence line %c%c%03u: no loaded 1.0 bank holds it", it.prefix & 0xff, it.prefix >> 8, it.line);
        return 0;
    }
    // per host tick: type 4 starts each line its delay after the previous one started; type 3 its delay after the
    // previous one ended (1.0 FUN_00715d80 adds the line's length when it starts)
    void seqTick()
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        DWORD now = GetTickCount();
        for (size_t r = 0; r < runs.size();) {
            Run& u = runs[r];
            const DlgSeqItem& it = u.seq.items[u.next];
            bool due = false;
            if (u.seq.type == 4) due = now - u.prevStart >= it.delay;
            else {
                if (!u.prevEnd) {
                    std::lock_guard<std::recursive_mutex> gl(g.mx);
                    if (!g.plays.count(u.prevPid)) u.prevEnd = now;
                }
                due = u.prevEnd && now - u.prevEnd >= it.delay;
            }
            if (due) {
                u.prevPid = playSeqLine(it, u.obj);
                u.prevStart = u.seq.type == 4 ? u.prevStart + it.delay : now;
                u.prevEnd = 0;
                if (++u.next >= u.seq.items.size()) { runs.erase(runs.begin() + r); continue; }
            }
            ++r;
        }
    }

    // the {DlgBank*, subsound} record an external source points at, if it is one of ours
    bool media(const void* p, DlgBank** bank, int* sub)
    {
        std::lock_guard<std::recursive_mutex> l(mx);
        for (auto& sp : speakers)
            for (auto& h : sp.second.hdrs) {
                const uint8_t* b = h->data();
                if (p < b || p >= b + h->size()) continue;
                *bank = *(DlgBank* const*)p;
                *sub = ((const int*)p)[1];
                return *bank && (*bank)->snd;
            }
        return false;
    }

    // every string of every 1.0 FEV ('STRR': u32 n, n offsets, strings), hashed as 1.0's dialog scripts name events
    void buildEvNames(const std::string& sound10)
    {
        if (evNamesBuilt) return;
        evNamesBuilt = true;
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((sound10 + "\\*.fev").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        std::vector<uint8_t> d;
        do {
            if (!slurp(sound10 + "\\" + fd.cFileName, d)) continue;
            for (size_t i = 0; i + 12 <= d.size(); ++i) {
                if (memcmp(d.data() + i, "STRR", 4)) continue;
                uint32_t len = *(uint32_t*)(d.data() + i + 4);
                const uint8_t* s = d.data() + i + 8;
                if (i + 8 + (size_t)len > d.size() || len < 4) break;
                uint32_t cnt = *(const uint32_t*)s;
                if (4 + (size_t)cnt * 4 > len) break;
                for (uint32_t k = 0; k < cnt; ++k) {
                    uint32_t o = 4 + cnt * 4 + ((const uint32_t*)(s + 4))[k];
                    if (o >= len || !memchr(s + o, 0, len - o)) continue;
                    evNames.emplace(h131((const char*)s + o, false), (const char*)s + o);
                }
                break;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        AUDLOG("vo10: %u FEV strings hashed for the 1.0 dialog scripts", (unsigned)evNames.size());
    }
};
inline Vo vo;

// Lipsync of a 1.0 line. 3.0 FUN_014c5550 opens 'sound/dialog/<lang>/<letters>/<media name>.anno' with the bank's media
// name (3.0 'MI0024-1' -> enu/mi/mi0024-1.anno); for a 1.0 line that name is the 1.0 subsound name, which is also what
// 1.0 named its .anno (FUN_00fb8e30: sound/dialog/<lang>/<character>/<subsound>.anno), kept in 1.0's
// sound/dialog/ENU/<Character>.zip (Edna.zip: ed019_02_kpr-zo55_01-02.anno). The character is the speaker's 1.0 bank's.
inline bool annoSource(const char* path, std::string& zipRel, std::string& member)
{
    std::string p = path ? lower(path) : "";
    for (char& c : p) if (c == '\\') c = '/';
    if (p.rfind("sound/dialog/", 0) || p.size() < 18 || p.compare(p.size() - 5, 5, ".anno")) return false;
    size_t a = p.find('/', 13), b = a == std::string::npos ? a : p.find('/', a + 1);
    if (b == std::string::npos || b - a < 3 || p.find('/', b + 1) != std::string::npos) return false;
    uint16_t prefix = (uint16_t)(toupper((uint8_t)p[a + 1]) | toupper((uint8_t)p[a + 2]) << 8);
    std::lock_guard<std::recursive_mutex> l(vo.mx);
    auto it = vo.speakers.find(prefix);
    if (it == vo.speakers.end() || it->second.banks.empty()) return false;
    std::string c = it->second.banks.front()->file;
    c = c.substr(0, c.rfind('.'));
    if (c.size() > 4 && (!_stricmp(c.c_str() + c.size() - 4, "_str") || !_stricmp(c.c_str() + c.size() - 4, "_ram"))) c.resize(c.size() - 4);
    zipRel = "sound/dialog/" + vo.dir10.substr(vo.dir10.rfind('\\') + 1) + "/" + c + ".zip";
    member = p.substr(b + 1);
    return true;
}

// ---- engine glue ----
struct Cfg {
    bool on = true;       // audio10: the FMOD host and the Wwise-seam mirroring
    bool music = true;    // music10: 1.0 music through the MusicStack
    bool vo = true;       // vo10: 1.0 dialog banks and scripts in 3.0's dialog system
    bool trace = false;   // audiotrace10: log each posted name once with the backend that took it
    bool mixer = true;    // mixer10: 1.0's Mixer presets (GameDB/Core/Mixer.lua) as the 'Mixer' source of the 1.0 categories
};
inline Cfg cfg;
inline std::string sound30;   // <3.0 install>\assets\sound
inline bool ready();

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
    if (cat && src && ready()) {
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
    if (!file || !*file || !ready()) return r;
    std::string f = file;
    size_t dot = f.rfind('.');
    if (dot == std::string::npos) return r;
    std::string stem = f.substr(0, dot), ext = lower(f.substr(dot));
    bool load = req[0] == 0;
    if (cfg.vo && ext == ".fsb") {   // 1.0's '.fsb' handler (FUN_01137bb0; 3.0 registers none): a dialog bank
        if (load) { if (vo.load(f)) r = 1; }
        else vo.unload(f);
        return r;
    }
    std::string group;   // 'x.fev:Group' (1.0 request form; a 'Load' item may carry one)
    size_t colon = ext.find(':');
    if (colon != std::string::npos) { group = ext.substr(colon + 1); ext.resize(colon); }
    if (ext != ".bnk" && ext != ".fev") return r;
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
    std::vector<std::string> deps;
    const char* gp = group.empty() ? nullptr : group.c_str();
    if (load) { if (g.load(fev, gp, &deps) && ext == ".fev") r = 1; }
    else g.unload(fev, gp, &deps);
    static thread_local int depth = 0;   // ponytail: 1.0 has no guard; no 1.0 'Load' chain is deeper than 3
    if (depth > 8) { AUDLOG("audio10: 'Load' chain too deep at %s", fev.c_str()); return r; }
    ++depth;
    for (auto& d : deps) {   // 1.0 FUN_01134440: each item through the dispatcher with the request's type ('%s -> %s')
        std::string desc = fev + " -> " + d;
        uint32_t dreq[5] = {req[0], req[1], (uint32_t)(uintptr_t)d.c_str(), req[3], (uint32_t)(uintptr_t)desc.c_str()};
        hkBankReq(mgr, nullptr, dreq);
    }
    --depth;
    return r;
}

// FUN_01948a10: Wwise PostEvent(id, gameObj, flags, callback, cookie, externals, playingId) -> playing id, 0 = unknown
inline uint32_t __cdecl hkPost(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, void* ext, uint32_t pid)
{
    uint32_t r = oPost(ev, obj, flags, cb, cookie, ext, pid);
    if (!ready()) return r;
    if (ev == idPauseAll || ev == idPauseExUi || ev == idResumeAll) {   // 3.0 FUN_0073f900
        g.pause(ev != idResumeAll, ev == idPauseExUi);
        if (ev != idPauseExUi) { std::lock_guard<std::recursive_mutex> l(g.mx); g.f.MusicSystem_SetPaused(g.ms, ev == idPauseAll); }
    }
    if (r || ext) return r;   // a dialog line with external sources is 3.0's own (VO: see vo10)
    std::lock_guard<std::recursive_mutex> l(g.mx);   // music.playset changes on the bank dispatcher's thread
    if (cfg.music && !music.playset.empty()) {
        if (ev == fnv1("StartMusic_" + music.playset)) {   // 3.0 FUN_0073eb30 starts its playset's music
            if (!music.startId) music.startId = g.standIn(ev, obj, flags, cb, cookie);
            return music.startId;
        }
        if (ev == fnv1("StopMusic_" + music.playset)) {   // 1.0 resets the MusicSystem before switching (FUN_00715650)
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
        std::lock_guard<std::mutex> l(g.posMx);
        V3& v = g.pos[obj];
        v.v[0] = p[0]; v.v[1] = p[1]; v.v[2] = -p[2];
    }
    return oSetPos(obj, p);
}

// FUN_01947150: SetListenerPosition(AkListenerPosition {front, top, position}, index); z negated by FUN_0182d340
inline int __cdecl hkListener(const float* p, uint32_t index)
{
    if (p && index == 0) {
        std::lock_guard<std::mutex> l(g.posMx);
        for (int i = 0; i < 3; ++i) {
            float s = i == 2 ? -1.0f : 1.0f;
            g.lfwd[i] = p[i] * s; g.lup[i] = p[3 + i] * s; g.lpos[i] = p[6 + i] * s;
        }
        g.lset = true;
    }
    return oListener(p, index);
}

// FUN_019475a0: GetSourcePlayPosition(id, AkTimeMs*, extrapolate) -> 1 AK_Success. 3.0's lipsync player
// (FUN_014cb180 -> FUN_0073a810 -> FUN_0182c0a0) advances a line's .anno segments by this position and stays on the
// first one when it fails, so a 1.0 line's mouth only moves if our id answers with the FMOD instance's played time.
typedef int(__cdecl* SrcPosFn)(uint32_t pid, int* pos, uint8_t extrapolate);
inline SrcPosFn oSrcPos;
inline int __cdecl hkSrcPos(uint32_t pid, int* pos, uint8_t extrapolate)
{
    int ms = 0;
    if (pos && g.position(pid, &ms)) { *pos = ms; return 1; }
    return oSrcPos(pid, pos, extrapolate);
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
    if (!group || !state || _strnicmp(group, "Music_", 6) || !ready()) return r;
    std::string x = group + 6;
    // A 1.0-hosted playset has no MusicStates.bin, and with no table loaded FUN_007397b0 accepts every cue (+0x14 null
    // -> 1, 0x00739837), so its 1 is no answer for such a group: the music is 1.0's either way (in-game 2026-10-14:
    // Inc_mx_in_city / Inc_mx_opening_cinematic pushed, Music_Inc.fev never loaded, silence).
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
    if (group && state && !_strnicmp(group, "Music_", 6)) {
        std::lock_guard<std::recursive_mutex> l(g.mx);   // music.playset changes on the bank dispatcher's thread
        if (!music.playset.empty() && !_stricmp(group + 6, music.playset.c_str())) {
            if (!_stricmp(state, "None")) music.endPrompt();
            else AUDLOG("audio10: music cue %s %s", state, music.begin(state) ? "prompted" : "failed");
        }
    }
    return oSetState(group, state);
}

// FUN_0073ed10 (thiscall on the music manager, 3 args (name, priority, string), ret 0xc): 3.0's 'play once' (the
// SoundMusicCue node's Once input) pushes the cue for a fixed 3 s (FUN_0073c5c0 arg 0x40400000), which a Wwise state
// change only needs to start a stinger. 1.0's counterpart FUN_00716ac0 -> FUN_01133610 -> FUN_01132b60 pushes nothing:
// it finds the cue (MusicSystem vtable +0x38 getCues) and calls +0x28, MusicSystem::promptCue -- FMOD plays it once
// and returns to the music under it. For a 1.0-hosted playset the cue goes the 1.0 way (in-game 2026-10-14:
// Inc_mx_opening_cinematic, a looping cue still active after 100 s offline, was cut back to Inc_mx_in_city at 3 s).
typedef uint32_t(__fastcall* MusicOnceFn)(void* self, void* edx, const char* name, int prio, const char* s);
inline MusicOnceFn oMusicOnce;
inline uint32_t __fastcall hkMusicOnce(void* self, void*, const char* name, int prio, const char* s)
{
    const char* u = name ? strchr(name, '_') : nullptr;
    if (u && ready()) {
        std::string x(name, u - name);
        if (hostedMusic(x) && music.ensure(x)) {
            std::lock_guard<std::recursive_mutex> l(g.mx);
            unsigned id = 0;
            bool ok = Music::cue(name, &id) && !g.f.MusicSystem_PromptCue(g.ms, id);
            AUDLOG("audio10: music cue %s %s", name, ok ? "prompted once (1.0 promptCue)" : "not in the 1.0 project");
            if (ok) return 0;
        }
    }
    return oMusicOnce(self, nullptr, name, prio, s);
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
        std::unique_lock<std::recursive_mutex> l(g.mx, std::try_to_lock);   // per frame: never wait out FMOD's update
        if (!l || !music.prompt) return;
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
        if (ours) {   // where a 1.0 sound plays against the listener, first 4 posts of each name (blast sounds inaudible?)
            static std::unordered_map<uint32_t, int> seen;
            std::lock_guard<std::recursive_mutex> l(g.mx);
            auto pl = g.plays.find((uint32_t)r);
            if (pl != g.plays.end() && seen[id]++ < 4) {
                std::lock_guard<std::mutex> p(g.posMx);
                auto it = g.pos.find(pl->second.obj);
                const float* v = it != g.pos.end() ? it->second.v : g.lpos;
                float dx = v[0] - g.lpos[0], dy = v[1] - g.lpos[1], dz = v[2] - g.lpos[2];
                AUDLOG("audio10: %s plays on obj %u at (%.1f %.1f %.1f)%s, %.1f from the listener (%.1f %.1f %.1f)", name,
                       pl->second.obj, v[0], v[1], v[2], it != g.pos.end() ? "" : " (no position: the listener's)",
                       sqrtf(dx * dx + dy * dy + dz * dz), g.lpos[0], g.lpos[1], g.lpos[2]);
                // the mix it plays in: FMOD mode (0x10 3D, 0x8 2D, 0x40000 head-relative), category volumes, and whether
                // FMOD took the listener (fwd / up must be unit and perpendicular)
                unsigned mode = 0;
                std::string chain;
                if (void* ev = pl->second.ev) {
                    g.f.Event_GetPropertyByIndex(ev, 14, &mode, 0);   // FMOD_EVENTPROPERTY_MODE
                    void* c = nullptr;
                    g.f.Event_GetCategory(ev, &c);
                    for (int d = 0; c && d < 8; ++d) {
                        char* cn = nullptr;
                        float cv = -1;
                        g.f.EventCategory_GetInfo(c, nullptr, &cn);
                        g.f.EventCategory_GetVolume(c, &cv);
                        char one[80];
                        sprintf_s(one, "%s%s %.3f", chain.empty() ? "" : " < ", cn ? cn : "?", cv);
                        chain += one;
                        void* parent = nullptr;
                        if (g.f.EventCategory_GetParentCategory(c, &parent)) break;
                        c = parent;
                    }
                }
                const float *lf = g.lfwd, *lu = g.lup;
                AUDLOG("audio10: %s mode %x, categories %s; listener result %d, fwd (%.3f %.3f %.3f) up (%.3f %.3f %.3f) "
                       "|fwd| %.3f |up| %.3f fwd.up %.3f", name, mode, chain.c_str(), (int)g.lres, lf[0], lf[1], lf[2], lu[0],
                       lu[1], lu[2], sqrtf(lf[0] * lf[0] + lf[1] * lf[1] + lf[2] * lf[2]),
                       sqrtf(lu[0] * lu[0] + lu[1] * lu[1] + lu[2] * lu[2]), lf[0] * lu[0] + lf[1] * lu[1] + lf[2] * lu[2]);
            }
        }
    }
    return r;
}

// FUN_01948d80: PostEvent(id, gameObj, flags, callback, cookie, externals count, AkExternalSourceInfo[] {cookie,
// codec, file, memory, size, file id}, playingId); 3.0 FUN_01830370 posts every dialog line through it. A line whose
// in-memory source is a 1.0 bank record plays as 1.0 played it; 3.0's bookkeeping (speaker, priority, callback
// record) is already done by the caller.
typedef uint32_t(__cdecl* PostExtFn)(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, uint32_t n,
                                     const uint32_t* ext, uint32_t pid);
inline PostExtFn oPostExt;
inline uint32_t __cdecl hkPostExt(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, uint32_t n,
                                  const uint32_t* ext, uint32_t pid)
{
    DlgBank* b = nullptr;
    int sub = 0;
    std::lock_guard<std::recursive_mutex> l(vo.mx);   // the bank's FSB stays open through playLine (Vo::unload: vo.mx, g.mx)
    if (n == 1 && ext && ext[3] && vo.media((const void*)(uintptr_t)ext[3], &b, &sub)) {
        uint32_t id = g.playLine(ev, obj, flags, cb, cookie, b->snd, sub);
        Name10 x = b->idx();
        trace(ev ^ (uint32_t)sub, x.subName(sub), id ? "1.0 FMOD dialog" : "nothing (its 1.0 event is not loaded)");
        if (id) vo.onLinePosted(b, sub, obj, id);   // the first line of an armed 1.0 sequence starts the rest
        return id;
    }
    return oPostExt(ev, obj, flags, cb, cookie, n, ext, pid);
}

// FUN_0182f570 (cdecl): a sound file read whole into the engine heap (FUN_00409e90; freed by FUN_00409ed0), 3.0's
// DialogueManager reads 'dialog/<script>' through it (0x00740a37). A script the 1.0 install has as DLG v11 is read
// from there through dlgTranscode (the 1.0 playset loads its own scripts: Audio|Inc_TBScript.dlg).
typedef void*(__cdecl* LoadFileFn)(const char* path, uint32_t* size);
typedef void*(__cdecl* AllocFn)(uint32_t size, char flag, uint32_t* got, uint32_t align);
typedef void(__cdecl* FreeFn)(void* p);
inline LoadFileFn oLoadFile;
inline AllocFn engineAlloc;
inline FreeFn engineFree;
inline void* __cdecl hkLoadFile(const char* path, uint32_t* size)
{
    size_t n = path ? strlen(path) : 0;
    std::vector<uint8_t> in;
    if (n <= 11 || _strnicmp(path, "dialog/", 7) || _stricmp(path + n - 4, ".dlg") || !ready() ||
        !slurp(vo.root10 + "\\" + (path + 7), in) || in.size() < 4 || *(uint32_t*)in.data() != 11)
        return oLoadFile(path, size);
    uint32_t n30 = 0;
    uint8_t* d30 = (uint8_t*)oLoadFile(path, &n30);   // 3.0's own script of that name, if any (its entries are kept)
    std::vector<uint8_t> b30, out;
    if (d30) b30.assign(d30, d30 + n30);
    DlgStats st;
    std::string err;
    std::lock_guard<std::recursive_mutex> l(vo.mx);
    vo.buildEvNames(g.sound10);
    uint8_t* p = nullptr;
    if (dlgTranscode(in, vo.evNames, out, &st, &err, d30 ? &b30 : nullptr, &vo.seqs)) {
        uint32_t got = 0;
        p = (uint8_t*)engineAlloc((uint32_t)out.size(), 0, &got, 0xffffffffu);
    } else
        AUDLOG("vo10: %s: 1.0 script not taken (%s)", path, err.c_str());
    if (!p) {
        if (size) *size = n30;
        return d30;
    }
    memcpy(p, out.data(), out.size());
    if (size) *size = (uint32_t)out.size();
    if (d30) engineFree(d30);
    vo.scripts.insert(p);
    AUDLOG("vo10: %s from 1.0: %d line classes (%d lines in %d ranges), %d named, %d events (%d unresolved); 3.0's kept: %d named, %d lines",
           path, st.classes, st.lines, st.ranges, st.named, st.events, st.unresolved, st.kept30, st.lines30);
    return p;
}

// FUN_0073bf50 (thiscall on the DialogueManager's request pool, its +0xa4; ret 8): a dialog request for {name, ...}.
// 3.0 looks named entries up by the lower-cased h131 (FUN_0073daf0: FUN_0040bb80), 1.0 by the name as written
// (FUN_0040bac0); a 1.0 script only has the latter, so the first request of a name with capitals adds the lower-cased
// key to that script (FUN_006eaa00). The scripts hang off the DialogueManager 0x02204128 (+0x48, FUN_0073f990 ->
// FUN_0073f550 -> FUN_0073daf0 walk them there).
typedef void*(__fastcall* DlgReqFn)(void* pool, void* edx, const char** nameRec, uint32_t a2);
typedef void(__fastcall* MapPutFn)(void* map, void* edx, uint32_t key, void* const* value);
inline DlgReqFn oDlgReq;
inline MapPutFn mapPut;
inline uint8_t* dialogMgr;
inline void* __fastcall hkDlgReq(void* pool, void*, const char** nameRec, uint32_t a2)
{
    const char* name = nameRec ? *nameRec : nullptr;
    uint8_t* dm = dialogMgr;
    if (name) {   // a 1.0 sequence entry of that name is armed: its first line's post starts the rest (Vo::onLinePosted)
        uint32_t cs = h131(name, false);
        std::lock_guard<std::recursive_mutex> l(vo.mx);
        auto s = vo.seqs.find(cs);
        if (s != vo.seqs.end()) vo.armed = {s->second, GetTickCount(), true};
    }
    if (name && dm) {
        uint32_t cs = h131(name, false), lo = h131(name, true);
        if (cs != lo) {
            // the DialogueManager's own lock, which FUN_0073daf0 holds while it looks names up in these maps
            CRITICAL_SECTION* dmLock = (CRITICAL_SECTION*)(dm + 0x58);
            EnterCriticalSection(dmLock);
            {
                std::lock_guard<std::recursive_mutex> l(vo.mx);
                for (uint8_t* s = *(uint8_t**)((uint8_t*)dm + 0x48); s; s = *(uint8_t**)(s + 4)) {
                    if (!vo.scripts.count(*(uint8_t**)(s + 0x7c)) || vo.mapFind(s + 8, nullptr, lo)) continue;
                    uint8_t* node = (uint8_t*)vo.mapFind(s + 8, nullptr, cs);
                    if (!node) continue;
                    void* entry = *(void**)(node + 0xc);
                    mapPut(s + 8, nullptr, lo, &entry);
                    AUDLOG("vo10: '%s' found in a 1.0 script by its 1.0 key", name);
                }
            }
            LeaveCriticalSection(dmLock);
        }
    }
    return oDlgReq(pool, nullptr, nameRec, a2);
}

// 1.0 ChoreographyInstance::SafeAudioSync (FUN_00ecabc0 -> FUN_00ec83c0, with the leaf name of the choreography path)
// starts two sounds of that name and plays the choreography once both are ready:
// - the dialog entry (FUN_0071b450 -> FUN_00718650, a type 3/4 line sequence: Vo);
// - the sound event (FUN_0071b530 -> FUN_00717670 -> FUN_011371e0). For example in_city.fev 'INC_CIN_OpeningA' is the
//   opening cinematic's effects track: 26 s, sound from 4.5 s (octtest --fmod EV_RUN, EV_SECS=30).
// 3.0's audio sync (instance flag 0x20, enableAudioSync) requests only the dialog, because a 3.0 cinematic carries
// one mixed track. So a 1.0 event of the choreography's name starts here, as the choreography starts. It plays at
// the listener: 1.0's request carries the dialog's position, and a cinematic has none.
// ponytail: the event plays out its own length, and 1.0's MixerPreset with it (FUN_00714180) is not applied. 1.0 also
// stops the event with its request. Add both if a skippable or ducked 1.0 cinematic shows up.
inline void choreographyStarted(const char* path, uint32_t flags)
{
    if (!(flags & 0x20) || !path || !*path || !ready()) return;
    const char* leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    uint32_t ev = fnv1(leaf);
    {
        std::lock_guard<std::recursive_mutex> l(g.mx);
        if (!g.byId.count(ev)) return;   // no 1.0 event of that name (every 3.0 choreography, most 1.0 ones)
    }
    uint32_t id = g.play(ev, 0, 0, nullptr, nullptr);
    AUDLOG("audio10: %s (audio sync) starts the 1.0 sound event of its name -> %s", leaf, id ? "1.0 FMOD" : "not played");
}

// one tick plus the end-of-event callbacks of what finished, outside every lock
inline int tickAndDeliver()
{
    std::vector<std::pair<uint32_t, Play>> fin;
    std::vector<Host::Len> lens;
    g.tick(fin, &lens);
    for (auto& d : lens) {
        if (!(d.p.flags & 8) || !d.p.cb) continue;   // AK_Duration requested (3.0 FUN_01830370 asks it for dialog lines)
        struct { void* cookie; uint32_t obj, pid, ev; float dur, est; uint32_t node, media; uint8_t streaming; } info =
            {d.p.cookie, d.p.obj, d.id, d.p.eventId, d.ms, d.ms, 0, 0, 1};   // AkDurationCallbackInfo (3.0 LAB_01830120 type 8)
        ((AkCallback)d.p.cb)(8, &info);
    }
    if (cfg.trace)   // how long each 1.0 sound played and what ended it, first 4 ends of each name (blasts inaudible?)
        for (auto& d : fin) {
            static std::unordered_map<uint32_t, int> seen;
            if (d.second.fsb || seen[d.second.eventId]++ >= 4) continue;
            std::string n;
            {
                std::lock_guard<std::recursive_mutex> l(g.mx);
                auto e = g.byId.find(d.second.eventId);
                if (e != g.byId.end()) n = e->second.name;
            }
            AUDLOG("audio10: %s ended after %lu ms (%s)", n.c_str(), GetTickCount() - d.second.t0,
                   d.second.stopReq ? "stopped by 3.0" : d.second.ev ? "finished or stolen" : "retired");
        }
    for (auto& d : fin) {
        if (!(d.second.flags & 1) || !d.second.cb) continue;   // AK_EndOfEvent requested
        struct { void* cookie; uint32_t obj, pid, ev; } info = {d.second.cookie, d.second.obj, d.first, d.second.eventId};
        ((AkCallback)d.second.cb)(1, &info);   // AkEventCallbackInfo, as Wwise delivers it (3.0 LAB_01830120)
    }
    vo.seqTick();   // 1.0 dialog sequences: the next line when due
    return (int)fin.size();
}

// The FMOD host's own thread: FMOD's init runs here, then the tick. fmodex.dll imports CoInitialize (STA) while 3.0
// initialises COM itself (CoInitializeEx MTA at 0x0132e41b / 0x0197106a, STA at 0x0198261f / 0x004403d6), so FMOD
// never touches the apartment of the thread that runs Pentane_Main.
// Pentane_Main runs inside Pentane.dll's DllMain (DLL_PROCESS_ATTACH at Pentane 0x101c2c8e -> 0x101df000, which calls
// each plugin record's +0x60 entry), i.e. under the loader lock, where a new thread cannot start until DllMain returns:
// install never waits for this thread; the first hook that needs FMOD does (ready()), on a game thread.
struct HostStart { std::string install10; bool nosound = false, ok = false; HANDLE done = nullptr; };   // nosound: octtest
inline HostStart hostStart;
inline volatile LONG hostState = 0;   // 0 starting, 1 up, -1 failed

inline void loadMixer(const std::string& install10)
{
    std::string core, lua, err;
    std::vector<std::string> paths;
    MixerRest rest;
    if (readRoot10(install10, "GameDB/Core/CoreDataMap.lua", core) && !(paths = mixerPaths(core)).empty() &&
        readRoot10(install10, paths[0], lua) && mixerRest(lua, rest, &err)) {
        std::string a, v;
        for (auto& s : rest.active) a += (a.empty() ? "" : ", ") + s;
        char b[64];
        for (auto& x : rest.vol) { sprintf_s(b, "%s%s %.3f", v.empty() ? "" : ", ", x.first.c_str(), x.second); v += b; }
        AUDLOG("mixer10: %s: active %s; at rest %s%s", paths[0].c_str(), a.c_str(), v.c_str(),
               rest.pitchSum ? " (a pitch offset is set: not applied)" : "");
        std::lock_guard<std::recursive_mutex> l(g.mx);
        g.mixer = rest.vol;
    } else
        AUDLOG("mixer10: no 1.0 mixer (%s)", core.empty() ? "no core datamap" : paths.empty() ? "no Mixer entry" : lua.empty() ? "file missing" : err.c_str());
}

inline DWORD WINAPI hostThread(void* p)
{
    HostStart* s = (HostStart*)p;
    bool ok = g.init(s->install10, s->nosound);
    if (ok) {
        if (cfg.mixer) loadMixer(s->install10);   // 1.0 loads it through its core datamap at boot, before any zone
        g.loadTree("Initialize.fev", "Permanent");   // 1.0 FUN_01138210 at audio init: 'Initialize.fev:Permanent'
    }
    s->ok = ok;
    SetEvent(s->done);
    if (!ok) return 0;
    for (;;) {
        Sleep(16);
        tickAndDeliver();
    }
}

// true once the FMOD host is up; the first caller waits for its init (never called under the loader lock)
inline bool ready()
{
    LONG st = hostState;
    if (st) return st > 0;
    DWORD w = WaitForSingleObject(hostStart.done, 20000);
    bool up = w == WAIT_OBJECT_0 && hostStart.ok;
    if (InterlockedCompareExchange(&hostState, up ? 1 : -1, 0) == 0 && !up)
        AUDLOG("audio10: FMOD host %s: 1.0 audio off", w != WAIT_OBJECT_0 ? "did not finish its init in 20 s" : "failed");
    return hostState > 0;
}

typedef bool (*HookFn)(const char* name, uint32_t staticVa, const char* prologue, void* detour, void** original);

inline bool install(HookFn hook, uintptr_t (*rt)(uint32_t), const std::string& install10, const std::string& game30)
{
    sound30 = game30 + "\\assets\\sound";
    g.idCounter = (volatile LONG*)rt(0x022AFBE8);
    hostStart.install10 = install10;
    hostStart.done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE th = hostStart.done ? CreateThread(nullptr, 0, hostThread, &hostStart, 0, nullptr) : nullptr;
    if (!th) { AUDLOG("audio10: FMOD host thread not started (%lu)", GetLastError()); return false; }
    CloseHandle(th);
    idPauseAll = fnv1("Pause_All");
    idPauseExUi = fnv1("Pause_All_Except_UI");
    idResumeAll = fnv1("Resume_All");
    hook("sound bank request (audio10)", 0x0182DB30, "568B711C5785F6", (void*)&hkBankReq, (void**)&oBankReq);
    hook("Wwise PostEvent (audio10)", 0x01948A10, "558BEC8B45088B0D????????", (void*)&hkPost, (void**)&oPost);
    hook("Wwise SetPosition (audio10)", 0x01947A50, "558BEC8B450885C07507", (void*)&hkSetPos, (void**)&oSetPos);
    hook("Wwise SetListenerPosition (audio10)", 0x01947150, "558BEC535657E8", (void*)&hkListener, (void**)&oListener);
    hook("Wwise StopPlayingID (audio10)", 0x019478D0, "558BEC568B750885F6", (void*)&hkStop, (void**)&oStop);
    hook("Wwise GetSourcePlayPosition (audio10)", 0x019475A0, "558BEC8B450C85C07507", (void*)&hkSrcPos, (void**)&oSrcPos);
    hook("Wwise UnregisterGameObj (audio10)", 0x01947640, "558BEC568B750885F67506", (void*)&hkUnreg, (void**)&oUnreg);
    hook("Wwise ExecuteActionOnEvent (audio10)", 0x01948C20, "558BEC8B450850E8", (void*)&hkAction, (void**)&oAction);
    hook("volume fader (audio10)", 0x0182C460, "8B4424048B4C240883EC30", (void*)&hkFader, (void**)&oFader);
    if (cfg.music) {
        stackPop = (StackPopFn)rt(0x0073B2C0);
        hook("MusicStates cue check (music10)", 0x007397B0, "578BF9837F1400", (void*)&hkMusicState, (void**)&oMusicState);
        hook("Wwise SetState (music10)", 0x01947BD0, "558BEC8B450856", (void*)&hkSetState, (void**)&oSetState);
        hook("MusicStack tick (music10)", 0x0073EDE0, "A1????????DB05????????81EC04010000", (void*)&hkStackTick, (void**)&oStackTick);
        hook("music cue once (music10)", 0x0073ED10, "8B44240883EC08535657", (void*)&hkMusicOnce, (void**)&oMusicOnce);
    }
    if (cfg.vo) {
        vo.root10 = install10 + "\\assets\\sound\\dialog";
        *(void**)&vo.lang = (void*)rt(0x00680C00);
        vo.mgr = (uint8_t*)rt(0x0228D668);
        *(void**)&vo.mapFind = (void*)rt(0x013CC1F0);
        *(void**)&vo.mapAdd = (void*)rt(0x018308E0);
        mapPut = (MapPutFn)rt(0x006EAA00);
        dialogMgr = (uint8_t*)rt(0x02204128);
        engineAlloc = (AllocFn)rt(0x00409E90);
        engineFree = (FreeFn)rt(0x00409ED0);
        hook("dialog line post (vo10)", 0x01948D80, "558BEC83EC105356578B7D1C", (void*)&hkPostExt, (void**)&oPostExt);
        hook("sound file read (vo10)", 0x0182F570, "8B44240485C0741F803800741A", (void*)&hkLoadFile, (void**)&oLoadFile);
        hook("dialog request (vo10)", 0x0073BF50, "53568BF1E8????????8D4618", (void*)&hkDlgReq, (void**)&oDlgReq);
    }
    if (cfg.trace) hook("queued sound post (audiotrace10)", 0x01830270, "83EC44538B5C244C", (void*)&hkQueuedPost, (void**)&oQueuedPost);
    return true;
}

}  // namespace audio
