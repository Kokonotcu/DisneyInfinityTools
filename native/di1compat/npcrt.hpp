// NPC track, runtime side (hooks). Included by di1compat.cpp after its helpers (logf, hook, g_cfg, read10s, read30x).
#pragma once

namespace npcrt {

// ---- gamedb30: the 3.0 GameDB tables of a playset served a 1.0 DataMap (npc::gamedb30) ----
// Pressing E on an NPC faulted in the spinlock FUN_00412ef0 (write to 0x30). Minidump crash-20261013-044617-2 stack:
// script VM FUN_00737010 -> native Game_ComputeCarryStyle FUN_00a7eef0 -> string intern FUN_00418c30 with
// ecx = [[0x22896e4]+0x58] = NULL. That CarryStyleInfo table is made only by the "carrystyles" GameDB loader callback
// FUN_014a8eb0 (registered in FUN_014ab560); 8 of 9 3.0 DataMaps list gamedb/core/carrystyles*.lua in their Playset
// record, the 1.0 DataMap has no Playset group, so it never loads. Same gap for the Actor-level tables (emuInfo,
// progression, toolset) the served 3.0 DNA name.
// Engine (dump30): DataMap manager [0x22035c0]; FUN_0070d730 includes a record (thiscall name, kind, flag, progress;
// ret 0x10; kind enum FUN_006fd430: 0 Playset, 2 Zone, 3 Actor, 12 Include ...; Includes expand in FUN_007058c0);
// FUN_00705860 finds one (thiscall name, kind; ret 8); FUN_0070df70 unloads a playset (thiscall name; ret 4); a GameDB
// entry loads as router [0x22a6e68] vtable[1](type, path, 0, 1) (FUN_0070c090 case 0/1; DBFileRouter FUN_018da710,
// ret 0x10) and unloads as vtable[2](type, path) (FUN_018da820, ret 8); the loader skips a path already loaded
// (FUN_018db270: FUN_018d9200).
typedef uint8_t(__fastcall* IncludeFn)(void* self, void* edx, const char* name, int kind, int flag, int prog);
typedef void*(__fastcall* LookupFn)(void* self, void* edx, const char* name, int kind);
typedef void(__fastcall* PlaysetUnloadFn)(void* self, void* edx, const char* name);
typedef uint8_t(__fastcall* RouteFn)(void* self, void* edx, const char* type, const char* path, int a3, int a4);
typedef uint8_t(__fastcall* UnrouteFn)(void* self, void* edx, const char* type, const char* path);
static IncludeFn g_include;
static PlaysetUnloadFn g_playsetUnload;

static std::mutex g_dbLock;
static npc::GameDb* g_db;                                   // built for the 1.0 playset g_dbPlayset
static std::string g_dbPlayset;
static bool g_dbActive;                                     // between its Playset include and unload
static std::set<std::pair<std::string, std::string>> g_dbRouted;   // routed in this activation (repeat includes skip)
static std::set<std::pair<std::string, std::string>> g_dbOwned;    // loaded by this hook: released at unload
static std::string g_psPath;                                // playset30: the served Playset-record DataMap
static const char* const KINDS[] = {"playset", "gamemode", "zone", "actor", "toy", "activity", "zonebackgroundload",
                                    "realm", "munition", "surfaceimpact", "costume", "powerdisc", "include"};

// Already loaded by a GameDB loader named 'type'? The router keeps no count (a second load is skipped, one unload
// removes the file), so a table the core loaded at boot (CoreDataMap _core_ / avatarprogression) must not be
// released here. DBFileRouter: loaders [router+8] x [router+0xc] (FUN_018da710); a loader's name is the string at +4,
// FUN_018d9730 (thiscall path) its loaded-file id or -1 (exact compare, as the loader stored the path).
static bool loaded(void* router, const std::string& type, const std::string& path)
{
    typedef int(__fastcall* FindFn)(void* self, void* edx, const char* path);
    FindFn find = (FindFn)rt(0x018D9730);
    void** ls = *(void***)((uint8_t*)router + 8);
    int n = *(int*)((uint8_t*)router + 0xc);
    for (int i = 0; ls && i < n; ++i) {
        const char* nm = ls[i] ? ((const char**)ls[i])[1] : nullptr;
        if (nm && type == nm && find(ls[i], nullptr, path.c_str()) >= 0) return true;
    }
    return false;
}

// -> the tables this call loaded (not resident before)
static std::vector<std::pair<std::string, std::string>> route(const std::vector<std::pair<std::string, std::string>>& tps, const char* what)
{
    std::vector<std::pair<std::string, std::string>> mine;
    void* router = *(void**)rt(0x022A6E68);
    if (!router || tps.empty()) return mine;
    RouteFn fn = (RouteFn)(*(void***)router)[1];
    std::string line;
    for (auto& [t, p] : tps) {
        bool had = loaded(router, t, p);
        uint8_t r = fn(router, nullptr, t.c_str(), p.c_str(), 0, 1);
        line += " " + t + "=" + p + (had ? "(resident)" : r ? "" : "(0)");
        if (!had) mine.push_back({t, p});
    }
    logf("gamedb30 %s: %u 3.0 tables:%s", what, (unsigned)tps.size(), line.c_str());
    return mine;
}

// The 1.0 DataMap of playset 'name' with no Playset record in the engine: build its supplement (once per playset).
static void prepare(void* self, const char* name)
{
    std::string n = npc::lower(name);
    if (g_db && g_dbPlayset == n) return;
    LookupFn lookup = (LookupFn)rt(0x00705860);
    std::string b10, b30, why;
    if (lookup(self, nullptr, name, 0) || !read10s("GameDB/Core/" + std::string(name) + "DataMap.lua", b10)) return;
    npc::DataMap m10, m30;
    if (!npc::dataMap(b10, m10, &why) || !read30x("3:gamedb/core/core/toyboxdatamap.lua", b30) || !npc::dataMap(b30, m30, &why)) {
        logf("gamedb30 %s: no supplement (%s)", name, why.empty() ? "ToyBox DataMap unreadable" : why.c_str());
        return;
    }
    delete g_db;
    g_db = new npc::GameDb(npc::gamedb30(m10, m30));
    g_dbPlayset = n;
    logf("gamedb30 %s: 1.0 DataMap (%u records), %u Playset tables, %u actors with 3.0 tables", name, (unsigned)m10.size(),
         (unsigned)g_db->playset.size(), (unsigned)g_db->actor.size());
    // playset30: its Playset record as one more DataMap, served from the session (hkOpen) -- the playset-standard
    // includes not in Toy Box come from the smallest other 3.0 DataMap (identical in all 8 that define them)
    std::string bo;
    npc::DataMap other;
    g_psPath.clear();
    if (g_cfg.playset30 && read30x("3:gamedb/core/core/dorydatamap.lua", bo) && npc::dataMap(bo, other, &why)) {
        std::string t = npc::playsetText(n, g_db->playset, m30, other);
        g_psPath = "GameDB/Core/" + std::string(name) + "DataMap30.lua";
        std::lock_guard<std::mutex> l2(g_sessLock);
        g_served[npc::lower(g_psPath)] = t;
        logf("playset30 %s: Playset record (%u B of Lua) at %s", name, (unsigned)t.size(), g_psPath.c_str());
    } else if (g_cfg.playset30) logf("playset30 %s: no Playset record (%s)", name, why.empty() ? "Dory DataMap unreadable" : why.c_str());
}

// the DataMap router call FUN_0143acc0 makes for a playset's DataMap (vtable[1] "DataMap", path, 0, 0; vtable[2] unloads)
static void dataMapRoute(bool load, const std::string& path)
{
    void* router = *(void**)rt(0x022A6E68);
    if (!router || path.empty()) return;
    uint8_t r = load ? ((RouteFn)(*(void***)router)[1])(router, nullptr, "DataMap", path.c_str(), 0, 0)
                     : ((UnrouteFn)(*(void***)router)[2])(router, nullptr, "DataMap", path.c_str());
    logf("playset30: DataMap %s %s -> %u", path.c_str(), load ? "loaded" : "released", r);
}

static uint8_t __fastcall hkInclude(void* self, void*, const char* name, int kind, int flag, int prog)
{
    std::vector<std::pair<std::string, std::string>> todo;
    std::string what, psMap;
    if (name && kind >= 0 && kind <= 12) {
        std::lock_guard<std::mutex> lock(g_dbLock);
        if (kind == 0 && g_cfg.serve10) {
            prepare(self, name);
            if (g_db && g_dbPlayset == npc::lower(name)) {
                g_dbActive = true;
                todo = g_db->playset;
                what = "Playset ";
                psMap = g_psPath;
            }
        }
        if (g_db && g_dbActive) {                           // actors this record includes (itself, Includes)
            std::set<std::pair<std::string, std::string>> seen;
            npc::reach(g_db->m10, {KINDS[kind], npc::lower(name)}, seen);
            int actors = 0;
            for (auto& k : seen) {
                auto a = k.first == "actor" ? g_db->actor.find(k.second) : g_db->actor.end();
                if (a == g_db->actor.end()) continue;
                ++actors;
                todo.insert(todo.end(), a->second.begin(), a->second.end());
            }
            if (actors) what += std::to_string(actors) + " actors under ";
        }
        auto e = std::remove_if(todo.begin(), todo.end(), [](const std::pair<std::string, std::string>& tp) { return !g_dbRouted.insert(tp).second; });
        todo.erase(e, todo.end());
    }
    // before the include: its DNA reads them
    auto mine = route(todo, (what + KINDS[kind < 0 || kind > 12 ? 12 : kind] + " " + (name ? name : "")).c_str());
    if (!mine.empty()) {
        std::lock_guard<std::mutex> lock(g_dbLock);
        g_dbOwned.insert(mine.begin(), mine.end());
    }
    if (!todo.empty() && kind == 0) {                       // the table Game_ComputeCarryStyle interns into
        uint8_t* cs = *(uint8_t**)rt(0x022896E4);
        logf("gamedb30: CarryStyleInfo table %s", cs && *(void**)(cs + 0x58) ? "present" : "STILL MISSING");
    }
    dataMapRoute(true, psMap);                              // before the include looks the Playset record up
    uint8_t r = g_include(self, nullptr, name, kind, flag, prog);
    if (!psMap.empty()) {
        LookupFn lookup = (LookupFn)rt(0x00705860);
        logf("playset30: Playset record %s %s", name, lookup(self, nullptr, name, 0) ? "found by the engine" : "NOT FOUND");
    }
    return r;
}

static void __fastcall hkPlaysetUnload(void* self, void*, const char* name)
{
    g_playsetUnload(self, nullptr, name);
    std::set<std::pair<std::string, std::string>> done;
    std::string psMap;
    {
        std::lock_guard<std::mutex> lock(g_dbLock);
        if (!name || !g_db || g_dbPlayset != npc::lower(name)) return;
        g_dbActive = false;
        g_dbRouted.clear();
        done.swap(g_dbOwned);
        psMap = g_psPath;
    }
    dataMapRoute(false, psMap);
    void* router = *(void**)rt(0x022A6E68);
    if (!router) return;
    UnrouteFn fn = (UnrouteFn)(*(void***)router)[2];
    for (auto& [t, p] : done) fn(router, nullptr, t.c_str(), p.c_str());   // a table already gone is skipped (FUN_018da6c0)
    logf("gamedb30 Playset %s unloaded: %u 3.0 tables it loaded released", name, (unsigned)done.size());
}

// ---- skin10: a 1.0 asset's InfluenceRemap palettes applied to its vertex buffer before the render transcode ----
// (npc::skinRemap; octtest --skin + skincheck2.py: 1,425 primitives in 419 models equal the independent expectation;
// against the 243 models 3.0 also ships, 830,242 weighted bone slots equal 3.0's, raw 1.0 indices 139,739)
static std::string skin10(oct::File& f, std::string& vbuf, const std::string& ibuf)
{
    static std::once_flag once;
    static npc::SkinLayouts L;
    std::call_once(once, [] {
        std::string t;
        if (read10s("shaders/vformat.vtxf", t)) L = npc::skinLayouts(t);
        logf("skin10: %u skinned 1.0 vertex formats", (unsigned)L.size());
    });
    if (vbuf.empty() || ibuf.empty() || L.empty()) return "";
    std::string why;
    int n = npc::skinRemap(f, vbuf, ibuf, L, &why);
    return n || !why.empty() ? "+skinremap(" + std::to_string(n) + (why.empty() ? "" : ": " + why) + ") " : "";
}

inline void install()
{
    if (g_cfg.gamedb30) {
        hook("DataMap record include (gamedb30)", 0x0070D730, "6AFF68????????64A100000000", (void*)&hkInclude, (void**)&g_include);
        hook("DataMap playset unload (gamedb30)", 0x0070DF70, "566A0068????????8BF1", (void*)&hkPlaysetUnload, (void**)&g_playsetUnload);
    }
}

}  // namespace npcrt
