// di1compat: Pentane plugin that serves Disney Infinity 1.0 data to the 3.0 engine (research plan
// D:\DI-Extract\_research\PLAN.md, INF-1/INF-6). The 1.0 files are read in place from the 1.0 install's own
// zips and handed to the engine as Genie::MemFile streams through its asset open, FUN_0041cb10 (worlds,
// textures, Lua ... all go through it, archive members included).
// Log: <game dir>\di1compat.log (Pentane's own log function is not used: its calling convention is unverified).
// Config: <plugin dir>\di1compat.cfg, "key = value" lines; every key and its default in games/di1/config.hpp.
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
#include "games/di1/renderio.hpp"
#include "games/di1/transcode.hpp"
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
using core::watch;
using core::copyToMemFile;
using core::memFile;
using core::readStream;
using hash::fnv64;
using text::endsWith;
using text::norm;

static di1::Config g_cfg;   // the cfg file's values (games/di1/config.hpp): set once in Pentane_Main, before any hook

// ---- VFS1 v0: the 1.0 install's own zips, read in place (never copied or rewritten) ----
// An engine path <dir>/<name>/<rest> lives in assets\<dir>\<name>.zip as member <rest> (the layout extract.py
// unpacks), and deeper or shallower splits are tried too (games/di1/archives.hpp). Archives open lazily; miniz is not
// thread-safe.
static std::mutex g_zipLock;
static std::map<std::string, std::unique_ptr<mz_zip_archive>> g_zips;   // guarded by g_zipLock; nullptr = no such zip

// The 1.0 archive rel, opened on first use and kept open for the session (_setmaxstdio in Pentane_Main). A failed
// open is kept too (A-12). Caller holds g_zipLock.
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
    di1::WorldRender w;
    w.translate = g_cfg.shaders10 && g_cfg.translate10;
    w.skinLayout = g_cfg.skinLayout10;
    w.skinTr = g_cfg.skinTr10;
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

// The plugin's .oct steps (games/di1/octsteps.hpp): its cfg switches, its render, the 3.0 install, the engine's table
static di1::OctSteps octSteps10()
{
    di1::OctSteps o;
    o.render = transcodeAssetRender;
    o.navmesh = g_cfg.navmesh10;
    o.cameras = g_cfg.camWorld10;
    o.realmFile = g_cfg.realmFile10;
    o.clips = g_cfg.clip10;
    if (g_cfg.clip10Motions) o.installed30 = has30;
    o.clipIds = g_cfg.clip10Ids;
    o.layout = g_cfg.choLayout10;
    o.time = g_cfg.choTime10;
    o.anim = g_cfg.choAnim10;
    o.scene = g_cfg.choScene10;
    o.curves = g_cfg.curves10;
    if (g_cfg.surfaces10) o.surfaceTable = surfaceTable30;
    o.constSections = g_cfg.choConst10;
    return o;
}

// The shaders a particle package's materials are served on: its .mtb (beside it, or the overlay member the alias
// names) through the stand-alone bundle transcode (the .mtb case of transcode(), same donors), kept in the session
// so the engine's .mtb request right after (log: particledata then mtb, same frame) is served the same bytes.
// Per 1.0 material: (1.0 shader, served shader); empty if the bundle is missing or does not transcode.
static std::vector<std::pair<std::string, std::string>> particleShaders(const std::string& pd, std::string& note)
{
    if (!g_renderOk) return {};
    std::string mp = pd.substr(0, pd.size() - 13) + ".mtb", over;
    {
        std::lock_guard<std::mutex> lock(g_sessLock);
        auto o = g_over10.find(mp);
        if (o != g_over10.end()) over = o->second;
    }
    std::string mtb;
    if (!read10s(over.empty() ? mp : over, mtb) || mtb.size() < 4 || memcmp(mtb.data(), "BNDL", 4)) return {};
    render::IO io = di1::bundleIO(read10s, read30x);
    render::Result r;
    if (!render::transcodeBundle(g_render, io, mtb, "3:" + mp.substr(0, mp.find('/') + 1), r)) {
        note += "channels: " + mp + " did not transcode ";
        return {};
    }
    note += "(" + mp + ": real " + std::to_string(r.tiers["real"]) + " same-sh " + std::to_string(r.tiers["same-sh"]) + " generic " +
            std::to_string(r.tiers["generic"]) + ", served from the session) ";
    std::lock_guard<std::mutex> lock(g_sessLock);
    g_served.emplace(mp, r.mtb);
    return r.shaders;
}

// currency10: does a 1.0 expendable's DNA ('FastCoin_Spark', 'X.dnax', a path) carry FastCoin.IsSpark? Memoised.
static bool isSpark10(const std::string& dnaFile)
{
    static std::mutex m;
    static std::map<std::string, bool> memo;
    std::string p = dnaFile;
    for (char& c : p) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    if (p.empty() || p == "none") return false;
    if (p.find('/') == std::string::npos) p = "expendables_data/dna/" + p;
    if (!endsWith(p.c_str(), ".dnax")) p += ".dnax";
    {
        std::lock_guard<std::mutex> l(m);
        if (auto it = memo.find(p); it != memo.end()) return it->second;
    }
    bool spark = false;
    std::string b;
    if (read10s(p, b)) {
        try {
            auto G = luachunk::eval(b);
            const luachunk::Value* d = G.count("DNA") ? &G["DNA"] : nullptr;
            const luachunk::Value* ag = d ? d->get("AgentGadgets") : nullptr;
            const luachunk::Value* fc = ag ? ag->get("FastCoin") : nullptr;
            const luachunk::Value* is = fc ? fc->get("IsSpark") : nullptr;
            spark = is && is->t == luachunk::Value::BOOL && is->b;
        } catch (std::exception&) {}
    }
    std::lock_guard<std::mutex> l(m);
    return memo[p] = spark;
}

// The cfg keys the transcode dispatch reads (games/di1/transcode.hpp)
static di1::TranscodeOptions transcodeOptions10()
{
    di1::TranscodeOptions o;
    o.challenge = g_cfg.challenge10;
    o.behaviour = g_cfg.behav10;
    o.events = g_cfg.events10;
    o.progress = g_cfg.progress10;
    o.text = g_cfg.text10;
    o.banm = g_cfg.banm10;
    o.psdna = g_cfg.psdna30;
    o.vfx = g_cfg.vfx10;
    o.rewards = g_cfg.rewards10;
    o.sx = g_cfg.sx10;
    o.bmx = g_cfg.bmx10;
    o.btx = g_cfg.btx10;
    return o;
}

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

// -> codec name if out holds transcoded bytes, nullptr to serve the 1.0 bytes unchanged (games/di1/transcode.hpp).
// mtc: for a .mer, also the .mtc built from the same transcoded graph (the engine asks for the .mtc right after,
// 48/48 pairs in a load: serving it from the session saves reading and transcoding the .mer a second time)
static const char* transcode(const char* path, const void* data, size_t n, std::string& out, std::string& note, std::string* mtc = nullptr)
{
    return di1::transcode(path, data, n, out, note, mtc, transcodeOptions10(), transcodeIO10());
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

// Trace only: the actor registry's "does <name> exist" (FUN_00751750, thiscall, ret 4), which the playset avatar
// choice FUN_013a4250 asks for <avatar> + avatarSuffix, then <avatar>, before taking FallbackAvatar.
typedef uint8_t(__fastcall* ActorExistsFn)(void* self, void* edx, const char** name);
static ActorExistsFn g_actorExists;
static uint8_t __fastcall hkActorExists(void* self, void*, const char** name)
{
    uint8_t r = g_actorExists(self, nullptr, name);
    char n[200];
    safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    if (g_cfg.hidePs && endsWith(n, "_PS") && !strncmp(n, "AV_", 3)) r = 0;
    logf("actor exists? %s -> %u", n, r);
    return r;
}

// Trace only: the playset flow's figure check FUN_00ddae30(player, sku, int* failCode, a4, a5) (thiscall, ret 0x14)
// and the figure's playset string Gateway[sku].playset, FUN_00435080(out, sku) (ret 8).
typedef uint8_t(__fastcall* FigureOkFn)(void* self, void* edx, uint32_t player, int sku, int* code, uint32_t a4, uint32_t a5);
static FigureOkFn g_figureOk;
static uint8_t __fastcall hkFigureOk(void* self, void*, uint32_t player, int sku, int* code, uint32_t a4, uint32_t a5)
{
    int mine = -1;
    uint8_t r = g_figureOk(self, nullptr, player, sku, code ? code : &mine, a4, a5);
    logf("figure check player %u sku %d (a4 %u a5 %u) -> %u code %d", player, sku, a4 & 0xff, a5 & 0xff, r, code ? *code : mine);
    return r;
}
// ... the reader item list the playset flow fills its figure slots from (FUN_00427330(out) -> count, ret 4; items
// 0x50 B from out+4: +0 type, +4 sku) and its per-item gate FUN_00deb610(type, sku, a3, a4, a5) (ret 0x14); logged
// on change
typedef int(__fastcall* ReaderListFn)(void* self, void* edx, int* out);
static ReaderListFn g_readerList;
static int __fastcall hkReaderList(void* self, void*, int* out)
{
    static std::string last;
    int n = g_readerList(self, nullptr, out);   // reader slot = this + 0x6c + index * 0x260, its toy id at +0x168
    std::string s = std::to_string(n) + ":";
    for (int i = 0; i < n && i < 12; ++i) s += " " + std::to_string(out[1 + i * 20]) + "/" + std::to_string(out[2 + i * 20]);
    if (s != last) { last = s; logf("reader list %s", s.c_str()); }
    return n;
}
// ... the playset flow's player figure slot (*(this + 0xc) + 0xc + player * 4, read by FUN_00df9fd0(out, player,
// flag), thiscall ret 0xc): the watchpoint shows who fills and who clears player 0's slot
typedef void*(__fastcall* IgpNameFn)(void* self, void* edx, void* out, uint32_t player, uint32_t flag);
static IgpNameFn g_igpName;
static void* __fastcall hkIgpName(void* self, void*, void* out, uint32_t player, uint32_t flag)
{
    if (self && *(char**)((char*)self + 0xc)) watch((uintptr_t)(*(char**)((char*)self + 0xc)) + 0xc);
    return g_igpName(self, nullptr, out, player, flag);
}
typedef uint8_t(__fastcall* ReaderItemOkFn)(void* self, void* edx, uint32_t type, int sku, uint32_t a3, uint32_t a4, uint32_t a5);
static ReaderItemOkFn g_readerItemOk;
static uint8_t __fastcall hkReaderItemOk(void* self, void*, uint32_t type, int sku, uint32_t a3, uint32_t a4, uint32_t a5)
{
    static std::string last;
    uint8_t r = g_readerItemOk(self, nullptr, type, sku, a3, a4, a5);
    std::string s = std::to_string(type) + "/" + std::to_string(sku) + " -> " + std::to_string(r);
    if (s != last) { last = s; logf("reader item %s", s.c_str()); }
    return r;
}
typedef const char** (__fastcall* FigurePlaysetFn)(void* self, void* edx, const char** out, int sku);
static FigurePlaysetFn g_figurePlayset;
static const char** __fastcall hkFigurePlayset(void* self, void*, const char** out, int sku)
{
    const char** r = g_figurePlayset(self, nullptr, out, sku);
    char s[200];
    safeCopy(s, sizeof(s), out && *out ? *out : "(null)");
    logf("Gateway[%d].playset = '%s'", sku, s);
    return r;
}

// Trace only: an entity instancing one of its assets' SubNetworks (FUN_007734f0(out, const char** subName, a3),
// thiscall ret 0xc; an empty name = the root SubNetwork).
typedef void*(__fastcall* AssetInstFn)(void* self, void* edx, void* out, const char** name, uint32_t a3);
static AssetInstFn g_assetInst;
static void* __fastcall hkAssetInst(void* self, void*, void* out, const char** name, uint32_t a3)
{
    char n[200];
    safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    logf("instance asset '%s' of entity %p", n, self);
    return g_assetInst(self, nullptr, out, name, a3);
}

// Diagnostic guard (trace): SubNetwork instancing FUN_004ff0e0(param_1, root, instanceNo) (thiscall on the template,
// ret 0xc). A template whose root handle (+0xc) no longer resolves faults in FUN_004f5900 (NULL+8, in-game
// 2026-10-12, instancing a 1.0 prop's 'Anchor'); log it and return 0 (FUN_007734f0 handles a failed instance).
typedef int(__fastcall* InstFn)(void* self, void* edx, uint32_t p1, uint32_t root, int no);
static InstFn g_inst;
static uint32_t g_instHeaders;   // header count of the template being instanced (trace)
static int __fastcall hkInst(void* self, void*, uint32_t p1, uint32_t root, int no)
{
    typedef void*(__fastcall * ResolveFn)(void* handleField, void* edx);
    void* r = ((ResolveFn)rt(0x004B46D0))((char*)self + 0xc, nullptr);
    if (!r) {
        uint32_t* t = (uint32_t*)self;
        logf("instancing template %p with a dead root handle %08X (block %08X, headers %u, conv %u, procs %u): skipped",
             self, t[3], t[0x4c / 4], t[0x5c / 4], t[100 / 4], t[0x60 / 4]);
        return 0;
    }
    g_instHeaders = ((uint32_t*)self)[0x5c / 4];
    return g_inst(self, nullptr, p1, root, no);
}

// Trace: header creation FUN_004fae90(out, name, parent, a4, a5) (thiscall on the allocator, ret 0x14). Logs the
// adapter headers ('As...') made at runtime, with the creator's stack.
typedef void*(__fastcall* HdrMakeFn)(void* self, void* edx, void* out, uint32_t name, uint32_t parent, uint32_t a4, uint32_t a5);
static HdrMakeFn g_hdrMake;
static volatile LONG g_hdrMakeLogged;
static void* __fastcall hkHdrMake(void* self, void*, void* out, uint32_t name, uint32_t parent, uint32_t a4, uint32_t a5)
{
    char n[80] = "";
    __try { if (name) safeCopy(n, sizeof(n), (const char*)(name + 0xc)); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (n[0] == 'A' && n[1] == 's' && n[2] >= 'A' && n[2] <= 'Z' && InterlockedIncrement(&g_hdrMakeLogged) <= 6) {
        char line[700];
        int len = sprintf_s(line, "make header '%s' (allocator %p, parent %08X, %u %u), stack:", n, self, parent, a4, a5);
        stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 20);
        logf("%s", line);
    }
    return g_hdrMake(self, nullptr, out, name, parent, a4, a5);
}

// Trace: the spawn lookup StartPositions FUN_013a85d0(player, zone, out xform, out, flags) (thiscall, ret 0x14):
// 0 = no registered start (the caller then uses the origin); out[4..6] = the translation. And the
// PlayerStartPositionNode factory (vtable slot 14, FUN_014884f0, ret 4), to see whether a graph builds one.
typedef uint32_t(__fastcall* StartLookupFn)(void* self, void* edx, uint32_t player, uint32_t zone, float* out, uint32_t* o2, uint32_t flags);
static StartLookupFn g_startLookup;
static volatile LONG g_startLogged;
static uint32_t __fastcall hkStartLookup(void* self, void*, uint32_t player, uint32_t zone, float* out, uint32_t* o2, uint32_t flags)
{
    uint32_t r = g_startLookup(self, nullptr, player, zone, out, o2, flags);
    if (InterlockedIncrement(&g_startLogged) <= 20)
        logf("start lookup player %u zone %08X flags %X -> %u at (%.2f, %.2f, %.2f)", player, zone, flags, r,
             out ? out[4] : 0.f, out ? out[5] : 0.f, out ? out[6] : 0.f);
    return r;
}
// Trace: a start node's provider query FUN_0145f720(a1, player, zone, out, a5) (thiscall on the StartPositionBase,
// ret 0x14): matches when zone is 0 or its +0x88, the player's bit is in byte +0x85 and vfunc +0x68 fills the transform.
typedef uint8_t(__fastcall* StartQueryFn)(uint8_t* self, void* edx, uint32_t a1, int player, uint32_t zone, float* out, uint32_t a5);
static StartQueryFn g_startQuery;
static volatile LONG g_startQueryLogged;
static uint8_t __fastcall hkStartQuery(uint8_t* self, void*, uint32_t a1, int player, uint32_t zone, float* out, uint32_t a5)
{
    uint8_t r = g_startQuery(self, nullptr, a1, player, zone, out, a5);
    if (InterlockedIncrement(&g_startQueryLogged) <= 12)
        logf("start query node %p (zone %08X, players %02X) for player %d zone %08X -> %u at (%.2f, %.2f, %.2f)", self,
             *(uint32_t*)(self + 0x88), self[0x85], player, zone, r, out ? out[4] : 0.f, out ? out[5] : 0.f, out ? out[6] : 0.f);
    return r;
}

// Trace: the avatar spawn path. Event posts FUN_0068bbb0(name, a2, a3) (thiscall on the event manager, ret 0xc),
// first 3 of each spawn/zone name; the level loader FUN_00d00530 state (+0x2c) changes; its load-done check
// FUN_00ccc450 result changes; AvatarSpawning finalize FUN_00aa36f0 and avatar removal FUN_00a9ec70.
typedef void(__fastcall* PostFn)(void* self, void* edx, const char* name, uint32_t a2, uint32_t a3);
static PostFn g_post;
static std::mutex g_postMx;
static std::map<std::string, int> g_postSeen;
static void __fastcall hkPost(void* self, void*, const char* name, uint32_t a2, uint32_t a3)
{
    char n[64] = "";
    safeCopy(n, sizeof(n), name);
    if (strstr(n, "Zone") || strstr(n, "Avatar") || strstr(n, "Creation") || strstr(n, "Toy") || strstr(n, "Spawn") || strstr(n, "Level")) {
        std::lock_guard<std::mutex> l(g_postMx);
        if (++g_postSeen[n] <= 3) logf("event %s (%08X %08X)", n, a2, a3);
    }
    g_post(self, nullptr, name, a2, a3);
}
typedef void(__fastcall* ThisFn)(void* self, void* edx);
static ThisFn g_levelTick;
static int g_levelState = -1;
static void __fastcall hkLevelTick(void* self, void*)
{
    int st = *(int*)((char*)self + 0x2c);
    if (st != g_levelState) { logf("level loader %p state %d -> %d", self, g_levelState, st); g_levelState = st; }
    g_levelTick(self, nullptr);
    st = *(int*)((char*)self + 0x2c);
    if (st != g_levelState) { logf("level loader %p state -> %d", self, st); g_levelState = st; }
}
typedef uint8_t(__fastcall* BoolThisFn)(void* self, void* edx);
static BoolThisFn g_loadDone;
static int g_loadDoneLast = -1;
static uint8_t __fastcall hkLoadDone(void* self, void*)
{
    uint8_t r = g_loadDone(self, nullptr);
    if (r != g_loadDoneLast) {
        uint8_t* o = (uint8_t*)self;
        logf("level load-done check %p -> %u (+0x21 %u, +0xc %08X)", self, r, o[0x21], *(uint32_t*)(o + 0xc));
        g_loadDoneLast = r;
    }
    return r;
}
typedef void(__fastcall* OneArgFn)(void* self, void* edx, uint32_t a1);
static OneArgFn g_avFinal, g_avRemove;
static void __fastcall hkAvFinal(void* self, void*, uint32_t a1) { logf("AvatarSpawning finalize %p rec %08X", self, a1); g_avFinal(self, nullptr, a1); }
static void __fastcall hkAvRemove(void* self, void*, uint32_t a1)
{
    char line[400];
    int len = sprintf_s(line, "AvatarSpawning remove avatar of player %u, stack:", a1);
    stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 12);
    logf("%s", line);
    g_avRemove(self, nullptr, a1);
}

// Probe (teardowntrace): who frees a Layout-held DataNode's memory during its own dtor (in-game 2026-10-13: AV write
// 004EDDBB in FUN_004edda0 after the dtor wrote node+0 on the same page; in between run FUN_004b2780(0) on the node's
// hierarchy entry and, inside FUN_004edda0 between its read and its write of node+0xc when the handle has 0x80000,
// FUN_004edcc0 -> FUN_004e1a40 freeing that entry). Logs, for each Zoo::Layout dtor (FUN_00787bc0): every
// placement's +4 scene node and +0xc DataNode (vtable, RTTI, refcount word, page protection, handle, hierarchy entry
// words and their tagged targets); then, while that dtor runs, each Samurai::Node dtor (FUN_004ee5f0), entry release
// (FUN_004b2780) and entry free (FUN_004e1a40) with the page protection of the node being destroyed before / after.
// Reads only checked memory (CrabeLoader dumps every AV).
static bool tdReadable(uintptr_t p, size_t n) { return sys::readable((const void*)p, n); }
static uint32_t tdRd(uintptr_t p) { return tdReadable(p, 4) ? *(const uint32_t*)p : 0xDEADBEEF; }
static uint32_t tdProt(uintptr_t p)
{
    MEMORY_BASIC_INFORMATION mbi;
    return p >= 0x10000 && VirtualQuery((void*)p, &mbi, sizeof(mbi)) ? (mbi.State == MEM_COMMIT ? mbi.Protect : 0x10000000 | mbi.State) : 0;
}
static uint32_t tdStatic(uint32_t v) { return v >= g_base && v < g_base + 0x0209B000 ? v - (uint32_t)g_base + 0x400000u : v; }

// Probe (breaktrace, log only): do played choreographies animate, and where does a 1.0 destructible's break stop?
// - ClipProcessor evaluate FUN_00543f40 (thiscall, ret 0; time = FUN_00528a70, the processor's input clock, which it
//   reads itself first): the first 40 processors (clip node +0x3c, output table +0x48, block +0x40, time), then a
//   summary every 20000 evaluations: processors seen and how many of them had their time change (= animating).
// - Exploder munition impact FUN_0151e420(actor, ?, munition) (cdecl, the callback FUN_01521560 registers when the
//   Exploder gadget reads): first 40 with the munition's weapon FourCC (+0x68) and energy (+0x60).
// - Exploder explode FUN_0151e130 (thiscall on ExploderData, 1 arg, ret 4): first 40 with PieceFile (+0x1c), PieceName
//   (+0x20), PieceCount (+0x60) before / after (the piece spawners write the pieces found) and +0xf1 after (0x40 =
//   exploded).
typedef void(__fastcall* BtThisFn)(void* self, void* edx);
static BtThisFn g_clipEval;
static std::mutex g_btMx;
static std::unordered_map<void*, float> g_btFirst;   // processor -> first time seen
static std::unordered_set<void*> g_btMoving;
static volatile LONG g_btEvals;
struct BtClock { ULONGLONG born, changed; float first, last; };
static std::unordered_map<void*, BtClock> g_btClock;   // every processor's clock, for the 2-second report
static ULONGLONG g_btReport;
static void __fastcall hkClipEval(void* self, void*)
{
    float t = ((float(__fastcall*)(void*, void*))rt(0x00528A70))(self, nullptr);
    LONG n = InterlockedIncrement(&g_btEvals);
    {
        std::lock_guard<std::mutex> l(g_btMx);
        ULONGLONG now = GetTickCount64();
        auto it = g_btFirst.find(self);
        if (it == g_btFirst.end()) {
            g_btFirst[self] = t;
            if (g_btFirst.size() <= 40) {
                uint32_t* p = (uint32_t*)self;
                logf("breaktrace clip eval: processor %p clip %08X table %08X block %08X t=%.3f", self, p[0x3c / 4], p[0x48 / 4],
                     p[0x40 / 4], t);
            }
        } else if (it->second != t) g_btMoving.insert(self);
        auto c = g_btClock.emplace(self, BtClock{now, now, t, t}).first;
        if (c->second.last != t) { c->second.last = t; c->second.changed = now; }
        if (n % 20000 == 0) logf("breaktrace clip evals %ld: %zu processors, %zu with a moving clock", n, g_btFirst.size(), g_btMoving.size());
        // every 2 s: the processors born in the last 60 s (a cinematic's), the youngest first, each one's clock and how
        // long it has stood
        if (now - g_btReport >= 2000) {
            g_btReport = now;
            std::vector<std::pair<ULONGLONG, void*>> fresh;
            int moving = 0;
            for (auto& e : g_btClock)
                if (now - e.second.born <= 60000) {
                    fresh.emplace_back(e.second.born, e.first);
                    moving += e.second.first != e.second.last;
                }
            std::sort(fresh.begin(), fresh.end(), [](auto& a, auto& b) { return a.first > b.first; });
            char line[900] = "";
            size_t at = 0;
            for (size_t i = 0; i < fresh.size() && i < 8; ++i) {
                const BtClock& c = g_btClock[fresh[i].second];
                at += sprintf_s(line + at, sizeof(line) - at, " %p born -%llu ms %.2f->%.2f (still %llu ms)", fresh[i].second,
                                now - c.born, c.first, c.last, now - c.changed);
            }
            if (!fresh.empty()) logf("breaktrace clocks: %zu born in the last 60 s, %d moved; youngest:%s", fresh.size(), moving, line);
        }
    }
    g_clipEval(self, nullptr);
}
// - choreography instance audio-sync wait FUN_01442ee0 (thiscall on the instance's +0x8c object {dialog request id,
//   active, seconds left}; true = go) and start FUN_01449390 (thiscall on the instance; +0x38 its path, +0x1b4 flags:
//   0x20 audio sync, 0x80 started). The tick FUN_0144a4d0 calls the wait until it says go, then the start.
typedef char(__fastcall* BtReadyFn)(int32_t* sync, void* edx);
static BtReadyFn g_choReady;
static std::unordered_map<int32_t*, char> g_btReady;
static char __fastcall hkChoReady(int32_t* sync, void*)
{
    int32_t id = sync ? sync[0] : -1;
    int act = sync ? ((uint8_t*)sync)[4] : 0;
    float left = sync ? ((float*)sync)[2] : 0.f;
    char r = g_choReady(sync, nullptr);
    std::lock_guard<std::mutex> l(g_btMx);
    auto it = g_btReady.find(sync);
    if (it == g_btReady.end() || it->second != r) {
        g_btReady[sync] = r;
        logf("breaktrace cho audio-sync %p: request %d active %d, %.2f s left -> %s", sync, id, act, left, r ? "go" : "waiting");
    }
    return r;
}
// Play FUN_01445e70 (thiscall on a handle {instance, ?, network, ?}; true = handled): not started -> sets the pending
// flag 0x40000; started -> plays the network only when its +0x63 bit 0 is set, else does nothing. Logged for
// audio-synced instances (0x20) only.
typedef char(__fastcall* BtPlayFn)(uint32_t* h, void* edx);
static BtPlayFn g_btPlay;
static char __fastcall hkBtPlay(uint32_t* h, void*)
{
    uint8_t* inst = h ? (uint8_t*)(uintptr_t)h[0] : nullptr;
    uint32_t before = inst ? *(uint32_t*)(inst + 0x1b4) : 0;
    char r = g_btPlay(h, nullptr);
    if (inst && (before & 0x20)) {
        uint8_t* net = (uint8_t*)(uintptr_t)h[2];
        logf("breaktrace cho play %p flags %08X -> %08X, network %p ready byte %d, sub %08X -> %d", inst, before,
             *(uint32_t*)(inst + 0x1b4), net, net ? net[0x63] : -1, h[3], r);
    }
    return r;
}
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
// - choreography layout attachments FUN_00788280 (thiscall on the layout, 4 args, ret 0x10): after its gate
//   FUN_00785b40(a1, a2) (ret 8; 0 = nothing applied) it walks the attachments (+0x30, count +0x34; 0xc each, base
//   instance u16 +8, layer instance u16 +10) and binds one only when both instance objects (a3 +4: 0xc each, object
//   first) exist; the base's vtable +0x14 accepts the layer and FUN_00787830 (4 args, ret 0x10) attaches the clip.
//   Logged: each attachment's instances and objects as the applier sees them, the gate's result, every bind.
typedef int(__fastcall* BtApplyFn)(uint8_t* self, void* edx, uint32_t a1, uint32_t a2, uint8_t* inst, uint32_t a4);
static BtApplyFn g_layApply;
static volatile LONG g_btApplies;
static int __fastcall hkLayApply(uint8_t* self, void*, uint32_t a1, uint32_t a2, uint8_t* inst, uint32_t a4)
{
    if (InterlockedIncrement(&g_btApplies) <= 80 && tdReadable((uintptr_t)self, 0x38)) {
        uint32_t n = *(uint32_t*)(self + 0x34);
        uint8_t* att = *(uint8_t**)(self + 0x30);
        uint8_t* objs = inst && tdReadable((uintptr_t)inst, 8) ? *(uint8_t**)(inst + 4) : nullptr;
        char line[900] = "";
        size_t at = 0;
        for (uint32_t k = 0; k < n && k < 24 && att && tdReadable((uintptr_t)(att + 0xc * k), 0xc); ++k) {
            uint16_t b = *(uint16_t*)(att + 0xc * k + 8), l = *(uint16_t*)(att + 0xc * k + 10);
            const char* ob = objs && tdReadable((uintptr_t)(objs + 0xc * b), 4) ? (*(uint32_t*)(objs + 0xc * b) ? "" : "(null)") : "(?)";
            const char* ol = objs && tdReadable((uintptr_t)(objs + 0xc * l), 4) ? (*(uint32_t*)(objs + 0xc * l) ? "" : "(null)") : "(?)";
            char m0[40] = "", m1[40] = "";
            safeCopy(m0, sizeof(m0), *(const char**)(att + 0xc * k));
            safeCopy(m1, sizeof(m1), *(const char**)(att + 0xc * k + 4));
            at += sprintf_s(line + at, sizeof(line) - at, " %u:%u%s'%s'/%u%s'%s'", k, b, ob, m0, l, ol, m1);
            if (at > sizeof(line) - 80) break;
        }
        logf("breaktrace layout apply %p: %u attachments, %u instances, args %08X %08X %p %08X:%s", self, n,
             *(uint32_t*)(self + 0x24), a1, a2, inst, a4, line);
    }
    return g_layApply(self, nullptr, a1, a2, inst, a4);
}
typedef int(__fastcall* BtGateFn)(void* self, void* edx, uint32_t a1, uint32_t a2);
static BtGateFn g_layGate;
static volatile LONG g_btGates;
static int __fastcall hkLayGate(void* self, void*, uint32_t a1, uint32_t a2)
{
    int r = g_layGate(self, nullptr, a1, a2);
    if (InterlockedIncrement(&g_btGates) <= 80) logf("breaktrace layout gate (%08X, %08X) -> %08X", a1, a2, r);
    return r;
}
// - clip setup FUN_00552fc0 (thiscall on a clip evaluator, 3 args, ret 0xc): takes a clip (a1) and its block's
//   descriptor (a2, u16 [1] = outputs); with a connection table at +0x44 (motion clips) FUN_005329b0 reads the outputs
//   through it, else FUN_00532050 (scene clips). Logged: evaluator, clip, outputs, table.
typedef void(__fastcall* BtClipSetFn)(uint8_t* self, void* edx, uint32_t clip, uint16_t* desc, uint32_t a3);
static BtClipSetFn g_clipSet;
static volatile LONG g_btClipSets;
// After the set-up ran: how many output ids of the descriptor's sampled / constant tables reach a node. FUN_00532050
// maps an id through the port table at +0x1c (+0x10 u16 per port, +0x14 port count, +0x18 mask; 0xffff = none; no
// table: the id itself) to a local, applied if < the local count at +0x34 (nodes at +0x30).
static void btClipBound(uint8_t* self, const uint16_t* desc, char* out, size_t cap)
{
    out[0] = 0;
    if (!tdReadable((uintptr_t)self, 0x4c) || !desc || !tdReadable((uintptr_t)desc, 8)) return;
    uint32_t map = *(uint32_t*)(self + 0x1c), nLoc = *(uint32_t*)(self + 0x34), arr = 0, ports = 0, mask = 0xffff;
    if (map && tdReadable(map, 0x1c)) { arr = *(uint32_t*)(map + 0x10); ports = *(uint32_t*)(map + 0x14); mask = *(uint16_t*)(map + 0x18); }
    uint16_t n = desc[1];
    if (!tdReadable((uintptr_t)(desc + 3 + n), 2)) return;
    const uint16_t* tabs[2] = {desc + 3 + n, (const uint16_t*)((const uint8_t*)desc + desc[2 + n])};
    uint32_t tot[2] = {}, hit[2] = {};
    for (int t = 0; t < 2; ++t) {
        if (!tdReadable((uintptr_t)tabs[t], 2) || !tdReadable((uintptr_t)tabs[t], 2 + 2 * (size_t)tabs[t][0])) continue;
        tot[t] = tabs[t][0];
        for (uint32_t k = 0; k < tot[t]; ++k) {
            uint32_t id = tabs[t][1 + k] & 0x1fff, loc = id;
            if (map)
                loc = id < ports && tdReadable(arr + 2 * id, 2) && *(uint16_t*)(uintptr_t)(arr + 2 * id) != 0xffff
                          ? *(uint16_t*)(uintptr_t)(arr + 2 * id) & mask : 0xffffffffu;
            hit[t] += loc < nLoc;
        }
    }
    sprintf_s(out, cap, "; ids reaching a node: sampled %u/%u, constant %u/%u (port table %08X: %u ports, %u locals)", hit[0], tot[0],
              hit[1], tot[1], map, ports, nLoc);
}
static void __fastcall hkClipSet(uint8_t* self, void*, uint32_t clip, uint16_t* desc, uint32_t a3)
{
    uint32_t table = tdReadable((uintptr_t)self, 0x48) ? *(uint32_t*)(self + 0x44) : 0;
    g_clipSet(self, nullptr, clip, desc, a3);
    if (InterlockedIncrement(&g_btClipSets) <= 400) {
        char bound[160];
        btClipBound(self, desc, bound, sizeof(bound));
        logf("breaktrace clip set %p: clip %08X desc %p outputs %d table %08X a3 %08X%s", self, clip, desc,
             desc && tdReadable((uintptr_t)desc, 4) ? desc[1] : -1, table, a3, bound);
    }
}
// - music cue push FUN_0073c5c0 (thiscall on the music stack, 4 args, ret 0x10; name, priority, float, string)
typedef uint32_t(__fastcall* BtMusicFn)(void* self, void* edx, const char* name, int prio, float f, const char* s);
static BtMusicFn g_musicPush;
static uint32_t __fastcall hkMusicPush(void* self, void*, const char* name, int prio, float f, const char* s)
{
    char n[80] = "", t[80] = "";
    safeCopy(n, sizeof(n), name);
    safeCopy(t, sizeof(t), s);
    uint32_t r = g_musicPush(self, nullptr, name, prio, f, s);
    logf("breaktrace music push '%s' priority %d %.2f '%s' -> %08X", n, prio, f, t, r);
    return r;
}
typedef int(__fastcall* BtBindFn)(void* self, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
static BtBindFn g_layBind;
static volatile LONG g_btBinds;
static int __fastcall hkLayBind(void* self, void*, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    if (InterlockedIncrement(&g_btBinds) <= 300) logf("breaktrace layout bind %p (%08X %08X %08X %08X)", self, a1, a2, a3, a4);
    return g_layBind(self, nullptr, a1, a2, a3, a4);
}
typedef void(__cdecl* BtHitFn)(uint32_t actor, uint32_t a2, uint8_t* mun);
static BtHitFn g_munHit;
static volatile LONG g_btHits;
static void __cdecl hkMunHit(uint32_t actor, uint32_t a2, uint8_t* mun)
{
    if (InterlockedIncrement(&g_btHits) <= 40 && tdReadable((uintptr_t)mun, 0x6c)) {
        char cc[5] = {};
        memcpy(cc, mun + 0x68, 4);
        logf("breaktrace exploder hit: actor %08X weapon '%s' energy %.2f", actor, cc, *(float*)(mun + 0x60));
    }
    g_munHit(actor, a2, mun);
}
typedef void(__fastcall* BtOneFn)(void* self, void* edx, uint32_t a1);
static BtOneFn g_explode;
static volatile LONG g_btBooms;
static void __fastcall hkExplode(void* self, void*, uint32_t a1)
{
    uint8_t* d = (uint8_t*)self;
    int before = *(int*)(d + 0x60);
    g_explode(self, nullptr, a1);
    if (InterlockedIncrement(&g_btBooms) <= 40) {
        char file[100] = "", piece[40] = "";
        safeCopy(file, sizeof(file), *(const char**)(d + 0x1c));
        safeCopy(piece, sizeof(piece), *(const char**)(d + 0x20));
        logf("breaktrace explode: data %p actor %08X PieceFile '%s' PieceName '%s' pieces %d -> %d flags %02X", self,
             *(uint32_t*)d, file, piece, before, *(int*)(d + 0x60), d[0xf1]);
    }
}
// "ptr vt=<static> <rtti> ref=<word> prot=<protect>"
static void tdObj(char* out, size_t cap, uintptr_t p)
{
    if (!tdReadable(p, 8)) { sprintf_s(out, cap, "%08X (unreadable, prot %X)", (uint32_t)p, tdProt(p)); return; }
    uint32_t vt = tdRd(p), col = tdRd(vt - 4), td = tdRd(col + 12);
    char name[96] = "";
    if (td != 0xDEADBEEF) safeCopy(name, sizeof(name), (const char*)(uintptr_t)(td + 8));
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery((void*)p, &mbi, sizeof(mbi));
    sprintf_s(out, cap, "%08X vt=%08X %s ref=%08X prot=%X alloc=%08X", (uint32_t)p, tdStatic(vt), name, tdRd(p + 4), tdProt(p),
              (uint32_t)(uintptr_t)mbi.AllocationBase);
}
// hierarchy entry of a node handle (FUN_004b46d0 / FUN_004b2800, table at *(0x021B739C)); 0 if none
static uintptr_t tdEntry(uint32_t handle)
{
    uint32_t h = handle & 0x7ffff, t = tdRd(rt(0x021B739C)), e;
    if (h == 0x7ffff || t == 0xDEADBEEF) return 0;
    if (!(h & 0x40000)) e = tdRd(t + 0x18 + (h >> 12) * 4) + (h & 0xfff) * 0x14;
    else e = tdRd(tdRd(t + 0x130 + (((h >> 3) & 0x1fff7fff) >> 9) * 4) + ((h >> 3) & 0x1ff) * 4) + (h & 7) * 0x14;
    return tdReadable(e, 0x14) ? e : 0;
}
static void tdEntryLog(const char* what, uintptr_t e)
{
    if (!e) { logf("td   %s entry: none", what); return; }
    char a[200], b[200];
    tdObj(a, sizeof(a), tdRd(e) & ~1u);
    tdObj(b, sizeof(b), tdRd(e + 0x10) & ~1u);
    logf("td   %s entry %08X: [0]=%08X [4]=%08X [8]=%08X [c]=%08X [10]=%08X; [0]-> %s; [10]-> %s", what, (uint32_t)e, tdRd(e),
         tdRd(e + 4), tdRd(e + 8), tdRd(e + 12), tdRd(e + 0x10), a, b);
}
static __declspec(thread) int t_tdDepth;
static __declspec(thread) uintptr_t t_tdNode;
static __declspec(thread) uintptr_t t_tdOuter;   // placement 0 node of the Layout being destroyed
static __declspec(thread) int t_tdFrees;          // tracker / heap free lines logged in this Layout dtor
static void tdBlock(char* out, size_t cap, uintptr_t p);
static volatile LONG g_tdLayouts, g_tdLines, g_tdEarly;
static std::mutex g_tdWatchLock;
static std::map<uintptr_t, uintptr_t> g_tdWatch;   // placement +0xc node -> Layout (hkPlBind)
typedef uint32_t(__fastcall* TdDtorFn)(void* self, void* edx);
typedef void(__fastcall* EntryRelFn)(void* self, void* edx, uint32_t a1);
static TdDtorFn g_layoutDtor, g_nodeDtor;
static EntryRelFn g_entryRel;
static uint32_t __fastcall hkLayoutDtor(void* self, void*)
{
    bool on = InterlockedIncrement(&g_tdLayouts) <= 3000;
    if (on) {
        uintptr_t L = (uintptr_t)self;
        uint32_t arr = tdRd(L + 0x40), n = tdRd(L + 0x44);
        logf("td layout %08X dtor: %u placements (array %08X)", (uint32_t)L, n, arr);
        for (uint32_t k = 0; k < n && k < 8 && arr != 0xDEADBEEF; ++k) {
            uintptr_t el = arr + 0x18 * k;
            char s[200], d[200], nm[64] = "";
            tdObj(s, sizeof(s), tdRd(el + 4));
            tdObj(d, sizeof(d), tdRd(el + 0xc));
            safeCopy(nm, sizeof(nm), (const char*)(uintptr_t)tdRd(el + 8));
            uint32_t node = tdRd(el + 0xc);
            bool watched;
            { std::lock_guard<std::mutex> l(g_tdWatchLock); watched = g_tdWatch.count(node) != 0; }
            logf("td  placement %u '%s' flags %X: scene %s; node %s handle %08X (bound and alive: %s)", k, nm, tdRd(el + 0x10), s, d,
                 tdRd(node + 0xc), watched ? "yes" : "no/never bound");
            if (node && node != 0xDEADBEEF) {
                tdEntryLog("node", tdEntry(tdRd(node + 0xc)));
                char b[160], eb[160];
                tdBlock(b, sizeof(b), node);
                tdBlock(eb, sizeof(eb), tdEntry(tdRd(node + 0xc)));
                logf("td   node %s; its entry %s", b, eb);
            }
        }
    }
    uintptr_t outerPrev = t_tdOuter;
    t_tdFrees = 0;
    if (on) {
        ++t_tdDepth;
        uint32_t arr = tdRd((uintptr_t)self + 0x40), first = arr == 0xDEADBEEF ? 0 : tdRd(arr + 0xc);
        t_tdOuter = first == 0xDEADBEEF ? 0 : first;   // placement 0's node: tracker / heap frees are logged against it
    }
    uint32_t r = g_layoutDtor(self, nullptr);
    if (on) { --t_tdDepth; t_tdOuter = outerPrev; logf("td layout %08X dtor done", (uint32_t)(uintptr_t)self); }
    return r;
}
// Placement nodes seen bound (FUN_0077d9c0, thiscall on the Layout when an instance binds its placements): a node
// destroyed outside its Layout's dtor was released once too often somewhere; its stack names the releaser.
typedef uint32_t(__fastcall* PlBindFn)(void* self, void* edx, uint32_t a1, uint32_t a2, uint32_t a3);   // ret 0xc
static PlBindFn g_plBind;
static uint32_t __fastcall hkPlBind(void* self, void*, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uintptr_t L = (uintptr_t)self;
    uint32_t arr = tdRd(L + 0x40), n = tdRd(L + 0x44);
    for (uint32_t k = 0; k < n && k < 64 && arr != 0xDEADBEEF; ++k) {
        uint32_t node = tdRd(arr + 0x18 * k + 0xc);
        if (!node || node == 0xDEADBEEF) continue;
        std::lock_guard<std::mutex> l(g_tdWatchLock);
        if (g_tdWatch.emplace(node, L).second && InterlockedIncrement(&g_tdLines) <= 30000) {
            char o[200];
            tdObj(o, sizeof(o), node);
            logf("td bind layout %08X placement %u node %s", (uint32_t)L, k, o);
        }
    }
    return g_plBind(self, nullptr, a1, a2, a3);
}
static uint32_t __fastcall hkNodeDtor(void* self, void*)
{
    uintptr_t prev = t_tdNode, layout = 0;
    {
        std::lock_guard<std::mutex> l(g_tdWatchLock);
        auto it = g_tdWatch.find((uintptr_t)self);
        if (it != g_tdWatch.end()) { layout = it->second; g_tdWatch.erase(it); }
    }
    if (layout && t_tdDepth <= 0 && InterlockedIncrement(&g_tdEarly) <= 200) {
        char o[200], line[600];
        tdObj(o, sizeof(o), (uintptr_t)self);
        int len = sprintf_s(line, "td PLACEMENT NODE DESTROYED OUTSIDE ITS LAYOUT DTOR (layout %08X): %s, stack:", (uint32_t)layout, o);
        stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 24);
        logf("%s", line);
    }
    if (t_tdDepth > 0 && InterlockedIncrement(&g_tdLines) <= 30000) {
        char o[200], b[160];
        tdObj(o, sizeof(o), (uintptr_t)self);
        tdBlock(b, sizeof(b), (uintptr_t)self);
        logf("td  node dtor %s handle %08X %s", o, tdRd((uintptr_t)self + 0xc), b);
        tdEntryLog("own", tdEntry(tdRd((uintptr_t)self + 0xc)));
        t_tdNode = (uintptr_t)self;
    }
    uint32_t r = g_nodeDtor(self, nullptr);
    t_tdNode = prev;
    return r;
}
static void __fastcall hkEntryRel(void* self, void*, uint32_t a1)
{
    if (t_tdDepth <= 0 || !t_tdNode || InterlockedIncrement(&g_tdLines) > 30000) { g_entryRel(self, nullptr, a1); return; }
    uintptr_t e = (uintptr_t)self;
    char o[200];
    tdObj(o, sizeof(o), tdRd(e + 0x10) & ~1u);
    logf("td   entry release %08X (arg %X) [10]=%08X -> %s; node %08X page prot %X", (uint32_t)e, a1, tdRd(e + 0x10), o,
         (uint32_t)t_tdNode, tdProt(t_tdNode));
    g_entryRel(self, nullptr, a1);
    logf("td   entry release done; node %08X page prot %X", (uint32_t)t_tdNode, tdProt(t_tdNode));
}
// FUN_004e1a40 (thiscall on the entry table, arg = entry): frees a hierarchy entry; reached from the node dtor's
// FUN_004edda0 -> FUN_004edcc0 between its read and its faulting write of node+0xc
static EntryRelFn g_entryFree;
// The allocation-block tracker at *(0x021B73A0) (FUN_00416840 / FUN_00412050): table at +0x10, u16 entry count at
// table+2, entries at table+4, 0x10 bytes {count | flags (count = & 0x0fffffff), ?, size, base}. An object allocated
// inside a tracked block (a SubNetwork arena) counts once; FUN_00416840 decrements on its delete and frees the block
// (FUN_00409e10) at the last one. tdBlock: the tracked block holding p, "untracked" if none.
static void tdBlock(char* out, size_t cap, uintptr_t p)
{
    uint32_t tr = tdRd(rt(0x021B73A0)), tbl = tr == 0xDEADBEEF ? 0xDEADBEEF : tdRd(tr + 0x10);
    if (tbl == 0xDEADBEEF || !tbl || !tdReadable(tbl, 4)) { sprintf_s(out, cap, "blk(%08X)=no tracker", (uint32_t)p); return; }
    uint32_t n = tdRd(tbl) >> 16;
    if (!tdReadable(tbl + 4, (size_t)n * 0x10)) { sprintf_s(out, cap, "blk(%08X)=tracker unreadable", (uint32_t)p); return; }
    const uint32_t* e = (const uint32_t*)(uintptr_t)(tbl + 4);
    for (uint32_t i = 0; i < n; ++i, e += 4)
        if (e[3] <= p && p < e[3] + e[2]) {
            sprintf_s(out, cap, "blk(%08X)=[%08X +%X count %u flags %X]", (uint32_t)p, e[3], e[2], e[0] & 0x0fffffff, e[0] >> 28);
            return;
        }
    sprintf_s(out, cap, "blk(%08X)=untracked (%u blocks)", (uint32_t)p, n);
}
static void __fastcall hkEntryFree(void* self, void*, uint32_t entry)
{
    if (t_tdDepth <= 0 || !t_tdNode || InterlockedIncrement(&g_tdLines) > 30000) { g_entryFree(self, nullptr, entry); return; }
    char b[160];
    tdBlock(b, sizeof(b), t_tdOuter);
    tdEntryLog("freeing", tdReadable(entry, 0x14) ? entry : 0);
    logf("td   entry free %08X; node %08X page prot %X; placement node %s", entry, (uint32_t)t_tdNode, tdProt(t_tdNode), b);
    g_entryFree(self, nullptr, entry);
    tdBlock(b, sizeof(b), t_tdOuter);
    logf("td   entry free done; node %08X page prot %X; placement node %s page prot %X", (uint32_t)t_tdNode, tdProt(t_tdNode), b,
         tdProt(t_tdOuter));
}
// FUN_00416840 (thiscall on the tracker, ret 0xc): an object's delete counted against its block
typedef char(__fastcall* TrackFreeFn)(void* self, void* edx, uint32_t obj, uint32_t* base, int* remaining);
static TrackFreeFn g_trackFree;
static char __fastcall hkTrackFree(void* self, void*, uint32_t obj, uint32_t* base, int* remaining)
{
    if (t_tdDepth <= 0 || !t_tdOuter || ++t_tdFrees > 80 || InterlockedIncrement(&g_tdLines) > 30000) return g_trackFree(self, nullptr, obj, base, remaining);
    char o[200], b[160], pb[160];
    tdObj(o, sizeof(o), obj);
    tdBlock(b, sizeof(b), obj);
    char r = g_trackFree(self, nullptr, obj, base, remaining);
    tdBlock(pb, sizeof(pb), t_tdOuter);
    logf("td    tracker delete %s %s -> %d (block %08X, %d left); placement node %08X %s page prot %X", o, b, r, base ? *base : 0,
         remaining ? *remaining : -1, (uint32_t)t_tdOuter, pb, tdProt(t_tdOuter));
    return r;
}
// FUN_00409e10 (cdecl): a heap / block free
typedef void(__cdecl* HeapFreeFn)(uint32_t p);
static HeapFreeFn g_heapFree;
static void __cdecl hkHeapFree(uint32_t p)
{
    if (t_tdDepth <= 0 || !t_tdOuter || ++t_tdFrees > 80 || InterlockedIncrement(&g_tdLines) > 30000) { g_heapFree(p); return; }
    char b[160];
    tdBlock(b, sizeof(b), p);
    g_heapFree(p);
    logf("td    heap free %08X (was %s); placement node %08X (node - p = %X) page prot %X", p, b, (uint32_t)t_tdOuter,
         (uint32_t)(t_tdOuter - p), tdProt(t_tdOuter));
}

// Trace: the spawn choreography. FUN_00a9f080 asks the creation cho for 'Cho Controls' (FUN_014835c0, thiscall 2 args)
// and only then registers FUN_00a8b670 on its 'ToyCreation_PlaceToy' event; that callback sets the per-player ready
// flag (+0xd9) the finalize step waits for.
typedef uint8_t(__fastcall* ChoHasFn)(void* self, void* edx, uint32_t a1, const char* name);
static ChoHasFn g_choHas;
static volatile LONG g_choHasLogged;
static uint8_t __fastcall hkChoHas(void* self, void*, uint32_t a1, const char* name)
{
    uint8_t r = g_choHas(self, nullptr, a1, name);
    char n[64] = "";
    safeCopy(n, sizeof(n), name);
    if (!strcmp(n, "Cho Controls") && InterlockedIncrement(&g_choHasLogged) <= 6) logf("cho %p has '%s' (a1 %08X) -> %u", self, n, a1, r);
    return r;
}
typedef void(__fastcall* AvCreateFn)(void* self, void* edx, uint32_t player, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
static AvCreateFn g_avCreate;
static void __fastcall hkAvCreate(void* self, void*, uint32_t player, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    logf("AvatarSpawning create player %u (%08X %08X %08X %08X) ->", player, a2, a3, a4, a5);
    g_avCreate(self, nullptr, player, a2, a3, a4, a5);
    logf("AvatarSpawning create player %u returned", player);
}
// AsyncActorCreator (the queued spawn FUN_00752a90 makes when the actor's data is not resident): Execute (vtable 1,
// FUN_0075a290) builds it through FUN_00759ca0 (cdecl, 11 args, returns the out handle pointer) and calls the
// completion callback; log Execute (actor name at +0x6c).
static ThisFn g_aacExec;
static void __fastcall hkAacExec(void* self, void*)
{
    bool avatar = *(uintptr_t*)((char*)self + 0x78) == rt(0x00A9B3E0);   // its callback is the avatar spawn's
    if (avatar) logf("AsyncActorCreator %p execute (avatar spawn, mode %u)", self, *(uint32_t*)((char*)self + 0x18));
    g_aacExec(self, nullptr);
    if (avatar) logf("AsyncActorCreator %p execute returned", self);
}
// The avatar spawn's completion callback FUN_00a9b3e0(actor, record) (cdecl): sets record+0x28 = actor and the ready
// flag unless AvatarSpawning+0x88 is 0.
typedef void(__cdecl* AvSpawnedFn)(uint32_t actor, uint8_t* rec);
static AvSpawnedFn g_avSpawned;
static void __cdecl hkAvSpawned(uint32_t actor, uint8_t* rec)
{
    uint8_t* as = *(uint8_t**)rt(0x02212BC4);
    logf("avatar spawn callback actor %08X rec %p (AvatarSpawning %p +0x88 %08X, rec+0x31 %02X)", actor, rec, as,
         as ? *(uint32_t*)(as + 0x88) : 0, rec ? rec[0x31] : 0);
    g_avSpawned(actor, rec);
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
typedef uint32_t*(__fastcall* ChoMakeFn)(void* self, void* edx, uint32_t* out, const char* name, uint32_t a3, uint32_t a4, uint32_t a5);
static ChoMakeFn g_choMake;
static volatile LONG g_choMakeLogged;
static uint32_t* __fastcall hkChoMake(void* self, void*, uint32_t* out, const char* name, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t* r = g_choMake(self, nullptr, out, name, a3, a4, a5);
    char n[64] = "";
    safeCopy(n, sizeof(n), name);
    if ((strstr(n, "oy") || strstr(n, "pawn") || !n[0]) && InterlockedIncrement(&g_choMakeLogged) <= 20)
        logf("cho make '%s' -> %08X", n, r == out && out ? *out : 0xffffffff);
    return r;
}
typedef void(__cdecl* PlaceToyFn)(void* msg);
static PlaceToyFn g_placeToy;
static void __cdecl hkPlaceToy(void* msg) { logf("ToyCreation_PlaceToy callback (msg %p)", msg); g_placeToy(msg); }
// MissionContext update (FUN_0166ffb0, per frame per context): logs an IN_ context's activation state when it changes.
// Offsets from the context parser FUN_016748d0 and the update itself; +0x10 is the context's name string.
typedef void(__fastcall* MissionTickFn)(uint8_t* self, void* edx, float dt);
static MissionTickFn g_missionTick;
// test (fpslog10): FUN_0047aab0 runs once per frame on the main thread (FUN_0047f0a0, the frame's render submit) and
// spins while the render thread still holds the previous frame (+0x71f0 == 2). Call-to-call time = frame time; time
// inside = the main thread waiting on rendering. Mostly waiting -> render thread / GPU / present bound; hardly
// waiting at 33 ms -> the main thread's own work (simulation, graphs, AI) is the limit.
typedef void(__cdecl* FrameSyncFn)();
static FrameSyncFn g_frameSync;
static volatile DWORD g_frameTid;   // loadprof10: the thread that runs the frame sync
static volatile LONG g_frameNo;     // loadprof10: frames so far (served lines carry it)
static double qpcMs();

// loadprof10: which fiber of the resource scheduler (DAT_0213d4f8: +0x5c main, +0x60/+0x64 the two "Resource Loader
// Fiber"s, +0x68 the extra fiber = the loading screen's draw loop, LAB_00678730) runs on the frame thread, and the ms
// each had. A zone load is a ZoneManager::ZoneLoadTask on loader fiber 0 (full dumps of 2026-10-12) while main waits
// for it (FUN_0041ef40); the loading screen draws when a yield (FUN_0041eb60) finds its 44 ms period (0x01fa9740)
// over. Every switch goes through FUN_0041e480 (cdecl from, to, ctx; ret): only the scheduler's thread calls it.
static const char* const kFiberRole[5] = {"main", "load0", "load1", "screen", "other"};
static volatile LONG g_fiberRole = 4;   // read by the sampler
static double g_fiberMs[5], g_fiberSince;   // cumulative, frame thread only
static int fiberRole(void* f)
{
    const uint8_t* s = *(const uint8_t* const*)rt(0x0213D4F8);
    for (int i = 0; s && f && i < 4; ++i)
        if (*(void* const*)(s + 0x5c + 4 * i) == f) return i;
    return 4;
}
static double fiberMsNow(int role)   // role's cumulative ms, the running stretch included
{
    return g_fiberMs[role] + (g_fiberRole == role && g_fiberSince ? qpcMs() - g_fiberSince : 0);
}
typedef void(__cdecl* FiberSwitchFn)(void* from, void* to, int ctx);
static FiberSwitchFn g_fiberSwitch;
static void __cdecl hkFiberSwitch(void* from, void* to, int ctx)
{
    double now = qpcMs();
    if (g_fiberSince) g_fiberMs[g_fiberRole] += now - g_fiberSince;
    g_fiberSince = now;
    g_fiberRole = fiberRole(to);
    g_fiberSwitch(from, to, ctx);
}
// Per-fiber counters (fibers share their thread, so thread_local alone would mix them): the .oct load depth and the
// file waits of the fiber running now (GetCurrentFiber: on a thread that never became a fiber, a per-thread value).
struct FiberProf { void* fiber; int depth; int waits; double waitMs; };
static FiberProf& fiberProf()
{
    thread_local FiberProf s[8];
    void* f = GetCurrentFiber();
    for (auto& e : s) if (e.fiber == f) return e;
    for (auto& e : s) if (!e.depth) { e = {f, 0, 0, 0}; return e; }   // ponytail: 8 busy fibers per thread at most
    return s[0];
}
// loadprof10: time spent in the engine's file-done wait (FUN_0041fc10, thiscall on the completion, no args, ret:
// WaitForSingleObject(ev, 1) + a yield until the file thread signals; EAX passed through)
typedef uint32_t(__fastcall* FileWaitFn)(void* done, void* edx);
static FileWaitFn g_fileWait;
static uint32_t __fastcall hkFileWait(void* done, void*)
{
    double t = qpcMs();
    uint32_t r = g_fileWait(done, nullptr);
    FiberProf& p = fiberProf();   // after: the wait may switch fibers, but it returns on the one that waited
    p.waitMs += qpcMs() - t;
    ++p.waits;
    return r;
}
// loadprof10: one .oct asset load in the engine (FUN_00786d00, cdecl (out, char** path, a3, a4), ret; its
// Zoo::AssetFileTask reads, parses and instances the file). Logged per outermost call on a fiber: wall ms, frames,
// each scheduler fiber's ms meanwhile, the file waits.
typedef int*(__cdecl* OctLoadFn)(int* out, char** path, uint32_t a3, uint32_t a4);
static OctLoadFn g_octLoad;
static int* __cdecl hkOctLoad(int* out, char** path, uint32_t a3, uint32_t a4)
{
    FiberProf& fp = fiberProf();
    if (fp.depth++) {
        int* r = g_octLoad(out, path, a3, a4);
        --fiberProf().depth;
        return r;
    }
    const bool ft = GetCurrentThreadId() == g_frameTid;
    double t0 = qpcMs(), w0 = fp.waitMs, f[5];
    int n0 = fp.waits;
    LONG fr0 = g_frameNo;
    for (int i = 0; i < 5; ++i) f[i] = ft ? fiberMsNow(i) : 0;
    char p[200] = "";
    if (path) safeCopy(p, sizeof(p), *path);
    int* r = g_octLoad(out, path, a3, a4);
    FiberProf& fq = fiberProf();   // the same slot (same fiber), looked up again: never hold a slot across a switch
    --fq.depth;
    char fib[160] = "";
    if (ft)
        sprintf_s(fib, ", fibers main %.1f load0 %.1f load1 %.1f screen %.1f", fiberMsNow(0) - f[0], fiberMsNow(1) - f[1],
                  fiberMsNow(2) - f[2], fiberMsNow(3) - f[3]);
    logf("prof oct load %s: %.1f ms, frames %ld, on %s%s, file waits %d / %.1f ms", p, qpcMs() - t0, g_frameNo - fr0,
         ft ? kFiberRole[g_fiberRole] : "other-thread", fib, fq.waits - n0, fq.waitMs - w0);
    return r;
}

static void __cdecl hkFrameSync()
{
    g_frameTid = GetCurrentThreadId();
    InterlockedIncrement(&g_frameNo);
    static LARGE_INTEGER freq, last, second;
    static double sumFrame, maxFrame, sumWait;
    static int frames;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    g_frameSync();
    QueryPerformanceCounter(&b);
    if (last.QuadPart) {
        double frame = 1000.0 * (a.QuadPart - last.QuadPart) / freq.QuadPart;
        sumFrame += frame; maxFrame = (std::max)(maxFrame, frame); ++frames;
        sumWait += 1000.0 * (b.QuadPart - a.QuadPart) / freq.QuadPart;
    } else second = a;
    last = a;
    if (frames && a.QuadPart - second.QuadPart >= freq.QuadPart) {
        static double lastF[5];
        char fib[120] = "";
        if (g_fiberSince) {   // loadprof10: the scheduler's fibers over the last second
            double d[5];
            for (int i = 0; i < 5; ++i) { double v = fiberMsNow(i); d[i] = v - lastF[i]; lastF[i] = v; }
            sprintf_s(fib, "; fibers main %.0f load0 %.0f load1 %.0f screen %.0f ms", d[0], d[1], d[2], d[3]);
        }
        logf("fps %.1f: frame %.2f ms avg, %.2f max; main thread waits on rendering %.2f ms of it%s", 1000.0 * frames / sumFrame,
             sumFrame / frames, maxFrame, sumWait / frames, fib);
        sumFrame = maxFrame = sumWait = 0; frames = 0; second = a;
    }
}

// ---- loadprof10 (diagnostic): where a load's wall time goes ----
// hkOpen appends its own timings to the served / MISSING lines. A sampler thread (high-resolution waitable timer,
// ~1 kHz, so the system timer resolution the engine sees is untouched) suspends the frame thread, every thread that
// entered hkOpen and the 3 busiest other threads of the last second, copies EIP and the top of the stack, resumes,
// and only then attributes the sample (nothing is allocated or locked while a thread is suspended). Every 5 s:
// a summary per thread in the log and every (thread, leaf, caller, caller's caller) count in di1compat_prof.tsv.
// leaf = the static EIP in the exe, else 'm<module>!<nearest export>+<off>' (a waiting thread sits in ntdll's
// NtWaitFor... / NtDelayExecution); callers = the first two exe return addresses on the stack (static VAs).
static double qpcMs()
{
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}
static volatile LONG g_openTidN;
static volatile DWORD g_openTids[8];
static void noteOpenThread()
{
    thread_local bool seen;
    if (seen) return;
    seen = true;
    LONG i = InterlockedIncrement(&g_openTidN) - 1;
    if (i < 8) g_openTids[i] = GetCurrentThreadId();
}
// bounded by the stack region, so no fault can reach the vectored handlers (avHandler logs, CrabeLoader dumps)
// while the thread is suspended
static int copyStack(const uint32_t* sp, uint32_t* out, int max)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(sp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    size_t room = ((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)sp) / 4;
    int n = (int)(std::min)(room, (size_t)max);
    memcpy(out, sp, n * 4);
    return n;
}
static bool afterCallSafe(uint32_t v)
{
    __try { return afterCall(v); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static uintptr_t g_selfLo, g_selfHi;   // di1compat.dll's image
// a return address into di1compat.dll (right after a call instruction)
static bool ourRetSafe(uint32_t v)
{
    if (v < g_selfLo + 6 || v >= g_selfHi) return false;
    __try {
        const uint8_t* p = (const uint8_t*)(uintptr_t)v;
        return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) || (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) ||
               (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// 'module!export+0xoff' for a code address outside the exe (nearest named export at or below it); in di1compat.dll
// 'di1compat.dll+0x<rva>' (resolve with the build's .map)
static std::string symbolAt(uintptr_t a)
{
    HMODULE m = nullptr;
    char full[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) || !m ||
        !GetModuleFileNameA(m, full, MAX_PATH))
        return "?";
    std::string s = strrchr(full, '\\') ? strrchr(full, '\\') + 1 : full;
    if ((uintptr_t)m == g_selfLo) {
        char off[24];
        sprintf_s(off, "+0x%X", (unsigned)(a - g_selfLo));
        return s + off;
    }
    const uint8_t* b = (const uint8_t*)m;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    uint32_t rva = (uint32_t)(a - (uintptr_t)m), best = 0;
    const char* name = nullptr;
    if (dir.VirtualAddress && dir.Size) {
        const IMAGE_EXPORT_DIRECTORY* ex = (const IMAGE_EXPORT_DIRECTORY*)(b + dir.VirtualAddress);
        const uint32_t* fns = (const uint32_t*)(b + ex->AddressOfFunctions);
        const uint32_t* names = (const uint32_t*)(b + ex->AddressOfNames);
        const uint16_t* ords = (const uint16_t*)(b + ex->AddressOfNameOrdinals);
        for (DWORD i = 0; i < ex->NumberOfNames; ++i)
            if (uint32_t f = fns[ords[i]]; f <= rva && f > best) { best = f; name = (const char*)(b + names[i]); }
    }
    char off[48];
    sprintf_s(off, "+0x%X", rva - best);
    return s + "!" + (name ? name : "") + off;
}
static std::string g_profPath;
static DWORD WINAPI profThread(void*)
{
    HANDLE tm = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    logf("loadprof10: sampler up (%s timer)", tm ? "high-resolution" : "Sleep(1)");
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    {
        HMODULE me = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&profThread, &me);
        const uint8_t* b = (const uint8_t*)me;
        g_selfLo = (uintptr_t)me;
        g_selfHi = me ? g_selfLo + ((const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew))->OptionalHeader.SizeOfImage : 0;
    }
    std::map<DWORD, HANDLE> handles;
    std::map<DWORD, uint64_t> cpuLast;
    // {tid, outside the exe?, EIP, 4 callers, ours, fiber}: fiber = the scheduler fiber the frame thread ran (kFiberRole;
    // 5: another thread); ours = for a sample outside the exe, the outermost
    // di1compat.dll code on the stack below the first exe frame (the detour or plugin thread it runs for) as an RVA + 1
    // (0: none) -- time the plugin itself costs, its system calls included; inside the engine's own functions it is not
    std::map<std::array<uint32_t, 9>, uint32_t> counts;
    std::map<uint32_t, std::string> syms;
    std::map<DWORD, uint32_t> samples;
    std::vector<DWORD> busy;
    double nextCpu = qpcMs() + 1000, nextDump = qpcMs() + 5000;
    int window = 0;
    auto handle = [&](DWORD tid) {
        auto it = handles.find(tid);
        if (it != handles.end()) return it->second;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
        return handles[tid] = h;
    };
    auto role = [&](DWORD tid) {
        std::string r = tid == g_frameTid ? "frame" : "";
        for (LONG i = 0; i < g_openTidN && i < 8; ++i)
            if (g_openTids[i] == tid) r += r.empty() ? "open" : "+open";
        return r.empty() ? std::string("busy") : r;
    };
    auto leafName = [&](const std::array<uint32_t, 9>& k) {
        char b[16];
        if (!k[1]) { sprintf_s(b, "%08X", k[2]); return std::string(b); }
        auto it = syms.find(k[2]);
        if (it == syms.end()) it = syms.emplace(k[2], symbolAt(k[2])).first;
        return "m" + it->second;
    };
    for (;;) {
        if (tm) {
            LARGE_INTEGER due;
            due.QuadPart = -10000;   // 1 ms
            SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(tm, 50);
        } else Sleep(1);
        std::vector<DWORD> tids;
        if (DWORD f = g_frameTid) tids.push_back(f);
        for (LONG i = 0; i < g_openTidN && i < 8; ++i)
            if (DWORD o = g_openTids[i]; std::find(tids.begin(), tids.end(), o) == tids.end()) tids.push_back(o);
        for (DWORD t : busy)
            if (std::find(tids.begin(), tids.end(), t) == tids.end()) tids.push_back(t);
        for (DWORD tid : tids) {
            HANDLE h = handle(tid);
            if (!h || tid == self) continue;
            CONTEXT c{};
            c.ContextFlags = CONTEXT_CONTROL;
            uint32_t stk[384];
            int n = 0;
            if (SuspendThread(h) == (DWORD)-1) continue;
            bool ok = GetThreadContext(h, &c) != 0;
            if (ok) n = copyStack((const uint32_t*)(uintptr_t)c.Esp, stk, 384);
            LONG fib = tid == g_frameTid ? g_fiberRole : 5;   // the fiber it runs (frame thread); 5 = not tracked
            ResumeThread(h);
            if (!ok) continue;
            uintptr_t eip = c.Eip;
            bool inExe = eip >= g_base && eip < g_base + 0x2100000;
            std::array<uint32_t, 9> k{tid, inExe ? 0u : 1u, (uint32_t)(inExe ? eip - g_base + 0x400000 : eip), 0, 0, 0, 0, 0, (uint32_t)fib};
            if (!inExe && eip >= g_selfLo && eip < g_selfHi) k[7] = (uint32_t)(eip - g_selfLo) + 1;
            for (int i = 0, f = 3; i < n && f < 7; ++i) {
                if ((stk[i] & 0xff00ff00) && afterCallSafe(stk[i])) k[f++] = (uint32_t)(stk[i] - g_base + 0x400000);
                else if (!inExe && f == 3 && ourRetSafe(stk[i])) k[7] = (uint32_t)(stk[i] - g_selfLo) + 1;   // outermost wins
            }
            ++counts[k];
            ++samples[tid];
        }
        double now = qpcMs();
        if (now >= nextCpu) {   // CPU time per thread over the last second; the 3 busiest others are sampled next
            nextCpu = now + 1000;
            std::vector<std::pair<uint64_t, DWORD>> d;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te{sizeof(te)};
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
                HANDLE h = handle(te.th32ThreadID);
                FILETIME a, b, k, u;
                if (!h || !GetThreadTimes(h, &a, &b, &k, &u)) continue;
                uint64_t t = ((uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime);
                uint64_t& last = cpuLast[te.th32ThreadID];
                if (last) d.push_back({t - last, te.th32ThreadID});
                last = t;
            }
            CloseHandle(snap);
            std::sort(d.rbegin(), d.rend());
            busy.clear();
            std::string line;
            uint64_t total = 0;
            for (auto& [t, tid] : d) total += t;
            for (size_t i = 0; i < d.size() && i < 6; ++i) {
                std::string r = role(d[i].second);
                if (r == "busy" && busy.size() < 3) busy.push_back(d[i].second);
                char b[80];
                sprintf_s(b, " %s %u %.0f ms;", r.c_str(), (unsigned)d[i].second, d[i].first / 1e4);
                line += b;
            }
            logf("prof cpu last second: process %.0f ms over %zu threads; busiest:%s", total / 1e4, d.size(), line.c_str());
        }
        if (now >= nextDump) {
            nextDump = now + 5000;
            ++window;
            FILE* f = nullptr;
            if (!fopen_s(&f, g_profPath.c_str(), window == 1 ? "w" : "a") && f) {
                if (window == 1) fprintf(f, "window\ttid\trole\tleaf\tcaller\tcaller2\tcaller3\tcaller4\tours\tcount\n");
                for (auto& [k, v] : counts)   // role 'frame/load0': the frame thread running that scheduler fiber
                    fprintf(f, "%d\t%u\t%s%s%s\t%s\t%08X\t%08X\t%08X\t%08X\t%X\t%u\n", window, k[0], role(k[0]).c_str(), k[8] < 5 ? "/" : "",
                            k[8] < 5 ? kFiberRole[k[8]] : "", leafName(k).c_str(), k[3], k[4], k[5], k[6], k[7] ? k[7] - 1 : 0xffffffffu, v);
                fclose(f);
            }
            for (auto& [tid, n] : samples) {   // per thread: share in the exe and in each module
                std::map<std::string, uint32_t> mods;
                uint32_t ours = 0;
                for (auto& [k, v] : counts)
                    if (k[0] == tid) {
                        std::string s = k[1] ? leafName(k).substr(1) : "exe";
                        mods[s.substr(0, s.find_first_of("!+"))] += v;
                        if (k[7]) ours += v;
                    }
                std::string line;
                for (auto& [m, v] : mods) {
                    char b[MAX_PATH + 24];
                    sprintf_s(b, " %s %.0f%%", m.c_str(), 100.0 * v / n);
                    line += b;
                }
                logf("prof window %d %s %u: %u samples, %.1f%% in di1compat code (its calls included):%s", window, role(tid).c_str(),
                     (unsigned)tid, n, 100.0 * ours / n, line.c_str());
            }
            counts.clear();
            samples.clear();
        }
    }
}
// test (opacitylog10): FUN_0044ad80 -> bit i set = primitive i is "faded" (opacity param * object fade < 1) and the
// opaque G-buffer list skips it (FUN_00461020); logs our skipped primitives with the values that decided it
typedef uint32_t(__cdecl* OpacityMaskFn)(uint32_t n, float fade, uint32_t prims, uint32_t insts);
static OpacityMaskFn g_opacityMask;
static uint32_t __cdecl hkOpacityMask(uint32_t n, float fade, uint32_t prims, uint32_t insts)
{
    uint32_t r = g_opacityMask(n, fade, prims, insts);
    static std::mutex m;
    static std::set<std::string> seen;
    uint32_t all = n >= 32 ? 0xffffffffu : (1u << n) - 1;
    if (r && (r & all) != all) {   // one object, some primitives skipped and some kept: log all of them once
        std::string line;
        for (uint32_t i = 0; i < n && i < 32; ++i) {
            uint32_t prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
            uint8_t* shd = *(uint8_t**)(uintptr_t)(prim + 0x10);
            uint32_t w = shd ? *(uint32_t*)(shd + 0x50) : 0;
            float v = w ? *(float*)(uintptr_t)((w >> 16) + *(uint32_t*)(uintptr_t)(insts + 4 * i)) : 1.0f;
            char e[96];
            sprintf_s(e, " [%u %.8s w%08x v%.3f %s]", i, shd && *(const char**)shd ? *(const char**)shd : "?", w, v, (r >> i) & 1 ? "SKIP" : "keep");
            line += e;
        }
        std::lock_guard<std::mutex> lock(m);
        if (seen.size() < 300 && seen.insert(line).second) logf("opacity mixed: fade %.3f%s", fade, line.c_str());
        static int dumped;
        for (uint32_t i = 0; i < n && i < 32 && dumped < 6; ++i) {
            if (!((r >> i) & 1)) continue;
            uint32_t prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
            uint8_t* shd = *(uint8_t**)(uintptr_t)(prim + 0x10);
            float* inst = *(float**)(uintptr_t)(insts + 4 * i);
            std::string f;
            for (int k = 0; k < 12; ++k) { char e[16]; sprintf_s(e, " %.3f", inst[k]); f += e; }
            std::string pr;
            for (int k = 0; k < 12; ++k) { char e[16]; sprintf_s(e, " %08x", ((uint32_t*)(uintptr_t)prim)[k]); pr += e; }
            logf("skipped primitive %s sizeA %u: instance floats%s | prim words%s", shd && *(const char**)shd ? *(const char**)shd : "?",
                 shd ? *(uint16_t*)(shd + 0x6a) : 0, f.c_str(), pr.c_str());
            ++dumped;
        }
    }
    for (uint32_t i = 0; r && i < n && i < 32; ++i) {
        uint32_t prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
        uint8_t* shd = *(uint8_t**)(uintptr_t)(prim + 0x10);
        const char* nm = shd ? *(const char**)shd : nullptr;
        uint32_t w = shd ? *(uint32_t*)(shd + 0x50) : 0;
        float v = w ? *(float*)(uintptr_t)((w >> 16) + *(uint32_t*)(uintptr_t)(insts + 4 * i)) : 1.0f;
        char key[160];
        sprintf_s(key, "%s %08x %.3f %.3f %u", nm ? nm : "(null)", w, v, fade, (r >> i) & 1);
        std::lock_guard<std::mutex> lock(m);
        if (seen.size() < 300 && seen.insert(key).second)
            logf("opacity mask: %s (word %08x: value %.3f, object fade %.3f) skip %u", nm ? nm : "(null)", w, v, fade, (r >> i) & 1);
    }
    return r;
}

// test (fadeprobe10): where the object fade comes from. FUN_0044b8f0 (cdecl, 6 args: render node, world sphere
// {x, y, z, r?}, ?, view params (view + 0x340), view mask, out) is the main-view visibility test of a node with
// flag 8 (FUN_00459af0); out[1] is the object fade FUN_0044ad80 then compares (< 1: the primitive leaves the opaque
// G-buffer list). Two terms: screen size (node byte +0xf bit 0: s = pi * sphere[3] / (z^2 * view[+0xd0]), culled
// under view[+0x130], fade (s - [+0x130]) * [+0x170] up to [+0x150]) and distance (node +0xc bit 0x80: start
// +0x10, end +0x14). Logs each faded node's inputs, once per node per 3 s, up to 800 lines. fadeprobe10 = 2 also
// draws every visible faded object unfaded (out[1] = 1), to see whether the faded path is what draws black.
typedef uint8_t(__cdecl* VisTestFn)(uint8_t* node, float* sphere, void* a3, uint8_t* view, uint8_t mask, float* out);
static VisTestFn g_visTest;
static uint8_t __cdecl hkVisTest(uint8_t* node, float* sphere, void* a3, uint8_t* view, uint8_t mask, float* out)
{
    uint8_t r = g_visTest(node, sphere, a3, view, mask, out);
    if (!(r & 1) || !(mask & 1) || out[1] >= 0.999f) return r;
    struct Unfade { float* o; bool on; ~Unfade() { if (on) o[1] = 1.0f; } } unfade{out, g_cfg.fadeProbe10 == 2};
    static std::mutex m;
    static std::unordered_map<uint8_t*, ULONGLONG> last;
    static int lines;
    ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m);
        if (lines >= 800) return r;
        auto it = last.find(node);
        if (it != last.end() && now - it->second < 3000) return r;
        last[node] = now;
        ++lines;
    }
    auto F = [&](size_t o) { return *(float*)(view + o); };
    float z = F(0x70) * sphere[1] + F(0x50) * sphere[0] + F(0x90) * sphere[2] + F(0xb0);
    float s = sphere[3] * 3.1415927f / (z * z * F(0xd0));
    logf("fade probe: node %p id %08X flags %04X f %02X start %.2f end %.2f | sphere (%.2f %.2f %.2f) %.4f z %.2f | k %.6g "
         "pop %.6g fadeEnd %.6g scale %.6g -> screen %.6g fade %.3f", node, *(uint32_t*)(node + 0x34), *(uint16_t*)(node + 0xc),
         node[0xf], *(float*)(node + 0x10), *(float*)(node + 0x14), sphere[0], sphere[1], sphere[2], sphere[3], z, F(0xd0),
         F(0x130), F(0x150), F(0x170), s, out[1]);
    return r;
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
    if (told.first(std::string(who) + filter)) logf("currency10: %s currency for %s -> %s", who, filter, it->second.c_str());
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
static int zones10(std::string& t, const char* p)
{
    if (norm(p) != "gamedb/core/zonelist.lua") return 0;
    std::string c10;
    if (!read10s("gamedb/core/zonelist.lua", c10)) { logf("zones10: no 1.0 zone list in the 1.0 install"); return 0; }
    std::vector<gameplay::Playset10> ps;
    try {
        auto g = luachunk::eval(c10);
        auto world = [](const std::string& zone) {   // the start world is in the 1.0 install: worlds\<zone>[.zip]
            std::string w = std::string(g_cfg.install10) + "\\assets\\worlds\\" + zone;
            return GetFileAttributesA((w + ".zip").c_str()) != INVALID_FILE_ATTRIBUTES || GetFileAttributesA(w.c_str()) != INVALID_FILE_ATTRIBUTES;
        };
        ps = gameplay::playsets10(t, g["ZoneList"], g_cfg.zones10, world);
        if (g_cfg.currency10) {
            auto cur = gameplay::currencies10(g["ZoneList"]);
            std::string l;
            for (auto& [f, c] : cur) l += " " + f + "=" + c;
            logf("currency10: 1.0 playset currencies:%s", l.c_str());
            std::lock_guard<std::mutex> lk(g_currency10Lock);
            g_currency10 = std::move(cur);
        }
    } catch (std::exception& e) {
        logf("zones10: 1.0 zone list not read (%s)", e.what());
        return 0;
    }
    int added = 0;
    std::vector<std::string> pre;
    for (auto& x : ps) {
        added += x.added;
        for (auto& z : x.zones)
            if (z.find('_') != std::string::npos && std::find(pre.begin(), pre.end(), z.substr(0, z.find('_') + 1)) == pre.end())
                pre.push_back(z.substr(0, z.find('_') + 1));
        if (x.added) logf("zones10: %s registered, PlaysetUniqueID %d, %u zones, start %s", x.name.c_str(), x.id, (unsigned)x.zones.size(), x.start.c_str());
        else logf("zones10: %s already in the 3.0 zone list (start %s)", x.name.c_str(), x.start.c_str());
    }
    std::lock_guard<std::mutex> l(g_playsets10Lock);
    g_playsets10 = ps;
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
static void __fastcall hkMissionTick(uint8_t* self, void*, float dt)
{
    g_missionTick(self, nullptr, dt);
    const char* name = *(const char**)(self + 0x10);
    if (!name || !playsetMission10(name)) return;   // a 1.0 playset's mission (zones10's prefixes)
    static bool armed;
    if (g_rdoc && !armed) {
        armed = true;
        std::thread([] { Sleep(1000 * g_cfg.renderdoc10); g_rdoc->TriggerCapture(); logf("renderdoc: capture triggered"); }).detach();
    }
    static const uint32_t stateVa[] = {0x0228a2c0, 0x0228a2c4, 0x0228a2c8, 0x0228a2cc, 0x0228a2d0, 0x0228a2d4, 0x0228a2d8,
                                       0x0228a2dc, 0x0228a2e0, 0x0228a2e4, 0x0228a2e8, 0x0228a2ec, 0x0228a2f0, 0x0228a2f4};
    auto stateName = [](uint32_t v) {
        for (int i = 0; i < 14; ++i) if (*(uint32_t*)rt(stateVa[i]) == v) return i;
        return -1;
    };
    uint32_t state = *(uint32_t*)(self + 0x44), m = *(uint32_t*)(self + 0x198);
    // state objects carry their name at +8: m_state +0x44, m_presentationState +0x54 (FUN_01662730, the context dump)
    auto stName = [](uint32_t st, char* buf) { safeCopy(buf, 48, st ? *(const char**)(uintptr_t)(st + 8) : "<null>"); return buf; };
    char sn[48], pn[48], sig[320];
    sprintf_s(sig, "state=%d(%s) pres=%s mission=%08X mflags=%02X faft=%d trig=%d b159=%d b15f=%d inzone=%d ignzone=%d avready=%d reason=%d "
                   "f3f=%08X beh=%d init=%d", stateName(state), stName(state, sn), stName(*(uint32_t*)(self + 0x54), pn), m,
              m ? *(uint8_t*)(m + 0x3d9) : 0xff, self[0x112], self[0x113],
              self[0x159], self[0x15f], self[0x114], self[0x135], self[0x137], *(int*)(self + 0x290), *(uint32_t*)(self + 0xfc),
              *(int*)(self + 0xc8), self[0x224]);
    static std::unordered_map<uint8_t*, std::string> last;
    std::string& l = last[self];
    if (l != sig) { l = sig; logf("mission %.60s: %s", name, sig); }
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

// locktrace10: who takes the player's controls and gives them back, and the mission chain around it (in-game
// 2026-10-13: control lost a while after IN_City_DockFire starts). The 598 Incredibles graphs hold 29
// LockIndividualPlayerControl (21 graphs), 84 ControllerContextNode (21) and 111 AnimatedCameraNode; DockFire's
// help-capsule ghosts lock when collected and unlock only when their choreography ends (nodes 272/273/253).
//   GamePlayers lock/unlock (vtable +0x14/+0x18, FUN_0139b8d0/FUN_0139b960: player, CString reason; the
//     LockIndividualPlayerControl node passes "MerLockInd%d", FUN_0180ecf0 / FUN_017ffee0)
//   ControllerContextNode apply/remove per agent (FUN_0121d5c0/FUN_0121d8c0; ContextName CString at node+0xa8,
//     FUN_01224880; DisableByDefault applies at init, FUN_012246e0)
//   Choreography node play (Link_In -> FUN_017e91b0; resolved path CString at node+0x8c, FUN_017e8680) and its
//     event handler at node+0x30 (FUN_017e5890: events 0/2 turn 'Playing' on, 1/3 off through FUN_017deef0)
//   MessageSender send (Link_In -> FUN_018120b0; MessageName/Target/Data CStrings at node+0x1c/+0x28/+0x34)
static uint32_t callerVa(void* ret) { return (uint32_t)((uintptr_t)ret - g_base + 0x400000u); }
// lock/unlock/context lines (rare, they name the holder) have their own budget: choreography and message lines
// cannot use it up before the control loss
static volatile LONG g_lockLines, g_traceLines;
static bool lockTraceRoom(volatile LONG& n = g_traceLines) { return InterlockedIncrement(&n) <= 4000; }
typedef void(__fastcall* CtlLockFn)(void* self, void* edx, int player, const void* reason);
static CtlLockFn g_ctlLock, g_ctlUnlock;
static void __fastcall hkCtlLock(void* self, void*, int player, const void* reason)
{
    char r[128];
    if (lockTraceRoom(g_lockLines)) logf("control LOCK player %d reason '%s' (from %08X)", player, cstr(reason, r, sizeof(r)), callerVa(_ReturnAddress()));
    g_ctlLock(self, nullptr, player, reason);
}
static void __fastcall hkCtlUnlock(void* self, void*, int player, const void* reason)
{
    char r[128];
    if (lockTraceRoom(g_lockLines)) logf("control unlock player %d reason '%s' (from %08X)", player, cstr(reason, r, sizeof(r)), callerVa(_ReturnAddress()));
    g_ctlUnlock(self, nullptr, player, reason);
}
typedef void(__fastcall* CtxFn)(uint8_t* node, void* edx, uint32_t agent);
static CtxFn g_ctxOn, g_ctxOff;
static void __fastcall hkCtxOn(uint8_t* node, void*, uint32_t agent)
{
    char n[200];
    if (lockTraceRoom(g_lockLines)) logf("controller context ON (inputs disabled) '%s' agent %08X (from %08X)", cstr(node + 0xa8, n, sizeof(n)), agent, callerVa(_ReturnAddress()));
    g_ctxOn(node, nullptr, agent);
}
static void __fastcall hkCtxOff(uint8_t* node, void*, uint32_t agent)
{
    char n[200];
    if (lockTraceRoom(g_lockLines)) logf("controller context off '%s' agent %08X (from %08X)", cstr(node + 0xa8, n, sizeof(n)), agent, callerVa(_ReturnAddress()));
    g_ctxOff(node, nullptr, agent);
}
typedef void(__fastcall* ChoPlayFn)(uint8_t* node, void* edx);
static ChoPlayFn g_choPlay;
static void __fastcall hkChoPlay(uint8_t* node, void*)
{
    g_choPlay(node, nullptr);
    char n[200];
    if (lockTraceRoom()) logf("cho node %p play '%s'", node, cstr(node + 0x8c, n, sizeof(n)));
}
typedef void(__fastcall* ChoEventFn)(uint8_t* handler, void* edx, const int* ev);
static ChoEventFn g_choEvent;
static void __fastcall hkChoEvent(uint8_t* handler, void*, const int* ev)
{
    int type = ev ? ev[0] : -1;
    if (type >= 0 && type <= 3 && lockTraceRoom()) {
        char n[200];
        logf("cho node %p event %d (Playing %s) '%s'", handler - 0x30, type, type == 0 || type == 2 ? "on" : "off", cstr(handler - 0x30 + 0x8c, n, sizeof(n)));
    }
    g_choEvent(handler, nullptr, ev);
}
typedef void(__fastcall* MsgSendFn)(uint8_t* node, void* edx, void* payload);
static MsgSendFn g_msgSend;
static void __fastcall hkMsgSend(uint8_t* node, void*, void* payload)
{
    char n[128], t[128], d[128];
    if (lockTraceRoom())
        logf("message send '%s' target '%s' data '%s'", cstr(node + 0x1c, n, sizeof(n)), cstr(node + 0x28, t, sizeof(t)), cstr(node + 0x34, d, sizeof(d)));
    g_msgSend(node, nullptr, payload);
}
// MessageHandler delivery (FUN_018049f0 on the listener at node+0x20, which points back at the node from +0x20;
// MessageName CString at node+0x4c, registered by FUN_017fd6b0): a sent name without this line reached no graph.
typedef void(__fastcall* MsgRecvFn)(uint8_t* listener, void* edx, const char* data, uint32_t a2, uint32_t a3);
static MsgRecvFn g_msgRecv;
static void __fastcall hkMsgRecv(uint8_t* listener, void*, const char* data, uint32_t a2, uint32_t a3)
{
    char n[128], d[128];
    if (lockTraceRoom()) {
        const void* node = nullptr;
        if (sys::readable(listener + 0x20, sizeof(node))) node = *(void* const*)(listener + 0x20);
        safeCopy(d, sizeof(d), data);
        logf("message recv '%s' data '%s'", node ? cstr((const uint8_t*)node + 0x4c, n, sizeof(n)) : "?", d);
    }
    g_msgRecv(listener, nullptr, data, a2, a3);
}

// The world graph's start gates (gate10 checks): the in_city.mer chains need these three node kinds to fire.
//   MissionMonitor: FUN_01730490 (thiscall on the node, pin index, ret 4) runs once per output it fires, right before
//     the fire (callers: the update FUN_01744090, the abort handler FUN_01730560); MissionName CString at node+0x64
//     (FUN_01743f60 resolves the mission from it). 841 IN_City_DockFire 'Actived' -> 846 -> 848 -> 847.Off releases
//     the TUT_DISABLE_ gate.
//   LoadScreenActive: update FUN_0145b9a0 (thiscall on node+0x14, ret 4): IsOn at +0x24 (starts 1, FUN_0145b860),
//     pending FireInit at +0xc; fires Started (def+0x28) or Stopped (def+0x30) when the load-screen state changes.
//   MasterNetSync: Link_In body FUN_006ec4f0 (thiscall on the node, payload, ret 4): Private always, Link_Out only
//     when GraphInstance vf+0x10 (networked, FUN_006ab280) and vf+0x14 (host) hold.
static volatile LONG g_gateLines;   // load-screen and net-sync lines: their own budget
typedef uint32_t(__fastcall* MonPinFn)(uint8_t* node, void* edx, int pin);
static MonPinFn g_monPin;
static uint32_t __fastcall hkMonPin(uint8_t* node, void*, int pin)
{
    static const char* const kPin[] = {"Locked", "Now Available", "Talking", "Actived", "Completed", "Failed", "Aborted",
                                       "Selected", "Unselected"};
    char n[128];
    static volatile LONG lines;   // its own budget: load fires every FireImpulseOnLoad monitor once
    if (lockTraceRoom(lines))
        logf("mission monitor '%s' fires %s (pin %d)", cstr(node + 0x64, n, sizeof(n)), pin >= 0 && pin < 9 ? kPin[pin] : "?", pin);
    return g_monPin(node, nullptr, pin);
}
typedef void(__fastcall* TickFn)(uint8_t* self, void* edx, uint32_t a1);
static TickFn g_loadTick;
static void __fastcall hkLoadTick(uint8_t* self, void*, uint32_t a1)
{
    uint8_t on = self[0x24], init = self[0xc];
    g_loadTick(self, nullptr, a1);
    static core::LogOnce<uint8_t*> seen(64);
    bool first = seen.first(self);
    bool fired = self[0x24] != on || (init && !self[0xc]);
    if ((first || fired) && lockTraceRoom(g_gateLines))
        logf("load screen node %p: %s (IsOn %d -> %d%s)", self - 0x14, fired ? (self[0x24] ? "fires Started" : "fires Stopped") : "first tick, no fire",
             on, self[0x24], init ? ", FireInit" : "");
}
typedef void(__fastcall* NetSyncFn)(uint8_t* node, void* edx, void* payload);
static NetSyncFn g_netSync;
static void __fastcall hkNetSync(uint8_t* node, void*, void* payload)
{
    if (lockTraceRoom(g_gateLines)) {
        typedef void*(__fastcall* GetFn)(void* self, void* edx);
        typedef bool(__fastcall* FlagFn)(void* self, void* edx);
        uint8_t* vb = node + 4 + *(int*)(*(uint8_t**)(node + 4) + 4);   // the ContainedBase, as FUN_006ec4f0 finds it
        void* g = ((GetFn)(*(void***)vb)[2])(vb, nullptr);
        int net = g ? ((FlagFn)(*(void***)g)[4])(g, nullptr) : -1, host = g ? ((FlagFn)(*(void***)g)[5])(g, nullptr) : -1;
        logf("net sync node %p Link_In: graph networked %d host %d -> 3.0 fires Private%s", node, net, host,
             net == 1 && host == 1 ? " and Link_Out" : " only");
    }
    g_netSync(node, nullptr, payload);
}

// ColVisToggle family (toggletype10 checks): LoadParameters 0x01808FE0 (thiscall on the node, record, ret 4) sets the
// mode bits at node+0x38 (1 collision, 2 visibility) from the 'Type' parameter and IsOn at +0x30; 0x01803550 (thiscall
// on the node, actor list {?, items, count}, ret 4) applies +0x30 to the list (start callback 0x018090E0, TurnOn /
// TurnOff / Toggle handlers 0x01803640..).
static const char* colVisMode(uint8_t bits)
{
    return (bits & 3) == 3 ? "collision+visibility" : (bits & 1) ? "collision only" : (bits & 2) ? "visibility only" : "none";
}
typedef void(__fastcall* ColVisFn)(uint8_t* node, void* edx, void* arg);
static ColVisFn g_colVisLoad, g_colVisApply;
static void __fastcall hkColVisLoad(uint8_t* node, void*, void* rec)
{
    g_colVisLoad(node, nullptr, rec);
    static volatile LONG lines;
    if (lockTraceRoom(lines)) logf("colvis toggle %p loaded: %s, IsOn %d", node, colVisMode(node[0x38]), node[0x30]);
}
static void __fastcall hkColVisApply(uint8_t* node, void*, void* list)
{
    static volatile LONG lines;
    uint32_t n = list ? *(const uint32_t*)((const uint8_t*)list + 8) : 0;
    if (n && lockTraceRoom(lines))
        logf("colvis toggle %p applies %s (%s) to %u actors", node, node[0x30] ? "ON" : "off", colVisMode(node[0x38]), n);
    g_colVisApply(node, nullptr, list);
}

// beacon10 check: MissionParams BeaconMap lookup FUN_01591570 (thiscall on the mission manager, CString name,
// CString fallback by value -- the callee releases both, ret 8) -> entry {name, cho, placement, float} or 0. Callers:
// focus points FUN_015c30a0 / FUN_015b86e0, givers FUN_015cce20 / FUN_015b9aa0, mission target FUN_015cdb70.
// One line per name: a beacon spawns only from an entry with a cho.
typedef uint8_t*(__fastcall* BeaconLookupFn)(void* self, void* edx, const char* name, const char* fallback);
static BeaconLookupFn g_beaconLookup;
static uint8_t* __fastcall hkBeaconLookup(void* self, void*, const char* name, const char* fallback)
{
    char n[96], fb[96];
    safeCopy(n, sizeof(n), name);   // copies first: the callee releases its by-value CStrings
    safeCopy(fb, sizeof(fb), fallback);
    uint8_t* e = g_beaconLookup(self, nullptr, name, fallback);
    static core::LogOnce<std::string> seen(300);
    bool first = seen.first(n);
    static volatile LONG lines;
    if (first && lockTraceRoom(lines)) {
        char c[96], pl[96];
        if (e) logf("beacon map '%s' -> cho '%s' placement '%s' offset %.2f", n, cstr(e + 4, c, sizeof(c)), cstr(e + 8, pl, sizeof(pl)), *(float*)(e + 0xc));
        else logf("beacon map '%s' (fallback '%s') -> no entry", n, fb);
    }
    return e;
}

// The compass follows the mission each local player tracks: FUN_015ba4d0 (thiscall on the mission manager, mission,
// player, flag, ret 0xc) sets slot mgr+0x10+4*n and posts "CheckCompass"; mission+0x140 = its context, name at +0x10.
typedef void(__fastcall* TrackFn)(uint8_t* mgr, void* edx, uint8_t* mission, int player, int flag);
static TrackFn g_track;
static void __fastcall hkTrack(uint8_t* mgr, void*, uint8_t* mission, int player, int flag)
{
    uint8_t* before[4];
    memcpy(before, mgr + 0x10, sizeof(before));
    g_track(mgr, nullptr, mission, player, flag);
    auto name = [](uint8_t* m, char* buf) {
        uint8_t* ctx = m ? *(uint8_t**)(m + 0x140) : nullptr;
        safeCopy(buf, 96, ctx ? *(const char**)(ctx + 0x10) : "nothing");
        return buf;
    };
    static volatile LONG lines;
    for (int s = 0; s < 4; ++s) {
        uint8_t* now = *(uint8_t**)(mgr + 0x10 + 4 * s);
        char a[96], b[96];
        if (now != before[s] && lockTraceRoom(lines))
            logf("compass: player slot %d tracks '%s' (asked for '%s', player %d)", s, name(now, a), name(mission, b), player);
    }
}

// The compass draw: MissionNavData's update FUN_00c13c50 (cdecl, the avatar's component). +0x75 enabled (CheckCompass),
// +0x68/+0x69 on / hidden, +0xa4 its hud_Marker screen handle (screen manager [0x02289E88], lookup FUN_0154fed0,
// screen +0x6d movie loaded), +0x78 the target FUN_00be3c20 last drew, +0x10 marker 0 (+0x24 visible, +8 x, +0xc y,
// +0x20 alpha 0..100: 0 under 10 m of the target, full from 31 m). One line whenever that state changes, plus the
// pause flag (DAT_0225f478[0], Pause_UnPauseFromPausedScreenIfPaused's test).
typedef void(__cdecl* NavUpdateFn)(uint8_t* nav);
static NavUpdateFn g_navUpdate;
static void __cdecl hkNavUpdate(uint8_t* nav)
{
    g_navUpdate(nav);
    if (!nav || !sys::readable(nav, 0xa8)) return;
    uint8_t* m0 = *(uint8_t**)(nav + 0x10);
    bool mOk = m0 && sys::readable(m0, 0x28);
    int vis = mOk ? m0[0x24] : -1;
    float alpha = mOk ? *(float*)(m0 + 0x20) : -1.f, *tg = (float*)(nav + 0x78);
    int h = *(int*)(nav + 0xa4), loaded = -1;
    void* mgr = *(void**)rt(0x02289E88);
    if (mgr && h != -1) {
        uint8_t* scr = ((uint8_t * (__fastcall*)(void*, void*, int)) rt(0x0154FED0))(mgr, nullptr, h);
        loaded = scr ? scr[0x6d] : -2;
    }
    int paused = *(uint8_t*)rt(0x0225F478);
    static int last[6] = {-9, -9, -9, -9, -9, -9};
    static float lastT[3];
    int now[6] = {nav[0x75], nav[0x68] | nav[0x69] << 1, loaded, vis, alpha < 0 ? -1 : alpha < 1 ? 0 : alpha < 99 ? 1 : 2, paused};
    bool moved = fabsf(tg[0] - lastT[0]) + fabsf(tg[1] - lastT[1]) + fabsf(tg[2] - lastT[2]) > 2.f;
    if (!memcmp(now, last, sizeof(now)) && !moved) return;
    memcpy(last, now, sizeof(now));
    memcpy(lastT, tg, sizeof(lastT));
    static volatile LONG lines;
    if (lockTraceRoom(lines) && lines <= 300)
        logf("compass draw: enabled %d on %d hidden %d, hud_Marker screen %d loaded %d, marker0 visible %d at (%.0f %.0f) alpha %.0f, "
             "target (%.2f %.2f %.2f), paused %d", nav[0x75], nav[0x68], nav[0x69], h, loaded, vis, mOk ? *(float*)(m0 + 8) : 0.f,
             mOk ? *(float*)(m0 + 0xc) : 0.f, alpha, tg[0], tg[1], tg[2], paused);
}

// The Mission Objectives pane: MissionDetailsPanel lists MissionLogData ProgressData[i].ProgressString (empty ones
// skipped), written for an instantiated mission by FUN_0157cd10 (thiscall on the mission, Lua table ref, ret 4) from
// its progress entries: pointer array mission+0x2ac, count +0x2b0; entry [0] state 1/2/3, [1] ProgressString,
// [2] ProgressStringKey, [3] suffix, [4]/[5] current/total (float), byte +0x1c StringIsKey, +0x1d IsOptional.
// One line per mission and entry count.
typedef void(__fastcall* MisProgressFn)(uint8_t* mission, void* edx, void* table);
static MisProgressFn g_misProgress;
static void __fastcall hkMisProgress(uint8_t* mission, void*, void* table)
{
    g_misProgress(mission, nullptr, table);
    if (!mission || !sys::readable(mission + 0x2ac, 8) || !sys::readable(mission + 0x140, 4)) return;
    char name[96], a[96], k[96], s[96];
    uint8_t* ctx = *(uint8_t**)(mission + 0x140);
    safeCopy(name, sizeof(name), ctx && sys::readable(ctx + 0x10, 4) ? *(const char**)(ctx + 0x10) : "?");
    uint8_t** arr = *(uint8_t***)(mission + 0x2ac);
    int n = *(int*)(mission + 0x2b0);
    static core::LogOnce<std::string> seen(201);   // at most 201 (mission, entry count) pairs
    if (!seen.first(std::string(name) + "#" + std::to_string(n))) return;
    static volatile LONG lines;
    if (!lockTraceRoom(lines) || lines > 400) return;
    logf("mission progress '%s': %d entries", name, n);
    for (int i = 0; i < n && i < 12 && arr && sys::readable(arr + i, 4); ++i) {
        const uint32_t* e = (const uint32_t*)arr[i];
        if (!e || !sys::readable(e, 0x24)) continue;
        safeCopy(a, sizeof(a), (const char*)e[1]);
        safeCopy(k, sizeof(k), (const char*)e[2]);
        safeCopy(s, sizeof(s), (const char*)e[3]);
        logf("  [%d] state %u progress '%s' key '%s' suffix '%s' %.0f/%.0f isKey %d optional %d", i, e[0], a, k, s,
             *(const float*)(e + 4), *(const float*)(e + 5), ((const uint8_t*)e)[0x1c], ((const uint8_t*)e)[0x1d]);
    }
}

// missiontrace: a 1.0 mission's actor focus points (MU tutorial 2026-10-15: no marker on the FearTech decorations).
// The graph node MissionFocusPoint_Actor (Meridian::FocusPointActor, vtable 01DE726C) binds at init to the mission's
// FocusPoint_Actor record by its MissionID 'name:id' (FUN_01747160: +0x40 the string, +0x28 the mission element,
// +0x30 the id; slot 28 sets +0x64 = record). Its Update pin (FUN_01730e80, cdecl(pin context), [ctx+4] = node) only
// marks the record (FUN_01571690: +0x140 |= 0x10) and does nothing while +0x64 is 0. The record (vtable 01DCA744)
// update FUN_015df390 (thiscall, int, ret 4) then tags the node's ActorList (node FUN_01740880 -> +0x180 = 1 once the
// pin had actors). Record fields: +0x142 MissionID, +0x144 ParentMissionID -> +0x148 parent task (+0xc flags: bit 0
// active), +0x140 flags (1 active, 4 turned off, 0x10 update), +0x168 FUN_015930d0's last active check, +0x17c tag,
// +0x190 node, +0xd4 mission context (+0x10 name), +0xe8/+0xec beacon cho/placement (FUN_015c30a0), +0x150
// EnableCompass, +0x160 MissionTarget, +0x170 NoHudMarker. Log-only: one line per record whenever that state changes.
typedef void(__fastcall* FpUpdateFn)(uint8_t* fp, void* edx, int a);
static FpUpdateFn g_fpUpdate;
static void __fastcall hkFpUpdate(uint8_t* fp, void*, int a)
{
    g_fpUpdate(fp, nullptr, a);
    if (!fp || *(uint32_t*)fp != (uint32_t)rt(0x01DCA744)) return;   // the engine's own this: no probe needed
    uint8_t* ctx = *(uint8_t**)(fp + 0xd4);
    char name[64];
    safeCopy(name, sizeof(name), ctx && sys::readable(ctx + 0x10, 4) ? *(const char**)(ctx + 0x10) : nullptr);
    if (!ctx || !playsetMission10(name)) return;
    uint8_t *parent = *(uint8_t**)(fp + 0x148), *node = *(uint8_t**)(fp + 0x190);
    char tag[64], cho[64], pl[64], ntag[64] = "";
    void *nRec = nullptr, *nAct = nullptr;
    if (node && sys::readable(node + 0x5c, 12)) { cstr(node + 0x60, ntag, sizeof(ntag)); nAct = *(void**)(node + 0x5c); nRec = *(void**)(node + 0x64); }
    uint8_t f = fp[0x140];
    char sig[512];
    sprintf_s(sig, "flags %02X (active %d, off %d, update %d) check %d, parent %d %p flags %d, node %p (record %p, tag '%s', "
                   "ActorTag %p), tagged %d tag '%s', beacon '%s' '%s', compass %d target %d nohud %d",
              f, f & 1, f >> 2 & 1, f >> 4 & 1, fp[0x168], *(int16_t*)(fp + 0x144), parent,
              parent && sys::readable(parent + 0xc, 1) ? parent[0xc] : -1, node, nRec, ntag, nAct, fp[0x180],
              cstr(fp + 0x17c, tag, sizeof(tag)), cstr(fp + 0xe8, cho, sizeof(cho)), cstr(fp + 0xec, pl, sizeof(pl)), fp[0x150],
              fp[0x160], fp[0x170]);
    static std::mutex m;
    static std::unordered_map<uint8_t*, std::string> last;
    {
        std::lock_guard<std::mutex> lock(m);
        if (last.size() > 400) return;
        std::string& l = last[fp];
        if (l == sig) return;
        l = sig;
    }
    static volatile LONG lines;
    if (lockTraceRoom(lines) && lines <= 300) logf("focus point %.60s:%d %s", name, *(int16_t*)(fp + 0x142), sig);
}
typedef void(__cdecl* FpPinFn)(uint8_t* pinCtx);
static FpPinFn g_fpNodeUpdate;
static void __cdecl hkFpNodeUpdate(uint8_t* pinCtx)
{
    uint8_t* node = pinCtx && sys::readable(pinCtx + 4, 4) ? *(uint8_t**)(pinCtx + 4) : nullptr;
    if (node && sys::readable(node, 0x68) && *(uint32_t*)node == (uint32_t)rt(0x01DE726C)) {
        char id[96];
        cstr(node + 0x40, id, sizeof(id));
        static volatile LONG lines;
        if (playsetMission10(id) && lockTraceRoom(lines) && lines <= 200)
            logf("focus point node %p '%s' Update: record %p, mission element %p, id %d", node, id, *(void**)(node + 0x64),
                 *(void**)(node + 0x28), *(int*)(node + 0x30));
    }
    g_fpNodeUpdate(pinCtx);
}
// MU run 2026-10-15 10:54 (run-mu-1054.log): the tutorial's five records bind, tag and turn active exactly as
// SuperCollect's, whose pennants got 20 Beacon_Mission_Arrow choreographies; the FearTech decorations got none. Two
// steps remain between the tag and a beacon:
//   node tag apply FUN_0173a3f0 (thiscall node): reads the ActorList pin into a list; each handle -> actor through
//     FUN_0074b200 (handle table + actor class check), which is tagged (FUN_014ac0a0)
//   record scan, FocusPoint_Actor slot 12 FUN_015d5f80 (thiscall record): the tag's type-1 entries (FUN_014ac5a0)
//     -> FUN_0074b200 -> actor; an actor with +0x10e == 2, or in actor state 0xe53 (0x1079 when HideOnSpawn), is
//     skipped, else a beacon item joins the record's list at +0x118 (head; next at [0])
// Each is wrapped once here, and FUN_0074b200 counts its calls (and the passes' +0x10e) only inside them.
struct FpScope { int mode, calls, actors; uint8_t b10e[4]; };
static thread_local FpScope g_fpScope;
typedef uint8_t*(__cdecl* ActorOfFn)(uint32_t handle);
static ActorOfFn g_actorOf;
static uint8_t* __cdecl hkActorOf(uint32_t handle)
{
    uint8_t* a = g_actorOf(handle);
    FpScope& s = g_fpScope;
    if (s.mode) {
        ++s.calls;
        if (a && s.actors < 4) s.b10e[s.actors] = a[0x10e];
        if (a) ++s.actors;
    }
    return a;
}
static const char* fpScopeText(char* buf, size_t cap)
{
    const FpScope& s = g_fpScope;
    sprintf_s(buf, cap, "%d handles -> %d actors (+0x10e %d %d %d %d)", s.calls, s.actors, s.actors > 0 ? s.b10e[0] : -1,
              s.actors > 1 ? s.b10e[1] : -1, s.actors > 2 ? s.b10e[2] : -1, s.actors > 3 ? s.b10e[3] : -1);
    return buf;
}
typedef void(__fastcall* FpThisFn)(uint8_t* self, void* edx);
static FpThisFn g_fpApply, g_fpScan;
static void __fastcall hkFpApply(uint8_t* node, void*)
{
    char id[96] = "";
    bool mine = node && *(uint32_t*)node == (uint32_t)rt(0x01DE726C) && playsetMission10(cstr(node + 0x40, id, sizeof(id)));
    if (!mine) return g_fpApply(node, nullptr);
    g_fpScope = FpScope{1};
    g_fpApply(node, nullptr);
    char t[96], sig[160];
    sprintf_s(sig, "tag apply: ActorList %s, ActorTag %p", fpScopeText(t, sizeof(t)), *(void**)(node + 0x5c));
    g_fpScope.mode = 0;
    static std::mutex m;
    static std::unordered_map<uint8_t*, std::string> last;
    {
        std::lock_guard<std::mutex> lock(m);
        if (last.size() > 400 || last[node] == sig) return;
        last[node] = sig;
    }
    static volatile LONG lines;
    if (lockTraceRoom(lines) && lines <= 200) logf("focus point node %p '%s' %s", node, id, sig);
}
static void __fastcall hkFpScan(uint8_t* fp, void*)
{
    uint8_t* ctx = fp && *(uint32_t*)fp == (uint32_t)rt(0x01DCA744) ? *(uint8_t**)(fp + 0xd4) : nullptr;
    char name[64];
    safeCopy(name, sizeof(name), ctx && sys::readable(ctx + 0x10, 4) ? *(const char**)(ctx + 0x10) : nullptr);
    if (!ctx || !playsetMission10(name)) return g_fpScan(fp, nullptr);
    g_fpScope = FpScope{2};
    g_fpScan(fp, nullptr);
    int items = 0;
    for (uint8_t** e = *(uint8_t***)(fp + 0x118); e && items < 1000; e = (uint8_t**)e[0]) ++items;
    char t[96], sig[200];
    sprintf_s(sig, "scan: tagged %s, active %d, beacon items %d", fpScopeText(t, sizeof(t)), fp[0x140] & 1, items);
    g_fpScope.mode = 0;
    static std::mutex m;
    static std::unordered_map<uint8_t*, std::string> last;
    {
        std::lock_guard<std::mutex> lock(m);
        if (last.size() > 400 || last[fp] == sig) return;
        last[fp] = sig;
    }
    static volatile LONG lines;
    if (lockTraceRoom(lines) && lines <= 300) logf("focus point %.60s:%d %s", name, *(int16_t*)(fp + 0x142), sig);
}

// Meridian node creators (cdecl placement constructors registered per class, FUN_006bc220): count what a graph builds.
typedef void*(__cdecl* NodeMakeFn)(void* mem, uint32_t a2, uint32_t a3);
static NodeMakeFn g_mkStart, g_mkLocator, g_mkSpawnExp;
static volatile LONG g_nStart, g_nLocator, g_nSpawnExp;
static void* __cdecl hkMkStart(void* m, uint32_t a2, uint32_t a3) { logf("MNode PlayerStartPosNode %p (#%ld)", m, InterlockedIncrement(&g_nStart)); return g_mkStart(m, a2, a3); }
static void* __cdecl hkMkLocator(void* m, uint32_t a2, uint32_t a3) { LONG n = InterlockedIncrement(&g_nLocator); if (n <= 2 || n % 100 == 0) logf("MNode LocatorNode #%ld", n); return g_mkLocator(m, a2, a3); }
static void* __cdecl hkMkSpawnExp(void* m, uint32_t a2, uint32_t a3) { LONG n = InterlockedIncrement(&g_nSpawnExp); if (n <= 2 || n % 50 == 0) logf("MNode SpawnExpendable #%ld", n); return g_mkSpawnExp(m, a2, a3); }

// Trace: instance header clone FUN_004f5900(index, a2, delta, instRoot) (thiscall on the instance header, ret 0x10).
// Header = [name, parent|rc, sibling|flags, child-or-self|slot, ...]. FUN_004b4470 gave every header of the cloned array
// a temp slot (DAT_021af260); 0x4f5a25 maps the +0xc target through it without a check, so a +0xc that leads outside
// the array reads slot 0 (NULL) and faults at 0x4cdabe. Log such headers with their target.
typedef void(__fastcall* HdrCloneFn)(void* self, void* edx, uint32_t index, uint32_t a2, int delta, uint32_t instRoot);
static HdrCloneFn g_hdrClone;
static volatile LONG g_hdrLogged;
static void hdrName(const uint32_t* h, char* out, size_t cap)
{
    __try { if (h && h[0]) safeCopy(out, cap, (const char*)(h[0] + 0xc)); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void __fastcall hkHdrClone(void* self, void*, uint32_t index, uint32_t a2, int delta, uint32_t instRoot)
{
    typedef void*(__fastcall * ResolveFn)(void* handleField, void* edx);
    ResolveFn res = (ResolveFn)rt(0x004B46D0);
    uint32_t* inst = (uint32_t*)self;
    uint32_t* tmpl = (uint32_t*)((char*)self - delta);
    uint32_t* t = (uint32_t*)res(inst + 3, nullptr);
    uint32_t slots = *(uint32_t*)rt(0x021B725C), e = 1, slot = 0;
    if (t) {
        slot = ((t[2] & 0xe001ffff) | (t[3] >> 3)) >> 17;
        e = slot < slots ? ((uint32_t*)rt(0x021AF260))[slot] : 1;
    }
    if ((!t || !e) && InterlockedIncrement(&g_hdrLogged) <= 6) {
        char name[80] = "?", tname[80] = "?", pname[80] = "?";
        hdrName(tmpl, name, sizeof(name));
        hdrName(t, tname, sizeof(tname));
        uint32_t* tp = t ? (uint32_t*)res(t + 1, nullptr) : nullptr;
        hdrName(tp, pname, sizeof(pname));
        const uint32_t* base = (const uint32_t*)((char*)tmpl - index * 0x14);
        logf("header clone %u/%u '%s' (instRoot %08X, %u slots): template %p [%08X %08X %08X %08X %08X] +0xc -> %p '%s' (array index %d, slot %u) [%08X %08X %08X %08X %08X] parent %p '%s'",
             index, g_instHeaders, name, instRoot, slots, tmpl, tmpl[0], tmpl[1], tmpl[2], tmpl[3], tmpl[4], t, tname,
             t ? (int)(((const char*)t - (const char*)base) / 0x14) : -1, slot,
             t ? t[0] : 0, t ? t[1] : 0, t ? t[2] : 0, t ? t[3] : 0, t ? t[4] : 0, tp, pname);
    }
    g_hdrClone(self, nullptr, index, a2, delta, instRoot);
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

// data/tips.lua (3.0's, opened as r) + the 1.0 tip records it lacks (ui::mergeTips); a ZoneTipCategories key naming
// a registered zone (the zone manager's by-name lookup FUN_01391150, thiscall (uint32_t* out, name), ret 8) stays
// 3.0's. The stream is consumed; the merged (or, failing that, the original) bytes come back as a MemFile.
typedef void(__fastcall* ZoneByNameFn)(void* self, void* edx, uint32_t* out, const char* name);
static int* tips30(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5, int* r)
{
    std::string t30, t10;
    if (!readStream(r, t30)) return g_open(path, flags, a3, a4, a5);
    if (t30.find("ZoneTipCategories") == std::string::npos || !read10s("data/tips.lua", t10)) return memFile(t30.data(), t30.size());
    void* zm = *(void**)rt(0x0225F548);
    int k = ui::mergeTips(t30, t10, [&](const std::string& z) {
        uint32_t id = 0;
        if (zm) ((ZoneByNameFn)rt(0x01391150))(zm, nullptr, &id, z.c_str());
        return id != 0;
    });
    logf("tips %s: %d 1.0 records added (zone manager %s)", path, k, zm ? "up" : "NOT up: zone keys unchecked");
    return memFile(t30.data(), t30.size());
}

static std::string mergeVtxf(const std::string& t30, const std::string& t10);
static int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    const bool prof = g_cfg.loadProf10;   // loadprof10: ms in the engine's own open, the 1.0 read, transcode, MemFile
    double tIn = prof ? qpcMs() : 0, t30 = 0, tRead = 0, tTc = 0;
    if (prof) noteOpenThread();
    int* r = g_open(path, flags, a3, a4, a5);
    if (prof) t30 = qpcMs() - tIn;
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (r && g_cfg.shaders10 && !(flags & 1) && _stricmp(p, "shaders/vformat.vtxf") == 0) {   // + 1.0's formats
        typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
        typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
        typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
        void** vt = *(void***)r;
        std::string t30, t10;
        t30.resize(((SizeFn)vt[4])(r, nullptr));
        uint32_t got = t30.empty() ? 0 : ((ReadFn)vt[5])(r, nullptr, &t30[0], (uint32_t)t30.size());
        if (got == t30.size() && read10s("shaders/vformat.vtxf", t10)) {
            std::string m = mergeVtxf(t30, t10);
            ((DtorFn)vt[11])(r, nullptr, 1);
            return memFile(m.data(), m.size());
        }
        ((DtorFn)vt[11])(r, nullptr, 1);
        return g_open(path, flags, a3, a4, a5);
    }
    // the file named Tips.lua only (FUN_00b70da0's literal), not every *tips.lua: the stream is consumed and re-served
    if (r && g_cfg.serve10 && g_cfg.tips10 && !(flags & 1) && (!_stricmp(p, "tips.lua") || endsWith(p, "/tips.lua") || endsWith(p, "\\tips.lua")))
        return tips30(path, flags, a3, a4, a5, r);
    // menu: the Play Sets list (screen PlaysetListWin32) gets the registered 1.0 playsets; its 3.0 chunk runs as shipped
    if (r && g_cfg.playsetMenu10 && !(flags & 1) && endsWith(p, "playsetlistwin32.lua") && norm(p) == "presentation/playsetlistwin32.lua") {
        std::vector<std::pair<std::string, std::string>> ps;
        {
            std::lock_guard<std::mutex> l(g_playsets10Lock);
            for (auto& x : g_playsets10) ps.push_back({x.name, x.start});
        }
        std::string t;
        if (readStream(r, t) && !t.empty() && t[0] == 0x1b && !ps.empty()) {
            std::string s = ui::playsetMenu10(t, ps), names;
            for (auto& x : ps) names += " " + x.first + "->" + x.second;
            logf("%s: %u 1.0 playset entries added:%s (%u B 3.0 chunk -> %u B Lua)", p, (unsigned)ps.size(), names.c_str(),
                 (unsigned)t.size(), (unsigned)s.size());
            return memFile(s.data(), s.size());
        }
        logf("%s: %s (%u B): unchanged", p, ps.empty() ? "no 1.0 playset registered" : "not the 3.0 chunk", (unsigned)t.size());
        return g_open(path, flags, a3, a4, a5);
    }
    // gp: 3.0 files the activity -> challenge redirect extends (gameplay.hpp): the zone list (ChallengeTypeDataFile
    // beside the 1.0 ActivityTypeDataFile) and ChallengeData.lua (+ the 1.0 beacons)
    bool zl = endsWith(p, "zonelist.lua"), cd = endsWith(p, "challengedata/challengedata.lua") || endsWith(p, "challengedata\\challengedata.lua");
    // mis (beacon10): the zone list's 1.0 MissionParams.BeaconMap rows in 3.0's row form (mission::beaconMap30)
    bool zl10 = zl && (g_cfg.zones10 || g_cfg.playsetMenu10 || g_cfg.missionTrace);   // the 1.0 playsets (zones10)
    if (r && !(flags & 1) && ((g_cfg.challenge10 && (zl || cd)) || (g_cfg.beacon10 && zl) || zl10)) {
        typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
        typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
        typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
        void** vt = *(void***)r;
        std::string t, t10;
        t.resize(((SizeFn)vt[4])(r, nullptr));
        uint32_t got = t.empty() ? 0 : ((ReadFn)vt[5])(r, nullptr, &t[0], (uint32_t)t.size());
        ((DtorFn)vt[11])(r, nullptr, 1);
        int k = 0, kb = 0, ka = 0, kz = 0;
        if (got == t.size() && !t.empty() && t[0] != 0x1b) {
            if (zl10) kz = zones10(t, p);   // first: the steps below apply to the added records as to written ones
            if (zl && g_cfg.challenge10) k = gameplay::zonelist30(t);
            else if (cd && read10s("activitydata/defaultactivitybeacons.lua", t10)) k = gameplay::beacons30(t, t10);
            if (zl && g_cfg.beacon10) kb = mission::beaconMap30(t, &ka);
        }
        if (g_cfg.challenge10) logf("%s: %d %s", p, k, zl ? "playset records got ChallengeTypeDataFile" : "1.0 beacons added");
        if (zl && g_cfg.beacon10) logf("%s: %d BeaconMap rows in 3.0 form, %d Beacon_Arrow_Blue rows added (beacon10)", p, kb, ka);
        return k || kb || kz ? memFile(t.data(), t.size()) : g_open(path, flags, a3, a4, a5);
    }
    if (g_cfg.trace && !_strnicmp(p, "gamedb/", 7) && endsWith(p, "/bitmasks.lua")) {   // any playset's, 1.0 or 3.0
        char line[600];
        int len = sprintf_s(line, "gamedb package open %s, stack:", p);
        stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 24);
        logf("%s", line);
    }
    if (!(flags & 1) && endsWith(p, ".lua") && takeChdPath(p)) return chdForLua(p, r);
    if (!r && !(flags & 1) && g_cfg.serve10 && !_strnicmp(p, "di1sys/", 7)) return sysServe(p);
    if (r && g_cfg.memtest[0] && !(flags & 1) && memtestMatch(p)) {
        size_t n = 0;
        r = copyToMemFile(r, &n);
        logf("memtest %s %u B -> %s", p, (unsigned)n, r ? "MemFile" : "FAILED");
        return r;
    }
    bool blocked = false;
    std::string missWhy = " (not served)";
    if (g_cfg.block10[0]) {
        std::string np = norm(p), list = g_cfg.block10;
        for (size_t a = 0, b; a < list.size(); a = b + 1) {
            b = list.find(',', a);
            if (b == std::string::npos) b = list.size();
            std::string pre = list.substr(a, b - a);
            if (!pre.empty() && np.rfind(pre, 0) == 0) blocked = true;
        }
    }
    if (!r && !blocked && g_cfg.serve10 && g_cfg.install10[0] && !(flags & 1)) {   // flags bit 0 = write (never served)
        std::string np = norm(p), hit, over;
        {
            std::lock_guard<std::mutex> lock(g_sessLock);
            auto it = g_served.find(np);                 // a world session product (.mtb/.vbuf/.ibuf)
            if (it != g_served.end()) hit = it->second;  // kept: the engine reopens files
            else {
                auto o = g_over10.find(np);              // a full-path member of a loaded 1.0 archive
                if (o != g_over10.end()) over = o->second;
            }
        }
        if (!hit.empty()) {
            r = memFile(hit.data(), hit.size());
            char pf[100] = "";
            if (prof) sprintf_s(pf, " [prof tid %u frame %ld: 3.0 open %.2f, total %.2f ms]", GetCurrentThreadId(), g_frameNo, t30, qpcMs() - tIn);
            logf("served %s %u B from asset session -> %s%s", p, (unsigned)hit.size(), r ? "ok" : "FAILED", pf);
            return r;
        }
        double tR = prof ? qpcMs() : 0;
        size_t n = 0;
        std::string where;
        missWhy = over.empty() ? " (not in the 1.0 archives, " + std::to_string(g_zips.size()) + " archives open)" : " (overlay " + over + " unreadable)";
        std::string movie = g_cfg.uiScope10 ? ui::flashImageMovie(np) : "";
        bool scoped = false;
        if (!movie.empty()) {
            std::lock_guard<std::mutex> lock(g_sessLock);
            if ((scoped = !g_movies10.count(movie))) missWhy = " (image id of a movie not served from 1.0: not served)";
        }
        std::string chal = g_cfg.challenge10 ? gameplay::challengeTypePath(np) : "";   // gp: ChallengeData/<1.0 type file>
        void* data = scoped ? nullptr : read10(!chal.empty() ? chal.c_str() : over.empty() ? p : over.c_str(), &n, &where);
        // 3.0 registers a language table only with its controller variant <lang>_pc_con.dct beside it, which 1.0
        // never shipped: without one the whole playset table was dropped (in-game 2026-10-13, lang\IN). The WIN32
        // table stands in (same strings).
        if (!data && endsWith(p, "_pc_con.dct")) {
            std::string w = std::string(p, strlen(p) - 11) + "_WIN32.dct";
            data = read10(w.c_str(), &n, &where);
        }
        // 3.0's MissionPresentation shows a radio call's giver as <icon>_Holo (missionpresentation.lua GetGiverIcon,
        // isRadioTransmission); 3.0 ships that variant for 7 givers, 1.0 for none (1.0 showed the portrait itself):
        // the second Edna window asked for ui/charactericons/inc_edna_full_holo.mtb and fell back to the disc icon.
        // The 1.0 portrait stands in.
        if (!data && np.rfind("ui/charactericons/", 0) == 0 && endsWith(p, "_holo.mtb")) {
            std::string w = std::string(p, strlen(p) - 9) + ".mtb";
            data = read10(w.c_str(), &n, &where);
        }
        std::string annoZip, annoMember;   // a 1.0 dialog line's lipsync (audio.hpp annoSource, vo10)
        if (!data && g_cfg.audio10 && g_cfg.vo10 && audio::annoSource(p, annoZip, annoMember)) {
            std::lock_guard<std::mutex> lock(g_zipLock);
            mz_zip_archive* z = zipFor(annoZip);
            int i = z ? mz_zip_reader_locate_file(z, annoMember.c_str(), nullptr, 0) : -1;
            if (i >= 0 && (data = mz_zip_reader_extract_to_heap(z, (mz_uint)i, &n, 0)) != nullptr) where = annoZip + ":" + annoMember;
        }
        if (prof) tRead = qpcMs() - tR;
        if (data) {
            indexZip10(where.substr(0, where.find(':')));
            double tT = prof ? qpcMs() : 0;
            std::string out, note, mtc;
            const char* codec = nullptr;
            try {
                if (endsWith(p, ".tbody")) {             // D3D11 refuses sub-4x4 block textures (padSmallDxt)
                    out.assign((const char*)data, n);
                    padSmallDxt(out);
                    codec = out.size() == n && memcmp(out.data(), data, n) ? "tbody" : nullptr;
                } else codec = transcode(p, data, n, out, note, &mtc);
                // the .mtc request transcodes read10s(<same path>.mer): these bytes when nothing redirected the read
                if (codec && !strcmp(codec, "mer") && !mtc.empty() && over.empty() && chal.empty()) {
                    std::lock_guard<std::mutex> lock(g_sessLock);
                    g_served[np.substr(0, np.size() - 4) + ".mtc"] = mtc;
                }
            } catch (std::exception& e) {   // never unwind into engine code (that ends in a silent fail-fast)
                note += std::string("TRANSCODE EXCEPTION: ") + e.what() + " ";
                codec = nullptr;
            } catch (...) {
                note += "TRANSCODE EXCEPTION ";
                codec = nullptr;
            }
            if (!over.empty()) note += "(archive overlay) ";
            if (std::string mv = ui::flashMovie(np); !mv.empty()) { std::lock_guard<std::mutex> lock(g_sessLock); g_movies10.insert(mv); }
            if (np.rfind("presentation/", 0) == 0 && endsWith(p, ".lua")) {
                std::lock_guard<std::mutex> lock(g_sessLock);
                g_screens10.insert(np.substr(13, np.size() - 17));
            }
            if (prof) tTc = qpcMs() - tT;
            double tM = prof ? qpcMs() : 0;
            r = codec ? memFile(out.data(), out.size()) : memFile(data, n);
            char pf[600] = "";
            if (prof) {
                int len = sprintf_s(pf, " [prof tid %u frame %ld: 3.0 open %.2f, 1.0 read %.2f, transcode %.2f, memfile %.2f, total %.2f ms]",
                                    GetCurrentThreadId(), g_frameNo, t30, tRead, tTc, qpcMs() - tM, qpcMs() - tIn);
                static volatile LONG stacks;   // who asks: the engine call stack of the first served files of each kind
                static const char* const kinds[] = {".oct", ".bent", ".banm", ".mer", ".tbody"};
                static volatile LONG perKind[5];
                for (int k = 0; k < 5; ++k)
                    if (endsWith(p, kinds[k]) && InterlockedIncrement(&perKind[k]) <= 3 && InterlockedIncrement(&stacks) <= 15) {
                        len += sprintf_s(pf + len, sizeof(pf) - len, " stack:");
                        stackOf((const uint32_t*)_AddressOfReturnAddress(), pf, sizeof(pf), len, 16);
                    }
            }
            logf("served %s %u B from 1.0 %s codec=%s%s%s -> %s%s", p, (unsigned)(codec ? out.size() : n), where.c_str(),
                 codec ? codec : "raw", note.empty() ? "" : " ", note.c_str(), r ? "ok" : "FAILED", pf);
            mz_free(data);
            return r;
        }
    }
    // misses always (rare and cheap; what an unexplained fallback asks for), hits only when tracing
    char pf[120] = "";
    if (prof)
        sprintf_s(pf, " [prof tid %u frame %ld: 3.0 open %.2f, 1.0 lookup %.2f, total %.2f ms]", GetCurrentThreadId(), g_frameNo, t30, tRead,
                  qpcMs() - tIn);
    if (g_cfg.trace || !r) logf("open %s flags=%X -> %s%s%s", p, flags & 0xff, r ? "ok" : "MISSING", r ? "" : missWhy.c_str(), pf);
    return r;
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
    FILE* f = nullptr;
    if (!fopen_s(&f, path.c_str(), "rb") && f) {
        char buf[65536];
        size_t got;
        code.clear();
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0) code.append(buf, got);
        fclose(f);
        if (code.size() > 32 && !code.compare(0, 4, "DXBC")) return true;
    }
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
    if (g_cfg.renderdoc10 > 0) {   // before the renderer creates its device, so RenderDoc's hooks see it
        HMODULE rd = LoadLibraryExA("C:\\Program Files\\RenderDoc\\x86\\renderdoc.dll", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        auto get = rd ? (pRENDERDOC_GetAPI)GetProcAddress(rd, "RENDERDOC_GetAPI") : nullptr;
        if (get && get(eRENDERDOC_API_Version_1_1_2, (void**)&g_rdoc) == 1) {
            g_rdoc->SetCaptureFilePathTemplate((g_gameDir + "\\di3cap").c_str());
            logf("renderdoc: loaded; capture %d s after the first mission tick (missiontrace)", g_cfg.renderdoc10);
        } else logf("renderdoc: not loaded (%lu)", GetLastError());
    }
    _setmaxstdio(8192);   // every 1.0 archive used stays open (miniz holds a FILE*); the CRT default is 512
    // 1.0 loads assets\startup*.zip resident, so their members are seen by full path: startup3 holds
    // expendables_data/terrainbuildingsexpendables.lua (in-game 2026-10-13: MISSING), startup4 flash/hud/hud_collection.gfx
    // (the HudPellet screen's movie) and hud_playerinfo.gfx, startup5 var/*.bin. read10 never looked there.
    if (g_cfg.serve10 && g_cfg.resident10 && g_cfg.install10[0]) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((std::string(g_cfg.install10) + "\\assets\\startup*.zip").c_str(), &fd);
        for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileA(h, &fd)) indexZip10(fd.cFileName);
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        logf("resident10: %u full-path members of the 1.0 startup archives indexed", (unsigned)g_over10.size());
    }
    auto slurpFile = [&](const char* name) {
        std::string text;
        FILE* tf = nullptr;
        if (!fopen_s(&tf, (std::string(dir) + "\\" + name).c_str(), "rb") && tf) {
            char buf[65536];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), tf)) > 0) text.append(buf, got);
            fclose(tf);
        }
        return text;
    };
    g_renderOk = g_render.load(slurpFile("di1compat_render.txt"));
    logf("render table: %s (%u shaders, %u donors, %u materials, %u vertex plans)", g_renderOk ? "ok" : "MISSING",
         (unsigned)g_render.shaders.size(), (unsigned)g_render.donors.size(), (unsigned)g_render.matindex.size(),
         (unsigned)g_render.plans.size());
    octx::g_slack = 4096;   // instanced 1.0 SubNetworks must fit their arenas (octx.hpp)
    octx::g_variantGroups = g_cfg.variant10;
    g_merOk = g_mer.load(slurpFile("di1compat_mer.txt"));
    g_mer.events10 = g_cfg.events10;
    g_mer.portRefs = g_cfg.portRefs10;
    g_mer.ports10 = g_cfg.ports10;
    g_mer.pins10 = g_cfg.pins10;
    g_mer.gate10 = g_cfg.gate10;
    g_mer.toggleType10 = g_cfg.toggleType10;
    logf("meridian table: %s (%u registered classes, %u described)", g_merOk ? "ok" : "MISSING",
         (unsigned)g_mer.registered.size(), (unsigned)g_mer.classes.size());
    if (g_cfg.shaders10) {
        HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
        g_d3dCompile = dc ? (D3DCompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
        g_shaderCache = std::string(dir) + "\\di1compat_shaders";
        CreateDirectoryA(g_shaderCache.c_str(), nullptr);
        std::string v10;
        if (read10s("shaders/vformat.vtxf", v10)) g_vf10 = gen30::vformats(v10);
        logf("shaders10: compiler %s, %u 1.0 vertex formats, %u 3.0 globals, cache %s", g_d3dCompile ? "ok" : "MISSING",
             (unsigned)g_vf10.size(), (unsigned)g_render.shaders10.globals.size(), g_shaderCache.c_str());
    }

    AddVectoredExceptionHandler(1, avHandler);
    if (MH_Initialize() != MH_OK) {
        logf("MinHook init failed; no hooks");
        return;
    }
    // Code outside hkOpen calls its original, so a refused AssetOpen installs nothing: the game runs as plain 3.0.
    bool assetOpen = hook("AssetOpen", 0x0041CB10, "6AFF68????????", (void*)&hkOpen, (void**)&g_open);
    if (!assetOpen) {
        logf("feature AssetOpen off: no other hook installed, the game runs as plain 3.0");
        return;
    }
    hook("GameDB locks load", 0x01612FA0, "6AFF64A100000000", (void*)&hkLoadLocks, (void**)&g_loadLocks);
    hook("GameDB inventory load", 0x014C08D0, "6AFF64A100000000", (void*)&hkLoadInventory, (void**)&g_loadInventory);
    hook("GameDB CHD callback load", 0x018DB450, "6AFF64A100000000", (void*)&hkLoadChdCallback, (void**)&g_loadChdCallback);
    if (g_cfg.bits10 || g_cfg.perc10)
        hook("GameDB callback load", 0x018DB270, "6AFF64A10000000068????????50", (void*)&hkCbLoad, (void**)&g_cbLoad);
    if (g_cfg.econ10)
        hook("combat SpawnEconomyOnDeath get", 0x00FF69C0, "53568BF1F6860C09000020", (void*)&hkEconOnDeath, (void**)&g_econOnDeath);
    if (g_cfg.popin10 || g_cfg.deliv10)
        hook("expendables package load", 0x0160AA30, "558BEC83E4F86AFF68????????64A1", (void*)&hkExpPkg, (void**)&g_expPkg);
    if (g_cfg.currency10)
        installFeature("currency10", {
            {"zone currency (currency10)", 0x00E21850, "8B4C240868????????E8", (void*)&hkZoneCurrency,
             (void**)&g_zoneCurrency},
            {"UI_GetPlaysetCurrency (currency10)", 0x00B54B50, "6AFF68????????64A100000000", (void*)&hkPlaysetCurrency,
             (void**)&g_playsetCurrency},
            {"HUD currency (currency10)", 0x00FBB420, "6AFF68????????64A100000000", (void*)&hkHudCurrency,
             (void**)&g_hudCurrency},
        });
    if (g_cfg.popin10)
        hook("expendable definition ctor", 0x016022C0, "6AFF68????????64A100000000", (void*)&hkExpDef, (void**)&g_expDef);
    hook("VR playset character slot", 0x00B7F910, "6AFF68????????64A100000000", (void*)&hkPlaysetChar, (void**)&g_playsetChar);
    if (g_cfg.trace)
        installFeature("trace", {
            {"actor exists (trace)", 0x00751750, "518B4424088B00", (void*)&hkActorExists, (void**)&g_actorExists},
            {"figure check (trace)", 0x00DDAE30, "64A1000000006AFF68", (void*)&hkFigureOk, (void**)&g_figureOk},
            {"figure playset (trace)", 0x00435080, "6AFF68????????64A100000000", (void*)&hkFigurePlayset,
             (void**)&g_figurePlayset},
            {"reader list (trace)", 0x00427330, "83EC08535556", (void*)&hkReaderList, (void**)&g_readerList},
            {"reader item gate (trace)", 0x00DEB610, "538B5C2408", (void*)&hkReaderItemOk, (void**)&g_readerItemOk},
            {"playset figure slot (trace)", 0x00DF9FD0, "6AFF68????????64A100000000", (void*)&hkIgpName,
             (void**)&g_igpName},
            {"asset instance (trace)", 0x007734F0, "6AFF68????????64A100000000", (void*)&hkAssetInst,
             (void**)&g_assetInst},
            {"SubNetwork instance guard (trace)", 0x004FF0E0, "6AFF68????????64A100000000", (void*)&hkInst,
             (void**)&g_inst},
            {"header clone (trace)", 0x004F5900, "5153555657", (void*)&hkHdrClone, (void**)&g_hdrClone},
            {"start lookup (trace)", 0x013A85D0, "6AFF68????????64A100000000", (void*)&hkStartLookup,
             (void**)&g_startLookup},
            {"AsyncActorCreator execute (trace)", 0x0075A290, "6AFF68????????64A100000000", (void*)&hkAacExec,
             (void**)&g_aacExec},
            {"avatar spawn callback (trace)", 0x00A9B3E0, "8B0D????????83B988000000", (void*)&hkAvSpawned,
             (void**)&g_avSpawned},
            {"cho make (trace)", 0x014523B0, "83EC1C33C0", (void*)&hkChoMake, (void**)&g_choMake},
            {"AvatarSpawning create (trace)", 0x00A9F080, "6AFF68????????64A100000000", (void*)&hkAvCreate,
             (void**)&g_avCreate},
            {"cho has (trace)", 0x014835C0, "53568B74240C", (void*)&hkChoHas, (void**)&g_choHas},
            {"PlaceToy callback (trace)", 0x00A8B670, "8B4424048B4004", (void*)&hkPlaceToy, (void**)&g_placeToy},
            {"event post (trace)", 0x0068BBB0, "56578BF98B07", (void*)&hkPost, (void**)&g_post},
            {"level loader tick (trace)", 0x00D00530, "6AFF64A100000000", (void*)&hkLevelTick, (void**)&g_levelTick},
            {"level load-done check (trace)", 0x00CCC450, "568BF1807E2100", (void*)&hkLoadDone, (void**)&g_loadDone},
            {"AvatarSpawning finalize (trace)", 0x00AA36F0, "64A1000000006AFF68", (void*)&hkAvFinal,
             (void**)&g_avFinal},
            {"AvatarSpawning remove (trace)", 0x00A9EC70, "6AFF68????????64A100000000", (void*)&hkAvRemove,
             (void**)&g_avRemove},
            {"start query (trace)", 0x0145F720, "8B44240C83EC30", (void*)&hkStartQuery, (void**)&g_startQuery},
            {"MNode PlayerStartPosNode (trace)", 0x0148A790, "518B442410", (void*)&hkMkStart, (void**)&g_mkStart},
            {"MNode LocatorNode (trace)", 0x01480090, "518B442410", (void*)&hkMkLocator, (void**)&g_mkLocator},
            {"MNode SpawnExpendable (trace)", 0x017BF020, "518B442410", (void*)&hkMkSpawnExp, (void**)&g_mkSpawnExp},
            {"header make (trace)", 0x004FAE90, "6AFF68????????64A100000000", (void*)&hkHdrMake, (void**)&g_hdrMake},
        });
    if (g_cfg.shaders10) {
        InitializeCriticalSection(&g_recCs);
        std::vector<core::HookSpec> shaders = {
            {"shader by name", 0x0045C820, "8B44240450E8????????8B0D", (void*)&hkShaderByName,
             (void**)&g_shaderByName}};
        if (g_cfg.lazyLink10)
            shaders.push_back({"shader linker (deferred)", 0x00472050, "83EC50A1????????535657", (void*)&hkLinkAll,
                               (void**)&g_linkAll});
        shaders.push_back({"pass-state clone (locked)", 0x0045C5C0, "83EC4856578B7C2454", (void*)&hkCloneRec,
                           (void**)&g_cloneRec});
        installFeature("shaders10", shaders);
    }
    if (g_cfg.missionTrace)
        installFeature("missiontrace", {
            {"MissionContext update (trace)", 0x0166FFB0, "6AFF68????????64A100000000", (void*)&hkMissionTick,
             (void**)&g_missionTick},
            {"FocusPoint_Actor update (missiontrace)", 0x015DF390, "6AFF64A10000000068????????50", (void*)&hkFpUpdate,
             (void**)&g_fpUpdate},
            {"FocusPointActor Update pin (missiontrace)", 0x01730E80, "8B4424048B40048B486485C9",
             (void*)&hkFpNodeUpdate,
             (void**)&g_fpNodeUpdate},
            {"FocusPointActor tag apply (missiontrace)", 0x0173A3F0, "6AFF68????????64A100000000", (void*)&hkFpApply,
             (void**)&g_fpApply},
            {"FocusPoint_Actor tag scan (missiontrace)", 0x015D5F80, "6AFF68????????64A100000000", (void*)&hkFpScan,
             (void**)&g_fpScan},
            {"handle to actor (missiontrace)", 0x0074B200, "83EC088B44240C8D0C2451", (void*)&hkActorOf,
             (void**)&g_actorOf},
        });
    if (g_cfg.breakTrace || (g_cfg.audio10 && g_cfg.cineSync10))   // cinesync10: 1.0's audio-sync sound event (audio.hpp)
        hook("choreography start (cinesync10 / breaktrace)", 0x01449390, "83EC18568BF1E8", (void*)&hkChoStart, (void**)&g_choStart);
    if (g_cfg.breakTrace)
        installFeature("breaktrace", {
            {"ClipProcessor evaluate (breaktrace)", 0x00543F40, "B8E42E0000E8", (void*)&hkClipEval,
             (void**)&g_clipEval},
            {"exploder munition impact (breaktrace)", 0x0151E420, "6AFF68????????64A100000000", (void*)&hkMunHit,
             (void**)&g_munHit},
            {"exploder explode (breaktrace)", 0x0151E130, "6AFF64A100000000", (void*)&hkExplode, (void**)&g_explode},
            {"choreography audio-sync wait (breaktrace)", 0x01442EE0, "568BF185F6744D8B06", (void*)&hkChoReady,
             (void**)&g_choReady},
            {"choreography play (breaktrace)", 0x01445E70, "5183790C00753B8B01", (void*)&hkBtPlay, (void**)&g_btPlay},
            {"layout attachments apply (breaktrace)", 0x00788280, "6AFF68????????64A100000000", (void*)&hkLayApply,
             (void**)&g_layApply},
            {"layout attachments gate (breaktrace)", 0x00785B40, "8B4C240885C97407", (void*)&hkLayGate,
             (void**)&g_layGate},
            {"layout attachment bind (breaktrace)", 0x00787830, "6AFF68????????64A100000000", (void*)&hkLayBind,
             (void**)&g_layBind},
            {"clip setup (breaktrace)", 0x00552FC0, "8B44240456578BF1", (void*)&hkClipSet, (void**)&g_clipSet},
            {"music cue push (breaktrace)", 0x0073C5C0, "81EC1C010000538BD9807B1000", (void*)&hkMusicPush,
             (void**)&g_musicPush},
        });
    if (g_cfg.teardownTrace)
        installFeature("teardowntrace", {
            {"Zoo::Layout dtor (teardowntrace)", 0x00787BC0, "6AFF68????????64A100000000", (void*)&hkLayoutDtor,
             (void**)&g_layoutDtor},
            {"Samurai::Node dtor (teardowntrace)", 0x004EE5F0, "6AFF68????????64A100000000", (void*)&hkNodeDtor,
             (void**)&g_nodeDtor},
            {"hierarchy entry release (teardowntrace)", 0x004B2780, "8B442404568D7110", (void*)&hkEntryRel,
             (void**)&g_entryRel},
            {"hierarchy entry free (teardowntrace)", 0x004E1A40, "83EC0853578B7C2414", (void*)&hkEntryFree,
             (void**)&g_entryFree},
            {"Layout placement bind (teardowntrace)", 0x0077D9C0, "6AFF68????????64A100000000", (void*)&hkPlBind,
             (void**)&g_plBind},
            {"block tracker delete (teardowntrace)", 0x00416840, "535556578BF9", (void*)&hkTrackFree,
             (void**)&g_trackFree},
            {"heap free (teardowntrace)", 0x00409E10, "8B4424046A0350E8", (void*)&hkHeapFree, (void**)&g_heapFree},
        });
    if ((g_cfg.aliases10 || g_cfg.native10) && g_merOk)
        hook("Meridian class registry", 0x0178E330, "6A006A0068????????68????????", (void*)&hkMerRegistry, (void**)&g_merRegistry);
    if (g_cfg.lockTrace10)
        installFeature("locktrace10", {
            {"GamePlayers lock controls (locktrace10)", 0x0139B8D0, "833D????????00558BE9", (void*)&hkCtlLock,
             (void**)&g_ctlLock},
            {"GamePlayers unlock controls (locktrace10)", 0x0139B960, "55578B7C240C8BE985FF", (void*)&hkCtlUnlock,
             (void**)&g_ctlUnlock},
            {"controller context apply (locktrace10)", 0x0121D5C0, "8B44240483EC0C53568BD9", (void*)&hkCtxOn,
             (void**)&g_ctxOn},
            {"controller context remove (locktrace10)", 0x0121D8C0, "8B44240483EC0C56578BF9", (void*)&hkCtxOff,
             (void**)&g_ctxOff},
            {"choreography node play (locktrace10)", 0x017E91B0, "83EC1C5556578BF1E8", (void*)&hkChoPlay,
             (void**)&g_choPlay},
            {"choreography node event (locktrace10)", 0x017E5890, "538BD9568D73D057", (void*)&hkChoEvent,
             (void**)&g_choEvent},
            {"message sender send (locktrace10)", 0x018120B0, "6AFF68????????64A100000000", (void*)&hkMsgSend,
             (void**)&g_msgSend},
            {"message handler delivery (locktrace10)", 0x018049F0, "83EC08568BF18B4E20", (void*)&hkMsgRecv,
             (void**)&g_msgRecv},
            {"mission monitor fire (locktrace10)", 0x01730490, "5156578BF18B068B504833FF57", (void*)&hkMonPin,
             (void**)&g_monPin},
            {"load screen active update (locktrace10)", 0x0145B9A0, "5356578BF98D77ECBB01000000", (void*)&hkLoadTick,
             (void**)&g_loadTick},
            {"master net sync Link_In (locktrace10)", 0x006EC4F0,
             "6AFF68????????64A100000000506489250000000083EC30535556578BF1",
             (void*)&hkNetSync, (void**)&g_netSync},
            {"colvis toggle parameters (locktrace10)", 0x01808FE0,
             "6AFF68????????64A1000000005064892500000000518B442414535657508BF1",
             (void*)&hkColVisLoad, (void**)&g_colVisLoad},
            {"colvis toggle apply (locktrace10)", 0x01803550, "558B6C2408565733FF8BF1", (void*)&hkColVisApply,
             (void**)&g_colVisApply},
            {"beacon map lookup (locktrace10)", 0x01591570, "83EC088B81740100005355", (void*)&hkBeaconLookup,
             (void**)&g_beaconLookup},
            {"compass mission track (locktrace10)", 0x015BA4D0, "83EC0C538B5C2414568BF1", (void*)&hkTrack,
             (void**)&g_track},
            {"compass draw (locktrace10)", 0x00C13C50, "558BEC83E4F081ECB4000000", (void*)&hkNavUpdate,
             (void**)&g_navUpdate},
            {"mission progress entries (locktrace10)", 0x0157CD10, "558BEC83E4F86AFF68????????64A100000000",
             (void*)&hkMisProgress,
             (void**)&g_misProgress},
        });
    if (g_cfg.opacityLog10)
        hook("opacity mask (log)", 0x0044AD80, "83EC088B442418F30F105424", (void*)&hkOpacityMask, (void**)&g_opacityMask);
    if (g_cfg.fadeProbe10)
        hook("visibility test (fadeprobe10)", 0x0044B8F0, "8B5424088A442414F30F10620C", (void*)&hkVisTest, (void**)&g_visTest);
    if (g_cfg.fpsLog10 || g_cfg.loadProf10) hook("frame sync (fps log)", 0x0047AAB0, "83EC105657B9????????E8", (void*)&hkFrameSync, (void**)&g_frameSync);
    bool profiler = g_cfg.loadProf10 && installFeature("loadprof10", {
        {"fiber switch (loadprof10)", 0x0041E480, "8B44240C85C0740C", (void*)&hkFiberSwitch, (void**)&g_fiberSwitch},
        {"file-done wait (loadprof10)", 0x0041FC10, "56578B3D????????8BF1", (void*)&hkFileWait, (void**)&g_fileWait},
        {".oct asset load (loadprof10)", 0x00786D00, "6AFF68????????64A100000000", (void*)&hkOctLoad,
         (void**)&g_octLoad},
    });
    if (profiler) {
        g_profPath = g_gameDir + "\\di1compat_prof.tsv";
        if (HANDLE h = CreateThread(nullptr, 0, profThread, nullptr, 0, nullptr)) CloseHandle(h);
    }
    hook("asset defs lookup", 0x00703340, "8B44240456", (void*)&hkAssetDef, (void**)&g_assetDef);
    hook("CRT __invoke_watson", 0x012CA1D1, "8BFF566A01", (void*)&hkWatson, (void**)&g_watson);
    hook("resident path normaliser", 0x0041FEF0, "8B44240C56", (void*)&hkNormPath, (void**)&g_normPath);
    sx::g_log = logf;   // also names the key ids BMX-1/BTX-1 give unpaired 1.0 key names
    if (g_cfg.sx10 && g_cfg.sxNatives10) {
        sx::g_exe = g_base;
        sx::init(g_cfg.sxPorts10);
        std::vector<core::HookSpec> natives = {
            {"Slang linker (1.0 natives)", 0x00736DA0, "51538B5C240C568BF18BCBE8", (void*)&sx::hkLink,
             (void**)&sx::g_link}};
        if (g_cfg.sxPorts10 && g_cfg.sxBeh10)
            natives.push_back({"Slang call by name (1.0 behavior type)", 0x00739090, "56578B7C240C8BF18B4E04",
                               (void*)&sx::hkCallByName,
                               (void**)&sx::g_callByName});
        installFeature("sxnatives10", natives);
    }
    npcrt::install();
    logf("ui: loadscreen10=%d uiscope10=%d fmv10=%d text10=%d tips10=%d tutorial10=%d context10=%d", g_cfg.loadScreen10,
         g_cfg.uiScope10, g_cfg.fmv10, g_cfg.text10, g_cfg.tips10, g_cfg.tutorial10, g_cfg.context10);
    if (g_cfg.serve10 && g_cfg.loadScreen10)
        hook("zone string field (ui: 1.0 loading screen)", 0x0139E0B0, "83EC08568BF180BEE403000000", (void*)&hkZoneStr, (void**)&g_zoneStr);
    if (g_cfg.serve10 && g_cfg.fmv10)
        hook("FMV path (ui: 1.0 clips)", 0x0067E460, "81EC0401000053568BB42418010000", (void*)&hkFmvPath, (void**)&g_fmvPath);
    if (g_cfg.serve10 && g_cfg.tutorial10) {
        // the redirect keys on the case-0x3d push (68 <imm32> at 0x00DE4C5A, relocated) of the literal "SelectMenu"
        const uint8_t* push = (const uint8_t*)rt(0x00DE4C5A);
        if (push[0] == 0x68 && *(const uint32_t*)(push + 1) == (uint32_t)rt(0x01D19CDC) && !strcmp((const char*)rt(0x01D19CDC), "SelectMenu"))
            hook("screen by name (ui: tutorial element 61)", 0x0154FF90, "837C2404007505", (void*)&hkFindScreen, (void**)&g_findScreen);
        else logf("tutorial10: the element-61 push of \"SelectMenu\" is not where it was analysed: not hooked");
    }
    if (g_cfg.serve10 && g_cfg.context10)
        hook("controller context list (ui: 1.0 tutorial contexts)", 0x01224880, "5153558BE98A85A4000000", (void*)&hkCtxParse, (void**)&g_ctxParse);
    if (g_cfg.audio10 && g_cfg.install10[0]) {
        audio::logfn = logf;
        audio::cfg.music = g_cfg.music10;
        audio::cfg.vo = g_cfg.vo10;
        audio::cfg.trace = g_cfg.audioTrace10;
        audio::cfg.mixer = g_cfg.mixer10;
        if (!audio::install(installFeature, rt, g_cfg.install10, g_gameDir)) logf("audio10: off (FMOD host failed)");
    }
    if (g_cfg.vfx10 && g_cfg.vfxUniversal10) {
        std::unique_lock<std::mutex> lock(g_sessLock);   // AssetOpen is live already
        for (const char* e : {".particledata", ".mtb"})   // the alias vfxrt::hkPkg loads -> 1.0 particles/universal.zip
            g_over10.emplace(std::string("particles/universal10") + e, std::string("particles/universal/universal") + e);
        lock.unlock();
        hook("particle package load (vfx)", 0x00672620, "6AFF68????????64A100000000", (void*)&vfxrt::hkPkg, (void**)&vfxrt::g_pkg);
    }
    if (g_cfg.vfxTrace)
        installFeature("vfx_trace", {
            {"effect create (vfx trace)", 0x0065E3A0, "8B54240C8B4A18", (void*)&vfxrt::hkEffect,
             (void**)&vfxrt::g_effect},
            {"emitter create (vfx trace)", 0x0066A1C0, "6AFF68????????64A100000000", (void*)&vfxrt::hkEmitter,
             (void**)&vfxrt::g_emitter},
            {"slaved emitter create (vfx trace)", 0x0066E7F0, "6AFF68????????64A100000000", (void*)&vfxrt::hkAttached,
             (void**)&vfxrt::g_attached},
        });
    gprt::install();
    bldrt::install();
    caprt::install();
    if (g_cfg.spawnTrace10 && !g_cfg.trace)   // where the player is put (Pirates fall): trace's start hooks alone
        installFeature("spawntrace10", {
            {"start lookup (spawntrace10)", 0x013A85D0, "6AFF68????????64A100000000", (void*)&hkStartLookup,
             (void**)&g_startLookup},
            {"start query (spawntrace10)", 0x0145F720, "8B44240C83EC30", (void*)&hkStartQuery, (void**)&g_startQuery},
        });
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
