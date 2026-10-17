// UI glue: a zone's loading screen, FMVs the engine lacks, the tutorial's SelectMenu, context lists. core. Main thread.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include "../core/hooks.hpp"
#include "../core/log.hpp"

namespace engine::ui {

// A stand-in for an FMV the engine lacks: its clip name and the full path of its .bik ("" none).
struct Fmv {
    std::string clip, path;
};

struct Config {
    const char* screenKey = nullptr;   // a zone string key whose value picks the loading screen
    std::function<std::string(const char* value, int& counter)> loadingScreen;   // the pick; empty: hook off
    std::function<Fmv(const char* name)> fmv;                                     // empty: hook off
    bool selectMenu = false;          // tutorial element 61's "SelectMenu" answered by 3.0's ribbon screen
    std::function<std::string(const char* list)> contexts;   // a context list rewritten ("" unchanged); empty: off
};

// Installs the hooks c asks for (H1); false when one was refused or element 61's push was not found.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kSelectMenuText = 0x01D19CDC;  // the literal "SelectMenu", one reference: the push below
constexpr uint32_t kSelectMenuPush = 0x00DE4C5A;  // case 0x3d of FUN_00de3a60: 68 <imm32> pushes the literal
constexpr uint8_t kPushImm32 = 0x68;
constexpr uint32_t kRibbonName = 0x00B42540;      // FUN_00b42540: 3.0's ribbon screen name (thiscall, args unused)
constexpr uint32_t kCStringAssign = 0x0041A250;   // FUN_0041a250: thiscall (const char*) on a CString
constexpr size_t kContextName = 0x98;             // ControllerContextNode +0x98: ContextName (FUN_01224880)
}  // namespace eng

typedef const char*(__fastcall* ZoneStrFn)(void* self, void* edx, const char* key, uint32_t zone, uint32_t a3,
                                           const char* def);
typedef int(__fastcall* FmvPathFn)(void* self, void* edx, char* out, uint32_t size, const char* name);
typedef void*(__fastcall* FindScreenFn)(void* self, void* edx, const char* name, int player, char last);
typedef void(__fastcall* CtxParseFn)(void* self, void* edx);

struct State {
    // originals: set once by hook() in install()
    ZoneStrFn g_zoneStr = nullptr;
    FmvPathFn g_fmvPath = nullptr;
    FindScreenFn g_findScreen = nullptr;
    CtxParseFn g_ctxParse = nullptr;
    Config cfg;                 // set once in install()
    std::mutex screenMx;        // guards counter and pick
    int counter = 0;            // the game's loading-screen rotation, per session
    std::string pick;           // the last pick; the engine reads its text after the lock is released (B-26)
};
inline State g_state;

// FUN_0139e0b0, thiscall (key, zone, a3, default; ret 0x10): a zone string. LoadingScreens: the game's pick, if keyed.
inline const char* __fastcall hkZoneStr(void* self, void*, const char* key, uint32_t zone, uint32_t a3, const char* def)
{
    State& s = g_state;
    const bool screens = key && key[0] == 'L' && !strcmp(key, "LoadingScreens");
    if (!screens) return s.g_zoneStr(self, nullptr, key, zone, a3, def);
    const char* value = s.g_zoneStr(self, nullptr, s.cfg.screenKey, zone, a3, nullptr);
    if (!value || !*value) return s.g_zoneStr(self, nullptr, key, zone, a3, def);
    std::lock_guard<std::mutex> lock(s.screenMx);
    s.pick = s.cfg.loadingScreen(value, s.counter);
    core::logf("loading screen: zone %X has 1.0 LoadingScreen=%s -> %s (1.0 rule)", zone, value, s.pick.c_str());
    return s.pick.c_str();
}

// FUN_0067e460, thiscall (out, size, name; ret 0xc): an FMV's path length, or 0. bink2w32 plays Bink 1 too.
inline int __fastcall hkFmvPath(void* self, void*, char* out, uint32_t size, const char* name)
{
    State& s = g_state;
    int n = s.g_fmvPath(self, nullptr, out, size, name);
    if (n || !out) return n;
    Fmv f = s.cfg.fmv(name);
    if (f.path.empty()) return n;
    if (f.path.size() >= size || GetFileAttributesA(f.path.c_str()) == INVALID_FILE_ATTRIBUTES) return n;
    snprintf(out, size, "%s", f.path.c_str());   // never cut: shorter than size
    core::logf("FMV %s: 3.0 has none -> 1.0 %s", f.clip.c_str(), f.path.c_str());
    return (int)f.path.size();
}

// FUN_0154ff90, thiscall (name, player, last): a screen by name; element 61's "SelectMenu" falls back to the ribbon's.
inline void* __fastcall hkFindScreen(void* self, void*, const char* name, int player, char last)
{
    State& s = g_state;
    void* screen = s.g_findScreen(self, nullptr, name, player, last);
    if (screen || name != (const char*)core::rt(eng::kSelectMenuText)) return screen;
    typedef const char*(__fastcall * RibbonFn)(void*, void*, int, int);
    const char* rib = ((RibbonFn)core::rt(eng::kRibbonName))(nullptr, nullptr, player, 0);
    if (!rib || !_stricmp(rib, name)) return screen;
    screen = s.g_findScreen(self, nullptr, rib, player, last);
    if (screen) core::logf("tutorial element 61: %s open (3.0's ribbon) stands for SelectMenu", rib);
    return screen;
}

// FUN_01224880, thiscall on a ControllerContextNode: its ContextName list parse; the game may rewrite the list first.
inline void __fastcall hkCtxParse(void* self, void*)
{
    State& s = g_state;
    char** name = (char**)((char*)self + eng::kContextName);
    std::string list = *name ? s.cfg.contexts(*name) : std::string();
    if (!list.empty()) {
        typedef void*(__fastcall * StrAssignFn)(void* str, void* edx, const char* v);
        ((StrAssignFn)core::rt(eng::kCStringAssign))(name, nullptr, list.c_str());
        core::logf("controller context %s (3.0 gates of the actions 1.0 kept)", *name);
    }
    s.g_ctxParse(self, nullptr);
}

// Element 61's lookup is redirected only where it was analysed: the push of the literal "SelectMenu".
inline bool selectMenuPushFound()
{
    const uint8_t* push = (const uint8_t*)core::rt(eng::kSelectMenuPush);
    const uint32_t text = (uint32_t)core::rt(eng::kSelectMenuText);
    const bool pushes = push[0] == eng::kPushImm32 && *(const uint32_t*)(push + 1) == text;
    return pushes && !strcmp((const char*)core::rt(eng::kSelectMenuText), "SelectMenu");
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    bool ok = true;
    if (c.loadingScreen)
        ok &= core::hook("zone string field (ui: 1.0 loading screen)", 0x0139E0B0, "83EC08568BF180BEE403000000",
                         (void*)&detail::hkZoneStr, (void**)&s.g_zoneStr);
    if (c.fmv)
        ok &= core::hook("FMV path (ui: 1.0 clips)", 0x0067E460, "81EC0401000053568BB42418010000",
                         (void*)&detail::hkFmvPath, (void**)&s.g_fmvPath);
    if (c.selectMenu && detail::selectMenuPushFound()) {
        ok &= core::hook("screen by name (ui: tutorial element 61)", 0x0154FF90, "837C2404007505",
                         (void*)&detail::hkFindScreen, (void**)&s.g_findScreen);
    } else if (c.selectMenu) {
        core::logf("tutorial10: the element-61 push of \"SelectMenu\" is not where it was analysed: not hooked");
        ok = false;
    }
    if (c.contexts)
        ok &= core::hook("controller context list (ui: 1.0 tutorial contexts)", 0x01224880, "5153558BE98A85A4000000",
                         (void*)&detail::hkCtxParse, (void**)&s.g_ctxParse);
    return ok;
}

}  // namespace engine::ui
