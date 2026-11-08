// hkOpen's stages that answer a 3.0 file with an edited copy (or a 1.0 CHD compile). games/di1, engine. Open threads.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../core/engine_file.hpp"
#include "../../core/log.hpp"
#include "../../convert/assets.hpp"
#include "../../engine/gamedb.hpp"
#include "../../engine/zones.hpp"
#include "../../formats/chd.hpp"
#include "../../formats/oct.hpp"
#include "challenges.hpp"
#include "ui.hpp"
#include "config.hpp"
#include "mission.hpp"
#include "openstage.hpp"
#include "playsets.hpp"
#include "render10.hpp"
#include "vfs.hpp"

namespace di1::edits30 {

// shaders10: 3.0's shaders/vformat.vtxf plus 1.0's formats. Destroys r; answers the merge or a fresh open.
inline bool vtxfStage(openstage::Open& o, int*& out);
// tips10: the file named Tips.lua only (FUN_00b70da0's literal), not every *tips.lua. Consumes r.
inline bool tipsStage(openstage::Open& o, int*& out);
// playsetmenu10: the Play Sets list and PlaysetSelectMenu get the 1.0 playsets. Consumes r; answers or reopens.
inline bool playsetMenuStage(openstage::Open& o, int*& out);
// currency10: the player HUD script rests on and counts a 1.0 zone's currency. Consumes r; answers text or reopens.
inline bool hudCurrencyStage(openstage::Open& o, int*& out);
// zonelist.lua (zones10, challenge10, beacon10), ChallengeData.lua (challenge10). Destroys r; answers text or reopens.
inline bool zoneListStage(openstage::Open& o, int*& out);
// A .lua the GameDB CHD callback loader is reading: its CHD compile. Consumes r (null or not).
inline bool chdStage(openstage::Open& o, int*& out);
// rctype10: a 3.0 RC catalog of a 1.0 playset gets 1.0's item types. Consumes r; answers text or reopens.
inline bool catalogTypeStage(openstage::Open& o, int*& out);
// rootgraph10: a 3.0 model in 1.0's older exploder form: 1.0's own is served, else Root gets its graph. Destroys r.
inline bool rootGraphStage(openstage::Open& o, int*& out);
// rootgraph10: the .mtb and buffers of a model rootGraphStage passed to 1.0 go to 1.0 too. Destroys r.
inline bool olderCompanionStage(openstage::Open& o, int*& out);
// statreward10: a 1.0 .lua_rewards 3.0 lacks; Stat rewards get their overrides' Value. Answers text or passes on.
inline bool statRewardStage(openstage::Open& o, int*& out);

namespace detail {

constexpr size_t kExtLen = sizeof(".oct") - 1;   // .oct and .mtb alike

struct State {
    std::mutex mx;                 // guards older
    std::set<std::string> older;   // rootgraph10: stems of models served from 1.0 over 3.0's copy; never cleared
};
inline State g_state;

// A model file's stem: "objects/x" for objects/x.oct, x.mtb, x_0.vbuf, x_0.ibuf; "" for anything else.
inline std::string modelStem(const std::string& np)
{
    const bool whole = text::endsWith(np.c_str(), ".oct") || text::endsWith(np.c_str(), ".mtb");
    if (whole) return np.substr(0, np.size() - kExtLen);
    const bool buffer = text::endsWith(np.c_str(), ".vbuf") || text::endsWith(np.c_str(), ".ibuf");
    const size_t u = np.rfind('_');
    return buffer && u != std::string::npos ? np.substr(0, u) : "";
}

// r dropped: the open goes on as a 3.0 miss, so serveStage answers it from 1.0 or the session.
inline void passTo10(openstage::Open& o, const char* why)
{
    openstage::destroyStream(o.r);
    o.r = nullptr;
    o.missWhy = why;
}

// 3.0's tips (o.r, consumed) plus the 1.0 records it lacks; registered zones' keys stay 3.0's (FUN_01391150).
inline int* tips30(const openstage::Open& o)
{
    std::string t30, t10;
    if (!core::readStream(o.r, t30)) return openstage::reopen(o);
    if (t30.find("ZoneTipCategories") == std::string::npos) return core::memFile(t30.data(), t30.size());
    const bool got10 = vfs::read10s("data/tips.lua", t10);
    if (!got10) return core::memFile(t30.data(), t30.size());
    void* zm = engine::zones::manager();
    int k = ui::mergeTips(t30, t10,
                          [&](const std::string& z) { return zm && engine::zones::zoneRegistered(zm, z.c_str()); });
    core::logf("tips %s: %d 1.0 records added (zone manager %s)", o.path, k,
               zm ? "up" : "NOT up: zone keys unchecked");
    return core::memFile(t30.data(), t30.size());
}

// Open half: a .lua stream (3.0's, or 1.0's served) as CHD bytes; nullptr leaves the open as it was.
inline int* chdForLua(const char* p, int* r)
{
    std::string src;
    if (r) {
        if (!core::readStream(r, src)) return core::memFile(src.data(), src.size());
    } else if (!vfs::read10s(text::norm(p), src)) {
        return nullptr;
    }
    try {
        std::string c = chd::compile(chd::lua(src));
        core::logf("compiled %s %u B -> CHD %u B (%s)", p, (unsigned)src.size(), (unsigned)c.size(),
                   r ? "3.0 file" : "1.0 file");
        return core::memFile(c.data(), c.size());
    } catch (std::exception& e) {
        core::logf("CHD compile of %s failed: %s; served as it is", p, e.what());
        return core::memFile(src.data(), src.size());
    }
}

// The quoted value after `key = ` from i, inside the record ending at end; at: where it starts.
inline std::string catalogField(const std::string& t, const char* key, size_t i, size_t end, size_t* at = nullptr)
{
    const size_t k = t.find(std::string(key) + " = ", i);
    if (k == std::string::npos || k >= end) return "";
    const size_t q = t.find_first_of("'\"", k);
    const size_t e = q == std::string::npos ? q : t.find(t[q], q + 1);
    if (e == std::string::npos || e >= end) return "";
    if (at) *at = q + 1;
    return t.substr(q + 1, e - q - 1);
}

// Each record's (name, type) in an RC catalog, in file order; both games write type after name.
inline std::vector<std::pair<std::string, std::string>> catalogTypes(const std::string& t)
{
    std::vector<std::pair<std::string, std::string>> r;
    for (size_t i = t.find("name = "); i != std::string::npos;) {
        const size_t next = t.find("name = ", i + 1);
        const size_t end = next == std::string::npos ? t.size() : next;
        r.push_back({catalogField(t, "name", i, end), catalogField(t, "type", i, end)});
        i = next;
    }
    return r;
}

// t30 with every record's type set to 1.0's for the same item name, where t10 has it and they differ.
inline std::string catalogTypes10(std::string t30, const std::string& t10, int& changed)
{
    std::map<std::string, std::string> types10;
    for (auto& [name, type] : catalogTypes(t10))
        if (!name.empty() && !type.empty()) types10.emplace(name, type);
    changed = 0;
    for (size_t i = t30.find("name = "); i != std::string::npos; i = t30.find("name = ", i + 1)) {
        const size_t next = t30.find("name = ", i + 1);
        const size_t end = next == std::string::npos ? t30.size() : next;
        size_t at = 0;
        const std::string type = catalogField(t30, "type", i, end, &at);
        auto it = types10.find(catalogField(t30, "name", i, end));
        if (type.empty() || it == types10.end() || it->second == type) continue;
        t30.replace(at, type.size(), it->second);
        ++changed;
    }
    return t30;
}

// The model t with Root given its lone graph (octx::rootTakesGraph); "" when not that form or unreadable.
inline std::string rootGraph(const std::string& t)
{
    try {
        oct::File f = oct::File::parse(t.data(), t.size());
        return octx::rootTakesGraph(f) ? f.write() : std::string();
    } catch (std::exception&) {
        return {};
    }
}

}  // namespace detail

inline bool vtxfStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->shaders10 || _stricmp(o.p, "shaders/vformat.vtxf") != 0) return false;
    std::string t30, t10;
    const bool full = openstage::readKeep(o.r, t30);
    if (full && vfs::read10s("shaders/vformat.vtxf", t10)) {
        std::string m = render10::mergeVtxf(t30, t10);
        openstage::destroyStream(o.r);
        out = core::memFile(m.data(), m.size());
        return true;
    }
    openstage::destroyStream(o.r);
    out = openstage::reopen(o);
    return true;
}

inline bool tipsStage(openstage::Open& o, int*& out)
{
    const bool on = o.cfg->serve10 && o.cfg->tips10;
    if (!o.edits() || !on) return false;
    const bool tips =
        !_stricmp(o.p, "tips.lua") || text::endsWith(o.p, "/tips.lua") || text::endsWith(o.p, "\\tips.lua");
    if (!tips) return false;
    out = detail::tips30(o);
    return true;
}

inline bool playsetMenuStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->playsetMenu10) return false;
    const bool list = text::endsWith(o.p, "playsetlistwin32.lua");
    const bool select = text::endsWith(o.p, "playsetselectmenu.lua");
    if (!list && !select) return false;
    const std::string np = text::norm(o.p);
    if (np != "presentation/playsetlistwin32.lua" && np != "presentation/playsetselectmenu.lua") return false;
    std::vector<std::pair<std::string, std::string>> ps = playsets::starts();
    std::string t;
    const bool read = core::readStream(o.r, t);
    const bool chunk = !t.empty() && t[0] == openstage::kLuaChunk;
    if (read && chunk && !ps.empty()) {
        std::string s = select ? ui::playsetSelect10(t, ps) : ui::playsetMenu10(t, ps), names;
        for (auto& x : ps) names += " " + x.first + "->" + x.second;
        core::logf("%s: %u 1.0 playset entries added:%s (%u B 3.0 chunk -> %u B Lua)", o.p, (unsigned)ps.size(),
                   names.c_str(), (unsigned)t.size(), (unsigned)s.size());
        out = core::memFile(s.data(), s.size());
        return true;
    }
    core::logf("%s: %s (%u B): unchanged", o.p, ps.empty() ? "no 1.0 playset registered" : "not the 3.0 chunk",
               (unsigned)t.size());
    out = openstage::reopen(o);
    return true;
}

inline bool hudCurrencyStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->currency10) return false;
    const bool hud = text::endsWith(o.p, "player_hud_combined.lua");
    if (!hud || text::norm(o.p) != "presentation/player_hud_combined.lua") return false;
    std::string t;
    const bool read = core::readStream(o.r, t);
    const bool chunk = !t.empty() && t[0] == openstage::kLuaChunk;
    if (read && chunk) {
        std::string s = ui::hudCurrency10(t);
        core::logf("currency10: %s wrapped: a 1.0 currency rests on the HUD (%u B 3.0 chunk -> %u B Lua)", o.p,
                   (unsigned)t.size(), (unsigned)s.size());
        out = core::memFile(s.data(), s.size());
        return true;
    }
    core::logf("currency10: %s: not the 3.0 chunk (%u B): unchanged", o.p, (unsigned)t.size());
    out = openstage::reopen(o);
    return true;
}

inline bool zoneListStage(openstage::Open& o, int*& out)
{
    const Config& c = *o.cfg;
    const bool zl = text::endsWith(o.p, "zonelist.lua");
    const bool cd = text::endsWith(o.p, "challengedata/challengedata.lua") ||
                    text::endsWith(o.p, "challengedata\\challengedata.lua");
    const bool playsetKeys = c.zones10 || c.playsetMenu10;   // zones10() runs for these
    const bool zl10 = zl && playsetKeys;
    const bool challengeFile = zl || cd;
    const bool challenge = c.challenge10 && challengeFile;
    const bool beacon = c.beacon10 && zl;
    const bool wanted = challenge || beacon || zl10;
    if (!o.edits() || !wanted) return false;
    std::string t, t10;
    const bool full = openstage::readKeep(o.r, t);
    openstage::destroyStream(o.r);
    int k = 0, kb = 0, ka = 0, kz = 0;
    if (full && !t.empty() && t[0] != openstage::kLuaChunk) {
        if (zl10) kz = playsets::registerPlaysets(t, o.p, vfs::read10s);   // first: later steps see the added records
        if (zl && c.challenge10) {
            k = challenges::zonelist30(t);
        } else if (cd) {
            const bool got10 = vfs::read10s("activitydata/defaultactivitybeacons.lua", t10);
            if (got10) k = challenges::beacons30(t, t10);
        }
        if (beacon) kb = mission::beaconMap30(t, &ka);
    }
    if (c.challenge10)
        core::logf("%s: %d %s", o.p, k, zl ? "playset records got ChallengeTypeDataFile" : "1.0 beacons added");
    if (beacon)
        core::logf("%s: %d BeaconMap rows in 3.0 form, %d Beacon_Arrow_Blue rows added (beacon10)", o.p, kb, ka);
    out = k || kb || kz ? core::memFile(t.data(), t.size()) : openstage::reopen(o);
    return true;
}

inline bool chdStage(openstage::Open& o, int*& out)
{
    if (o.write || !text::endsWith(o.p, ".lua") || !engine::gamedb::isChdPath(o.p)) return false;
    out = detail::chdForLua(o.p, o.r);
    return true;
}

inline bool catalogTypeStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->rcType10 || !text::endsWith(o.p, "rc_catalog__lua.lua")) return false;
    const std::string np = text::norm(o.p);
    std::string t30, t10;
    if (!vfs::read10s(np, t10)) return false;   // not a 1.0 playset's catalog: 3.0's stays
    const bool read = core::readStream(o.r, t30);
    const bool chunk = !t30.empty() && t30[0] == openstage::kLuaChunk;
    if (!read || chunk) {
        core::logf("rctype10: %s: not readable text (%u B): unchanged", o.p, (unsigned)t30.size());
        out = openstage::reopen(o);
        return true;
    }
    int changed = 0;
    std::string s = detail::catalogTypes10(t30, t10, changed);
    // 3.0 retyped Cars' CAT_ACTIONTOY items CAT_INSTANTFUN, which RC_CatalogType lacks: no type record, a fault
    if (changed) core::logf("rctype10: %s: %d item types back to 1.0's", o.p, changed);
    out = core::memFile(s.data(), s.size());
    return true;
}

inline bool rootGraphStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->rootGraph10 || !text::endsWith(o.p, ".oct")) return false;
    const std::string np = text::norm(o.p);
    // ponytail: only object and exploder models are read (one parse per open); widen if another folder ships the form
    const bool model = np.rfind("objects/", 0) == 0 || np.rfind("exploders/", 0) == 0;
    if (!model) return false;
    std::string t, edited, t10;
    const bool read = openstage::readKeep(o.r, t);
    if (read) edited = detail::rootGraph(t);
    if (edited.empty()) {
        openstage::destroyStream(o.r);
        out = read ? core::memFile(t.data(), t.size()) : openstage::reopen(o);
        return true;
    }
    // the 3.0 copy is another export (BasisConversion, no transform node): 1.0's own goes through the .oct steps
    if (vfs::read10s(np, t10)) {
        {
            std::lock_guard<std::mutex> lock(detail::g_state.mx);
            detail::g_state.older.insert(detail::modelStem(np));
        }
        core::logf("rootgraph10: %s: 3.0's copy is 1.0's older form; 1.0's own model is served", o.p);
        detail::passTo10(o, " (3.0's copy is the older form: served from 1.0)");
        return false;
    }
    openstage::destroyStream(o.r);
    core::logf("rootgraph10: %s: Root takes its lone graph (1.0's older exploder form in a 3.0 file)", o.p);
    out = core::memFile(edited.data(), edited.size());
    return true;
}

inline bool olderCompanionStage(openstage::Open& o, int*&)
{
    if (!o.edits() || !o.cfg->rootGraph10 || text::endsWith(o.p, ".oct")) return false;
    const std::string stem = detail::modelStem(text::norm(o.p));
    if (stem.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(detail::g_state.mx);
        if (!detail::g_state.older.count(stem)) return false;
    }
    detail::passTo10(o, " (its model was served from 1.0)");
    return false;
}

inline bool statRewardStage(openstage::Open& o, int*& out)
{
    if (o.r || o.write || !o.cfg->statReward10 || !text::endsWith(o.p, ".lua_rewards")) return false;
    const std::string np = text::norm(o.p);
    std::string t, overrides;
    if (!vfs::read10s(np, t) || t.find("\"Stat\"") == std::string::npos) return false;
    const std::string stem = np.substr(0, np.size() - strlen("rewards"));
    vfs::read10s(stem + "overrides", overrides);
    int added = 0;
    std::string s = mission::statRewards10(t, overrides, added);
    if (!added) return false;   // serveStage serves 1.0's file as it is
    // 3.0 reads a Stat reward's amount from the reward itself; 1.0's sat in the overrides, so 3.0 paid 0
    core::logf("statreward10: %s: %d Stat reward(s) given their 1.0 overrides' Value", o.p, added);
    out = core::memFile(s.data(), s.size());
    return true;
}

}  // namespace di1::edits30
