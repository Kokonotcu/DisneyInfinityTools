// 1.0 Flash movies on 3.0 screens: action maps, focus, keys (actionmap10, focus10, keys10), input10 probes.
// engine/screens. Main thread.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/screens.hpp"
#include "config.hpp"
#include "inputtrace.hpp"
#include "session.hpp"

namespace di1::movies {

// Installs the ExternalInterface, focus and action delivery hooks, as c switches them on (H1, H2).
inline bool install(const Config& c);

namespace detail {

namespace scr = engine::screens::eng;   // a screen's fields

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kSetHasFocus = 0x01552390;   // FUN_01552390: thiscall (focus) on a screen: root.SetHasFocus, +0x60
constexpr size_t kFocusLists = 0x28C;           // the manager's per-player focus lists, 0x20 apart
constexpr int kFocusListBytes = 0x20;
constexpr size_t kFocusTop = 8;                 // a list head's holder of the top screen
constexpr size_t kMapClear = 1;                 // an action map's vt[1], FUN_007f3640: empties it
constexpr size_t kMapCount = 8;                 // an action map's entry count
constexpr size_t kMapSlot = 0x20;               // the map's player slot (a byte), as FUN_00b48250 queries it
constexpr size_t kInputMovie = 0x3C;            // the delivery input's movie (FUN_015520c0)
constexpr size_t kHandleEvent = 0x8C / 4;       // a movie's vt index of HandleEvent
constexpr uint32_t kKeyDown = 5;                // FUN_007f3320's KeyDown event type (0x007f3451..0x007f3480)
constexpr size_t kMappers = 0xD54;              // the screen manager's UI control mappers, one per player slot
constexpr size_t kMapperBytes = 0x1BC4;
constexpr size_t kMapperEntries = 0xC, kMapperCount = 0x10;   // its entry pointers and their count
constexpr size_t kEntryDown = 0x10, kEntryKey = 0x20;
constexpr uint32_t kSlots = 11;                 // player slots 0..10 (FUN_01566f10)
constexpr size_t kGfxType = 0xC, kGfxValue = 0x10, kGfxBytes = 0x20;   // a GFx Value
constexpr uint32_t kGfxTypeMask = 0x8F, kGfxString = 6, kGfxManaged = 0x40;   // managed: one more hop
}  // namespace eng

typedef void(__cdecl* FlashCallFn)(void* a1, void* a2, const char* name, uint8_t* args, void* a5);
typedef void(__fastcall* ActionMapClearFn)(void* map, void* edx);
typedef void(__fastcall* FocusGainFn)(uint8_t* mgr, void* edx, uint8_t* screen);
typedef void(__fastcall* FocusLossFn)(uint8_t* mgr, void* edx, uint8_t** holder);
typedef void(__fastcall* SetHasFocusFn)(uint8_t* screen, void* edx, int focus);
typedef void(__fastcall* DeliverFn)(uint8_t* screen, void* edx, void* a1, void* a2);
typedef char(__fastcall* MapStateFn)(void* map, void* edx, int i, uint32_t* key, uint8_t* down, uint8_t* up);
typedef uint32_t(__fastcall* HandleEventFn)(void* movie, void* edx, const void* ev);

struct State {
    bool actionMap10 = false, keys10 = false, lockTrace10 = false;   // set once in install()
    // originals: set once by hook() / installFeature in install()
    FlashCallFn g_flashCall = nullptr;
    FocusGainFn g_focusGain = nullptr;
    FocusLossFn g_focusLoss = nullptr;
    DeliverFn g_deliver = nullptr;
    MapStateFn g_mapState = nullptr;
    // main thread only: set while FUN_01565330 runs for a 3.0-script screen on a 1.0 movie (GP-15)
    uint8_t* keysScreen = nullptr;
    void* keysMovie = nullptr;
    // main thread only: the delivery probes' last screen and gates, and each mapper entry's last state
    uint8_t* lastScreen = nullptr;
    uint32_t lastGates = ~0u;
    uint8_t wasDown[256] = {};
};
inline State g_state;

// Whether the screen's movie was served from 1.0 (session: flash/<movie>, lower case, '/').
inline bool movie10(const uint8_t* screen)
{
    char m[96] = "";
    core::safeCopy(m, sizeof(m), screen ? *(const char* const*)(screen + scr::kMovie) : nullptr);
    if (!*m) return false;
    std::string k = std::string("flash/") + m;
    for (char& c : k) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    return session::movieServed(k);
}

// Whether the screen's Lua script was served from 1.0.
inline bool script10(const uint8_t* screen)
{
    char t[64] = "";
    engine::screens::copyField(screen, scr::kScript, t, sizeof(t));
    for (char* c = t; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return session::screenServed(t);
}

// A GFx Value's string, or null.
inline const char* gfxString(const uint8_t* v)
{
    if (!core::readable(v, eng::kGfxValue + 4)) return nullptr;
    uint32_t t = *(const uint32_t*)(v + eng::kGfxType);
    const uint8_t* p = *(const uint8_t* const*)(v + eng::kGfxValue);
    if ((t & eng::kGfxTypeMask) != eng::kGfxString) return nullptr;
    if (t & eng::kGfxManaged) p = core::readable(p, 4) ? *(const uint8_t* const*)p : nullptr;
    return (const char*)p;
}

// The screen an ExternalInterface call comes from: args[0] is its Lua name (copied into lua).
inline uint8_t* screenOfCall(const uint8_t* args, char* lua, size_t cap)
{
    core::safeCopy(lua, cap, gfxString(args));
    void* mgr = engine::screens::manager();
    int h = *lua && mgr ? engine::screens::byName(mgr, lua) : -1;
    return h != -1 ? engine::screens::byHandle(mgr, h) : nullptr;
}

// 0x01564790, cdecl, main thread, per ExternalInterface call of any movie: UsesActionMap (88 of 249 1.0 movies).
inline void __cdecl hkFlashCall(void* a1, void* a2, const char* name, uint8_t* args, void* argc)
{
    State& s = g_state;
    char lua[64] = "";
    uint8_t* screen = name && args ? screenOfCall(args, lua, sizeof(lua)) : nullptr;
    bool ten = screen && movie10(screen);
    // 3.0 ignores UsesActionMap (0x01564f7d): for a 1.0 movie the map is cleared, then appended to
    if (ten && !strcmp(name, "UsesActionMap") && s.actionMap10) {
        if (void* map = *(void**)screen) (*(ActionMapClearFn**)map)[eng::kMapClear](map, nullptr);
        core::logf("actionmap10: %s (1.0 movie) UsesActionMap -> its map is the movie's list", lua);
        name = "AppendActionMap";
    }
    // probe: genericlist asks CanChangeSelection every frame while a key is held, which used up the budget
    if (ten && strcmp(name, "CanChangeSelection") && inputtrace::room()) {
        char a[64] = "";
        if ((uintptr_t)argc >= 2) core::safeCopy(a, sizeof(a), gfxString(args + eng::kGfxBytes));
        core::logf("input10: movie of %s calls %s('%s'), %u args", lua, name, a, (unsigned)(uintptr_t)argc);
    }
    s.g_flashCall(a1, a2, name, args, argc);
}

// +0x60 gates every action; a 1.0 movie never sends NotifyFocusChange, so it also gets the non-FSM SetHasFocus.
inline void movieFocus10(uint8_t* screen, bool focus, const char* why)
{
    if (!screen || !screen[scr::kLoaded] || !movie10(screen)) return;
    ((SetHasFocusFn)core::rt(eng::kSetHasFocus))(screen, nullptr, focus ? 1 : 0);
    char name[64] = "";
    engine::screens::copyField(screen, scr::kName, name, sizeof(name));
    core::logf("focus10: %s (1.0 movie) %s: SetHasFocus(%d), +0x60 now %d", name, why, focus ? 1 : 0,
               screen[scr::kFocus]);
}

// FUN_01559480, thiscall on the manager (screen; ret 4), main thread, per focus change: the top screen gains it.
inline void __fastcall hkFocusGain(uint8_t* mgr, void* edx, uint8_t* screen)
{
    g_state.g_focusGain(mgr, edx, screen);
    // the top of the screen's player's focus list: what 3.0 focused
    uint8_t* head =
        screen ? *(uint8_t**)(mgr + eng::kFocusLists + *(int*)(screen + scr::kPlayer) * eng::kFocusListBytes) : nullptr;
    uint8_t** holder = head ? *(uint8_t***)(head + eng::kFocusTop) : nullptr;
    movieFocus10(holder ? holder[0] : nullptr, true, "gain");
}

// FUN_01554590, thiscall on the manager (holder; ret 4), main thread, per focus change.
inline void __fastcall hkFocusLoss(uint8_t* mgr, void* edx, uint8_t** holder)
{
    g_state.g_focusLoss(mgr, edx, holder);
    movieFocus10(holder ? holder[0] : nullptr, false, "loss");
}

// A KeyDown event for the movie as FUN_007f3320 fills one: type, modifiers, keyCode, the player slot twice.
inline void sendKey(void* movie, const void* map, uint32_t key)
{
    uint32_t slot = *(const uint8_t*)((const uint8_t*)map + eng::kMapSlot);
    uint32_t ev[6] = {eng::kKeyDown, 0, key, slot, 0, slot};
    (*(HandleEventFn**)movie)[eng::kHandleEvent](movie, nullptr, ev);
}

// FUN_00b48250, thiscall (vtable), main thread, per map entry per delivery query.
inline char __fastcall hkMapState(void* map, void* edx, int i, uint32_t* key, uint8_t* down, uint8_t* up)
{
    // 1.0 sent mapped actions to the movie as key events (keyCode = action code); 3.0 calls the screen's Lua
    State& s = g_state;
    char r = s.g_mapState(map, edx, i, key, down, up);
    const bool asked = key && down && up;
    if (!r || !s.keysScreen || !asked) return r;
    if (!*down && !*up) return r;
    char name[64] = "";
    engine::screens::copyField(s.keysScreen, scr::kName, name, sizeof(name));
    bool sent = false;
    if (*down && s.keysMovie && s.keys10) {
        sendKey(s.keysMovie, map, *key);
        sent = true;
    }
    if (inputtrace::room())
        core::logf("input10: %s action key %u (map entry %d) %s -> %s", name, *key, i, *down ? "down" : "released",
                   sent ? "sent to the movie as a key event" : "not sent");
    return r;
}

// probe (locktrace10): a 1.0-movie screen's delivery gates, logged whenever they change.
inline void gatesProbe(uint8_t* screen, void* a1)
{
    State& s = g_state;
    void* map = *(void**)screen;
    uint32_t n = map ? *(uint32_t*)((uint8_t*)map + eng::kMapCount) : 0;
    uint32_t gates = screen[scr::kFocus] | screen[scr::kLoaded] << 1 | screen[scr::kHidden] << 2 |
                     screen[scr::kHiding] << 3 | n << 8 | (a1 ? 1u << 31 : 0);
    char name[64] = "";
    engine::screens::copyField(screen, scr::kName, name, sizeof(name));
    const bool changed = screen != s.lastScreen || gates != s.lastGates;
    if (changed && inputtrace::room())
        core::logf("input10: %s delivery gates: focus(+0x60) %d loaded %d hidden %d hiding %d, "
                   "map %u actions, input %p, 1.0 script %d",
                   name, screen[scr::kFocus], screen[scr::kLoaded], screen[scr::kHidden], screen[scr::kHiding], n, a1,
                   script10(screen));
    s.lastScreen = screen;
    s.lastGates = gates;
}

// probe: each UI action of the player's mapper that turns down while the screen takes input, mapped or not.
inline void mapperProbe(uint8_t* screen)
{
    State& s = g_state;
    void* map = *(void**)screen;
    uint8_t* mgr = (uint8_t*)engine::screens::manager();
    uint32_t slot = map ? *(const uint8_t*)((const uint8_t*)map + eng::kMapSlot) : 0;
    uint8_t* mp = mgr && slot < eng::kSlots ? mgr + eng::kMappers + slot * eng::kMapperBytes : nullptr;
    uint8_t** es = mp ? *(uint8_t***)(mp + eng::kMapperEntries) : nullptr;
    uint32_t ne = mp ? *(uint32_t*)(mp + eng::kMapperCount) : 0;
    for (uint32_t i = 0; es && i < ne && i < sizeof(s.wasDown); ++i) {
        uint8_t d = es[i] ? es[i][eng::kEntryDown] : 0;
        if (d && !s.wasDown[i] && inputtrace::room())
            core::logf("input10: %s player slot %u UI action down: mapper entry %u key %u",
                       *(const char**)(screen + scr::kName), slot, i, *(uint32_t*)(es[i] + eng::kEntryKey));
        s.wasDown[i] = d;
    }
}

// FUN_01565330, thiscall (a1, a2; ret 8), main thread, per frame per input screen and slot: a screen's actions.
inline void __fastcall hkDeliver(uint8_t* screen, void* edx, void* a1, void* a2)
{
    State& s = g_state;
    bool ten = screen && movie10(screen);
    if (ten && s.lockTrace10) gatesProbe(screen, a1);
    // a 1.0 script's screen keeps 3.0's path: SelectMenu works on raw keys and would see its select twice
    if (ten && a1 && !script10(screen)) {
        s.keysScreen = screen;
        s.keysMovie = *(void**)((uint8_t*)a1 + eng::kInputMovie);
        mapperProbe(screen);
    }
    s.g_deliver(screen, edx, a1, a2);
    s.keysScreen = nullptr;
    s.keysMovie = nullptr;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.actionMap10 = c.actionMap10;
    s.keys10 = c.keys10;
    s.lockTrace10 = c.lockTrace10;
    bool ok = true;
    if (c.actionMap10 || c.lockTrace10)
        ok &= core::hook("movie ExternalInterface call (1.0 UsesActionMap; input10 probe)", 0x01564790,
                         "64A1000000006AFF68????????506489250000000081ECC8000000", (void*)&detail::hkFlashCall,
                         (void**)&s.g_flashCall);
    if (c.focus10)
        ok &= core::installFeature("focus10", {
            {"screen focus gain (1.0 movies)", 0x01559480, "568B742408578BF98B4E50C1E105", (void*)&detail::hkFocusGain,
             (void**)&s.g_focusGain},
            {"screen focus loss (1.0 movies)", 0x01554590, "64A1000000006AFF68????????506489250000000083EC28",
             (void*)&detail::hkFocusLoss, (void**)&s.g_focusLoss},
        });
    if (c.lockTrace10 || c.keys10)
        ok &= core::installFeature("keys10", {
            {"screen action delivery (keys10; input10 probe)", 0x01565330,
             "64A1000000006AFF68????????506489250000000083EC44", (void*)&detail::hkDeliver, (void**)&s.g_deliver},
            {"action map entry state (keys10)", 0x00B48250, "8B44240456578BF1508D4E04", (void*)&detail::hkMapState,
             (void**)&s.g_mapState},
        });
    return ok;
}

}  // namespace di1::movies
