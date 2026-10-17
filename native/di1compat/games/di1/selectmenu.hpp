// selectmenu10 (plan KIO-1): Tab opens 1.0's SelectMenu; pause10: a 1.0 screen's pause ends with it. engine/screens.
// Main thread.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/players.hpp"
#include "../../engine/screens.hpp"
#include "config.hpp"
#include "playsets.hpp"
#include "session.hpp"

namespace di1::selectmenu {

// Installs selectmenu10's hooks, all or none (H1, H2), then pause10's, which need them.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kRibbonName = 0x00B42540;   // FUN_00b42540, hooked here: thiscall (player, a2) -> ribbon screen
constexpr uint32_t kRibbonOwner = 0x0221CEA0;  // *: the object FUN_00b68f60 asks the ribbon name of
constexpr uint32_t kMayOpenUi = 0x014362E0;    // FUN_014362e0: cdecl (player) -> may the player open UI
constexpr uint32_t kBlocked = 0x00DB4110;      // FUN_00db4110: cdecl () -> playset 7 blocked
constexpr uint32_t kEventSetBool = 0x0156C890; // FUN_0156c890: thiscall (key, value) on a screen event
constexpr uint32_t kLuaType = 0x0130D2D0;      // lua_type
constexpr uint32_t kLuaToString = 0x0130D5A0;  // lua_tolstring
constexpr uint32_t kLuaPushInt = 0x0130D7E0;   // lua_pushinteger
constexpr int kLuaString = 4;                  // LUA_TSTRING
constexpr uint32_t kPaused = 0x0225F478;       // bool per player: FUN_0138da80 sets it, FUN_01385bb0 clears it
constexpr int kPauseSlots = 8;                 // FUN_0138da80 writes the flag only for players 0..7
constexpr uint32_t kUnpause = 0x015E91C0;      // FUN_015e91c0: cdecl (player): Pause_UnPauseFromPausedScreenIfPaused
// H4: FUN_015e91c0 starts mov eax,[0x0225F484]; cmp [eax+0x1c],0
constexpr char kUnpausePrologue[] = "A1????????83781C00";
}  // namespace eng

typedef const char*(__fastcall* RibbonNameFn)(void* ecx, void* edx, int player, int a2);
typedef int(__fastcall* ShowRibbonFn)(uint8_t* self, void* edx, int player);
typedef void*(__fastcall* ScreenReqFn)(void* ui, void* edx, const char* name, int a2, int player);
typedef int(__fastcall* LuaAddScreenFn)(uint8_t* screen, void* edx, void* L);
typedef char(__cdecl* CanOpenFn)(int player);
typedef bool(__cdecl* BlockedFn)();
typedef uint8_t(__fastcall* EventSetBoolFn)(void* ev, void* edx, const char* key, uint32_t v);
typedef int(__cdecl* LuaTypeFn)(void* L, int idx);
typedef const char*(__cdecl* LuaToStringFn)(void* L, int idx, size_t* len);
typedef void(__cdecl* LuaPushIntFn)(void* L, int n);
typedef void(__fastcall* ScreenLifeFn)(uint8_t* screen, void* edx);
typedef void(__cdecl* UnpauseFn)(int player);

// The 1.0 screen whose onEnter paused a player, compared only (never read), and whether a screen took the pause over.
struct Lease {
    const uint8_t* screen = nullptr;
    bool handed = false;
};

struct State {
    // originals: set once by installFeature in install()
    RibbonNameFn g_ribbonName = nullptr;
    ShowRibbonFn g_showRibbon = nullptr;
    ScreenReqFn g_screenReq = nullptr;
    LuaAddScreenFn g_luaAddScreen = nullptr;
    ScreenLifeFn g_screenEnter = nullptr, g_screenExit = nullptr;
    Lease leases[eng::kPauseSlots] = {};   // per player; main thread only, cleared at the screen's onExit (H11)
    bool trace = false;                    // uitrace10: set once in install()
};
inline State g_state;

// The screen's Lua script name, lower case, into script; whether that script was served from 1.0.
inline bool script10(const uint8_t* screen, char* script, size_t cap)
{
    engine::screens::copyField(screen, engine::screens::eng::kScript, script, cap);
    for (char* c = script; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return session::screenServed(script);
}

inline bool pauseSlot(int player) { return player >= 0 && player < eng::kPauseSlots; }

inline bool paused(int player) { return ((const uint8_t*)core::rt(eng::kPaused))[player] != 0; }

// The 1.0 pause of player now belongs to a screen that releases it (catalog.lua onExit, missionlog.lua onExit).
inline void handOn(int player, const uint8_t* from, const char* to)
{
    State& s = g_state;
    if (!pauseSlot(player) || !s.leases[player].screen) return;
    if (from && s.leases[player].screen != from) return;
    s.leases[player].handed = true;
    if (s.trace) core::logf("uitrace10: player %d's 1.0 pause handed on to %s", player, to);
}

// uitrace10: a screen's onEnter or onExit, its player's pause flag after it (-1 none) and what its 1.0 pause did.
inline void traceScreen(const uint8_t* screen, const char* what, const char* note)
{
    char name[64] = "", script[64] = "";
    engine::screens::copyField(screen, engine::screens::eng::kName, name, sizeof(name));
    const bool ten = script10(screen, script, sizeof(script));
    const int player = *(const int*)(screen + engine::screens::eng::kPlayer);
    const int flag = pauseSlot(player) ? paused(player) : -1;
    core::logf("uitrace10: screen %s %s (player %d, 1.0 script %d), player paused %d%s", name, what, player, ten, flag,
               note);
}

// The uitrace10 note of a screen's exit: what became of the 1.0 pause it took, if it took one.
inline const char* exitNote(bool held, bool handed, bool released)
{
    if (!held) return "";
    if (handed) return ", its 1.0 pause was handed on";
    return released ? ", pause10 released its 1.0 pause" : ", it had released its 1.0 pause";
}

// FUN_00b42540, thiscall, main thread, per ribbon query: 3.0's "MissionLog"; 1.0's was SelectMenu (FUN_00c32cf0).
inline const char* __fastcall hkRibbonName(void* ecx, void* edx, int player, int a2)
{
    const char* r = g_state.g_ribbonName(ecx, edx, player, a2);
    return r && playsets::currentZone10() ? "SelectMenu" : r;
}

// FUN_00b68f60, thiscall (player; ret 4), main thread, per Tab: SelectMenu is added by name, as 1.0's opener did.
inline int __fastcall hkShowRibbon(uint8_t* self, void* edx, int player)
{
    void* owner = *(void**)core::rt(eng::kRibbonOwner);
    const char* name = ((RibbonNameFn)core::rt(eng::kRibbonName))(owner, nullptr, player, 1);
    if (!name || strcmp(name, "SelectMenu")) {
        core::logf("ribbon: Tab (ShowRibbonUI) for player %d, ribbon '%s' -> 3.0's opener", player, name ? name : "?");
        return g_state.g_showRibbon(self, edx, player);
    }
    char can = ((CanOpenFn)core::rt(eng::kMayOpenUi))(player);
    bool blocked = ((BlockedFn)core::rt(eng::kBlocked))();
    if (!can || blocked) {   // probe (Cars: Tab opened nothing)
        core::logf("ribbon: Tab (ShowRibbonUI) for player %d, SelectMenu refused: may open UI %d, playset blocked %d",
                   player, can, blocked);
        return -1;
    }
    int h = engine::screens::addByName("SelectMenu", player);
    core::logf("ribbon: SelectMenu for player %d -> screen %d", player, h);
    return -1;   // as 3.0's refusal: FUN_00df6230 then skips OverrideModeToRibbon for the screen (GP-17)
}

// FUN_0156dde0, thiscall, main thread, per event screen request: 1.0's ProgressLog event is 3.0's MissionLog.
inline void* __fastcall hkScreenReq(void* ui, void* edx, const char* name, int a2, int player)
{
    bool log10 = name && !strcmp(name, "ProgressLog");
    void* ev = g_state.g_screenReq(ui, edx, log10 ? "MissionLog" : name, a2, player);
    // pauseGame as 3.0's opener sets it for one local player (0x00B69025): else closing the log left the game paused
    uint8_t* pm = log10 && ev ? engine::players::manager() : nullptr;
    if (pm && engine::players::localCount(pm) == 1) {
        ((EventSetBoolFn)core::rt(eng::kEventSetBool))(ev, nullptr, "pauseGame", 1);
        core::logf("screen request ProgressLog -> MissionLog for player %d, pauseGame "
                   "(the log releases SelectMenu's pause)",
                   player);
        handOn(player, nullptr, "MissionLog");
    }
    if (g_state.trace)
        core::logf("uitrace10: event screen request %s for player %d: %s", name ? name : "?", player,
                   ev ? "made" : "refused");
    return ev;
}

// FUN_0156a360, thiscall (L; ret 4), main thread, per Lua AddScreen: 3.0 takes only the table; 1.0 scripts pass names.
inline int __fastcall hkLuaAddScreen(uint8_t* screen, void* edx, void* L)
{
    // a screen served from 1.0 gets the name added as 3.0 adds one by name, with no parent
    if (screen && ((LuaTypeFn)core::rt(eng::kLuaType))(L, 2) == eng::kLuaString) {
        char caller[64] = "";
        bool from10 = script10(screen, caller, sizeof(caller));
        const char* name = ((LuaToStringFn)core::rt(eng::kLuaToString))(L, 2, nullptr);
        if (from10 && name && *name) {
            int player = *(int*)(screen + engine::screens::eng::kPlayer);   // the caller's, from its request
            int h = engine::screens::addByName(name, player);
            core::logf("AddScreen(\"%s\") from 1.0 screen %s, player %d -> screen request %d", name, caller, player, h);
            if (h != -1) handOn(player, screen, name);   // a refused add leaves the pause with the caller
            ((LuaPushIntFn)core::rt(eng::kLuaPushInt))(L, h);
            return 1;
        }
    }
    return g_state.g_luaAddScreen(screen, edx, L);
}

// The release a 3.0 screen's onExit makes (missionlog.lua), for a 1.0 screen that left its pause behind.
inline void releasePause(const uint8_t* screen, int player)
{
    ((UnpauseFn)core::rt(eng::kUnpause))(player);
    char name[64] = "";
    engine::screens::copyField(screen, engine::screens::eng::kName, name, sizeof(name));
    core::logf("pause10: %s (1.0 screen) left without releasing or handing on its pause: player %d unpaused "
               "(3.0's onExit release)",
               name, player);
}

// FUN_01551d70, thiscall, main thread, once per screen as FUN_0156b200 creates it: the screen's Lua onEnter.
inline void __fastcall hkScreenEnter(uint8_t* screen, void* edx)
{
    State& s = g_state;
    const int player = screen ? *(const int*)(screen + engine::screens::eng::kPlayer) : -1;
    const bool before = pauseSlot(player) && paused(player);
    s.g_screenEnter(screen, edx);
    char script[64] = "";
    const bool pausedHere = pauseSlot(player) && !before && paused(player);
    // selectmenu.lua's onEnter pauses; only its ExitScreen releases or hands on, 3.0's screens release in onExit
    const bool took = pausedHere && script10(screen, script, sizeof(script));
    if (took) s.leases[player] = {screen, false};
    if (s.trace && screen) traceScreen(screen, "enter", took ? ", it took the pause" : "");
}

// FUN_01551eb0, thiscall, main thread, once per screen in its teardown FUN_01565750: the screen's Lua onExit.
inline void __fastcall hkScreenExit(uint8_t* screen, void* edx)
{
    State& s = g_state;
    const int player = screen ? *(const int*)(screen + engine::screens::eng::kPlayer) : -1;
    s.g_screenExit(screen, edx);
    const bool held = pauseSlot(player) && s.leases[player].screen == screen;
    const bool handed = held && s.leases[player].handed;
    if (held) s.leases[player] = {};
    const bool release = held && !handed && paused(player);
    if (release) releasePause(screen, player);
    if (s.trace && screen) traceScreen(screen, "exit", exitNote(held, handed, release));
}

// pause10's hooks; they need selectmenu10's handoff hooks (H2).
inline bool installPause()
{
    State& s = g_state;
    if (!core::prologueMatches(eng::kUnpause, eng::kUnpausePrologue)) {
        core::logf("feature pause10 off: Pause_UnPauseFromPausedScreenIfPaused @ 015E91C0 differs from the analysis");
        return false;
    }
    return core::installFeature("pause10", {
        {"screen onEnter (pause10: a 1.0 screen's pause)", 0x01551D70, "83EC1C535556578D7140",
         (void*)&hkScreenEnter, (void**)&s.g_screenEnter},
        {"screen onExit (pause10: a 1.0 screen's pause)", 0x01551EB0, "83EC148B41405355568D714057",
         (void*)&hkScreenExit, (void**)&s.g_screenExit},
    });
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.selectMenu10) return true;
    detail::State& s = detail::g_state;
    s.trace = c.uiTrace10;
    const bool menu = core::installFeature("selectmenu10", {
        {"ribbon name (1.0 SelectMenu)", 0x00B42540, "518B0D????????8B819C010000", (void*)&detail::hkRibbonName,
         (void**)&s.g_ribbonName},
        {"ribbon open (1.0 SelectMenu)", 0x00B68F60, "6AFF68????????64A100000000", (void*)&detail::hkShowRibbon,
         (void**)&s.g_showRibbon},
        {"screen request (ProgressLog)", 0x0156DDE0, "64A1000000006AFF68", (void*)&detail::hkScreenReq,
         (void**)&s.g_screenReq},
        {"screen AddScreen by name (1.0 scripts)", 0x0156A360, "558BEC83E4F86AFF68????????64A100000000",
         (void*)&detail::hkLuaAddScreen, (void**)&s.g_luaAddScreen},
    });
    if (!c.pause10) return menu;
    if (!menu) core::logf("feature pause10 off: it needs selectmenu10's hooks");
    return menu && detail::installPause();
}

}  // namespace di1::selectmenu
