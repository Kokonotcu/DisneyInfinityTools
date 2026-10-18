// hkOpen's stages that answer a 3.0 file with an edited copy (or a 1.0 CHD compile). games/di1, engine. Open threads.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../core/engine_file.hpp"
#include "../../core/log.hpp"
#include "../../core/stack.hpp"
#include "../../engine/gamedb.hpp"
#include "../../engine/zones.hpp"
#include "../../formats/chd.hpp"
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
// playsetmenu10: the Play Sets list gets the registered 1.0 playsets. Consumes r; answers the menu or a fresh open.
inline bool playsetMenuStage(openstage::Open& o, int*& out);
// currency10: the player HUD script rests on and counts a 1.0 zone's currency. Consumes r; answers text or reopens.
inline bool hudCurrencyStage(openstage::Open& o, int*& out);
// zonelist.lua (zones10, challenge10, beacon10), ChallengeData.lua (challenge10). Destroys r; answers text or reopens.
inline bool zoneListStage(openstage::Open& o, int*& out);
// trace: who opens a GameDB package's bitmasks.lua (any playset's, 1.0 or 3.0), with the stack. Always passes on.
inline bool bitmasksStack(openstage::Open& o, int*& out);
// A .lua the GameDB CHD callback loader is reading: its CHD compile. Consumes r (null or not).
inline bool chdStage(openstage::Open& o, int*& out);
// memtest: a listed path is served from a memory copy of 3.0's stream (consumed).
inline bool memtestStage(openstage::Open& o, int*& out);

namespace detail {

constexpr size_t kGameDbDir = sizeof("gamedb/") - 1;
constexpr int kStackWords = 24;   // the exe addresses a bitmasks open's line lists

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

// memtest: whether p contains one of the cfg's comma-separated substrings.
inline bool memtestMatch(const Config& c, const char* p)
{
    char list[sizeof(c.memtest)];
    snprintf(list, sizeof(list), "%s", c.memtest);   // never cut: the same size
    char* ctx = nullptr;
    for (char* t = strtok_s(list, ",", &ctx); t; t = strtok_s(nullptr, ",", &ctx))
        if (*t && strstr(p, t)) return true;
    return false;
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
    const bool menu = text::endsWith(o.p, "playsetlistwin32.lua");
    if (!menu || text::norm(o.p) != "presentation/playsetlistwin32.lua") return false;
    std::vector<std::pair<std::string, std::string>> ps = playsets::starts();
    std::string t;
    const bool read = core::readStream(o.r, t);
    const bool chunk = !t.empty() && t[0] == openstage::kLuaChunk;
    if (read && chunk && !ps.empty()) {
        std::string s = ui::playsetMenu10(t, ps), names;
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
    const bool playsetKeys = c.zones10 || c.playsetMenu10 || c.missionTrace;   // zones10() runs for these
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

inline bool bitmasksStack(openstage::Open& o, int*&)
{
    if (!o.cfg->trace) return false;
    if (_strnicmp(o.p, "gamedb/", detail::kGameDbDir) || !text::endsWith(o.p, "/bitmasks.lua")) return false;
    char line[600];
    // never cut: at most 327 characters before the stack's 216
    int len = snprintf(line, sizeof(line), "gamedb package open %s, stack:", o.p);
    core::stackOf(o.sp, line, sizeof(line), len, detail::kStackWords);
    core::logf("%s", line);
    return false;
}

inline bool chdStage(openstage::Open& o, int*& out)
{
    if (o.write || !text::endsWith(o.p, ".lua") || !engine::gamedb::isChdPath(o.p)) return false;
    out = detail::chdForLua(o.p, o.r);
    return true;
}

inline bool memtestStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->memtest[0] || !detail::memtestMatch(*o.cfg, o.p)) return false;
    size_t n = 0;
    out = core::copyToMemFile(o.r, &n);
    core::logf("memtest %s %u B -> %s", o.p, (unsigned)n, out ? "MemFile" : "FAILED");
    return true;
}

}  // namespace di1::edits30
