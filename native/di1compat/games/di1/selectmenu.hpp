// selectmenu10 (plan KIO-1): Tab in a 1.0 zone opens 1.0's SelectMenu, the way to the Toy Store. engine/screens.
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

// Installs the ribbon, screen request and Lua AddScreen hooks when c has selectMenu10, all or none (H1, H2).
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

struct State {
    // originals: set once by installFeature in install()
    RibbonNameFn g_ribbonName = nullptr;
    ShowRibbonFn g_showRibbon = nullptr;
    ScreenReqFn g_screenReq = nullptr;
    LuaAddScreenFn g_luaAddScreen = nullptr;
};
inline State g_state;

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
    }
    return ev;
}

// FUN_0156a360, thiscall (L; ret 4), main thread, per Lua AddScreen: 3.0 takes only the table; 1.0 scripts pass names.
inline int __fastcall hkLuaAddScreen(uint8_t* screen, void* edx, void* L)
{
    // a screen served from 1.0 gets the name added as 3.0 adds one by name, with no parent
    if (screen && ((LuaTypeFn)core::rt(eng::kLuaType))(L, 2) == eng::kLuaString) {
        char caller[64] = "";
        engine::screens::copyField(screen, engine::screens::eng::kScript, caller, sizeof(caller));
        for (char* c = caller; *c; ++c) *c = (char)tolower((unsigned char)*c);
        bool from10 = session::screenServed(caller);
        const char* name = ((LuaToStringFn)core::rt(eng::kLuaToString))(L, 2, nullptr);
        if (from10 && name && *name) {
            int player = *(int*)(screen + engine::screens::eng::kPlayer);   // the caller's, from its request
            int h = engine::screens::addByName(name, player);
            core::logf("AddScreen(\"%s\") from 1.0 screen %s, player %d -> screen request %d", name, caller, player, h);
            ((LuaPushIntFn)core::rt(eng::kLuaPushInt))(L, h);
            return 1;
        }
    }
    return g_state.g_luaAddScreen(screen, edx, L);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.selectMenu10) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("selectmenu10", {
        {"ribbon name (1.0 SelectMenu)", 0x00B42540, "518B0D????????8B819C010000", (void*)&detail::hkRibbonName,
         (void**)&s.g_ribbonName},
        {"ribbon open (1.0 SelectMenu)", 0x00B68F60, "6AFF68????????64A100000000", (void*)&detail::hkShowRibbon,
         (void**)&s.g_showRibbon},
        {"screen request (ProgressLog)", 0x0156DDE0, "64A1000000006AFF68", (void*)&detail::hkScreenReq,
         (void**)&s.g_screenReq},
        {"screen AddScreen by name (1.0 scripts)", 0x0156A360, "558BEC83E4F86AFF68????????64A100000000",
         (void*)&detail::hkLuaAddScreen, (void**)&s.g_luaAddScreen},
    });
}

}  // namespace di1::selectmenu
