// di1compat: a Pentane plugin that serves Disney Infinity 1.0 data, read in place from its zips, to the 3.0 engine.

// Files reach the engine as Genie::MemFile streams through its asset open, FUN_0041cb10.

// Log: <game dir>\di1compat.log, not Pentane's (its calling convention is unverified). Config: games/di1/config.hpp.
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <string>
#include "minhook/MinHook.h"
#include "base/format.hpp"
#include "base/hash.hpp"
#include "base/text.hpp"
#include "core/engine_file.hpp"
#include "core/hooks.hpp"
#include "core/log.hpp"
#include "core/memcheck.hpp"
#include "core/readonly_file.hpp"
#include "core/stack.hpp"
#include "core/veh.hpp"
#include "core/zip.hpp"
#include "engine/assets.hpp"
#include "engine/choreography.hpp"
#include "engine/crt.hpp"
#include "engine/langkeys.hpp"
#include "engine/playsetslot.hpp"
#include "engine/shaderlinks.hpp"
#include "engine/shaders.hpp"
#include "engine/zones.hpp"
#include "games/di1/archives.hpp"
#include "games/di1/assetdefs.hpp"
#include "games/di1/choalias.hpp"
#include "games/di1/config.hpp"
#include "games/di1/currency.hpp"
#include "games/di1/econ.hpp"
#include "games/di1/expendables.hpp"
#include "games/di1/gamedb.hpp"
#include "games/di1/hideps.hpp"
#include "games/di1/meridian.hpp"
#include "games/di1/octsteps.hpp"
#include "games/di1/options.hpp"
#include "games/di1/playsets.hpp"
#include "games/di1/popin.hpp"
#include "games/di1/renderio.hpp"
#include "games/di1/render10.hpp"
#include "games/di1/screens.hpp"
#include "games/di1/session.hpp"
#include "games/di1/shaders10.hpp"
#include "games/di1/tables.hpp"
#include "games/di1/transcode.hpp"
#include "games/di1/transcodeio.hpp"
#include "games/di1/vfs.hpp"
#include "games/di1/zones.hpp"
#include "formats/oct.hpp"
#include "formats/chd.hpp"
#include "trace/breaktrace.hpp"
#include "trace/fade.hpp"
#include "trace/loadprof.hpp"
#include "trace/locktrace.hpp"
#include "trace/mission.hpp"
#include "trace/opacity.hpp"
#include "trace/renderdoc.hpp"
#include "trace/spawn.hpp"
#include "trace/spawntrace.hpp"
#include "trace/teardown.hpp"
#include "mer.hpp"
#include "mernode.hpp"
#include "games/di1/renderoct.hpp"
#include "formats/arc30.hpp"
#include "convert/datamap.hpp"
#include "formats/dct.hpp"
#include "formats/luachunk.hpp"
#include "sx.hpp"
#include "sys.hpp"
#include "npc.hpp"
#include "cho.hpp"
#include "ui.hpp"
#include "audio.hpp"
#include "games/di1/vfx.hpp"
#include "games/di1/chan.hpp"
#include "gameplay.hpp"
#include "npcrt.hpp"
#include "vfxrt.hpp"
#include "gprt.hpp"
#include "bldrt.hpp"
#include "caprt.hpp"
#include "third_party/miniz/miniz.h"

// ---- Pentane plugin ABI (upstream source/plugin.hpp; DI3G strings match) ----
struct SemVer { uint16_t major, minor, patch; };
struct PluginInformation {
    char name[256];
    char author[256];
    uint8_t uuid[16];
    SemVer version;
    SemVer minimum_pentane_version;
    char reserved[484];
};
static_assert(sizeof(PluginInformation) == 0x400, "Pentane expects 0x400 bytes");

extern "C" PluginInformation Pentane_PluginInformation = {
    "di1compat", "DI3Tools",
    {0x6d, 0x1c, 0x0e, 0x5a, 0x3f, 0x41, 0x4b, 0x8e, 0x9a, 0x21, 0xd1, 0x7c, 0x0a, 0x03, 0x30, 0x10},
    {0, 1, 0}, {0, 0, 0}, {}};
extern "C" size_t Pentane_PluginDependencyCount = 0;

// The services by their old names: this file's code calls them unqualified
using core::g_base;
using core::hook;
using core::installFeature;
using core::logf;
using core::rt;
using core::cstr;
using core::safeCopy;
using core::afterCall;
using core::avHandler;
using core::stackOf;
using core::copyToMemFile;
using core::memFile;
using core::readStream;
using text::endsWith;
using engine::assets::read30;
using engine::assets::read30x;
using text::norm;
using trace::loadprof::frameNo;
using trace::loadprof::noteOpenThread;
using trace::loadprof::nowMs;
using di1::vfs::read10;
using di1::vfs::read10s;

static di1::Config g_cfg;   // the cfg file's values (games/di1/config.hpp): set once in Pentane_Main, before any hook

static std::string g_gameDir;   // the 3.0 install; set once in Pentane_Main, before any hook

// D3D11 rejects block-compressed textures under 4x4; a 1x1/2x2 one-mip DXT file holds a whole block:
// header only (conv.copy_texture).
static void padSmallDxt(std::string& d)
{
    if (d.size() < 128 || d.compare(0, 4, "DDS ") || d.compare(84, 3, "DXT")) return;
    uint32_t h = oct::u((const uint8_t*)d.data() + 12, 4), w = oct::u((const uint8_t*)d.data() + 16, 4);
    uint32_t mips = oct::u((const uint8_t*)d.data() + 28, 4);
    if ((w < 4 || h < 4) && mips <= 1) {
        uint32_t v[2] = {h < 4 ? 4u : h, w < 4 ? 4u : w};
        memcpy(&d[12], v, 8);
    }
}

// memtest: whether p contains one of the cfg's comma-separated substrings.
static bool memtestMatch(const char* p)
{
    char list[sizeof(g_cfg.memtest)];
    snprintf(list, sizeof(list), "%s", g_cfg.memtest);   // never cut: the same size
    char* ctx = nullptr;
    for (char* t = strtok_s(list, ",", &ctx); t; t = strtok_s(nullptr, ",", &ctx))
        if (*t && strstr(p, t)) return true;
    return false;
}

// The choreography start hook: cinesync10 starts 1.0's audio-sync sound event (audio.hpp); breaktrace logs.
static bool installChoStart(const di1::Config& c)
{
    engine::choreography::Config cho;
    if (c.breakTrace) cho.starting = trace::breaktrace::choreographyStart;
    const bool cineSync = c.audio10 && c.cineSync10;
    if (cineSync)
        cho.started = [](const uint8_t*, const char* name, uint32_t flags) { audio::choreographyStarted(name, flags); };
    return engine::choreography::install(cho);
}

// missiontrace's config: 1.0 playset missions by zones10's prefixes; renderdoc10 arms on the first one's tick,
// missiontrace or not.
static trace::mission::Config missionTraceOf(const di1::Config& c)
{
    trace::mission::Config m;
    m.on = c.missionTrace;
    m.ownMission = di1::playsets::ownMission;
    if (trace::renderdoc::loaded()) m.ownMissionTick = trace::renderdoc::arm;
    return m;
}

// Open half: a .lua stream (3.0's, or 1.0's served) as CHD bytes; nullptr leaves the open as it was.
static int* chdForLua(const char* p, int* r)
{
    std::string src;
    if (r) { if (!readStream(r, src)) return memFile(src.data(), src.size()); }
    else if (!read10s(norm(p), src)) return nullptr;
    try {
        std::string c = chd::compile(chd::lua(src));
        logf("compiled %s %u B -> CHD %u B (%s)", p, (unsigned)src.size(), (unsigned)c.size(),
             r ? "3.0 file" : "1.0 file");
        return memFile(c.data(), c.size());
    } catch (std::exception& e) {
        logf("CHD compile of %s failed: %s; served as it is", p, e.what());
        return memFile(src.data(), src.size());
    }
}

// sys.hpp supplements: a 1.0 GameDB table reduced to the records the target DB lacks (served under a di1sys/ path)
static int* sysServe(const char* p)
{
    std::string np = norm(p), src, out;
    size_t rpre = strlen(sys::kRaw10Prefix);
    if (!np.compare(0, rpre, sys::kRaw10Prefix)) {   // the 1.0 file at a path 3.0 also has
        bool ok = read10s(np.substr(rpre), src);
        logf("served %s %u B from 1.0 %s%s", p, (unsigned)src.size(), np.substr(rpre).c_str(), ok ? "" : " -- MISSING");
        return ok ? memFile(src.data(), src.size()) : nullptr;
    }
    size_t cpre = strlen(sys::kChest10Prefix);
    if (!np.compare(0, cpre, sys::kChest10Prefix) && endsWith(p, ".lua")) {   // GLB-1 ChestInfo.bin -> Lua
        std::string bin = np.substr(cpre, np.size() - cpre - 4) + ".bin", why;
        bool ok = read10s(bin, src) && sys::chestInfoLua(src, out, &why);
        logf("served %s from 1.0 %s (%u B) -> chestInfo Lua %u B%s%s", p, bin.c_str(), (unsigned)src.size(),
             (unsigned)out.size(), ok ? "" : " FAILED: ", ok ? "" : (src.empty() ? "no 1.0 file" : why.c_str()));
        return ok ? memFile(out.data(), out.size()) : nullptr;
    }
    size_t ppre = strlen(sys::kPerc10Prefix);
    if (!np.compare(0, ppre, sys::kPerc10Prefix) && endsWith(p, ".lua")) {   // GLB-1 PerceptionInfo.bin -> Lua
        std::string bin = np.substr(ppre, np.size() - ppre - 4) + ".bin", why;
        int added = 0;
        bool ok = read10s(bin, src) && sys::perceptionInfoLua(src, sys::perc30(), out, added, &why);
        logf("served %s from 1.0 %s (%u B) -> perceptions Lua %u B, %d perceptions 3.0 lacks%s%s", p, bin.c_str(),
             (unsigned)src.size(), (unsigned)out.size(), added, ok ? "" : " FAILED: ",
             ok ? "" : (src.empty() ? "no 1.0 file" : why.c_str()));
        return ok ? memFile(out.data(), out.size()) : nullptr;
    }
    size_t pre = strlen(sys::kLocks10Prefix);
    if (np.compare(0, pre, sys::kLocks10Prefix)) return nullptr;
    if (!read10s(np.substr(pre), src)) { logf("supplement %s: no 1.0 table", p); return nullptr; }
    std::set<uint32_t> have = sys::lockUids();
    int kept = 0, total = 0;
    bool ok = sys::keepRecords(src, [&](uint32_t u) { return !have.count(u); }, out, kept, total);
    logf("supplement %s: %d of %d 1.0 lock records (UIDs the Locks DB lacked; it held %u)%s", p, kept, total,
         (unsigned)have.size(), ok ? "" : " -- layout not found, not served");
    return ok ? memFile(out.data(), out.size()) : nullptr;
}

// ---- AssetOpen: every engine file open goes through hkOpen's stages ----
constexpr uint32_t kOpenWrite = 1;   // AssetOpen flags bit 0: a write, never served or rewritten
constexpr char kLuaChunk = 0x1b;     // a compiled Lua chunk's first byte ("\x1bLua")
constexpr size_t kGameDbDir = sizeof("gamedb/") - 1;
constexpr size_t kSysDir = sizeof("di1sys/") - 1;

// One open as the stages see it: FUN_0041cb10's arguments, the engine's own stream and the path copied.
struct Open {
    const char* path;
    uint32_t flags, a3, a4;
    int* a5;
    const uint32_t* sp;                   // hkOpen's return address slot: its stack walks start there
    bool write;                           // flags & kOpenWrite
    bool prof;                            // loadprof10: the served and missing lines carry timings
    double tIn = 0, t30 = 0, tRead = 0;   // loadprof10: start, the engine's own open, the 1.0 read
    int* r = nullptr;                     // the engine's stream, null when 3.0 has no such file
    char p[300] = "";
    bool blocked = false;                 // block10 matched
    std::string missWhy = " (not served)";

    // A 3.0 file opened for reading: the stages that rewrite 3.0 files take these.
    bool edits() const { return r && !write; }
};

// The engine's own open with the same arguments (a stage that consumed r answers this).
static int* reopen(const Open& o) { return engine::assets::open(o.path, o.flags, o.a3, o.a4, o.a5); }

// r read whole as read30 reads it: no Read when empty, the full size kept on a short read.
static bool readKeep(int* r, std::string& t)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    void** vt = *(void***)r;
    t.resize(((SizeFn)vt[core::eng::kFileSize])(r, nullptr));
    uint32_t got = t.empty() ? 0 : ((ReadFn)vt[core::eng::kFileRead])(r, nullptr, &t[0], (uint32_t)t.size());
    return got == t.size();
}

static void destroyStream(int* r)
{
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    ((DtorFn)(*(void***)r)[core::eng::kFileDestroy])(r, nullptr, core::eng::kDestroyFree);
}

// 3.0's tips (o.r, consumed) plus the 1.0 records it lacks; registered zones' keys stay 3.0's (FUN_01391150).
static int* tips30(const Open& o)
{
    std::string t30, t10;
    if (!readStream(o.r, t30)) return reopen(o);
    if (t30.find("ZoneTipCategories") == std::string::npos) return memFile(t30.data(), t30.size());
    const bool got10 = read10s("data/tips.lua", t10);
    if (!got10) return memFile(t30.data(), t30.size());
    void* zm = engine::zones::manager();
    int k = ui::mergeTips(t30, t10,
                          [&](const std::string& z) { return zm && engine::zones::zoneRegistered(zm, z.c_str()); });
    logf("tips %s: %d 1.0 records added (zone manager %s)", o.path, k, zm ? "up" : "NOT up: zone keys unchecked");
    return memFile(t30.data(), t30.size());
}

// shaders10: 3.0's shaders/vformat.vtxf plus 1.0's formats. Destroys r; answers the merge or a fresh open.
static bool vtxfStage(Open& o, int*& out)
{
    if (!o.edits() || !g_cfg.shaders10 || _stricmp(o.p, "shaders/vformat.vtxf") != 0) return false;
    std::string t30, t10;
    const bool full = readKeep(o.r, t30);
    if (full && read10s("shaders/vformat.vtxf", t10)) {
        std::string m = di1::render10::mergeVtxf(t30, t10);
        destroyStream(o.r);
        out = memFile(m.data(), m.size());
        return true;
    }
    destroyStream(o.r);
    out = reopen(o);
    return true;
}

// tips10: the file named Tips.lua only (FUN_00b70da0's literal), not every *tips.lua. tips30 consumes r.
static bool tipsStage(Open& o, int*& out)
{
    const bool on = g_cfg.serve10 && g_cfg.tips10;
    if (!o.edits() || !on) return false;
    const bool tips = !_stricmp(o.p, "tips.lua") || endsWith(o.p, "/tips.lua") || endsWith(o.p, "\\tips.lua");
    if (!tips) return false;
    out = tips30(o);
    return true;
}

// playsetmenu10: the Play Sets list gets the registered 1.0 playsets. Consumes r; answers the menu or a fresh open.
static bool playsetMenuStage(Open& o, int*& out)
{
    if (!o.edits() || !g_cfg.playsetMenu10) return false;
    if (!endsWith(o.p, "playsetlistwin32.lua") || norm(o.p) != "presentation/playsetlistwin32.lua") return false;
    std::vector<std::pair<std::string, std::string>> ps = di1::playsets::starts();
    std::string t;
    const bool read = readStream(o.r, t);
    const bool chunk = !t.empty() && t[0] == kLuaChunk;
    if (read && chunk && !ps.empty()) {
        std::string s = ui::playsetMenu10(t, ps), names;
        for (auto& x : ps) names += " " + x.first + "->" + x.second;
        logf("%s: %u 1.0 playset entries added:%s (%u B 3.0 chunk -> %u B Lua)", o.p, (unsigned)ps.size(),
             names.c_str(), (unsigned)t.size(), (unsigned)s.size());
        out = memFile(s.data(), s.size());
        return true;
    }
    logf("%s: %s (%u B): unchanged", o.p, ps.empty() ? "no 1.0 playset registered" : "not the 3.0 chunk",
         (unsigned)t.size());
    out = reopen(o);
    return true;
}

// zonelist.lua (zones10, challenge10, beacon10), ChallengeData.lua (challenge10). Destroys r; answers text or reopens.
static bool zoneListStage(Open& o, int*& out)
{
    const bool zl = endsWith(o.p, "zonelist.lua");
    const bool cd =
        endsWith(o.p, "challengedata/challengedata.lua") || endsWith(o.p, "challengedata\\challengedata.lua");
    const bool playsets = g_cfg.zones10 || g_cfg.playsetMenu10 || g_cfg.missionTrace;   // zones10() runs for these
    const bool zl10 = zl && playsets;
    const bool challengeFile = zl || cd;
    const bool challenge = g_cfg.challenge10 && challengeFile;
    const bool beacon = g_cfg.beacon10 && zl;
    const bool wanted = challenge || beacon || zl10;
    if (!o.edits() || !wanted) return false;
    std::string t, t10;
    const bool full = readKeep(o.r, t);
    destroyStream(o.r);
    int k = 0, kb = 0, ka = 0, kz = 0;
    if (full && !t.empty() && t[0] != kLuaChunk) {
        if (zl10) kz = di1::playsets::registerPlaysets(t, o.p, read10s);   // first: later steps see the added records
        if (zl && g_cfg.challenge10) {
            k = gameplay::zonelist30(t);
        } else if (cd) {
            const bool got10 = read10s("activitydata/defaultactivitybeacons.lua", t10);
            if (got10) k = gameplay::beacons30(t, t10);
        }
        if (beacon) kb = mission::beaconMap30(t, &ka);
    }
    if (g_cfg.challenge10)
        logf("%s: %d %s", o.p, k, zl ? "playset records got ChallengeTypeDataFile" : "1.0 beacons added");
    if (beacon) logf("%s: %d BeaconMap rows in 3.0 form, %d Beacon_Arrow_Blue rows added (beacon10)", o.p, kb, ka);
    out = k || kb || kz ? memFile(t.data(), t.size()) : reopen(o);
    return true;
}

// trace: who opens a GameDB package's bitmasks.lua (any playset's, 1.0 or 3.0), with the stack. Always passes on.
static bool bitmasksStack(Open& o, int*&)
{
    if (!g_cfg.trace) return false;
    if (_strnicmp(o.p, "gamedb/", kGameDbDir) || !endsWith(o.p, "/bitmasks.lua")) return false;
    char line[600];
    // never cut: at most 327 characters before the stack's 216
    int len = snprintf(line, sizeof(line), "gamedb package open %s, stack:", o.p);
    stackOf(o.sp, line, sizeof(line), len, 24);
    logf("%s", line);
    return false;
}

// A .lua the GameDB CHD callback loader is reading: its CHD compile. chdForLua consumes r (null or not).
static bool chdStage(Open& o, int*& out)
{
    if (o.write || !endsWith(o.p, ".lua") || !engine::gamedb::isChdPath(o.p)) return false;
    out = chdForLua(o.p, o.r);
    return true;
}

// serve10: a di1sys/ path is a GameDB supplement (sys.hpp), served only where 3.0 has no file.
static bool sysStage(Open& o, int*& out)
{
    const bool absent = !o.r && !o.write;
    if (!absent || !g_cfg.serve10 || _strnicmp(o.p, "di1sys/", kSysDir)) return false;
    out = sysServe(o.p);
    return true;
}

// memtest: a listed path is served from a memory copy of 3.0's stream (consumed).
static bool memtestStage(Open& o, int*& out)
{
    if (!o.edits() || !g_cfg.memtest[0] || !memtestMatch(o.p)) return false;
    size_t n = 0;
    out = copyToMemFile(o.r, &n);
    logf("memtest %s %u B -> %s", o.p, (unsigned)n, out ? "MemFile" : "FAILED");
    return true;
}

// block10: a path under a listed prefix is never served from 1.0. Always passes on.
static bool blockStage(Open& o, int*&)
{
    if (!g_cfg.block10[0]) return false;
    std::string np = norm(o.p), list = g_cfg.block10;
    for (size_t a = 0, b; a < list.size(); a = b + 1) {
        b = list.find(',', a);
        if (b == std::string::npos) b = list.size();
        std::string pre = list.substr(a, b - a);
        if (!pre.empty() && np.rfind(pre, 0) == 0) o.blocked = true;
    }
    return false;
}

static int* serveSession(const Open& o, const std::string& hit)
{
    int* r = memFile(hit.data(), hit.size());
    char pf[100] = "";
    if (o.prof)
        format::fitOrFailFast(pf, sizeof(pf), " [prof tid %u frame %ld: 3.0 open %.2f, total %.2f ms]",
                              GetCurrentThreadId(), frameNo(), o.t30, nowMs() - o.tIn);
    logf("served %s %u B from asset session -> %s%s", o.p, (unsigned)hit.size(), r ? "ok" : "FAILED", pf);
    return r;
}

// The 1.0 bytes an open is served from: the member (mz_free it), its size, "zip:member".
struct Read10 {
    void* data = nullptr;
    size_t n = 0;
    std::string where;
    std::string chal;   // gp: the ChallengeData/<1.0 type file> path read instead, "" if none
};

// uiscope10: an image of a flash movie not served from 1.0 is not served either; that sets the miss reason.
static bool imageOfMovie30(Open& o, const std::string& np)
{
    std::string movie = g_cfg.uiScope10 ? ui::flashImageMovie(np) : "";
    if (movie.empty()) return false;
    const bool scoped = !di1::session::movieServed(movie);
    if (scoped) o.missWhy = " (image id of a movie not served from 1.0: not served)";
    return scoped;
}

constexpr size_t kPcConSuffix = sizeof("_pc_con.dct") - 1;
constexpr size_t kHoloSuffix = sizeof("_holo.mtb") - 1;

// Stand-ins 1.0 never shipped: a language table's WIN32 for its _pc_con, a giver's portrait for its _Holo icon.
static void* read10StandIn(const Open& o, const std::string& np, size_t* n, std::string* where)
{
    if (endsWith(o.p, "_pc_con.dct")) {
        std::string w = std::string(o.p, strlen(o.p) - kPcConSuffix) + "_WIN32.dct";
        return read10(w.c_str(), n, where);
    }
    if (np.rfind("ui/charactericons/", 0) != 0 || !endsWith(o.p, "_holo.mtb")) return nullptr;
    std::string w = std::string(o.p, strlen(o.p) - kHoloSuffix) + ".mtb";
    return read10(w.c_str(), n, where);
}

// vo10: a 1.0 dialog line's lipsync (audio.hpp annoSource), read from its zip.
static void* read10Lipsync(const char* p, size_t* n, std::string* where)
{
    std::string annoZip, annoMember;
    const bool vo = g_cfg.audio10 && g_cfg.vo10;
    if (!vo || !audio::annoSource(p, annoZip, annoMember)) return nullptr;
    void* data = nullptr;
    di1::vfs::withArchive(annoZip, [&](mz_zip_archive* z) {
        if (z) data = core::extractZipMember(z, annoMember.c_str(), n);
    });
    if (data) *where = annoZip + ":" + annoMember;
    return data;
}

// The 1.0 read of a missing file: overlay member, ChallengeData redirect, stand-ins, lipsync. Sets the miss reason.
static Read10 read10For(Open& o, const std::string& np, const std::string& over)
{
    double tR = o.prof ? nowMs() : 0;
    Read10 rd;
    // the count includes the candidate names that were not there (A-12)
    o.missWhy = over.empty()
                    ? " (not in the 1.0 archives, " + std::to_string(di1::vfs::archiveCount()) + " archives open)"
                    : " (overlay " + over + " unreadable)";
    const bool scoped = imageOfMovie30(o, np);
    rd.chal = g_cfg.challenge10 ? gameplay::challengeTypePath(np) : "";
    const char* from = !rd.chal.empty() ? rd.chal.c_str() : over.empty() ? o.p : over.c_str();
    rd.data = scoped ? nullptr : read10(from, &rd.n, &rd.where);
    if (!rd.data) rd.data = read10StandIn(o, np, &rd.n, &rd.where);
    if (!rd.data) rd.data = read10Lipsync(o.p, &rd.n, &rd.where);
    if (o.prof) o.tRead = nowMs() - tR;
    return rd;
}

// A served file's bytes in the form 3.0 reads, its codec ("" raw) and the note its line carries.
struct Transcoded {
    std::string out, note;
    const char* codec = nullptr;
};

constexpr size_t kMerSuffix = sizeof(".mer") - 1;

// A served file's transcode; a throw never reaches the engine: it is noted, the raw bytes served.
static Transcoded transcodeServed(const Open& o, const std::string& np, const std::string& over, const Read10& rd)
{
    Transcoded t;
    std::string mtc;
    try {
        if (endsWith(o.p, ".tbody")) {   // D3D11 refuses sub-4x4 block textures (padSmallDxt)
            t.out.assign((const char*)rd.data, rd.n);
            padSmallDxt(t.out);
            t.codec = t.out.size() == rd.n && memcmp(t.out.data(), rd.data, rd.n) ? "tbody" : nullptr;
        } else {
            t.codec =
                di1::transcode({o.p, rd.data, rd.n}, {t.out, t.note, &mtc}, di1::transcodeio::options(),
                               di1::transcodeio::io());
        }
        // the .mtc request transcodes read10s(<same path>.mer): these bytes when nothing redirected the read
        const bool mer = t.codec && !strcmp(t.codec, "mer") && !mtc.empty();
        if (mer && over.empty() && rd.chal.empty())
            di1::session::keep({{np.substr(0, np.size() - kMerSuffix) + ".mtc", mtc}});
    } catch (std::exception& e) {   // never unwind into engine code (that ends in a silent fail-fast)
        t.note += std::string("TRANSCODE EXCEPTION: ") + e.what() + " ";
        t.codec = nullptr;
    } catch (...) {
        t.note += "TRANSCODE EXCEPTION ";
        t.codec = nullptr;
    }
    if (!over.empty()) t.note += "(archive overlay) ";
    return t;
}

constexpr size_t kPresentationDir = sizeof("presentation/") - 1;
constexpr size_t kLuaSuffix = sizeof(".lua") - 1;

// ui: a flash movie served from 1.0 (its images may be too) and a presentation screen served from 1.0 (gprt).
static void noteServedUi(const std::string& np, const char* p)
{
    std::string mv = ui::flashMovie(np);
    if (!mv.empty()) di1::session::noteMovie(mv);
    if (np.rfind("presentation/", 0) != 0 || !endsWith(p, ".lua")) return;
    di1::session::noteScreen(np.substr(kPresentationDir, np.size() - kPresentationDir - kLuaSuffix));
}

// loadprof10: the engine stack of the first 3 served files per kind (15 in all), appended to pf.
static void appendServedStack(const char* p, const uint32_t* sp, char* pf, size_t cap, int len)
{
    static const char* const kKinds[] = {".oct", ".bent", ".banm", ".mer", ".tbody"};
    static std::atomic<LONG> stacks, perKind[5];   // any open thread
    for (int k = 0; k < 5; ++k) {
        if (!endsWith(p, kKinds[k])) continue;
        const LONG ofKind = ++perKind[k];
        if (ofKind > 3) continue;
        const LONG all = ++stacks;
        if (all > 15) continue;
        len += format::fitOrFailFast(pf + len, cap - len, " stack:");
        stackOf(sp, pf, cap, len, 16);
    }
}

// A file served from 1.0: indexed overlay, transcode, MemFile, the served line. Frees rd's bytes.
static int* serveRead(const Open& o, const std::string& np, const std::string& over, const Read10& rd)
{
    di1::session::indexArchive(rd.where.substr(0, rd.where.find(':')));
    double tT = o.prof ? nowMs() : 0;
    Transcoded t = transcodeServed(o, np, over, rd);
    noteServedUi(np, o.p);
    double tTc = o.prof ? nowMs() - tT : 0;
    double tM = o.prof ? nowMs() : 0;
    int* r = t.codec ? memFile(t.out.data(), t.out.size()) : memFile(rd.data, rd.n);
    char pf[600] = "";
    if (o.prof) {
        int len = format::fitOrFailFast(pf, sizeof(pf), " [prof tid %u frame %ld: 3.0 open %.2f, 1.0 read %.2f, "
                                        "transcode %.2f, memfile %.2f, total %.2f ms]", GetCurrentThreadId(), frameNo(),
                                        o.t30, o.tRead, tTc, nowMs() - tM, nowMs() - o.tIn);
        appendServedStack(o.p, o.sp, pf, sizeof(pf), len);
    }
    logf("served %s %u B from 1.0 %s codec=%s%s%s -> %s%s", o.p, (unsigned)(t.codec ? t.out.size() : rd.n),
         rd.where.c_str(), t.codec ? t.codec : "raw", t.note.empty() ? "" : " ", t.note.c_str(), r ? "ok" : "FAILED",
         pf);
    mz_free(rd.data);
    return r;
}

// serve10: a file 3.0 lacks, from the session or the 1.0 install; passes on when 1.0 has none.
static bool serveStage(Open& o, int*& out)
{
    const bool absent = !o.r && !o.blocked && !o.write;
    if (!absent || !g_cfg.serve10 || !g_cfg.install10[0]) return false;
    std::string np = norm(o.p), hit, over;
    di1::session::lookup(np, hit, over);
    if (!hit.empty()) {
        out = serveSession(o, hit);
        return true;
    }
    Read10 rd = read10For(o, np, over);
    if (!rd.data) return false;
    out = serveRead(o, np, over, rd);
    return true;
}

// Misses always (rare and cheap: what an unexplained fallback asks for), hits only when tracing.
static void logOpen(const Open& o)
{
    char pf[120] = "";
    if (o.prof)
        format::fitOrFailFast(pf, sizeof(pf), " [prof tid %u frame %ld: 3.0 open %.2f, 1.0 lookup %.2f, total %.2f ms]",
                              GetCurrentThreadId(), frameNo(), o.t30, o.tRead, nowMs() - o.tIn);
    if (g_cfg.trace || !o.r)
        logf("open %s flags=%X -> %s%s%s", o.p, o.flags & 0xff, o.r ? "ok" : "MISSING", o.r ? "" : o.missWhy.c_str(),
             pf);
}

// A stage answers (true, out) or passes the open on. Each one's comment says what it does with r.
typedef bool (*OpenStage)(Open& o, int*& out);

// First answer wins, in this order (B3, C3); bitmasksStack, blockStage and a serveStage miss pass on.
static const OpenStage kOpenStages[] = {vtxfStage, tipsStage,    playsetMenuStage, zoneListStage, bitmasksStack,
                                        chdStage,  sysStage,     memtestStage,     blockStage,    serveStage};

// FUN_0041cb10, cdecl (path, flags, a3, a4, a5): AssetOpen, every engine file open; several engine threads.
static int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    Open o{path, flags, a3, a4, a5, (const uint32_t*)_AddressOfReturnAddress(), (flags & kOpenWrite) != 0,
           g_cfg.loadProf10};
    if (o.prof) {
        o.tIn = nowMs();
        noteOpenThread();
    }
    o.r = engine::assets::open(path, flags, a3, a4, a5);
    if (o.prof) o.t30 = nowMs() - o.tIn;
    safeCopy(o.p, sizeof(o.p), path);
    for (OpenStage stage : kOpenStages) {
        int* out = nullptr;
        if (stage(o, out)) return out;
    }
    logOpen(o);
    return o.r;
}

// sxnatives10's hooks (H1). sx::g_log is set in every config: BMX-1 and BTX-1 log unpaired key ids through it.
static bool installSx(const di1::Config& c)
{
    sx::g_log = logf;
    if (!c.sx10 || !c.sxNatives10) return true;
    sx::g_exe = g_base;
    sx::init(c.sxPorts10);
    std::vector<core::HookSpec> natives = {
        {"Slang linker (1.0 natives)", 0x00736DA0, "51538B5C240C568BF18BCBE8", (void*)&sx::hkLink,
         (void**)&sx::g_link}};
    if (c.sxPorts10 && c.sxBeh10)
        natives.push_back({"Slang call by name (1.0 behavior type)", 0x00739090, "56578B7C240C8BF18B4E04",
                           (void*)&sx::hkCallByName, (void**)&sx::g_callByName});
    return installFeature("sxnatives10", natives);
}

// audio10's hooks (audio::install) need the 1.0 install; its log goes in first.
static bool installAudio(const di1::Config& c)
{
    if (!c.audio10 || !c.install10[0]) return true;
    audio::logfn = logf;
    audio::Cfg a;
    a.music = c.music10;
    a.vo = c.vo10;
    a.trace = c.audioTrace10;
    a.mixer = c.mixer10;
    a.install10 = c.install10;
    a.game30 = g_gameDir;
    a.installFeature = installFeature;
    a.rt = rt;
    if (audio::install(a)) return true;
    logf("audio10: off (FMOD host failed)");
    return false;
}

// Before any hook, in this order: RenderDoc, 1.0 archives, resident archives, tables, shaders10, playsets.
static void prepare(const di1::Config& c)
{
    trace::renderdoc::install({c.renderdoc10, g_gameDir + "\\di3cap"});
    di1::vfs::install(c);
    di1::choalias::install(c);
    di1::session::install(c);
    di1::tables::install(c);
    di1::render10::install(c);
    di1::playsets::install(c);
    engine::langkeys::install({g_gameDir, read30x});
    di1::transcodeio::install(c);
}

// The first-AV handler, then MinHook; false: no hook can be installed.
static bool startHooks()
{
    AddVectoredExceptionHandler(1, avHandler);
    if (MH_Initialize() == MH_OK) return true;
    logf("MinHook init failed; no hooks");
    return false;
}

extern "C" void __stdcall Pentane_Main();
extern "C" void __stdcall Pentane_Main()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) format::fitOrFailFast(slash + 1, path + MAX_PATH - (slash + 1), "%s", "di1compat.log");
    {   // the last 5 runs' logs stay as di1compat.1.log (newest) .. di1compat.5.log: a launch keeps the evidence
        std::string base(path, strlen(path) - 4);
        for (int i = 4; i >= 0; --i)
            MoveFileExA(i ? (base + "." + std::to_string(i) + ".log").c_str() : path,
                        (base + "." + std::to_string(i + 1) + ".log").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    core::openLog(path);
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    g_gameDir.assign(path, slash ? slash - path : 0);
    logf("di1compat 0.3.0 loaded; exe base %08X", (unsigned)g_base);
    char dir[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&Pentane_Main, &self);
    GetModuleFileNameA(self, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) *s = 0;
    di1::loadConfig(dir, g_cfg);
    prepare(g_cfg);

    // the modules' hooks in install order (B3): a hook is live once enabled
    if (!startHooks()) return;
    if (!engine::assets::install({(void*)&hkOpen, g_gameDir})) return;
    di1::gamedb::install(g_cfg);
    di1::econ::install(g_cfg);
    di1::expendables::install(g_cfg);
    di1::currency::install(g_cfg);
    di1::popin::install(g_cfg);
    engine::playsetslot::install({});   // always on: no cfg key (B-20)
    trace::spawn::install({g_cfg.trace, g_cfg.hidePs});
    di1::hideps::install(g_cfg);
    di1::shaders10::install(g_cfg);
    trace::mission::install(missionTraceOf(g_cfg));
    installChoStart(g_cfg);
    trace::breaktrace::install({g_cfg.breakTrace});
    trace::teardown::install({g_cfg.teardownTrace});
    di1::meridian::install({g_cfg.aliases10, g_cfg.native10, di1::tables::mer()});
    trace::locktrace::install({g_cfg.lockTrace10});
    trace::opacity::install({g_cfg.opacityLog10});
    trace::fade::install({g_cfg.fadeProbe10});
    trace::loadprof::install({g_cfg.fpsLog10, g_cfg.loadProf10, g_gameDir});
    di1::assetdefs::install(g_cfg);
    engine::crt::install({});
    installSx(g_cfg);
    npcrt::install(g_cfg);
    di1::screens::install(g_cfg);
    installAudio(g_cfg);
    vfxrt::install(g_cfg);
    gprt::install(g_cfg);
    bldrt::install(g_cfg);
    caprt::install(g_cfg);
    trace::spawntrace::install({g_cfg.spawnTrace10 && !g_cfg.trace});
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
