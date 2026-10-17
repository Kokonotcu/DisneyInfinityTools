// Gameplay and menu runtime: selectmenu10, actionmap10, focus10, keys10, accept10, tools10. core, games/di1.
// Game thread.
#pragma once
#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "core/hooks.hpp"
#include "core/log.hpp"
#include "core/memcheck.hpp"
#include "core/zip.hpp"
#include "gameplay.hpp"
#include "games/di1/config.hpp"
#include "games/di1/session.hpp"
#include "games/di1/vfs.hpp"
#include "sys.hpp"
#include "third_party/miniz/miniz.h"

namespace gprt {

// core's services this file calls by their short names (declarations: N26 forbids only using namespace)
using core::hook;
using core::installFeature;
using core::logf;
using core::rt;
using core::safeCopy;

struct State {
    di1::Config cfg;   // the keys its hooks read: set once in install(), before them
};
inline State g_state;

// ---- selectmenu10: the 1.0 playset ribbon, the way to the Toy Store (plan KIO-1) ----
// The story needs the Toy Store right after DockFire: Investigate_Explosions waits on Eval_LockState
// RC_INC_MrInc_SportsCar == PURCHASED (investigate_explosions.lua; text "First you must buy the car from the toy
// store"), Buy_Training_Facility / Buy_Tech_Building / Build_SMPrison on buildings bought there. 1.0 reached it with
// Tab: TOY_STORE -> ShowRibbonUI -> FUN_00c37230 opens UI_GetRibbonName = FUN_00c32cf0: zone key worldIsPlayset ->
// "SelectMenu" (1.0 selectmenu.lua: Scn_Pause_MissionLog -> UI_TriggerEvent("ProgressLog"), Scn_Pause_Catalog ->
// AddScreen("Catalog")). 3.0's ribbon FUN_00b42540 is "MissionLog" in a playset and its opener FUN_00b68f60 knows only
// MissionLog / RibbonToyBox, so no 1.0 store screen was reachable. 3.0 kept the rest: SelectMenu in containerdata.lua,
// the Catalog screen (3.0 catalog.lua = 1.0's, same Catalog_* natives, 1.0's storefront.gfx), and the engine-bound
// screen methods 1.0's selectmenu.lua / basemenu.lua call (AddScreen, RemoveScreen, CallFlashFunction: 1.0
// FUN_01051440, 3.0 FUN_0156b200); both scripts are 1.0-only and served from 1.0. 3.0 reads zone keys with
// lua_gettable (FUN_0139be30 -> FUN_00445190), and 3.0 zone records spell the key WorldIsPlayset (shipped 3.0 zone
// list: 0 x worldIsPlayset, 1.0's: 47), so 1.0's own test selects 1.0 zones only.
typedef char(__fastcall* ZoneBoolFn)(void* mgr, void* edx, const char* key, uint32_t zone, uint32_t a3, uint32_t def);
typedef const char*(__fastcall* RibbonNameFn)(void* ecx, void* edx, int player, int a2);
typedef int(__fastcall* ShowRibbonFn)(uint8_t* self, void* edx, int player);
typedef void*(__fastcall* ScreenReqFn)(void* ui, void* edx, const char* name, int a2, int player);
typedef void(__fastcall* ThisFn)(void* self, void* edx);
typedef void*(__fastcall* StrAssignFn)(void* str, void* edx, const char* v);
typedef int(__fastcall* AddScreenFn)(void* ui, void* edx, void* req);
typedef char(__cdecl* CanOpenFn)(int player);
typedef bool(__cdecl* BlockedFn)();
static RibbonNameFn g_ribbonName;
static ShowRibbonFn g_showRibbon;
static ScreenReqFn g_screenReq;

static bool zonePlayset10()   // 1.0 FUN_00c32cf0's test, read by 3.0's zone-key reader FUN_0139de80
{
    uint8_t* mgr = *(uint8_t**)rt(0x0225F548);
    return mgr && ((ZoneBoolFn)rt(0x0139DE80))(mgr, nullptr, "worldIsPlayset", *(uint32_t*)(mgr + 0x19C), 0, 0) != 0;
}

static const char* __fastcall hkRibbonName(void* ecx, void* edx, int player, int a2)
{
    const char* r = g_ribbonName(ecx, edx, player, a2);
    return r && zonePlayset10() ? "SelectMenu" : r;
}

// The opener for "SelectMenu", with 3.0's own gates of FUN_00b68f60's MissionLog branch (FUN_014362e0 may the player
// open UI, FUN_00db4110 playset 7 blocked). That branch opens MissionLog through a UI event template (FUN_0156dde0
// + FUN_0156df40), but no event names SelectMenu in either game (eventtoscreenmap.lua 1.0 / 3.0): 1.0's opener
// FUN_00c37230 added the screen by name (UI manager vtable +0x6c). 3.0's add-by-name is the screen request
// FUN_00b670a0 builds for the ribbon: ctor FUN_0155a630, name CString +0x40 (assign FUN_0041a250), player +0x6c,
// FUN_015699b0 on [0x02289E88] (thiscall, ret 4; -1 = refused), dtor FUN_0155c270. No pauseGame parameter: 1.0's
// opener passed none and selectmenu.lua pauses in onEnter (Pause_PauseGame).
static int __fastcall hkShowRibbon(uint8_t* self, void* edx, int player)
{
    const char* name = ((RibbonNameFn)rt(0x00B42540))(*(void**)rt(0x0221CEA0), nullptr, player, 1);
    if (!name || strcmp(name, "SelectMenu")) {
        logf("ribbon: Tab (ShowRibbonUI) for player %d, ribbon '%s' -> 3.0's opener", player, name ? name : "?");
        return g_showRibbon(self, edx, player);
    }
    char can = ((CanOpenFn)rt(0x014362E0))(player);
    bool blocked = ((BlockedFn)rt(0x00DB4110))();
    if (!can || blocked) {   // probe (Cars: Tab opened nothing, run-cars-0545 has no ribbon line)
        logf("ribbon: Tab (ShowRibbonUI) for player %d, SelectMenu refused: may open UI %d, playset blocked %d", player, can, blocked);
        return -1;
    }
    alignas(8) uint8_t req[0x100] = {};   // the ctor sets 0x00..0x93
    ((ThisFn)rt(0x0155A630))(req, nullptr);
    ((StrAssignFn)rt(0x0041A250))(req + 0x40, nullptr, "SelectMenu");
    *(int*)(req + 0x6C) = player;
    int h = ((AddScreenFn)rt(0x015699B0))(*(void**)rt(0x02289E88), nullptr, req);
    ((ThisFn)rt(0x0155C270))(req, nullptr);
    logf("ribbon: SelectMenu for player %d -> screen %d", player, h);
    return -1;
}

// 1.0's ProgressLog screen is 3.0's MissionLog: 1.0's eventtoscreenmap.lua maps the event ProgressLog to the
// ProgressLog screen, 3.0's has no ProgressLog and maps MissionLog to MissionLog; tutorial element 62 is "ProgressLog"
// in 1.0 (FUN_00ba3950) and "MissionLog" in 3.0 (FUN_00de3a60). selectmenu.lua's Mission Log entry asks for the 1.0
// event (UI_TriggerEvent -> FUN_0156dde0, the event-template request).
// The pause: 1.0's SelectMenu pauses in onEnter (Pause_PauseGame) and, for its Mission Log entry, removes itself and
// leaves the pause to the log. 3.0's MissionLog releases a pause only when its event carries pauseGame (onExit /
// onHidden -> Pause_UnPauseFromPausedScreenIfPaused), which 3.0's own opener sets for one local player (FUN_00b68f60
// 0x00B69025: player manager +0x90 == 1 -> FUN_0156c890(event, "pauseGame", 1)). The renamed 1.0 event had none, so
// closing the log (Guide Me, Esc) left the game paused: dump 2026-10-14 17:25 (guideme-freeze.dmp) has player 0
// paused (DAT_0225f478[0] = 1), no pausing screen left in the screen manager, selectmenu.lua's strings loaded.
typedef uint8_t(__fastcall* EventSetBoolFn)(void* ev, void* edx, const char* key, uint32_t v);
static void* __fastcall hkScreenReq(void* ui, void* edx, const char* name, int a2, int player)
{
    bool log10 = name && !strcmp(name, "ProgressLog");
    void* ev = g_screenReq(ui, edx, log10 ? "MissionLog" : name, a2, player);
    uint8_t* pm = log10 && ev ? *(uint8_t**)rt(0x0225F540) : nullptr;
    if (pm && *(int*)(pm + 0x90) == 1) {
        ((EventSetBoolFn)rt(0x0156C890))(ev, nullptr, "pauseGame", 1);
        logf("screen request ProgressLog -> MissionLog for player %d, pauseGame (the log releases SelectMenu's pause)", player);
    }
    return ev;
}

// The Toy Store entry: selectmenu.lua's ExitScreen does self:RemoveScreen(), then self:AddScreen("Catalog"), leaving
// SelectMenu's pause to the Catalog (3.0 catalog.lua onExit -> Pause_UnPauseFromPausedScreen). The screen method
// AddScreen takes its argument in two forms in 1.0 (FUN_0104a600): a table, or a screen name (+ optional data) that it
// adds by name (UI manager vtable +0x6c). 3.0's (FUN_0156a360, thiscall(screen, L), ret 4) kept only the table:
// for a name it just reads the string and returns -1 (0x0156AB.. else branch). So no Catalog request was ever queued
// and the pause stayed (dump toystore-freeze.dmp: no Catalog screen, catalog.lua never loaded, player 0 paused;
// Crabe's log: no loadbuffer Presentation/Catalog.lua). 3.0's own pause menu opens it as
// self:AddScreen({screenName = "Catalog"}) (pausemenu.lua ExitScreen, Scn_Pause_Catalog). 1.0's UI scripts use the
// name form 298 times, 3.0's 23 (dead branches: friends, iOS) -- addscreenform.py. For a screen whose script was
// served from 1.0 (g_screens10), a name is added the way 3.0 adds one by name itself (FUN_00b670a0, the kiosk ribbon:
// request ctor FUN_0155a630, name +0x40, player +0x6c, FUN_015699b0 on the manager, dtor FUN_0155c270): no parent
// (+0x60 = -1), so it opens although the caller is already gone. The player is the caller's (screen +0x50, set from
// its request +0x6c by FUN_0156b200).
typedef int(__fastcall* LuaAddScreenFn)(uint8_t* screen, void* edx, void* L);
typedef int(__cdecl* LuaTypeFn)(void* L, int idx);
typedef const char*(__cdecl* LuaToStringFn)(void* L, int idx, size_t* len);
typedef void(__cdecl* LuaPushIntFn)(void* L, int n);
static LuaAddScreenFn g_luaAddScreen;
static int __fastcall hkLuaAddScreen(uint8_t* screen, void* edx, void* L)
{
    if (screen && ((LuaTypeFn)rt(0x0130D2D0))(L, 2) == 4) {   // LUA_TSTRING
        char caller[64] = "";
        safeCopy(caller, sizeof(caller), *(const char**)(screen + 0xc));
        for (char* c = caller; *c; ++c) *c = (char)tolower((unsigned char)*c);
        bool from10;
        from10 = di1::session::screenServed(caller);
        const char* name = ((LuaToStringFn)rt(0x0130D5A0))(L, 2, nullptr);
        if (from10 && name && *name) {
            int player = *(int*)(screen + 0x50);
            alignas(8) uint8_t req[0x100] = {};   // the ctor sets 0x00..0x93
            ((ThisFn)rt(0x0155A630))(req, nullptr);
            ((StrAssignFn)rt(0x0041A250))(req + 0x40, nullptr, name);
            *(int*)(req + 0x6C) = player;
            int h = ((AddScreenFn)rt(0x015699B0))(*(void**)rt(0x02289E88), nullptr, req);
            ((ThisFn)rt(0x0155C270))(req, nullptr);
            logf("AddScreen(\"%s\") from 1.0 screen %s, player %d -> screen request %d", name, caller, player, h);
            ((LuaPushIntFn)rt(0x0130D7E0))(L, h);
            return 1;
        }
    }
    return g_luaAddScreen(screen, edx, L);
}

// ---- actionmap10: a 1.0 movie's UsesActionMap sets its screen's action map, as 1.0 did ----
// A movie receives a controller action only if its screen's action map (screen +0: {vt, ids, count}) holds it.
// Movies name theirs through ExternalInterface (callback at 0x01564790, cdecl (?, ?, name, args, ..); args[0] = the
// screen's Lua name, FUN_01550050; Values 0x20 apart, type +0xc, value +0x10): 1.0 (0x01050a3e) handled
// "UsesActionMap" as map->vt[0](list) = set (3.0 vt[0] FUN_00b3fef0 -> FUN_007f3620: clear, then add each name) and
// "AppendActionMap" as add; 3.0 adds for AppendActionMap (create via manager vt+0x78, FUN_007f3580) and ignores
// UsesActionMap (0x01564f7d). 1.0 movies call only UsesActionMap (88 of 249; 3.0's own: 9, which 3.0 ignores).
// The Toy Store's Catalog (3.0 catalog.lua on 1.0's storefront.gfx; storefront.Init: UsesActionMap
// 'CATALOG_PURCHASE_OR_PLACE,CATALOG_CANCEL,CATALOG_LEFT,..', then SetHasFocus(true) itself) kept 3.0's default map
// (dump catalog-input-0649: START, RIGHT, LEFT, DOWN, UP, .._NOREP, RT, LT, RSTICK_*), and its keyDownListener acts
// only on CATALOG_*: no key, Esc included, did anything. For a screen whose movie was served from 1.0 (+0x14 in
// g_movies10) the map is set from the list (clear, then 3.0's AppendActionMap). The map holds entries of the
// player's UI control mapper (manager +0xd54 + slot*0x1bc4, names -> entries at +0x18); it has every CATALOG_* entry
// with the key codes the movie's KeyMap expects (dump: CATALOG_PURCHASE_OR_PLACE 133, CATALOG_CANCEL 134,
// CATALOG_UP..VIEW_COMMERCIAL 171-175). The map alone is not enough: see focus10 below.
typedef void(__cdecl* FlashCallFn)(void* a1, void* a2, const char* name, uint8_t* args, void* a5);
typedef int(__fastcall* ScreenByNameFn)(void* mgr, void* edx, const char* name);
typedef uint8_t*(__fastcall* ScreenByHandleFn)(void* mgr, void* edx, int handle);
typedef void(__fastcall* ActionMapClearFn)(void* map, void* edx);
static FlashCallFn g_flashCall;

static bool movie10(const uint8_t* screen)
{
    char m[96] = "";
    safeCopy(m, sizeof(m), screen ? *(const char* const*)(screen + 0x14) : nullptr);
    if (!*m) return false;
    std::string k = std::string("flash/") + m;
    for (char& c : k) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    return di1::session::movieServed(k);
}

static const char* gfxString(const uint8_t* v)   // a GFx Value (type +0xc: 6 string, bit 6 managed = one more hop)
{
    if (!sys::readable(v, 0x14)) return nullptr;
    uint32_t t = *(const uint32_t*)(v + 0xc);
    const uint8_t* p = *(const uint8_t* const*)(v + 0x10);
    if ((t & 0x8f) != 6) return nullptr;
    if (t & 0x40) p = sys::readable(p, 4) ? *(const uint8_t* const*)p : nullptr;
    return (const char*)p;
}

static uint8_t* screenOfCall(const uint8_t* args, char* lua, size_t cap)   // args[0] = the screen's Lua name
{
    safeCopy(lua, cap, gfxString(args));
    void* mgr = *(void**)rt(0x02289E88);
    int h = *lua && mgr ? ((ScreenByNameFn)rt(0x01550050))(mgr, nullptr, lua) : -1;
    return h != -1 ? ((ScreenByHandleFn)rt(0x0154FED0))(mgr, nullptr, h) : nullptr;
}

static volatile LONG g_inputLines;   // probe budget (locktrace10): movie calls, focus, delivery gates, actions
static bool inputRoom() { return g_state.cfg.lockTrace10 && InterlockedIncrement(&g_inputLines) <= 600; }

static void __cdecl hkFlashCall(void* a1, void* a2, const char* name, uint8_t* args, void* argc)
{
    char lua[64] = "";
    uint8_t* s = name && args ? screenOfCall(args, lua, sizeof(lua)) : nullptr;
    bool ten = s && movie10(s);
    if (ten && !strcmp(name, "UsesActionMap") && g_state.cfg.actionMap10) {
        if (void* map = *(void**)s) (*(ActionMapClearFn**)map)[1](map, nullptr);   // vt[1] FUN_007f3640: empty it
        logf("actionmap10: %s (1.0 movie) UsesActionMap -> its map is the movie's list", lua);
        name = "AppendActionMap";
    }
    // probe: what the 1.0 movie asks of its screen (PlaySound names, ExitScreen, BuyItem, ...); genericlist asks
    // CanChangeSelection every frame while a key is held, which used up the budget in run-in-store5
    if (ten && strcmp(name, "CanChangeSelection") && inputRoom()) {
        char a[64] = "";
        if ((uintptr_t)argc >= 2) safeCopy(a, sizeof(a), gfxString(args + 0x20));
        logf("input10: movie of %s calls %s('%s'), %u args", lua, name, a, (unsigned)(uintptr_t)argc);
    }
    g_flashCall(a1, a2, name, args, argc);
}

// focus10: the manager tells the top screen's movie it has the focus (FUN_01559480 gain, FUN_01554590 loss; thiscall
// on the manager, ret 4). A screen with HasScreenFSM (3.0's Screen) gets root.GainFocus()/LoseFocus() and the movie
// answers CallLua("NotifyFocusChange", b) (callback 0x01564e2a: screen +0x60 = b); any other screen gets
// root.SetHasFocus(b) from FUN_01552390, which also sets +0x60 itself. +0x60 gates every action: the per-frame
// delivery FUN_01565330 (thiscall(screen, a1, a2), ret 8) passes a screen's map entries to its movie only while +0x60
// is set (0x01565400), the movie loaded (+0x6d) and neither hidden (+0x6c) nor hiding (+0x6b). A 1.0 movie has no
// GainFocus and never sends NotifyFocusChange, so the Catalog (3.0 catalog.lua, a Screen, on 1.0's storefront.gfx)
// kept +0x60 = 0 (dump catalog-input-0649: Catalog45 the only screen of player 0's focus list, +0x60 0): no action
// reached it. (8ed4a27 called FUN_01552390 but put 3.0's +0x60 back afterwards, which undid exactly this.) A screen
// whose movie was served from 1.0 now gets the non-FSM treatment, FUN_01552390(focus), after 3.0's own.
typedef void(__fastcall* FocusGainFn)(uint8_t* mgr, void* edx, uint8_t* screen);
typedef void(__fastcall* FocusLossFn)(uint8_t* mgr, void* edx, uint8_t** holder);
typedef void(__fastcall* SetHasFocusFn)(uint8_t* screen, void* edx, int focus);
typedef void(__fastcall* DeliverFn)(uint8_t* screen, void* edx, void* a1, void* a2);
typedef char(__fastcall* ActionStateFn)(void* map, void* edx, int i, uint32_t* key, uint8_t* b1, uint8_t* b2);
static FocusGainFn g_focusGain;
static FocusLossFn g_focusLoss;
static DeliverFn g_deliver;

static void movieFocus10(uint8_t* screen, bool focus, const char* why)
{
    if (!screen || !screen[0x6d] || !movie10(screen)) return;
    ((SetHasFocusFn)rt(0x01552390))(screen, nullptr, focus ? 1 : 0);
    char name[64] = "";
    safeCopy(name, sizeof(name), *(const char**)(screen + 0x10));
    logf("focus10: %s (1.0 movie) %s: SetHasFocus(%d), +0x60 now %d", name, why, focus ? 1 : 0, screen[0x60]);
}

static void __fastcall hkFocusGain(uint8_t* mgr, void* edx, uint8_t* screen)
{
    g_focusGain(mgr, edx, screen);
    uint8_t* head = screen ? *(uint8_t**)(mgr + 0x28C + *(int*)(screen + 0x50) * 0x20) : nullptr;   // the top: what 3.0 focused
    uint8_t** holder = head ? *(uint8_t***)(head + 8) : nullptr;
    movieFocus10(holder ? holder[0] : nullptr, true, "gain");
}

static void __fastcall hkFocusLoss(uint8_t* mgr, void* edx, uint8_t** holder)
{
    g_focusLoss(mgr, edx, holder);
    movieFocus10(holder ? holder[0] : nullptr, false, "loss");
}

// keys10: where an action goes. 1.0 handed a screen's mapped actions to its movie as Flash key events whose keyCode is
// the action's code (storefront.keyDownListener: keyCode == KeyMap.CATALOG_CANCEL 134 -> _Hide -> list clip frame 30
// -> CallLua("ExitScreen"); CATALOG_PURCHASE_OR_PLACE 133 -> BuyCurrentItem -> CallLua("BuyItem")). 3.0 hands them to
// the screen's Lua instead: the delivery FUN_01565330 queries each map entry (vt[5] FUN_00b48250 -> FUN_00b48120:
// key +0x20, down +0x10, released +0x11) and calls Lua onKeyDown / onKeyUp {action = key, controllerId} on the
// screen (0x01565552 / 0x015655c3, FUN_015604a0; 204 3.0 scripts define onKeyDown, no 1.0 script does). The movie
// gets only raw keyboard events (FUN_007f3320: KeyDown, type 5, keyCode 8/9/13/27/37-40/46, into
// FUN_015520c0(screen) +0x3c, vt+0x8c HandleEvent). 3.0's catalog.lua has no onKeyDown, so run-in-store4 has 48 x
// key 133 and 22 x 134 queried with focus open and not one movie call after them. For a screen whose movie came from
// 1.0 and whose script is 3.0's (not in g_screens10: a 1.0 script's screen keeps today's path, SelectMenu works on
// raw keys and would see its select twice), each action 3.0 finds down is also sent to the movie as 1.0 did: a
// KeyDown event with that keyCode, built as FUN_007f3320 builds one, into the same movie. The movie's checks pass:
// IsMyPlayer returns true for playerNum < 0 and 3.0 never sets playerNum (its trait default is -2; 3.0 writes only
// luaName, 0x015646ff), and HasFocus is true (storefront.Init ends with SetHasFocus(true); focus10 too).
typedef char(__fastcall* MapStateFn)(void* map, void* edx, int i, uint32_t* key, uint8_t* down, uint8_t* up);
typedef uint32_t(__fastcall* HandleEventFn)(void* movie, void* edx, const void* ev);
static MapStateFn g_mapState;
static uint8_t* g_keysScreen;   // set while FUN_01565330 runs for a 3.0-script screen on a 1.0 movie (main thread only)
static void* g_keysMovie;

static bool script10(const uint8_t* screen)
{
    char t[64] = "";
    safeCopy(t, sizeof(t), *(const char* const*)(screen + 0xc));
    for (char* c = t; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return di1::session::screenServed(t);
}

static char __fastcall hkMapState(void* map, void* edx, int i, uint32_t* key, uint8_t* down, uint8_t* up)
{
    char r = g_mapState(map, edx, i, key, down, up);
    if (r && g_keysScreen && key && down && up && (*down || *up)) {
        char name[64] = "";
        safeCopy(name, sizeof(name), *(const char**)(g_keysScreen + 0x10));
        bool sent = false;
        if (*down && g_keysMovie && g_state.cfg.keys10) {
            // as FUN_007f3320 fills its KeyDown (0x007f3451..0x007f3480): type 5, modifiers, keyCode, +0xc and +0x14 =
            // the player slot (its arg 1; here the map's slot +0x20, which FUN_00b48250 queries), +0x10 = 0
            uint32_t slot = *(const uint8_t*)((const uint8_t*)map + 0x20);
            uint32_t ev[6] = {5, 0, *key, slot, 0, slot};
            (*(HandleEventFn**)g_keysMovie)[0x8C / 4](g_keysMovie, nullptr, ev);
            sent = true;
        }
        if (inputRoom())
            logf("input10: %s action key %u (map entry %d) %s -> %s", name, *key, i, *down ? "down" : "released",
                 sent ? "sent to the movie as a key event" : "not sent");
    }
    return r;
}

// probe (locktrace10): the delivery gates of a screen on a 1.0 movie whenever they change.
static void __fastcall hkDeliver(uint8_t* screen, void* edx, void* a1, void* a2)
{
    bool ten = screen && movie10(screen);
    if (ten && g_state.cfg.lockTrace10) {
        static uint8_t* last;
        static uint32_t lastGates = ~0u;
        void* map = *(void**)screen;
        uint32_t n = map ? *(uint32_t*)((uint8_t*)map + 8) : 0;
        uint32_t gates = screen[0x60] | screen[0x6d] << 1 | screen[0x6c] << 2 | screen[0x6b] << 3 | n << 8 | (a1 ? 1u << 31 : 0);
        char name[64] = "";
        safeCopy(name, sizeof(name), *(const char**)(screen + 0x10));
        if ((screen != last || gates != lastGates) && inputRoom())
            logf("input10: %s delivery gates: focus(+0x60) %d loaded %d hidden %d hiding %d, map %u actions, input %p, 1.0 script %d",
                 name, screen[0x60], screen[0x6d], screen[0x6c], screen[0x6b], n, a1, script10(screen));
        last = screen, lastGates = gates;
    }
    if (ten && a1 && !script10(screen)) {
        g_keysScreen = screen;
        g_keysMovie = *(void**)((uint8_t*)a1 + 0x3C);
        // probe: every UI action of the player's mapper that turns down while this screen takes input, in or out of
        // its map (the mapper: manager +0xd54 + slot * 0x1bc4; entries +0xc / count +0x10; entry +0x10 down, +0x20 key)
        void* map = *(void**)screen;
        uint8_t* mgr = *(uint8_t**)rt(0x02289E88);
        uint32_t slot = map ? *(const uint8_t*)((const uint8_t*)map + 0x20) : 0;
        uint8_t* mp = mgr && slot < 11 ? mgr + 0xD54 + slot * 0x1BC4 : nullptr;
        uint8_t** es = mp ? *(uint8_t***)(mp + 0xC) : nullptr;
        uint32_t ne = mp ? *(uint32_t*)(mp + 0x10) : 0;
        static uint8_t wasDown[256];
        for (uint32_t i = 0; es && i < ne && i < 256; ++i) {
            uint8_t d = es[i] ? es[i][0x10] : 0;
            if (d && !wasDown[i] && inputRoom())
                logf("input10: %s player slot %u UI action down: mapper entry %u key %u", *(const char**)(screen + 0x10), slot, i,
                     *(uint32_t*)(es[i] + 0x20));
            wasDown[i] = d;
        }
    }
    g_deliver(screen, edx, a1, a2);
    g_keysScreen = nullptr, g_keysMovie = nullptr;
}

// ---- accept10: a 1.0 mission activated in the world counts as accepted for the tutorial's log step ----
// The "Press E to activate the mission" box is Mission_Tutorial's Text Dialog3 (@IN_City_MissionLog_Accept): shown at
// the Activate of task 13 (Eval_TutorialElement 63) and removed only at that task's Success (presentation list
// mission_tutorial:15 -> TextDialog.Remove) or the mission's (list mission_tutorial:1). Element 63 completes on one
// event: 1.0 posts MissionLog_MissionAccepted from the log's accept (0x00c3dc96, id [0x016bf920]; handler
// 0x00b54620), 3.0 the same slot from Mission_MissionActivated (FUN_00b67560 -> FUN_0069efe0([0x0221cdcc],
// player id, 0)); the only handler is the element's (3.0 FUN_00dae4e0: element +0xc0 == 0x3f -> +0xc5 |= 0x20, read
// by the evaluator FUN_00de3a60 case 0x3f). 1.0 kept the player in the log for that step
// (UI_TUT_DISABLE_PROG_LOG_*,-.._ACTIVATE_MISSION: no cancel), but 3.0's MissionLog runs on GN_LST_* actions, so the
// player can leave it (run-in-store5: MissionLog52/61/65 closed, IN_City_TeachTrapped activated at its giver at
// 11:29:39) and the box stays. Forcing the 1.0 lock onto GN_LST_* would trap the player on 3.0's E = GuideMission,
// which aborts the selected mission when it is the active one (missionlog.lua GuideMission: IsSelected & canAbort
// -> AbortMission; that log opened on the active Investigate_Explosions). So in a 1.0 zone a mission's activation
// (FUN_015bcc10, the MissionNode's one-time activation, +0x3db bit 7) also posts that event for each local player,
// as Mission_MissionActivated does (player id: FUN_004291b0; 3.0's player count is 4, FUN_01204cf0).
typedef void(__fastcall* MissionActivateFn)(uint8_t* mission, void* edx);
typedef void*(__fastcall* PlayerIdFn)(void* pm, void* edx, uint32_t* out, int player);
typedef void(__cdecl* PostEventFn)(uint32_t id, uint32_t data, uint32_t a3);
static MissionActivateFn g_missionActivate;

static void __fastcall hkMissionActivate(uint8_t* mission, void* edx)
{
    bool first = mission && !(mission[0x3DB] & 0x80);
    g_missionActivate(mission, edx);
    if (!first || !(mission[0x3DB] & 0x80) || !zonePlayset10()) return;
    uint8_t* pm = *(uint8_t**)rt(0x0225F540);
    uint8_t* players = pm ? *(uint8_t**)(pm + 0x108) : nullptr;
    uint32_t ev = *(const uint16_t*)rt(0x0221CDCC);
    char name[96] = "";
    uint8_t* ctx = *(uint8_t**)(mission + 0x140);
    safeCopy(name, sizeof(name), ctx ? *(const char**)(ctx + 0x10) : nullptr);
    for (int p = 0; players && p < 4; ++p) {
        if (*(int*)(players + 0x10 + p * 0x50) != 1) continue;   // the local players FUN_015bcc10 selects for
        uint32_t id = 0;
        ((PlayerIdFn)rt(0x004291B0))(pm, nullptr, &id, p);
        ((PostEventFn)rt(0x0069EFE0))(ev, id, 0);
        logf("accept10: %s activated in a 1.0 zone -> tutorial 'mission accepted' event %u for player %d", name, ev, p);
    }
}

// probe (locktrace10), Cars Tab: Tab = TOY_STORE -> ShowRibbonUI exists only in the Biped layers, and its filter
// needs the context Allow_UIRibbon (avatar_controls_win32.xldb ShowRibbonUI_filter). 3.0 sets Allow_UIRibbon in one
// place, when a player takes control of a vehicle (FUN_00d612a0, thiscall(control, vehicle), ret 4), and only if the
// zone's PlaysetProperties is "ToyBox" (FUN_0139e6e0 -> FUN_01377970), not CartRacing (FUN_0139e790) and the actor
// carries tag 0xff. A 1.0 car avatar drives as a vehicle (Cars: Tow prompt, Vehicle_Control). Logs each takeover in
// a 1.0 zone with the ToyBox test, so a Cars run shows whether this gate is what leaves Tab dead.
typedef void(__fastcall* VehicleCtlFn)(uint8_t* ctl, void* edx, void* vehicle);
typedef char(__fastcall* ZoneFlagFn)(void* zm, void* edx, int zone);
static VehicleCtlFn g_vehicleCtl;
static void __fastcall hkVehicleCtl(uint8_t* ctl, void* edx, void* vehicle)
{
    void* old = ctl ? *(void**)(ctl + 0xF0) : nullptr;
    g_vehicleCtl(ctl, edx, vehicle);
    if (!g_state.cfg.lockTrace10 || !zonePlayset10() || !inputRoom()) return;
    void* zm = *(void**)rt(0x0225F548);
    char toy = zm ? ((ZoneFlagFn)rt(0x0139E6E0))(zm, nullptr, 0) : 0;
    logf("vehicle10: player control %p takes vehicle %p (was %p); zone PlaysetProperties ToyBox %d -> 3.0 %s Allow_UIRibbon "
         "(Tab = ShowRibbonUI needs it)", ctl, vehicle, old, toy, vehicle && toy ? "may set" : "does not set");
}

// ---- tools10: the 1.0 level lists of the tools and packs (plan TL-1) ----
// 3.0 enables a tool only where its ToolSetTable LevelPrefix allows (gate FUN_0150f7d0, thiscall (tool, list) ret 4,
// called by FUN_0151b960 with the Tools record + 4; result cached in tool +0x74): an empty list, a prefix the
// lower-cased zone name starts with, or the zone key AllowAllToolsAndPacks. 3.0 dropped in_ from the lists, so in
// IN_City the three Metroville packs were off. toolgate.py over 1.0 ToolSet.bin (177 tools) and 3.0
// in2/in3_core_toolset.lua (153): 3 tools have a 1.0 list allowing in_city -- PROP_CAPE, PROP_HOVERBOARD,
// ITEM_ZEROPOINTENERGY (rumpus, adv_, core, holo, in_) -- and 3.0 blocks all 3 there (e.g. PROP_CAPE rumpus, adv_, core,
// holo, tbx_, emp_). The story needs them: IN_City_GP_Tutorial (CapeEquipped), the hoverboard missions,
// IN_City_Syndrome_Fight (MissionTaskModifier_ForceActorTool ITEM_ZEROPOINTENERGY). In a 1.0 zone a tool 3.0 refuses
// is judged by its 1.0 record instead, read from the 1.0 install (startup5.zip, mounted at assets\ in 1.0).
typedef uint32_t(__fastcall* ToolGateFn)(uint8_t* tool, void* edx, const uint8_t* prefixes);
typedef uint8_t*(__fastcall* ToolSetFindFn)(void* table, void* edx, const char* section, const char* name);
typedef const char*(__fastcall* ZoneNameFn)(void* mgr, void* edx, uint32_t zone, int a2);
static ToolGateFn g_toolGate;

static const std::map<std::string, std::vector<std::string>>& toolLevels10()
{
    static const auto t = [] {   // read once (thread-safe static init): later calls take no lock on the game thread
        std::map<std::string, std::vector<std::string>> m;
        di1::vfs::withArchive("startup5.zip", [&m](mz_zip_archive* z) {
            size_t n = 0;
            if (void* d = z ? core::extractZipMember(z, "var/toolset/toolset.bin", &n) : nullptr) {
                m = gameplay::toolLevels10((const uint8_t*)d, n);
                mz_free(d);
            }
            logf("tools10: %u 1.0 tool records from startup5.zip:var/toolset/toolset.bin", (unsigned)m.size());
        });
        return m;
    }();
    return t;
}

static uint32_t __fastcall hkToolGate(uint8_t* tool, void* edx, const uint8_t* prefixes)
{
    uint32_t r = g_toolGate(tool, edx, prefixes);
    if (r || !zonePlayset10()) return r;
    uint8_t* mgr = *(uint8_t**)rt(0x0225F548);
    const char* zone = ((ZoneNameFn)rt(0x01391040))(mgr, nullptr, *(uint32_t*)(mgr + 0x19C), 1);
    void* table = *(void**)rt(0x0228AA3C);
    for (auto& kv : toolLevels10()) {
        if (kv.second.empty() || !gameplay::levelAllows(kv.second, zone)) continue;
        const uint8_t* rec = ((ToolSetFindFn)rt(0x015F6A60))(table, nullptr, "Tools", kv.first.c_str());
        if (rec && rec + 4 == prefixes) {
            *(uint32_t*)(tool + 0x74) = 1;
            logf("tools10: %s allowed in %s by its 1.0 level list", kv.first.c_str(), zone ? zone : "?");
            return 1;
        }
    }
    return r;
}

// The gameplay and menu runtime's hooks (H1).
inline bool install(const di1::Config& c)
{
    g_state.cfg = c;
    bool ok = true;
    if (c.lockTrace10)
        ok &= hook("vehicle control takeover (vehicle10 probe)", 0x00D612A0, "6AFF68????????64A100000000",
                   (void*)&hkVehicleCtl, (void**)&g_vehicleCtl);
    if (c.accept10)
        ok &= hook("mission activation (accept10)", 0x015BCC10, "83EC14568BF18A86DB03000084C0",
                   (void*)&hkMissionActivate, (void**)&g_missionActivate);
    if (c.tools10)
        ok &= hook("tool level gate (1.0 lists)", 0x0150F7D0, "538BD9837B74FF", (void*)&hkToolGate,
                   (void**)&g_toolGate);
    if (c.selectMenu10)
        ok &= installFeature("selectmenu10", {
            {"ribbon name (1.0 SelectMenu)", 0x00B42540, "518B0D????????8B819C010000", (void*)&hkRibbonName,
             (void**)&g_ribbonName},
            {"ribbon open (1.0 SelectMenu)", 0x00B68F60, "6AFF68????????64A100000000", (void*)&hkShowRibbon,
             (void**)&g_showRibbon},
            {"screen request (ProgressLog)", 0x0156DDE0, "64A1000000006AFF68", (void*)&hkScreenReq,
             (void**)&g_screenReq},
            {"screen AddScreen by name (1.0 scripts)", 0x0156A360, "558BEC83E4F86AFF68????????64A100000000",
             (void*)&hkLuaAddScreen,
             (void**)&g_luaAddScreen},
        });
    if (c.actionMap10 || c.lockTrace10)
        ok &= hook("movie ExternalInterface call (1.0 UsesActionMap; input10 probe)", 0x01564790,
                   "64A1000000006AFF68????????506489250000000081ECC8000000", (void*)&hkFlashCall, (void**)&g_flashCall);
    if (c.focus10)
        ok &= installFeature("focus10", {
            {"screen focus gain (1.0 movies)", 0x01559480, "568B742408578BF98B4E50C1E105", (void*)&hkFocusGain,
             (void**)&g_focusGain},
            {"screen focus loss (1.0 movies)", 0x01554590, "64A1000000006AFF68????????506489250000000083EC28",
             (void*)&hkFocusLoss,
             (void**)&g_focusLoss},
        });
    if (c.lockTrace10 || c.keys10)
        ok &= installFeature("keys10", {
            {"screen action delivery (keys10; input10 probe)", 0x01565330,
             "64A1000000006AFF68????????506489250000000083EC44",
             (void*)&hkDeliver, (void**)&g_deliver},
            {"action map entry state (keys10)", 0x00B48250, "8B44240456578BF1508D4E04", (void*)&hkMapState,
             (void**)&g_mapState},
        });
    return ok;
}

}   // namespace gprt
