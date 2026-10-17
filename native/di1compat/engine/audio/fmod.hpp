// The FMOD Ex / Event 4.44 C API: exports loaded from a folder, and the constants the host uses. Windows.
#pragma once
#include <windows.h>
#include <climits>
#include <string>
#include "log.hpp"

namespace engine::audio::fmod {

typedef int Result;   // FMOD_RESULT, 0 = FMOD_OK
typedef Result(__stdcall* EventCallback)(void* ev, int type, void* p1, void* p2, void* ud);

// fmod_event.h / fmod.h 4.44 values
constexpr unsigned kEventDefault = 0;     // FMOD_EVENT_DEFAULT
constexpr unsigned kEventInfoOnly = 4;    // FMOD_EVENT_INFOONLY
constexpr int kCacheEvents = 1;           // getGroup's cacheevents
constexpr int kPropMaxPlaybacksBehavior = 13;   // FMOD_EVENTPROPERTY_*
constexpr int kPropMode = 14;
constexpr int kPropOneshot = 35;
constexpr int kJustFailIfQuietest = 5;    // MAX_PLAYBACKS_BEHAVIOR value
constexpr int kCallbackStolen = 3;        // FMOD_EVENT_CALLBACKTYPE_*
constexpr int kCallbackFinished = 4;
constexpr int kCallbackSoundDefCreate = 6;
constexpr int kOutputNoSound = 2;         // FMOD_OUTPUTTYPE_NOSOUND
constexpr unsigned kTimeUnitMs = 1;       // FMOD_TIMEUNIT_MS
constexpr int kStopImmediate = 1;         // Event_Stop's immediate
constexpr int kStopFadeOut = 0;
constexpr int kDefaultDriver = 0;
constexpr int kListener = 0;
constexpr size_t kEventInfoSize = 0x80;   // our FMOD_EVENT_INFO buffer
constexpr size_t kInfoPositionMs = 4;     // FMOD_EVENT_INFO.positionms
constexpr size_t kInfoSystemId = 0x20;    // FMOD_EVENT_INFO.systemid
constexpr size_t kAdvancedSettingsSize = 0x68;   // FMOD_ADVANCEDSETTINGS, its cbsize

// FMOD_MUSIC_ITERATOR and the entity it points at.
struct MusicEntity {
    const char* name;
    unsigned id;
};
struct MusicIterator {
    const MusicEntity* value;
    const char* filter;
};

// The C API of fmodex.dll / fmod_event.dll 4.44.14, all __stdcall (argument counts checked by each export's ret N).
struct Api {
    Result(__stdcall* EventSystem_Create)(void** es);
    Result(__stdcall* EventSystem_GetSystemObject)(void* es, void** sys);
    Result(__stdcall* EventSystem_GetMusicSystem)(void* es, void** ms);
    Result(__stdcall* EventSystem_Init)(void* es, int maxch, unsigned flags, void* extra, unsigned evflags);
    Result(__stdcall* EventSystem_SetMediaPath)(void* es, const char* path);
    Result(__stdcall* EventSystem_Load)(void* es, const char* name, void* info, void** proj);
    Result(__stdcall* EventSystem_Update)(void* es);
    Result(__stdcall* EventSystem_GetEventBySystemID)(void* es, unsigned id, unsigned mode, void** ev);
    Result(__stdcall* EventSystem_Set3DListenerAttributes)(void* es, int l, const float* pos, const float* vel,
                                                           const float* fwd, const float* up);
    Result(__stdcall* EventSystem_GetVersion)(void* es, unsigned* v);
    Result(__stdcall* EventProject_Release)(void* p);
    Result(__stdcall* EventProject_GetNumGroups)(void* p, int* n);
    Result(__stdcall* EventProject_GetGroupByIndex)(void* p, int i, int cache, void** g);
    Result(__stdcall* EventProject_GetGroup)(void* p, const char* name, int cache, void** g);
    Result(__stdcall* EventProject_LoadSampleData)(void* p, int* ids, int nids, char** groups, int ngroups,
                                                   unsigned mode);
    Result(__stdcall* EventGroup_GetNumGroups)(void* g, int* n);
    Result(__stdcall* EventGroup_GetGroupByIndex)(void* g, int i, int cache, void** sub);
    Result(__stdcall* EventGroup_GetNumEvents)(void* g, int* n);
    Result(__stdcall* EventGroup_GetEventByIndex)(void* g, int i, unsigned mode, void** ev);
    Result(__stdcall* EventGroup_GetProperty)(void* g, const char* name, void* value);
    Result(__stdcall* Event_GetInfo)(void* ev, int* index, char** name, void* info);
    Result(__stdcall* Event_GetPropertyByIndex)(void* ev, int idx, void* value, int thisInstance);
    Result(__stdcall* Event_GetCategory)(void* ev, void** cat);
    Result(__stdcall* EventCategory_GetInfo)(void* cat, int* index, char** name);
    Result(__stdcall* EventCategory_GetParentCategory)(void* cat, void** parent);
    Result(__stdcall* Event_Start)(void* ev);
    Result(__stdcall* Event_Stop)(void* ev, int immediate);
    Result(__stdcall* Event_SetPaused)(void* ev, int paused);
    Result(__stdcall* Event_Set3DAttributes)(void* ev, const float* pos, const float* vel, const float* orient);
    Result(__stdcall* Event_SetVolume)(void* ev, float v);
    Result(__stdcall* Event_SetCallback)(void* ev, EventCallback cb, void* ud);
    Result(__stdcall* EventSystem_GetCategory)(void* es, const char* name, void** cat);
    Result(__stdcall* EventSystem_GetMusicCategory)(void* es, void** cat);
    Result(__stdcall* EventCategory_SetVolume)(void* cat, float v);
    Result(__stdcall* EventCategory_GetVolume)(void* cat, float* v);
    Result(__stdcall* MusicSystem_GetCues)(void* ms, void* it, const char* filter);
    Result(__stdcall* MusicSystem_GetNextCue)(void* ms, void* it);
    Result(__stdcall* MusicSystem_PrepareCue)(void* ms, unsigned id, void** prompt);
    Result(__stdcall* MusicSystem_PromptCue)(void* ms, unsigned id);
    Result(__stdcall* MusicSystem_Reset)(void* ms);
    Result(__stdcall* MusicSystem_SetPaused)(void* ms, int paused);
    Result(__stdcall* MusicPrompt_Begin)(void* p);
    Result(__stdcall* MusicPrompt_End)(void* p);
    Result(__stdcall* MusicPrompt_IsActive)(void* p, int* active);
    Result(__stdcall* MusicPrompt_Release)(void* p);
    Result(__stdcall* System_Set3DSettings)(void* s, float doppler, float dist, float rolloff);
    Result(__stdcall* System_GetNumDrivers)(void* s, int* n);
    Result(__stdcall* System_GetDriverCaps)(void* s, int id, unsigned* caps, int* rate, int* speakermode);
    Result(__stdcall* System_SetSpeakerMode)(void* s, int mode);
    Result(__stdcall* System_SetOutput)(void* s, int type);
    Result(__stdcall* System_GetSoftwareFormat)(void* s, int* rate, int* fmt, int* nout, int* maxin, int* resampler,
                                                int* bits);
    Result(__stdcall* System_SetSoftwareFormat)(void* s, int rate, int fmt, int nout, int maxin, int resampler);
    Result(__stdcall* System_SetStreamBufferSize)(void* s, unsigned size, unsigned unit);
    Result(__stdcall* System_SetAdvancedSettings)(void* s, void* adv);
    Result(__stdcall* System_GetVersion)(void* s, unsigned* v);
    Result(__stdcall* System_CreateSound)(void* s, const char* name, unsigned mode, void* exinfo, void** snd);
    Result(__stdcall* Sound_GetSubSound)(void* snd, int i, void** sub);
    Result(__stdcall* Sound_GetNumSubSounds)(void* snd, int* n);
    Result(__stdcall* Sound_GetLength)(void* snd, unsigned* len, unsigned unit);
    Result(__stdcall* Sound_Release)(void* snd);

    // Both DLLs from dir, every export bound; false if a DLL or an export (logged) is missing.
    bool load(const std::string& dir);
};

// Calls fn(category) from cat up through its parents, at most maxDepth of them.
template <class Fn>
inline void walkCategories(const Api& f, void* cat, int maxDepth, Fn fn);
constexpr int kUncapped = INT_MAX;

namespace detail {

// Binds exports of one DLL; a missing one is logged and makes ok false.
struct Binder {
    HMODULE m;
    bool ok;
    template <class Fn>
    void operator()(Fn& fn, const char* name)
    {
        void* p = (void*)GetProcAddress(m, name);
        if (!p) {
            log("audio10: %s missing", name);
            ok = false;
        }
        fn = reinterpret_cast<Fn>(p);
    }
};

inline void bindEvent(Api& a, Binder& b)
{
    b(a.EventSystem_Create, "FMOD_EventSystem_Create");
    b(a.EventSystem_GetSystemObject, "FMOD_EventSystem_GetSystemObject");
    b(a.EventSystem_GetMusicSystem, "FMOD_EventSystem_GetMusicSystem");
    b(a.EventSystem_Init, "FMOD_EventSystem_Init");
    b(a.EventSystem_SetMediaPath, "FMOD_EventSystem_SetMediaPath");
    b(a.EventSystem_Load, "FMOD_EventSystem_Load");
    b(a.EventSystem_Update, "FMOD_EventSystem_Update");
    b(a.EventSystem_GetEventBySystemID, "FMOD_EventSystem_GetEventBySystemID");
    b(a.EventSystem_Set3DListenerAttributes, "FMOD_EventSystem_Set3DListenerAttributes");
    b(a.EventSystem_GetVersion, "FMOD_EventSystem_GetVersion");
    b(a.EventProject_Release, "FMOD_EventProject_Release");
    b(a.EventProject_GetNumGroups, "FMOD_EventProject_GetNumGroups");
    b(a.EventProject_GetGroupByIndex, "FMOD_EventProject_GetGroupByIndex");
    b(a.EventProject_GetGroup, "FMOD_EventProject_GetGroup");
    b(a.EventProject_LoadSampleData, "FMOD_EventProject_LoadSampleData");
    b(a.EventGroup_GetNumGroups, "FMOD_EventGroup_GetNumGroups");
    b(a.EventGroup_GetGroupByIndex, "FMOD_EventGroup_GetGroupByIndex");
    b(a.EventGroup_GetNumEvents, "FMOD_EventGroup_GetNumEvents");
    b(a.EventGroup_GetEventByIndex, "FMOD_EventGroup_GetEventByIndex");
    b(a.EventGroup_GetProperty, "FMOD_EventGroup_GetProperty");
    b(a.Event_GetInfo, "FMOD_Event_GetInfo");
    b(a.Event_GetPropertyByIndex, "FMOD_Event_GetPropertyByIndex");
    b(a.Event_GetCategory, "FMOD_Event_GetCategory");
    b(a.EventCategory_GetInfo, "FMOD_EventCategory_GetInfo");
    b(a.EventCategory_GetParentCategory, "FMOD_EventCategory_GetParentCategory");
    b(a.Event_Start, "FMOD_Event_Start");
    b(a.Event_Stop, "FMOD_Event_Stop");
    b(a.Event_SetPaused, "FMOD_Event_SetPaused");
    b(a.Event_Set3DAttributes, "FMOD_Event_Set3DAttributes");
    b(a.Event_SetVolume, "FMOD_Event_SetVolume");
    b(a.Event_SetCallback, "FMOD_Event_SetCallback");
    b(a.EventSystem_GetCategory, "FMOD_EventSystem_GetCategory");
    b(a.EventSystem_GetMusicCategory, "FMOD_EventSystem_GetMusicCategory");
    b(a.EventCategory_SetVolume, "FMOD_EventCategory_SetVolume");
    b(a.EventCategory_GetVolume, "FMOD_EventCategory_GetVolume");
    b(a.MusicSystem_GetCues, "FMOD_MusicSystem_GetCues");
    b(a.MusicSystem_GetNextCue, "FMOD_MusicSystem_GetNextCue");
    b(a.MusicSystem_PrepareCue, "FMOD_MusicSystem_PrepareCue");
    b(a.MusicSystem_PromptCue, "FMOD_MusicSystem_PromptCue");
    b(a.MusicSystem_Reset, "FMOD_MusicSystem_Reset");
    b(a.MusicSystem_SetPaused, "FMOD_MusicSystem_SetPaused");
    b(a.MusicPrompt_Begin, "FMOD_MusicPrompt_Begin");
    b(a.MusicPrompt_End, "FMOD_MusicPrompt_End");
    b(a.MusicPrompt_IsActive, "FMOD_MusicPrompt_IsActive");
    b(a.MusicPrompt_Release, "FMOD_MusicPrompt_Release");
}

inline void bindEx(Api& a, Binder& b)
{
    b(a.System_Set3DSettings, "FMOD_System_Set3DSettings");
    b(a.System_GetNumDrivers, "FMOD_System_GetNumDrivers");
    b(a.System_GetDriverCaps, "FMOD_System_GetDriverCaps");
    b(a.System_SetSpeakerMode, "FMOD_System_SetSpeakerMode");
    b(a.System_SetOutput, "FMOD_System_SetOutput");
    b(a.System_GetSoftwareFormat, "FMOD_System_GetSoftwareFormat");
    b(a.System_SetSoftwareFormat, "FMOD_System_SetSoftwareFormat");
    b(a.System_SetStreamBufferSize, "FMOD_System_SetStreamBufferSize");
    b(a.System_SetAdvancedSettings, "FMOD_System_SetAdvancedSettings");
    b(a.System_GetVersion, "FMOD_System_GetVersion");
    b(a.System_CreateSound, "FMOD_System_CreateSound");
    b(a.Sound_GetSubSound, "FMOD_Sound_GetSubSound");
    b(a.Sound_GetNumSubSounds, "FMOD_Sound_GetNumSubSounds");
    b(a.Sound_GetLength, "FMOD_Sound_GetLength");
    b(a.Sound_Release, "FMOD_Sound_Release");
}

}  // namespace detail

inline bool Api::load(const std::string& dir)
{
    HMODULE ex = LoadLibraryExA((dir + "\\fmodex.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    HMODULE ev = ex ? LoadLibraryExA((dir + "\\fmod_event.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
                    : nullptr;
    if (!ev) return false;
    detail::Binder events{ev, true};
    detail::bindEvent(*this, events);
    detail::Binder system{ex, events.ok};
    detail::bindEx(*this, system);
    return system.ok;
}

template <class Fn>
inline void walkCategories(const Api& f, void* cat, int maxDepth, Fn fn)
{
    for (int d = 0; cat && d < maxDepth; ++d) {
        fn(cat);
        void* parent = nullptr;
        if (const Result r = f.EventCategory_GetParentCategory(cat, &parent); r) parent = nullptr;
        cat = parent;
    }
}

}  // namespace engine::audio::fmod
