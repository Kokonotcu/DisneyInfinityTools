// gamedb30, playset30, rewards10, avperc10: a 1.0 playset's supplement around its includes. Engine load threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <exception>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../core/enginestring.hpp"
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/assets.hpp"
#include "../../engine/datamaps.hpp"
#include "../../engine/gamedb.hpp"
#include "../../formats/datamap.hpp"
#include "avperc.hpp"
#include "config.hpp"
#include "session.hpp"
#include "supplement.hpp"
#include "vfs.hpp"

namespace di1::gamedb30 {

// The DataMap steps (engine/datamaps) and, with avperc10, the perception hooks (H1, H2); false when refused.
inline bool install(const Config& c);

namespace detail {

using supplement::Table;

namespace eng {
// H4 open: the engine functions below are called without a prologue check (X-12)
constexpr uint32_t kCarryStyleInfo = 0x022896E4;   // *: CarryStyleInfo; +0x58 its table (loader callback FUN_014a8eb0)
constexpr size_t kCarryStyles = 0x58;
constexpr uint32_t kGameMode = 0x0225F548;         // *: the game mode; +0x19c the zone, as FUN_0154a470 reads it
constexpr size_t kZone = 0x19c;
constexpr uint32_t kZoneName = 0x01391040;         // stdcall (zone, 1) -> its name
constexpr uint32_t kPercId = 0x01496720;           // thiscall (name) on PerceptionInfo: id, case-sensitive; -1 unknown
constexpr uint32_t kComponentOf = 0x0069ECD0;      // cdecl (actor, type): its component
constexpr uint32_t kPercComponent = 0x02260044;    // u16: the perception component's type
constexpr uint32_t kNetwork = 0x0228DB60;          // *: vtable slot 8 networked, slot 9 host
constexpr int kNetworked = 8;
constexpr int kHost = 9;
constexpr uint32_t kPercEnable = 0x013F3BA0;       // thiscall (id) on the component: refcounted enable
constexpr uint32_t kPercDisable = 0x013C9E50;
// ModApplyPerceptionMode (vtable 0x1dc3a74), parsed by FUN_0154a470: plain ids and 12-byte zone records
constexpr size_t kPlainIds = 0x1c, kPlainCount = 0x20, kZoneRecords = 0x34, kZoneCount = 0x38;
constexpr size_t kZoneRecord = 12;                 // ifMatch, ifNot, matched (byte 8), hostonly (byte 9)
constexpr size_t kMatched = 8, kHostOnly = 9;
constexpr int kNoOp = -7;                          // FUN_0152ae80's a2 that does nothing
}  // namespace eng

constexpr int kPlaysetKind = 0;   // FUN_006fd430's kinds: 0 Playset .. 12 Include
constexpr int kLastKind = 12;
inline constexpr const char* kToyBoxDataMap = "3:gamedb/core/core/toyboxdatamap.lua";   // 3.0's 1.0 host playset

typedef uint32_t(__fastcall* PercFn)(uint8_t* self, void* edx, int a1, int a2, void* actor);
typedef int(__fastcall* PercIdFn)(void* self, void* edx, const char* name);
typedef void(__fastcall* PercOnFn)(void* comp, void* edx, int id);

// The 3.0 list a parsed modifier matched, by its plain and zone record arrays.
struct PercMod {
    const void* plain;
    const void* zone;
    int list;
};

struct State {
    // the cfg keys the steps read: set once in install()
    bool serve10 = false, avperc10 = false, playset30 = false, rewards10 = false, hudMarker10 = false;
    std::mutex mx;                    // guards everything below; held across prepare's engine opens (NR-02)
    supplement::GameDb* db = nullptr;   // owned; for the playset `playset`. Raw until NR-01 is decided (N19)
    std::string playset;
    bool active = false;              // between its Playset include and its unload
    std::set<Table> routed;           // routed in this activation: repeat includes skip them
    std::set<Table> owned;            // loaded by these steps: released at the unload
    std::string psPath;               // playset30: the served Playset-record DataMap
    std::vector<avperc::PercList> lists10, lists30;   // distinct avatar.btx lists, loaded with the playset
    std::map<uint8_t*, PercMod> mods;                                  // modifier -> its 3.0 list (-1: none)
    std::map<std::string, std::vector<avperc::PercAdds>> zones;        // zone -> additions per 3.0 list
    std::map<std::pair<void*, uint8_t*>, std::vector<std::vector<int>>> enabled;   // ids enabled per apply (NR-06)
    PercFn g_percApply = nullptr, g_percRemove = nullptr;              // set once by installFeature
};
inline State g_state;

// The tables tps in the router: those not resident before are returned (the router keeps no count).
inline std::vector<Table> route(const std::vector<Table>& tps, const char* what)
{
    std::vector<Table> mine;
    void* router = engine::datamaps::router();
    if (!router || tps.empty()) return mine;
    std::string line;
    for (auto& [t, p] : tps) {
        bool had = engine::datamaps::loaded(router, t, p);
        core::EngineString ct(t.c_str()), cp(p.c_str());
        uint8_t r = engine::datamaps::load(router, ct, cp, 0, 1);
        line += " " + t + "=" + p + (had ? "(resident)" : r ? "" : "(0)");
        if (!had) mine.push_back({t, p});
    }
    core::logf("gamedb30 %s: %u 3.0 tables:%s", what, (unsigned)tps.size(), line.c_str());
    return mine;
}

enum class Route { kLoad, kRelease };

// The DataMap route FUN_0143acc0 makes for a playset's DataMap, or its release.
inline void routeDataMap(Route how, const std::string& path)
{
    void* router = engine::datamaps::router();
    if (!router || path.empty()) return;
    core::EngineString ct("DataMap"), cp(path.c_str());
    const bool load = how == Route::kLoad;
    uint8_t r = load ? engine::datamaps::load(router, ct, cp, 0, 0) : engine::datamaps::release(router, ct, cp);
    core::logf("playset30: DataMap %s %s -> %u", path.c_str(), load ? "loaded" : "released", r);
}

// avperc10: the avatar trees, 1.0's from the 1.0 install and 3.0's (its DNA names it) from 3.0. why is shared.
inline void avatarLists(State& s, std::string& why)
{
    std::string a10, a30;
    const bool read10 = vfs::read10s("characters/avatar.btx", a10);
    const bool read = read10 && engine::assets::read30("Characters/Avatar.btx", a30);
    if (!read || !avperc::percLists(a10, s.lists10, &why) || !avperc::percLists(a30, s.lists30, &why)) {
        s.lists10.clear();
        s.lists30.clear();
        core::logf("avperc10: avatar.btx unreadable (%s)", why.c_str());
    } else {
        core::logf("avperc10: %u 1.0 / %u 3.0 avatar perception lists", (unsigned)s.lists10.size(),
                   (unsigned)s.lists30.size());
    }
}

// rewards10: the 1.0 core expendables packages 3.0 does not ship, for the Playset record. why is shared.
inline std::vector<std::string> corePackages(const char* name, std::string& why)
{
    std::vector<std::string> pkgs;
    std::string c10;
    datamap::DataMap core10;
    if (vfs::read10s("GameDB/Core/CoreDataMap.lua", c10) && datamap::parse(c10, core10, &why))
        for (auto& f : supplement::coreExpendables10(core10))
            if (!engine::assets::shipped(f.substr(supplement::kExpendablesKind).c_str())) pkgs.push_back(f);
    std::string l;
    for (auto& f : pkgs) l += f.substr(supplement::kExpendablesKind) + " ";
    core::logf("rewards10 %s: 1.0 core expendables packages in the Playset record: %s", name,
               l.empty() ? "none" : l.c_str());
    return pkgs;
}

// playset30: the Playset record as one more DataMap, served from the session; Dory gives the non-Toy Box includes.
inline void playsetRecord(State& s, const char* name, const std::string& n, const datamap::DataMap& m30,
                          std::string& why)
{
    std::string bo;
    datamap::DataMap other;
    if (!engine::assets::read30x("3:gamedb/core/core/dorydatamap.lua", bo) || !datamap::parse(bo, other, &why)) {
        core::logf("playset30 %s: no Playset record (%s)", name, why.empty() ? "Dory DataMap unreadable" : why.c_str());
        return;
    }
    std::vector<std::string> pkgs = s.rewards10 ? corePackages(name, why) : std::vector<std::string>();
    std::string t = supplement::playsetText({n, s.db->playset, pkgs, s.hudMarker10}, m30, other);
    s.psPath = "GameDB/Core/" + std::string(name) + "DataMap30.lua";
    session::keep({{text::lower(s.psPath), t}});
    core::logf("playset30 %s: Playset record (%u B of Lua) at %s, hudmarker10=%d (compass screen %s)", name,
               (unsigned)t.size(), s.psPath.c_str(), s.hudMarker10,
               t.find("\"hudmarker_inc|Include\"") != std::string::npos ? "included" : "absent");
}

// The 1.0 DataMap of playset name, which the engine has no Playset record for: its supplement, once. Under mx.
inline void prepare(State& s, void* self, const char* name)
{
    std::string n = text::lower(name);
    // "Core" is 3.0's boot include: routing for it ran before DAT_0228db60 exists, an AV at boot
    if (n == "core" || (s.db && s.playset == n)) return;
    std::string b10, b30, why;
    if (engine::datamaps::findRecord(self, name, kPlaysetKind)) return;
    if (!vfs::read10s("GameDB/Core/" + std::string(name) + "DataMap.lua", b10)) return;
    datamap::DataMap m10, m30;
    const bool maps = datamap::parse(b10, m10, &why) && engine::assets::read30x(kToyBoxDataMap, b30);
    if (!maps || !datamap::parse(b30, m30, &why)) {
        core::logf("gamedb30 %s: no supplement (%s)", name, why.empty() ? "ToyBox DataMap unreadable" : why.c_str());
        return;
    }
    delete s.db;
    s.db = new supplement::GameDb(supplement::gamedb30(m10, m30));
    s.playset = n;
    s.psPath.clear();
    core::logf("gamedb30 %s: 1.0 DataMap (%u records), %u Playset tables, %u actors with 3.0 tables", name,
               (unsigned)m10.size(), (unsigned)s.db->playset.size(), (unsigned)s.db->actor.size());
    s.lists10.clear();
    s.lists30.clear();
    if (s.avperc10) avatarLists(s, why);
    if (s.playset30) playsetRecord(s, name, n, m30, why);
}

// What an include routes, decided under mx: the playset's tables on its Playset include, its actors' tables after.
struct Plan {
    std::vector<Table> todo;
    std::string what, psMap;
};

inline void planPlayset(State& s, void* self, const char* name, Plan& p)
{
    try {
        prepare(s, self, name);
    } catch (std::exception& e) {   // never unwind into engine code (luaEval's .at() on a malformed chunk)
        core::logf("gamedb30 %s: supplement failed: %s", name, e.what());
    }
    if (!s.db || s.playset != text::lower(name)) return;
    s.active = true;
    p.todo = s.db->playset;
    p.what = "Playset ";
    p.psMap = s.psPath;
}

// The actors this record includes (itself and its Includes) and their 3.0 tables.
inline void planActors(State& s, const char* name, int kind, Plan& p)
{
    std::set<datamap::Key> seen;
    datamap::reach(s.db->m10, {datamap::kKinds[kind].key, text::lower(name)}, seen);
    int actors = 0;
    for (auto& k : seen) {
        auto a = k.first == "actor" ? s.db->actor.find(k.second) : s.db->actor.end();
        if (a == s.db->actor.end()) continue;
        ++actors;
        p.todo.insert(p.todo.end(), a->second.begin(), a->second.end());
    }
    if (actors) p.what += std::to_string(actors) + " actors under ";
}

inline Plan plan(void* self, const char* name, int kind)
{
    State& s = g_state;
    Plan p;
    std::lock_guard<std::mutex> lock(s.mx);
    if (kind == kPlaysetKind && s.serve10) planPlayset(s, self, name, p);
    if (s.db && s.active) planActors(s, name, kind, p);
    std::vector<Table> fresh;   // first-wins against this activation's routed set (NR-12)
    for (auto& tp : p.todo)
        if (auto [at, isNew] = s.routed.insert(tp); isNew) fresh.push_back(tp);
    p.todo.swap(fresh);
    return p;
}

// Before the include, which reads them: the tables, then the Playset DataMap the include looks the record up in.
inline std::string beforeInclude(void* self, const char* name, int kind)
{
    State& s = g_state;
    const bool known = name && kind >= 0 && kind <= kLastKind;
    Plan p = known ? plan(self, name, kind) : Plan();
    if (!p.todo.empty()) {   // route() ignores an empty list (NR-10)
        const int k = kind < 0 || kind > kLastKind ? kLastKind : kind;
        std::vector<Table> mine = route(p.todo, (p.what + datamap::kKinds[k].key + " " + (name ? name : "")).c_str());
        if (!mine.empty()) {
            std::lock_guard<std::mutex> lock(s.mx);
            s.owned.insert(mine.begin(), mine.end());
        }
    }
    if (!p.todo.empty() && kind == kPlaysetKind) {   // the table Game_ComputeCarryStyle interns into
        uint8_t* cs = *(uint8_t**)core::rt(eng::kCarryStyleInfo);
        const bool present = cs && *(void**)(cs + eng::kCarryStyles);
        core::logf("gamedb30: CarryStyleInfo table %s", present ? "present" : "STILL MISSING");
    }
    routeDataMap(Route::kLoad, p.psMap);
    return p.psMap;
}

inline void afterInclude(void* self, const char* name, const std::string& psMap)
{
    if (psMap.empty()) return;
    const bool found = engine::datamaps::findRecord(self, name, kPlaysetKind) != nullptr;
    core::logf("playset30: Playset record %s %s", name, found ? "found by the engine" : "NOT FOUND");
}

// A second 1.0 playset included before the first's unload shares this bookkeeping (NR-13).
inline void afterUnload(const char* name)
{
    State& s = g_state;
    std::set<Table> done;
    std::string psMap;
    {
        std::lock_guard<std::mutex> lock(s.mx);
        if (!name || !s.db || s.playset != text::lower(name)) return;
        s.active = false;
        s.routed.clear();
        s.mods.clear();
        s.zones.clear();
        s.enabled.clear();
        done.swap(s.owned);
        psMap = s.psPath;
    }
    routeDataMap(Route::kRelease, psMap);
    void* router = engine::datamaps::router();
    if (!router) return;
    for (auto& [t, p] : done) {   // a table already gone is skipped (FUN_018da6c0)
        core::EngineString ct(t.c_str()), cp(p.c_str());
        engine::datamaps::release(router, ct, cp);
    }
    core::logf("gamedb30 Playset %s unloaded: %u 3.0 tables it loaded released", name, (unsigned)done.size());
}

// avperc10 (FUN_0154a470 parses a modifier's names into ids; a -1 parsed before the name existed matches any).
inline std::string zoneName()
{
    uint8_t* gm = *(uint8_t**)core::rt(eng::kGameMode);
    if (!gm) return "";
    typedef const char*(__stdcall * ZoneNameFn)(uint32_t zone, uint32_t flag);
    const char* n = ((ZoneNameFn)core::rt(eng::kZoneName))(*(uint32_t*)(gm + eng::kZone), 1);
    return n ? text::lower(n) : "";
}

inline int percId(const std::string& name)
{
    void* pi = *(void**)core::rt(engine::gamedb::eng::kPerceptionInfo);
    return pi && !name.empty() ? ((PercIdFn)core::rt(eng::kPercId))(pi, nullptr, name.c_str()) : -1;
}

inline bool sameId(int parsed, int now) { return parsed == -1 || parsed == now; }

// Whether a modifier's plain ids and zone records are list l's names, in order (NR-08: no side effects in &&).
inline bool listMatches(const avperc::PercList& l, const int* plain, uint32_t np, const int* zone, uint32_t nz)
{
    uint32_t p = 0, z = 0;
    for (auto& e : l) {
        if (e.kind == 'p') {
            if (p >= np || !sameId(plain[p], percId(e.on))) return false;
            ++p;
            continue;
        }
        if (z >= nz || !sameId(zone[3 * z], percId(e.on)) || !sameId(zone[3 * z + 1], percId(e.off))) return false;
        ++z;
    }
    return p == np && z == nz;
}

// The one 3.0 list a parsed modifier holds, -1 if none or several; cached per modifier. Under mx.
inline int percList(State& s, uint8_t* self)
{
    const int* plain = *(const int**)(self + eng::kPlainIds);
    const int* zone = *(const int**)(self + eng::kZoneRecords);
    uint32_t np = *(uint32_t*)(self + eng::kPlainCount), nz = *(uint32_t*)(self + eng::kZoneCount);
    auto c = s.mods.find(self);
    if (c != s.mods.end() && c->second.plain == plain && c->second.zone == zone) return c->second.list;
    int found = -1, hits = 0;
    for (size_t j = 0; j < s.lists30.size(); ++j) {
        if (!listMatches(s.lists30[j], plain, np, zone, nz)) continue;
        found = (int)j;
        ++hits;
    }
    int r = hits == 1 ? found : -1;
    s.mods[self] = {plain, zone, r};
    return r;
}

// The zone's additions per 3.0 list, computed and logged at the first apply in it. Under mx.
inline const std::vector<avperc::PercAdds>& zoneAdditions(State& s, const std::string& z)
{
    auto zi = s.zones.find(z);
    if (zi != s.zones.end()) return zi->second;
    zi = s.zones.emplace(z, std::vector<avperc::PercAdds>()).first;
    for (auto& [from, add] : avperc::percAdds(s.lists10, s.lists30, z)) {
        zi->second.push_back(add);
        if (add.empty()) continue;
        std::string line;
        for (auto& [n, h] : add) line += " " + n + (h ? "[hostonly]" : "") + "=" + std::to_string(percId(n));
        core::logf("avperc10 zone %s: 3.0 avatar list %u (%u entries) from 1.0 list %d gets%s", z.c_str(),
                   (unsigned)(zi->second.size() - 1), (unsigned)s.lists30[zi->second.size() - 1].size(), from,
                   line.c_str());
    }
    return zi->second;
}

// The ids (and hostonly) a 1.0 playset adds to this modifier's list in this zone; empty: nothing to do.
inline std::vector<std::pair<int, bool>> additions(uint8_t* self)
{
    State& s = g_state;
    std::vector<std::pair<int, bool>> ids;
    std::lock_guard<std::mutex> lock(s.mx);
    if (!s.active || s.lists30.empty()) return ids;
    int j = percList(s, self);
    if (j < 0) return ids;
    for (auto& [n, h] : zoneAdditions(s, zoneName())[j]) {
        int id = percId(n);
        if (id >= 0) ids.push_back({id, h});
    }
    return ids;
}

inline void* component(void* actor)
{
    typedef void*(__cdecl * CompFn)(void* actor, uint32_t type);
    return ((CompFn)core::rt(eng::kComponentOf))(actor, *(uint16_t*)core::rt(eng::kPercComponent));
}

inline bool isHost()
{
    void** net = *(void***)core::rt(eng::kNetwork);
    if (!net) return true;
    typedef uint8_t(__fastcall * BoolFn)(void* self, void* edx);
    void** vt = *(void***)net;
    if (!((BoolFn)vt[eng::kNetworked])(net, nullptr)) return true;
    return ((BoolFn)vt[eng::kHost])(net, nullptr) != 0;
}

// Remove disables both sides of each zone record; an id on a record's other side gets the refs it loses.
inline int refsFor(const uint8_t* zone, int nz, int id, bool host)
{
    int n = 1;
    for (int z = 0; zone && z < nz; ++z) {
        const uint8_t* rec = zone + eng::kZoneRecord * z;
        int ifMatch = *(const int*)rec, ifNot = *(const int*)(rec + 4);
        const bool applies = !rec[eng::kHostOnly] || host;
        int enabled = applies ? (rec[eng::kMatched] ? ifMatch : ifNot) : -1;
        n += (ifMatch == id) + (ifNot == id) - (enabled == id);
    }
    return n;
}

// FUN_0152ae80, thiscall (a1, a2, actor; ret 0xc), AL applied: enables a modifier's perceptions. Per apply.
inline uint32_t __fastcall hkPercApply(uint8_t* self, void*, int a1, int a2, void* actor)
{
    uint32_t r = g_state.g_percApply(self, nullptr, a1, a2, actor);
    if (a2 == eng::kNoOp || !(r & 0xff) || !actor) return r;
    std::vector<std::pair<int, bool>> ids = additions(self);
    if (ids.empty()) return r;
    void* comp = component(actor);
    if (!comp) return r;
    bool host = isHost();
    const uint8_t* zone = *(const uint8_t**)(self + eng::kZoneRecords);
    int nz = *(int*)(self + eng::kZoneCount);
    std::vector<int> on;
    for (auto& [id, h] : ids) {
        if (h && !host) continue;
        PercOnFn enable = (PercOnFn)core::rt(eng::kPercEnable);
        for (int n = refsFor(zone, nz, id, host); n > 0; --n) enable(comp, nullptr, id);
        on.push_back(id);
    }
    std::lock_guard<std::mutex> lock(g_state.mx);
    g_state.enabled[{comp, self}].push_back(on);
    return r;
}

// FUN_0152afa0, thiscall (a1, a2, actor; ret 0xc): disables them; the last apply's additions go too. Per remove.
inline uint32_t __fastcall hkPercRemove(uint8_t* self, void*, int a1, int a2, void* actor)
{
    uint32_t r = g_state.g_percRemove(self, nullptr, a1, a2, actor);
    if (a2 == eng::kNoOp || !actor) return r;
    void* comp = component(actor);
    std::vector<int> off;
    {
        std::lock_guard<std::mutex> lock(g_state.mx);
        auto it = comp ? g_state.enabled.find({comp, self}) : g_state.enabled.end();
        if (it == g_state.enabled.end()) return r;
        off.swap(it->second.back());
        it->second.pop_back();
        if (it->second.empty()) g_state.enabled.erase(it);
    }
    for (int id : off) ((PercOnFn)core::rt(eng::kPercDisable))(comp, nullptr, id);
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.serve10 = c.serve10;
    s.avperc10 = c.avperc10;
    s.playset30 = c.playset30;
    s.rewards10 = c.rewards10;
    s.hudMarker10 = c.hudMarker10;
    if (!c.gamedb30) return true;
    const bool db = engine::datamaps::install({detail::beforeInclude, detail::afterInclude, detail::afterUnload});
    if (!db || !c.avperc10) return db;
    return core::installFeature("avperc10", {
        {"perception mode apply (avperc10)", 0x0152AE80, "51837C240CF9578BF9", (void*)&detail::hkPercApply,
         (void**)&s.g_percApply},
        {"perception mode remove (avperc10)", 0x0152AFA0, "837C2408F9568BF1", (void*)&detail::hkPercRemove,
         (void**)&s.g_percRemove},
    });
}

}  // namespace di1::gamedb30
