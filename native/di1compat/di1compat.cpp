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
#include "core/stack.hpp"
#include "core/veh.hpp"
#include "core/zip.hpp"
#include "games/di1/archives.hpp"
#include "games/di1/config.hpp"
#include "games/di1/octsteps.hpp"
#include "games/di1/options.hpp"
#include "games/di1/renderio.hpp"
#include "games/di1/transcode.hpp"
#include "games/di1/zones.hpp"
#include "trace/breaktrace.hpp"
#include "trace/fade.hpp"
#include "trace/loadprof.hpp"
#include "trace/locktrace.hpp"
#include "trace/mission.hpp"
#include "trace/opacity.hpp"
#include "trace/spawn.hpp"
#include "trace/spawntrace.hpp"
#include "trace/teardown.hpp"
#include "octx.hpp"
#include "bent.hpp"
#include "btd.hpp"
#include "chd.hpp"
#include "mer.hpp"
#include "mernode.hpp"
#include "render.hpp"
#include "arc30.hpp"
#include "datamap.hpp"
#include "dct.hpp"
#include "luachunk.hpp"
#include "sx.hpp"
#include "sys.hpp"
#include "npc.hpp"
#include "cho.hpp"
#include "ui.hpp"
#include "audio.hpp"
#include "vfx.hpp"
#include "chan.hpp"
#include "gameplay.hpp"
#include "third_party/miniz/miniz.h"
#include "renderdoc_app.h"
#include <thread>

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
using hash::fnv64;
using text::endsWith;
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

typedef int*(__cdecl* OpenFn)(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);
static OpenFn g_open;

// A 3.0 asset through the engine's own open (archives, keys, index), read whole; the stream is destroyed.
static bool read30(const std::string& path, std::string& out)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    int* s = g_open(path.c_str(), 0, 0, 0, nullptr);
    if (!s) return false;
    void** vt = *(void***)s;
    uint32_t n = ((SizeFn)vt[4])(s, nullptr);
    out.assign(n, '\0');
    uint32_t got = n ? ((ReadFn)vt[5])(s, nullptr, &out[0], n) : 0;
    ((DtorFn)vt[11])(s, nullptr, 1);
    return got == n;
}

// A 3.0 asset by its extract path ('3:X/name/member' = member of <game>/assets/X/name.zip, the extract.py layout),
// read straight from the install's archive (arc30.hpp) -- the engine's own path conventions vary by asset kind.
static std::string g_gameDir;
static bool read30x(const std::string& extract, std::string& out)
{
    std::string rel = extract.substr(2);
    size_t cut = rel.rfind('/');
    if (cut == std::string::npos) return false;
    std::string arc = g_gameDir + "/assets/" + rel.substr(0, cut) + ".zip";
    for (char& c : arc) if (c == '/') c = '\\';
    return arc30::readMember(arc, rel.substr(cut + 1), out);
}

// The key hashes of every 3.0 table of language `lang` (english_ntsc ...: _win32 and _pc_con of each package under
// <game>\assets\lang), read from the archives once (text10: 3.0's keys are never shadowed).
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

// World session: the .oct request transcodes the whole world (its .mtb/.vbuf/.ibuf decisions are coupled), and the
// engine's next requests for those files are answered from here.
// 1.0 archive overlay: a 1.0 asset archive also carries files under their full asset paths -- textures/<hash>.tbody,
// and the motions its entity uses (characters/inc_bob/inc_bob_ps.zip holds characters/biped/motions/*.oct and .banm,
// which no characters/biped archive has: in-game 2026-10-12 the transcoded inc_bob_ps.bent named them and they were
// missing). The 1.0 engine sees them once the archive is loaded, so every 1.0 archive a file is served from gets its
// full-path members indexed (engine path -> "<archive without .zip>/<member>", which read10 resolves); first wins.
static std::mutex g_sessLock;
static std::map<std::string, std::string> g_served;
static std::map<std::string, std::string> g_over10;
static std::set<std::string> g_indexed10;
static std::set<std::string> g_movies10;    // flash/<dir>/<movie> served from 1.0 (ui: their images may be too)
static std::set<std::string> g_screens10;   // presentation/<screen>.lua served from 1.0: lower-case screen names (gprt)
static render::Table g_render;
static gen30::VFormats g_vf10;              // 1.0 vertex formats (real 1.0 shaders: shaders10)
static gen30::VFormats g_vf30;              // 3.0's (skintr10: the formats skinned passes draw in), set with the vtxf
static std::map<uint32_t, gen30::ColourTail> g_tails10;   // re-laid 1.0 formats in the served vformat.vtxf
static uint8_t* shader10(const char* name, bool create);
static bool g_renderOk;

// Both games' scenesettingsformat.ssf (render.hpp ssfBlock30), parsed on first use; both or neither (cfg scenesettings10)
static void sceneSettingsIO(render::IO& io)
{
    static std::mutex mx;
    static bool done, ok;
    static render::Ssf f10, f30;
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
        if (g_cfg.vfx10) if (std::string a = vfx::p3dAlias(n); !a.empty()) g_over10.emplace(a, base + name);   // 3.0 opens meshes by hash
    }
}

// D3D11 rejects block-compressed textures below 4x4; a 1x1/2x2 single-mip DXT file already holds one whole
// 4x4 block, so only the header size changes (conv.copy_texture).
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

// The render half of a 1.0 asset with a material bundle -- a world, a mission graph, ... (render.hpp): materials
// onto 3.0 shaders and, where the asset has buffers, its vertices into formats those shaders take.
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
    io.translatable = [](const std::string& sh) { return shader10(("x" + sh).c_str(), true) != nullptr; };
    sceneSettingsIO(io);
    render::Result r;
    if (!render::transcodeWorld(g_render, io, f, stem, mtb, vbuf, ibuf, r)) {
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
    char buf[200];
    sprintf_s(buf, "+render(translated %d real %d same-sh %d generic %d, %u 1.0 textures%s%s) ", r.tiers["translated"], r.tiers["real"],
              r.tiers["same-sh"], r.tiers["generic"], (unsigned)r.textures10.size(),
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
    return GetFileAttributesA(a.c_str()) != INVALID_FILE_ATTRIBUTES || GetFileAttributesA((a + ".zip").c_str()) != INVALID_FILE_ATTRIBUTES;
}

// gp: the engine's global surface table, in index order (FUN_00754eb0 fills *(0x2204830): +0xc {char*, bool}[],
// +0x10 count; loaded from startup2 at boot, before any world). Empty until then.
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

static bool memtestMatch(const char* p)
{
    char list[600];
    strcpy_s(list, g_cfg.memtest);
    char* ctx = nullptr;
    for (char* t = strtok_s(list, ",", &ctx); t; t = strtok_s(nullptr, ",", &ctx))
        if (*t && strstr(p, t)) return true;
    return false;
}

// GameDB CHD-only callback loaders (DBFileLoader_Callback_CHD::load, FUN_018db450 -- RumpusData, FUN_00f63e70) given
// a 1.0 .lua table: the file opened during that call is the table compiled to CHD (chd.hpp), not its Lua text
// (which the CHD parser reads as a wild pointer: in-game 2026-10-12 AV in FUN_01824810 after rumpusdata.lua).
// thiscall, three stack arguments (ret 0xc), as the dispatcher FUN_018da710 calls vtable[1]
typedef uint8_t(__fastcall* ChdLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);
static ChdLoadFn g_loadChdCallback;
// the path whose next open compiles to CHD; process-wide because the engine opens files on other threads
static std::mutex g_chdLock;
static std::string g_chdPath;
static bool takeChdPath(const char* p)
{
    std::lock_guard<std::mutex> lock(g_chdLock);
    return !g_chdPath.empty() && norm(p) == g_chdPath;
}
static uint8_t __fastcall hkLoadChdCallback(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (!endsWith(p, ".lua")) return g_loadChdCallback(self, nullptr, path, a2, a3);
    { std::lock_guard<std::mutex> lock(g_chdLock); g_chdPath = norm(p); }
    uint8_t r = g_loadChdCallback(self, nullptr, path, a2, a3);
    { std::lock_guard<std::mutex> lock(g_chdLock); g_chdPath.clear(); }
    logf("gamedb CHD callback loader given %s -> %u", p, r);
    return r;
}

// The PC virtual reader's remembered character per playset (FUN_00b7f910(key "IGP_PLAYSET_<playset>", sku), thiscall,
// ret 8) has slots for 3.0's four playsets only (Empire, InsideOut, TheCloneWars, PlaysetX) and is false for any other;
// FUN_00b83b90 then resets the current character to none every frame on such a playset world (watchpoint on the
// reader tag's toy id, 2026-10-12: written to -1 from there during the IN_City load), the playset flow finds no figure,
// keeps the proxy avatar and shows "Missing Disney Infinity Figure". A playset without a slot keeps the selected
// character; whether that figure may play here stays the playset flow's own check (Gateway[sku].playset, FUN_00ddae30).
typedef uint8_t(__fastcall* PlaysetCharFn)(void* self, void* edx, const char* key, int sku);
static PlaysetCharFn g_playsetChar;
static uint8_t __fastcall hkPlaysetChar(void* self, void*, const char* key, int sku)
{
    static const char* const slots[] = {"IGP_PLAYSET_Empire", "IGP_PLAYSET_InsideOut", "IGP_PLAYSET_TheCloneWars", "IGP_PLAYSET_PlaysetX"};
    char k[100];
    safeCopy(k, sizeof(k), key);
    for (auto s : slots)
        if (!_stricmp(k, s)) return g_playsetChar(self, nullptr, key, sku);
    return 1;
}

// The playset slot's hook (H1): always on, it has no cfg key (B-20).
static bool installPlaysetSlot(const di1::Config&)
{
    return hook("VR playset character slot", 0x00B7F910, "6AFF68????????64A100000000", (void*)&hkPlaysetChar,
                (void**)&g_playsetChar);
}

// FUN_01449390, thiscall on a choreography instance (+0x38 its path, +0x1b4 flags: 0x20 audio sync): its start.
typedef void(__fastcall* BtStartFn)(uint8_t* self, void* edx);
static BtStartFn g_choStart;
static void __fastcall hkChoStart(uint8_t* self, void*)
{
    char name[100] = "";
    safeCopy(name, sizeof(name), *(const char**)(self + 0x38));
    uint32_t flags = *(uint32_t*)(self + 0x1b4);
    if (g_cfg.breakTrace) logf("breaktrace cho start %p '%s' flags %08X", self, name, flags);
    g_choStart(self, nullptr);
    if (g_cfg.audio10 && g_cfg.cineSync10) audio::choreographyStarted(name, flags);
}

// The choreography start hook (H1): cinesync10 starts 1.0's audio-sync sound event (audio.hpp); breaktrace logs.
static bool installChoStart(const di1::Config& c)
{
    const bool cineSync = c.audio10 && c.cineSync10;
    if (!c.breakTrace && !cineSync) return true;
    return hook("choreography start (cinesync10 / breaktrace)", 0x01449390, "83EC18568BF1E8", (void*)&hkChoStart,
                (void**)&g_choStart);
}
// Asset defs lookup FUN_00703340(key) (thiscall on the AssetDefsManager, ret 4): DefID -> FileName of a Cho, "" when
// no loaded assetdefs has it. A 3.0 playset's gamedb package carries assetdefs.lua (30 engine entries every playset
// ships identically, GamePlayers_ToyCreation_Fast among them, plus character entries) and the GameDB loader parses it
// when the playset loads; the Incredibles package is loaded by the older fixed-list path that never opens it, so
// AvatarSpawning got no creation cho, skipped its ToyCreation_PlaceToy setup and never finalized the avatar
// (in-game 2026-10-13). On a miss the lookup answers from gamedb/incredibles/assetdefs.lua (3.0 entries, added to that
// package), read once through the engine's own open.
typedef const char*(__fastcall* AssetDefFn)(void* self, void* edx, const char* key);
static AssetDefFn g_assetDef;
static std::map<std::string, std::string>* g_defs;   // lowercase DefID -> FileName (Cho entries)
static std::mutex g_defsMx;
static bool read30(const std::string& path, std::string& out);
static const char* fallbackDef(const char* key)
{
    std::lock_guard<std::mutex> l(g_defsMx);
    if (!g_defs) {
        g_defs = new std::map<std::string, std::string>;
        std::string t;
        if (read30("gamedb/incredibles/assetdefs.lua", t)) {
            for (size_t a = t.find('{', t.find("AssetDefs")); a != std::string::npos; a = t.find('{', a + 1)) {
                size_t b = t.find('}', a);
                if (b == std::string::npos) break;
                std::string blk = t.substr(a, b - a);
                auto field = [&](const char* n) {
                    size_t i = blk.find(n);
                    if (i == std::string::npos) return std::string();
                    size_t q = blk.find('"', i), e = q == std::string::npos ? q : blk.find('"', q + 1);
                    return e == std::string::npos ? std::string() : blk.substr(q + 1, e - q - 1);
                };
                std::string id = field("DefID"), file = field("FileName");
                if (field("AssetType") != "Cho" || id.empty()) continue;
                for (char& c : id) c = (char)tolower((unsigned char)c);
                (*g_defs)[id] = file;
            }
        }
        logf("asset defs fallback: %u Cho entries from gamedb/incredibles/assetdefs.lua", (unsigned)g_defs->size());
    }
    std::string k = key ? key : "";
    for (char& c : k) c = (char)tolower((unsigned char)c);
    auto it = g_defs->find(k);
    return it == g_defs->end() ? nullptr : it->second.c_str();
}
static volatile LONG g_assetDefLogged;
static const char* __fastcall hkAssetDef(void* self, void*, const char* key)
{
    const char* r = g_assetDef(self, nullptr, key);
    if (r && !*r) {
        const char* f = nullptr;
        __try { f = fallbackDef(key); } __except (EXCEPTION_EXECUTE_HANDLER) { f = nullptr; }
        if (f) {
            if (InterlockedIncrement(&g_assetDefLogged) <= 20) { char k[64] = ""; safeCopy(k, sizeof(k), key); logf("assetdef '%s' -> '%s' (fallback)", k, f); }
            return f;
        }
    }
    return r;
}

// The asset defs fallback's hook (H1): always on.
static bool installAssetDefs(const di1::Config&)
{
    return hook("asset defs lookup", 0x00703340, "8B44240456", (void*)&hkAssetDef, (void**)&g_assetDef);
}

// PS-1 (zones10): the 1.0 playsets the 3.0 zone list holds once it has loaded (gameplay::playsets10; added here unless
// zones10 is off), for the Play Sets menu, and their zones' name prefixes ("IN_", "MU_", "PC_" ...: the 1.0 missions
// carry them) for missiontrace.
static std::vector<gameplay::Playset10> g_playsets10;
static std::vector<std::string> g_missionPrefix10;
static std::mutex g_playsets10Lock;
// currency10: inventoryFilter -> the 1.0 playset's currency (gameplay::currencies10). 3.0 answers INV_SPARKS in three
// places, all redirected to it:
// - "currency of the zone's inventory filter" FUN_00e21850 (thiscall, filter, CString* out; ret 8): every filter. Its
//   callers pass the zone's filter: the coin cache that banks Spark-type pickups (FUN_00e27ba0 / FUN_00e27c90),
//   currency drops, legacy reward groups.
// - UI_GetPlaysetCurrency FUN_00b54b50 (thiscall(L), ret 4): the Toy Store's currencyName (balance, price icon;
//   catalog.lua) and player_hud_info.lua's. It reads the zone's filter, then asks FUN_00e21850 for LVL_RUMPUSROOM's;
//   1.0's FUN_00b40a40 asked with the zone's filter, as this does again (lua_pushstring FUN_0130d840).
// - the HUD's currency FUN_00fbb420 (below). Prices need nothing: a 1.0 recipe card's PelletCost names its currency
//   ('INV_NSA_BUCKS:1000') and catalog.lua charges that (UI_ChangePelletCount).
static std::map<std::string, std::string> g_currency10;
static std::mutex g_currency10Lock;
static std::string currency10(const char* filter, const char* who)
{
    if (!filter || !*filter) return {};
    std::lock_guard<std::mutex> l(g_currency10Lock);
    auto it = g_currency10.find(filter);
    if (it == g_currency10.end()) return {};
    static core::LogOnce<std::string> told;
    if (told.first(std::string(who) + filter))
        logf("currency10: %s currency for %s -> %s", who, filter, it->second.c_str());
    return it->second;
}
// The zone's: the zone manager's (*0x0225f548) string field inventoryFilter of its zone (+0x19c), read through
// FUN_0139e0b0 (thiscall: key, zone, a3, default; ret 0x10) as FUN_00e27ba0 and FUN_00b54b50 read it.
static std::string zoneCurrency10(const char* who)
{
    uint8_t* zm = *(uint8_t**)rt(0x0225F548);
    uint32_t zone = zm ? *(uint32_t*)(zm + 0x19C) : 0;
    if (!zone) return {};
    typedef const char*(__fastcall* StrFn)(void*, void*, const char*, uint32_t, uint32_t, const char*);
    return currency10(((StrFn)rt(0x0139E0B0))(zm, nullptr, "inventoryFilter", zone, 0, ""), who);
}
typedef void(__fastcall* ZoneCurrencyFn)(void* self, void* edx, const char* filter, void* out);
static ZoneCurrencyFn g_zoneCurrency;
static void __fastcall hkZoneCurrency(void* self, void*, const char* filter, void* out)
{
    std::string cur = currency10(filter, "zone");
    if (cur.empty()) return g_zoneCurrency(self, nullptr, filter, out);
    ((void(__fastcall*)(void*, void*, const char*))rt(0x0041A250))(out, nullptr, cur.c_str());   // CString = cur
}
typedef int(__fastcall* PlaysetCurrencyFn)(void* self, void* edx, void* L);
static PlaysetCurrencyFn g_playsetCurrency;
static int __fastcall hkPlaysetCurrency(void* self, void*, void* L)
{
    std::string cur = zoneCurrency10("Toy Store / UI_GetPlaysetCurrency");
    if (cur.empty()) return g_playsetCurrency(self, nullptr, L);
    ((void(__cdecl*)(void*, const char*))rt(0x0130D840))(L, cur.c_str());
    return 1;
}
// The HUD's currency FUN_00fbb420 (thiscall(data), ret 4; FUN_00fc11b0's pick unless the HUD's playset is Empire,
// InsideOut or PlaysetX, which have their own) shows INV_SPARKS with icon "SPARKS", both fixed. In a 1.0 playset's
// zone this shows the playset's currency through the same steps: amount FUN_00e07f60 (inventory *0x02289734,
// thiscall(name), ret 4); into the hud's (+0x5c) table PlayerInfo (FUN_0154c520) when its currencyAmt (FUN_004510b0,
// lua_tointeger FUN_0130d530, release FUN_0130e4d0) differs or the hud forces it (+0x87): currencyIcon (FUN_00b3f0a0)
// = the currency less "INV_" (the icon naming of 3.0's empire_currency / scrapcurrency / out_balloons, and of
// catalog.lua's MangleIconName: ui/icons/nsa_bucks.mtb ...), dirty (FUN_00b3f060), PlayerCurrencyAmt (FUN_0154c100).
struct ScriptRef { void* L; uint32_t idx; };
typedef uint8_t(__fastcall* HudCurrencyFn)(uint8_t* hud, void* edx, void* data);
static HudCurrencyFn g_hudCurrency;
static uint8_t __fastcall hkHudCurrency(uint8_t* hud, void*, void* data)
{
    std::string cur = zoneCurrency10("HUD");
    if (cur.empty() || cur == "INV_SPARKS") return g_hudCurrency(hud, nullptr, data);
    typedef void(__cdecl* ReleaseFn)(void*, uint32_t);
    int amt = ((int(__fastcall*)(void*, void*, const char*))rt(0x00E07F60))(*(void**)rt(0x02289734), nullptr, cur.c_str());
    ScriptRef info{nullptr, 0xFFE17B81u}, f{nullptr, 0};
    ((void(__fastcall*)(void*, void*, const char*, ScriptRef*))rt(0x0154C520))(*(void**)(hud + 0x5C), nullptr, "PlayerInfo", &info);
    ScriptRef* got = ((ScriptRef*(__fastcall*)(ScriptRef*, void*, ScriptRef*, const char*))rt(0x004510B0))(&info, nullptr, &f, "currencyAmt");
    int shown = ((int(__cdecl*)(void*, uint32_t))rt(0x0130D530))(got->L, got->idx);
    if (f.L) ((ReleaseFn)rt(0x0130E4D0))(f.L, f.idx);
    uint8_t changed = 0;
    if (shown != amt || hud[0x87]) {
        std::string icon = cur.compare(0, 4, "INV_") ? cur : cur.substr(4);
        const char* key = "currencyIcon";
        const char* val = icon.c_str();
        ((void(__fastcall*)(ScriptRef*, void*, const char**, const char**))rt(0x00B3F0A0))(&info, nullptr, &key, &val);
        changed = 1;
        key = "dirty";
        ((void(__fastcall*)(ScriptRef*, void*, const char**, uint8_t*))rt(0x00B3F060))(&info, nullptr, &key, &changed);
        ((void(__fastcall*)(void*, void*, const char*, int))rt(0x0154C100))(data, nullptr, "PlayerCurrencyAmt", amt);
    }
    if (info.L) ((ReleaseFn)rt(0x0130E4D0))(info.L, info.idx);
    return changed;
}

// currency10's hooks (H1), all or none.
static bool installCurrency(const di1::Config& c)
{
    if (!c.currency10) return true;
    return installFeature("currency10", {
        {"zone currency (currency10)", 0x00E21850, "8B4C240868????????E8", (void*)&hkZoneCurrency,
         (void**)&g_zoneCurrency},
        {"UI_GetPlaysetCurrency (currency10)", 0x00B54B50, "6AFF68????????64A100000000", (void*)&hkPlaysetCurrency,
         (void**)&g_playsetCurrency},
        {"HUD currency (currency10)", 0x00FBB420, "6AFF68????????64A100000000", (void*)&hkHudCurrency,
         (void**)&g_hudCurrency},
    });
}
static bool world10(const std::string& zone) { return di1::worldInstalled(g_cfg.install10, zone); }
static int zones10(std::string& t, const char* p)
{
    if (norm(p) != "gamedb/core/zonelist.lua") return 0;
    std::string c10;
    if (!read10s("gamedb/core/zonelist.lua", c10)) { logf("zones10: no 1.0 zone list in the 1.0 install"); return 0; }
    di1::Zones10Options o;
    o.add = g_cfg.zones10;
    o.currencies = g_cfg.currency10;
    o.world = world10;
    di1::Zones10 z;
    try {
        z = di1::zones10(t, c10, o);
        if (g_cfg.currency10) {
            std::string l;
            for (auto& [f, c] : z.currencies) l += " " + f + "=" + c;
            logf("currency10: 1.0 playset currencies:%s", l.c_str());
            std::lock_guard<std::mutex> lk(g_currency10Lock);
            g_currency10 = std::move(z.currencies);
        }
    } catch (std::exception& e) {
        logf("zones10: 1.0 zone list not read (%s)", e.what());
        return 0;
    }
    int added = 0;
    std::vector<std::string> pre = di1::zonePrefixes(z.playsets);
    for (auto& x : z.playsets) {
        added += x.added;
        if (x.added) logf("zones10: %s registered, PlaysetUniqueID %d, %u zones, start %s", x.name.c_str(), x.id, (unsigned)x.zones.size(), x.start.c_str());
        else logf("zones10: %s already in the 3.0 zone list (start %s)", x.name.c_str(), x.start.c_str());
    }
    std::lock_guard<std::mutex> l(g_playsets10Lock);
    g_playsets10 = z.playsets;
    g_missionPrefix10 = pre;
    return added;
}
static bool playsetMission10(const char* name)
{
    std::lock_guard<std::mutex> l(g_playsets10Lock);
    for (auto& x : g_missionPrefix10) if (!_strnicmp(name, x.c_str(), x.size())) return true;
    return false;
}

static RENDERDOC_API_1_1_2* g_rdoc;   // renderdoc10: RenderDoc loaded in-process, one frame captured once the world runs
// renderdoc10: one frame captured renderdoc10 seconds after the first 1.0 playset mission's tick (missiontrace).
static void armRenderDocCapture()
{
    static bool armed;   // first mission tick only; missiontrace's tick runs on one thread (B-48: unverified)
    if (armed) return;
    armed = true;
    std::thread([] {
        Sleep(1000 * g_cfg.renderdoc10);
        g_rdoc->TriggerCapture();
        logf("renderdoc: capture triggered");
    }).detach();
}

// missiontrace's config: a 1.0 playset's missions by zones10's prefixes; renderdoc10 arms on the first one's tick.
static trace::mission::Config missionTraceOf(const di1::Config& c)
{
    trace::mission::Config m;
    m.on = c.missionTrace;
    m.ownMission = playsetMission10;
    if (g_rdoc) m.ownMissionTick = armRenderDocCapture;
    return m;
}
// Meridian class registry (FUN_0178e330, run once at boot by FUN_013aaba0): the 1.0 classes of mission.hpp kAliases
// are registered after it with their 3.0 template. The transcoder treats them as registered only once that has run,
// so a registry built before the hook (plugin loaded late) leaves them stand-ins, never slotless (verify-meridian 6).
// native10: then the native classes of mernode.hpp. Only the SYS-1 probe is added here (no graph names it, so the
// registration path runs at boot and nothing instantiates it); 1.0 classes are added with mernode::add before this.
typedef void(__cdecl* VoidFn)();
static VoidFn g_merRegistry;
static int g_probeClass = -1;
static void probeImpulse(uint8_t* node, int, void* payload) { mernode::fire(node, g_probeClass, 1, payload); }
static void __cdecl hkMerRegistry()
{
    g_merRegistry();
    if (g_cfg.aliases10) {
        typedef void(__cdecl * RegFn)(const char* name);
        for (auto& a : mission::kAliases) {
            ((RegFn)rt(a.templateVa))(a.name);
            logf("meridian: 1.0 class %s registered with the template of %s", a.name, a.twin);
        }
        mer::addAliases(g_mer);
    }
    if (g_cfg.native10) {
        if (g_probeClass < 0)
            g_probeClass = mernode::add({"DI1NativeProbe", {{"Link_In", mernode::In}, {"Link_Out", mernode::Out}}, {}, 0, probeImpulse});
        int n = mernode::registerAll(rt, static_cast<void (*)(const char*, ...)>(logf), g_mer);
        logf("meridian native: %d of %d classes registered", n, mernode::count());
    }
}

// The Meridian registry hook (H1): aliases10 or native10, and only with the mer table loaded.
static bool installMerRegistry(const di1::Config& c)
{
    const bool classes = c.aliases10 || c.native10;
    if (!classes || !g_merOk) return true;
    return hook("Meridian class registry", 0x0178E330, "6A006A0068????????68????????", (void*)&hkMerRegistry,
                (void**)&g_merRegistry);
}

// The open half: a .lua stream (3.0's own or served from 1.0) -> CHD bytes. nullptr leaves the open as it was.
static int* chdForLua(const char* p, int* r)
{
    std::string src;
    if (r) { if (!readStream(r, src)) return memFile(src.data(), src.size()); }
    else if (!read10s(norm(p), src)) return nullptr;
    try {
        std::string c = chd::compile(chd::lua(src));
        logf("compiled %s %u B -> CHD %u B (%s)", p, (unsigned)src.size(), (unsigned)c.size(), r ? "3.0 file" : "1.0 file");
        return memFile(c.data(), c.size());
    } catch (std::exception& e) {
        logf("CHD compile of %s failed: %s; served as it is", p, e.what());
        return memFile(src.data(), src.size());
    }
}

// GameDB tables (plan PS-4). 3.0's DataMaps point its locks / inventory loaders at compiled .chd files and the
// loader objects are built in CHD mode (byte +0x2d = 1); the 1.0 DataMap points them at the Lua tables
// (GameDB/Incredibles/locks__lua.lua), whose text read as CHD is a wild pointer (in-game 2026-10-12: AV in
// FUN_018249d0 under the locks loader). The same 3.0 loaders read Lua when +0x2d == 0 (FUN_01612fa0: Lua state +
// FUN_018d9200(path), else FUN_018d91f0), so a .lua path runs the loader in its own Lua mode for that one call.
typedef uint32_t(__fastcall* GameDbLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);
static GameDbLoadFn g_loadLocks, g_loadInventory;

static uint32_t gameDbLoad(GameDbLoadFn orig, const char* tag, void* self, const char* path, uint32_t a2, uint32_t a3)
{
    char p[300];
    safeCopy(p, sizeof(p), path);
    uint8_t* mode = (uint8_t*)self + 0x2d;
    if (*mode && endsWith(p, ".lua")) {
        *mode = 0;
        uint32_t r = orig(self, nullptr, path, a2, a3);
        *mode = 1;
        logf("gamedb %s %s: CHD loader run in its Lua mode -> %u", tag, p, r);
        return r;
    }
    return orig(self, nullptr, path, a2, a3);
}
// DBFileLoader_Callback::load FUN_018db270 is vtable[1] (as the router FUN_018da710 calls it) and takes an engine
// CString: it add-refs the header at path-0xc (FUN_00caa0d0 unless the count is 0x7fffffff), so a plain char* path
// would be written through. The path goes in as a CString (ctor FUN_00418310, release FUN_00418230).
static uint8_t cbLoad(void* loader, const char* path, uint32_t a2, uint32_t a3)
{
    char* cs = nullptr;
    ((void*(__fastcall*)(void*, void*, const char*))rt(0x00418310))(&cs, nullptr, path);
    uint8_t ok = ((uint8_t(__fastcall*)(void*, void*, char*, uint32_t, uint32_t))(*(void***)loader)[1])(loader, nullptr, cs, a2, a3);
    ((void(__fastcall*)(void*, void*))rt(0x00418230))(&cs, nullptr);
    return ok;
}
// a 3.0 singleton's DBFileLoader_Callback (+4: ChestInfo DAT_022896fc, PerceptionInfo DAT_02289700), null if unreadable
static void* singletonLoader(uint32_t global)
{
    uint8_t* o = *(uint8_t**)rt(global);
    void* loader = o && sys::readable(o + 4, 4) ? *(void**)(o + 4) : nullptr;
    return loader && sys::readable(loader, 4) ? loader : nullptr;
}
static uint32_t __fastcall hkLoadLocks(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    uint32_t r = gameDbLoad(g_loadLocks, "locks", self, path, a2, a3);
    char p[300];
    safeCopy(p, sizeof(p), path);
    bool playset10 = sys::dbTrigger10(p, "locks__lua.lua");   // a 1.0 playset DataMap's locks table
    if (g_cfg.locks10 && playset10)   // PS-5: the 1.0 tables' records the Locks DB lacks
        for (std::string s : {std::string(sys::kLocks10Prefix) + p, std::string(sys::kLocks10Core)}) {
            sys::setLockUids(sys::loaderUids(self));
            gameDbLoad(g_loadLocks, "locks", self, s.c_str(), a2, a3);
        }
    // GLB-1: 1.0 ChestInfo.bin through 3.0's chestInfo loader
    void* loader = g_cfg.chest10 && playset10 ? singletonLoader(0x022896FC) : nullptr;
    if (loader) {
        uint8_t ok = cbLoad(loader, sys::kChest10, a2, a3);
        uint8_t *ci = *(uint8_t**)rt(0x022896FC), *chests = ci + 0x1c;
        logf("chestInfo loader given %s -> %u; ChestInfo holds %d chests, %d resources, %d choreographies", sys::kChest10, ok,
             *(int*)(chests + 4), *(int*)(ci + 0x10), *(int*)(ci + 0x30));
    }
    return r;
}

// PS-8 (sys.hpp): the expendables package loader FUN_0160aa30(path, ?) (ret 8) and the definition ctor FUN_016022c0
// (thiscall, ret 4) -- entries of a package 3.0 lacks get the 1.0 pop-in
typedef void(__fastcall* ExpPkgFn)(void* ecx, void* edx, const char* path, uint32_t a2);
static ExpPkgFn g_expPkg;
// deliv10: 1.0 RewardType "Delivery" (1.0 registry FUN_00cc2580 -> FUN_00b757c0, DeliveryExpendableReward) is the one
// 1.0 type 3.0's registry (FUN_01232de0: FUN_0160a9c0(name, factory), thiscall on the expendables manager DAT_0228aa40)
// lacks, so the 15 DELIVERY entries of inc_expendables.lua (INC_Spawn_GlidePack, _HoverBoard, _ZPE, cars ...) get no
// object (entry reader FUN_01603930 drops a type without factory). On PC its grant 0xbae4d0 runs the Multi grant's
// EXPENDABLE / UNLOCK / SPAWNACTOR / STAT handlers (FUN_00f99940, FUN_00f75bf0, FUN_00f75c30, FUN_00facb90, as Multi
// FUN_00facc50) plus DeliveryNode bookkeeping (FUN_00b59940/80, FUN_00b599e0; the network branch is dead, FUN_00493940
// returns 0). 3.0 kept Multi (factory FUN_016049f0, grant FUN_0160fbd0, same command table), so Delivery is registered
// with it; the bookkeeping comes with a DeliveryNode port (impl-sys 6.3). Registered before the first package load.
static void registerDelivery10()
{
    static std::mutex m;   // packages may load on several threads; one registry insert
    static bool done = false;
    std::lock_guard<std::mutex> l(m);
    if (done) return;
    void* mgr = *(void**)rt(0x0228AA40);
    if (!mgr || !sys::readable(mgr, 0x70)) return;
    ((void(__fastcall*)(void*, void*, const char*, void*))rt(0x0160A9C0))(mgr, nullptr, "Delivery", (void*)rt(0x016049F0));
    done = true;
    logf("deliv10: RewardType Delivery registered with 3.0's MultiExpendableReward factory (FUN_016049f0)");
}
static void __fastcall hkExpPkg(void* ecx, void*, const char* path, uint32_t a2)
{
    if (g_cfg.deliv10) registerDelivery10();
    if (!g_cfg.popin10) return g_expPkg(ecx, nullptr, path, a2);
    char p[300];
    safeCopy(p, sizeof(p), path);
    int* s = g_open(p, 0, 0, 0, nullptr);   // the engine's own open, unhooked: does 3.0 ship this package?
    if (s) ((void(__fastcall*)(void*, void*, uint32_t))(*(void***)s)[11])(s, nullptr, 1);
    sys::g_pkg10 = s ? -1 : 0;
    g_expPkg(ecx, nullptr, path, a2);
    if (sys::g_pkg10 >= 0) logf("expendables %s (a 1.0 package): %d entries without PopinDistance pop in as in 1.0", p, sys::g_pkg10);
    sys::g_pkg10 = -1;
}

// The expendables package hook (H1): popin10 or deliv10.
static bool installExpPkg(const di1::Config& c)
{
    if (!c.popin10 && !c.deliv10) return true;
    return hook("expendables package load", 0x0160AA30, "558BEC83E4F86AFF68????????64A1", (void*)&hkExpPkg,
                (void**)&g_expPkg);
}
// PS-5 (sys.hpp): DBFileLoader_Callback::load FUN_018db270 (thiscall CString path, a2, a3; ret 0xc) -- every Lua
// callback loader (bitMask, stats, chestInfo ...); after the playset's bitmasks, the 1.0 core masks as one more package,
// then (GLB-1) the 1.0 perceptions 3.0 lacks, whose actor types those masks register
typedef uint8_t(__fastcall* CbLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);
static CbLoadFn g_cbLoad;
static uint8_t __fastcall hkCbLoad(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    uint8_t r = g_cbLoad(self, nullptr, path, a2, a3);
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (!sys::dbTrigger10(p, "bitmasks.lua")) return r;   // a 1.0 playset DataMap's bitmasks
    if (g_cfg.bits10) logf("bitMask loader given %s after %s -> %u", sys::kBits10, p, cbLoad(self, sys::kBits10, a2, a3));
    void* pl = g_cfg.perc10 ? singletonLoader(0x02289700) : nullptr;
    const uint8_t* pi = pl ? *(uint8_t**)rt(0x02289700) : nullptr;
    if (pi && sys::snapshotPerc30(pi)) {
        int before = *(const int*)(pi + 0x104);
        uint8_t ok = cbLoad(pl, sys::kPerc10, a2, a3);
        logf("perceptions loader given %s -> %u; PerceptionInfo holds %d perceptions (%d before)", sys::kPerc10, ok,
             *(const int*)(pi + 0x104), before);
    }
    return r;
}

// NPC-8: the .ap key SpawnEconomyOnDeath. 1.0's combat getter FUN_00aa1520 decides once per actor: default 1, then
// the key from the actor's params (component DAT_016a6a80, +8; 271 uses in the 195 Metroville .ap files: 268 say 1,
// 3 in in_city_story__missiongraph.ap say 0). 3.0's getter FUN_00ff69c0 (thiscall, decided-bit 0x20 of +0x90c) sets 1 unread; the 1.0
// read goes in front of it: params component DAT_0220353c (FUN_0069ecd0, +8, as 3.0 reads ChestName in FUN_00b9ebb0),
// has-key FUN_006f9640, bool FUN_006f9610, setter FUN_00fee6b0 (the one Combat_SetOnDeathSpawnEconomy calls).
typedef uint32_t(__fastcall* CombatGetFn)(uint8_t* self, void* edx);
static CombatGetFn g_econOnDeath;
static uint32_t __fastcall hkEconOnDeath(uint8_t* self, void*)
{
    if (!(self[0x90c] & 0x20)) {
        void* comp = ((void*(__cdecl*)(void*, uint32_t))rt(0x0069ECD0))(*(void**)self, *(uint16_t*)rt(0x0220353C));
        void* params = comp ? (uint8_t*)comp + 8 : nullptr;
        if (params && ((uint8_t(__fastcall*)(void*, void*, const char*, uint32_t))rt(0x006F9640))(params, nullptr, "SpawnEconomyOnDeath", 0)) {
            uint8_t v = ((uint8_t(__fastcall*)(void*, void*, const char*, uint32_t, uint32_t))rt(0x006F9610))(params, nullptr, "SpawnEconomyOnDeath", 0, 0);
            ((void(__fastcall*)(void*, void*, uint32_t))rt(0x00FEE6B0))(self, nullptr, v);
            if (!v) logf("SpawnEconomyOnDeath = 0 read for actor %p", *(void**)self);
        }
    }
    return g_econOnDeath(self, nullptr);
}

// econ10's hook (H1).
static bool installEcon(const di1::Config& c)
{
    if (!c.econ10) return true;
    return hook("combat SpawnEconomyOnDeath get", 0x00FF69C0, "53568BF1F6860C09000020", (void*)&hkEconOnDeath,
                (void**)&g_econOnDeath);
}

// the ctor returns this, and the entry reader uses it (FUN_01603930 at 0x01603a20: mov esi, eax for an entry without
// RewardType), so the detour must hand it back
typedef void*(__fastcall* ExpDefFn)(void* self, void* edx, void* entry);
static ExpDefFn g_expDef;
static void* __fastcall hkExpDef(void* self, void*, void* entry)
{
    void* r = g_expDef(self, nullptr, entry);
    if (sys::g_pkg10 >= 0 && sys::applyPopin10(self)) ++sys::g_pkg10;
    return r;
}

// popin10's definition ctor hook (H1).
static bool installExpDef(const di1::Config& c)
{
    if (!c.popin10) return true;
    return hook("expendable definition ctor", 0x016022C0, "6AFF68????????64A100000000", (void*)&hkExpDef,
                (void**)&g_expDef);
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
        logf("served %s from 1.0 %s (%u B) -> chestInfo Lua %u B%s%s", p, bin.c_str(), (unsigned)src.size(), (unsigned)out.size(),
             ok ? "" : " FAILED: ", ok ? "" : (src.empty() ? "no 1.0 file" : why.c_str()));
        return ok ? memFile(out.data(), out.size()) : nullptr;
    }
    size_t ppre = strlen(sys::kPerc10Prefix);
    if (!np.compare(0, ppre, sys::kPerc10Prefix) && endsWith(p, ".lua")) {   // GLB-1 PerceptionInfo.bin -> Lua
        std::string bin = np.substr(ppre, np.size() - ppre - 4) + ".bin", why;
        int added = 0;
        bool ok = read10s(bin, src) && sys::perceptionInfoLua(src, sys::perc30(), out, added, &why);
        logf("served %s from 1.0 %s (%u B) -> perceptions Lua %u B, %d perceptions 3.0 lacks%s%s", p, bin.c_str(),
             (unsigned)src.size(), (unsigned)out.size(), added, ok ? "" : " FAILED: ", ok ? "" : (src.empty() ? "no 1.0 file" : why.c_str()));
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
static uint32_t __fastcall hkLoadInventory(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    return gameDbLoad(g_loadInventory, "inventory", self, path, a2, a3);
}

// The GameDB loader hooks (H1); the callback loader only for bits10 or perc10.
static bool installGameDb(const di1::Config& c)
{
    bool ok = hook("GameDB locks load", 0x01612FA0, "6AFF64A100000000", (void*)&hkLoadLocks, (void**)&g_loadLocks);
    ok &= hook("GameDB inventory load", 0x014C08D0, "6AFF64A100000000", (void*)&hkLoadInventory,
               (void**)&g_loadInventory);
    ok &= hook("GameDB CHD callback load", 0x018DB450, "6AFF64A100000000", (void*)&hkLoadChdCallback,
               (void**)&g_loadChdCallback);
    if (c.bits10 || c.perc10)
        ok &= hook("GameDB callback load", 0x018DB270, "6AFF64A10000000068????????50", (void*)&hkCbLoad,
                   (void**)&g_cbLoad);
    return ok;
}

// __invoke_watson (0x12CA1D1): the CRT's invalid-parameter fail-fast terminates without any exception handler
// seeing it (in-game 2026-10-12: 0xC0000417 loading the 1.0 side-missions graph). Log who called it, then die.
typedef void(__cdecl* WatsonFn)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
static WatsonFn g_watson;
static void __cdecl hkWatson(const wchar_t* a, const wchar_t* b, const wchar_t* c, unsigned d, uintptr_t e)
{
    void* frames[24];
    USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    char line[600] = "CRT invalid parameter (__invoke_watson); stack (static):";
    for (USHORT i = 0; i < n; ++i) {
        uintptr_t f = (uintptr_t)frames[i];
        char s[24];
        if (f >= g_base && f < g_base + 0x2100000) sprintf_s(s, " %08X", (unsigned)(f - g_base + 0x400000));
        else sprintf_s(s, " ext:%08X", (unsigned)f);
        strcat_s(line, s);
    }
    logf("%s", line);
    g_watson(a, b, c, d, e);
}

// FUN_0041fef0: the resident-file handlers' path normaliser, strcpy_s into a 0x80-byte stack buffer; a longer path
// is a CRT fail-fast (found through the watson hook). A too-long path is logged and truncated (the lookup misses).
typedef void(__cdecl* NormPathFn)(char* dst, unsigned size, const char* src);
static NormPathFn g_normPath;
static void __cdecl hkNormPath(char* dst, unsigned size, const char* src)
{
    size_t n = src ? strnlen(src, 4096) : 0;
    if (src && n >= size) {
        logf("path too long for the engine's %u-byte buffer (%u): %s", size, (unsigned)n, src);
        std::string cut(src, size - 1);
        g_normPath(dst, size, cut.c_str());
        return;
    }
    g_normPath(dst, size, src);
}

// The exe CRT's fail-fast hooks (H1): always on.
static bool installCrt(const di1::Config&)
{
    bool ok = hook("CRT __invoke_watson", 0x012CA1D1, "8BFF566A01", (void*)&hkWatson, (void**)&g_watson);
    ok &= hook("resident path normaliser", 0x0041FEF0, "8B44240C56", (void*)&hkNormPath, (void**)&g_normPath);
    return ok;
}

// ---- ui (ui.hpp) ----
// 3.0's zone string getter FUN_0139e0b0 (thiscall on the zone manager *0x0225f548: key, zone, a3, default; ret 0x10).
// Its only "LoadingScreens" reader is the loading-screen choice FUN_00b78c70; a zone whose record carries 1.0's
// LoadingScreen key (11 1.0 records, 0 of 3.0's: zonelist30) gets 1.0's choice (ui::loadingScreen10) instead of
// zonereg's LoadingScreens (in-game 2026-10-13: flash/loadingscreen/in/loadingscreen_inc.gfx MISSING).
typedef const char*(__fastcall* ZoneStrFn)(void* self, void* edx, const char* key, uint32_t zone, uint32_t a3, const char* def);
static ZoneStrFn g_zoneStr;
static const char* __fastcall hkZoneStr(void* self, void*, const char* key, uint32_t zone, uint32_t a3, const char* def)
{
    if (key && key[0] == 'L' && !strcmp(key, "LoadingScreens")) {
        const char* k10 = g_zoneStr(self, nullptr, "LoadingScreen", zone, a3, nullptr);
        if (k10 && *k10) {
            static std::mutex mx;
            static int counter;   // 1.0's profile stat LastLoadingScreen; ponytail: per session, 1.0 kept it in the profile
            static std::string pick;
            std::lock_guard<std::mutex> lock(mx);
            pick = ui::loadingScreen10(k10, counter);
            logf("loading screen: zone %X has 1.0 LoadingScreen=%s -> %s (1.0 rule)", zone, k10, pick.c_str());
            return pick.c_str();
        }
    }
    return g_zoneStr(self, nullptr, key, zone, a3, def);
}

// 3.0's FMV path resolver FUN_0067e460 (thiscall: out, size, name; ret 0xc): "FMV\%s.bik" through the asset open ->
// path length, or 0; FUN_0067e9a0 then BinkOpen()s that path by name. A clip 3.0 lacks resolves to the 1.0 install's
// loose assets\fmv\PC1\<clip>.bik, opened in place (ui::fmvClip). bink2w32 accepts Bink 1: it compares the header
// with 'BIKf'..'BIKi' as well as 'KB2f'..'KB2j' (byte search of bink2w32_orig.dll); the 1.0 clips are 'BIKi', and
// FUN_0067f260 skips 3.0's de-obfuscation for a 'BIK' header.
typedef int(__fastcall* FmvPathFn)(void* self, void* edx, char* out, uint32_t size, const char* name);
static FmvPathFn g_fmvPath;
static int __fastcall hkFmvPath(void* self, void*, char* out, uint32_t size, const char* name)
{
    int n = g_fmvPath(self, nullptr, out, size, name);
    if (n || !g_cfg.install10[0] || !out) return n;
    std::string clip = ui::fmvClip(name);
    if (clip.empty()) return n;
    std::string p = std::string(g_cfg.install10) + "\\assets\\fmv\\PC1\\" + clip + ".bik";
    if (p.size() >= size || GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return n;
    strcpy_s(out, size, p.c_str());
    logf("FMV %s: 3.0 has none -> 1.0 %s", clip.c_str(), p.c_str());
    return (int)p.size();
}

// Tutorial element 61 (Mission_Tutorial's first task, "Press Tab to access the Mission Log"): both exes' evaluate
// (1.0 FUN_00ba3950, 3.0 FUN_00de3a60) test that a screen named "SelectMenu" is open. That was 1.0's playset ribbon
// (UI_GetRibbonName, 1.0 FUN_00c32cf0: "SelectMenu" in a playset); 3.0's ribbon (FUN_00b42540, thiscall, ecx and
// args unused, ret 8) opens "MissionLog" in a playset instead (Tab -> TOY_STORE -> ShowRibbonUI,
// avatar_controls_win32.xldb), so 61 never completed. The literal 0x01D19CDC has one reference in the exe (the
// case-0x3d push at 0x00DE4C5B), so only that lookup is redirected: it also answers for 3.0's ribbon. 62 is already
// 3.0's MissionLog and 63 3.0's Mission_MissionActivated event (FUN_00b67560 -> FUN_00dae4e0).
typedef void*(__fastcall* FindScreenFn)(void* self, void* edx, const char* name, int player, char last);
static FindScreenFn g_findScreen;
static void* __fastcall hkFindScreen(void* self, void*, const char* name, int player, char last)
{
    void* s = g_findScreen(self, nullptr, name, player, last);
    if (s || name != (const char*)rt(0x01D19CDC)) return s;
    typedef const char*(__fastcall* RibbonFn)(void*, void*, int, int);
    const char* rib = ((RibbonFn)rt(0x00B42540))(nullptr, nullptr, player, 0);
    if (!rib || !_stricmp(rib, name)) return s;
    if ((s = g_findScreen(self, nullptr, rib, player, last))) logf("tutorial element 61: %s open (3.0's ribbon) stands for SelectMenu", rib);
    return s;
}

// ControllerContextNode list parse FUN_01224880 (thiscall on the node, no args): ContextName is the engine string at
// +0x98 (copied to +0xa8 and split on ','; +0xa4 = UseAsPrefix). A 1.0 list gets the 3.0 contexts that gate the
// same action as one it keeps enabled (ui::contextList10); the string is reassigned with the engine's own
// assign (FUN_0041a250, thiscall (const char*), ret 4) before the parse. No shipped 3.0 graph negates
// TUT_DISABLE_SELECT_ITEM (its exclusions: -TUT_DISABLE_CAM, -UI_TUT_DISABLE_GEN_CANCEL/_OK), so only 1.0 lists change.
typedef void(__fastcall* CtxParseFn)(void* self, void* edx);
static CtxParseFn g_ctxParse;
static void __fastcall hkCtxParse(void* self, void*)
{
    char** name = (char**)((char*)self + 0x98);
    if (*name) {
        std::string s = ui::contextList10(*name);
        if (!s.empty()) {
            typedef void*(__fastcall* StrAssignFn)(void* str, void* edx, const char* v);
            ((StrAssignFn)rt(0x0041A250))(name, nullptr, s.c_str());
            logf("controller context %s (3.0 gates of the actions 1.0 kept)", *name);
        }
    }
    g_ctxParse(self, nullptr);
}

// tutorial10 keys on the case-0x3d push (68 <imm32> at 0x00DE4C5A, relocated) of the literal "SelectMenu".
static bool selectMenuPushFound()
{
    const uint8_t* push = (const uint8_t*)rt(0x00DE4C5A);
    const bool pushes = push[0] == 0x68 && *(const uint32_t*)(push + 1) == (uint32_t)rt(0x01D19CDC);
    return pushes && !strcmp((const char*)rt(0x01D19CDC), "SelectMenu");
}

// The ui hooks (H1), each with serve10; the ui keys are logged first.
static bool installUi(const di1::Config& c)
{
    logf("ui: loadscreen10=%d uiscope10=%d fmv10=%d text10=%d tips10=%d tutorial10=%d context10=%d", c.loadScreen10,
         c.uiScope10, c.fmv10, c.text10, c.tips10, c.tutorial10, c.context10);
    if (!c.serve10) return true;
    bool ok = true;
    if (c.loadScreen10)
        ok &= hook("zone string field (ui: 1.0 loading screen)", 0x0139E0B0, "83EC08568BF180BEE403000000",
                   (void*)&hkZoneStr, (void**)&g_zoneStr);
    if (c.fmv10)
        ok &= hook("FMV path (ui: 1.0 clips)", 0x0067E460, "81EC0401000053568BB42418010000", (void*)&hkFmvPath,
                   (void**)&g_fmvPath);
    if (c.tutorial10 && selectMenuPushFound()) {
        ok &= hook("screen by name (ui: tutorial element 61)", 0x0154FF90, "837C2404007505", (void*)&hkFindScreen,
                   (void**)&g_findScreen);
    } else if (c.tutorial10) {
        logf("tutorial10: the element-61 push of \"SelectMenu\" is not where it was analysed: not hooked");
        ok = false;
    }
    if (c.context10)
        ok &= hook("controller context list (ui: 1.0 tutorial contexts)", 0x01224880, "5153558BE98A85A4000000",
                   (void*)&hkCtxParse, (void**)&g_ctxParse);
    return ok;
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
static int* reopen(const Open& o) { return g_open(o.path, o.flags, o.a3, o.a4, o.a5); }

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

constexpr uint32_t kZoneManager = 0x0225F548;   // *: the zone manager (FUN_0139e0b0's this)
constexpr uint32_t kZoneByName = 0x01391150;    // FUN_01391150: thiscall (uint32_t* out, name), its zone by name

// 3.0's tips (o.r, consumed) plus the 1.0 records it lacks; registered zones' keys stay 3.0's (FUN_01391150).
typedef void(__fastcall* ZoneByNameFn)(void* self, void* edx, uint32_t* out, const char* name);
static int* tips30(const Open& o)
{
    std::string t30, t10;
    if (!readStream(o.r, t30)) return reopen(o);
    if (t30.find("ZoneTipCategories") == std::string::npos) return memFile(t30.data(), t30.size());
    const bool got10 = read10s("data/tips.lua", t10);
    if (!got10) return memFile(t30.data(), t30.size());
    void* zm = *(void**)rt(kZoneManager);
    int k = ui::mergeTips(t30, t10, [&](const std::string& z) {
        uint32_t id = 0;
        if (zm) ((ZoneByNameFn)rt(kZoneByName))(zm, nullptr, &id, z.c_str());
        return id != 0;
    });
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
    std::vector<std::pair<std::string, std::string>> ps;
    {
        std::lock_guard<std::mutex> l(g_playsets10Lock);
        for (auto& x : g_playsets10) ps.push_back({x.name, x.start});
    }
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
        if (zl10) kz = zones10(t, o.p);   // first: the steps below apply to the added records as to written ones
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
    if (o.write || !endsWith(o.p, ".lua") || !takeChdPath(o.p)) return false;
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
    o.r = g_open(path, flags, a3, a4, a5);
    if (o.prof) o.t30 = nowMs() - o.tIn;
    safeCopy(o.p, sizeof(o.p), path);
    for (OpenStage stage : kOpenStages) {
        int* out = nullptr;
        if (stage(o, out)) return out;
    }
    logOpen(o);
    return o.r;
}

// The asset open hook (H1). Code outside hkOpen calls its original, so a refused AssetOpen installs nothing else.
static bool installAssetOpen(const di1::Config&)
{
    if (hook("AssetOpen", 0x0041CB10, "6AFF68????????", (void*)&hkOpen, (void**)&g_open)) return true;
    logf("feature AssetOpen off: no other hook installed, the game runs as plain 3.0");
    return false;
}

// ---- real 1.0 shaders at runtime (gen30.hpp; plan in DI3Tools/gen30.py) ----
// The engine loads its whole shader package once, at boot (renderer init FUN_00479470: shaders.list ->
// FUN_00476180, then the linker FUN_00472050), and 3.0's index fields cap a session at 2047 programs per stage --
// 1.0's shaders do not all fit beside 3.0's, so each is registered when a material first names it ('x'<GUID>, the
// name the world transcode gives it): the shader-by-name lookup FUN_0045c820 is hooked. On a miss for an 'x' name
// the 1.0 shader is translated (programs compiled once, then from <plugin dir>\di1compat_shaders), its programs are
// added through the engine's own registration (VS FUN_0045c230 / PS FUN_0045c350: CreateVertexShader /
// CreatePixelShader, table append) under ids above every shipped one (the tables stay sorted), the .shd is
// finalized by the engine's FUN_0045cf40 (EDI = shader, ECX = &cursor, as the loader calls it) and each pass gets
// a pass-state record built as the linker builds one (records 0x38 B at 0x02173a68, count u16 at 0x02173a66).
struct Blob : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, Blob**, Blob**);
static D3DCompileFn g_d3dCompile;
static std::string g_shaderCache;
static std::mutex g_sh10Lock;
static std::unordered_map<std::string, uint8_t*> g_sh10;                       // 'x'<GUID> -> finalized .shd (nullptr: failed)
static std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> g_prog10;  // code -> engine id, table index

static thread_local std::string g_compiling10;   // the shader shader10 is generating (debug views name it)
// The shader cache's file at path, read into code: true when it holds DXBC (a whole header, "DXBC" first).
static bool cachedDxbc(const std::string& path, std::string& code)
{
    constexpr size_t kDxbcHeader = 32;   // magic, checksum, version, size and chunk count
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") || !f) return false;
    char buf[65536];
    size_t got;
    code.clear();
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) code.append(buf, got);
    fclose(f);
    return code.size() > kDxbcHeader && !code.compare(0, 4, "DXBC");
}

static bool compile10(const std::string& hlsl0, const char* target, std::string& code, std::string& msg)
{
    std::string idcolor;
    if (g_cfg.bufview10 == 8 && target[0] == 'p') {
        // test: final passes skip, G-buffer albedo carries the shader's ID colour (3.0's own composite shows it)
        uint64_t h = fnv64(g_compiling10);
        char c[160];
        sprintf_s(c, "o.o1 = float4(sqrt(float3(%.2f, %.2f, %.2f)), 0.2236);", (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
        if (hlsl0.find("tLight") != std::string::npos)
            idcolor = "float4 main(float4 p : SV_Position) : SV_Target0 { discard; return 0; }";
        else if (size_t at = hlsl0.find("o.o1 = float4(0, 0, 0, 0);"); at != std::string::npos) {
            idcolor = hlsl0;
            idcolor.replace(at, strlen("o.o1 = float4(0, 0, 0, 0);"), c);
            logf("idcolor %s = %.2f %.2f %.2f", g_compiling10.c_str(), (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
        }
    }
    if (g_cfg.bufview10 >= 6 && g_cfg.bufview10 <= 7 && target[0] == 'p' && (g_cfg.bufview10 == 7 ? hlsl0.find("SV_Target1") == std::string::npos
                                                                           : hlsl0.find("tLight") != std::string::npos)) {
        // test: each translated final pass a solid colour of its own, logged with its shader
        uint64_t h = fnv64(g_compiling10);
        char c[200];
        sprintf_s(c, "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(%.2f, %.2f, %.2f, 1); }",
                  (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
        idcolor = c;
        logf("idcolor %s = %.2f %.2f %.2f", g_compiling10.c_str(), (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
    }
    static const std::string magenta = "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(1, 0, 1, 1); }";
    static const std::string bufview[5] = {
        "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t3 : register(t3); SamplerState s3 : register(s3);\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t3.Sample(s3, p.xy * CB1[17].zw).rgb, 1); }",
        "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t0.Sample(s0, p.xy * CB1[17].zw).xxx, 1); }",
        "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t4 : register(t4); SamplerState s4 : register(s4);\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t4.Sample(s4, p.xy * CB1[17].zw).rgb, 1); }",
        "cbuffer perInstance : register(b4) { float4 CB4[1]; };\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(CB4[0].rgb * 0.5 + float3(0.5, 0, 0) * (CB4[0].a == 0), 1); }",
        "Texture2D t5 : register(t5); SamplerState s5 : register(s5);\n"
        "struct PSIn { float4 pos : SV_Position; float4 TEXCOORD0 : TEXCOORD0; float4 TEXCOORD1 : TEXCOORD1; float4 TEXCOORD2 : TEXCOORD2; };\n"
        "float4 main(PSIn i) : SV_Target0 { return float4(t5.Sample(s5, i.TEXCOORD2.xy).rgb, 1); }"};
    const std::string& hlsl = g_cfg.magenta10 && target[0] == 'p' ? magenta
        : !idcolor.empty() ? idcolor
        : g_cfg.bufview10 && target[0] == 'p' && hlsl0.find("tLight") != std::string::npos ? bufview[(g_cfg.bufview10 - 1) % 5] : hlsl0;
    std::string path = g_shaderCache + "\\" + hash::shaderCacheName(target, hlsl);
    if (cachedDxbc(path, code)) return true;
    FILE* f = nullptr;
    if (!g_d3dCompile) { msg = "no d3dcompiler_47"; return false; }
    Blob *c = nullptr, *e = nullptr;
    HRESULT hr = g_d3dCompile(hlsl.data(), hlsl.size(), "di1compat", nullptr, nullptr, "main", target, 0, 0, &c, &e);
    if (e) { msg.assign((const char*)e->GetBufferPointer(), e->GetBufferSize()); e->Release(); }
    if (FAILED(hr) || !c) { if (c) c->Release(); return false; }
    code.assign((const char*)c->GetBufferPointer(), c->GetBufferSize());
    c->Release();
    if (!fopen_s(&f, path.c_str(), "wb") && f) { fwrite(code.data(), 1, code.size(), f); fclose(f); }
    return true;
}

// A program into the engine's table for its stage -> engine id and table index (identical code shared).
static bool registerProgram10(const gen30::Program& p, uint32_t& id, uint32_t& index)
{
    uint64_t key = fnv64(p.code) ^ (p.ps ? 0x9e3779b97f4a7c15ull : 0);
    if (auto it = g_prog10.find(key); it != g_prog10.end()) { id = it->second.first; index = it->second.second; return true; }
    const uint32_t countVa = p.ps ? 0x0216B04C : 0x0215EFC4, tableVa = p.ps ? 0x0216B050 : 0x0215EFC8;
    uint16_t n = *(uint16_t*)rt(countVa);
    if (n >= 2047) { logf("shaders10: %s table full (%u)", p.ps ? "PS" : "VS", n); return false; }
    static uint32_t next[2] = {0xF0000000u, 0xF0000000u};
    uint32_t last = n ? *(uint32_t*)rt(tableVa + 8 * (n - 1)) : 0;
    uint32_t& nx = next[p.ps ? 1 : 0];
    if (nx <= last) nx = last + 1;
    id = nx++;
    void* data = _aligned_malloc(p.code.size(), 16);   // kept: the engine reads VS bytecode again for input layouts
    memcpy(data, p.code.data(), p.code.size());
    typedef void(__cdecl * RegFn)(uint32_t id, const void* data, uint32_t size);
    ((RegFn)rt(p.ps ? 0x0045C350 : 0x0045C230))(id, data, (uint32_t)p.code.size());
    if (*(uint16_t*)rt(countVa) != n + 1 || !*(uint32_t*)rt(tableVa + 8 * n + 4)) {
        logf("shaders10: program %08X not created", id);
        return false;
    }
    index = n;
    g_prog10[key] = {id, index};
    return true;
}

static void finalizeShd10(uint8_t* shd)
{
    uint32_t cursor[2] = {(uint32_t)(uintptr_t)(shd + 0xE0), 0};
    uint32_t* pc = cursor;
    void* fn = (void*)rt(0x0045CF40);
    __asm {
        push edi
        mov ecx, pc
        mov edi, shd
        call fn
        pop edi
    }
}

static uint32_t inputLayout10(uint32_t vfIndex, uint32_t vsIndex)
{
    void* fn = (void*)rt(0x0044D430);
    uint32_t entry = (uint32_t)rt(0x02170664) + vfIndex * 0x34, r;
    __asm {
        push 0xff
        push 0xff
        push 0xff
        mov ecx, entry
        mov eax, vsIndex
        call fn
        add esp, 12
        mov r, eax
    }
    return r;
}

// Pass-state records (0x38 B at 0x02173a68, u16 count at 0x02173a66): the renderer packs a record index into 12 bits
// of its draw sort key, so 4096 is a hard ceiling -- and 3.0's boot linker FUN_00472050 spends 3802 of them on every
// pass of all 836 3.0 shaders. So nothing links at boot (hkLinkAll keeps only the linker's shader-array sort) and a
// shader's passes are linked on its first lookup (every shader the engine uses comes through FUN_0045c820): the
// records go to the shaders a session uses. Fields as FUN_00472050 builds them; program indices by the engine's
// FUN_00450970 over its sorted tables (ours too: their ids sort after every shipped one).
// The render thread adds records too (FUN_0045c5c0 clones one per extra vertex format, mid-draw); with our linker
// writing from loader threads the two raced for the count and the clone chains (FUN_0045c5c0 walks them in a loop):
// the first lazy-linking run froze on the loading screen (2026-10-13). One lock for both writers.
static CRITICAL_SECTION g_recCs;
static std::unordered_set<uint8_t*> g_linked;
struct RecLock { RecLock() { EnterCriticalSection(&g_recCs); } ~RecLock() { LeaveCriticalSection(&g_recCs); } };

typedef uint32_t(__cdecl* CloneRecFn)(uint32_t rec, uint32_t vformat);
static CloneRecFn g_cloneRec;
static uint32_t __cdecl hkCloneRec(uint32_t rec, uint32_t vformat)
{
    RecLock lock;
    uint32_t r = g_cloneRec(rec, vformat);
    uint32_t* c = (uint32_t*)rt(0x02173A68) + 14 * (r & 0xffff);
    const char* nm = c[6] ? *(const char**)(uintptr_t)c[6] : nullptr;
    if (!c[7] && nm) {
        static int told;
        if (told++ < 40) logf("clone without input layout: %s pass %u vformat %u (record %u from %u)", nm, c[0] & 0x3f, vformat & 0xff, r & 0xffff, rec & 0xffff);
    }
    return r;
}

static bool linkShader(uint8_t* shd)
{
    RecLock lock;
    bool seen = g_linked.count(shd) != 0;
    int fresh = 0;
    typedef uint32_t(__cdecl * IndexFn)(uint32_t id, uint32_t table, uint32_t count, uint32_t fallback);
    IndexFn index = (IndexFn)rt(0x00450970);
    auto idx = [&](uint32_t id, uint32_t tableVa, uint32_t countVa, uint32_t fallback) -> uint32_t {
        return id ? index(id, (uint32_t)rt(tableVa), *(uint16_t*)rt(countVa), fallback) & 0xffff : 0;
    };
    uint32_t shaderIndex = 0x3ff;                      // the record key's shader field; ours are not in the array
    uint32_t nsh = *(uint32_t*)rt(0x0215CFC0);
    for (uint32_t k = 0; k < nsh; ++k)
        if (*(uint32_t*)rt(0x0215CFC8 + 8 * k) == (uint32_t)(uintptr_t)shd) { shaderIndex = k & 0x3ff; break; }
    uint8_t* passes = *(uint8_t**)(shd + 0x38);
    int npass = shd[0x19];
    for (int i = 0; i < npass; ++i) {
        uint8_t* p = passes + 0x50 * i;
        if (!p[0x14]) {                                // no stage (yet): not drawn
            if (g_cfg.fadeProbe10 && !seen)
                logf("fade probe: shader %s pass %d word %08x has no stage at link time (no record)",
                     *(const char**)shd ? *(const char**)shd : "(built-in)", i, *(uint32_t*)p);
            continue;
        }
        uint16_t& count = *(uint16_t*)rt(0x02173A66);
        uint16_t had = *(uint16_t*)(p + 0x16);         // linked already: its record points back at this pass
        uint32_t* old = (uint32_t*)rt(0x02173A68) + 14 * had;
        if (had < count && old[6] == (uint32_t)(uintptr_t)shd && (old[0] & 0x3f) == (uint32_t)i) continue;
        ++fresh;
        if (count >= 4095) { logf("pass-state records full (%u)", count); return false; }
        uint16_t n = count;
        uint32_t* rec = (uint32_t*)rt(0x02173A68) + 14 * n;
        memset(rec, 0, 0x38);
        uint32_t word = *(uint32_t*)p;
        uint32_t vsi = idx(*(uint32_t*)(p + 0x28), 0x0215EFC8, 0x0215EFC4, 0x7ff);
        uint32_t psi = idx(*(uint32_t*)(p + 0x30), 0x0216B050, 0x0216B04C, 0x7ff);
        uint32_t s0 = idx(*(uint32_t*)(p + 0x2c), 0x0216AFCC, 0x0216AFC8, 0xf), s1 = idx(*(uint32_t*)(p + 0x38), 0x0216F0D8, 0x0216F0D4, 0xf);
        uint32_t s2 = idx(*(uint32_t*)(p + 0x3c), 0x0216F15C, 0x0216F158, 0xf), s3 = idx(*(uint32_t*)(p + 0x34), 0x0216F054, 0x0216F050, 0xf);
        rec[0] = (uint32_t)(i & 0x3f) | (shaderIndex << 6) | ((word >> 25) << 16) | ((psi & 0xff) << 24);
        rec[1] = ((psi & 0x7ff) >> 8) | ((vsi & 0x7ff) << 3) | ((uint32_t)p[0x18] << 14);
        rec[2] = (s0 & 0xf) | (s1 & 0xf) << 4 | (s2 & 0xf) << 8 | (s3 & 0xf) << 12;
        ((uint16_t*)rec)[6] = n;                       // +0x0c, +0x0e: itself (FUN_0045c5c0 chains other vformats)
        ((uint16_t*)rec)[7] = n;
        ((uint16_t*)rec)[8] = *(uint16_t*)(p + 0x18);  // +0x10
        ((uint16_t*)rec)[9] = p[0x15];                 // +0x12: run length
        rec[5] = *(uint32_t*)(p + 0x1c);
        rec[6] = (uint32_t)(uintptr_t)shd;
        rec[7] = inputLayout10(p[0x18], vsi);
        if (!rec[7]) {   // the VS reads an input its pass's vertex format lacks (CreateInputLayout refused)
            static int told;
            if (told++ < 40) logf("pass-state record without input layout: %s pass %d vformat %u", *(const char**)shd ? *(const char**)shd : "(built-in)", i, p[0x18]);
        }
        if (shaderIndex == 0x3ff)
            logf("ours: %s pass %d word %08x stage %u run %u b1c %02x layout %08x", *(const char**)shd, i, word, p[0x14], p[0x15],
                 p[0x1c], rec[7]);
        rec[8] = vsi ? *(uint32_t*)rt(0x0215EFCC + 8 * vsi) : 0;
        rec[9] = psi ? *(uint32_t*)rt(0x0216B054 + 8 * psi) : 0;
        rec[10] = s0 ? *(uint32_t*)rt(0x0216AFD0 + 8 * s0) : 0;
        rec[11] = s1 ? *(uint32_t*)rt(0x0216F0DC + 8 * s1) : 0;
        rec[12] = s2 ? *(uint32_t*)rt(0x0216F160 + 8 * s2) : 0;
        rec[13] = s3 ? *(uint32_t*)rt(0x0216F058 + 8 * s3) : 0;
        *(uint16_t*)(p + 0x16) = n;
        count = n + 1;
    }
    if (seen && fresh) logf("late link: shader %p gained %d staged passes", shd, fresh);
    if (seen) return true;
    g_linked.insert(shd);
    if (g_linked.size() % 50 == 0) logf("linked %u shaders (records %u)", (unsigned)g_linked.size(), *(uint16_t*)rt(0x02173A66));
    return true;
}

typedef void(__cdecl* LinkAllFn)();
static LinkAllFn g_linkAll;
static void __cdecl hkLinkAll()
{
    typedef void(__cdecl * SortFn)(uint32_t arr, uint32_t n);
    ((SortFn)rt(0x004703A0))((uint32_t)rt(0x0215CFC4), *(uint32_t*)rt(0x0215CFC0));
    // The shaders the engine builds in code have no name (shd+0): they are used by pointer, never looked up, so
    // they link now -- unlinked, the world drew black or white and the loading screen never closed (2026-10-13).
    unsigned builtin = 0;
    for (uint32_t k = 0, n = *(uint32_t*)rt(0x0215CFC0); k < n; ++k) {
        uint8_t* s = *(uint8_t**)rt(0x0215CFC8 + 8 * k);
        if (!*(const char**)s) { linkShader(s); ++builtin; }
    }
    logf("shader linking deferred to first use (%u shaders registered, %u built-in linked now: %u pass-state records)",
         *(uint32_t*)rt(0x0215CFC0), builtin, *(uint16_t*)rt(0x02173A66));
}

// create: generate it if new (the material transcode asks first, so a 1.0 shader that does not translate keeps its
// material on the other tiers); lookups only find ones already made, whatever the name's length
static uint8_t* shader10(const char* name, bool create)
{
    std::lock_guard<std::mutex> lock(g_sh10Lock);
    if (auto it = g_sh10.find(name); it != g_sh10.end()) return it->second;
    if (!create) return nullptr;
    uint8_t*& out = g_sh10[name];
    out = nullptr;
    gen30::Generated g;
    g_compiling10 = name;
    std::function<uint32_t(uint32_t)> skinTarget;
    if (g_cfg.skinTr10 && g_cfg.skinLayout10 && !g_vf30.empty()) skinTarget = [](uint32_t vf) { return render::skinTarget30(g_render, vf); };
    gen30::generate(g_render.shaders10, read10s, g_vf10, std::string(name + 1), compile10, g, skinTarget, &g_vf30);
    for (auto& pr : g.problems) logf("shaders10 %s: %s", name, pr.c_str());
    if (g.shd.empty() || !g.problems.empty()) return nullptr;
    std::map<uint32_t, uint32_t> engineId, vsIndex, psIndex;   // generator id -> engine id; engine id -> index
    for (auto& p : g.programs) {
        uint32_t id = 0, index = 0;
        if (!registerProgram10(p, id, index)) return nullptr;
        engineId[p.id] = id;
        (p.ps ? psIndex : vsIndex)[id] = index;
    }
    std::string shd = g.shd;
    for (int i = 0; i < (uint8_t)shd[0x19]; ++i)
        for (size_t off : {(size_t)0x28, (size_t)0x30}) {
            uint32_t& pid = *(uint32_t*)&shd[0xE0 + 0x50 * i + off];
            if (pid) pid = engineId.at(pid);
        }
    uint8_t* mem = (uint8_t*)_aligned_malloc(shd.size(), 16);   // shaders live for the session, as the engine's do
    memcpy(mem, shd.data(), shd.size());
    char* nm = _strdup(name);
    *(uint32_t*)mem = (uint32_t)(uintptr_t)nm;
    *(uint32_t*)(mem + 4) = (uint32_t)shd.size();
    finalizeShd10(mem);
    logf("shaders10 %s: %u passes, %u programs registered (pass-state records in use: %u)", name, (unsigned)mem[0x19],
         (unsigned)g.programs.size(), *(uint16_t*)rt(0x02173A66));
    out = mem;
    return out;
}

typedef uint8_t*(__cdecl* ShaderByNameFn)(const char* name);
static ShaderByNameFn g_shaderByName;
static uint8_t* __cdecl hkShaderByName(const char* name)
{
    uint8_t* s = nullptr;
    // 'x'<1.0 shader>: hash-named ones (33 chars) are made on demand; named ones ('xfades') exist only if the
    // transcode made them -- a name it could not translate reached the engine, whose loader task waited on it
    // forever: the respawn after Edna's dialog never finished and control never came back (2026-10-13)
    if (name && name[0] == 'x') s = shader10(name, strlen(name) == 33);
    bool ours = s != nullptr;
    if (!s) s = g_shaderByName(name);
    if (s && (ours || g_cfg.lazyLink10)) linkShader(s);   // with the boot linker on, 3.0's are linked already
    return s;
}

// shaders10's hooks (H1), all or none; the record lock is ready before the first can run (B-01).
static bool installShaders(const di1::Config& c)
{
    if (!c.shaders10) return true;
    InitializeCriticalSection(&g_recCs);
    std::vector<core::HookSpec> shaders = {
        {"shader by name", 0x0045C820, "8B44240450E8????????8B0D", (void*)&hkShaderByName, (void**)&g_shaderByName}};
    if (c.lazyLink10)
        shaders.push_back({"shader linker (deferred)", 0x00472050, "83EC50A1????????535657", (void*)&hkLinkAll,
                           (void**)&g_linkAll});
    shaders.push_back({"pass-state clone (locked)", 0x0045C5C0, "83EC4856578B7C2454", (void*)&hkCloneRec,
                       (void**)&g_cloneRec});
    return installFeature("shaders10", shaders);
}

// vformat.vtxf: 3.0's formats plus every 1.0 format 3.0 lacks, so 1.0 vertex buffers draw as they are (their
// trailing vertex colour in a stream of its own: gen30::mergeVtxf). Read once at boot, before any asset transcode.
static std::string mergeVtxf(const std::string& t30, const std::string& t10)
{
    int n = 0;
    std::string m = gen30::mergeVtxf(t30, t10, n, g_tails10);
    g_vf30 = gen30::vformats(t30);
    logf("shaders10: vformat.vtxf + %d 1.0 formats, %u with their vertex colour in its own stream", n, (unsigned)g_tails10.size());
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

// renderdoc10: RenderDoc in-process before the renderer creates its device, so its hooks see the device.
static void loadRenderDoc(const di1::Config& c)
{
    if (c.renderdoc10 <= 0) return;
    HMODULE rd = LoadLibraryExA("C:\\Program Files\\RenderDoc\\x86\\renderdoc.dll", nullptr,
                                LOAD_WITH_ALTERED_SEARCH_PATH);
    auto get = rd ? (pRENDERDOC_GetAPI)GetProcAddress(rd, "RENDERDOC_GetAPI") : nullptr;
    if (get && get(eRENDERDOC_API_Version_1_1_2, (void**)&g_rdoc) == 1) {
        g_rdoc->SetCaptureFilePathTemplate((g_gameDir + "\\di3cap").c_str());
        logf("renderdoc: loaded; capture %d s after the first mission tick (missiontrace)", c.renderdoc10);
    } else {
        logf("renderdoc: not loaded (%lu)", GetLastError());
    }
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

constexpr size_t kOctSlack = 4096;   // octx arena slack: instanced 1.0 SubNetworks must fit their arenas (octx.hpp)

// The render and Meridian tables beside the DLL, and the octx switches the transcode runs with.
static void loadTables(const di1::Config& c, const char* dir)
{
    g_renderOk = g_render.load(slurpFile(dir, "di1compat_render.txt"));
    logf("render table: %s (%u shaders, %u donors, %u materials, %u vertex plans)", g_renderOk ? "ok" : "MISSING",
         (unsigned)g_render.shaders.size(), (unsigned)g_render.donors.size(), (unsigned)g_render.matindex.size(),
         (unsigned)g_render.plans.size());
    octx::g_slack = kOctSlack;
    octx::g_variantGroups = c.variant10;
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

// shaders10: the HLSL compiler, the DXBC cache folder and 1.0's vertex formats, before any hook.
static void prepareShaders10(const di1::Config& c, const char* dir)
{
    if (!c.shaders10) return;
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    g_d3dCompile = dc ? (D3DCompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
    g_shaderCache = std::string(dir) + "\\di1compat_shaders";
    CreateDirectoryA(g_shaderCache.c_str(), nullptr);
    std::string v10;
    if (read10s("shaders/vformat.vtxf", v10)) g_vf10 = gen30::vformats(v10);
    logf("shaders10: compiler %s, %u 1.0 vertex formats, %u 3.0 globals, cache %s", g_d3dCompile ? "ok" : "MISSING",
         (unsigned)g_vf10.size(), (unsigned)g_render.shaders10.globals.size(), g_shaderCache.c_str());
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
    if (slash) strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "di1compat.log");
    {   // the last 5 runs' logs stay as di1compat.1.log (newest) .. di1compat.5.log: a launch must not erase the evidence
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
    loadRenderDoc(g_cfg);
    _setmaxstdio(8192);   // every 1.0 archive used stays open (miniz holds a FILE*); the CRT default is 512
    indexResident10(g_cfg);
    loadTables(g_cfg, dir);
    prepareShaders10(g_cfg, dir);

    // the modules' hooks in install order (B3): a hook is live once enabled
    if (!startHooks()) return;
    if (!installAssetOpen(g_cfg)) return;
    installGameDb(g_cfg);
    installEcon(g_cfg);
    installExpPkg(g_cfg);
    installCurrency(g_cfg);
    installExpDef(g_cfg);
    installPlaysetSlot(g_cfg);
    trace::spawn::install({g_cfg.trace, g_cfg.hidePs});
    installShaders(g_cfg);
    trace::mission::install(missionTraceOf(g_cfg));
    installChoStart(g_cfg);
    trace::breaktrace::install({g_cfg.breakTrace});
    trace::teardown::install({g_cfg.teardownTrace});
    installMerRegistry(g_cfg);
    trace::locktrace::install({g_cfg.lockTrace10});
    trace::opacity::install({g_cfg.opacityLog10});
    trace::fade::install({g_cfg.fadeProbe10});
    trace::loadprof::install({g_cfg.fpsLog10, g_cfg.loadProf10, g_gameDir});
    installAssetDefs(g_cfg);
    installCrt(g_cfg);
    installSx(g_cfg);
    npcrt::install(g_cfg);
    installUi(g_cfg);
    installAudio(g_cfg);
    vfxrt::install(g_cfg);
    gprt::install(g_cfg);
    bldrt::install(g_cfg);
    caprt::install(g_cfg);
    trace::spawntrace::install({g_cfg.spawnTrace10 && !g_cfg.trace});
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
