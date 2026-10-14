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
    if (!name || strcmp(name, "SelectMenu")) return g_showRibbon(self, edx, player);
    if (!((CanOpenFn)rt(0x014362E0))(player) || ((BlockedFn)rt(0x00DB4110))()) return -1;
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
static void* __fastcall hkScreenReq(void* ui, void* edx, const char* name, int a2, int player)
{
    if (name && !strcmp(name, "ProgressLog")) name = "MissionLog";
    return g_screenReq(ui, edx, name, a2, player);
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
    static std::map<std::string, std::vector<std::string>> t;
    static bool read = false;
    std::lock_guard<std::mutex> lock(g_zipLock);
    if (read) return t;
    read = true;
    if (mz_zip_archive* z = zipFor("startup5.zip")) {
        int i = mz_zip_reader_locate_file(z, "var/toolset/toolset.bin", nullptr, 0);
        size_t n = 0;
        if (void* d = i < 0 ? nullptr : mz_zip_reader_extract_to_heap(z, (mz_uint)i, &n, 0)) {
            t = gameplay::toolLevels10((const uint8_t*)d, n);
            mz_free(d);
        }
    }
    logf("tools10: %u 1.0 tool records from startup5.zip:var/toolset/toolset.bin", (unsigned)t.size());
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
    }
}

}   // namespace gprt
