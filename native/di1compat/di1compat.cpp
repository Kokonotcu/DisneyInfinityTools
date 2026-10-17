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
#include "engine/playsetslot.hpp"
#include "engine/shaderlinks.hpp"
#include "engine/shaders.hpp"
#include "engine/zones.hpp"
#include "games/di1/archives.hpp"
#include "games/di1/assetdefs.hpp"
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
#include "games/di1/screens.hpp"
#include "games/di1/transcode.hpp"
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

// core's services by their old names: this file and the runtime headers it includes below call them unqualified
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

static di1::Config g_cfg;   // the cfg file's values (games/di1/config.hpp): set once in Pentane_Main, before any hook

// ---- VFS1: the 1.0 install's own zips, read in place ----

// The open archives, by path under assets; miniz is not thread-safe. Search order: games/di1/archives.hpp.
static std::mutex g_zipLock;
static std::map<std::string, std::unique_ptr<mz_zip_archive>> g_zips;   // guarded by g_zipLock; nullptr = no such zip

// The archive rel, opened on first use and kept for the session; failures too (A-12). Caller holds g_zipLock.
static mz_zip_archive* zipFor(const std::string& rel)
{
    auto it = g_zips.find(rel);
    if (it != g_zips.end()) return it->second.get();
    return (g_zips[rel] = core::openZip(di1::archivePath(g_cfg.install10, rel))).get();
}

static cho::Aliases g_choAlias;   // Cho DataMap paths outside choreographies/<n>/<n> (cho.hpp, choalias10)

// -> heap buffer (mz_free it) with the member's bytes, or nullptr. where = "zip:member" for the proof line.
static void* read10z(const std::string& p, size_t* size, std::string* where);
static void* read10(const char* enginePath, size_t* size, std::string* where)
{
    std::string p = norm(enginePath);
    void* d = read10z(p, size, where);
    std::string a = d || !g_cfg.choAlias10 ? "" : g_choAlias.resolve(p);
    return a.empty() ? d : read10z(a, size, where);
}

static void* read10z(const std::string& p, size_t* size, std::string* where)
{
    std::lock_guard<std::mutex> lock(g_zipLock);
    const di1::AssetBuffers buffers = g_cfg.assetBufs10 ? di1::AssetBuffers::Search : di1::AssetBuffers::Skip;
    for (size_t cut = p.rfind('/'); cut != std::string::npos && cut > 0; cut = p.rfind('/', cut - 1)) {
        std::string member = p.substr(cut + 1);
        for (const std::string& zr : di1::candidateArchives(p, cut, buffers)) {
            mz_zip_archive* z = zr.empty() ? nullptr : zipFor(zr);
            void* data = z ? core::extractZipMember(z, member.c_str(), size) : nullptr;
            if (data) { *where = zr + ":" + member; return data; }
        }
    }
    if (g_cfg.root10)
        for (const char* zr : di1::kRootArchives) {
            mz_zip_archive* z = zipFor(zr);
            void* data = z ? core::extractZipMember(z, p.c_str(), size) : nullptr;
            if (data) { *where = std::string(zr) + ":" + p; return data; }
        }
    return nullptr;
}

// ---- load-time transcodes: 1.0 bytes in, the form the 3.0 reader takes out (the 1.0 file is never rewritten) ----
static bool read10s(const std::string& path, std::string& out)
{
    size_t n = 0;
    std::string where;
    void* d = read10(path.c_str(), &n, &where);
    if (!d) return false;
    out.assign((const char*)d, n);
    mz_free(d);
    return true;
}

// npcrt calls the engine's own open by this name (its internals move in a later lane).
static int* g_open(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    return engine::assets::open(path, flags, a3, a4, a5);
}

static std::string g_gameDir;   // the 3.0 install; set once in Pentane_Main, before any hook

// Every 3.0 table's key hashes for language lang (both .dct kinds of each assets\lang package), read once (text10).
static const std::unordered_set<uint32_t>& lang30(const std::string& lang)
{
    static std::mutex mx;
    static std::map<std::string, std::unordered_set<uint32_t>> cache;
    std::lock_guard<std::mutex> lock(mx);
    auto it = cache.find(lang);
    if (it != cache.end()) return it->second;
    auto& s = cache[lang];
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((g_gameDir + "\\assets\\lang\\*").c_str(), &fd);
    for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileA(h, &fd)) {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        std::string pkg = fd.cFileName, t;
        for (const char* v : {"_win32.dct", "_pc_con.dct"})
            if (read30x("3:lang/" + pkg + "/" + pkg + "/" + lang + v, t)) dct::hashes(t, s);
    }
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    return s;
}

// World session: one .oct request transcodes the whole world; the engine's next requests for its files come from here.
static std::mutex g_sessLock;
static std::map<std::string, std::string> g_served;   // guarded by g_sessLock
// 1.0 archives' full-path members (path -> "<archive>/<member>"), seen once 1.0 loads the archive; first wins.
static std::map<std::string, std::string> g_over10;   // guarded by g_sessLock
static std::set<std::string> g_indexed10;
static std::set<std::string> g_movies10;    // flash/<dir>/<movie> served from 1.0 (ui: their images may be too)
static std::set<std::string> g_screens10;   // presentation/<screen>.lua served from 1.0: lower-case screen names (gprt)
static rentable::Table g_render;
static vtxf::VFormats g_vf10;              // 1.0 vertex formats (real 1.0 shaders: shaders10)
static vtxf::VFormats g_vf30;              // 3.0's (skintr10: the formats skinned passes draw in), set with the vtxf
static std::map<uint32_t, vtxf::ColourTail> g_tails10;   // re-laid 1.0 formats in the served vformat.vtxf
static bool g_renderOk;

// Both games' scenesettingsformat.ssf (ssf::relayBlock), parsed once; both or neither (scenesettings10).
static void sceneSettingsIO(render::IO& io)
{
    static std::mutex mx;
    static bool done, ok;
    static ssf::Format f10, f30;
    std::lock_guard<std::mutex> lock(mx);
    if (!done) {
        done = true;
        ok = di1::sceneSettings(read10s, read30x, f10, f30);
        logf("scene settings formats: %s (1.0 block %d B, 3.0 block %d B)", ok ? "ok" : "MISSING", f10.size, f30.size);
    }
    if (ok && g_cfg.sceneSettings10) { io.ssf10 = &f10; io.ssf30 = &f30; }
}
static mer::Table g_mer;
static bool g_merOk;

static void indexZip10(const std::string& zipRel)
{
    std::lock_guard<std::mutex> lock(g_zipLock);
    mz_zip_archive* z = zipFor(zipRel);
    if (!z) return;
    std::lock_guard<std::mutex> l2(g_sessLock);
    if (!g_indexed10.insert(zipRel).second) return;
    std::string base = zipRel.substr(0, zipRel.size() - 4) + "/";
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z); ++i) {
        char name[300];
        mz_zip_reader_get_filename(z, i, name, sizeof(name));
        std::string n = name;
        for (char& c : n) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
        if (n.find('/') != std::string::npos && n.back() != '/') g_over10.emplace(n, base + name);
        if (!g_cfg.vfx10) continue;
        // 3.0 opens meshes by hash
        if (std::string a = vfx::p3dAlias(n); !a.empty()) g_over10.emplace(a, base + name);
    }
}

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

#include "npcrt.hpp"

// The render half of a 1.0 asset with a material bundle (render.hpp): materials onto 3.0 shaders, vertices into
// their formats.
static bool transcodeAssetRender(const std::string& p, oct::File& f, std::string& note)
{
    if (!g_renderOk) { note += "render table missing "; return false; }
    size_t b = p.rfind('/');
    if (b == std::string::npos) return false;
    std::string dir = p.substr(0, b), stem = p.substr(b + 1, p.size() - b - 1 - 4);
    std::string mtb, vbuf, ibuf;
    if (!read10s(dir + "/" + stem + ".mtb", mtb)) return false;     // nothing to render
    read10s(dir + "/" + stem + "_0.vbuf", vbuf);
    read10s(dir + "/" + stem + "_0.ibuf", ibuf);
    if (g_cfg.skin10) note += npcrt::skin10(f, vbuf, ibuf);
    di1::WorldRender w = di1::worldRenderOf(g_cfg);
    w.vf10 = &g_vf10;
    w.vf30 = &g_vf30;
    w.tails10 = &g_tails10;
    render::IO io = di1::worldIO(w, read10s, read30x);
    io.log = [](const std::string& s) { logf("%s", s.c_str()); };
    io.translatable = [](const std::string& sh) { return engine::shaders::make(("x" + sh).c_str()) != nullptr; };
    sceneSettingsIO(io);
    render::Result r;
    if (!render::transcodeWorld(g_render, io, f, {stem, mtb, vbuf, ibuf}, r)) {
        for (auto& pr : r.problems) logf("render problem %s: %s", p.c_str(), pr.c_str());
        note += "render FAILED ";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_sessLock);
        g_served[dir + "/" + stem + ".mtb"] = r.mtb;
        if (!r.vbuf.empty()) g_served[dir + "/" + stem + "_0.vbuf"] = r.vbuf;
        if (!r.ibuf.empty()) g_served[dir + "/" + stem + "_0.ibuf"] = r.ibuf;
        if (!r.ibuf2.empty()) g_served[dir + "/" + stem + "_2.ibuf"] = r.ibuf2;
    }
    // at most 179 characters (60 fixed, five numbers, both tails): never cut
    char buf[200];
    snprintf(buf, sizeof(buf), "+render(translated %d real %d same-sh %d generic %d, %u 1.0 textures%s%s) ",
             r.tiers["translated"], r.tiers["real"], r.tiers["same-sh"], r.tiers["generic"],
             (unsigned)r.textures10.size(),
             r.skinned ? (", " + std::to_string(r.skinned) + " skinned in skin layout").c_str() : "",
             r.sceneSettings ? (", " + std::to_string(r.sceneSettings) + " scene settings").c_str() : "");
    note += buf;
    return true;
}

// clip10motions: is a clip template's folder ('characters/kiosk' of 'characters/kiosk/kiosk.ct') in the 3.0 install?
static bool has30(std::string dir)
{
    std::string a = g_gameDir + "\\assets\\" + dir;
    for (char& c : a) if (c == '/') c = '\\';
    if (GetFileAttributesA(a.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return GetFileAttributesA((a + ".zip").c_str()) != INVALID_FILE_ATTRIBUTES;
}

// gp: the engine's surface table, index order (FUN_00754eb0 fills *(0x2204830): +0xc {char*, bool}[], +0x10 count);
// empty until startup2 loads.
static std::vector<std::string> surfaceTable30()
{
    std::vector<std::string> t;
    uintptr_t obj = *(const uintptr_t*)rt(0x02204830);
    if (!sys::readable((const void*)obj, 0x14)) return t;
    uintptr_t arr = *(const uintptr_t*)(obj + 0xc);
    uint32_t n = *(const uint32_t*)(obj + 0x10);
    if (n > 1024 || !sys::readable((const void*)arr, 8 * (size_t)n)) return t;
    for (uint32_t i = 0; i < n; ++i) {
        char s[64];
        safeCopy(s, sizeof(s), *(const char* const*)(arr + 8 * i));
        t.push_back(s);
    }
    return t;
}

// The plugin's .oct steps: its cfg, its render, the 3.0 install and the engine's surface table
static di1::OctSteps octSteps10()
{
    di1::OctSteps o = di1::octStepsOf(g_cfg);
    o.render = transcodeAssetRender;
    if (g_cfg.clip10Motions) o.installed30 = has30;
    if (g_cfg.surfaces10) o.surfaceTable = surfaceTable30;
    return o;
}

// vfxchan10: a particle package's shaders; its served .mtb is kept for the engine's request right after (same frame).
static di1::Shaders particleShaders(const std::string& pd, std::string& note)
{
    if (!g_renderOk) return {};
    std::string mp = pd.substr(0, pd.size() - strlen(".particledata")) + ".mtb", over;
    // the .mtb beside it, or the overlay member the alias names
    {
        std::lock_guard<std::mutex> lock(g_sessLock);
        auto o = g_over10.find(mp);
        if (o != g_over10.end()) over = o->second;
    }
    std::string mtb;
    if (!read10s(over.empty() ? mp : over, mtb)) return {};
    di1::ParticleBundle b = di1::particleBundle(g_render, di1::bundleIO(read10s, read30x), mp, mtb, note);
    if (!b.ok) return {};
    std::lock_guard<std::mutex> lock(g_sessLock);
    g_served.emplace(mp, std::move(b.mtb));
    return b.shaders;
}

// currency10: does a 1.0 expendable's DNA ('FastCoin_Spark', 'X.dnax', a path) carry FastCoin.IsSpark? Memoised.
static bool isSpark10(const std::string& dnaFile)
{
    static std::mutex m;
    static std::map<std::string, bool> memo;
    std::string p = di1::dnaPath(dnaFile);
    if (p.empty()) return false;
    {
        std::lock_guard<std::mutex> l(m);
        if (auto it = memo.find(p); it != memo.end()) return it->second;
    }
    bool spark = false;
    std::string b;
    if (read10s(p, b)) spark = di1::paysSparks(b);
    std::lock_guard<std::mutex> l(m);
    return memo[p] = spark;
}

static di1::TranscodeOptions transcodeOptions10() { return di1::transcodeOptionsOf(g_cfg); }

static void keepBundle(const std::string& path, const std::string& bytes)
{
    std::lock_guard<std::mutex> lock(g_sessLock);
    g_served[path] = bytes;
}

// A DataMap entry, before it is normalised: Cho aliases (choalias10) and the playset's GameDB tables (sys.hpp)
static void dataMapEntry10(const std::string& s)
{
    if (g_cfg.choAlias10) g_choAlias.add(s);
    sys::dbTrigger10Add(s);   // the playset's GameDB tables: the locks / bitMask supplements follow them
}

// The plugin's readers, tables and session for the transcode dispatch
static di1::TranscodeIO transcodeIO10()
{
    di1::TranscodeIO io;
    io.read10 = read10s;
    io.read30x = read30x;
    io.read30 = read30;
    io.oct = octSteps10();
    io.mer = g_merOk ? &g_mer : nullptr;
    io.render = g_renderOk ? &g_render : nullptr;
    io.lang30 = lang30;
    io.keep = keepBundle;
    io.renderProblem = [](const std::string& p, const std::string& pr) {
        logf("render problem %s: %s", p.c_str(), pr.c_str());
    };
    if (g_cfg.vfxChan10) io.particleShaders = particleShaders;
    if (g_cfg.currency10) io.isSpark = isSpark10;
    io.dataMapEntry = dataMapEntry10;
    return io;
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
static std::string mergeVtxf(const std::string& t30, const std::string& t10);

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
        std::string m = mergeVtxf(t30, t10);
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

// A world session product (kept: the engine reopens files), else a full-path member of a loaded 1.0 archive.
static void sessionLookup(const std::string& np, std::string& hit, std::string& over)
{
    std::lock_guard<std::mutex> lock(g_sessLock);
    auto it = g_served.find(np);
    if (it != g_served.end()) {
        hit = it->second;
        return;
    }
    auto o = g_over10.find(np);
    if (o != g_over10.end()) over = o->second;
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
    std::lock_guard<std::mutex> lock(g_sessLock);
    const bool scoped = !g_movies10.count(movie);
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

// vo10: a 1.0 dialog line's lipsync (audio.hpp annoSource), read from its zip under g_zipLock.
static void* read10Lipsync(const char* p, size_t* n, std::string* where)
{
    std::string annoZip, annoMember;
    const bool vo = g_cfg.audio10 && g_cfg.vo10;
    if (!vo || !audio::annoSource(p, annoZip, annoMember)) return nullptr;
    std::lock_guard<std::mutex> lock(g_zipLock);
    mz_zip_archive* z = zipFor(annoZip);
    int i = z ? mz_zip_reader_locate_file(z, annoMember.c_str(), nullptr, 0) : -1;
    if (i < 0) return nullptr;
    void* data = mz_zip_reader_extract_to_heap(z, (mz_uint)i, n, 0);
    if (data) *where = annoZip + ":" + annoMember;
    return data;
}

// The 1.0 read of a missing file: overlay member, ChallengeData redirect, stand-ins, lipsync. Sets the miss reason.
static Read10 read10For(Open& o, const std::string& np, const std::string& over)
{
    double tR = o.prof ? nowMs() : 0;
    Read10 rd;
    // the count includes the candidate names that were not there (A-12)
    o.missWhy = over.empty() ? " (not in the 1.0 archives, " + std::to_string(g_zips.size()) + " archives open)"
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
                di1::transcode({o.p, rd.data, rd.n}, {t.out, t.note, &mtc}, transcodeOptions10(), transcodeIO10());
        }
        // the .mtc request transcodes read10s(<same path>.mer): these bytes when nothing redirected the read
        const bool mer = t.codec && !strcmp(t.codec, "mer") && !mtc.empty();
        if (mer && over.empty() && rd.chal.empty()) {
            std::lock_guard<std::mutex> lock(g_sessLock);
            g_served[np.substr(0, np.size() - kMerSuffix) + ".mtc"] = mtc;
        }
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
    if (!mv.empty()) {
        std::lock_guard<std::mutex> lock(g_sessLock);
        g_movies10.insert(mv);
    }
    if (np.rfind("presentation/", 0) != 0 || !endsWith(p, ".lua")) return;
    std::lock_guard<std::mutex> lock(g_sessLock);
    g_screens10.insert(np.substr(kPresentationDir, np.size() - kPresentationDir - kLuaSuffix));
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
    indexZip10(rd.where.substr(0, rd.where.find(':')));
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
    sessionLookup(np, hit, over);
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


// ---- real 1.0 shaders at runtime (engine/shaders.hpp makes them, engine/shaderlinks.hpp links them) ----
constexpr size_t kShaderHashName = 33;   // 'x' and a hash-named 1.0 shader's name, as the world transcode writes it

// 'x' names are 1.0 shaders: hash names are made on demand, others only found (an unmade name hangs the loader).
static uint8_t* shader10(const char* name)
{
    if (!name || name[0] != 'x') return nullptr;
    return strlen(name) == kShaderHashName ? engine::shaders::make(name) : engine::shaders::find(name);
}

// The 1.0 shader after name's 'x' as a 3.0 one (gen30: render table V/W/Q lines); false when it has problems.
static bool generateShader10(const char* name, const engine::shaders::Compile& compile, engine::shaders::Generated& out)
{
    gen30::Generated g;
    std::function<uint32_t(uint32_t)> skinTarget;
    if (g_cfg.skinTr10 && g_cfg.skinLayout10 && !g_vf30.empty())
        skinTarget = [](uint32_t vf) { return render::skinTarget30(g_render, vf); };
    gen30::generate(g_render.shaderRules, {read10s, g_vf10, compile, skinTarget, &g_vf30}, std::string(name + 1), g);
    for (auto& pr : g.problems) logf("shaders10 %s: %s", name, pr.c_str());
    if (g.shd.empty() || !g.problems.empty()) return false;
    out.shd = std::move(g.shd);
    for (gen30::Program& p : g.programs) out.programs.push_back({p.id, p.ps, std::move(p.code)});
    return true;
}

// shaders10: 1.0's generator and test views, then the linking hooks with 1.0's shader names.
static bool installShaders(const di1::Config& c, const engine::shaders::Compiler& compiler)
{
    if (!c.shaders10) return true;
    engine::shaders::Config made;
    made.compiler = compiler;
    made.bufView = c.bufview10;
    made.magenta = c.magenta10;
    made.generate = generateShader10;
    engine::shaders::install(made);
    engine::shaderlinks::Config links;
    links.lazyLink = c.lazyLink10;
    links.fadeProbe = c.fadeProbe10 != 0;
    links.gameShader = shader10;
    return engine::shaderlinks::install(links);
}

// vformat.vtxf: 3.0's formats plus the 1.0 formats 3.0 lacks (vtxf::merge). Read at boot, before any transcode.
static std::string mergeVtxf(const std::string& t30, const std::string& t10)
{
    int n = 0;
    std::string m = vtxf::merge(t30, t10, n, g_tails10);
    g_vf30 = vtxf::formats(t30);
    logf("shaders10: vformat.vtxf + %d 1.0 formats, %u with their vertex colour in its own stream", n,
         (unsigned)g_tails10.size());
    return m;
}

#include "vfxrt.hpp"
#include "gprt.hpp"
#include "bldrt.hpp"
#include "caprt.hpp"

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

// resident10: 1.0 loads assets\startup*.zip resident, so their members are found by full path.
static void indexResident10(const di1::Config& c)
{
    if (!c.serve10 || !c.resident10 || !c.install10[0]) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(c.install10) + "\\assets\\startup*.zip").c_str(), &fd);
    for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileA(h, &fd)) indexZip10(fd.cFileName);
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    logf("resident10: %u full-path members of the 1.0 startup archives indexed", (unsigned)g_over10.size());
}

// A file in the plugin's folder, read whole; "" when it cannot be opened.
static std::string slurpFile(const char* dir, const char* name)
{
    std::string text;
    FILE* tf = nullptr;
    if (fopen_s(&tf, (std::string(dir) + "\\" + name).c_str(), "rb") || !tf) return text;
    char buf[65536];
    for (size_t got = fread(buf, 1, sizeof(buf), tf); got > 0; got = fread(buf, 1, sizeof(buf), tf))
        text.append(buf, got);
    fclose(tf);
    return text;
}

// The render and Meridian tables beside the DLL.
static void loadTables(const di1::Config& c, const char* dir)
{
    g_renderOk = g_render.load(slurpFile(dir, "di1compat_render.txt"));
    logf("render table: %s (%u shaders, %u donors, %u materials, %u vertex plans)", g_renderOk ? "ok" : "MISSING",
         (unsigned)g_render.shaders.size(), (unsigned)g_render.donors.size(), (unsigned)g_render.matindex.size(),
         (unsigned)g_render.plans.size());
    g_merOk = g_mer.load(slurpFile(dir, "di1compat_mer.txt"));
    g_mer.events10 = c.events10;
    g_mer.portRefs = c.portRefs10;
    g_mer.ports10 = c.ports10;
    g_mer.pins10 = c.pins10;
    g_mer.gate10 = c.gate10;
    g_mer.toggleType10 = c.toggleType10;
    logf("meridian table: %s (%u registered classes, %u described)", g_merOk ? "ok" : "MISSING",
         (unsigned)g_mer.registered.size(), (unsigned)g_mer.classes.size());
}

// shaders10: 1.0's vertex formats, and the HLSL compiler and DXBC cache folder it returns, before any hook.
static engine::shaders::Compiler prepareShaders10(const di1::Config& c, const char* dir)
{
    engine::shaders::Compiler out;
    if (!c.shaders10) return out;
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    out.compile = dc ? (engine::shaders::D3DCompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
    out.cacheDir = std::string(dir) + "\\di1compat_shaders";
    CreateDirectoryA(out.cacheDir.c_str(), nullptr);
    std::string v10;
    if (read10s("shaders/vformat.vtxf", v10)) g_vf10 = vtxf::formats(v10);
    logf("shaders10: compiler %s, %u 1.0 vertex formats, %u 3.0 globals, cache %s", out.compile ? "ok" : "MISSING",
         (unsigned)g_vf10.size(), (unsigned)g_render.shaderRules.globals.size(), out.cacheDir.c_str());
    return out;
}

constexpr int kOpenFiles = 8192;   // every 1.0 archive used stays open (miniz holds a FILE*); the CRT default is 512

// Before any hook, in this order: RenderDoc, file limit, resident archives, tables, shaders10, playsets.
// Returns shaders10's compiler.
static engine::shaders::Compiler prepare(const di1::Config& c, const char* dir)
{
    trace::renderdoc::install({c.renderdoc10, g_gameDir + "\\di3cap"});
    _setmaxstdio(kOpenFiles);
    indexResident10(c);
    loadTables(c, dir);
    engine::shaders::Compiler compiler = prepareShaders10(c, dir);
    di1::playsets::install(c);
    return compiler;
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
    const engine::shaders::Compiler compiler = prepare(g_cfg, dir);

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
    installShaders(g_cfg, compiler);
    trace::mission::install(missionTraceOf(g_cfg));
    installChoStart(g_cfg);
    trace::breaktrace::install({g_cfg.breakTrace});
    trace::teardown::install({g_cfg.teardownTrace});
    di1::meridian::install({g_cfg.aliases10, g_cfg.native10, g_merOk ? &g_mer : nullptr});
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
