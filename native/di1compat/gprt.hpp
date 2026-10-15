// gp track, runtime side (hooks). Included by di1compat.cpp after its helpers (logf, hook, rt, g_cfg).
#pragma once

namespace gprt {

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
        { std::lock_guard<std::mutex> lock(g_sessLock); from10 = g_screens10.count(caller) != 0; }
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

// ---- focus10: a 1.0 movie in a 3.0 Screen gets the input focus ----
// The manager tells the top screen's movie it has the focus (FUN_01559480 gain, FUN_01554590 loss; thiscall on the
// manager, ret 4) in one of two ways: a screen whose Lua class has HasScreenFSM (3.0's Screen) gets root.GainFocus()
// / LoseFocus(), any other root.SetHasFocus(true/false) (FUN_01552390, thiscall(screen, bool): invoke when loaded
// (+0x6d), then +0x60 = the value). 1.0 movies know only SetHasFocus (AvaElement: hasFocus, false until set; their
// key listeners return unless HasFocus(), storefront.keyDownListener). 3.0 ships catalog.lua (a Screen) but no
// storefront movie, so the Toy Store's Catalog runs 1.0's storefront.gfx: it never got SetHasFocus and dropped every
// key (run-mu-0557-store: no Storefront_Up/_Left/_Exit or frontend_cancel post while browsing and pressing back, only
// the mouse-driven GetItemActionsStates; Esc = CANCEL -> UI_CATALOG_CANCEL, id 134 in both exes and in the movie's
// KeyMap). A screen whose movie was served from 1.0 (+0x14 = "<dir>/<movie>", in g_movies10) also gets SetHasFocus
// with the same value, 3.0's +0x60 kept as it set it. Not 1.0 scripts' screens alone: any 3.0 screen on a 1.0 movie.
typedef void(__fastcall* FocusGainFn)(uint8_t* mgr, void* edx, uint8_t* screen);
typedef void(__fastcall* FocusLossFn)(uint8_t* mgr, void* edx, uint8_t** holder);
typedef void(__fastcall* SetHasFocusFn)(uint8_t* screen, void* edx, int focus);
static FocusGainFn g_focusGain;
static FocusLossFn g_focusLoss;

static void movieFocus10(uint8_t* screen, bool focus)
{
    const char* m = screen && screen[0x6d] ? *(const char**)(screen + 0x14) : nullptr;
    if (!m) return;
    std::string k = "flash/" + std::string(m);
    for (char& c : k) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    { std::lock_guard<std::mutex> lock(g_sessLock); if (!g_movies10.count(k)) return; }
    uint8_t keep = screen[0x60];
    ((SetHasFocusFn)rt(0x01552390))(screen, nullptr, focus ? 1 : 0);
    screen[0x60] = keep;
    char name[64] = "";
    safeCopy(name, sizeof(name), *(const char**)(screen + 0x10));
    logf("focus10: %s (1.0 movie %s) SetHasFocus(%d)", name, m, focus ? 1 : 0);
}

static void __fastcall hkFocusGain(uint8_t* mgr, void* edx, uint8_t* screen)
{
    g_focusGain(mgr, edx, screen);
    uint8_t* head = screen ? *(uint8_t**)(mgr + 0x28C + *(int*)(screen + 0x50) * 0x20) : nullptr;   // the top: what 3.0 focused
    uint8_t** holder = head ? *(uint8_t***)(head + 8) : nullptr;
    movieFocus10(holder ? holder[0] : nullptr, true);
}

static void __fastcall hkFocusLoss(uint8_t* mgr, void* edx, uint8_t** holder)
{
    g_focusLoss(mgr, edx, holder);
    movieFocus10(holder ? holder[0] : nullptr, false);
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
        std::lock_guard<std::mutex> lock(g_zipLock);
        if (mz_zip_archive* z = zipFor("startup5.zip")) {
            int i = mz_zip_reader_locate_file(z, "var/toolset/toolset.bin", nullptr, 0);
            size_t n = 0;
            if (void* d = i < 0 ? nullptr : mz_zip_reader_extract_to_heap(z, (mz_uint)i, &n, 0)) {
                m = gameplay::toolLevels10((const uint8_t*)d, n);
                mz_free(d);
            }
        }
        logf("tools10: %u 1.0 tool records from startup5.zip:var/toolset/toolset.bin", (unsigned)m.size());
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

static void install()
{
    if (g_cfg.tools10) hook("tool level gate (1.0 lists)", 0x0150F7D0, "538BD9837B74FF", (void*)&hkToolGate, (void**)&g_toolGate);
    if (g_cfg.selectMenu10) {
        hook("ribbon name (1.0 SelectMenu)", 0x00B42540, "518B0D????????8B819C010000", (void*)&hkRibbonName, (void**)&g_ribbonName);
        hook("ribbon open (1.0 SelectMenu)", 0x00B68F60, "6AFF68????????64A100000000", (void*)&hkShowRibbon, (void**)&g_showRibbon);
        hook("screen request (ProgressLog)", 0x0156DDE0, "64A1000000006AFF68", (void*)&hkScreenReq, (void**)&g_screenReq);
        hook("screen AddScreen by name (1.0 scripts)", 0x0156A360, "558BEC83E4F86AFF68????????64A100000000", (void*)&hkLuaAddScreen,
             (void**)&g_luaAddScreen);
    }
    if (g_cfg.focus10) {
        hook("screen focus gain (1.0 movies)", 0x01559480, "568B742408578BF98B4E50C1E105", (void*)&hkFocusGain, (void**)&g_focusGain);
        hook("screen focus loss (1.0 movies)", 0x01554590, "64A1000000006AFF68????????506489250000000083EC28", (void*)&hkFocusLoss,
             (void**)&g_focusLoss);
    }
}

}   // namespace gprt
