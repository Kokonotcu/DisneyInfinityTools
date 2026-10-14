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

// The path must be an engine CString: the loader (FUN_018db270) add-refs its header (FUN_00caa0d0 on path-0xc: a
// std::string buffer there is a garbage refcount incremented in place, or a garbage-length copy). The type may be a
// plain char*: FUN_018da710 / FUN_018da820 intern it into their own argument slot first (FUN_00a72870 at 0x18da74d /
// 0x18da844; FUN_0143acc0 passes the literal "DataMap"); a CString is just as good. Ctor FUN_00418310, release
// FUN_00418230 (as sys's cbLoad).
struct ECStr {
    char* p = nullptr;
    explicit ECStr(const std::string& s) { ((void*(__fastcall*)(void*, void*, const char*))rt(0x00418310))(&p, nullptr, s.c_str()); }
    ~ECStr() { ((void(__fastcall*)(void*, void*))rt(0x00418230))(&p, nullptr); }
    ECStr(const ECStr&) = delete;
    ECStr& operator=(const ECStr&) = delete;
};

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
        ECStr ct(t), cp(p);
        uint8_t r = fn(router, nullptr, ct.p, cp.p, 0, 1);
        line += " " + t + "=" + p + (had ? "(resident)" : r ? "" : "(0)");
        if (!had) mine.push_back({t, p});
    }
    logf("gamedb30 %s: %u 3.0 tables:%s", what, (unsigned)tps.size(), line.c_str());
    return mine;
}

// ---- avperc10: the 1.0 avatar's perception policy in a 1.0 playset (npc::percAdds) ----
// ModApplyPerceptionMode (vtable 0x1dc3a74). Its parse FUN_0154a470 resolves names through the PerceptionInfo name map
// (FUN_01496720, thiscall on [0x2289700], case-sensitive, -1 unknown) into plain ids [+0x1c]/[+0x20] and zone records
// [+0x34]/[+0x38] (12 B: ifMatch, ifNot, matched, hostonly). Apply FUN_0152ae80 (thiscall a1, a2, actor; ret 0xc; a2 == -7
// does nothing) enables them on the actor's perception component (FUN_0069ecd0(actor, [0x2260044])) with the refcounted
// FUN_013f3ba0, a hostonly record only on the host ([0x228db60] vtable +0x20 networked, +0x24 host); remove FUN_0152afa0
// disables them (FUN_013c9e50). The 1.0 additions ride on the same two calls. A parsed modifier is matched to its 3.0
// avatar.btx list by its ids (a -1 parsed before the name existed matches any id).
typedef uint32_t(__fastcall* PercFn)(uint8_t* self, void* edx, int a1, int a2, void* actor);
typedef int(__fastcall* PercIdFn)(void* self, void* edx, const char* name);
typedef void(__fastcall* PercOnFn)(void* comp, void* edx, int id);
static PercFn g_percApply, g_percRemove;
static std::vector<npc::PercList> g_pl10, g_pl30;                // distinct avatar.btx lists (loaded with the playset)
struct PercMod { const void* plain; const void* zone; int list; };
static std::map<uint8_t*, PercMod> g_percMod;                     // modifier -> its 3.0 list (-1: none)
static std::map<std::string, std::vector<npc::PercAdds>> g_percZone;   // zone -> additions per 3.0 list
static std::map<std::pair<void*, uint8_t*>, std::vector<std::vector<int>>> g_percOn;   // ids enabled per apply

static std::string zoneName()
{
    uint8_t* gm = *(uint8_t**)rt(0x0225F548);
    if (!gm) return "";
    typedef const char*(__stdcall * ZoneNameFn)(uint32_t zone, uint32_t flag);
    const char* n = ((ZoneNameFn)rt(0x01391040))(*(uint32_t*)(gm + 0x19c), 1);   // as FUN_0154a470 reads it
    return n ? npc::lower(n) : "";
}

static int percId(const std::string& name)
{
    void* pi = *(void**)rt(0x02289700);
    return pi && !name.empty() ? ((PercIdFn)rt(0x01496720))(pi, nullptr, name.c_str()) : -1;
}

// the 3.0 list a parsed modifier holds (caller holds g_dbLock)
static int percList(uint8_t* self)
{
    const int* plain = *(const int**)(self + 0x1c);
    const int* zone = *(const int**)(self + 0x34);
    uint32_t np = *(uint32_t*)(self + 0x20), nz = *(uint32_t*)(self + 0x38);
    auto c = g_percMod.find(self);
    if (c != g_percMod.end() && c->second.plain == plain && c->second.zone == zone) return c->second.list;
    int found = -1, hits = 0;
    for (size_t j = 0; j < g_pl30.size(); ++j) {
        uint32_t p = 0, z = 0;
        bool ok = true;
        auto same = [](int parsed, int now) { return parsed == -1 || parsed == now; };
        for (auto& e : g_pl30[j]) {
            if (e.kind == 'p') ok = ok && p < np && same(plain[p++], percId(e.on));
            else ok = ok && z < nz && same(zone[3 * z], percId(e.on)) && same(zone[3 * z + 1], percId(e.off)) && ++z;
            if (!ok) break;
        }
        if (ok && p == np && z == nz) { found = (int)j; ++hits; }
    }
    int r = hits == 1 ? found : -1;
    g_percMod[self] = {plain, zone, r};
    return r;
}

static uint32_t __fastcall hkPercApply(uint8_t* self, void*, int a1, int a2, void* actor)
{
    uint32_t r = g_percApply(self, nullptr, a1, a2, actor);
    if (a2 == -7 || !(r & 0xff) || !actor) return r;
    std::vector<std::pair<int, bool>> ids;
    {
        std::lock_guard<std::mutex> lock(g_dbLock);
        if (!g_dbActive || g_pl30.empty()) return r;
        int j = percList(self);
        if (j < 0) return r;
        std::string z = zoneName();
        auto zi = g_percZone.find(z);
        if (zi == g_percZone.end()) {
            zi = g_percZone.emplace(z, std::vector<npc::PercAdds>()).first;
            for (auto& [from, add] : npc::percAdds(g_pl10, g_pl30, z)) {
                zi->second.push_back(add);
                if (add.empty()) continue;
                std::string line;
                for (auto& [n, h] : add) line += " " + n + (h ? "[hostonly]" : "") + "=" + std::to_string(percId(n));
                logf("avperc10 zone %s: 3.0 avatar list %u (%u entries) from 1.0 list %d gets%s", z.c_str(),
                     (unsigned)(zi->second.size() - 1), (unsigned)g_pl30[zi->second.size() - 1].size(), from, line.c_str());
            }
        }
        for (auto& [n, h] : zi->second[j]) {
            int id = percId(n);
            if (id >= 0) ids.push_back({id, h});
        }
    }
    if (ids.empty()) return r;
    typedef void*(__cdecl * CompFn)(void* actor, uint32_t type);
    void* comp = ((CompFn)rt(0x0069ECD0))(actor, *(uint16_t*)rt(0x02260044));
    if (!comp) return r;
    bool host = true;
    if (void** net = *(void***)rt(0x0228DB60)) {
        typedef uint8_t(__fastcall * BoolFn)(void* self, void* edx);
        void** vt = *(void***)net;
        if (((BoolFn)vt[8])(net, nullptr)) host = ((BoolFn)vt[9])(net, nullptr) != 0;
    }
    // The original remove disables both sides of every zone record, the apply enabled one side (ifMatch if matched, else
    // ifNot; hostonly records only on the host): an added id on a record's other side would lose one ref per such record
    // at remove -- with two modifiers of this list overlapping, the first remove takes the addition from the other. Those
    // refs are added here, so the remove leaves the count as apply found it.
    const uint8_t* zone = *(const uint8_t**)(self + 0x34);
    int nz = *(int*)(self + 0x38);
    std::vector<int> on;
    for (auto& [id, h] : ids) {
        if (h && !host) continue;
        int n = 1;
        for (int z = 0; zone && z < nz; ++z) {
            const uint8_t* rec = zone + 12 * z;
            int ifMatch = *(const int*)rec, ifNot = *(const int*)(rec + 4);
            int enabled = !rec[9] || host ? (rec[8] ? ifMatch : ifNot) : -1;
            n += (ifMatch == id) + (ifNot == id) - (enabled == id);
        }
        while (n-- > 0) ((PercOnFn)rt(0x013F3BA0))(comp, nullptr, id);
        on.push_back(id);
    }
    std::lock_guard<std::mutex> lock(g_dbLock);
    g_percOn[{comp, self}].push_back(on);
    return r;
}

static uint32_t __fastcall hkPercRemove(uint8_t* self, void*, int a1, int a2, void* actor)
{
    uint32_t r = g_percRemove(self, nullptr, a1, a2, actor);
    if (a2 == -7 || !actor) return r;
    typedef void*(__cdecl * CompFn)(void* actor, uint32_t type);
    void* comp = ((CompFn)rt(0x0069ECD0))(actor, *(uint16_t*)rt(0x02260044));
    std::vector<int> off;
    {
        std::lock_guard<std::mutex> lock(g_dbLock);
        auto it = comp ? g_percOn.find({comp, self}) : g_percOn.end();
        if (it == g_percOn.end()) return r;
        off.swap(it->second.back());
        it->second.pop_back();
        if (it->second.empty()) g_percOn.erase(it);
    }
    for (int id : off) ((PercOnFn)rt(0x013C9E50))(comp, nullptr, id);
    return r;
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
    g_psPath.clear();
    logf("gamedb30 %s: 1.0 DataMap (%u records), %u Playset tables, %u actors with 3.0 tables", name, (unsigned)m10.size(),
         (unsigned)g_db->playset.size(), (unsigned)g_db->actor.size());
    g_pl10.clear();
    g_pl30.clear();
    if (g_cfg.avperc10) {   // the avatar trees: 1.0's from the 1.0 install, 3.0's (the one its DNA names) from 3.0
        std::string a10, a30;
        if (!read10s("characters/avatar.btx", a10) || !read30("Characters/Avatar.btx", a30) || !npc::percLists(a10, g_pl10, &why) ||
            !npc::percLists(a30, g_pl30, &why)) {
            g_pl10.clear();
            g_pl30.clear();
            logf("avperc10: avatar.btx unreadable (%s)", why.c_str());
        } else logf("avperc10: %u 1.0 / %u 3.0 avatar perception lists", (unsigned)g_pl10.size(), (unsigned)g_pl30.size());
    }
    // playset30: its Playset record as one more DataMap, served from the session (hkOpen) -- the playset-standard
    // includes not in Toy Box come from the smallest other 3.0 DataMap (identical in all 8 that define them)
    std::string bo;
    npc::DataMap other;
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
    ECStr ct("DataMap"), cp(path);
    uint8_t r = load ? ((RouteFn)(*(void***)router)[1])(router, nullptr, ct.p, cp.p, 0, 0)
                     : ((UnrouteFn)(*(void***)router)[2])(router, nullptr, ct.p, cp.p);
    logf("playset30: DataMap %s %s -> %u", path.c_str(), load ? "loaded" : "released", r);
}

static uint8_t __fastcall hkInclude(void* self, void*, const char* name, int kind, int flag, int prog)
{
    std::vector<std::pair<std::string, std::string>> todo;
    std::string what, psMap;
    if (name && kind >= 0 && kind <= 12) {
        std::lock_guard<std::mutex> lock(g_dbLock);
        if (kind == 0 && g_cfg.serve10) {
            try {
                prepare(self, name);
            } catch (std::exception& e) {   // never unwind into engine code (luaEval's .at() on a malformed chunk)
                logf("gamedb30 %s: supplement failed: %s", name, e.what());
            }
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
        g_percMod.clear();
        g_percZone.clear();
        g_percOn.clear();
        done.swap(g_dbOwned);
        psMap = g_psPath;
    }
    dataMapRoute(false, psMap);
    void* router = *(void**)rt(0x022A6E68);
    if (!router) return;
    UnrouteFn fn = (UnrouteFn)(*(void***)router)[2];
    for (auto& [t, p] : done) {   // a table already gone is skipped (FUN_018da6c0)
        ECStr ct(t), cp(p);
        fn(router, nullptr, ct.p, cp.p);
    }
    logf("gamedb30 Playset %s unloaded: %u 3.0 tables it loaded released", name, (unsigned)done.size());
}

// ---- spawner10: the 1.0 SpawnerQuarterback group (SYS-3) ----
// The 1.0 spawners' AI scripts (inc_omnisky_spawner_ai.btl.sx, inc_omnibotspawner_ai.btl.sx: 32 spawners in 15
// Incredibles mission .ap, story Defeat_Omnidroids01/02 and Escort_Hoarder among them; also the omnidroid fly-in ball)
// run LevelQuarterback_Create("SpawnerQuarterback_GROUP", "SpawnerQuarterback") and then
// AIQuarterback_SetActive / AIQuarterback_AddActor on the handle (sxdis.py: pc 26, 36, 62). 3.0 still names the type --
// its QB type table 0x2088228 maps "SpawnerQuarterback" to 10 (14 entries, _stricmp lookup FUN_010a8d30) -- but its factory
// FUN_010c2f70 has no case 10 (byte table 0x10c3410[10] = the default, returns 0), and SetActive / AddActor (natives
// 0x1376880 / 0x13767f0) call through the handle without a null check: once sx runs these 1.0 scripts, the first
// spawner faults on a NULL vtable. The 1.0 SpawnerQuarterback is an AIQuarterback subclass (1.0 vtable 0x146f3e4); its
// base part is made natively: type 10 is built as type 0, 3.0's AIQuarterback (0xc0 B, ctor FUN_0153e1b0).
typedef void*(__cdecl* QbFactoryFn)(int type, int param);
static QbFactoryFn g_qbFactory;
static void* __cdecl hkQbFactory(int type, int param)
{
    if (type != 10) return g_qbFactory(type, param);
    void* qb = g_qbFactory(0, param);
    logf("spawner10: SpawnerQuarterback (QB type 10, no 3.0 factory case) made as its AIQuarterback base -> %p", qb);
    return qb;
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
    if (g_cfg.spawner10)
        hook("quarterback factory (spawner10)", 0x010C2F70, "64A1000000006AFF68????????50", (void*)&hkQbFactory, (void**)&g_qbFactory);
    if (g_cfg.gamedb30) {
        hook("DataMap record include (gamedb30)", 0x0070D730, "6AFF68????????64A100000000", (void*)&hkInclude, (void**)&g_include);
        hook("DataMap playset unload (gamedb30)", 0x0070DF70, "566A0068????????8BF1", (void*)&hkPlaysetUnload, (void**)&g_playsetUnload);
        if (g_cfg.avperc10) {
            hook("perception mode apply (avperc10)", 0x0152AE80, "51837C240CF9578BF9", (void*)&hkPercApply, (void**)&g_percApply);
            hook("perception mode remove (avperc10)", 0x0152AFA0, "837C2408F9568BF1", (void*)&hkPercRemove, (void**)&g_percRemove);
        }
    }
}

}  // namespace npcrt
