// The MusicStack bridge: a playset's FMOD music project driven by the game's music stack. host, core. Music thread.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include "../../base/hash.hpp"
#include "../../base/text.hpp"
#include "../../core/hooks.hpp"
#include "host.hpp"
#include "log.hpp"
#include "system.hpp"

namespace engine::audio::music {

struct Config {
    std::function<std::string(const std::string& playset)> project;   // its music project as installed; "" none
    std::function<bool(const std::string& playset)> hosted;            // its music is ours, not the game's
    core::InstallFeatureFn installFeature = nullptr;
    uintptr_t (*rt)(uint32_t) = nullptr;   // core::rt; null: no engine (octtest)
};

// Keeps c and binds the stack's unlink; before any audio hook.
inline void configure(const Config& c);
// The music10 hooks, all or none; false when refused.
inline bool install(const Config& c);
inline bool hosted(const std::string& playset);
// 1.0 FUN_011335f0: the first cue named name in the loaded music project; false: id untouched.
inline bool cue(const char* name, unsigned* id);
inline void endPrompt();
// 1.0 FUN_00717bd0: prepare and begin the cue of the new top entry.
inline bool begin(const char* name);
// 1.0 FUN_00715650: the playset's project loaded, the previous playset's dropped first.
inline bool ensure(const std::string& playset);
inline void drop();
// Without system's lock, as hkBankReq always read it (AU-08 open).
inline bool isPlaysetUnlocked(const std::string& playset);
// system's lock held: the game's StartMusic_/StopMusic_ post of our playset -> its playing id; nullopt: not ours.
inline std::optional<uint32_t> onPost(const host::Post& p);
inline void setPaused(bool paused);
// octtest's view.
inline void* prompt();
inline const std::string& file();

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kStackPop = 0x0073B2C0;   // FUN_0073b2c0: thiscall (entry) on the music stack, its unlink
constexpr size_t kStackTop = 0xc;            // the music stack's top entry
}  // namespace eng

constexpr DWORD kStartGraceMs = 2000;   // a prompt never seen active ends after this

typedef uint32_t(__fastcall* MusicStateFn)(void* self, void* edx, const char* group, const char* state);
typedef int(__cdecl* SetStateFn)(const char* group, const char* state);
typedef void(__fastcall* StackTickFn)(void* self, void* edx);
typedef void(__fastcall* StackPopFn)(void* self, void* edx, void* entry);
typedef uint32_t(__fastcall* MusicOnceFn)(void* self, void* edx, const char* name, int prio, const char* s);

struct State {
    Config cfg;                           // set once by configure()
    StackPopFn stackPop = nullptr;        // set once by configure()
    MusicStateFn oMusicState = nullptr;   // originals: each set once by hook() in install()
    SetStateFn oSetState = nullptr;       // set once by hook()
    StackTickFn oStackTick = nullptr;     // set once by hook()
    MusicOnceFn oMusicOnce = nullptr;     // set once by hook()
    std::string playset;   // system's lock; the playset whose music project is loaded
    uint32_t startEvent = 0, stopEvent = 0;   // system's lock; 'StartMusic_' / 'StopMusic_' + playset ids (AU-17)
    std::string file;      // system's lock
    std::atomic<void*> prompt{nullptr};   // system's lock for changes; hkStackTick peeks first (1.0 keeps one prompt)
    bool seenActive = false;   // system's lock
    DWORD begunAt = 0;         // system's lock
    uint32_t startId = 0;      // system's lock; the stand-in id of 'StartMusic_<X>'
};
inline State g_state;

// The playset of a 'Music_<X>' state group, or null.
inline const char* playsetOfGroup(const char* group)
{
    constexpr size_t kPrefix = 6;   // "Music_"
    if (!group || _strnicmp(group, "Music_", kPrefix)) return nullptr;
    return group + kPrefix;
}

// The stack's top entry once its prompt ended; null otherwise or when system's lock is busy.
inline void* endedTop(void* self)
{
    State& s = g_state;
    std::unique_lock<std::recursive_mutex> l = system::tryLock();
    if (!l || !s.prompt) return nullptr;
    const fmod::Api& f = system::api();
    int active = 0;
    f.MusicPrompt_IsActive(s.prompt, &active);
    if (active) {
        s.seenActive = true;
        return nullptr;
    }
    // ended: seen active once (FMOD updates on its own thread), or never started within the grace time
    if (!s.seenActive && GetTickCount() - s.begunAt < kStartGraceMs) return nullptr;
    f.MusicPrompt_Release(s.prompt);
    s.prompt = nullptr;
    return *(void**)((uint8_t*)self + eng::kStackTop);
}

// FUN_007397b0, thiscall (group, state) on MusicStates.bin: whether a cue is known. Music thread, per cue push.
inline uint32_t __fastcall hkMusicState(void* self, void*, const char* group, const char* state)
{
    State& s = g_state;
    uint32_t r = s.oMusicState(self, nullptr, group, state);
    const char* x = playsetOfGroup(group);
    if (!x || !state) return r;
    if (const bool up = system::ready(); !up) return r;
    // With no MusicStates.bin loaded FUN_007397b0 accepts every cue (0x00739837): a hosted playset's answer is ours.
    if (!hosted(x)) return r;
    if (const bool loaded = ensure(x); !loaded) return r;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    unsigned id = 0;
    const bool known = cue(state, &id);
    r = known ? 1 : 0;
    log("audio10: music cue %s:%s %s", group, state, r ? "is a 1.0 cue" : "is not in the 1.0 project");
    return r;
}

// FUN_01947bd0, cdecl SetState(group, state): 3.0 FUN_0073ede0 sets 'Music_<X>' to its top cue. Music thread, per set.
inline int __cdecl hkSetState(const char* group, const char* state)
{
    State& s = g_state;
    const char* x = state ? playsetOfGroup(group) : nullptr;
    if (x) {
        std::unique_lock<std::recursive_mutex> l = system::lock();   // playset changes on the bank dispatcher's thread
        const bool ours = !s.playset.empty() && !_stricmp(x, s.playset.c_str());
        if (ours && !_stricmp(state, "None")) endPrompt();
        if (ours && _stricmp(state, "None")) {
            const bool begun = begin(state);
            log("audio10: music cue %s %s", state, begun ? "prompted" : "failed");
        }
    }
    return s.oSetState(group, state);
}

// FUN_0073ed10, thiscall (name, priority, string; ret 0xc): a cue for 3 s. Music thread, per stinger.
inline uint32_t __fastcall hkMusicOnce(void* self, void*, const char* name, int prio, const char* str)
{
    State& s = g_state;
    const char* u = name ? strchr(name, '_') : nullptr;
    const bool up = u && system::ready();
    const std::string x = up ? std::string(name, u - name) : std::string();
    const bool ours = up && hosted(x) && ensure(x);
    if (ours) {
        // 1.0 FUN_00716ac0 prompts it instead (MusicSystem::promptCue): FMOD plays it once, then what is under it
        std::unique_lock<std::recursive_mutex> l = system::lock();
        unsigned id = 0;
        const bool found = cue(name, &id);
        const bool ok = found && !system::api().MusicSystem_PromptCue(system::musicSystem(), id);
        log("audio10: music cue %s %s", name, ok ? "prompted once (1.0 promptCue)" : "not in the 1.0 project");
        if (ok) return 0;
    }
    return s.oMusicOnce(self, nullptr, name, prio, str);
}

// FUN_0073ede0, thiscall, MusicStack tick; music thread, per frame. 1.0 FUN_00717bd0 also pops an ended prompt.
inline void __fastcall hkStackTick(void* self, void*)
{
    State& s = g_state;
    s.oStackTick(self, nullptr);
    if (!s.prompt) return;
    void* entry = endedTop(self);
    if (!entry) return;
    *(void**)((uint8_t*)self + eng::kStackTop) = nullptr;
    s.stackPop(self, nullptr, entry);
}

}  // namespace detail

inline void configure(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    if (c.rt) s.stackPop = (detail::StackPopFn)c.rt(detail::eng::kStackPop);
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    const core::InstallFeatureFn installFeature = c.installFeature;
    return installFeature("music10", {
        {"MusicStates cue check (music10)", 0x007397B0, "578BF9837F1400", (void*)&detail::hkMusicState,
         (void**)&s.oMusicState},
        {"Wwise SetState (music10)", 0x01947BD0, "558BEC8B450856", (void*)&detail::hkSetState, (void**)&s.oSetState},
        {"MusicStack tick (music10)", 0x0073EDE0, "A1????????DB05????????81EC04010000", (void*)&detail::hkStackTick,
         (void**)&s.oStackTick},
        {"music cue once (music10)", 0x0073ED10, "8B44240883EC08535657", (void*)&detail::hkMusicOnce,
         (void**)&s.oMusicOnce},
    });
}

inline bool hosted(const std::string& playset) { return detail::g_state.cfg.hosted(playset); }

inline bool cue(const char* name, unsigned* id)
{
    fmod::MusicIterator it{};
    const fmod::Result r = system::api().MusicSystem_GetCues(system::musicSystem(), &it, name);
    if (r || !it.value) return false;
    *id = it.value->id;
    return true;
}

inline void endPrompt()
{
    detail::State& s = detail::g_state;
    void* p = s.prompt;
    if (!p) return;
    system::api().MusicPrompt_End(p);
    system::api().MusicPrompt_Release(p);
    s.prompt = nullptr;
}

inline bool begin(const char* name)
{
    detail::State& s = detail::g_state;
    endPrompt();
    unsigned id = 0;
    void* p = nullptr;
    const bool found = cue(name, &id);
    const fmod::Result r = found ? system::api().MusicSystem_PrepareCue(system::musicSystem(), id, &p) : 0;
    if (!found || r || !p) {
        s.prompt = nullptr;
        return false;
    }
    s.prompt = p;
    system::api().MusicPrompt_Begin(p);
    s.seenActive = false;
    s.begunAt = GetTickCount();
    return true;
}

inline bool ensure(const std::string& playset)
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    const std::string want = s.cfg.project(playset);
    if (want.empty()) return false;
    if (text::lower(want) == text::lower(s.file)) return true;
    drop();   // MusicSystem::reset first (1.0 FUN_01133630)
    void* p = host::load(want);
    if (!p) return false;
    s.file = want;
    s.playset = playset;
    s.startEvent = hash::fnv1Lower("StartMusic_" + playset);   // 3.0 FUN_0073eb30 starts a playset's music so
    s.stopEvent = hash::fnv1Lower("StopMusic_" + playset);
    return true;
}

inline void drop()
{
    detail::State& s = detail::g_state;
    std::unique_lock<std::recursive_mutex> l = system::lock();
    if (s.file.empty()) return;
    endPrompt();
    system::api().MusicSystem_Reset(system::musicSystem());
    host::unload(s.file);
    s.file.clear();
    s.playset.clear();
    if (!s.startId) return;
    host::act(s.startId, host::kActionStop);
    s.startId = 0;
}

inline bool isPlaysetUnlocked(const std::string& playset)
{
    return text::lower(detail::g_state.playset) == text::lower(playset);
}

inline std::optional<uint32_t> onPost(const host::Post& p)
{
    detail::State& s = detail::g_state;
    if (s.playset.empty()) return std::nullopt;   // the cached ids hold only while a playset is set
    if (p.eventId == s.startEvent) {
        if (!s.startId) s.startId = host::standIn(p);
        return s.startId;
    }
    if (p.eventId != s.stopEvent) return std::nullopt;
    endPrompt();   // 1.0 resets the MusicSystem before it switches (FUN_00715650)
    system::api().MusicSystem_Reset(system::musicSystem());
    if (s.startId) {
        host::act(s.startId, host::kActionStop);
        s.startId = 0;
    }
    return 0;
}

inline void setPaused(bool paused)
{
    std::unique_lock<std::recursive_mutex> l = system::lock();
    system::api().MusicSystem_SetPaused(system::musicSystem(), paused);
}

inline void* prompt() { return detail::g_state.prompt; }
inline const std::string& file() { return detail::g_state.file; }

}  // namespace engine::audio::music
